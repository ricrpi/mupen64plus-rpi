

#ifndef RPI_GLES
	#define RPI_GLES

#include  <X11/Xlib.h>
#include  <X11/Xatom.h>
#include  <X11/Xutil.h>

//typedef void (*PauseCallback)(int value);

int RPI_OpenWindow(const char* sTitle, unsigned int uiWidth, unsigned int uiHeight, int bFullScreen, unsigned int Xflags);

int RPI_GetScreenSize(unsigned int *uiWidth, unsigned int *uiHeight);

int RPI_GetWindowSize(unsigned int *uiWidth, unsigned int *uiHeight);

int RPI_FullScreen(unsigned int bFullscreen);

int RPI_ChangeTitle(const char* sTitle);

int RPI_CloseWindow();

//int RPI_MoveScreen(unsigned int uiLeft, unsigned int uiTop, unsigned int uiWidth, unsigned int uiHeight);

int RPI_NextXEvent(XEvent* xEvent);

void RPI_SwapBuffers();

void RPI_Pause(unsigned int bPause);

void RPI_SetPauseCallback(void (*callback)(int));

#endif
