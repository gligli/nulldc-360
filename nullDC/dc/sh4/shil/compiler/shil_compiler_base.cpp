//#include "shil_compile_slow.h"

#include "types.h"
#include "dc/sh4/shil/shil.h"
#include <assert.h>
#include "emitter/emitter.h"
#include "emitter/3DMath.h"

#include "dc/sh4/shil/shil_ce.h"
#include "dc/sh4/sh4_registers.h"
#include "dc/sh4/rec_v1/blockmanager.h"
#include "dc/sh4/rec_v1/driver.h"
#include "dc/sh4/sh4_opcode_list.h"
#include "dc/mem/sh4_mem.h"
#include "emitter/regalloc/ppc_fpregalloc.h"

#include "shil_compiler_base.h"

//Current arch (19.8.07) :
//Single compilation unit, single branching with backpatching, including indirect calls
//Static reg alloc, seperate for fpu/cpu (fpu is disabled too , atm)
//uh. thats it ?

FloatRegAllocator*		fra;
IntegerRegAllocator*	ira;
vector<roml_patch> roml_patch_list;

ppc_block* ppce;

typedef void __fastcall shil_compileFP(shil_opcode* op);

bool inited=false;

//[R|W][sz][M/F][addr]
__attribute__((aligned(128)))
void* mio_pvt[2][4][2][8];

/*
int fallbacks=0;
int native=0;
*/
u32 T_jcond_value;

u32 reg_pc_temp_value;


union _Cmp64
{
	struct
	{
		u32 l;
		u32 h;
	};
	u64 v;
};

//more helpers
//ensure mode is 32b (for floating point)
void c_Ensure32()
{
	assert(sh4r.fpscr.PR==0);
}
//emit a call to c_Ensure32 
bool Ensure32()
{
	//gli ppce->emitBranch((void*)c_Ensure32,1);
	return true;
}

//Register managment related
//Get a pointer to a reg
u32* GetRegPtr(u32 reg)
{
	if (reg==reg_pc_temp)
		return &reg_pc_temp_value;

	u32* rv=Sh4_int_GetRegisterPtr((Sh4RegType)reg);
	assert(rv!=0);
	return rv;
}
u32* GetRegPtr(Sh4RegType reg)
{
	return GetRegPtr((u8)reg);
}
bool IsFpuReg(u32 reg)
{
	return (reg >=fr_0 && reg<=xf_15);
}
//FPU !!! YESH
u32 IsInFReg(u32 reg)
{
	if (IsFpuReg(reg))
	{
		if (fra->IsRegAllocated(reg))
			return 1;
	}
	return 0;
}

void FlushDouble(u32 reg)
{
	if(IsInFReg(reg))
	{
		fra->FlushRegister(reg);
		fra->FlushRegister(reg+1);
	}
}
void ReloadDouble(u32 reg)
{
	if(IsInFReg(reg))
	{
		fra->ReloadRegister(reg);
		fra->ReloadRegister(reg+1);
	}
}

//REGISTER ALLOCATION
#define LoadReg(to,reg) ira->GetRegister(to,reg,RA_DEFAULT)
#define LoadReg_force(to,reg) ira->GetRegister(to,reg,RA_FORCE)
#define LoadReg_nodata(to,reg) ira->GetRegister(to,reg,RA_NODATA)
#define SaveReg(reg,from)	ira->SaveRegister(reg,from)


#define EMIT_SET_RDRARB(ppc,rd,ra,rb,inv,rc) {			\
	if(inv){											\
		PPC_SET_RD(ppc,ra);PPC_SET_RA(ppc,rd);			\
	}else{												\
		PPC_SET_RD(ppc,rd);PPC_SET_RA(ppc,ra);			\
	}													\
	if (rc){											\
		PPC_SET_RC(ppc,rb);								\
	}else{												\
		PPC_SET_RB(ppc,rb);								\
	}													\
	ppce->write32(ppc);									\
}


//Common opcode handling code
//reg to reg
void fastcall op_reg_to_reg(shil_opcode* op,PowerPC_instr ppc, PowerPC_instr ppc_imm, bool invRDRA, bool sub, bool carry)
{
//	ppce->do_disasm=true;
	assert(FLAG_32==(op->flags & 3));
	assert(0==(op->flags & (FLAG_IMM2)));
	assert(op->flags & FLAG_REG1);
	if (op->flags & FLAG_IMM1)
	{
		assert(0==(op->flags & FLAG_REG2));
		if (ira->IsRegAllocated(op->reg1))
		{
			ppc_gpr_reg r1 = LoadReg(R3,op->reg1);
			assert(r1!=R3);
			
			if(carry)
			{
				EMIT_BC(ppce,2,0,0,PPC_CC_F,CR_T_FLAG);
				EMIT_ADDI(ppce,r1,r1,1);
			}
			
			if(ppc_imm && (s32)op->imm1>=-32768 && (s32)op->imm1<=32767)
			{
				PPC_SET_RD(ppc_imm,r1);
				PPC_SET_RA(ppc_imm,r1);
				PPC_SET_IMMED(ppc_imm,op->imm1*(sub?-1:1));
				ppce->write32(ppc_imm);
			}
			else
			{
				ppce->emitLoadImmediate32(R4,op->imm1);
				EMIT_SET_RDRARB(ppc,r1,R4,r1,invRDRA,0);
			}

			SaveReg(op->reg1,r1);

			if(sub && op->imm1==1) // lousy test for DT
			{
				EMIT_CMPI(ppce,r1,0,0);
			}
		}
		else
		{
			/*ppce-> _ItM_ (GetRegPtr(op->reg1),op->imm1);*/
			u32 * ptr = GetRegPtr(op->reg1);
			ppce->emitLoad32(R4,ptr);

			if(carry)
			{
				EMIT_BC(ppce,2,0,0,PPC_CC_F,CR_T_FLAG);
				EMIT_ADDI(ppce,R4,R4,1);
			}
			
			if(ppc_imm && (s32)op->imm1>=-32768 && (s32)op->imm1<=32767)
			{
				PPC_SET_RD(ppc_imm,R4);
				PPC_SET_RA(ppc_imm,R4);
				PPC_SET_IMMED(ppc_imm,op->imm1*(sub?-1:1));
				ppce->write32(ppc_imm);
			}
			else
			{
				ppce->emitLoadImmediate32(R3,op->imm1);
				EMIT_SET_RDRARB(ppc,R4,R3,R4,invRDRA,0);
			}

			ppce->emitStore32(ptr,R4);

			if(sub && op->imm1==1) // lousy test for DT
			{
				EMIT_CMPI(ppce,R4,0,0);
			}
		}
	}
	else
	{
		assert(op->flags & FLAG_REG2);
		if (ira->IsRegAllocated(op->reg1))
		{
			ppc_gpr_reg r1 = LoadReg(R3,op->reg1);
			assert(r1!=R3);
			
			if(carry)
			{
				EMIT_BC(ppce,2,0,0,PPC_CC_F,CR_T_FLAG);
				EMIT_ADDI(ppce,r1,r1,1);
			}
			
			if (ira->IsRegAllocated(op->reg2))
			{
				ppc_gpr_reg r2 = LoadReg(R3,op->reg2);
				assert(r2!=R3);
				EMIT_SET_RDRARB(ppc,r1,r2,r1,invRDRA,0);
			}
			else
			{
				ppce->emitLoad32(R4,GetRegPtr(op->reg2));
				EMIT_SET_RDRARB(ppc,r1,R4,r1,invRDRA,0);
			}
			SaveReg(op->reg1,r1);
		}
		else
		{
			ppc_gpr_reg r2 = LoadReg(R3,op->reg2);

			u32 * ptr = GetRegPtr(op->reg1);
			ppce->emitLoad32(R4,ptr);

			if(carry)
			{
				EMIT_BC(ppce,2,0,0,PPC_CC_F,CR_T_FLAG);
				EMIT_ADDI(ppce,R4,R4,1);
			}
			
			EMIT_SET_RDRARB(ppc,R4,r2,R4,invRDRA,0);
			ppce->emitStore32(ptr,R4);
		}
	}
}

//imm to reg
void fastcall op_imm_to_reg(shil_opcode* op,PowerPC_instr ppc, bool mask, bool right)
{
	assert(FLAG_32==(op->flags & 3));
	assert(op->flags & FLAG_IMM1);
	assert(op->flags & FLAG_IMM2);
	assert(op->flags & FLAG_REG1);
	assert(0==(op->flags & FLAG_REG2));
    
    ppc_gpr_reg r1=LoadReg(R3,op->reg1);
		
    if(op->imm2) // to T flag?
    {
        EMIT_RLWINM(ppce,R5,r1,right?0:1,31,31);
        EMIT_CMPLI(ppce,R5,1,0);
    }

    PPC_SET_RD(ppc,r1);
    PPC_SET_RA(ppc,r1);
    PPC_SET_SH(ppc,((right && mask)?32-op->imm1:op->imm1));
    if(mask)
    {
        if(right)
        {
            PPC_SET_MB(ppc,op->imm1);
            PPC_SET_ME(ppc,31);
        }
        else
        {
            PPC_SET_MB(ppc,0);
            PPC_SET_ME(ppc,(31-op->imm1));
        }
    }
    ppce->write32(ppc);

    SaveReg(op->reg1,r1);
}


void op_reg_rot(ppc_gpr_reg r1, int rot, bool carry)
{
	if(rot)
	{
		EMIT_RLWINM(ppce,R5,r1,(rot==2)?1:0,31,31);
		EMIT_CMPLI(ppce,R5,1,0);

		if(carry)
		{
			EMIT_BC(ppce,3,0,0,PPC_CC_T,CR_T_FLAG);
			if(rot==2)
			{
				EMIT_RLWINM(ppce,r1,r1,0,1,31);
			}
			else
			{
				EMIT_RLWINM(ppce,r1,r1,0,0,30);
			}
			EMIT_B(ppce,2,0,0);
			if(rot==2)
			{
				EMIT_ORIS(ppce,r1,r1,0x8000);
			}
			else
			{
				EMIT_ORI(ppce,r1,r1,1);
			}
		}
	}
}

//reg
void fastcall op_reg(shil_opcode* op,PowerPC_instr ppc, bool setRB, int rot, bool carry) // rot: 0=!rot, 1=left, 2=right
{
	assert(FLAG_32==(op->flags & 3));
	assert(0==(op->flags & FLAG_IMM1));
	assert(0==(op->flags & (FLAG_IMM2)));
	assert(op->flags & FLAG_REG1);
	assert(0==(op->flags & FLAG_REG2));
	if (ira->IsRegAllocated(op->reg1))
	{
		ppc_gpr_reg r1=LoadReg(R3,op->reg1);
		assert(r1!=R3);
		
		PPC_SET_RD(ppc,r1);
		PPC_SET_RA(ppc,r1);
		if (setRB) PPC_SET_RB(ppc,r1);
		ppce->write32(ppc);
		
		op_reg_rot(r1,rot,carry);
		
		SaveReg(op->reg1,r1);
	}
	else
	{
		u32 * ptr = GetRegPtr(op->reg1);
		ppce->emitLoad32(R4,ptr);
		
		PPC_SET_RD(ppc,R4);
		PPC_SET_RA(ppc,R4);
		if (setRB) PPC_SET_RB(ppc,R4);
		ppce->write32(ppc);

		op_reg_rot(R4,rot,carry);
		
		ppce->emitStore32(ptr,R4);
	}
}

//_vmem for dynarec ;)
//on dynarec we have 3 cases for input , mem , reg , const
//and 3 cases of output , mem , reg , xmm [fpu reg]
//this will also support 64b mem/xmm reads

//handler tables
extern _vmem_ReadMem8FP*		_vmem_RF8[0x1000];
extern _vmem_WriteMem8FP*		_vmem_WF8[0x1000];

extern _vmem_ReadMem16FP*		_vmem_RF16[0x1000];
extern _vmem_WriteMem16FP*		_vmem_WF16[0x1000];

extern _vmem_ReadMem32FP*		_vmem_RF32[0x1000];
extern _vmem_WriteMem32FP*		_vmem_WF32[0x1000];
//extern void* _vmem_MemInfo[0x10000];

bool _vmem_translate(u32 addr,unat& entry_or_fptr);

bool nvmem_GetPointer(void* &ptr,u32 addr,u32 rw,u32 sz)
{
	void* p_RWF_table=NULL;
	if (rw==0)
	{
		if (sz==FLAG_8)
			p_RWF_table=&_vmem_RF8[0];
		else if (sz==FLAG_16)
			p_RWF_table=&_vmem_RF16[0];
		else if (sz==FLAG_32)
			p_RWF_table=&_vmem_RF32[0];
		else if (sz==FLAG_64)
			p_RWF_table=&_vmem_RF32[0];
		else
			die("invalid read size");
	}
	else
	{
		if (sz==FLAG_8)
			p_RWF_table=&_vmem_WF8[0];
		else if (sz==FLAG_16)
			p_RWF_table=&_vmem_WF16[0];
		else if (sz==FLAG_32)
			p_RWF_table=&_vmem_WF32[0];
		else if (sz==FLAG_64)
			p_RWF_table=&_vmem_WF32[0];
		else
			die("invalid write size");
	}

	unat tptr = 0;

	if (_vmem_translate(addr,tptr))
	{
		ptr=((void**)p_RWF_table)[tptr >> 2];	//direct value, no need to derefernce
		return false;
	}
	//else
	{
		ptr=(void*)tptr;				//pointer, needs to be readed
		return true;
	}
}

