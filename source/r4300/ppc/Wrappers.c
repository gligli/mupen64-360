/**
 * Wii64 - Wrappers.c
 * Copyright (C) 2008, 2009, 2010 Mike Slegeir
 * 
 * Interface between emulator code and recompiled code
 *
 * Wii64 homepage: http://www.emulatemii.com
 * email address: tehpola@gmail.com
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
**/

#include <stdlib.h>
#include <assert.h>
#include <debug.h>
#include <ctype.h>
#include "../ARAM-blocks.h"
#include "../../memory/memory.h"
#include "../interupt.h"
#include "../r4300.h"
#include "../Recomp-Cache.h"
#include "Recompile.h"
#include "Wrappers.h"
#include "r4300/exception.h"
#include "r4300/macros.h"

extern int stop;
extern unsigned long instructionCount;
extern void (*interp_ops[64])(void);
inline unsigned long update_invalid_addr(unsigned long addr);
unsigned int dyna_check_cop1_unusable(unsigned int, int);
unsigned int dyna_mem(unsigned int, unsigned int, memType, unsigned int, int);

int noCheckInterrupt = 0;

static PowerPC_instr* link_branch = NULL;
static PowerPC_func* last_func;

/* Recompiled code stack frame:
 *  $sp+12  |
 *  $sp+8   | old cr
 *  $sp+4   | old lr
 *  $sp	    | old sp
 */

