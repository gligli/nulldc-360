
#include "types.h"

#include "rec_v1/blockmanager.h"

#include "sh4_interpreter.h"
#include "sh4_opcode_list.h"
#include "sh4_registers.h"
#include "sh4_if.h"
#include "dc/pvr/pvr_if.h"
#include "dc/aica/aica_if.h"
#include "dmac.h"
#include "dc/gdrom/gdrom_if.h"
#include "naomi/naomi.h"
#include "intc.h"
#include "tmu.h"
#include "dc/mem/sh4_mem.h"

#include <time.h>
#include <float.h>
#include <assert.h>

#include <xenon_soc/xenon_power.h>
#include <xenon_uart/xenon_uart.h>
#include <ppc/timebase.h>

#include "sh4r_rename.h"

#define CPU_TIMESLICE	(448)
#define CPU_RATIO		(8)

volatile bool threaded_subsystems=true;

//uh uh 
volatile bool  sh4_int_bCpuRun=false;

u32 exec_cycles=0;
time_t odtime=0;

u32 sh4_ex_ExeptionCode,sh4_ex_VectorAddress;
Sh4RegContext sh4_ex_SRC;


#define GetN(str) ((str>>8) & 0xf)
#define GetM(str) ((str>>4) & 0xf)
bool exept_was_dslot=false;

extern "C"{
void sh4_int_restore_reg_cnt()
{
	//restore reg context
	LoadSh4Regs(&sh4_ex_SRC);
	//fix certain registers that may need fixing
	if (mmu_error_TT!=MMU_TT_IREAD)
	{
		//if the error was on IREAD no need for any fixing :)
		//this should realy not raise any exeptions :p
		u16 op = IReadMem16(pc);
		u32 n = GetN(op);
		u32 m = GetM(op);
		switch(OpDesc[op]->ex_fixup)
		{
		case rn_4:
			{
				r[n]+=4;
			}
			break;
		case rn_fpu_4:
			{
				//dbgbreak;
				r[n]+=4;
				//8 byte fixup if on double mode -> actualy this catches the exeption at the first reg read/write so i gota be carefull
				if (fpscr.SZ)
					r[n]+=4;
			}
			break;

		case rn_opt_1:
			{
				if (n!=m)
					r[n]+=1;
				//else
				//	dbgbreak;
			}
			break;

		case rn_opt_2:
			{
				if (n!=m)
					r[n]+=2;
				//else
				//	dbgbreak;
			}
			break;

		case rn_opt_4:
			{
				if (n!=m)
					r[n]+=4;
				//else
				//	dbgbreak;
			}
			break;

		case fix_none:
			break;
		}

	}
	if (exept_was_dslot)
		pc-=2;
	exept_was_dslot=false;
	//raise exeption
	RaiseExeption(sh4_ex_ExeptionCode,sh4_ex_VectorAddress);
	sh4_exept_raised=false;
}

}

void naked sh4_int_exept_hook()
{
/*gli	__asm 
	{
		call sh4_int_restore_reg_cnt;
		jmp [sh4_exept_next];
	}
	*/
	
	assert(false);
	
	__asm
	(
		"b sh4_int_restore_reg_cnt \n"
		"lis %r0,sh4_exept_next@h \n"
		"ori %r0,%r0,sh4_exept_next@l \n"
		"lwz %r0,0(%r0) \n"
		"mtctr %r0 \n"
		"bctr \n"
	);
}

void __fastcall sh4_int_RaiseExeption(u32 ExeptionCode,u32 VectorAddress)
{
	if (sh4_exept_raised)
	{
		dlog("WARNING : DOUBLE EXEPTION RAISED , IGNORING SECOND EXEPTION\n");
		dbgbreak;
		return;
	}
	sh4_exept_raised=true;
	*sh4_exept_ssp=(u32)sh4_int_exept_hook;

	sh4_ex_ExeptionCode=ExeptionCode;
	sh4_ex_VectorAddress=VectorAddress;

	//save reg context
	SaveSh4Regs(&sh4_ex_SRC);
}

#define xstr(s) str(s)
#define str(s) #s