//sz is 1,2,4,8
void emit_vmem_op_compat_const(ppc_block* ppce,u32 rw,u32 sz,u32 addr,u32 reg)
{
//	printf("emit_vmem_op_compat_const %d %d %08x %d\n",rw,sz,addr,reg);
	
	if (sz==FLAG_64)
	{
		reg=GetSingleFromDouble((u8)reg);
		FlushDouble(reg);
	}

	void* ptr;
	if (nvmem_GetPointer(ptr,addr,rw,sz))
	{
		/*
		this needs more checks before beeing implemented :)
		if (sh4_ram_alt!=0 && IsOnRam(addr))
		{
			ptr=sh4_ram_alt+(addr&RAM_MASK);
		}*/
		//direct access
		switch(sz)
		{
		case FLAG_8:
			if (rw==0)
			{
				ira->SaveRegister(reg,(s8*)((u32)ptr^3));
			}
			else
			{
				ppc_reg rs= ira->GetRegister(R5,reg,RA_DEFAULT);
				ppce->emitStore8((s8*)((u32)ptr^3),rs);
			}
			break;

		case FLAG_16:
			if (rw==0)
			{
				ira->SaveRegister(reg,(s16*)((u32)ptr^2));
			}
			else
			{
				ppc_reg rs= ira->GetRegister(R5,reg,RA_DEFAULT);//any reg will do
				ppce->emitStore16((s16*)((u32)ptr^2),rs);
			}
			break;

		case FLAG_32:
			if (rw==0)
			{
				if (IsFpuReg(reg))
				{
					fra->SaveRegister(reg,(float*)ptr);
				}
				else
				{
					ira->SaveRegister(reg,(u32*)ptr);
				}
			}
			else
			{
				if (IsFpuReg(reg))
				{
					ppc_reg rs= fra->GetRegister(FR0,reg,RA_DEFAULT);
					ppce->emitStoreFloat(ptr,rs);
				}
				else
				{
					ppc_reg rs= ira->GetRegister(R4,reg,RA_DEFAULT);//any reg will do
					ppce->emitStore32(ptr,rs);
				}
			}
			break;
		case FLAG_64:
			verify(IsFpuReg(reg));
			
			if (rw==0)
			{
				ppce->emitLoadDouble(FR0,(u32*)ptr);
				ppce->emitStoreDouble(GetRegPtr(reg),FR0);
			}
			else
			{
				ppce->emitLoadDouble(FR0,GetRegPtr(reg));
				ppce->emitStoreDouble((u32*)ptr,FR0);
			}
			break;
		}
	}
	else
	{
		bool doloop=false;//sz==FLAG_64;

		do
		{
			doloop=doloop ^(sz==FLAG_64); 
			//gota call functions
			ppce->emitLoadImmediate32(R3,addr);



			if (rw==1)
			{
				if (IsFpuReg(reg))
				{
					fra->LoadRegisterGPR(R4,reg);
				}
				else
				{
					ira->GetRegister(R4,reg,RA_FORCE);//any reg will do
				}
			}
			
	/*		ppce->emitDebugValue(rw);
			ppce->emitDebugReg(R3);
			if(rw==1) ppce->emitDebugReg(R4);*/
			
			//call the function
			ppce->emitBranch(ptr,1);
			
//			if(rw==0) ppce->emitDebugReg(R3);

			if(rw==0)
			{
				switch(sz)
				{
				case FLAG_8:
					{
					ppc_reg r=ira->GetRegister(R3,reg,RA_NODATA);
					EMIT_EXTSB(ppce,r,R3);
					ira->SaveRegister(reg,r);
					}
					break;
				case FLAG_16:
					{
					ppc_reg r=ira->GetRegister(R3,reg,RA_NODATA);
					EMIT_EXTSH(ppce,r,R3);
					ira->SaveRegister(reg,r);
					}
					break;
				case FLAG_32:
					if (IsFpuReg(reg))
					{
						fra->SaveRegisterGPR(reg,R3);
					}
					else
					{
						ira->SaveRegister(reg,R3);
					}
					break;
				case FLAG_64:
					fra->SaveRegisterGPR(reg,R3);
					break;
				}
			}

			//in case we loop -- its 64 bit read
			reg++;
			addr+=4;
		}
		while(doloop);
	}
}

//#define REG_ALLOC_COUNT			 (shil_compile_slow_settings.RegAllocCount)
//#define REG_ALLOC_X86			 (shil_compile_slow_settings.RegAllocX86)
//#define REG_ALLOC_XMM			 (shil_compile_slow_settings.RegAllocXMM)
/*
#define INLINE_MEM_READ_CONST   (shil_compile_slow_settings.InlineMemRead_const)
#define INLINE_MEM_READ			(shil_compile_slow_settings.InlineMemRead)
#define INLINE_MEM_WRITE		(shil_compile_slow_settings.InlineMemWrite)
*/




//shil compilation
//shil opcode handlers

//mov32/64 reg,reg/imm
void __fastcall shil_compile_mov(shil_opcode* op)
{
	u32 size=op->flags&3;
	assert(op->flags & FLAG_REG1);//reg1 has to be used on mov :)
	
	if (op->reg1==op->reg2)
			return;

	if (size==FLAG_32)
	{
		//sse_WBF(op->reg2);//write back possibly readed reg
		//OP_RegToReg_simple(MOV32);
		//sse_RLF(op->reg1);//reload writen reg
		#define mov_flag_GRP_1 1
		#define mov_flag_GRP_2 2
		#define mov_flag_XMM_1 4
		#define mov_flag_XMM_2 8
		#define mov_flag_M32_1 0
		#define mov_flag_M32_2 0
		#define mov_flag_imm_2 16

		#define mov_flag_T_1   32
		#define mov_flag_T_2   64

		u32 flags = 0;

		if (op->flags & FLAG_IMM1)
			flags|=mov_flag_imm_2;

		if (IsInFReg(op->reg1))
			flags|=mov_flag_XMM_1;
		
		if (((op->flags & FLAG_IMM1)==0) && IsInFReg(op->reg2))
			flags|=mov_flag_XMM_2;

		if (IsFpuReg(op->reg1)==false && ira->IsRegAllocated(op->reg1))
			flags|=mov_flag_GRP_1;

		if (((op->flags & FLAG_IMM1)==0) && IsFpuReg(op->reg2)==false && ira->IsRegAllocated(op->reg2))
			flags|=mov_flag_GRP_2;

		if (op->reg1==reg_sr_T)
		{
			flags&=~mov_flag_GRP_1;
			flags|=mov_flag_T_1;
		}

		if (op->reg2==reg_sr_T)
		{
			flags&=~mov_flag_GRP_2;
			flags|=mov_flag_T_2;
		}

		#define XMMtoXMM (mov_flag_XMM_1 | mov_flag_XMM_2)
		#define XMMtoGPR (mov_flag_GRP_1 | mov_flag_XMM_2)
		#define XMMtoM32 (mov_flag_M32_1 | mov_flag_XMM_2)
		
		#define GPRtoXMM (mov_flag_XMM_1 | mov_flag_GRP_2)
		#define GPRtoGPR (mov_flag_GRP_1 | mov_flag_GRP_2)
		#define GPRtoM32 (mov_flag_M32_1 | mov_flag_GRP_2)

		#define M32toXMM (mov_flag_XMM_1 | mov_flag_M32_2)
		#define M32toGPR (mov_flag_GRP_1 | mov_flag_M32_2)
		#define M32toM32 (mov_flag_M32_1 | mov_flag_M32_2)

		#define IMMtoXMM (mov_flag_XMM_1 | mov_flag_imm_2)
		#define IMMtoGPR (mov_flag_GRP_1 | mov_flag_imm_2)
		#define IMMtoM32 (mov_flag_M32_1 | mov_flag_imm_2)

		#define IMMtoT   (mov_flag_T_1   | mov_flag_imm_2)
		#define TtoGPR   (mov_flag_GRP_1 | mov_flag_T_2  )
		#define TtoM32   (mov_flag_M32_1 | mov_flag_T_2  )

		ppc_fpr_reg fpr1=ERROR_REG;
		ppc_fpr_reg fpr2=ERROR_REG;
		if (flags & mov_flag_XMM_1)
		{
			fpr1=fra->GetRegister(FR0,op->reg1,RA_NODATA);
			assert(fpr1!=FR0);
		}

		if (flags & mov_flag_XMM_2)
		{
			fpr2=fra->GetRegister(FR0,op->reg2,RA_DEFAULT);
			assert(fpr2!=FR0);
		}

		ppc_gpr_reg gpr1=ERROR_REG;
		ppc_gpr_reg gpr2=ERROR_REG;

		if (flags & mov_flag_GRP_1)
		{
			gpr1=ira->GetRegister(R4,op->reg1,RA_NODATA);
			assert(gpr1!=R4);
		}

		if (flags & mov_flag_GRP_2)
		{
			gpr2=ira->GetRegister(R4,op->reg2,RA_DEFAULT);
			assert(gpr2!=R4);
		}

//		printf("shil_compile_mov %x\n",flags);
		switch(flags)
		{
		case XMMtoXMM:
			{
				EMIT_FMR(ppce,fpr1,fpr2);
				fra->SaveRegister(op->reg1,fpr1);
			}
			break;
		case XMMtoGPR:
			{
				//write back to mem location
				ppce->emitStoreFloat(GetRegPtr(op->reg1),fpr2);
				//mark that the register has to be reloaded from there
				ira->ReloadRegister(op->reg1);
			}
			break;
		case XMMtoM32:
			{
				//copy to mem location
				ppce->emitStoreFloat(GetRegPtr(op->reg1),fpr2);
			}
			break;

		case GPRtoXMM:		
			{
				//write back to ram
				fra->SaveRegisterGPR(op->reg1,gpr2);
			}
			break;
		case GPRtoGPR:
			{
				ppce->emitMoveRegister(gpr1,gpr2);
				ira->SaveRegister(op->reg1,gpr1);
			}
			break;
		case GPRtoM32:
			{
				//copy to ram
				ppce->emitStore32(GetRegPtr(op->reg1),gpr2);
			}
			break;
		case M32toXMM:
			{
				ppce->emitLoadFloat(fpr1,GetRegPtr(op->reg2));
				fra->SaveRegister(op->reg1,fpr1);
			}
			break;
		case M32toGPR:
			{
				ppce->emitLoad32(gpr1,GetRegPtr(op->reg2));
				ira->SaveRegister(op->reg1,gpr1);
			}
			break;
		case M32toM32:
			{
				ppce->emitLoad32(R4,GetRegPtr(op->reg2));
				ppce->emitStore32(GetRegPtr(op->reg1),R4);
			}
			break;
		case IMMtoXMM:
			{
				if(op->imm1==0x00000000) // 0.0f
				{
					EMIT_FMR(ppce,fpr1,FRZERO);
					fra->SaveRegister(op->reg1,fpr1);
				}
				else if(op->imm1==0x3f800000) // 1.0f
				{
					EMIT_FMR(ppce,fpr1,FRONE);
					fra->SaveRegister(op->reg1,fpr1);
				}
				else
				{
					//log("impossible mov IMMtoXMM [%X]\n",flags);
					//__asm int 3;
					//write back to ram
					ppce->emitLoadImmediate32(R4,op->imm1);
					fra->SaveRegisterGPR(op->reg1,R4);
				}
			}
			break;

		case IMMtoGPR:
			{
				ppce->emitLoadImmediate32(gpr1,op->imm1);
				ira->SaveRegister(op->reg1,gpr1);
			}
			break;

		case IMMtoM32:
			{
				ppce->emitLoadImmediate32(R4,op->imm1);
				ppce->emitStore32(GetRegPtr(op->reg1),R4);
			}
			break;
		
		case IMMtoT:
			{
				ppce->emitLoadImmediate32(R4,op->imm1);
				EMIT_CMPLI(ppce,R4,0,0);
				EMIT_CRNOR(ppce,CR_T_FLAG,PPC_CC_ZER,PPC_CC_ZER);
			}
			break;

		case TtoGPR:
			{
				EMIT_CROR(ppce,PPC_CC_ZER,CR_T_FLAG,CR_T_FLAG);
				EMIT_LI(ppce,R4,1);
				EMIT_BC(ppce,2,0,0,PPC_CC_T,PPC_CC_ZER);
				EMIT_LI(ppce,R4,0);
				ira->SaveRegister(op->reg1,R4);
			}
		case TtoM32:
			{
				EMIT_CROR(ppce,PPC_CC_ZER,CR_T_FLAG,CR_T_FLAG);
				EMIT_LI(ppce,R4,1);
				EMIT_BC(ppce,2,0,0,PPC_CC_T,PPC_CC_ZER);
				EMIT_LI(ppce,R4,0);
				ppce->emitStore32(GetRegPtr(op->reg1),R4);
			}
			break;

		default:
			log("unknown mov %X\n",flags);
			asm volatile ("sc");
			break;
		}
	}
	else
	{
		assert(size==FLAG_64);//32 or 64 b
		assert(0==(op->flags & (FLAG_IMM1|FLAG_IMM2)));//no imm can be used
		//log("mov64 not supported\n");
		u8 dest=GetSingleFromDouble((u8)op->reg1);
		u8 source=GetSingleFromDouble((u8)op->reg2);

		FlushDouble(source);
		
		ppce->emitLoad32(R4,GetRegPtr(source));
		ppce->emitLoad32(R3,GetRegPtr(source+1));
//		ppce->Emit(op_movlps,FR0,GetRegPtr(source));

		ppce->emitStore32(GetRegPtr(dest),R4);
		ppce->emitStore32(GetRegPtr(dest+1),R3);
//		ppce->Emit(op_movlps,GetRegPtr(dest),FR0);

		ReloadDouble(dest);
	}
}

