/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - interupt.c                                              *
 *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
 *   Copyright (C) 2002 Hacktarux                                          *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.          *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include <stdlib.h>

#include <SDL.h>

#define M64P_CORE_PROTOTYPES 1
#include "api/m64p_types.h"
#include "api/callbacks.h"
#include "api/m64p_vidext.h"
#include "api/vidext.h"
#include "memory/memory.h"
#include "main/rom.h"
#include "main/main.h"
#include "main/savestates.h"
#include "main/cheat.h"
#include "osd/osd.h"
#include "plugin/plugin.h"
#include "api/rpiGLES.h"
#include "main/eventloop.h"

#include "interupt.h"
#include "r4300.h"
#include "macros.h"
#include "exception.h"
#include "reset.h"
#include "new_dynarec/new_dynarec.h"

#ifdef WITH_LIRC
#include "main/lirc.h"
#endif

#include <unistd.h>
#include <pthread.h>
#include "event.h"

#include <sys/prctl.h>

//#define DEBUG_PRINT(...) printf(__VA_ARGS__);
#define QUEUE_SIZE	32


#ifndef DEBUG_PRINT
#define DEBUG_PRINT(...)
#endif

mt_thread mt_s, mt_vi, mt_ai, mt_pi, mt_si, mt_dp, mt_sp, mt_nmi, mt_hw2;
mt_options mt_o;


extern uint32_t SDL_GetTicks();

//----- Prototypes ----------------------

void X11_PumpEvents();


//----- Local Variables -----------------

static pthread_mutex_t 	InterruptLock;


static pthread_t 		SystemTimer_Thread, 	Graphics_Timer_Thread;
static pthread_t		Graphics_Thread,	PI_Thread, 	SI_Thread, DP_Thread, SP_Thread, HW2_Thread, Audio_Thread;

static unsigned int 	InterruptFlag;
static int 				int_count 	= 0;
static int 				vi_counter 	= 0;

static int SPECIAL_done = 0;
//----- Global Variables -----------------

unsigned int next_vi;
int vi_field=0;
int interupt_unsafe_state = 0;

//-------------------------------------------------------

typedef struct _interupt_queue
{
   int type;
   unsigned int count;
   struct _interupt_queue *next;
} interupt_queue;

static interupt_queue *q = NULL;
static interupt_queue *qstack[QUEUE_SIZE];
static unsigned int qstackindex = 0;
static interupt_queue *qbase = NULL;

static interupt_queue* queue_malloc(size_t Bytes)
{
	if (qstackindex >= QUEUE_SIZE - 1) // should never happen
	{
		static int bNotified = 0;

		if (!bNotified)
		{
			DebugMessage(M64MSG_VERBOSE, "/mupen64plus-core/src/4300/interupt.c: QUEUE_SIZE too small");
			bNotified = 1;
		}

 		return malloc(Bytes);
	}
	interupt_queue* newQueue = qstack[qstackindex];
	qstackindex ++;

	return newQueue;
}

static void queue_free(interupt_queue *qToFree)
{
	if (qToFree < qbase || qToFree >= qbase + sizeof(interupt_queue) * QUEUE_SIZE )
	{
		free(qToFree); //must be a non-stack memory allocation
 		return;
	}
	/*if (qstackindex == 0 ) // should never happen
	{
		DebugMessage(M64MSG_ERROR, "Nothing to free");
 		return;
	}*/
	qstackindex --;
	qstack[qstackindex] = qToFree;
}

//-------------------------------------------------------

static void clear_queue(void)
{
    while(q != NULL)
    {
        interupt_queue *aux = q->next;
        queue_free(q);
        q = aux;
    }
}

/*static void print_queue(void)
{
    interupt_queue *aux;
    //if (Count < 0x7000000) return;
    DebugMessage(M64MSG_INFO, "------------------ 0x%x", (unsigned int)Count);
    aux = q;
    while (aux != NULL)
    {
        DebugMessage(M64MSG_INFO, "Count:%x, %x", (unsigned int)aux->count, aux->type);
        aux = aux->next;
    }
}*/


static int before_event(unsigned int evt1, unsigned int evt2, int type2)
{
    if(evt1 - Count < 0x80000000)
    {
        if(evt2 - Count < 0x80000000)
        {
            if(evt1 < evt2) return 1;
            else return 0;
        }
        else
        {
            if((Count - evt2) < 0x10000000)
            {
                switch(type2)
                {
                    case SPECIAL_INT:
                        if(SPECIAL_done) return 1;
                        else return 0;
                        break;
                    default:
                        return 0;
                }
            }
            else return 1;
        }
    }
    else return 0;
}

