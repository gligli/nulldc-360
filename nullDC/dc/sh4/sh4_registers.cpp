#include <assert.h>

#include "types.h"
#include "sh4_registers.h"
#include "intc.h"
#include "rec_v1/driver.h"

__attribute__((aligned(65536))) struct Sh4RegContext sh4r;

__attribute__((aligned(128))) f32 sin_table[0x10000+0x4000];	//+0x4000 to avoid having to warp around twice on cos

u32*  xf_hex=(u32*)sh4r.xf,*fr_hex=(u32*)sh4r.fr;

#define SAVE_REG(name) memcpy(&to->name,&sh4r.name,sizeof(sh4r.name))
#define LOAD_REG(name) memcpy(&sh4r.name,&from->name,sizeof(sh4r.name))

#define SAVE_REG_A(name) memcpy(to->name,sh4r.name,sizeof(sh4r.name))
#define LOAD_REG_A(name) memcpy(sh4r.name,from->name,sizeof(sh4r.name))

void SaveSh4Regs(Sh4RegContext* to)
{
    SAVE_REG_A(r);
	SAVE_REG_A(r_bank);

	SAVE_REG(gbr);
	SAVE_REG(ssr);
	SAVE_REG(spc);
	SAVE_REG(sgr);
	SAVE_REG(dbr);
	SAVE_REG(vbr);


	SAVE_REG(mac);
	SAVE_REG(pr);
	SAVE_REG(fpul);
	SAVE_REG(pc);

	SAVE_REG(sr);
	SAVE_REG(fpscr);

	SAVE_REG_A(xf);
	SAVE_REG_A(fr);

	SAVE_REG(old_sr);
	SAVE_REG(old_fpscr);
}
void LoadSh4Regs(Sh4RegContext* from)
{
	LOAD_REG_A(r);
	LOAD_REG_A(r_bank);

	LOAD_REG(gbr);
	LOAD_REG(ssr);
	LOAD_REG(spc);
	LOAD_REG(sgr);
	LOAD_REG(dbr);
	LOAD_REG(vbr);


	LOAD_REG(mac);
	LOAD_REG(pr);
	LOAD_REG(fpul);
	LOAD_REG(pc);

	LOAD_REG(sr);
	LOAD_REG(fpscr);

	LOAD_REG_A(xf);
	LOAD_REG_A(fr);

	LOAD_REG(old_sr);
	LOAD_REG(old_fpscr);
}

INLINE void ChangeGPR()
{
	u32 temp[8];
	
	if (settings.dynarec.Enable)
	{
		asm volatile(
			"lis 3,sh4r@h				\n"
			"stw 16,64+0(3)				\n"
			"stw 17,64+4(3)				\n"
			"stw 18,64+8(3)				\n"
			"stw 19,64+12(3)			\n"
			"stw 20,64+16(3)			\n"
			"stw 21,64+20(3)			\n"
			"stw 22,64+24(3)			\n"
			"stw 23,64+28(3)			\n"
		:::"3");
	}
	
	for (int i=0;i<8;i++)
	{
		temp[i]=sh4r.r[i];
		sh4r.r[i]=sh4r.r_bank[i];
		sh4r.r_bank[i]=temp[i];
	}

	if (settings.dynarec.Enable)
	{
		asm volatile(
			"lis 3,sh4r@h				\n"
			"lwz 16,64+0(3)				\n"
			"lwz 17,64+4(3)				\n"
			"lwz 18,64+8(3)				\n"
			"lwz 19,64+12(3)			\n"
			"lwz 20,64+16(3)			\n"
			"lwz 21,64+20(3)			\n"
			"lwz 22,64+24(3)			\n"
			"lwz 23,64+28(3)			\n"
		:::"3","16","17","18","19","20","21","22","23");
	}
}


