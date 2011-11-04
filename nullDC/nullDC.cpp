// nullDC.cpp : Makes magic cookies
//

#include <debug.h>
#include <xenos/xenos.h>
#include <input/input.h>
#include <console/console.h>
#include <console/telnet_console.h>
#include <diskio/diskio.h>
#include <diskio/ata.h>
#include <usb/usbmain.h>
#include <time/time.h>
#include <ppc/timebase.h>
#include <xenon_soc/xenon_power.h>
#include <network/network.h>

//initialse Emu
#include "types.h"
#include "dc/mem/_vmem.h"
#include "stdclass.h"
#include "dc/dc.h"
#include "gui/base.h"
#include "config/config.h"
#include "plugins/plugin_manager.h"
#include "serial_ipc/serial_ipc_client.h"
#include "cl/cl.h"
//#include "emitter/emitter.h"

#define max(a,b) (((a)>(b))?(a):(b))
#define min(a,b) (((a)<(b))?(a):(b))

__settings settings;

/*BOOL CtrlHandler( DWORD fdwCtrlType ) 
{ 
  switch( fdwCtrlType ) 
  { 
	  case CTRL_SHUTDOWN_EVENT: 
	  case CTRL_LOGOFF_EVENT: 
	  // Pass other signals to the next handler. 
    case CTRL_BREAK_EVENT: 
	  // CTRL-CLOSE: confirm that the user wants to exit. 
    case CTRL_CLOSE_EVENT: 
    // Handle the CTRL-C signal. 
    case CTRL_C_EVENT: 
		SendMessageA((HWND)GetRenderTargetHandle(),WM_CLOSE,0,0); //FIXME
      return( TRUE );
    default: 
      return FALSE; 
  } 
} */

//Simple command line bootstrap
int RunDC(int argc, wchar* argv[])
{

	if(settings.dynarec.Enable)
	{
		sh4_cpu=Get_Sh4Recompiler();
		log("Using Recompiler\n");
	}
	else
	{
		sh4_cpu=Get_Sh4Interpreter();
		log("Using Interpreter\n");
	}
	
	if (settings.emulator.AutoStart)
		Start_DC();

	GuiLoop();

	Term_DC();
	Release_Sh4If(sh4_cpu);
	return 0;
}


void EnumPlugins()
{
	EnumeratePlugins();

	vector<PluginLoadInfo>* pvr= GetPluginList(Plugin_PowerVR);
	vector<PluginLoadInfo>* gdrom= GetPluginList(Plugin_GDRom);
	vector<PluginLoadInfo>* aica= GetPluginList(Plugin_AICA);
	vector<PluginLoadInfo>* arm= GetPluginList(Plugin_ARM);
	vector<PluginLoadInfo>* maple= GetPluginList(Plugin_Maple);
	vector<PluginLoadInfo>* extdev= GetPluginList(Plugin_ExtDevice);

	log("PowerVR plugins :\n");
	for (u32 i=0;i<pvr->size();i++)
	{
		printf("*\tFound %s\n" ,(*pvr)[i].Name);
	}

	log("\nGDRom plugins :\n");
	for (u32 i=0;i<gdrom->size();i++)
	{
		printf("*\tFound %s\n" ,(*gdrom)[i].Name);
	}

	
	log("\nAica plugins :\n");
	for (u32 i=0;i<aica->size();i++)
	{
		printf("*\tFound %s\n" ,(*aica)[i].Name);
	}

	log("\nArm plugins :\n");
	for (u32 i=0;i<arm->size();i++)
	{
		printf("*\tFound %s\n" ,(*arm)[i].Name);
	}

	log("\nMaple plugins :\n");
	for (u32 i=0;i<maple->size();i++)
	{
		printf("*\tFound %s\n" ,(*maple)[i].Name);
	}
	log("\nExtDevice plugins :\n");
	for (u32 i=0;i<extdev->size();i++)
	{
		printf("*\tFound %s\n" ,(*extdev)[i].Name);
	}

	delete pvr;
	delete gdrom;
	delete aica;
	delete maple;
	delete extdev;
}

