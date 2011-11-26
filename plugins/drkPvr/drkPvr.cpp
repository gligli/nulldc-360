// drkPvr.cpp : Defines the entry point for the DLL application.
//

/*
	Plugin structure
	Interface
	SPG
	TA
	Renderer
*/

#include <algorithm>

#include "drkPvr.h"

#include "ta.h"
#include "spg.h"
#include "regs.h"
#include "Renderer_if.h"

#include "threadedPvr.h"

//RaiseInterruptFP* RaiseInterrupt;

//void* Hwnd;

wchar emu_name_pvr[512];

pvr_init_params params;
_drkpvr_settings_type drkpvr_settings;

/*
BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
					 )
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
    return TRUE;
}
*/

class Optiongroup
{
public:
	Optiongroup()
	{
		root_menu=0;
		format=0;
	}
	u32 root_menu;
	wchar* format;
	struct itempair { u32 id;int value;wchar* ex_name;};
	vector<itempair> items;

	void (*callback) (int val) ;
	void Add(u32 id,int val,wchar* ex_name) { itempair t={id,val,ex_name}; items.push_back(t); }
	void Add(u32 root,wchar* name,int val,wchar* ex_name=0,int style=0) 
	{ 
		if (root_menu==0)
			root_menu=root;
		u32 ids=emu.AddMenuItem(root,-1,name,handler,0);
		emu.SetMenuItemStyle(ids,style,style);
		
		MenuItem t;
		t.PUser=this;
		emu.SetMenuItem(ids,&t,MIM_PUser);

		Add(ids,val,ex_name);
	}

	static void EXPORT_CALL handler(u32 id,void* win,void* puser)
	{
		Optiongroup* pthis=(Optiongroup*)puser;
		pthis->handle(id);
	}
	void SetValue(int val)
	{
		for (u32 i=0;i<items.size();i++)
		{
			if (val==items[i].value)
			{
				emu.SetMenuItemStyle(items[i].id,MIS_Checked,MIS_Checked);
				if (root_menu && format)
				{
					MenuItem t;
					emu.GetMenuItem(items[i].id,&t,MIM_Text);
					wchar temp[512];
					snprintf(temp,512,format,items[i].ex_name==0?t.Text:items[i].ex_name);
					t.Text=temp;
					emu.SetMenuItem(root_menu,&t,MIM_Text);
				}
			}
			else
				emu.SetMenuItemStyle(items[i].id,0,MIS_Checked);
		}
		callback(val);
	}
	void handle(u32 id)
	{
		int val=0;
		for (u32 i=0;i<items.size();i++)
		{
			if (id==items[i].id)
			{
				val=items[i].value;
			}
		}

		SetValue(val);
	}

};
void AddSeperator(u32 menuid)
{
	emu.AddMenuItem(menuid,-1,0,0,0);
}
Optiongroup menu_res;
Optiongroup menu_sortmode;
Optiongroup menu_modvolmode;
Optiongroup menu_palmode;
Optiongroup menu_zbuffer;
Optiongroup menu_TCM;
Optiongroup menu_widemode;
Optiongroup menu_resolution;
int oldmode=-1;
int osx=-1,osy=-1;


//dst[0] -> width of rect to be adjusted
//dst[1] -> height
//src: w,h of rect to be matched
void CalcRect(float* dst,float* src)
{
	if (drkpvr_settings.Enhancements.AspectRatioMode!=0)
	{
		//printf("New rect %d %d\n",sx,sy);
		float nw=((float)src[0]/(float)src[1])*dst[1];
		float nh=((float)src[1]/(float)src[0])*dst[0];
		if (nh<dst[1])
		{
			nh=dst[1];
		}
		else
		{
			nw=dst[0];
		}
		dst[0]=nw;
		dst[1]=nh;
	}
}
void UpdateRRect()
{
	float dc_wh[2]={640,480};
	float rect[4];

	if(xe){
		int sx=Xe_GetFramebufferSurface(xe)->width;
		int sy=Xe_GetFramebufferSurface(xe)->height;
		if (osx!=sx || osy!=sy)
		{
			osx=sx;
			osy=sy;
			oldmode=-1;
		}

		float win_wh[2]={(float)sx,(float)sy};

		CalcRect(dc_wh,win_wh);
	}

	rect[0]=(dc_wh[0]-640)/2;
	rect[2]=dc_wh[0];
	rect[1]=(dc_wh[1]-480)/2;
	rect[3]=dc_wh[1];

	rend_set_render_rect(rect,oldmode!=drkpvr_settings.Enhancements.AspectRatioMode);
	oldmode=drkpvr_settings.Enhancements.AspectRatioMode;
}

