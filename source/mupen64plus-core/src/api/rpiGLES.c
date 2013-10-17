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
#include <bcm_host.h>

#include <SDL/SDL_keysym.h>

#define DEBUG_PRINT(...) printf(__VA_ARGS__)

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
static void (*PauseCallback)(int) = NULL;

///////////////////////////////////////////////////////////////////////////////////////////////

#include "unistd.h"
#include "linux/kd.h"
#include "termios.h"
#include "fcntl.h"
#include "sys/ioctl.h"

#include <signal.h>

static struct termios tty_attr_old;
static int old_keyboard_mode;
static int bRawKeyboard =0;

static int setupKeyboard()
{
    struct termios tty_attr;
    int flags;

    /* make stdin non-blocking */
    flags = fcntl(0, F_GETFL);
    flags |= O_NONBLOCK;
    fcntl(0, F_SETFL, flags);

    /* save old keyboard mode */
    if (ioctl(0, KDGKBMODE, &old_keyboard_mode) < 0) {
	DEBUG_PRINT("Could not change keyboard mode");
	return 0;
    }

    tcgetattr(0, &tty_attr_old);

    /* turn off buffering, echo and key processing */
    tty_attr = tty_attr_old;
    tty_attr.c_lflag &= ~(ICANON | ECHO | ISIG);
    tty_attr.c_iflag &= ~(ISTRIP | INLCR | ICRNL | IGNCR | IXON | IXOFF);
    tcsetattr(0, TCSANOW, &tty_attr);

    ioctl(0, KDSKBMODE, K_RAW);
	bRawKeyboard = 1;
	DEBUG_PRINT("Setup keyboard in RAW mode\n");
    return 1;
}

static void restoreKeyboard()
{
    tcsetattr(0, TCSAFLUSH, &tty_attr_old);
    ioctl(0, KDSKBMODE, old_keyboard_mode);
}

///////////////////////////////////////////////////////////////////////////////////////////////

void RPI_SetPauseCallback(void (*callback)(int))
{
	PauseCallback = callback;
}

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

   		if (NULL != PauseCallback) PauseCallback(bPause);
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
		if (NULL != PauseCallback) PauseCallback(bPause);
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

void restKeyboard(int val)
{
	DEBUG_PRINT("signal %d, restoring keyboard\n", val);
	restoreKeyboard();
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

	if (!bUsingXwindow)
	{
		// we want the keyboard returned to normal if something goes wrong
		signal(SIGTERM, &restKeyboard);
		signal(SIGSEGV, &restKeyboard);
		signal(SIGKILL, &restKeyboard);

		//change keyboard to raw mode
		setupKeyboard();
	}
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
	if (!bUsingXwindow) restoreKeyboard();

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
	else if (bRawKeyboard) // there is no X window.
	{
		// http://www.win.tue.nl/~aeb/linux/kbd/scancodes-1.html
		// http://wiki.libsdl.org/SDL_Keymod?highlight=%28\bCategoryEnum\b%29|%28CategoryKeyboard%29

		//DEBUG_PRINT("Check for keyboard event\n");
		char buf[] = {0,0};
    	int res;
		int byteToRead =0;

		/* read scan code from stdin */
		res = read(0, &buf[0], 1);
		/* keep reading til there's no more*/
		if (res > 0) 
		{	
			xEvent->xkey.state = 0;

			DEBUG_PRINT("keyboard input: %d, %d %d\n",res, buf[0], buf[1]);
			if (buf[0] == 0xe0)
			{
				byteToRead = 1;
				switch (buf[1])
				{	
					case 0x1d: xEvent->xkey.state = KMOD_RCTRL 		break;
					case 0x38: xEvent->xkey.state = KMOD_RALT     	break;
					case 0x5b: xEvent->xkey.state = KMOD_LGUI     	break;
					case 0x5c: xEvent->xkey.state = KMOD_RGUI    	break;
				}

			} 

			if ( buf[byteToRead] & 0x80 )
			{
				xEvent->type = KeyPress;
			}
			else
			{
				xEvent->type = KeyRelease;
			}

			xEvent->xkey.keycode = buf[byteToRead]&0x7F;

			if (0 == byteToRead)
			{	
				switch (buf[1]&0x7F)
				{	
					case 0x1d: xEvent->xkey.state = KMOD_LCTRL   	break;
					case 0x2a: xEvent->xkey.state = KMOD_LSHIFT  	break;
					case 0x36: xEvent->xkey.state = KMOD_RSHIFT  	break;
					case 0x38: xEvent->xkey.state = KMOD_LALT      	break;
					case 0x32: xEvent->xkey.state = KMOD_CAPS;   	break;
					case 0x45: xEvent->xkey.state = KMOD_NUM;   	break;
				}
			}

			return 1;
		}
	}
	else  // remote ssh or in terminal or X window broken
	{
		DEBUG_PRINT("Don't know how to handle input\n");
	}

	//catch all
	return 0;
}

void RPI_SwapBuffers()
{
	eglSwapBuffers ( egl_display, egl_surface );
}
