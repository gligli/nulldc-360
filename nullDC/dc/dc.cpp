//dc.cpp
//emulation driver - interface
#include "mem/sh4_mem.h"
#include "sh4/sh4_opcode_list.h"
#include "pvr/pvr_if.h"
#include "mem/sh4_internal_reg.h"
#include "aica/aica_if.h"
#include "maple/maple_if.h"
#include "dc.h"
#include "config/config.h"
#include "profiler/profiler.h"
#include <string.h>

#include <time/time.h>

void threaded_wait(bool wait_ta_working);
void threaded_peripherals_wait();

bool dc_inited=false;
bool dc_reseted=false;
bool dc_ingore_init=false;
bool dc_running=false;

void _Reset_DC(bool Manual);

//Init mainly means allocate
//Reset is called before first run
//Init is called olny once
//When Init is called , cpu interface and all plugins configurations myst be finished
//Plugins/Cpu core must not change after this call is made.
bool Init_DC()
{
	if (dc_inited)
		return true;

	if(settings.dynarec.Enable)
	{
		sh4_cpu=Get_Sh4Recompiler();
		dlog("Using Recompiler\n");
	}
	else
	{
		sh4_cpu=Get_Sh4Interpreter();
		dlog("Using Interpreter\n");
	}
	
	if (!plugins_Load())
		return false;

    if (!plugins_Init())
    { 
        //dlog("Emulation thread : Plugin init failed\n"); 	
        plugins_Term();
        return false;
    }
    
    sh4_cpu->Init();


    mem_Init();
    pvr_Init();
    aica_Init();
    mem_map_defualt();


	dc_inited=true;
	return true;
}
void _Reset_DC(bool Manual)
{
	plugins_Reset(Manual);
	sh4_cpu->Reset(Manual);
	mem_Reset(Manual);
	pvr_Reset(Manual);
	aica_Reset(Manual);
}
bool SoftReset_DC()
{
	if (sh4_cpu->IsCpuRunning())
	{
		sh4_cpu->Stop();
    	_Reset_DC(true);
		return true;
	}
	else
		return false;
}
bool Reset_DC(bool Manual)
{
	if (!dc_inited || sh4_cpu->IsCpuRunning())
		return false;

    _Reset_DC(Manual);

    //when we boot from ip.bin , it's nice to have it seted up
    sh4_cpu->SetRegister(reg_gbr,0x8c000000);
    sh4_cpu->SetRegister(reg_sr,0x700000F0);
    sh4_cpu->SetRegister(reg_fpscr,0x0004001);

	dc_reseted=true;
	return true;
}

void Term_DC()
{
	if (dc_inited)
	{
		Stop_DC();
        
        aica_Term();
        pvr_Term();
        mem_Term();
        sh4_cpu->Term();
        plugins_Term();

        Release_Sh4If(sh4_cpu);
        sh4_cpu=NULL;

		dc_inited=false;
        dc_reseted=false;
	}
}

void Start_DC()
{
    if (!dc_inited)
    {
        if (!Init_DC())
            return;
    }

	if (!sh4_cpu->IsCpuRunning())
	{
		if (!dc_reseted)
			Reset_DC(false);//hard reset kthx

		sh4_cpu->Run();
	}
}

void Stop_DC()
{
	if (dc_inited)//sh4_cpu may not be inited ;)
	{
        if (sh4_cpu->IsCpuRunning())
		{
            // threads must have ended their work before stopping
            threaded_wait(true);
            threaded_peripherals_wait();

			sh4_cpu->Stop();
		}
	}
}

bool IsDCInited()
{
	return dc_inited;
}