void FASTCALL vramLockCB (vram_block* block,u32 addr)
{
	rend_text_invl(block);
}
#include <vector>
using std::vector;

extern volatile bool render_restart;

void EXPORT_CALL handler_Vsync (u32 id,void* win,void* puser)
{
	if (drkpvr_settings.Video.VSync)
		drkpvr_settings.Video.VSync=0;
	else
		drkpvr_settings.Video.VSync=1;

	emu.SetMenuItemStyle(id,drkpvr_settings.Video.VSync?MIS_Checked:0,MIS_Checked);
	
	SaveSettingsPvr();
	render_restart=true;
}

void EXPORT_CALL handler_ShowStats(u32 id,void* win,void* puser)
{
	if (drkpvr_settings.OSD.ShowStats)
		drkpvr_settings.OSD.ShowStats=0;
	else
		drkpvr_settings.OSD.ShowStats=1;

	emu.SetMenuItemStyle(id,drkpvr_settings.OSD.ShowStats?MIS_Checked:0,MIS_Checked);
	
	SaveSettingsPvr();
}

void EXPORT_CALL handler_ShowFps(u32 id,void* win,void* puser)
{
	if (drkpvr_settings.OSD.ShowFPS)
		drkpvr_settings.OSD.ShowFPS=0;
	else
		drkpvr_settings.OSD.ShowFPS=1;

	emu.SetMenuItemStyle(id,drkpvr_settings.OSD.ShowFPS?MIS_Checked:0,MIS_Checked);
	
	SaveSettingsPvr();
}
void handler_widemode(int mode)
{
	drkpvr_settings.Enhancements.AspectRatioMode=mode;
		
	SaveSettingsPvr();
	UpdateRRect();
}

void handler_resmode(int mode)
{
	drkpvr_settings.Video.ResolutionMode=mode;
		
	SaveSettingsPvr();
	rend_handle_event(NDE_GUI_RESIZED,0);
}
void handler_PalMode(int  mode)
{
	drkpvr_settings.Emulation.PaletteMode=mode;
	SaveSettingsPvr();
}
void handler_ModVolMode(int  mode)
{
	drkpvr_settings.Emulation.ModVolMode=mode;
	SaveSettingsPvr();
}
void handler_TexCacheMode(int  mode)
{
	drkpvr_settings.Emulation.TexCacheMode=mode;
	SaveSettingsPvr();
}

void handler_ZBufferMode(int  mode)
{
	drkpvr_settings.Emulation.ZBufferMode=mode;
	SaveSettingsPvr();
	render_restart=true;
}
u32 AA_mid_menu;
u32 AA_mid_0;

struct resolution
{
	u32 w;
	u32 h;
	u32 rr;
};
bool operator<(const resolution &left, const resolution &right)
{
	/* put any condition you want to sort on here */
	if (left.h*(u64)left.w>right.h*(u64)right.w)
		return true;
	else if (left.h*(u64)left.w==right.h*(u64)right.w)
		return left.rr>right.rr;
	else
		return false;
}

void EXPORT_CALL handle_About(u32 id,void* w,void* p)
{
//	MessageBox((HWND)w,"Made by the nullDC Team","About nullDC PVR...",MB_ICONINFORMATION);
}
u32 sort_menu;
u32 sort_sm[3];


u32 ssm(u32 nm)
{
	nm=min(nm,2);

	return nm;
}

