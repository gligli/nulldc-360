#pragma once
#define _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_DEPRECATE
#include "config.h"



//bleh stupid windoze header
#include "../../nullDC/types.h"
#include <stdlib.h>
#include <stdio.h>

#include <string.h>
#include <assert.h>

#include <ppc/timebase.h>

#define __assume assert

static inline bool _BitScanReverse(unsigned long * r,u32 v){
	*r=31-__builtin_clz(v);
    return v!=0;
}

static inline u32 timeGetTime(){
	return mftb()/(PPC_TIMEBASE_FREQ/1000);
}

int msgboxf(char* text,unsigned int type,...);

#define BUILD 0
#define MINOR 1
#define MAJOR 0
#define DCclock (200*1000*1000)


float GetSeconds();


#define dlog0(xx) printf(xx " (from "__FUNCTION__ ")\n");
#define dlog1(xx,yy) printf(xx " (from "__FUNCTION__ ")\n",yy);
#define dlog2(xx,yy,zz) printf(xx " (from "__FUNCTION__ ")\n",yy,zz);
#define dlog3(xx,yy,gg) printf(xx " (from "__FUNCTION__ ")\n",yy,zz,gg);

#include "helper_classes.h"

extern volatile bool render_end_pending;
extern u32 render_end_pending_cycles;

extern pvr_init_params params;
extern emu_info emu;
extern char emu_name[512];

void LoadSettingsPvr();
void SaveSettingsPvr();

#define REND_NAME "Xenos"
#define GetRenderer GetXenosRenderer

struct _drkpvr_settings_type
{
	struct
	{
		u32 ResolutionMode;
		u32 VSync;
	} Video;

	struct 
	{
		u32 MultiSampleCount;
		u32 MultiSampleQuality;
		u32 AspectRatioMode;
	} Enhancements;
	
	struct
	{
		u32 PaletteMode;
		u32 AlphaSortMode;
		u32 ModVolMode;
		u32 ZBufferMode;
		u32 TexCacheMode;
	} Emulation;

	struct
	{
		u32 ShowFPS;
		u32 ShowStats;
	} OSD;
};
enum ModVolMode
{
	MVM_NormalAndClip,
	MVM_Normal,
	MVM_Off,
	MVM_Volume,
};
extern _drkpvr_settings_type drkpvr_settings;
void UpdateRRect();