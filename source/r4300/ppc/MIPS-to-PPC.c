/**
 * Wii64 - MIPS-to-PPC.c
 * Copyright (C) 2007, 2008, 2009, 2010 Mike Slegeir
 * 
 * Convert MIPS code into PPC (take 2 1/2)
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

#include "Recompile.h"


/* TODO: Optimize idle branches (generate a call to gen_interrupt)
		 Optimize instruction scheduling & reduce branch instructions
 */

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include <assert.h>

#include "MIPS-to-PPC.h"
#include "Register-Cache.h"
#include "Interpreter.h"
#include "Wrappers.h"
#include "../../memory/memory.h"
#include "memory/tlb.h"
#include "r4300/Recomp-Cache.h"

#include <debug.h>
#include <ppc/cache.h>

// Prototypes for functions used and defined in this file
static void genCallInterp(MIPS_instr);
#define JUMPTO_REG  0
#define JUMPTO_OFF  1
#define JUMPTO_ADDR 2
#define JUMPTO_OFF_SIZE  13 //11
static void genJumpTo(unsigned int loc, unsigned int type);
static void genUpdateCount(int checkCount);
static void genCheckFP(void);
void genCallDynaMem(memType type, int base, short immed);
void genCallDynaMem2(int type, int base, short immed);
static int genCallDynaMemVM(int rs_reg, int rt_reg, memType type, int immed);
void RecompCache_Update(PowerPC_func*);
void jump_to(unsigned int);
void check_interupt();
extern int llbit;

double __floatdidf(long long);
float __floatdisf(long long);
long long __fixdfdi(double);
long long __fixsfdi(float);

#define CANT_COMPILE_DELAY() \
	((get_src_pc()&0xFFF) == 0xFFC && \
	 (get_src_pc() <  0x80000000 || \
	  get_src_pc() >= 0xC0000000))

static inline unsigned short extractUpper16(void* address){
	unsigned int addr = (unsigned int)address;
	return (addr>>16) + ((addr>>15)&1);
}

static inline short extractLower16(void* address){
	unsigned int addr = (unsigned int)address;
	return addr&0x8000 ? (addr&0xffff)-0x10000 : addr&0xffff;
}

static int FP_need_check;

// Variable to indicate whether the next recompiled instruction
//   is a delay slot (which needs to have its registers flushed)
//   and the current instruction
static int delaySlotNext, isDelaySlot;
// This should be called before the jump is recompiled
static inline int check_delaySlot(void){
	if(peek_next_src() == 0){ // MIPS uses 0 as a NOP
		get_next_src();   // Get rid of the NOP
		return 0;
	} else {
		if(mips_is_jump(peek_next_src())) return CONVERT_WARNING;
		delaySlotNext = 1;
		convert(); // This just moves the delay slot instruction ahead of the branch
		return 1;
	}
}

#define MIPS_REG_HI 32
#define MIPS_REG_LO 33

// Initialize register mappings
void start_new_block(void){
	invalidateRegisters();
	// Check if the previous instruction was a branch
	//   and thus whether this block begins with a delay slot
	unget_last_src();
	if(mips_is_jump(get_next_src())) delaySlotNext = 2;
	else delaySlotNext = 0;
}
void start_new_mapping(void){
	flushRegisters();
	FP_need_check = 1;
	reset_code_addr();
}

static inline int signExtend(int value, int size){
	int signMask = 1 << (size-1);
	int negMask = 0xffffffff << (size-1);
	if(value & signMask) value |= negMask;
	return value;
}

static void genCmp64(int cr, int _ra, int _rb){
	
	
	if(getRegisterMapping(_ra) == MAPPING_32 ||
	   getRegisterMapping(_rb) == MAPPING_32){
		// Here we cheat a little bit: if either of the registers are mapped
		// as 32-bit, only compare the 32-bit values
		int ra = mapRegister(_ra), rb = mapRegister(_rb);
		
		EMIT_CMP(ra, rb, 4);
	} else {
		RegMapping ra = mapRegister64(_ra), rb = mapRegister64(_rb);
		
		EMIT_CMP(ra.hi, rb.hi, 4);
		// Skip low word comparison if high words are mismatched
		EMIT_BNE(4, 2, 0, 0);
		// Compare low words if hi words don't match
		EMIT_CMPL(ra.lo, rb.lo, 4);
	}
}

static void genCmpi64(int cr, int _ra, short immed){
	
	
	if(getRegisterMapping(_ra) == MAPPING_32){
		// If we've mapped this register as 32-bit, don't bother with 64-bit
		int ra = mapRegister(_ra);
		
		EMIT_CMPI(ra, immed, 4);
	} else {
		RegMapping ra = mapRegister64(_ra);
		
		EMIT_CMPI(ra.hi, (immed&0x8000) ? ~0 : 0, 4);
		// Skip low word comparison if high words are mismatched
		EMIT_BNE(4, 2, 0, 0);
		// Compare low words if hi words don't match
		EMIT_CMPLI(ra.lo, immed, 4);
	}
}