//interface
void Sh4_int_Run()
{
	sh4_int_bCpuRun=true;

#ifdef XENON
/*
	
	asm volatile(
		//for exeption rollback
		"lis	3,sh4_exept_ssp@h \n"
		"ori	3,3,sh4_exept_ssp@l \n"
		"subi	4,1,4 \n"
		"stwx	4,0,3 \n"
		//init vars
		"li		28," xstr(CPU_TIMESLICE) " \n"  //cycle count = max
		"lis	27,pc@h \n"
		"ori	27,27,pc@l \n"
		"lis	26,OpPtr@h \n"
		"ori	26,26,OpPtr@l \n"

		//loop start
		"i_mainloop: \n"
		
		"i_run_opcode: \n"
		
		"lwzx	3,0,27 \n"	
		"b		_vmem_ReadMem16 \n"       
		"slwi	4,3,2 \n"
//		"lwzx	4,4,26 \n"
//		"mtctr	4 \n"
//		"bctr \n"

//		"lwzx	3,0,27 \n"	
//		"addi	3,3,2 \n"
		"li		3,0x42 \n"
		"stwx	3,0,27 \n"

		"b	i_run_opcode \n"

		"subi	28,28," xstr(CPU_RATIO) " \n"
		"cmpwi	28,0 \n"
		"bgt	i_run_opcode \n"

		//exeption rollback point
		//if an exception happened, resume execution here
		"i_exept_rp: \n"
		//update system and run a new timeslice
		"li		4,0 \n"
		
		//Calculate next timeslice
		"lis	5,exec_cycles@h \n"
		"ori	5,5,exec_cycles@l \n"
		"lwzx	3,0,5 \n"
		"subf	28,3,28 \n"
		"addi	28,28," xstr(CPU_TIMESLICE) " \n"
		"stwx	4,0,5 \n"

		"b		UpdateSystem \n"

		//if cpu still on go for one more brust of opcodes :)
		"lis	5,sh4_int_bCpuRun@h \n"
		"ori	5,5,sh4_int_bCpuRun@l \n"
		"lwzx	3,0,5 \n"
		"cmplwi	3,0 \n"
		"bne	i_mainloop \n"
		::: "3","4","5","26","27","28"
	);
*/ 
	
	u32 * sp;
	asm volatile("mr %[stack],1 \n":[stack]"=r"(sp));
	
	sh4_exept_ssp=sp;
	sh4_exept_ssp--;
	sh4_exept_next=(u32*)&&i_exept_rp;
	
	int cycles=CPU_TIMESLICE;
	int op;
	
	do{
//		if (kbhit()){getch();printf("pc %08x\n",pc);};
		do{
			op=IReadMem16(pc);
			OpPtr[op](op);
			cycles-=CPU_RATIO;
			pc+=2;
		}while(cycles>0);

i_exept_rp:

		cycles-=exec_cycles;
		cycles+=CPU_TIMESLICE;
		exec_cycles=0;

		UpdateSystem();
	}while(sh4_int_bCpuRun);
	
#else
	__asm
	{
		//save regs used
		push esi;

		//for exeption rollback
		mov sh4_exept_ssp,esp;		//esp wont change after that :)
		sub sh4_exept_ssp,4;		//point to next stack item :)
		mov sh4_exept_next,offset i_exept_rp;

		//init vars
		mov esi,CPU_TIMESLICE;  //cycle count = max

		//loop start
i_mainloop:

		//run a single opcode -- doesn't use _any_ stack space :D
		{
i_run_opcode:
			mov ecx , pc;			//param #1 for readmem16
			call IReadMem16;		//ax has opcode to execute now
			movzx eax,ax;			//zero extend to 32b

			mov ecx,eax;			//ecx=opcode (param 1 to opcode handler)
			call OpPtr[eax*4];		//call opcode handler
			add pc,2;				//pc+=2 -> goto next opcode


			sub esi,CPU_RATIO;		//remove cycles from cycle count
			jns i_run_opcode;		//jump not (esi>0) , inner loop til timeslice is executed
		}

		//exeption rollback point
		//if an exception happened, resume execution here
i_exept_rp:
		//update system and run a new timeslice
		xor eax,eax;			//zero eax [used later]

		//Calculate next timeslice
		sub esi,exec_cycles;	//Add delayslot cycles
		add esi,CPU_TIMESLICE;

		mov exec_cycles,eax;	//zero out delayslot cycles

		//Call update system (cycle cnt is fixed to 448)
		call UpdateSystem;

		//if cpu still on go for one more brust of opcodes :)
		cmp  sh4_int_bCpuRun,0;
		jne i_mainloop;

i_exit_mainloop:
		//restore regs used
		pop esi;
	}
#endif
	
	sh4_int_bCpuRun=false;
}

void Sh4_int_Stop()
{
	if (sh4_int_bCpuRun)
	{
		sh4_int_bCpuRun=false;
	}
}