//movex8/16 reg32,zx/sx reg8/16
void __fastcall shil_compile_movex(shil_opcode* op)
{
	u32 size=op->flags&3;
	assert(op->flags & (FLAG_REG1|FLAG_REG2));	//reg1 , reg2 has to be used on movex :)
	assert((size!=FLAG_8)||(size!=FLAG_16));	//olny 8 or 16 bits can be extended

	if (size==FLAG_8)
	{//8 bit
		if (op->flags & FLAG_SX)
		{//SX 8
			ppc_gpr_reg r2= LoadReg_force(R4,op->reg2);
			ppc_gpr_reg r1= LoadReg_nodata(R3,op->reg1);//if same reg (so data is needed) that is done by the above op
			EMIT_EXTSB(ppce,r1,r2);
			SaveReg(op->reg1,r1);
		}
		else
		{//ZX 8
			ppc_gpr_reg r2= LoadReg_force(R4,op->reg2);
			ppc_gpr_reg r1= LoadReg_nodata(R3,op->reg1);//if same reg (so data is needed) that is done by the above op
			EMIT_RLWINM(ppce,r1,r2,0,24,31);
			SaveReg(op->reg1,r1);
		}
	}
	else
	{//16 bit
		if (op->flags & FLAG_SX)
		{//SX 16
			ppc_gpr_reg r1;
			if (op->reg1!=op->reg2)
				r1= LoadReg_nodata(R3,op->reg1);	//get a spare reg , or the allocated one. Data will be overwriten
			else
				r1= LoadReg(R3,op->reg1);	//get or alocate reg 1 , load data b/c it's gona be used

			if (ira->IsRegAllocated(op->reg2))
			{
				ppc_gpr_reg r2= LoadReg(R4,op->reg2);
				assert(r2!=R4);//reg 2 must be allocated
				EMIT_EXTSH(ppce,r1,r2);
			}
			else
			{
				ppce->emitLoad32(R4,(u16*)GetRegPtr(op->reg2));
				EMIT_EXTSH(ppce,r1,R4);
			}
			SaveReg(op->reg1,r1);	//ensure it is saved
		}
		else
		{//ZX 16
			ppc_gpr_reg r1;
			if (op->reg1!=op->reg2)
				r1= LoadReg_nodata(R3,op->reg1);	//get a spare reg , or the allocated one. Data will be overwriten
			else
				r1= LoadReg(R3,op->reg1);	//get or alocate reg 1 , load data b/c it's gona be used

			if (ira->IsRegAllocated(op->reg2))
			{
				ppc_gpr_reg r2= LoadReg(R4,op->reg2);
				assert(r2!=R4);//reg 2 must be allocated
				EMIT_RLWINM(ppce,r1,r2,0,16,31);
			}
			else
			{
				ppce->emitLoad32(R4,(u16*)GetRegPtr(op->reg2));
				EMIT_RLWINM(ppce,r1,R4,0,16,31);
			}
			SaveReg(op->reg1,r1);	//ensure it is saved
		}
	}
}

//shift based
//swap8/16 reg32
void __fastcall shil_compile_swap(shil_opcode* op)
{
	u32 size=op->flags&3;
	
	assert(0==(op->flags & (FLAG_IMM1|FLAG_IMM2)));//no imms
	assert(op->flags & FLAG_REG1);//reg1
	assert(0==(op->flags & FLAG_REG2));//reg2

	if (size==FLAG_8)
	{
		ppc_gpr_reg r1 = LoadReg(R3,op->reg1);
        ppce->emitMoveRegister(R5,r1);
        EMIT_RLWIMI(ppce,r1,R5,24,24,31);
		EMIT_RLWIMI(ppce,r1,R5,8,16,23);		
		SaveReg(op->reg1,r1);
	}
	else
	{
		assert(size==FLAG_16);//has to be 16 bit
		ppc_gpr_reg r1 = LoadReg(R3,op->reg1);
		EMIT_RLWINM(ppce,r1,r1,16,0,31);
		SaveReg(op->reg1,r1);
	}
}

//shl reg32,imm , set flags
void __fastcall shil_compile_shl(shil_opcode* op)
{
	PowerPC_instr ppc;
	GEN_RLWINM(ppc,0,0,0,0,0);
	op_imm_to_reg(op,ppc,true,false);
}
//shr reg32,imm , set flags
void __fastcall shil_compile_shr(shil_opcode* op)
{
	PowerPC_instr ppc;
	GEN_RLWINM(ppc,0,0,0,0,0);
	op_imm_to_reg(op,ppc,true,true);
}

//shld reg32,reg32
void __fastcall shil_compile_shld(shil_opcode* op)
{
	ppc_gpr_reg r = LoadReg(R3,op->reg1);
	ppc_gpr_reg s = LoadReg(R4,op->reg2);

	ppc_Label* else_ = ppce->CreateLabel(false,0);
	ppc_Label* end_ = ppce->CreateLabel(false,0);
	
	EMIT_CMPI(ppce,s,0,0);
	ppce->emitBranchConditionalToLabel(else_,0,PPC_CC_F,PPC_CC_NEG);
	
	EMIT_NEG(ppce,R5,s);
	EMIT_SRW(ppce,r,r,R5);
	ppce->emitBranchToLabel(end_,0);

	ppce->MarkLabel(else_);
	
	EMIT_SLW(ppce,r,r,s);

	ppce->MarkLabel(end_);

	SaveReg(op->reg1,r);
}

//sar reg32,imm , set flags
void __fastcall shil_compile_sar(shil_opcode* op)
{
	PowerPC_instr ppc;
	GEN_SRAWI(ppc,0,0,0);
	op_imm_to_reg(op,ppc,false,true);
}

//rotates
//rcl reg32, CF is set before calling
void __fastcall shil_compile_rcl(shil_opcode* op)
{
	PowerPC_instr ppc;
	GEN_RLWINM(ppc,0,0,1,0,31);
	op_reg(op,ppc,false,1,true);
}
//rcr reg32, CF is set before calling
void __fastcall shil_compile_rcr(shil_opcode* op)
{
	PowerPC_instr ppc;
	GEN_RLWINM(ppc,0,0,31,0,31);
	op_reg(op,ppc,false,2,true);
}
//ror reg32
void __fastcall shil_compile_ror(shil_opcode* op)
{
	PowerPC_instr ppc;
	GEN_RLWINM(ppc,0,0,31,0,31);
	op_reg(op,ppc,false,2,false);
}
//rol reg32
void __fastcall shil_compile_rol(shil_opcode* op)
{
	PowerPC_instr ppc;
	GEN_RLWINM(ppc,0,0,1,0,31);
	op_reg(op,ppc,false,1,false);
}
//neg
void __fastcall shil_compile_neg(shil_opcode* op)
{
	PowerPC_instr ppc;
	GEN_NEG(ppc,0,0);
	op_reg(op,ppc,false,0,false);
}
//not
void __fastcall shil_compile_not(shil_opcode* op)
{
	PowerPC_instr ppc;
	GEN_NOR(ppc,0,0,0);
	op_reg(op,ppc,true,0,false);
}

//xor reg32,reg32/imm32
void __fastcall shil_compile_xor(shil_opcode* op)
{
	PowerPC_instr ppc,ppc_imm;
	GEN_XOR(ppc,0,0,0);
	GEN_XORI(ppc_imm,0,0,0);
	op_reg_to_reg(op,ppc,ppc_imm,true,false,false);
}
//or reg32,reg32/imm32
void __fastcall shil_compile_or(shil_opcode* op)
{
	PowerPC_instr ppc,ppc_imm;
	GEN_OR(ppc,0,0,0);
	GEN_ORI(ppc_imm,0,0,0);
	op_reg_to_reg(op,ppc,ppc_imm,true,false,false);
}
//and reg32,reg32/imm32
void __fastcall shil_compile_and(shil_opcode* op)
{
	PowerPC_instr ppc;
	GEN_AND(ppc,0,0,0);
	op_reg_to_reg(op,ppc,0,true,false,false);
}
//readm/writem 
//Address calculation helpers
ppc_reg readwrteparams1(u8 reg1,u32 imm,u32 size,s32* fast_imm)
{
	ppc_reg reg;
	
	if (ira->IsRegAllocated(reg1))
	{
		reg=LoadReg(R3,reg1);
		assert(reg!=R3);
		
		if((s32)imm>=-32768 && (s32)imm<=32767)
		{
			if(size>=FLAG_32)
			{
				*fast_imm=(s32)imm;
			}
			else
			{
				EMIT_ADDI(ppce,R3,reg,imm);
				reg=R3;
			}
		}
		else
		{
			EMIT_ADDIS(ppce,R3,reg,HA(imm))
			EMIT_ADDI(ppce,R3,R3,imm);
			reg=R3;
		}
	}
	else
	{
		ppce->emitLoad32(R3,GetRegPtr(reg1));
		reg=R3;

		if((s32)imm>=-32768 && (s32)imm<=32767)
		{
			if(size>=FLAG_32)
			{
				*fast_imm=(s32)imm;
			}
			else
			{
				EMIT_ADDI(ppce,R3,R3,imm);
			}
		}
		else
		{
			EMIT_ADDIS(ppce,R3,R3,HA(imm))
			EMIT_ADDI(ppce,R3,R3,imm);
		}
	}
	return reg;
}
void readwrteparams2(u8 reg1,u8 reg2)
{
	if (ira->IsRegAllocated(reg1))
	{
		ppc_reg r1=LoadReg(R3,reg1);
		assert(r1!=R3);
		
		if (ira->IsRegAllocated(reg2))
		{
			//lea ecx,[reg1+reg2]
			ppc_reg r2=LoadReg(R3,reg2);
			assert(r2!=R3);
			EMIT_ADD(ppce,R3,r1,r2);
		}
		else
		{
			//mov ecx,reg1
			//add ecx,[reg2]
			ppce->emitLoad32(R3,GetRegPtr(reg2));
			EMIT_ADD(ppce,R3,R3,r1);
		}
	}
	else
	{
		if (ira->IsRegAllocated(reg2))
		{
			readwrteparams2(reg2,reg1);
		}
		else
		{
			//mov ecx,[reg1]
			//add ecx,[reg2]
			ppce->emitLoad32(R3,GetRegPtr(reg1));
			ppce->emitLoad32(R4,GetRegPtr(reg2));
			EMIT_ADD(ppce,R3,R3,R4);
		}
	}
}
void readwrteparams3(u8 reg1,u8 reg2,u32 imm,u32 size,s32* fast_imm)
{
	//verify((imm&0xffff)==imm);
	verify((s32)imm>=-32768 && (s32)imm<=32767);
		
	if (ira->IsRegAllocated(reg1))
	{
		ppc_reg r1=LoadReg(R3,reg1);
		assert(r1!=R3);
		
		if (ira->IsRegAllocated(reg2))
		{
			//lea ecx,[reg1+reg2]
			ppc_reg r2=LoadReg(R3,reg2);
			assert(r2!=R3);
			EMIT_ADD(ppce,R3,r1,r2);
			if(size>=FLAG_32)
			{
TR				*fast_imm=(s32)imm;
			}
			else
			{
				EMIT_ADDI(ppce,R3,R3,imm);
			}
		}
		else
		{
			//lea ecx,[reg1+imm]
			//add ecx,[reg2]
			ppce->emitLoad32(R3,GetRegPtr(reg2));
			EMIT_ADD(ppce,R3,R3,r1);
			if(size>=FLAG_32)
			{
TR				*fast_imm=(s32)imm;
			}
			else
			{
				EMIT_ADDI(ppce,R3,R3,imm);
			}
		}
	}
	else
	{
		if (ira->IsRegAllocated(reg2))
		{
			readwrteparams3(reg2,reg1,imm,size,fast_imm);
		}
		else
		{
			//mov ecx,[reg1]
			//add ecx,[reg2]
			ppce->emitLoad32(R3,GetRegPtr(reg1));
			ppce->emitLoad32(R4,GetRegPtr(reg2));
			EMIT_ADD(ppce,R3,R3,R4);
			if(size>=FLAG_32)
			{
TR				*fast_imm=(s32)imm;
			}
			else
			{
				EMIT_ADDI(ppce,R3,R3,imm);
			}
		}
	}
}
//Emit needed calc. asm and return register that has the address :)
ppc_reg  readwrteparams(shil_opcode* op,ppc_reg* fast_reg,s32* fast_offset)
{
	assert(0==(op->flags & FLAG_IMM2));
	assert(op->flags & FLAG_REG1);
	*fast_reg=ERROR_REG;
//	bool Loaded=false;

	//can use
	//mov ecx,imm
	//lea ecx[r[2]+imm]
	//lea ecx[r0/gbr+imm]
	//mov ecx,r0/gbr
	//mov ecx,r[2]
	//lea ecx[r0/gbr+r[2]*1]
	//lea ecx,[r0/gbr+r[2]*1+imm] ;)

	u32 flags=0;
#define flag_imm 1
#define flag_r2 2
#define flag_r0 4
#define flag_gbr 8

	if (op->flags & FLAG_IMM1)
	{
		if (op->imm1!=0)	//gota do that on const elimiation pass :D
			flags|=flag_imm;
	}
	if (op->flags & FLAG_REG2)
	{
		flags|=flag_r2;
	}
	if (op->flags & FLAG_R0)
	{
		flags|=flag_r0;
	}
	if (op->flags & FLAG_GBR)
	{
		flags|=flag_gbr;
	}

	verify(flags!=0);
	ppc_reg reg=ERROR_REG;

	u32 size=op->flags&3;

//	printf("readwrteparams flags %d\n",flags);
	
	switch(flags)
	{
		//1 olny
	case flag_imm:
		ppce->emitLoadImmediate32(R3,op->imm1);
		reg=R3;
		//gli dbgbreak;//must never ever happen
		break;

	case flag_r2:
		reg=LoadReg(R3,op->reg2);
		break;

	case flag_r0:
		reg=LoadReg(R3,r0);
		break;

	case flag_gbr:
		reg=LoadReg(R3,reg_gbr);
		break;

		//2 olny
	case flag_imm | flag_r2:
		reg=readwrteparams1((u8)op->reg2,op->imm1,size,fast_offset);
		break;

	case flag_imm | flag_r0:
		reg=readwrteparams1((u8)r0,op->imm1,size,fast_offset);
		break;

	case flag_imm | flag_gbr:
		reg=readwrteparams1((u8)reg_gbr,op->imm1,size,fast_offset);
		break;

	case flag_r2 | flag_r0:
		readwrteparams2((u8)op->reg2,(u8)r0);
		reg=R3;
		break;

	case flag_r2 | flag_gbr:
		readwrteparams2((u8)op->reg2,(u8)reg_gbr);
		reg=R3;
		break;

		//3 olny
	case flag_imm | flag_r2 | flag_gbr:
		readwrteparams3((u8)op->reg2,(u8)reg_gbr,op->imm1,size,fast_offset);
		reg=R3;
		break;

	case flag_imm | flag_r2 | flag_r0:
		readwrteparams3((u8)op->reg2,(u8)r0,op->imm1,size,fast_offset);
		reg=R3;
		break;

	default:
		die("Unable to compute readwrteparams");
		break;
	}

	verify(reg!=ERROR_REG);
	return reg;
}


