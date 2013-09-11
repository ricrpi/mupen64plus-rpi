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

#include <SDL.h>
#include <SDL_audio.h>

#define M64P_PLUGIN_PROTOTYPES 1
#include "m64p_types.h"
#include "m64p_plugin.h"
#include "m64p_common.h"
#include "m64p_config.h"

#include "main.h"
#include "volume.h"
#include "osal_dynamiclib.h"

/* Default start-time size of primary buffer (in equivalent output samples).
   This is the buffer where audio is loaded after it's extracted from n64's memory.
   This value must be larger than PRIMARY_BUFFER_TARGET */
#define PRIMARY_BUFFER_MULTIPLE 10

/* this is the buffer fullness level (in equivalent output samples) which is targeted
   for the primary audio buffer (by inserting delays) each time data is received from
   the running N64 program.  This value must be larger than the SECONDARY_BUFFER_SIZE.
   Decreasing this value will reduce audio latency but requires a faster PC to avoid
   choppiness. Increasing this will increase audio latency but reduce the chance of
   drop-outs. The default value 10240 gives a 232ms maximum A/V delay at 44.1khz */
#define PRIMARY_BUFFER_TARGET 8

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

/* This is the mode for selecting the output frequency for the Audio plugin
	MODE 0 is to use ROM frequency (or DEFAULT if not set)
	MODE 1 is to use 11025, 22050, 44100 if <= ROM frequency
	MODE N is the frequency to use for sound output
*/
#define DEFAULT_MODE 0

/* number of bytes per sample */
#define N64_SAMPLE_BYTES 4
#define SDL_SAMPLE_BYTES 4

/* volume mixer types */
#define VOLUME_TYPE_SDL     1
#define VOLUME_TYPE_OSS     2

/* local variables */
static void (*l_DebugCallback)(void *, int, const char *) = NULL;
static void *l_DebugCallContext = NULL;
static int l_PluginInit = 0;
static int l_PausedForSync = 1; /* Audio is started in paused state after SDL initialization */
static m64p_handle l_ConfigAudio;

/* Read header for type definition */
static AUDIO_INFO AudioInfo;

/* The hardware specifications we are using */
static SDL_AudioSpec *hardware_spec;

/* Audio buffer variables */
static unsigned char *pBuffer 		= NULL;
static unsigned int uiBufferStart   = 0;
static unsigned int uiBufferEnd     = 0;
static unsigned int uiBufferSize	= 0;
static unsigned int uiBufferSizeMax = 0;

/* Audio frequency, this is usually obtained from the game, but for compatibility we set default value */
static int GameFreq = DEFAULT_FREQUENCY;

/* timestamp for the last time that our audio callback was called */
static unsigned int last_callback_ticks = 0;

/* SpeedFactor is used to increase/decrease game playback speed */
static unsigned int speed_factor = 100;

// If this is true then left and right channels are swapped */
static unsigned int bSwapChannels = 0;

// This is the frequency mode or Target Output Frequency
static unsigned int uiOutputFrequencyMode = DEFAULT_MODE;

// Size of Primary audio buffer in equivalent output samples
static unsigned int uiBufferMultiple = PRIMARY_BUFFER_MULTIPLE;

// Fullness level target for Primary audio buffer, in equivalent output samples
static unsigned int uiBufferTargetMultiple = PRIMARY_BUFFER_TARGET;

// Size of Secondary audio buffer in output samples
static unsigned int uiSecondaryBufferSize = SECONDARY_BUFFER_SIZE;

// volume to scale the audio by, range of 0..100
// if muted, this holds the volume when not muted
static unsigned int VolPercent = 80;

// how much percent to increment/decrement volume by
static unsigned int VolDelta = 5;

// the actual volume passed into SDL, range of 0..SDL_MIX_MAXVOLUME
static unsigned int VolSDL = SDL_MIX_MAXVOLUME;

// Muted or not
static unsigned int VolIsMuted = 0;

//which type of volume control to use
static int VolumeControlType = VOLUME_TYPE_SDL;

static unsigned int OutputFreq;

