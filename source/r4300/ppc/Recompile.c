/**
 * Wii64 - Recompile.c
 * Copyright (C) 2007, 2008, 2009, 2010 Mike Slegeir
 * 
 * Recompiles a block of MIPS code to PPC
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

/* TODO: Try to conform more to the interface mupen64 uses.
         If we have the entire RAM and recompiled code in memory,
           we'll run out of room, we should implement a recompiled
           code cache (e.g. free blocks which haven't been used lately)
         If it's possible, only use the blocks physical addresses (saves space)
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <debug.h>

#include "../../memory/memory.h"
#include "../interupt.h"
#include "Recompile.h"
#include "../Recomp-Cache.h"
#include "Wrappers.h"
#include "../ARAM-blocks.h"

#include "Register-Cache.h"
#include "../r4300.h"

int do_disasm=0;

static PowerPC_func* cf=NULL;
static unsigned int cur_src;

static PowerPC_instr* dst;
static jump_node    jump_table[MAX_JUMPS];
static unsigned int current_jump;
static PowerPC_instr** code_addr;
static unsigned char isJmpDst[1024];

static PowerPC_instr code_buffer[64*1024];
static PowerPC_instr* code_addr_buffer[1024];

static struct func_list {
	PowerPC_func*     func;
	struct func_list* next;
} *freed_funcs = NULL;

static int pass0(PowerPC_block* ppc_block,PowerPC_func* func);
static void pass2(PowerPC_block* ppc_block,PowerPC_func* func);
//static void genRecompileBlock(PowerPC_block*);
static void genJumpPad(void);

extern int FP_need_check;

int has_next_src(void)
{
	return cur_src < cf->end_address; 
}

MIPS_instr peek_next_src(void)
{
	unsigned int old_address=address;
	long long int * old_rdword=rdword;
	long long dyna_rdword;
	
	rdword = &dyna_rdword;

	if(cur_src<cf->start_address)
	{
		address = get_physical_addr(cf->start_address);
		assert(address!=PHY_INVALID_ADDR);
		
		address -= (cur_src - cf->start_address);
	}
	else
	{
		address = get_physical_addr(cur_src);
		assert(address!=PHY_INVALID_ADDR);
	}
	
	read_word_in_memory();
	
	MIPS_instr mips=dyna_rdword;	
	
//	printf("mips %08x %08x\n",address,mips);
	
	address=old_address;
	rdword=old_rdword;
	
	return mips;
}

MIPS_instr get_next_src(void)
{
	MIPS_instr mips=peek_next_src();
	
	cur_src+=4;
	
	return mips; 
}

// Used for finding how many instructions were generated
PowerPC_instr* get_curr_dst(void)
{
	return dst;
}

// Returns the MIPS PC
unsigned int get_src_pc(void)
{
	return cur_src-4;
}

void set_next_dst(PowerPC_instr i)
{
	*(dst++) = i;
    ++cf->code_length;
}

void nop_ignored(void)
{
	if(cur_src<cf->end_address)
	{
		code_addr[(cur_src-4-cf->start_address)>>2] = dst;
	}
}

// Adjusts the code_addr for the current instruction to account for flushes
void reset_code_addr(void)
{
	if(cur_src<=cf->end_address)
	{
		code_addr[(cur_src-4-cf->start_address)>>2] = dst;
	}
}

// Undoes a get_next_src
void unget_last_src(void)
{
	cur_src-=4;
}

int add_jump(int old_jump, int is_j, int is_call){
	int id = current_jump;
	jump_node* jump = &jump_table[current_jump++];
	jump->old_jump  = old_jump;
	jump->new_jump  = 0;     // This should be filled in when known
	jump->src_pc = cur_src-4;
	jump->dst_instr = dst;   // set_next hasn't happened
	jump->type      = (is_j    ? JUMP_TYPE_J    : 0)
	                | (is_call ? JUMP_TYPE_CALL : 0);
	return id;
}

int add_jump_special(int is_j){
	int id = current_jump;
	jump_node* jump = &jump_table[current_jump++];
	jump->new_jump  = 0;     // This should be filled in when known
	jump->dst_instr = dst;   // set_next hasn't happened
	jump->type      = JUMP_TYPE_SPEC | (is_j ? JUMP_TYPE_J : 0);
	return id;
}

void set_jump_special(int which, int new_jump){
	jump_node* jump = &jump_table[which];
	if(!(jump->type & JUMP_TYPE_SPEC)) return;
	jump->new_jump = new_jump;
}

int is_j_dst(void)
{
	return isJmpDst[(get_src_pc()&0xfff)>>2];
}

void add_block_split(PowerPC_block * block, unsigned int addr){

	block->splits[block->split_count++]=addr;
	assert(block->split_count<1024);
}

int is_block_split(PowerPC_block * block, unsigned int addr){

	int i;
	for(i=0;i<block->split_count;++i)
	{
		if (block->splits[i]==addr)
			return 1;
	}

	return 0;
}

// Converts a sequence of MIPS instructions to a PowerPC block
PowerPC_func* recompile_block(PowerPC_block* ppc_block, unsigned int addr){
	
	code_addr = NULL; // Just to make sure this isn't used here

	ppc_block->adler32 = 0;

	// Create a PowerPC_func for this function
	PowerPC_func* func = malloc(sizeof(PowerPC_func));
	
	cf=func;

	cur_src = addr;
	
	func->start_address = addr;
	func->end_address = ppc_block->end_address;
	func->code = NULL;
	func->links_in = NULL;
	func->links_out = NULL;
	func->code_addr = NULL;
	func->code_length = 0;

	int need_pad = pass0(ppc_block,func);

	// insert this func into the block
	insert_func(&ppc_block->funcs, func);
	
	cf=func;

	cur_src = func->start_address;
	dst = code_buffer; // Use buffer to avoid guessing length
	current_jump = 0;
	code_addr = code_addr_buffer;
	memset(code_addr, 0, func->end_address - func->start_address);
	
	start_new_block();
	isJmpDst[(cur_src-ppc_block->start_address)>>2] = 1;
	
	int i;
	for(i=0;i<ppc_block->split_count;++i){
		isJmpDst[(ppc_block->splits[i]-ppc_block->start_address)>>2] = 1;
	}

	// If the final instruction is a branch delay slot and is branched to
	//   we will need a jump pad so that execution will continue after it
	need_pad |= isJmpDst[(func->end_address-4-ppc_block->start_address)>>2];
	
	while(has_next_src()){
		unsigned int offset = (cur_src-ppc_block->start_address)>>2;
		
		if(isJmpDst[offset]){
			cur_src+=4; start_new_mapping(); cur_src-=4;
		}

		convert();
	}

	// Flush any remaining mapped registers
	flushRegisters(); //start_new_mapping();
	
	// In case we couldn't compile the whole function, use a pad
	if(need_pad)
		genJumpPad();

	// Allocate the func buffers and copy the code
	assert(!func->code);

#ifdef USE_RECOMP_CACHE
	RecompCache_Alloc(func->code_length * sizeof(PowerPC_instr), addr, func);
#else
	func->code = malloc(func->code_length * sizeof(PowerPC_instr));
#endif
	
	memcpy(func->code, code_buffer, func->code_length * sizeof(PowerPC_instr));
	memcpy(func->code_addr, code_addr_buffer, func->end_address - func->start_address + 4);

	// Readjusting pointers to the func buffers
	code_addr = func->code_addr;
	dst = func->code + (dst - code_buffer);

	for(i=0; i<(cur_src-func->start_address)>>2; ++i)
		if(code_addr[i])
			code_addr[i] = func->code + (code_addr[i] - code_buffer);
	
	for(i=0; i<current_jump; ++i)
		jump_table[i].dst_instr = func->code +
		                          (jump_table[i].dst_instr - code_buffer);

	// Here we recompute jumps and branches
	pass2(ppc_block,func);

	// Since this is a fresh block of code,
	// Make sure it wil show up in the ICache
	DCFlushRange(func->code, func->code_length*sizeof(PowerPC_instr));
	ICInvalidateRange(func->code, func->code_length*sizeof(PowerPC_instr));

	int force_disasm=0;
	
	if (do_disasm || force_disasm)
	{
		for(i=0;i<func->code_length;++i)
		{
			if(do_disasm || force_disasm) disassemble((uint32_t)&func->code[i],func->code[i]);
		}

		for(i=0;i<(func->end_address-func->start_address)>>2;++i)
		{
			if(func->code_addr[i])
				printf("%p = %p\n",(i<<2)+func->start_address,func->code_addr[i]);
		}
	}

	return func;
}

void init_block(PowerPC_block* ppc_block){
	PowerPC_block* temp_block;

	// FIXME: Equivalent addresses should point to the same code/funcs?
	if(ppc_block->end_address < 0x80000000 || ppc_block->start_address >= 0xc0000000){
		unsigned long paddr;

		paddr = virtual_to_physical_address(ppc_block->start_address, 2);
		invalid_code[paddr>>12]=0;
		temp_block = blocks_get(paddr>>12);
		if(!temp_block){
  		   temp_block = malloc(sizeof(PowerPC_block));
		     blocks_set(paddr>>12, temp_block);
		     //blocks[paddr>>12]->code_addr = ppc_block->code_addr;
		     temp_block->funcs = NULL;
		     temp_block->start_address = paddr & ~0xFFF;
		     temp_block->end_address = (paddr & ~0xFFF) + 0x1000;
		     init_block(temp_block);
		}

		paddr += ppc_block->end_address - ppc_block->start_address - 4;
		invalid_code[paddr>>12]=0;
		temp_block = blocks_get(paddr>>12);
		if(!temp_block){
  		   temp_block = malloc(sizeof(PowerPC_block));
		     blocks_set(paddr>>12, temp_block);
		     //blocks[paddr>>12]->code_addr = ppc_block->code_addr;
		     temp_block->funcs = NULL;
		     temp_block->start_address = paddr & ~0xFFF;
		     temp_block->end_address = (paddr & ~0xFFF) + 0x1000;
		     init_block(temp_block);
		}

	} else {
		unsigned int start = ppc_block->start_address;
		unsigned int end   = ppc_block->end_address;
		temp_block = blocks_get((start+0x20000000)>>12);
		if(start >= 0x80000000 && end < 0xa0000000 &&
		   invalid_code[(start+0x20000000)>>12]){
			invalid_code[(start+0x20000000)>>12]=0;
			if(!temp_block){
  			temp_block = malloc(sizeof(PowerPC_block));
				blocks_set((start+0x20000000)>>12, temp_block);
				//blocks[(start+0x20000000)>>12]->code_addr = ppc_block->code_addr;
				temp_block->funcs = NULL;
				temp_block->start_address = (start+0x20000000) & ~0xFFF;
				temp_block->end_address		= ((start+0x20000000) & ~0xFFF) + 0x1000;
				init_block(temp_block);
			}
		}
		if(start >= 0xa0000000 && end < 0xc0000000 &&
		   invalid_code[(start-0x20000000)>>12]){
			invalid_code[(start-0x20000000)>>12]=0;
			temp_block = blocks_get((start-0x20000000)>>12);
			if(!temp_block){
  			temp_block = malloc(sizeof(PowerPC_block));
				blocks_set((start-0x20000000)>>12, temp_block);
				//blocks[(start-0x20000000)>>12]->code_addr = ppc_block->code_addr;
				temp_block->funcs = NULL;
				temp_block->start_address		= (start-0x20000000) & ~0xFFF;
				temp_block->end_address			= ((start-0x20000000) & ~0xFFF) + 0x1000;
				init_block(temp_block);
					
			}
		}
	}
	invalid_code[ppc_block->start_address>>12]=0;
}

void deinit_block(PowerPC_block* ppc_block){
	PowerPC_block* temp_block;
	invalidate_block(ppc_block);

	invalid_code[ppc_block->start_address>>12]=1;

	// We need to mark all equivalent addresses as invalid
	if(ppc_block->end_address < 0x80000000 || ppc_block->start_address >= 0xc0000000){
		unsigned long paddr;

		paddr = virtual_to_physical_address(ppc_block->start_address, 2);
		temp_block = blocks_get(paddr>>12);
		if(temp_block){
		     //blocks[paddr>>12]->code_addr = NULL;
		     invalid_code[paddr>>12]=1;
		}

		paddr += ppc_block->end_address - ppc_block->start_address - 4;
		temp_block = blocks_get(paddr>>12);
		if(temp_block){
		     //blocks[paddr>>12]->code_addr = NULL;
		     invalid_code[paddr>>12]=1;
		}

	} else {
		unsigned int start = ppc_block->start_address;
		unsigned int end   = ppc_block->end_address;
		temp_block = blocks_get((start+0x20000000)>>12);
		if(start >= 0x80000000 && end < 0xa0000000 && temp_block){
			//blocks[(start+0x20000000)>>12]->code_addr = NULL;
			invalid_code[(start+0x20000000)>>12]=1;
		}
		temp_block = blocks_get((start-0x20000000)>>12);
		if(start >= 0xa0000000 && end < 0xc0000000 && temp_block){
			//blocks[(start-0x20000000)>>12]->code_addr = NULL;
			invalid_code[(start-0x20000000)>>12]=1;
		}
	}
}

int is_j_out(int branch, int is_aa){
	if(is_aa)
		return ((branch << 2 | (cf->start_address & 0xF0000000)) <  cf->start_address ||
		        (branch << 2 | (cf->start_address & 0xF0000000)) >= cf->end_address);
	else {
		int dst_instr = ((cur_src-cf->start_address-4)>>2) + branch;
		return (dst_instr < 0 || dst_instr >= (cf->end_address-cf->start_address)>>2);
	}
}

// Pass 2 fills in all the new addresses
static void pass2(PowerPC_block* ppc_block,PowerPC_func* func){
	int i;
	PowerPC_instr* current;
	for(i=0; i<current_jump; ++i){
		current = jump_table[i].dst_instr;

		// Special jump, its been filled out
		if(jump_table[i].type & JUMP_TYPE_SPEC){
			if(!(jump_table[i].type & JUMP_TYPE_J)){
				// We're filling in a branch instruction
				*current &= ~(PPC_BD_MASK << PPC_BD_SHIFT);
				PPC_SET_BD(*current, jump_table[i].new_jump);
			} else {
				// We're filling in a jump instrucion
				*current &= ~(PPC_LI_MASK << PPC_LI_SHIFT);
				PPC_SET_LI(*current, jump_table[i].new_jump);
			}
			continue;
		}

		if(jump_table[i].type & JUMP_TYPE_CALL){ // Call to C function code
			// old_jump is the address of the function to call
			int jump_offset = ((unsigned int)jump_table[i].old_jump -
			                   (unsigned int)current)/4;
			// We're filling in a jump instrucion
			*current &= ~(PPC_LI_MASK << PPC_LI_SHIFT);
			PPC_SET_LI(*current, jump_offset);

		} else if(!(jump_table[i].type & JUMP_TYPE_J)){ // Branch instruction
			int jump_offset = (unsigned int)jump_table[i].old_jump +
				         ((jump_table[i].src_pc - func->start_address)>>2);

			jump_table[i].new_jump = code_addr[jump_offset] - current;

#if 0
			// FIXME: Reenable this when blocks are small enough to BC within
			//          Make sure that branch is using BC/B as appropriate
			*current &= ~(PPC_BD_MASK << PPC_BD_SHIFT);
			PPC_SET_BD(*current, jump_table[i].new_jump);
#else
			*current &= ~(PPC_LI_MASK << PPC_LI_SHIFT);
			PPC_SET_LI(*current, jump_table[i].new_jump);
#endif

		} else { // Jump instruction
			// The destination is actually calculated from the delay slot
			unsigned int jump_addr = (jump_table[i].old_jump << 2) |
			                         (ppc_block->start_address & 0xF0000000);

			// We're jumping within this block, find out where
			int jump_offset = (jump_addr - func->start_address) >> 2;
			jump_table[i].new_jump = code_addr[jump_offset] - current;

			*current &= ~(PPC_LI_MASK << PPC_LI_SHIFT);
			PPC_SET_LI(*current, jump_table[i].new_jump);

		}
	}
}

static int pass0(PowerPC_block* ppc_block,PowerPC_func* func){
	
	// Zero out the jump destinations table
	int i;
	for(i=0; i<1024; ++i) isJmpDst[i] = 0;

	for(cur_src = func->start_address; cur_src < func->end_address; cur_src+=4){
	
		MIPS_instr mips=peek_next_src();

		int opcode = MIPS_GET_OPCODE(mips);
		int index = (cur_src - ppc_block->start_address) >> 2;
		if(opcode == MIPS_OPCODE_J || opcode == MIPS_OPCODE_JAL){
			unsigned int li = MIPS_GET_LI(mips);
			cur_src+=8;
			if(!is_j_out(li, 1)){
				assert( ((li&0x3FF) >= 0) && ((li&0x3FF) < 1024) );
				isJmpDst[ li & 0x3FF ] = 1;
			}
			cur_src-=4;
			if(opcode == MIPS_OPCODE_JAL && index + 2 < 1024)
				isJmpDst[ index + 2 ] = 1;
			if(opcode == MIPS_OPCODE_J){ cur_src+=4; break; }
		} else if(opcode == MIPS_OPCODE_BEQ   ||
		          opcode == MIPS_OPCODE_BNE   ||
		          opcode == MIPS_OPCODE_BLEZ  ||
		          opcode == MIPS_OPCODE_BGTZ  ||
		          opcode == MIPS_OPCODE_BEQL  ||
		          opcode == MIPS_OPCODE_BNEL  ||
		          opcode == MIPS_OPCODE_BLEZL ||
		          opcode == MIPS_OPCODE_BGTZL ||
		          opcode == MIPS_OPCODE_B     ||
		          (opcode == MIPS_OPCODE_COP1 &&
		           MIPS_GET_RS(mips) == MIPS_FRMT_BC)){
			int bd = MIPS_GET_IMMED(mips);
			cur_src+=8;
			bd |= (bd & 0x8000) ? 0xFFFF0000 : 0; // sign extend
			if(!is_j_out(bd, 0)){
				assert( index + 1 + bd >= 0 && index + 1 + bd < 1024 );
				isJmpDst[ index + 1 + bd ] = 1;
			}
			cur_src-=4;

			if(index + 2 < 1024)
				isJmpDst[ index + 2 ] = 1;
			
		} else if(opcode == MIPS_OPCODE_R &&
		          (MIPS_GET_FUNC(mips) == MIPS_FUNC_JR ||
		           MIPS_GET_FUNC(mips) == MIPS_FUNC_JALR)){
			cur_src+=8;
			break;
		} else if(opcode == MIPS_OPCODE_COP0 &&
		          MIPS_GET_FUNC(mips) == MIPS_FUNC_ERET){
			cur_src+=4;
			break;
		}
	}
	
	if(cur_src < func->end_address){
		func->end_address=cur_src;
		return 0;
	} else {
		return 1;
	}
}

extern int stop;

void jump_to(unsigned int address)
{
	stop = 1;
};

extern unsigned long jump_to_address;

void dyna_jump(void)
{
	jump_to(jump_to_address);
}

void dyna_stop(void)
{

}

void jump_to_func(void)
{
	jump_to(jump_to_address);
}

static void genJumpPad(void){
	// noCheckInterrupt = 1
	EMIT_LIS(3, (unsigned int)(&noCheckInterrupt)>>16);
	EMIT_ORI(3, 3, (unsigned int)(&noCheckInterrupt));
	EMIT_LI(0, 1);
	EMIT_STW(0, 0, 3);

	// TODO: I could link to the next block here
	//       When I do, I need to ensure that noCheckInterrupt is cleared
	// Set the next address to the first address in the next block if
		
	//   we've really reached the end of the block, not jumped to the pad
	EMIT_LIS(3, (get_src_pc()+4)>>16);
	EMIT_ORI(3, 3, get_src_pc()+4);

	// return destination
	EMIT_BLR(0);
}

// Free the code for all the functions in this block
PowerPC_func_node* free_tree(PowerPC_func_node* node){
	if(!node) return NULL;
	node->left = free_tree(node->left);
	node->right = free_tree(node->right);
	RecompCache_Free(node->function->start_address);
	return NULL;
}

void invalidate_block(PowerPC_block* ppc_block){
	ppc_block->funcs = free_tree(ppc_block->funcs);

	// Now that we've handled the invalidation, reinit ourselves
	init_block(ppc_block);
}

int func_was_freed(PowerPC_func* func){
	struct func_list* node;
	for(node = freed_funcs; node != NULL; node = node->next)
		if(node->func == func) return 1;
	return 0;
}

void clear_freed_funcs(void){
	struct func_list* node, * next;
	for(node = freed_funcs; node != NULL; node = next){
		next = node->next;
		free(node);
	}
	freed_funcs = NULL;
}