void add_interupt_event(int type, unsigned int delay)
{
    unsigned int count = Count + delay/**2*/;
    int special = 0;

pthread_mutex_lock(&InterruptLock);

	interupt_queue *aux = q;

    if(type == SPECIAL_INT /*|| type == COMPARE_INT*/) special = 1;
    if(Count > 0x80000000) SPECIAL_done = 0;

	if(mt_s.bPrintDMsg) printf("add_interupt_event(0x%X, %d), Count = %d\n",type, delay, Count);

    if (get_event(type)) {
        DebugMessage(M64MSG_WARNING, "two events of type 0x%X in interrupt queue", type);
    }

    if (q == NULL)
    {
        q = (interupt_queue *) queue_malloc(sizeof(interupt_queue));
        q->next = NULL;
        q->count = count;
        q->type = type;
        next_interupt = q->count;
        //print_queue();
pthread_mutex_unlock(&InterruptLock);
        return;
    }

    if(before_event(count, q->count, q->type) && !special)
    {
        q = (interupt_queue *) queue_malloc(sizeof(interupt_queue));
        q->next = aux;
        q->count = count;
        q->type = type;
        next_interupt = q->count;
        //print_queue();
pthread_mutex_unlock(&InterruptLock);
        return;
    }

//if not at end of list and (count is after next item of type or special) then get next
    while (aux->next != NULL && (!before_event(count, aux->next->count, aux->next->type) || special))
        aux = aux->next;

    if (aux->next == NULL)
    {
        aux->next = (interupt_queue *) queue_malloc(sizeof(interupt_queue));
        aux = aux->next;
        aux->next = NULL;
        aux->count = count;
        aux->type = type;
    }
    else
    {
        interupt_queue *aux2;
        if (type != SPECIAL_INT)
            while(aux->next != NULL && aux->next->count == count)
                aux = aux->next;
        aux2 = aux->next;
        aux->next = (interupt_queue *) queue_malloc(sizeof(interupt_queue));
        aux = aux->next;
        aux->next = aux2;
        aux->count = count;
        aux->type = type;
    }
pthread_mutex_unlock(&InterruptLock);
}

void add_interupt_event_count(int type, unsigned int count)
{
    add_interupt_event(type, (count - Count)/*/2*/);
}

static void remove_interupt_event(void)
{
	if(mt_s.bPrintDMsg) printf("remove_interupt_event 0x%X, Count = %d\n", q->type, Count);
	if (NULL == q) return;

    interupt_queue *aux = q->next;
    if(q->type == SPECIAL_INT) SPECIAL_done = 1;
    queue_free(q);
    q = aux;
    if (q != NULL && (q->count > Count || (Count - q->count) < 0x80000000))
        next_interupt = q->count;
    else
        next_interupt = 0;
}

unsigned int get_event(int type)
{
    interupt_queue *aux = q;
    if (q == NULL){
	 	return 0;
	}
    if (q->type == type)
        return q->count;
    while (aux->next != NULL && aux->next->type != type)
        aux = aux->next;
    if (aux->next != NULL)
        return aux->next->count;
    return 0;
}

int get_next_event_type(void)
{
    if (q == NULL) return 0;
    return q->type;
}

void remove_event(int type)
{
	//if (type == 1) printf("remove_interupt_event %d\n", type);
    interupt_queue *aux = q;
    if (q == NULL){
 		return;
    }

    if (q->type == type)
    {
        aux = aux->next;
        queue_free(q);
        q = aux;
		if(mt_s.bPrintDMsg) printf("remove_event(0x%X), Count = %d\n", type, Count);
        return;
    }
    while (aux->next != NULL && aux->next->type != type)
        aux = aux->next;
    if (aux->next != NULL) // it's a type int
    {
        interupt_queue *aux2 = aux->next->next;
        queue_free(aux->next);
        aux->next = aux2;
		if(mt_s.bPrintDMsg) printf("remove_event(0x%X), Count = %d\n", type, Count);
    }
}

void translate_event_queue(unsigned int base)
{
    interupt_queue *aux;
pthread_mutex_lock(&InterruptLock);
    remove_event(COMPARE_INT);
    remove_event(SPECIAL_INT);
    aux=q;
    while (aux != NULL)
    {
        aux->count = (aux->count - Count)+base;
        aux = aux->next;
    }
pthread_mutex_unlock(&InterruptLock);
    add_interupt_event_count(COMPARE_INT, Compare);
    add_interupt_event_count(SPECIAL_INT, 0);
}

int save_eventqueue_infos(char *buf)
{
    int len = 0;
pthread_mutex_lock(&InterruptLock);
    interupt_queue *aux = q;
    if (q == NULL)
    {
        *((unsigned int*)&buf[0]) = 0xFFFFFFFF;
        return 4;
    }
    while (aux != NULL)
    {
        memcpy(buf+len  , &aux->type , 4);
        memcpy(buf+len+4, &aux->count, 4);
        len += 8;
        aux = aux->next;
    }
    *((unsigned int*)&buf[len]) = 0xFFFFFFFF;
pthread_mutex_unlock(&InterruptLock);
    return len+4;
}

