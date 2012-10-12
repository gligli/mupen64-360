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

#include <zlib.h>

uLong ZEXPORT adler32(uLong adler, const Bytef *buf, uInt len);

unsigned int tlb_LUT_r[0x100000];
unsigned int tlb_LUT_w[0x100000];

void tlb_init()
{
	memset(tlb_LUT_r,0,sizeof(tlb_LUT_r));
	memset(tlb_LUT_w,0,sizeof(tlb_LUT_w));
}

void tlb_unmap(tlb *entry)
{
    unsigned int i;

    if (entry->v_even)
    {
        for (i=entry->start_even; i<entry->end_even; i += 0x1000)
            tlb_LUT_r[i>>12] = 0;
        if (entry->d_even)
            for (i=entry->start_even; i<entry->end_even; i += 0x1000)
                tlb_LUT_w[i>>12] = 0;
    }

    if (entry->v_odd)
    {
        for (i=entry->start_odd; i<entry->end_odd; i += 0x1000)
            tlb_LUT_r[i>>12] = 0;
        if (entry->d_odd)
            for (i=entry->start_odd; i<entry->end_odd; i += 0x1000)
                tlb_LUT_w[i>>12] = 0;
    }
}

void tlb_map(tlb *entry)
{
    unsigned int i;

    if (entry->v_even)
    {
        if (entry->start_even < entry->end_even &&
            !(entry->start_even >= 0x80000000 && entry->end_even < 0xC0000000) &&
            entry->phys_even < 0x20000000)
        {
            for (i=entry->start_even;i<entry->end_even;i+=0x1000)
			{
                tlb_LUT_r[i>>12] = 0x80000000 | (entry->phys_even + (i - entry->start_even) + 0xFFF);
				memory_vm_unmap_address(i);
			}
			
            if (entry->d_even)
                for (i=entry->start_even;i<entry->end_even;i+=0x1000)
				{
                    tlb_LUT_w[i>>12] = 0x80000000 | (entry->phys_even + (i - entry->start_even) + 0xFFF);
					memory_vm_unmap_address(i);
				}
        }
    }

    if (entry->v_odd)
    {
        if (entry->start_odd < entry->end_odd &&
            !(entry->start_odd >= 0x80000000 && entry->end_odd < 0xC0000000) &&
            entry->phys_odd < 0x20000000)
        {
            for (i=entry->start_odd;i<entry->end_odd;i+=0x1000)
			{
                tlb_LUT_r[i>>12] = 0x80000000 | (entry->phys_odd + (i - entry->start_odd) + 0xFFF);
				memory_vm_unmap_address(i);
			}
			
            if (entry->d_odd)
                for (i=entry->start_odd;i<entry->end_odd;i+=0x1000)
				{
                    tlb_LUT_w[i>>12] = 0x80000000 | (entry->phys_odd + (i - entry->start_odd) + 0xFFF);
					memory_vm_unmap_address(i);
				}
        }
    }
}

static inline unsigned int tlb_access(unsigned int vaddr, int w)
{
	if(w==1)
	{
		if(tlb_LUT_w[vaddr>>12])
		{
			return (tlb_LUT_w[vaddr>>12]&0xFFFFF000)|(vaddr&0xFFF);
		}
	}
	else
	{
		if(tlb_LUT_r[vaddr>>12])
		{
			return (tlb_LUT_r[vaddr>>12]&0xFFFFF000)|(vaddr&0xFFF);
		}
	}
	
	return PHY_INVALID_ADDR;
}

static inline unsigned int goldeneye_hack(unsigned int vaddr, int w)
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

	return tlb_access(vaddr,w);
}

unsigned int get_physical_addr(unsigned int vaddr)
{
	if (vaddr >= 0x80000000 && vaddr < 0xc0000000)
	{
		return vaddr;
	}
	else if(isGoldeneyeRom)
	{
		return goldeneye_hack(vaddr,2);
	}
	
	return tlb_access(vaddr,2);
}