int main___(int argc,char* argv[])
{
	if(ParseCommandLine(argc,argv))
	{
		log("\n\n(Exiting due to command line, without starting nullDC)\n");
		return 69;
	}

	if(!cfgOpen())
	{
		msgboxf(_T("Unable to open config file"),MBX_ICONERROR);
		return -4;
	}
	LoadSettings();

	if (!CreateGUI())
	{
		msgboxf(_T("Creating GUI failed\n"),MBX_ICONERROR);
		return -1;
	}
	int rv= 0;

	wchar* temp_path=GetEmuPath(_T("data/"));

	bool lrf=LoadRomFiles(temp_path);

	free(temp_path);

	if (!lrf)
	{
		rv=-3;
		goto cleanup;
	}

	EnumPlugins();

	while (!plugins_Load())
	{
		if (!plugins_Select())
		{
			msgboxf(_T"Unable to load plugins -- exiting\n",MBX_ICONERROR);
			rv = -2;
			goto cleanup;
		}
	}
	
	console_close();
	
	rv = RunDC(argc,argv);
	
cleanup:
	DestroyGUI();
	
	SaveSettings();
	return rv;
}

int main()
{
	int argc=1;
	char* argv[] ={"uda:/xenon.elf"};

	xenos_init(VIDEO_MODE_AUTO);
	console_init();
	xenon_make_it_faster(XENON_SPEED_FULL);
	
/*	network_init();
	network_print_config();
	
	telnet_console_init();*/

	usb_init();
	usb_do_poll();
	
	xenon_ata_init();
	
	if (!_vmem_reserve())
	{
		msgboxf(_T"Unable to reserve nullDC memory ...",MBX_OK | MBX_ICONERROR);
		return -5;
	}
	int rv=0;

	__try
	{
		rv=main___(argc,argv);
	}
	//__except( ExeptionHandler( GetExceptionCode(), (GetExceptionInformation()) ) )
	catch(...)
	{
		printf("[Exception] in main\n");
	}

	_vmem_release();
	return rv;
}

void LoadSettings()
{
	settings.dynarec.Enable=1; //gli cfgLoadInt(_T"nullDC",_T"Dynarec.Enabled",1)!=0;
	settings.dynarec.CPpass=0; //gli cfgLoadInt(_T"nullDC",_T"Dynarec.DoConstantPropagation",1)!=0;
	settings.dynarec.Safe=cfgLoadInt(_T"nullDC",_T"Dynarec.SafeMode",1)!=0;
	settings.dynarec.UnderclockFpu=cfgLoadInt(_T"nullDC",_T"Dynarec.UnderclockFpu",0)!=0;
	
	settings.dreamcast.cable=cfgLoadInt(_T"nullDC",_T"Dreamcast.Cable",3);
	settings.dreamcast.RTC=cfgLoadInt(_T"nullDC",_T"Dreamcast.RTC",GetRTC_now());

	settings.dreamcast.region=1; //gli USA cfgLoadInt(_T"nullDC",_T"Dreamcast.Region",3);
	settings.dreamcast.broadcast=cfgLoadInt(_T"nullDC",_T"Dreamcast.Broadcast",4);

	settings.emulator.AutoStart=1; //gli cfgLoadInt(_T"nullDC",_T"Emulator.AutoStart",0)!=0;
	settings.emulator.NoConsole=cfgLoadInt(_T"nullDC",_T"Emulator.NoConsole",0)!=0;

	//make sure values are valid
	settings.dreamcast.cable=min(max(settings.dreamcast.cable,0),3);
	settings.dreamcast.region=min(max(settings.dreamcast.region,0),3);
	settings.dreamcast.broadcast=min(max(settings.dreamcast.broadcast,0),4);
}
void SaveSettings()
{
	cfgSaveInt(_T"nullDC",_T"Dynarec.Enabled",settings.dynarec.Enable);
	cfgSaveInt(_T"nullDC",_T"Dynarec.DoConstantPropagation",settings.dynarec.CPpass);
	cfgSaveInt(_T"nullDC",_T"Dynarec.SafeMode",settings.dynarec.Safe);
	cfgSaveInt(_T"nullDC",_T"Dynarec.UnderclockFpu",settings.dynarec.UnderclockFpu);
	cfgSaveInt(_T"nullDC",_T"Dreamcast.Cable",settings.dreamcast.cable);
	cfgSaveInt(_T"nullDC",_T"Dreamcast.RTC",settings.dreamcast.RTC);
	cfgSaveInt(_T"nullDC",_T"Dreamcast.Region",settings.dreamcast.region);
	cfgSaveInt(_T"nullDC",_T"Dreamcast.Broadcast",settings.dreamcast.broadcast);
	cfgSaveInt(_T"nullDC",_T"Emulator.AutoStart",settings.emulator.AutoStart);
	cfgSaveInt(_T"nullDC",_T"Emulator.NoConsole",settings.emulator.NoConsole);
}

bool PendingSerialData()
{
	return false;
}

void WriteSerial(u8 data)
{
}

s32 ReadSerial()
{
	return -1;
}