INLINE void ChangeFP()
{
	u32 temp[16];

	if (settings.dynarec.Enable)
	{
		asm volatile(
			"lis 3,sh4r@h				\n"
			"li 4,(48+0)*4              \n" // &sh4r.xf[0]
			"li 5,(48+4)*4              \n" // &sh4r.xf[4]
			"li 6,(48+8)*4              \n" // &sh4r.xf[8]
			"li 7,(48+12)*4             \n" // &sh4r.xf[12]
        
			"stvx " xstr(RXF0) ",4,3	\n"
			"stvx " xstr(RXF4) ",5,3	\n"
			"stvx " xstr(RXF8) ",6,3	\n"
			"stvx " xstr(RXF12) ",7,3	\n"
		:::"3","4","5","6","7");
	}
	
	for (int i=0;i<16;i++)
	{
		temp[i]=fr_hex[i];
		fr_hex[i]=xf_hex[i];
		xf_hex[i]=temp[i];
	}

	if (settings.dynarec.Enable)
	{
		asm volatile(
			"lis 3,sh4r@h				\n"
			"li 4,(48+0)*4              \n"
			"li 5,(48+4)*4              \n"
			"li 6,(48+8)*4              \n"
			"li 7,(48+12)*4             \n"
        
			"lvx " xstr(RXF0) ",4,3     \n"
			"lvx " xstr(RXF4) ",5,3     \n"
			"lvx " xstr(RXF8) ",6,3     \n"
			"lvx " xstr(RXF12) ",7,3	\n"
		:::"3","4","5","6","7");
	}
}

//called when sr is changed and we must check for reg banks ect.. , returns true if interrupts got 
bool UpdateSR()
{
	if (sh4r.sr.MD)
	{
		if (sh4r.old_sr.RB !=sh4r.sr.RB)
			ChangeGPR();//bank change
	}
	else
	{
		if (sh4r.sr.RB)
		{
			log("UpdateSR MD=0;RB=1 , this must not happen\n");
			sh4r.sr.RB =0;//error - must allways be 0
			if (sh4r.old_sr.RB)
				ChangeGPR();//switch
		}
		else
		{
			if (sh4r.old_sr.RB)
				ChangeGPR();//switch
		}
	}
/*
	if ((old_sr.IMASK!=0xF) && (sr.IMASK==0xF))
	{
		//log("Interrupts disabled  , pc=0x%X\n",pc);
	}

	if ((old_sr.IMASK==0xF) && (sr.IMASK!=0xF))
	{
		//log("Interrupts enabled  , pc=0x%X\n",pc);
	}
	
	bool rv=old_sr.IMASK > sr.IMASK;
	rv|=old_sr.BL==1 && sr.BL==0; 
	if (sr.IMASK==0xF)
		rv=false;
*/
	sh4r.old_sr.m_full=sh4r.sr.m_full;
	
	return SRdecode();
}

//make x86 and sh4 float status registers match ;)
u32 old_rm=0xFF;
u32 old_dn=0xFF;
void SetFloatStatusReg()
{
	if ((old_rm!=sh4r.fpscr.RM) || (old_dn!=sh4r.fpscr.DN) || (old_rm==0xFF )|| (old_dn==0xFF))
	{
		old_rm=sh4r.fpscr.RM ;
		old_dn=sh4r.fpscr.DN ;
		u32 temp=0x1f80;	//no flush to zero && round to nearest

		if (sh4r.fpscr.RM==1)	//if round to 0 , set the flag
			temp|=(3<<13);

		if (sh4r.fpscr.DN)		//denormals are considered 0
			temp|=(1<<15);
/*gli		_asm 
		{
			ldmxcsr temp;	//load the float status :)
		}*/
	}
}
//called when fpscr is changed and we must check for reg banks ect..
void UpdateFPSCR()
{
	if (sh4r.fpscr.FR !=sh4r.old_fpscr.FR)
		ChangeFP();//fpu bank change
	sh4r.old_fpscr=sh4r.fpscr;
	SetFloatStatusReg();//ensure they are on sync :)
}

