#include "types.h"
#include "emitter.h"
#include <dc/sh4/sh4_registers.h>
extern "C"{
#include <ppc/cache.h>
}

int ppc_condition_flags[][3] = { // bo,bi,logical
	{PPC_CC_T, PPC_CC_OVR,0}, // 0
	{PPC_CC_F, PPC_CC_OVR,0}, // 1

	{PPC_CC_T, PPC_CC_NEG,1}, // 2
	{PPC_CC_F, PPC_CC_NEG,1}, // 3

	{PPC_CC_T, PPC_CC_ZER,0}, // 4
	{PPC_CC_F, PPC_CC_ZER,0}, // 5

	{PPC_CC_F, PPC_CC_POS,1}, // 6
	{PPC_CC_T, PPC_CC_POS,1}, // 7

	{PPC_CC_A, PPC_CC_NEG,0}, // 8 fake
	{PPC_CC_A, PPC_CC_NEG,0}, // 9 fake

	{PPC_CC_A, PPC_CC_NEG,0}, // A fake
	{PPC_CC_A, PPC_CC_NEG,0}, // B fake

	{PPC_CC_T, PPC_CC_NEG,0}, // C
	{PPC_CC_F, PPC_CC_NEG,0}, // D

	{PPC_CC_F, PPC_CC_POS,0}, // E
	{PPC_CC_T, PPC_CC_POS,0}, // F
};

bool IsS8(u32 value)
{
	if (((value&0xFFFFFF80)==0xFFFFFF80) ||
		(value&0xFFFFFF80)==0  )
		return true;
	else
		return false;
}

//ppc_Label 
/*
//ppc_ptr/ppc_ptr_imm
ppc_ptr ppc_ptr::create(void* ptr)
{
	ppc_ptr rv={ptr};
	return rv;
}*/
ppc_ptr ppc_ptr::create(unat ptr)
{
	ppc_ptr rv(0);
	rv.ptr_int=ptr;
	return rv;
}
/*
ppc_ptr_imm ppc_ptr_imm::create(void* ptr)
{
	ppc_ptr_imm rv={ptr};
	return rv;
}*/
ppc_ptr_imm ppc_ptr_imm::create(unat ptr)
{
	ppc_ptr_imm rv(0);
	rv.ptr_int=ptr;
	return rv;
}
//ppc_block
//init things

void* ppc_Label::GetPtr()
{
	return owner->ppc_buff + this->target_opcode;
}

void ppc_block::Init(dyna_reallocFP* ral,dyna_finalizeFP* alf)
{
	ralloc=ral;
	allocfin=alf;
	ppc_buff=0;
	ppc_indx=0;
	ppc_size=0;
	do_realloc=true;
}
#define patches (*(vector<code_patch>*) _patches)
#define labels (*(vector<ppc_Label*>*) _labels)

//Generates code.if user_data is non zero , user_data_size bytes are allocated after the executable code
//and user_data is set to the first byte of em.Allways 16 byte alligned
void* ppc_block::Generate()
{
	if (do_realloc)
	{
		u8* final_buffer=0;

		final_buffer=(u8*)allocfin(ppc_buff,ppc_size,ppc_indx);

		if (final_buffer==0)
			return 0;

		ppc_buff=final_buffer;
	}
	
	ApplyPatches(ppc_buff);

	memicbi(ppc_buff,ppc_indx);
	
	bool force=false;
	
//	force=true;
	
	if(do_disasm || force) printf("Gen %p %04x %04x\n",ppc_buff,ppc_size,ppc_indx);
	
	if (do_disasm || force) for(u32 i=0;i<ppc_indx;i+=4) disassemble((u32)&ppc_buff[i],*(u32*)&ppc_buff[i]);
	do_disasm=false;
	
	return &ppc_buff[0];
}

struct ppc_block_externs_i : ppc_block_externs
{
	struct extern_entry { u8* dst;u32 offs:28;u32 size:4; };
	vector<extern_entry> externs;

