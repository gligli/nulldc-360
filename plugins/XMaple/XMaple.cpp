#define _WIN32_WINNT 0x500
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "XMaple.h"

#include "FT0.h"
#include "FT4.h"
#include "FT8.h"

//////////////////////////////////////////////////////////////////////////
// TODO something
emu_info host;
u32 deviceMenu;
u32 devicemenuItem[4];

xmaple_settings maplesettings;

void mapleUpdateMenu(u32 parentmenu, maple_device_instance* inst, bool override);

/*
// win32 dlls gotta have it...
HMODULE hModule;
HINSTANCE hInstance;
BOOL APIENTRY DllMain(HMODULE hModule,
					  DWORD  ul_reason_for_call,
					  LPVOID lpReserved)
{
	::hModule = hModule;
	hInstance = (HINSTANCE)hModule;
	return TRUE;
}
*/


void mapleLoadConfig()
{
	maplesettings.Controller.Deadzone = host.ConfigLoadInt("Xmaple", "Controller.DeadZone", 25);

	maplesettings.PuruPuru.UseRealFreq = host.ConfigLoadInt("Xmaple", "PuruPuru.UseRealFrequency", 1) == 1 ? true:false;
	maplesettings.PuruPuru.Length = host.ConfigLoadInt("Xmaple", "PuruPuru.Length", 175);
	maplesettings.PuruPuru.Intensity = host.ConfigLoadInt("Xmaple", "PuruPuru.Intensity", 100);
	
	// Just to be sure this ain't negative.
	maplesettings.PuruPuru.Intensity = abs(maplesettings.PuruPuru.Intensity);
	maplesettings.PuruPuru.Length = abs(maplesettings.PuruPuru.Length);
}

void mapleSaveConfig()
{	
	host.ConfigSaveInt("Xmaple", "PuruPuru.UseRealFrequency", maplesettings.PuruPuru.UseRealFreq);
}

//Called when plugin is loaded by the emu, the param has some handy functions so i make a copy of it ;)
s32 FASTCALL mapleLoad(emu_info* emu)
{
	memcpy(&host, emu, sizeof(host));

	mapleLoadConfig();

	return rv_ok;
}

//called when plugin is unloaded by emu(only if Load was called)
void FASTCALL mapleUnload()
{
}

void EXPORT_CALL MaxiCallback(u32 id, void* w, void* p)
{
	// Same setting for EVERY PuruPuru Pack, but doesn't crash. :)
	// TODO: Make it per subdevice.

	if (maplesettings.PuruPuru.UseRealFreq)
		maplesettings.PuruPuru.UseRealFreq = 0;
	else
		maplesettings.PuruPuru.UseRealFreq = 1;

	host.SetMenuItemStyle(id,maplesettings.PuruPuru.UseRealFreq?MIS_Checked:0,MIS_Checked);

	mapleSaveConfig();
}

void EXPORT_CALL mapleAboutBoxCallback(u32 id, void* w, void* p)
{
/*	MessageBox(
		(HWND)w,
		"XMaple input plugin for nullDC\n"
		"By shuffle2\n\n",
		"About XMaple",
		MB_OK);*/
}

//MUST be EXPORT_CALL, because it is a callback for a menu ;)
//Called when the config menu is selected
//id = menu id (can be used to set the style with host.SetMenuItemStyle & co)
//w = window handle (HWND) that owns the menu
//p = user specified data
void EXPORT_CALL mapleConfigMainCallback(u32 id, void* w, void* p)
{
	maple_device_instance* inst = (maple_device_instance*)p;

	// Get the selection
	MenuItem mi;
	host.GetMenuItem(id, &mi, MIM_Text);
	u8 newXPad = atoi(mi.Text+8);
	((EmulatedDevices::MapleInterface*)inst->data)->SetXPad(newXPad); //"x360pad X"

	// Delete the menu
	for (int i = 0; i < 4; ++i)
		host.DeleteMenuItem(devicemenuItem[i]);

	// Recreate
	mapleUpdateMenu(deviceMenu, inst, true);

	// Make subdevice tag along
	for (int i = 0; i < 5; ++i)
		if (inst->subdevices[i].dma == EmulatedDevices::MapleInterface::ClassDMA)
			((EmulatedDevices::MapleInterface*)inst->subdevices[i].data)->SetXPad(newXPad);
}

