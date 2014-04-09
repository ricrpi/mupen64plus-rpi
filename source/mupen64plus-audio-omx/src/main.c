/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus-omx-audio - main.c                                        *
 *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
 *   Copyright (C) 2007-2009 Richard Goedeken                              *
 *   Copyright (C) 2007-2008 Ebenblues                                     *
 *   Copyright (C) 2003 JttL                                               *
 *   Copyright (C) 2002 Hacktarux                                          *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.          *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <semaphore.h>

#include "bcm_host.h"
#include "interface/vmcs_host/vc_tvservice.h"
#include "IL/OMX_Broadcom.h"

#include "interface/vmcs_host/vcilcs.h"
#include "interface/vmcs_host/vchost.h"
#include "interface/vmcs_host/vcilcs_common.h"

#define M64P_PLUGIN_PROTOTYPES 1
#include "m64p_types.h"
#include "m64p_plugin.h"
#include "m64p_common.h"
#include "m64p_config.h"

#include "main.h"
#include "osal_dynamiclib.h"

extern uint32_t SDL_GetTicks(void);

#define DEFAULT_BUFFER_SIZE 2048

/* This sets default frequency what is used if rom doesn't want to change it.
   Probably only game that needs this is Zelda: Ocarina Of Time Master Quest
 *NOTICE* We should try to find out why Demos' frequencies are always wrong
   They tend to rely on a default frequency, apparently, never the same one ;)*/
#define DEFAULT_FREQUENCY 33600

/* This is the mode for selecting the output frequency for the Audio plugin
	MODE 0 Force use ROM frequency (or DEFAULT if not set)
	MODE 1 Auto. HDMI is queried to check game frequency is supported.
	MODE 2 is to use 11025, 22050, 44100 if <= ROM frequency
	MODE 3 is to use 11025, 22050, 44100 if > ROM frequency
	MODE N is the frequency to use for sound output
 */
#define DEFAULT_MODE 0

/* Default audio output on HDMI (1) or analogue port (0) */
#define OUTPUT_PORT 1

/* Latency in ms that the audio buffers may have*/
#define DEFAULT_LATENCY 300

/* Number of buffers used by Audio*/
#define DEFAULT_NUM_BUFFERS 3

#define MIN_LATENCY_TIME 10

//#define DEBUG_PRINT(...) printf(__VA_ARGS__)

#ifndef DEBUG_PRINT
#define DEBUG_PRINT(...)
#endif

#define PORT_INDEX 100

//#define MONITOR_BUFFER_READY

//---------------------------------------------------------------

static const char *audio_dest[] = {"local", "hdmi"};

/* local variables */
static void (*l_DebugCallback)(void *, int, const char *) = NULL;
static void *l_DebugCallContext = NULL;
static int l_PluginInit = 0;

//static OMX_PARAM_PORTDEFINITIONTYPE portdef;
static OMX_HANDLETYPE OMX_Handle;
static m64p_handle l_ConfigAudio;

/* Read header for type definition */
static AUDIO_INFO AudioInfo;

/* Audio frequency, this is usually obtained from the game, but for compatibility we set default value */
static int GameFreq = DEFAULT_FREQUENCY;

/* SpeedFactor is used to increase/decrease game playback speed */
static uint32_t speed_factor = 100;

// If this is true then left and right channels are swapped */
static uint32_t bSwapChannels = 0;

// This is the frequency mode or Target Output Frequency
static uint32_t uiOutputFrequencyMode = DEFAULT_MODE;

// Size of Secondary audio buffer in output samples
static uint32_t uiSecondaryBufferSamples = DEFAULT_BUFFER_SIZE;

static uint32_t uiOutputPort = OUTPUT_PORT;

static uint32_t OutputFreq = 44100;

static uint32_t uiLatency = DEFAULT_LATENCY;

static uint32_t uiNumBuffers = DEFAULT_NUM_BUFFERS;

static uint32_t	bNative = 1;

static uint32_t uiUnderrunMode = 1;

static uint32_t critical_failure = 0;

//---------------------------------------------------------------
// OMX buffers and pointers to speed up copying of audio data
static OMX_BUFFERHEADERTYPE**	audioBuffers = NULL;
static uint32_t 	uiBufferIndex = 0;

static uint32_t*	pNextAudioSample;
static uint32_t 	uiCurrentBufferLength = 0;

static pthread_mutex_t 	audioLock;
static pthread_cond_t omxStateCond;
static OMX_STATETYPE omxState = 0;

#ifdef MONITOR_BUFFER_READY
static uint32_t buffersReady = 0;
#endif

//---------------------------------------------------------------

// Prototype of local functions
static void InitializeAudio(int freq);
static void ReadConfig(void);
static uint32_t SendBufferToAudio();
static uint32_t audioplay_get_latency();

//---------------------------------------------------------------

/* definitions of pointers to Core config functions */
ptr_ConfigOpenSection      ConfigOpenSection 	= NULL;
ptr_ConfigDeleteSection    ConfigDeleteSection 	= NULL;
ptr_ConfigSaveSection      ConfigSaveSection 	= NULL;
ptr_ConfigSetParameter     ConfigSetParameter 	= NULL;
ptr_ConfigGetParameter     ConfigGetParameter 	= NULL;
ptr_ConfigGetParameterHelp ConfigGetParameterHelp = NULL;
ptr_ConfigSetDefaultInt    ConfigSetDefaultInt 	= NULL;
ptr_ConfigSetDefaultFloat  ConfigSetDefaultFloat = NULL;
ptr_ConfigSetDefaultBool   ConfigSetDefaultBool = NULL;
ptr_ConfigSetDefaultString ConfigSetDefaultString = NULL;
ptr_ConfigGetParamInt      ConfigGetParamInt 	= NULL;
ptr_ConfigGetParamFloat    ConfigGetParamFloat 	= NULL;
ptr_ConfigGetParamBool     ConfigGetParamBool 	= NULL;
ptr_ConfigGetParamString   ConfigGetParamString = NULL;

