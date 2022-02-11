/*
	MMU support code
	This is mostly hacked-on as the core was never meant to have mmu support

	There are two modes, one with 'full' mmu emulation (for wince/bleem/wtfever)
	and a fast-hack mode for 1mb sqremaps (for katana)

	defining NO_MMU disables the full mmu emulation
*/
#include "mmu.h"
#include "dc/sh4/sh4_if.h"
#include "dc/sh4/ccn.h"
#include "dc/sh4/intc.h"
#include "dc/sh4/sh4_registers.h"
#include "plugins/plugin_manager.h"

#include "_vmem.h"

#define printf_mmu(...)
#define printf_win32 dlog

//SQ fast remap , mailny hackish , assumes 1 mb pages
//max 64 mb can be remapped on SQ
u32 sq_remap[64];

TLB_Entry UTLB[64];
TLB_Entry ITLB[4];
const u32 mmu_mask[4]= 
{
	((0xFFFFFFFF)>>10)<<10,	//1 kb page
	((0xFFFFFFFF)>>12)<<12,	//4 kb page
	((0xFFFFFFFF)>>16)<<16,	//64 kb page
	((0xFFFFFFFF)>>20)<<20	//1 MB page
};

const u32 fast_reg_lut[8]= 
{
	0,0,0,0	//P0-U0
	,1		//P1
	,1		//P2
	,0		//P3
	,1		//P4
};

const u32 ITLB_LRU_OR[4]=
{
	0x00,//000xxx
	0x20,//1xx00x
	0x14,//x1x1x0
	0x0B,//xx1x11
};
const u32 ITLB_LRU_AND[4]=
{
	0x07,//000xxx
	0x39,//1xx00x
	0x3E,//x1x1x0
	0x3F,//xx1x11
};
u32 ITLB_LRU_USE[64];

void UTLB_SyncUnmap(u32 entry)
{
}
//sync mem mapping to mmu , suspend compiled blocks if needed.entry is a UTLB entry # , -1 is for full sync
bool UTLB_SyncMap(u32 entry)
{
	printf_mmu("UTLB MEM remap %d : 0x%X to 0x%X : %d\n",entry,UTLB[entry].Address.VPN<<10,UTLB[entry].Data.PPN<<10,UTLB[entry].Data.V);
	if (UTLB[entry].Data.V==0)
		return true;

	if ((UTLB[entry].Address.VPN & (0xFC000000>>10)) == (0xE0000000>>10))
	{
		#ifdef NO_MMU
		u32 vpn_sq=((UTLB[entry].Address.VPN & (0x3FFFFFF>>10) )>>10) &0x3F;//upper bits are allways known [0xE0/E1/E2/E3]
		sq_remap[vpn_sq]=UTLB[entry].Data.PPN<<10;
		dlog("SQ remap %d : 0x%X to 0x%X\n",entry,UTLB[entry].Address.VPN<<10,UTLB[entry].Data.PPN<<10);
		#endif
		return true;
	}
	else
	{
		#ifdef NO_MMU
		if ((UTLB[entry].Address.VPN&(0x1FFFFFFF>>10))==(UTLB[entry].Data.PPN&(0x1FFFFFFF>>10)))
		{
			dlog("Static remap %d : 0x%X to 0x%X\n",entry,UTLB[entry].Address.VPN<<10,UTLB[entry].Data.PPN<<10);
			return true;
		}
		dlog("Dynamic remap %d : 0x%X to 0x%X\n",entry,UTLB[entry].Address.VPN<<10,UTLB[entry].Data.PPN<<10);
		#endif
		return false;//dlog("MEM remap %d : 0x%X to 0x%X\n",entry,UTLB[entry].Address.VPN<<10,UTLB[entry].Data.PPN<<10);
	}
}
//sync mem mapping to mmu , suspend compiled blocks if needed.entry is a ITLB entry # , -1 is for full sync
void ITLB_Sync(u32 entry)
{
	printf_mmu("ITLB MEM remap %d : 0x%X to 0x%X : %d\n",entry,ITLB[entry].Address.VPN<<10,ITLB[entry].Data.PPN<<10,ITLB[entry].Data.V);
}