typedef enum { NONE=0, EQ, NE, LT, GT, LE, GE } condition;
// Branch a certain offset (possibly conditionally, linking, or likely)
//   offset: N64 instructions from current N64 instruction to branch
//   cond: type of branch to execute depending on cr 7
//   link: if nonzero, branch and link
//   likely: if nonzero, the delay slot will only be executed when cond is true
static int branch(int offset, condition cond, int link, int likely){
	
	int likely_id;
	// Condition codes for bc (and their negations)
	int bo, bi, nbo;
	switch(cond){
		case EQ:
			bo = 0xc, nbo = 0x4, bi = 18;
			break;
		case NE:
			bo = 0x4, nbo = 0xc, bi = 18;
			break;
		case LT:
			bo = 0xc, nbo = 0x4, bi = 16;
			break;
		case GE:
			bo = 0x4, nbo = 0xc, bi = 16;
			break;
		case GT:
			bo = 0xc, nbo = 0x4, bi = 17;
			break;
		case LE:
			bo = 0x4, nbo = 0xc, bi = 17;
			break;
		default:
			bo = 0x14; nbo = 0x4; bi = 19;
			break;
	}

	flushRegisters();

	if(link){
		// Set LR to next instruction
		int lr = mapRegisterNew(MIPS_REG_LR);
		// lis	lr, pc@ha(0)
		EMIT_LIS(lr, (get_src_pc()+8)>>16);
		// la	lr, pc@l(lr)
		EMIT_ORI(lr, lr, get_src_pc()+8);

		flushRegisters();
	}

	if(likely){
		// b[!cond] <past delay to update_count>
		likely_id = add_jump_special(0);
		EMIT_BC(likely_id, 0, 0, nbo, bi);
	}

	// Check the delay slot, and note how big it is
	PowerPC_instr* preDelay = get_curr_dst();
	check_delaySlot();
	int delaySlot = get_curr_dst() - preDelay;

#ifdef COMPARE_CORE
	EMIT_LI(4, 1);
	if(likely){
		EMIT_B(2, 0);
		EMIT_LI(4, 0);

		set_jump_special(likely_id, delaySlot+2+1);
	}
#else
	if(likely) set_jump_special(likely_id, delaySlot+1);
#endif

	genUpdateCount(1); // Sets cr2 to (next_interupt ? Count)

#ifndef INTERPRET_BRANCH
	// If we're jumping out, we need to trampoline using genJumpTo
	if(is_j_out(offset, 0)){
#endif // INTEPRET_BRANCH

		// b[!cond] <past jumpto & delay>
		//   Note: if there's a delay slot, I will branch to the branch over it
		EMIT_BC(JUMPTO_OFF_SIZE+1, 0, 0, nbo, bi);

		genJumpTo(offset, JUMPTO_OFF);

		// The branch isn't taken, but we need to check interrupts
		// Load the address of the next instruction
		EMIT_LIS(3, (get_src_pc()+4)>>16);
		EMIT_ORI(3, 3, get_src_pc()+4);
		// If taking the interrupt, return to the trampoline
		EMIT_BLELR(2, 0);

#ifndef INTERPRET_BRANCH
	} else {
		// last_addr = naddr
		if(cond != NONE){
			EMIT_BC(4, 0, 0, bo, bi);
			EMIT_LIS(3, (get_src_pc()+4)>>16);
			EMIT_ORI(3, 3, get_src_pc()+4);
			EMIT_B(3, 0, 0);
		}
		EMIT_LIS(3, (get_src_pc() + (offset<<2))>>16);
		EMIT_ORI(3, 3, get_src_pc() + (offset<<2));
		EMIT_STW(3, 0, DYNAREG_LADDR);

		// If taking the interrupt, return to the trampoline
		EMIT_BLELR(2, 0);

		// The actual branch
#if 0
		// FIXME: Reenable this when blocks are small enough to BC within
		//          Make sure that pass2 uses BD/LI as appropriate
		EMIT_BC(add_jump((int)(offset, 0, 0), 0, 0, bo, bi);
#else
		EMIT_BC(2, 0, 0, nbo, bi);
		EMIT_B(add_jump(offset, 0, 0), 0, 0);
#endif

	}
#endif // INTERPRET_BRANCH

	// Let's still recompile the delay slot in place in case its branched to
	// Unless the delay slot is in the next block, in which case there's nothing to skip
	//   Testing is_j_out with an offset of 0 checks whether the delay slot is out
	if(delaySlot){
		if(is_j_dst() && !is_j_out(0, 0)){
			// Step over the already executed delay slot if the branch isn't taken
			// b delaySlot+1
			EMIT_B(delaySlot+1, 0, 0);

			unget_last_src();
			delaySlotNext = 2;
		}
	} else nop_ignored();

#ifdef INTERPRET_BRANCH
	return INTERPRETED;
#else // INTERPRET_BRANCH
	return CONVERT_SUCCESS;
#endif
}

static int (*gen_ops[64])(MIPS_instr);

int convert(void){
	int needFlush = delaySlotNext;
	isDelaySlot = (delaySlotNext == 1);
	delaySlotNext = 0;

	MIPS_instr mips = get_next_src();
	int result = gen_ops[MIPS_GET_OPCODE(mips)](mips);
	
	if(needFlush) flushRegisters();
	return result;
}

static int NI(){
	return CONVERT_ERROR;
}

// -- Primary Opcodes --

static int J(MIPS_instr mips){
	
	unsigned int naddr = (MIPS_GET_LI(mips)<<2)|((get_src_pc()+4)&0xf0000000);

	if(naddr == get_src_pc() || CANT_COMPILE_DELAY()){
		// J_IDLE || virtual delay
		genCallInterp(mips);
		return INTERPRETED;
	}

	flushRegisters();
	reset_code_addr();

	// Check the delay slot, and note how big it is
	PowerPC_instr* preDelay = get_curr_dst();
	check_delaySlot();
	int delaySlot = get_curr_dst() - preDelay;

#ifdef COMPARE_CORE
	EMIT_LI(4, 0, 1);
#endif
	// Sets cr2 to (next_interupt ? Count)
	genUpdateCount(1);

#ifdef INTERPRET_J
	genJumpTo(MIPS_GET_LI(mips), JUMPTO_ADDR);
#else // INTERPRET_J
	// If we're jumping out, we can't just use a branch instruction
	if(is_j_out(MIPS_GET_LI(mips), 1)){
		genJumpTo(MIPS_GET_LI(mips), JUMPTO_ADDR);
	} else {
		// last_addr = naddr
		EMIT_LIS(3, naddr>>16);
		EMIT_ORI(3, 3, naddr);
		EMIT_STW(3, 0, DYNAREG_LADDR);

		// if(next_interupt <= Count) return;
		EMIT_BLELR(2, 0);

		// Even though this is an absolute branch
		//   in pass 2, we generate a relative branch
		EMIT_B(add_jump(MIPS_GET_LI(mips), 1, 0), 0, 0);
	}
#endif

	// Let's still recompile the delay slot in place in case its branched to
	if(delaySlot){ if(is_j_dst()){ unget_last_src(); delaySlotNext = 2; } }
	else nop_ignored();

#ifdef INTERPRET_J
	return INTERPRETED;
#else // INTERPRET_J
	return CONVERT_SUCCESS;
#endif
}

static int JAL(MIPS_instr mips){
	
	unsigned int naddr = (MIPS_GET_LI(mips)<<2)|((get_src_pc()+4)&0xf0000000);

	if(CANT_COMPILE_DELAY()){
		genCallInterp(mips);
		return INTERPRETED;
	}

	flushRegisters();
	reset_code_addr();

	// Check the delay slot, and note how big it is
	PowerPC_instr* preDelay = get_curr_dst();
	check_delaySlot();
	int delaySlot = get_curr_dst() - preDelay;

#ifdef COMPARE_CORE
	EMIT_LI(4, 1);
#endif
	// Sets cr2 to (next_interupt ? Count)
	genUpdateCount(1);

	// Set LR to next instruction
	int lr = mapRegisterNew(MIPS_REG_LR);
	// lis	lr, pc@ha(0)
	EMIT_LIS(lr, (get_src_pc()+4)>>16);
	// la	lr, pc@l(lr)
	EMIT_ORI(lr, lr, get_src_pc()+4);

	flushRegisters();

#ifdef INTERPRET_JAL
	genJumpTo(MIPS_GET_LI(mips), JUMPTO_ADDR);
#else // INTERPRET_JAL
	// If we're jumping out, we can't just use a branch instruction
	if(is_j_out(MIPS_GET_LI(mips), 1)){
		genJumpTo(MIPS_GET_LI(mips), JUMPTO_ADDR);
	} else {
		// last_addr = naddr
		EMIT_LIS(3, naddr>>16);
		EMIT_ORI(3, 3, naddr);
		EMIT_STW(3, 0, DYNAREG_LADDR);

		/// if(next_interupt <= Count) return;
		EMIT_BLELR(2, 0);

		// Even though this is an absolute branch
		//   in pass 2, we generate a relative branch
		EMIT_B(add_jump(MIPS_GET_LI(mips), 1, 0), 0, 0);
	}
#endif

	// Let's still recompile the delay slot in place in case its branched to
	if(delaySlot){ if(is_j_dst()){ unget_last_src(); delaySlotNext = 2; } }
	else nop_ignored();

#ifdef INTERPRET_JAL
	return INTERPRETED;
#else // INTERPRET_JAL
	return CONVERT_SUCCESS;
#endif
}

static int BEQ(MIPS_instr mips){
	

	if((MIPS_GET_IMMED(mips) == 0xffff &&
	    MIPS_GET_RA(mips) == MIPS_GET_RB(mips)) ||
	   CANT_COMPILE_DELAY()){
		// BEQ_IDLE || virtual delay
		genCallInterp(mips);
		return INTERPRETED;
	}
	
	genCmp64(4, MIPS_GET_RA(mips), MIPS_GET_RB(mips));

	return branch(signExtend(MIPS_GET_IMMED(mips),16), EQ, 0, 0);
}

static int BNE(MIPS_instr mips){
	

	if(CANT_COMPILE_DELAY()){
		genCallInterp(mips);
		return INTERPRETED;
	}

	genCmp64(4, MIPS_GET_RA(mips), MIPS_GET_RB(mips));

	return branch(signExtend(MIPS_GET_IMMED(mips),16), NE, 0, 0);
}

static int BLEZ(MIPS_instr mips){
	

	if(CANT_COMPILE_DELAY()){
		genCallInterp(mips);
		return INTERPRETED;
	}

	genCmpi64(4, MIPS_GET_RA(mips), 0);

	return branch(signExtend(MIPS_GET_IMMED(mips),16), LE, 0, 0);
}

static int BGTZ(MIPS_instr mips){
	

	if(CANT_COMPILE_DELAY()){
		genCallInterp(mips);
		return INTERPRETED;
	}

	genCmpi64(4, MIPS_GET_RA(mips), 0);

	return branch(signExtend(MIPS_GET_IMMED(mips),16), GT, 0, 0);
}

static int ADDIU(MIPS_instr mips){
	
	int rs = mapRegister( MIPS_GET_RS(mips) );
	EMIT_ADDI(
	         mapRegisterNew( MIPS_GET_RT(mips) ),
	         rs,
	         MIPS_GET_IMMED(mips));
	return CONVERT_SUCCESS;
}

static int ADDI(MIPS_instr mips){
	return ADDIU(mips);
}

static int SLTI(MIPS_instr mips){
	
#ifdef INTERPRET_SLTI
	genCallInterp(mips);
	return INTERPRETED;
#else
	// FIXME: Do I need to worry about 64-bit values?
	int rs = mapRegister( MIPS_GET_RS(mips) );
	int rt = mapRegisterNew( MIPS_GET_RT(mips) );
	int tmp = (rs == rt) ? mapRegisterTemp() : rt;

	// tmp = immed (sign extended)
	EMIT_ADDI(tmp, 0, MIPS_GET_IMMED(mips));
	// carry = rs < immed ? 0 : 1 (unsigned)
	EMIT_SUBFC(0, tmp, rs);
	// rt = ~(rs ^ immed)
	EMIT_EQV(rt, tmp, rs);
	// rt = sign(rs) == sign(immed) ? 1 : 0
	EMIT_SRWI(rt, rt, 31);
	// rt += carry
	EMIT_ADDZE(rt, rt);
	// rt &= 1 ( = (sign(rs) == sign(immed)) xor (rs < immed (unsigned)) )
	EMIT_RLWINM(rt, rt, 0, 31, 31);

	if(rs == rt) unmapRegisterTemp(tmp);

	return CONVERT_SUCCESS;
#endif
}

static int SLTIU(MIPS_instr mips){
	
#ifdef INTERPRET_SLTIU
	genCallInterp(mips);
	return INTERPRETED;
#else
	// FIXME: Do I need to worry about 64-bit values?
	int rs = mapRegister( MIPS_GET_RS(mips) );
	int rt = mapRegisterNew( MIPS_GET_RT(mips) );

	// r0 = EXTS(immed)
	EMIT_ADDI(0, 0, MIPS_GET_IMMED(mips));
	// carry = rs < immed ? 0 : 1
	EMIT_SUBFC(rt, 0, rs);
	// rt = carry - 1 ( = rs < immed ? -1 : 0 )
	EMIT_SUBFE(rt, rt, rt);
	// rt = !carry ( = rs < immed ? 1 : 0 )
	EMIT_NEG(rt, rt);

	return CONVERT_SUCCESS;
#endif
}

static int ANDI(MIPS_instr mips){
	
	int rs = mapRegister( MIPS_GET_RS(mips) );
	int rt = mapRegisterNew( MIPS_GET_RT(mips) );

	EMIT_ANDI(rt, rs, MIPS_GET_IMMED(mips));

	return CONVERT_SUCCESS;
}

static int ORI(MIPS_instr mips){
	
	RegMapping rs = mapRegister64( MIPS_GET_RS(mips) );
	RegMapping rt = mapRegister64New( MIPS_GET_RT(mips) );

	EMIT_OR(rt.hi, rs.hi, rs.hi);
	EMIT_ORI(rt.lo, rs.lo, MIPS_GET_IMMED(mips));

	return CONVERT_SUCCESS;
}

static int XORI(MIPS_instr mips){
	
	RegMapping rs = mapRegister64( MIPS_GET_RS(mips) );
	RegMapping rt = mapRegister64New( MIPS_GET_RT(mips) );

	EMIT_OR(rt.hi, rs.hi, rs.hi);
	EMIT_XORI(rt.lo, rs.lo, MIPS_GET_IMMED(mips));

	return CONVERT_SUCCESS;
}

static int LUI(MIPS_instr mips){
	
	EMIT_LIS(
	        mapRegisterNew( MIPS_GET_RT(mips) ),
	        MIPS_GET_IMMED(mips));

	return CONVERT_SUCCESS;
}

static int BEQL(MIPS_instr mips){
	

	if(CANT_COMPILE_DELAY()){
		genCallInterp(mips);
		return INTERPRETED;
	}

	genCmp64(4, MIPS_GET_RA(mips), MIPS_GET_RB(mips));

	return branch(signExtend(MIPS_GET_IMMED(mips),16), EQ, 0, 1);
}

static int BNEL(MIPS_instr mips){
	

	if(CANT_COMPILE_DELAY()){
		genCallInterp(mips);
		return INTERPRETED;
	}

	genCmp64(4, MIPS_GET_RA(mips), MIPS_GET_RB(mips));

	return branch(signExtend(MIPS_GET_IMMED(mips),16), NE, 0, 1);
}

static int BLEZL(MIPS_instr mips){
	

	if(CANT_COMPILE_DELAY()){
		genCallInterp(mips);
		return INTERPRETED;
	}

	genCmpi64(4, MIPS_GET_RA(mips), 0);

	return branch(signExtend(MIPS_GET_IMMED(mips),16), LE, 0, 1);
}

static int BGTZL(MIPS_instr mips){
	

	if(CANT_COMPILE_DELAY()){
		genCallInterp(mips);
		return INTERPRETED;
	}

	genCmpi64(4, MIPS_GET_RA(mips), 0);

	return branch(signExtend(MIPS_GET_IMMED(mips),16), GT, 0, 1);
}

static int DADDIU(MIPS_instr mips){
	
#if defined(INTERPRET_DW) || defined(INTERPRET_DADDIU)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_DW || INTERPRET_DADDIU

	RegMapping rs = mapRegister64( MIPS_GET_RS(mips) );
	RegMapping rt = mapRegister64New( MIPS_GET_RT(mips) );

	// Sign extend the immediate for the MSW
	EMIT_ADDI(0, 0, (MIPS_GET_IMMED(mips)&0x8000) ? ~0 : 0);
	// Add the immediate to the LSW
	EMIT_ADDIC(rt.lo, rs.lo, MIPS_GET_IMMED(mips));
	// Add the MSW with the sign-extension and the carry
	EMIT_ADDE(rt.hi, rs.hi, 0);

	return CONVERT_SUCCESS;
#endif
}

static int DADDI(MIPS_instr mips){
	return DADDIU(mips);
}

static int LDL(MIPS_instr mips){
	
#ifdef INTERPRET_LDL
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_LDL
	// TODO: ldl
	return CONVERT_ERROR;
#endif
}

static int LDR(MIPS_instr mips){
	
#ifdef INTERPRET_LDR
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_LDR
	// TODO: ldr
	return CONVERT_ERROR;
#endif
}

static int LB(MIPS_instr mips){
	
#ifdef INTERPRET_LB
	genCallInterp(mips);
	return INTERPRETED;
#endif

    return genCallDynaMemVM(MIPS_GET_RS(mips),MIPS_GET_RT(mips),MEM_LB,MIPS_GET_IMMED(mips));
}

static int LH(MIPS_instr mips){
	
#ifdef INTERPRET_LH
	genCallInterp(mips);
	return INTERPRETED;
#endif

    return genCallDynaMemVM(MIPS_GET_RS(mips),MIPS_GET_RT(mips),MEM_LH,MIPS_GET_IMMED(mips));
}

static int LWL(MIPS_instr mips){
	
#ifdef INTERPRET_LWL
	genCallInterp(mips);
	return INTERPRETED;
#endif

    return genCallDynaMemVM(MIPS_GET_RS(mips),MIPS_GET_RT(mips),MEM_LWL,MIPS_GET_IMMED(mips));
}

static int LW(MIPS_instr mips){
	
#ifdef INTERPRET_LW
	genCallInterp(mips);
	return INTERPRETED;
#endif

    return genCallDynaMemVM(MIPS_GET_RS(mips),MIPS_GET_RT(mips),MEM_LW,MIPS_GET_IMMED(mips));
}

static int LBU(MIPS_instr mips){

#ifdef INTERPRET_LBU
	genCallInterp(mips);
	return INTERPRETED;
#endif

    return genCallDynaMemVM(MIPS_GET_RS(mips),MIPS_GET_RT(mips),MEM_LBU,MIPS_GET_IMMED(mips));
}

static int LHU(MIPS_instr mips){
	
#ifdef INTERPRET_LHU
	genCallInterp(mips);
	return INTERPRETED;
#endif

    return genCallDynaMemVM(MIPS_GET_RS(mips),MIPS_GET_RT(mips),MEM_LHU,MIPS_GET_IMMED(mips));
}

static int LWR(MIPS_instr mips){
	
#ifdef INTERPRET_LWR
	genCallInterp(mips);
	return INTERPRETED;
#endif
	
    return genCallDynaMemVM(MIPS_GET_RS(mips),MIPS_GET_RT(mips),MEM_LWR,MIPS_GET_IMMED(mips));
}

static int LWU(MIPS_instr mips){
	
#ifdef INTERPRET_LWU
	genCallInterp(mips);
	return INTERPRETED;
#endif

    return genCallDynaMemVM(MIPS_GET_RS(mips),MIPS_GET_RT(mips),MEM_LWU,MIPS_GET_IMMED(mips));
}

static int LD(MIPS_instr mips){
	
#ifdef INTERPRET_LD
	genCallInterp(mips);
	return INTERPRETED;
#endif

    return genCallDynaMemVM(MIPS_GET_RS(mips),MIPS_GET_RT(mips),MEM_LD,MIPS_GET_IMMED(mips));
}

extern long long int reg_cop1_fgr_64[32];

static int LWC1(MIPS_instr mips){
	
#ifdef INTERPRET_LWC1
	genCallInterp(mips);
	return INTERPRETED;
#endif

    return genCallDynaMemVM(MIPS_GET_RS(mips),MIPS_GET_RT(mips),MEM_LWC1,MIPS_GET_IMMED(mips));
}

static int LDC1(MIPS_instr mips){
	
#ifdef INTERPRET_LDC1
	genCallInterp(mips);
	return INTERPRETED;
#endif

    return genCallDynaMemVM(MIPS_GET_RS(mips),MIPS_GET_RT(mips),MEM_LDC1,MIPS_GET_IMMED(mips));
}

static int SB(MIPS_instr mips){
	
#ifdef INTERPRET_SB
	genCallInterp(mips);
	return INTERPRETED;
#endif

    return genCallDynaMemVM(MIPS_GET_RS(mips),MIPS_GET_RT(mips),MEM_SB,MIPS_GET_IMMED(mips));
}

static int SH(MIPS_instr mips){
	
#ifdef INTERPRET_SH
	genCallInterp(mips);
	return INTERPRETED;
#endif

    return genCallDynaMemVM(MIPS_GET_RS(mips),MIPS_GET_RT(mips),MEM_SH,MIPS_GET_IMMED(mips));
}

static int SWL(MIPS_instr mips){
	
#ifdef INTERPRET_SWL
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_SWL
	// TODO: swl
	return CONVERT_ERROR;
#endif
}

static int SW(MIPS_instr mips){
	
#ifdef INTERPRET_SW
	genCallInterp(mips);
	return INTERPRETED;
#endif

    return genCallDynaMemVM(MIPS_GET_RS(mips),MIPS_GET_RT(mips),MEM_SW,MIPS_GET_IMMED(mips));
}

static int SDL(MIPS_instr mips){
	
#ifdef INTERPRET_SDL
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_SDL
	// TODO: sdl
	return CONVERT_ERROR;
#endif
}

static int SDR(MIPS_instr mips){
	
#ifdef INTERPRET_SDR
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_SDR
	// TODO: sdr
	return CONVERT_ERROR;
#endif
}

static int SWR(MIPS_instr mips){
	
#ifdef INTERPRET_SWR
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_SWR
	// TODO: swr
	return CONVERT_ERROR;
#endif
}

static int SD(MIPS_instr mips){
	
#ifdef INTERPRET_SD
	genCallInterp(mips);
	return INTERPRETED;
#endif

    return genCallDynaMemVM(MIPS_GET_RS(mips),MIPS_GET_RT(mips),MEM_SD,MIPS_GET_IMMED(mips));
}

static int SWC1(MIPS_instr mips){
	
#ifdef INTERPRET_SWC1
	genCallInterp(mips);
	return INTERPRETED;
#endif

    return genCallDynaMemVM(MIPS_GET_RS(mips),MIPS_GET_RT(mips),MEM_SWC1,MIPS_GET_IMMED(mips));
}

static int SDC1(MIPS_instr mips){
	
#ifdef INTERPRET_SDC1
	genCallInterp(mips);
	return INTERPRETED;
#endif

    return genCallDynaMemVM(MIPS_GET_RS(mips),MIPS_GET_RT(mips),MEM_SDC1,MIPS_GET_IMMED(mips));
}

static int CACHE(MIPS_instr mips){
	return CONVERT_ERROR;
}

static int LL(MIPS_instr mips){
	
#ifdef INTERPRET_LL
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_LL
	// TODO: ll
	return CONVERT_ERROR;
#endif
}

static int SC(MIPS_instr mips){
	
#ifdef INTERPRET_SC
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_SC
	// TODO: sc
	return CONVERT_ERROR;
#endif
}

// -- Special Functions --

static int SLL(MIPS_instr mips){
	

	int rt = mapRegister( MIPS_GET_RT(mips) );
	int rd = mapRegisterNew( MIPS_GET_RD(mips) );

	EMIT_SLWI(rd, rt, MIPS_GET_SA(mips));

	return CONVERT_SUCCESS;
}

static int SRL(MIPS_instr mips){
	

	int rt = mapRegister( MIPS_GET_RT(mips) );
	int rd = mapRegisterNew( MIPS_GET_RD(mips) );

	EMIT_SRWI(rd, rt, MIPS_GET_SA(mips));

	return CONVERT_SUCCESS;
}

static int SRA(MIPS_instr mips){
	

	int rt = mapRegister( MIPS_GET_RT(mips) );
	int rd = mapRegisterNew( MIPS_GET_RD(mips) );

	EMIT_SRAWI(rd, rt, MIPS_GET_SA(mips));

	return CONVERT_SUCCESS;
}

static int SLLV(MIPS_instr mips){
	
	int rt = mapRegister( MIPS_GET_RT(mips) );
	int rs = mapRegister( MIPS_GET_RS(mips) );
	int rd = mapRegisterNew( MIPS_GET_RD(mips) );
	EMIT_RLWINM(0, rs, 0, 27, 31); // Mask the lower 5-bits of rs
	EMIT_SLW(rd, rt, 0);

	return CONVERT_SUCCESS;
}

static int SRLV(MIPS_instr mips){
	
	int rt = mapRegister( MIPS_GET_RT(mips) );
	int rs = mapRegister( MIPS_GET_RS(mips) );
	int rd = mapRegisterNew( MIPS_GET_RD(mips) );
	EMIT_RLWINM(0, rs, 0, 27, 31); // Mask the lower 5-bits of rs
	EMIT_SRW(rd, rt, 0);

	return CONVERT_SUCCESS;
}

static int SRAV(MIPS_instr mips){
	
	int rt = mapRegister( MIPS_GET_RT(mips) );
	int rs = mapRegister( MIPS_GET_RS(mips) );
	int rd = mapRegisterNew( MIPS_GET_RD(mips) );
	EMIT_RLWINM(0, rs, 0, 27, 31); // Mask the lower 5-bits of rs
	EMIT_SRAW(rd, rt, 0);

	return CONVERT_SUCCESS;
}

static int JR(MIPS_instr mips){
	

	if(CANT_COMPILE_DELAY()){
		genCallInterp(mips);
		return INTERPRETED;
	}

	flushRegisters();
	reset_code_addr();
	
	EMIT_STW(mapRegister(MIPS_GET_RS(mips)),
			REG_LOCALRS*8+4, DYNAREG_REG);
	invalidateRegisters();

	// Check the delay slot, and note how big it is
	PowerPC_instr* preDelay = get_curr_dst();
	check_delaySlot();
	int delaySlot = get_curr_dst() - preDelay;

#ifdef COMPARE_CORE
	EMIT_LI(4, 1);
#endif
	genUpdateCount(0);

#ifdef INTERPRET_JR
	genJumpTo(REG_LOCALRS, JUMPTO_REG);
#else // INTERPRET_JR
	// TODO: jr
#endif

	// Let's still recompile the delay slot in place in case its branched to
	if(delaySlot){ if(is_j_dst()){ unget_last_src(); delaySlotNext = 2; } }
	else nop_ignored();

#ifdef INTERPRET_JR
	return INTERPRETED;
#else // INTERPRET_JR
	return CONVER_ERROR;
#endif
}

static int JALR(MIPS_instr mips){
	

	if(CANT_COMPILE_DELAY()){
		genCallInterp(mips);
		return INTERPRETED;
	}

	flushRegisters();
	reset_code_addr();

	EMIT_STW(mapRegister(MIPS_GET_RS(mips)),
			REG_LOCALRS*8+4, DYNAREG_REG);
	invalidateRegisters();

	// Check the delay slot, and note how big it is
	PowerPC_instr* preDelay = get_curr_dst();
	check_delaySlot();
	int delaySlot = get_curr_dst() - preDelay;

#ifdef COMPARE_CORE
	EMIT_LI(4, 1);
#endif
	genUpdateCount(0);

	// Set LR to next instruction
	int rd = mapRegisterNew(MIPS_GET_RD(mips));
	// lis	lr, pc@ha(0)
	EMIT_LIS(rd, (get_src_pc()+4)>>16);
	// la	lr, pc@l(lr)
	EMIT_ORI(rd, rd, get_src_pc()+4);

	flushRegisters();

#ifdef INTERPRET_JALR
	genJumpTo(REG_LOCALRS, JUMPTO_REG);
#else // INTERPRET_JALR
	// TODO: jalr
#endif

	// Let's still recompile the delay slot in place in case its branched to
	if(delaySlot){ if(is_j_dst()){ unget_last_src(); delaySlotNext = 2; } }
	else nop_ignored();

#ifdef INTERPRET_JALR
	return INTERPRETED;
#else // INTERPRET_JALR
	return CONVERT_ERROR;
#endif
}

static int SYSCALL(MIPS_instr mips){
	
#ifdef INTERPRET_SYSCALL
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_SYSCALL
	// TODO: syscall
	return CONVERT_ERROR;
#endif
}

static int BREAK(MIPS_instr mips){
	
#ifdef INTERPRET_BREAK
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_BREAK
	return CONVERT_ERROR;
#endif
}

static int SYNC(MIPS_instr mips){
	
#ifdef INTERPRET_SYNC
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_SYNC
	return CONVERT_ERROR;
#endif
}

static int MFHI(MIPS_instr mips){
	
#ifdef INTERPRET_HILO
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_HILO

	RegMapping hi = mapRegister64( MIPS_REG_HI );
	RegMapping rd = mapRegister64New( MIPS_GET_RD(mips) );

	// mr rd, hi
	EMIT_OR(rd.lo, hi.lo, hi.lo);
	EMIT_OR(rd.hi, hi.hi, hi.hi);

	return CONVERT_SUCCESS;
#endif
}

static int MTHI(MIPS_instr mips){
	
#ifdef INTERPRET_HILO
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_HILO

	RegMapping rs = mapRegister64( MIPS_GET_RS(mips) );
	RegMapping hi = mapRegister64New( MIPS_REG_HI );

	// mr hi, rs
	EMIT_OR(hi.lo, rs.lo, rs.lo);
	EMIT_OR(hi.hi, rs.hi, rs.hi);

	return CONVERT_SUCCESS;
#endif
}

static int MFLO(MIPS_instr mips){
	
#ifdef INTERPRET_HILO
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_HILO

	RegMapping lo = mapRegister64( MIPS_REG_LO );
	RegMapping rd = mapRegister64New( MIPS_GET_RD(mips) );

	// mr rd, lo
	EMIT_OR(rd.lo, lo.lo, lo.lo);
	EMIT_OR(rd.hi, lo.hi, lo.hi);

	return CONVERT_SUCCESS;
#endif
}

static int MTLO(MIPS_instr mips){
	
#ifdef INTERPRET_HILO
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_HILO

	RegMapping rs = mapRegister64( MIPS_GET_RS(mips) );
	RegMapping lo = mapRegister64New( MIPS_REG_LO );

	// mr lo, rs
	EMIT_OR(lo.lo, rs.lo, rs.lo);
	EMIT_OR(lo.hi, rs.hi, rs.hi);

	return CONVERT_SUCCESS;
#endif
}

static int MULT(MIPS_instr mips){
	
#ifdef INTERPRET_MULT
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_MULT
	int rs = mapRegister( MIPS_GET_RS(mips) );
	int rt = mapRegister( MIPS_GET_RT(mips) );
	int hi = mapRegisterNew( MIPS_REG_HI );
	int lo = mapRegisterNew( MIPS_REG_LO );

	// Don't multiply if they're using r0
	if(MIPS_GET_RS(mips) && MIPS_GET_RT(mips)){
		// mullw lo, rs, rt
		EMIT_MULLW(lo, rs, rt);
		// mulhw hi, rs, rt
		EMIT_MULHW(hi, rs, rt);
	} else {
		// li lo, 0
		EMIT_LI(lo, 0);
		// li hi, 0
		EMIT_LI(hi, 0);
	}

	return CONVERT_SUCCESS;
#endif
}

static int MULTU(MIPS_instr mips){
	
#ifdef INTERPRET_MULTU
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_MULTU
	int rs = mapRegister( MIPS_GET_RS(mips) );
	int rt = mapRegister( MIPS_GET_RT(mips) );
	int hi = mapRegisterNew( MIPS_REG_HI );
	int lo = mapRegisterNew( MIPS_REG_LO );

	// Don't multiply if they're using r0
	if(MIPS_GET_RS(mips) && MIPS_GET_RT(mips)){
		// mullw lo, rs, rt
		EMIT_MULLW(lo, rs, rt);
		// mulhwu hi, rs, rt
		EMIT_MULHWU(hi, rs, rt);
	} else {
		// li lo, 0
		EMIT_LI(lo, 0);
		// li hi, 0
		EMIT_LI(hi, 0);
	}

	return CONVERT_SUCCESS;
#endif
}

static int DIV(MIPS_instr mips){
	
#ifdef INTERPRET_DIV
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_DIV
	// This instruction computes the quotient and remainder
	//   and stores the results in lo and hi respectively
	int rs = mapRegister( MIPS_GET_RS(mips) );
	int rt = mapRegister( MIPS_GET_RT(mips) );
	int hi = mapRegisterNew( MIPS_REG_HI );
	int lo = mapRegisterNew( MIPS_REG_LO );

	// Don't divide if they're using r0
	if(MIPS_GET_RS(mips) && MIPS_GET_RT(mips)){
		// divw lo, rs, rt
		EMIT_DIVW(lo, rs, rt);
		// This is how you perform a mod in PPC
		// divw lo, rs, rt
		// NOTE: We already did that
		// mullw hi, lo, rt
		EMIT_MULLW(hi, lo, rt);
		// subf hi, hi, rs
		EMIT_SUBF(hi, hi, rs);
	}

	return CONVERT_SUCCESS;
#endif
}

static int DIVU(MIPS_instr mips){
	
#ifdef INTERPRET_DIVU
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_DIVU
	// This instruction computes the quotient and remainder
	//   and stores the results in lo and hi respectively
	int rs = mapRegister( MIPS_GET_RS(mips) );
	int rt = mapRegister( MIPS_GET_RT(mips) );
	int hi = mapRegisterNew( MIPS_REG_HI );
	int lo = mapRegisterNew( MIPS_REG_LO );

	// Don't divide if they're using r0
	if(MIPS_GET_RS(mips) && MIPS_GET_RT(mips)){
		// divwu lo, rs, rt
		EMIT_DIVWU(lo, rs, rt);
		// This is how you perform a mod in PPC
		// divw lo, rs, rt
		// NOTE: We already did that
		// mullw hi, lo, rt
		EMIT_MULLW(hi, lo, rt);
		// subf hi, hi, rs
		EMIT_SUBF(hi, hi, rs);
	}

	return CONVERT_SUCCESS;
#endif
}

static int DSLLV(MIPS_instr mips){
	
#if defined(INTERPRET_DW) || defined(INTERPRET_DSLLV)
	genCallInterp(mips);
	return INTERPRETED;
#else  // INTERPRET_DW || INTERPRET_DSLLV

	int rs = mapRegister( MIPS_GET_RS(mips) );
	int sa = mapRegisterTemp();
	RegMapping rt = mapRegister64( MIPS_GET_RT(mips) );
	RegMapping rd = mapRegister64New( MIPS_GET_RD(mips) );

	// Mask off the shift amount (0x3f)
	EMIT_RLWINM(sa, rs, 0, 26, 31);
	// Shift the MSW
	EMIT_SLW(rd.hi, rt.hi, sa);
	// Calculate 32-sh
	EMIT_SUBFIC(0, sa, 32);
	// Extract the bits that will be shifted out the LSW (sh < 32)
	EMIT_SRW(0, rt.lo, 0);
	// Insert the bits into the MSW
	EMIT_OR(rd.hi, rd.hi, 0);
	// Calculate sh-32
	EMIT_ADDI(0, sa, -32);
	// Extract the bits that will be shifted out the LSW (sh > 31)
	EMIT_SLW(0, rt.lo, 0);
	// Insert the bits into the MSW
	EMIT_OR(rd.hi, rd.hi, 0);
	// Shift the LSW
	EMIT_SLW(rd.lo, rt.lo, sa);

	unmapRegisterTemp(sa);

	return CONVERT_SUCCESS;
#endif
}

static int DSRLV(MIPS_instr mips){
	
#if defined(INTERPRET_DW) || defined(INTERPRET_DSRLV)
	genCallInterp(mips);
	return INTERPRETED;
#else  // INTERPRET_DW || INTERPRET_DSRLV

	int rs = mapRegister( MIPS_GET_RS(mips) );
	int sa = mapRegisterTemp();
	RegMapping rt = mapRegister64( MIPS_GET_RT(mips) );
	RegMapping rd = mapRegister64New( MIPS_GET_RD(mips) );

	// Mask off the shift amount (0x3f)
	EMIT_RLWINM(sa, rs, 0, 26, 31);
	// Shift the LSW
	EMIT_SRW(rd.lo, rt.lo, sa);
	// Calculate 32-sh
	EMIT_SUBFIC(0, sa, 32);
	// Extract the bits that will be shifted out the MSW (sh < 32)
	EMIT_SLW(0, rt.hi, 0);
	// Insert the bits into the LSW
	EMIT_OR(rd.lo, rd.lo, 0);
	// Calculate sh-32
	EMIT_ADDI(0, sa, -32);
	// Extract the bits that will be shifted out the MSW (sh > 31)
	EMIT_SRW(0, rt.hi, 0);
	// Insert the bits into the LSW
	EMIT_OR(rd.lo, rd.lo, 0);
	// Shift the MSW
	EMIT_SRW(rd.hi, rt.hi, sa);

	unmapRegisterTemp(sa);

	return CONVERT_SUCCESS;
#endif
}

static int DSRAV(MIPS_instr mips){
	
#if defined(INTERPRET_DW) || defined(INTERPRET_DSRAV)
	genCallInterp(mips);
	return INTERPRETED;
#else  // INTERPRET_DW || INTERPRET_DSRAV

	int rs = mapRegister( MIPS_GET_RS(mips) );
	int sa = mapRegisterTemp();
	RegMapping rt = mapRegister64( MIPS_GET_RT(mips) );
	RegMapping rd = mapRegister64New( MIPS_GET_RD(mips) );

	// Mask off the shift amount (0x3f)
	EMIT_RLWINM(sa, rs, 0, 26, 31);
	// Check whether the shift amount is < 32
	EMIT_CMPI(sa, 32, 1);
	// Shift the LSW
	EMIT_SRW(rd.lo, rt.lo, sa);
	// Skip over this code if sh >= 32
	EMIT_BGE(1, 5, 0, 0);
	// Calculate 32-sh
	EMIT_SUBFIC(0, sa, 32);
	// Extract the bits that will be shifted out the MSW (sh < 32)
	EMIT_SLW(0, rt.hi, 0);
	// Insert the bits into the LSW
	EMIT_OR(rd.lo, rd.lo, 0);
	// Skip over the else
	EMIT_B(4, 0, 0);
	// Calculate sh-32
	EMIT_ADDI(0, sa, -32);
	// Extract the bits that will be shifted out the MSW (sh > 31)
	EMIT_SRAW(0, rt.hi, 0);
	// Insert the bits into the LSW
	EMIT_OR(rd.lo, rd.lo, 0);
	// Shift the MSW
	EMIT_SRAW(rd.hi, rt.hi, sa);

	unmapRegisterTemp(sa);

	return CONVERT_SUCCESS;
#endif
}

static int DMULT(MIPS_instr mips){
	
#if defined(INTERPRET_DW) || defined(INTERPRET_DMULT)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_DW || INTERPRET_DMULT
	// TODO: dmult
	return CONVERT_ERROR;
#endif
}

static int DMULTU(MIPS_instr mips){
	
#if defined(INTERPRET_DW) || defined(INTERPRET_DMULTU)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_DW || INTERPRET_DMULTU
	// TODO: dmultu
	return CONVERT_ERROR;
#endif
}

static int DDIV(MIPS_instr mips){
	
#if defined(INTERPRET_DW) || defined(INTERPRET_DDIV)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_DW || INTERPRET_DDIV
	// TODO: ddiv
	return CONVERT_ERROR;
#endif
}

static int DDIVU(MIPS_instr mips){
	
#if defined(INTERPRET_DW) || defined(INTERPRET_DDIVU)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_DW || INTERPRET_DDIVU
	// TODO: ddivu
	return CONVERT_ERROR;
#endif
}

static int DADDU(MIPS_instr mips){
	
#if defined(INTERPRET_DW) || defined(INTERPRET_DADDU)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_DW || INTERPRET_DADDU

	RegMapping rs = mapRegister64( MIPS_GET_RS(mips) );
	RegMapping rt = mapRegister64( MIPS_GET_RT(mips) );
	RegMapping rd = mapRegister64New( MIPS_GET_RD(mips) );

	EMIT_ADDC(rd.lo, rs.lo, rt.lo);
	EMIT_ADDE(rd.hi, rs.hi, rt.hi);

	return CONVERT_SUCCESS;
#endif
}

static int DADD(MIPS_instr mips){
	return DADDU(mips);
}

static int DSUBU(MIPS_instr mips){
	
#if defined(INTERPRET_DW) || defined(INTERPRET_DSUBU)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_DW || INTERPRET_DSUBU

	RegMapping rs = mapRegister64( MIPS_GET_RS(mips) );
	RegMapping rt = mapRegister64( MIPS_GET_RT(mips) );
	RegMapping rd = mapRegister64New( MIPS_GET_RD(mips) );

	EMIT_SUBFC(rd.lo, rt.lo, rs.lo);
	EMIT_SUBFE(rd.hi, rt.hi, rs.hi);

	return CONVERT_SUCCESS;
#endif
}

static int DSUB(MIPS_instr mips){
	return DSUBU(mips);
}

static int DSLL(MIPS_instr mips){
	
#if defined(INTERPRET_DW) || defined(INTERPRET_DSLL)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_DW || INTERPRET_DSLL

	RegMapping rt = mapRegister64( MIPS_GET_RT(mips) );
	RegMapping rd = mapRegister64New( MIPS_GET_RD(mips) );
	int sa = MIPS_GET_SA(mips);

	if(sa){
		// Shift MSW left by SA
		EMIT_SLWI(rd.hi, rt.hi, sa);
		// Extract the bits shifted out of the LSW
		EMIT_RLWINM(0, rt.lo, sa, 32-sa, 31);
		// Insert those bits into the MSW
		EMIT_OR(rd.hi, rd.hi, 0);
		// Shift LSW left by SA
		EMIT_SLWI(rd.lo, rt.lo, sa);
	} else {
		// Copy over the register
		EMIT_ADDI(rd.hi, rt.hi, 0);
		EMIT_ADDI(rd.lo, rt.lo, 0);
	}

	return CONVERT_SUCCESS;
#endif
}

static int DSRL(MIPS_instr mips){
	
#if defined(INTERPRET_DW) || defined(INTERPRET_DSRL)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_DW || INTERPRET_DSRL

	RegMapping rt = mapRegister64( MIPS_GET_RT(mips) );
	RegMapping rd = mapRegister64New( MIPS_GET_RD(mips) );
	int sa = MIPS_GET_SA(mips);

	if(sa){
		// Shift LSW right by SA
		EMIT_SRWI(rd.lo, rt.lo, sa);
		// Extract the bits shifted out of the MSW
		EMIT_RLWINM(0, rt.hi, 32-sa, 0, sa-1);
		// Insert those bits into the LSW
		EMIT_OR(rd.lo, rd.lo, 0);
		// Shift MSW right by SA
		EMIT_SRWI(rd.hi, rt.hi, sa);
	} else {
		// Copy over the register
		EMIT_ADDI(rd.hi, rt.hi, 0);
		EMIT_ADDI(rd.lo, rt.lo, 0);
	}

	return CONVERT_SUCCESS;
#endif
}

static int DSRA(MIPS_instr mips){
	
#if defined(INTERPRET_DW) || defined(INTERPRET_DSRA)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_DW || INTERPRET_DSRA

	RegMapping rt = mapRegister64( MIPS_GET_RT(mips) );
	RegMapping rd = mapRegister64New( MIPS_GET_RD(mips) );
	int sa = MIPS_GET_SA(mips);

	if(sa){
		// Shift LSW right by SA
		EMIT_SRWI(rd.lo, rt.lo, sa);
		// Extract the bits shifted out of the MSW
		EMIT_RLWINM(0, rt.hi, 32-sa, 0, sa-1);
		// Insert those bits into the LSW
		EMIT_OR(rd.lo, rd.lo, 0);
		// Shift (arithmetically) MSW right by SA
		EMIT_SRAWI(rd.hi, rt.hi, sa);
	} else {
		// Copy over the register
		EMIT_ADDI(rd.hi, rt.hi, 0);
		EMIT_ADDI(rd.lo, rt.lo, 0);
	}

	return CONVERT_SUCCESS;
#endif
}

static int DSLL32(MIPS_instr mips){
	
#if defined(INTERPRET_DW) || defined(INTERPRET_DSLL32)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_DW || INTERPRET_DSLL32

	RegMapping rt = mapRegister64( MIPS_GET_RT(mips) );
	RegMapping rd = mapRegister64New( MIPS_GET_RD(mips) );
	int sa = MIPS_GET_SA(mips);

	// Shift LSW into MSW and by SA
	EMIT_SLWI(rd.hi, rt.lo, sa);
	// Clear out LSW
	EMIT_ADDI(rd.lo, 0, 0);

	return CONVERT_SUCCESS;
#endif
}

static int DSRL32(MIPS_instr mips){
	
#if defined(INTERPRET_DW) || defined(INTERPRET_DSRL32)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_DW || INTERPRET_DSRL32

	RegMapping rt = mapRegister64( MIPS_GET_RT(mips) );
	RegMapping rd = mapRegister64New( MIPS_GET_RD(mips) );
	int sa = MIPS_GET_SA(mips);

	// Shift MSW into LSW and by SA
	EMIT_SRWI(rd.lo, rt.hi, sa);
	// Clear out MSW
	EMIT_ADDI(rd.hi, 0, 0);

	return CONVERT_SUCCESS;
#endif
}

static int DSRA32(MIPS_instr mips){
	
#if defined(INTERPRET_DW) || defined(INTERPRET_DSRA32)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_DW || INTERPRET_DSRA32

	RegMapping rt = mapRegister64( MIPS_GET_RT(mips) );
	RegMapping rd = mapRegister64New( MIPS_GET_RD(mips) );
	int sa = MIPS_GET_SA(mips);

	// Shift (arithmetically) MSW into LSW and by SA
	EMIT_SRAWI(rd.lo, rt.hi, sa);
	// Fill MSW with sign of MSW
	EMIT_SRAWI(rd.hi, rt.hi, 31);

	return CONVERT_SUCCESS;
#endif
}

static int ADDU(MIPS_instr mips){
	
	int rt = mapRegister( MIPS_GET_RT(mips) );
	int rs = mapRegister( MIPS_GET_RS(mips) );
	EMIT_ADD(
	        mapRegisterNew( MIPS_GET_RD(mips) ),
	        rs,
	        rt);

	return CONVERT_SUCCESS;
}

static int ADD(MIPS_instr mips){
	return ADDU(mips);
}

static int SUBU(MIPS_instr mips){
	
	int rt = mapRegister( MIPS_GET_RT(mips) );
	int rs = mapRegister( MIPS_GET_RS(mips) );
	EMIT_SUB(
	        mapRegisterNew( MIPS_GET_RD(mips) ),
	        rs,
	        rt);

	return CONVERT_SUCCESS;
}

static int SUB(MIPS_instr mips){
	return SUBU(mips);
}

static int AND(MIPS_instr mips){
	
	RegMapping rt = mapRegister64( MIPS_GET_RT(mips) );
	RegMapping rs = mapRegister64( MIPS_GET_RS(mips) );
	RegMapping rd = mapRegister64New( MIPS_GET_RD(mips) );

	EMIT_AND(rd.hi, rs.hi, rt.hi);
	EMIT_AND(rd.lo, rs.lo, rt.lo);

	return CONVERT_SUCCESS;
}

static int OR(MIPS_instr mips){
	
	RegMapping rt = mapRegister64( MIPS_GET_RT(mips) );
	RegMapping rs = mapRegister64( MIPS_GET_RS(mips) );
	RegMapping rd = mapRegister64New( MIPS_GET_RD(mips) );

	EMIT_OR(rd.hi, rs.hi, rt.hi);
	EMIT_OR(rd.lo, rs.lo, rt.lo);

	return CONVERT_SUCCESS;
}

static int XOR(MIPS_instr mips){
	
	RegMapping rt = mapRegister64( MIPS_GET_RT(mips) );
	RegMapping rs = mapRegister64( MIPS_GET_RS(mips) );
	RegMapping rd = mapRegister64New( MIPS_GET_RD(mips) );

	EMIT_XOR(rd.hi, rs.hi, rt.hi);
	EMIT_XOR(rd.lo, rs.lo, rt.lo);

	return CONVERT_SUCCESS;
}

static int NOR(MIPS_instr mips){
	
	RegMapping rt = mapRegister64( MIPS_GET_RT(mips) );
	RegMapping rs = mapRegister64( MIPS_GET_RS(mips) );
	RegMapping rd = mapRegister64New( MIPS_GET_RD(mips) );

	EMIT_NOR(rd.hi, rs.hi, rt.hi);
	EMIT_NOR(rd.lo, rs.lo, rt.lo);

	return CONVERT_SUCCESS;
}

static int SLT(MIPS_instr mips){
	
#ifdef INTERPRET_SLT
	genCallInterp(mips);
	return INTERPRETED;
#else
	// FIXME: Do I need to worry about 64-bit values?
	int rt = mapRegister( MIPS_GET_RT(mips) );
	int rs = mapRegister( MIPS_GET_RS(mips) );
	int rd = mapRegisterNew( MIPS_GET_RD(mips) );

	// carry = rs < rt ? 0 : 1 (unsigned)
	EMIT_SUBFC(0, rt, rs);
	// rd = ~(rs ^ rt)
	EMIT_EQV(rd, rt, rs);
	// rd = sign(rs) == sign(rt) ? 1 : 0
	EMIT_SRWI(rd, rd, 31);
	// rd += carry
	EMIT_ADDZE(rd, rd);
	// rt &= 1 ( = (sign(rs) == sign(rt)) xor (rs < rt (unsigned)) )
	EMIT_RLWINM(rd, rd, 0, 31, 31);

	return CONVERT_SUCCESS;
#endif
}

static int SLTU(MIPS_instr mips){
	
#ifdef INTERPRET_SLTU
	genCallInterp(mips);
	return INTERPRETED;
#else
	// FIXME: Do I need to worry about 64-bit values?
	int rt = mapRegister( MIPS_GET_RT(mips) );
	int rs = mapRegister( MIPS_GET_RS(mips) );
	int rd = mapRegisterNew( MIPS_GET_RD(mips) );
	// carry = rs < rt ? 0 : 1
	EMIT_SUBFC(rd, rt, rs);
	// rd = carry - 1 ( = rs < rt ? -1 : 0 )
	EMIT_SUBFE(rd, rd, rd);
	// rd = !carry ( = rs < rt ? 1 : 0 )
	EMIT_NEG(rd, rd);

	return CONVERT_SUCCESS;
#endif
}

static int TEQ(MIPS_instr mips){
	
#ifdef INTERPRET_TRAPS
	genCallInterp(mips);
	return INTERPRETED;
#else
	return CONVERT_ERROR;
#endif
}

static int (*gen_special[64])(MIPS_instr) =
{
   SLL , NI   , SRL , SRA , SLLV   , NI    , SRLV  , SRAV  ,
   JR  , JALR , NI  , NI  , SYSCALL, BREAK , NI    , SYNC  ,
   MFHI, MTHI , MFLO, MTLO, DSLLV  , NI    , DSRLV , DSRAV ,
   MULT, MULTU, DIV , DIVU, DMULT  , DMULTU, DDIV  , DDIVU ,
   ADD , ADDU , SUB , SUBU, AND    , OR    , XOR   , NOR   ,
   NI  , NI   , SLT , SLTU, DADD   , DADDU , DSUB  , DSUBU ,
   NI  , NI   , NI  , NI  , TEQ    , NI    , NI    , NI    ,
   DSLL, NI   , DSRL, DSRA, DSLL32 , NI    , DSRL32, DSRA32
};

static int SPECIAL(MIPS_instr mips){
#ifdef INTERPRET_SPECIAL
	genCallInterp(mips);
	return INTERPRETED;
#else
	return gen_special[MIPS_GET_FUNC(mips)](mips);
#endif
}

// -- RegImmed Instructions --

// Since the RegImmed instructions are very similar:
//   BLTZ, BGEZ, BLTZL, BGEZL, BLZAL, BGEZAL, BLTZALL, BGEZALL
//   It's less work to handle them all in one function
static int REGIMM(MIPS_instr mips){
	
	int which = MIPS_GET_RT(mips);
	int cond   = which & 1; // t = GE, f = LT
	int likely = which & 2;
	int link   = which & 16;

	if(MIPS_GET_IMMED(mips) == 0xffff || CANT_COMPILE_DELAY()){
		// REGIMM_IDLE || virtual delay
		genCallInterp(mips);
		return INTERPRETED;
	}

	genCmpi64(4, MIPS_GET_RA(mips), 0);

	return branch(signExtend(MIPS_GET_IMMED(mips),16),
	              cond ? GE : LT, link, likely);
}

// -- COP0 Instructions --

static int TLBR(MIPS_instr mips){
	
#ifdef INTERPRET_TLBR
	genCallInterp(mips);
	return INTERPRETED;
#else
	return CONVERT_ERROR;
#endif
}

static int TLBWI(MIPS_instr mips){
	
#ifdef INTERPRET_TLBWI
	genCallInterp(mips);
	return INTERPRETED;
#else
	return CONVERT_ERROR;
#endif
}

static int TLBWR(MIPS_instr mips){
	
#ifdef INTERPRET_TLBWR
	genCallInterp(mips);
	return INTERPRETED;
#else
	return CONVERT_ERROR;
#endif
}

static int TLBP(MIPS_instr mips){
	
#ifdef INTERPRET_TLBP
	genCallInterp(mips);
	return INTERPRETED;
#else
	return CONVERT_ERROR;
#endif
}

static int ERET(MIPS_instr mips){
	
#ifdef INTERPRET_ERET
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_ERET

	flushRegisters();

	genUpdateCount(0);
	// Load Status
	EMIT_LWZ(3, 12*4, DYNAREG_COP0);
	// Load upper address of llbit
	EMIT_LIS(4, extractUpper16(&llbit));
	// Status & ~0x2
	EMIT_RLWINM(3, 3, 0, 31, 29);
	// llbit = 0
	EMIT_STW(DYNAREG_ZERO, extractLower16(&llbit), 4);
	// Store updated Status
	EMIT_STW(3, 12*4, DYNAREG_COP0);
	// check_interupt()
	EMIT_B(add_jump((int)(&check_interupt), 1, 1), 0, 1);
	// Load the old LR
	EMIT_LWZ(0, DYNAOFF_LR, 1);
	// interp_addr = EPC
	EMIT_LWZ(3, 14*4, DYNAREG_COP0);
	// Restore the LR
	EMIT_MTLR(0);
	// Return to trampoline with EPC
	EMIT_BLR(0);

	return CONVERT_SUCCESS;
#endif
}

static int (*gen_tlb[64])(MIPS_instr) =
{
   NI  , TLBR, TLBWI, NI, NI, NI, TLBWR, NI,
   TLBP, NI  , NI   , NI, NI, NI, NI   , NI,
   NI  , NI  , NI   , NI, NI, NI, NI   , NI,
   ERET, NI  , NI   , NI, NI, NI, NI   , NI,
   NI  , NI  , NI   , NI, NI, NI, NI   , NI,
   NI  , NI  , NI   , NI, NI, NI, NI   , NI,
   NI  , NI  , NI   , NI, NI, NI, NI   , NI,
   NI  , NI  , NI   , NI, NI, NI, NI   , NI
};

static int MFC0(MIPS_instr mips){
	
#ifdef INTERPRET_MFC0
	genCallInterp(mips);
	return INTERPRETED;
#else

	int rt = mapRegisterNew(MIPS_GET_RT(mips));
	// *rt = reg_cop0[rd]
	EMIT_LWZ(rt, MIPS_GET_RD(mips)*4, DYNAREG_COP0);

	return CONVERT_SUCCESS;
#endif
}

static int MTC0(MIPS_instr mips){
	
#ifdef INTERPRET_MTC0
	genCallInterp(mips);
	return INTERPRETED;
#else
	
	int rt = MIPS_GET_RT(mips), rrt;
	int rd = MIPS_GET_RD(mips);
	int tmp;
	
	switch(rd){
	case 0: // Index
		rrt = mapRegister(rt);
		// r0 = rt & 0x8000003F
		EMIT_RLWINM(0, rrt, 0, 26, 0);
		// reg_cop0[rd] = r0
		EMIT_STW(0, rd*4, DYNAREG_COP0);
		return CONVERT_SUCCESS;
	
	case 2: // EntryLo0
	case 3: // EntryLo1
		rrt = mapRegister(rt);
		// r0 = rt & 0x3FFFFFFF
		EMIT_RLWINM(0, rrt, 0, 2, 31);
		// reg_cop0[rd] = r0
		EMIT_STW(0, rd*4, DYNAREG_COP0);
		return CONVERT_SUCCESS;
	
	case 4: // Context
		rrt = mapRegister(rt), tmp = mapRegisterTemp();
		// tmp = reg_cop0[rd]
		EMIT_LWZ(tmp, rd*4, DYNAREG_COP0);
		// r0 = rt & 0xFF800000
		EMIT_RLWINM(0, rrt, 0, 0, 8);
		// tmp &= 0x007FFFF0
		EMIT_RLWINM(tmp, tmp, 0, 9, 27);
		// tmp |= r0
		EMIT_OR(tmp, tmp, 0);
		// reg_cop0[rd] = tmp
		EMIT_STW(tmp, rd*4, DYNAREG_COP0);
		
		unmapRegisterTemp(tmp);
		
		return CONVERT_SUCCESS;
	
	case 5: // PageMask
		rrt = mapRegister(rt);
		// r0 = rt & 0x01FFE000
		EMIT_RLWINM(0, rrt, 0, 7, 18);
		// reg_cop0[rd] = r0
		EMIT_STW(0, rd*4, DYNAREG_COP0);
		return CONVERT_SUCCESS;
	
	case 6: // Wired
		rrt = mapRegister(rt);
		// r0 = 31
		EMIT_ADDI(0, 0, 31);
		// reg_cop0[rd] = rt
		EMIT_STW(rrt, rd*4, DYNAREG_COP0);
		// reg_cop0[1] = r0
		EMIT_STW(0, 1*4, DYNAREG_COP0);
		return CONVERT_SUCCESS;
	
	case 10: // EntryHi
		rrt = mapRegister(rt);
		// r0 = rt & 0xFFFFE0FF
		EMIT_RLWINM(0, rrt, 0, 24, 18);
		// reg_cop0[rd] = r0
		EMIT_STW(0, rd*4, DYNAREG_COP0);
		return CONVERT_SUCCESS;
	
	case 13: // Cause
		rrt = mapRegister(rt);
		// TODO: Ensure that rrt == 0?
		// reg_cop0[rd] = rt
		EMIT_STW(rrt, rd*4, DYNAREG_COP0);
		return CONVERT_SUCCESS;
	
	case 14: // EPC
	case 16: // Config
	case 18: // WatchLo
	case 19: // WatchHi
		rrt = mapRegister(rt);
		// reg_cop0[rd] = rt
		EMIT_STW(rrt, rd*4, DYNAREG_COP0);
		return CONVERT_SUCCESS;
	
	case 28: // TagLo
		rrt = mapRegister(rt);
		// r0 = rt & 0x0FFFFFC0
		EMIT_RLWINM(0, rrt, 0, 4, 25);
		// reg_cop0[rd] = r0
		EMIT_STW(0, rd*4, DYNAREG_COP0);
		return CONVERT_SUCCESS;
	
	case 29: // TagHi
		// reg_cop0[rd] = 0
		EMIT_STW(DYNAREG_ZERO, rd*4, DYNAREG_COP0);
		return CONVERT_SUCCESS;
	
	case 1: // Random
	case 8: // BadVAddr
	case 15: // PRevID
	case 27: // CacheErr
		// Do nothing
		return CONVERT_SUCCESS;
	
	case 9: // Count
	case 11: // Compare
	case 12: // Status
	default:
		genCallInterp(mips);
		return INTERPRETED;
	}
	
#endif
}

static int TLB(MIPS_instr mips){
	
#ifdef INTERPRET_TLB
	genCallInterp(mips);
	return INTERPRETED;
#else
	return gen_tlb[mips&0x3f](mips);
#endif
}

static int (*gen_cop0[32])(MIPS_instr) =
{
   MFC0, NI, NI, NI, MTC0, NI, NI, NI,
   NI  , NI, NI, NI, NI  , NI, NI, NI,
   TLB , NI, NI, NI, NI  , NI, NI, NI,
   NI  , NI, NI, NI, NI  , NI, NI, NI
};

static int COP0(MIPS_instr mips){
#ifdef INTERPRET_COP0
	genCallInterp(mips);
	return INTERPRETED;
#else
	return gen_cop0[MIPS_GET_RS(mips)](mips);
#endif
}

// -- COP1 Instructions --

static int MFC1(MIPS_instr mips){
	
#if defined(INTERPRET_FP) || defined(INTERPRET_MFC1)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_FP

	genCheckFP();

	int fs = MIPS_GET_FS(mips);
	int rt = mapRegisterNew( MIPS_GET_RT(mips) );
	flushFPR(fs);

	// rt = reg_cop1_simple[fs]
	EMIT_LWZ(rt, fs*4, DYNAREG_FPR_32);
	// rt = *rt
	EMIT_LWZ(rt, 0, rt);

	return CONVERT_SUCCESS;
#endif
}

static int DMFC1(MIPS_instr mips){
	
#if defined(INTERPRET_FP) || defined(INTERPRET_DMFC1)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_FP

	genCheckFP();

	int fs = MIPS_GET_FS(mips);
	RegMapping rt = mapRegister64New( MIPS_GET_RT(mips) );
	int addr = mapRegisterTemp();
	flushFPR(fs);

	// addr = reg_cop1_double[fs]
	EMIT_LWZ(addr, fs*4, DYNAREG_FPR_64);
	// rt[hi] = *addr
	EMIT_LWZ(rt.hi, 0, addr);
	// rt[lo] = *(addr+4)
	EMIT_LWZ(rt.lo, 4, addr);

	unmapRegisterTemp(addr);

	return CONVERT_SUCCESS;
#endif
}

static int CFC1(MIPS_instr mips){
	
#if defined(INTERPRET_FP) || defined(INTERPRET_CFC1)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_FP || INTERPRET_CFC1

	genCheckFP();

	if(MIPS_GET_FS(mips) == 31){
		int rt = mapRegisterNew( MIPS_GET_RT(mips) );

		EMIT_LWZ(rt, 0, DYNAREG_FCR31);
	} else if(MIPS_GET_FS(mips) == 0){
		int rt = mapRegisterNew( MIPS_GET_RT(mips) );

		EMIT_LI(rt, 0x511);
	}

	return CONVERT_SUCCESS;
#endif
}

static int MTC1(MIPS_instr mips){
	
#if defined(INTERPRET_FP) || defined(INTERPRET_MTC1)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_FP

	genCheckFP();

	int rt = mapRegister( MIPS_GET_RT(mips) );
	int fs = MIPS_GET_FS(mips);
	int addr = mapRegisterTemp();
	invalidateFPR(fs);

	// addr = reg_cop1_simple[fs]
	EMIT_LWZ(addr, fs*4, DYNAREG_FPR_32);
	// *addr = rt
	EMIT_STW(rt, 0, addr);

	unmapRegisterTemp(addr);

	return CONVERT_SUCCESS;
#endif
}

static int DMTC1(MIPS_instr mips){
	
#if defined(INTERPRET_FP) || defined(INTERPRET_DMTC1)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_FP

	genCheckFP();

	RegMapping rt = mapRegister64( MIPS_GET_RT(mips) );
	int fs = MIPS_GET_FS(mips);
	int addr = mapRegisterTemp();
	invalidateFPR(fs);

	EMIT_LWZ(addr, fs*4, DYNAREG_FPR_64);
	EMIT_STW(rt.hi, 0, addr);
	EMIT_STW(rt.lo, 4, addr);

	unmapRegisterTemp(addr);

	return CONVERT_SUCCESS;
#endif
}

static int CTC1(MIPS_instr mips){
	
#if defined(INTERPRET_FP) || defined(INTERPRET_CTC1)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_FP || INTERPRET_CTC1

	genCheckFP();

	if(MIPS_GET_FS(mips) == 31){
		int rt = mapRegister( MIPS_GET_RT(mips) );

		EMIT_STW(rt, 0, DYNAREG_FCR31);
	}

	return CONVERT_SUCCESS;
#endif
}

static int BC(MIPS_instr mips){
	
#if defined(INTERPRET_BC)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_BC

	if(CANT_COMPILE_DELAY()){
		genCallInterp(mips);
		return INTERPRETED;
	}

	genCheckFP();

	int cond   = mips & 0x00010000;
	int likely = mips & 0x00020000;

	EMIT_LWZ(0, 0, DYNAREG_FCR31);
	EMIT_RLWINM(0, 0, 9, 31, 31);
	EMIT_CMPI(0, 0, 4);

	return branch(signExtend(MIPS_GET_IMMED(mips),16), cond?NE:EQ, 0, likely);
#endif
}

// -- Floating Point Arithmetic --
static int ADD_FP(MIPS_instr mips, int dbl){
	
#if defined(INTERPRET_FP) || defined(INTERPRET_FP_ADD)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_FP || INTERPRET_FP_ADD

	genCheckFP();

	int fs = mapFPR( MIPS_GET_FS(mips), dbl );
	int ft = mapFPR( MIPS_GET_FT(mips), dbl );
	int fd = mapFPRNew( MIPS_GET_FD(mips), dbl );

	EMIT_FADD(fd, fs, ft, dbl);

	return CONVERT_SUCCESS;
#endif
}

static int SUB_FP(MIPS_instr mips, int dbl){
	
#if defined(INTERPRET_FP) || defined(INTERPRET_FP_SUB)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_FP || INTERPRET_FP_SUB

	genCheckFP();

	int fs = mapFPR( MIPS_GET_FS(mips), dbl );
	int ft = mapFPR( MIPS_GET_FT(mips), dbl );
	int fd = mapFPRNew( MIPS_GET_FD(mips), dbl );

	EMIT_FSUB(fd, fs, ft, dbl);

	return CONVERT_SUCCESS;
#endif
}

static int MUL_FP(MIPS_instr mips, int dbl){
	
#if defined(INTERPRET_FP) || defined(INTERPRET_FP_MUL)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_FP || INTERPRET_FP_MUL

	genCheckFP();

	int fs = mapFPR( MIPS_GET_FS(mips), dbl );
	int ft = mapFPR( MIPS_GET_FT(mips), dbl );
	int fd = mapFPRNew( MIPS_GET_FD(mips), dbl );

	EMIT_FMUL(fd, fs, ft, dbl);

	return CONVERT_SUCCESS;
#endif
}

static int DIV_FP(MIPS_instr mips, int dbl){
	
#if defined(INTERPRET_FP) || defined(INTERPRET_FP_DIV)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_FP || INTERPRET_FP_DIV

	genCheckFP();

	int fs = mapFPR( MIPS_GET_FS(mips), dbl );
	int ft = mapFPR( MIPS_GET_FT(mips), dbl );
	int fd = mapFPRNew( MIPS_GET_FD(mips), dbl );

	EMIT_FDIV(fd, fs, ft, dbl);

	return CONVERT_SUCCESS;
#endif
}

static int SQRT_FP(MIPS_instr mips, int dbl){
	
#if defined(INTERPRET_FP) || defined(INTERPRET_FP_SQRT)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_FP || INTERPRET_FP_SQRT

	genCheckFP();

	int fr=mapFPR( MIPS_GET_FS(mips), dbl );

	EMIT_FMR(1,fr);
	
	// call sqrt
#if 1
	EMIT_B(add_jump((dbl ? (int)&sqrt : (int)&sqrtf), 1, 1), 0, 1);
#else
	EMIT_LIS(12, ((unsigned int)(dbl ? &sqrt : &sqrtf))>>16);
	EMIT_ORI(12, 12, (unsigned int)(dbl ? &sqrt : &sqrtf));
	EMIT_MTCTR(12);
	EMIT_BCTRL(ppc);
#endif
	
	fr=mapFPRNew( MIPS_GET_FD(mips), dbl ); // maps to f1 (FP return)

	EMIT_FMR(fr,1);
	
	// Load old LR
	EMIT_LWZ(0, DYNAOFF_LR, 1);
	// Restore LR
	EMIT_MTLR(0);

	return CONVERT_SUCCESS;
#endif
}

static int ABS_FP(MIPS_instr mips, int dbl){
	
#if defined(INTERPRET_FP) || defined(INTERPRET_FP_ABS)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_FP || INTERPRET_FP_ABS

	genCheckFP();

	int fs = mapFPR( MIPS_GET_FS(mips), dbl );
	int fd = mapFPRNew( MIPS_GET_FD(mips), dbl );

	EMIT_FABS(fd, fs);

	return CONVERT_SUCCESS;
#endif
}

static int MOV_FP(MIPS_instr mips, int dbl){
	
#if defined(INTERPRET_FP) || defined(INTERPRET_FP_MOV)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_FP || INTERPRET_FP_MOV

	genCheckFP();

	int fs = mapFPR( MIPS_GET_FS(mips), dbl );
	int fd = mapFPRNew( MIPS_GET_FD(mips), dbl );

	EMIT_FMR(fd, fs);

	return CONVERT_SUCCESS;
#endif
}

static int NEG_FP(MIPS_instr mips, int dbl){
	
#if defined(INTERPRET_FP) || defined(INTERPRET_FP_NEG)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_FP || INTERPRET_FP_NEG

	genCheckFP();

	int fs = mapFPR( MIPS_GET_FS(mips), dbl );
	int fd = mapFPRNew( MIPS_GET_FD(mips), dbl );

	EMIT_FNEG(fd, fs);

	return CONVERT_SUCCESS;
#endif
}

// -- Floating Point Rounding/Conversion --
#define PPC_ROUNDING_NEAREST 0
#define PPC_ROUNDING_TRUNC   1
#define PPC_ROUNDING_CEIL    2
#define PPC_ROUNDING_FLOOR   3
static void set_rounding(int rounding_mode){
	

	EMIT_MTFSFI(7, rounding_mode);
}

static void set_rounding_reg(int fs){
	

	EMIT_MTFSF(1, fs);
}

static int ROUND_L_FP(MIPS_instr mips, int dbl){
	
#if defined(INTERPRET_FP) || defined(INTERPRET_FP_ROUND_L)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_FP || INTERPRET_FP_ROUND_L
	
	genCheckFP();

	int fd = MIPS_GET_FD(mips);
	int fs = mapFPR( MIPS_GET_FS(mips), dbl );
	invalidateFPR( MIPS_GET_FS(mips) );
	
	EMIT_FMR(1,fs);

	// round
#if 1
	EMIT_B(add_jump((dbl ? (int)&round : (int)&roundf), 1, 1), 0, 1);
#else
	EMIT_LIS(12, ((unsigned int)(dbl ? &round : &roundf))>>16);
	EMIT_ORI(12, 12, (unsigned int)(dbl ? &round : &roundf));
	EMIT_MTCTR(12);
	EMIT_BCTRL(ppc);
#endif
	
	// convert
#if 1
	EMIT_B(add_jump((dbl ? (int)&__fixdfdi : (int)&__fixsfdi), 1, 1), 0, 1);
#else
	EMIT_LIS(12, ((unsigned int)(dbl ? &__fixdfdi : &__fixsfdi))>>16);
	EMIT_ORI(12, 12, (unsigned int)(dbl ? &__fixdfdi : &__fixsfdi));
	EMIT_MTCTR(12);
	EMIT_BCTRL(ppc);
#endif
	
	int addr = 5; // Use r5 for the addr (to not clobber r3/r4)
	// addr = reg_cop1_double[fd]
	EMIT_LWZ(addr, fd*4, DYNAREG_FPR_64);
	// Load old LR
	EMIT_LWZ(0, DYNAOFF_LR, 1);
	// stw r3, 0(addr)
	EMIT_STW(3, 0, addr);
	// Restore LR
	EMIT_MTLR(0);
	// stw r4, 4(addr)
	EMIT_STW(4, 4, addr);

	return CONVERT_SUCCESS;
#endif
}

static int TRUNC_L_FP(MIPS_instr mips, int dbl){
	
#if defined(INTERPRET_FP) || defined(INTERPRET_FP_TRUNC_L)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_FP || INTERPRET_FP_TRUNC_L

	genCheckFP();

	int fd = MIPS_GET_FD(mips);
	int fs = mapFPR( MIPS_GET_FS(mips), dbl );
	invalidateFPR( MIPS_GET_FS(mips) );

	EMIT_FMR(1,fs);

	// convert
#if 1
	EMIT_B(add_jump((dbl ? (int)&__fixdfdi : (int)&__fixsfdi), 1, 1), 0, 1);
#else
	EMIT_LIS(12, ((unsigned int)(dbl ? &__fixdfdi : &__fixsfdi))>>16);
	EMIT_ORI(12, 12, (unsigned int)(dbl ? &__fixdfdi : &__fixsfdi));
	EMIT_MTCTR(12);
	EMIT_BCTRL(ppc);
#endif
	
	int addr = 5; // Use r5 for the addr (to not clobber r3/r4)
	// addr = reg_cop1_double[fd]
	EMIT_LWZ(addr, fd*4, DYNAREG_FPR_64);
	// Load old LR
	EMIT_LWZ(0, DYNAOFF_LR, 1);
	// stw r3, 0(addr)
	EMIT_STW(3, 0, addr);
	// Restore LR
	EMIT_MTLR(0);
	// stw r4, 4(addr)
	EMIT_STW(4, 4, addr);

	return CONVERT_SUCCESS;
#endif
}

static int CEIL_L_FP(MIPS_instr mips, int dbl){
	
#if defined(INTERPRET_FP) || defined(INTERPRET_FP_CEIL_L)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_FP || INTERPRET_FP_CEIL_L

	genCheckFP();

	int fd = MIPS_GET_FD(mips);
	int fs = mapFPR( MIPS_GET_FS(mips), dbl );
	invalidateFPR( MIPS_GET_FS(mips) );

	EMIT_FMR(1,fs);

	// ceil
#if 1
	EMIT_B(add_jump((dbl ? (int)&ceil : (int)&ceilf), 1, 1), 0, 1);
#else
	EMIT_LIS(12, ((unsigned int)(dbl ? &ceil : &ceilf)))>>16);
	EMIT_ORI(12, 12, (unsigned int)(dbl ? &ceil : &ceilf)));
	EMIT_MTCTR(12);
	EMIT_BCTRL(ppc);
