#pragma once

#define NO_MMU

//Basic types & stuff
#include "plugins/plugin_header.h"

//Basic types :)
//#include "basic_types.h"
#include <vector>
#include <string>
using namespace std;

#define No_gdb_stub
//#define DEBUG_DLL

//basic includes from runtime lib
#include <stdlib.h>
#include <stdio.h>

//used for asm-olny functions
#ifdef X86
#define naked   __declspec( naked )
#else
#define naked
#endif

//pageup = fast forward
#define DEV_TOOL_FAST_FW_KEY (VK_PRIOR)

#undef INCLUDE_DEV_TOOLS
#undef NO_VERIFY
#undef SB_MAP_UNKNOWN_REGS

//gli
#define RELEASE

//On release we have no checks
#ifndef RELEASE
	#define MEM_ALLOC_CHECK
	#define MEM_BOUND_CHECK
	#define MEM_ERROR_BREAK
#endif

#ifdef XENON
//force
#define INLINE
//sugest
#define SINLINE
//no inline :)
#define NOINLINE
#else
#if DEBUG
//force
#define INLINE
//sugest
#define SINLINE
#else
//force
#define INLINE __forceinline
//sugest
#define SINLINE __inline
#endif
//no inline :)
#define NOINLINE __declspec(noinline)
#endif

//basic includes
#include "stdclass.h"

	
#define EMUERROR(x)(printf("Error in %s:" "%s" ":%d  -> " x  "\n",GetNullDCSoruceFileName(__FILE__),__FUNCTION__,__LINE__))
#define EMUERROR2(x,a)(printf("Error in %s:" "%s" ":%d  -> " x  "\n",GetNullDCSoruceFileName(__FILE__),__FUNCTION__,__LINE__,a))
#define EMUERROR3(x,a,b)(printf("Error in %s:" "%s" ":%d  -> " x "\n",GetNullDCSoruceFileName(__FILE__),__FUNCTION__,__LINE__,a,b))
#define EMUERROR4(x,a,b,c)(printf("Error in %s:" "%s" ":%d  -> " x  "\n",GetNullDCSoruceFileName(__FILE__),__FUNCTION__,__LINE__,a,b,c))

#define EMUWARN(x)(printf( "Warning in %s:" "%s" ":%d  -> " x  "\n",GetNullDCSoruceFileName(__FILE__),__FUNCTION__,__LINE__))
#define EMUWARN2(x,a)(printf( "Warning in %s:" "%s" ":%d  -> " x  "\n",GetNullDCSoruceFileName(__FILE__),__FUNCTION__,__LINE__,a))
#define EMUWARN3(x,a,b)(printf( "Warning in %s:"  "%s" ":%d  -> " x  "\n"),GetNullDCSoruceFileName(__FILE__),__FUNCTION__,__LINE__,a,b))
#define EMUWARN4(x,a,b,c)(printf( "Warning in %s:" "%s" ":%d  -> " x  "\n",GetNullDCSoruceFileName(__FILE__),__FUNCTION__,__LINE__,a,b,c))


#ifndef NO_MMU
#define _X_x_X_MMU_VER_STR "/mmu"
#else
#define _X_x_X_MMU_VER_STR ""
#endif


#if DC_PLATFORM==DC_PLATFORM_NORMAL
	#define VER_EMUNAME		"nullDC"
#elif DC_PLATFORM==DC_PLATFORM_DEV_UNIT
	#define VER_EMUNAME		"nullDC-DevKit-SET5.21"
#elif DC_PLATFORM==DC_PLATFORM_NAOMI
	#define VER_EMUNAME		"nullDC-Naomi"
#elif DC_PLATFORM==DC_PLATFORM_ATOMISWAVE
	#define VER_EMUNAME		"nullDC-AtomisWave"
#else
	#error unknown target platform
#endif


#define VER_FULLNAME	VER_EMUNAME " v1.0.4" _X_x_X_MMU_VER_STR " (built " __DATE__ "@" __TIME__ ")"
#define VER_SHORTNAME	VER_EMUNAME " 1.0.4" _X_x_X_MMU_VER_STR

#ifdef XENON
#include <xenon_uart/xenon_uart.h>
#define __fastcall
#define fastcall
#define FASTCALL
#define __debugbreak getch
#else
#define fastcall __fastcall
#define FASTCALL __fastcall
#endif

#define dbgbreak asm volatile ("sc");

#ifndef NO_VERIFY
#define verify(x) if((x)==false){ msgboxf("Verify Failed  : " #x "\n in %s -> %s : %d \n",MBX_ICONERROR,(__FUNCTION__),(__FILE__),__LINE__); dbgbreak;}
#else
#define verify(__x__) /* __x__ */ ; 
#endif

#define die(reason) { msgboxf(("Fatal error : %s\n in %s -> %s : %d \n"),MBX_ICONERROR,(reason),(__FUNCTION__),(__FILE__),__LINE__); dbgbreak;}
#define fverify verify

//will be removed sometime soon
//This shit needs to be moved to proper headers
typedef u32  RegReadFP();
typedef void RegWriteFP(u32 data);
typedef u32  RegChangeFP();

enum RegStructFlags
{
	//Basic :
	REG_8BIT_READWRITE=1,	//used when doing direct reads from data for checks [not  on pvr , size is allways 32bits]
	REG_16BIT_READWRITE=2,	//used when doing direct reads from data for checks [not  on pvr , size is allways 32bits]
	REG_32BIT_READWRITE=4,	//used when doing direct reads from data for checks [not  on pvr , size is allways 32bits]
	REG_READ_DATA=8,		//we can read the data from the data member
	REG_WRITE_DATA=16,		//we can write the data to the data member
	REG_READ_PREDICT=32,	//we can call the predict function and know when the register will change
	//Extended :
	REG_WRITE_NOCHANGE=64,	//we can read and write to this register , but the write won't change the value readed 
	REG_CONST=128,			//register contains constant value
	REG_NOT_IMPL=256		//Register is not implemented/unknown
};

struct RegisterStruct
{
	union{
	u32* data32;					//stores data of reg variable [if used] 32b
	u16* data16;					//stores data of reg variable [if used] 16b
	u8* data8  ;					//stores data of reg variable [if used]	8b
	};
	RegReadFP* readFunction;	//stored pointer to reg read function
	RegWriteFP* writeFunction;	//stored pointer to reg write function
	u32 flags;					//flags for read/write
	#ifdef SB_MAP_UNKNOWN_REGS
	u16 unk;
	#endif
};


struct __settings
{
	struct
	{
		bool Enable;
		bool CPpass;
		bool UnderclockFpu;
		bool Safe;	//only use the 'safe' subset of the dynarec
	} dynarec;
	
	struct
	{
		u32 cable;
		u32 RTC;
		u32 region;
		u32 broadcast;
	} dreamcast;
	struct 
	{
		bool AutoStart;
		bool NoConsole;
	} emulator;
};
extern __settings settings;

void LoadSettings();
void SaveSettings();
u32 GetRTC_now();
extern u32 patchRB;


#include <byteswap.h>

#define bswap_n(data,size)
/*
#define bswap_n(data,size){				\
	if (size==8)						\
		data=bswap_64(data);			\
	else if (size==4)					\
		data=bswap_32(data);			\
	else if (size==2)					\
		data=bswap_16(data);			\
}
*/