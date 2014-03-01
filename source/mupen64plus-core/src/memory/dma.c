/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - dma.c                                                   *
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
#include <string.h>
#include <stdio.h>
#include <stdint.h>		// types
#include <unistd.h>		// usleep()
#include <sys/mman.h>	// mmap()
#include <sys/time.h>	// gettimeofday() for debug
#include <fcntl.h>

#include "api/m64p_types.h"

#include "dma.h"
#include "memory.h"
#include "pif.h"
#include "flashram.h"

#include "r4300/r4300.h"
#include "r4300/interupt.h"
#include "r4300/macros.h"
#include "r4300/ops.h"
#include "../r4300/new_dynarec/new_dynarec.h"

#define M64P_CORE_PROTOTYPES 1
#include "api/m64p_config.h"
#include "api/config.h"
#include "api/callbacks.h"
#include "main/main.h"
#include "main/rom.h"
#include "main/util.h"
#include "r4300/new_dynarec/assem_arm.h"

//---------------------------------------------

#define PRINT_DMA_MSG(...) 		DebugMessage(__VA_ARGS__)

#ifndef PRINT_DMA_MSG
#define PRINT_DMA_MSG(...)
#endif

#define DMA_CHAN_SIZE           0x100
#define DMA_CHAN_MIN            0
#define DMA_CHAN_MAX            14
#define DMA_CHAN_DEFAULT        14

#if 1
#define DMA_BASE                0x20007000
#else
#define DMA_BASE				0x7E007000	// BCM2835 ARM Peripherals.pdf says the address is this
#endif

#define DMA_LEN                 DMA_CHAN_SIZE * (DMA_CHAN_MAX+1)

#define DMA_CS 					(0x00/4)
#define DMA_CONBLK_AD 			(0x04/4)
#define DMA_TI 					(0x08/4)
#define DMA_SRC 				(0x0C/4)
#define DMA_DEST 				(0x10/4)
#define DMA_TLEN 				(0x14/4)
#define DMA_STRIDE				(0x18/4)
#define DMA_NEXT				(0x1C/4)
#define DMA_DEBUG 				(0x20/4)

#define DMA_TI_NO_WIDE_BURSTS 					(1<<26)
#define DMA_TI_D_DREQ 							(0x00000040)
#define DMA_TI_PER_MAP(x) 						((x)<<16)
#define DMA_TI_SRC_IGNORE						(0x00000800)	// ???
#define DMA_TI_SRC_DREQ							(0x00000400)	// 1 DREQ selected by PERMAP will gate the src read 
#define DMA_TI_SRC_WIDTH						(0x00000200)	// 1 = 128-bit read width, 0 = 32-bit
#define DMA_TI_SRC_INC							(0x00000100)	// 1 addr+=4 if SRC_WIDTH=0 else addr+=32, 0 Src addr does not inc
#define DMA_TI_DST_IGNORE						(0x00000080)	// 0 write data to dest
#define DMA_TI_DST_DREQ							(0x00000040)	// 1 DREQ selected by PERMAP will gate the dest writes 
#define DMA_TI_DST_WIDTH						(0x00000020)	// 1 = 128-bit write width, 0 = 32-bit
#define DMA_TI_DST_INC							(0x00000010)	// 1 addr+=4 if DEST_WIDTH=0 else addr+=32, 0 Dest addr does not inc
#define DMA_TI_WAIT_RESP 						(0x00000008)
#define DMA_TI_2D		 						(0x00000002)
#define DMA_TI_INT		 						(0x00000001)

#define DMA_CS_RESET 							(0x80000000)
#define DMA_CS_ABORT 							(0x40000000)
#define DMA_CS_DISDEBUG 						(0x20000000)
#define DMA_CS_WAIT_FOR_OUTSTANDING_WRITES		(0x10000000)
#define DMA_CS_PANIC_PRIORITY			 		(0x00F00000)
#define DMA_CS_PRIORITY 						(0x000F0000)
#define DMA_CS_ERROR							(0x00000100)
#define DMA_CS_WAITING_FOR_OUTSTANDING_WRITES	(0x00000040)
#define DMA_CS_DREQ_STOPS_DMA 					(0x00000020)
#define DMA_CS_PAUSED 							(0x00000010)
#define DMA_CS_DREQ 							(0x00000008)
#define DMA_CS_INT 								(0x00000004)
#define DMA_CS_END 								(0x00000002)
#define DMA_CS_ACTIVE 							(0x00000001)

#define DMA_DBG_LITE							(0x10000000)
#define DMA_DBG_VERSION							(0x0E000000)
#define DMA_DBG_STATE							(0x01FF0000)
#define DMA_DBG_ID								(0x0000FF00)
#define DMA_DBG_OUTSTANDING_WRITES				(0x000000F0)
#define DMA_DBG_READ_ERROR						(0x00000004)
#define DMA_DBG_FIFO_ERROR						(0x00000002)
#define DMA_DBG_READ_LAST_NOT_SET_ERROR			(0x00000001)

