/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - interupt.h                                              *
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

void compare_interupt(void);
void gen_dp(void);
void init_interupt(void);

extern int vi_field;
extern unsigned int next_vi;

// set to avoid savestates/reset if state may be inconsistent
// (e.g. in the middle of an instruction)
extern int interupt_unsafe_state;

void gen_interupt(void);
void check_interupt(void);

void translate_event_queue(unsigned int base);
void remove_event(int type);
void add_interupt_event_count(int type, unsigned int count);
void add_interupt_event(int type, unsigned int delay);
unsigned int get_event(int type);
int get_next_event_type(void);

int save_eventqueue_infos(char *buf);
void load_eventqueue_infos(char *buf);

#define VI_INT      		0x00000001
#define VI_INT_DONE			0x00000002
#define VI_INT_DRAW			0x00000004
#define VI_INT_DLIST		0x00000008
#define VI_INT_PENDING		0x00010000

#define AI_INT      		0x00000010
#define AI_INT_DONE			0x00000020
#define AI_INT_PENDING		0x00100000

#define SI_INT      		0x00000400
#define SI_INT_PENDING		0x04000000

#define PI_INT      		0x00000800
#define PI_INT_PENDING		0x08000000

#define SP_INT      		0x00001000
#define SP_INT_PENDING		0x10000000

#define DP_INT      		0x00002000
#define DP_INT_PENDING		0x20000000

#define HW2_INT     		0x00004000
#define	HW2_INT_PENDING		0x40000000

#define NMI_INT     		0x00008000

#define COMPARE_INT 		0x00020000
#define CHECK_INT   		0x00040000
#define SPECIAL_INT 		0x00080000

typedef struct _mt_thread {
	unsigned int uiPriority;
	unsigned int uiWait;
	unsigned int bUseEvents;
	unsigned int bPrintDMsg;
} mt_thread;

typedef struct _mt_options {
	unsigned int bEventYields;
	unsigned int bEventDMsg;
} mt_options;

extern mt_options mt_o;
extern mt_thread mt_s, mt_vi, mt_ai, mt_pi, mt_si, mt_dp, mt_sp, mt_nmi, mt_hw2;

