

#include <stdlib.h>
#include "r4300.h"
#include "ppc/Recompile.h"
#include "ppc/Wrappers.h"
#include "Recomp-Cache.h"

#include <ppc/cache.h>
#include <nocfe/lib_malloc.h>
#include <debug.h>

void DCFlushRange(void* startaddr, unsigned int len){
	if(len == 0) return;
#if 1
    memdcbf(startaddr,len);
#else
        __asm__ volatile (
		"clrlwi.	5, %0, 27\n" 
		"beq	1f\n" 
		"addi	%1, %1, 0x20\n" 
		"1:\n" 
		"addi	%1, %1, 0x1f\n" 
		"srwi	%1, %1, 5\n" 
		"mtctr	%1\n" 
		"2:\n" 
		"dcbf	0, %0\n" 
		"addi	%0, %0, 0x20\n" 
		"bdnz	2b\n" 
		"sync\n"
		: : "b" (startaddr), "b" (len) : "5", "memory" );
#endif
}

void ICInvalidateRange(void* startaddr, unsigned int len)  {
	if(len == 0) return;
#if 1
//	printf("icbi %p %d\n",startaddr,len);
	memicbi(startaddr,len);
#else
	__asm__ volatile (
		"clrlwi.	5, %0, 27\n" 
		"beq	1f\n" 
		"addi	%1, %1, 0x20\n" 
		"1:\n" 
		"addi	%1, %1, 0x1f\n" 
		"srwi	%1, %1, 5\n" 
		"mtctr	%1\n" 
		"2:\n" 
		"icbi	0, %0\n" 
		"addi	%0, %0, 0x20\n" 
		"bdnz	2b\n" 
		"sync\n" 
		"isync\n"
		: : "b" (startaddr), "b" (len) : "5", "memory" );
#endif
}

typedef struct _meta_node {
	unsigned int  addr;
	PowerPC_func* func;
	unsigned int  size;
} CacheMetaNode;

static int cacheSize;

#define HEAP_CHILD1(i) ((i<<1)+1)
#define HEAP_CHILD2(i) ((i<<1)+2)
#define HEAP_PARENT(i) ((i-1)>>2)

#define INITIAL_HEAP_SIZE (64)
static unsigned int heapSize = 0;
static unsigned int maxHeapSize = 0;
static CacheMetaNode** cacheHeap = NULL;

void * cache_buf=NULL;
void * meta_cache_buf=NULL;
mempool_t cache_mempool;
mempool_t meta_cache_mempool;

static void heapSwap(int i, int j){
	CacheMetaNode* t = cacheHeap[i];
	cacheHeap[i] = cacheHeap[j];
	cacheHeap[j] = t;
}

static void heapUp(int i){
	// While the given element is out of order
	while(i && cacheHeap[i]->func->lru < cacheHeap[HEAP_PARENT(i)]->func->lru){
		// Swap the child with its parent
		heapSwap(i, HEAP_PARENT(i));
		// Consider its new position
		i = HEAP_PARENT(i);
	}
}

static void heapDown(int i){
	// While the given element is out of order
	while(1){
		unsigned int lru = cacheHeap[i]->func->lru;
		CacheMetaNode* c1 = (HEAP_CHILD1(i) < heapSize) ?
		                     cacheHeap[HEAP_CHILD1(i)] : NULL;
		CacheMetaNode* c2 = (HEAP_CHILD2(i) < heapSize) ?
		                     cacheHeap[HEAP_CHILD2(i)] : NULL;
		// Check against the children, swapping with the min if parent isn't
		if(c1 && lru > c1->func->lru &&
		   (!c2 || c1->func->lru < c2->func->lru)){
			heapSwap(i, HEAP_CHILD1(i));
			i = HEAP_CHILD1(i);
		} else if(c2 && lru > c2->func->lru){
			heapSwap(i, HEAP_CHILD2(i));
			i = HEAP_CHILD2(i);
		} else break;
	}
}

static void heapify(void){
	int i;
	for(i=1; i<heapSize; ++i) heapUp(i);
}

static void heapPush(CacheMetaNode* node){
	if(heapSize == maxHeapSize){
		maxHeapSize = 3*maxHeapSize/2 + 10;
		cacheHeap = realloc(cacheHeap, maxHeapSize*sizeof(void*));
	}
	// Simply add it to the end of the heap
	// No need to heapUp since its the most recently used
	cacheHeap[heapSize++] = node;
}