u32 mmu_error_TT;
void fastcall mmu_raise_exeption(u32 mmu_error,u32 address,u32 am)
{
	printf_mmu("mmu_raise_exeption -> pc = 0x%X : ",sh4r.pc);
	CCN_TEA=address;
	CCN_PTEH.VPN=address>>10;

	//save translation type error :)
	mmu_error_TT=am;

	switch(mmu_error)
	{
	//No error
	case MMU_ERROR_NONE:
		dlog("Error : mmu_raise_exeption(MMU_ERROR_NONE)\n");
		getc(stdin);
		break;

	//TLB miss
	case MMU_ERROR_TLB_MISS :
		printf_mmu("MMU_ERROR_UTLB_MISS 0x%X, handled\n",address);
		if (am==MMU_TT_DWRITE)			//WTLBMISS - Write Data TLB Miss Exception
			sh4_cpu->RaiseExeption(0x60,0x400);
		else if (am==MMU_TT_DREAD)		//RTLBMISS - Read Data TLB Miss Exception
			sh4_cpu->RaiseExeption(0x40,0x400);
		else							//ITLBMISS - Instruction TLB Miss Exception
			sh4_cpu->RaiseExeption(0x40,0x400);

		return;
		break;

	//TLB Multyhit
	case MMU_ERROR_TLB_MHIT :
		dlog("MMU_ERROR_TLB_MHIT @ 0x%X\n",address);
		break;

	//Mem is read/write protected (depends on translation type)
	case MMU_ERROR_PROTECTED :
		printf_mmu("MMU_ERROR_PROTECTED 0x%X, handled\n",address);
		if (am==MMU_TT_DWRITE)			//WRITEPROT - Write Data TLB Protection Violation Exception
			sh4_cpu->RaiseExeption(0xC0,0x100);
		else if (am==MMU_TT_DREAD)		//READPROT - Data TLB Protection Violation Exception
			sh4_cpu->RaiseExeption(0xA0,0x100);
		else
		{
			verify(false);
		}
		return;
		break;

	//Mem is write protected , firstwrite
	case MMU_ERROR_FIRSTWRITE :
		printf_mmu("MMU_ERROR_FIRSTWRITE\n");
		verify(am==MMU_TT_DWRITE);
		//FIRSTWRITE - Initial Page Write Exception
		sh4_cpu->RaiseExeption(0x80,0x100);
		
		return;
		break;

	//data read/write missasligned
	case MMU_ERROR_BADADDR :
		if (am==MMU_TT_DWRITE)			//WADDERR - Write Data Address Error
			sh4_cpu->RaiseExeption(0x100,0x100);
		else if (am==MMU_TT_DREAD)		//RADDERR - Read Data Address Error
			sh4_cpu->RaiseExeption(0xE0,0x100);
		else							//IADDERR - Instruction Address Error
		{
			printf_win32("MMU_ERROR_BADADDR(i) 0x%X\n",address);
			sh4_cpu->RaiseExeption(0xE0,0x100);
			return;
		}
		printf_mmu("MMU_ERROR_BADADDR(d) 0x%X, handled\n",address);
		return;
		break;

	//Can't Execute
	case MMU_ERROR_EXECPROT :
		printf_mmu("MMU_ERROR_EXECPROT 0x%X\n",address);

		//EXECPROT - Instruction TLB Protection Violation Exception
		sh4_cpu->RaiseExeption(0xA0,0x100);
		return;
		break;
	}

	__debugbreak(); 
}

bool mmu_match(u32 va,CCN_PTEH_type Address,CCN_PTEL_type Data)
{
	if (Data.V==0)
		return false;

	u32 sz=Data.SZ1*2+Data.SZ0;
	u32 mask=mmu_mask[sz];

	if ( (((Address.VPN<<10)&mask)==(va&mask)) )
	{
		bool asid_match = (Data.SH==0) && ( (sh4r.sr.MD==0) || (CCN_MMUCR.SV == 0) );

		if ( ( asid_match==false ) || (Address.ASID==CCN_PTEH.ASID) )
		{
			return true;
		}
	}

	return false;
}
//Do a full lookup on the UTLB entry's
u32 fastcall mmu_full_lookup(u32 va,u32& idx,u32& rv)
{
	CCN_MMUCR.URC++;
	if (CCN_MMUCR.URB==CCN_MMUCR.URC)
		CCN_MMUCR.URC=0;
	

	u32 entry=0;
	u32 nom=0;


	for (u32 i=0;i<64;i++)
	{
		//verify(sz!=0);
		if (mmu_match(va,UTLB[i].Address,UTLB[i].Data))
		{
			entry=i;
			nom++;
			u32 sz=UTLB[i].Data.SZ1*2+UTLB[i].Data.SZ0;
			u32 mask=mmu_mask[sz];
			//VPN->PPN | low bits
			rv=((UTLB[i].Data.PPN<<10)&mask) | (va&(~mask));
		}
	}

	if (nom!=1)
	{
		if (nom)
		{
			return MMU_ERROR_TLB_MHIT;
		}
		else
		{
			return MMU_ERROR_TLB_MISS;
		}
	}
	
	idx=entry;

	return MMU_ERROR_NONE;
}

