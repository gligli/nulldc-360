#include "basicblock.h"
#include "dc/sh4/shil/compiler/shil_compiler_base.h"
#include "dc/mem/sh4_mem.h"
#include "dc/sh4/sh4_registers.h"
#include "emitter/emitter.h"
#include "emitter/regalloc/ppc_fpregalloc.h"
#include <memory>
#include "recompiler.h"
#include "dc/sh4/sh4_interpreter.h"
#include "dc/sh4/rec_v1/blockmanager.h"
#include "recompiler.h"
#include "analyser.h"
#include "dc/sh4/shil/shil_ce.h"
#include "driver.h"

void FASTCALL RewriteBasicBlock(CompiledBlockInfo* cBB);

//needed declarations
void bb_link_compile_inject_TF_stub(CompiledBlockInfo* ptr);
void bb_link_compile_inject_TT_stub(CompiledBlockInfo* ptr);
void RewriteBasicBlockCond(CompiledBlockInfo* cBB);


//Basic Block
bool BasicBlock::IsMemLocked(u32 adr)
{
	if (IsOnRam(adr)==false)
		return false;

	if (flags.ProtectionType==BLOCK_PROTECTIONTYPE_MANUAL)
		return false;

	//if block isnt on ram , then there's no way to tell if its locked (well , bios mem is allways locked :p)
	if (OnRam()==false)
		return false;

	verify(page_start()<=page_end());

	u32 adrP=GetRamPageFromAddress(adr);

	return (page_start() <=adrP) && (adrP<=page_end());
}

void BasicBlock::SetCompiledBlockInfo(CompiledBlockInfo* cBl)
{
	cBB= cBl;
	
	cBB->block_type.ProtectionType=flags.ProtectionType;

	cBB->start=start;
	cBB->end=end;
	cBB->cpu_mode_tag=flags.FpuMode;
	cBB->lookups=0;
	cBB->Discarded=false;
	cBB->tbp_ticks=0;

	cBB->TF_next_addr=TF_next_addr;
	cBB->TT_next_addr=TT_next_addr;

	cBB->TF_block=cBB->TT_block=0;
}
//BasicBlock compiler :D

void RewriteBasicBlockFixed(CompiledBlockInfo* cBB)
{
	verify(cBB->Rewrite.Type==2);
	u8 flags=0;
	if  (cBB->TF_block)
		flags=1;

	if (cBB->Rewrite.Last==flags)
		return;

	ppc_block* ppce = new ppc_block();

	ppce->Init(dyna_realloc,dyna_finalize);
	ppce->do_realloc=false;
	ppce->ppc_buff=(u8*)cBB->Code + cBB->Rewrite.Offset;
	ppce->ppc_size=32;

	cBB->Rewrite.Last=flags;

	if  (cBB->TF_block)
	{
		ppce->emitBranch((void*)cBB->TF_block->Code,0);
	}
	else
	{
		ppce->emitLoadImmediate32(R3,(u32)cBB);
		ppce->emitBranch((void*)bb_link_compile_inject_TF_stub,0);
	}
	ppce->Generate();

	delete ppce;
}
void RewriteBasicBlockCond(CompiledBlockInfo* cBB)
{
	verify(cBB->Rewrite.Type==1);
	if (cBB->Rewrite.Type==2)
	{
		RewriteBasicBlockFixed(cBB);
		return;
	}

	u8 flags=0;
	if (cBB->TT_block!=0)
		flags|=1;
	if (cBB->TF_block)
		flags|=2;

	if (cBB->Rewrite.Last==flags)
		return;

	ppc_block* ppce = new ppc_block();
	
	ppce->Init(dyna_realloc,dyna_finalize);
	ppce->do_realloc=false;
	ppce->ppc_buff=(u8*)cBB->Code + cBB->Rewrite.Offset;
	ppce->ppc_size=32;

	cBB->Rewrite.Last=flags;
	
//	printf("cBB->Rewrite.RCFlags %08x %d\n",cBB->Rewrite.RCFlags,flags);
	
	if (flags==1)
	{
		ppce->emitBranchConditional((void*)cBB->TT_block->Code,PPC_CC_F,CR_T_COND_FLAG,0);
		ppce->emitLoadImmediate32(R3,(u32)cBB);
		ppce->emitBranch((void*)bb_link_compile_inject_TF_stub,0);
	}
	else if  (flags==2)
	{
		ppce->emitBranchConditional((void*)cBB->TF_block->Code,PPC_CC_T,CR_T_COND_FLAG,0);
		ppce->emitLoadImmediate32(R3,(u32)cBB);
		ppce->emitBranch((void*)bb_link_compile_inject_TT_stub,0);
	}
	else  if  (flags==3)
	{
		ppce->emitBranchConditional((void*)cBB->TF_block->Code,PPC_CC_T,CR_T_COND_FLAG,0);
		ppce->emitBranch((void*)cBB->TT_block->Code,0);
	}
	else
	{
		ppce->emitLoadImmediate32(R3,(u32)cBB);
		ppce->emitBranchConditional((void*)bb_link_compile_inject_TF_stub,PPC_CC_T,CR_T_COND_FLAG,0);
		ppce->emitBranch((void*)bb_link_compile_inject_TT_stub,0);
	}
	ppce->Generate();

	delete ppce;
}