static CacheMetaNode* heapPop(void){
	heapSwap(0, --heapSize);
	heapDown(0);
	return cacheHeap[heapSize];
}

static void unlink_func(PowerPC_func* func){
	//start_section(UNLINK_SECTION);
	
	// Remove any incoming links to this func
	PowerPC_func_link_node* link, * next_link;
	for(link = func->links_in; link != NULL; link = next_link){
		next_link = link->next;
		
		GEN_ORI(*(link->branch-16), 0, 0, 0);
		GEN_ORI(*(link->branch-15), 0, 0, 0);
		GEN_BLR(*link->branch, 1); // Set the linking branch to blrl
//		DCFlushRange(link->branch-16, 17*sizeof(PowerPC_instr));
		ICInvalidateRange(link->branch-16, 17*sizeof(PowerPC_instr));
		
		remove_func(&link->func->links_out, func);
		MetaCache_Free(link);
	}
	func->links_in = NULL;
	
	// Remove any references to outgoing links from this func
	void remove_outgoing_links(PowerPC_func_node** node){
		if(!*node) return;
		if((*node)->left) remove_outgoing_links(&(*node)->left);
		if((*node)->right) remove_outgoing_links(&(*node)->right);

		// Remove any links this function has which point in the code
		PowerPC_func_link_node** link, ** next;
		for(link = &(*node)->function->links_in; *link != NULL; link = next){
			next = &(*link)->next;
			if((*link)->func == func){
				PowerPC_func_link_node* tmp = (*link)->next;
				MetaCache_Free(*link);
				*link = tmp;
				next = link;
			}
		}
		MetaCache_Free(*node); // Free the PowerPC_func_node*
	}
	remove_outgoing_links(&func->links_out);
	func->links_out = NULL;
	
	//end_section(UNLINK_SECTION);
}

static void free_func(PowerPC_func* func, unsigned int addr){

        // Free the code associated with the func
	kfree(&cache_mempool,func->code);
	MetaCache_Free(func->code_addr);

	// Remove any holes into this func
	PowerPC_func_hole_node* hole, * next;
	for(hole = func->holes; hole != NULL; hole = next){
		next = hole->next;
		free(hole);
	}
	
	// Remove any pointers to this code
	PowerPC_block* block = blocks[addr>>12];
	remove_func(&block->funcs, func);
	// Remove func links
	unlink_func(func);

	free(func);
}

static inline void update_lru(PowerPC_func* func){
	static unsigned int nextLRU = 0;
	/*if(func->lru != nextLRU-1)*/ func->lru = nextLRU++;
	
	if(!nextLRU){
		// Handle nextLRU overflows
		// By heap-sorting and assigning new LRUs
		heapify();
		// Since you can't do an in-place min-heap ascending-sort
		//   I have to create a new heap
		CacheMetaNode** newHeap = malloc(maxHeapSize * sizeof(CacheMetaNode*));
		int i, savedSize = heapSize;
		for(i=0; heapSize > 0; ++i){
			newHeap[i] = heapPop();
			newHeap[i]->func->lru = i;
		}
		free(cacheHeap);
		cacheHeap = newHeap;
		
		nextLRU = heapSize = savedSize;
	}
}

static void release(int minNeeded){
	// Frees alloc'ed blocks so that at least minNeeded bytes are available
	int toFree = minNeeded * 2; // Free 2x what is needed
//	printf("RecompCache requires ~%dkB to be released\n", minNeeded/1024);
	// Restore the heap properties to pop the LRU
	heapify();
	// Release nodes' memory until we've freed enough
	while(toFree > 0 && cacheSize){
		// Pop the LRU to be freed
		CacheMetaNode* n = heapPop();
		// Free the function it contains
		free_func(n->func, n->addr);
		toFree    -= n->size;
		cacheSize -= n->size;
		// And the cache node itself
		free(n);
	}
}

