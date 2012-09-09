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
#include <malloc.h>

//uh uh 
volatile bool rec_sh4_int_bCpuRun=false;

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
u32 __attribute__((externally_visible)) old_esp,old_lr;

#define DR_STACK_SIZE 0x100000

u8 __attribute__((externally_visible,aligned(65536))) dr_stack[DR_STACK_SIZE];

u32 __attribute__((externally_visible)) Dynarec_Mainloop_no_update_fast;

#define xstr(s) str(s)
#define str(s) #s

f32 __attribute__((externally_visible)) float_zero=0.0f;
f32 __attribute__((externally_visible)) float_one=1.0f;

u64 time_lookup=0;

///////////////////////////////////////////////////////////////////////////////

#define DYNA_LOOKUP_SIZE (RAM_SIZE/2) // one sh4 op is 2 bytes
#define DYNA_LOOKUP_BYTE_SIZE (DYNA_LOOKUP_SIZE*4)

u32 * __attribute__((aligned(VM_USER_PAGE_SIZE))) sh4_pc_to_ppc=NULL; 
u32 __attribute__((aligned(VM_USER_PAGE_SIZE))) sh4_pc_to_ppc_full_lookup[VM_USER_PAGE_SIZE/sizeof(void*)];

extern "C" {

void __attribute__((naked)) DynaFullLookup()
{
    asm volatile (
		"mr 3," xstr(RPC) "						\n" 
		"stw 3,0(" xstr(RSH4R) ")               \n" //sh4r+0 is pc
        "li 4,0                                 \n"
		"bl FindCode_full						\n"
        "mtctr 3								\n"
//"bctr\n"
        "rlwinm 5," xstr(RPC) ",16,0x1FFF       \n"
        "cmplwi 5,0x0c00                        \n"
        "bltctr                                 \n"
        // make sure target is in recompiled code
        "lis 4,dyna_mem_pool@ha                  \n"
        "addi 4,4,dyna_mem_pool@l               \n"
        "cmplw 3,4                              \n"
        "bltctr                                 \n"
        "addis 4,4," xstr(DYNA_MEM_POOL_SIZE>>16) "\n"
        "cmplw 3,4                              \n"
        "bgectr                                 \n"
        // update lookup table
        "lis 5,sh4_pc_to_ppc@ha                 \n"
        "lwz 5,sh4_pc_to_ppc@l(5)               \n"
        "rlwinm 6," xstr(RPC) ",1,0x01fffffc    \n"
        "stwx 3,5,6                             \n"
        "bctr                                   \n"
    );

}

}

void DynaLookupMap(u32 start_pc, u32 end_pc, void* ppc_code)
{
    u32 i;
    
    if((start_pc&0x1FFFFFFF)<0x08000000 || (start_pc&0x1FFFFFFF)>=0x10000000)
        return;
    
//    printf("s %p e %p code %p\n",start_pc,end_pc,ppc_code);
    
    u32* addr=(u32*)(((start_pc<<1)&0x0FFFFFFC)|0x30000000);
    
    for(i=start_pc;i<=end_pc;i+=2)
    {
        sh4_pc_to_ppc[(i>>1)&(DYNA_LOOKUP_SIZE-1)]=(u32)ppc_code;

        verify(*addr==(u32)ppc_code);
        
        ++addr;
    }
}

void DynaLookupUnmap(u32 start_pc, u32 end_pc)
{
    DynaLookupMap(start_pc,end_pc,(void*)DynaFullLookup);
}

void DynaLookupReset()
{
    if(!sh4_pc_to_ppc)
        sh4_pc_to_ppc=(u32*)memalign(VM_USER_PAGE_SIZE,DYNA_LOOKUP_BYTE_SIZE);
    
    int i;
    for(i=0;i<DYNA_LOOKUP_SIZE;i+=4)
    {
        sh4_pc_to_ppc[i]=(u32)DynaFullLookup;
        sh4_pc_to_ppc[i+1]=(u32)DynaFullLookup;
        sh4_pc_to_ppc[i+2]=(u32)DynaFullLookup;
        sh4_pc_to_ppc[i+3]=(u32)DynaFullLookup;
    }
}

void DynaLookupInit()
{
    DynaLookupReset();
    
    u32 i;

    for(i=0;i<VM_USER_PAGE_SIZE/sizeof(void*);++i)
    {
        sh4_pc_to_ppc_full_lookup[i]=(u32)DynaFullLookup;
    }

    u32 virt=0x30000000;
    
    for(i=0;i<0x8000000;i+=VM_USER_PAGE_SIZE)
    {
        vm_create_user_mapping(virt,(u32)sh4_pc_to_ppc_full_lookup&0x7fffffff,VM_USER_PAGE_SIZE,VM_WIMG_CACHED_READ_ONLY);
        virt+=VM_USER_PAGE_SIZE;
    }


    for(i=0;i<4;++i)
    {
        vm_create_user_mapping(virt,(u32)sh4_pc_to_ppc&0x7fffffff,DYNA_LOOKUP_BYTE_SIZE,VM_WIMG_CACHED);
        virt+=DYNA_LOOKUP_BYTE_SIZE;
    }
    
    verify(virt==0x40000000);
}


///////////////////////////////////////////////////////////////////////////////

void __attribute__((naked)) DynaMainLoop()
{
	asm volatile (
        "mflr 4                                 \n"
        "lis 3,old_lr@ha						\n"
		"stw 4,old_lr@l(3)						\n"
		"lis 3,old_esp@ha						\n"
		"stw 1,old_esp@l(3)						\n"
        
        // own stack for dr
        "lis 1,dr_stack@ha                      \n"
        "addis 1,1," xstr(DR_STACK_SIZE>>16) "  \n"
        "subi 1,1,0x1000                        \n"
    
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

        "rlwinm 6," xstr(RPC) ",1,0x0ffffffc    \n"
        "oris 6,6,0x3000                        \n"
        "lwz 4,0(6)                             \n"
        "mtctr 4                                \n"
        "bctr                                   \n"

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

"end_of_mainloop:								\n"

        "lis 6,rec_cycles@ha					\n" 
		"stw " xstr(RCYCLES) ",rec_cycles@l(6)	\n"

        //exit from function
		"lis 6,old_esp@ha						\n"
		"lwz 1,old_esp@l(6)						\n"
		"lis 6,old_lr@ha						\n"
		"lwz 3,old_lr@l(6)						\n"
        "mtlr 3                                 \n"
        "blr                                    \n"
	);
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
    
    DynaLookupInit();
    
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

