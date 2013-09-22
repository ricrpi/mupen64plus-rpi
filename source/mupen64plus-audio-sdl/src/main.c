/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus-rpi-audio - main.c                                        *
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
#include "ilclient.h"

#define M64P_PLUGIN_PROTOTYPES 1
#include "m64p_types.h"
#include "m64p_plugin.h"
#include "m64p_common.h"
#include "m64p_config.h"

#include "main.h"
#include "volume.h"
#include "osal_dynamiclib.h"

extern uint32_t SDL_GetTicks(void);

/* Size of secondary buffer, in output samples. This is the requested size of SDL's
   hardware buffer, and the size of the mix buffer for doing SDL volume control. The
   SDL documentation states that this should be a power of two between 512 and 8192.
   2048 represents 11.6ms of data at 44kHz with 32bit sample lengths*/
#define SECONDARY_BUFFER_SIZE 2048

/* This sets default frequency what is used if rom doesn't want to change it.
   Probably only game that needs this is Zelda: Ocarina Of Time Master Quest
 *NOTICE* We should try to find out why Demos' frequencies are always wrong
   They tend to rely on a default frequency, apparently, never the same one ;)*/
#define DEFAULT_FREQUENCY 33600

/* Default audio output on HDMI (1) or analogue port (0) */
#define OUTPUT_PORT 1

/* Latency in ms that the audio buffers may have*/
#define DEFAULT_LATENCY 100

/* Number of buffers used by Audio*/
#define DEFAULT_NUM_BUFFERS 10


#define SAMPLE_SIZE_BITS 	16
#define NUM_CHANNELS		2

#define CTTW_SLEEP_TIME 10
#define MIN_LATENCY_TIME 10

#define OUT_CHANNELS(num_channels) ((num_channels) > 4 ? 8: (num_channels) > 2 ? 4: (num_channels))

static const char *audio_dest[] = {"local", "hdmi"};

typedef struct {
   sem_t sema;
   ILCLIENT_T *client;
   COMPONENT_T *audio_render;
   COMPONENT_T *list[2];
   OMX_BUFFERHEADERTYPE *user_buffer_list; // buffers owned by the client
   uint32_t num_buffers;
   uint32_t bytes_per_sample;
} AUDIOPLAY_STATE_T;


/* local variables */
static void (*l_DebugCallback)(void *, int, const char *) = NULL;
static void *l_DebugCallContext = NULL;
static int l_PluginInit = 0;

static m64p_handle l_ConfigAudio;

/* Read header for type definition */
static AUDIO_INFO AudioInfo;

static AUDIOPLAY_STATE_T *st;

/* Audio frequency, this is usually obtained from the game, but for compatibility we set default value */
static int GameFreq = DEFAULT_FREQUENCY;

/* timestamp for the last time that our audio callback was called */
//static unsigned int last_callback_ticks = 0;

/* SpeedFactor is used to increase/decrease game playback speed */
static unsigned int speed_factor = 100;

// If this is true then left and right channels are swapped */
static unsigned int bSwapChannels = 0;

// Size of Secondary audio buffer in output samples
static unsigned int uiSecondaryBufferSamples = SECONDARY_BUFFER_SIZE;

static unsigned int uiOutputPort = OUTPUT_PORT;

static unsigned int OutputFreq = 22050;

static unsigned int uiLatency = DEFAULT_LATENCY;

static unsigned int uiNumBuffers = DEFAULT_NUM_BUFFERS;

// volume to scale the audio by, range of 0..100
// if muted, this holds the volume when not muted
static unsigned int VolPercent = 80;

// how much percent to increment/decrement volume by
static unsigned int VolDelta = 5;

// the actual volume passed into SDL, range of 0..SDL_MIX_MAXVOLUME
static unsigned int VolSDL = 80;

// Muted or not
static unsigned int VolIsMuted = 0;

// Prototype of local functions
static void InitializeAudio(int freq);
static void ReadConfig(void);