void handler_SSM(int val)
{
	drkpvr_settings.Emulation.AlphaSortMode=val;

	SaveSettingsPvr();
}
void CreateSortMenu()
{
	sort_menu=emu.AddMenuItem(emu.RootMenu,-1,"Sort : %s",0,0);
	
	menu_sortmode.format="Sort : %s";

	menu_sortmode.callback=handler_SSM;
	menu_sortmode.Add(sort_menu,"Off (Fastest, lowest accuracy)",0,"Off");
	menu_sortmode.Add(sort_menu,"Per Strip",1,"Strip");
	menu_sortmode.Add(sort_menu,"Per Triangle (Slowest, highest accuracy)",2,"Triangle");
	menu_sortmode.SetValue(drkpvr_settings.Emulation.AlphaSortMode);
}
//called when plugin is used by emu (you should do first time init here)
s32 FASTCALL LoadPvr(emu_info* emu_inf)
{
	// wchar temp[512]; // Unreferenced
	memcpy(&emu,emu_inf,sizeof(emu));
	emu.ConfigLoadStr("emu","shortname",emu_name_pvr,0);
	
	LoadSettingsPvr();

	u32 RSM=emu.AddMenuItem(emu.RootMenu,-1,"Render Resolution: %s",0,0);
	menu_resolution.format="Resolution: %s";
	menu_resolution.callback=handler_resmode;

	menu_resolution.Add(RSM,"Maximum Supported (Highest quality)",0);
	menu_resolution.Add(RSM,"Maximum, but up to 1280x800",1);
	menu_resolution.Add(RSM,"Native (640x480)",2);
	menu_resolution.Add(RSM,"Half of maximum pixels",3);
	menu_resolution.Add(RSM,"Quarter of maximum pixels (Lowest quality)",4);
	menu_resolution.SetValue(drkpvr_settings.Video.ResolutionMode);


	u32 WSM=emu.AddMenuItem(emu.RootMenu,-1,"Aspect Ratio: %s",0,0);
	
	menu_widemode.format="Aspect Ratio: %s";
	menu_widemode.callback=handler_widemode;

	menu_widemode.Add(WSM,"Stretch",0);
	menu_widemode.Add(WSM,"Borders",1);
	menu_widemode.Add(WSM,"Extra Geom",2);
	menu_widemode.SetValue(drkpvr_settings.Enhancements.AspectRatioMode);

	u32 PMT=emu.AddMenuItem(emu.RootMenu,-1,"Palette Handling",0,0);
	
	menu_palmode.callback=handler_PalMode;

	menu_palmode.format="Paletted Textures: %s";
	menu_palmode.Add(PMT,"Static",0);
	menu_palmode.Add(PMT,"Versioned",1);
	AddSeperator(PMT);
	menu_palmode.Add(PMT,"Dynamic, Point",2);
	menu_palmode.Add(PMT,"Dynamic, Full",3);

	menu_palmode.SetValue(drkpvr_settings.Emulation.PaletteMode);

	CreateSortMenu();

	u32 MVM=emu.AddMenuItem(emu.RootMenu,-1,"Modifier Volumes: %s",0,0);

	menu_modvolmode.format="Modifier Volumes: %s";
	menu_modvolmode.callback=handler_ModVolMode;

	menu_modvolmode.Add(MVM,"Normal And Clip (Slowest, highest accuracy)",MVM_NormalAndClip);
	menu_modvolmode.Add(MVM,"Normal (Good speed, good accuracy)",MVM_Normal);
	menu_modvolmode.Add(MVM,"Off  (Fastest, no shadows)",MVM_Off);
	menu_modvolmode.Add(MVM,"Volumes (For debuging)",MVM_Volume);
	menu_modvolmode.SetValue(drkpvr_settings.Emulation.ModVolMode);

	u32 ZBM=emu.AddMenuItem(emu.RootMenu,-1,"Z Buffer Mode: %s",0,0);

	menu_zbuffer.format="Z Buffer Mode: %s";
	menu_zbuffer.callback=handler_ZBufferMode;

	menu_zbuffer.Add(ZBM,"D24S8 Adaptive Linear (New, Buggy still)",4);
	menu_zbuffer.Add(ZBM,"D24FS8 (Fast when avaiable, Best Precision)",0);
	menu_zbuffer.Add(ZBM,"D24S8+FPE (Slow, Good Precision)",1);
	menu_zbuffer.Add(ZBM,"D24S8 Mode 1 (Lower Precision)",2);
	menu_zbuffer.Add(ZBM,"D24S8 Mode 2 (Lower Precision)",3);

	menu_zbuffer.SetValue(drkpvr_settings.Emulation.ZBufferMode);
	
	u32 TCM=emu.AddMenuItem(emu.RootMenu,-1,"Texture Cache Mode: %s",0,0);

	menu_TCM.format="Texture Cache Mode: %s";
	menu_TCM.callback=handler_TexCacheMode;

	menu_TCM.Add(TCM,"Delete old",0);
	menu_TCM.Add(TCM,"Delete invalidated",1);

	menu_TCM.SetValue(drkpvr_settings.Emulation.TexCacheMode);

	AddSeperator(emu.RootMenu);
	
	emu.AddMenuItem(emu.RootMenu,-1,"Vsync",handler_Vsync,drkpvr_settings.Video.VSync);
	emu.AddMenuItem(emu.RootMenu,-1,"Show Fps",handler_ShowFps,drkpvr_settings.OSD.ShowFPS);
	emu.AddMenuItem(emu.RootMenu,-1,"Show Stats",handler_ShowStats,drkpvr_settings.OSD.ShowStats);

	AddSeperator(emu.RootMenu);
	
	emu.AddMenuItem(emu.RootMenu,-1,"About",handle_About,0);

	return rv_ok;
}