//Simple QACR translation for mmu (when AT is off)
u32 fastcall mmu_QACR_SQ(u32 va)
{
	u32 QACR;
	
	//default to sq 0
	QACR = CCN_QACR_TR[0];
	//sq1 ? if so use QACR1
	if (va& 0x20)
		QACR = CCN_QACR_TR[1];
	va&=~0x1f;
	return QACR+va;
}
template<u32 translation_type>
u32 fastcall mmu_full_SQ(u32 va,u32& rv)
{

	if ((va&3) || (CCN_MMUCR.SQMD==1 && sh4r.sr.MD==0))
	{
		//here, or after ?
		return MMU_ERROR_BADADDR;
	}

	if (CCN_MMUCR.AT)
	{
		//Address=Dest&0xFFFFFFE0;

		u32 entry;
		u32 lookup = mmu_full_lookup(va,entry,rv);

		rv&=~31;//lower 5 bits are forced to 0

		if (lookup!=MMU_ERROR_NONE)
			return lookup;

		u32 md=UTLB[entry].Data.PR>>1;

		//Priv mode protection
		if ((md==0) && sh4r.sr.MD==0)
		{
			return MMU_ERROR_PROTECTED;
		}

		//Write Protection (Lock or FW)
		if (translation_type==MMU_TT_DWRITE)
		{
			if ((UTLB[entry].Data.PR&1)==0)
				return MMU_ERROR_PROTECTED;
			else if (UTLB[entry].Data.D==0)
				return MMU_ERROR_FIRSTWRITE;
		}
	}
	else
	{
		rv = mmu_QACR_SQ(va);
	}
	return MMU_ERROR_NONE;
}
template<u32 translation_type>
u32 fastcall mmu_data_translation(u32 va,u32& rv)
{
	//*opt notice* this could be only checked for writes, as reads are invalid
	if ((va&0xFC000000)==0xE0000000)
	{
		u32 lookup=mmu_full_SQ<translation_type>(va,rv);
		if (lookup!=MMU_ERROR_NONE)
			return lookup;
		rv=va;	//SQ writes are not translated, only write backs are.
		return MMU_ERROR_NONE;
	}

	if ((sh4r.sr.MD==0) && (va&0x80000000)!=0)
	{
		//if on kernel, and not SQ addr -> error
		return MMU_ERROR_BADADDR;
	}

	if (sh4r.sr.MD==1 && ((va&0xFC000000)==0x7C000000))
	{
		rv=va;
		return MMU_ERROR_NONE;
	}

	if ((CCN_MMUCR.AT==0) || (fast_reg_lut[va>>29]!=0))
	{
		rv=va;
		return MMU_ERROR_NONE;
	}
	/*
	if ( CCN_CCR.ORA && ((va&0xFC000000)==0x7C000000))
	{
		verify(false);
		return va;
	}
	*/
	u32 entry;
	u32 lookup=mmu_full_lookup(va,entry,rv);
	
	if (lookup!=MMU_ERROR_NONE)
		return lookup;

	u32 md=UTLB[entry].Data.PR>>1;
	
	//0X  & User mode-> protection violation
	//Priv mode protection
	if ((md==0) && sh4r.sr.MD==0)
	{
		return MMU_ERROR_PROTECTED;
	}

	//X0 -> read olny
	//X1 -> read/write , can be FW

	//Write Protection (Lock or FW)
	if (translation_type==MMU_TT_DWRITE)
	{
		if ((UTLB[entry].Data.PR&1)==0)
			return MMU_ERROR_PROTECTED;
		else if (UTLB[entry].Data.D==0)
			return MMU_ERROR_FIRSTWRITE;
	}
	return MMU_ERROR_NONE;
}