void Sh4_int_Step() 
{
	if (sh4_int_bCpuRun)
	{
		dlog("Sh4 Is running , can't step\n");
	}
	else
	{
		u32 op=ReadMem16(pc);
		ExecuteOpcode(op);
		pc+=2;
	}
}

void Sh4_int_Skip() 
{
	if (sh4_int_bCpuRun)
	{
		dlog("Sh4 Is running , can't Skip\n");
	}
	else
	{
		pc+=2;
	}
}

void Sh4_int_Reset(bool Manual) 
{
	if (sh4_int_bCpuRun)
	{
		dlog("Sh4 Is running , can't Reset\n");
	}
	else
	{
		pc = 0xA0000000;

		memset(r,0,sizeof(r));
		memset(r_bank,0,sizeof(r_bank));

		gbr=ssr=spc=sgr=dbr=vbr=0;
		mac.h=mac.l=pr=fpul=0;
		sh4r.zer_fpul=0;

		sr.SetFull(0x700000F0,true);
		old_sr=sr;
		UpdateSR();

		fpscr.full = 0x0004001;
		old_fpscr=fpscr;
		UpdateFPSCR();
		
		//Any more registers have default value ?
		dlog("Sh4 Reset\n");
		patchRB=0;
	}
}

//3584 Cycles
#define AICA_SAMPLE_GCM 441
#define AICA_SAMPLE_CYCLES (SH4_MAIN_CLOCK/(44100/AICA_SAMPLE_GCM))

void aica_periodical(u32 cycl);
void maple_periodical(u32 cycl);
void FASTCALL spgUpdatePvr(u32 cycles); // quicker to use direct plugin call

u32 aica_sample_cycles=0;

#include "ccn.h"

//General update
s32 rtc_cycles = 0;
u32 update_cnt = 0;

//typicaly, 446428 calls/second (448 cycles/call)
//fast update is 448 cycles
//medium update is 448*8=3584 cycles
//slow update is 448*16=7168  cycles

//14336 Cycles
int __fastcall VerySlowUpdate()
{
	rtc_cycles-=14336;
	if (rtc_cycles<=0)
	{
		rtc_cycles+=200*1000*1000;
		settings.dreamcast.RTC++;
	}
	//This is a patch for the DC LOOPBACK test GDROM (disables serial i/o)
	/*
	*(u16*)&mem_b.data[(0xC0196EC)& 0xFFFFFF] =9;
	*(u16*)&mem_b.data[(0xD0196D8+2)& 0xFFFFFF]=9;
	*/
	return FreeSuspendedBlocks();
}
//7168 Cycles
int __fastcall SlowUpdate()
{
    if (!(update_cnt&0x1f))
		return VerySlowUpdate();
    return 0;
}

void ThreadedUpdate()
{
   aica_sample_cycles+=3584*AICA_SAMPLE_GCM;

    if (aica_sample_cycles>=AICA_SAMPLE_CYCLES)
    {
        UpdateArm(512);
        UpdateAica(1);
        aica_sample_cycles-=AICA_SAMPLE_CYCLES;
    }

    aica_periodical(3584);

	libExtDevice.UpdateExtDevice(3584);

    #if DC_PLATFORM!=DC_PLATFORM_NAOMI
        UpdateGDRom();
    #else
        Update_naomi();
    #endif	
}

static volatile bool running=false;
static volatile bool update_pending=false;

static void threaded_task()
{
	while(running)
	{
		if(update_pending){
				ThreadedUpdate();
				update_pending=false;
		}
	}
}

static void threaded_term()
{
	running=false;
	while (xenon_is_thread_task_running(4));
}

void threaded_peripherals_wait()
{
    while(update_pending) asm volatile("db16cyc");
}

static  __attribute__((aligned(256))) u8 stack[0x100000];

void Sh4_int_Init() 
{
	BuildOpcodeTables();
	GenerateSinCos();
	dlog("Sh4 Init\n");

	running=true;
	if (threaded_subsystems)
        xenon_run_thread_task(4,&stack[sizeof(stack)-0x100],(void*)threaded_task);

    atexit(threaded_term);
}

void Sh4_int_Term() 
{
	threaded_term();
    
    Sh4_int_Stop();
	dlog("Sh4 Term\n");
}

bool Sh4_int_IsCpuRunning() 
{
	return sh4_int_bCpuRun;
}