//called when plugin is unloaded by emu , olny if dcInitPvr is called (eg , not called to enumerate plugins)
void FASTCALL UnloadPvr()
{
	
}

//It's suposed to reset anything but vram (vram is set to 0 by emu)
void FASTCALL ResetPvr(bool Manual)
{
	Regs_Reset(Manual);
	spg_Reset(Manual);
	rend_reset(Manual);
}

//called when entering sh4 thread , from the new thread context (for any thread speciacific init)
s32 FASTCALL InitPvr(pvr_init_params* param)
{
	memcpy(&params,param,sizeof(params));

	extern void BuildTwiddleTables();
	BuildTwiddleTables();

	if ((!Regs_Init()))
	{
		//failed
		return rv_error;
	}
	if (!spg_Init())
	{
		//failed
		return rv_error;
	}
	if (!rend_init())
	{
		//failed
		return rv_error;
	}
	UpdateRRect();
	//olny the renderer cares about thread speciacific shit ..
	if (!rend_thread_start())
	{
		return rv_error;
	}

	threaded_init();
	
	return rv_ok;
}

//called when exiting from sh4 thread , from the new thread context (for any thread speciacific de init) :P
void FASTCALL TermPvr()
{
	rend_thread_end();

	rend_term();
	spg_Term();
	Regs_Term();
	
	threaded_term();
}

//Helper functions
float GetSeconds()
{
	return timeGetTime()/1000.0f;
}

void EXPORT_CALL EventHandler(u32 id,void*p)
{
	rend_handle_event(id,p);
}
//Give to the emu pointers for the PowerVR interface
void EXPORT_CALL drkPvrGetInterface(plugin_interface* info)
{
#define c  info->common
#define p info->pvr

	info->InterfaceVersion=PLUGIN_I_F_VERSION;
	
	c.Type=Plugin_PowerVR;
	c.InterfaceVersion=PVR_PLUGIN_I_F_VERSION;

	strcpy(c.Name,"nullDC PowerVR -- " REND_NAME " [" __DATE__ "]");

	c.Load=LoadPvr;
	c.Unload=UnloadPvr;
	c.EventHandler=EventHandler;
	p.ExeptionHanlder=0;
	p.Init=InitPvr;
	p.Reset=ResetPvr;
	p.Term=TermPvr;

	
	p.ReadReg=ReadPvrRegister;
	p.WriteReg=WritePvrRegister;

#if 1
	p.UpdatePvr=threaded_spgUpdatePvr;
	
	p.TaDMA=threaded_TADma;
	p.TaSQ=threaded_TASQ;
#else
	p.UpdatePvr=spgUpdatePvr;
	
	p.TaDMA=TASplitter::Dma;
	p.TaSQ=TASplitter::SQ;
#endif	
	
	p.LockedBlockWrite=vramLockCB;
	
#undef c
#undef p
}

