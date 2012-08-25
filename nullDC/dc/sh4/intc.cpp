
#include "types.h"
#include "intc.h"
#include "tmu.h"
#include "ccn.h"
#include "sh4_registers.h"
#include "log/logging_interface.h"
#include <assert.h>

/*
	Interrupt controller emulation
	Sh4 has a very configurable intc, supporting variable priority per interrupt source
	On the IRL inputs the holly intc is connected

	The intc is very efficiently implemented using bit-vectors and lotsa caching
*/

#include "dc/mem/sh4_internal_reg.h"
#include "dc/asic/asic.h"
#include "dc/maple/maple_if.h"

#define printf_except(...)

//#define COUNT_INTERRUPT_UPDATES
//Return interrupt priority level
template<u32 IRL>
u32 GetIRLPriority()
{
	return 15-IRL;
}
//Return interrupt priority level
template<u16* reg,u32 part>
u32 GetPriority_bug()	//VC++ bugs this ;p
{
	return ((*reg)>>(4*part))&0xF;
}
template<u32 part>
u32 GetPriority_a()
{
	return ((INTC_IPRA.reg_data)>>(4*part))&0xF;
}
template<u32 part>
u32 GetPriority_b()
{
	return ((INTC_IPRB.reg_data)>>(4*part))&0xF;
}
template<u32 part>
u32 GetPriority_c()
{
	return ((INTC_IPRC.reg_data)>>(4*part))&0xF;
}
#define GIPA(p) GetPriority_a< p >
#define GIPB(p)  GetPriority_b< p >
#define GIPC(p)  GetPriority_c< p >

const InterptSourceList_Entry InterruptSourceList[]=
{
	//IRL
	{GetIRLPriority<9>,0x320},//sh4_IRL_9			= KMIID(sh4_int,0x320,0),
	{GetIRLPriority<11>,0x360},//sh4_IRL_11			= KMIID(sh4_int,0x360,1),
	{GetIRLPriority<13>,0x3A0},//sh4_IRL_13			= KMIID(sh4_int,0x3A0,2),

	//HUDI
	{GIPC(0),0x600},//sh4_HUDI_HUDI		= KMIID(sh4_int,0x600,3),  /* H-UDI underflow */

	//GPIO (missing on dc ?)
	{GIPC(3),0x620},//sh4_GPIO_GPIOI		= KMIID(sh4_int,0x620,4),

	//DMAC
	{GIPC(2),0x640},//sh4_DMAC_DMTE0		= KMIID(sh4_int,0x640,5),
	{GIPC(2),0x660},//sh4_DMAC_DMTE1		= KMIID(sh4_int,0x660,6),
	{GIPC(2),0x680},//sh4_DMAC_DMTE2		= KMIID(sh4_int,0x680,7),
	{GIPC(2),0x6A0},//sh4_DMAC_DMTE3		= KMIID(sh4_int,0x6A0,8),
	{GIPC(2),0x6C0},//sh4_DMAC_DMAE		= KMIID(sh4_int,0x6C0,9),

	//TMU
	{GIPA(3),0x400},//sh4_TMU0_TUNI0		=  KMIID(sh4_int,0x400,10), /* TMU0 underflow */
	{GIPA(2),0x420},//sh4_TMU1_TUNI1		=  KMIID(sh4_int,0x420,11), /* TMU1 underflow */
	{GIPA(1),0x440},//sh4_TMU2_TUNI2		=  KMIID(sh4_int,0x440,12), /* TMU2 underflow */
	{GIPA(1),0x460},//sh4_TMU2_TICPI2		=  KMIID(sh4_int,0x460,13),

	//RTC
	{GIPA(0),0x480},//sh4_RTC_ATI			= KMIID(sh4_int,0x480,14),
	{GIPA(0),0x4A0},//sh4_RTC_PRI			= KMIID(sh4_int,0x4A0,15),
	{GIPA(0),0x4C0},//sh4_RTC_CUI			= KMIID(sh4_int,0x4C0,16),

	//SCI
	{GIPB(1),0x4E0},//sh4_SCI1_ERI		= KMIID(sh4_int,0x4E0,17),
	{GIPB(1),0x500},//sh4_SCI1_RXI		= KMIID(sh4_int,0x500,18),
	{GIPB(1),0x520},//sh4_SCI1_TXI		= KMIID(sh4_int,0x520,19),
	{GIPB(1),0x540},//sh4_SCI1_TEI		= KMIID(sh4_int,0x540,29),

	//SCIF
	{GIPC(1),0x700},//sh4_SCIF_ERI		= KMIID(sh4_int,0x700,21),
	{GIPC(1),0x720},//sh4_SCIF_RXI		= KMIID(sh4_int,0x720,22),
	{GIPC(1),0x740},//sh4_SCIF_BRI		= KMIID(sh4_int,0x740,23),
	{GIPC(1),0x760},//sh4_SCIF_TXI		= KMIID(sh4_int,0x760,24),

	//WDT
	{GIPB(3),0x560},//sh4_WDT_ITI			= KMIID(sh4_int,0x560,25),

	//REF
	{GIPB(2),0x580},//sh4_REF_RCMI		= KMIID(sh4_int,0x580,26),
	{GIPA(2),0x5A0},//sh4_REF_ROVI		= KMIID(sh4_int,0x5A0,27),
};