//----------------------------------------------

/*
Switching off DMA_CHECKING could be dangerous as it may be possible for a game to write into physical memory not used by the emulator (including at 0x0!).
Pages are often grouped together in 32's but this will not cover rdram space

On a Bare-metal system it should be possible to switch checking off as we would have control of the whole memory.

+---------+---------+-------+
| memory  |   Bytes | Pages |
+---------+---------+-------+
| rdram   | 2097152 |   512 | 
| SP_DMEM |    1280 |    <1 |
| PIF     |      16 |    <1 |
| sram    |   32768 |     8 |
| rom	  |       ? |     ? |
+---------+---------+-------+
*/

#define DMA_CHECKING				// must be defined with linux!
//#define DMA_TIMING				// print time to perform DMA (SW copy / HW Setup Overhead)
#define DMA_SW_THRESHOLD	1000	// used to revert to SW copying when data is small (and avoid DMA overhead)

//----------------------------------------------

unsigned char* 				sram;
unsigned int 				dmaMode 	= 0;
dma_cb_t*					cb_base;

static volatile uint32_t* 	dma_reg 	= NULL;
static int 					dma_chan 	= DMA_CHAN_DEFAULT;
static uint32_t 			physical_cb_base;
//----------------------------------------------

#ifdef M64P_ALLOCATE_MEMORY

#ifdef DMA_CHECKING
static uint32_t mem_virt_to_phys(void *virt, uint32_t*numContiguous)
#else
static uint32_t mem_virt_to_phys(void *virt)
#endif
{
   	uint32_t offset = (uint8_t *)virt - n64_memory;

#ifdef DMA_CHECKING
	if (virt > (void*)cb_base || virt < (void*)n64_memory)	//cb_base is the last region in the mmap'ed space
	{

		DebugMessage(M64MSG_ERROR, "DMA invalid location %p, offset %d, page %d", virt, offset, (offset >> PAGE_SHIFT));
		*numContiguous = 0;
		return 0;
	}
	if (numContiguous) *numContiguous = n64_memory_map[offset >> PAGE_SHIFT].numContiguous;
#endif
	
	return n64_memory_map[offset >> PAGE_SHIFT].physaddr + (offset & ~(PAGE_SIZE-1));
}
#endif

static void * map_peripheral(uint32_t base, uint32_t len)
{
    int fd = open("/dev/mem", O_RDWR);
    void * vaddr;

    if (fd < 0)
	{
            DebugMessage(M64MSG_ERROR, "%d, Failed to open /dev/mem: %m", __LINE__);
    		return NULL;
	}
	else
	{
		vaddr = mmap(NULL, len, PROT_READ|PROT_WRITE, MAP_SHARED, fd, base);

		if (vaddr == MAP_FAILED)
		{
			DebugMessage(M64MSG_ERROR, "Failed to map peripheral at 0x%08x: %m\n", base);
			return NULL;
		}

		close(fd);
		return vaddr;
	}

	dmaMode = 1;
	DebugMessage(M64MSG_ERROR, "Cannot get DMA Controller");

	return NULL;
}

#ifdef M64P_ALLOCATE_MEMORY
void dma_WaitComplete(unsigned int type)
{
	if (2 == dmaMode)
	{
//		DebugMessage(M64MSG_INFO, "dma_WaitForComplete(%d), CS 0x%X, BLK 0x%X, DBG 0x%X", type, dma_reg[DMA_CS], dma_reg[DMA_CONBLK_AD], dma_reg[DMA_DEBUG]);
		int x;
		for (x=0; x< 20; x++)
		{
			if(!(dma_reg[DMA_CS] & DMA_CS_ACTIVE))	break;
#if 0
			DebugMessage(M64MSG_INFO, "%3d dma_WaitForComplete(%d), CS 0x%X, BLK 0x%X, TI 0x%X, SRC 0x%X, DST 0x%X, DBG 0x%X", 
				x, type, dma_reg[DMA_CS], dma_reg[DMA_CONBLK_AD], dma_reg[DMA_TI], dma_reg[DMA_SRC], dma_reg[DMA_DEST], dma_reg[DMA_DEBUG]);
#endif
			usleep(1);
		}
		dma_reg[DMA_CS] |= DMA_CS_END | DMA_CS_RESET;
	}
}
#endif

