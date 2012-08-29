#include "threadedPvr.h"

#include <stdio.h>
#include <stdlib.h>

#include <xenon_soc/xenon_power.h>

#include <ppc/xenonsprs.h>
#include <ppc/register.h>
#include <ppc/atomic.h>
#include <time/time.h>

#include "Renderer_if.h"
#include "ta.h"
#include "spg.h"

volatile bool threaded_pvr=true;

static volatile bool running=false;

#define TA_DMA_MAX_SIZE 1048576

static volatile __attribute__((aligned(128))) u32 ta_data[2][TA_DMA_MAX_SIZE/4];
static volatile int ta_size[2];
static volatile int ta_cur=0;

static volatile bool ta_pending=false;
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

void threaded_Call(void (*call)())
{
    if(threaded_pvr)
    {
       while(ta_pending||call_pending) asm volatile("db16cyc");
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
    if(threaded_pvr)
    {
        while(ta_pending||call_pending) asm volatile("db16cyc");
    }

    TASplitter::Dma(data,size);

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
        while(ta_pending||call_pending) asm volatile("db16cyc");

        u64 * d=(u64*)ta_data[ta_cur];
        u64 * s=(u64*)data;

        d[0]=s[0];
        d[1]=s[1];
        d[2]=s[2];
        d[3]=s[3];

        ta_size[ta_cur]=0;

        ta_pending=true;
    }
    else
    {
    	TASplitter::SQ(data);
    }
    
    threaded_ImmediateIRQ(data);
}

static void threaded_task()
{
	while(running)
	{
		if(ta_pending)
		{
            u32 * data=(u32*)ta_data[ta_cur];
			u32 size = ta_size[ta_cur];
			
			ta_cur=1-ta_cur;
			
    		ta_pending=false;
			
			if (!size)
			{
				TASplitter::SQ(data);
			}
			else
			{
				TASplitter::Dma(data,size);
			}
		}
        
		if(call_pending){
            (*call_function)();
            call_pending=false;
		}
	}
}

static u8 stack[0x100000];

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