//dynamicaly built
//Maps siid -> EventID
__attribute__((aligned(128))) u16 InterruptEnvId[32]=
{
	0
};
//dynamicaly built
//Maps piid -> 1<<siid
__attribute__((aligned(128))) u32 InterruptBit[32] = 
{
	0
};
__attribute__((aligned(128))) u32 InterruptLevelBit[16]=
{
	0
};
void FASTCALL RaiseInterrupt_(InterruptID intr);
bool fastcall Do_Interrupt(u32 intEvn);
bool Do_Exeption(u32 lvl, u32 expEvn, u32 CallVect);
int Check_Ints();
bool HandleSH4_exept(InterruptID expt);


#define IPr_LVL6  0x6
#define IPr_LVL4  0x4
#define IPr_LVL2  0x2

extern bool sh4_sleeping;

//bool InterruptsArePending=true;

INTC_ICR_type  INTC_ICR;
INTC_IPRA_type INTC_IPRA;
INTC_IPRB_type INTC_IPRB;
INTC_IPRC_type INTC_IPRC;

//InterruptID intr_l;

#pragma pack(1)
__attribute__((aligned(65536))) struct intr_s{
u32 interrupt_vpend;
u32 interrupt_vmask;	//-1 -> ingore that kthx.0x0FFFffff allows all interrupts ( virtual interrupts are allways masked
u32 decoded_srimask;	//-1 kills all interrupts,including rebuild ones.
} intr;

//bit 0 ~ 27 : interrupt source 27:0. 0 = lowest level, 27 = highest level.
//bit 28~31  : virtual interrupt sources.These have to do with the emu

//28:31 = 0 -> nothing (disabled)
//28:31 = 1 -> rebuild interrupt list		, retest interrupts
//28:31 = * -> undefined

//Rebuild sorted interrupt id table (priorities where updated)
void RequestSIIDRebuild() 
{ 
	intr.interrupt_vpend|=2<<28;
}
bool SRdecode() 
{ 
	u32 imsk=sh4r.sr.IMASK;
	intr.decoded_srimask=InterruptLevelBit[imsk];

	if (sh4r.sr.BL)
		intr.decoded_srimask=0x0FFFFFFF;

	return (intr.interrupt_vpend&intr.interrupt_vmask)>intr.decoded_srimask;
}

void fastcall VirtualInterrupt(u32 id)
{
	if (id)
	{
		u32 cnt=0;
		u32 vpend=intr.interrupt_vpend;
		u32 vmask=intr.interrupt_vmask;
		intr.interrupt_vpend=0;
		intr.interrupt_vmask=0xF0000000;
		//rebuild interrupt table
		for (u32 ilevel=0;ilevel<16;ilevel++)
		{
			for (u32 isrc=0;isrc<28;isrc++)
			{
				if (InterruptSourceList[isrc].GetPrLvl()==ilevel)
				{
					InterruptEnvId[cnt]=(u16)InterruptSourceList[isrc].IntEvnCode;
					bool p=(InterruptBit[isrc]&vpend) != 0;
					bool m=(InterruptBit[isrc]&vmask) != 0;

					InterruptBit[isrc]=1<<cnt;

					if (p)
						intr.interrupt_vpend|=InterruptBit[isrc];

					if (m)
						intr.interrupt_vmask|=InterruptBit[isrc];

					cnt++;
				}
			}
			InterruptLevelBit[ilevel]=(1<<cnt)-1;
		}

		SRdecode();
	}
}
//#ifdef COUNT_INTERRUPT_UPDATES
u32 no_interrupts,yes_interrupts;
//#endif

