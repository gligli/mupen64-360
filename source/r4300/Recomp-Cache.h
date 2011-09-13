/* Recomp-Cache.h - Interface for allocating/freeing blocks of recompiled code
   by Mike Slegeir for Mupen64-GC
 */

#ifndef RECOMP_CACHE_H
#define RECOMP_CACHE_H

// Hold 32MB worth of recompiled data max
#define RECOMP_CACHE_SIZE (16*1024*1024)
#define RECOMP_CACHE_ALLOC_SIZE (24*1024*1024)

#define META_CACHE_ALLOC_SIZE (8*1024*1024)

// Allocate and free memory to be used for recompiled code
//   Any memory allocated this way can be freed at any time
//   you must check invalid_code before you can access it
void RecompCache_Alloc(unsigned int size, unsigned int address, PowerPC_func* func);
void RecmopCache_Realloc(PowerPC_func*, unsigned int new_size);
void RecompCache_Free(unsigned int addr);
// Update the LRU info of the indicated block
//   (call when the block is accessed)
void RecompCache_Update(PowerPC_func* func);
// Create a link between two funcs
void RecompCache_Link(PowerPC_func* src_func, PowerPC_instr* src_instr,
                      PowerPC_func* dst_func, PowerPC_instr* dst_instr);

void RecompCache_Init(void);


void* MetaCache_Alloc(unsigned int size);
void MetaCache_Free(void* ptr);
#endif
