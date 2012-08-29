#ifdef MCHK

#include <stdlib.h>
#include <string.h>
#include <xetypes.h>
#include <debug.h>
#include <ppc/atomic.h>

#define PRE_GUARD 1024
#define POST_GUARD 1024

#define FILL 0x42

static unsigned int lck=0;

void * __real_malloc(size_t size);
void * __real_calloc(size_t num, size_t size);
void * __real_realloc(void * p,size_t size);
void __real_free(void * p);



void * __wrap_malloc(size_t size)
{
    u32 lr=0;
    asm volatile("mflr %[lr]":[lr] "+r" (lr));

    lock(&lck);
    
    size+=PRE_GUARD;
    size+=POST_GUARD;
    
    u8 * p=__real_malloc(size);
    
    memset(p,FILL,size);
    
    *(size_t*)&p[0]=size;
    *(u32*)&p[4]=lr;
    
    unlock(&lck);
    
    return &p[PRE_GUARD];
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
    u8 * sp=&pp[-PRE_GUARD];
    
    size_t size=*(size_t*)&sp[0];
    u32 lr =*(u32*)&sp[4];

    int i;
    for(i=8;i<PRE_GUARD;++i)
        if (sp[i]!=FILL)
        {
            printf("[mchk] corrupted malloc !!!! size=%d lr=%p\n",size-PRE_GUARD-POST_GUARD,lr);
            buffer_dump(sp,PRE_GUARD);
            asm volatile("sc");
        }
    
    for(i=0;i<POST_GUARD;++i)
        if (sp[i+size-POST_GUARD]!=FILL)
        {
            printf("[mchk] corrupted malloc !!!! size=%d lr=%p\n",size-PRE_GUARD-POST_GUARD,lr);
            buffer_dump(&sp[size-POST_GUARD],POST_GUARD);
            asm volatile("sc");
        }

    memset(sp,FILL,size);
    
    __real_free(sp);

    unlock(&lck);
    
}

void * __wrap_realloc(void * p,size_t size)
{
    void * np=__wrap_malloc(size);
    
    if(p)
    {
        u8 * pp=(u8*)p;
        u8 * sp=&pp[-PRE_GUARD];

        size_t osize=*(size_t*)&sp[0];

        memcpy(np,pp,(osize>size)?size:osize);

        __wrap_free(p);
    }
    
    return np;
}

#endif