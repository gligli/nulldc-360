// nullAICA.cpp : Defines the entry point for the DLL application.
//

#include "nullAICA.h"
#include "sgc_if.h"
#include "aica.h"
#include "aica_mem.h"
#include "audiostream.h"

aica_setts aica_settings;
aica_init_params aica_params;
extern emu_info emu;

s32 FASTCALL PluginLoadAica(emu_info* param)
{
	LoadSettingsAica();
    return rv_ok;
}

void FASTCALL PluginUnloadAica()
{
	SaveSettingsAica();
}

//called when plugin is used by emu (you should do first time init here)
s32 FASTCALL InitAica(aica_init_params* initp)
{
	memcpy(&aica_params,initp,sizeof(aica_params));

	init_mem();
	AICA_Init();
	InitAudio();

	return rv_ok;
}

//called when plugin is unloaded by emu , olny if dcInit is called (eg , not called to enumerate plugins)
void FASTCALL TermAica()
{
	TermAudio();
	AICA_Term();
	term_mem();
}

//It's suposed to reset anything 
void FASTCALL ResetAica(bool Manual)
{

}


//Give to the emu pointers for the PowerVR interface
EXPORT void EXPORT_CALL aicaGetInterface(plugin_interface* info)
{
	info->InterfaceVersion=PLUGIN_I_F_VERSION;
/*
	info->Init=dcInit;
	info->Term=dcTerm;
	info->Reset=dcReset;

	info->ThreadInit=dcThreadInit;
	info->ThreadTerm=dcThreadTerm;
	info->ShowConfig=cfgdlg;

	info->Type=PluginType::AICA;

	info->InterfaceVersion.full=AICA_PLUGIN_I_F_VERSION;

	info->ReadMem_aica_ram=ReadMem_ram;
	info->WriteMem_aica_ram=WriteMem_ram;
	info->ReadMem_aica_reg=ReadMem_reg;
	info->WriteMem_aica_reg=WriteMem_reg;
	info->UpdateAICA=UpdateSystem;
*/
#define c info->common
#define a info->aica

	strcpy(c.Name,"nullDC AICA [" __DATE__ "]");

	c.InterfaceVersion=AICA_PLUGIN_I_F_VERSION;
	c.Type=Plugin_AICA;

	c.Load=PluginLoadAica;
	c.Unload=PluginUnloadAica;

	a.Init=InitAica;
	a.Reset=ResetAica;
	a.Term=TermAica;

	a.Update=UpdateAICA;

	a.ReadMem_aica_reg=aica_ReadMem_reg;
	a.WriteMem_aica_reg=aica_WriteMem_reg;
}

int cfgGetIntAica(char* key,int def)
{
	return emu.ConfigLoadInt("nullAica",key,def);
}
void cfgSetIntAica(char* key,int def)
{
	emu.ConfigSaveInt("nullAica",key,def);
}

void LoadSettingsAica()
{
	//load default settings before init
	aica_settings.BufferSize=cfgGetIntAica("BufferSize",2048);
	aica_settings.LimitFPS=cfgGetIntAica("LimitFPS",1);
	aica_settings.HW_mixing=cfgGetIntAica("HW_mixing",0);
	aica_settings.SoundRenderer=cfgGetIntAica("SoundRenderer",1);
	aica_settings.GlobalFocus=cfgGetIntAica("GlobalFocus",1);
	aica_settings.BufferCount=cfgGetIntAica("BufferCount",1);
	aica_settings.CDDAMute=cfgGetIntAica("CDDAMute",0);
	aica_settings.GlobalMute=cfgGetIntAica("GlobalMute",0);
	aica_settings.DSPEnabled=cfgGetIntAica("DSPEnabled",0);		

	aica_settings.Volume = max(0,min(cfgGetIntAica("Volume",90),100));
}

void SaveSettingsAica()
{
	//load default settings before init
	cfgSetIntAica("BufferSize",aica_settings.BufferSize);
	cfgSetIntAica("LimitFPS",aica_settings.LimitFPS);
	cfgSetIntAica("HW_mixing",aica_settings.HW_mixing);
	cfgSetIntAica("SoundRenderer",aica_settings.SoundRenderer);
	cfgSetIntAica("GlobalFocus",aica_settings.GlobalFocus);
	cfgSetIntAica("BufferCount",aica_settings.BufferCount);
	cfgSetIntAica("CDDAMute",aica_settings.CDDAMute);
	cfgSetIntAica("GlobalMute",aica_settings.GlobalMute);
	cfgSetIntAica("DSPEnabled",aica_settings.DSPEnabled);
	
	cfgSetIntAica("Volume",max(0,min(aica_settings.Volume,100)));
}