void dma_initialize()
{
#ifdef M64P_ALLOCATE_MEMORY
	if (2 == dmaMode)
	{
		dma_reg = map_peripheral(DMA_BASE, DMA_LEN);

		if (!dma_reg)
		{
			dmaMode = 1;
			DebugMessage(M64MSG_INFO, "DMA will be done by Software Mode %d",dmaMode); 
			return;
		}

#ifdef DMA_CHECKING
		physical_cb_base = mem_virt_to_phys(cb_base,0);
#else
		physical_cb_base = mem_virt_to_phys(cb_base);
#endif
		cb_base->NEXTCONBK = 0;

		//move DMA pointer to desired channel
		dma_reg += dma_chan * DMA_CHAN_SIZE / sizeof(uint32_t);

		dma_reg[DMA_CS] |= DMA_CS_RESET;

        usleep(10);

		dma_reg[DMA_CS] |=  DMA_CS_INT | DMA_CS_END;
		dma_reg[DMA_CONBLK_AD] = physical_cb_base;
        dma_reg[DMA_DEBUG] = 7; // clear debug error flags
        dma_reg[DMA_CS] = 0x10880001;        // go, mid priority, wait for outstanding writes

		DebugMessage(M64MSG_INFO, "Hardware DMA Initialized. CB addr %p, CS 0x%X", physical_cb_base, dma_reg[DMA_CS]); 
	}
#endif
}

#ifdef M64P_ALLOCATE_MEMORY
void dma_copy_multiple(void* from, void* to, uint32_t len, uint32_t skip, uint32_t loop)
{
	dma_WaitComplete(0);

#ifdef DMA_CHECKING
	uint32_t numBoundaryCrossingsSrc = (((uint32_t)from + len) >> PAGE_SHIFT) - ((uint32_t)from >> PAGE_SHIFT);
	uint32_t numBoundaryCrossingsDst = (((uint32_t)to + len) >> PAGE_SHIFT) - ((uint32_t)to >> PAGE_SHIFT);

	uint32_t numContiguousSrc, numContiguousDst;

	uint32_t physicalSrc = mem_virt_to_phys(from, &numContiguousSrc);
	uint32_t physicalDest = mem_virt_to_phys(to, &numContiguousDst);

	// Can we do a transfer in one hit?
	if (numContiguousSrc >= numBoundaryCrossingsSrc && numContiguousDst >= numBoundaryCrossingsDst && physicalSrc && physicalDest)
	{

		cb_base->TI = DMA_TI_NO_WIDE_BURSTS | DMA_TI_WAIT_RESP | DMA_TI_DST_INC | DMA_TI_SRC_INC;
 		cb_base->SOURCE_AD = physicalSrc;
		cb_base->DEST_AD = physicalDest;
		cb_base->LENGTH = len;
		cb_base->STRIDE = skip;
		
		//start the transfer
		dma_reg[DMA_CONBLK_AD] = (uint32_t)physical_cb_base;
		dma_reg[DMA_CS] |= DMA_CS_ACTIVE;
	}
	else
	{
		DebugMessage(M64MSG_WARNING, "%d dma panic! %p =>%p, length %d. %d %d %d %d", __LINE__, physicalSrc, physicalDest, len, numBoundaryCrossingsSrc, numBoundaryCrossingsDst, numContiguousSrc, numContiguousDst); 
		
		unsigned int j;
		for(j=0; j<loop; j++) 
		{
			memcpy(to, from, len);

			to += len/4;
			from += (len + skip)/4;
		}
	}
#else
		cb_base->TI = DMA_TI_NO_WIDE_BURSTS | DMA_TI_WAIT_RESP | DMA_TI_DST_INC | DMA_TI_SRC_INC;
 		cb_base->SOURCE_AD = mem_virt_to_phys(from);
		cb_base->DEST_AD = mem_virt_to_phys(to);
		cb_base->LENGTH = len;
		cb_base->STRIDE = skip;
		
		//start the transfer
		dma_reg[DMA_CONBLK_AD] = (uint32_t)physical_cb_base;
		dma_reg[DMA_CS] |= DMA_CS_ACTIVE;
#endif
}

void dma_copy(void* from, void* to, uint32_t len)
{
	dma_WaitComplete(0);
	
#ifdef DMA_CHECKING
	uint32_t numBoundaryCrossingsSrc = (((uint32_t)from + len) >> PAGE_SHIFT) - ((uint32_t)from >> PAGE_SHIFT);
	uint32_t numBoundaryCrossingsDst = (((uint32_t)to + len) >> PAGE_SHIFT) - ((uint32_t)to >> PAGE_SHIFT);

	uint32_t numContiguousSrc, numContiguousDst;

	uint32_t physicalSrc = mem_virt_to_phys(from, &numContiguousSrc);
	uint32_t physicalDest = mem_virt_to_phys(to, &numContiguousDst);
	
	// Can we do a transfer in one hit?
	if (numContiguousSrc >= numBoundaryCrossingsSrc && numContiguousDst >= numBoundaryCrossingsDst && physicalSrc && physicalDest)
	{
		cb_base->TI = DMA_TI_NO_WIDE_BURSTS | DMA_TI_WAIT_RESP | DMA_TI_DST_INC | DMA_TI_SRC_INC;
 		cb_base->SOURCE_AD = physicalSrc;
		cb_base->DEST_AD = physicalDest;
		cb_base->LENGTH = len;
		cb_base->STRIDE = 0;
		
		//start the transfer
		dma_reg[DMA_CONBLK_AD] = (uint32_t)physical_cb_base;
		dma_reg[DMA_CS] |= DMA_CS_ACTIVE;
	}
	else
	{
		DebugMessage(M64MSG_WARNING, "%d dma panic! %p =>%p, length %d. %d %d %d %d", __LINE__, physicalSrc, physicalDest, len, numBoundaryCrossingsSrc, numBoundaryCrossingsDst, numContiguousSrc, numContiguousDst); 
		memcpy(to, from, len);
	}
#else
	cb_base->TI = DMA_TI_NO_WIDE_BURSTS | DMA_TI_WAIT_RESP | DMA_TI_DST_INC | DMA_TI_SRC_INC;
	cb_base->SOURCE_AD = mem_virt_to_phys(from);
	cb_base->DEST_AD = mem_virt_to_phys(to);
	cb_base->LENGTH = len;
	cb_base->STRIDE = 0;
	
	//start the transfer
	dma_reg[DMA_CONBLK_AD] = (uint32_t)physical_cb_base;
	dma_reg[DMA_CS] |= DMA_CS_ACTIVE;
#endif
}
#endif //M64P_ALLOCATE_MEMORY

