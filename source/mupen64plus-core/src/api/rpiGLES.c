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
#include "memory/dma.h"

//#define DEBUG_PRINT(...) printf(__VA_ARGS__)

#ifndef DEBUG_PRINT
#define DEBUG_PRINT(...)
#endif

/*
0,	1,	2,	3,	4,	5,	6,	7,	8,	9,	10,	11,	12,	13,	14,	15,
16,	17,	18,	19,	20,	21,	22,	23,	24,	25,	26,	27,	28,	29,	30,	31,
32,	33,	34,	35,	36,	37,	38,	39,	40,	41,	42,	43,	44,	45,	45,	46,
48,	49,	50,	51,	52,	53,	54,	55,	56,	57,	58,	59,	60,	61,	62,	63,
64,	65,	66,	67,	68,	69,	70,	71,	72,	73,	74,	75,	76,	77,	78,	79,
80,	81,	82,	83,	84,	85,	86,	87,	88,	89,	90,	91,	92,	93,	94,	95,
96,	97,	98,	99,	100,101,102,103,104,105,106,107,108,109,110,111,
112,113,114,115,116,117,118,119,120,121,122,123,124,125,126,127
*/

#define NA	(-1)

static const int RAWtoSDL[] = {
							  //0,			1,			2,			3,			4,			5,			6,			7,	
								NA,			SDLK_ESCAPE,SDLK_1,		SDLK_2,		SDLK_3,		SDLK_4,		SDLK_5,		SDLK_6,
							  //8,			9,			10,			11,			12,			13,			14,			15,	
								SDLK_7,		SDLK_8,		SDLK_9,		SDLK_0,		SDLK_MINUS,	SDLK_EQUALS,SDLK_BACKSPACE,	SDLK_TAB,
							  //16 (0x10),	17,			18,			19,			20,			21,			22,			23,	
								SDLK_q,		SDLK_w,		SDLK_e,		SDLK_r,		SDLK_t,		SDLK_y,		SDLK_u,		SDLK_i,
							  //24 (0x18),	25,			26,			27,			28,			29,			30,			31,		
								SDLK_o,		SDLK_p,		SDLK_LEFTBRACKET,SDLK_RIGHTBRACKET,	SDLK_RETURN,SDLK_LSHIFT,	SDLK_a,	SDLK_s,
							  //32 (0x20),	33,			34,			35,			36,			37,			38,			39,
								SDLK_d,		SDLK_f,		SDLK_g,		SDLK_h,		SDLK_j,		SDLK_k,		SDLK_l,		SDLK_SEMICOLON,	
							  //40 (0x28),	41,			42,			43,			44,			45,			46,			47,
								SDLK_AT, 	-1/*SDLK_GRAVE*/, SDLK_LCTRL,	SDLK_BACKSLASH,	SDLK_z,	SDLK_x,		SDLK_c,		SDLK_v,
							  //48 (0x30),	49,			50,			51,			52,			53,			54,			55,
								SDLK_b,		SDLK_n,		SDLK_m,		SDLK_LESS,	SDLK_GREATER,SDLK_SLASH,SDLK_RSHIFT,SDLK_KP_MULTIPLY,
							  //56 (0x38),	57,			58,			59,			60,			61,			62,			63,
								NA,			SDLK_SPACE,	SDLK_CAPSLOCK,SDLK_F1,	SDLK_F2,	SDLK_F3,	SDLK_F4,	SDLK_F5,
							  //64 (0x40),	65,			66,			67,			68,			69,			70,			71,
								SDLK_F6,	SDLK_F7,	SDLK_F8,	SDLK_F9,	SDLK_F10,	-1/*SDLK_NUMLOCKCLEAR*/,	-1/*SDLK_SCROLLLOCK*/,	SDLK_KP7,	
							  //72 (0x48),	73,			74,			75,			76,			77,			78,			79,	
								SDLK_KP8,	SDLK_KP9,	SDLK_KP_MINUS, SDLK_KP4,SDLK_KP5, 	SDLK_KP6, 	SDLK_KP_PLUS,SDLK_KP1,
							  //80 (0x50),	81,			82,			83,			84,			85,			86,			87,	
								SDLK_KP2,	SDLK_KP3,	SDLK_KP0,	SDLK_KP_PERIOD,SDLK_SYSREQ,NA,		NA,			SDLK_F11,
							  //88 (0x58),	89,			90,			91,			92,			93,			94,			95,
								SDLK_F12,	NA,			NA,			NA,			NA,			NA,			NA,			NA,
							  //96 (0x60),	97,			98,			99,			100,		101,		102,		103,
								NA,			NA,			NA,			NA,			NA,			NA,			NA,			SDLK_UP,
							  //104 (0x68),	105,		106,		107,		108,		109,		110,		111,
								NA,			SDLK_LEFT,			SDLK_RIGHT,			NA,			SDLK_DOWN,			NA,			NA,			NA,
							  //112,		113,		114,		115,		116,		117,		118,		119,
								NA,			NA,			NA,			NA,			NA,			NA,			NA,			NA,
							  //120,		121,		122,		123,		124,		125,		126,		127
								NA,			NA,			NA,			NA,			NA,			NA,			NA,			NA,

							  //Escaped Keys

							  //0,			1,			2,			3,			4,			5,			6,			7,
								NA,			SDLK_ESCAPE,SDLK_1,		SDLK_2,		SDLK_3,		SDLK_4,		SDLK_5,		SDLK_6,
							  //8,			9,			10,			11,			12,			13,			14,			15,
								SDLK_7,		SDLK_8,		SDLK_9,		SDLK_0,		SDLK_MINUS,	SDLK_EQUALS,SDLK_BACKSPACE,	SDLK_TAB,
							  //16,			17,			18,			19,			20,			21,			22,			23,
								SDLK_q,		SDLK_w,		SDLK_e,		SDLK_r,		SDLK_t,		SDLK_y,		SDLK_u,		SDLK_i,
							  //24,			25,			26,			27,			28,			29,			30,			31,
								SDLK_o,		SDLK_p,		SDLK_LEFTBRACKET,SDLK_RIGHTBRACKET,	SDLK_RETURN,SDLK_RCTRL,	SDLK_a,	SDLK_s,
							  //32,			33,			34,			35,			36,			37,			38,			39,
								SDLK_d,		SDLK_f,		SDLK_g,		SDLK_h,		SDLK_j,		SDLK_k,		SDLK_l,		SDLK_SEMICOLON,
							  //40,			41,			42,			43,			44,			45,			46,			47,
								SDLK_AT, 	-1/*SDLK_GRAVE*/, NA,	SDLK_BACKSLASH,	SDLK_z,	SDLK_x,		SDLK_c,		SDLK_v,
							  //48,			49,			50,			51,			52,			53,			54,			55,
								SDLK_b,		SDLK_n,		SDLK_m,		SDLK_LESS,	SDLK_GREATER,SDLK_SLASH,NA,			SDLK_KP_MULTIPLY,
							  //56 (0x38),	57,			58,			59,			60,			61,			62,			63,
								NA,			SDLK_SPACE,	SDLK_CAPSLOCK,SDLK_F1,	SDLK_F2,	SDLK_F3,	SDLK_F4,	SDLK_F5,
							  //64 (0x40),	65,			66,			67,			68,			69,			70,			71,
								SDLK_F6,	SDLK_F7,	SDLK_F8,	SDLK_F9,	SDLK_F10,	-1/*SDLK_NUMLOCKCLEAR*/,	-1/*Ctrl-Break*/,	SDLK_HOME,
							  //72 (0x48),	73,			74,			75,			76,			77,			78,			79,
								SDLK_UP,	SDLK_PAGEUP,-1, 		SDLK_LEFT,-1, 		SDLK_RIGHT, -1,			SDLK_END,
							  //80 (0x50),	81,			82,			83,			84,			85,			86,			87,
								SDLK_DOWN,	SDLK_PAGEDOWN,	SDLK_INSERT,	SDLK_KP_PERIOD,SDLK_SYSREQ,NA,		NA,			SDLK_F11,
							  //88 (0x58),	89,			90,			91,			92,			93,			94,			95,
								SDLK_F12,	NA,			NA,			NA,			NA,			NA,			NA,			NA,
							  //96 (0x60),	97,			98,			99,			100,		101,		102,		103,
								NA,			NA,			NA,			NA,			NA,			NA,			NA,			NA,
							  //104,		105,		106,		107,		108,		109,		110,		111,
								NA,			NA,			NA,			NA,			NA,			NA,			NA,			NA,
							  //112,		113,		114,		115,		116,		117,		118,		119,
								NA,			NA,			NA,			NA,			NA,			NA,			NA,			NA,
							  //120,		121,		122,		123,		124,		125,		126,		127
								NA,			NA,			NA,			NA,			NA,			NA,			NA,			NA};