//End AutoResetEvent

int cfgGetIntPvr(wchar* key,int def)
{
	return emu.ConfigLoadInt("drkpvr",key,def);
}
void cfgSetIntPvr(wchar* key,int val)
{
	emu.ConfigSaveInt("drkpvr",key,val);
}

void LoadSettingsPvr()
{
	drkpvr_settings.Emulation.AlphaSortMode			=	cfgGetIntPvr("Emulation.AlphaSortMode",1);
	drkpvr_settings.Emulation.PaletteMode				=	cfgGetIntPvr("Emulation.PaletteMode",1);
	drkpvr_settings.Emulation.ModVolMode				= 	cfgGetIntPvr("Emulation.ModVolMode",MVM_NormalAndClip);
	drkpvr_settings.Emulation.ZBufferMode				= 	cfgGetIntPvr("Emulation.ZBufferMode",0);
	drkpvr_settings.Emulation.TexCacheMode				= 	cfgGetIntPvr("Emulation.TexCacheMode",0);

	drkpvr_settings.OSD.ShowFPS						=	cfgGetIntPvr("OSD.ShowFPS",0);
	drkpvr_settings.OSD.ShowStats						=	cfgGetIntPvr("OSD.ShowStats",0);

	drkpvr_settings.Video.ResolutionMode				=	cfgGetIntPvr("Video.ResolutionMode",0);
	drkpvr_settings.Video.VSync						=	cfgGetIntPvr("Video.VSync",0);

	drkpvr_settings.Enhancements.MultiSampleCount		=	cfgGetIntPvr("Enhancements.MultiSampleCount",0);
	drkpvr_settings.Enhancements.MultiSampleQuality	=	cfgGetIntPvr("Enhancements.MultiSampleQuality",0);
	drkpvr_settings.Enhancements.AspectRatioMode		=	cfgGetIntPvr("Enhancements.AspectRatioMode",1);
	
	drkpvr_settings.Emulation.ZBufferMode=2;
	drkpvr_settings.Emulation.AlphaSortMode=1;
	drkpvr_settings.Emulation.ModVolMode=MVM_Off;
}


void SaveSettingsPvr()
{
	cfgSetIntPvr("Emulation.AlphaSortMode",drkpvr_settings.Emulation.AlphaSortMode);
	cfgSetIntPvr("Emulation.PaletteMode",drkpvr_settings.Emulation.PaletteMode);
	cfgSetIntPvr("Emulation.ModVolMode",drkpvr_settings.Emulation.ModVolMode);
	cfgSetIntPvr("Emulation.ZBufferMode",drkpvr_settings.Emulation.ZBufferMode);
	cfgSetIntPvr("Emulation.TexCacheMode",drkpvr_settings.Emulation.TexCacheMode);

	cfgSetIntPvr("OSD.ShowFPS",drkpvr_settings.OSD.ShowFPS);
	cfgSetIntPvr("OSD.ShowStats",drkpvr_settings.OSD.ShowStats);

	cfgSetIntPvr("Video.ResolutionMode",drkpvr_settings.Video.ResolutionMode);
	cfgSetIntPvr("Video.VSync",drkpvr_settings.Video.VSync);

	cfgSetIntPvr("Enhancements.MultiSampleCount",drkpvr_settings.Enhancements.MultiSampleCount);
	cfgSetIntPvr("Enhancements.MultiSampleQuality",drkpvr_settings.Enhancements.MultiSampleQuality);
	cfgSetIntPvr("Enhancements.AspectRatioMode",drkpvr_settings.Enhancements.AspectRatioMode);
}
