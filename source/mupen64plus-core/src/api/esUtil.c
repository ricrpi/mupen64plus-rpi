//
// Book:      OpenGL(R) ES 2.0 Programming Guide
// Authors:   Aaftab Munshi, Dan Ginsburg, Dave Shreiner
// ISBN-10:   0321502795
// ISBN-13:   9780321502797
// Publisher: Addison-Wesley Professional
// URLs:      http://safari.informit.com/9780321563835
//            http://www.opengles-book.com
//

// ESUtil.c
//
//    A utility library for OpenGL ES.  This library provides a
//    basic common framework for the example applications in the
//    OpenGL ES 2.0 Programming Guide.
//

///
//  Includes
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/time.h>

#include "esUtil.h"

#include "api/callbacks.h"


#define RPI_NO_X 1

#ifdef RPI_NO_X
#include  "bcm_host.h"
#else
#include  <X11/Xlib.h>
#include  <X11/Xatom.h>
#include  <X11/Xutil.h>
#endif

#ifndef RPI_NO_X
// X11 related local variables
static Display *x_display = NULL;
#endif

///
// CreateEGLContext()
//
//    Creates an EGL rendering context and all associated elements
//
static EGLBoolean CreateEGLContext ( EGLNativeWindowType hWnd, EGLDisplay* eglDisplay,
                              EGLContext* eglContext, EGLSurface* eglSurface,
                              EGLint attribList[])
{
   EGLint numConfigs;
   EGLint majorVersion;
   EGLint minorVersion;
   EGLDisplay display;
   EGLContext context;
   EGLSurface surface;
   EGLConfig config;

   #ifndef RPI_NO_X
   EGLint contextAttribs[] = { 
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE, EGL_NONE };
   #else
   EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
   #endif
 /*	EGLint PBufferAttribs[] = { 
		EGL_WIDTH, 640,
		EGL_HEIGHT, 480,
		EGL_NONE
	};*/

   // Get Display
   #ifndef RPI_NO_X
   display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   if ( display == EGL_NO_DISPLAY )
   {
	DebugMessage(M64MSG_ERROR, "Could not Get Display");
      return M64ERR_SYSTEM_FAIL;
   }
   #else
   display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   if ( display == EGL_NO_DISPLAY )
   {
	DebugMessage(M64MSG_ERROR, "Could not Get Display");
      return M64ERR_SYSTEM_FAIL;
   }
   #endif
   
   // Initialize EGL
   if ( !eglInitialize(display, &majorVersion, &minorVersion) )
   {
	DebugMessage(M64MSG_ERROR, "Could not Initialize EGL");
      return M64ERR_SYSTEM_FAIL;
   }
	DebugMessage(M64MSG_VERBOSE, "EGL %d.%d Initialized", majorVersion, minorVersion );

   // Get configs
   if ( !eglGetConfigs(display, NULL, 0, &numConfigs) )
   {
	DebugMessage(M64MSG_ERROR, "Could not Get EGL Configurations");
      return M64ERR_SYSTEM_FAIL;
   }

   // Choose config
   if ( !eglChooseConfig(display, attribList, &config, 1, &numConfigs) )
   {
	DebugMessage(M64MSG_ERROR, "Could not Choose EGL Configuration");
      return M64ERR_SYSTEM_FAIL;
   }

   // Create a surface
   surface = eglCreateWindowSurface(display, config, (EGLNativeWindowType)hWnd, NULL);
   //surface = eglCreatePbufferSurface(display, config, PBufferAttribs);
   if ( surface == EGL_NO_SURFACE )
   {
	DebugMessage(M64MSG_ERROR, "Could not Create EGL Surface");
      return M64ERR_SYSTEM_FAIL;
   }

   // Create a GL context
   context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttribs );
   if ( context == EGL_NO_CONTEXT )
   {
	DebugMessage(M64MSG_ERROR, "Could not Create EGL Context");
      return M64ERR_SYSTEM_FAIL;
   }   
   
   // Make the context current
   if ( !eglMakeCurrent(display, surface, surface, context) )
   {
	DebugMessage(M64MSG_ERROR, "Could not Make EGL Context current");
      return M64ERR_SYSTEM_FAIL;
   }
   
   *eglDisplay = display;
   *eglSurface = surface;
   *eglContext = context;

   return M64ERR_SUCCESS;
} 