	void Apply(u8* base)
	{
		for (u32 i=0;i<externs.size();i++)
		{
			u8* dest=(u8*)externs[i].dst;

			u8* code_offset=base+externs[i].offs;
			u8* diff_offset=code_offset+externs[i].size;

			u32 diff=(u32)(dest-diff_offset);
			if (externs[i].size==1)
			{
				verify(IsS8(diff));
				*code_offset=(u8)diff;
			}
			else if (externs[i].size==2)
			{
				*(u16*)code_offset=(u16)diff;
			}
			else if (externs[i].size==4)
			{
				*(u32*)code_offset=(u32)diff;
			}
		}
	}
	bool Modify(u32 offs,u8* dst)
	{
		for (u32 i=0;i<externs.size();i++)
		{
			if (externs[i].offs==offs)
			{
				externs[i].dst=dst;
				return true;
			}
		}
		return false;
	}
	void Add(u8* dst,u32 offset,u32 size)
	{
		extern_entry vta={dst,offset,size};
		externs.push_back(vta);
	}
	void Free()
	{
		delete this;
	}
};

ppc_block_externs::~ppc_block_externs() {}
void ppc_block_externs::Apply(void* lolwhut) { ((ppc_block_externs_i*)this)->Apply((u8*)lolwhut); }
bool ppc_block_externs::Modify(u32 offs,u8* dst) { return ((ppc_block_externs_i*)this)->Modify(offs,dst); }
void ppc_block_externs::Free() { ((ppc_block_externs_i*)this)->Free(); }

ppc_block_externs* ppc_block::GetExterns()
{
	ppc_block_externs_i* rv=new ppc_block_externs_i();
	u8* base=ppc_buff;
	for (u32 i=0;i<patches.size();i++)
	{
		u8* dest=(u8*)patches[i].dest;

		if (patches[i].type&16)
		{
			if (patches[i].lbl->owner==this)
				dest = base + patches[i].lbl->target_opcode;
			else
				dest = patches[i].lbl->owner->ppc_buff + patches[i].lbl->target_opcode;
		}

		u32 start_diff=(u32)(dest-base);
		if (start_diff>=ppc_indx)
		{
			rv->Add(dest,patches[i].offset,patches[i].type&0xF);
		}
	}

	return rv;
}

/*void ppc_block::CopyTo(void* to)
{
	memcpy(to,ppc_buff,ppc_indx);
	free(ppc_buff);
	ppc_buff=(u8*)to;

	ApplyPatches(ppc_buff);
}
*/

//wut ?
void ppc_block::ApplyPatches(u8* base)
{
	for (u32 i=0;i<patches.size();i++)
	{
		u8* dest=(u8*)patches[i].dest;

		u8* code_offset=base+patches[i].offset;
		u8* diff_offset=code_offset;//gli wtf?!? +(patches[i].type&0xF);

		if (patches[i].type&16)
		{
			if (patches[i].lbl->owner==this)
				dest = base + patches[i].lbl->target_opcode;
			else
				dest = patches[i].lbl->owner->ppc_buff + patches[i].lbl->target_opcode;

		}

		s32 diff=(s32)(dest-diff_offset);
		
		if ((patches[i].type&0xF)==1)
		{
			verify(IsS8(diff));
			*code_offset=(u8)diff;
		}
		else if ((patches[i].type&0xF)==2)
		{
			*(u16*)code_offset=(u16)diff;
		}
		else if ((patches[i].type&0xF)==4)
		{
			*(u32*)code_offset=(u32)diff;
		}
		else if ((patches[i].type&0xF)==5) // 5 is for BC
		{
			verify(!(diff&3));
			verify(diff<0x10000);
			*(u32*)code_offset|=(u32)diff&0xffff;
		}
		else if ((patches[i].type&0xF)==6) // 6 is for B
		{
			verify(!(diff&3));
			verify(diff<0x04000000);
			*(u32*)code_offset|=(u32)diff&0x03ffffff;
		}
	}

	/* process branches */
	u32 i;
	PowerPC_instr op,newop;
	u32 opaddr,jmpaddr;
	for(i=0;i<ppc_indx;i+=4)
	{
		opaddr=(u32)&ppc_buff[i];
		op=*(u32*)&ppc_buff[i];

		jmpaddr=(op&0x03ffffff)-(opaddr&0x7fffffff);
		
		if((op&0xfc000000) == 0) // b
		{
			//printf("b %08x %08x %08x\n",opaddr,op,jmpaddr);
			GEN_B(newop,jmpaddr>>2,0,0);
			*(PowerPC_instr*)opaddr=newop;
		}
		else if((op&0xfc000000) == 1<<26) // bl
		{
			//printf("bl %08x %08x %08x\n",opaddr,op,jmpaddr);
			GEN_B(newop,jmpaddr>>2,0,1);
			*(PowerPC_instr*)opaddr=newop;
		}
	}
}
ppc_block::ppc_block()
{
	_patches=new vector<code_patch>;
	_labels=new vector<code_patch>;
	labels.reserve(64);
	do_disasm=false;
}
ppc_block::~ppc_block()
{
	//ensure everything is free'd :)
	Free();
	delete &patches;
	delete &labels;
}
//Will free any used resources exept generated code
void ppc_block::Free()
{
	for (u32 i =0;i<labels.size();i++)
		delete labels[i];
	labels.clear();
}
void ppc_block::ppc_buffer_ensure(u32 size)
{
	if (this->ppc_size<(size+ppc_indx))
	{
		verify(do_realloc!=false);
		u32 old_size=ppc_size;
		ppc_size+=128;
		ppc_size*=2;
		ppc_buff=(u8*)ralloc(ppc_buff,old_size,ppc_size);
	}
}
void  ppc_block::write8(u32 value)
{
	ppc_buffer_ensure(15);
	//printf("ppc_block::write8 %02X\n",value);
	ppc_buff[ppc_indx]=value;
	ppc_indx+=1;
}
void  ppc_block::write16(u32 value)
{
	ppc_buffer_ensure(15);
	//printf("ppc_block::write16 %04X\n",value);
	*(u16*)&ppc_buff[ppc_indx]=value;
	ppc_indx+=2;
}
void  ppc_block::write32(u32 value)
{
	ppc_buffer_ensure(15);
	//printf("ppc_block::write32 %08X\n",value);
	*(u32*)&ppc_buff[ppc_indx]=value;
	ppc_indx+=4;
}