// Prototype of local functions
static void my_audio_callback(void *userdata, unsigned char *stream, int len);
static void InitializeAudio(int freq);
static void ReadConfig(void);
static void InitializeSDL(void);
static void CreatePrimaryBuffer();

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
	ConfigSetDefaultFloat(l_ConfigAudio, "Version",             CONFIG_PARAM_VERSION,  "Mupen64Plus RPI Audio Plugin config parameter version number");
	ConfigSetDefaultInt(l_ConfigAudio, "DEFAULT_FREQUENCY",     DEFAULT_FREQUENCY,     "Frequency which is used if rom doesn't want to change it");
	ConfigSetDefaultInt(l_ConfigAudio, "DEFAULT_MODE",     	    DEFAULT_MODE,          "Audio Output Frequncy mode: 0 = Rom Frequency, 1 = Standard frequency that less than or equal to Rom Frequency, [N] the frequency output to use");
	ConfigSetDefaultBool(l_ConfigAudio, "SWAP_CHANNELS",        0,                     "Swaps left and right channels");
	ConfigSetDefaultInt(l_ConfigAudio, "SECONDARY_BUFFER_SIZE", SECONDARY_BUFFER_SIZE, "Number of output samples per Audio callback. This is SDL's hardware buffer.");
    ConfigSetDefaultInt(l_ConfigAudio, "PRIMARY_BUFFER_MULTIPLE",   PRIMARY_BUFFER_MULTIPLE,   "Size of buffer in terms of N * SECONDARY_BUFFER_SIZE. This is where audio is loaded after it's extracted from n64's memory.");
	ConfigSetDefaultInt(l_ConfigAudio, "PRIMARY_BUFFER_TARGET", PRIMARY_BUFFER_TARGET, "The desired amount of data in the buffer in terms of N * SECONDARY_BUFFER_SIZE" );
	ConfigSetDefaultInt(l_ConfigAudio, "VOLUME_CONTROL_TYPE",   VOLUME_TYPE_SDL,       "Volume control type: 1 = SDL (only affects Mupen64Plus output)  2 = OSS mixer (adjusts master PC volume)");
	ConfigSetDefaultInt(l_ConfigAudio, "VOLUME_ADJUST",         5,                     "Percentage change each time the volume is increased or decreased");
	ConfigSetDefaultInt(l_ConfigAudio, "VOLUME_DEFAULT",        80,                    "Default volume when a game is started.  Only used if VOLUME_CONTROL_TYPE is 1");

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
	pBuffer 			= 0;
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

/*
 * AiLenChanged is called by the Emulator Core however this Audio plugin will perform the resampling here so that when
 * the Audio Callback is run, Data can just be copied into the buffer.
 */