u32 fastcall mmu_instruction_translation(u32 va,u32& rv)
{
	if ((sh4r.sr.MD==0) && (va&0x80000000)!=0)
	{
		//if SQ disabled , or if if SQ on but out of SQ mem then BAD ADDR ;)
		if (va>=0xE0000000)
			return MMU_ERROR_BADADDR;
	}

	if ((CCN_MMUCR.AT==0) || (fast_reg_lut[va>>29]!=0))
	{
		rv=va;
		return MMU_ERROR_NONE;
	}

	bool mmach=false;
retry_ITLB_Match:
	u32 entry=4;
	u32 nom=0;
	for (u32 i=0;i<4;i++)
	{
		if (ITLB[i].Data.V==0)
			continue;
		u32 sz=ITLB[i].Data.SZ1*2+ITLB[i].Data.SZ0;
		u32 mask=mmu_mask[sz];

		if ( (((ITLB[i].Address.VPN<<10)&mask)==(va&mask)) )
		{
			bool asid_match = (ITLB[i].Data.SH==0) && ( (sh4r.sr.MD==0) || (CCN_MMUCR.SV == 0) );

			if ( ( asid_match==false ) || (ITLB[i].Address.ASID==CCN_PTEH.ASID) )
			{
				//verify(sz!=0);
				entry=i;
				nom++;
				//VPN->PPN | low bits
				rv=((ITLB[i].Data.PPN<<10)&mask) | (va&(~mask));
			}
		}
	}

	if (entry==4)
	{
		verify(mmach==false);
		u32 lookup=mmu_full_lookup(va,entry,rv);

		if (lookup!=MMU_ERROR_NONE)
			return lookup;
		u32 replace_index=ITLB_LRU_USE[CCN_MMUCR.LRUI];
		verify(replace_index!=0xFFFFFFFF);
		ITLB[replace_index]=UTLB[entry];
		entry=replace_index;
		ITLB_Sync(entry);
		mmach=true;
		goto retry_ITLB_Match;
	}
	else if (nom!=1)
	{
		if (nom)
		{
			return MMU_ERROR_TLB_MHIT;
		}
		else
		{
			return MMU_ERROR_TLB_MISS;
		}
	}

	CCN_MMUCR.LRUI&=ITLB_LRU_AND[entry];
	CCN_MMUCR.LRUI|=ITLB_LRU_OR[entry];

	u32 md=ITLB[entry].Data.PR>>1;
	
	//0X  & User mode-> protection violation
	//Priv mode protection
	if ((md==0) && sh4r.sr.MD==0)
	{
		return MMU_ERROR_PROTECTED;
	}

	return MMU_ERROR_NONE;
}
void MMU_Init()
{
	memset(ITLB_LRU_USE,0xFF,sizeof(ITLB_LRU_USE));
	for (u32 e=0;e<4;e++)
	{
		u32 match_key=((~ITLB_LRU_AND[e])&0x3F);
		u32 match_mask=match_key | ITLB_LRU_OR[e];
		for (u32 i=0;i<64;i++)
		{
			if ((i & match_mask)==match_key)
			{
				verify(ITLB_LRU_USE[i]==0xFFFFFFFF);
				ITLB_LRU_USE[i]=e;
			}
		}
	}
}


void MMU_Reset(bool Manual)
{
	memset(UTLB,0,sizeof(UTLB));
	memset(ITLB,0,sizeof(ITLB));
}

void MMU_Term()
{
}

u8 __fastcall mmu_ReadMem8(u32 adr)
{
	u32 addr;
	u32 tv=mmu_data_translation<MMU_TT_DREAD>(adr,addr);
	if (tv==0)
		return _vmem_ReadMem8(addr);
	else
		mmu_raise_exeption(tv,adr,MMU_TT_DREAD);

	return 0;
}

