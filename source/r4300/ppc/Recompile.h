/**
 * Wii64 - Recompile.h
 * Copyright (C) 2007, 2008, 2009, 2010 Mike Slegeir
 * 
 * Functions and data structures for recompiling blocks of code
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

#ifndef RECOMPILE_H
#define RECOMPILE_H

#include "main/main.h"
#include "MIPS-to-PPC.h"

typedef unsigned int uint;

struct func;

typedef struct func_node {
	struct func* function;
	struct func_node* left;
	struct func_node* right;
} PowerPC_func_node;

typedef struct link_node {
	PowerPC_instr*    branch;
	struct func*      func;
	struct link_node* next;
} PowerPC_func_link_node;

typedef struct func {
	unsigned int start_address;
	unsigned int end_address;
	PowerPC_instr* code;
	unsigned int   code_length;
	unsigned int   lru;
	PowerPC_func_link_node* links_in;
	PowerPC_func_node*      links_out;
	PowerPC_instr** code_addr;
} PowerPC_func;

PowerPC_func* find_func(PowerPC_func_node** root, unsigned int addr);
void insert_func(PowerPC_func_node** root, PowerPC_func* func);
void remove_func(PowerPC_func_node** root, PowerPC_func* func);
void remove_node(PowerPC_func_node** node);

typedef struct {
	unsigned int    start_address; // The address this code begins for the 64
	unsigned int    end_address;
	//PowerPC_instr** code_addr;     // table of block offsets to code pointer,
	                               //   its length is end_addr - start_addr
	PowerPC_func_node* funcs;      // BST of functions in this block
	unsigned long   adler32;       // Used for TLB
	
	unsigned long splits[1024];
	int split_count;
	
} PowerPC_block;

#define MAX_JUMPS        4096
#define JUMP_TYPE_J      1   // uses a long immed & abs addr
#define JUMP_TYPE_CALL   2   // the jump is to a C function
#define JUMP_TYPE_SPEC   4   // special jump, destination precomputed
typedef struct {
	unsigned int   src_pc;
	PowerPC_instr* dst_instr;
	unsigned int   old_jump;
	unsigned int   new_jump;
	uint           type;
} jump_node;

MIPS_instr get_next_src(void);
MIPS_instr peek_next_src(void);

void       set_next_dst(PowerPC_instr);

int        add_jump(int old_jump, int is_j, int is_call);
int        is_j_out(int branch, int is_aa);
int        is_j_dst(void);
// Use these for jumps that won't be known until later in compile time
int        add_jump_special(int is_j);
void       set_jump_special(int which, int new_jump);

int  func_was_freed(PowerPC_func*);
void clear_freed_funcs(void);

/* These functions are used to initialize, recompile, and deinit a block
   init assumes that all pointers in the block fed it it are NULL or allocated
   memory. Deinit frees a block with the same preconditions.
 */
PowerPC_func* recompile_block(PowerPC_block* ppc_block, unsigned int addr);
void init_block			(PowerPC_block* ppc_block);
void deinit_block		(PowerPC_block* ppc_block);
void invalidate_block	(PowerPC_block* ppc_block);

void add_block_split(PowerPC_block * block, unsigned int addr);
int is_block_split(PowerPC_block * block, unsigned int addr);

#ifdef HW_RVL
#include "../../memory/MEM2.h"
extern PowerPC_block **blocks;
#else
#ifndef ARAM_BLOCKCACHE
extern PowerPC_block *blocks[0x100000];
#endif
#endif

extern char txtbuffer[1024];
#define DBG_USBGECKO

#if 1
#define DEBUG_print(txtbuffer, dummy) printf("%s",txtbuffer)
#else
#define DEBUG_print(txtbuffer, dummy)
#endif

extern int do_disasm;
int disassemble(unsigned int a, unsigned int op);