const u32 m_unpack_sz[4]={1,2,4,8};
//Ram Only Mem Lookup
ppc_reg roml(ppc_reg reg,ppc_Label* lbl,u32* offset_Edit,int size,int rw)
{
	ppc_reg addr_reg=R5;
	
	switch(size)
	{
		case FLAG_8:  EMIT_XORI(ppce,R6,reg,3); reg=R6; break;
		case FLAG_16: EMIT_XORI(ppce,R6,reg,2); reg=R6; break;
	}


#if 0
	ppce->emitLoadImmediate32(R5,0xE0000000);
	EMIT_CMPL(ppce,reg,R5,0);
	ppce->emitBranchConditionalToLabel(lbl,0,PPC_CC_F,PPC_CC_NEG);
	EMIT_RLWINM(ppce,R5,reg,0,3,31);
	EMIT_ORIS(ppce,R5,R5,(u32)sh4_reserved_mem>>16);
#else		
	if ((reg==R3 && rw==0) || reg==R6)
		addr_reg=reg;
	else
	{
		ppce->emitMoveRegister(R5,reg);
		addr_reg=R5;
	}
		
	EMIT_RLWIMI(ppce,addr_reg,RSH4R,31,0,2); // RSH4R is 0x8xxxxxxx, it's the only reason it's used here
#endif
	
	return addr_reg;
}
//const ppc_opcode_class rm_table[4]={op_movsx8to32,op_movsx16to32,op_mov32,op_movlps};
void __fastcall shil_compile_readm(shil_opcode* op)
{
//	ppce->do_disasm=true;
	
	u32 size=op->flags&3;
	
//	printf("shil_compile_readm %d\n",size);

	//if constant read , and on ram area , make it a direct mem access
	//_watch_ mmu
	if (!(op->flags & (FLAG_R0|FLAG_GBR|FLAG_REG2)))
	{
		//[imm1] form
		assert(op->flags & FLAG_IMM1);
		emit_vmem_op_compat_const(ppce,0,size,op->imm1,op->reg1);
		return;
	}

	u32 old_offset=ppce->ppc_indx;
	ppc_reg fast_reg;
	s32 fast_offset=0;
	ppc_reg reg_addr = readwrteparams(op,&fast_reg,&fast_offset);
	
	old_offset=ppce->ppc_indx-old_offset;
	/*ppc_Label* patch_point=*/ ppce->CreateLabel(true,0);
	ppc_Label* p4_handler = ppce->CreateLabel(false,0);
	ppc_Label* roml_search_lbl = ppce->CreateLabel(false,0);

	//Ram Only Mem Lookup
	ppc_reg addr_reg=roml(reg_addr,p4_handler,&old_offset,size,0);

	//mov to dest or temp
	u32 is_float=IsInFReg(op->reg1);
	ppc_reg destreg;
	if (size==FLAG_64)
	{
		destreg=FR0;
	}
	else
	{
		if (is_float)
		{
			destreg=fra->GetRegister(FR0,op->reg1,RA_NODATA);
		}
		else
		{
			if (ira->IsRegAllocated(op->reg1))
			{
				destreg=LoadReg_nodata(R4,op->reg1);
				verify(destreg!=R4);
			}
			else
			{
				destreg=R4;
			}
		}
	}

//	printf("destreg %d %d\n",destreg,reg_addr);
	
	ppce->MarkLabel(roml_search_lbl);
	
	if(is_float)
	{
		switch(size)
		{
			case FLAG_32: EMIT_LFS(ppce,destreg,fast_offset,addr_reg); break;
			case FLAG_64: EMIT_LFD(ppce,destreg,fast_offset,addr_reg); break;
			default: verify(false);
		}
	}
	else
	{
		switch(size)
		{
			case FLAG_8:  EMIT_LBZ(ppce,destreg,fast_offset,addr_reg); break;
			case FLAG_16: EMIT_LHZ(ppce,destreg,fast_offset,addr_reg); break;
			case FLAG_32: EMIT_LWZ(ppce,destreg,fast_offset,addr_reg); break;
			case FLAG_64: EMIT_LFD(ppce,destreg,fast_offset,addr_reg); break;
			default: verify(false);
		}
	}
	
	roml_patch t;
	
	if (size==FLAG_64)
	{
		u32 dblreg=GetSingleFromDouble((u8)op->reg1);
		ppce->emitStoreDouble(GetRegPtr(dblreg),FR0);
		t.exit_point=ppce->CreateLabel(true,0);
		ReloadDouble(dblreg);
	}
	else
	{
		t.exit_point=ppce->CreateLabel(true,0);

		switch(size)
		{
			case FLAG_8:  EMIT_EXTSB(ppce,destreg,destreg); break;
			case FLAG_16: EMIT_EXTSH(ppce,destreg,destreg); break;
		}

		if (is_float)
		{
			fra->SaveRegister(op->reg1,destreg);
		}
		else
		{
			SaveReg(op->reg1,destreg);
		}
	}

    t.sh4_reg_data=op->reg1;
	t.p4_access=p4_handler;
	t.resume_offset=(u8)old_offset;
	t.asz=size;
	t.type=0;
	t.is_float=is_float;
	t.reg_addr=reg_addr;
	t.fast_imm=fast_offset;
	if (size!=FLAG_64)
	{
		t.reg_data=destreg;
	}
	else
	{
		t.reg_data=(ppc_reg)GetSingleFromDouble((u8)op->reg1);
		ReloadDouble(t.reg_data);
	}
	

	t.roml_search_lbl=roml_search_lbl;

	//emit_vmem_read(reg_addr,op->reg1,m_unpack_sz[size]);
	roml_patch_list.push_back(t);
}
//const ppc_opcode_class wm_table[4]={op_mov8,op_mov16,op_mov32,op_movlps};
void __fastcall shil_compile_writem(shil_opcode* op)
{
	//sse_WBF(op->reg1);//Write back possibly readed reg
	u32 size=op->flags&3;
	u32 is_float=IsInFReg(op->reg1);
	u32 was_float=is_float;
	//if constant read , and on ram area , make it a direct mem access
	//_watch_ mmu
	if (!(op->flags & (FLAG_R0|FLAG_GBR|FLAG_REG2)))
	{//[reg2+imm] form
		assert(op->flags & FLAG_IMM1);
		emit_vmem_op_compat_const(ppce,1,size,op->imm1,op->reg1);
		return;
	}

	u32 old_offset=ppce->ppc_indx;
	ppc_reg fast_reg;
	s32 fast_offset=0;
	ppc_reg reg_addr = readwrteparams(op,&fast_reg,&fast_offset);

	ppc_reg rsrc;
	if (size==FLAG_64)
	{
		u8 f32reg=GetSingleFromDouble((u8)op->reg1);
		FlushDouble(f32reg);
		ppce->emitLoadDouble(FR0,GetRegPtr(f32reg));
		rsrc=FR0;
	}
	else
	{
		if (!is_float)
		{
			rsrc=LoadReg(R4,op->reg1);
		}
		else
		{
			if (fra->IsRegAllocated(op->reg1))
			{
				rsrc=fra->GetRegister(FR0,op->reg1,RA_DEFAULT);
			}
			else
			{
				rsrc=R4;
				ppce->emitLoad32(R4,GetRegPtr(op->reg1));
				was_float=0;
			}
		}
	}
	
	old_offset=ppce->ppc_indx-old_offset;
	
	//ppc_Label* patch_point= ppce->CreateLabel(true,0);
	ppc_Label* p4_handler = ppce->CreateLabel(false,0);
	//Ram Only Mem Lookup
	ppc_reg addr_reg = roml(reg_addr,p4_handler,&old_offset,size,1);
	//mov [ecx],src

	ppc_Label* roml_search_lbl = ppce->CreateLabel(true,0);
	
	if (was_float)
	{
        switch(size)
		{
			case FLAG_32: EMIT_STFS(ppce,rsrc,fast_offset,addr_reg); break;
			case FLAG_64: EMIT_STFD(ppce,rsrc,fast_offset,addr_reg); break;
		}
	}
	else
	{
		switch(size)
		{
			case FLAG_8:  EMIT_STB(ppce,rsrc,fast_offset,addr_reg); break;
			case FLAG_16: EMIT_STH(ppce,rsrc,fast_offset,addr_reg); break;
			case FLAG_32: EMIT_STW(ppce,rsrc,fast_offset,addr_reg); break;
			case FLAG_64: EMIT_STFD(ppce,rsrc,fast_offset,addr_reg); break;
		}
	}

	
	//if  (is_float)
	//ppce->Emit(op_jmp,p4_handler);

	roml_patch t;
    t.sh4_reg_data=op->reg1;
	t.p4_access=p4_handler;
	t.resume_offset=(u8)old_offset;
	t.exit_point=ppce->CreateLabel(true,0);
	t.asz=size;
	t.type=1;
	t.is_float=was_float != 0;
	t.reg_addr=reg_addr;
	t.fast_imm=fast_offset;
	if (size!=FLAG_64)
	{
		t.reg_data=rsrc;
	}
	else
	{
		t.reg_data=(ppc_reg)GetSingleFromDouble((u8)op->reg1);
		FlushDouble(t.reg_data);
	}

	t.roml_search_lbl=roml_search_lbl;
	
	roml_patch_list.push_back(t);
	
	//emit_vmem_write(reg_addr,op->reg1,m_unpack_sz[size]);
}
void* nvw_lut[4]={(void*)WriteMem8,(void*)WriteMem16,(void*)WriteMem32,(void*)WriteMem32};
void* nvr_lut[4]={(void*)ReadMem8,(void*)ReadMem16,(void*)ReadMem32,(void*)ReadMem32};
#include "dc/mem/sh4_internal_reg.h"

