

#ifndef PROFILER_H
	#define PROFILER_H

	#ifdef TIME_PROFILE
extern "C" {
		void Profile_start(const char * Name);
		void Profile_end();
		void Profile_end1(unsigned int v1);
		void Profile_end2(unsigned int v1,unsigned int v2);
void Profile_event(const char * Name);
void Profile_event1(const char * Name, unsigned int v1);
		void Profile_close();
}
	#else
		#define Profile_start(x)
		#define Profile_end()
		#define Profile_end1(x)
		#define Profile_end2(x,y)
		#define Profile_event(x)
		#define Profile_event1(x,y)
		#define Profile_close()
	#endif

#endif