static unsigned int critical_failure = 0;

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
	if (ConfigOpenSection("Audio-RPI", &l_ConfigAudio) != M64ERR_SUCCESS)
	{
		DebugMessage(M64MSG_ERROR, "Couldn't open config section 'Audio-RPI'");
		return M64ERR_INPUT_NOT_FOUND;
	}

	/* check the section version number */
	bSaveConfig = 0;
	if (ConfigGetParameter(l_ConfigAudio, "Version", M64TYPE_FLOAT, &fConfigParamsVersion, sizeof(float)) != M64ERR_SUCCESS)
	{
		DebugMessage(M64MSG_WARNING, "No version number in 'Audio-RPI' config section. Setting defaults.");
		ConfigDeleteSection("Audio-RPI");
		ConfigOpenSection("Audio-RPI", &l_ConfigAudio);
		bSaveConfig = 1;
	}
	else if (((int) fConfigParamsVersion) != ((int) CONFIG_PARAM_VERSION))
	{
		DebugMessage(M64MSG_WARNING, "Incompatible version %.2f in 'Audio-RPI' config section: current is %.2f. Setting defaults.", fConfigParamsVersion, (float) CONFIG_PARAM_VERSION);
		ConfigDeleteSection("Audio-RPI");
		ConfigOpenSection("Audio-RPI", &l_ConfigAudio);
		bSaveConfig = 1;
	}
	else if ((CONFIG_PARAM_VERSION - fConfigParamsVersion) >= 0.0001f)
	{
		/* handle upgrades */
		float fVersion = CONFIG_PARAM_VERSION;
		ConfigSetParameter(l_ConfigAudio, "Version", M64TYPE_FLOAT, &fVersion);
		DebugMessage(M64MSG_INFO, "Updating parameter set version in 'Audio-RPI' config section to %.2f", fVersion);
		bSaveConfig = 1;
	}

	/* set the default values for this plugin */
	ConfigSetDefaultFloat(l_ConfigAudio,"Version",             	CONFIG_PARAM_VERSION,  		"Mupen64Plus RPI Audio Plugin config parameter version number");
	ConfigSetDefaultInt(l_ConfigAudio, 	"DEFAULT_FREQUENCY",    DEFAULT_FREQUENCY,     		"Frequency which is used if rom doesn't want to change it");
	ConfigSetDefaultBool(l_ConfigAudio, "SWAP_CHANNELS",        0,                     		"Swaps left and right channels");
	ConfigSetDefaultInt(l_ConfigAudio, 	"SECONDARY_BUFFER_SIZE",SECONDARY_BUFFER_SIZE, 		"Number of output samples per Audio callback. This is SDL's hardware buffer.");
    ConfigSetDefaultInt(l_ConfigAudio, 	"OUTPUT_PORT",   		OUTPUT_PORT,   				"Audio output to go to (0) Analogue jack, (1) HDMI");
	ConfigSetDefaultInt(l_ConfigAudio, 	"LATENCY",      		20,                     	"Desired Latency in ms");
	ConfigSetDefaultInt(l_ConfigAudio, 	"VOLUME_ADJUST",        5,                     		"Percentage change each time the volume is increased or decreased");
	ConfigSetDefaultInt(l_ConfigAudio, 	"VOLUME_DEFAULT",       80,                    		"Default volume when a game is started");
	ConfigSetDefaultInt(l_ConfigAudio,	"DEFAULT_NUM_BUFFERS",	10,							"The number of Audio buffers to use");
	if (bSaveConfig && ConfigAPIVersion >= 0x020100)
		ConfigSaveSection("Audio-RPI");

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
		*PluginNamePtr = "Mupen64Plus RPI Audio Plugin";

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

static void input_buffer_callback(void *data, COMPONENT_T *comp)
{
   // do nothing - could add a callback to the user
   // to indicate more buffers may be available.
}

