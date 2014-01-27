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
#include <sys/mman.h>	// mmap()
#include <unistd.h>		// usleep()
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

#define MODE 1 //0,1,2
#define PRINT_DMA_MSG(...) DebugMessage(__VA_ARGS__)


#ifndef PRINT_DMA_MSG
#define PRINT_DMA_MSG(...)
#endif


#define PAGE_SIZE               4096
#define PAGE_SHIFT              12

#define DMA_CHAN_SIZE           0x100
#define DMA_CHAN_MIN            0
#define DMA_CHAN_MAX            14
#define DMA_CHAN_DEFAULT        14

#define DMA_BASE                0x20007000
#define DMA_LEN                 DMA_CHAN_SIZE * (DMA_CHAN_MAX+1)


#define DMA_NO_WIDE_BURSTS		(1<<26)
#define DMA_WAIT_RESP			(1<<3)
#define DMA_D_DREQ				(1<<6)
#define DMA_PER_MAP(x)			((x)<<16)
#define DMA_END					(1<<1)
#define DMA_RESET				(1<<31)
#define DMA_INT 				(1<<2)

#define DMA_CS					(0x00/4)
#define DMA_CONBLK_AD			(0x04/4)
#define DMA_DEBUG				(0x20/4)


typedef struct {
        uint32_t info, src, dst, length,
                 stride, next, pad[2];
} dma_cb_t;

typedef struct {
        uint32_t physaddr;
} page_map_t;


static unsigned char sram[0x8000];
static unsigned int dmaMode = 0;
static volatile uint32_t *dma_reg;
static int dma_chan = DMA_CHAN_DEFAULT;
static dma_cb_t *cb_base;
page_map_t *page_map = NULL;

static uint8_t *virtbase;
static uint8_t *virtcached;

static uint32_t num_pages = 0;

//----------------------------------------------

static void make_pagemap(void);

//----------------------------------------------

static uint32_t mem_virt_to_phys(void *virt)
{
	if (virt)
	{
       	uint32_t offset = (uint8_t *)virt - virtbase;
		DebugMessage(M64MSG_INFO, "mem_virt_to_phys(%p) offset %x, page %d, .p addr 0x%x", virt, offset, offset >> PAGE_SHIFT, page_map[offset >> PAGE_SHIFT].physaddr + (offset % PAGE_SIZE));
    	return page_map[offset >> PAGE_SHIFT].physaddr + (offset % PAGE_SIZE);
	}

	DebugMessage(M64MSG_ERROR, "mem_virt_to_phys(0)");
	return 0;
}

static uint32_t CrossPageBoundary(void* a1, void* a2)
{
	return (((a1 - (void*)virtbase) >> PAGE_SHIFT) != ((a2 - (void*)virtbase) >> PAGE_SHIFT));
}

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
	
	return NULL;        
}


