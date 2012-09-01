#pragma once
#include "types.h"
#include "sh4_if.h"

union mac_type
{
#ifdef XENON
	struct { u32 h,l; };
#else
	struct { u32 l,h; };
#endif
	u64 full;
};

__attribute__((aligned(128))) extern f32 sin_table[0x10000+0x4000];

struct Sh4RegContext
{
	u32 pc;
	StatusReg sr;
	u32 gbr,ssr,sgr,dbr,vbr;
	u32 r_bank[8];
	u32 r[16];
	f32 fr[16];
	f32 xf[16];
	u32 zer_fpul,fpul;
	u32 pr;
	u32 spc;
	mac_type mac;
	fpscr_type fpscr;
	StatusReg old_sr;
	fpscr_type old_fpscr;
};

void GenerateSinCos();

extern Sh4RegContext sh4r;
extern Sh4RegContext * vm_sh4r;

extern u32* xf_hex;
extern u32* fr_hex;
	
void SaveSh4Regs(Sh4RegContext* to);
void LoadSh4Regs(Sh4RegContext* from);

void UpdateFPSCR();
bool UpdateSR();

#ifndef DEBUG
static INLINE  __attribute__((unused)) f64 GetDR(u32 n)
{
#ifdef TRACE
	if (n>7)
		log("DR_r INDEX OVERRUN %d >7",n);
#endif
	double t;
	((u32*)(&t))[1]=fr_hex[(n<<1) | 0];
	((u32*)(&t))[0]=fr_hex[(n<<1) | 1];
	printf("GetDR %f\n",n);
	return t;
}

static INLINE __attribute__((unused)) f64 GetXD(u32 n)
{
#ifdef TRACE
	if (n>7)
		log("XD_r INDEX OVERRUN %d >7",n);
#endif
	double t;
	((u32*)(&t))[1]=xf_hex[(n<<1) | 0];
	((u32*)(&t))[0]=xf_hex[(n<<1) | 1];
	printf("GetXD %f\n",n);
	return t;
}

static INLINE __attribute__((unused)) void SetDR(u32 n,f64 val)
{
#ifdef TRACE
	if (n>7)
		log("DR_w INDEX OVERRUN %d >7",n);
#endif
	fr_hex[(n<<1) | 1]=((u32*)(&val))[0];
	fr_hex[(n<<1) | 0]=((u32*)(&val))[1];
}

static INLINE __attribute__((unused)) void SetXD(u32 n,f64 val)
{
#ifdef TRACE
	if (n>7)
		log("XD_w INDEX OVERRUN %d >7",n);
#endif

	xf_hex[(n<<1) | 1]=((u32*)(&val))[0];
	xf_hex[(n<<1) | 0]=((u32*)(&val))[1];
}
#else
f64 GetDR(u32 n);
f64 GetXD(u32 n);
void SetDR(u32 n,f64 val);
void SetXD(u32 n,f64 val);
#endif

u32* Sh4_int_GetRegisterPtr(Sh4RegType reg);
void SetFloatStatusReg();