void dma_close(void)
{
#ifdef M64P_ALLOCATE_MEMORY
	if (2 == dmaMode && dma_reg && n64_memory)
	{
		dma_reg[DMA_CS] |= DMA_CS_RESET;
    }
#endif
}

static char *get_sram_path(void)
{
    return formatstr("%s%s.sra", get_savesrampath(), ROM_SETTINGS.goodname);
}

static void sram_format(void)
{
    memset(sram, 0, sizeof(sram));
}

static void sram_read_file(void)
{
    char *filename = get_sram_path();

    sram_format();
    switch (read_from_file(filename, sram, sizeof(sram)))
    {
        case file_open_error:
            DebugMessage(M64MSG_VERBOSE, "couldn't open sram file '%s' for reading", filename);
            sram_format();
            break;
        case file_read_error:
            DebugMessage(M64MSG_WARNING, "fread() failed on 32kb read from sram file '%s'", filename);
            sram_format();
            break;
        default: break;
    }

    free(filename);
}

static void sram_write_file(void)
{
    char *filename = get_sram_path();

    switch (write_to_file(filename, sram, sizeof(sram)))
    {
        case file_open_error:
            DebugMessage(M64MSG_WARNING, "couldn't open sram file '%s' for writing.", filename);
            break;
        case file_write_error:
            DebugMessage(M64MSG_WARNING, "fwrite() failed on 32kb write to sram file '%s'", filename);
            break;
        default: break;
    }

    free(filename);
}

void dma_pi_read(void)
{
    if (pi_register.pi_cart_addr_reg >= 0x08000000
            && pi_register.pi_cart_addr_reg < 0x08010000)
    {
        if (flashram_info.use_flashram != 1)
        {
            sram_read_file();
#ifdef DMA_TIMING
			struct timeval ts,te;
			gettimeofday(&ts,NULL);
#endif			
#if defined(M64P_ALLOCATE_MEMORY)
			if (2 == dmaMode && (pi_register.pi_rd_len_reg & 0xFFFFFF)+1 > DMA_SW_THRESHOLD)
			{
				/*DebugMessage(M64MSG_INFO, "dma_pi_read %p => %p (%d %d), pi_register.pi_dram_addr_reg %x, %x", 
					&((unsigned char*)rdram)[pi_register.pi_dram_addr_reg], 
					&sram[(pi_register.pi_cart_addr_reg-0x08000000)&0xFFFF],
					((unsigned char*)rdram)[pi_register.pi_dram_addr_reg], 
					sram[(pi_register.pi_cart_addr_reg-0x08000000)&0xFFFF],
					pi_register.pi_dram_addr_reg,
					(pi_register.pi_cart_addr_reg-0x08000000)&0xFFFF);
*/
				dma_copy(&((unsigned char*)rdram)[pi_register.pi_dram_addr_reg], &sram[(pi_register.pi_cart_addr_reg-0x08000000)&0xFFFF], (pi_register.pi_rd_len_reg & 0xFFFFFF)+1);
				
				/*DebugMessage(M64MSG_INFO, "dma_pi_read %p => %p (%d %d)", 
					&rdram[pi_register.pi_dram_addr_reg], 
					&sram[(pi_register.pi_cart_addr_reg-0x08000000)&0xFFFF],
					rdram[pi_register.pi_dram_addr_reg], 
					sram[(pi_register.pi_cart_addr_reg-0x08000000)&0xFFFF]);*/
			}
			else if (dmaMode)
#else
			if (dmaMode)
#endif
			{
				memcpy(&sram[(pi_register.pi_cart_addr_reg-0x08000000)&0xFFFF], &((unsigned char*)rdram)[pi_register.pi_dram_addr_reg], (pi_register.pi_rd_len_reg & 0xFFFFFF)+1);
			}
			else
			{
				unsigned int i;
		        for (i=0; i < (pi_register.pi_rd_len_reg & 0xFFFFFF)+1; i++)
		        {
		            sram[((pi_register.pi_cart_addr_reg-0x08000000)+i)^S8] =
		                ((unsigned char*)rdram)[(pi_register.pi_dram_addr_reg+i)^S8];
		        }
			}
#ifdef DMA_TIMING
			gettimeofday(&te,NULL);
			DebugMessage(M64MSG_INFO, "%d dma took %dus for length %d", __LINE__, 1000000 * te.tv_sec + te.tv_usec - (1000000 * ts.tv_sec + ts.tv_usec), (pi_register.pi_rd_len_reg & 0xFFFFFF)+1 );
#endif
			sram_write_file();

            flashram_info.use_flashram = -1;
        }
        else
        {
            dma_write_flashram();
        }
    }
    else
    {
        DebugMessage(M64MSG_WARNING, "Unknown dma read in dma_pi_read()");
    }

    pi_register.read_pi_status_reg |= 1;
    update_count();
    add_interupt_event(PI_INT, 0x1000/*pi_register.pi_rd_len_reg*/);
}

