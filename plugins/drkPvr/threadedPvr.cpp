#include "threadedPvr.h"

#include <stdio.h>
#include <stdlib.h>

#include <xenon_soc/xenon_power.h>
#include <ppc/atomic.h>

#include "Renderer_if.h"
#include "ta.h"
#include "spg.h"

static volatile bool running=false;

#define TA_DMA_MAX_SIZE 1048576

static __attribute__((aligned(128))) u32 ta_data[2][TA_DMA_MAX_SIZE/4];
static volatile int ta_cur=0;
static volatile bool ta_pending=false;

volatile bool do_render_pending=false;
volatile bool rend_end_render_call_pending=false;


void threaded_TADma(u32* data,u32 size)
{
	while(ta_pending);

	TASplitter::Dma((u32*)data,size);
	return;
}

extern u64 time_pref;

void threaded_TASQ(u32* data)
{
//	u64 prt=mftb();
	
	while(ta_pending);

	u64 * d=(u64*)ta_data[ta_cur];
	u64 * s=(u64*)data;
	
	d[0]=s[0];
	d[1]=s[1];
	d[2]=s[2];
	d[3]=s[3];
	
	ta_pending=true;

/*	prt=mftb()-prt;
	time_pref+=prt;*/
}

static void threaded_task()
{
	while(running)
	{
		if(ta_pending)
		{
			u32 * data=ta_data[ta_cur];
			
			ta_cur=1-ta_cur;
			ta_pending=false;
			
			TASplitter::SQ(data);
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
	xenon_run_thread_task(2,&stack[sizeof(stack)-0x100],(void*)threaded_task);
	
	atexit(threaded_term);
}

void threaded_term()
{
	running=false;
	while (xenon_is_thread_task_running(2));
}