void mapleUpdateMenu(u32 parentmenu, maple_device_instance* inst, bool override)
{
	// Get the port number
	u32 x = inst->port >> 6;

	// Pad selection parent menu
	if (!deviceMenu)
		deviceMenu = host.AddMenuItem(parentmenu, -1, "Device: NONE", 0, 0);
	// so default is to not overlap with other devices
	if (!override)
		((EmulatedDevices::MapleInterface*)inst->data)->SetXPad(5);
	// Add plugged pads
	for (u32 i = 0; i < 4; ++i)
	{
		if (XInput::IsConnected(i))
		{
			wchar temp[512];
			sprintf(temp, "x360pad %i", i);
			devicemenuItem[i] = host.AddMenuItem(deviceMenu, -1, temp, mapleConfigMainCallback, 0);
			//Set the user defined pointer for the menu to the device instance,
			// so we can tell for which port the menu was called ;)
			MenuItem mi;
			mi.PUser = inst;
			mi.Style = MIS_Radiocheck;
			// default device
			if (!override && x == i)
			{
				mi.Style |= MIS_Checked;
				((EmulatedDevices::MapleInterface*)inst->data)->SetXPad(i);
				//update parent menu text
				wchar menu[512];
				sprintf(menu, "Device: x360pad %i", i);
				MenuItem parent;
				parent.Text = menu;
				host.SetMenuItem(deviceMenu, &parent, MIM_Text);
			}

			// user selection
			if (override && i == ((EmulatedDevices::MapleInterface*)inst->data)->GetXPad())
			{
				mi.Style |= MIS_Checked;
				//update parent menu text
				wchar menu[512];
				sprintf(menu, "Device: x360pad %i", i);
				MenuItem parent;
				parent.Text = menu;
				host.SetMenuItem(deviceMenu, &parent, MIM_Text);
			}
			host.SetMenuItem(devicemenuItem[i], &mi, MIM_PUser | MIM_Style);
		}
	}
}

//Called to create a main device, like joystick, lightgun and such
///inst : instance info
//inst->port is the port (in the native maple format) of the device
//inst->dma must be filled 
//inst->data can be filled (its what you get as data parameter on all the maple callbacks ;p)
//rest of it can be ignored (unless you want to do some ugly shit ;p)
///id : maple device id
//Its the index of the device on the Maple device list that the dcGetInterface gives to the emu
///flags : creation flags, ignore it for now (MDCF_HOTPLUG if the plugin is loaded after the emu startup)
///rootmenu : Root menu for the device
s32 FASTCALL mapleCreateMain(maple_device_instance* inst, u32 id, u32 flags, u32 rootmenu)
{
	
	// Only FT0 here for the time being...so w/e
	//pass the interface pointer using the device_instance field
	inst->data = (EmulatedDevices::MapleInterface*)(EmulatedDevices::CreateFT0(id, inst));
	if (inst->data == NULL)
	{
		printf("XMAPLE: BAD DEVICE ID\n");
		return rv_error;
	}
	inst->dma = EmulatedDevices::MapleInterface::ClassDMA;

	// Add type name
	wchar name[512];
	sprintf(name, deviceNames[((EmulatedDevices::MapleInterface*)inst->data)->GetID()]);
	host.AddMenuItem(rootmenu, -1, name, 0, 0);

	// About...
	host.AddMenuItem(rootmenu, -1, "About...", mapleAboutBoxCallback, 0);

	//gli UpdateMenu(rootmenu, inst, false);

	//All done well !
	return rv_ok;
}

//Create subdevice, same params as CreateMain
s32 FASTCALL mapleCreateSub(maple_subdevice_instance* inst, u32 id, u32 flags, u32 rootmenu)
{
	printf("id %d\n",id);
	//pass the interface pointer using the device_instance field
	switch (id)
	{
	case ID_PURUPURUPACK:
		{
		inst->data = (EmulatedDevices::MapleInterface*)(EmulatedDevices::CreateFT8(id, inst));
		
		// Frequency menuitem 
		
		host.AddMenuItem(rootmenu, -1, "Real Frequency Scaling", MaxiCallback, maplesettings.PuruPuru.UseRealFreq);
		
		}
		break;

	case ID_MIC:
	case ID_DREAMEYEMIC:
		inst->data = (EmulatedDevices::MapleInterface*)(EmulatedDevices::CreateFT4(id, inst));
		break;
	}

	if (inst->data == NULL)
	{
		printf("XMAPLE: BAD SUBDEVICE ID\n");
		return rv_error;
	}
	inst->dma = EmulatedDevices::MapleInterface::ClassDMA;

	// Add type name
	wchar name[512];
	sprintf(name, deviceNames[((EmulatedDevices::MapleInterface*)inst->data)->GetID()]);
	host.AddMenuItem(rootmenu, -1, name, 0, 0);

	return rv_ok;
}