void load_eventqueue_infos(char *buf)
{
    int len = 0;
    //clear_queue();

	if (qbase != NULL) free(qbase);
	qbase = (interupt_queue *) malloc(sizeof(interupt_queue) * QUEUE_SIZE );
	memset(qbase,0,sizeof(interupt_queue) * QUEUE_SIZE );
	qstackindex=0;

	int i=0;

	//load the stack with the addresses of available slots
	for (i =0; i < QUEUE_SIZE; i++)
	{
		qstack[i] = &qbase[i];
	}
    while (*((unsigned int*)&buf[len]) != 0xFFFFFFFF)
    {
        int type = *((unsigned int*)&buf[len]);
        unsigned int count = *((unsigned int*)&buf[len+4]);
        add_interupt_event_count(type, count);
        len += 8;
    }
}

//------------------------ Threads ---------------------

static void* SystemTimer(void * args)
{
	struct sched_param p;
	int pol = -1;

	p.sched_priority = mt_s.uiPriority;
	pthread_setschedparam(0,SCHED_FIFO, &p);

	prctl(PR_SET_NAME,"M64P Timer",0,0,0);

	pthread_getschedparam(0, &pol, &p);
	if(mt_s.bPrintDMsg) printf("Starting SysTimer thread %lu", pthread_self());

	if (mt_s.bUseEvents){
		while (!stop)
		{
			_gen_interupt();
			usleep(mt_s.uiWait);	//sleep 500KHz
		}

		vi_counter = 0; // debug
		dyna_stop();
	}
	return NULL;
}

static void* GraphicsTimer(void * args)
{
	struct sched_param p;
	int pol = -1;
	p.sched_priority = mt_vi.uiPriority;
	pthread_setschedparam(0,SCHED_FIFO, &p);

	prctl(PR_SET_NAME,"M64P Gfx Timer",0,0,0);

	pthread_getschedparam(0, &pol, &p);
	if(mt_vi.bPrintDMsg) printf("Starting GFX Timer Thread %lu", pthread_self());

	//sleep(10);
	if (mt_vi.bUseEvents) Event_Send(VI_INT);

	while (!stop)
	{
		Event_ReceiveAll(VI_INT_PENDING);

		usleep(mt_vi.uiWait);	//sleep 60Hz
		if(mt_vi.bPrintDMsg) printf("send Event VI_INT, Count = %d\n", Count);
		if (mt_vi.bUseEvents)	Event_Send(VI_INT);
		else add_interupt_event(VI_INT, 500000);
	}

	return NULL;
}

static void* GraphicsThread(void * args)
{
	struct sched_param p;
	p.sched_priority = mt_vi.uiPriority;
	pthread_setschedparam(0,SCHED_FIFO, &p);


	prctl(PR_SET_NAME,"M64P Graphics",0,0,0);
	if(mt_vi.bPrintDMsg) printf("Starting Graphics Thread %lu", pthread_self());

	gfx.romOpen();

	while (!stop)
	{
		uint32_t Flags;

		Event_ReceiveAny(VI_INT_DRAW|VI_INT_DLIST, &Flags);

		if (Flags & VI_INT_DLIST)
		{
			gfx.processDList();
		}

		if (Flags & VI_INT_DRAW)
		{
			gfx.updateScreen();

			//Send event to start next wait for screen draw
			Event_Send(VI_INT_PENDING);
		}

		Event_Send(VI_INT_DONE);
	}
	return NULL;
}

static void* AudioThread(void * args)
{
	struct sched_param p;
	p.sched_priority = mt_ai.uiPriority;
	pthread_setschedparam(0, SCHED_FIFO, &p);

	prctl(PR_SET_NAME,"M64P Audio",0,0,0);
	if(mt_ai.bPrintDMsg) printf("Starting AudioThread %lu\n", pthread_self());

	while (!stop)
	{
		Event_ReceiveAll(AI_INT_PENDING);
		usleep(mt_ai.uiWait);
		if(mt_ai.bPrintDMsg) printf("send Event AI_INT, Count = %d\n", Count);
		Event_Send(AI_INT);
	}
	return NULL;
}

static void* _PI_Thread(void * args)
{
	struct sched_param p;
	p.sched_priority = mt_pi.uiPriority;
	pthread_setschedparam(0, SCHED_FIFO, &p);

	prctl(PR_SET_NAME,"M64P PI_INT",0,0,0);
	if(mt_pi.bPrintDMsg) printf("Starting PI_INT Thread %lu\n", pthread_self());
	while (!stop)
	{
		Event_ReceiveAll(PI_INT_PENDING);
		usleep(mt_pi.uiWait);
		if(mt_pi.bPrintDMsg) printf("send Event PI_INT, Count = %d\n", Count);
		Event_Send(PI_INT);
	}
	return NULL;
}

