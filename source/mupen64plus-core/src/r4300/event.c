
#include <pthread.h>
#include <sched.h>

static unsigned int Flag = 0;
static pthread_mutex_t EventLock;
static pthread_cond_t EventSig;
static unsigned int bInitialized = 0;

//#define DEBUG_PRINT(...) printf(__VA_ARGS__)

#ifndef DEBUG_PRINT
	#define DEBUG_PRINT(...)
#endif

static void Event_init()
{
	bInitialized = 1;
	pthread_mutex_init(&EventLock, 0);
	pthread_cond_init(&EventSig, 0);
}

int Event_Send(unsigned int Flags)
{
	if (!bInitialized) Event_init();

	DEBUG_PRINT("%d   Event_Send(0x%08X), Flag = 0x%08X\n", pthread_self(), Flags, Flag | Flags);

	pthread_mutex_lock(&EventLock);
	Flag |= Flags;
	pthread_cond_broadcast(&EventSig);
	pthread_mutex_unlock(&EventLock);
	sched_yield();
	return 0;
}

int Event_ReceiveAll(unsigned int Flags)
{
	if (!bInitialized) Event_init();
	DEBUG_PRINT("%d   Event_ReceiveAll(0x%08X) wait\n", pthread_self(), Flags);
	pthread_mutex_lock(&EventLock);

	//while we do not have all required flags
	while ((Flag & Flags) != Flags)
	{
		pthread_cond_wait(&EventSig, &EventLock);
	}

	Flag &= ~Flags;
	pthread_mutex_unlock(&EventLock);
	DEBUG_PRINT("%d   Event_ReceiveAll(0x%08X) received\n", pthread_self(), Flags);
	return 0;
}


int Event_ReceiveAny(unsigned int Flags, unsigned int * Found)
{
	if (!bInitialized) Event_init();
	DEBUG_PRINT("%d   Event_ReceiveAny(0x%08X) wait\n", pthread_self(), Flags);
	pthread_mutex_lock(&EventLock);

	//while we do not have any desired flags
	while (!(Flag & Flags))
	{
		pthread_cond_wait(&EventSig, &EventLock);
	}

	// pass back found Flags
	if (NULL != Found) *Found = Flag & Flags;

	// remove flags that are going to be returned
	Flag &= ~(Flag & Flags);

	pthread_mutex_unlock(&EventLock);
	DEBUG_PRINT("%d   Event_ReceiveAny(0x%08X) received \n", pthread_self(), *Found);
	return 0;
}

int Event_ReceiveAnyNB(unsigned int Flags, unsigned int * Found)
{
	if (!bInitialized) Event_init();
	DEBUG_PRINT("%d   Event_ReceiveAny(0x%08X) wait\n", pthread_self(), Flags);
	pthread_mutex_lock(&EventLock);

	// pass back found Flags
	if (NULL != Found) *Found = Flag & Flags;

	// remove flags that are going to be returned
	Flag &= ~(Flag & Flags);

	pthread_mutex_unlock(&EventLock);
	DEBUG_PRINT("%d   Event_ReceiveAny(0x%08X) received \n", pthread_self(), *Found);
	return 0;
}

int Event_Peek(unsigned int * Found)
{
	pthread_mutex_lock(&EventLock);
	if (NULL != Found) *Found = Flag;
	pthread_mutex_unlock(&EventLock);
	
	return 0;
}

