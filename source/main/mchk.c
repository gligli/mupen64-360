#ifdef MCHK

#include <stdlib.h>
#include <string.h>
#include <xetypes.h>
#include <debug.h>
#include <ppc/atomic.h>

#define PRE_GUARD 4096
#define POST_GUARD 4096

#define MAGIC 0xdeadbeef

#define FILL 0xaa
#define FILL_FREE 0xcc

static unsigned int lck=0;

void * __real_malloc(size_t size);
void * __real_calloc(size_t num, size_t size);
void * __real_memalign(size_t align, size_t size);
void * __real_realloc(void * p,size_t size);
void __real_free(void * p);



void * __wrap_malloc(size_t size)
{
    u32 lr=(u32)__builtin_return_address(0);

    lock(&lck);
    
    size+=PRE_GUARD;
    size+=POST_GUARD;
    
    u8 * p=__real_malloc(size);
    
    memset(p,FILL,size);
    
    *(u32*)&p[PRE_GUARD-4]=MAGIC;
    *(size_t*)&p[PRE_GUARD-8]=PRE_GUARD;
    *(size_t*)&p[PRE_GUARD-12]=size;
    *(u32*)&p[PRE_GUARD-16]=lr;
    
    unlock(&lck);
    
    return &p[PRE_GUARD];
}

void * __wrap_memalign(size_t align, size_t size)
{
    u32 lr=(u32)__builtin_return_address(0);

    lock(&lck);
    
    u32 pg=align>PRE_GUARD?align:PRE_GUARD;
    
    size+=pg;
    size+=POST_GUARD;
    
    u8 * p=__real_memalign(align,size);
    
    memset(p,FILL,size);
    
    *(u32*)&p[pg-4]=MAGIC;
    *(size_t*)&p[pg-8]=pg;
    *(size_t*)&p[pg-12]=size;
    *(u32*)&p[pg-16]=lr;
    
    unlock(&lck);
    
    return &p[pg];
}

void * __wrap_calloc(size_t num, size_t size)
{
    u8 * p=__wrap_malloc(num*size);
    memset(p,0,num*size);
    return p;
}

void __wrap_free(void * p)
{
    if(!p) return;
    
    lock(&lck);

    u8 * pp=(u8*)p;
    
    u32 magic=*(u32*)&pp[-4];

    if(magic!=MAGIC)
    {
        printf("[mchk free] bad magic !!!!\n");
        buffer_dump(&pp[-PRE_GUARD],PRE_GUARD);
        unlock(&lck);
        return;
    }
    
    size_t pg=*(size_t*)&pp[-8];
    size_t size=*(size_t*)&pp[-12];
    u32 lr =*(u32*)&pp[-16];

    u8 * sp=&pp[-pg];

    int i;
    for(i=0;i<pg-16;++i)
        if (sp[i]!=FILL)
        {
            printf("[mchk] corrupted malloc !!!! size=%d lr=%p\n",size-pg-POST_GUARD,lr);
            buffer_dump(sp,pg);
            asm volatile("sc");
        }
    
    for(i=0;i<POST_GUARD;++i)
        if (sp[i+size-POST_GUARD]!=FILL)
        {
            printf("[mchk] corrupted malloc !!!! size=%d lr=%p\n",size-pg-POST_GUARD,lr);
            buffer_dump(&sp[size-POST_GUARD],POST_GUARD);
            asm volatile("sc");
        }

    memset(sp,FILL_FREE,size);
    
    __real_free(sp);

    unlock(&lck);
    
}

void * __wrap_realloc(void * p,size_t size)
{
    void * np=__wrap_malloc(size);
    
    if(p)
    {
        u8 * pp=(u8*)p;

        u32 magic=*(u32*)&pp[-4];

        if(magic!=MAGIC)
        {
            printf("[mchk realloc] bad magic !!!!\n");
            buffer_dump(&pp[-PRE_GUARD],PRE_GUARD);
            unlock(&lck);
            return NULL;
        }

        size_t osize=*(size_t*)&pp[-12];

        memcpy(np,pp,(osize>size)?size:osize);

        __wrap_free(p);
    }
    
    return np;
}

#endif