int UpdateINTC_C();

extern "C" { // called from ASM
int __attribute__((externally_visible)) UpdateINTCDoINT()
{
	u32 ecx=intr.interrupt_vpend&intr.interrupt_vmask;

#ifdef COUNT_INTERRUPT_UPDATES
	yes_interrupts++;
#endif
	if((ecx&0xF0000000)==0){
		int eax=31-__builtin_clz(ecx);
//            printf("eax %08x ecx %08x\n",eax,ecx);
		return Do_Interrupt(InterruptEnvId[eax]);
	}else{
		VirtualInterrupt(ecx>>28);
		return UpdateINTC();			
	}
}
}
#define ASM_INTC

int UpdateINTC()
{
#ifndef ASM_INTC
	u32 ecx=intr.interrupt_vpend&intr.interrupt_vmask;

	if(ecx<=intr.decoded_srimask){
#ifdef COUNT_INTERRUPT_UPDATES
		no_interrupts++;
#endif
		return 0;
	}else{
		return UpdateINTCDoINT();
	}
#else
	u32 rv=0;
    
    asm volatile (
		"lis 7,intr@ha					\n"
		"lwz 4,intr@l+0(7)              \n" // vpend
		"lwz 5,intr@l+4(7)              \n" // vmask
		"lwz 6,intr@l+8(7)              \n" // sri
		"and 4,4,5						\n"
		"cmplw 4,6						\n"
		"ble 1f                         \n"
        "li %[rv],1                     \n"
		"1:                             \n"
        : [rv] "+r" (rv)
        :: "3","4","5","6","7","cc"
	);

    if(rv) return UpdateINTCDoINT();

    return 0;
#endif
}

void RaiseExeption(u32 code,u32 vector)
{
	if (sh4r.sr.BL!=0)
	{
		log("RaiseExeption: sr.BL==1, pc=%08X\n",sh4r.pc);
		verify(sh4r.sr.BL == 0);
	}
		
	sh4r.spc = sh4r.pc;
	sh4r.ssr = sh4r.sr.GetFull(true);
	sh4r.sgr = sh4r.r[15];
	CCN_EXPEVT = code;
	sh4r.sr.MD = 1;
	sh4r.sr.RB = 1;
	sh4r.sr.BL = 1;
	sh4r.pc = sh4r.vbr + vector;
	UpdateSR();
	printf_except("RaiseExeption: from %08X , pc errh %08X\n",spc,pc);
}

void SetInterruptPend(InterruptID intr_)
{
	u32 piid= intr_ & InterruptPIIDMask;
	intr.interrupt_vpend|=InterruptBit[piid];
}
void ResetInterruptPend(InterruptID intr_)
{
	u32 piid= intr_ & InterruptPIIDMask;
	intr.interrupt_vpend&=~InterruptBit[piid];
}

void SetInterruptMask(InterruptID intr_)
{
	u32 piid= intr_ & InterruptPIIDMask;
	intr.interrupt_vmask|=InterruptBit[piid];
}
void ResetInterruptMask(InterruptID intr_)
{
	u32 piid= intr_ & InterruptPIIDMask;
	intr.interrupt_vmask&=~InterruptBit[piid];
}
//this is what left from the old intc .. meh .. 
//exeptions are handled here .. .. hmm are they ? :P
bool HandleSH4_exept(InterruptID expt)
{
	switch(expt)
	{

	case sh4_ex_TRAP:
		return Do_Exeption(0,0x160,0x100);

	default:
		return false;
	}
}

bool fastcall Do_Interrupt(u32 intEvn)
{
//	printf("Interrupt : 0x%04x,0x%08x\n",intEvn,pc);
	verify(sh4r.sr.BL==0);

	CCN_INTEVT = intEvn;

	sh4r.ssr = sh4r.sr.GetFull(true);
	sh4r.spc = sh4r.pc;
	sh4r.sgr = sh4r.r[15];
	sh4r.sr.BL = 1;
	sh4r.sr.MD = 1;
	sh4r.sr.RB = 1;
	UpdateSR();

	sh4r.pc = sh4r.vbr + 0x600;

	return true;
}

