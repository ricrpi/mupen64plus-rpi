/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus-core - api/vidext.c                                       *
 *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
 *   Copyright (C) 2009 Richard Goedeken                                   *
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
                       
/* This file contains the Core video extension functions which will be exported
 * outside of the core library.
 */

#include <stdlib.h>
#include <string.h>
#include <SDL.h>

#define M64P_CORE_PROTOTYPES 1
#include "m64p_types.h"
#include "m64p_vidext.h"
#include "rpiGLES.h"

#include "r4300/r4300.h" 	// for global rompause
#include "callbacks.h"
#include "../osd/osd.h"

#if SDL_VERSION_ATLEAST(2,0,0)
#include "vidext_sdl2_compat.h"
#endif

#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include <EGL/eglplatform.h>
//#include "esUtil.h"

//

/* local variables */
static m64p_video_extension_functions l_ExternalVideoFuncTable = {10, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
static int l_VideoExtensionActive = 0;
static int l_VideoOutputActive = 0;
static int l_Fullscreen = 0;

/* global function for use by frontend.c */
m64p_error OverrideVideoFunctions(m64p_video_extension_functions *VideoFunctionStruct)
{
    /* check input data */
    if (VideoFunctionStruct == NULL)
        return M64ERR_INPUT_ASSERT;
    if (VideoFunctionStruct->Functions < 11)
        return M64ERR_INPUT_INVALID;

    /* disable video extension if any of the function pointers are NULL */
    if (VideoFunctionStruct->VidExtFuncInit == NULL ||
        VideoFunctionStruct->VidExtFuncQuit == NULL ||
        VideoFunctionStruct->VidExtFuncListModes == NULL ||
        VideoFunctionStruct->VidExtFuncSetMode == NULL ||
        VideoFunctionStruct->VidExtFuncGLGetProc == NULL ||
        VideoFunctionStruct->VidExtFuncGLSetAttr == NULL ||
        VideoFunctionStruct->VidExtFuncGLGetAttr == NULL ||
        VideoFunctionStruct->VidExtFuncGLSwapBuf == NULL ||
        VideoFunctionStruct->VidExtFuncSetCaption == NULL ||
        VideoFunctionStruct->VidExtFuncToggleFS == NULL ||
        VideoFunctionStruct->VidExtFuncResizeWindow == NULL)
    {
        l_ExternalVideoFuncTable.Functions = 11;
        memset(&l_ExternalVideoFuncTable.VidExtFuncInit, 0, 11 * sizeof(void *));
        l_VideoExtensionActive = 0;
        return M64ERR_SUCCESS;
    }

    /* otherwise copy in the override function pointers */
    memcpy(&l_ExternalVideoFuncTable, VideoFunctionStruct, sizeof(m64p_video_extension_functions));
    l_VideoExtensionActive = 1;
    return M64ERR_SUCCESS;
}

int VidExt_InFullscreenMode(void)
{
    return l_Fullscreen;
}

int VidExt_VideoRunning(void)
{
    return l_VideoOutputActive;
}

/* video extension functions to be called by the video plugin */
EXPORT m64p_error CALL VidExt_Init(void)
{
    /* call video extension override if necessary */
    /*if (l_VideoExtensionActive)
        return (*l_ExternalVideoFuncTable.VidExtFuncInit)();

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_NOPARACHUTE);

    if (SDL_InitSubSystem(SDL_INIT_VIDEO) == -1)
    {
        DebugMessage(M64MSG_ERROR, "SDL video subsystem init failed: %s", SDL_GetError());
        return M64ERR_SYSTEM_FAIL;
    }
*/
    return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL VidExt_Quit(void)
{
    RPI_CloseWindow();
    return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL VidExt_ListFullscreenModes(m64p_2d_size *SizeArray, int *NumSizes)
{/*
    const SDL_VideoInfo *videoInfo;
    unsigned int videoFlags;
    SDL_Rect **modes;
    int i;

    // call video extension override if necessary 
    if (l_VideoExtensionActive)
        return (*l_ExternalVideoFuncTable.VidExtFuncListModes)(SizeArray, NumSizes);

    if (!SDL_WasInit(SDL_INIT_VIDEO))
        return M64ERR_NOT_INIT;

    // get a list of SDL video modes 
    videoFlags = SDL_OPENGL | 
SDL_FULLSCREEN;

    if ((videoInfo = SDL_GetVideoInfo()) == NULL)
    {
        DebugMessage(M64MSG_ERROR, "SDL_GetVideoInfo query failed: %s", SDL_GetError());
        return M64ERR_SYSTEM_FAIL;
    }

    if(videoInfo->hw_available)
        videoFlags |= SDL_HWSURFACE;
    else
        videoFlags |= SDL_SWSURFACE;

    modes = SDL_ListModes(NULL, videoFlags);

    if (modes == (SDL_Rect **) 0 || modes == (SDL_Rect **) -1)
    {
        DebugMessage(M64MSG_WARNING, "No fullscreen SDL video modes available");
        *NumSizes = 0;
        return M64ERR_SUCCESS;
    }

    i = 0;
    while (i < *NumSizes && modes[i] != NULL)
    {
        SizeArray[i].uiWidth  = modes[i]->w;
        SizeArray[i].uiHeight = modes[i]->h;
        i++;
    }

    *NumSizes = i;
*/
    return M64ERR_SUCCESS;
}

void PauseState(int value)
{
	rompause = value;
}

EXPORT m64p_error CALL VidExt_SetVideoMode(int Width, int Height, int BitsPerPixel, m64p_video_mode ScreenMode, m64p_video_flags Flags)
{
	RPI_OpenWindow("Mupen64plus", (unsigned int)Width, (unsigned int)Height, l_Fullscreen, 
	PointerMotionMask | ButtonPressMask | ButtonReleaseMask |
	KeyPressMask | KeyReleaseMask);
	
	RPI_SetPauseCallback(&PauseState);
	
	return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL VidExt_ResizeWindow(int Width, int Height)
{
    const SDL_VideoInfo *videoInfo;
    int videoFlags = 0;
 DebugMessage(M64MSG_INFO, "VidExt_ResizeWindow(%d, %d)", Width, Height);
    /* call video extension override if necessary */
    if (l_VideoExtensionActive)
    {
        m64p_error rval;
        // shut down the OSD
        osd_exit();
        // re-create the OGL context
        rval = (*l_ExternalVideoFuncTable.VidExtFuncResizeWindow)(Width, Height);
        if (rval == M64ERR_SUCCESS)
        {
            StateChanged(M64CORE_VIDEO_SIZE, (Width << 16) | Height);
            // re-create the On-Screen Display
            osd_init(Width, Height);
        }
        return rval;
    }

    if (!l_VideoOutputActive)
        return M64ERR_NOT_INIT;

    if (l_Fullscreen)
    {
        DebugMessage(M64MSG_ERROR, "VidExt_ResizeWindow() called in fullscreen mode.");
        return M64ERR_INVALID_STATE;
    }

    /* Get SDL video flags to use */
    videoFlags = SDL_OPENGL | 
SDL_RESIZABLE;
    if ((videoInfo = SDL_GetVideoInfo()) == NULL)
    {
        DebugMessage(M64MSG_ERROR, "SDL_GetVideoInfo query failed: %s", SDL_GetError());
        return M64ERR_SYSTEM_FAIL;
    }
    if (videoInfo->hw_available)
        videoFlags |= SDL_HWSURFACE;
    else
        videoFlags |= SDL_SWSURFACE;

    // destroy the On-Screen Display
    osd_exit();
    

    StateChanged(M64CORE_VIDEO_SIZE, (Width << 16) | Height);
    // re-create the On-Screen Display
    osd_init(Width, Height);
    return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL VidExt_SetCaption(const char *Title)
{
	RPI_ChangeTitle(Title);
    	// SDL_WM_SetCaption(Title, "M64+ Video");

    	return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL VidExt_ToggleFullScreen(void)
{
	l_Fullscreen = !l_Fullscreen;
	RPI_FullScreen(l_Fullscreen);
       StateChanged(M64CORE_VIDEO_MODE, l_Fullscreen ? M64VIDEO_FULLSCREEN : M64VIDEO_WINDOWED);
       return M64ERR_SUCCESS;

}

EXPORT void * CALL VidExt_GL_GetProcAddress(const char* Proc)
{
    /* call video extension override if necessary */
    if (l_VideoExtensionActive)
        return (*l_ExternalVideoFuncTable.VidExtFuncGLGetProc)(Proc); 

    return glXGetProcAddress(Proc);
}

typedef struct {
    m64p_GLattr m64Attr;
    SDL_GLattr sdlAttr;
} GLAttrMapNode;

static const GLAttrMapNode GLAttrMap[] = {
        { M64P_GL_DOUBLEBUFFER, SDL_GL_DOUBLEBUFFER },
        { M64P_GL_BUFFER_SIZE,  SDL_GL_BUFFER_SIZE },	
        { M64P_GL_DEPTH_SIZE,   SDL_GL_DEPTH_SIZE },	// GL_DEPTH_BITS
        { M64P_GL_RED_SIZE,     SDL_GL_RED_SIZE },		// GL_RED_BITS
        { M64P_GL_GREEN_SIZE,   SDL_GL_GREEN_SIZE },	// GL_GREEN_BITS
        { M64P_GL_BLUE_SIZE,    SDL_GL_BLUE_SIZE },		// GL_BLUE_BITS
        { M64P_GL_ALPHA_SIZE,   SDL_GL_ALPHA_SIZE },	// GL_ALPHA_BITS
#if SDL_VERSION_ATLEAST(1,3,0)
        { M64P_GL_SWAP_CONTROL, SDL_RENDERER_PRESENTVSYNC },
#else
        { M64P_GL_SWAP_CONTROL, SDL_GL_SWAP_CONTROL },
#endif
        { M64P_GL_MULTISAMPLEBUFFERS, SDL_GL_MULTISAMPLEBUFFERS },
        { M64P_GL_MULTISAMPLESAMPLES, SDL_GL_MULTISAMPLESAMPLES }};
static const int mapSize = sizeof(GLAttrMap) / sizeof(GLAttrMapNode);

EXPORT m64p_error CALL VidExt_GL_SetAttribute(m64p_GLattr Attr, int Value)
{
    int i;

    /* call video extension override if necessary */
    if (l_VideoExtensionActive)
        return (*l_ExternalVideoFuncTable.VidExtFuncGLSetAttr)(Attr, Value);

    if (!SDL_WasInit(SDL_INIT_VIDEO))
        return M64ERR_NOT_INIT;

    for (i = 0; i < mapSize; i++)
    {
        if (GLAttrMap[i].m64Attr == Attr)
        {
           // if (SDL_GL_SetAttribute(GLAttrMap[i].sdlAttr, Value) != 0)
             //   return M64ERR_SYSTEM_FAIL;
            return M64ERR_SUCCESS;
        }
    }

    return M64ERR_INPUT_INVALID;
}

EXPORT m64p_error CALL VidExt_GL_GetAttribute(m64p_GLattr Attr, int *pValue)
{
    int i;

    /* call video extension override if necessary */
    if (l_VideoExtensionActive)
        return (*l_ExternalVideoFuncTable.VidExtFuncGLGetAttr)(Attr, pValue);

    if (!SDL_WasInit(SDL_INIT_VIDEO))
        return M64ERR_NOT_INIT;

    for (i = 0; i < mapSize; i++)
    {
        if (GLAttrMap[i].m64Attr == Attr)
        {
            int NewValue = 0;
            //if (glGet(GLAttrMap[i].sdlAttr, &NewValue) != 0)
             //   return M64ERR_SYSTEM_FAIL;
            *pValue = NewValue;
            return M64ERR_SUCCESS;
        }
    }

    return M64ERR_INPUT_INVALID;
}

EXPORT m64p_error CALL VidExt_GL_SwapBuffers(void)
{
/*
	static uint32_t count	= 0;
	static uint32_t sum		= 0, sum2 = 0;
	static uint32_t time =0 , time2 =0;

	time2 = SDL_GetTicks();
	sum2 += (time2 - time);
	//Draw(&esContext);
    // call video extension override if necessary 
 */   
	if (l_VideoExtensionActive)
        return (*l_ExternalVideoFuncTable.VidExtFuncGLSwapBuf)();

    //eglSwapBuffers(esContext.eglDisplay, esContext.eglSurface);
	RPI_SwapBuffers();
	
/*
	time = SDL_GetTicks();
	
	count ++;
	sum += (time - time2);
	
	if (count >= 32)
	{	
		DebugMessage(M64MSG_INFO, "%d eglSwapBuffers() took %d ms on average. %dms", time, sum/count, sum2/count);
		count = 0;
		sum   = 0;
		sum2  = 0;
	}
*/
    return M64ERR_SUCCESS;
}

