/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - exception.c                                             *
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

#include <assert.h>
#include <debug.h>

#include "api/m64p_types.h"
#include "api/callbacks.h"
#include "memory/memory.h"

#include "exception.h"
#include "r4300.h"
#include "macros.h"
#include "recomph.h"
#include "ppc/Wrappers.h"

void TLB_refill_exception(unsigned int address, int w)
{
	if (!r4300emu && w != 2) update_count();
	
	if (w == 1) Cause = (3 << 2);
	else Cause = (2 << 2);
	
	switch(w)
	{
	case 0:
		Cause=2<<2; //gli should be TLBL TLB Exception (Load or instruction fetch)
		break;
	case 1:
		Cause=1<<2; //gli should be TLBS TLB Exception (Store)
		break;
	case 2:
		Cause=2<<2; //gli not sure here, but same as read makes sense
		break;
	}
	
	BadVAddr = address;
	Context = (Context & 0xFF80000F) | ((address >> 9) & 0x007FFFF0);
	EntryHi = address & 0xFFFFE000;
	if (Status & 0x2) // Testing EXL , to detect double exception
	{
		DebugMessage(M64MSG_ERROR,"TLB refill while in exception, addr=%p, w=%d",address,w);

		interp_addr = 0x80000180;
		if (delay_slot == 1 || delay_slot == 3) Cause |= 0x80000000;
		else Cause &= 0x7FFFFFFF;
	}
	else
	{
		DebugMessage(M64MSG_INFO,"TLB refill, addr=%p, w=%d, interp_addr=%p, dslot=%d",address,w,interp_addr,delay_slot);
				
		if (!interpcore && !r4300emu)
		{ 
			assert(0);
		}
		else if(w==2)
		{
			EPC = address;
		}
		else
		{
			EPC = interp_addr;
		}
		
		Cause &= ~0x80000000;
		Status |= 0x2; //EXL=1
		interp_addr = 0x80000000;
	}
	if (delay_slot == 1 || delay_slot == 3)
	{
		Cause |= 0x80000000;
		EPC -= 4;
	}
	else
	{
		Cause &= 0x7FFFFFFF;
	}
	if (w != 2) EPC -= 4;

	last_addr = interp_addr;

	dyna_interp = 0;
	if (delay_slot)
	{
		skip_jump = interp_addr;
		next_interupt = 0;
	}
}

void exception_general(void)
{
	update_count();
	Status |= 2;

	EPC = interp_addr;

	if (delay_slot == 1 || delay_slot == 3)
	{
		Cause |= 0x80000000;
		EPC -= 4;
	}
	else
	{
		Cause &= 0x7FFFFFFF;
	}
	
	interp_addr = 0x80000180;
	last_addr = interp_addr;
	
	dyna_interp = 0;
	if (delay_slot)
	{
		skip_jump = interp_addr;
		next_interupt = 0;
	}
}