//Label related code

//NOTE : Label position in mem must not chainge
void ppc_block::CreateLabel(ppc_Label* lbl,bool mark,u32 sz)
{
	memset(lbl,0xFFFFFFFF,sizeof(ppc_Label));
	lbl->owner=this;
	lbl->marked=false;
	lbl->patch_sz=sz;
	if (mark)
		MarkLabel(lbl);
}
//Allocate a label and create it :).Will be delete'd when calling free and/or dtor
ppc_Label* ppc_block::CreateLabel(bool mark,u32 sz)
{
	ppc_Label* lbl = new ppc_Label();
	CreateLabel(lbl,mark,sz);
	labels.push_back(lbl);
	return lbl;
}
//Mark a label so that it points to next emitted opcode
void ppc_block::MarkLabel(ppc_Label* lbl)
{
	verify(lbl->marked==false);
	lbl->marked=true;
	lbl->target_opcode=ppc_indx;
	//lbl->target_opcode=(u32)opcodes.size();
}

ppc_gpr_reg ppc_block::getHighReg(u16 value)
{
	verify((((u32)(&sh4r))&0xffff)==0);
	if(value==HA((u32)&sh4r))
	{
		return (ppc_reg)RSH4R;
	}
	
	EMIT_LIS(this,RTMP,value);
	return (ppc_reg)RTMP;
}

void ppc_block::emitBranch(void * addr, int lk)
{
	u32 faa=(u32)addr&0x7fffffff;	
	u32 aa=faa&0x03ffffff;
	
	if(aa==faa) // 26 bits max for rel
	{
		write32(aa|((lk?1:0)<<26)); // primary opcode 0=b; 1=bl
	}
	else
	{
		emitLongBranch(addr,lk);
	}
}

void ppc_block::emitLongBranch(void * addr, int lk)
{
	EMIT_LIS(this,RTMP,((u32)addr)>>16);
	EMIT_ORI(this,RTMP,RTMP,(u32)addr);
	EMIT_MTLR(this,RTMP);
	EMIT_BLR(this,lk);
}

void ppc_block::emitReverseBranchConditional(void * addr, int bo, int bi, int lk)
{
	EMIT_BC(this,2,0,0,bo,bi); // makes the assumption that emitBranch will only generate 1 op
	u32 idx=ppc_indx;
	emitBranch(addr,lk);
	verify(ppc_indx==idx+4);
}

void ppc_block::emitLoadDouble(ppc_fpr_reg reg, void * addr)
{
	ppc_reg hr=getHighReg(HA((u32)addr));
	EMIT_LFD(this,reg,(u32)addr,hr);
}
	