EXPORT void CALL AiLenChanged( void )
{
	unsigned int LenReg,AudioBytes;
	volatile unsigned char *p;
	int oldsamplerate, newsamplerate;
	
	newsamplerate = OutputFreq * 100 / speed_factor;
	oldsamplerate = GameFreq;

	if (critical_failure == 1) return;
	if (!l_PluginInit) return;
	if (pBuffer == 0)  return;

	LenReg = *AudioInfo.AI_LEN_REG;
	AudioBytes = ((LenReg * newsamplerate) / oldsamplerate)&0xFFFC;
	p = AudioInfo.RDRAM + (*AudioInfo.AI_DRAM_ADDR_REG & 0xFFFFFF);

	DebugMessage(M64MSG_VERBOSE, "AiLenChanged() buffer = %d to %d (%d), LenReg = %d, +bytes? = %d", uiBufferStart, uiBufferEnd, uiBufferSize, LenReg, AudioBytes );

	// Are we not going to overflow?
	if ( (uiBufferSize + (LenReg * newsamplerate) / oldsamplerate) > uiBufferMultiple * uiSecondaryBufferSize * SDL_SAMPLE_BYTES)
	{
		unsigned int ExtraSpace = (LenReg / uiSecondaryBufferSize);

		uiBufferMultiple += ExtraSpace;
		CreatePrimaryBuffer();
		DebugMessage(M64MSG_WARNING, "AiLenChanged(): Audio buffer to small. Increased PRIMARY_BUFFER_MULTIPLE to %d", uiBufferMultiple);
	}
	
	SDL_LockAudio();

	if (newsamplerate > oldsamplerate) // This is SLOW but the emulation should be running at < 100% game speed unless defaults over-ridden
	{
		int j = 0;
		int sldf = oldsamplerate;
		int const2 = 2*sldf;
		int dldf = newsamplerate;
		int const1 = const2 - 2*dldf;
		int criteria = const2 - dldf;

		while (j < LenReg)
		{
			if(bSwapChannels == 0)
			{
				// Left channel
				pBuffer[ uiBufferEnd ]     = p[ j + 2 ];
				pBuffer[ uiBufferEnd + 1 ] = p[ j + 3 ];

				// Right channel
				pBuffer[ uiBufferEnd + 2 ] = p[ j ];
				pBuffer[ uiBufferEnd + 3 ] = p[ j + 1 ];
			} else {
				// Left channel
				pBuffer[ uiBufferEnd ]     = p[ j ];
				pBuffer[ uiBufferEnd + 1 ] = p[ j + 1 ];

				// Right channel
				pBuffer[ uiBufferEnd + 2 ] = p[ j + 2 ];
				pBuffer[ uiBufferEnd + 3 ] = p[ j + 3 ];
			}

			if(criteria >= 0)
			{
				j+=4;
				criteria += const1;
			}
			else
			{
				criteria += const2;
			}

			uiBufferEnd += 4;
			uiBufferSize += 4;
			if (uiBufferEnd > uiBufferSizeMax) uiBufferEnd -= uiBufferSizeMax;
		}
	}
	else if (newsamplerate == oldsamplerate)
	{
		if(bSwapChannels == 0)
   		{	
			static unsigned char carryover[2] = {0, 0};
			// Not quite the same (one channel ahead of other by one sample) but is not noticable
			pBuffer[ uiBufferEnd ] = carryover[0];
			pBuffer[ uiBufferEnd + 1 ] = carryover[1];
			
			memcpy(&pBuffer[ uiBufferEnd + 2 ], (const char*)p, LenReg - 2);
			
			carryover[0] = p[ LenReg - 2 ];
			carryover[1] = p[ LenReg - 1 ];
   		}
		else 
		{
			memcpy(&pBuffer[ uiBufferEnd ], (const char*)p, LenReg);
   		}

		uiBufferEnd += LenReg;
		uiBufferSize += LenReg;
		if (uiBufferEnd > uiBufferSizeMax) uiBufferEnd -= uiBufferSizeMax;
	}
	else // newsamplerate < oldsamplerate, this only happens when speed_factor > 1
	{
		int j = 0;
	
		while (j < LenReg )
		{
			if(bSwapChannels == 0)
			{
				// Left channel
				pBuffer[ uiBufferEnd ]     = p[ j + 2 ];
				pBuffer[ uiBufferEnd + 1 ] = p[ j + 3 ];

				// Right channel
				pBuffer[ uiBufferEnd + 2 ] = p[ j ];
				pBuffer[ uiBufferEnd + 3 ] = p[ j + 1 ];
			} else {
				// Left channel
				pBuffer[ uiBufferEnd ] = p[ j ];
				pBuffer[ uiBufferEnd + 1 ] = p[ j + 1 ];

				// Right channel
				pBuffer[ uiBufferEnd + 2 ] = p[ j + 2 ];
				pBuffer[ uiBufferEnd + 3 ] = p[ j + 3 ];
			}
			j += 4 * oldsamplerate / newsamplerate;
			uiBufferEnd += 4;
			uiBufferSize += 4;
			if (uiBufferEnd > uiBufferSizeMax) uiBufferEnd -= uiBufferSizeMax;
		}
	}

	SDL_UnlockAudio();
	//DebugMessage(M64MSG_VERBOSE, "AiLenChanged() now buffer = %d to %d (%d)", uiBufferStart, uiBufferEnd, uiBufferSize);

	if (uiBufferSize >  uiBufferTargetMultiple * uiSecondaryBufferSize * SDL_SAMPLE_BYTES) 
	{
		unsigned int WaitTime =  (1000 *(uiBufferSize - uiBufferTargetMultiple * uiSecondaryBufferSize) / SDL_SAMPLE_BYTES) / OutputFreq;
		DebugMessage(M64MSG_VERBOSE, "    AiLenChanged(): Waiting %ims", WaitTime);
		if (l_PausedForSync)
			SDL_PauseAudio(0);
		l_PausedForSync = 0;
		SDL_Delay(WaitTime);
	}
	/* Or if the expected level of the primary buffer is less than the secondary buffer size
       (ie, predicting an underflow), then pause the audio to let the emulator catch up to speed */
	else if (uiBufferSize < uiSecondaryBufferSize * SDL_SAMPLE_BYTES)
	{
		DebugMessage(M64MSG_VERBOSE, "    AiLenChanged(): Possible underflow at next audio callback; pausing playback");
		if (!l_PausedForSync) SDL_PauseAudio(1);
		l_PausedForSync = 1;
	}
	/* otherwise the predicted buffer level is within our tolerance, so everything is okay */
	else
	{
		if (l_PausedForSync) SDL_PauseAudio(0);
		l_PausedForSync = 0;
	}
}