extern "C" { // called from asm

//Compile block and return pointer to it's code
void* __fastcall bb_link_compile_inject_TF(CompiledBlockInfo* ptr)
{
	CompiledBlockInfo* target= FindOrRecompileBlock(ptr->TF_next_addr);

	//if current block is Discared , we must not add any chain info , just jump to the new one :)
	if (ptr->Discarded==false)
	{
		//Add reference so we can undo the chain later
		target->AddRef(ptr);
		ptr->TF_block=target;
		ptr->pTF_next_addr=(void*)target->Code;
		if (ptr->Rewrite.Type)
			RewriteBasicBlock(ptr);
	}
	return (void*)target->Code;
}

void* __fastcall bb_link_compile_inject_TT(CompiledBlockInfo* ptr)
{
	CompiledBlockInfo* target= FindOrRecompileBlock(ptr->TT_next_addr);

	//if current block is Discared , we must not add any chain info , just jump to the new one :)
	if (ptr->Discarded==false)
	{
		//Add reference so we can undo the chain later
		target->AddRef(ptr);
		ptr->TT_block=target;
		ptr->pTT_next_addr=(void*)target->Code;
		if (ptr->Rewrite.Type)
			RewriteBasicBlock(ptr);
	}
	return (void*)target->Code;
} 

}

#if 0
//call link_compile_inject_TF , and jump to code
void bb_link_compile_inject_TF_stub(CompiledBlockInfo* ptr)
{
	((void(*)())bb_link_compile_inject_TF(ptr))();
}

void bb_link_compile_inject_TT_stub(CompiledBlockInfo* ptr)
{
	((void(*)())bb_link_compile_inject_TT(ptr))();
}
#else
//call link_compile_inject_TF , and jump to code
void __attribute__((naked)) bb_link_compile_inject_TF_stub(CompiledBlockInfo* ptr)
{
	asm volatile(
		"bl bb_link_compile_inject_TF				\n"
		"mtlr 3										\n"
		"blr										\n"
	);
}

void __attribute__((naked)) bb_link_compile_inject_TT_stub(CompiledBlockInfo* ptr)
{
	asm volatile(
		"bl bb_link_compile_inject_TT				\n"
		"mtlr 3										\n"
		"blr										\n"
	);
}
#endif

u32 ret_cache_hits=0;
u32 ret_cache_total=0;

#define RET_CACHE_PTR_MASK_AND (0xFFFFFFFF - (RET_CACHE_SZ)  )
#define RET_CACHE_PTR_MASK_OR ( RET_CACHE_SZ*2 )
#define RET_CACHE_STACK_OFFSET_A (RET_CACHE_SZ)
#define RET_CACHE_STACK_OFFSET_B (RET_CACHE_SZ+4)
/*__declspec(align(512)) //must be 16* size
struct 
{
	u32 waste[RET_CACHE_SIZE];//force top bit to 1
	CompiledBlockInfo* ptr[RET_CACHE_SIZE];//force top bit to 1,store ptrs here
	u32 data[RET_CACHE_SIZE*2];//entrys for addr (the *2 is to make sure align is right =P)
}ret_cache;

u32* call_ret_cache_ptr=ret_cache.data;
*/

ret_cache_entry* ret_cache_base;
//new idea : build the BRT on the stack
//
//
//......
//ESP+132 CBB ptr
//ESP+128 first address
//ESP+124 ..esp can be on these values , this space is 'NOT USED' , called functions can (And will) use it
//...
//ESP+0