unsigned int dyna_run(PowerPC_func* func, unsigned int (*code)(void)){
	unsigned int naddr;
	PowerPC_instr* return_addr;

	__asm__ volatile(
		// Create the stack frame for code
		"stwu	1, -32(1) \n"
		"mfcr	14        \n"
		"stw	14, 8(1)  \n"
		// Setup saved registers for code
		"mr	14, %0    \n"
		"mr	15, %1    \n"
		"mr	16, %2    \n"
		"mr	17, %3    \n"
		"mr	18, %4    \n"
		"mr	19, %5    \n"
		"mr	20, %6    \n"
		"mr	21, %7    \n"
		"mr	22, %8    \n"
		"addi	23, 0, 0  \n"
		:: "r" (reg), "r" (reg_cop0),
		   "r" (reg_cop1_simple), "r" (reg_cop1_double),
		   "r" (&FCR31), "r" (&rdram[0]),
		   "r" (&last_addr), "r" (&next_interupt),
		   "r" (func)
		: "14", "15", "16", "17", "18", "19", "20", "21", "22", "23");

	end_section(TRAMP_SECTION);

	// naddr = code();
	__asm__ volatile(
		// Save the lr so the recompiled code won't have to
		"bl	4         \n"
		"mtctr	%4        \n"
		"mflr	4         \n"
		"addi	4, 4, 20  \n"
		"stw	4, 20(1)  \n"
		// Execute the code
		"bctrl           \n"
		"mr	%0, 3     \n"
		// Get return_addr, link_branch, and last_func
		"lwz	%2, 20(1) \n"
		"mflr	%1        \n"
		"mr	%3, 22    \n"
		// Pop the stack
		"lwz	1, 0(1)   \n"
		: "=r" (naddr), "=r" (link_branch), "=r" (return_addr),
		  "=r" (last_func)
		: "r" (code)
		: "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "22", "ctr", "lr", "cr0", "cr2");

	link_branch = (link_branch == return_addr || link_branch == NULL) ? NULL : link_branch - 1;
	
	return naddr;
}

void dynarec(unsigned int address){
	
	while(!stop){
		refresh_stat();

		start_section(TRAMP_SECTION);
		PowerPC_block* dst_block = blocks_get(address>>12);
		unsigned long paddr = get_physical_addr(address);

		static int dtr=0;
		static int dcp=0;
		static int dtb=0;

#if 0
		if(kbhit())
		{
			switch(getch())
			{
			case 't':
				dtr=!dtr;
				break;
			case 'p':
				dcp=!dcp;
				break;
			case 'b':
				dtb=!dtb;
				break;
			case 'd':
				do_disasm=!do_disasm;
				break;
			case 'x':
				exit(0);
				break;
			}
		}
#endif			
		
		if(paddr==PHY_INVALID_ADDR){

			TLB_refill_exception(address,2);
			
			if(dtb)
			{
				sprintf(txtbuffer, "tlb exception old addr %p new addr %p\n", address, interp_addr);
				DEBUG_print(txtbuffer, DBG_USBGECKO);
			}
			
			//gli we had an exception, so don't try to link
			link_branch=NULL;
			
			address = interp_addr;
			continue;
		}
		
		if(!dst_block){
			
			if(dcp)
			{
				sprintf(txtbuffer, "block at %08x doesn't exist, paddr %p\n", address,paddr);
				DEBUG_print(txtbuffer, DBG_USBGECKO);
			}
			
			dst_block = calloc(1,sizeof(PowerPC_block));
			dst_block->start_address = address & ~0xFFF;
			dst_block->end_address   = (address & ~0xFFF) + 0x1000;
			
			init_block(dst_block);
		} else if(invalid_code[address>>12]){
			if(dcp)			
			{
				sprintf(txtbuffer, "invalidate blk %p %p %p\n",address,dst_block->start_address,dst_block->end_address);
				DEBUG_print(txtbuffer, DBG_USBGECKO);
			}
			
			invalidate_block(dst_block);
		}

		PowerPC_func* func = find_func(&dst_block->funcs, address);
		
		if(!func || !func->code_addr[(address-func->start_address)>>2]){
			
			unsigned int saddr=address;
			
			if(func)
			{
				if(dcp)			
				{
					sprintf(txtbuffer, "function split at %p %p %p\n", address,func->start_address,func->end_address);
					DEBUG_print(txtbuffer, DBG_USBGECKO);
				}

				saddr=func->start_address;

				invalidate_func(saddr);
				dst_block->flags[(address-dst_block->start_address)>>2]|=BLOCK_FLAG_SPLIT;
			}
			
			start_section(COMPILER_SECTION);
			func = recompile_block(dst_block, saddr);
			end_section(COMPILER_SECTION);
		
			if(dcp)			
			{
				sprintf(txtbuffer, "compiled code at %p %p %p\n", address,func->start_address,func->end_address);
				DEBUG_print(txtbuffer, DBG_USBGECKO);
			}
		} else {
#ifdef USE_RECOMP_CACHE
			RecompCache_Update(func);
#endif
		}

		int index = (address - func->start_address)>>2;

		// Recompute the block offset
		unsigned int (*code)(void);
		code = (unsigned int (*)(void))func->code_addr[index];
		
		if(dtr)
		{
			sprintf(txtbuffer, "trp %p ppc %p\n", address, code);
			DEBUG_print(txtbuffer, DBG_USBGECKO);
		}
		
		assert(code);
		
		// Create a link if possible
		if(link_branch &&
			link_branch>=last_func->code && link_branch<last_func->code+last_func->code_length) //gli test ppc location coherency
		{
			PowerPC_block  * lfblk=blocks_get(last_func->start_address>>12);
			
			if(!lfblk)
			{
				sprintf(txtbuffer, "link !lfblk : lfs %p lfe %p\n", last_func->start_address, last_func->end_address);
				DEBUG_print(txtbuffer, DBG_USBGECKO);
			}
			else
			{
				PowerPC_func * ffunc=find_func(&lfblk->funcs,last_func->start_address);

				if(!ffunc)
				{
					sprintf(txtbuffer, "link !ffunc : lfs %p lfe %p\n", last_func->start_address, last_func->end_address);
					DEBUG_print(txtbuffer, DBG_USBGECKO);
				}
				else if(ffunc!=last_func)
				{
					sprintf(txtbuffer, "link ffunc!=last_func : lfs %p lfe %p ffs %p ffe %p\n", last_func->start_address, last_func->end_address, ffunc->start_address, ffunc->end_address);
					DEBUG_print(txtbuffer, DBG_USBGECKO);
				}
				else
				{		
					RecompCache_Link(last_func, link_branch, func,(PowerPC_instr*) code);
				}
			}
		}
		
		interp_addr = address = dyna_run(func, code);
		
		if(!noCheckInterrupt){
			last_addr = interp_addr;
			// Check for interrupts
			if(next_interupt <= Count){
				gen_interupt();
				address = interp_addr;
			}
		}
		noCheckInterrupt = 0;
	}
	interp_addr = address;
}

unsigned int decodeNInterpret(MIPS_instr mips, unsigned int pc,
                              int isDelaySlot){
	delay_slot = isDelaySlot; // Make sure we set delay_slot properly
	PC->addr = interp_addr = pc;
	start_section(INTERP_SECTION);
	prefetch_opcode(mips);
	interp_ops[MIPS_GET_OPCODE(mips)]();
	end_section(INTERP_SECTION);
	delay_slot = 0;

	if(interp_addr != pc + 4) noCheckInterrupt = 1;

	return interp_addr != pc + 4 ? interp_addr : 0;
}

#ifdef COMPARE_CORE
int dyna_update_count(unsigned int pc, int isDelaySlot){
#else
int dyna_update_count(unsigned int pc){
#endif

	do_SP_Task(1,(pc - last_addr) / 2);

	Count += (pc - last_addr)/2;
	last_addr = pc;

#ifdef COMPARE_CORE
	if(isDelaySlot){
		interp_addr = pc;
		compare_core();
	}
#endif

	return next_interupt - Count;
}

unsigned int dyna_check_cop1_unusable(unsigned int pc, int isDelaySlot){
	// Set state so it can be recovered after exception
	delay_slot = isDelaySlot;
	PC->addr = interp_addr = pc;
	// Take a FP unavailable exception
	Cause = (11 << 2) | 0x10000000;
	exception_general();
	// Reset state
	delay_slot = 0;
	noCheckInterrupt = 1;
	// Return the address to trampoline to
	return interp_addr;
}

void invalidate_func(unsigned int addr){
//	printf("invalidate_func %08x %d\n",address,invalid_code_get(address>>12));

	PowerPC_block* block = blocks_get(addr>>12);
	
	for(;;)
	{
		PowerPC_func* func = find_func(&block->funcs, addr);
		
		if(func)
			RecompCache_Free(func->start_address);
		else
			break;
	}
}

void check_invalidate_memory(unsigned int addr){
#ifdef INVALIDATE_FUNC_ON_CHECK_MEMORY
	if(!invalid_code[addr>>12])
		invalidate_func(addr);
#else
	invalid_code[addr>>12] = 1;
#endif	
}

unsigned int dyna_mem(unsigned int value, unsigned int addr,
                      memType type, unsigned int pc, int isDelaySlot){
	static unsigned long long int dyna_rdword;
	
	address = addr;
	rdword = (long long int *)&dyna_rdword;
	PC->addr = interp_addr = pc;
	delay_slot = isDelaySlot;

	switch(type)
	{
	case MEM_LW:
		read_word_in_memory();
		reg[value] = (long long)((long)dyna_rdword);
		break;
	case MEM_LWU:
		rdword = &reg[value];
		read_word_in_memory();
		break;
	case MEM_LH:
		read_hword_in_memory();
		reg[value] = (long long)((short)dyna_rdword);
		break;
	case MEM_LHU:
		rdword = &reg[value];
		read_hword_in_memory();
		break;
	case MEM_LB:
		read_byte_in_memory();
		reg[value] = (long long)((signed char)dyna_rdword);
		break;
	case MEM_LBU:
		rdword = &reg[value];
		read_byte_in_memory();
		break;
	case MEM_LD:
		rdword = &reg[value];
		read_dword_in_memory();
		break;
	case MEM_LWC1:
		read_word_in_memory();
		*((long*)reg_cop1_simple[value]) = (long)dyna_rdword;
		break;
	case MEM_LDC1:
		read_dword_in_memory();
		*((long long*)reg_cop1_double[value]) = (long long)dyna_rdword;
		break;
	case MEM_LWL:
		address = addr & 0xFFFFFFFC;
		read_word_in_memory();
		switch(addr&3)
		{
		case 0:
			reg[value] = dyna_rdword;
			break;
		case 1:
			reg[value] = (reg[value]&0x00000000000000FFLL) | (dyna_rdword<<8);
			break;
		case 2:
			reg[value] = (reg[value]&0x000000000000FFFFLL) | (dyna_rdword<<16);
			break;
		case 3:
			reg[value] = (reg[value]&0x0000000000FFFFFFLL) | (dyna_rdword<<24);
			break;
		}
		sign_extended(reg[value]);
		break;
	case MEM_LWR:
		address = addr & 0xFFFFFFFC;
		read_word_in_memory();
		switch(addr&3)
		{
		case 0:
			reg[value] = (reg[value]&0xFFFFFFFFFFFFFF00LL) | ((dyna_rdword>>24)&0xFF);
			break;
		case 1:
			reg[value] = (reg[value]&0xFFFFFFFFFFFF0000LL) | ((dyna_rdword>>16)&0xFFFF);
			break;
		case 2:
			reg[value] = (reg[value]&0xFFFFFFFFFF000000LL) | ((dyna_rdword>>8)&0xFFFFFF);
			break;
		case 3:
			reg[value] = dyna_rdword;
			break;
		}
		sign_extended(reg[value]);
		break;
	case MEM_SW:
		word = value;
		write_word_in_memory();
		check_invalidate_memory(address);
		break;
	case MEM_SH:
		hword = value;
		write_hword_in_memory();
		check_invalidate_memory(address);
		break;
	case MEM_SB:
		cpu_byte = value;
		write_byte_in_memory();
		check_invalidate_memory(address);
		break;
	case MEM_SD:
		dword = reg[value];
		write_dword_in_memory();
		check_invalidate_memory(address);
		break;
	case MEM_SWC1:
		word = *((long*)reg_cop1_simple[value]);
		write_word_in_memory();
		check_invalidate_memory(address);
		break;
	case MEM_SDC1:
		dword = *((unsigned long long*)reg_cop1_double[value]);
		write_dword_in_memory();
		check_invalidate_memory(address);
		break;
	default:
		printf("dyna_mem bad type\n");
		stop = 1;
		break;
	}
	delay_slot = 0;

	if(interp_addr != pc) noCheckInterrupt = 1;

	return interp_addr != pc ? interp_addr : 0;
}