#endif
	// convert
#if 1
	EMIT_B(add_jump((dbl ? (int)&__fixdfdi : (int)&__fixsfdi), 1, 1), 0, 1);
#else
	EMIT_LIS(12, ((unsigned int)(dbl ? &__fixdfdi : &__fixsfdi)))>>16);
	EMIT_ORI(12, 12, (unsigned int)(dbl ? &__fixdfdi : &__fixsfdi)));
	EMIT_MTCTR(12);
	EMIT_BCTRL(ppc);
#endif
	
	int addr = 5; // Use r5 for the addr (to not clobber r3/r4)
	// addr = reg_cop1_double[fd]
	EMIT_LWZ(addr, fd*4, DYNAREG_FPR_64);
	// Load old LR
	EMIT_LWZ(0, DYNAOFF_LR, 1);
	// stw r3, 0(addr)
	EMIT_STW(3, 0, addr);
	// Restore LR
	EMIT_MTLR(0);
	// stw r4, 4(addr)
	EMIT_STW(4, 4, addr);

	return CONVERT_SUCCESS;
#endif
}

static int FLOOR_L_FP(MIPS_instr mips, int dbl){
	
#if defined(INTERPRET_FP) || defined(INTERPRET_FP_FLOOR_L)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_FP || INTERPRET_FP_FLOOR_L

	genCheckFP();

	int fd = MIPS_GET_FD(mips);
	int fs = mapFPR( MIPS_GET_FS(mips), dbl );
	invalidateFPR( MIPS_GET_FS(mips) );

	EMIT_FMR(1,fs);

	// round
#if 1
	EMIT_B(add_jump((dbl ? (int)&floor : (int)&floorf), 1, 1), 0, 1);
#else
	EMIT_LIS(12, ((unsigned int)(dbl ? &floor : &floorf)))>>16);
	EMIT_ORI(12, 12, (unsigned int)(dbl ? &floor : &floorf)));
	EMIT_MTCTR(12);
	EMIT_BCTRL(ppc);