static int32_t audioplay_create(AUDIOPLAY_STATE_T **handle,
                         uint32_t num_channels,
                         uint32_t bit_depth,
                         uint32_t num_buffers,
                         uint32_t buffer_size)
{
   uint32_t bytes_per_sample = (bit_depth * OUT_CHANNELS(num_channels)) >> 3;
   int32_t ret = -1;

   *handle = NULL;

   // basic sanity check on arguments
   if((num_channels >= 1 && num_channels <= 8) &&
      (bit_depth == 16 || bit_depth == 32) &&
      num_buffers > 0 &&
      buffer_size >= bytes_per_sample)
   {
      // buffer lengths must be 16 byte aligned for VCHI
      int size = (buffer_size + 15) & ~15;
      AUDIOPLAY_STATE_T *st;

      // buffer offsets must also be 16 byte aligned for VCHI
      st = calloc(1, sizeof(AUDIOPLAY_STATE_T));

      if(st)
      {
         OMX_ERRORTYPE error;
         OMX_PARAM_PORTDEFINITIONTYPE param;
         OMX_AUDIO_PARAM_PCMMODETYPE pcm;
         int32_t s;

         ret = 0;
         *handle = st;

         // create and start up everything
         s = sem_init(&st->sema, 0, 1);
         if(s != 0) DebugMessage(M64MSG_ERROR, "%d",__LINE__);

         st->bytes_per_sample = bytes_per_sample;
         st->num_buffers = num_buffers;

         st->client = ilclient_init();
         if(st->client == NULL) DebugMessage(M64MSG_ERROR, "%d",__LINE__);

         ilclient_set_empty_buffer_done_callback(st->client, input_buffer_callback, st);

         error = OMX_Init();
         if(error != OMX_ErrorNone) DebugMessage(M64MSG_ERROR, "%d",__LINE__);

         ilclient_create_component(st->client, &st->audio_render, "audio_render", ILCLIENT_ENABLE_INPUT_BUFFERS | ILCLIENT_DISABLE_ALL_PORTS);
         if(st->audio_render == NULL) DebugMessage(M64MSG_ERROR, "%d",__LINE__);

         st->list[0] = st->audio_render;

         // set up the number/size of buffers
         memset(&param, 0, sizeof(OMX_PARAM_PORTDEFINITIONTYPE));
         param.nSize = sizeof(OMX_PARAM_PORTDEFINITIONTYPE);
         param.nVersion.nVersion = OMX_VERSION;
         param.nPortIndex = 100;

         error = OMX_GetParameter(ILC_GET_HANDLE(st->audio_render), OMX_IndexParamPortDefinition, &param);
         if(error != OMX_ErrorNone) DebugMessage(M64MSG_ERROR, "%d",__LINE__);

         param.nBufferSize = size;
         param.nBufferCountActual = num_buffers;

         error = OMX_SetParameter(ILC_GET_HANDLE(st->audio_render), OMX_IndexParamPortDefinition, &param);
         if(error != OMX_ErrorNone) DebugMessage(M64MSG_ERROR, "%d",__LINE__);

         // set the pcm parameters
         memset(&pcm, 0, sizeof(OMX_AUDIO_PARAM_PCMMODETYPE));
         pcm.nSize = sizeof(OMX_AUDIO_PARAM_PCMMODETYPE);
         pcm.nVersion.nVersion = OMX_VERSION;
         pcm.nPortIndex = 100;
         pcm.nChannels = OUT_CHANNELS(num_channels);
         pcm.eNumData = OMX_NumericalDataSigned;
         pcm.eEndian = OMX_EndianLittle;
         pcm.nSamplingRate = OutputFreq;
         pcm.bInterleaved = OMX_TRUE;
         pcm.nBitPerSample = bit_depth;
         pcm.ePCMMode = OMX_AUDIO_PCMModeLinear;

         switch(num_channels) {
         case 1:
            pcm.eChannelMapping[0] = OMX_AUDIO_ChannelCF;
            break;
         case 3:
            pcm.eChannelMapping[2] = OMX_AUDIO_ChannelCF;
            pcm.eChannelMapping[1] = OMX_AUDIO_ChannelRF;
            pcm.eChannelMapping[0] = OMX_AUDIO_ChannelLF;
            break;
         case 8:
            pcm.eChannelMapping[7] = OMX_AUDIO_ChannelRS;
         case 7:
            pcm.eChannelMapping[6] = OMX_AUDIO_ChannelLS;
         case 6:
            //pcm.eChannelMapping[5] = OMX_AUDIO_ChannelRR;
         case 5:
            //pcm.eChannelMapping[4] = OMX_AUDIO_ChannelLR;
         case 4:
            //pcm.eChannelMapping[3] = OMX_AUDIO_ChannelLFE;
            //pcm.eChannelMapping[2] = OMX_AUDIO_ChannelCF;
			pcm.eChannelMapping[3] = OMX_AUDIO_ChannelRR;
			pcm.eChannelMapping[2] = OMX_AUDIO_ChannelLR;
         case 2:
            pcm.eChannelMapping[1] = OMX_AUDIO_ChannelRF;
            pcm.eChannelMapping[0] = OMX_AUDIO_ChannelLF;
            break;
         }

         error = OMX_SetParameter(ILC_GET_HANDLE(st->audio_render), OMX_IndexParamAudioPcm, &pcm);
         if(error != OMX_ErrorNone) DebugMessage(M64MSG_ERROR, "line %d: Failed to Set OMX Parameters",__LINE__);

         ilclient_change_component_state(st->audio_render, OMX_StateIdle);

		/* TODO arg 3 and 4 set the function to malloc / free buffers. This could be optimized to a 
			pre-malloc'ed ring buffer*/
         if(ilclient_enable_port_buffers(st->audio_render, 100, NULL, NULL, NULL) < 0)
         {
            // error
            ilclient_change_component_state(st->audio_render, OMX_StateLoaded);
            ilclient_cleanup_components(st->list);

            error = OMX_Deinit();
            if(error != OMX_ErrorNone) DebugMessage(M64MSG_ERROR, "%d",__LINE__);

            ilclient_destroy(st->client);

            sem_destroy(&st->sema);
            free(st);
            *handle = NULL;
            return -1;
	}
	DebugMessage(M64MSG_INFO, "RPI Audio plugin Initialized. Output Frequency %d Hz", OutputFreq);
	ilclient_change_component_state(st->audio_render, OMX_StateExecuting);
	}
    }
	
	return ret;
}

