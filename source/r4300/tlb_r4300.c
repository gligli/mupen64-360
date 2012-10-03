/**
 * Mupen64 - tlb.c
 * Copyright (C) 2002 Hacktarux
 *
 * Mupen64 homepage: http://mupen64.emulation64.com
 * email address: hacktarux@yahoo.fr
 * 
 * If you want to contribute to the project please contact
 * me first (maybe someone is already making what you are
 * planning to do).
 *
 *
 * This program is free software; you can redistribute it and/
 * or modify it under the terms of the GNU General Public Li-
 * cence as published by the Free Software Foundation; either
 * version 2 of the Licence, or any later version.
 *
 * This program is distributed in the hope that it will be use-
 * ful, but WITHOUT ANY WARRANTY; without even the implied war-
 * ranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public Licence for more details.
 *
 * You should have received a copy of the GNU General Public
 * Licence along with this program; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139,
 * USA.
 *
 **/

#include "r4300.h"
#include "macros.h"
#include "ops.h"
#include "recomph.h"
#include "interupt.h"
#include "../main/md5.h"
#include "../memory/memory.h"
#include "ARAM-blocks.h"
#include "ppc/Wrappers.h"

#include <debug.h>

void TLBR()
{
	int index;
	index = Index & 0x1F;
	PageMask = tlb_e[index].mask << 13;
	EntryHi = ((tlb_e[index].vpn2 << 13) | tlb_e[index].asid);
	EntryLo0 = (tlb_e[index].pfn_even << 6) | (tlb_e[index].c_even << 3)
			| (tlb_e[index].d_even << 2) | (tlb_e[index].v_even << 1)
			| tlb_e[index].g;
	EntryLo1 = (tlb_e[index].pfn_odd << 6) | (tlb_e[index].c_odd << 3)
			| (tlb_e[index].d_odd << 2) | (tlb_e[index].v_odd << 1)
			| tlb_e[index].g;
	PC++;
}

void TLBWrite(unsigned int idx)
{
	unsigned int i;

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

	if (!interpcore)
	{
		//printf("e %d se %p ee %p o %d so %p vo %p\n",tlb_e[idx].v_even,tlb_e[idx].start_even,tlb_e[idx].end_even,tlb_e[idx].v_odd,tlb_e[idx].start_odd,tlb_e[idx].end_odd);
		if (tlb_e[idx].v_even)
		{
			for (i = tlb_e[idx].start_even >> 12; i <= tlb_e[idx].end_even >> 12; i++)
			{
				invalid_code[i] = 1;
				//if(tlb_LUT_r[i]) invalid_code[tlb_LUT_r[i]>>12] = 1;
			}
		}
		if (tlb_e[idx].v_odd)
		{
			for (i = tlb_e[idx].start_odd >> 12; i <= tlb_e[idx].end_odd >> 12; i++)
			{
				invalid_code[i] = 1;
				//if(tlb_LUT_r[i]) invalid_code[tlb_LUT_r[i]>>12] = 1;
			}
		}
	}

}

void TLBWI()
{
	TLBWrite(Index & 0x3F);
	PC++;
}

void TLBWR()
{
	update_count();
	Random = (Count / 2 % (32 - Wired)) + Wired;
	TLBWrite(Random);
	PC++;
}

void TLBP()
{
	int i;
	Index |= 0x80000000;
	for (i = 0; i < 32; i++)
	{
		if (((tlb_e[i].vpn2 & (~tlb_e[i].mask)) ==
				(((EntryHi & 0xFFFFE000) >> 13) & (~tlb_e[i].mask))) &&
				((tlb_e[i].g) ||
				(tlb_e[i].asid == (EntryHi & 0xFF))))
		{
			Index = i;
			break;
		}
	}
	PC++;
}

int jump_marker = 0;

void ERET()
{
	update_count();
	if (Status & 0x4)
	{
		printf("ERET error\n");
		stop = 1;
	}
	else
	{
		Status &= ~0x2;
		jump_to(EPC);
	}
	llbit = 0;
	check_interupt();
	last_addr = PC->addr;
	if (next_interupt <= Count) gen_interupt();
}