EXPORT int CALL InitiateAudio( AUDIO_INFO Audio_Info )
{
	if (!l_PluginInit)
		return 0;

	AudioInfo = Audio_Info;
	return 1;
}

static int underrun_count = 0;

static void my_audio_callback(void *userdata, unsigned char *stream, int len)
{
	static unsigned int successfullCallbacks = 0;

	if (pBuffer == 0)  return;
	if (!l_PluginInit) return;

	/* mark the time, for synchronization on the input side */
	last_callback_ticks = SDL_GetTicks();
	SDL_LockAudio();
	if (uiBufferSize >= len)
	{
		//Adjust for volume
		DebugMessage(M64MSG_VERBOSE, "%03i my_audio_callback: used %i samples %d to %d (%d)",
				last_callback_ticks % 1000, len / SDL_SAMPLE_BYTES, uiBufferStart, uiBufferEnd, uiBufferSize);
		SDL_MixAudio(stream, &pBuffer[uiBufferStart], len, VolSDL);
		
		uiBufferStart += len;
		if (uiBufferStart > uiBufferSizeMax) uiBufferStart -= uiBufferSizeMax;

		uiBufferSize -= len;

		DebugMessage(M64MSG_VERBOSE, "%03i my_audio_callback now: used %i samples %d to %d (%d)",
				last_callback_ticks % 1000, len / SDL_SAMPLE_BYTES, uiBufferStart, uiBufferEnd, uiBufferSize);

		successfullCallbacks++;
	}
	else
	{
		underrun_count++;
		DebugMessage(M64MSG_WARNING, "%03i Buffer underflow (%i).  %i samples present, %i needed", // M64MSG_VERBOSE
				last_callback_ticks % 1000, underrun_count, uiBufferSize/SDL_SAMPLE_BYTES, (len - uiBufferSize)/SDL_SAMPLE_BYTES
//, successfullCallbacks * 2048 + uiBufferSize/SDL_SAMPLE_BYTES
);

		//Dont give old sound to Audio
		memset(stream , 0, len);

		successfullCallbacks =0;
	}
	SDL_UnlockAudio();
}

EXPORT int CALL RomOpen(void)
{
	if (!l_PluginInit) return 0;

	ReadConfig();
	InitializeAudio(GameFreq);
	return 1;
}

