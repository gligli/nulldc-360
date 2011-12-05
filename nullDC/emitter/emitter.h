#pragma once
#include "types.h"

using namespace std;

#define REG_CLASS(regv) (regv>>16)
#define REG_ID(regv) (regv&0xFFFF)

enum reg_class
{
	reg_GPR=0<<5,
	reg_FPR=0<<5,
};

//Enum of all registers
enum ppc_reg
{
	R0 = reg_GPR,
	R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, R15, R16, R17,
	R18, R19, R20, R21, R22, R23, R24, R25, R26, R27, R28, R29, R30, R31,
	
	FR0 = reg_FPR,
	FR1, FR2, FR3, FR4, FR5, FR6, FR7, FR8, FR9, FR10, FR11, FR12, FR13, FR14, FR15, FR16, FR17,
	FR18, FR19, FR20, FR21, FR22, FR23, FR24, FR25, FR26, FR27, FR28, FR29, FR30, FR31,

	//misc :p
	NO_REG=-1,
	ERROR_REG=-2,
};

extern int ppc_condition_flags[][3];

#define ppc_fpr_reg ppc_reg
#define ppc_gpr_reg ppc_reg

// CR2 bit 2
#define CR_T_FLAG 10
#define CR_T_COND_FLAG 9

#define RTMP 10
#define RSH4R 2

#include "PowerPC.h"
extern "C" {
int disassemble(u32 a, u32 op);
}

//memory managment !
typedef void* dyna_reallocFP(void*ptr,u32 oldsize,u32 newsize);
typedef void* dyna_finalizeFP(void* ptr,u32 oldsize,u32 newsize);

//define it here cus we use it on label type ;)
class ppc_block;
// a label
struct ppc_Label
{
	u32 target_opcode;
	u8 patch_sz;
	ppc_block* owner;
	bool marked;
	void* GetPtr();
};


//An empty type that we will use as ptr type.This is ptr-reference
struct  ppc_ptr
{
	union
	{
		void* ptr;
		unat ptr_int;
	};
	static ppc_ptr create(unat ptr);
	ppc_ptr(void* ptr)
	{
		this->ptr=ptr;
	}
};
//This is ptr/imm (for call/jmp)
struct  ppc_ptr_imm
{
	union
	{
		void* ptr;
		unat ptr_int;
	};
	static ppc_ptr_imm create(unat ptr);
	ppc_ptr_imm(void* ptr)
	{
		this->ptr=ptr;
	}
};

struct code_patch
{
	u8 type;//0 = 8 bit , 2 = 16 bit  , 4 = 32 bit , 16[flag] is label
	union
	{
		void* dest;		//ptr for patch
		ppc_Label* lbl;	//lbl for patch
	};
	u32 offset;			//offset in opcode stream :)
};

struct ppc_block_externs
{
	void Apply(void* code_base);
	bool Modify(u32 offs,u8* dst);
	void Free();
	~ppc_block_externs();
};

class ppc_block
{
private:
	void* _labels;
	void ApplyPatches(u8* base);
	dyna_reallocFP* ralloc;
	dyna_finalizeFP* allocfin;
	u32 bc_tab[0x4000];
	u32 bc_tab_next_idx;
public:
	void* _patches;

	u8* ppc_buff;
	u32 ppc_indx;
	u32 ppc_size;
	bool do_realloc;
	bool do_disasm;
	bool do_disasm_imm;

	ppc_block();
	~ppc_block();
	void ppc_buffer_ensure(u32 size);

	void  write8(u32 value);
	void  write16(u32 value);
	void  write32(u32 value);

	//init things
	void Init(dyna_reallocFP* ral,dyna_finalizeFP* alf);

	//Generates code.
	void* Generate();
	ppc_block_externs* GetExterns();
	//void CopyTo(void* to);

	//Will free any used resources exept generated code
	void Free();

	//Label related code
	//NOTE : Label position in mem must not chainge
	void CreateLabel(ppc_Label* lbl,bool mark,u32 sz);
	//Allocate a label and create it :).Will be delete'd when calling free and/or dtor
	ppc_Label* CreateLabel(bool mark,u32 sz);
	void MarkLabel(ppc_Label* lbl);

	//When we want to keep info to mark opcodes dead , there is no need to create labels :p
	//Get an index to next emitted opcode
	u32 GetOpcodeIndex();
	