unsigned int virtual_to_physical_address(unsigned int vaddr, int w)
{
	unsigned int paddr;
	
	if(isGoldeneyeRom)
	{
		paddr=goldeneye_hack(vaddr,w);
	}
	else
	{
		paddr=tlb_access(vaddr,w);
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


void tlb_write(unsigned int idx)
{
	unsigned int i;

   if (r4300emu && !interpcore)
   {
		if (tlb_e[idx].v_even)
		{
			for (i = tlb_e[idx].start_even >> 12; i <= tlb_e[idx].end_even >> 12; i++)
			{
				if (!invalid_code[i] && (invalid_code[tlb_LUT_r[i] >> 12] ||
						invalid_code[(tlb_LUT_r[i] >> 12) + 0x20000]))
					invalid_code[i] = 1;
				if (!invalid_code[i])
				{
					blocks[i]->adler32 = adler32(0, (const unsigned char *) &rdram[(tlb_LUT_r[i]&0x7FF000) / 4], 0x1000);

					invalid_code[i] = 1;
				}
				else if (blocks[i])
				{
					blocks[i]->adler32 = 0;
				}
			}
		}
		if (tlb_e[idx].v_odd)
		{
			for (i = tlb_e[idx].start_odd >> 12; i <= tlb_e[idx].end_odd >> 12; i++)
			{
				if (!invalid_code[i] && (invalid_code[tlb_LUT_r[i] >> 12] ||
						invalid_code[(tlb_LUT_r[i] >> 12) + 0x20000]))
					invalid_code[i] = 1;
				if (!invalid_code[i])
				{
					blocks[i]->adler32 = adler32(0, (const unsigned char *) &rdram[(tlb_LUT_r[i]&0x7FF000) / 4], 0x1000);

					invalid_code[i] = 1;
				}
				else if (blocks[i])
				{
					blocks[i]->adler32 = 0;
				}
			}
		}
   }

	tlb_unmap(&tlb_e[idx]);

	tlb_e[idx].g = (EntryLo0 & EntryLo1 & 1);
	tlb_e[idx].pfn_even = (EntryLo0 & 0x3FFFFFC0) >> 6;
	tlb_e[idx].pfn_odd = (EntryLo1 & 0x3FFFFFC0) >> 6;
	tlb_e[idx].c_even = (EntryLo0 & 0x38) >> 3;
	tlb_e[idx].c_odd = (EntryLo1 & 0x38) >> 3;
	tlb_e[idx].d_even = (EntryLo0 & 0x4) >> 2;
	tlb_e[idx].d_odd = (EntryLo1 & 0x4) >> 2;
	tlb_e[idx].v_even = (EntryLo0 & 0x2) >> 1;
	tlb_e[idx].v_odd = (EntryLo1 & 0x2) >> 1;
	tlb_e[idx].asid = (EntryHi & 0xFF);
	tlb_e[idx].vpn2 = (EntryHi & 0xFFFFE000) >> 13;
	tlb_e[idx].mask = (PageMask & 0x1FFE000) >> 13;

	tlb_e[idx].start_even = tlb_e[idx].vpn2 << 13;
	tlb_e[idx].end_even = tlb_e[idx].start_even +
			(tlb_e[idx].mask << 12) + 0xFFF;
	tlb_e[idx].phys_even = tlb_e[idx].pfn_even << 12;


	tlb_e[idx].start_odd = tlb_e[idx].end_even + 1;
	tlb_e[idx].end_odd = tlb_e[idx].start_odd +
			(tlb_e[idx].mask << 12) + 0xFFF;
	tlb_e[idx].phys_odd = tlb_e[idx].pfn_odd << 12;

	tlb_map(&tlb_e[idx]);

//	printf("idx %d e %d se %p pe %p o %d so %p po %p pm %p\n",idx,tlb_e[idx].v_even,tlb_e[idx].start_even,tlb_e[idx].phys_even,tlb_e[idx].v_odd,tlb_e[idx].start_odd,tlb_e[idx].phys_odd,tlb_e[idx].mask);
   if (r4300emu && !interpcore)
   {
		if (tlb_e[idx].v_even)
		{
			for (i = tlb_e[idx].start_even >> 12; i <= tlb_e[idx].end_even >> 12; i++)
			{
				if (blocks[i] && blocks[i]->adler32)
				{
					if (blocks[i]->adler32 == adler32(0, (const unsigned char *) &rdram[(tlb_LUT_r[i]&0x7FF000) / 4], 0x1000))
						invalid_code[i] = 0;
				}
			}
		}

		if (tlb_e[idx].v_odd)
		{
			for (i = tlb_e[idx].start_odd >> 12; i <= tlb_e[idx].end_odd >> 12; i++)
			{
				if (blocks[i] && blocks[i]->adler32)
				{
					if (blocks[i]->adler32 == adler32(0, (const unsigned char *) &rdram[(tlb_LUT_r[i]&0x7FF000) / 4], 0x1000))
						invalid_code[i] = 0;
				}
			}
		}
   }
}