//---------------------------------------------------------------

/* Global functions */
static void DebugMessage(int level, const char *message, ...)
{
	char msgbuf[1024];
	va_list args;

	if (l_DebugCallback == NULL)
		return;

	va_start(args, message);
	vsprintf(msgbuf, message, args);

	(*l_DebugCallback)(l_DebugCallContext, level, msgbuf);

	va_end(args);
}

/* Mupen64Plus plugin functions */
EXPORT m64p_error CALL PluginStartup(m64p_dynlib_handle CoreLibHandle, void *Context,
		void (*DebugCallback)(void *, int, const char *))
{
	ptr_CoreGetAPIVersions CoreAPIVersionFunc;

	int ConfigAPIVersion, DebugAPIVersion, VidextAPIVersion, bSaveConfig;
	float fConfigParamsVersion = 0.0f;

	if (l_PluginInit)
		return M64ERR_ALREADY_INIT;

	/* first thing is to set the callback function for debug info */
	l_DebugCallback = DebugCallback;
	l_DebugCallContext = Context;

	/* attach and call the CoreGetAPIVersions function, check Config API version for compatibility */
	CoreAPIVersionFunc = (ptr_CoreGetAPIVersions) osal_dynlib_getproc(CoreLibHandle, "CoreGetAPIVersions");
	if (CoreAPIVersionFunc == NULL)
	{
		DebugMessage(M64MSG_ERROR, "Core emulator broken; no CoreAPIVersionFunc() function found.");
		return M64ERR_INCOMPATIBLE;
	}

	(*CoreAPIVersionFunc)(&ConfigAPIVersion, &DebugAPIVersion, &VidextAPIVersion, NULL);
	if ((ConfigAPIVersion & 0xffff0000) != (CONFIG_API_VERSION & 0xffff0000))
	{
		DebugMessage(M64MSG_ERROR, "Emulator core Config API (v%i.%i.%i) incompatible with plugin (v%i.%i.%i)",
				VERSION_PRINTF_SPLIT(ConfigAPIVersion), VERSION_PRINTF_SPLIT(CONFIG_API_VERSION));
		return M64ERR_INCOMPATIBLE;
	}

	/* Get the core config function pointers from the library handle */
	ConfigOpenSection = 	(ptr_ConfigOpenSection) 	osal_dynlib_getproc(CoreLibHandle, "ConfigOpenSection");
	ConfigDeleteSection = 	(ptr_ConfigDeleteSection) 	osal_dynlib_getproc(CoreLibHandle, "ConfigDeleteSection");
	ConfigSaveSection = 	(ptr_ConfigSaveSection) 	osal_dynlib_getproc(CoreLibHandle, "ConfigSaveSection");
	ConfigSetParameter = 	(ptr_ConfigSetParameter) 	osal_dynlib_getproc(CoreLibHandle, "ConfigSetParameter");
	ConfigGetParameter = 	(ptr_ConfigGetParameter) 	osal_dynlib_getproc(CoreLibHandle, "ConfigGetParameter");
	ConfigSetDefaultInt = 	(ptr_ConfigSetDefaultInt) 	osal_dynlib_getproc(CoreLibHandle, "ConfigSetDefaultInt");
	ConfigSetDefaultFloat = (ptr_ConfigSetDefaultFloat) osal_dynlib_getproc(CoreLibHandle, "ConfigSetDefaultFloat");
	ConfigSetDefaultBool = 	(ptr_ConfigSetDefaultBool) 	osal_dynlib_getproc(CoreLibHandle, "ConfigSetDefaultBool");
	ConfigSetDefaultString = (ptr_ConfigSetDefaultString) osal_dynlib_getproc(CoreLibHandle, "ConfigSetDefaultString");
	ConfigGetParamInt = 	(ptr_ConfigGetParamInt) 	osal_dynlib_getproc(CoreLibHandle, "ConfigGetParamInt");
	ConfigGetParamFloat = 	(ptr_ConfigGetParamFloat) 	osal_dynlib_getproc(CoreLibHandle, "ConfigGetParamFloat");
	ConfigGetParamBool = 	(ptr_ConfigGetParamBool) 	osal_dynlib_getproc(CoreLibHandle, "ConfigGetParamBool");
	ConfigGetParamString = 	(ptr_ConfigGetParamString) 	osal_dynlib_getproc(CoreLibHandle, "ConfigGetParamString");

	if (!ConfigOpenSection || !ConfigDeleteSection || !ConfigSetParameter || !ConfigGetParameter ||
			!ConfigSetDefaultInt || !ConfigSetDefaultFloat || !ConfigSetDefaultBool || !ConfigSetDefaultString ||
			!ConfigGetParamInt   || !ConfigGetParamFloat   || !ConfigGetParamBool   || !ConfigGetParamString)
		return M64ERR_INCOMPATIBLE;

	/* ConfigSaveSection was added in Config API v2.1.0 */
	if (ConfigAPIVersion >= 0x020100 && !ConfigSaveSection)
		return M64ERR_INCOMPATIBLE;

	/* get a configuration section handle */
	if (ConfigOpenSection("Audio-OMX", &l_ConfigAudio) != M64ERR_SUCCESS)
	{
		DebugMessage(M64MSG_ERROR, "Couldn't open config section 'Audio-OMX'");
		return M64ERR_INPUT_NOT_FOUND;
	}

	/* check the section version number */
	bSaveConfig = 0;
	if (ConfigGetParameter(l_ConfigAudio, "Version", M64TYPE_FLOAT, &fConfigParamsVersion, sizeof(float)) != M64ERR_SUCCESS)
	{
		DebugMessage(M64MSG_WARNING, "No version number in 'Audio-OMX' config section. Setting defaults.");
		ConfigDeleteSection("Audio-OMX");
		ConfigOpenSection("Audio-OMX", &l_ConfigAudio);
		bSaveConfig = 1;
	}
	else if (((int) fConfigParamsVersion) != ((int) CONFIG_PARAM_VERSION))
	{
		DebugMessage(M64MSG_WARNING, "Incompatible version %.2f in 'Audio-OMX' config section: current is %.2f. Setting defaults.", fConfigParamsVersion, (float) CONFIG_PARAM_VERSION);
		ConfigDeleteSection("Audio-OMX");
		ConfigOpenSection("Audio-OMX", &l_ConfigAudio);
		bSaveConfig = 1;
	}
	else if ((CONFIG_PARAM_VERSION - fConfigParamsVersion) >= 0.0001f)
	{
		/* handle upgrades */
		float fVersion = CONFIG_PARAM_VERSION;
		ConfigSetParameter(l_ConfigAudio, "Version", M64TYPE_FLOAT, &fVersion);
		DebugMessage(M64MSG_INFO, "Updating parameter set version in 'Audio-OMX' config section to %.2f", fVersion);
		bSaveConfig = 1;
	}

	/* set the default values for this plugin */
	ConfigSetDefaultFloat(l_ConfigAudio,"Version",             	CONFIG_PARAM_VERSION,  		"Mupen64Plus OMX Audio Plugin config parameter version number");
	ConfigSetDefaultInt(l_ConfigAudio, 	"DEFAULT_FREQUENCY",    DEFAULT_FREQUENCY,     		"Frequency which is used if rom doesn't want to change it");
	ConfigSetDefaultBool(l_ConfigAudio, "SWAP_CHANNELS",        0,                     		"Swaps left and right channels");
	ConfigSetDefaultInt(l_ConfigAudio, 	"OUTPUT_PORT",   		OUTPUT_PORT,   				"Audio output to go to (0) Analogue jack, (1) HDMI");
	
#ifdef EXT_CFG
ConfigSetDefaultBool(l_ConfigAudio, "NATIVE_MODE",       	bNative,                    "Point OMX to the raw N64 audio data region instead of copying audio into buffer. Overrides SECONDARY_BUFFER_SIZE, DEFAULT_MODE and LATENCY");
	ConfigSetDefaultInt(l_ConfigAudio, 	"BUFFER_SIZE",DEFAULT_BUFFER_SIZE, 		"Number of output samples per Audio callback. This is for hardware buffers.");
#endif	
	ConfigSetDefaultInt(l_ConfigAudio, 	"DEFAULT_MODE",     	DEFAULT_MODE,          		"Audio Output Frequncy mode: 0 = Rom Frequency, 1 ROM Frequency if supported (HDMI only), 2 = Standard frequency < Rom Frequency, 3 = Standard frequency > Rom Frequency, [N] Force output frequency");
#ifdef EXT_CFG	
	ConfigSetDefaultInt(l_ConfigAudio, 	"LATENCY",      		DEFAULT_LATENCY,           	"Desired Latency in ms");
	ConfigSetDefaultInt(l_ConfigAudio,	"UNDERRUN_MODE",		uiUnderrunMode,				"Underrun Mode, 0 = Ignore, 1 = Report, 2 = repeat audio when latency < LATENCY/2");
#endif
	if (bSaveConfig && ConfigAPIVersion >= 0x020100)
		ConfigSaveSection("Audio-OMX");

	l_PluginInit = 1;
	return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginShutdown(void)
{
	if (!l_PluginInit)
		return M64ERR_NOT_INIT;

	/* reset some local variables */
	l_DebugCallback 	= NULL;
	l_DebugCallContext 	= NULL;
	l_PluginInit 		= 0;

	return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginGetVersion(m64p_plugin_type *PluginType, int *PluginVersion, int *APIVersion, const char **PluginNamePtr, int *Capabilities)
{
	/* set version info */
	if (PluginType != NULL)
		*PluginType = M64PLUGIN_AUDIO;

	if (PluginVersion != NULL)
		*PluginVersion = SDL_AUDIO_PLUGIN_VERSION;

	if (APIVersion != NULL)
		*APIVersion = AUDIO_PLUGIN_API_VERSION;

	if (PluginNamePtr != NULL)
		*PluginNamePtr = "Mupen64Plus OMX Audio Plugin";

	if (Capabilities != NULL)
	{
		*Capabilities = 0;
	}

	return M64ERR_SUCCESS;
}

/* ----------- Audio Functions ------------- */
EXPORT void CALL AiDacrateChanged( int SystemType )
{
	int f = GameFreq;

	if (!l_PluginInit) return;

	switch (SystemType)
	{
	case SYSTEM_NTSC:
		f = 48681812 / (*AudioInfo.AI_DACRATE_REG + 1);
		break;
	case SYSTEM_PAL:
		f = 49656530 / (*AudioInfo.AI_DACRATE_REG + 1);
		break;
	case SYSTEM_MPAL:
		f = 48628316 / (*AudioInfo.AI_DACRATE_REG + 1);
		break;
	}
	InitializeAudio(f);
}

//============================================================================

OMX_ERRORTYPE EventHandler(
        OMX_IN OMX_HANDLETYPE hComponent,
        OMX_IN OMX_PTR pAppData,
        OMX_IN OMX_EVENTTYPE eEvent,
        OMX_IN OMX_U32 nData1,
        OMX_IN OMX_U32 nData2,
        OMX_IN OMX_PTR pEventData)
{
	pthread_mutex_lock(&audioLock);

	if (eEvent == OMX_EventCmdComplete && nData1 == OMX_CommandStateSet)
	{
		omxState = nData2;
		pthread_cond_signal(&omxStateCond);
	}

	pthread_mutex_unlock(&audioLock);
	return OMX_ErrorNone;
}

OMX_ERRORTYPE EmptyBufferDone(
        OMX_IN OMX_HANDLETYPE hComponent,
        OMX_IN OMX_PTR pAppData,
        OMX_IN OMX_BUFFERHEADERTYPE* pBuffer)
{
	#ifdef MONITOR_BUFFER_READY
	pthread_mutex_lock(&audioLock);
	buffersReady--;
	pthread_mutex_unlock(&audioLock);
	#endif
	return OMX_ErrorNone;
}

uint32_t audio_wait_for_state(OMX_U32 state)
{
	pthread_mutex_lock(&audioLock);
	while (omxState != state)
	{
		pthread_cond_wait(&omxStateCond, &audioLock);
	}
	pthread_mutex_unlock(&audioLock);

	return 0;
}

static int32_t audioplay_create(uint32_t num_buffers,
		uint32_t buffer_size)
{
	uint32_t bytes_per_sample = 4;
	OMX_ERRORTYPE error;

	// basic sanity check on arguments
	if(!num_buffers ||	buffer_size < bytes_per_sample)	return -1;

	OMX_PARAM_PORTDEFINITIONTYPE param;
	OMX_AUDIO_PARAM_PCMMODETYPE pcm;
	OMX_STATETYPE state;
	int i;

	OMX_CALLBACKTYPE callbacks;
	callbacks.EventHandler = EventHandler;
	callbacks.EmptyBufferDone = EmptyBufferDone;

	error = OMX_GetHandle(&OMX_Handle, "OMX.broadcom.audio_render", NULL, &callbacks);
	if(error != OMX_ErrorNone){
		DebugMessage(M64MSG_ERROR, "%d OMX_GetHandle() failed. Error 0x%X", __LINE__, error);
		critical_failure = 1;
	}

	// set up the number/size of buffers
	memset(&param, 0, sizeof(OMX_PARAM_PORTDEFINITIONTYPE));
	param.nSize = sizeof(OMX_PARAM_PORTDEFINITIONTYPE);
	param.nVersion.nVersion = OMX_VERSION;
	param.nPortIndex = PORT_INDEX;
	param.nBufferSize = buffer_size;
	param.nBufferCountActual = num_buffers;
	param.format.audio.eEncoding = OMX_AUDIO_CodingPCM;

	error = OMX_SetParameter(OMX_Handle, OMX_IndexParamPortDefinition, &param);
	if(error != OMX_ErrorNone) DebugMessage(M64MSG_ERROR, "line %d: Failed to set OMX_IndexParamPortDefinition",__LINE__);

	// set the pcm parameters
	memset(&pcm, 0, sizeof(OMX_AUDIO_PARAM_PCMMODETYPE));
	pcm.nSize = sizeof(OMX_AUDIO_PARAM_PCMMODETYPE);
	pcm.nVersion.nVersion = OMX_VERSION;

	pcm.nPortIndex = PORT_INDEX;
	pcm.nChannels = 2;
	pcm.eNumData = OMX_NumericalDataSigned;
	pcm.eEndian = OMX_EndianLittle;
	pcm.nSamplingRate = OutputFreq;
	pcm.bInterleaved = OMX_TRUE;
	pcm.nBitPerSample = 16;
	pcm.ePCMMode = OMX_AUDIO_PCMModeLinear;

	if(bSwapChannels == 0)
	{
		pcm.eChannelMapping[1] = OMX_AUDIO_ChannelRF;
		pcm.eChannelMapping[0] = OMX_AUDIO_ChannelLF;
	}
	else
	{
		pcm.eChannelMapping[1] = OMX_AUDIO_ChannelLF;
		pcm.eChannelMapping[0] = OMX_AUDIO_ChannelRF;
	}

	error = OMX_SetParameter(OMX_Handle, OMX_IndexParamAudioPcm, &pcm);
	if(error != OMX_ErrorNone) DebugMessage(M64MSG_ERROR, "line %d: Failed to Set OMX Parameters. Error 0x%X",__LINE__, error);

	error = OMX_SendCommand(OMX_Handle, OMX_CommandPortDisable, PORT_INDEX, NULL);

	int nPorts;
    int startPortNumber;
    int n;
	OMX_PORT_PARAM_TYPE param2;

	memset(&param2, 0, sizeof(OMX_PORT_PARAM_TYPE));
	param2.nSize = sizeof(OMX_PORT_PARAM_TYPE);
	param2.nVersion.nVersion = OMX_VERSION;

    error = OMX_GetParameter(OMX_Handle, OMX_IndexParamOtherInit, &param2);
    if(error != OMX_ErrorNone)
{
		DebugMessage(M64MSG_ERROR, "line %d: Failed to Get OMX Parameters. Error 0x%X",__LINE__, error);
    }
	else
	{
		startPortNumber = ((OMX_PORT_PARAM_TYPE)param2).nStartPortNumber;
		nPorts = ((OMX_PORT_PARAM_TYPE)param2).nPorts;
		if (nPorts > 0)
		{
			for (n = 0; n < nPorts; n++) {
				error = OMX_SendCommand(OMX_Handle, OMX_CommandPortDisable, n + startPortNumber, NULL);
				if (error != OMX_ErrorNone) {
					DebugMessage(M64MSG_ERROR, "line %d: Could not disable port. Error 0x%X",__LINE__, error);
				}
			}
		}
	}

	//We can now go to IdleState
	error = OMX_SendCommand(OMX_Handle, OMX_CommandStateSet, OMX_StateIdle, NULL);
	if(error != OMX_ErrorNone)
	{
		DebugMessage(M64MSG_ERROR, "line %d: OMX_SendCommand() Failed. Error 0x%X",__LINE__, error);
		critical_failure = 1;
		return -1;
	}
	audio_wait_for_state(OMX_StateIdle);

	// check component is in the right state to accept buffers
	error = OMX_GetState(OMX_Handle, &state);
	if (error != OMX_ErrorNone || !(state == OMX_StateIdle || state == OMX_StateExecuting || state == OMX_StatePause))
	{
		DebugMessage(M64MSG_ERROR, "OMX not in correct state. state %d, error %d", state, error );
		critical_failure = 1;
		return -1;
	}

	DebugMessage(M64MSG_VERBOSE, "Creating %d Buffers", num_buffers);

	// Now enable the port ready for setting up buffers
	error = OMX_SendCommand(OMX_Handle, OMX_CommandPortEnable, PORT_INDEX, NULL);
	if(error != OMX_ErrorNone) DebugMessage(M64MSG_ERROR, "line %d: OMX_SendCommand() Failed. Error 0x%X", __LINE__, error);

	if (audioBuffers) free(audioBuffers);

	audioBuffers = (OMX_BUFFERHEADERTYPE**)malloc(num_buffers * sizeof(OMX_BUFFERHEADERTYPE*));

	for (i = 0; i < num_buffers; i++)
	{
		error = OMX_AllocateBuffer(OMX_Handle, &audioBuffers[i], PORT_INDEX, NULL, buffer_size);
		if (error != OMX_ErrorNone )
		{
			DebugMessage(M64MSG_ERROR, "Failed to allocate buffer[%d] for OMX. error 0x%X. ", i, error);
			critical_failure = 1;
			return -1;
		}
	}

	uiCurrentBufferLength = 0;
	uiBufferIndex = 0;
	pNextAudioSample = (uint32_t*)(audioBuffers[uiBufferIndex]->pBuffer);

	error = OMX_SendCommand(OMX_Handle, OMX_CommandPortEnable, 100, NULL);
	if(error != OMX_ErrorNone) DebugMessage(M64MSG_ERROR, "line %d: OMX_CommandPortEnable Failed",__LINE__);

	error = OMX_SendCommand(OMX_Handle, OMX_CommandStateSet, OMX_StateExecuting, NULL);
	if(error != OMX_ErrorNone)
	{
		DebugMessage(M64MSG_ERROR, "line %d: OMX_StateExecuting Failed",__LINE__);
		critical_failure = 1;
		return -1;
	}
	audio_wait_for_state(OMX_StateExecuting);

	DebugMessage(M64MSG_INFO, "OMX Audio plugin Initialized. Output Frequency %d Hz", OutputFreq);

	for (i = 0; i < num_buffers; i++)
	{
		memset(audioBuffers[i]->pBuffer, 0, buffer_size);
		audioBuffers[i]->nOffset = 0;
    	audioBuffers[i]->nFilledLen = (uiSecondaryBufferSamples * 4);

		error = OMX_EmptyThisBuffer(OMX_Handle, audioBuffers[i]);
		if(error != OMX_ErrorNone) DebugMessage(M64MSG_ERROR, "line %d: OMX_EmptyThisBuffer Failed",__LINE__);
	}
	return 0;
}

static int32_t audioplay_delete()
{
	OMX_ERRORTYPE error;

	error = OMX_SendCommand(OMX_Handle, OMX_CommandStateSet, OMX_StateLoaded, NULL);
	if (error != OMX_ErrorNone) DebugMessage(M64MSG_ERROR, "%d",__LINE__);

	error = OMX_Deinit();
	if(error != OMX_ErrorNone) DebugMessage(M64MSG_ERROR, "%d",__LINE__);

	return 0;
}


static int32_t audioplay_set_dest(const char *name)
{
	int32_t success = -1;
	OMX_CONFIG_BRCMAUDIODESTINATIONTYPE ar_dest;

	if (name && strlen(name) < sizeof(ar_dest.sName))
	{
		OMX_ERRORTYPE error;
		memset(&ar_dest, 0, sizeof(ar_dest));
		ar_dest.nSize = sizeof(OMX_CONFIG_BRCMAUDIODESTINATIONTYPE);
		ar_dest.nVersion.nVersion = OMX_VERSION;
		strcpy((char *)ar_dest.sName, name);

		error = OMX_SetConfig(OMX_Handle, OMX_IndexConfigBrcmAudioDestination, &ar_dest);
		if(error != OMX_ErrorNone) DebugMessage(M64MSG_ERROR, "Line %d: Failed to set OMX Configuration (AudioDestination)", __LINE__);
		success = 0;

		DebugMessage(M64MSG_VERBOSE, "Audio output to %s", name);
	}

	return success;
}

/* Get the latency in ms for audio */
static uint32_t audioplay_get_latency()
{
	OMX_PARAM_U32TYPE param;
	OMX_ERRORTYPE error;

	memset(&param, 0, sizeof(OMX_PARAM_U32TYPE));
	param.nSize = sizeof(OMX_PARAM_U32TYPE);
	param.nVersion.nVersion = OMX_VERSION;
	param.nPortIndex = 100;

	error = OMX_GetConfig(OMX_Handle, OMX_IndexConfigAudioRenderingLatency, &param);
	if(error != OMX_ErrorNone) DebugMessage(M64MSG_ERROR, "Line %d: Failed to get OMX Config Parameter ", __LINE__);

#ifdef MONITOR_BUFFER_READY
	DEBUG_PRINT("audio latency %dms %d\n", param.nU32 * 1000 / OutputFreq, buffersReady);
#else
	DEBUG_PRINT("audio latency %dms\n", param.nU32 * 1000 / OutputFreq);
#endif

	return param.nU32 * 1000 / OutputFreq;
}


static uint32_t SendBufferToAudio()
{
	uint32_t 		latency;
	static uint32_t uiUnderRunCount = 0;
	OMX_ERRORTYPE error;

	// try and wait for a minimum latency time (in ms) before
	// sending the next packet

	if (uiUnderrunMode)
	{	
		latency = audioplay_get_latency();

		if(latency > uiLatency)
		{
			DebugMessage(M64MSG_VERBOSE, "Waiting %dms ", latency - uiLatency);
			usleep((latency - uiLatency) * 1000 );
		}
		else if (latency == 0)
		{
			DebugMessage(M64MSG_WARNING, "Audio Buffer under run(%d)", uiUnderRunCount);
			uiUnderRunCount++;
		}
	}

	DEBUG_PRINT("audioplay_play_buffer()\n");
	
	audioBuffers[uiBufferIndex]->nOffset = 0;
    audioBuffers[uiBufferIndex]->nFilledLen = (uiSecondaryBufferSamples * 4);

	error = OMX_EmptyThisBuffer(OMX_Handle, audioBuffers[uiBufferIndex++]);
	if ( error != OMX_ErrorNone)
	{
		DebugMessage(M64MSG_ERROR, "Line %d: Failed on OMX_EmptyThisBuffer(). error = 0x%X (%u)",__LINE__, error, error);
	}
	#ifdef MONITOR_BUFFER_READY
	pthread_mutex_lock(&audioLock);
	buffersReady ++;
	pthread_mutex_unlock(&audioLock);
	#endif
	if (uiBufferIndex == uiNumBuffers) uiBufferIndex = 0;
	pNextAudioSample = (uint32_t*)audioBuffers[uiBufferIndex]->pBuffer;

	//DebugMessage(M64MSG_INFO,"uiBufferIndex %d, buffersReady %d, latency %d",uiBufferIndex, buffersReady, latency);

	uiCurrentBufferLength = 0;

	return 0;
}

/*
 * AiLenChanged is called by the Emulator Core however this Audio plugin will perform the resampling here so that when
 * the Audio Callback is run, Data can just be copied into the buffer.
 */
EXPORT void CALL AiLenChanged( void )
{
	uint32_t 		uiAudioBytes;
	volatile int32_t *p;

	int oldsamplerate, newsamplerate;

	if (!pNextAudioSample) return;
	if (critical_failure) return;
	if (!l_PluginInit) return;

	newsamplerate = OutputFreq * 100 / speed_factor;
	oldsamplerate = GameFreq;

	uiAudioBytes = (uint32_t)(*AudioInfo.AI_LEN_REG);
	p = (int32_t*)(AudioInfo.RDRAM + (*AudioInfo.AI_DRAM_ADDR_REG & 0xFFFFFF));

	if (bNative)
	{
		static uint32_t b=0, uiUnderRunCount = 0;
		uint32_t latency =0;	
			
		if (uiUnderrunMode)
		{		
			latency = audioplay_get_latency();

			if(latency > uiLatency)
			{
				DebugMessage(M64MSG_VERBOSE, "Waiting %dms ", latency - uiLatency);
				usleep((latency - uiLatency) * 1000 );
			}
			else if (latency == 0)
			{
				DebugMessage(M64MSG_WARNING, "Audio Buffer under run(%d)", uiUnderRunCount);
				uiUnderRunCount++;
			}

			if (uiUnderrunMode == 2 && latency < uiLatency/2)
			{
				audioBuffers[b]->pBuffer = (void*)p;
				audioBuffers[b]->nFilledLen = uiAudioBytes;
				OMX_EmptyThisBuffer(OMX_Handle, audioBuffers[b++]);
				if (b >= uiNumBuffers) b = 0;
			}
		}

		//Lylat wars uses 3 buffers and AiLenChanged is called every 200ms
		//Super Mario also uses 3 buffers, length is smaller and frequency is lower but AiLenChanged is also called every 200ms
		//fprintf(stderr, "%p, length %u, latency, %u, %dms\n", p, uiAudioBytes, latency, SDL_GetTicks());

		audioBuffers[b]->pBuffer = (void*)p;
		audioBuffers[b]->nFilledLen = uiAudioBytes;
		OMX_EmptyThisBuffer(OMX_Handle, audioBuffers[b++]);
		if (b >= uiNumBuffers) b = 0;

		return;
	}

	// ------------------------------- Copy music into audio buffer -----------------------------

	if (newsamplerate > oldsamplerate)
	{
		int j = 0;
		int sldf = oldsamplerate;
		int const2 = 2*sldf;
		int dldf = newsamplerate;
		int const1 = const2 - 2*dldf;
		int criteria = const2 - dldf;

		while (j < uiAudioBytes)
		{
			*pNextAudioSample++     = *p;

			uiCurrentBufferLength ++;

			if(criteria >= 0)
			{
				p++;
				j+=4;
				criteria += const1;
			}
			else
			{
				criteria += const2;
			}

			if (uiCurrentBufferLength >= uiSecondaryBufferSamples)
			{
				SendBufferToAudio();
			}
		}
	}
	else if (newsamplerate == oldsamplerate)
	{
		if (uiCurrentBufferLength + (uiAudioBytes / sizeof(int32_t)) >= uiSecondaryBufferSamples)
		{
			uint32_t bytesCanCopy = (uiSecondaryBufferSamples - uiCurrentBufferLength)*sizeof(int32_t);

			memcpy((void*)pNextAudioSample,(const void*) p, bytesCanCopy); 

			SendBufferToAudio();

			p+=	bytesCanCopy/sizeof(int32_t);

			memcpy((void*)pNextAudioSample,(const void*) p, uiAudioBytes - bytesCanCopy); 
			pNextAudioSample += (uiAudioBytes - bytesCanCopy) / sizeof(int32_t);
			uiCurrentBufferLength = (uiAudioBytes - bytesCanCopy)/ sizeof(int32_t);
		}
		else
		{
			memcpy((void*)pNextAudioSample,(const void*) p, uiAudioBytes); 
			pNextAudioSample += uiAudioBytes / sizeof(int32_t);
			uiCurrentBufferLength += uiAudioBytes/ sizeof(int32_t);
		}
	}
	else // newsamplerate < oldsamplerate, this only happens when speed_factor > 1
	{
		int inc = ((oldsamplerate << 10) / newsamplerate);
		int j = 0;
		volatile int * start = p;
		int repeat = 1;

		if (uiUnderrunMode == 2 && audioplay_get_latency() < uiLatency/2) repeat = 2;

		while (repeat)
		{
			while ((j>>8) < uiAudioBytes )
			{
				*pNextAudioSample++ = *p;

				j += inc ; //4 * oldsamplerate / newsamplerate;
				p = start+(j>>10); // (oldsamplerate / newsamplerate);

				uiCurrentBufferLength ++;

				if (uiCurrentBufferLength >= uiSecondaryBufferSamples)
				{
					SendBufferToAudio();
				}
			}

			repeat --;
			j= 0;
			p = start;
		}
	}
}

EXPORT int CALL InitiateAudio( AUDIO_INFO Audio_Info )
{
	if (!l_PluginInit)
		return 0;
	
	bcm_host_init(); 
	AudioInfo = Audio_Info;

	OMX_ERRORTYPE error;

	error = OMX_Init();
	if(error != OMX_ErrorNone) DebugMessage(M64MSG_ERROR, "%d OMX_Init() failed", __LINE__);

return 1;
}

static void InitializeAudio(int freq)
{
	if (freq < 4000) return; 			// Sometimes a bad freq is requested so ignore it 
	if (critical_failure) return;
	GameFreq = freq;

	int buffer_size = (uiSecondaryBufferSamples * 4);

	switch (uiOutputFrequencyMode)
	{
	case 0:										// Select Frequency ROM requests
		OutputFreq = freq;

		#ifndef EXT_CFG
		if (uiOutputPort == 1) bNative = 1;	//HDMI
		#endif

		break;
	case 1:										//Audo
		if (uiOutputPort == 1)
		{
			if (0 == vc_tv_hdmi_audio_supported(EDID_AudioFormat_ePCM, 2 /*channels*/, freq, 16))
			{
				DebugMessage(M64MSG_VERBOSE, "HDMI supports requested Frequency");
				break;
			}
			DebugMessage(M64MSG_VERBOSE, "HDMI failed to get requested Frequency");
		}
		else
		{
			DebugMessage(M64MSG_VERBOSE, "Analogue Mode 1 not supported. Selecting Mode 2");
		}
	case 2:										// Select Frequency less than ROM frequency
		if(freq >= 44100)
		{
			OutputFreq = 44100;
		}
		else if(freq >= 32000)
		{
			OutputFreq = 32000;
		}
		else if(freq >= 22050)
		{
			OutputFreq = 22050;
		}
		else
		{
			OutputFreq = 11025;
		}
		break;
	case 3:
		if(freq < 11025)
		{
			OutputFreq = 11025;
		}
		else if(freq < 22050)
		{
			OutputFreq = 22050;
		}
		else if(freq < 32000)
		{
			OutputFreq = 32000;
		}else
		{
			OutputFreq = 44100;
		}
		break;
	default: 									// User override frequency
		if (uiOutputFrequencyMode >= 10000)
		{
			OutputFreq = uiOutputFrequencyMode;

			// Does not work for me but worth a try ...
			if (0 == vc_tv_hdmi_audio_supported(EDID_AudioFormat_ePCM, 2, OutputFreq, 16))
			{
				DebugMessage(M64MSG_VERBOSE, "HDMI supports Audio at %d Hz", OutputFreq);
			}
			else
			{
				DebugMessage(M64MSG_VERBOSE, "HDMI does not support Audio at %d Hz", OutputFreq);
			}
		}
		else
		{
			OutputFreq = 10000;
		}
		break;
	}
	
	if (bNative)
	{
 		OutputFreq = freq;
		uiSecondaryBufferSamples = DEFAULT_BUFFER_SIZE;
		uiLatency = DEFAULT_LATENCY;
	}

	if (audioBuffers)
	{
		int x;

		for (x = 0; x < uiNumBuffers; x++ ) OMX_FreeBuffer(OMX_Handle, PORT_INDEX, audioBuffers[x]);
		free(audioBuffers);
		audioBuffers = NULL;
	}

	uiNumBuffers = 2 + OutputFreq * uiLatency / (uiSecondaryBufferSamples*1000);

	audioplay_create(uiNumBuffers, buffer_size);

	if (!critical_failure) audioplay_set_dest(audio_dest[uiOutputPort]);
}

EXPORT int CALL RomOpen(void)
{
	if (!l_PluginInit) return 0;

	ReadConfig();

	pthread_mutex_init(&audioLock, NULL);
	
	InitializeAudio(GameFreq);
	return 1;
}

EXPORT void CALL RomClosed( void )
{
	DebugMessage(M64MSG_VERBOSE, "Cleaning up OMX sound plugin...");

	audioplay_delete();

	if (audioBuffers)
	{
		int x;
		for (x = 0; x < uiNumBuffers; x++ ) OMX_FreeBuffer(OMX_Handle, PORT_INDEX, audioBuffers[x]);
		free(audioBuffers);
		audioBuffers = NULL;
	}

	pthread_mutex_destroy(&audioLock);

	if (!l_PluginInit)
		return;
	if (critical_failure == 1)
		return;
}

EXPORT void CALL ProcessAList(void)
{
}

EXPORT void CALL SetSpeedFactor(int percentage)
{
	if (!l_PluginInit)
		return;
	if (percentage >= 10 && percentage <= 300)
		speed_factor = percentage;
}

static void ReadConfig(void)
{
	/* read the configuration values into our static variables */
#ifdef EXT_CFG
	bNative = 					ConfigGetParamBool(l_ConfigAudio, "NATIVE_MODE");
	uiSecondaryBufferSamples = 	ConfigGetParamInt(l_ConfigAudio, "BUFFER_SIZE");
	uiLatency = 				ConfigGetParamInt(l_ConfigAudio, "LATENCY");
	uiUnderrunMode = 			ConfigGetParamInt(l_ConfigAudio, "UNDERRUN_MODE");
	if (uiLatency <= MIN_LATENCY_TIME) uiLatency = MIN_LATENCY_TIME + 1;
#endif

	GameFreq = 					ConfigGetParamInt(l_ConfigAudio, "DEFAULT_FREQUENCY");
	bSwapChannels = 			ConfigGetParamBool(l_ConfigAudio, "SWAP_CHANNELS");
	uiOutputPort = 				ConfigGetParamInt(l_ConfigAudio, "OUTPUT_PORT");
	uiOutputFrequencyMode = 	ConfigGetParamInt(l_ConfigAudio, "DEFAULT_MODE");	
}

// Sets the volume level based on the contents of VolPercent and VolIsMuted
// OMX does not support Volumne control on the Raspberry PI
/*
static void VolumeCommit(void)
{

	int levelToCommit = 100;

	OMX_ERRORTYPE omxErr;

	OMX_AUDIO_CONFIG_VOLUMETYPE volumeConfig;
	memset(&volumeConfig, 0, sizeof(OMX_AUDIO_CONFIG_VOLUMETYPE));
	volumeConfig.nSize = sizeof(OMX_AUDIO_CONFIG_VOLUMETYPE);
	volumeConfig.nVersion.nVersion = OMX_VERSION;
	volumeConfig.nPortIndex = PORT_INDEX;

	volumeConfig.sVolume.nMax = 100;
	volumeConfig.bLinear = OMX_TRUE;
	volumeConfig.sVolume.nValue = levelToCommit;

	omxErr = OMX_SetConfig(OMX_Handle, OMX_IndexConfigAudioVolume, &volumeConfig);
	if(omxErr != OMX_ErrorNone)
	{
		DebugMessage(M64MSG_ERROR, "Could not set Audio Volume. OMX Error 0x%X", omxErr);
	}
}
*/

EXPORT void CALL VolumeMute(void)
{
}

EXPORT void CALL VolumeUp(void)
{
}

EXPORT void CALL VolumeDown(void)
{
}

EXPORT int CALL VolumeGetLevel(void)
{
	return 100;
}

EXPORT void CALL VolumeSetLevel(int level)
{
}

EXPORT const char * CALL VolumeGetString(void)
{
	return "100%";
}