bool Do_Exeption(u32 lvl, u32 expEvn, u32 CallVect)
{
	CCN_EXPEVT = expEvn;

	sh4r.ssr = sh4r.sr.GetFull(true);
	sh4r.spc = sh4r.pc+2;
	sh4r.sgr = sh4r.r[15];
	sh4r.sr.BL = 1;
	sh4r.sr.MD = 1;
	sh4r.sr.RB = 1;
	UpdateSR();

	//this is from when the project was still in C#
	//left in for novely reasons ...
	//CallStackTrace.cstAddCall(sh4.pc, sh4.pc, sh4.vbr + 0x600, CallType.Interrupt);

	sh4r.pc = sh4r.vbr + CallVect;

	sh4r.pc-=2;//fix up ;)
	return true;
}

//Register writes need interrupt re-testing !

void write_INTC_IPRA(u32 data)
{
	if (INTC_IPRA.reg_data!=(u16)data)
	{
		INTC_IPRA.reg_data=(u16)data;
		RequestSIIDRebuild();	//we need to rebuild the table
	}
}
void write_INTC_IPRB(u32 data)
{
	if (INTC_IPRB.reg_data!=(u16)data)
	{
		INTC_IPRB.reg_data=(u16)data;
		RequestSIIDRebuild();	//we need to rebuild the table
	}
}
void write_INTC_IPRC(u32 data)
{
	if (INTC_IPRC.reg_data!=(u16)data)
	{
		INTC_IPRC.reg_data=(u16)data;
		RequestSIIDRebuild();	//we need to rebuild the table
	}
}
//Init/Res/Term
void intc_Init()
{
	//INTC ICR 0xFFD00000 0x1FD00000 16 0x0000 0x0000 Held Held Pclk
	INTC[(u32)(INTC_ICR_addr&0xFF)>>2].flags=REG_16BIT_READWRITE | REG_READ_DATA | REG_WRITE_DATA;
	INTC[(u32)(INTC_ICR_addr&0xFF)>>2].readFunction=0;
	INTC[(u32)(INTC_ICR_addr&0xFF)>>2].writeFunction=0;
	INTC[(u32)(INTC_ICR_addr&0xFF)>>2].data16=&INTC_ICR.reg_data;

	//INTC IPRA 0xFFD00004 0x1FD00004 16 0x0000 0x0000 Held Held Pclk
	INTC[(u32)(INTC_IPRA_addr&0xFF)>>2].flags=REG_16BIT_READWRITE | REG_READ_DATA;
	INTC[(u32)(INTC_IPRA_addr&0xFF)>>2].readFunction=0;
	INTC[(u32)(INTC_IPRA_addr&0xFF)>>2].writeFunction=write_INTC_IPRA;
	INTC[(u32)(INTC_IPRA_addr&0xFF)>>2].data16=&INTC_IPRA.reg_data;

	//INTC IPRB 0xFFD00008 0x1FD00008 16 0x0000 0x0000 Held Held Pclk
	INTC[(u32)(INTC_IPRB_addr&0xFF)>>2].flags=REG_16BIT_READWRITE | REG_READ_DATA;
	INTC[(u32)(INTC_IPRB_addr&0xFF)>>2].readFunction=0;
	INTC[(u32)(INTC_IPRB_addr&0xFF)>>2].writeFunction=write_INTC_IPRB;
	INTC[(u32)(INTC_IPRB_addr&0xFF)>>2].data16=&INTC_IPRB.reg_data;

	//INTC IPRC 0xFFD0000C 0x1FD0000C 16 0x0000 0x0000 Held Held Pclk
	INTC[(u32)(INTC_IPRC_addr&0xFF)>>2].flags=REG_16BIT_READWRITE | REG_READ_DATA;
	INTC[(u32)(INTC_IPRC_addr&0xFF)>>2].readFunction=0;
	INTC[(u32)(INTC_IPRC_addr&0xFF)>>2].writeFunction=write_INTC_IPRC;
	INTC[(u32)(INTC_IPRC_addr&0xFF)>>2].data16=&INTC_IPRC.reg_data;
}

void intc_Reset(bool Manual)
{
	INTC_ICR.reg_data = 0x0;
	INTC_IPRA.reg_data = 0x0;
	INTC_IPRB.reg_data = 0x0;
	INTC_IPRC.reg_data = 0x0;

	intr.interrupt_vpend=0x00000000;	//rebuild & recalc
	intr.interrupt_vmask=0xFFFFFFFF;	//no masking
	intr.decoded_srimask=0;			//nothing is real, everything is allowed ...

	RequestSIIDRebuild();		//we have to rebuild the table.

	for (u32 i=0;i<28;i++)
		InterruptBit[i]=1<<i;
}

void intc_Term()
{

}