	ppc_gpr_reg getHighReg(u16 value);
	
	void emitBranch(void * addr, int lk);
	void emitLongBranch(void * addr, int lk);
	void emitBranchConditional(void * addr, int bo, int bi, int lk);
	void emitLoadDouble(ppc_fpr_reg reg, void * addr);
	void emitLoadFloat(ppc_fpr_reg reg, void * addr);
	void emitLoad32(ppc_gpr_reg reg, void * addr);
	void emitLoad16(ppc_gpr_reg reg, void * addr);
	void emitLoad8(ppc_gpr_reg reg, void * addr);
	void emitStoreDouble(void * addr, ppc_fpr_reg reg);
	void emitStoreFloat(void * addr, ppc_fpr_reg reg);
	void emitStore32(void * addr, ppc_gpr_reg reg);
	void emitStore16(void * addr, ppc_gpr_reg reg);
	void emitStore8(void * addr, ppc_gpr_reg reg);
	void emitMoveRegister(ppc_gpr_reg to,ppc_gpr_reg from);
	void emitLoadImmediate32(ppc_gpr_reg reg, u32 val);
	void emitBranchConditionalToLabel(ppc_Label * lab,int lk,int bo,int bi);
	void emitBranchToLabel(ppc_Label * lab,int lk);
	void emitDebugValue(u32 value);
	void emitDebugReg(ppc_gpr_reg reg);
};