void dma_pi_write(void)
{
    unsigned int longueur;
    int i;

    if (pi_register.pi_cart_addr_reg < 0x10000000)
    {
        if (pi_register.pi_cart_addr_reg >= 0x08000000
                && pi_register.pi_cart_addr_reg < 0x08010000)
        {
            if (flashram_info.use_flashram != 1)
            {
                sram_read_file();
#ifdef DMA_TIMING
				struct timeval ts,te;
				gettimeofday(&ts,NULL);
#endif
				//PRINT_DMA_MSG(M64MSG_INFO, "DMA %d %d %x > %x", __LINE__, pi_register.pi_wr_len_reg & 0xFFFFFF, sram, rdram );
#if defined(M64P_ALLOCATE_MEMORY)
				if (2 == dmaMode && (pi_register.pi_wr_len_reg & 0xFFFFFF)+1 > DMA_SW_THRESHOLD)
				{
					/*DebugMessage(M64MSG_INFO, "dma_pi_write %p => %p (%d %d), pi_register.pi_dram_addr_reg %x, %x", 
						&sram[(pi_register.pi_cart_addr_reg-0x08000000)&0xFFFF], 
						&((unsigned char*)rdram)[pi_register.pi_dram_addr_reg],
						sram[(pi_register.pi_cart_addr_reg-0x08000000)&0xFFFF], 
						((unsigned char*)rdram)[pi_register.pi_dram_addr_reg],
						(pi_register.pi_cart_addr_reg-0x08000000)&0xFFFF,
						pi_register.pi_dram_addr_reg);*/

					dma_copy(&sram[(pi_register.pi_cart_addr_reg-0x08000000)&0xFFFF], &((unsigned char*)rdram)[pi_register.pi_dram_addr_reg], (pi_register.pi_wr_len_reg & 0xFFFFFF)+1);

					/*DebugMessage(M64MSG_INFO, "dma_pi_write %p => %p (%d %d)", 
						&sram[(pi_register.pi_cart_addr_reg-0x08000000)&0xFFFF], 
						&rdram[pi_register.pi_dram_addr_reg],
						sram[(pi_register.pi_cart_addr_reg-0x08000000)&0xFFFF], 
						rdram[pi_register.pi_dram_addr_reg]);*/
				}
				else if (dmaMode)
#else
				if (dmaMode)
#endif
				{
					memcpy(&((unsigned char*)rdram)[pi_register.pi_dram_addr_reg], &sram[(pi_register.pi_cart_addr_reg-0x08000000)&0xFFFF], (pi_register.pi_wr_len_reg & 0xFFFFFF)+1);
				}
				else
				{
		            for (i=0; i<(int)(pi_register.pi_wr_len_reg & 0xFFFFFF)+1; i++)
		            {
		                ((unsigned char*)rdram)[(pi_register.pi_dram_addr_reg+i)^S8]=
		                    sram[(((pi_register.pi_cart_addr_reg-0x08000000)&0xFFFF)+i)^S8];
		            }
				}
#ifdef DMA_TIMING
				gettimeofday(&te,NULL);
				DebugMessage(M64MSG_INFO, "%d dma took %dus for length %d", __LINE__, 1000000 * te.tv_sec + te.tv_usec - (1000000 * ts.tv_sec + ts.tv_usec), (pi_register.pi_rd_len_reg & 0xFFFFFF)+1 );
#endif
                flashram_info.use_flashram = -1;
            }
            else
            {
                dma_read_flashram();
            }
        }
        else if (pi_register.pi_cart_addr_reg >= 0x06000000
                 && pi_register.pi_cart_addr_reg < 0x08000000)
        {
        }
        else
        {
            DebugMessage(M64MSG_WARNING, "Unknown dma write 0x%x in dma_pi_write()", (int)pi_register.pi_cart_addr_reg);
        }

        pi_register.read_pi_status_reg |= 1;
        update_count();
        add_interupt_event(PI_INT, /*pi_register.pi_wr_len_reg*/0x1000);

        return;
    }

    if (pi_register.pi_cart_addr_reg >= 0x1fc00000) // for paper mario
    {
        pi_register.read_pi_status_reg |= 1;
        update_count();
        add_interupt_event(PI_INT, 0x1000);

        return;
    }

    longueur = (pi_register.pi_wr_len_reg & 0xFFFFFF)+1;
    i = (pi_register.pi_cart_addr_reg-0x10000000)&0x3FFFFFF;
    longueur = (i + (int) longueur) > rom_size ?
               (rom_size - i) : longueur;
    longueur = (pi_register.pi_dram_addr_reg + longueur) > 0x7FFFFF ?
               (0x7FFFFF - pi_register.pi_dram_addr_reg) : longueur;

    if (i>rom_size || pi_register.pi_dram_addr_reg > 0x7FFFFF)
    {
        pi_register.read_pi_status_reg |= 3;
        update_count();
        add_interupt_event(PI_INT, longueur/8);

        return;
    }

    if (r4300emu != CORE_PURE_INTERPRETER)
    {
		unsigned long rdram_address1 = pi_register.pi_dram_addr_reg+i+0x80000000;
        unsigned long rdram_address2 = pi_register.pi_dram_addr_reg+i+0xa0000000;

		//PRINT_DMA_MSG(M64MSG_INFO, "dma.c:%d 0x%08X 0x%08X, longueur = %d", __LINE__, &((unsigned char*)rdram)[pi_register.pi_dram_addr_reg], &rom[(pi_register.pi_cart_addr_reg-0x10000000)&0x3FFFFFF], longueur );

#if defined(M64P_ALLOCATE_MEMORY) && 0
		if (2 == dmaMode && longueur > DMA_SW_THRESHOLD)
		{
			DebugMessage(M64MSG_INFO, "dma_pi_write2 %p => %p (%d %d)", 
					&rom[((pi_register.pi_cart_addr_reg-0x10000000)&0x3FFFFFF)], 
					&((unsigned char*)rdram)[pi_register.pi_dram_addr_reg], 
					rom[((pi_register.pi_cart_addr_reg-0x10000000)&0x3FFFFFF)], 
					((unsigned char*)rdram)[pi_register.pi_dram_addr_reg]);

			dma_copy(&rom[((pi_register.pi_cart_addr_reg-0x10000000)&0x3FFFFFF)], &((unsigned char*)rdram)[pi_register.pi_dram_addr_reg], longueur);
			dma_WaitComplete(0);

			DebugMessage(M64MSG_INFO, "dma_pi_write2 %p => %p (%d %d)", 
					&rom[((pi_register.pi_cart_addr_reg-0x10000000)&0x3FFFFFF)], 
					&((unsigned char*)rdram)[pi_register.pi_dram_addr_reg], 
					rom[((pi_register.pi_cart_addr_reg-0x10000000)&0x3FFFFFF)], 
					((unsigned char*)rdram)[pi_register.pi_dram_addr_reg]);
		}
		else if (dmaMode)
#else
		if (dmaMode)
#endif
		{
			memcpy(&((unsigned char*)rdram)[pi_register.pi_dram_addr_reg], &rom[((pi_register.pi_cart_addr_reg-0x10000000)&0x3FFFFFF)], longueur);
		
			if (!invalid_code[rdram_address1>>12])
		    {
		        if (!blocks[rdram_address1>>12] ||
		            blocks[rdram_address1>>12]->block[(rdram_address1&0xFFF)/4].ops !=
		            current_instruction_table.NOTCOMPILED)
		        {
		            invalid_code[rdram_address1>>12] = 1;
		        }
		#ifdef NEW_DYNAREC
		        invalidate_block(rdram_address1>>12);
		#endif
		    }
		    if (!invalid_code[rdram_address2>>12])
		    {
		        if (!blocks[rdram_address1>>12] ||
		            blocks[rdram_address2>>12]->block[(rdram_address2&0xFFF)/4].ops !=
		            current_instruction_table.NOTCOMPILED)
		        {
		            invalid_code[rdram_address2>>12] = 1;
		        }
		    }
		}else{
			for (i=0; i<(int)longueur; i++)
		    {            
				((unsigned char*)rdram)[(pi_register.pi_dram_addr_reg+i)^S8]= rom[(((pi_register.pi_cart_addr_reg-0x10000000)&0x3FFFFFF)+i)^S8];

		        if (!invalid_code[rdram_address1>>12])
		        {
		            if (!blocks[rdram_address1>>12] ||
		                blocks[rdram_address1>>12]->block[(rdram_address1&0xFFF)/4].ops !=
		                current_instruction_table.NOTCOMPILED)
		            {
		                invalid_code[rdram_address1>>12] = 1;
		            }
#ifdef NEW_DYNAREC
                	invalidate_block(rdram_address1>>12);
#endif
		        }
		        if (!invalid_code[rdram_address2>>12])
		        {
		            if (!blocks[rdram_address1>>12] ||
		                blocks[rdram_address2>>12]->block[(rdram_address2&0xFFF)/4].ops !=
		                current_instruction_table.NOTCOMPILED)
		            {
		                invalid_code[rdram_address2>>12] = 1;
		            }
		        }
		    }
		}   
	}
    else //(r4300emu == CORE_PURE_INTERPRETER)
    {
#if defined(M64P_ALLOCATE_MEMORY) && 0
		if (2 == dmaMode && longueur > DMA_SW_THRESHOLD)
		{
			DebugMessage(M64MSG_INFO, "dma_pi_write3 %p => %p (%d %d)", 
					&rom[(pi_register.pi_cart_addr_reg-0x10000000)&0x3FFFFFF], 
					&((unsigned char*)rdram)[pi_register.pi_dram_addr_reg], 
					rom[(pi_register.pi_cart_addr_reg-0x10000000)&0x3FFFFFF], 
					((unsigned char*)rdram)[pi_register.pi_dram_addr_reg]);

			dma_copy(&rom[(pi_register.pi_cart_addr_reg-0x10000000)&0x3FFFFFF], &((unsigned char*)rdram)[pi_register.pi_dram_addr_reg], longueur);
			dma_WaitComplete(0);

			DebugMessage(M64MSG_INFO, "dma_pi_write3 %p => %p (%d %d)", 
					&rom[(pi_register.pi_cart_addr_reg-0x10000000)&0x3FFFFFF], 
					&((unsigned char*)rdram)[pi_register.pi_dram_addr_reg], 
					rom[(pi_register.pi_cart_addr_reg-0x10000000)&0x3FFFFFF], 
					((unsigned char*)rdram)[pi_register.pi_dram_addr_reg]);
		}
		else if (dmaMode)
#else
		if (dmaMode)
#endif
		{
			//PRINT_DMA_MSG(M64MSG_INFO, "dma.c:%d %X %X, longueur = %d", __LINE__, ((unsigned char*)rdram)[pi_register.pi_dram_addr_reg], rom[(pi_register.pi_cart_addr_reg-0x10000000)&0x3FFFFFF], longueur );
			memcpy(&((unsigned char*)rdram)[pi_register.pi_dram_addr_reg], &rom[(pi_register.pi_cart_addr_reg-0x10000000)&0x3FFFFFF], longueur);
		}
		else
		{
			for (i=0; i<(int)longueur; i++)
		    {
		        ((unsigned char*)rdram)[(pi_register.pi_dram_addr_reg+i)^S8]=
		            rom[(((pi_register.pi_cart_addr_reg-0x10000000)&0x3FFFFFF)+i)^S8];
		    }
		}
    }

    // Set the RDRAM memory size when copying main ROM code
    // (This is just a convenient way to run this code once at the beginning)
    if (pi_register.pi_cart_addr_reg == 0x10001000)
    {
        switch (CIC_Chip)
        {
        case 1:
        case 2:
        case 3:
        case 6:
        {
            if (ConfigGetParamInt(g_CoreConfig, "DisableExtraMem"))
            {
                rdram[0x318/4] = 0x400000;
            }
            else
            {
                rdram[0x318/4] = 0x800000;
            }
            break;
        }
        case 5:
        {
            if (ConfigGetParamInt(g_CoreConfig, "DisableExtraMem"))
            {
                rdram[0x3F0/4] = 0x400000;
            }
            else
            {
                rdram[0x3F0/4] = 0x800000;
            }
            break;
        }
        }
    }

    pi_register.read_pi_status_reg |= 3;
    update_count();
    add_interupt_event(PI_INT, longueur/8);

    return;
}

