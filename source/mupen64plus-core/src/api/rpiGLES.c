/* Created by exoticorn ( http://talk.maemo.org/showthread.php?t=37356 )
 * edited and commented by André Bergner [endboss]
 *
 * libraries needed:   libx11-dev, libgles2-dev
 *
 * compile with:   g++  -lX11 -lEGL -lGLESv2  egl-example.cpp
 */
 
#include <stdio.h>

#include <sys/time.h>
 
#include "rpiGLES.h"

#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include <pthread.h>	
#include <bcm_host.h>


//#define DEBUG_PRINT(...) printf(__VA_ARGS__)

#ifndef DEBUG_PRINT
#define DEBUG_PRINT(...)
#endif

// Dispmanx variables
static DISPMANX_ELEMENT_HANDLE_T dispman_element;
static DISPMANX_DISPLAY_HANDLE_T dispman_display;
static DISPMANX_UPDATE_HANDLE_T dispman_update;
static EGL_DISPMANX_WINDOW_T nativewindow;

// X11 variables
static Display    *x_display;
static Window root, win;

// EGL variables
static EGLDisplay  egl_display;
static EGLContext  egl_context;
static EGLSurface  egl_surface;

//internal variables
static VC_RECT_T src_rect, dest_rect;
static unsigned int bPaused=1;
static unsigned int bFullScreened=0;
static uint32_t uiXflags=0; 
static unsigned int bUsingXwindow=0;

///////////////////////////////////////////////////////////////////////////////////////////////

void RPI_Pause(unsigned int bPause)
{	
	if (!bUsingXwindow) return;
	
	if (bPause && !bPaused)
	{
		DEBUG_PRINT("Pausing\n");
		XUngrabPointer(x_display,CurrentTime);
		
		VC_RECT_T dummy_rect;
		dummy_rect.x = 0;
	   	dummy_rect.y = 0;
	   	dummy_rect.width = 1;
	   	dummy_rect.height = 1;

		dispman_update = vc_dispmanx_update_start( 0 /* Priority*/);
	DEBUG_PRINT("%d RPI Window at %d,%d %dx%d\n", __LINE__, dest_rect.x, dest_rect.y, dest_rect.width, dest_rect.height);

		vc_dispmanx_element_change_attributes( dispman_update, dispman_element, 0, 
			0, 255, &dummy_rect, &src_rect, DISPMANX_PROTECTION_NONE,(DISPMANX_TRANSFORM_T)0 );
   
   		vc_dispmanx_update_submit_sync( dispman_update );
	}
	else if (!bPause && bPaused)
	{
		DEBUG_PRINT("Unpausing\n");
		XGrabPointer(x_display,DefaultRootWindow(x_display), 1, 	
		ButtonPressMask | ButtonReleaseMask |PointerMotionMask |
		FocusChangeMask | EnterWindowMask | FocusChangeMask, //| LeaveWindowMask, 
		GrabModeAsync,GrabModeAsync, win, None, CurrentTime);
		
		if (bFullScreened)
		{
			RPI_FullScreen(1);
		}
		else
		{
			RPI_FullScreen(0);
		}
	}	
	bPaused = bPause;
}

static int RPI_OpenDispmanx(unsigned int uiWidth, unsigned int uiHeight)
{
	VC_DISPMANX_ALPHA_T dispman_alpha = {DISPMANX_FLAGS_ALPHA_FIXED_ALL_PIXELS,255,0};

	bcm_host_init();
	 
   	src_rect.x 		= 0;
   	src_rect.y 		= 0;
   	src_rect.width 	= uiWidth	<< 16;
   	src_rect.height = uiHeight 	<< 16;   
	
	if (bUsingXwindow)	//if using X11 do not go full screen. Resize will take place later
	{
		dest_rect.x = 0;
		dest_rect.y = 0;
		dest_rect.width = 1;
		dest_rect.height = 1;
	}

	DEBUG_PRINT("%d RPI Window at %d,%d %dx%d\n", __LINE__, dest_rect.x, dest_rect.y, dest_rect.width, dest_rect.height);
   	
   	dispman_display = vc_dispmanx_display_open(0);

   	dispman_update = vc_dispmanx_update_start(0);
	
   	dispman_element = vc_dispmanx_element_add ( dispman_update, dispman_display,0/*layer*/, &dest_rect, 
		0 /*src*/,&src_rect, DISPMANX_PROTECTION_NONE, &dispman_alpha, 0, (DISPMANX_TRANSFORM_T)0);

   	vc_dispmanx_update_submit_sync( dispman_update );
    
	nativewindow.element = dispman_element;
   	nativewindow.width = uiWidth;
   	nativewindow.height = uiHeight;

   	return 0;
}