static const int X11toSDL[] = { 0,			1,			2,			3,			4,			5,			6,			7,
								8,			SDLK_ESCAPE,			10,			11,			12,			13,			14,			15,
								16,			17,			18,			19,			20,			21,			22,			23,
								24,			25,			26,			27,			28,			29,			30,			31,
								32,			33,			34,			35,			36,			37,			38,			39,
								40,			41,			42,			43,			44,			45,			46,			47,
								48,			49,			50,			51,			52,			53,			54,			55,
								56,			57,			58,			59,			60,			61,			62,			63,
								64,			65,			66,			67,			68,			69,			70,			71,
								72,			73,			74,			75,			76,			77,			78,			79,
								80,			81,			82,			83,			84,			85,			86,			87,
								88,			89,			90,			91,			92,			93,			94,			95,
								96,			97,			98,			99,			100,		101,		102,		103,
								104,		105,		106,		107,		108,		109,		110,		111,
								112,		113,		114,		115,		116,		117,		118,		119,
								120,		121,		122,		123,		124,		125,		126,		127};


typedef enum
{
	NOT_INIT,
	DESKTOP,
	CONSOLE,
	REMOTE
	} keyboard_mode_t;


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
static void (*PauseCallback)(int) = NULL;

static keyboard_mode_t key_mode = NOT_INIT; 