#define EMIT_B(ppce,dst,aa,lk) \
{PowerPC_instr ppc;GEN_B(ppc,dst,aa,lk);ppce->write32(ppc);}
#define EMIT_MTCTR(ppce,rs) \
{PowerPC_instr ppc;GEN_MTCTR(ppc,rs);ppce->write32(ppc);}
#define EMIT_MFCTR(ppce,rd) \
{PowerPC_instr ppc;GEN_MFCTR(ppc,rd);ppce->write32(ppc);}
#define EMIT_ADDIS(ppce,rd,ra,immed) \
{PowerPC_instr ppc;GEN_ADDIS(ppc,rd,ra,immed);ppce->write32(ppc);}
#define EMIT_LIS(ppce,rd,immed) \
{PowerPC_instr ppc;GEN_LIS(ppc,rd,immed);ppce->write32(ppc);}
#define EMIT_LI(ppce,rd,immed) \
{PowerPC_instr ppc;GEN_LI(ppc,rd,immed);ppce->write32(ppc);}
#define EMIT_LWZ(ppce,rd,immed,ra) \
{PowerPC_instr ppc;GEN_LWZ(ppc,rd,immed,ra);ppce->write32(ppc);}
#define EMIT_LHZ(ppce,rd,immed,ra) \
{PowerPC_instr ppc;GEN_LHZ(ppc,rd,immed,ra);ppce->write32(ppc);}
#define EMIT_LHA(ppce,rd,immed,ra) \
{PowerPC_instr ppc;GEN_LHA(ppc,rd,immed,ra);ppce->write32(ppc);}
#define EMIT_LBZ(ppce,rd,immed,ra) \
{PowerPC_instr ppc;GEN_LBZ(ppc,rd,immed,ra);ppce->write32(ppc);}
#define EMIT_EXTSB(ppce,rd,rs) \
{PowerPC_instr ppc;GEN_EXTSB(ppc,rd,rs);ppce->write32(ppc);}
#define EMIT_EXTSH(ppce,rd,rs) \
{PowerPC_instr ppc;GEN_EXTSH(ppc,rd,rs);ppce->write32(ppc);}
#define EMIT_EXTSW(ppce,rd,rs) \
{PowerPC_instr ppc;GEN_EXTSW(ppc,rd,rs);ppce->write32(ppc);}
#define EMIT_STB(ppce,rs,immed,ra) \
{PowerPC_instr ppc;GEN_STB(ppc,rs,immed,ra);ppce->write32(ppc);}
#define EMIT_STH(ppce,rs,immed,ra) \
{PowerPC_instr ppc;GEN_STH(ppc,rs,immed,ra);ppce->write32(ppc);}
#define EMIT_STW(ppce,rs,immed,ra) \
{PowerPC_instr ppc;GEN_STW(ppc,rs,immed,ra);ppce->write32(ppc);}
#define EMIT_BCTR(ppce) \
{PowerPC_instr ppc;GEN_BCTR(ppc);ppce->write32(ppc);}
#define EMIT_BCTRL(ppce) \
{PowerPC_instr ppc;GEN_BCTRL(ppc);ppce->write32(ppc);}
#define EMIT_BCCTR(ppce,bo,bi,lk) \
{PowerPC_instr ppc;GEN_BCCTR(ppc,bo,bi,lk);ppce->write32(ppc);}
#define EMIT_CMP(ppce,ra,rb,cr) \
{PowerPC_instr ppc;GEN_CMP(ppc,ra,rb,cr);ppce->write32(ppc);}
#define EMIT_CMPL(ppce,ra,rb,cr) \
{PowerPC_instr ppc;GEN_CMPL(ppc,ra,rb,cr);ppce->write32(ppc);}
#define EMIT_CMPI(ppce,ra,immed,cr) \
{PowerPC_instr ppc;GEN_CMPI(ppc,ra,immed,cr);ppce->write32(ppc);}
#define EMIT_CMPLI(ppce,ra,immed,cr) \
{PowerPC_instr ppc;GEN_CMPLI(ppc,ra,immed,cr);ppce->write32(ppc);}
#define EMIT_BC(ppce,dst,aa,lk,bo,bi) \
{PowerPC_instr ppc;GEN_BC(ppc,dst,aa,lk,bo,bi);ppce->write32(ppc);}
#define EMIT_BNE(ppce,cr,dst,aa,lk) \
{PowerPC_instr ppc;GEN_BNE(ppc,cr,dst,aa,lk);ppce->write32(ppc);}
#define EMIT_BEQ(ppce,cr,dst,aa,lk) \
{PowerPC_instr ppc;GEN_BEQ(ppc,cr,dst,aa,lk);ppce->write32(ppc);}
#define EMIT_BGT(ppce,cr,dst,aa,lk) \
{PowerPC_instr ppc;GEN_BGT(ppc,cr,dst,aa,lk);ppce->write32(ppc);}
#define EMIT_BLE(ppce,cr,dst,aa,lk) \
{PowerPC_instr ppc;GEN_BLE(ppc,cr,dst,aa,lk);ppce->write32(ppc);}
#define EMIT_BGE(ppce,cr,dst,aa,lk) \
{PowerPC_instr ppc;GEN_BGE(ppc,cr,dst,aa,lk);ppce->write32(ppc);}
#define EMIT_BLT(ppce,cr,dst,aa,lk) \
{PowerPC_instr ppc;GEN_BLT(ppc,cr,dst,aa,lk);ppce->write32(ppc);}
#define EMIT_ADDI(ppce,rd,ra,immed) \
{PowerPC_instr ppc;GEN_ADDI(ppc,rd,ra,immed);ppce->write32(ppc);}
#define EMIT_RLWINM(ppce,rd,ra,sh,mb,me) \
{PowerPC_instr ppc;GEN_RLWINM(ppc,rd,ra,sh,mb,me);ppce->write32(ppc);}
#define EMIT_RLWIMI(ppce,rd,ra,sh,mb,me) \
{PowerPC_instr ppc;GEN_RLWIMI(ppc,rd,ra,sh,mb,me);ppce->write32(ppc);}
#define EMIT_SRWI(ppce,rd,ra,sh) \
{PowerPC_instr ppc;GEN_SRWI(ppc,rd,ra,sh);ppce->write32(ppc);}
#define EMIT_SLWI(ppce,rd,ra,sh) \
{PowerPC_instr ppc;GEN_SLWI(ppc,rd,ra,sh);ppce->write32(ppc);}
#define EMIT_SRAWI(ppce,rd,ra,sh) \
{PowerPC_instr ppc;GEN_SRAWI(ppc,rd,ra,sh);ppce->write32(ppc);}
#define EMIT_SLW(ppce,rd,ra,rb) \
{PowerPC_instr ppc;GEN_SLW(ppc,rd,ra,rb);ppce->write32(ppc);}
#define EMIT_SRW(ppce,rd,ra,rb) \
{PowerPC_instr ppc;GEN_SRW(ppc,rd,ra,rb);ppce->write32(ppc);}
#define EMIT_SRAW(ppce,rd,ra,rb) \
{PowerPC_instr ppc;GEN_SRAW(ppc,rd,ra,rb);ppce->write32(ppc);}
#define EMIT_ANDI(ppce,rd,ra,immed) \
{PowerPC_instr ppc;GEN_ANDI(ppc,rd,ra,immed);ppce->write32(ppc);}
#define EMIT_ORI(ppce,rd,ra,immed) \
{PowerPC_instr ppc;GEN_ORI(ppc,rd,ra,immed);ppce->write32(ppc);}
#define EMIT_XORI(ppce,rd,ra,immed) \
{PowerPC_instr ppc;GEN_XORI(ppc,rd,ra,immed);ppce->write32(ppc);}
#define EMIT_XORIS(ppce,rd,ra,immed) \
{PowerPC_instr ppc;GEN_XORIS(ppc,rd,ra,immed);ppce->write32(ppc);}
#define EMIT_MULLW(ppce,rd,ra,rb) \
{PowerPC_instr ppc;GEN_MULLW(ppc,rd,ra,rb);ppce->write32(ppc);}
#define EMIT_MULHW(ppce,rd,ra,rb) \
{PowerPC_instr ppc;GEN_MULHW(ppc,rd,ra,rb);ppce->write32(ppc);}
#define EMIT_MULHWU(ppce,rd,ra,rb) \
{PowerPC_instr ppc;GEN_MULHWU(ppc,rd,ra,rb);ppce->write32(ppc);}
#define EMIT_DIVW(ppce,rd,ra,rb) \
{PowerPC_instr ppc;GEN_DIVW(ppc,rd,ra,rb);ppce->write32(ppc);}
#define EMIT_DIVWU(ppce,rd,ra,rb) \
{PowerPC_instr ppc;GEN_DIVWU(ppc,rd,ra,rb);ppce->write32(ppc);}
#define EMIT_ADD(ppce,rd,ra,rb) \
{PowerPC_instr ppc;GEN_ADD(ppc,rd,ra,rb);ppce->write32(ppc);}
#define EMIT_SUBF(ppce,rd,ra,rb) \
{PowerPC_instr ppc;GEN_SUBF(ppc,rd,ra,rb);ppce->write32(ppc);}
#define EMIT_SUBFC(ppce,rd,ra,rb) \
{PowerPC_instr ppc;GEN_SUBFC(ppc,rd,ra,rb);ppce->write32(ppc);}
#define EMIT_SUBFE(ppce,rd,ra,rb) \
{PowerPC_instr ppc;GEN_SUBFE(ppc,rd,ra,rb);ppce->write32(ppc);}
#define EMIT_ADDIC(ppce,rd,ra,immed) \
{PowerPC_instr ppc;GEN_ADDIC(ppc,rd,ra,immed);ppce->write32(ppc);}
#define EMIT_SUB(ppce,rd,ra,rb) \
{PowerPC_instr ppc;GEN_SUB(ppc,rd,ra,rb);ppce->write32(ppc);}
#define EMIT_AND(ppce,rd,ra,rb) \
{PowerPC_instr ppc;GEN_AND(ppc,rd,ra,rb);ppce->write32(ppc);}
#define EMIT_NAND(ppce,rd,ra,rb) \
{PowerPC_instr ppc;GEN_NAND(ppc,rd,ra,rb);ppce->write32(ppc);}
#define EMIT_ANDC(ppce,rd,ra,rb) \
{PowerPC_instr ppc;GEN_ANDC(ppc,rd,ra,rb);ppce->write32(ppc);}
#define EMIT_NOR(ppce,rd,ra,rb) \
{PowerPC_instr ppc;GEN_NOR(ppc,rd,ra,rb);ppce->write32(ppc);}
#define EMIT_OR(ppce,rd,ra,rb) \
{PowerPC_instr ppc;GEN_OR(ppc,rd,ra,rb);ppce->write32(ppc);}
#define EMIT_XOR(ppce,rd,ra,rb) \
{PowerPC_instr ppc;GEN_XOR(ppc,rd,ra,rb);ppce->write32(ppc);}
#define EMIT_BLR(ppce,lk) \
{PowerPC_instr ppc;GEN_BLR(ppc,lk);ppce->write32(ppc);}
#define EMIT_MTLR(ppce,rs) \
{PowerPC_instr ppc;GEN_MTLR(ppc,rs);ppce->write32(ppc);}
#define EMIT_MFLR(ppce,rd) \
{PowerPC_instr ppc;GEN_MFLR(ppc,rd);ppce->write32(ppc);}
#define EMIT_MTCR(ppce,rs) \
{PowerPC_instr ppc;GEN_MTCR(ppc,rs);ppce->write32(ppc);}
#define EMIT_NEG(ppce,rd,rs) \
{PowerPC_instr ppc;GEN_NEG(ppc,rd,rs);ppce->write32(ppc);}
#define EMIT_EQV(ppce,rd,ra,rb) \
{PowerPC_instr ppc;GEN_EQV(ppc,rd,ra,rb);ppce->write32(ppc);}
#define EMIT_ADDZE(ppce,rd,rs) \
{PowerPC_instr ppc;GEN_ADDZE(ppc,rd,rs);ppce->write32(ppc);}
#define EMIT_ADDC(ppce,rd,ra,rb) \
{PowerPC_instr ppc;GEN_ADDC(ppc,rd,ra,rb);ppce->write32(ppc);}
#define EMIT_ADDE(ppce,rd,ra,rb) \
{PowerPC_instr ppc;GEN_ADDE(ppc,rd,ra,rb);ppce->write32(ppc);}
#define EMIT_SUBFIC(ppce,rd,ra,immed) \
{PowerPC_instr ppc;GEN_SUBFIC(ppc,rd,ra,immed);ppce->write32(ppc);}
#define EMIT_STFD(ppce,fs,immed,rb) \
{PowerPC_instr ppc;GEN_STFD(ppc,fs,immed,rb);ppce->write32(ppc);}
#define EMIT_STFS(ppce,fs,immed,rb) \
{PowerPC_instr ppc;GEN_STFS(ppc,fs,immed,rb);ppce->write32(ppc);}
#define EMIT_LFD(ppce,fd,immed,rb) \
{PowerPC_instr ppc;GEN_LFD(ppc,fd,immed,rb);ppce->write32(ppc);}
#define EMIT_LFS(ppce,fd,immed,rb) \
{PowerPC_instr ppc;GEN_LFS(ppc,fd,immed,rb);ppce->write32(ppc);}
#define EMIT_FADD(ppce,fd,fa,fb,dbl) \
{PowerPC_instr ppc;GEN_FADD(ppc,fd,fa,fb,dbl);ppce->write32(ppc);}
#define EMIT_FSUB(ppce,fd,fa,fb,dbl) \
{PowerPC_instr ppc;GEN_FSUB(ppc,fd,fa,fb,dbl);ppce->write32(ppc);}
#define EMIT_FMUL(ppce,fd,fa,fb,dbl) \
{PowerPC_instr ppc;GEN_FMUL(ppc,fd,fa,fb,dbl);ppce->write32(ppc);}
#define EMIT_FDIV(ppce,fd,fa,fb,dbl) \
{PowerPC_instr ppc;GEN_FDIV(ppc,fd,fa,fb,dbl);ppce->write32(ppc);}
#define EMIT_FABS(ppce,fd,fs) \
{PowerPC_instr ppc;GEN_FABS(ppc,fd,fs);ppce->write32(ppc);}
#define EMIT_FCFID(ppce,fd,fs) \
{PowerPC_instr ppc;GEN_FCFID(ppc,fd,fs);ppce->write32(ppc);}
#define EMIT_FRSP(ppce,fd,fs) \
{PowerPC_instr ppc;GEN_FRSP(ppc,fd,fs);ppce->write32(ppc);}
#define EMIT_FMR(ppce,fd,fs) \
{PowerPC_instr ppc;GEN_FMR(ppc,fd,fs);ppce->write32(ppc);}
#define EMIT_FNEG(ppce,fd,fs) \
{PowerPC_instr ppc;GEN_FNEG(ppc,fd,fs);ppce->write32(ppc);}
#define EMIT_FCTIW(ppce,fd,fs) \
{PowerPC_instr ppc;GEN_FCTIW(ppc,fd,fs);ppce->write32(ppc);}
#define EMIT_FCTIWZ(ppce,fd,fs) \
{PowerPC_instr ppc;GEN_FCTIWZ(ppc,fd,fs);ppce->write32(ppc);}
#define EMIT_STFIWX(ppce,fs,ra,rb) \
{PowerPC_instr ppc;GEN_STFIWX(ppc,fs,ra,rb);ppce->write32(ppc);}
#define EMIT_MTFSFI(ppce,field,immed) \
{PowerPC_instr ppc;GEN_MTFSFI(ppc,field,immed);ppce->write32(ppc);}
#define EMIT_MTFSF(ppce,fields,fs) \
{PowerPC_instr ppc;GEN_MTFSF(ppc,fields,fs);ppce->write32(ppc);}
#define EMIT_FCMPU(ppce,fa,fb,cr) \
{PowerPC_instr ppc;GEN_FCMPU(ppc,fa,fb,cr);ppce->write32(ppc);}
#define EMIT_FRSQRTE(ppce,fd,fs) \
{PowerPC_instr ppc;GEN_FRSQRTE(ppc,fd,fs);ppce->write32(ppc);}
#define EMIT_FSQRT(ppce,fd,fs) \
{PowerPC_instr ppc;GEN_FSQRT(ppc,fd,fs);ppce->write32(ppc);}
#define EMIT_FSQRTS(ppce,fd,fs) \
{PowerPC_instr ppc;GEN_FSQRTS(ppc,fd,fs);ppce->write32(ppc);}
#define EMIT_FSEL(ppce,fd,fa,fb,fc) \
{PowerPC_instr ppc;GEN_FSEL(ppc,fd,fa,fb,fc);ppce->write32(ppc);}
#define EMIT_FRES(ppce,fd,fs) \
{PowerPC_instr ppc;GEN_FRES(ppc,fd,fs);ppce->write32(ppc);}
#define EMIT_FNMSUB(ppce,fd,fa,fc,fb) \
{PowerPC_instr ppc;GEN_FNMSUB(ppc,fd,fa,fc,fb);ppce->write32(ppc);}
#define EMIT_FNMSUBS(ppce,fd,fa,fc,fb) \
{PowerPC_instr ppc;GEN_FNMSUBS(ppc,fd,fa,fc,fb);ppce->write32(ppc);}
#define EMIT_FMADD(ppce,fd,fa,fc,fb) \
{PowerPC_instr ppc;GEN_FMADD(ppc,fd,fa,fc,fb);ppce->write32(ppc);}
#define EMIT_FMADDS(ppce,fd,fa,fc,fb) \
{PowerPC_instr ppc;GEN_FMADDS(ppc,fd,fa,fc,fb);ppce->write32(ppc);}
#define EMIT_BCLR(ppce,lk,bo,bi) \
{PowerPC_instr ppc;GEN_BCLR(ppc,lk,bo,bi);ppce->write32(ppc);}
#define EMIT_BNELR(ppce,cr,lk) \
{PowerPC_instr ppc;GEN_BNELR(ppc,cr,lk);ppce->write32(ppc);}
#define EMIT_BLELR(ppce,cr,lk) \
{PowerPC_instr ppc;GEN_BLELR(ppc,cr,lk);ppce->write32(ppc);}
#define EMIT_ANDIS(ppce,rd,ra,immed) \
{PowerPC_instr ppc;GEN_ANDIS(ppc,rd,ra,immed);ppce->write32(ppc);}
#define EMIT_ORIS(ppce,rd,rs,immed) \
{PowerPC_instr ppc;GEN_ORIS(ppc,rd,rs,immed);ppce->write32(ppc);}
#define EMIT_CROR(ppce,cd,ca,cb) \
{PowerPC_instr ppc;GEN_CROR(ppc,cd,ca,cb);ppce->write32(ppc);}
#define EMIT_CRNOR(ppce,cd,ca,cb) \
{PowerPC_instr ppc;GEN_CRNOR(ppc,cd,ca,cb);ppce->write32(ppc);}
#define EMIT_MFCR(ppce,rt) \
{PowerPC_instr ppc;GEN_MFCR(ppc,rt);ppce->write32(ppc);}
#define EMIT_MCRXR(ppce,bf) \
{PowerPC_instr ppc;GEN_MCRXR(ppc,bf);ppce->write32(ppc);}

// debug

#define EMIT_LINE(ppce) ppce->emitDebugValue(__LINE__);