void ppc_block::emitLoadFloat(ppc_fpr_reg reg, void * addr)
{
	ppc_reg hr=getHighReg(HA((u32)addr));
	EMIT_LFS(this,reg,(u32)addr,hr);
}
	
void ppc_block::emitLoad32(ppc_gpr_reg reg, void * addr)
{
	ppc_reg hr=getHighReg(HA((u32)addr));
	EMIT_LWZ(this,reg,(u32)addr,hr);
}

void ppc_block::emitLoad16(ppc_gpr_reg reg, void * addr)
{
	ppc_reg hr=getHighReg(HA((u32)addr));
	EMIT_LHZ(this,reg,(u32)addr,hr);
}

void ppc_block::emitLoad8(ppc_gpr_reg reg, void * addr)
{
	ppc_reg hr=getHighReg(HA((u32)addr));
	EMIT_LBZ(this,reg,(u32)addr,hr);
}

void ppc_block::emitStoreDouble(void * addr, ppc_fpr_reg reg)
{
	ppc_reg hr=getHighReg(HA((u32)addr));
	EMIT_STFD(this,reg,(u32)addr,hr);
}

void ppc_block::emitStoreFloat(void * addr, ppc_fpr_reg reg)
{
	ppc_reg hr=getHighReg(HA((u32)addr));
	EMIT_STFS(this,reg,(u32)addr,hr);
}

void ppc_block::emitStore32(void * addr, ppc_gpr_reg reg)
{
	ppc_reg hr=getHighReg(HA((u32)addr));
	EMIT_STW(this,reg,(u32)addr,hr);
}

void ppc_block::emitStore16(void * addr, ppc_gpr_reg reg)
{
	ppc_reg hr=getHighReg(HA((u32)addr));
	EMIT_STH(this,reg,(u32)addr,hr);
}

void ppc_block::emitStore8(void * addr, ppc_gpr_reg reg)
{
	ppc_reg hr=getHighReg(HA((u32)addr));
	EMIT_STB(this,reg,(u32)addr,hr);
}

void ppc_block::emitMoveRegister(ppc_gpr_reg to,ppc_gpr_reg from)
{
	EMIT_OR(this,to,from,from);
}

void ppc_block::emitLoadImmediate32(ppc_gpr_reg reg, u32 val)
{
	if(val&0xffff0000)
	{
		EMIT_LIS(this,reg,val>>16);
		if (val&0xffff) EMIT_ORI(this,reg,reg,val);
	}
	else
	{
		EMIT_LI(this,reg,val);
	}
}

void ppc_block::emitBranchConditionalToLabel(ppc_Label * lab,int lk,int bo,int bi)
{
	code_patch cp;
	
	cp.type=5|16;
	cp.lbl=lab;
	cp.offset=ppc_indx; // next op
	patches.push_back(cp);	
	
	EMIT_BC(this,0,0,lk,bo,bi);
}

void ppc_block::emitBranchToLabel(ppc_Label * lab,int lk)
{
	code_patch cp;
	
	cp.type=6|16;
	cp.lbl=lab;
	cp.offset=ppc_indx; // next op
	patches.push_back(cp);	

	EMIT_B(this,0,0,lk);
}

extern "C" {

void debugValue(u32 value){
	printf("dv %d(%x)\n",value,value);
}

}

static u32 r[32];

void ppc_block::emitDebugValue(u32 value)
{
	emitStore32(&r[3],R3);	
	emitStore32(&r[4],R4);	
	emitStore32(&r[5],R5);
	emitLoadImmediate32(R3,value);
	emitBranch((void*)debugValue,1);
	emitLoad32(R3,&r[3]);	
	emitLoad32(R4,&r[4]);	
	emitLoad32(R5,&r[5]);	
}

void ppc_block::emitDebugReg(ppc_gpr_reg reg)
{
	emitStore32(&r[3],R3);	
	emitStore32(&r[4],R4);	
	emitStore32(&r[5],R5);
	if (reg!=R3) emitMoveRegister(R3,reg);
	emitBranch((void*)debugValue,1);
	emitLoad32(R3,&r[3]);	
	emitLoad32(R4,&r[4]);	
	emitLoad32(R5,&r[5]);	
}
