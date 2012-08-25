#include "threadedPvr.h"

#include <stdio.h>
#include <stdlib.h>

#include <xenon_soc/xenon_power.h>
#include <ppc/atomic.h>

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

volatile bool do_render_pending=false;
volatile bool rend_end_render_call_pending=false;

extern volatile bool dmac_ch2_end_pending;

void threaded_TADma(u32* data,u32 size)
{
	while(ta_pending||ta_working||do_render_pending||rend_end_render_call_pending);

#if 1
    TASplitter::Dma(data,size);
    dmac_ch2_end_pending=true;
#else	
	verify(size*32<=TA_DMA_MAX_SIZE);
	
	u64 * d=(u64*)ta_data[ta_cur];
	
	memcpy(d,data,size*32);
	
	ta_size[ta_cur]=size;
	
	ta_pending=true;
#endif
}

extern u64 time_pref;

void threaded_TASQ(u32* data)
{
    if(threaded_pvr)
    {
        while(ta_pending||do_render_pending||rend_end_render_call_pending);

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
			
			ta_working=true;
			ta_pending=false;
			
			if (!size)
			{
				TASplitter::SQ(data);
			}
			else
			{
				TASplitter::Dma(data,size);
				dmac_ch2_end_pending=true;
			}

			ta_working=false;
		}
		
		if(do_render_pending){
				DoRender();
				do_render_pending=false;
		}

		if (rend_end_render_call_pending)
		{
			params.RaiseInterrupt(holly_RENDER_DONE);
			params.RaiseInterrupt(holly_RENDER_DONE_isp);
			params.RaiseInterrupt(holly_RENDER_DONE_vd);
			rend_end_render();
			render_end_pending=false;
			rend_end_render_call_pending=false;
		}
	}
}

static u8 stack[0x100000];

void threaded_init()
{
	running=true;
    
    if (threaded_pvr)
        xenon_run_thread_task(2,&stack[sizeof(stack)-0x100],(void*)threaded_task);
	
	atexit(threaded_term);
}

void threaded_term()
{
	running=false;
	while (xenon_is_thread_task_running(2));
}