static void* _SI_Thread(void* args)
{
	struct sched_param p;
	p.sched_priority = mt_si.uiPriority;
	pthread_setschedparam(0, SCHED_FIFO, &p);

	prctl(PR_SET_NAME,"M64P SI_INT",0,0,0);
	if(mt_si.bPrintDMsg) printf("Starting SI_INT Thread %lu\n", pthread_self());
	while (!stop)
	{
		Event_ReceiveAll(SI_INT_PENDING);
		usleep(mt_si.uiWait);
		if(mt_si.bPrintDMsg) printf("send Event SI_INT, Count = %d\n", Count);
		Event_Send(SI_INT);
	}
	return NULL;
}

static void* _SP_Thread(void* args)
{
	struct sched_param p;
	p.sched_priority = mt_sp.uiPriority;
	pthread_setschedparam(0, SCHED_FIFO, &p);

	prctl(PR_SET_NAME,"M64P SP_INT",0,0,0);
	if(mt_sp.bPrintDMsg) printf("Starting SP_INT Thread %lu\n", pthread_self());
	while (!stop)
	{
		Event_ReceiveAll(SP_INT_PENDING);
		usleep(mt_sp.uiWait);
		if(mt_sp.bPrintDMsg) printf("send Event SP_INT, Count = %d\n", Count);
		Event_Send(SP_INT);
	}
	return NULL;
}

static void* _DP_Thread(void* args)
{
	struct sched_param p;
	p.sched_priority = mt_dp.uiPriority;
	pthread_setschedparam(0, SCHED_FIFO, &p);

	prctl(PR_SET_NAME,"M64P DP_INT",0,0,0);
	if(mt_dp.bPrintDMsg) printf("Starting DP_INT Thread %lu\n", pthread_self());
	while (!stop)
	{
		Event_ReceiveAll(DP_INT_PENDING);
		usleep(mt_dp.uiWait);
		
		if(mt_dp.bPrintDMsg) printf("send Event DP_INT, Count = %d\n", Count);
		Event_Send(DP_INT);
		
	}
	return NULL;
}

static void* _HW2_Thread(void* args)
{
	struct sched_param p;
	p.sched_priority = mt_hw2.uiPriority;
	pthread_setschedparam(0, SCHED_FIFO, &p);

	prctl(PR_SET_NAME,"M64P HW2_INT",0,0,0);
	if(mt_hw2.bPrintDMsg) printf("Starting HW2_INT Thread %lu\n", pthread_self());
	while (!stop)
	{
		Event_ReceiveAll(HW2_INT_PENDING);
		usleep(mt_hw2.uiWait);
		if(mt_hw2.bPrintDMsg) printf("send Event HW2_INT, Count = %d\n", Count);
		Event_Send(HW2_INT);
		usleep(mt_nmi.uiWait);
		if(mt_hw2.bPrintDMsg) printf("send Event NMI_INT, Count = %d\n", Count);
		Event_Send(NMI_INT);
	}
	return NULL;
}

void init_interupt(void)
{
 	if (qbase != NULL) free(qbase);
	qbase = (interupt_queue *) malloc(sizeof(interupt_queue) * QUEUE_SIZE );
	memset(qbase,0,sizeof(interupt_queue) * QUEUE_SIZE );
    qstackindex=0;
	int i=0;

	//load the stack with the addresses of available slots
	for (i =0; i < QUEUE_SIZE; i++)
	{
		qstack[i] = &qbase[i];
	}

	int e = 0;

	DEBUG_PRINT("init_interupt()\n");
 	// create the emulated ISR in a new thread
	pthread_attr_t attr;
	pthread_attr_init(&attr);

	pthread_mutex_init(&InterruptLock,NULL);

	if ((e = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED)))
	{
		DebugMessage(M64MSG_ERROR, "Failed to set attr detachstate %d", e);
	}

	pthread_create(&SystemTimer_Thread, &attr, 	SystemTimer,	NULL);
	pthread_create(&Graphics_Thread,	&attr,	GraphicsThread,	NULL);
	pthread_create(&Graphics_Timer_Thread, &attr, GraphicsTimer, 	NULL);

	//pthread_create(&Audio_Thread,		&attr,	AudioThread,	NULL);
	pthread_create(&PI_Thread,			&attr,	_PI_Thread,		NULL);
	pthread_create(&SI_Thread,			&attr,	_SI_Thread,		NULL);
	pthread_create(&SP_Thread,			&attr,	_SP_Thread,		NULL);
	pthread_create(&DP_Thread,			&attr,	_DP_Thread,		NULL);
	pthread_create(&HW2_Thread,			&attr,	_HW2_Thread,		NULL);

	usleep(10);
	pthread_attr_destroy(&attr);

	//allow thread to run and finish initializing
	//pthread_yield();


	SPECIAL_done = 1;
    next_vi = next_interupt = 5000;
    vi_register.vi_delay = next_vi;
    vi_field = 0;
    //clear_queue();
	if (!mt_vi.bUseEvents) add_interupt_event_count(VI_INT, next_vi);

    add_interupt_event_count(SPECIAL_INT, 0);
}