void dma_sp_write(void)
{
    unsigned int l = sp_register.sp_rd_len_reg;

    unsigned int length = ((l & 0xfff) | 7) + 1;
    unsigned int count = ((l >> 12) & 0xff) + 1;
    unsigned int skip = ((l >> 20) & 0xfff);
 
    unsigned int memaddr = sp_register.sp_mem_addr_reg & 0xfff;
    unsigned int dramaddr = sp_register.sp_dram_addr_reg & 0xffffff;

    unsigned char *spmem = ((sp_register.sp_mem_addr_reg & 0x1000) != 0) ? (unsigned char*)SP_IMEM : (unsigned char*)SP_DMEM;
    unsigned char *dram = (unsigned char*)rdram;

	//PRINT_DMA_MSG(M64MSG_INFO, "DMA:%d, %x << %x, len=%d, count=%d, skip=%d", __LINE__, &spmem[memaddr], &dram[dramaddr], length, count, skip);
#ifdef DMA_TIMING
	struct timeval ts,te;
	gettimeofday(&ts,NULL);
#endif

#ifdef M64P_ALLOCATE_MEMORY
	if (2 == dmaMode && length > DMA_SW_THRESHOLD)
	{
		//DebugMessage(M64MSG_INFO, "dma_sp_write %p => %p (%d %d)", &dram[dramaddr], &spmem[memaddr], *(int*)&dram[dramaddr], *(int*)&spmem[memaddr]);
		dma_copy_multiple(&dram[dramaddr], &spmem[memaddr], length,skip, count);
		//dma_WaitComplete(0);
		//DebugMessage(M64MSG_INFO, "dma_sp_write %p => %p (%d %d)\n", &dram[dramaddr], &spmem[memaddr], *(int*)&dram[dramaddr], *(int*)&spmem[memaddr]);
	}
	else if (dmaMode)
#else
	if (dmaMode)
#endif
	{
		unsigned int j;
		for(j=0; j<count; j++) 
		{
			memcpy(&spmem[memaddr], &dram[dramaddr], length);
			memaddr += length;
			dramaddr += length + skip;
		}
	}
	else
	{
		unsigned int i,j;
		for(j=0; j<count; j++)
		{
			for(i=0; i<length; i++)
			{
			    spmem[memaddr^S8] = dram[dramaddr^S8];
			    memaddr++;
			    dramaddr++;
			}
			dramaddr+=skip;
		}
	}
#ifdef DMA_TIMING
	gettimeofday(&te,NULL);
	DebugMessage(M64MSG_INFO, "%d dma took %dus for length %d", __LINE__, 1000000 * te.tv_sec + te.tv_usec - (1000000 * ts.tv_sec + ts.tv_usec), length );
#endif
}

