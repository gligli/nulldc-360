#include "types.h"
#include "emitter.h"

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
		u8* diff_offset=code_offset+(patches[i].type&0xF);

		if (patches[i].type&16)
		{
			if (patches[i].lbl->owner==this)
				dest = base + patches[i].lbl->target_opcode;
			else
				dest = patches[i].lbl->owner->ppc_buff + patches[i].lbl->target_opcode;

		}

		u32 diff=(u32)(dest-diff_offset);
		
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
			verify(diff>0x10000);
			*(u16*)code_offset|=(u16)diff;
		}
		else if ((patches[i].type&0xF)==6) // 6 is for B
		{
			verify(!(diff&3));
			verify(diff>0x4000000);
			*(u32*)code_offset|=(u32)diff;
		}
	}
}
ppc_block::ppc_block()
{
	_patches=new vector<code_patch>;
	_labels=new vector<code_patch>;
	labels.reserve(64);
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
	//printf("%02X ",value);
	ppc_buff[ppc_indx]=value;
	ppc_indx+=1;
}
void  ppc_block::write16(u32 value)
{
	ppc_buffer_ensure(15);
	//printf("%04X ",value);
	*(u16*)&ppc_buff[ppc_indx]=value;
	ppc_indx+=2;
}
void  ppc_block::write32(u32 value)
{
	ppc_buffer_ensure(15);
	//printf("%08X ",value);
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

void ppc_block::emitLongBranch(void * addr, int lk)
{
	emitMoveRegister(R0,R3);
	EMIT_LIS(this,R3,HA((u32)addr));
	EMIT_ADDI(this,R3,R3,(u32)addr);
	EMIT_MTCTR(this,R3);
	emitMoveRegister(R3,R0);
	if (lk)
	{
		EMIT_BCTRL(this);
	}
	else
	{
		EMIT_BCTR(this);
	}
}

void ppc_block::emitLoad32(ppc_gpr_reg reg, void * addr)
{
	EMIT_LIS(this,0,HA((u32)addr));
	EMIT_LWZ(this,reg,(u32)addr,0);
}

void ppc_block::emitLoad16(ppc_gpr_reg reg, void * addr)
{
	EMIT_LIS(this,0,HA((u32)addr));
	EMIT_LHZ(this,reg,(u32)addr,0);
}

void ppc_block::emitLoad8(ppc_gpr_reg reg, void * addr)
{
	EMIT_LIS(this,0,HA((u32)addr));
	EMIT_LBZ(this,reg,(u32)addr,0);
}

void ppc_block::emitStore32(void * addr, ppc_gpr_reg reg)
{
	EMIT_LIS(this,0,HA((u32)addr));
	EMIT_STW(this,reg,(u32)addr,0);
}

void ppc_block::emitStore16(void * addr, ppc_gpr_reg reg)
{
	EMIT_LIS(this,0,HA((u32)addr));
	EMIT_STH(this,reg,(u32)addr,0);
}

void ppc_block::emitStore8(void * addr, ppc_gpr_reg reg)
{
	EMIT_LIS(this,0,HA((u32)addr));
	EMIT_STB(this,reg,(u32)addr,0);
}

void ppc_block::emitMoveRegister(ppc_gpr_reg to,ppc_gpr_reg from)
{
	EMIT_OR(this,to,from,from);
}

void ppc_block::emitLoadImmediate32(ppc_gpr_reg reg, u32 val)
{
	EMIT_LIS(this,reg,HA(val));
	EMIT_ADDI(this,reg,reg,val);
}

void ppc_block::emitBranchConditionalToLabel(ppc_Label * lab,int lk,int bo,int bi)
{
	code_patch cp;
	
	cp.type=5|16;
	cp.lbl=lab;
	cp.offset=ppc_indx+2; // lower half of next op
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