void apply_roml_patches()
{
	for (u32 i=0;i<roml_patch_list.size();i++)
	{
//		printf("apply_roml_patches %d %d\n",roml_patch_list[i].type,roml_patch_list[i].asz);
		void * function=roml_patch_list[i].type==1 ?nvw_lut[roml_patch_list[i].asz]:nvr_lut[roml_patch_list[i].asz];
		
		ppc_Label* normal_write_nosq=ppce->CreateLabel(false,8);

		ppce->emitBranchToLabel(roml_patch_list[i].roml_search_lbl,0);
		
		ppce->MarkLabel(roml_patch_list[i].p4_access);

		if(roml_patch_list[i].reg_addr!=R3 || roml_patch_list[i].fast_imm)
			EMIT_ADDI(ppce,R3,roml_patch_list[i].reg_addr,roml_patch_list[i].fast_imm);
		
		if (roml_patch_list[i].type==1 && (roml_patch_list[i].asz>=FLAG_32))
		{
			//check for SQ write
			EMIT_RLWINM(ppce,R6,R3,16,16,31);
			EMIT_CMPLI(ppce,R6,0xe000,0);
			ppce->emitBranchConditionalToLabel(normal_write_nosq,0,PPC_CC_T, PPC_CC_NEG);
			EMIT_CMPLI(ppce,R6,0xe400,0);
			ppce->emitBranchConditionalToLabel(normal_write_nosq,0,PPC_CC_F, PPC_CC_NEG);

			EMIT_RLWINM(ppce,R3,R3,0,26,29); // & 0x3c
			EMIT_ORIS(ppce,R3,R3,((u32)sq_both)>>16);
			if (FLAG_32==roml_patch_list[i].asz)
			{
				if(roml_patch_list[i].is_float)
				{
					EMIT_STFS(ppce,roml_patch_list[i].reg_data,0,R3);
				}
				else
				{
					EMIT_STW(ppce,roml_patch_list[i].reg_data,0,R3);
				}
			}
			else
			{
				EMIT_STFD(ppce,FR0,0,R3);
			}
			ppce->emitBranchToLabel(roml_patch_list[i].exit_point,0);
		}

		ppce->MarkLabel(normal_write_nosq);
		
		if (roml_patch_list[i].asz!=FLAG_64)
		{
			if (roml_patch_list[i].type==1)
			{
				if (roml_patch_list[i].is_float)
				{
                    fra->LoadRegisterGPR(R4,roml_patch_list[i].sh4_reg_data);
				}
				else
				{
					//if write make sure data is on edx
					if (roml_patch_list[i].reg_data!=R4)
						ppce->emitMoveRegister(R4,roml_patch_list[i].reg_data);
				}
			}

			ppce->emitBranch(function,1);
			
			if (roml_patch_list[i].type==0)
			{
				if (roml_patch_list[i].is_float)
				{
					static u32 tmp;
					ppce->emitStore32(&tmp,R3);
					ppce->emitLoadFloat(roml_patch_list[i].reg_data,&tmp);
				}
				else
				{
					if (roml_patch_list[i].reg_data!=R3)
					ppce->emitMoveRegister(roml_patch_list[i].reg_data,R3);
				}
			}
		}
		else
		{
			//if (roml_patch_list[i].type==0)
			//	ppce->Emit(op_int3);
			//save address once
			static u32 tmp;
			
			ppce->emitStore32(&tmp,R3);
			
			EMIT_ADDI(ppce,R3,R3,4);
			u32* target=GetRegPtr(roml_patch_list[i].reg_data);

			for (int j=1;j>=0;j--)
			{
				if (roml_patch_list[i].type==1)
				{
					//write , need data on EDX
					ppce->emitLoad32(R4,&target[j]);
				}
				ppce->emitBranch(function,1);
				if (roml_patch_list[i].type==0)
				{
					//read, save data from eax
					ppce->emitStore32(&target[j],R3);
				}

				if (j==1)
					ppce->emitLoad32(R3,&tmp);//get the 'low' address
			}

		}
		ppce->emitBranchToLabel(roml_patch_list[i].exit_point,0);
		//ppce->MarkLabel(roml_patch_list[i].p4_access);
		
		//roml_patch_list[i].reg_data;
		//emit_vmem_write(roml_patch_list[i].reg_addr,
	}
	roml_patch_list.clear();
}

//save-loadT
void __fastcall shil_compile_SaveT(shil_opcode* op)
{
	assert(op->flags & FLAG_IMM1);//imm1
	assert(0==(op->flags & (FLAG_IMM2|FLAG_REG1|FLAG_REG2)));//no imm2/r1/r2
//	printf("shil_compile_SaveT %x\n",op->imm1);
	switch(ppc_condition_flags[op->imm1][0])
	{
		case PPC_CC_T:
			EMIT_CROR(ppce,CR_T_FLAG,ppc_condition_flags[op->imm1][1],ppc_condition_flags[op->imm1][1]);
			break;
		case PPC_CC_F:
			EMIT_CRNOR(ppce,CR_T_FLAG,ppc_condition_flags[op->imm1][1],ppc_condition_flags[op->imm1][1]);
			break;
		default:
			verify(false);
	}
}

void __fastcall shil_compile_LoadT(shil_opcode* op)
{
	assert(op->flags & FLAG_IMM1);//imm1
	assert(0==(op->flags & (FLAG_IMM2|FLAG_REG1|FLAG_REG2)));//no imm2/r1/r2
	

	assert( (op->imm1==CF) || (op->imm1==jcond_flag) );

	if (op->imm1!=jcond_flag)
	{
		EMIT_CROR(ppce,PPC_CC_ZER,CR_T_FLAG,CR_T_FLAG);
	}
	else
	{
		EMIT_CROR(ppce,CR_T_COND_FLAG,CR_T_FLAG,CR_T_FLAG);
	}
}
//cmp-test
void __fastcall shil_compile_cmp(shil_opcode* op)
{
	assert(FLAG_32==(op->flags & 3));
	if (op->flags & FLAG_IMM1)
	{
		assert(((s32)op->imm1>=-32768 && (s32)op->imm1<=32767));
		assert(0==(op->flags & (FLAG_REG2|FLAG_IMM2)));
		if (ira->IsRegAllocated(op->reg1))
		{
			ppc_gpr_reg r1 = LoadReg(R4,op->reg1);
			if (op->flags & FLAG_LOGICAL_CMP)
			{
				EMIT_CMPLI(ppce,r1,op->imm1,0);
			}
			else
			{
				EMIT_CMPI(ppce,r1,op->imm1,0);
			}
		}
		else
		{
			ppce->emitLoad32(R3,GetRegPtr(op->reg1));
			if (op->flags & FLAG_LOGICAL_CMP)
			{
				EMIT_CMPLI(ppce,R3,op->imm1,0);
			}
			else
			{
				EMIT_CMPI(ppce,R3,op->imm1,0);
			}
		}
		//eflags is used w/ combination of SaveT
	}
	else
	{
		assert(0==(op->flags & FLAG_IMM2));
		assert(op->flags & FLAG_REG2);

		ppc_gpr_reg r1 = LoadReg(R4,op->reg1);
		if (ira->IsRegAllocated(op->reg2))
		{
			ppc_gpr_reg r2 = LoadReg(R3,op->reg2);
			if (op->flags & FLAG_LOGICAL_CMP)
			{
				EMIT_CMPL(ppce,r1,r2,0);
			}
			else
			{
				EMIT_CMP(ppce,r1,r2,0);
			}
		}
		else
		{
			ppce->emitLoad32(R3,GetRegPtr(op->reg2));
			if (op->flags & FLAG_LOGICAL_CMP)
			{
				EMIT_CMPL(ppce,r1,R3,0);
			}
			else
			{
				EMIT_CMP(ppce,r1,R3,0);
			}
		}
		//eflags is used w/ combination of SaveT
	}
}
void __fastcall shil_compile_test(shil_opcode* op)
{
	assert(FLAG_32==(op->flags & 3));
	if (op->flags & FLAG_IMM1)
	{
		assert((op->imm1&0xffff)==op->imm1);
		assert(0==(op->flags & (FLAG_REG2|FLAG_IMM2)));
		if (ira->IsRegAllocated(op->reg1))
		{
			ppc_gpr_reg r1 = LoadReg(R4,op->reg1);
			EMIT_ANDI(ppce,R0,r1,op->imm1);
		}
		else
		{
			ppce->emitLoad32(R3,GetRegPtr(op->reg1));
			EMIT_ANDI(ppce,R0,R3,op->imm1);
		}
		//eflags is used w/ combination of SaveT
	}
	else
	{
		assert(0==(op->flags & FLAG_IMM2));
		assert(op->flags & FLAG_REG2);
		
		ppc_gpr_reg r1 = LoadReg(R4,op->reg1);
        ppc_gpr_reg r2 = LoadReg(R3,op->reg2);
#if 0
		PowerPC_instr ppc;
        GEN_AND(ppc,R0,r1,r2);
        ppc|=1; // record bit
        ppce->write32(ppc);		
#else
        EMIT_AND(ppce,R5,r1,r2);
        EMIT_CMPLI(ppce,R5,0,0);
#endif        
		//eflags is used w/ combination of SaveT
	}
}

//add-sub
void __fastcall shil_compile_add(shil_opcode* op)
{
	PowerPC_instr ppc,ppc_imm;
	GEN_ADD(ppc,0,0,0);
	GEN_ADDI(ppc_imm,0,0,0);
	op_reg_to_reg(op,ppc,ppc_imm,false,false,false);
}
void __fastcall shil_compile_adc(shil_opcode* op)
{
	PowerPC_instr ppc;
	GEN_ADD(ppc,0,0,0);
	ppc|=1; // record bit
	op_reg_to_reg(op,ppc,0,false,false,true);
}
void __fastcall shil_compile_sub(shil_opcode* op)
{
	PowerPC_instr ppc,ppc_imm;
	GEN_SUBF(ppc,0,0,0);
	GEN_ADDI(ppc_imm,0,0,0);
	op_reg_to_reg(op,ppc,ppc_imm,false,true,false);
}


//left over from older code
void __fastcall shil_compile_jcond(shil_opcode* op)
{
	log("jcond ... heh not implemented\n");
	assert(false);
}
void __fastcall shil_compile_jmp(shil_opcode* op)
{
	log("jmp ... heh not implemented\n");
}
//helpers for mul
void load_with_se16(ppc_gpr_reg to,u8 from)
{
	if (ira->IsRegAllocated(from))
	{
		ppc_gpr_reg r1=LoadReg(R4,from);
		EMIT_EXTSH(ppce,to,r1);
	}
	else
	{
		ppce->emitLoad32(to,GetRegPtr(from));
		EMIT_EXTSH(ppce,to,to);
	}
}

void load_with_ze16(ppc_gpr_reg to,u8 from)
{
	if (ira->IsRegAllocated(from))
	{
		ppc_gpr_reg r1=LoadReg(R4,from);
		EMIT_RLWINM(ppce,to,r1,0,16,31);
	}
	else
	{
		ppce->emitLoad32(to,GetRegPtr(from));
		EMIT_RLWINM(ppce,to,to,0,16,31);
	}
}
//mul16/32/64 reg,reg
void __fastcall shil_compile_mul(shil_opcode* op)
{
	u32 sz=op->flags&3;

	assert(sz!=FLAG_8);//nope , can't be 16 bit..

	if (sz==FLAG_64)//mach is olny used on 64b version
		assert(op->flags & FLAG_MACH);
	else
		assert(0==(op->flags & FLAG_MACH));


	if (sz!=FLAG_64)
	{
		if (sz==FLAG_16)
		{
			//FlushRegCache_reg(op->reg1);
			//FlushRegCache_reg(op->reg2);

			if (op->flags & FLAG_SX)
			{
				//ppce->Emit(op_movsx16to32, EAX,(u16*)GetRegPtr(op->reg1));
				load_with_se16(R4,(u8)op->reg1);
				//ppce->Emit(op_movsx16to32, ECX,(u16*)GetRegPtr(op->reg2));
				load_with_se16(R5,(u8)op->reg2);
			}
			else
			{
				//ppce->Emit(op_movzx16to32, EAX,(u16*)GetRegPtr(op->reg1));
				load_with_ze16(R4,(u8)op->reg1);
				//ppce->Emit(op_movzx16to32, ECX,(u16*)GetRegPtr(op->reg2));
				load_with_ze16(R5,(u8)op->reg2);
			}
		}
		else
		{
			//ppce->Emit(op_mov32,EAX,GetRegPtr(op->reg1));
			//ppce->Emit(op_mov32,ECX,GetRegPtr(op->reg2));
			LoadReg_force(R4,op->reg1);
			LoadReg_force(R5,op->reg2);
		}

		/*if (op->flags & FLAG_SX)
			ppce->Emit(op_imul32,R4,ECX);
		else
			ppce->Emit(op_mul32,ECX);*/
		
		EMIT_MULLW(ppce,R3,R4,R5);
		
		SaveReg((u8)reg_macl,R3);
	}
	else
	{
		assert(sz==FLAG_64);
		
		ira->FlushRegister(op->reg1);
		ira->FlushRegister(op->reg2);

		ppce->emitLoad32(R4,GetRegPtr(op->reg1));
		ppce->emitLoad32(R5,GetRegPtr(op->reg2));

		EMIT_MULLW(ppce,R3,R4,R5);
		
		if (op->flags & FLAG_SX)
		{
			EMIT_MULHW(ppce,R4,R4,R5);
		}
		else
		{
			EMIT_MULHWU(ppce,R4,R4,R5);
		}

		SaveReg((u8)reg_macl,R3);
		SaveReg((u8)reg_mach,R4);
	}
}
#if 1 //gli86
void FASTCALL sh4_div0u();
void FASTCALL sh4_div0s(u32 rn,u32 rm);
u32 FASTCALL sh4_rotcl(u32 rn);
u32 FASTCALL sh4_div1(u32 rn,u32 rm);