#define EMIT_B(dst,aa,lk) \
{PowerPC_instr ppc;GEN_B(ppc,dst,aa,lk);set_next_dst(ppc);}
#define EMIT_MTCTR(rs) \
{PowerPC_instr ppc;GEN_MTCTR(ppc,rs);set_next_dst(ppc);}
#define EMIT_MFCTR(rd) \
{PowerPC_instr ppc;GEN_MFCTR(ppc,rd);set_next_dst(ppc);}
#define EMIT_ADDIS(rd,ra,immed) \
{PowerPC_instr ppc;GEN_ADDIS(ppc,rd,ra,immed);set_next_dst(ppc);}
#define EMIT_LIS(rd,immed) \
{PowerPC_instr ppc;GEN_LIS(ppc,rd,immed);set_next_dst(ppc);}
#define EMIT_LI(rd,immed) \
{PowerPC_instr ppc;GEN_LI(ppc,rd,immed);set_next_dst(ppc);}
#define EMIT_LWZ(rd,immed,ra) \
{PowerPC_instr ppc;GEN_LWZ(ppc,rd,immed,ra);set_next_dst(ppc);}
#define EMIT_LHZ(rd,immed,ra) \
{PowerPC_instr ppc;GEN_LHZ(ppc,rd,immed,ra);set_next_dst(ppc);}
#define EMIT_LHA(rd,immed,ra) \
{PowerPC_instr ppc;GEN_LHA(ppc,rd,immed,ra);set_next_dst(ppc);}
#define EMIT_LBZ(rd,immed,ra) \
{PowerPC_instr ppc;GEN_LBZ(ppc,rd,immed,ra);set_next_dst(ppc);}
#define EMIT_EXTSB(rd,rs) \
{PowerPC_instr ppc;GEN_EXTSB(ppc,rd,rs);set_next_dst(ppc);}
#define EMIT_EXTSH(rd,rs) \
{PowerPC_instr ppc;GEN_EXTSH(ppc,rd,rs);set_next_dst(ppc);}
#define EMIT_EXTSW(rd,rs) \
{PowerPC_instr ppc;GEN_EXTSW(ppc,rd,rs);set_next_dst(ppc);}
#define EMIT_STB(rs,immed,ra) \
{PowerPC_instr ppc;GEN_STB(ppc,rs,immed,ra);set_next_dst(ppc);}
#define EMIT_STH(rs,immed,ra) \
{PowerPC_instr ppc;GEN_STH(ppc,rs,immed,ra);set_next_dst(ppc);}
#define EMIT_STW(rs,immed,ra) \
{PowerPC_instr ppc;GEN_STW(ppc,rs,immed,ra);set_next_dst(ppc);}
#define EMIT_BCTR(ppce) \
{PowerPC_instr ppc;GEN_BCTR(ppc);set_next_dst(ppc);}
#define EMIT_BCTRL(ppce) \
{PowerPC_instr ppc;GEN_BCTRL(ppc);set_next_dst(ppc);}
#define EMIT_BCCTR(bo,bi,lk) \
{PowerPC_instr ppc;GEN_BCCTR(ppc,bo,bi,lk);set_next_dst(ppc);}
#define EMIT_CMP(ra,rb,cr) \
{PowerPC_instr ppc;GEN_CMP(ppc,ra,rb,cr);set_next_dst(ppc);}
#define EMIT_CMPL(ra,rb,cr) \
{PowerPC_instr ppc;GEN_CMPL(ppc,ra,rb,cr);set_next_dst(ppc);}
#define EMIT_CMPI(ra,immed,cr) \
{PowerPC_instr ppc;GEN_CMPI(ppc,ra,immed,cr);set_next_dst(ppc);}
#define EMIT_CMPLI(ra,immed,cr) \
{PowerPC_instr ppc;GEN_CMPLI(ppc,ra,immed,cr);set_next_dst(ppc);}
#define EMIT_BC(dst,aa,lk,bo,bi) \
{PowerPC_instr ppc;GEN_BC(ppc,dst,aa,lk,bo,bi);set_next_dst(ppc);}
#define EMIT_BNE(cr,dst,aa,lk) \
{PowerPC_instr ppc;GEN_BNE(ppc,cr,dst,aa,lk);set_next_dst(ppc);}
#define EMIT_BEQ(cr,dst,aa,lk) \
{PowerPC_instr ppc;GEN_BEQ(ppc,cr,dst,aa,lk);set_next_dst(ppc);}
#define EMIT_BGT(cr,dst,aa,lk) \
{PowerPC_instr ppc;GEN_BGT(ppc,cr,dst,aa,lk);set_next_dst(ppc);}
#define EMIT_BLE(cr,dst,aa,lk) \
{PowerPC_instr ppc;GEN_BLE(ppc,cr,dst,aa,lk);set_next_dst(ppc);}
#define EMIT_BGE(cr,dst,aa,lk) \
{PowerPC_instr ppc;GEN_BGE(ppc,cr,dst,aa,lk);set_next_dst(ppc);}
#define EMIT_BLT(cr,dst,aa,lk) \
{PowerPC_instr ppc;GEN_BLT(ppc,cr,dst,aa,lk);set_next_dst(ppc);}
#define EMIT_ADDI(rd,ra,immed) \
{PowerPC_instr ppc;GEN_ADDI(ppc,rd,ra,immed);set_next_dst(ppc);}
#define EMIT_RLWINM(rd,ra,sh,mb,me) \
{PowerPC_instr ppc;GEN_RLWINM(ppc,rd,ra,sh,mb,me);set_next_dst(ppc);}
#define EMIT_RLWIMI(rd,ra,sh,mb,me) \
{PowerPC_instr ppc;GEN_RLWIMI(ppc,rd,ra,sh,mb,me);set_next_dst(ppc);}
#define EMIT_SRWI(rd,ra,sh) \
{PowerPC_instr ppc;GEN_SRWI(ppc,rd,ra,sh);set_next_dst(ppc);}
#define EMIT_SLWI(rd,ra,sh) \
{PowerPC_instr ppc;GEN_SLWI(ppc,rd,ra,sh);set_next_dst(ppc);}
#define EMIT_SRAWI(rd,ra,sh) \
{PowerPC_instr ppc;GEN_SRAWI(ppc,rd,ra,sh);set_next_dst(ppc);}
#define EMIT_SLW(rd,ra,rb) \
{PowerPC_instr ppc;GEN_SLW(ppc,rd,ra,rb);set_next_dst(ppc);}
#define EMIT_SRW(rd,ra,rb) \
{PowerPC_instr ppc;GEN_SRW(ppc,rd,ra,rb);set_next_dst(ppc);}
#define EMIT_SRAW(rd,ra,rb) \
{PowerPC_instr ppc;GEN_SRAW(ppc,rd,ra,rb);set_next_dst(ppc);}
#define EMIT_ANDI(rd,ra,immed) \
{PowerPC_instr ppc;GEN_ANDI(ppc,rd,ra,immed);set_next_dst(ppc);}
#define EMIT_ORI(rd,ra,immed) \
{PowerPC_instr ppc;GEN_ORI(ppc,rd,ra,immed);set_next_dst(ppc);}
#define EMIT_XORI(rd,ra,immed) \
{PowerPC_instr ppc;GEN_XORI(ppc,rd,ra,immed);set_next_dst(ppc);}
#define EMIT_XORIS(rd,ra,immed) \
{PowerPC_instr ppc;GEN_XORIS(ppc,rd,ra,immed);set_next_dst(ppc);}
#define EMIT_MULLW(rd,ra,rb) \
{PowerPC_instr ppc;GEN_MULLW(ppc,rd,ra,rb);set_next_dst(ppc);}
#define EMIT_MULHW(rd,ra,rb) \
{PowerPC_instr ppc;GEN_MULHW(ppc,rd,ra,rb);set_next_dst(ppc);}
#define EMIT_MULHWU(rd,ra,rb) \
{PowerPC_instr ppc;GEN_MULHWU(ppc,rd,ra,rb);set_next_dst(ppc);}
#define EMIT_DIVW(rd,ra,rb) \
{PowerPC_instr ppc;GEN_DIVW(ppc,rd,ra,rb);set_next_dst(ppc);}
#define EMIT_DIVWU(rd,ra,rb) \
{PowerPC_instr ppc;GEN_DIVWU(ppc,rd,ra,rb);set_next_dst(ppc);}
#define EMIT_ADD(rd,ra,rb) \
{PowerPC_instr ppc;GEN_ADD(ppc,rd,ra,rb);set_next_dst(ppc);}
#define EMIT_SUBF(rd,ra,rb) \
{PowerPC_instr ppc;GEN_SUBF(ppc,rd,ra,rb);set_next_dst(ppc);}
#define EMIT_SUBFC(rd,ra,rb) \
{PowerPC_instr ppc;GEN_SUBFC(ppc,rd,ra,rb);set_next_dst(ppc);}
#define EMIT_SUBFE(rd,ra,rb) \
{PowerPC_instr ppc;GEN_SUBFE(ppc,rd,ra,rb);set_next_dst(ppc);}
#define EMIT_ADDIC(rd,ra,immed) \
{PowerPC_instr ppc;GEN_ADDIC(ppc,rd,ra,immed);set_next_dst(ppc);}
#define EMIT_SUB(rd,ra,rb) \
{PowerPC_instr ppc;GEN_SUB(ppc,rd,ra,rb);set_next_dst(ppc);}
#define EMIT_AND(rd,ra,rb) \
{PowerPC_instr ppc;GEN_AND(ppc,rd,ra,rb);set_next_dst(ppc);}
#define EMIT_NAND(rd,ra,rb) \
{PowerPC_instr ppc;GEN_NAND(ppc,rd,ra,rb);set_next_dst(ppc);}
#define EMIT_ANDC(rd,ra,rb) \
{PowerPC_instr ppc;GEN_ANDC(ppc,rd,ra,rb);set_next_dst(ppc);}
#define EMIT_NOR(rd,ra,rb) \
{PowerPC_instr ppc;GEN_NOR(ppc,rd,ra,rb);set_next_dst(ppc);}
#define EMIT_OR(rd,ra,rb) \
{PowerPC_instr ppc;GEN_OR(ppc,rd,ra,rb);set_next_dst(ppc);}
#define EMIT_XOR(rd,ra,rb) \
{PowerPC_instr ppc;GEN_XOR(ppc,rd,ra,rb);set_next_dst(ppc);}
#define EMIT_BLR(lk) \
{PowerPC_instr ppc;GEN_BLR(ppc,lk);set_next_dst(ppc);}
#define EMIT_MTLR(rs) \
{PowerPC_instr ppc;GEN_MTLR(ppc,rs);set_next_dst(ppc);}
#define EMIT_MFLR(rd) \
{PowerPC_instr ppc;GEN_MFLR(ppc,rd);set_next_dst(ppc);}
#define EMIT_MTCR(rs) \
{PowerPC_instr ppc;GEN_MTCR(ppc,rs);set_next_dst(ppc);}
#define EMIT_NEG(rd,rs) \
{PowerPC_instr ppc;GEN_NEG(ppc,rd,rs);set_next_dst(ppc);}
#define EMIT_EQV(rd,ra,rb) \
{PowerPC_instr ppc;GEN_EQV(ppc,rd,ra,rb);set_next_dst(ppc);}
#define EMIT_ADDZE(rd,rs) \
{PowerPC_instr ppc;GEN_ADDZE(ppc,rd,rs);set_next_dst(ppc);}
#define EMIT_ADDC(rd,ra,rb) \
{PowerPC_instr ppc;GEN_ADDC(ppc,rd,ra,rb);set_next_dst(ppc);}
#define EMIT_ADDE(rd,ra,rb) \
{PowerPC_instr ppc;GEN_ADDE(ppc,rd,ra,rb);set_next_dst(ppc);}
#define EMIT_SUBFIC(rd,ra,immed) \
{PowerPC_instr ppc;GEN_SUBFIC(ppc,rd,ra,immed);set_next_dst(ppc);}
#define EMIT_STFD(fs,immed,rb) \
{PowerPC_instr ppc;GEN_STFD(ppc,fs,immed,rb);set_next_dst(ppc);}
#define EMIT_STFS(fs,immed,rb) \
{PowerPC_instr ppc;GEN_STFS(ppc,fs,immed,rb);set_next_dst(ppc);}
#define EMIT_LFD(fd,immed,rb) \
{PowerPC_instr ppc;GEN_LFD(ppc,fd,immed,rb);set_next_dst(ppc);}
#define EMIT_LFS(fd,immed,rb) \
{PowerPC_instr ppc;GEN_LFS(ppc,fd,immed,rb);set_next_dst(ppc);}
#define EMIT_FADD(fd,fa,fb,dbl) \
{PowerPC_instr ppc;GEN_FADD(ppc,fd,fa,fb,dbl);set_next_dst(ppc);}
#define EMIT_FSUB(fd,fa,fb,dbl) \
{PowerPC_instr ppc;GEN_FSUB(ppc,fd,fa,fb,dbl);set_next_dst(ppc);}
#define EMIT_FMUL(fd,fa,fb,dbl) \
{PowerPC_instr ppc;GEN_FMUL(ppc,fd,fa,fb,dbl);set_next_dst(ppc);}
#define EMIT_FDIV(fd,fa,fb,dbl) \
{PowerPC_instr ppc;GEN_FDIV(ppc,fd,fa,fb,dbl);set_next_dst(ppc);}
#define EMIT_FABS(fd,fs) \
{PowerPC_instr ppc;GEN_FABS(ppc,fd,fs);set_next_dst(ppc);}
#define EMIT_FCFID(fd,fs) \
{PowerPC_instr ppc;GEN_FCFID(ppc,fd,fs);set_next_dst(ppc);}
#define EMIT_FRSP(fd,fs) \
{PowerPC_instr ppc;GEN_FRSP(ppc,fd,fs);set_next_dst(ppc);}
#define EMIT_FMR(fd,fs) \
{PowerPC_instr ppc;GEN_FMR(ppc,fd,fs);set_next_dst(ppc);}
#define EMIT_FNEG(fd,fs) \
{PowerPC_instr ppc;GEN_FNEG(ppc,fd,fs);set_next_dst(ppc);}
#define EMIT_FCTIW(fd,fs) \
{PowerPC_instr ppc;GEN_FCTIW(ppc,fd,fs);set_next_dst(ppc);}
#define EMIT_FCTIWZ(fd,fs) \
{PowerPC_instr ppc;GEN_FCTIWZ(ppc,fd,fs);set_next_dst(ppc);}
#define EMIT_STFIWX(fs,ra,rb) \
{PowerPC_instr ppc;GEN_STFIWX(ppc,fs,ra,rb);set_next_dst(ppc);}
#define EMIT_MTFSFI(field,immed) \
{PowerPC_instr ppc;GEN_MTFSFI(ppc,field,immed);set_next_dst(ppc);}
#define EMIT_MTFSF(fields,fs) \
{PowerPC_instr ppc;GEN_MTFSF(ppc,fields,fs);set_next_dst(ppc);}
#define EMIT_FCMPU(fa,fb,cr) \
{PowerPC_instr ppc;GEN_FCMPU(ppc,fa,fb,cr);set_next_dst(ppc);}
#define EMIT_FRSQRTE(fd,fs) \
{PowerPC_instr ppc;GEN_FRSQRTE(ppc,fd,fs);set_next_dst(ppc);}
#define EMIT_FSQRT(fd,fs) \
{PowerPC_instr ppc;GEN_FSQRT(ppc,fd,fs);set_next_dst(ppc);}
#define EMIT_FSQRTS(fd,fs) \
{PowerPC_instr ppc;GEN_FSQRTS(ppc,fd,fs);set_next_dst(ppc);}
#define EMIT_FSEL(fd,fa,fb,fc) \
{PowerPC_instr ppc;GEN_FSEL(ppc,fd,fa,fb,fc);set_next_dst(ppc);}
#define EMIT_FRES(fd,fs) \
{PowerPC_instr ppc;GEN_FRES(ppc,fd,fs);set_next_dst(ppc);}
#define EMIT_FNMSUB(fd,fa,fc,fb) \
{PowerPC_instr ppc;GEN_FNMSUB(ppc,fd,fa,fc,fb);set_next_dst(ppc);}
#define EMIT_FNMSUBS(fd,fa,fc,fb) \
{PowerPC_instr ppc;GEN_FNMSUBS(ppc,fd,fa,fc,fb);set_next_dst(ppc);}
#define EMIT_FMADD(fd,fa,fc,fb) \
{PowerPC_instr ppc;GEN_FMADD(ppc,fd,fa,fc,fb);set_next_dst(ppc);}
#define EMIT_FMADDS(fd,fa,fc,fb) \
{PowerPC_instr ppc;GEN_FMADDS(ppc,fd,fa,fc,fb);set_next_dst(ppc);}
#define EMIT_BCLR(lk,bo,bi) \
{PowerPC_instr ppc;GEN_BCLR(ppc,lk,bo,bi);set_next_dst(ppc);}
#define EMIT_BNELR(cr,lk) \
{PowerPC_instr ppc;GEN_BNELR(ppc,cr,lk);set_next_dst(ppc);}
#define EMIT_BLELR(cr,lk) \
{PowerPC_instr ppc;GEN_BLELR(ppc,cr,lk);set_next_dst(ppc);}
#define EMIT_ANDIS(rd,ra,immed) \
{PowerPC_instr ppc;GEN_ANDIS(ppc,rd,ra,immed);set_next_dst(ppc);}
#define EMIT_ORIS(rd,rs,immed) \
{PowerPC_instr ppc;GEN_ORIS(ppc,rd,rs,immed);set_next_dst(ppc);}
#define EMIT_CROR(cd,ca,cb) \
{PowerPC_instr ppc;GEN_CROR(ppc,cd,ca,cb);set_next_dst(ppc);}
#define EMIT_CRNOR(cd,ca,cb) \
{PowerPC_instr ppc;GEN_CRNOR(ppc,cd,ca,cb);set_next_dst(ppc);}
#define EMIT_MFCR(rt) \
{PowerPC_instr ppc;GEN_MFCR(ppc,rt);set_next_dst(ppc);}
#define EMIT_MCRXR(bf) \
{PowerPC_instr ppc;GEN_MCRXR(ppc,bf);set_next_dst(ppc);}

#define EMIT_LVX(vd,ra,rb) \
{PowerPC_instr ppc=0x7C0000CE;PPC_SET_RD(ppc,vd);PPC_SET_RA(ppc,ra);PPC_SET_RB(ppc,rb);set_next_dst(ppc);}
#define EMIT_STVX(vs,ra,rb) \
{PowerPC_instr ppc=0x7C0001CE;PPC_SET_RD(ppc,vs);PPC_SET_RA(ppc,ra);PPC_SET_RB(ppc,rb);set_next_dst(ppc);}
#define EMIT_VOR(vd,va,vb) \
{PowerPC_instr ppc=0x10000484;PPC_SET_RD(ppc,vd);PPC_SET_RA(ppc,va);PPC_SET_RB(ppc,vb);set_next_dst(ppc);}


#endif

