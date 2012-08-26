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
static volatile bool ta_working=false;

static volatile bool start_render_call_pending=false;
static volatile bool end_render_call_pending=false;

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

void threaded_Wait(bool ta,bool render,bool set_srcp,bool set_ercp)
{
    if(threaded_pvr)
    {
       if (ta)
       {
           while(ta_working||ta_pending) asm volatile("db16cyc");
       }
       if (render)
       {
           while(start_render_call_pending||end_render_call_pending) asm volatile("db16cyc");
           start_render_call_pending|=set_srcp;
           end_render_call_pending|=set_ercp;
       }
    }
}

void threaded_TADma(u32* data,u32 size)
{
    if(threaded_pvr)
    {
        while(ta_pending) asm volatile("db16cyc");
        
        verify(size*32<=TA_DMA_MAX_SIZE);

        u64 * d=(u64*)ta_data[ta_cur];

        memcpy(d,data,size*32);

        ta_size[ta_cur]=size;

        ta_pending=true;
    }
    else
    {
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
        while(ta_pending) asm volatile("db16cyc");

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
			ta_working=true;

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

			ta_working=false;
		}
        
		if(start_render_call_pending){
            rend_start_render();
            start_render_call_pending=false;
		}

		if (end_render_call_pending)
		{
            params.RaiseInterrupt(holly_RENDER_DONE);
			params.RaiseInterrupt(holly_RENDER_DONE_isp);
			params.RaiseInterrupt(holly_RENDER_DONE_vd);
			rend_end_render();
			render_end_pending=false;
			end_render_call_pending=false;
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