void* dma_allocate_memory(unsigned int size)
{
	if (0 == dmaMode)
	{
		return mmap ((u_char *)BASE_ADDR, size,
		        PROT_READ | PROT_WRITE | PROT_EXEC,
		        MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS,
		        -1, 0);
	}
	else if (1 == dmaMode)
	{
		num_pages = (size >> PAGE_SHIFT) + 1; //Add 1 for DMA control block memory

		//num_cbs =     num_samples * 2 + MAX_SERVOS;
		//num_pages = (num_cbs * sizeof(dma_cb_t) + num_samples * 4 + MAX_SERVOS * 4 + PAGE_SIZE - 1) >> PAGE_SHIFT;

		virtcached = mmap((u_char *)BASE_ADDR, num_pages * PAGE_SIZE, PROT_READ|PROT_WRITE,
                        MAP_SHARED|MAP_ANONYMOUS|MAP_NORESERVE|MAP_LOCKED,
                        -1, 0);

        if (virtcached == MAP_FAILED) 					DebugMessage(M64MSG_ERROR, "Failed to mmap for cached pages: %m\n");
        if ((unsigned long)virtcached & (PAGE_SIZE-1)) 	DebugMessage(M64MSG_ERROR, "Virtual address is not page aligned\n");
        
		//force linux to allocate
		memset(virtcached, 0, num_pages * PAGE_SIZE);

        virtbase = mmap(NULL, num_pages * PAGE_SIZE, PROT_READ|PROT_WRITE|PROT_EXEC,
                        MAP_SHARED|MAP_ANONYMOUS|MAP_NORESERVE|MAP_LOCKED, -1, 0);

		//RJH. I don't think this is needed as mmap manual states pages are unmapped if mmap'ed a second time
		//unmap(virtbase, num_pages * PAGE_SIZE);		
		
		make_pagemap();

		cb_base = (dma_cb_t *)(virtbase + size);

		dma_reg[DMA_CS] = DMA_RESET;

        usleep(10);

        dma_reg[DMA_CS] = DMA_INT | DMA_END;
		dma_reg[DMA_CONBLK_AD] = mem_virt_to_phys(cb_base);
        dma_reg[DMA_DEBUG] = 7; // clear debug error flags
        dma_reg[DMA_CS] = 0x10880001;        // go, mid priority, wait for outstanding writes

		DebugMessage(M64MSG_INFO, "Hardware DMA Initialized"); 
		DebugMessage(M64MSG_INFO, "virtbase = 0x%X, cb_base 0x%X => 0x%X, num_pages %d", virtbase, cb_base, mem_virt_to_phys(cb_base), num_pages); 

		return virtbase;
	}
	else
	{
		DebugMessage(M64MSG_ERROR, "Invalid DMA mode %d", dmaMode);

		return NULL;
	}
}

static void make_pagemap(void)
{
    int i, fd, memfd, pid;
    char pagemap_fn[64];

    page_map = malloc(num_pages * sizeof(*page_map));
    if (page_map == 0)
            DebugMessage(M64MSG_ERROR, "Failed to malloc page_map: %m\n");
    memfd = open("/dev/mem", O_RDWR);
    if (memfd < 0)
            DebugMessage(M64MSG_ERROR, "Failed to open /dev/mem: %m\n");
    pid = getpid();
    sprintf(pagemap_fn, "/proc/%d/pagemap", pid);
    fd = open(pagemap_fn, O_RDONLY);
    if (fd < 0)
            DebugMessage(M64MSG_ERROR, "Failed to open %s: %m\n", pagemap_fn);
    if (lseek(fd, (uint32_t)(size_t)virtcached >> 9, SEEK_SET) !=
                                            (uint32_t)(size_t)virtcached >> 9) {
            DebugMessage(M64MSG_ERROR, "Failed to seek on %s: %m\n", pagemap_fn);
    }
    for (i = 0; i < num_pages; i++) 
	{
        uint64_t pfn;
        if (read(fd, &pfn, sizeof(pfn)) != sizeof(pfn)) DebugMessage(M64MSG_ERROR, "Failed to read %s: %m\n", pagemap_fn);
        if (((pfn >> 55) & 0x1bf) != 0x10c) 			DebugMessage(M64MSG_ERROR, "Page %d not present (pfn 0x%016llx)\n", i, pfn);
        
		page_map[i].physaddr = (uint32_t)pfn << PAGE_SHIFT | 0x40000000;
        if ( i%100 == 0) DebugMessage(M64MSG_INFO, "DMA %4d, p addr 0x%X, v addr 0x%X",i, page_map[i].physaddr, virtcached + i * PAGE_SIZE); 

		if (mmap(virtcached + i * PAGE_SIZE, PAGE_SIZE, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_SHARED|MAP_FIXED|MAP_LOCKED|MAP_NORESERVE,
                memfd, (uint32_t)pfn << PAGE_SHIFT | 0x40000000) != virtcached + i * PAGE_SIZE) 
		{
			DebugMessage(M64MSG_ERROR, "Failed to create uncached map of page %d at %p\n", i, virtbase + i * PAGE_SIZE);
        }
    }
    close(fd);
    close(memfd);

	DebugMessage(M64MSG_INFO, "Done DMA page map"); 
    memset(virtbase, 0, num_pages * PAGE_SIZE);
	DebugMessage(M64MSG_INFO, "Cleaned DMA pages"); 
}

void dma_WaitComplete(unsigned int type)
{

}