void GenerateSinCos()
{
	wchar* path=GetEmuPath("data/fsca-table.bin");
	FILE* tbl=fopen(path,"rb");
	free(path);
	if (!tbl)
		die("fsca-table.bin is missing!");
	fread(sin_table,1,4*0x8000,tbl);
	fclose(tbl);

	for (int i=0;i<0x10000;i++)
	{
		if (i<0x8000)
			*(u32*)&sin_table[i]=__builtin_bswap32(*(u32*)&sin_table[i]);
		else if (i==0x8000)
			sin_table[i]=0;
		else
			sin_table[i]=-sin_table[i-0x8000];
	}
	for (int i=0x10000;i<0x14000;i++)
	{
		sin_table[i]=sin_table[(u16)i];//warp around for the last 0x4000 entries
	}
}
#ifdef DEBUG
f64 GetDR(u32 n)
{
#ifdef TRACE
	if (n>7)
		log("DR_r INDEX OVERRUN %d >7",n);
#endif
	double t;
	((u32*)(&t))[1]=fr_hex[(n<<1) | 0];
	((u32*)(&t))[0]=fr_hex[(n<<1) | 1];
	return t;
}

f64 GetXD(u32 n)
{
#ifdef TRACE
	if (n>7)
		log("XD_r INDEX OVERRUN %d >7",n);
#endif
	double t;
	((u32*)(&t))[1]=xf_hex[(n<<1) | 0];
	((u32*)(&t))[0]=xf_hex[(n<<1) | 1];
	return t;
}

void SetDR(u32 n,f64 val)
{
#ifdef TRACE
	if (n>7)
		log("DR_w INDEX OVERRUN %d >7",n);
#endif
	fr_hex[(n<<1) | 1]=((u32*)(&val))[0];
	fr_hex[(n<<1) | 0]=((u32*)(&val))[1];
}

void SetXD(u32 n,f64 val)
{
#ifdef TRACE
	if (n>7)
		log("XD_w INDEX OVERRUN %d >7",n);
#endif

	xf_hex[(n<<1) | 1]=((u32*)(&val))[0];
	xf_hex[(n<<1) | 0]=((u32*)(&val))[1];
}
#endif

u32* Sh4_int_GetRegisterPtr(Sh4RegType reg)
{
	if ((reg>=r0) && (reg<=r15))
	{
		return &sh4r.r[reg-r0];
	}
	else if ((reg>=r0_Bank) && (reg<=r7_Bank))
	{
		return &sh4r.r_bank[reg-r0_Bank];
	}
	else if ((reg>=fr_0) && (reg<=fr_15))
	{
		return &fr_hex[reg-fr_0];
	}
	else if ((reg>=xf_0) && (reg<=xf_15))
	{
		return &xf_hex[reg-xf_0];
	}
	else
	{
		switch(reg)
		{
		case reg_gbr :
			return &sh4r.gbr;
			break;
		case reg_vbr :
			return &sh4r.vbr;
			break;

		case reg_ssr :
			return &sh4r.ssr;
			break;

		case reg_spc :
			return &sh4r.spc;
			break;

		case reg_sgr :
			return &sh4r.sgr;
			break;

		case reg_dbr :
			return &sh4r.dbr;
			break;

		case reg_mach :
			return &sh4r.mac.h;
			break;

		case reg_macl :
			return &sh4r.mac.l;
			break;

		case reg_pr :
			return &sh4r.pr;
			break;

		case reg_fpul :
			return &sh4r.fpul;
			break;


		case reg_pc :
			return &sh4r.pc;
			break;

		case reg_sr :
			verify(false);
            return 0;//&sr.m_full;
			break;

		case reg_sr_T :
			verify(false);
            return 0;//&sh4r.sr.T;
			break;

		case reg_fpscr :
			return &sh4r.fpscr.full;
			break;


		default:
			EMUERROR2("unknown register Id %d",reg);
			return 0;
			break;
		}
	}
}