static int32_t audioplay_delete(AUDIOPLAY_STATE_T *st)
{
   OMX_ERRORTYPE error;

   ilclient_change_component_state(st->audio_render, OMX_StateIdle);

   error = OMX_SendCommand(ILC_GET_HANDLE(st->audio_render), OMX_CommandStateSet, OMX_StateLoaded, NULL);
   if (error != OMX_ErrorNone) DebugMessage(M64MSG_ERROR, "%d",__LINE__);

   ilclient_disable_port_buffers(st->audio_render, 100, st->user_buffer_list, NULL, NULL);
   ilclient_change_component_state(st->audio_render, OMX_StateLoaded);
   ilclient_cleanup_components(st->list);

   error = OMX_Deinit();
   if(error != OMX_ErrorNone) DebugMessage(M64MSG_ERROR, "%d",__LINE__);

   ilclient_destroy(st->client);

   sem_destroy(&st->sema);
   free(st);

   return 0;
}

static uint8_t *audioplay_get_buffer(AUDIOPLAY_STATE_T *st)
{
   OMX_BUFFERHEADERTYPE *hdr = NULL;

   hdr = ilclient_get_input_buffer(st->audio_render, 100, 0);

   if(hdr)
   {
      // put on the user list
      sem_wait(&st->sema);

      hdr->pAppPrivate = st->user_buffer_list;
      st->user_buffer_list = hdr;

      sem_post(&st->sema);
   }

   return hdr ? hdr->pBuffer : NULL;
}