void dma_initialize()
{
	dmaMode = ConfigGetParamInt(g_CoreConfig, "DMA_MODE");

	if (dmaMode)
	{
		dma_reg = map_peripheral(DMA_BASE, DMA_LEN);

		if (!dma_reg)
		{
			dmaMode = 0;
			DebugMessage(M64MSG_INFO, "DMA will be done by Software"); 
			return;
		}

		//move DMA pointer to desired channel
		dma_reg += dma_chan * DMA_CHAN_SIZE / sizeof(uint32_t);
	}
}

void dma_close(void)
{
	if (dma_reg && virtbase) 
	{
		dma_reg[DMA_CS] = DMA_RESET;
    }
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

			//PRINT_DMA_MSG(M64MSG_INFO, "DMA %d %d %x > %x", __LINE__, pi_register.pi_rd_len_reg & 0xFFFFFF, rdram, sram );

#if MODE > 0
			memcpy(&sram[(pi_register.pi_cart_addr_reg-0x08000000)&0xFFFF], &rdram[pi_register.pi_dram_addr_reg], (pi_register.pi_rd_len_reg & 0xFFFFFF)+1);
#else
			unsigned int i;
            for (i=0; i < (pi_register.pi_rd_len_reg & 0xFFFFFF)+1; i++)
            {
                sram[((pi_register.pi_cart_addr_reg-0x08000000)+i)^S8] =
                    ((unsigned char*)rdram)[(pi_register.pi_dram_addr_reg+i)^S8];
            }
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

				//PRINT_DMA_MSG(M64MSG_INFO, "DMA %d %d %x > %x", __LINE__, pi_register.pi_wr_len_reg & 0xFFFFFF, sram, rdram );
#if MODE > 0
				memcpy(&rdram[pi_register.pi_dram_addr_reg], &sram[(pi_register.pi_cart_addr_reg-0x08000000)&0xFFFF], (pi_register.pi_wr_len_reg & 0xFFFFFF)+1);
#else
                for (i=0; i<(int)(pi_register.pi_wr_len_reg & 0xFFFFFF)+1; i++)
                {
                    ((unsigned char*)rdram)[(pi_register.pi_dram_addr_reg+i)^S8]=
                        sram[(((pi_register.pi_cart_addr_reg-0x08000000)&0xFFFF)+i)^S8];
                }
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
        
#if 1
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
#else
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
#endif    
	}
    else
    {

#if MODE > 0 && 1
		//PRINT_DMA_MSG(M64MSG_INFO, "dma.c:%d %X %X, longueur = %d", __LINE__, ((unsigned char*)rdram)[pi_register.pi_dram_addr_reg], rom[(pi_register.pi_cart_addr_reg-0x10000000)&0x3FFFFFF], longueur );
		memcpy(&((unsigned char*)rdram)[pi_register.pi_dram_addr_reg], &rom[(pi_register.pi_cart_addr_reg-0x10000000)&0x3FFFFFF], longueur);
#else
		for (i=0; i<(int)longueur; i++)
        {
            ((unsigned char*)rdram)[(pi_register.pi_dram_addr_reg+i)^S8]=
                rom[(((pi_register.pi_cart_addr_reg-0x10000000)&0x3FFFFFF)+i)^S8];
        }
#endif
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

#if !defined(NO_ASM) && defined(ARM) && MODE > 1
	dma_copy(&spmem[memaddr], &dram[dramaddr], length, count, skip);
#elif MODE > 0
	unsigned int j;
	for(j=0; j<count; j++) 
	{
		memcpy(&spmem[memaddr], &dram[dramaddr], length);
		memaddr += length;
		dramaddr += length + skip;
    }
#else
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

#if !defined(NO_ASM) && defined(ARM) && MODE > 1
	dma_copy(&dram[dramaddr],  &spmem[memaddr], length, count, skip);
#elif MODE > 0
    unsigned int j;
    	for(j=0; j<count; j++) {
		memcpy(&dram[dramaddr], &spmem[memaddr], length);
		memaddr += length;
		dramaddr += length + skip;
    }
#else
    unsigned int i,j;
        for(j=0; j<count; j++) {
        for(i=0; i<length; i++) {
            dram[dramaddr^S8] = spmem[memaddr^S8];
            memaddr++;
            dramaddr++;
        }
        dramaddr+=skip;
    }
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

