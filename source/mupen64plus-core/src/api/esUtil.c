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

#include  "bcm_host.h"

///
// CreateEGLContext()
//
//    Creates an EGL rendering context and all associated elements
//
EGLBoolean CreateEGLContext ( EGLNativeWindowType hWnd, EGLDisplay* eglDisplay,
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
   EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };	// Want to use GLES2 in the EGL Context
      
   // Get Display
   display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   if ( display == EGL_NO_DISPLAY )
   {
      return M64ERR_SYSTEM_FAIL;
   }
   
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
	//display_width=640;
	//display_height=480;
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
      &src_rect, DISPMANX_PROTECTION_NONE, 0 /*alpha*/, 0/*clamp*/, 0/*transform*/);
      
   nativewindow.element = dispman_element;
   nativewindow.width = display_width;
   nativewindow.height = display_height;

   vc_dispmanx_update_submit_sync( dispman_update );
   
   esContext->hWnd = &nativewindow;

	return M64ERR_SUCCESS;
}

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
   bcm_host_init();

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
       EGL_RED_SIZE,       5,
       EGL_GREEN_SIZE,     6,
       EGL_BLUE_SIZE,      5,
       EGL_ALPHA_SIZE,     (flags & ES_WINDOW_ALPHA) ? 8 : EGL_DONT_CARE,
       EGL_DEPTH_SIZE,     (flags & ES_WINDOW_DEPTH) ? 8 : EGL_DONT_CARE,
       EGL_STENCIL_SIZE,   (flags & ES_WINDOW_STENCIL) ? 8 : EGL_DONT_CARE,
       EGL_SAMPLE_BUFFERS, (flags & ES_WINDOW_MULTISAMPLE) ? 1 : 0,
       EGL_NONE
   };
   
   if ( esContext == NULL )
   {
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
   

   return M64ERR_SUCCESS;
}