u32 divtmp;

u32 FASTCALL shil_helper_slowdiv32_sgn(u32 r3,u32 r2,u32 r1)
{
	sh4_div0s(r2,r3);
    
	for (int i=0;i<32;i++)
	{
		r1=sh4_rotcl(r1);
		r2=sh4_div1(r2,r3);
	}

    divtmp=r1;
    return r3;
    
	return ((u64)r3<<32)|r1;//EAX=r1, EDX=r3 
}


u32 FASTCALL shil_helper_slowdiv32(u32 r3,u32 r2,u32 r1)
{
	sh4_div0u();

	for (int i=0;i<32;i++)
	{
		r1=sh4_rotcl(r1);
		r2=sh4_div1(r2,r3);
	}

    divtmp=r1;
    return r3;
    
	return ((u64)r3<<32)|r1;//EAX=r1, EDX=r3 
}

void __fastcall shil_compile_div32(shil_opcode* op)
{
	assert(op->flags & (FLAG_IMM1));
	assert(0==(op->flags & (FLAG_IMM2)));

	u8 rQuotient=(u8)op->reg1;
	u8 rDivisor=(u8)op->reg2;
	u8 rDividend=(u8)op->imm1;
	//Q=Dend/Dsor

    ppc_gpr_reg divisor=LoadReg(R3,rDivisor);
    ppc_gpr_reg dividend=LoadReg(R4,rDividend);
    ppc_gpr_reg quotient=LoadReg(R5,rQuotient);
    
	ppc_Label* slowdiv=ppce->CreateLabel(false,8);
	ppc_Label* fastdiv=ppce->CreateLabel(false,8);
	ppc_Label* exit =ppce->CreateLabel(false,8);

    if (op->flags & FLAG_SX)
    {
        EMIT_SRAWI(ppce,R6,quotient,31);
        EMIT_CMP(ppce,R6,dividend,0);
    }
    else
    {
        EMIT_CMPI(ppce,dividend,0,0);
    }        
    ppce->emitBranchConditionalToLabel(slowdiv,0,PPC_CC_F,PPC_CC_ZER);
    
    EMIT_CMPI(ppce,divisor,0,0);
	ppce->emitBranchConditionalToLabel(slowdiv,0,PPC_CC_T,PPC_CC_ZER);

    ppce->emitBranchToLabel(fastdiv,0);

	ppce->MarkLabel(slowdiv);

/*    ppce->emitBranch((void*)((op->flags & FLAG_SX) ? shil_helper_slowdiv32_sgn : shil_helper_slowdiv32),true);
    ppce->emitLoad32(R5,&divtmp);*/
    
    ppce->emitDebugValue(0xdeadc0de);

	ppce->emitBranchToLabel(exit,0);
	ppce->MarkLabel(fastdiv);

	if (op->flags & FLAG_SX)
	{
        EMIT_DIVW(ppce,R7,quotient,divisor);
	}
	else
	{
        EMIT_DIVWU(ppce,R7,quotient,divisor);
	}

    EMIT_MULLW(ppce,R6,R7,divisor);
    EMIT_SUB(ppce,dividend,quotient,R6);
    
    EMIT_ANDI(ppce,R8,R7,1);
    EMIT_CRNOR(ppce,CR_T_FLAG,PPC_CC_ZER,PPC_CC_ZER);
    
	if (op->flags & FLAG_SX)
	{
        EMIT_SRAWI(ppce,quotient,R7,1);
	}
	else
	{
        EMIT_SRWI(ppce,quotient,R7,1);
	}

    ppce->emitBranchConditionalToLabel(exit,0,PPC_CC_T,CR_T_FLAG);
    
    EMIT_SUB(ppce,dividend,dividend,divisor);
    
    ppce->MarkLabel(exit);
	SaveReg(rQuotient,quotient);
	SaveReg(rDividend,dividend);

}
#endif

//Fpu alloc helpers
#define fa_r1r2 (1|2)
#define fa_r1m2 (1|0)
#define fa_m1r2 (0|2)
#define fa_m1m2 (0|0)

#define frs(op) (IsInFReg(op->reg1)|(IsInFReg(op->reg2)<<1))

//Fpu consts
#define pi (3.14159265f)

__attribute__((aligned(128))) u32 ps_not_data[4]={0x80000000,0x80000000,0x80000000,0x80000000};
__attribute__((aligned(128))) u32 ps_and_data[4]={0x7FFFFFFF,0x7FFFFFFF,0x7FFFFFFF,0x7FFFFFFF};

__attribute__((aligned(128))) float mm_1[4]={1.0f,1.0f,1.0f,1.0f};
//__declspec(align(16)) float fsca_fpul_adj[4]={((2*pi)/65536.0f),((2*pi)/65536.0f),((2*pi)/65536.0f),((2*pi)/65536.0f)};

void fpr_reg_to_reg(shil_opcode* op,PowerPC_instr ppc,bool useRC)
{
/*	printf("fpr_reg_to_reg %d\n",frs(op));
	disassemble(0,ppc);*/
/*	switch (frs(op))
	{
	case fa_r1r2:*/
		{
			ppc_fpr_reg r1=fra->GetRegister(FR0,op->reg1,RA_DEFAULT);
			ppc_fpr_reg r2=fra->GetRegister(FR1,op->reg2,RA_DEFAULT);
			EMIT_SET_RDRARB(ppc,r1,r1,r2,0,useRC);
			fra->SaveRegister(op->reg1,r1);
		}
/*		break;
	case fa_r1m2:
		{
			ppc_fpr_reg r1=fra->GetRegister(FR0,op->reg1,RA_DEFAULT);
			assert(r1!=FR0);
			ppce->emitLoadFloat(FR0,GetRegPtr(op->reg2));
			EMIT_SET_RDRARB(ppc,r1,r1,FR0,0,useRC);
			fra->SaveRegister(op->reg1,r1);
		}
		break;
	case fa_m1r2:
		{
			ppc_fpr_reg r1=fra->GetRegister(FR0,op->reg1,RA_DEFAULT);
			ppc_fpr_reg r2=fra->GetRegister(FR0,op->reg2,RA_DEFAULT);
			assert(r1==FR0);
			assert(r2!=FR0);
			EMIT_SET_RDRARB(ppc,r1,r1,r2,0,useRC);
			fra->SaveRegister(op->reg1,r1);
		}
		break;
	case fa_m1m2:
		{
			ppc_fpr_reg r1=fra->GetRegister(FR0,op->reg1,RA_DEFAULT);
			assert(r1==FR0);
			ppce->emitLoadFloat(FR1,GetRegPtr(op->reg2));
			EMIT_SET_RDRARB(ppc,r1,r1,FR1,0,useRC);
			fra->SaveRegister(op->reg1,r1);
		}
		break;
	}*/
}

//#define SSE_s(op,sseop) sse_reg_to_reg(op,sseop);
//simple opcodes
void __fastcall shil_compile_fadd(shil_opcode* op)
{
	assert(0==(op->flags & (FLAG_IMM1|FLAG_IMM2)));
	u32 sz=op->flags & 3;
	if (sz==FLAG_32)
	{
		assert(!IsReg64((Sh4RegType)op->reg1));
		assert(Ensure32());

		PowerPC_instr ppc;
		GEN_FADD(ppc,0,0,0,0);
		fpr_reg_to_reg(op,ppc,false);
	}
	else
	{
		assert(false);
	}
}
void __fastcall shil_compile_fsub(shil_opcode* op)
{
	assert(0==(op->flags & (FLAG_IMM1|FLAG_IMM2)));
	u32 sz=op->flags & 3;
	if (sz==FLAG_32)
	{
		assert(!IsReg64((Sh4RegType)op->reg1));
		assert(Ensure32());
		
		PowerPC_instr ppc;
		GEN_FSUB(ppc,0,0,0,0);
		fpr_reg_to_reg(op,ppc,false);
	}
	else
	{
		assert(false);
	}
}

void __fastcall shil_compile_fmul(shil_opcode* op)
{
	assert(0==(op->flags & (FLAG_IMM1|FLAG_IMM2)));
	u32 sz=op->flags & 3;
	if (sz==FLAG_32)
	{
		assert(!IsReg64((Sh4RegType)op->reg1));
		assert(Ensure32());

		PowerPC_instr ppc;
		GEN_FMUL(ppc,0,0,0,0);
		fpr_reg_to_reg(op,ppc,true);
	}
	else
	{
		assert(false);
	}
}

void __fastcall shil_compile_fdiv(shil_opcode* op)
{
	assert(0==(op->flags & (FLAG_IMM1|FLAG_IMM2)));
	u32 sz=op->flags & 3;
	if (sz==FLAG_32)
	{
		assert(!IsReg64((Sh4RegType)op->reg1));
		assert(Ensure32());

		PowerPC_instr ppc;
		GEN_FDIV(ppc,0,0,0,0);
		fpr_reg_to_reg(op,ppc,false);
	}
	else
	{
		assert(false);
	}
}

//binary opcodes
void __fastcall shil_compile_fneg(shil_opcode* op)
{
	assert(0==(op->flags & (FLAG_IMM1|FLAG_IMM2|FLAG_REG2)));
	u32 sz=op->flags & 3;
	if (sz==FLAG_32)
	{
		assert(!IsReg64 ((Sh4RegType)op->reg1));
		if (IsInFReg(op->reg1))
		{
			ppc_fpr_reg r1=fra->GetRegister(FR0,op->reg1,RA_DEFAULT);
			assert(r1!=FR0);
			EMIT_FNEG(ppce,r1,r1);
			fra->SaveRegister(op->reg1,r1);
		}
		else
		{
			ppce->emitLoadFloat(FR0,GetRegPtr(op->reg1));
			EMIT_FNEG(ppce,FR0,FR0);
			ppce->emitStoreFloat(GetRegPtr(op->reg1),FR0);
		}
	}
	else
	{
/*		assert(sz==FLAG_64);
		assert(IsReg64((Sh4RegType)op->reg1));
		u32 reg=GetSingleFromDouble((u8)op->reg1);
		ppce->Emit(op_xor32,GetRegPtr(reg+0),0x80000000);*/
		assert(false);
	}
}

void __fastcall shil_compile_fabs(shil_opcode* op)
{
	assert(0==(op->flags & (FLAG_IMM1|FLAG_IMM2|FLAG_REG2)));
	u32 sz=op->flags & 3;
	if (sz==FLAG_32)
	{
		assert(!IsReg64 ((Sh4RegType)op->reg1));
		if (IsInFReg(op->reg1))
		{
			ppc_fpr_reg r1=fra->GetRegister(FR0,op->reg1,RA_DEFAULT);
			assert(r1!=FR0);
			EMIT_FABS(ppce,r1,r1);
			fra->SaveRegister(op->reg1,r1);
		}
		else
		{
			ppce->emitLoadFloat(FR0,GetRegPtr(op->reg1));
			EMIT_FABS(ppce,FR0,FR0);
			ppce->emitStoreFloat(GetRegPtr(op->reg1),FR0);
		}
	}
	else
	{
/*		assert(sz==FLAG_64);
		assert(IsReg64((Sh4RegType)op->reg1));
		u32 reg=GetSingleFromDouble((u8)op->reg1);
		ppce->Emit(op_and32,GetRegPtr(reg),0x7FFFFFFF);*/
		assert(false);
	}
}

//pref !
void __fastcall do_pref(u32 Dest);

void threaded_TASQ(u32* data);

void __fastcall shil_compile_pref(shil_opcode* op)
{
	assert(0==(op->flags & (FLAG_REG2|FLAG_IMM2)));
	assert(op->flags & (FLAG_REG1|FLAG_IMM1));
	
//	u32 sz=op->flags & 3;
//	assert(sz==FLAG_32);
	assert((op->flags & 3)==FLAG_32);

	if (op->flags&FLAG_REG1)
	{
		LoadReg_force(R3,op->reg1);
		
		EMIT_RLWINM(ppce,R4,R3,6,26,31);
		EMIT_CMPLI(ppce,R4,0xe0>>2,0);

		ppc_Label* after=ppce->CreateLabel(false,8);		
		ppce->emitBranchConditionalToLabel(after,0,PPC_CC_F,PPC_CC_ZER);

		if(CCN_MMUCR.AT==0)
		{
			ppc_Label* else_=ppce->CreateLabel(false,0);		
		
			EMIT_LIS(ppce,R4,(u32)CCN_QACR_TR>>16);
			EMIT_RLWIMI(ppce,R4,R3,32-3,29,29);		
			
			EMIT_LWZ(ppce,R4,0,R4);
			EMIT_RLWINM(ppce,R4,R4,32-26,29,31);
			EMIT_CMPLI(ppce,R4,4,0);			
			
			ppce->emitBranchConditionalToLabel(else_,0,PPC_CC_F,PPC_CC_ZER);
			
			EMIT_RLWINM(ppce,R3,R3,0,26,26);		
			EMIT_ORIS(ppce,R3,R3,(u32)sq_both>>16);
			
			ppce->emitBranch((void*)threaded_TASQ,1);
			ppce->emitBranchToLabel(after,0);
			
			ppce->MarkLabel(else_);
			
			ppce->emitBranch((void*)do_pref,1);
		}
		else
		{
			ppce->emitBranch((void*)do_pref,1);
		}

		ppce->MarkLabel(after);
		
/*		ppc_Label* after=ppce->CreateLabel(false,8);
		ppce->emitBranchConditionalToLabel(after,0,PPC_CC_F,PPC_CC_ZER);
		ppce->emitBranch((void*)do_pref,1);
		ppce->MarkLabel(after);*/
	}
	else if (op->flags&FLAG_IMM1)
	{
		if((op->imm1&0xFC000000)==0xE0000000)
		{
			ppce->emitLoadImmediate32(R3,op->imm1);
			ppce->emitBranch((void*)do_pref,1);
		}
	}
}

