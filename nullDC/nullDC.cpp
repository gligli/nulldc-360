// nullDC.cpp : Makes magic cookies
//

#include <debug.h>
#include <xenos/xenos.h>
#include <input/input.h>
#include <console/console.h>
#include <console/telnet_console.h>
#include <diskio/ata.h>
#include <usb/usbmain.h>
#include <time/time.h>
#include <ppc/timebase.h>
#include <xenon_soc/xenon_power.h>
#include <xenon_sound/sound.h>
#include <libfat/fat.h>

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
#include "../plugins/ImgReader/ImgReader.h"


#define max(a,b) (((a)>(b))?(a):(b))
#define min(a,b) (((a)<(b))?(a):(b))

__settings settings;

int nulldc_run(char * filename)
{
	if(filename)
    {
        strcpy(irsettings.DefaultImage, filename);
        printf("Filename : %s\n", filename);
    }
	
    Start_DC();

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

	dlog("PowerVR plugins :\n");
	for (u32 i=0;i<pvr->size();i++)
	{
		printf("*\tFound %s\n" ,(*pvr)[i].Name);
	}

	dlog("\nGDRom plugins :\n");
	for (u32 i=0;i<gdrom->size();i++)
	{
		printf("*\tFound %s\n" ,(*gdrom)[i].Name);
	}

	
	dlog("\nAica plugins :\n");
	for (u32 i=0;i<aica->size();i++)
	{
		printf("*\tFound %s\n" ,(*aica)[i].Name);
	}

	dlog("\nArm plugins :\n");
	for (u32 i=0;i<arm->size();i++)
	{
		printf("*\tFound %s\n" ,(*arm)[i].Name);
	}

	dlog("\nMaple plugins :\n");
	for (u32 i=0;i<maple->size();i++)
	{
		printf("*\tFound %s\n" ,(*maple)[i].Name);
	}
	dlog("\nExtDevice plugins :\n");
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

#ifdef USE_GUI

int nulldc_init()
{
	static int already_inited = 0;
	int argc = 1;
	int rv = 0;
	char* argv[] = {"uda:/xenon.elf"};

	if (already_inited == 0) {
		
		_vmem_reserve();
		
		if (ParseCommandLine(argc, argv)) {
			dlog("\n\n(Exiting due to command line, without starting nullDC)\n");
			return -1;
		}

		if (!cfgOpen()) {
			msgboxf(("Unable to open config file"), MBX_ICONERROR);
			return -4;
		}
		LoadSettings();

        if (!CreateGUI())
        {
            msgboxf(("Creating GUI failed\n"),MBX_ICONERROR);
            return -1;
        }
		
        char* temp_path = GetEmuPath(("data/"));

		bool lrf = LoadRomFiles(temp_path);

		free(temp_path);

		if (!lrf) {
			rv = -3;
			TR
			DestroyGUI();
			SaveSettings();	
		}

		EnumPlugins();

		while (!plugins_Load()) {
			if (!plugins_Select()) {
				TR
				msgboxf("Unable to load plugins -- exiting\n", MBX_ICONERROR);
				rv = -2;
				DestroyGUI();
				SaveSettings();	
			}
		}

	}
	already_inited = 1;
	
	return rv;
}

#else


int main___(int argc,char* argv[])
{
	if(ParseCommandLine(argc,argv))
	{
		dlog("\n\n(Exiting due to command line, without starting nullDC)\n");
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

	char* temp_path=GetEmuPath(_T("data/"));

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
			msgboxf("Unable to load plugins -- exiting\n",MBX_ICONERROR);
			rv = -2;
			goto cleanup;
		}
	}
	
	console_close();
	
	rv = nulldc_run(NULL);
	
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
    
    xenon_sound_init();

    xenon_make_it_faster(XENON_SPEED_FULL);
	
/*	network_init();
	network_print_config();
	
	telnet_console_init();*/

	usb_init();
	usb_do_poll();
	
	xenon_ata_init();
    
    fatInitDefault();
    
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
#endif

#ifdef _MUDFLAP
extern "C" 
{
int __wrap_main(){
    mf_check((void*)0x8b0b9c00,1024,0,1,0);
    return main();
}
}
#endif

void LoadSettings()
{
	settings.dynarec.Enable=1; //gli cfgLoadInt(_T"nullDC",_T"Dynarec.Enabled",1)!=0;
	settings.dynarec.CPpass=1; //gli cfgLoadInt(_T"nullDC",_T"Dynarec.DoConstantPropagation",1)!=0;
	settings.dynarec.Safe=0; //gli cfgLoadInt(_T"nullDC",_T"Dynarec.SafeMode",1)!=0;
	settings.dynarec.UnderclockFpu=1; //gli cfgLoadInt(_T"nullDC",_T"Dynarec.UnderclockFpu",0)!=0;
	
	settings.dreamcast.cable=0; //gli cfgLoadInt(_T"nullDC",_T"Dreamcast.Cable",3);
	settings.dreamcast.RTC=cfgLoadInt("nullDC","Dreamcast.RTC",GetRTC_now());

	settings.dreamcast.region=1; //gli USA cfgLoadInt(_T"nullDC",_T"Dreamcast.Region",3);
	settings.dreamcast.broadcast=4; //gli cfgLoadInt(_T"nullDC",_T"Dreamcast.Broadcast",4);

	settings.emulator.AutoStart=0; //gli cfgLoadInt(_T"nullDC",_T"Emulator.AutoStart",0)!=0;
	settings.emulator.NoConsole=cfgLoadInt("nullDC","Emulator.NoConsole",0)!=0;

	//make sure values are valid
	settings.dreamcast.cable=min(max(settings.dreamcast.cable,0),3);
	settings.dreamcast.region=min(max(settings.dreamcast.region,0),3);
	settings.dreamcast.broadcast=min(max(settings.dreamcast.broadcast,0),4);
}
void SaveSettings()
{
	cfgSaveInt("nullDC","Dynarec.Enabled",settings.dynarec.Enable);
	cfgSaveInt("nullDC","Dynarec.DoConstantPropagation",settings.dynarec.CPpass);
	cfgSaveInt("nullDC","Dynarec.SafeMode",settings.dynarec.Safe);
	cfgSaveInt("nullDC","Dynarec.UnderclockFpu",settings.dynarec.UnderclockFpu);
	cfgSaveInt("nullDC","Dreamcast.Cable",settings.dreamcast.cable);
	cfgSaveInt("nullDC","Dreamcast.RTC",settings.dreamcast.RTC);
	cfgSaveInt("nullDC","Dreamcast.Region",settings.dreamcast.region);
	cfgSaveInt("nullDC","Dreamcast.Broadcast",settings.dreamcast.broadcast);
	cfgSaveInt("nullDC","Emulator.AutoStart",settings.emulator.AutoStart);
	cfgSaveInt("nullDC","Emulator.NoConsole",settings.emulator.NoConsole);
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