void RecompCache_Alloc(unsigned int size, unsigned int address, PowerPC_func* func){
	CacheMetaNode* newBlock = malloc( sizeof(CacheMetaNode) );
	newBlock->addr = address;
	newBlock->size = size;
	newBlock->func = func;
	
	int num_instrs = (func->end_addr - func->start_addr);
	if(cacheSize + size + num_instrs * sizeof(void*) > RECOMP_CACHE_SIZE)
		// Free up at least enough space for it to fit
		release(cacheSize + size + num_instrs * sizeof(void*) - RECOMP_CACHE_SIZE);
	
	void* code = kmalloc(&cache_mempool,size,128);
	while(!code){
		release(size);
		code = kmalloc(&cache_mempool,size,128);
	}

	// We have the necessary space for this alloc, so just call malloc
	cacheSize += size;
	
	newBlock->func->code = code;
	newBlock->func->code_addr = MetaCache_Alloc(num_instrs * sizeof(void*));
	// Add it to the heap
	heapPush(newBlock);
	// Make this function the LRU
	update_lru(func);
}

void RecompCache_Realloc(PowerPC_func* func, unsigned int new_size){
	// I'm not worrying about maintaining the cache size here for now
	kfree(&cache_mempool,func->code);
	func->code = kmalloc(&cache_mempool,new_size,128);
	int i;
	for(i=heapSize-1; i>=0; --i){
		if(func == cacheHeap[i]->func){
			cacheSize += new_size - cacheHeap[i]->size;
			cacheHeap[i]->size = new_size;
			break;
		}
	}
	
	// Remove any func links since the code has changed
	unlink_func(func);
}

void RecompCache_Free(unsigned int addr){
	int i;
	CacheMetaNode* n = NULL;
	// Find the corresponding node
	for(i=heapSize-1; i>=0; --i){
		if(addr == cacheHeap[i]->addr){
			n = cacheHeap[i];
			// Remove from the heap
			heapSwap(i, --heapSize);
			// Free n's func
			free_func(n->func, addr);
			cacheSize -= n->size;
			// Free the cache node
			free(n);
			break;
		}
	}
}

void RecompCache_Update(PowerPC_func* func){
	update_lru(func);
}

void RecompCache_Link(PowerPC_func* src_func, PowerPC_instr* src_instr,
                      PowerPC_func* dst_func, PowerPC_instr* dst_instr){
	//start_section(LINK_SECTION);
	
	// Setup book-keeping
	// Create the incoming link info
	PowerPC_func_link_node* fln = MetaCache_Alloc(sizeof(PowerPC_func_link_node));

	fln->branch = src_instr;
	fln->func = src_func;
	fln->next = dst_func->links_in;
	dst_func->links_in = fln;
	// Create the outgoing link info
	insert_func(&src_func->links_out, dst_func);
	
	// Actually link the funcs

	GEN_LIS(*(src_instr-16), DYNAREG_FUNC, (unsigned int)dst_func>>16);
	GEN_ORI(*(src_instr-15), DYNAREG_FUNC,DYNAREG_FUNC, (unsigned int)dst_func);

	if (abs(dst_instr-src_instr)>8*1024*1024){
		GEN_LIS(*(src_instr-3), 12, (unsigned int)dst_instr>>16);
		GEN_ORI(*(src_instr-2), 12, 12, (unsigned int)dst_instr);
		GEN_MTCTR(*(src_instr-1), 12);
		GEN_BCTR(*src_instr);
	}else{
		GEN_B(*(src_instr), (PowerPC_instr*)dst_instr-src_instr, 0, 0);
	}

//	DCFlushRange(src_instr-16, 17*sizeof(PowerPC_instr));
	ICInvalidateRange(src_instr-16, 17*sizeof(PowerPC_instr));
	
	//end_section(LINK_SECTION);
}

void RecompCache_Init(void){
	if(!cache_buf){
		cache_buf=malloc(RECOMP_CACHE_ALLOC_SIZE);
	}
    kmeminit(&cache_mempool,cache_buf,RECOMP_CACHE_ALLOC_SIZE);
		
/*	if(!meta_cache_buf){
		meta_cache_buf=malloc(META_CACHE_ALLOC_SIZE);
	}
    kmeminit(&meta_cache_mempool,meta_cache_buf,META_CACHE_ALLOC_SIZE);*/
}

void* MetaCache_Alloc(unsigned int size){
	//return kmalloc(&meta_cache_mempool,size,0);
	return malloc(size);
}

void MetaCache_Free(void* ptr){
	//kfree(&meta_cache_mempool,ptr);
	free(ptr);
}