#endif
	// convert
#if 1
	EMIT_B(add_jump((dbl ? (int)&__fixdfdi : (int)&__fixsfdi), 1, 1), 0, 1);
#else
	EMIT_LIS(12, ((unsigned int)(dbl ? &__fixdfdi : &__fixsfdi)))>>16);
	EMIT_ORI(12, 12, (unsigned int)(dbl ? &__fixdfdi : &__fixsfdi)));
	EMIT_MTCTR(12);
	EMIT_BCTRL(ppc);
#endif
	
	int addr = 5; // Use r5 for the addr (to not clobber r3/r4)
	// addr = reg_cop1_double[fd]
	EMIT_LWZ(addr, fd*4, DYNAREG_FPR_64);
	// Load old LR
	EMIT_LWZ(0, DYNAOFF_LR, 1);
	// stw r3, 0(addr)
	EMIT_STW(3, 0, addr);
	// Restore LR
	EMIT_MTLR(0);
	// stw r4, 4(addr)
	EMIT_STW(4, 4, addr);

	return CONVERT_SUCCESS;
#endif
}

static int ROUND_W_FP(MIPS_instr mips, int dbl){
	
#if defined(INTERPRET_FP) || defined(INTERPRET_FP_ROUND_W)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_FP || INTERPRET_FP_ROUND_W

	genCheckFP();

	set_rounding(PPC_ROUNDING_NEAREST); // TODO: Presume its already set?

	int fd = MIPS_GET_FD(mips);
	int fs = mapFPR( MIPS_GET_FS(mips), dbl );
	invalidateFPR(fd);
	int addr = mapRegisterTemp();

	// fctiw f0, fs
	EMIT_FCTIW(0, fs);
	// addr = reg_cop1_simple[fd]
	EMIT_LWZ(addr, fd*4, DYNAREG_FPR_32);
	// stfiwx f0, 0, addr
	EMIT_STFIWX(0, 0, addr);

	unmapRegisterTemp(addr);

	return CONVERT_SUCCESS;
#endif
}

