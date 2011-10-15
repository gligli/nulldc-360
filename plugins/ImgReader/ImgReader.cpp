// nullGDR.cpp : Defines the entry point for the DLL application.
//

#include "ImgReader.h"
//Get a copy of the operators for structs ... ugly , but works :)
#include "common.h"

#include <time/time.h>

/*HINSTANCE hInstance;
BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
					 )
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		hInstance=(HINSTANCE)hModule;
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
    return TRUE;
}*/

_setts irsettings;
int cfgGetInt(wchar* key,int def)
{
	return emu.ConfigLoadInt("ImageReader",key,def);
}
void cfgSetInt(wchar* key,int v)
{
	emu.ConfigSaveInt("ImageReader",key,v);
}
void cfgGetStr(wchar* key,wchar* v,const wchar*def)
{
	emu.ConfigLoadStr("ImageReader",key,v,def);
}
void cfgSetStr(wchar* key,const wchar* v)
{
	emu.ConfigSaveStr("ImageReader",key,v);
}
void irLoadSettings()
{
	irsettings.PatchRegion=cfgGetInt("PatchRegion",0)!=0;
	irsettings.LoadDefaultImage=cfgGetInt("LoadDefaultImage",0)!=0;
	cfgGetStr("DefaultImage",irsettings.DefaultImage,"defualt.gdi");
	cfgGetStr("LastImage",irsettings.LastImage,"c:\\game.gdi");
	
	irsettings.LoadDefaultImage=true;
//	irsettings.LoadDefaultImage=false;
	strcpy(irsettings.DefaultImage,"sda:/dcisos/soulcalibur/Soul Calibur v1.000 (1999)(Namco)(NTSC)(US)[!][4S T-1401N].gdi");
//	strcpy(irsettings.DefaultImage,"sda:/dcisos/sonic/Sonic Adventure International v1.003 (1999)(Sega)(NTSC)(JP)(M5)[!].gdi");
//	irsettings.PatchRegion=true;
}
void irSaveSettings()
{
	cfgSetInt("PatchRegion",irsettings.PatchRegion);
	cfgSetInt("LoadDefaultImage",irsettings.LoadDefaultImage);
	cfgSetStr("DefaultImage",irsettings.DefaultImage);
	cfgSetStr("LastImage",irsettings.LastImage);
}
#define PLUGIN_NAME "Image Reader plugin by drk||Raziel & GiGaHeRz [" __DATE__ "]"
#define PLUGIN_NAMEW "Image Reader plugin by drk||Raziel & GiGaHeRz [" __DATE__ "]"

void FASTCALL GetSessionInfo(u8* out,u8 ses);

void FASTCALL DriveReadSubChannel(u8 * buff, u32 format, u32 len)
{
	if (format==0)
	{
		memcpy(buff,q_subchannel,len);
	}
}

void FASTCALL DriveReadSector(u8 * buff,u32 StartSector,u32 SectorCount,u32 secsz)
{
	printf("DriveReadSector %5d %5d %5d\n",StartSector,SectorCount,secsz); 
	
	GetDriveSector(buff,StartSector,SectorCount,secsz);

//    buffer_dump(buff,secsz);
	
	//if (CurrDrive)
	//	CurrDrive->ReadSector(buff,StartSector,SectorCount,secsz);
}

void FASTCALL DriveGetTocInfo(u32* toc,u32 area)
{
	printf("DriveGetTocInfo %5d\n",area); 
	
	GetDriveToc(toc,(DiskArea)area);
}
//TODO : fix up
u32 FASTCALL DriveGetDiscType()
{
	u32 dt;
	
	if (disc)
		dt=disc->type;
	else
		dt=NullDriveDiscType;
	
	printf("DriveGetDiscType %d\n",dt); 

	return dt;
}

void FASTCALL GetSessionInfo(u8* out,u8 ses)
{
	printf("GetSessionInfo\n"); 
	GetDriveSessionInfo(out,ses);
}
emu_info emu;
wchar iremu_name[512];
void EXPORT_CALL handle_PatchRegion(u32 id,void* w,void* p)
{
	if (irsettings.PatchRegion)
		irsettings.PatchRegion=0;
	else
		irsettings.PatchRegion=1;

	emu.SetMenuItemStyle(id,irsettings.PatchRegion?MIS_Checked:0,MIS_Checked);

	irSaveSettings();
}