CompiledBlockInfo* Curr_block;

//sp is 0 if manual discard
void CBBs_BlockSuspended(CompiledBlockInfo* block,u32* sp)
{/*
	u32* sp_inblock=block_stack_pointer-1;

	if(sp_inblock==sp)
	{
		//log("Exeption within the same block !\n");
	}
	else
	{
		if (sp!=0)
		{
			//log("Exeption possibly within the same block ; 0x%X\n",sp_inblock[-1]);
			//log("Block EP : 0x%X , sz : 0x%X\n",block->Code,block->size);
		}
	}
	*/
	if (ret_cache_base==0)
		return;
	for (int i=0;i<RET_CACHE_COUNT;i++)
	{
		if (ret_cache_base[i].cBB == block)
		{
			ret_cache_base[i].addr=0xFFFFFFFF;
			ret_cache_base[i].cBB=0;
		}
	}
}

extern "C" { // called from asm

void ret_cache_reset()
{
	printf("ret_cache_base %08x\n",ret_cache_base);
	if (ret_cache_base==0)
		return;
	for (int i=0;i<RET_CACHE_COUNT;i++)
	{
		ret_cache_base[i].addr=0xFFFFFFFF;
		ret_cache_base[i].cBB=0;
	}
}

void __fastcall CheckBlock(CompiledBlockInfo* block)
{
	verify(block->cpu_mode_tag==sh4r.fpscr.PR_SZ);
	//verify(block->size==pc);
	verify(block->Discarded==false);
}

void FASTCALL RewriteBasicBlockGuess_FLUT(CompiledBlockInfo* cBB)
{
	verify(cBB->Rewrite.Type==3);
	//indirect call , rewrite & link , second time(does fast look up)
	ppc_block* ppce = new ppc_block();

	cBB->Rewrite.RCFlags=2;
	ppce->Init(dyna_realloc,dyna_finalize);
	ppce->do_realloc=false;
	ppce->ppc_buff=(u8*)cBB->Code + cBB->Rewrite.Offset;
	ppce->ppc_size=32;

	ppce->emitBranch(Dynarec_Mainloop_no_update,0);
	
	ppce->Generate();
	delete ppce;
}

}

#if 0
//can corrupt anything apart esp
void RewriteBasicBlockGuess_FLUT_stub(CompiledBlockInfo* ptr)
{
	RewriteBasicBlockGuess_FLUT(ptr);
	
	u32 fx = *(u32*)Dynarec_Mainloop_do_update;
	
	((void(*)())fx)();
}
#else
void __attribute__((naked)) RewriteBasicBlockGuess_FLUT_stub(CompiledBlockInfo* ptr)
{
	asm volatile(
		"bl RewriteBasicBlockGuess_FLUT				\n"
		"lis 3,Dynarec_Mainloop_no_update@h			\n"
		"ori 3,3,Dynarec_Mainloop_no_update@l		\n"
		"lwz 3,0(3)									\n"
		"mtlr 3										\n"
		"blr										\n"
	);
}
#endif

extern "C" {

void* FASTCALL RewriteBasicBlockGuess_TTG(CompiledBlockInfo* cBB)
{
	verify(cBB->Rewrite.Type==3);
	//indirect call , rewrite & link , first time (hardlinks to target)
	CompiledBlockInfo*	new_block=FindOrRecompileBlock(sh4r.pc);

	if (cBB->Discarded)
	{
		return (void*)new_block->Code;
	}
	cBB->Rewrite.RCFlags=1;
	//Add reference so we can undo the chain later
	new_block->AddRef(cBB);
	cBB->TF_block=new_block;

	ppc_block* ppce = new ppc_block();

	ppce->Init(dyna_realloc,dyna_finalize);
	ppce->do_realloc=false;
	ppce->ppc_buff=(u8*)cBB->Code + cBB->Rewrite.Offset;
	ppce->ppc_size=64;

	cBB->TF_block=new_block;
#if 1
	ppce->emitLoad32(R3,GetRegPtr(reg_pc));
	ppce->emitLoadImmediate32(R4,sh4r.pc);
	EMIT_CMP(ppce,R3,R4,0);
	ppce->emitBranchConditional((void*)new_block->Code,PPC_CC_T,PPC_CC_ZER,0);
	ppce->emitLoadImmediate32(R3,(u32)cBB);
	ppce->emitBranch((void*)RewriteBasicBlockGuess_FLUT_stub,0);
#else
	ppce->emitLoadImmediate32(R3,(u32)cBB);
	ppce->emitBranch((void*)RewriteBasicBlockGuess_FLUT_stub,0);
#endif	
	verify(ppce->ppc_indx<=36);
	
	ppce->Generate();
	delete ppce;

	return (void*)new_block->Code;
}

}