static int TRUNC_W_FP(MIPS_instr mips, int dbl){
	
#if defined(INTERPRET_FP) || defined(INTERPRET_FP_TRUNC_W)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_FP || INTERPRET_FP_TRUNC_W

	genCheckFP();

	int fd = MIPS_GET_FD(mips);
	int fs = mapFPR( MIPS_GET_FS(mips), dbl );
	invalidateFPR(fd);
	int addr = mapRegisterTemp();

	// fctiwz f0, fs
	EMIT_FCTIWZ(0, fs);
	// addr = reg_cop1_simple[fd]
	EMIT_LWZ(addr, fd*4, DYNAREG_FPR_32);
	// stfiwx f0, 0, addr
	EMIT_STFIWX(0, 0, addr);

	unmapRegisterTemp(addr);

	return CONVERT_SUCCESS;
#endif
}

static int CEIL_W_FP(MIPS_instr mips, int dbl){
	
#if defined(INTERPRET_FP) || defined(INTERPRET_FP_CEIL_W)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_FP || INTERPRET_FP_CEIL_W

	genCheckFP();

	set_rounding(PPC_ROUNDING_CEIL);

	int fd = MIPS_GET_FD(mips);
	int fs = mapFPR( MIPS_GET_FS(mips), dbl );
	invalidateFPR(fd);
	int addr = mapRegisterTemp();

	// fctiw f0, fs
	EMIT_FCTIW(0, fs);
	// addr = reg_cop1_simple[fd]
	EMIT_LWZ(addr, fd*4, DYNAREG_FPR_32);
	// stfiwx f0, 0, addr
	EMIT_STFIWX(0, 0, addr);

	unmapRegisterTemp(addr);
	
	set_rounding(PPC_ROUNDING_NEAREST);

	return CONVERT_SUCCESS;
#endif
}