void EXPORT_CALL handle_UseDefImg(u32 id,void* w,void* p)
{
	if (irsettings.LoadDefaultImage)
		irsettings.LoadDefaultImage=0;
	else
		irsettings.LoadDefaultImage=1;

	emu.SetMenuItemStyle(id,irsettings.LoadDefaultImage?MIS_Checked:0,MIS_Checked);

	irSaveSettings();
}
void EXPORT_CALL handle_SelDefImg(u32 id,void* w,void* p)
{
	if (GetFile(irsettings.DefaultImage,0,1)==1)//no "no disk"
	{
		irSaveSettings();
	}
}

void EXPORT_CALL irhandle_About(u32 id,void* w,void* p)
{
	printf("About ImageReader..." "\n" "Made by drk||Raziel & GiGaHeRz" "\n");
}

void EXPORT_CALL handle_SwitchDisc(u32 id,void* w,void* p)
{
	//msgboxf("This feature is not yet implemented",MB_ICONWARNING);
	//return;
	TermDrive();
	
	NullDriveDiscType=Busy;
	DriveNotifyEvent(DiskChange,0);
	mdelay(150);	//busy for a bit

	NullDriveDiscType=Open;
	DriveNotifyEvent(DiskChange,0);
	mdelay(150); //tray is open

	while(!InitDrive(2))//no "cancel"
		msgboxf("Init Drive failed, disc must be valid for swap",0x00000010L);

	DriveNotifyEvent(DiskChange,0);
	//new disc is in
}
//called when plugin is used by emu (you should do first time init here)
s32 FASTCALL irLoad(emu_info* emu_inf)
{
	if (emu_inf==0)
		return rv_ok;
	memcpy(&emu,emu_inf,sizeof(emu));

	emu.ConfigLoadStr("emu","shortname",iremu_name,0);
	
	irLoadSettings();

	emu.AddMenuItem(emu.RootMenu,-1,"Swap Disc",handle_SwitchDisc,irsettings.LoadDefaultImage);
	emu.AddMenuItem(emu.RootMenu,-1,0,0,0);
	emu.AddMenuItem(emu.RootMenu,-1,"Use Default Image",handle_UseDefImg,irsettings.LoadDefaultImage);
	emu.AddMenuItem(emu.RootMenu,-1,"Select Default Image",handle_SelDefImg,0);
	emu.AddMenuItem(emu.RootMenu,-1,"Patch GDROM region",handle_PatchRegion,irsettings.PatchRegion);
	emu.AddMenuItem(emu.RootMenu,-1,0,0,0);
	emu.AddMenuItem(emu.RootMenu,-1,"About",irhandle_About,0);
	
	
	return rv_ok;
}

//called when plugin is unloaded by emu , olny if dcInitGDR is called (eg , not called to enumerate plugins)
void FASTCALL irUnload()
{
	
}

//It's suposed to reset everything (if not a manual reset)
void FASTCALL ResetGDR(bool Manual)
{
	DriveNotifyEvent(DiskChange,0);
}

//called when entering sh4 thread , from the new thread context (for any thread speciacific init)
s32 FASTCALL InitGDR(gdr_init_params* prm)
{
	DriveNotifyEvent=prm->DriveNotifyEvent;
	if (!InitDrive())
		return rv_serror;
	DriveNotifyEvent(DiskChange,0);
	irLoadSettings();
	return rv_ok;
}

//called when exiting from sh4 thread , from the new thread context (for any thread speciacific de init) :P
void FASTCALL TermGDR()
{
	TermDrive();
}

//Give to the emu pointers for the gd rom interface
void EXPORT_CALL irGetInterface(plugin_interface* info)
{
#define c info->common
#define g info->gdr
	
	info->InterfaceVersion=PLUGIN_I_F_VERSION;

	c.Type=Plugin_GDRom;
	c.InterfaceVersion=GDR_PLUGIN_I_F_VERSION;

	strcpy(c.Name,PLUGIN_NAMEW);
	
	c.Load=irLoad;
	c.Unload=irUnload;
	
	
	g.Init=InitGDR;
	g.Term=TermGDR;
	g.Reset=ResetGDR;
	
	g.GetDiscType=DriveGetDiscType;
	g.GetToc=DriveGetTocInfo;
	g.ReadSector=DriveReadSector;
	g.GetSessionInfo=GetSessionInfo;
	g.ReadSubChannel=DriveReadSubChannel;
	g.ExeptionHanlder=0;
}