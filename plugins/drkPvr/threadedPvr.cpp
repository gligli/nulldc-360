#include <deque>

#include "threadedPvr.h"

#include <stdio.h>
#include <stdlib.h>

#include <xenon_soc/xenon_power.h>

#include <ppc/xenonsprs.h>
#include <ppc/register.h>
#include <ppc/atomic.h>
#include <time/time.h>
#include <bits/stl_bvector.h>
#include "emitter/3DMath.h"

#include "Renderer_if.h"
#include "ta.h"
#include "spg.h"

using namespace std;

volatile bool threaded_pvr=true;

static volatile bool running=false;

#define TA_RING_MAX_COUNT 32768

#define ta_ring_count(str) ((u32)__builtin_labs((str[1]%TA_RING_MAX_COUNT)-(str[0]%TA_RING_MAX_COUNT)))
#define ta_read_idx ta_idx[0]
#define ta_write_idx ta_idx[1]

static __attribute__((aligned(65536))) u32 ta_ring[TA_RING_MAX_COUNT][32/sizeof(u32)];

static volatile  __attribute__((aligned(128)))  u64 ta_idx[2];

static volatile bool ta_working=false;
static volatile bool call_pending=false;

static volatile void (*call_function)()=NULL;

using namespace TASplitter;

static u32 threaded_CurrentList=ListType_None; 

// send "Job done" irq immediately, and let the pvr thread handle it when it can :)
void threaded_ImmediateIRQ(u32 * data)
{
    Ta_Dma * td = (Ta_Dma*) data;
    
    switch (td->pcw.ParaType)
    {
        case ParamType_End_Of_List:

            if (threaded_CurrentList==ListType_None)
                threaded_CurrentList=td->pcw.ListType;
            
//            printf("RaiseInterrupt %d\n",threaded_CurrentList);
            params.RaiseInterrupt(ListEndInterrupt[threaded_CurrentList]);

			threaded_CurrentList=ListType_None;
            break;

        case ParamType_Sprite:
        case ParamType_Polygon_or_Modifier_Volume:

            if (threaded_CurrentList==ListType_None)
                threaded_CurrentList=td->pcw.ListType;
            
            break;
    }
}

u64 time_ta=0;

void threaded_wait(bool wait_ta_working)
{
    if(threaded_pvr)
    {
        while(call_pending) asm volatile("db16cyc");
        
        if(wait_ta_working)
            while(ta_working||ta_ring_count(ta_idx)>0) asm volatile("db16cyc");
    }
}

void threaded_call(void (*call)())
{
    threaded_wait(false);
    
    if(threaded_pvr)
    {
        call_function=(volatile void (*)())call;
        if((void*)call) call_pending=true;
    }
    else
    {
        if((void*)call) call();
    }
}

void threaded_TADma(u32* data,u32 size)
{
    if(threaded_pvr && size<TA_RING_MAX_COUNT)
    {
        verify(!((u32)data&0x1f));
        
        vector float v1,v2;
        
        u32 * lda=data;
        u32 lsi=size;
        u32 wi=ta_write_idx;

        while(size>TA_RING_MAX_COUNT-ta_ring_count(ta_idx)) asm volatile("db16cyc");
    
        while(lsi)
        {
            u64 * d =(u64*) ta_ring[wi%TA_RING_MAX_COUNT];
            u64 * s =(u64*) lda;
            
#if 0
            d[0]=s[0];d[1]=s[1];d[2]=s[2];d[3]=s[3];
#else
            LOAD_ALIGNED_VECTOR(v1,&s[0]);
            LOAD_ALIGNED_VECTOR(v2,&s[2]);
            STORE_ALIGNED_VECTOR(v1,&d[0]);
            STORE_ALIGNED_VECTOR(v2,&d[2]);
#endif

            ++wi;
            --lsi;
            lda+=8;
        }

        ta_write_idx+=size;
    }
    else
    {
        threaded_wait(true);
        TASplitter::Dma(data,size);
    }

    u32 * end=data+size*32/4;
    while(data<end)
    {
        threaded_ImmediateIRQ(data);
        data+=32/4;
    }
}

extern u64 time_pref;

void threaded_TASQ(u32* data)
{
    if(threaded_pvr)
    {
        vector float v1,v2;
        
        while(ta_ring_count(ta_idx)>=TA_RING_MAX_COUNT) asm volatile("db16cyc");
    
        u64 * d =(u64*) ta_ring[ta_write_idx%TA_RING_MAX_COUNT];
        u64 * s =(u64*) data;

        LOAD_ALIGNED_VECTOR(v1,&s[0]);
        LOAD_ALIGNED_VECTOR(v2,&s[2]);
        STORE_ALIGNED_VECTOR(v1,&d[0]);
        STORE_ALIGNED_VECTOR(v2,&d[2]);
        
        ++ta_write_idx;
    }
    else
    {
        threaded_wait(true);
    	TASplitter::SQ(data);
    }
    
    threaded_ImmediateIRQ(data);
}

static void threaded_task()
{
    u64  __attribute__((aligned(128))) lidx[2];
    vector float vt;
    
	while(running)
	{
        LOAD_ALIGNED_VECTOR(vt,ta_idx); // for atomicness
        STORE_ALIGNED_VECTOR(vt,lidx);

        if(ta_ring_count(lidx)!=0)
        {
            u32 ri=lidx[0]%TA_RING_MAX_COUNT;
            u32 rc=ta_ring_count(lidx);
            
            u32 chunk=min(rc,(TA_RING_MAX_COUNT-ri));
            u32 * chunk_start=ta_ring[ri];

            ta_working=true;

            TASplitter::Dma(chunk_start,chunk);            

            ta_read_idx+=chunk;
            
            ta_working=false;
        }
        else if(call_pending)
        {
            (*call_function)();
            call_pending=false;
		}
	}
}

static  __attribute__((aligned(256)))  u8 stack[0x100000];

void threaded_init()
{
	running=true;
    
    if (threaded_pvr)
        xenon_run_thread_task(2,&stack[sizeof(stack)-0x1000],(void*)threaded_task);
	
	atexit(threaded_term);
}

void threaded_term()
{
	running=false;
	while (xenon_is_thread_task_running(2));
}