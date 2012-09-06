#include "types.h"

#include "driver.h"

#include "dc/sh4/sh4_interpreter.h"
#include "dc/sh4/sh4_opcode_list.h"
#include "dc/sh4/sh4_registers.h"
#include "dc/sh4/sh4_if.h"
#include "dc/pvr/pvr_if.h"
#include "dc/aica/aica_if.h"
#include "dc/gdrom/gdrom_if.h"
#include "dc/sh4/intc.h"
#include "dc/sh4/tmu.h"
#include "dc/mem/sh4_mem.h"
#include "dc/sh4/shil/shil_ce.h"

#include "emitter/emitter.h"

#include "recompiler.h"
#include "blockmanager.h"
#include "analyser.h"

#include <time.h>
#include <float.h>

#include <ppc/timebase.h>
extern "C"
{
#include <ppc/vm.h>
}

//uh uh 
volatile bool  rec_sh4_int_bCpuRun=false;
cThread* rec_sh4_int_thr_handle=0;

u32 rec_exec_cycles=0;
time_t rec_odtime=0;

u32 avg_rat=0;
u32 avg_rat_cnt=0;
u32 avg_bc=0;

u32 sb_count=0;
u32 nb_count=0;
u32 __attribute__((externally_visible)) rec_cycles=0;

void* __attribute__((externally_visible)) Dynarec_Mainloop_no_update;
void* __attribute__((externally_visible)) Dynarec_Mainloop_do_update;
void* __attribute__((externally_visible)) Dynarec_Mainloop_end;

void DynaPrintLinkStart();
void DynaPrintLinkEnd();
void DumpBlockMappings();
LARGE_INTEGER total_compile;
u32 CompiledSRCsz=0;
CompiledBlockInfo*  __fastcall CompileCode(u32 pc)
{
	LARGE_INTEGER comp_time_start,comp_time_end;
	CompiledBlockInfo* cblock=0;
	//bool reset_blocks;

	comp_time_start=mftb();
	{
		nb_count++;
		//cblock=AnalyseCodeSuperBlock(pc);
		if (cblock==0)
			cblock=CompileBasicBlock(pc);
		RegisterBlock(cblock);
	}
	comp_time_end=mftb();
	total_compile+=comp_time_end-comp_time_start;
	//compile code
	//return pointer
	if (!cblock)
	{
		_SuspendAllBlocks();
		log("Compile failed -- retrying\n");
		return CompileCode(pc);//retry
	}
	
	return cblock;
}

extern "C" { // called from asm
BasicBlockEP* __attribute__((externally_visible)) __fastcall CompileCodePtr()
{
	return CompileCode(sh4r.pc)->Code;
}
}

INLINE BasicBlockEP * __fastcall GetRecompiledCodePointer(u32 pc)
{
	return FindCode(pc);
}

CompiledBlockInfo* FindOrRecompileBlock(u32 pc)
{
	CompiledBlockInfo* cblock=FindBlock(pc);
	
	if (cblock)
		return cblock;
	else
		return CompileCode(pc);
}

void __attribute__((naked)) CompileAndRunCode()
{
#ifdef XENON
	asm volatile (
		"bl CompileCodePtr	\n"
		"mtctr 3			\n"
		"bctr				\n"
	);
#else
	__asm 
	{
		call CompileCodePtr;
		jmp eax;
	}
#endif
}

void __fastcall rec_sh4_int_RaiseExeption(u32 ExeptionCode,u32 VectorAddress)
{
}
void rec_sh4_ResetCache()
{
	SuspendAllBlocks();
}
//asm version
BasicBlockEP* __fastcall FindCode_full(u32 address,CompiledBlockInfo* fastblock);

extern u32 fast_lookups;
u32 __attribute__((externally_visible)) old_esp;
u32 __attribute__((externally_visible)) Dynarec_Mainloop_no_update_fast;

#define xstr(s) str(s)
#define str(s) #s

f32 __attribute__((externally_visible)) float_zero=0.0f;
f32 __attribute__((externally_visible)) float_one=1.0f;

u64 time_lookup=0;