#ifdef RPI_NO_X
///
//  WinCreate() - RaspberryPi, direct surface (No X, Xlib)
//
//      This function initialized the display and window for EGL
//
EGLBoolean WinCreate(ESContext *esContext, const char *title) 
{
   int32_t success = 0;

   static EGL_DISPMANX_WINDOW_T nativewindow;

   DISPMANX_ELEMENT_HANDLE_T dispman_element;
   DISPMANX_DISPLAY_HANDLE_T dispman_display;
   DISPMANX_UPDATE_HANDLE_T dispman_update;
   VC_RECT_T dst_rect;
   VC_RECT_T src_rect;
   
	VC_DISPMANX_ALPHA_T dispman_alpha = {
		DISPMANX_FLAGS_ALPHA_FIXED_ALL_PIXELS,
		255, /* opacity 0-255 */
		0 /* mask resource handle */
	};

   uint32_t display_width;
   uint32_t display_height;

   // create an EGL window surface, passing context width/height
   success = graphics_get_display_size(0 /* LCD */, &display_width, &display_height);
   if ( success < 0 )
   {
	DebugMessage(M64MSG_ERROR, "graphics_get_display_size failed");
      return M64ERR_SYSTEM_FAIL;
   }
   DebugMessage(M64MSG_VERBOSE, "Screen resolution is: %dx%d", display_width, display_height);
   
    display_width = esContext->width;
   display_height = esContext->height;

   dst_rect.x = 0;
   dst_rect.y = 0;
   dst_rect.width = display_width;
   dst_rect.height = display_height;
      
   src_rect.x = 0;
   src_rect.y = 0;
   src_rect.width = display_width << 16;
   src_rect.height = display_height << 16;   

   dispman_display = vc_dispmanx_display_open( 0 /* LCD */);
   dispman_update = vc_dispmanx_update_start( 0 );
   dispman_element = vc_dispmanx_element_add ( dispman_update, dispman_display,
      0/*layer*/, &dst_rect, 0/*src*/,
      &src_rect, DISPMANX_PROTECTION_NONE, &dispman_alpha, 0/*clamp*/, 0/*transform*/);
   nativewindow.element = dispman_element;
   nativewindow.width = display_width;
   nativewindow.height = display_height;

   vc_dispmanx_update_submit_sync( dispman_update );
   
   esContext->hWnd = &nativewindow;

	return M64ERR_SUCCESS;
}
///
//  userInterrupt()
//
//      Reads from X11 event loop and interrupt program if there is a keypress, or
//      window close action.
//
GLboolean userInterrupt(ESContext *esContext)
{
	//GLboolean userinterrupt = GL_FALSE;
    //return userinterrupt;
    
    // Ctrl-C for now to stop
    
    return GL_FALSE;
}
#else
///
//  WinCreate()
//
//      This function initialized the native X11 display and window for EGL
//
EGLBoolean WinCreate(ESContext *esContext, const char *title)
{
    Window root;
    XSetWindowAttributes swa;
    XSetWindowAttributes  xattr;
    Atom wm_state;
    XWMHints hints;
    XEvent xev;
    EGLConfig ecfg;
    EGLint num_config;
    Window win;

    /*
     * X11 native display initialization
     */

    x_display = XOpenDisplay(NULL);
    if ( x_display == NULL )
    {
        return M64ERR_SYSTEM_FAIL;
    }

    root = DefaultRootWindow(x_display);

    swa.event_mask  =  ExposureMask | PointerMotionMask | KeyPressMask;
    win = XCreateWindow(
               x_display, root,
               0, 0, esContext->width, esContext->height, 0,
               CopyFromParent, InputOutput,
               CopyFromParent, CWEventMask,
               &swa );

    xattr.override_redirect = FALSE;
    XChangeWindowAttributes ( x_display, win, CWOverrideRedirect, &xattr );

    hints.input = TRUE;
    hints.flags = InputHint;
    XSetWMHints(x_display, win, &hints);

    // make the window visible on the screen
    XMapWindow (x_display, win);
    XStoreName (x_display, win, title);

    // get identifiers for the provided atom name strings
    wm_state = XInternAtom (x_display, "_NET_WM_STATE", FALSE);

    memset ( &xev, 0, sizeof(xev) );
    xev.type                 = ClientMessage;
    xev.xclient.window       = win;
    xev.xclient.message_type = wm_state;
    xev.xclient.format       = 32;
    xev.xclient.data.l[0]    = 1;
    xev.xclient.data.l[1]    = FALSE;
    XSendEvent (
       x_display,
       DefaultRootWindow ( x_display ),
       FALSE,
       SubstructureNotifyMask,
       &xev );

    esContext->hWnd = (EGLNativeWindowType) win;
    return M64ERR_SUCCESS;
}