#if 0
void RewriteBasicBlockGuess_TTG_stub(CompiledBlockInfo* ptr)
{
	((void(*)())RewriteBasicBlockGuess_TTG(ptr))();
}
#else
void __attribute__((naked)) RewriteBasicBlockGuess_TTG_stub(CompiledBlockInfo* ptr)
{
	asm volatile(
		"bl RewriteBasicBlockGuess_TTG				\n"
		"mtlr 3										\n"
		"blr										\n"
	);
}
#endif

//default behavior , calls _TTG rewrite
void FASTCALL RewriteBasicBlockGuess_NULL(CompiledBlockInfo* cBB)
{
	verify(cBB->Rewrite.Type==3);
	cBB->Rewrite.RCFlags=0;

	ppc_block* ppce = new ppc_block();

	ppce->Init(dyna_realloc,dyna_finalize);
	ppce->do_realloc=false;
	ppce->ppc_buff=(u8*)cBB->Code + cBB->Rewrite.Offset;
	ppce->ppc_size=32;
	ppce->emitLoadImmediate32(R3,(u32)cBB);
	ppce->emitBranch((void*)RewriteBasicBlockGuess_TTG_stub,0);
	ppce->Generate();
	delete ppce;
}
void FASTCALL RewriteBasicBlock(CompiledBlockInfo* cBB)
{
//	printf("RewriteBasicBlock %d %d\n",cBB->Rewrite.Type,cBB->Rewrite.RCFlags);
	if (cBB->Rewrite.Type==1)
		RewriteBasicBlockCond(cBB);
	else if (cBB->Rewrite.Type==2)
		RewriteBasicBlockFixed(cBB);
	else if (cBB->Rewrite.Type==3)
	{
		if (cBB->Rewrite.RCFlags==0)
			RewriteBasicBlockGuess_NULL(cBB);
		else if (cBB->Rewrite.RCFlags==1)
			RewriteBasicBlockGuess_TTG(cBB);
		else if (cBB->Rewrite.RCFlags==2)
			RewriteBasicBlockGuess_FLUT(cBB);
	}
}
#ifdef RET_CACHE_PROF
void naked ret_cache_misscall()
{
	__asm jmp [Dynarec_Mainloop_no_update];
}
#endif
void ret_cache_push(CompiledBlockInfo* cBB,ppc_block* ppce)
{
//	ppce->do_disasm=true;
	EMIT_ADDI(ppce,R1,R1,8); //add the ptr ;)
#if 0	
	ppce->emitLoadImmediate32(R3,RET_CACHE_PTR_MASK_AND);
	EMIT_AND(ppce,R1,R1,R3);
#else
	EMIT_RLWIMI(ppce,R1,R1,10,23,23);
#endif
	//Adress
	ppce->emitLoadImmediate32(R3,cBB->TT_next_addr);
	EMIT_STW(ppce,R3,RET_CACHE_STACK_OFFSET_A,R1);
	//Block
	ppce->emitLoadImmediate32(R3,(u32)cBB);
	EMIT_STW(ppce,R3,RET_CACHE_STACK_OFFSET_B,R1);
}
bool BasicBlock::Compile()
{
	FloatRegAllocator*		fra;
	IntegerRegAllocator*	ira;

	ppc_block* ppce=new ppc_block();
	
	ppce->Init(dyna_realloc,dyna_finalize);

	cBB=new CompiledBlockInfo();

	SetCompiledBlockInfo(cBB);

	/*
	//that is a realy nice debug helper :)
	ppce->Emit(op_mov32,&Curr_block,(u32)cBB);
	*/

	ppc_Label* block_begin = ppce->CreateLabel(true,0);
	ppc_Label* block_exit = ppce->CreateLabel(false,0);

	/*
	ppce->Emit(op_mov32,ECX,(u32)cBB);
	ppce->Emit(op_call,ppc_ptr_imm(CheckBlock));
	*/

	verify(cycles<0x10000);
	EMIT_ADDI(ppce,RCYCLES,RCYCLES,-cycles);
	EMIT_CMPI(ppce,RCYCLES,0,0);
	
/*	
	// debug tool
	ppce->emitLoadImmediate32(R3,(cBB->start&0x1FFFFFFF)|0x70000000);
	EMIT_LBZ(ppce,R3,0,R3);	
*/
	
	ppce->emitBranchConditionalToLabel(block_exit,0,PPC_CC_T,PPC_CC_NEG);

	if (flags.ProtectionType==BLOCK_PROTECTIONTYPE_MANUAL)
	{
#ifdef COUNT_BLOCK_LOCKTYPE_USAGE
		ppce->Emit(op_add32,&manbs,1);
#endif
		int sz=Size();
		verify(sz!=0);

		int i=0;
		
		//that can be optimised a lota :p
		ppc_Label* exit_discard_block= ppce->CreateLabel(false,0);
		ppc_Label* execute_block= ppce->CreateLabel(false,8);
		verify(sz!=0);

		while(sz>=4)
		{
			u32* pmem=(u32*)GetMemPtr(start+i,4);
//			printf("man %p %08x %d\n",pmem,*pmem,sz);
			ppce->emitLoad32(R3,pmem);
			ppce->emitLoadImmediate32(R4,*pmem);
			EMIT_CMPL(ppce,R3,R4,0);

			if (sz==4)
			{
				ppce->emitBranchConditionalToLabel(execute_block,0,PPC_CC_T,PPC_CC_ZER);
			}
			else
			{
				ppce->emitBranchConditionalToLabel(exit_discard_block,0,PPC_CC_F,PPC_CC_ZER);
			}
			i+=4;
			sz-=4;
		}
		if (sz>=2)
		{
			//die("lol");
			u16* pmem=(u16*)GetMemPtr(start+i,2);
//			printf("man %p %04x %d\n",pmem,*pmem,sz);
			ppce->emitLoad16(R3,pmem);
			EMIT_CMPLI(ppce,R3,*pmem,0);
			
			ppce->emitBranchConditionalToLabel(execute_block,0,PPC_CC_T,PPC_CC_ZER);

			i+=2;
			sz-=2;
		}
		verify(sz==0);

		ppce->MarkLabel(exit_discard_block);
		ppce->emitLoadImmediate32(R3,start);
		ppce->emitStore32(GetRegPtr(reg_pc),R3);
		ppce->emitLoadImmediate32(R3,(u32)cBB);
		ppce->emitBranch((void*)SuspendBlock,1);
		ppce->emitBranch((void*)Dynarec_Mainloop_no_update,0);
		ppce->MarkLabel(execute_block);
	}
#ifdef COUNT_BLOCK_LOCKTYPE_USAGE
	else
		ppce->Emit(op_add32,&lockbs,1);
#endif

	fra=GetFloatAllocator();
	ira=GetGPRtAllocator();
	
	ira->DoAllocation(this,ppce);
	fra->DoAllocation(this,ppce);

	ira->BeforeEmit();
	fra->BeforeEmit();

	
	shil_compiler_init(ppce,ira,fra);

	u32 list_sz=(u32)ilst.opcodes.size();
	
	for (u32 i=0;i<list_sz;i++)
	{
		shil_opcode* op=&ilst.opcodes[i];
		shil_compile(op);
	}

	ira->BeforeTrail();
	fra->BeforeTrail();

	//end block acording to block type :)
	cBB->Rewrite.Type=0;
	cBB->Rewrite.RCFlags=0;
	cBB->Rewrite.Last=0xFF;
	cBB->block_type.exit_type=flags.ExitType;

//	printf("flags.ExitType %d\n",flags.ExitType);
	switch(flags.ExitType)
	{
	case BLOCK_EXITTYPE_DYNAMIC_CALL:	//same as below , sets call guess
		{
			ret_cache_push(cBB,ppce);
		}
	case BLOCK_EXITTYPE_DYNAMIC:		//not guess 
		{
			cBB->Rewrite.Type=3;
			cBB->Rewrite.RCFlags=0;
			cBB->Rewrite.Offset=ppce->ppc_indx;
			ppce->emitLoadImmediate32(R3,(u32)cBB);
			ppce->emitBranch((void*)RewriteBasicBlockGuess_TTG_stub,0);
			u32 extrasz=36-(ppce->ppc_indx-cBB->Rewrite.Offset);
			for (u32 i=0;i<extrasz/4;i++)
				ppce->write32(PPC_NOP);
		}
		break;
	case BLOCK_EXITTYPE_RET:			//guess
		{
#ifdef RET_CACHE_PROF
			ppce->Emit(op_add32,ppc_ptr(&ret_cache_total),1);
#endif

			ira->GetRegister(R3,reg_pc,RA_FORCE);
			EMIT_LWZ(ppce,R4,RET_CACHE_STACK_OFFSET_A,R1);
			EMIT_CMP(ppce,R3,R4,0);
			
#ifndef RET_CACHE_PROF
			ppce->emitBranchConditional(Dynarec_Mainloop_no_update,PPC_CC_F,PPC_CC_ZER,0);
#else
			ppce->Emit(op_jne ,ppc_ptr_imm(ret_cache_misscall));
#endif

			//Get the block ptr
			EMIT_LWZ(ppce,R3,RET_CACHE_STACK_OFFSET_B,R1);

			EMIT_ADDI(ppce,R1,R1,-8);
			EMIT_RLWIMI(ppce,R1,R1,10,23,23);
			EMIT_ORI(ppce,R1,R1,RET_CACHE_PTR_MASK_OR);
			
#ifdef RET_CACHE_PROF
			ppce->Emit(op_add32,ppc_ptr(&ret_cache_hits),1);
#endif

			EMIT_LWZ(ppce,R4,offsetof(CompiledBlockInfo,pTT_next_addr),R3);
			EMIT_MTCTR(ppce,R4);
			EMIT_BCTR(ppce);
		}
		break;
	case BLOCK_EXITTYPE_COND:			//linkable
		{
			//ok , handle COND here :)
			//mem address
			u32 TT_a=cBB->TT_next_addr;
			u32 TF_a=cBB->TF_next_addr;
			
			if (TT_a==cBB->start)
			{
				cBB->TT_block=cBB;
			}
			else
			{
				cBB->TT_block=FindBlock(TT_a);
				if (cBB->TT_block)
					cBB->TT_block->AddRef(cBB);
			}

			if  (TF_a==cBB->start)
			{
				cBB->TF_block=cBB;
			}
			else
			{
				cBB->TF_block=FindBlock(TF_a);
				if (cBB->TF_block)
					cBB->TF_block->AddRef(cBB);
			}

			cBB->Rewrite.Type=1;
			cBB->Rewrite.Offset=ppce->ppc_indx;
			
			/* gli 8 ops max for cond rewrite */
			for(int i=0;i<8;++i) ppce->write32(PPC_NOP);
		} 
		break;
	case BLOCK_EXITTYPE_FIXED_CALL:		//same as below
		{
			ret_cache_push(cBB,ppce);
		}
	case BLOCK_EXITTYPE_FIXED:			//linkable
		{
			if (cBB->TF_next_addr==cBB->start)
			{
				log("Fast Link possible\n");
			}
			else
			{
				cBB->TF_block=FindBlock(cBB->TF_next_addr);
				if (cBB->TF_block)
					cBB->TF_block->AddRef(cBB);
			}

			cBB->Rewrite.Type=2;
			cBB->Rewrite.Offset=ppce->ppc_indx;
			//link to next block :
			ppce->emitLoadImmediate32(R3,(u32)cBB);
			ppce->emitBranch((u32*)&(cBB->pTF_next_addr),0);
		}
		break;
	case BLOCK_EXITTYPE_FIXED_CSC:		//forced lookup , possible state chainge
		{
			//ppce->Emit(op_int3);
			//We have to exit , as we gota do mode lookup :)
			//We also have to reset return cache to ensure its ok -> removed for now

			//call_ret_address=0xFFFFFFFF;
			//ppce->Emit(op_mov32 ,EBX,&call_ret_cache_ptr);
			//ppce->Emit(op_mov32,ppc_mrm(EBX),0xFFFFFFFF);

			//pcall_ret_address=0;
			//Good , now return to caller :)
			ppce->emitBranch((void*)Dynarec_Mainloop_no_update,0);
		}
		break;
	}

	ira->AfterTrail();
	fra->AfterTrail();

	ppce->MarkLabel(block_exit);

	EMIT_ADDI(ppce,RCYCLES,RCYCLES,cycles+CPU_TIMESLICE);
	
	ppce->emitLoadImmediate32(R3,start);
	ppce->emitStore32(GetRegPtr(reg_pc),R3);

	ppce->emitBranch((void*)UpdateSystem,1);
	EMIT_CMPI(ppce,R3,0,0);
	ppce->emitBranchConditionalToLabel(block_begin,0,PPC_CC_T,PPC_CC_ZER);
	
	ppce->emitBranch((void*)Dynarec_Mainloop_do_update,0);

	//apply roml patches and generate needed code
	apply_roml_patches();

	void* codeptr=ppce->Generate();//NOTE, codeptr can be 0 here </NOTE>
	
	cBB->Code=(BasicBlockEP*)codeptr;
	cBB->size=ppce->ppc_indx;

	//make it call the stubs , unless otherwise needed
	cBB->pTF_next_addr=(void*)bb_link_compile_inject_TF_stub;
	cBB->pTT_next_addr=(void*)bb_link_compile_inject_TT_stub;

	cBB->ppc_code_fixups=ppce->GetExterns();

	delete fra;
	delete ira;
	ppce->Free();
	delete ppce;
	
	if (codeptr==0)
		return false; // didnt manage to generate code
	//rewrite needs valid codeptr
	if (cBB->Rewrite.Type)
		RewriteBasicBlock(cBB);

	return true;
}

