/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - tlb.c                                                   *
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

#include <string.h>
#include <limits.h>

#include "api/m64p_types.h"

#include "memory.h"

#include "r4300/r4300.h"
#include "r4300/exception.h"
#include "r4300/macros.h"
#include "main/rom.h"
#include "tlb.h"

unsigned int tlb_LUT_r[0x100000];
unsigned char tlb_LUT_valid[0x100000];

void tlb_init()
{
	memset(tlb_LUT_r,0,sizeof(tlb_LUT_r));
	memset(tlb_LUT_valid,0,sizeof(tlb_LUT_valid));
}

void tlb_unmap(tlb *entry)
{
    unsigned int i;

	for (i=entry->start_even; i<entry->end_even; i += 0x1000)
	{
		tlb_LUT_valid[i>>12] = entry->v_even;
		tlb_LUT_r[i>>12] = 0;
	}
	
	for (i=entry->start_odd; i<entry->end_odd; i += 0x1000)
	{
		tlb_LUT_valid[i>>12] = entry->v_odd;
		tlb_LUT_r[i>>12] = 0;
	}
}

void tlb_map(tlb *entry)
{
    unsigned int i;

	for (i=entry->start_even; i<entry->end_even; i += 0x1000)
		tlb_LUT_valid[i>>12] = entry->v_even;
	
	if (entry->start_even < entry->end_even &&
		!(entry->start_even >= 0x80000000 && entry->end_even < 0xC0000000) &&
		entry->phys_even < 0x20000000)
	{
		for (i=entry->start_even;i<entry->end_even;i+=0x1000)
			tlb_LUT_r[i>>12] = 0x80000000 | (entry->phys_even + (i - entry->start_even) + 0xFFF);
	}

	for (i=entry->start_odd; i<entry->end_odd; i += 0x1000)
		tlb_LUT_valid[i>>12] = entry->v_odd;
	
	if (entry->start_odd < entry->end_odd &&
		!(entry->start_odd >= 0x80000000 && entry->end_odd < 0xC0000000) &&
		entry->phys_odd < 0x20000000)
	{
		for (i=entry->start_odd;i<entry->end_odd;i+=0x1000)
			tlb_LUT_r[i>>12] = 0x80000000 | (entry->phys_odd + (i - entry->start_odd) + 0xFFF);
	}
}

static inline unsigned int tlb_access(unsigned int vaddr)
{
	if(tlb_LUT_valid[vaddr>>12] && tlb_LUT_r[vaddr>>12])
	{
		return (tlb_LUT_r[vaddr>>12]&0xFFFFF000)|(vaddr&0xFFF);
	}
	
	return PHY_INVALID_ADDR;
}

static inline unsigned int goldeneye_hack(unsigned int vaddr)
{
	if (vaddr >= 0x7f000000 && vaddr < 0x80000000)
    {
        /**************************************************
         GoldenEye 007 hack allows for use of TLB.
         Recoded by okaygo to support all US, J, and E ROMS.
        **************************************************/
        switch ((ROM_HEADER.Country_code>>8) & 0xFF)
        {
        case 0x45:
            // U
            return 0xb0034b30 + (vaddr & 0xFFFFFF);
            break;
        case 0x4A:
            // J
            return 0xb0034b70 + (vaddr & 0xFFFFFF);
            break;
        case 0x50:
            // E
            return 0xb00329f0 + (vaddr & 0xFFFFFF);
            break;
        default:
            // UNKNOWN COUNTRY CODE FOR GOLDENEYE USING AMERICAN VERSION HACK
            return 0xb0034b30 + (vaddr & 0xFFFFFF);
            break;
        }
    }

	return tlb_access(vaddr);
}

unsigned int get_physical_addr(unsigned int vaddr)
{
	if (vaddr >= 0x80000000 && vaddr < 0xc0000000)
	{
		return vaddr;
	}
	else if(isGoldeneyeRom)
	{
		return goldeneye_hack(vaddr);
	}
	
	return tlb_access(vaddr);
}

unsigned int virtual_to_physical_address(unsigned int vaddr, int w)
{
	unsigned int paddr;
	
	if(isGoldeneyeRom)
	{
		paddr=goldeneye_hack(vaddr);
	}
	else
	{
		paddr=tlb_access(vaddr);
	}

	if(paddr==PHY_INVALID_ADDR)
	{
		TLB_refill_exception(vaddr,w);
	}
	
    return paddr;
}


#define MEMMASK 0x7FFFFF
#define TOPOFMEM 0x80800000

int probe_nop(unsigned long address)
{
   unsigned long a;
   if (address < 0x80000000 || address > 0xc0000000)
     {
	if (tlb_LUT_r[address>>12])
	  a = (tlb_LUT_r[address>>12]&0xFFFFF000)|(address&0xFFF);
	else
	  return 0;
     }
   else
     a = address;
   
   if (a >= 0xa4000000 && a < 0xa4001000)
     {
	if (!SP_DMEM[(a&0xFFF)/4]) return 1;
	else return 0;
     }
   else if (a >= 0x80000000 && a < TOPOFMEM)
     {
	if (!rdram[(a&MEMMASK)/4]) return 1;
	else return 0;
     }
   else return 0;
}