static int FLOOR_W_FP(MIPS_instr mips, int dbl){
	
#if defined(INTERPRET_FP) || defined(INTERPRET_FP_FLOOR_W)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_FP || INTERPRET_FP_FLOOR_W

	genCheckFP();

	set_rounding(PPC_ROUNDING_FLOOR);

	int fd = MIPS_GET_FD(mips);
	int fs = mapFPR( MIPS_GET_FS(mips), dbl );
	invalidateFPR(fd);
	int addr = mapRegisterTemp();

	// fctiw f0, fs
	EMIT_FCTIW(0, fs);
	// addr = reg_cop1_simple[fd]
	EMIT_LWZ(addr, fd*4, DYNAREG_FPR_32);
	// stfiwx f0, 0, addr
	EMIT_STFIWX(0, 0, addr);

	unmapRegisterTemp(addr);
	
	set_rounding(PPC_ROUNDING_NEAREST);

	return CONVERT_SUCCESS;
#endif
}

static int CVT_S_FP(MIPS_instr mips, int dbl){
	
#if defined(INTERPRET_FP) || defined(INTERPRET_FP_CVT_S)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_FP || INTERPRET_FP_CVT_S

	genCheckFP();

	int fs = mapFPR( MIPS_GET_FS(mips), dbl );
	int fd = mapFPRNew( MIPS_GET_FD(mips), 0 );

	EMIT_FMR(fd, fs);

	return CONVERT_SUCCESS;
#endif
}

static int CVT_D_FP(MIPS_instr mips, int dbl){
	
#if defined(INTERPRET_FP) || defined(INTERPRET_FP_CVT_D)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_FP || INTERPRET_FP_CVT_D

	genCheckFP();

	int fs = mapFPR( MIPS_GET_FS(mips), dbl );
	int fd = mapFPRNew( MIPS_GET_FD(mips), 1 );

	EMIT_FMR(fd, fs);

	return CONVERT_SUCCESS;
#endif
}

static int CVT_W_FP(MIPS_instr mips, int dbl){
	
#if defined(INTERPRET_FP) || defined(INTERPRET_FP_CVT_W)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_FP || INTERPRET_FP_CVT_W

	genCheckFP();

	// Set rounding mode according to FCR31
	EMIT_LFD(0, -4, DYNAREG_FCR31);

	// FIXME: Here I have the potential to disable IEEE mode
	//          and enable inexact exceptions
	set_rounding_reg(0);

	int fd = MIPS_GET_FD(mips);
	int fs = mapFPR( MIPS_GET_FS(mips), dbl );
	invalidateFPR(fd);
	int addr = mapRegisterTemp();

	// fctiw f0, fs
	EMIT_FCTIW(0, fs);
	// addr = reg_cop1_simple[fd]
	EMIT_LWZ(addr, fd*4, DYNAREG_FPR_32);
	// stfiwx f0, 0, addr
	EMIT_STFIWX(0, 0, addr);

	unmapRegisterTemp(addr);
	
	set_rounding(PPC_ROUNDING_NEAREST);

	return CONVERT_SUCCESS;
#endif
}

static int CVT_L_FP(MIPS_instr mips, int dbl){
	
#if defined(INTERPRET_FP) || defined(INTERPRET_FP_CVT_L)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_FP || INTERPRET_FP_CVT_L

	genCheckFP();

	int fd = MIPS_GET_FD(mips);
	int fs = mapFPR( MIPS_GET_FS(mips), dbl );
	invalidateFPR( MIPS_GET_FS(mips) );

	EMIT_FMR(1,fs);

	// FIXME: I'm fairly certain this will always trunc
	// convert
#if 1
	EMIT_B(add_jump((dbl ? (int)&__fixdfdi : (int)&__fixsfdi), 1, 1), 0, 1);
#else
	EMIT_LIS(12, ((unsigned int)(dbl ? &__fixdfdi : &__fixsfdi))>>16);
	EMIT_ORI(12, 12, (unsigned int)(dbl ? &__fixdfdi : &__fixsfdi));
	EMIT_MTCTR(12);
	EMIT_BCTRL(ppc);
#endif
	
	int addr = 5; // Use r5 for the addr (to not clobber r3/r4)
	// addr = reg_cop1_double[fd]
	EMIT_LWZ(addr, fd*4, DYNAREG_FPR_64);
	// Load old LR
	EMIT_LWZ(0, DYNAOFF_LR, 1);
	// stw r3, 0(addr)
	EMIT_STW(3, 0, addr);
	// Restore LR
	EMIT_MTLR(0);
	// stw r4, 4(addr)
	EMIT_STW(4, 4, addr);

	return CONVERT_SUCCESS;
#endif
}

// -- Floating Point Comparisons --
static int C_F_FP(MIPS_instr mips, int dbl){
	
#if defined(INTERPRET_FP) || defined(INTERPRET_FP_C_F)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_FP || INTERPRET_FP_C_F

	genCheckFP();

	// lwz r0, 0(&fcr31)
	EMIT_LWZ(0, 0, DYNAREG_FCR31);
	// and r0, r0, 0xff7fffff (clear cond)
	EMIT_RLWINM(0, 0, 0, 9, 7);
	// stw r0, 0(&fcr31)
	EMIT_STW(0, 0, DYNAREG_FCR31);

	return CONVERT_SUCCESS;
#endif
}

static int C_UN_FP(MIPS_instr mips, int dbl){
	
#if defined(INTERPRET_FP) || defined(INTERPRET_FP_C_UN)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_FP || INTERPRET_FP_C_UN

	genCheckFP();

	int fs = mapFPR( MIPS_GET_FS(mips), dbl );
	int ft = mapFPR( MIPS_GET_FT(mips), dbl );

	// lwz r0, 0(&fcr31)
	EMIT_LWZ(0, 0, DYNAREG_FCR31);
	// fcmpu cr0, fs, ft
	EMIT_FCMPU(fs, ft, 0);
	// and r0, r0, 0xff7fffff (clear cond)
	EMIT_RLWINM(0, 0, 0, 9, 7);
	// bord cr0, 2 (past setting cond)
	EMIT_BC(2, 0, 0, 0x4, 3);
	// oris r0, r0, 0x0080 (set cond)
	EMIT_ORIS(0, 0, 0x0080);
	// stw r0, 0(&fcr31)
	EMIT_STW(0, 0, DYNAREG_FCR31);

	return CONVERT_SUCCESS;
#endif
}

static int C_EQ_FP(MIPS_instr mips, int dbl){
	
#if defined(INTERPRET_FP) || defined(INTERPRET_FP_C_EQ)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_FP || INTERPRET_FP_C_EQ

	genCheckFP();

	int fs = mapFPR( MIPS_GET_FS(mips), dbl );
	int ft = mapFPR( MIPS_GET_FT(mips), dbl );

	// lwz r0, 0(&fcr31)
	EMIT_LWZ(0, 0, DYNAREG_FCR31);
	// fcmpu cr0, fs, ft
	EMIT_FCMPU(fs, ft, 0);
	// and r0, r0, 0xff7fffff (clear cond)
	EMIT_RLWINM(0, 0, 0, 9, 7);
	// bne cr0, 2 (past setting cond)
	EMIT_BNE(0, 2, 0, 0);
	// oris r0, r0, 0x0080 (set cond)
	EMIT_ORIS(0, 0, 0x0080);
	// stw r0, 0(&fcr31)
	EMIT_STW(0, 0, DYNAREG_FCR31);

	return CONVERT_SUCCESS;
#endif
}

static int C_UEQ_FP(MIPS_instr mips, int dbl){
	
#if defined(INTERPRET_FP) || defined(INTERPRET_FP_C_UEQ)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_FP || INTERPRET_FP_C_UEQ

	genCheckFP();

	int fs = mapFPR( MIPS_GET_FS(mips), dbl );
	int ft = mapFPR( MIPS_GET_FT(mips), dbl );

	// lwz r0, 0(&fcr31)
	EMIT_LWZ(0, 0, DYNAREG_FCR31);
	// fcmpu cr0, fs, ft
	EMIT_FCMPU(fs, ft, 0);
	// and r0, r0, 0xff7fffff (clear cond)
	EMIT_RLWINM(0, 0, 0, 9, 7);
	// cror cr0[eq], cr0[eq], cr0[un]
	EMIT_CROR(2, 2, 3);
	// bne cr0, 2 (past setting cond)
	EMIT_BNE(0, 2, 0, 0);
	// oris r0, r0, 0x0080 (set cond)
	EMIT_ORIS(0, 0, 0x0080);
	// stw r0, 0(&fcr31)
	EMIT_STW(0, 0, DYNAREG_FCR31);

	return CONVERT_SUCCESS;
#endif
}

static int C_OLT_FP(MIPS_instr mips, int dbl){
	
#if defined(INTERPRET_FP) || defined(INTERPRET_FP_C_OLT)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_FP || INTERPRET_FP_C_OLT

	genCheckFP();

	int fs = mapFPR( MIPS_GET_FS(mips), dbl );
	int ft = mapFPR( MIPS_GET_FT(mips), dbl );

	// lwz r0, 0(&fcr31)
	EMIT_LWZ(0, 0, DYNAREG_FCR31);
	// fcmpu cr0, fs, ft
	EMIT_FCMPU(fs, ft, 0);
	// and r0, r0, 0xff7fffff (clear cond)
	EMIT_RLWINM(0, 0, 0, 9, 7);
	// bge cr0, 2 (past setting cond)
	EMIT_BGE(0, 2, 0, 0);
	// oris r0, r0, 0x0080 (set cond)
	EMIT_ORIS(0, 0, 0x0080);
	// stw r0, 0(&fcr31)
	EMIT_STW(0, 0, DYNAREG_FCR31);

	return CONVERT_SUCCESS;
#endif
}

static int C_ULT_FP(MIPS_instr mips, int dbl){
	
#if defined(INTERPRET_FP) || defined(INTERPRET_FP_C_ULT)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_FP || INTERPRET_FP_C_ULT

	genCheckFP();

	int fs = mapFPR( MIPS_GET_FS(mips), dbl );
	int ft = mapFPR( MIPS_GET_FT(mips), dbl );

	// lwz r0, 0(&fcr31)
	EMIT_LWZ(0, 0, DYNAREG_FCR31);
	// fcmpu cr0, fs, ft
	EMIT_FCMPU(fs, ft, 0);
	// and r0, r0, 0xff7fffff (clear cond)
	EMIT_RLWINM(0, 0, 0, 9, 7);
	// cror cr0[lt], cr0[lt], cr0[un]
	EMIT_CROR(0, 0, 3);
	// bge cr0, 2 (past setting cond)
	EMIT_BGE(0, 2, 0, 0);
	// oris r0, r0, 0x0080 (set cond)
	EMIT_ORIS(0, 0, 0x0080);
	// stw r0, 0(&fcr31)
	EMIT_STW(0, 0, DYNAREG_FCR31);

	return CONVERT_SUCCESS;
#endif
}

static int C_OLE_FP(MIPS_instr mips, int dbl){
	
#if defined(INTERPRET_FP) || defined(INTERPRET_FP_C_OLE)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_FP || INTERPRET_FP_C_OLE

	genCheckFP();

	int fs = mapFPR( MIPS_GET_FS(mips), dbl );
	int ft = mapFPR( MIPS_GET_FT(mips), dbl );

	// lwz r0, 0(&fcr31)
	EMIT_LWZ(0, 0, DYNAREG_FCR31);
	// fcmpu cr0, fs, ft
	EMIT_FCMPU(fs, ft, 0);
	// and r0, r0, 0xff7fffff (clear cond)
	EMIT_RLWINM(0, 0, 0, 9, 7);
	// cror cr0[gt], cr0[gt], cr0[un]
	EMIT_CROR(1, 1, 3);
	// bgt cr0, 2 (past setting cond)
	EMIT_BGT(0, 2, 0, 0);
	// oris r0, r0, 0x0080 (set cond)
	EMIT_ORIS(0, 0, 0x0080);
	// stw r0, 0(&fcr31)
	EMIT_STW(0, 0, DYNAREG_FCR31);

	return CONVERT_SUCCESS;
#endif
}