static int RPI_OpenXWindow(const char* sTitle, unsigned int uiWidth, unsigned int uiHeight, int bFullScreen, unsigned int Xflags)
{
	x_display = XOpenDisplay(NULL);
    if (NULL == x_display) return 1;

	root = DefaultRootWindow(x_display);
	win = XCreateSimpleWindow(x_display, root, 10, 10, uiWidth, uiHeight, 0, 0, 0);

	if (0 == win) return 2;
 
	XSetWindowAttributes swa;
	
	swa.event_mask = Xflags | StructureNotifyMask 
    // | ResizeRedirectMask | VisibilityChangeMask | ExposureMask
	;	
	uiXflags = swa.event_mask;
	
	XSelectInput (x_display, win, swa.event_mask);

	// make the window visible on the screen
	XMapWindow (x_display, win);
	XStoreName (x_display, win, sTitle);
	
	bUsingXwindow = 1;
	
	return 0;
}

static int RPI_OpenEGL_GLES2()
{ 
	egl_display  =  eglGetDisplay(EGL_DEFAULT_DISPLAY);
   	if ( egl_display == EGL_NO_DISPLAY ) {
		DEBUG_PRINT("No EGL display.\n");
		return 1;
	}
   
	EGLint majorVersion, minorVersion;
   
	if ( !eglInitialize( egl_display,  &majorVersion, &minorVersion ) ) {
      	DEBUG_PRINT("Unable to initialize EGL\n");
      return 1;
   	}

 	DEBUG_PRINT("EGL %d.%d Initialized\n", majorVersion, minorVersion);
   
	EGLint attr[] =
	{
       	EGL_RED_SIZE,       8,
       	EGL_GREEN_SIZE,     8,
       	EGL_BLUE_SIZE,      8,
       	EGL_ALPHA_SIZE,     8, 
       	EGL_DEPTH_SIZE,     24, 
       	EGL_STENCIL_SIZE,   EGL_DONT_CARE,
		EGL_SURFACE_TYPE, 	EGL_WINDOW_BIT,
	   //EGL_BIND_TO_TEXTURE_RGBA, EGL_TRUE, 
		EGL_SAMPLE_BUFFERS, 1,
		EGL_NONE
   };
 
   	EGLConfig  ecfg;
   	EGLint     num_config;
   	if ( !eglChooseConfig( egl_display, attr, &ecfg, 1, &num_config ) ) {
      	DEBUG_PRINT("Failed to choose config (eglError: %d)\n", eglGetError());
      	return 1;
   	}
 
   if ( num_config != 1 ) {
      DEBUG_PRINT("Didn't get exactly one config, but %d\n",num_config);
      return 1;
   }
 
   	egl_surface = eglCreateWindowSurface ( egl_display, ecfg, &nativewindow, NULL );
   	if ( egl_surface == EGL_NO_SURFACE ) {
      	DEBUG_PRINT("Unable to create EGL surface eglError: %d\n", eglGetError());
      	return 1;
   	}
 
   	//// egl-contexts collect all state descriptions needed required for operation
   	EGLint ctxattr[] = {
      	EGL_CONTEXT_CLIENT_VERSION, 2,
      	EGL_NONE
   	};
   	egl_context = eglCreateContext ( egl_display, ecfg, EGL_NO_CONTEXT, ctxattr );
   	if ( egl_context == EGL_NO_CONTEXT ) {
      	DEBUG_PRINT("Unable to create EGL context eglError: %d\n", eglGetError());
      	return 1;
   	}
 
   	//// associate the egl-context with the egl-surface
   	eglMakeCurrent( egl_display, egl_surface, egl_surface, egl_context );
     
	glViewport ( 0 , 0 , src_rect.width , src_rect.height );
	return 0;
}

int RPI_OpenWindow(const char* sTitle, unsigned int uiWidth, unsigned int uiHeight, int bFullScreen, unsigned int Xflags)
{
	DEBUG_PRINT("RPI_OpenWindow(\"%s\", %d, %d)\n", sTitle, uiWidth, uiHeight);

	if ( 0 < RPI_OpenXWindow(sTitle, uiWidth, uiHeight, bFullScreen, Xflags))
	{
		DEBUG_PRINT("Could not open X window\n");
		
		//make RPI_OpenDispmanx() go full screen with scaling
		dest_rect.x = 0;
		dest_rect.y = 0;
		dest_rect.width = 0;
		dest_rect.height =0;
	}	
	
	RPI_OpenDispmanx(uiWidth, uiHeight);

	RPI_OpenEGL_GLES2();

	RPI_Pause(0);

	DEBUG_PRINT("RPI_OpenWindow() finished\n");
	return 0;
}

int RPI_GetScreenSize(unsigned int *uiWidth, unsigned int *uiHeight)
{
	return graphics_get_display_size(0 /* LCD */, uiWidth, uiHeight);
}