void dma_sp_read(void)
{
    unsigned int l = sp_register.sp_wr_len_reg;

    unsigned int length = ((l & 0xfff) | 7) + 1;
    unsigned int count = ((l >> 12) & 0xff) + 1;
    unsigned int skip = ((l >> 20) & 0xfff);

    unsigned int memaddr = sp_register.sp_mem_addr_reg & 0xfff;
    unsigned int dramaddr = sp_register.sp_dram_addr_reg & 0xffffff;

    unsigned char *spmem = ((sp_register.sp_mem_addr_reg & 0x1000) != 0) ? (unsigned char*)SP_IMEM : (unsigned char*)SP_DMEM;
    unsigned char *dram = (unsigned char*)rdram;

	//PRINT_DMA_MSG(M64MSG_INFO, "DMA:%d, %x << %x, len=%d, count=%d, skip=%d", __LINE__, &spmem[memaddr], &dram[dramaddr], length, count, skip);
#ifdef DMA_TIMING
	struct timeval ts,te;
	gettimeofday(&ts,NULL);
#endif
#ifdef M64P_ALLOCATE_MEMORY
	if (2 == dmaMode && length > DMA_SW_THRESHOLD)
	{
		//DebugMessage(M64MSG_INFO, "dma_sp_read 0x%x => 0x%x", *(int*)&spmem[memaddr], *(int*)&dram[dramaddr]);
		dma_copy_multiple(&spmem[memaddr], &dram[dramaddr], length, skip, count);
		//dma_WaitComplete(0);
		//DebugMessage(M64MSG_INFO, "dma_sp_read 0x%x => 0x%x", *(int*)&spmem[memaddr], *(int*)&dram[dramaddr]);
	}
	else if (dmaMode)
#else
	if (dmaMode)
#endif
	{
		unsigned int j;
		for(j=0; j<count; j++) 
		{
			memcpy(&dram[dramaddr], &spmem[memaddr], length);
			memaddr += length;
			dramaddr += length + skip;
		}
	}
	else
	{

		unsigned int i,j;
		    for(j=0; j<count; j++) {
		    for(i=0; i<length; i++) {
		        dram[dramaddr^S8] = spmem[memaddr^S8];
		        memaddr++;
		        dramaddr++;
		    }
		    dramaddr+=skip;
		}
	}
#ifdef DMA_TIMING
	gettimeofday(&te,NULL);
	DebugMessage(M64MSG_INFO, "%d dma took %dus for length %d", __LINE__, 1000000 * te.tv_sec + te.tv_usec - (1000000 * ts.tv_sec + ts.tv_usec), length );
#endif
}

void dma_si_write(void)
{
    int i;

    if (si_register.si_pif_addr_wr64b != 0x1FC007C0)
    {
        DebugMessage(M64MSG_ERROR, "dma_si_write(): unknown SI use");
        stop=1;
    }

    for (i=0; i<(64/4); i++)
    {
        PIF_RAM[i] = sl(rdram[si_register.si_dram_addr/4+i]);
    }

    update_pif_write();
    update_count();
    add_interupt_event(SI_INT, /*0x100*/0x900);
}

void dma_si_read(void)
{
    int i;

    if (si_register.si_pif_addr_rd64b != 0x1FC007C0)
    {
        DebugMessage(M64MSG_ERROR, "dma_si_read(): unknown SI use");
        stop=1;
    }

    update_pif_read();

    for (i=0; i<(64/4); i++)
    {
        rdram[si_register.si_dram_addr/4+i] = sl(PIF_RAM[i]);
    }

    update_count();
    add_interupt_event(SI_INT, /*0x100*/0x900);
}

