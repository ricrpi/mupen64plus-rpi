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


#define VI_INT      		0x00001
#define VI_INT_DONE			0x00002
#define VI_INT_DRAW			0x00004
#define VI_INT_DLIST		0x00008

#define AI_INT      		0x00010
#define AI_INT_DONE			0x00020
#define AI_INT_PENDING		0x00040

#define COMPARE_INT 		0x00100
#define CHECK_INT   		0x00200
#define SI_INT      		0x00400
#define PI_INT      		0x00800
#define SPECIAL_INT 		0x01000
#define SP_INT      		0x02000
#define DP_INT      		0x04000
#define HW2_INT     		0x08000
#define NMI_INT     		0x10000

#define VI_INT_NEXT			0x20000

/*
#define VI_INT      0x001
#define COMPARE_INT 0x002
#define CHECK_INT   0x003
#define SI_INT      0x004
#define PI_INT      0x005
#define SPECIAL_INT 0x006
#define AI_INT      0x007
#define SP_INT      0x008
#define DP_INT      0x009
#define HW2_INT     0x00A
#define NMI_INT     0x00B
*/
#define INT_COUNT	15