///
//  userInterrupt()
//
//      Reads from X11 event loop and interrupt program if there is a keypress, or
//      window close action.
//
GLboolean userInterrupt(ESContext *esContext)
{
    XEvent xev;
    KeySym key;
    GLboolean userinterrupt = GL_FALSE;
    char text;

    // Pump all messages from X server. Keypresses are directed to keyfunc (if defined)
    while ( XPending ( x_display ) )
    {
        XNextEvent( x_display, &xev );
        if ( xev.type == KeyPress )
        {
            if (XLookupString(&xev.xkey,&text,1,&key,0)==1)
            {
DebugMessage(M64MSG_INFO, "Key press: %c",text );
                if (esContext->keyFunc != NULL)
                    esContext->keyFunc(esContext, text, 0, 0);
            }
        }
        if ( xev.type == DestroyNotify )
            userinterrupt = GL_TRUE;
    }
    return userinterrupt;
}
#endif

//////////////////////////////////////////////////////////////////
//
//  Public Functions
//
//

///
//  esInitContext()
//
//      Initialize ES utility context.  This must be called before calling any other
//      functions.
//
m64p_error esInitContext ( ESContext *esContext )
{
   #ifdef RPI_NO_X
   bcm_host_init();
#endif
   if ( esContext != NULL )
   {
      memset( esContext, 0, sizeof( ESContext) );
   }
	return M64ERR_SUCCESS;
}


///
//  esCreateWindow()
//
//      title - name for title bar of window
//      width - width of window to create
//      height - height of window to create
//      flags  - bitwise or of window creation flags 
//          ES_WINDOW_ALPHA       - specifies that the framebuffer should have alpha
//          ES_WINDOW_DEPTH       - specifies that a depth buffer should be created
//          ES_WINDOW_STENCIL     - specifies that a stencil buffer should be created
//          ES_WINDOW_MULTISAMPLE - specifies that a multi-sample buffer should be created
//
m64p_error esCreateWindow ( ESContext *esContext, const char* title, GLint width, GLint height, GLuint flags )
{
   EGLint attribList[] =
   {
       EGL_RED_SIZE,       8,
       EGL_GREEN_SIZE,     8, //6,
       EGL_BLUE_SIZE,      8,
       EGL_ALPHA_SIZE,     8, //EGL_DONT_CARE,
       EGL_DEPTH_SIZE,     24, //EGL_DONT_CARE,
       EGL_STENCIL_SIZE,   EGL_DONT_CARE,
EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
	   //EGL_BIND_TO_TEXTURE_RGBA, EGL_TRUE, 

      EGL_SAMPLE_BUFFERS, 1,
       EGL_NONE
   };
   
   if ( esContext == NULL )
   {	DebugMessage(M64MSG_ERROR, "esContext = NULL");
      return M64ERR_SYSTEM_FAIL;
   }

   esContext->width = width;
   esContext->height = height;

   if ( WinCreate ( esContext, title) != M64ERR_SUCCESS )
   {
      DebugMessage(M64MSG_ERROR, "Failed to create GLES Window");
   
      return M64ERR_SYSTEM_FAIL;
   }

  
   if ( CreateEGLContext ( esContext->hWnd,
                            &esContext->eglDisplay,
                            &esContext->eglContext,
                            &esContext->eglSurface,
                            attribList) != M64ERR_SUCCESS)
   {
      DebugMessage(M64MSG_ERROR, "Failed to create EGL Context");
      return M64ERR_SYSTEM_FAIL;
   }

	//glBindFramebuffer(GL_FRAMEBUFFER, 0);

   return M64ERR_SUCCESS;
}