void check_interupt(void)
{
	//DEBUG_PRINT("check_interupt\n");

    if (MI_register.mi_intr_reg & MI_register.mi_intr_mask_reg)
        Cause = (Cause | 0x400) & 0xFFFFFF83;
    else
        Cause &= ~0x400;
    if ((Status & 7) != 1) return;
    if (Status & Cause & 0xFF00)
    {
pthread_mutex_lock(&InterruptLock);
        if(q == NULL)
        {
            q = (interupt_queue *) queue_malloc(sizeof(interupt_queue));
            q->next = NULL;
            q->count = Count;
            q->type = CHECK_INT;
        }
        else
        {
            interupt_queue* aux = (interupt_queue *) queue_malloc(sizeof(interupt_queue));
            aux->next = q;
            aux->count = Count;
            aux->type = CHECK_INT;
            q = aux;
        }
        next_interupt = Count;
pthread_mutex_unlock(&InterruptLock);
    }
}

void X11_PumpEvents()
{
 	XEvent  xev;

	while (RPI_NextXEvent(&xev) )
	{   // check for events from the x-server
		switch (xev.type)
		{
			case MotionNotify:   // if mouse has moved
        				//xev.xmotion.x,xev.xmotion.y

				break;
			case ButtonPress:
				// xev.xbutton.state, xev.xbutton.button << endl;
				break;
			case KeyPress:
				event_sdl_keydown(xev.xkey.keycode, xev.xkey.state);
				break;
			case KeyRelease:
				event_sdl_keyup(xev.xkey.keycode, xev.xkey.state);	//TODO is this correct?
				break;
			default:
				break;
		}
	}
}


void gen_interupt(void)
{
	void _gen_interupt(void);
	if (!mt_s.bUseEvents) _gen_interupt();

}