static int32_t audioplay_play_buffer(AUDIOPLAY_STATE_T *st,
                              uint8_t *buffer,
                              uint32_t length)
{
   	OMX_BUFFERHEADERTYPE *hdr = NULL, *prev = NULL;
   	int32_t ret = -1;

   	if(length % st->bytes_per_sample)
	{
		DebugMessage(M64MSG_ERROR, "Line %d: Buffer length is not correct. Not playing buffer", __LINE__);
      	return ret;
	}
   	sem_wait(&st->sema);

   	// search through user list for the right buffer header
   	hdr = st->user_buffer_list;
   	while(hdr != NULL && hdr->pBuffer != buffer && hdr->nAllocLen < length)
   	{
      	prev = hdr;
      	hdr = hdr->pAppPrivate;
   	}

   	if(hdr) // we found it, remove from list
   	{
      	ret = 0;
      	if(prev)
         	prev->pAppPrivate = hdr->pAppPrivate;
      	else
         	st->user_buffer_list = hdr->pAppPrivate;
   	}

   	sem_post(&st->sema);

	if(hdr)
	{
		OMX_ERRORTYPE error;

		hdr->pAppPrivate = NULL;
      	hdr->nOffset = 0;
      	hdr->nFilledLen = length;

      	error = OMX_EmptyThisBuffer(ILC_GET_HANDLE(st->audio_render), hdr);
      	if(error != OMX_ErrorNone)
		{
			DebugMessage(M64MSG_ERROR, "Line %d: Failed on OMX_EmptyThisBuffer()",__LINE__);
		}
		else
		{
			DebugMessage(M64MSG_VERBOSE, "Playing buffer at 0x%X, hdr = 0x8%X", buffer, hdr);
		}
   	}
	else
	{
		DebugMessage(M64MSG_ERROR, "Line %d: Failed to find header. SECONDARY_BUFFER_SIZE may be too large", __LINE__);
	}

   return ret;
}

static int32_t audioplay_set_dest(AUDIOPLAY_STATE_T *st, const char *name)
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

      error = OMX_SetConfig(ILC_GET_HANDLE(st->audio_render), OMX_IndexConfigBrcmAudioDestination, &ar_dest);
      if(error != OMX_ErrorNone) DebugMessage(M64MSG_ERROR, "Line %d: Failed to set OMX Configuration (AudioDestination)", __LINE__);
      success = 0;

	
	DebugMessage(M64MSG_VERBOSE, "Audio output to %s", name);
   }

   return success;
}

/* Get the latency in ms for audio */
uint32_t audioplay_get_latency(AUDIOPLAY_STATE_T *st)
{
   	OMX_PARAM_U32TYPE param;
   	OMX_ERRORTYPE error;
   	
	memset(&param, 0, sizeof(OMX_PARAM_U32TYPE));
   	param.nSize = sizeof(OMX_PARAM_U32TYPE);
	param.nVersion.nVersion = OMX_VERSION;
	param.nPortIndex = 100;
		
   	error = OMX_GetConfig(ILC_GET_HANDLE(st->audio_render), OMX_IndexConfigAudioRenderingLatency, &param);
   	if(error != OMX_ErrorNone) DebugMessage(M64MSG_ERROR, "Line %d: Failed to get OMX Config Parameter ", __LINE__);

   return param.nU32 * 1000 / OutputFreq;
}


static uint32_t SendBufferToAudio(int16_t *pCurrentBuffer)
{
	uint32_t 		latency;
	static uint32_t uiUnderRunCount = 0;

	#if NUM_CHANNELS > 2
		memcpy(&pCurrentBuffer[uiSecondaryBufferSamples * 2], pCurrentBuffer, 2 * uiSecondaryBufferSamples * sizeof(int16_t))
	#endif

	// try and wait for a minimum latency time (in ms) before
	// sending the next packet
	latency = audioplay_get_latency(st);
				
	if(latency > uiLatency)
	{
		DebugMessage(M64MSG_VERBOSE, "Waiting %dms ", latency - uiLatency);
		usleep((latency - uiLatency) * 1000 );
	}
	else if (latency == 0)
	{
		DebugMessage(M64MSG_VERBOSE, "Audio Buffer under run(%d)", uiUnderRunCount);
		uiUnderRunCount++;
	}

	audioplay_play_buffer(st, (uint8_t*)pCurrentBuffer, (uiSecondaryBufferSamples * SAMPLE_SIZE_BITS * NUM_CHANNELS)>>3);	
	
	return 0;
}

/*
 * AiLenChanged is called by the Emulator Core however this Audio plugin will perform the resampling here so that when
 * the Audio Callback is run, Data can just be copied into the buffer.
 */