//
void BasicBlock::CalculateLockFlags()
{
	u32 addr=start;

	while(addr<end)
	{
		flags.ProtectionType |= GetPageInfo(addr).flags.ManualCheck;
		addr+=PAGE_SIZE;
	}
	//check the last one , it is possible to skip it on the above loop :)
	flags.ProtectionType |= GetPageInfo(end).flags.ManualCheck;
}
extern u32 CompiledSRCsz;

CompiledBlockInfo*  __fastcall CompileBasicBlock(u32 pc)
{

	CompiledBlockInfo* rv;
	BasicBlock* block=new BasicBlock();

	//scan code
	ScanCode(pc,block);
	//Fill block lock type info
	block->CalculateLockFlags();
	CompiledSRCsz+=block->Size();
	//analyse code [generate il/block type]
	AnalyseCode(block);
	//optimise
	shil_optimise_pass_ce_driver(block);
	//Compile code
	if (block->Compile())
		rv=block->cBB;
	else
		rv=0;

	delete block;
	
	//printf("rec pc %p\n",pc);
	
	return rv;
}


void FASTCALL RewriteBasicBlock(CompiledBlockInfo* cBB);
//CompiledBlockInfo Helper functions
void CompiledBlockInfo::ClearBlock(CompiledBlockInfo* block)
{
	if (TF_block==block)
	{
		TF_block=0;
		pTF_next_addr=(void*)bb_link_compile_inject_TF_stub;
		if (block_type.exit_type==BLOCK_EXITTYPE_DYNAMIC ||
			block_type.exit_type==BLOCK_EXITTYPE_DYNAMIC_CALL)
		{
			TF_next_addr=0xFFFFFFFF;
			Rewrite.RCFlags=0;
		}
		if (Rewrite.Type)
			RewriteBasicBlock(this);
	}

	if (TT_block==block)
	{
		TT_block=0;
		pTT_next_addr=(void*)bb_link_compile_inject_TT_stub;
		if (Rewrite.Type)
			RewriteBasicBlock(this);
	}
}

void CompiledBlockInfo::BlockWasSuspended(CompiledBlockInfo* block)
{
	for (u32 i=0;i<blocks_to_clear.size();i++)
	{
		if (blocks_to_clear[i]==block)
		{
			blocks_to_clear[i]=0;
		}
	}
}

void CompiledBlockInfo::AddRef(CompiledBlockInfo* block)
{
	verify(Discarded==false);
	//if we reference ourselfs we dont care ;) were suspended anyhow
	if (block !=this)
		blocks_to_clear.push_back(block);
}
void CompiledBlockInfo::Suspend()
{
	for (u32 i=0;i<blocks_to_clear.size();i++)
	{
		if (blocks_to_clear[i])
		{
			blocks_to_clear[i]->ClearBlock(this);
		}
	}
	blocks_to_clear.clear();

	if (TF_block)
		TF_block->BlockWasSuspended(this);

	if (TT_block)
		TT_block->BlockWasSuspended(this);

	//if we jump to another block , we have to re compile it :)
	Discarded=true;
}

void CompiledBlockInfo::Free()
{
		Code=0;	
		((ppc_block_externs*)ppc_code_fixups)->Free();
}