u32 Sh4_int_GetRegister(Sh4RegType reg)
{
	if ((reg>=r0) && (reg<=r15))
	{
		return r[reg-r0];
	}
	else if ((reg>=r0_Bank) && (reg<=r7_Bank))
	{
		return r_bank[reg-r0_Bank];
	}
	else if ((reg>=fr_0) && (reg<=fr_15))
	{
		return fr_hex[reg-fr_0];
	}
	else if ((reg>=xf_0) && (reg<=xf_15))
	{
		return xf_hex[reg-xf_0];
	}
	else
	{
		printf("Sh4_int_GetRegister %08x\n",reg);
		switch(reg)
		{
		case reg_gbr :
			return gbr;
			break;
		case reg_vbr :
			return vbr;
			break;

		case reg_ssr :
			return ssr;
			break;

		case reg_spc :
			return spc;
			break;

		case reg_sgr :
			return sgr;
			break;

		case reg_dbr :
			return dbr;
			break;

		case reg_mach :
			return mac.h;
			break;

		case reg_macl :
			return mac.l;
			break;

		case reg_pr :
			return pr;
			break;

		case reg_fpul :
			return fpul;
			break;


		case reg_pc :
			return pc;
			break;

		case reg_sr :
			return sr.GetFull(true);
			break;
		case reg_fpscr :
			return fpscr.full;
			break;


		default:
			EMUERROR2("unknown register Id %d",reg);
			return 0;
			break;
		}
	}
}


void Sh4_int_SetRegister(Sh4RegType reg,u32 regdata)
{
	if (reg<=r15)
	{
		r[reg]=regdata;
	}
	else if (reg<=r7_Bank)
	{
		r_bank[reg-16]=regdata;
	}
	else
	{
		printf("Sh4_int_SetRegister %08x %08x\n",reg,regdata);
		switch(reg)
		{
		case reg_gbr :
			gbr=regdata;
			break;

		case reg_ssr :
			ssr=regdata;
			break;

		case reg_spc :
			spc=regdata;
			break;

		case reg_sgr :
			sgr=regdata;
			break;

		case reg_dbr :
			dbr=regdata;
			break;

		case reg_mach :
			mac.h=regdata;
			break;

		case reg_macl :
			mac.l=regdata;
			break;

		case reg_pr :
			pr=regdata;
			break;

		case reg_fpul :
			fpul=regdata;
			break;


		case reg_pc :
			pc=regdata;
			break;
		case reg_sr :
			sr.SetFull(regdata,true);
			UpdateSR();
			break;
		case reg_fpscr :
			fpscr.full=regdata;
			UpdateFPSCR();
			break;


		default:
			EMUERROR2("unknown register Id %d",reg);
			break;
		}
	}
}

//more coke .. err code

bool ExecuteDelayslot()
{
	exec_cycles+=CPU_RATIO;
	pc+=2;

	u32 op=IReadMem16(pc);

	if (sh4_exept_raised)
	{
		exept_was_dslot=true;
		return false;
	}

	//verify(sh4_exept_raised==false);
	if (op!=0)
		ExecuteOpcode(op);

	if(sh4_exept_raised)
	{
		exept_was_dslot=true;
		return false;
	}

	return true;
}
bool ExecuteDelayslot_RTE()
{
    exec_cycles+=CPU_RATIO;

	pc+=2;
	u32 op=IReadMem16(pc);
	sr.SetFull(ssr,true);
	bool rv=UpdateSR();
	verify(sh4_exept_raised==false);
	if (op!=0)
	ExecuteOpcode(op);
	verify(sh4_exept_raised==false);

	return rv;
}

int __fastcall MediumUpdate()
{
    if(threaded_subsystems)
    {
        threaded_peripherals_wait();
        update_pending=true;
    }
    else
    {
        ThreadedUpdate();
    }
    
	UpdateDMA();

   	maple_periodical(3584);

	if (!(update_cnt&0xf))
        return SlowUpdate();
    
    return 0;
}

u64 time_update_system=0;


//#define PROF_UPDATESYSTEM

extern "C" {
//448 Cycles
//as of 7/2/2k8 this is fixed to 448 cycles
int __attribute__((externally_visible)) __fastcall UpdateSystem()
{		
#ifdef PROF_UPDATESYSTEM	
	u64 ust=mftb();
#endif
    
    UpdateTMU(448);
    spgUpdatePvr(448);
    
    int rv=0;
    
	if (!(update_cnt&0x7))
		if(MediumUpdate())
            rv=-1;

	update_cnt++;

	if(UpdateINTC())
        rv=-1;
	
#ifdef PROF_UPDATESYSTEM	
	ust=mftb()-ust;
	time_update_system+=ust;
#endif
	
	return rv;
}

}