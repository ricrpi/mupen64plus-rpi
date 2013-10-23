

#include <stdio.h>
#include <time.h>

#define DEPTH 30

static unsigned int bInit 	= 0;
static unsigned int uiIndex = 0;

static const char * sNames[DEPTH];
static unsigned long long int StartTimes[DEPTH];

static FILE * myFile = NULL;

//static char blanks[] = "                             ";
static char blanks[] = "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t";

static unsigned long long int CurrentTime();

static void Init()
{
	myFile = fopen("Profile.log","w");
	uiIndex = 0;
	bInit = 1;
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
	sNames[uiIndex] = Name;
	StartTimes[uiIndex] = CurrentTime(); // write index
	uiIndex++;
}


void Profile_end()
{
	uiIndex--;
	fprintf(myFile, "%9lld\t",  CurrentTime() - StartTimes[uiIndex]);
	fwrite( (void*)blanks, 1,uiIndex, myFile);
	fprintf(myFile, "%s\n",sNames[uiIndex]); // write index
}

void Profile_end1(unsigned int v1)
{
	uiIndex--;
	fprintf(myFile, "%9lld\t",  CurrentTime() - StartTimes[uiIndex]);
	fwrite( (void*)blanks, 1,uiIndex, myFile);
	fprintf(myFile, "%s\t %d\n",sNames[uiIndex], v1); // write index
}

void Profile_end2(unsigned int v1,unsigned int v2)
{
	uiIndex--;
	fprintf(myFile, "%9lld\t",  CurrentTime() - StartTimes[uiIndex]);
	fwrite( (void*)blanks, 1,uiIndex, myFile);
	fprintf(myFile, "%s\t %6d \t%6d\n",sNames[uiIndex], v1, v2); // write index
}

void Profile_event(const char * Name)
{
	fwrite( (void*)blanks, 1,uiIndex + 9, myFile);
	fprintf(myFile, "Event: %s\n", Name); // write index
}

void Profile_event1(const char * Name,unsigned int v1)
{
	fwrite( (void*)blanks, 1,uiIndex + 9, myFile);
	fprintf(myFile, "Event: %s\t %d\n", Name, v1); // write index
}

void Profile_close()
{
	fclose(myFile);
}
