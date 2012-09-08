#include "ppc_intregalloc.h"

#include "dc/sh4/rec_v1/driver.h"

/*
//implement register allocators on a class , so we can swap em around?
//methods needed
//
//DoAllocation		: do allocation on the block
//BeforeEmit		: generate any code needed before the main emittion begins (other register allocators may have emited code tho)
//BeforeTrail		: generate any code needed after the main emittion has ended (other register allocators may emit code after that tho)
//AfterTrail		: generate code after the native block end (after the ret) , can be used to emit helper functions (other register allocators may emit code after that tho)
//IsRegAllocated	: *couh* yea .. :P
//GetRegister		: Get the register , needs flag if it's read or write. Carefull w/ register state , we may need to implement state push/pop
//PushRegister		: push register to stack (if allocated)
//PopRegister		: pop register from stack (if allocated)
//FlushRegister		: write reg to reg location , and dealloc it
//WriteBackRegister	: write reg to reg location
//ReloadRegister	: read reg from reg location , discard old result

*/

u32 ssenoalloc=0;
u32 ssealloc=0;
ppc_gpr_reg reg_to_alloc[16]=
{
	R16,R17,R18,R19,R20,R21,R22,R23,R24,R25,R26,R27,R28,R29,R30,R31
};

#define REG_ALLOC_COUNT 16

//////////////////////////////////////////////////
// new reg alloc class							//
//////////////////////////////////////////////////
class SimpleGPRAlloc : public IntegerRegAllocator
{

	ppc_block* ppce;
	//helpers & misc shit
	struct RegAllocInfo
	{
		ppc_gpr_reg ppcreg;
		bool InReg;
		bool Dirty;
	};
	RegAllocInfo r_alloced[sh4_reg_count];
	struct sort_temp
	{
		int cnt;
		int reg;
		bool no_load;
	};

	//ebx, ebp, esi, and edi are preserved

	//Yay bubble sort
	void bubble_sort(sort_temp numbers[] , int array_size)
	{
		int i, j;
		sort_temp temp;
		for (i = (array_size - 1); i >= 0; i--)
		{
			for (j = 1; j <= i; j++)
			{
				if (numbers[j-1].cnt < numbers[j].cnt)
				{
					temp = numbers[j-1];
					numbers[j-1] = numbers[j];
					numbers[j] = temp;
				}
			}
		}
	}