///////////////////////////////////////////////////////////////////////////////////////////////

#include "unistd.h"
#include "linux/kd.h"
#include "termios.h"
#include "fcntl.h"
#include "sys/ioctl.h"

#include <signal.h>

static struct termios tty_attr_old;
static int old_keyboard_mode;


static void restoreKeyboard()
{
	tcsetattr(0, TCSAFLUSH, &tty_attr_old);

	if (key_mode == CONSOLE)
	{
		ioctl(0, KDSKBMODE, old_keyboard_mode);
	}
}

void ForceClose(int val)
{
	DEBUG_PRINT("signal %d, restoring keyboard\n", val);
	restoreKeyboard();
}

static int setupKeyboard()
{
    struct termios tty_attr;
    int flags;

    /* make stdin non-blocking */
    flags = fcntl(0, F_GETFL);
    flags |= O_NONBLOCK;
    fcntl(0, F_SETFL, flags);

	tcgetattr(0, &tty_attr_old);

    /* turn off buffering, echo and key processing */
    tty_attr = tty_attr_old;
    tty_attr.c_lflag &= ~(ICANON | ECHO | ISIG);
    tty_attr.c_iflag &= ~(ISTRIP | INLCR | ICRNL | IGNCR | IXON | IXOFF);
    tcsetattr(0, TCSANOW, &tty_attr);

    // we want the keyboard returned to normal if something goes wrong
	signal(SIGILL, &ForceClose);	//illegal instruction
	signal(SIGTERM, &ForceClose);
	signal(SIGSEGV, &ForceClose);
	signal(SIGINT, &ForceClose);
	signal(SIGQUIT, &ForceClose);

	/* save old keyboard mode */
    if (ioctl(0, KDGKBMODE, &old_keyboard_mode) < 0)
	{
		DEBUG_PRINT("Setup keyboard in REMOTE mode\n");
		key_mode = REMOTE;
		return 0;
    }

    ioctl(0, KDSKBMODE, K_RAW);
	key_mode = CONSOLE;


	DEBUG_PRINT("Setup keyboard in RAW mode\n");
    return 1;
}