static void InitializeSDL(void)
{
	DebugMessage(M64MSG_INFO, "Initializing RPI audio subsystem...");

	if(SDL_Init(SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0)
	{
		DebugMessage(M64MSG_ERROR, "Failed to initialize RPI audio subsystem; forcing exit.\n");
		critical_failure = 1;
		return;
	}
	critical_failure = 0;
}

static void CreatePrimaryBuffer()
{
	unsigned int Size = uiBufferMultiple * SECONDARY_BUFFER_SIZE * SDL_SAMPLE_BYTES;
	
	if (pBuffer == NULL)
	{
		DebugMessage(M64MSG_VERBOSE, "Allocating memory for audio buffer: %i bytes.", Size);
		pBuffer = (unsigned char*) malloc(Size);
		uiBufferSizeMax = Size;
	}
	else if (Size > uiBufferSizeMax) /* primary buffer only grows; there's no point in shrinking it */
	{
		unsigned char *newPrimaryBuffer = (unsigned char*) malloc(Size);
		unsigned char *oldPrimaryBuffer = pBuffer;
		SDL_LockAudio();
		memcpy(newPrimaryBuffer, oldPrimaryBuffer, uiBufferSizeMax);
		
		pBuffer = newPrimaryBuffer;
		uiBufferSizeMax = Size;
		
		SDL_UnlockAudio();
		
		DebugMessage(M64MSG_VERBOSE, "Increased memory for audio buffer: %i bytes.", Size);
		
		free(oldPrimaryBuffer);
	}
}

static void InitializeAudio(int freq)
{
	SDL_AudioSpec *desired, *obtained;

	if(SDL_WasInit(SDL_INIT_AUDIO|SDL_INIT_TIMER) == (SDL_INIT_AUDIO|SDL_INIT_TIMER) )
	{
		DebugMessage(M64MSG_VERBOSE, "InitializeAudio(): RPI Audio sub-system already initialized.");
		SDL_PauseAudio(1);
		SDL_CloseAudio();
	}
	else
	{
		DebugMessage(M64MSG_VERBOSE, "InitializeAudio(): Initializing RPI Audio");
		DebugMessage(M64MSG_VERBOSE, "Primary buffer multiple: %i", uiBufferMultiple);
		DebugMessage(M64MSG_VERBOSE, "Primary target multiple: %i", uiBufferTargetMultiple);
		DebugMessage(M64MSG_VERBOSE, "Secondary buffer: %i output samples.", uiSecondaryBufferSize);
		InitializeSDL();
	}
	if (critical_failure == 1) return;
	GameFreq = freq; // This is important for the sync

	if(hardware_spec != NULL) 
	{
		free(hardware_spec);
		hardware_spec = NULL;
	}
	// Allocate space for SDL_AudioSpec
	desired = malloc(sizeof(SDL_AudioSpec));
	obtained = malloc(sizeof(SDL_AudioSpec));

	uiOutputFrequencyMode = ConfigGetParamInt(l_ConfigAudio, "DEFAULT_MODE");
DebugMessage(M64MSG_VERBOSE, "Output frequency Mode : %i.", uiOutputFrequencyMode);
	switch (uiOutputFrequencyMode)
	{
		case 0:
			OutputFreq = freq;
			break;
		case 1:
			if(freq >= 44100) OutputFreq = 44100;
			else if(freq >= 22050) OutputFreq = 22050;
			else OutputFreq = 11025;
			break;
		default: 
			if (uiOutputFrequencyMode >= 11025)
			{
				OutputFreq = uiOutputFrequencyMode;
			}
			else
			{
				OutputFreq = 22050; 
			}	
			break;
	}
	desired->freq = OutputFreq;

	DebugMessage(M64MSG_VERBOSE, "Requesting frequency: %iHz.", desired->freq);
	/* 16-bit signed audio */
	desired->format=AUDIO_S16SYS;
	DebugMessage(M64MSG_VERBOSE, "Requesting format: %i.", desired->format);
	/* Stereo */
	desired->channels=2;
	/* reload these because they gets re-assigned from SDL data below, and InitializeAudio can be called more than once */
	uiBufferMultiple = ConfigGetParamInt(l_ConfigAudio, "PRIMARY_BUFFER_MULTIPLE");
	uiBufferTargetMultiple = ConfigGetParamInt(l_ConfigAudio, "PRIMARY_BUFFER_TARGET");
	uiSecondaryBufferSize = ConfigGetParamInt(l_ConfigAudio, "SECONDARY_BUFFER_SIZE");
	
	desired->samples = uiSecondaryBufferSize;

	/* Our callback function */
	desired->callback = my_audio_callback;
	desired->userdata = NULL;

	/* Open the audio device */
	l_PausedForSync = 1;
	if (SDL_OpenAudio(desired, obtained) < 0)
	{
		DebugMessage(M64MSG_ERROR, "Couldn't open audio: %s", SDL_GetError());
		critical_failure = 1;
		return;
	}
	if (desired->format != obtained->format)
	{
		DebugMessage(M64MSG_WARNING, "Obtained audio format differs from requested.");
	}
	if (desired->freq != obtained->freq)
	{
		DebugMessage(M64MSG_WARNING, "Obtained frequency differs from requested.");
	}

	/* desired spec is no longer needed */
	free(desired);
	hardware_spec=obtained;

	/* allocate memory for audio buffers */
	OutputFreq = hardware_spec->freq;
	uiSecondaryBufferSize = hardware_spec->samples;

	if (uiBufferMultiple < 5) uiBufferMultiple = 5;
	if (uiBufferMultiple > 10000)
	{ 
		DebugMessage(M64MSG_VERBOSE, "Limiting PRIMARY_BUFFER_MULTIPLE to 10000");
		uiBufferMultiple = 10000;
	}
	
	if (uiBufferTargetMultiple < uiBufferMultiple) uiBufferTargetMultiple = uiBufferMultiple + 2;
	if (uiBufferTargetMultiple > 9000)
	{ 
		DebugMessage(M64MSG_VERBOSE, "Limiting PRIMARY_BUFFER_TARGET to 1500");
		uiBufferTargetMultiple = 9000;
	}

	CreatePrimaryBuffer();
	
	/* preset the last callback time */
	if (last_callback_ticks == 0)
		last_callback_ticks = SDL_GetTicks();

	DebugMessage(M64MSG_VERBOSE, "Frequency: %i", hardware_spec->freq);
	DebugMessage(M64MSG_VERBOSE, "Format: %i", hardware_spec->format);
	DebugMessage(M64MSG_VERBOSE, "Channels: %i", hardware_spec->channels);
	DebugMessage(M64MSG_VERBOSE, "Silence: %i", hardware_spec->silence);
	DebugMessage(M64MSG_VERBOSE, "Samples: %i", hardware_spec->samples);
	DebugMessage(M64MSG_VERBOSE, "Size: %i", hardware_spec->size);

	/* set playback volume */
#if defined(HAS_OSS_SUPPORT)
	if (VolumeControlType == VOLUME_TYPE_OSS)
	{
		VolPercent = volGet();
	}
	else
#endif
{
		VolSDL = SDL_MIX_MAXVOLUME * VolPercent / 100;
}

}
EXPORT void CALL RomClosed( void )
{
	if (!l_PluginInit)
		return;
	if (critical_failure == 1)
		return;
	DebugMessage(M64MSG_VERBOSE, "Cleaning up RPI sound plugin...");

	// Shut down SDL Audio output
	SDL_PauseAudio(1);
	SDL_CloseAudio();

	// Delete the buffer, as we are done producing sound
	if (pBuffer != NULL)
	{
		uiBufferSizeMax = 0;
		free(pBuffer);
		pBuffer = NULL;
	}
	
	// Delete the hardware spec struct
	if(hardware_spec != NULL)
	{	
		free(hardware_spec);
		hardware_spec = NULL;
	}
	hardware_spec = NULL;

	// Shutdown the respective subsystems
	if(SDL_WasInit(SDL_INIT_AUDIO) != 0) SDL_QuitSubSystem(SDL_INIT_AUDIO);
	if(SDL_WasInit(SDL_INIT_TIMER) != 0) SDL_QuitSubSystem(SDL_INIT_TIMER);
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
	GameFreq = 				ConfigGetParamInt(l_ConfigAudio, "DEFAULT_FREQUENCY");
	bSwapChannels = 		ConfigGetParamBool(l_ConfigAudio, "SWAP_CHANNELS");
	uiBufferMultiple = 		ConfigGetParamInt(l_ConfigAudio, "PRIMARY_BUFFER_MULTIPLE");
	uiBufferTargetMultiple = ConfigGetParamInt(l_ConfigAudio, "PRIMARY_BUFFER_TARGET");
	uiSecondaryBufferSize = ConfigGetParamInt(l_ConfigAudio, "SECONDARY_BUFFER_SIZE");
	VolumeControlType = 	ConfigGetParamInt(l_ConfigAudio, "VOLUME_CONTROL_TYPE");
	VolDelta = 				ConfigGetParamInt(l_ConfigAudio, "VOLUME_ADJUST");
	VolPercent = 			ConfigGetParamInt(l_ConfigAudio, "VOLUME_DEFAULT");
}

// Returns the most recent ummuted volume level.
static int VolumeGetUnmutedLevel(void)
{
#if defined(HAS_OSS_SUPPORT)
	// reload volume if we're using OSS
	if (!VolIsMuted && VolumeControlType == VOLUME_TYPE_OSS)
	{
		return volGet();
	}
#endif

	return VolPercent;
}

// Sets the volume level based on the contents of VolPercent and VolIsMuted
static void VolumeCommit(void)
{
	int levelToCommit = VolIsMuted ? 0 : VolPercent;

#if defined(HAS_OSS_SUPPORT)
	if (VolumeControlType == VOLUME_TYPE_OSS)
	{
		//OSS mixer volume
		volSet(levelToCommit);
	}
	else
#endif
	{
		VolSDL = SDL_MIX_MAXVOLUME * levelToCommit / 100;
	}
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