EXPORT void CALL AiLenChanged( void )
{
	uint32_t 		uiAudioBytes;
	uint32_t 		latency;
	uint32_t		time 			= 0;
	static int16_t 	*pCurrentBuffer = NULL;
	static uint32_t uiBufferIndex 	= 0;
	volatile int16_t *p;
	int oldsamplerate, newsamplerate;
	
	newsamplerate = OutputFreq * 100 / speed_factor;
	oldsamplerate = GameFreq / 2;				//GameFreq is frequency of 2 samples
	
	if (critical_failure == 1) return;
	if (!l_PluginInit) return;
	
	time = SDL_GetTicks();

	uiAudioBytes = (uint32_t)(*AudioInfo.AI_LEN_REG);
	
	//unsigned int AudioBytes = ((uiAudioBytes * newsamplerate) / oldsamplerate)&0xFFFC;
	
	p = (int16_t*)(AudioInfo.RDRAM + (*AudioInfo.AI_DRAM_ADDR_REG & 0xFFFFFF));

	//DebugMessage(M64MSG_INFO, "uiAudioBytes = %d, uiBufferIndex = %d", uiAudioBytes, uiBufferIndex);
	//DebugMessage(M64MSG_INFO, "in  %8d %8d %8d %8d %8d %8d %8d %8d",p[0],p[1],p[2],p[3],p[4],p[5],p[6],p[7]);

	if (pCurrentBuffer == NULL)
    	{
		while((pCurrentBuffer = (int16_t*)audioplay_get_buffer(st)) == NULL) usleep(1*1000);
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
			if(bSwapChannels == 0)
			{
				pCurrentBuffer[ uiBufferIndex ]     = p[1];
				pCurrentBuffer[ uiBufferIndex + uiSecondaryBufferSamples ] = p[0];
			} else {
				pCurrentBuffer[ uiBufferIndex ]     = p[0];
				pCurrentBuffer[ uiBufferIndex + uiSecondaryBufferSamples ] = p[1];
			}
			uiBufferIndex ++;
			
			if(criteria >= 0)
			{
				p+=2;
				j+=4;
				criteria += const1;
			}
			else
			{
				criteria += const2;
			}

			if (uiBufferIndex >= uiSecondaryBufferSamples)
			{
				SendBufferToAudio(pCurrentBuffer);
				uiBufferIndex = 0;
				
				while((pCurrentBuffer = (int16_t*)audioplay_get_buffer(st)) == NULL)
				{ 
					DebugMessage(M64MSG_VERBOSE, "Can't get next pCurrentBuffer for Audio");	
					usleep(1*1000);
				}
			}
		}
	}
	else // newsamplerate < oldsamplerate, this only happens when speed_factor > 1
	{
		int j = 0;

		while (j < uiAudioBytes )
		{
			if(bSwapChannels == 0)
			{
				pCurrentBuffer[ uiBufferIndex ] = p[ 1 ];
				pCurrentBuffer[ uiBufferIndex + uiSecondaryBufferSamples ] = p[ 0 ];
			} else {
				pCurrentBuffer[ uiBufferIndex ] = p[ 0 ];
				pCurrentBuffer[ uiBufferIndex + uiSecondaryBufferSamples ] = p[ 1 ];
			}
			j += 4 * oldsamplerate / newsamplerate;
			p += 2 * oldsamplerate / newsamplerate;
			
			uiBufferIndex ++;

			if (uiBufferIndex >= uiSecondaryBufferSamples)
			{
				SendBufferToAudio(pCurrentBuffer);
				uiBufferIndex = 0;
				
				while((pCurrentBuffer = (int16_t*)audioplay_get_buffer(st)) == NULL)
				{ 
					DebugMessage(M64MSG_VERBOSE, "Can't get next pCurrentBuffer for Audio");	
					usleep(1*1000);
				}
			}
		}
	}
	
	//DebugMessage(M64MSG_VERBOSE, "out %8d %8d %8d %8d %8d %8d %8d %8d",pCurrentBuffer[0],pCurrentBuffer[1],pCurrentBuffer[2],pCurrentBuffer[3],pCurrentBuffer[4],pCurrentBuffer[5],pCurrentBuffer[6],pCurrentBuffer[7]);

		//latency = audioplay_get_latency(st);
		//DebugMessage(M64MSG_INFO, "%6d latency = %d", time, latency);

}

