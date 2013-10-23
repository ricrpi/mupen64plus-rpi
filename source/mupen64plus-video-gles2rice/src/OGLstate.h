

#include "osal_opengl.h"

void gl_Enable(unsigned int value);
void gl_Disable(unsigned int value);
unsigned int gl_IsEnabled(unsigned int value);

typedef enum {
	/*GL_TEXTURE_2D= 1,
	GL_CULL_FACE,
	GL_BLEND,
	GL_DITHER,
	GL_STENCIL_TEST,
	GL_DEPTH_TEST,
	GL_SCISSOR_TEST,
	GL_POLYGON_OFFSET_FILL,
	GL_SAMPLE_ALPHA_TO_COVERAGE,
	GL_SAMPLE_COVERAGE,*/
	GL_DEPTH_MASK = 11
} gl_state_t;