static int C_ULE_FP(MIPS_instr mips, int dbl){
	
#if defined(INTERPRET_FP) || defined(INTERPRET_FP_C_ULE)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_FP || INTERPRET_FP_C_ULE

	genCheckFP();

	int fs = mapFPR( MIPS_GET_FS(mips), dbl );
	int ft = mapFPR( MIPS_GET_FT(mips), dbl );

	// lwz r0, 0(&fcr31)
	EMIT_LWZ(0, 0, DYNAREG_FCR31);
	// fcmpu cr0, fs, ft
	EMIT_FCMPU(fs, ft, 0);
	// and r0, r0, 0xff7fffff (clear cond)
	EMIT_RLWINM(0, 0, 0, 9, 7);
	// bgt cr0, 2 (past setting cond)
	EMIT_BGT(0, 2, 0, 0);
	// oris r0, r0, 0x0080 (set cond)
	EMIT_ORIS(0, 0, 0x0080);
	// stw r0, 0(&fcr31)
	EMIT_STW(0, 0, DYNAREG_FCR31);

	return CONVERT_SUCCESS;
#endif
}

static int C_SF_FP(MIPS_instr mips, int dbl){
	
#if defined(INTERPRET_FP) || defined(INTERPRET_FP_C_SF)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_FP || INTERPRET_FP_C_SF

	genCheckFP();

	// lwz r0, 0(&fcr31)
	EMIT_LWZ(0, 0, DYNAREG_FCR31);
	// and r0, r0, 0xff7fffff (clear cond)
	EMIT_RLWINM(0, 0, 0, 9, 7);
	// stw r0, 0(&fcr31)
	EMIT_STW(0, 0, DYNAREG_FCR31);

	return CONVERT_SUCCESS;
#endif
}

static int C_NGLE_FP(MIPS_instr mips, int dbl){
	
#if defined(INTERPRET_FP) || defined(INTERPRET_FP_C_NGLE)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_FP || INTERPRET_FP_C_NGLE

	genCheckFP();

	// lwz r0, 0(&fcr31)
	EMIT_LWZ(0, 0, DYNAREG_FCR31);
	// and r0, r0, 0xff7fffff (clear cond)
	EMIT_RLWINM(0, 0, 0, 9, 7);
	// stw r0, 0(&fcr31)
	EMIT_STW(0, 0, DYNAREG_FCR31);

	return CONVERT_SUCCESS;
#endif
}

static int C_SEQ_FP(MIPS_instr mips, int dbl){
	
#if defined(INTERPRET_FP) || defined(INTERPRET_FP_C_SEQ)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_FP || INTERPRET_FP_C_SEQ

	genCheckFP();

	int fs = mapFPR( MIPS_GET_FS(mips), dbl );
	int ft = mapFPR( MIPS_GET_FT(mips), dbl );

	// lwz r0, 0(&fcr31)
	EMIT_LWZ(0, 0, DYNAREG_FCR31);
	// fcmpu cr0, fs, ft
	EMIT_FCMPU(fs, ft, 0);
	// and r0, r0, 0xff7fffff (clear cond)
	EMIT_RLWINM(0, 0, 0, 9, 7);
	// bne cr0, 2 (past setting cond)
	EMIT_BNE(0, 2, 0, 0);
	// oris r0, r0, 0x0080 (set cond)
	EMIT_ORIS(0, 0, 0x0080);
	// stw r0, 0(&fcr31)
	EMIT_STW(0, 0, DYNAREG_FCR31);

	return CONVERT_SUCCESS;
#endif
}

static int C_NGL_FP(MIPS_instr mips, int dbl){
	
#if defined(INTERPRET_FP) || defined(INTERPRET_FP_C_NGL)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_FP || INTERPRET_FP_C_NGL

	genCheckFP();

	int fs = mapFPR( MIPS_GET_FS(mips), dbl );
	int ft = mapFPR( MIPS_GET_FT(mips), dbl );

	// lwz r0, 0(&fcr31)
	EMIT_LWZ(0, 0, DYNAREG_FCR31);
	// fcmpu cr0, fs, ft
	EMIT_FCMPU(fs, ft, 0);
	// and r0, r0, 0xff7fffff (clear cond)
	EMIT_RLWINM(0, 0, 0, 9, 7);
	// bne cr0, 2 (past setting cond)
	EMIT_BNE(0, 2, 0, 0);
	// oris r0, r0, 0x0080 (set cond)
	EMIT_ORIS(0, 0, 0x0080);
	// stw r0, 0(&fcr31)
	EMIT_STW(0, 0, DYNAREG_FCR31);

	return CONVERT_SUCCESS;
#endif
}

static int C_LT_FP(MIPS_instr mips, int dbl){
	
#if defined(INTERPRET_FP) || defined(INTERPRET_FP_C_LT)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_FP || INTERPRET_FP_C_LT

	genCheckFP();

	int fs = mapFPR( MIPS_GET_FS(mips), dbl );
	int ft = mapFPR( MIPS_GET_FT(mips), dbl );

	// lwz r0, 0(&fcr31)
	EMIT_LWZ(0, 0, DYNAREG_FCR31);
	// fcmpu cr0, fs, ft
	EMIT_FCMPU(fs, ft, 0);
	// and r0, r0, 0xff7fffff (clear cond)
	EMIT_RLWINM(0, 0, 0, 9, 7);
	// bge cr0, 2 (past setting cond)
	EMIT_BGE(0, 2, 0, 0);
	// oris r0, r0, 0x0080 (set cond)
	EMIT_ORIS(0, 0, 0x0080);
	// stw r0, 0(&fcr31)
	EMIT_STW(0, 0, DYNAREG_FCR31);

	return CONVERT_SUCCESS;
#endif
}

static int C_NGE_FP(MIPS_instr mips, int dbl){
	
#if defined(INTERPRET_FP) || defined(INTERPRET_FP_C_NGE)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_FP || INTERPRET_FP_C_NGE

	genCheckFP();

	int fs = mapFPR( MIPS_GET_FS(mips), dbl );
	int ft = mapFPR( MIPS_GET_FT(mips), dbl );

	// lwz r0, 0(&fcr31)
	EMIT_LWZ(0, 0, DYNAREG_FCR31);
	// fcmpu cr0, fs, ft
	EMIT_FCMPU(fs, ft, 0);
	// and r0, r0, 0xff7fffff (clear cond)
	EMIT_RLWINM(0, 0, 0, 9, 7);
	// bge cr0, 2 (past setting cond)
	EMIT_BGE(0, 2, 0, 0);
	// oris r0, r0, 0x0080 (set cond)
	EMIT_ORIS(0, 0, 0x0080);
	// stw r0, 0(&fcr31)
	EMIT_STW(0, 0, DYNAREG_FCR31);

	return CONVERT_SUCCESS;
#endif
}

static int C_LE_FP(MIPS_instr mips, int dbl){
	
#if defined(INTERPRET_FP) || defined(INTERPRET_FP_C_LE)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_FP || INTERPRET_FP_C_LE

	genCheckFP();

	int fs = mapFPR( MIPS_GET_FS(mips), dbl );
	int ft = mapFPR( MIPS_GET_FT(mips), dbl );

	// lwz r0, 0(&fcr31)
	EMIT_LWZ(0, 0, DYNAREG_FCR31);
	// fcmpu cr0, fs, ft
	EMIT_FCMPU(fs, ft, 0);
	// and r0, r0, 0xff7fffff (clear cond)
	EMIT_RLWINM(0, 0, 0, 9, 7);
	// bgt cr0, 2 (past setting cond)
	EMIT_BGT(0, 2, 0, 0);
	// oris r0, r0, 0x0080 (set cond)
	EMIT_ORIS(0, 0, 0x0080);
	// stw r0, 0(&fcr31)
	EMIT_STW(0, 0, DYNAREG_FCR31);

	return CONVERT_SUCCESS;
#endif
}

static int C_NGT_FP(MIPS_instr mips, int dbl){
	
#if defined(INTERPRET_FP) || defined(INTERPRET_FP_C_NGT)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_FP || INTERPRET_FP_C_NGT

	genCheckFP();

	int fs = mapFPR( MIPS_GET_FS(mips), dbl );
	int ft = mapFPR( MIPS_GET_FT(mips), dbl );

	// lwz r0, 0(&fcr31)
	EMIT_LWZ(0, 0, DYNAREG_FCR31);
	// fcmpu cr0, fs, ft
	EMIT_FCMPU(fs, ft, 0);
	// and r0, r0, 0xff7fffff (clear cond)
	EMIT_RLWINM(0, 0, 0, 9, 7);
	// bgt cr0, 2 (past setting cond)
	EMIT_BGT(0, 2, 0, 0);
	// oris r0, r0, 0x0080 (set cond)
	EMIT_ORIS(0, 0, 0x0080);
	// stw r0, 0(&fcr31)
	EMIT_STW(0, 0, DYNAREG_FCR31);

	return CONVERT_SUCCESS;
#endif
}

static int (*gen_cop1_fp[64])(MIPS_instr, int) =
{
   ADD_FP    ,SUB_FP    ,MUL_FP   ,DIV_FP    ,SQRT_FP   ,ABS_FP    ,MOV_FP   ,NEG_FP    ,
   ROUND_L_FP,TRUNC_L_FP,CEIL_L_FP,FLOOR_L_FP,ROUND_W_FP,TRUNC_W_FP,CEIL_W_FP,FLOOR_W_FP,
   NI        ,NI        ,NI       ,NI        ,NI        ,NI        ,NI       ,NI        ,
   NI        ,NI        ,NI       ,NI        ,NI        ,NI        ,NI       ,NI        ,
   CVT_S_FP  ,CVT_D_FP  ,NI       ,NI        ,CVT_W_FP  ,CVT_L_FP  ,NI       ,NI        ,
   NI        ,NI        ,NI       ,NI        ,NI        ,NI        ,NI       ,NI        ,
   C_F_FP    ,C_UN_FP   ,C_EQ_FP  ,C_UEQ_FP  ,C_OLT_FP  ,C_ULT_FP  ,C_OLE_FP ,C_ULE_FP  ,
   C_SF_FP   ,C_NGLE_FP ,C_SEQ_FP ,C_NGL_FP  ,C_LT_FP   ,C_NGE_FP  ,C_LE_FP  ,C_NGT_FP
};

static int S(MIPS_instr mips){
#if defined(INTERPRET_FP) || defined(INTERPRET_FP_S)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_FP || INTERPRET_FP_S
	return gen_cop1_fp[ MIPS_GET_FUNC(mips) ](mips, 0);
#endif
}

static int D(MIPS_instr mips){
#if defined(INTERPRET_FP) || defined(INTERPRET_FP_D)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_FP || INTERPRET_FP_D
	return gen_cop1_fp[ MIPS_GET_FUNC(mips) ](mips, 1);
#endif
}

static int CVT_FP_W(MIPS_instr mips, int dbl){
	

	genCheckFP();

	int fs = MIPS_GET_FS(mips);
	flushFPR(fs);
	int fd = mapFPRNew( MIPS_GET_FD(mips), dbl );
	int tmp = mapRegisterTemp();

	// Get the integer value into a GPR
	// tmp = fpr32[fs]
	EMIT_LWZ(tmp, fs*4, DYNAREG_FPR_32);
	// tmp = *tmp (src)
	EMIT_LWZ(tmp, 0, tmp);

	// lis r0, 0x4330
	EMIT_LIS(0, 0x4330);
	// stw r0, -8(r1)
	EMIT_STW(0, -8, 1);
	// lis r0, 0x8000
	EMIT_LIS(0, 0x8000);
	// stw r0, -4(r1)
	EMIT_STW(0, -4, 1);
	// xor r0, src, 0x80000000
	EMIT_XOR(0, tmp, 0);
	// lfd f0, -8(r1)
	EMIT_LFD(0, -8, 1);
	// stw r0 -4(r1)
	EMIT_STW(0, -4, 1);
	// lfd fd, -8(r1)
	EMIT_LFD(fd, -8, 1);
	// fsub fd, fd, f0
	EMIT_FSUB(fd, fd, 0, dbl);

	unmapRegisterTemp(tmp);

	return CONVERT_SUCCESS;
}

static int W(MIPS_instr mips){
#if defined(INTERPRET_FP) || defined(INTERPRET_FP_W)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_FP || INTERPRET_FP_W

	int func = MIPS_GET_FUNC(mips);

	if(func == MIPS_FUNC_CVT_S_) return CVT_FP_W(mips, 0);
	if(func == MIPS_FUNC_CVT_D_) return CVT_FP_W(mips, 1);
	else return CONVERT_ERROR;
#endif
}

static int CVT_FP_L(MIPS_instr mips, int dbl){
	
	
	genCheckFP();

	int fs = MIPS_GET_FS(mips);
	int fd = mapFPRNew( MIPS_GET_FD(mips), dbl ); // f1
	int hi = mapRegisterTemp(); // r3
	int lo = mapRegisterTemp(); // r4

	// Get the long value into GPRs
	// lo = fpr64[fs]
	EMIT_LWZ(lo, fs*4, DYNAREG_FPR_64);
	// hi = *lo (hi word)
	EMIT_LWZ(hi, 0, lo);
	// lo = *(lo+4) (lo word)
	EMIT_LWZ(lo, 4, lo);
	
	EMIT_OR(3,hi,hi);
	EMIT_OR(4,lo,lo);

	// convert
#if 1
	EMIT_B(add_jump((dbl ? (int)&__floatdidf : (int)&__floatdisf), 1, 1), 0, 1);
#else
	EMIT_LIS(12, ((unsigned int)(dbl ? &__floatdidf : &__floatdisf))>>16);
	EMIT_ORI(12, 12, (unsigned int)(dbl ? &__floatdidf : &__floatdisf));
	EMIT_MTCTR(12);
	EMIT_BCTRL(ppc);
#endif
	
	EMIT_FMR(fd,1);
	
	// Load old LR
	EMIT_LWZ(0, DYNAOFF_LR, 1);
	// Restore LR
	EMIT_MTLR(0);
	
	unmapRegisterTemp(hi);
	unmapRegisterTemp(lo);

	return CONVERT_SUCCESS;
}

static int L(MIPS_instr mips){
#if defined(INTERPRET_FP) || defined(INTERPRET_FP_L)
	genCallInterp(mips);
	return INTERPRETED;
#else // INTERPRET_FP || INTERPRET_FP_L

	int func = MIPS_GET_FUNC(mips);

	if(func == MIPS_FUNC_CVT_S_) return CVT_FP_L(mips, 0);
	if(func == MIPS_FUNC_CVT_D_) return CVT_FP_L(mips, 1);
	else return CONVERT_ERROR;
#endif
}

static int (*gen_cop1[32])(MIPS_instr) =
{
   MFC1, DMFC1, CFC1, NI, MTC1, DMTC1, CTC1, NI,
   BC  , NI   , NI  , NI, NI  , NI   , NI  , NI,
   S   , D    , NI  , NI, W   , L    , NI  , NI,
   NI  , NI   , NI  , NI, NI  , NI   , NI  , NI
};

static int COP1(MIPS_instr mips){
	return gen_cop1[MIPS_GET_RS(mips)](mips);
}

static int (*gen_ops[64])(MIPS_instr) =
{
   SPECIAL, REGIMM, J   , JAL  , BEQ , BNE , BLEZ , BGTZ ,
   ADDI   , ADDIU , SLTI, SLTIU, ANDI, ORI , XORI , LUI  ,
   COP0   , COP1  , NI  , NI   , BEQL, BNEL, BLEZL, BGTZL,
   DADDI  , DADDIU, LDL , LDR  , NI  , NI  , NI   , NI   ,
   LB     , LH    , LWL , LW   , LBU , LHU , LWR  , LWU  ,
   SB     , SH    , SWL , SW   , SDL , SDR , SWR  , CACHE,
   LL     , LWC1  , NI  , NI   , NI  , LDC1, NI   , LD   ,
   SC     , SWC1  , NI  , NI   , NI  , SDC1, NI   , SD
};