u16 __fastcall mmu_ReadMem16(u32 adr)
{
	if (adr&1)
	{
		mmu_raise_exeption(MMU_ERROR_BADADDR,adr,MMU_TT_DREAD);
			return 0;
	}
	u32 addr;
	u32 tv=mmu_data_translation<MMU_TT_DREAD>(adr,addr);
	if (tv==0)
		return _vmem_ReadMem16(addr);
	else
		mmu_raise_exeption(tv,adr,MMU_TT_DREAD);

	return 0;
}
u16 __fastcall mmu_IReadMem16(u32 adr)
{
	if (adr&1)
	{
		mmu_raise_exeption(MMU_ERROR_BADADDR,adr,MMU_TT_IREAD);
		return 0;
	}
	u32 addr;
	u32 tv=mmu_instruction_translation(adr,addr);
	if (tv==0)
		return _vmem_ReadMem16(addr);
	else
		mmu_raise_exeption(tv,adr,MMU_TT_IREAD);

	return 0;
}

u32 __fastcall mmu_ReadMem32(u32 adr)
{
	if (adr&3)
	{
		mmu_raise_exeption(MMU_ERROR_BADADDR,adr,MMU_TT_DREAD);
		return 0;
	}
	u32 addr;
	u32 tv=mmu_data_translation<MMU_TT_DREAD>(adr,addr);
	if (tv==0)
		return _vmem_ReadMem32(addr);
	else
		mmu_raise_exeption(tv,adr,MMU_TT_DREAD);

	return 0;
}
u64 __fastcall mmu_ReadMem64(u32 adr)
{
	if (adr&7)
	{
		mmu_raise_exeption(MMU_ERROR_BADADDR,adr,MMU_TT_DREAD);
		return 0;
	}
	u32 addr;
	u32 tv=mmu_data_translation<MMU_TT_DREAD>(adr,addr);
	if (tv==0)
	{
		return _vmem_ReadMem64(addr);
	}
	else
		mmu_raise_exeption(tv,adr,MMU_TT_DREAD);

	return 0;
}

void __fastcall mmu_WriteMem8(u32 adr,u8 data)
{
	u32 addr;
	u32 tv=mmu_data_translation<MMU_TT_DWRITE>(adr,addr);
	if (tv==0)
	{
		_vmem_WriteMem8(addr,data);
		return;
	}
	else
		mmu_raise_exeption(tv,adr,MMU_TT_DWRITE);
}

void __fastcall mmu_WriteMem16(u32 adr,u16 data)
{
	if (adr&1)
	{
		mmu_raise_exeption(MMU_ERROR_BADADDR,adr,MMU_TT_DWRITE);
		return;
	}
	u32 addr;
	u32 tv=mmu_data_translation<MMU_TT_DWRITE>(adr,addr);
	if (tv==0)
	{
		_vmem_WriteMem16(addr,data);
		return;
	}
	else
		mmu_raise_exeption(tv,adr,MMU_TT_DWRITE);
}
void __fastcall mmu_WriteMem32(u32 adr,u32 data)
{
	if (adr&3)
	{
		mmu_raise_exeption(MMU_ERROR_BADADDR,adr,MMU_TT_DWRITE);
		return;
	}
	u32 addr;
	u32 tv=mmu_data_translation<MMU_TT_DWRITE>(adr,addr);
	if (tv==0)
	{
		_vmem_WriteMem32(addr,data);
		return;
	}
	else
		mmu_raise_exeption(tv,adr,MMU_TT_DWRITE);
}
void __fastcall mmu_WriteMem64(u32 adr,u64 data)
{
	if (adr&7)
	{
		mmu_raise_exeption(MMU_ERROR_BADADDR,adr,MMU_TT_DWRITE);
		return;
	}
	u32 addr;
	u32 tv=mmu_data_translation<MMU_TT_DWRITE>(adr,addr);
	if (tv==0)
	{
		_vmem_WriteMem64(addr,data);
		return;
	}
	else
		mmu_raise_exeption(tv,adr,MMU_TT_DWRITE);
}

bool __fastcall mmu_TranslateSQW(u32& adr)
{
#ifndef NO_MMU

	u32 addr;
	u32 tv=mmu_full_SQ<MMU_TT_DREAD>(adr,addr);
	if (tv!=0)
	{
		mmu_raise_exeption(tv,adr,MMU_TT_DREAD);
		return false;
	}

	adr=addr;
#else
	//This will olny work for 1 mb pages .. hopefully nothing else is used
	//*FIXME* to work for all page sizes ?

	if (CCN_MMUCR.AT==0)
	{	//simple translation
		adr=mmu_QACR_SQ(adr);
	}
	else
	{	//remap table
		adr=sq_remap[(adr>>20)&0x3F] | (adr & 0xFFFE0);
	}
#endif
	return true;
}