

#include "OGLstate.h"


unsigned int state[] = {0,0,0,0,1,0,0,0,0,0,0};	//GL_DITHER is true on init from spec

void gl_Enable(unsigned int value)
{
	unsigned int s = 0;
	switch (value)
	{
		case GL_TEXTURE_2D:  				s = 1; 	break;
		case GL_CULL_FACE:  				s = 2; 	break;
		case GL_BLEND:  					s = 3; 	break;
		case GL_DITHER:  					s = 4; 	break;
		case GL_STENCIL_TEST:  				s = 5; 	break;
		case GL_DEPTH_TEST:  				s = 6; 	break;
		case GL_SCISSOR_TEST:  				s = 7; 	break;
		case GL_POLYGON_OFFSET_FILL:  		s = 8; 	break;
		case GL_SAMPLE_ALPHA_TO_COVERAGE:  	s = 9; 	break;
		case GL_SAMPLE_COVERAGE:  			s = 10; break;
	}

	if ( 0 == s) return;
	if (!state[s]) glEnable(value);
	state[s] = 1;
}

void gl_Disable(unsigned int value)
{
	unsigned int s = 0;
	switch (value)
	{
		case GL_TEXTURE_2D:  				s = 1; 	break;
		case GL_CULL_FACE:  				s = 2; 	break;
		case GL_BLEND:  					s = 3; 	break;
		case GL_DITHER:  					s = 4; 	break;
		case GL_STENCIL_TEST:  				s = 5; 	break;
		case GL_DEPTH_TEST:  				s = 6; 	break;
		case GL_SCISSOR_TEST:  				s = 7; 	break;
		case GL_POLYGON_OFFSET_FILL:  		s = 8; 	break;
		case GL_SAMPLE_ALPHA_TO_COVERAGE:  	s = 9; 	break;
		case GL_SAMPLE_COVERAGE:  			s = 10; break;
	}

	if ( 0 == s) return;
	if (state[s]) glDisable(value);
	state[s] = 0;
}

unsigned int gl_IsEnabled(unsigned int value)
{
	unsigned int s = 0;
	switch (value)
	{
		case GL_TEXTURE_2D:  				s = 1; 	break;
		case GL_CULL_FACE:  				s = 2; 	break;
		case GL_BLEND:  					s = 3; 	break;
		case GL_DITHER:  					s = 4; 	break;
		case GL_STENCIL_TEST:  				s = 5; 	break;
		case GL_DEPTH_TEST:  				s = 6; 	break;
		case GL_SCISSOR_TEST:  				s = 7; 	break;
		case GL_POLYGON_OFFSET_FILL:  		s = 8; 	break;
		case GL_SAMPLE_ALPHA_TO_COVERAGE:  	s = 9; 	break;
		case GL_SAMPLE_COVERAGE:  			s = 10; break;
	}
	if ( 0 == s) return 0;
	return state[s];
}