//complex opcodes
void __fastcall shil_compile_fcmp(shil_opcode* op)
{
	assert(0==(op->flags & (FLAG_IMM1|FLAG_IMM2)));
	u32 sz=op->flags & 3;
	if (sz==FLAG_32)
	{
		assert(!IsReg64((Sh4RegType)op->reg1));
		assert(Ensure32());

		//ppce->Emit(op_movss,XMM0,GetRegPtr(op->reg1));
		ppc_fpr_reg fr1=fra->GetRegister(FR0,op->reg1,RA_DEFAULT);
		
		//ppce->SSE_UCOMISS_M32_to_XMM(XMM0,GetRegPtr(op->reg2));
		if (fra->IsRegAllocated(op->reg2))
		{
			ppc_fpr_reg fr2=fra->GetRegister(FR0,op->reg2,RA_DEFAULT);
			assert(fr2!=FR0);
			EMIT_FCMPU(ppce,fr1,fr2,0);
			//ppce->Emit(op_ucomiss, fr1,fr2);
		}
		else
		{
			ppce->emitLoadFloat(FR1,GetRegPtr(op->reg2));
			EMIT_FCMPU(ppce,fr1,FR1,0);
//			ppce->Emit(op_ucomiss ,fr1,);
		}
	}
	else
	{
		assert(false);
	}
}

void __fastcall shil_compile_fmac(shil_opcode* op)
{
	assert(0==(op->flags & (FLAG_IMM1|FLAG_IMM2)));
	u32 sz=op->flags & 3;
	if (sz==FLAG_32)
	{
		assert(!IsReg64((Sh4RegType)op->reg1));
		//fr[n] += fr[0] * fr[m];
		assert(Ensure32());

		ppc_fpr_reg fr0=fra->GetRegister(FR0,fr_0,RA_DEFAULT);
		ppc_fpr_reg frp=fra->GetRegister(FR1,op->reg2,RA_DEFAULT);
		ppc_fpr_reg frs=fra->GetRegister(FR2,op->reg1,RA_DEFAULT);
		
		EMIT_FMADDS(ppce,frs,fr0,frp,frs);

		fra->SaveRegister(op->reg1,frs);
	}
	else
	{
		assert(false);
	}
}

void __fastcall shil_compile_fsqrt(shil_opcode* op)
{
	assert(0==(op->flags & (FLAG_IMM1|FLAG_IMM2|FLAG_REG2)));
	u32 sz=op->flags & 3;
	if (sz==FLAG_32)
	{
		assert(Ensure32());

		assert(!IsReg64((Sh4RegType)op->reg1));
		//RSQRT vs SQRTSS -- why rsqrt no workie ? :P -> RSQRT = 1/SQRTSS
		if (fra->IsRegAllocated(op->reg1))
		{
			ppc_fpr_reg fr1=fra->GetRegister(FR0,op->reg1,RA_DEFAULT);
			assert(fr1!=FR0);
			EMIT_FSQRT(ppce,fr1,fr1);
			fra->SaveRegister(op->reg1,fr1);
		}
		else
		{
			ppce->emitLoadFloat(FR0,GetRegPtr(op->reg1));
			EMIT_FSQRT(ppce,FR0,FR0);
			ppce->emitStoreFloat(GetRegPtr(op->reg1),FR0);
		}
		
	}
	else
	{
		assert(false);
	}
}

//#define _FAST_fssra

void __fastcall shil_compile_fsrra(shil_opcode* op)
{
	assert(0==(op->flags & (FLAG_IMM1|FLAG_IMM2|FLAG_REG2)));
	u32 sz=op->flags & 3;
	if (sz==FLAG_32)
	{
		assert(Ensure32());
		assert(!IsReg64((Sh4RegType)op->reg1));
		//maby need to calculate 1/sqrt manualy ? -> yes , it seems rcp is not as accurate as needed :)
		//-> no , it wasn that , rcp=1/x , RSQRT=1/srqt tho

		//ppce->SSE_SQRTSS_M32_to_XMM(XMM1,GetRegPtr(op->reg1));	//XMM1=sqrt
		//ppce->Emit(op_movss,XMM0,(u32*)mm_1);			//XMM0=1
		//ppce->SSE_DIVSS_XMM_to_XMM(XMM0,XMM1);					//XMM0=1/sqrt
		//or
		//ppce->SSE_RSQRTSS_M32_to_XMM(XMM0,GetRegPtr(op->reg1));//XMM0=APPR(1/sqrt(fr1))
		//-> im using Approximate version , since this is an aproximate opcode on sh4 too
		//i hope x86 isnt less accurate ..

		ppc_fpr_reg fr=fra->GetRegister(FR0,op->reg1,RA_DEFAULT);
#ifdef _FAST_fssra
		// Using the frsqrte instruction for the initial estimate followed
		// by 2 iterations of Newton-Raphson to get sufficient accuracy.
		EMIT_FRSQRTE(ppce,FR1,fr);
		EMIT_FMUL(ppce,FR3,FR1,fr,0);
		EMIT_FMUL(ppce,FR2,FR1,FRHALF,0);
		EMIT_FNMSUB(ppce,FR3,FR1,FR3,FRONE);
		EMIT_FMADDS(ppce,FR1,FR2,FR3,FR1);		
		EMIT_FMUL(ppce,FR3,FR1,fr,0);
		EMIT_FMUL(ppce,FR2,FR1,FRHALF,0);
		EMIT_FNMSUB(ppce,FR3,FR1,FR3,FRONE);
		EMIT_FMADDS(ppce,fr,FR2,FR3,FR1);		
#else
		EMIT_FSQRTS(ppce,fr,fr);
		EMIT_FDIV(ppce,fr,FRONE,fr,0);
#endif
		fra->SaveRegister(op->reg1,fr);
	}
	else
	{
		assert(false);
	}
}

void __fastcall shil_compile_floatfpul(shil_opcode* op)
{
	assert(0==(op->flags & (FLAG_IMM1|FLAG_IMM2|FLAG_REG2)));
	u32 sz=op->flags & 3;
	if (sz==FLAG_32)
	{
		assert(Ensure32());
		assert(!IsReg64((Sh4RegType)op->reg1));

		//TODO : This is not entietly correct , sh4 rounds too [need to set MXCSR]
		ppc_fpr_reg r1=fra->GetRegister(FR0,op->reg1,RA_NODATA);
		
		//lousy 64bit sign extension...
		ppce->emitLoad32(R3,GetRegPtr(reg_fpul));
		EMIT_RLWINM(ppce,R3,R3,1,31,31);
		EMIT_NEG(ppce,R3,R3);
		ppce->emitStore32(GetRegPtr(reg_fpul)-1,R3);
				
		ppce->emitLoadDouble(r1,GetRegPtr(reg_fpul)-1);
		EMIT_FCFID(ppce,r1,r1);
		fra->SaveRegister(op->reg1,r1);
		
	}
	else
	{
		assert(false);
	}
}

//#define _CHEAP_FTRC_FIX

f32 fpr_ftrc_saturate=0x7FFFFFBF;//1.11111111111111111111111 << 31
void __fastcall shil_compile_ftrc(shil_opcode* op)
{
	assert(0==(op->flags & (FLAG_IMM1|FLAG_IMM2|FLAG_REG2)));
	u32 sz=op->flags & 3;
	if (sz==FLAG_32)
	{
		assert(Ensure32());
		assert(!IsReg64((Sh4RegType)op->reg1));

		//TODO : This is not entietly correct , sh4 saturates too -> its correct now._CHEAP_FTRC_FIX can be defined for a cheaper, but not 100% accurate version
		//EAX=(int)saturate(fr[n])

		ppc_fpr_reg r1=fra->GetRegister(FR0,op->reg1,RA_DEFAULT);


#ifdef _CHEAP_FTRC_FIX 
		ppce->emitLoadFloat(FR1,&fpr_ftrc_saturate);
		EMIT_FSUB(ppce,FR2,r1,FR1,0);
		EMIT_FSEL(ppce,r1,FR2,FR1,r1);
#endif

		EMIT_FCTIWZ(ppce,FR0,r1);

#ifndef _CHEAP_FTRC_FIX 
/*
		ppc_gpr_reg r2=ECX;
		fra->LoadRegisterGPR(r2,op->reg1);
		assert(r2!=EAX);
		ppce->Emit(op_shr32,r2,31);			//sign -> LSB
		ppce->Emit(op_add32,r2,0x7FFFFFFF);	//0x7FFFFFFF for +, 0x80000000 for -
		ppce->Emit(op_cmp32,EAX,0x80000000);//is result indefinitive ?
		ppce->Emit(op_cmove32,EAX,r2);		//if yes, saturate		
*/
#endif

		EMIT_LI(ppce,R3,(u32)GetRegPtr(reg_fpul)-(u32)&sh4r);
		EMIT_STFIWX(ppce,FR0,R3,RSH4R);
		ira->ReloadRegister(reg_fpul);
	}
	else
	{
		assert(false);
	}
}

//Mixed opcodes (sse & x87)
void __fastcall shil_compile_fsca(shil_opcode* op)
{
	assert(0==(op->flags & (FLAG_IMM1|FLAG_IMM2|FLAG_REG2)));
	u32 sz=op->flags & 3;
	if (sz==FLAG_32)
	{
		assert(Ensure32());

		assert(!IsReg64((Sh4RegType)op->reg1));
		//float real_pi=(((float)(s32)fpul)/65536)*(2*pi);
		//real_pi=(s32)fpul * ((2*pi)/65536.0f);
		
		/*
		//that is the old way :P

		ppce->Emit(op_fild32i,ppc_ptr(GetRegPtr(reg_fpul)));		//st(0)=(s32)fpul
		ppce->Emit(op_fmul32f,ppc_ptr(fsca_fpul_adj));			//st(0)=(s32)fpul * ((2*pi)/65536.0f)
		ppce->Emit(op_fsincos);						//st(0)=sin , st(1)=cos
		
		ppce->Emit(op_fstp32f,ppc_ptr(GetRegPtr(op->reg1 +1)));	//Store cos to reg+1
		ppce->Emit(op_fstp32f,ppc_ptr(GetRegPtr(op->reg1)));		//store sin to reg

		fra->ReloadRegister(op->reg1+1);
		fra->ReloadRegister(op->reg1);
		*/


		///THIS IS THE NEW way :p
		/*
		u32 pi_index=fpul&0xFFFF;
		
		fr[n | 0] = sin_table[pi_index];
		fr[n | 1] = sin_table[(16384 + pi_index) & 0xFFFF];
		*/
		//ppce->Emit(op_int3);
		ppc_fpr_reg r1=fra->GetRegister(FR0,op->reg1,RA_NODATA);	//to store sin
		ppc_fpr_reg r2=fra->GetRegister(FR1,op->reg1+1,RA_NODATA);	//to store cos

		verify(!ira->IsRegAllocated(reg_fpul));
		ppce->emitLoad32(R4,GetRegPtr(reg_fpul));			//we do the 'and' here :p
		EMIT_RLWINM(ppce,R4,R4,2,14,29);
		EMIT_ADDIS(ppce,R4,R4,HA((u32)sin_table));
		EMIT_ADDI(ppce,R4,R4,(u32)sin_table);
		
		EMIT_LFS(ppce,r1,0,R4); //r1=sin
		
		//cos(x) = sin (pi/2 + x) , we add 1/4 of 2pi (2^16/4)
		EMIT_ADDIS(ppce,R4,R4,1);
		EMIT_LFS(ppce,r2,0,R4); //r2=cos, table has 0x4000 more for warping :)

		fra->SaveRegister(op->reg1,r1);
		fra->SaveRegister(op->reg1 + 1,r2);
	}
	else
	{
		assert(false);
	}
}
//Vector opcodes ;)
void __fastcall shil_compile_ftrv(shil_opcode* op)
{
	assert(0==(op->flags & (FLAG_IMM1|FLAG_IMM2|FLAG_REG2)));
	u32 sz=op->flags & 3;
	if (sz==FLAG_32)
	{
#if 0
		fra->FlushRegister(op->reg1);
		fra->FlushRegister(op->reg1+1);
		fra->FlushRegister(op->reg1+2);
		fra->FlushRegister(op->reg1+3);

		assert(!IsReg64((Sh4RegType)op->reg1));
		assert(Ensure32());

		ppce->emitLoadImmediate32(R3,(u32)GetRegPtr(op->reg1));
		ppce->emitLoadImmediate32(R4,(u32)GetRegPtr(xf_0));
		ppce->emitBranch((void*)Mat44TransformVector,1);
		
		fra->ReloadRegister(op->reg1);
		fra->ReloadRegister(op->reg1+1);
		fra->ReloadRegister(op->reg1+2);
		fra->ReloadRegister(op->reg1+3);
#else
		ppc_reg v0=fra->GetRegister(FR1,op->reg1,RA_FORCE);
		ppc_reg v1=fra->GetRegister(FR2,op->reg1+1,RA_FORCE);
		ppc_reg v2=fra->GetRegister(FR3,op->reg1+2,RA_FORCE);
		ppc_reg v3=fra->GetRegister(FR4,op->reg1+3,RA_FORCE);
		
		ppce->emitLoadFloat(FR5,GetRegPtr(xf_0));
		ppce->emitLoadFloat(FR9,GetRegPtr(xf_1));
		ppce->emitLoadFloat(FR6,GetRegPtr(xf_4));
		ppce->emitLoadFloat(FR10,GetRegPtr(xf_5));
		ppce->emitLoadFloat(FR7,GetRegPtr(xf_8));
		ppce->emitLoadFloat(FR11,GetRegPtr(xf_9));
		ppce->emitLoadFloat(FR8,GetRegPtr(xf_12));
		ppce->emitLoadFloat(FR12,GetRegPtr(xf_13));

		EMIT_FMUL(ppce,FR0,FR5,v0,0);
		EMIT_FMUL(ppce,FR13,FR9,v0,0);
		EMIT_FMADDS(ppce,FR0,FR6,v1,FR0);
		EMIT_FMADDS(ppce,FR13,FR10,v1,FR13);
		EMIT_FMADDS(ppce,FR0,FR7,v2,FR0);
		EMIT_FMADDS(ppce,FR13,FR11,v2,FR13);
		EMIT_FMADDS(ppce,FR0,FR8,v3,FR0);
		EMIT_FMADDS(ppce,FR13,FR12,v3,FR13);

		ppce->emitLoadFloat(FR5,GetRegPtr(xf_2));
		ppce->emitLoadFloat(FR9,GetRegPtr(xf_3));
		ppce->emitLoadFloat(FR6,GetRegPtr(xf_6));
		ppce->emitLoadFloat(FR10,GetRegPtr(xf_7));
		ppce->emitLoadFloat(FR7,GetRegPtr(xf_10));
		ppce->emitLoadFloat(FR11,GetRegPtr(xf_11));
		ppce->emitLoadFloat(FR8,GetRegPtr(xf_14));
		ppce->emitLoadFloat(FR12,GetRegPtr(xf_15));
		
		fra->SaveRegister(op->reg1,FR0);
		fra->SaveRegister(op->reg1+1,FR13);
		
		EMIT_FMUL(ppce,FR0,FR5,v0,0);
		EMIT_FMUL(ppce,FR13,FR9,v0,0);
		EMIT_FMADDS(ppce,FR0,FR6,v1,FR0);
		EMIT_FMADDS(ppce,FR13,FR10,v1,FR13);
		EMIT_FMADDS(ppce,FR0,FR7,v2,FR0);
		EMIT_FMADDS(ppce,FR13,FR11,v2,FR13);
		EMIT_FMADDS(ppce,FR0,FR8,v3,FR0);
		EMIT_FMADDS(ppce,FR13,FR12,v3,FR13);

		fra->SaveRegister(op->reg1+2,FR0);
		fra->SaveRegister(op->reg1+3,FR13);
#endif
	}
	else
	{
		assert(false);
	}
}

