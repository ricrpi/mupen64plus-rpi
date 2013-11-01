

#include <stdio.h>
#include <time.h>
#include <pthread.h>
#include <string.h>

#define DEPTH 30
#define THREADS 20

static unsigned int bInit 	= 0;
static unsigned int uiIndex[THREADS];
static pthread_mutex_t Profiler_lock;

static const char * sNames[THREADS][DEPTH];
static unsigned long long int StartTimes[THREADS][DEPTH];

static FILE * myFile = NULL;

//static char blanks[] = "                             ";
static char blanks[] = "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t";

static pthread_t ThreadLookup[20];
static unsigned int ThreadLookupIndex = 0;

static unsigned long long int CurrentTime();

static void Init()
{
	myFile = fopen("Profile.log","w");
	pthread_mutex_init(&Profiler_lock, 0);

	if (NULL == myFile ) printf("Failed to open Profile.log - check owner\n");
	memset(uiIndex, 0, sizeof(int) * THREADS);

	bInit = 1;
}

static int lookupThread()
{
	int x;
	int bOK = 0;
	for (x = 0; x < ThreadLookupIndex; x++)
	{
		if (ThreadLookup[x] == pthread_self())
		{
			return x;
		}
	}

	if (!bOK)
	{
		ThreadLookup[ThreadLookupIndex] = pthread_self();
		fprintf(myFile, "New Thread %lu -> %d\n", ThreadLookup[ThreadLookupIndex], ThreadLookupIndex);
		ThreadLookupIndex++;
	
		return ThreadLookupIndex - 1;
	}
	
	return 0;
}

static unsigned long long int CurrentTime()
{
	struct timespec ts;
     clock_gettime(CLOCK_MONOTONIC, &ts);
     return (long long int)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

void Profile_start(const char * Name)
{
	if (!bInit) Init();

	pthread_mutex_lock(&Profiler_lock);

	int id = lookupThread();

	sNames[id][uiIndex[id]] = Name;
	StartTimes[id][uiIndex[id]] = CurrentTime(); // write index
	uiIndex[id]++;
	pthread_mutex_unlock(&Profiler_lock);
}


void Profile_end()
{
	pthread_mutex_lock(&Profiler_lock);
	int id = lookupThread();
	if (uiIndex[id] > 0) uiIndex[id]--;
	fprintf(myFile, "%9lld\t",  CurrentTime() - StartTimes[id][uiIndex[id]]);
	fwrite( (void*)blanks, 1,uiIndex[id], myFile);
	fprintf(myFile, "%2d %s\n",id, sNames[id][uiIndex[id]]); // write index
	pthread_mutex_unlock(&Profiler_lock);
}

void Profile_end1(unsigned int v1)
{
	pthread_mutex_lock(&Profiler_lock);
	int id = lookupThread();
	if (uiIndex[id] > 0) uiIndex[id]--;
	fprintf(myFile, "%9lld\t",  CurrentTime() - StartTimes[id][uiIndex[id]]);
	fwrite( (void*)blanks, 1,uiIndex[id], myFile);
	fprintf(myFile, "%2d %s\t %d\n", id, sNames[id][uiIndex[id]], v1); // write index
	pthread_mutex_unlock(&Profiler_lock);
}

void Profile_end2(unsigned int v1,unsigned int v2)
{
	pthread_mutex_lock(&Profiler_lock);
	int id = lookupThread();
	if (uiIndex[id] > 0) uiIndex[id]--;
	fprintf(myFile, "%9lld\t",  CurrentTime() - StartTimes[id][uiIndex[id]]);
	fwrite( (void*)blanks, 1,uiIndex[id], myFile);
	fprintf(myFile, "%2d %s\t %6d \t%6d\n", id, sNames[id][uiIndex[id]], v1, v2); // write index
	pthread_mutex_unlock(&Profiler_lock);
}

void Profile_event(const char * Name)
{
	pthread_mutex_lock(&Profiler_lock);
	int id = lookupThread();
	fwrite( (void*)blanks, 1,uiIndex[id] + 9, myFile);
	fprintf(myFile, "%2d Event: %s\n", id, Name); // write index
	pthread_mutex_unlock(&Profiler_lock);
}

void Profile_event1(const char * Name,unsigned int v1)
{
	pthread_mutex_lock(&Profiler_lock);
	int id = lookupThread();
	fwrite( (void*)blanks, 1,uiIndex[id] + 9, myFile);
	fprintf(myFile, "Event: %s\t %d\n", Name, v1); // write index
	pthread_mutex_unlock(&Profiler_lock);
}

void Profile_close()
{
	fclose(myFile);
}