void naked DynaMainLoop()
{
	asm volatile (
		"lis 3,old_esp@ha						\n"
		"stw 1,old_esp@l(3)						\n"

		// constant regs
		"lis 3,float_zero@ha					\n"
		"lfs " xstr(FRZERO) ",float_zero@l(3)	\n"
		"lis 3,float_one@ha						\n"
		"lfs " xstr(FRONE) ",float_one@l(3)		\n"
	
		//misc pointers needed
		"lis 3,block_stack_pointer@ha			\n"
		"stw 1,block_stack_pointer@l(3)			\n"
		"lis 4,no_update@h						\n"
		"ori 4,4,no_update@l					\n"
		"lis 3,Dynarec_Mainloop_no_update@ha	\n"
		"stw 4,Dynarec_Mainloop_no_update@l(3)	\n"
		"lis 3,Dynarec_Mainloop_no_update_fast@ha		\n"
		"stw 4,Dynarec_Mainloop_no_update_fast@l(3)		\n"
		"lis 4,do_update@h						\n"
		"ori 4,4,do_update@l					\n"
		"lis 3,Dynarec_Mainloop_do_update@ha	\n"
		"stw 4,Dynarec_Mainloop_do_update@l(3)	\n"
		"lis 4,end_of_mainloop@h				\n"
		"ori 4,4,end_of_mainloop@l				\n"
		"lis 3,Dynarec_Mainloop_end@ha			\n"
		"stw 4,Dynarec_Mainloop_end@l(3)		\n"

		//
//		"lis " xstr(RSH4R) ",sh4r@ha			\n"
        "lis " xstr(RSH4R) ",0x7406             \n"
        
		"lwz " xstr(RPC) ",0(" xstr(RSH4R) ")	\n"//sh4r+0 is pc
	
		//Max cycle count :)
		"li " xstr(RCYCLES) "," xstr(CPU_TIMESLICE*9/10)		"\n"
		"lis 3,rec_cycles@ha					\n" 
		"stw " xstr(RCYCLES) ",rec_cycles@l(3)					\n"

		"b no_update							\n"

		//some utility code :)

		//partial , faster lookup. When we know the mode hasnt changed :)

		".align 4								\n"
"no_update:										\n"
		/*
		//called if no update is needed
		
		call GetRecompiledCodePointer;
		*/
		
		/*
		#define LOOKUP_HASH_SIZE	0x4000
		#define LOOKUP_HASH_MASK	(LOOKUP_HASH_SIZE-1)
		#define GetLookupHash(addr) ((addr>>2)&LOOKUP_HASH_MASK)

		*/
		/*
		CompiledBlockInfo* fastblock;
		fastblock=BlockLookupGuess[GetLookupHash(address)];
		*/
		
		"rlwinm 4," xstr(RPC) ",0," xstr(LOOKUP_HASH_MASK<<2)	"\n"
		"lis 5,BlockLookupGuess@h				\n"
		"ori 5,5,BlockLookupGuess@l				\n"
		"lwzx 4,5,4								\n"
	
		"lwz 5,4(" xstr(RSH4R) ")   			\n" //sh4r+4 is fpscr
		"lwz 6,0(4)								\n"
		"cmp 0,6," xstr(RPC) "					\n"
		"bne full_lookup						\n"

/*gli usefull?
		"rlwinm 5,5,32-19,30,31					\n"
	
		"lwz 6,12(4)							\n"
		"cmp 0,6,5								\n"
		"bne full_lookup						\n"
				
		"lwz 6,16(4)							\n"
		"addi 6,6,1								\n"
		"stw 6,16(4)							\n"
*/

		"lwz 3,8(4)								\n"
		"mtctr 3								\n"
		"bctr									\n"
		/*
		else
		{
			return FindCode_full(address,fastblock);
		}*/
"full_lookup:									\n"
		"stw " xstr(RPC) ",0(" xstr(RSH4R) ")	\n" //sh4r+0 is pc
		"mr 3," xstr(RPC) "						\n"

		"bl FindCode_full						\n"
		"mtctr 3								\n"
		"bctr									\n"

		".align 4								\n"
"do_update:										\n"
		"mflr " xstr(RPC) "                     \n" // RPC is only used as a temp here
        "bl UpdateSystem						\n"
		"mtlr " xstr(RPC) "                     \n"
		"cmplwi 3,0                             \n"
        "beqlr                                  \n"
	
		"lwz " xstr(RPC) ",0(" xstr(RSH4R) ")	\n" //sh4r+0 is pc
		
		//check for exit
		"lis 6,rec_sh4_int_bCpuRun@ha			\n"
		"lwz 6,rec_sh4_int_bCpuRun@l(6)			\n"
		"cmpwi 6,0								\n"
		"bne no_update							\n"

		//exit from function
		"lis 6,old_esp@ha						\n"
		"lwz 1,old_esp@l(6)						\n"
"end_of_mainloop:								\n"
		"lis 6,rec_cycles@ha					\n" 
		"stw " xstr(RCYCLES) ",rec_cycles@l(6)	\n"
		"blr									\n"
	::: "15","16","17","18","19","20","21","22","23","24","25","26","27","28","29","30","31"
		,"%fr16","%fr17","%fr18","%fr19","%fr20","%fr21","%fr22","%fr23","%fr24","%fr25","%fr26","%fr27","%fr28","%fr29","%fr30","%fr31");
}

u64 time_dr_start=0;

//interface
void rec_Sh4_int_Run()
{
	rec_sh4_int_bCpuRun=true;
	rec_cycles=0;
	SetFloatStatusReg();
	time_dr_start=mftb();
	DynaMainLoop();
	printf("################### out of DynaMainLoop\n");
}

void rec_Sh4_int_Stop()
{
	if (rec_sh4_int_bCpuRun)
	{
		rec_sh4_int_bCpuRun=false;
	}
}

void rec_Sh4_int_Step() 
{
	if (rec_sh4_int_bCpuRun)
	{
		log("recSh4 Is running , can't step\n");
	}
	else
	{
		u32 op=ReadMem16(sh4r.pc);
		ExecuteOpcode(op);
		sh4r.pc+=2;
	}
}

void rec_Sh4_int_Skip() 
{
	if (rec_sh4_int_bCpuRun)
	{
		log("recSh4 Is running , can't Skip\n");
	}
	else
	{
		sh4r.pc+=2;
	}
}

void rec_Sh4_int_Reset(bool Manual) 
{
	if (rec_sh4_int_bCpuRun)
	{
		log("Sh4 Is running , can't Reset\n");
	}
	else
	{
		Sh4_int_Reset(Manual);

		ResetAnalyser();
		ResetBlockManager();
		
		//Any more registers have default value ?
		log("recSh4 Reset\n");
	}
}

void rec_Sh4_int_Init() 
{
	//InitHash();
	Sh4_int_Init();
	InitAnalyser();
	InitBlockManager();

	ResetAnalyser();
	ResetBlockManager();

    vm_create_user_mapping(0x74060000,((u32)&sh4r)&~0x80000000,VM_USER_PAGE_SIZE,VM_WIMG_CACHED);
    
	log("recSh4 Init\n");
}

void rec_Sh4_int_Term() 
{
	TermBlockManager();
	TermAnalyser();
	Sh4_int_Term();
	log("recSh4 Term\n");
}

bool rec_Sh4_int_IsCpuRunning() 
{
	return rec_sh4_int_bCpuRun;
}
