/* Recomp-Cache.h - Interface for allocating/freeing blocks of recompiled code
   by Mike Slegeir for Mupen64-GC
 */

#ifndef RECOMP_CACHE_H
#define RECOMP_CACHE_H

#define RECOMP_CACHE_SIZE (12*1024*1024)
#define RECOMP_CACHE_ALLOC_SIZE (20*1024*1024)

#define META_CACHE_ALLOC_SIZE (4*1024*1024)

extern __attribute__((aligned(65536))) unsigned char recomp_cache_buffer[RECOMP_CACHE_ALLOC_SIZE];


void DCFlushRange(void* startaddr, unsigned int len);
void ICInvalidateRange(void* startaddr, unsigned int len);

// Allocate and free memory to be used for recompiled code
//   Any memory allocated this way can be freed at any time
//   you must check invalid_code before you can access it
void RecompCache_Alloc(unsigned int size, unsigned int address, PowerPC_func* func);
void RecompCache_Realloc(PowerPC_func*, unsigned int new_size);
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