void __fastcall shil_compile_fipr(shil_opcode* op)
{
	assert(0==(op->flags & (FLAG_IMM1|FLAG_IMM2)));
	u32 sz=op->flags & 3;
	if (sz==FLAG_32)
	{
		assert(Ensure32());

#if 1
		ppc_fpr_reg v0[4],v1[4];
		u32 i;
		for(i=0;i<4;++i)
		{
			v0[i]=fra->GetRegister((ppc_reg)(FR1+i),op->reg1+i,RA_DEFAULT);
			v1[i]=fra->GetRegister((ppc_reg)(FR5+i),op->reg2+i,RA_DEFAULT);
		}

		EMIT_FMUL(ppce,FR9,v0[0],v1[0],0);
		EMIT_FMADDS(ppce,FR10,v0[1],v1[1],FR9);
		EMIT_FMADDS(ppce,FR9,v0[2],v1[2],FR10);
		EMIT_FMADDS(ppce,v0[3],v0[3],v1[3],FR9);

		fra->SaveRegister(op->reg1+3,v0[3]);
#else		
		fra->FlushRegister(op->reg1);
		fra->FlushRegister(op->reg1+1);
		fra->FlushRegister(op->reg1+2);
		fra->FlushRegister(op->reg1+3);

		fra->FlushRegister(op->reg2);
		fra->FlushRegister(op->reg2+1);
		fra->FlushRegister(op->reg2+2);
		fra->FlushRegister(op->reg2+3);

		ppce->emitLoadImmediate32(R3,(u32)GetRegPtr(op->reg1));
		ppce->emitLoadImmediate32(R4,(u32)GetRegPtr(op->reg2));
		ppce->emitBranch((void*)DotProduct,1);
		ppce->emitStoreFloat(GetRegPtr(op->reg1+3),FR1);
		
		fra->ReloadRegister(op->reg1+3);
#endif
	}
	else
	{
		assert(false);
	}
}

//Default handler , should never be called
void __fastcall shil_compile_nimp(shil_opcode* op)
{
	log("*********SHIL \"%s\" not recompiled*********\n\n",GetShilName((shil_opcodes)op->opcode));
	asm volatile("sc");
}

//#define PROF_IFB
u32 op_usage[0x10000]={0};

//shil_ifb opcode
//calls interpreter to emulate the opcode
void __fastcall shil_compile_shil_ifb(shil_opcode* op)
{

	//if opcode needs pc , save it
	if (OpTyp[op->imm1] !=Normal)
		SaveReg(reg_pc,op->imm2);
	
#if 1
	// bunch of heuristics to lower the number of flushed regs per ifb ops
	
	if (OpTyp[op->imm1] !=Normal)
		ira->FlushRegister(reg_pc);

	ira->FlushRegister(r0);
	
	if (OpDesc[op->imm1])
	{
		switch(OpDesc[op->imm1]->mask)
		{
			case Mask_imm8:
				break;

			case Mask_n_m:
			case Mask_n_m_imm4:
			case Mask_n_ml3bit:
				ira->FlushRegister(GetN(op->imm1));
				ira->FlushRegister(GetM(op->imm1));
				break;

			default:
				ira->FlushRegCache();
				break;
		}
		
		if ((OpDesc[op->imm1]->type & WritesFPSCR) || (op->imm1>=0xf000)) fra->FlushRegCache();
	}
	else
	{
		ira->FlushRegCache();
		fra->FlushRegCache();
	}
#else
	ira->FlushRegCache();
	fra->FlushRegCache();
#endif

#ifdef PROF_IFB
	ppce->emitLoad32(R3,&op_usage[op->imm1]);
	EMIT_ADDI(ppce,R3,R3,1);
	ppce->emitStore32(&op_usage[op->imm1],R3);
#endif

	// get T flag for interp
	EMIT_CROR(ppce,PPC_CC_ZER,CR_T_FLAG,CR_T_FLAG);
	EMIT_LI(ppce,R3,1);
	EMIT_BC(ppce,2,0,0,PPC_CC_T,PPC_CC_ZER);
	EMIT_LI(ppce,R3,0);
	ppce->emitStore32(GetRegPtr(reg_sr_T),R3);
	
	ppce->emitLoadImmediate32(R3,op->imm1);
	ppce->emitBranch((void*)OpPtr[op->imm1],1);

	// store interp T flag for dynarec
	ppce->emitLoad32(R3,GetRegPtr(reg_sr_T));
	EMIT_CMPLI(ppce,R3,0,0);
	EMIT_CRNOR(ppce,CR_T_FLAG,PPC_CC_ZER,PPC_CC_ZER);
}


//decoding table ;)

shil_compileFP* sclt[shilop_count]=
{
	shil_compile_nimp,shil_compile_nimp,shil_compile_nimp,shil_compile_nimp,
	shil_compile_nimp,shil_compile_nimp,shil_compile_nimp,shil_compile_nimp,
	shil_compile_nimp,shil_compile_nimp,shil_compile_nimp,shil_compile_nimp,
	shil_compile_nimp,shil_compile_nimp,shil_compile_nimp,shil_compile_nimp,
	shil_compile_nimp,shil_compile_nimp,shil_compile_nimp,shil_compile_nimp,
	shil_compile_nimp,shil_compile_nimp,shil_compile_nimp,shil_compile_nimp,
	shil_compile_nimp,shil_compile_nimp,shil_compile_nimp,shil_compile_nimp,
	shil_compile_nimp,shil_compile_nimp,shil_compile_nimp,shil_compile_nimp,
	shil_compile_nimp,shil_compile_nimp,shil_compile_nimp,shil_compile_nimp,
	shil_compile_nimp,shil_compile_nimp,shil_compile_nimp,shil_compile_nimp,
	shil_compile_nimp,shil_compile_nimp,shil_compile_nimp,shil_compile_nimp,
	shil_compile_nimp,shil_compile_nimp
};

void SetH(shil_opcodes op,shil_compileFP* ha)
{
	if (op>(shilop_count-1))
	{
		log("SHIL COMPILER ERROR\n");
	}
	if (sclt[op]!=shil_compile_nimp)
	{
		log("SHIL COMPILER ERROR [hash table overwrite]\n");
	}

	sclt[op]=ha;
}
bool sclt_inited=false;
void sclt_Init()
{
	//
	SetH(shilop_ifb,shil_compile_shil_ifb);
	SetH(shilop_adc,shil_compile_adc);
	SetH(shilop_add,shil_compile_add);
	SetH(shilop_and,shil_compile_and);
	SetH(shilop_or,shil_compile_or);
	SetH(shilop_sub,shil_compile_sub);
	SetH(shilop_xor,shil_compile_xor);
	SetH(shilop_LoadT,shil_compile_LoadT);
	SetH(shilop_sar,shil_compile_sar);
	SetH(shilop_shl,shil_compile_shl);
	SetH(shilop_shr,shil_compile_shr);
	SetH(shilop_shld,shil_compile_shld);
	SetH(shilop_SaveT,shil_compile_SaveT);
	SetH(shilop_cmp,shil_compile_cmp);
	SetH(shilop_test,shil_compile_test);
	SetH(shilop_rcl,shil_compile_rcl);
	SetH(shilop_rcr,shil_compile_rcr);
	SetH(shilop_rol,shil_compile_rol);
	SetH(shilop_ror,shil_compile_ror);
	SetH(shilop_neg,shil_compile_neg);
	SetH(shilop_not,shil_compile_not);
	SetH(shilop_mov,shil_compile_mov);
	SetH(shilop_readm,shil_compile_readm);
	SetH(shilop_writem,shil_compile_writem);
	SetH(shilop_pref,shil_compile_pref);
	SetH(shilop_fadd,shil_compile_fadd);
	SetH(shilop_fsub,shil_compile_fsub);
	SetH(shilop_fmul,shil_compile_fmul);
	SetH(shilop_fdiv,shil_compile_fdiv);
	SetH(shilop_fcmp,shil_compile_fcmp);
	SetH(shilop_fmac,shil_compile_fmac);
	SetH(shilop_movex,shil_compile_movex);
	SetH(shilop_fabs,shil_compile_fabs);
	SetH(shilop_fneg,shil_compile_fneg);
	SetH(shilop_fsqrt,shil_compile_fsqrt);
	SetH(shilop_ftrc,shil_compile_ftrc);
	SetH(shilop_mul,shil_compile_mul);
	SetH(shilop_swap,shil_compile_swap);
	SetH(shilop_fsrra,shil_compile_fsrra);
	SetH(shilop_floatfpul,shil_compile_floatfpul);
	SetH(shilop_fsca,shil_compile_fsca);
	SetH(shilop_ftrv,shil_compile_ftrv);
	SetH(shilop_fipr,shil_compile_fipr);
	SetH(shilop_div32,shil_compile_div32);
		
	/*
	u32 shil_nimp=shilop_count;
	for (int i=0;i<shilop_count;i++)
	{
		if (sclt[i]==shil_compile_nimp)
			shil_nimp--;
	}

	//log("lazy shil compiler stats : %d%% opcodes done\n",shil_nimp*100/shilop_count);
	*/
}


//#define PROF_SHIL

void shil_compile(shil_opcode* op)
{
	if (op->opcode>(shilop_count-1))
	{
		log("SHIL COMPILER ERROR\n");
	}
//	log("SHIL %s\n",GetShilName((shil_opcodes)op->opcode));

#ifdef PROF_SHIL 
	ppce->emitLoad32(R3,&op_usage[op->opcode]);
	EMIT_ADDI(ppce,R3,R3,1);
	ppce->emitStore32(&op_usage[op->opcode],R3);
#endif
 
	sclt[op->opcode](op);
}

void shil_compiler_init(ppc_block* block,IntegerRegAllocator* _ira,FloatRegAllocator* _fra)
{
	if (!sclt_inited)
	{
		sclt_Init();
		sclt_inited=true;
	}

	fra = _fra;
	ira = _ira;
	ppce = block;
}
