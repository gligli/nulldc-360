#pragma once
#include "nullDC/types.h"
#include <stdlib.h>
#include <stdio.h>

#include <string.h>
#include <assert.h>

#include <ppc/timebase.h>

#define UINT16 u16
#define INT32 int32_t
#define DWORD u32
#define UINT32 u32

#define BUILD 0
#define MINOR 1
#define MAJOR 0

static inline bool _BitScanReverse(unsigned long * r,u32 v){
	*r=31-__builtin_clz(v);
    return v!=0;
}

/*static inline u32 timeGetTime(){
	return mftb()/(PPC_TIMEBASE_FREQ/1000);
}*/

static inline bool QueryPerformanceCounter(u64 * v){
    *v=mftb();
    return true;
}

static inline bool QueryPerformanceFrequency(u64 * v){
    *v=PPC_TIMEBASE_FREQ;
    return true;
}

#define max(a,b) (((a)>(b))?(a):(b))
#define min(a,b) (((a)<(b))?(a):(b))

int msgboxf(wchar* text,unsigned int type,...);

#define DCclock (200*1000*1000)

//called when plugin is used by emu (you should do first time init here)
void dcInit(void* param,PluginType type);

//called when plugin is unloaded by emu , olny if dcInit is called (eg , not called to enumerate plugins)
void dcTerm(PluginType type);

//It's suposed to reset anything 
void dcReset(bool Manual,PluginType type);

//called when entering sh4 thread , from the new thread context (for any thread speciacific init)
void dcThreadInit(PluginType type);

//called when exiting from sh4 thread , from the new thread context (for any thread speciacific de init) :P
void dcThreadTerm(PluginType type);

#define 	ReadMemArrRet(arr,addr,sz)				\
			{if (sz==1)								\
				return arr[addr];					\
			else if (sz==2)							\
				return *(u16*)&arr[addr];			\
			else if (sz==4)							\
				return *(u32*)&arr[addr];}	

#define WriteMemArrRet(arr,addr,data,sz)				\
			{if(sz==1)								\
				{arr[addr]=(u8)data;return;}				\
			else if (sz==2)							\
				{*(u16*)&arr[addr]=(u16)data;return;}		\
			else if (sz==4)							\
			{*(u32*)&arr[addr]=data;return;}}	
#define WriteMemArr(arr,addr,data,sz)				\
			{if(sz==1)								\
				{arr[addr]=(u8)data;}				\
			else if (sz==2)							\
				{*(u16*)&arr[addr]=(u16)data;}		\
			else if (sz==4)							\
			{*(u32*)&arr[addr]=data;}}	
struct aica_setts
{
	u32 SoundRenderer;	//0 -> sdl , (1) -> DS
	u32 HW_mixing;		//(0) -> SW , 1 -> HW , 2 -> Auto
	u32 BufferSize;		//In samples ,*4 for bytes (1024)
	u32 LimitFPS;		//0 -> no , (1) -> limit
	u32 GlobalFocus;	//0 -> only hwnd , (1) -> Global
	u32 BufferCount;	//BufferCount+2 buffers used , max 60 , default 0
	u32 CDDAMute;
	u32 GlobalMute;
	u32 DSPEnabled;		//0 -> no, 1 -> yes
	u32 Volume;		    //0-100
};

extern aica_setts aica_settings;
extern aica_init_params aica_params;

void LoadSettingsAica();
void SaveSettingsAica();

int cfgGetInt(char* key,int def);