///////////////////////////////////////////////////////////////////////////////////////////////

void RPI_SetPauseCallback(void (*callback)(int))
{
	PauseCallback = callback;
}

void RPI_Pause(unsigned int bPause)
{
	if (key_mode != DESKTOP) return;

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

	if (key_mode == DESKTOP)	//if using X11 do not go full screen. Resize will take place later
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
    if (NULL == x_display)return 1;

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

	key_mode = DESKTOP;

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

	if (key_mode == NOT_INIT)
	{
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
	else if (key_mode == DESKTOP)	// we can goto an X window
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
	if (key_mode != DESKTOP) return 1;

	DEBUG_PRINT("RPI_ChangeTitle(\"%s\")\n", sTitle);
	XStoreName (x_display, win, sTitle);
	return 0;
}

int RPI_CloseWindow()
{
	if (key_mode == CONSOLE || key_mode == REMOTE) restoreKeyboard();

	DEBUG_PRINT("RPI_CloseWindow\n");
	eglDestroyContext ( egl_display, egl_context );
   	eglDestroySurface ( egl_display, egl_surface );
   	eglTerminate      ( egl_display );

	if (key_mode == DESKTOP)
	{
		XCloseDisplay     ( x_display );
	   	XDestroyWindow    ( x_display, root );
	}
	return 0;
}

static int RPI_MoveScreen()
{
	//glViewport ( 0 , 0 , dest_rect.width , dest_rect.height );	//If changing the Viewport then must also change the src_rect
	if (key_mode == DESKTOP)
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
	if (key_mode == DESKTOP)
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

				case KeyRelease:
				case KeyPress:
				DEBUG_PRINT("keyboard input: 0x%x -> SDL key %d\n", xEvent->xkey.keycode, X11toSDL[xEvent->xkey.keycode&0x7F]);

					xEvent->xkey.keycode = X11toSDL[xEvent->xkey.keycode&0x7f];
					break;
				}
			}
			return 1;
		}
		else
		{
			return 0;
		}
	}
	else if (key_mode == CONSOLE) // there is no X window.
	{
		static int keyState = 0;
		// http://www.win.tue.nl/~aeb/linux/kbd/scancodes-1.html
		// http://wiki.libsdl.org/SDL_Keymod?highlight=%28\bCategoryEnum\b%29|%28CategoryKeyboard%29

		//DEBUG_PRINT("Check for keyboard event\n");
		char buf[] = {0,0};
    	int res;
		int byteToRead =0;
		int updateState =0;

		/* read scan code from stdin */
		res = read(0, &buf[0], 1);
		/* keep reading til there's no more*/
		if (res > 0)
		{
			printf("keyboard input: %d, 0x%x 0x%x -> SDL key %d %d\n",res, buf[0], buf[1], RAWtoSDL[buf[0]&0x7F], RAWtoSDL[buf[1]&0x7F]);

			//escape key
			if (buf[0] == 0xe0)
			{
				read(0, &buf[1], 1);

				byteToRead = 1;
				switch (buf[1]&0x7F)
				{
					case 0x1d: updateState = KMOD_RCTRL; 	break;
					case 0x38: updateState = KMOD_RALT;     break;
					//case 0x5b: updateState = KMOD_LGUI;   break;
					//case 0x5c: updateState = KMOD_RGUI;   break;
				}
			}
			else
			{
				switch (buf[0]&0x7F)
				{
					case 0x1d: updateState = KMOD_LCTRL;  break;
					case 0x2a: updateState = KMOD_LSHIFT; break;
					case 0x36: updateState = KMOD_RSHIFT; break;
					case 0x38: updateState = KMOD_LALT;   break;
					case 0x32: updateState = KMOD_CAPS;   break;
					case 0x45: updateState = KMOD_NUM;    break;
				}
			}

			if ( buf[byteToRead] & 0x80 )
			{
					xEvent->type = KeyRelease;
				 	keyState |= updateState;
			}
			else
			{
					xEvent->type = KeyPress;
					keyState &= ~updateState;
			}

				xEvent->xkey.keycode = RAWtoSDL[ (buf[byteToRead]&0x7F) | byteToRead << 7 ];
				xEvent->xkey.state = keyState;
				printf("keyboard type %d, keycode %d\n", xEvent->type, xEvent->xkey.keycode);
				return 1;
		}
		else
		{
			return 0;
		}
	}
	else  // remote ssh or X window broken
	{
		static unsigned char kstates[]= {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

		int res;
		int byteToRead = 0;
		char buf[] = {0,0,0,0,0};
		int i=0;

		xEvent->xkey.keycode = 0;

		/* read scan code from stdin */
		res = read(0, &buf[0], 5);
		/* keep reading til there's no more*/
		if (res > 0)
		{
			if (buf[0] == 27)
			{
				if (res > 2)
				{
					switch (buf[2])
					{
						case 'B': 	i=1; xEvent->xkey.keycode = SDLK_LEFT; 		break;
						case 'A': 	i=2; xEvent->xkey.keycode = SDLK_RIGHT; 	break;
						case 'C': 	i=3; xEvent->xkey.keycode = SDLK_UP; 		break;
						case 'D': 	i=4; xEvent->xkey.keycode = SDLK_DOWN; 		break;
					}
					if (buf[1] == 91 && buf[2] == 49 && buf[3] == 53) // F5 key (save state)
					{
						i=5; xEvent->xkey.keycode = SDLK_F5;
					}
					else if (buf[1] == 91 && buf[2] == 49 && buf[3] == 56) // F7 key (load state)
					{
						i=6; xEvent->xkey.keycode = SDLK_F7;
					}
					else if (buf[1] == 91 && buf[2] == 50 && buf[3] == 52) // F12 key (screen shot)
					{
						i=7; xEvent->xkey.keycode = SDLK_F12;
					}
				}
				else if (byteToRead == 0)	//ESC key pressed
				{
				 	i = 8; 
					xEvent->xkey.keycode = SDLK_ESCAPE;
				}
			}
			else
			{
				switch (buf[0])
				{
					case 'q': 	i=9; xEvent->xkey.keycode = SDLK_ESCAPE; 		break;
					case 'a': 	i=10; xEvent->xkey.keycode = SDLK_LSHIFT; 		break;	// B button
					case 'z': 	i=11; xEvent->xkey.keycode = SDLK_LCTRL; 		break;	// A button
					case '\n': 	i=12; xEvent->xkey.keycode = SDLK_RETURN; 		break;	// Start button
				}
			}
			if (i > 0)
			{
				kstates[i] ^= 1;
				if (kstates[i]) xEvent->type = KeyPress;
				else xEvent->type = KeyRelease;
			}

			if (res == 1) DEBUG_PRINT("<< %3u                 | %c             | SDL key %d, pressed %d\n", buf[0], buf[0], xEvent->xkey.keycode, kstates[i]);
			if (res == 2) DEBUG_PRINT("<< %3u %3u             | %c %c          | SDL key %d, pressed %d\n", buf[0], buf[1], buf[0], buf[1], xEvent->xkey.keycode, kstates[i]);
			if (res == 3) DEBUG_PRINT("<< %3u %3u %3u         | %c %c %c       | SDL key %d, pressed %d\n", buf[0], buf[1], buf[2], buf[0], buf[1], buf[2], xEvent->xkey.keycode, kstates[i]);
			if (res == 4) DEBUG_PRINT("<< %3u %3u %3u %3u     | %c %c %c %c    | SDL key %d, pressed %d\n", buf[0], buf[1], buf[2], buf[3], buf[0], buf[1], buf[2], buf[3], xEvent->xkey.keycode, kstates[i]);
			if (res == 5) DEBUG_PRINT("<< %3u %3u %3u %3u %3u | %c %c %c %c %c | SDL key %d, pressed %d\n", buf[0], buf[1], buf[2], buf[3], buf[4], buf[0], buf[1], buf[2], buf[3], buf[4], xEvent->xkey.keycode, kstates[i]);

			return 1;
		}
	}

	//catch all
	return 0;
}

void RPI_SwapBuffers()
{
	eglSwapBuffers ( egl_display, egl_surface );
}