static void genCallInterp(MIPS_instr mips){
	flushRegisters();
	reset_code_addr();
	// Pass in whether this instruction is in the delay slot
	EMIT_LI(5, isDelaySlot ? 1 : 0);
	// Move the address of decodeNInterpret to ctr for a bctr
	//EMIT_MTCTR(DYNAREG_INTERP);
	// Load our argument into r3 (mips)
	EMIT_LIS(3, mips>>16);
	// Load the current PC as the second arg
	EMIT_LIS(4, get_src_pc()>>16);
	// Load the lower halves of mips and PC
	EMIT_ORI(3, 3, mips);
	EMIT_ORI(4, 4, get_src_pc());
	// Branch to decodeNInterpret
#if 1
	EMIT_B(add_jump((int)(&decodeNInterpret), 1, 1), 0, 1);
#else
	EMIT_LIS(12, ((unsigned int)&decodeNInterpret)>>16);
	EMIT_ORI(12, 12, (unsigned int)&decodeNInterpret);
	EMIT_MTCTR(12);
	EMIT_BCTRL(ppc);
#endif
	// Load the old LR
	EMIT_LWZ(0, DYNAOFF_LR, 1);
	// Check if the PC changed
	EMIT_CMPI(3, 0, 6);
	// Restore the LR
	EMIT_MTLR(0);
	// if decodeNInterpret returned an address
	//   jumpTo it
	EMIT_BNELR(6, 0);

	if(mips_is_jump(mips)) delaySlotNext = 2;
}

static void genJumpTo(unsigned int loc, unsigned int type){
	if(type == JUMPTO_REG){
		// Load the register as the return value
		EMIT_LWZ(3, loc*8+4, DYNAREG_REG);
	} else {
		// Calculate the destination address
		loc <<= 2;
		if(type == JUMPTO_OFF) loc += get_src_pc();
		else loc |= get_src_pc() & 0xf0000000;
#if 0
		// Create space to load destination func*
		EMIT_ORI(0, 0, 0);
		EMIT_ORI(0, 0, 0);
		// Move func* into r3 as argument
		EMIT_ADDI(3, DYNAREG_FUNC, 0);
		// Call RecompCache_Update(func)
		EMIT_B(add_jump((int)(RecompCache_Update), 1, 1), 0, 1);
		// Restore LR
		EMIT_LWZ(0, DYNAOFF_LR, 1);
		EMIT_MTLR(0);
#else
		EMIT_LIS(12,((unsigned int)&recomp_cache_nextLRU)>>16);
		EMIT_LWZ(3,(unsigned int)&recomp_cache_nextLRU,12);

		// Create space to load destination func*
		EMIT_ORI(0, 0, 0);
		EMIT_ORI(0, 0, 0);

		// that block of code must be 4 ops
		EMIT_ORI(0, 0, 0);
		EMIT_STW(3,offsetof(PowerPC_func,lru),DYNAREG_FUNC);
		EMIT_ADDI(3,3,1);
		EMIT_STW(3,(unsigned int)&recomp_cache_nextLRU,12);
#endif
		
		// Load the address as the return value
		EMIT_LIS(3, loc >> 16);
		EMIT_ORI(3, 3, loc);
		// Since we could be linking, return on interrupt
		EMIT_BLELR(2, 0);
		// Store last_addr for linking
		EMIT_STW(3, 0, DYNAREG_LADDR);
	}

	EMIT_BLR((type != JUMPTO_REG));
}

// Updates Count, and sets cr2 to (next_interupt ? Count)
static void genUpdateCount(int checkCount){
#ifndef COMPARE_CORE
	// Dynarec inlined code equivalent:
	int tmp = mapRegisterTemp();
	// lis    tmp, pc >> 16
	EMIT_LIS(tmp, (get_src_pc()+4)>>16);
	// lwz    r0,  0(&last_addr)     // r0 = last_addr
	EMIT_LWZ(0, 0, DYNAREG_LADDR);
	// ori    tmp, tmp, pc & 0xffff  // tmp = pc
	EMIT_ORI(tmp, tmp, get_src_pc()+4);
	// stw    tmp, 0(&last_addr)     // last_addr = pc
	EMIT_STW(tmp, 0, DYNAREG_LADDR);
	// subf   r0,  r0, tmp           // r0 = pc - last_addr
	EMIT_SUBF(0, 0, tmp);
	// lwz    tmp, 9*4(reg_cop0)     // tmp = Count
	EMIT_LWZ(tmp, 9*4, DYNAREG_COP0);
	// srwi r0, r0, 1                // r0 = (pc - last_addr)/2
	EMIT_SRWI(0, 0, 1);
	// add    r0,  r0, tmp           // r0 += Count
	EMIT_ADD(0, 0, tmp);
	if(checkCount){
		// lwz    tmp, 0(&next_interupt) // tmp = next_interupt
		EMIT_LWZ(tmp, 0, DYNAREG_NINTR);
	}
	// stw    r0,  9*4(reg_cop0)    // Count = r0
	EMIT_STW(0, 9*4, DYNAREG_COP0);
	if(checkCount){
		// cmpl   cr2,  tmp, r0  // cr2 = next_interupt ? Count
		EMIT_CMPL(tmp, 0, 2);
	}
	// Free tmp register
	unmapRegisterTemp(tmp);
#else
	// Load the current PC as the argument
	EMIT_LIS(3, (get_src_pc()+4)>>16);
	EMIT_ORI(3, 3, get_src_pc()+4);
	// Call dyna_update_count
#if 1
	EMIT_B(add_jump((int)(&dyna_update_count, 1, 1), 0, 1);
#else
	EMIT_LIS(12, ((unsigned int)&dyna_update_count)>>16);
	EMIT_ORI(12, 12, (unsigned int)&dyna_update_count);
	EMIT_MTCTR(12);
	EMIT_BCTRL(ppc);
#endif
	// Load the lr
	EMIT_LWZ(0, DYNAOFF_LR, 1);
	EMIT_MTLR(0);
	if(checkCount){
		// If next_interupt <= Count (cr2)
		EMIT_CMPI(3, 0, 2);
	}
#endif
}

// Check whether we need to take a FP unavailable exception
static void genCheckFP(void){
	
	if(FP_need_check || isDelaySlot){
		flushRegisters();
		reset_code_addr();
		// lwz r0, 12*4(reg_cop0)
		EMIT_LWZ(0, 12*4, DYNAREG_COP0);
		// andis. r0, r0, 0x2000
		EMIT_ANDIS(0, 0, 0x2000);
		// bne cr0, end
		EMIT_BNE(0, 8, 0, 0);
		// Load the current PC as arg 1 (upper half)
		EMIT_LIS(3, get_src_pc()>>16);
		// Pass in whether this instruction is in the delay slot as arg 2
		EMIT_LI(4, isDelaySlot ? 1 : 0);
		// Current PC (lower half)
		EMIT_ORI(3, 3, get_src_pc());
		// Call dyna_check_cop1_unusable
#if 1
		EMIT_B(add_jump((int)(&dyna_check_cop1_unusable), 1, 1), 0, 1);
#else
		EMIT_LIS(12, ((unsigned int)&dyna_check_cop1_unusable)>>16);
		EMIT_ORI(12, 12, (unsigned int)&dyna_check_cop1_unusable);
		EMIT_MTCTR(12);
		EMIT_BCTRL(ppc);
#endif
		// Load the old LR
		EMIT_LWZ(0, DYNAOFF_LR, 1);
		// Restore the LR
		EMIT_MTLR(0);
		// Return to trampoline
		EMIT_BLR(0);
		// Don't check for the rest of this mapping
		// Unless this instruction is in a delay slot
		FP_need_check = isDelaySlot;
	}
}

void genCallDynaMem(memType type, int base, short immed){
	
	// PRE: value to store, or register # to load into should be in r3
	// Pass PC as arg 4 (upper half)
	EMIT_LIS(6, (get_src_pc()+4)>>16);
	// addr = base + immed (arg 2)
	EMIT_ADDI(4, base, immed);
	// type passed as arg 3
	EMIT_LI(5, type);
	// Lower half of PC
	EMIT_ORI(6, 6, get_src_pc()+4);
	// isDelaySlot as arg 5
	EMIT_LI(7, isDelaySlot ? 1 : 0);
	// call dyna_mem
#if 1
	EMIT_B(add_jump((int)(&dyna_mem), 1, 1), 0, 1);
#else
	EMIT_LIS(12, ((unsigned int)&dyna_mem)>>16);
	EMIT_ORI(12, 12, (unsigned int)&dyna_mem);
	EMIT_MTCTR(12);
	EMIT_BCTRL(ppc);
#endif
	// Load old LR
	EMIT_LWZ(0, DYNAOFF_LR, 1);
	// Check whether we need to take an interrupt
	EMIT_CMPI(3, 0, 6);
	// Restore LR
	EMIT_MTLR(0);
	// If so, return to trampoline
	EMIT_BNELR(6, 0);
}

extern char __attribute__((aligned(65536))) invalid_code[0x100000];

#define CHECK_INVALID_CODE()                                                   \
    EMIT_ADDI(3, base, immed);                                                 \
    /* test invalid code */                                                    \
    EMIT_LIS(12, HA((unsigned int)&invalid_code));                             \
    EMIT_RLWINM(5, 3, 20, 12, 31);                                             \
    EMIT_LBZX(12, 12, 5);													   \
    EMIT_CMPI(12,0,6);                                                         \
    EMIT_BNE(6,4,0,0);                                                         \
    /* invalidate code if needed */                                            \
    EMIT_B(add_jump((int)(&invalidate_func), 1, 1),0,1);                       \
    /* restore LR */                                                           \
    EMIT_LWZ(0, DYNAOFF_LR, 1);                                                \
    EMIT_MTLR(0);

static int genCallDynaMemVM(int rs_reg, int rt_reg, memType type, int immed){

	if(failsafeRec&FAILSAFE_REC_NO_VM)
	{
		flushRegisters();
	}
	
	PowerPC_instr* preCall=NULL;
	int not_fastmem_id=0;
	
	if(type==MEM_LWC1 || type==MEM_LDC1 || type==MEM_SWC1 || type==MEM_SDC1)
	{
		genCheckFP();
		
		if(type==MEM_SWC1 || type==MEM_SDC1)
		{
			flushFPR(rt_reg);
		}
		else
		{
			invalidateFPR(rt_reg);
		}
	}

	int base = mapRegister( rs_reg );

	if(!(failsafeRec&FAILSAFE_REC_NO_VM))
	{
		int rd = 5;
		int addr = 6;

		EMIT_RLWINM(rd,base,0,2,31);
		EMIT_ORIS(rd,rd,0x4000); // virtual mapping base address

		// Perform the actual load
		switch (type)
		{
			case MEM_LB:
			{
				int r = mapRegisterNew( rt_reg );
				EMIT_LBZ(r, immed, rd);
				EMIT_EXTSB(r,r);
				break;
			}
			case MEM_LBU:
			{
				int r = mapRegisterNew( rt_reg );
				EMIT_LBZ(r, immed, rd);
				break;
			}
			case MEM_LH:
			{
				int r = mapRegisterNew( rt_reg );
				EMIT_LHA(r, immed, rd);
				break;
			}
			case MEM_LHU:
			{
				int r = mapRegisterNew( rt_reg );
				EMIT_LHZ(r, immed, rd)
				break;
			}
			case MEM_LW:
			{
				int r = mapRegisterNew( rt_reg );
				EMIT_LWZ(r, immed, rd);
				break;
			}
			case MEM_LWU:
			{
				// Create a mapping for this value
				RegMapping r = mapRegister64New( rt_reg );
				// Perform the actual load
				EMIT_LWZ(r.lo, immed, rd);
				// Zero out the upper word
				EMIT_LI(r.hi, 0);
				break;
			}
			case MEM_LD:
			{
				// Create a mapping for this value
				RegMapping r = mapRegister64New( rt_reg );
				// Perform the actual load
				EMIT_LWZ(r.hi, immed, rd);
				EMIT_LWZ(r.lo, immed+4, rd);
				break;
			}
			case MEM_LWC1:
			{
				int r = 3;
				EMIT_LWZ(r, immed, rd);
				EMIT_LWZ(addr, rt_reg*4, DYNAREG_FPR_32);
				EMIT_STW(r, 0, addr);
				break;
			}
			case MEM_LDC1:
			{
				int r = 3;
				int r2 = 4;
				EMIT_LWZ(r, immed, rd);
				EMIT_LWZ(r2, immed+4, rd);
				EMIT_LWZ(addr, rt_reg*4, DYNAREG_FPR_64);
				EMIT_STW(r, 0, addr);
				EMIT_STW(r2, 4, addr);
				break;
			}
			case MEM_LWL:
			{
				EMIT_ADDI(rd, rd, immed);
				EMIT_RLWINM(0, rd, 0, 30, 31);	// r0 = addr & 3
				EMIT_CMPI(0, 0, 1);
				EMIT_BNE(1, 3, 0, 0); // /!\ branch to just after 'Skip over else'

				int r = mapRegisterNew( rt_reg );
				EMIT_LWZ(r, 0, rd);
				break;
			}
			case MEM_LWR:
			{
				EMIT_ADDI(rd, rd, immed);
				EMIT_RLWINM(0, rd, 0, 30, 31);	// r0 = addr & 3
				EMIT_CMPI(0, 3, 1);
				EMIT_BNE(1, 4, 0, 0); // /!\ branch to just after 'Skip over else'

				EMIT_RLWINM(rd, rd, 0, 0, 29);	// addr &= 0xFFFFFFFC

				int r = mapRegisterNew( rt_reg );
				EMIT_LWZ(r, 0, rd);
				break;
			}
			case MEM_SB:
			{
				int r = mapRegister( rt_reg );
				EMIT_STB(r, immed, rd);
				CHECK_INVALID_CODE();
				break;
			}
			case MEM_SH:
			{
				int r = mapRegister( rt_reg );
				EMIT_STH(r, immed, rd);
				CHECK_INVALID_CODE();
				break;
			}
			case MEM_SW:
			{
				int r = mapRegister( rt_reg );
				EMIT_STW(r, immed, rd);
				CHECK_INVALID_CODE();
				break;
			}
			case MEM_SD:
			{
				RegMapping r = mapRegister64( rt_reg );
				EMIT_STW(r.hi, immed, rd);
				EMIT_STW(r.lo, immed+4, rd);
				CHECK_INVALID_CODE();
				break;
			}
			case MEM_SWC1:
			{
				int r = 3;
				EMIT_LWZ(addr, rt_reg*4, DYNAREG_FPR_32);
				EMIT_LWZ(r, 0, addr);
				EMIT_STW(r, immed, rd);
				CHECK_INVALID_CODE();
				break;
			}
			case MEM_SDC1:
			{
				int r = 3;
				int r2 = 4;
				EMIT_LWZ(addr, rt_reg*4, DYNAREG_FPR_64);
				EMIT_LWZ(r, 0, addr);
				EMIT_LWZ(r2, 4, addr);
				EMIT_STW(r, immed, rd);
				EMIT_STW(r2, immed+4, rd);
				CHECK_INVALID_CODE();
				break;
			}
			default:
				assert(0);
		}

		// Skip over else
		not_fastmem_id = add_jump_special(1);
		EMIT_B(not_fastmem_id, 0, 0);
		preCall = get_curr_dst();
	}
	
	// load into rt
    if(type!=MEM_SW && type!=MEM_SH && type!=MEM_SB)
    {
        EMIT_LI(3, rt_reg);
    }
    else
    {
        int r=mapRegister( rt_reg );
        if(r!=3)
            EMIT_OR(3,r,r);
    }
	
	genCallDynaMem(type, base, immed);
	
	if(!(failsafeRec&FAILSAFE_REC_NO_VM))
	{
		if(type==MEM_LW || type==MEM_LH || type==MEM_LB || type==MEM_LHU || type==MEM_LBU || type==MEM_LWL || type==MEM_LWR)
		{
			reloadRegister( rt_reg );
		}
		else if(type==MEM_LWU || type==MEM_LD)
		{
			reloadRegister64( rt_reg );
		}

		int callSize = get_curr_dst() - preCall;
		set_jump_special(not_fastmem_id, callSize+1);
	}
	else
	{
		invalidateRegisters();
	}

	return CONVERT_SUCCESS;
}

void * rewriteDynaMemVM(void* fault_addr)
{
    // enabling slow access by adding a jump from the fault address to the slow mem access code

    PowerPC_instr * fault_op=(PowerPC_instr*)fault_addr;
    
	PowerPC_instr * op=fault_op;
	
    while((*op>>PPC_OPCODE_SHIFT)!=PPC_OPCODE_B || (*op&1)!=0)
    {
        ++op;
    }

    // branch op
	++op;
	
	PowerPC_instr * first_slow_op=op;

	GEN_B(*fault_op,first_slow_op-fault_op,0,0);
	
    memicbi(fault_op,4);
    
    return first_slow_op;
}