EXPORT int CALL InitiateAudio( AUDIO_INFO Audio_Info )
{
	if (!l_PluginInit)
		return 0;

	bcm_host_init(); 
	AudioInfo = Audio_Info;
	return 1;
}

EXPORT int CALL RomOpen(void)
{
	if (!l_PluginInit) return 0;

	ReadConfig();
	InitializeAudio(GameFreq);
	return 1;
}

static void InitializeAudio(int freq)
{

	if (freq < 4000) return; 			// Sometimes a bad freq is requested so ignore it 
	if (critical_failure == 1) return;
	GameFreq = freq;


	int buffer_size = (uiSecondaryBufferSamples * SAMPLE_SIZE_BITS * OUT_CHANNELS(NUM_CHANNELS))>>3;

	//if (st != NULL) audioplay_delete(st);

	audioplay_create(&st, NUM_CHANNELS, SAMPLE_SIZE_BITS, uiNumBuffers, buffer_size);

	audioplay_set_dest(st, audio_dest[uiOutputPort]); // hdmi for now

}
EXPORT void CALL RomClosed( void )
{
	DebugMessage(M64MSG_VERBOSE, "Cleaning up RPI sound plugin...");

 	audioplay_delete(st);

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
	GameFreq = 					ConfigGetParamInt(l_ConfigAudio, "DEFAULT_FREQUENCY");
	bSwapChannels = 			ConfigGetParamBool(l_ConfigAudio, "SWAP_CHANNELS");
	uiSecondaryBufferSamples = 	ConfigGetParamInt(l_ConfigAudio, "SECONDARY_BUFFER_SIZE");
	uiOutputPort = 				ConfigGetParamInt(l_ConfigAudio, "OUTPUT_PORT");
	uiLatency = 				ConfigGetParamInt(l_ConfigAudio, "LATENCY");
	VolDelta = 					ConfigGetParamInt(l_ConfigAudio, "VOLUME_ADJUST");
	VolPercent = 				ConfigGetParamInt(l_ConfigAudio, "VOLUME_DEFAULT");
	uiNumBuffers = 				ConfigGetParamInt(l_ConfigAudio, "DEFAULT_NUM_BUFFERS");

	if (uiLatency <= MIN_LATENCY_TIME) uiLatency = MIN_LATENCY_TIME + 1;

}

// Returns the most recent ummuted volume level.
static int VolumeGetUnmutedLevel(void)
{
	return VolPercent;
}

// Sets the volume level based on the contents of VolPercent and VolIsMuted
static void VolumeCommit(void)
{
	int levelToCommit = VolIsMuted ? 0 : VolPercent;
	
	VolSDL = 100 * levelToCommit / 100;
	
}

EXPORT void CALL VolumeMute(void)
{
	if (!l_PluginInit)
		return;

	// Store the volume level in order to restore it later
	if (!VolIsMuted)
		VolPercent = VolumeGetUnmutedLevel();

	// Toogle mute
	VolIsMuted = !VolIsMuted;
	VolumeCommit();
}

EXPORT void CALL VolumeUp(void)
{
	if (!l_PluginInit)
		return;

	VolumeSetLevel(VolumeGetUnmutedLevel() + VolDelta);
}

EXPORT void CALL VolumeDown(void)
{
	if (!l_PluginInit)
		return;

	VolumeSetLevel(VolumeGetUnmutedLevel() - VolDelta);
}

EXPORT int CALL VolumeGetLevel(void)
{
	return VolIsMuted ? 0 : VolumeGetUnmutedLevel();
}

EXPORT void CALL VolumeSetLevel(int level)
{
	if (!l_PluginInit)
		return;

	//if muted, unmute first
	VolIsMuted = 0;

	// adjust volume
	VolPercent = level;
	if (VolPercent < 0)
		VolPercent = 0;
	else if (VolPercent > 100)
		VolPercent = 100;

	VolumeCommit();
}

EXPORT const char * CALL VolumeGetString(void)
{
	static char VolumeString[32];

	if (VolIsMuted)
	{
		strcpy(VolumeString, "Mute");
	}
	else
	{
		sprintf(VolumeString, "%i%%", VolPercent);
	}

	return VolumeString;
}