int RPI_GetWindowSize(unsigned int *uiWidth, unsigned int *uiHeight)
{
	//if (uiWidth != NULL) *uiWidth  = dest_rect.width;
	//if (uiHeight!= NULL) *uiHeight = dest_rect.height;
	
	if (uiWidth != NULL) *uiWidth  = src_rect.width >> 16;	//return the src dimensions to allow scaling in dispmanx
	if (uiHeight!= NULL) *uiHeight = src_rect.height>> 16;
	return 0;
}

int RPI_FullScreen(unsigned int bFullscreen)
{
	DEBUG_PRINT("RPI_FullScreen(%d)\n", bFullscreen);
	if (bFullscreen)
	{
		bFullScreened = 1;
		VC_RECT_T dummy_rect;
		dummy_rect.x = 0;
	   	dummy_rect.y = 0;
	   	dummy_rect.width = 0;
	   	dummy_rect.height = 0;

		dispman_update = vc_dispmanx_update_start(0);
		DEBUG_PRINT("%d RPI Window at %d,%d %dx%d\n", __LINE__, dest_rect.x, dest_rect.y, dest_rect.width, dest_rect.height);

		vc_dispmanx_element_change_attributes( dispman_update, dispman_element, 0, 
			0, 255, &dummy_rect, &src_rect, DISPMANX_PROTECTION_NONE,(DISPMANX_TRANSFORM_T)0 );
   
   		vc_dispmanx_update_submit_sync( dispman_update );
	}
	else if (bUsingXwindow)	// we can goto an X window
	{
		bFullScreened = 0;
		dispman_update = vc_dispmanx_update_start( 0 /* Priority*/);
		DEBUG_PRINT("%d RPI Window at %d,%d %dx%d\n", __LINE__, dest_rect.x, dest_rect.y, dest_rect.width, dest_rect.height);

		vc_dispmanx_element_change_attributes( dispman_update, dispman_element, 0, 
			0, 255, &dest_rect, &src_rect, DISPMANX_PROTECTION_NONE,(DISPMANX_TRANSFORM_T)0 );
   
   		vc_dispmanx_update_submit_sync( dispman_update );
	}
	else
	{
		RPI_Pause(1);
	}
	return 0;
}

int RPI_ChangeTitle(const char* sTitle)
{
	if (!bUsingXwindow) return 1;
	
	DEBUG_PRINT("RPI_ChangeTitle(\"%s\")\n", sTitle);
	XStoreName (x_display, win, sTitle);
	return 0;
}

int RPI_CloseWindow()
{
	DEBUG_PRINT("RPI_CloseWindow\n");
	eglDestroyContext ( egl_display, egl_context );
   	eglDestroySurface ( egl_display, egl_surface );
   	eglTerminate      ( egl_display );
   	
	if (bUsingXwindow) 
	{
		XCloseDisplay     ( x_display );
	   	XDestroyWindow    ( x_display, root );
	}	
	return 0;
}

static int RPI_MoveScreen()
{
	//glViewport ( 0 , 0 , dest_rect.width , dest_rect.height );	//If changing the Viewport then must also change the src_rect
	if (bUsingXwindow)
	{
		dispman_update = vc_dispmanx_update_start(0);
		DEBUG_PRINT("%d RPI Window at %d,%d %dx%d\n", __LINE__, dest_rect.x, dest_rect.y, dest_rect.width, dest_rect.height);

		vc_dispmanx_element_change_attributes( dispman_update, dispman_element, 0, 0, 255, &dest_rect, 
			&src_rect, DISPMANX_PROTECTION_NONE, (DISPMANX_TRANSFORM_T)0 );
	   
	   	vc_dispmanx_update_submit_sync( dispman_update );
	}	
	return 0;	
}

int RPI_NextXEvent(XEvent* xEvent)
{
	if (bUsingXwindow)
	{
		if (XPending ( x_display ))
		{	
		     	XNextEvent( x_display, xEvent );
		     	
		     	switch (xEvent->type)
		     	{
		     		case FocusOut:
		     		DEBUG_PRINT("rpiGLES FocusOut\n");
					RPI_Pause(1);
	   				break;
				case ConfigureNotify:
		     		//case ResizeRequest:
		     		
		     		if (dest_rect.width != xEvent->xconfigure.width ||
		     			dest_rect.height != xEvent->xconfigure.height ||
		     			1 != xEvent->xconfigure.x)
		     		{
		     			DEBUG_PRINT("RPI Screen size changed\n");
		     			dest_rect.x = xEvent->xconfigure.x;
					dest_rect.y = xEvent->xconfigure.y;
					dest_rect.width = xEvent->xconfigure.width;
					dest_rect.height = xEvent->xconfigure.height;
					if (!bPaused) RPI_MoveScreen();
				}
			}
			return 1;
		}
		else
		{
			return 0;
		}
	}
	else // there is no X window. TODO get from stdin - would allow debugging remotely
	{
		
		return 0;
	}
}

void RPI_SwapBuffers()
{
	eglSwapBuffers ( egl_display, egl_surface );
}
