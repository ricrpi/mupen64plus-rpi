#ifndef GLN64_H
#define GLN64_H

#include "m64p_config.h"
#include "stdio.h"
#include "m64p_vidext.h"

#ifndef min
#define min(a,b) ((a) < (b) ? (a) : (b))
#endif

//#define DEBUG

#define PLUGIN_NAME     "gles2n64"
#define PLUGIN_VERSION  0x000005
#define PLUGIN_API_VERSION 0x020200

extern ptr_ConfigGetSharedDataFilepath 	ConfigGetSharedDataFilepath;
extern ptr_VidExt_GL_SwapBuffers       	CoreVideo_GL_SwapBuffers;
extern ptr_VidExt_SetVideoMode		CoreVideo_SetVideoMode;
extern ptr_VidExt_Quit                  CoreVideo_Quit;

extern void (*CheckInterrupts)( void );
extern void (*renderCallback)();


#endif

