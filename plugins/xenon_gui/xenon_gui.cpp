#include "nullDC/plugins/plugin_header.h"
#include "nullDC/plugins/gui_plugin_header.h"

#ifdef USE_GUI
#include <input/input.h>
#include <usb/usbmain.h>

// gui messages
void ErrorPrompt(const char *msg);
void InfoPrompt(const char *msg);
#endif

#include <cstring>

wchar emu_name[128];
gui_emu_info gui_emu;

#define FAKE_MENU_ID 0

s32 EXPORT_CALL guiLoad(gui_emu_info* emui)
{
	memcpy(&gui_emu,emui,sizeof(gui_emu));
	
	gui_emu.ConfigLoadStr("emu","FullName",emu_name,"");
	return rv_ok;
}
void EXPORT_CALL GetMenuIDs(MenuIDList* mid)
{
	mid->PowerVR=FAKE_MENU_ID;
	mid->GDRom=FAKE_MENU_ID;
	mid->Aica=FAKE_MENU_ID;
	mid->Arm=FAKE_MENU_ID;
	mid->Maple=FAKE_MENU_ID;
	
	for (int i=0;i<4;i++) for (int j=0;j<6;j++)
		mid->Maple_port[i][j] =FAKE_MENU_ID;

	mid->ExtDev=FAKE_MENU_ID;
	mid->Debug=FAKE_MENU_ID;
}
void EXPORT_CALL guiUnload()
{
}


void EXPORT_CALL Mainloop()
{
	TR
}

int EXPORT_CALL guiMsgBox(wchar* text,int type)
{
	return 0;
}

u32 EXPORT_CALL AddMenuItem(u32 parent,s32 pos,const wchar* text,MenuItemSelectedFP* handler ,u32 checked)
{
	return 1;
}

void EXPORT_CALL SetMenuItemStyle(u32 id,u32 style,u32 mask)
{
}

void EXPORT_CALL GetMenuItem(u32 id,MenuItem* info,u32 mask)
{
}

void EXPORT_CALL SetMenuItem(u32 id,MenuItem* info,u32 mask)
{
}

void EXPORT_CALL DeleteMenuItem(u32 id)
{
}

void EXPORT_CALL DeleteAllMenuItemChilds(u32 id)
{
}

void* EXPORT_CALL GetRTWH()
{
	return (void*)42;
}

bool EXPORT_CALL pSelectPluginsGui()
{
	return false;
}

void EXPORT_CALL guiEventHandler(u32 nid,void* p)
{
}


void EXPORT_CALL guiGetInterface(gui_plugin_info* info)
{
	info->InterfaceVersion=GuiPluginInterfaceVersion;
	strcpy(info->Name,"Default Gui");
	
	info->Load=guiLoad;
	info->Unload=guiUnload;
	info->Mainloop=Mainloop;

	info->MsgBox=guiMsgBox;
	info->AddMenuItem=AddMenuItem;
	info->SetMenuItemStyle=SetMenuItemStyle;
	info->SetMenuItem=SetMenuItem;
	info->GetMenuItem=GetMenuItem;
	info->DeleteMenuItem=DeleteMenuItem;
	info->GetMenuIDs=GetMenuIDs;
	info->GetRenderTarget=GetRTWH;
	info->SelectPluginsGui=pSelectPluginsGui;
	info->DeleteAllMenuItemChilds=DeleteAllMenuItemChilds;

	info->EventHandler=guiEventHandler;
}