	//check
	void checkvr(u32 reg)
	{
		//if (reg>=fr_0 && reg<=fr_15 )
		//	__asm int 3;
	//	if (reg>=xf_0 && reg<=xf_15 )
		//	__asm int 3; 
	}
	//Helper , olny internaly used now
	virtual void MarkDirty(u32 reg)
	{
		checkvr(reg);
		if (IsRegAllocated(reg))
		{
			r_alloced[reg].Dirty=true;
		}
	}
	//reg alloc interface :)
	//DoAllocation		: do allocation on the block
	virtual void DoAllocation(BasicBlock* block,ppc_block* ppce)
	{
		this->ppce=ppce;
		for (int i=0;i<sh4_reg_count;i++)
		{
			r_alloced[i].ppcreg=ERROR_REG;

			if (i<REG_ALLOC_COUNT)
				r_alloced[i].ppcreg=reg_to_alloc[i];
			else if (i==reg_pc)
				r_alloced[i].ppcreg=(ppc_reg)RPC;
			else if (i==reg_pr)
				r_alloced[i].ppcreg=(ppc_reg)RPR;
				
			r_alloced[i].InReg=true;
			r_alloced[i].Dirty=true;
		}
	}
	//BeforeEmit		: generate any code needed before the main emittion begins (other register allocators may have emited code tho)
	virtual void BeforeEmit()
	{
	}
	//BeforeTrail		: generate any code needed after the main emittion has ended (other register allocators may emit code after that tho)
	virtual void BeforeTrail()
	{
		for (int i=0;i<sh4_reg_count;i++)
		{
			if (IsRegAllocated(i))
			{
				GetRegister(R3,i,RA_DEFAULT);
			}
		}
	}
	//AfterTrail		: generate code after the native block end (after the ret) , can be used to emit helper functions (other register allocators may emit code after that tho)
	virtual void AfterTrail()
	{
	}
	//IsRegAllocated	: *couh* yea .. :P
	virtual bool IsRegAllocated(u32 sh4_reg)
	{
		checkvr(sh4_reg);
		return r_alloced[sh4_reg].ppcreg!=ERROR_REG;
	}
	//Carefull w/ register state , we may need to implement state push/pop
	//GetRegister		: Get the register , needs flag for mode
	virtual ppc_gpr_reg GetRegister(ppc_gpr_reg d_reg,u32 sh4_reg,u32 mode)
	{
		checkvr(sh4_reg);
		//No move , or RtR move + reload
		if (IsRegAllocated(sh4_reg))
		{
			if ( r_alloced[sh4_reg].InReg==false)
			{
				if ((mode & RA_NODATA)==0)
				{
					//if we must load , and its not loaded
					ppce->emitLoad32(r_alloced[sh4_reg].ppcreg,GetRegPtr(sh4_reg));
					
					r_alloced[sh4_reg].Dirty=false;//not dirty .ffs, we just loaded it....
				}
				else
				{
					r_alloced[sh4_reg].Dirty=true;//its dirty, we dint load data :)
				}
			}
			
			r_alloced[sh4_reg].InReg=true;

			//nada
			if (mode & RA_FORCE)
			{
				//move to forced reg , if needed
				if ((r_alloced[sh4_reg].ppcreg!=d_reg) && ((mode & RA_NODATA)==0))
					ppce->emitMoveRegister(d_reg,r_alloced[sh4_reg].ppcreg);
				return d_reg;
			}
			else
			{
				//return allocated reg
				return r_alloced[sh4_reg].ppcreg;
			}
		}
		else
		{
			//MtoR move , force has no effect (allways forced) ;)
			if (0==(mode & RA_NODATA))
			{
				ppce->emitLoad32(d_reg,GetRegPtr(sh4_reg));
			}
			return d_reg;
		}

		//yes it realy can't come here
//		__asm int 3;
		//return EAX;
	}
	//Save registers
	virtual void SaveRegister(u32 reg,ppc_gpr_reg from)
	{
		checkvr(reg);
		if (!IsRegAllocated(reg))
		{
			ppce->emitStore32(GetRegPtr(reg),from);
		}
		else
		{
			ppc_gpr_reg ppcreg=GetRegister(R3,reg,RA_NODATA);
			if (ppcreg!=from)
				ppce->emitMoveRegister(ppcreg,from);
		}
		MarkDirty(reg);
	}
	virtual void SaveRegister(u32 reg,u32 from)
	{
		checkvr(reg);
		if (!IsRegAllocated(reg))
		{
			ppce->emitLoadImmediate32(R3,from);
			ppce->emitStore32(GetRegPtr(reg),R3);
		}
		else
		{
			ppc_gpr_reg ppcreg=GetRegister(R3,reg,RA_NODATA);
			ppce->emitLoadImmediate32(ppcreg,from);
		}
		MarkDirty(reg);
	}
	virtual void SaveRegister(u32 reg,u32* from)
	{
		checkvr(reg);
		if (!IsRegAllocated(reg))
		{
			ppce->emitLoad32(R4,from);
			ppce->emitStore32(GetRegPtr(reg),R4);
		}
		else
		{
			ppc_gpr_reg ppcreg=GetRegister(R3,reg,RA_NODATA);
			ppce->emitLoad32(ppcreg,from);
		}
		MarkDirty(reg);
	}
	virtual void SaveRegister(u32 reg,s16* from)
	{
		checkvr(reg);
		if (!IsRegAllocated(reg))
		{
			ppce->emitLoad16(R4,from);
			EMIT_EXTSH(ppce,R4,R4);
			ppce->emitStore32(GetRegPtr(reg),R4);
		}
		else
		{
			ppc_gpr_reg ppcreg=GetRegister(R3,reg,RA_NODATA);
			ppce->emitLoad16(ppcreg,from);
			EMIT_EXTSH(ppce,ppcreg,ppcreg);
		}
		MarkDirty(reg);
	}
	virtual void SaveRegister(u32 reg,s8* from)
	{
		checkvr(reg);
		if (!IsRegAllocated(reg))
		{
			ppce->emitLoad8(R4,from);
			EMIT_EXTSB(ppce,R4,R4);
			ppce->emitStore32(GetRegPtr(reg),R4);
		}
		else
		{
			ppc_gpr_reg ppcreg=GetRegister(R3,reg,RA_NODATA);
			ppce->emitLoad8(ppcreg,from);
			EMIT_EXTSB(ppce,ppcreg,ppcreg);
		}
		MarkDirty(reg);
	}

	//FlushRegister		: write reg to reg location , and dealloc it
	virtual void FlushRegister(u32 reg)
	{
		checkvr(reg);
		if (IsRegAllocated(reg) && r_alloced[reg].InReg)
		{
			if (r_alloced[reg].Dirty)
			{
				ppce->emitStore32(GetRegPtr(reg),r_alloced[reg].ppcreg);
			}
			r_alloced[reg].InReg=false;
			r_alloced[reg].Dirty=false;
		}
	}
	//Flush all regs
	virtual void FlushRegCache()
	{
		for (int i=0;i<sh4_reg_count;i++)
		{
			FlushRegister(i);
		}
	}
	//WriteBackRegister	: write reg to reg location
	virtual void WriteBackRegister(u32 reg)
	{
		checkvr(reg);
		if (IsRegAllocated(reg) && r_alloced[reg].InReg && r_alloced[reg].Dirty)
		{
			r_alloced[reg].Dirty=false;
			ppce->emitStore32(GetRegPtr(reg),r_alloced[reg].ppcreg);
		}
	}
	//ReloadRegister	: read reg from reg location , discard old result
	virtual void ReloadRegister(u32 reg)
	{
		checkvr(reg);
		if (IsRegAllocated(reg) && r_alloced[reg].InReg)
		{
			//r_alloced[reg].Dirty=false;
			//ppce->Emit(op_mov32,r_alloced[reg].ppcreg,GetRegPtr(reg));
			r_alloced[reg].InReg=false;
		}
	}
};

IntegerRegAllocator* GetGPRtAllocator()
{
	return new SimpleGPRAlloc();
}