//Called when emulation is started.
//data   : the inst->data pointer as filled by the Create* functions
//id     : device index on the dcGetInterface
//params : nothing useful (just a placeholder)
//NOTE: Hot plugged devices can miss this call if the emulation is started
// before the device is hotplugged.This is a bug on the emu %) ;p
s32 FASTCALL mapleInit(void* data, u32 id, maple_init_params* params)
{	
	
	//Emulation was started ! OMFG ! GET THE GUNSHIPS READYYYY
	//Err, init dinput or smth, i do all my init work on the Create() part anyway ..			
	switch (id)
	{
	case ID_PURUPURUPACK:
		((EmulatedDevices::FT8*)data)->StartVibThread();
		break;
	}

	return rv_ok;
}

//Called when emulation is terminated
//data   : the inst->data pointer as filled by the Create* functions
//id     : device index on the dcGetInterface
//NOTE 
// Called only if Init() was called ;)
void FASTCALL mapleTerm(void* data, u32 id)
{
	
	//kill whatever you did on Init()
	// TODO figure out why this only gets called for main devices
	switch (id)
	{
	case ID_PURUPURUPACK:
		((EmulatedDevices::FT8*)data)->StopVibThread();
		break;
	}
}

//Destroy the device
//data   : the inst->data pointer as filled by the Create* functions
//id     : device index on the dcGetInterface
void FASTCALL mapleDestroy(void* data,u32 id)
{
	//Free any memory allocated (if any)
}

#define AddMapleDevice(name, flags)	\
	strcpy(info->maple.devices[id].Name, name" [XInput] (" __DATE__ ")"); \
	info->maple.devices[id].Type	= MDT_Main;	\
	info->maple.devices[id].Flags	= flags;	\
	id++;

#define AddMapleSubDevice(name, flags) \
	strcpy(info->maple.devices[id].Name, name" [XInput] (" __DATE__ ")"); \
	info->maple.devices[id].Type	= MDT_Sub;	\
	info->maple.devices[id].Flags	= flags;	\
	id++;

//Give a list of the devices to the emu
//_T hijack so i can get unicode __DATE__ ;/

void EXPORT_CALL mapleGetInterface(plugin_interface* info)
{		
	//Fill in the common (for all plugin types) info
	info->InterfaceVersion			= PLUGIN_I_F_VERSION;
	info->common.InterfaceVersion	= MAPLE_PLUGIN_I_F_VERSION;
	info->common.Load	= mapleLoad;
	info->common.Unload	= mapleUnload;
	info->common.Type	= Plugin_Maple;

	//wcscpy : unicode ;)
	strcpy(info->common.Name, "XInput for nullDC by shuffle2 [" __DATE__ "]");

	//Fill in the maple info
	info->maple.CreateMain	= mapleCreateMain;
	info->maple.CreateSub	= mapleCreateSub;
	info->maple.Init		= mapleInit;
	info->maple.Term		= mapleTerm;
	info->maple.Destroy		= mapleDestroy;

	// Start at 0...
	u32 id = ID_STDCONTROLLER;

#ifndef BUILD_NAOMI
	AddMapleDevice("Controller", MDTF_Sub0|MDTF_Sub1|MDTF_Hotplug);
	AddMapleDevice("Twinstick", MDTF_Sub0|MDTF_Hotplug);
	AddMapleDevice("Arcade Stick", MDTF_Sub0|MDTF_Hotplug);

	AddMapleSubDevice("Puru-Puru Pak", MDTF_Hotplug);
	AddMapleSubDevice("Mic", MDTF_Hotplug);
	AddMapleSubDevice("Dreameye Mic", MDTF_Hotplug);
#endif
	//EOL marker
	info->maple.devices[id].Type = MDT_EndOfList;
}