void _gen_interupt(void)
{
	/*static int count=0, time=0;
	count++;

	if (count >= 500)
	{
		double f = (500.0)/(SDL_GetTicks() - time);
		DebugMessage(M64MSG_INFO, "gen_interrupt: %.3fKHz", f);
		count = 0;
		time = SDL_GetTicks();
	}*/

    if (stop == 1)
    {
        vi_counter = 0; // debug
        dyna_stop();
    }

    if (!interupt_unsafe_state)
    {
        if (savestates_get_job() == savestates_job_load)
        {
            savestates_load();
            return;
        }

        if (reset_hard_job)
        {
            reset_hard();
            reset_hard_job = 0;
            return;
        }
    }

    if (skip_jump)
    {
        unsigned int dest = skip_jump;
        skip_jump = 0;
		pthread_mutex_lock(&InterruptLock);
        if (q->count > Count || (Count - q->count) < 0x80000000)
            next_interupt = q->count;
        else
            next_interupt = 0;

        last_addr = dest;
		pthread_mutex_unlock(&InterruptLock);
        generic_jump_to(dest);

        return;
    }
	if (mt_s.bPrintDMsg) printf("\tgen_interupt() called, Count = %d\n", Count);
	unsigned int Flags = 0;

	//Non-blocking receive any of the following events
	Event_ReceiveAnyNB(VI_INT | AI_INT | SI_INT | PI_INT | SP_INT | DP_INT | NMI_INT | HW2_INT, &Flags);

	if (Flags & VI_INT)
	{
		if(vi_counter < 60)
        {
            if (vi_counter == 0)
                cheat_apply_cheats(ENTRY_BOOT);
            vi_counter++;
        }
        else
        {
            cheat_apply_cheats(ENTRY_VI);
        }

		if(mt_vi.bPrintDMsg) printf("\tVI_INT\n");

		//let the graphics thread start
		Event_Send(VI_INT_DRAW);

		SDL_PumpEvents();
		X11_PumpEvents();

        refresh_stat();

        // if paused, poll for input events
        if(rompause)
        {
            osd_render();  // draw Paused message in case gfx.updateScreen didn't do it
            //VidExt_GL_SwapBuffers(); - Not going to work with graphics in other thread!
            while(rompause)
            {
                SDL_Delay(10);
                SDL_PumpEvents();
				X11_PumpEvents();
            }
        }

        Event_ReceiveAll(VI_INT_DONE);

        new_vi();
        if (vi_register.vi_v_sync == 0)
		{
			vi_register.vi_delay = 500000;
		}
        else 
		{
			vi_register.vi_delay = ((vi_register.vi_v_sync + 1)*1500);
		}

        next_vi += vi_register.vi_delay;
        if (vi_register.vi_status&0x40) vi_field=1-vi_field;
        else vi_field=0;

		MI_register.mi_intr_reg |= 0x08;
        if (MI_register.mi_intr_reg & MI_register.mi_intr_mask_reg)
            Cause = (Cause | 0x400) & 0xFFFFFF83;
        else
            return;
        if ((Status & 7) != 1) return;
        if (!(Status & Cause & 0xFF00)) return;
	}

	if (Flags & SI_INT)	
	{
		if(mt_pi.bPrintDMsg) printf("\tSI_INT\n");
		SDL_PumpEvents();
        X11_PumpEvents();
	    PIF_RAMb[0x3F] = 0x0;

        MI_register.mi_intr_reg |= 0x02;
		si_register.si_stat |= 0x1000;
		if (MI_register.mi_intr_reg & MI_register.mi_intr_mask_reg)
			Cause = (Cause | 0x400) & 0xFFFFFF83;
		else
			return;
		if ((Status & 7) != 1) return;
		if (!(Status & Cause & 0xFF00)) return;
	}

	if (Flags & PI_INT)	
	{
		if(mt_pi.bPrintDMsg) printf("\tPI_INT\n");
        MI_register.mi_intr_reg |= 0x10;
        pi_register.read_pi_status_reg &= ~3;
        if (MI_register.mi_intr_reg & MI_register.mi_intr_mask_reg)
            Cause = (Cause | 0x400) & 0xFFFFFF83;
        else
            return;
        if ((Status & 7) != 1) return;
        if (!(Status & Cause & 0xFF00)) return;
	}

	if (Flags & AI_INT)	
	{
		if (ai_register.ai_status & 0x80000000) // full
        {
			if(mt_ai.bPrintDMsg) printf("\tAI_INT ai_status full\n");
            ai_register.ai_status &= ~0x80000000;
            ai_register.current_delay = ai_register.next_delay;
            ai_register.current_len = ai_register.next_len;

			Event_Send(AI_INT_PENDING);

            MI_register.mi_intr_reg |= 0x04;
            if (MI_register.mi_intr_reg & MI_register.mi_intr_mask_reg)
                Cause = (Cause | 0x400) & 0xFFFFFF83;
            else
                return;
            if ((Status & 7) != 1) return;
            if (!(Status & Cause & 0xFF00)) return;
        }
        else
        {
			if(mt_ai.bPrintDMsg) printf("\tAI_INT\n");
            ai_register.ai_status &= ~0x40000000;

            //-------
            MI_register.mi_intr_reg |= 0x04;
            if (MI_register.mi_intr_reg & MI_register.mi_intr_mask_reg)
                Cause = (Cause | 0x400) & 0xFFFFFF83;
            else
                return;
            if ((Status & 7) != 1) return;
            if (!(Status & Cause & 0xFF00)) return;
        }
	}

	if (Flags & SP_INT)	
	{
		if(mt_sp.bPrintDMsg) printf("\tSP_INT\n");
        sp_register.sp_status_reg |= 0x203;
        // sp_register.sp_status_reg |= 0x303;

        if (!(sp_register.sp_status_reg & 0x40)) return; // !intr_on_break
        MI_register.mi_intr_reg |= 0x01;
        if (MI_register.mi_intr_reg & MI_register.mi_intr_mask_reg)
            Cause = (Cause | 0x400) & 0xFFFFFF83;
        else
            return;
        if ((Status & 7) != 1) return;
        if (!(Status & Cause & 0xFF00)) return;
	}

	if (Flags & DP_INT)	
	{
		if(mt_dp.bPrintDMsg) printf("\tDP_INT\n");
        dpc_register.dpc_status &= ~2;
        dpc_register.dpc_status |= 0x81;
        MI_register.mi_intr_reg |= 0x20;
        if (MI_register.mi_intr_reg & MI_register.mi_intr_mask_reg)
            Cause = (Cause | 0x400) & 0xFFFFFF83;
        else
            return;
        if ((Status & 7) != 1) return;
        if (!(Status & Cause & 0xFF00)) return;
	}

	if (Flags & HW2_INT)	
	{
		// setup r4300 Status flags: reset TS, and SR, set IM2
        Status = (Status & ~0x00380000) | 0x1000;
        Cause = (Cause | 0x1000) & 0xFFFFFF83;
        /* the exception_general() call below will jump to the interrupt vector (0x80000180) and setup the
         * interpreter or dynarec
         */
	}

	if (Flags & NMI_INT)	
	{
		// Non Maskable Interrupt -- remove interrupt event from queue
        // setup r4300 Status flags: reset TS and SR, set BEV, ERL, and SR
        Status = (Status & ~0x00380000) | 0x00500004;
        Cause  = 0x00000000;
        // simulate the soft reset code which would run from the PIF ROM
        r4300_reset_soft();
        // clear all interrupts, reset interrupt counters back to 0
        Count = 0;
        vi_counter = 0;
        init_interupt();
        // clear the audio status register so that subsequent write_ai() calls will work properly
        ai_register.ai_status = 0;
        // set ErrorEPC with the last instruction address
        ErrorEPC = PC->addr;
        // reset the r4300 internal state
        if (r4300emu != CORE_PURE_INTERPRETER)
        {
            // clear all the compiled instruction blocks and re-initialize
            free_blocks();
            init_blocks();
        }
        // adjust ErrorEPC if we were in a delay slot, and clear the delay_slot and dyna_interp flags
        if(delay_slot==1 || delay_slot==3)
        {
            ErrorEPC-=4;
        }
        delay_slot = 0;
        dyna_interp = 0;
        // set next instruction address to reset vector
        last_addr = 0xa4000040;
		DEBUG_PRINT("generic_jump_to(0xa4000040)\n");
        generic_jump_to(0xa4000040);
	}

	pthread_mutex_lock(&InterruptLock);
	
	switch(q->type)
    {
        case SPECIAL_INT:
            if (Count > 0x10000000) 
			{
				pthread_mutex_unlock(&InterruptLock);
				return;
			}
            remove_interupt_event();
			pthread_mutex_unlock(&InterruptLock);
            add_interupt_event_count(SPECIAL_INT, 0);
            return;
            break;
        case VI_INT:
            if(vi_counter < 60)
            {
                if (vi_counter == 0)
                    cheat_apply_cheats(ENTRY_BOOT);
                vi_counter++;
            }
            else
            {
                cheat_apply_cheats(ENTRY_VI);
            }

			remove_interupt_event();
			pthread_mutex_unlock(&InterruptLock);
			if(mt_vi.bPrintDMsg) printf("\tVI_INT\n");
			//let the graphics thread start
			Event_Send(VI_INT_DRAW);

			SDL_PumpEvents();
			X11_PumpEvents();

            refresh_stat();

            // if paused, poll for input events
            if(rompause)
            {
                osd_render();  // draw Paused message in case gfx.updateScreen didn't do it
                //VidExt_GL_SwapBuffers(); - Not going to work with graphics in other thread!
                while(rompause)
                {
                    SDL_Delay(10);
                    SDL_PumpEvents();
					X11_PumpEvents();
                }
            }

            Event_ReceiveAll(VI_INT_DONE);

            new_vi();
            if (vi_register.vi_v_sync == 0)
			{
				vi_register.vi_delay = 500000;
			}
            else 
			{
				vi_register.vi_delay = ((vi_register.vi_v_sync + 1)*1500);
			}

            next_vi += vi_register.vi_delay;
            if (vi_register.vi_status&0x40) vi_field=1-vi_field;
            else vi_field=0;

           // add_interupt_event_count(VI_INT, next_vi);

			MI_register.mi_intr_reg |= 0x08;
            if (MI_register.mi_intr_reg & MI_register.mi_intr_mask_reg)
                Cause = (Cause | 0x400) & 0xFFFFFF83;
            else
                return;
            if ((Status & 7) != 1) return;
            if (!(Status & Cause & 0xFF00)) return;
            break;

        case COMPARE_INT:
            remove_interupt_event();
			pthread_mutex_unlock(&InterruptLock);
            Count+=2;
            add_interupt_event_count(COMPARE_INT, Compare);
            Count-=2;

            Cause = (Cause | 0x8000) & 0xFFFFFF83;
            if ((Status & 7) != 1) return;
            if (!(Status & Cause & 0xFF00)) return;
            break;

        case CHECK_INT:
            remove_interupt_event();
			pthread_mutex_unlock(&InterruptLock);
            break;
        case SI_INT:
            SDL_PumpEvents();
            X11_PumpEvents();
	    PIF_RAMb[0x3F] = 0x0;
            remove_interupt_event();
			pthread_mutex_unlock(&InterruptLock);
			if(mt_si.bPrintDMsg) printf("\tSI_INT\n");
            MI_register.mi_intr_reg |= 0x02;
            si_register.si_stat |= 0x1000;
            if (MI_register.mi_intr_reg & MI_register.mi_intr_mask_reg)
                Cause = (Cause | 0x400) & 0xFFFFFF83;
            else
                return;
            if ((Status & 7) != 1) return;
            if (!(Status & Cause & 0xFF00)) return;
            break;
        case PI_INT:
            remove_interupt_event();
			pthread_mutex_unlock(&InterruptLock);
			if(mt_pi.bPrintDMsg) printf("\tPI_INT\n");
            MI_register.mi_intr_reg |= 0x10;
            pi_register.read_pi_status_reg &= ~3;
            if (MI_register.mi_intr_reg & MI_register.mi_intr_mask_reg)
                Cause = (Cause | 0x400) & 0xFFFFFF83;
            else
                return;
            if ((Status & 7) != 1) return;
            if (!(Status & Cause & 0xFF00)) return;
            break;
        case AI_INT:
            if (ai_register.ai_status & 0x80000000) // full
            {
                unsigned int ai_event = get_event(AI_INT);
                remove_interupt_event();
				pthread_mutex_unlock(&InterruptLock);
				if(mt_ai.bPrintDMsg) printf("\tAI_INT ai_status full\n");
                ai_register.ai_status &= ~0x80000000;
                ai_register.current_delay = ai_register.next_delay;
                ai_register.current_len = ai_register.next_len;
                add_interupt_event_count(AI_INT, ai_event+ai_register.next_delay);
                MI_register.mi_intr_reg |= 0x04;
                if (MI_register.mi_intr_reg & MI_register.mi_intr_mask_reg)
                    Cause = (Cause | 0x400) & 0xFFFFFF83;
                else
                    return;
                if ((Status & 7) != 1) return;
                if (!(Status & Cause & 0xFF00)) return;
            }
            else
            {
                remove_interupt_event();
				pthread_mutex_unlock(&InterruptLock);
				if(mt_ai.bPrintDMsg) printf("\tAI_INT\n");
                ai_register.ai_status &= ~0x40000000;

                //-------
                MI_register.mi_intr_reg |= 0x04;
                if (MI_register.mi_intr_reg & MI_register.mi_intr_mask_reg)
                    Cause = (Cause | 0x400) & 0xFFFFFF83;
                else
                    return;
                if ((Status & 7) != 1) return;
                if (!(Status & Cause & 0xFF00)) return;
            }
            break;

        case SP_INT:
            remove_interupt_event();
			pthread_mutex_unlock(&InterruptLock);
			if(mt_sp.bPrintDMsg) printf("\tSP_INT\n");
            sp_register.sp_status_reg |= 0x203;
            // sp_register.sp_status_reg |= 0x303;

            if (!(sp_register.sp_status_reg & 0x40)) return; // !intr_on_break
            MI_register.mi_intr_reg |= 0x01;
            if (MI_register.mi_intr_reg & MI_register.mi_intr_mask_reg)
                Cause = (Cause | 0x400) & 0xFFFFFF83;
            else
                return;
            if ((Status & 7) != 1) return;
            if (!(Status & Cause & 0xFF00)) return;
            break;

        case DP_INT:
            remove_interupt_event();
			pthread_mutex_unlock(&InterruptLock);
			if(mt_dp.bPrintDMsg) printf("\tDP_INT\n");
            dpc_register.dpc_status &= ~2;
            dpc_register.dpc_status |= 0x81;
            MI_register.mi_intr_reg |= 0x20;
            if (MI_register.mi_intr_reg & MI_register.mi_intr_mask_reg)
                Cause = (Cause | 0x400) & 0xFFFFFF83;
            else
                return;
            if ((Status & 7) != 1) return;
            if (!(Status & Cause & 0xFF00)) return;
            break;

        case HW2_INT:
            // Hardware Interrupt 2 -- remove interrupt event from queue
            remove_interupt_event();
            // setup r4300 Status flags: reset TS, and SR, set IM2
            Status = (Status & ~0x00380000) | 0x1000;
            Cause = (Cause | 0x1000) & 0xFFFFFF83;
            /* the exception_general() call below will jump to the interrupt vector (0x80000180) and setup the
             * interpreter or dynarec
             */
            break;

        case NMI_INT:
            // Non Maskable Interrupt -- remove interrupt event from queue
            remove_interupt_event();
			pthread_mutex_unlock(&InterruptLock);
            // setup r4300 Status flags: reset TS and SR, set BEV, ERL, and SR
            Status = (Status & ~0x00380000) | 0x00500004;
            Cause  = 0x00000000;
            // simulate the soft reset code which would run from the PIF ROM
            r4300_reset_soft();
            // clear all interrupts, reset interrupt counters back to 0
            Count = 0;
            vi_counter = 0;
            init_interupt();
            // clear the audio status register so that subsequent write_ai() calls will work properly
            ai_register.ai_status = 0;
            // set ErrorEPC with the last instruction address
            ErrorEPC = PC->addr;
            // reset the r4300 internal state
            if (r4300emu != CORE_PURE_INTERPRETER)
            {
                // clear all the compiled instruction blocks and re-initialize
                free_blocks();
                init_blocks();
            }
            // adjust ErrorEPC if we were in a delay slot, and clear the delay_slot and dyna_interp flags
            if(delay_slot==1 || delay_slot==3)
            {
                ErrorEPC-=4;
            }
            delay_slot = 0;
            dyna_interp = 0;
            // set next instruction address to reset vector
            last_addr = 0xa4000040;
			DEBUG_PRINT("generic_jump_to(0xa4000040)\n");
            generic_jump_to(0xa4000040);
            return;

        default:
            DebugMessage(M64MSG_ERROR, "Unknown interrupt queue event type %.8X.", q->type);
            remove_interupt_event();
			pthread_mutex_unlock(&InterruptLock);
            break;
	}

#ifdef NEW_DYNAREC
    if (r4300emu == CORE_DYNAREC) {
		//DEBUG_PRINT("Setting PC for Dynarec %X\n", pcaddr);
        EPC = pcaddr;
        pcaddr = 0x80000180;
        Status |= 2;
        Cause &= 0x7FFFFFFF;
        pending_exception=1;
    } else {
        exception_general();
    }
#else
    exception_general();
#endif

    if (!interupt_unsafe_state)
    {
        if (savestates_get_job() == savestates_job_save)
        {
            savestates_save();
            return;
        }
    }
}

