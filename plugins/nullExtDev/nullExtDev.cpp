// nullAICA.cpp : Defines the entry point for the DLL application.
//

#include "nullExtDev.h"
#include "modem.h"
#include "lan.h"
#include "bba.h"
emu_info extemu;
ext_device_init_params extparams;
__extsettings extsettings;

void extLoadSettings()
{
	extsettings.mode=max(0,min(extemu.ConfigLoadInt("nullExtDev","mode",0),3));
	extsettings.adapter=extemu.ConfigLoadInt("nullExtDev","adapter",0);
}
void extSaveSettings()
{
	extemu.ConfigSaveInt("nullExtDev","mode",extsettings.mode);
	extemu.ConfigSaveInt("nullExtDev","adapter",extsettings.adapter);
}

//u32 mode=1;//0=null, 1=modem,2=lan,3=bba
//"Emulates" the "nothing atached to ext.dev" :p

//006* , on area0
u32 FASTCALL ReadMem_A0_006(u32 addr,u32 size)
{
	switch(extsettings.mode)
	{
	case 0:
		return 0;
	case 1:
		return ModemReadMem_A0_006(addr,size);
	case 2:
		return LanReadMem_A0_006(addr,size);
	default:
		//printf("Read from modem area on mode %d, addr[%d] 0x%08X\n",extsettings.mode,size,addr);
		return 0;
	}
	
}
void FASTCALL WriteMem_A0_006(u32 addr,u32 data,u32 size)
{
	switch(extsettings.mode)
	{
	case 0:
		return;
	case 1:
		ModemWriteMem_A0_006(addr,data,size);
		return;
	case 2:
		LanWriteMem_A0_006(addr,data,size);
		return;
	default:
		//printf("Write to modem area on mode %d, addr[%d] 0x%08X=0x%08X\n",extsettings.mode,size,addr,data);
		return;
	}
}
//010* , on area0
/*u8 tram[0x10000];*/
u32 FASTCALL ReadMem_A0_010(u32 addr,u32 size)
{
	if (extsettings.mode==3)
		return bba_ReadMem(addr,size);
	else
	{
		/*
		u32 data=0;
		printf("Read[%d] from 0x%08X\n",size*8,addr);
		if (addr==0x01000000)
			return 0;
		memcpy(&data,&tram[addr&0xFFFF],size);
		//if (*(u16*)&tram[addr&0xFFFF]==0x100)
			*(u16*)&tram[addr&0xFFFF]^=0x100;
		if (addr==0x01000100)
			data^=0x100;
		return data;*/
		return 0;
	}
}
void FASTCALL WriteMem_A0_010(u32 addr,u32 data,u32 size)
{
	if (extsettings.mode==3)
		bba_WriteMem(addr,data,size);
	else
	{
		/*
		printf("Write[%d] to 0x%08X = 0x%08X\n",size*8,addr,data);
		memcpy(&tram[addr&0xFFFF],&data,size);
		*/
		return;
	}
		
}
//Area 5
u32 FASTCALL ReadMem_A5(u32 addr,u32 size)
{
	return 0;
}
void FASTCALL WriteMem_A5(u32 addr,u32 data,u32 size)
{
}
u32 update_timer;
void (*update_callback) ();
void SetUpdateCallback(void (*callback) (),u32 ms)
{
	verify(update_callback==0 || update_callback==callback);
	if (update_callback)
		return;
	update_callback=callback;
	update_timer=ms*DCclock/1000;
}
void ExpireUpdate(bool v) 
{
	void (*t)()=update_callback;
	update_callback=0;
	if (v)
		t();
}
//~ called every 1.5k cycles
void FASTCALL extUpdate(u32 cycles)
{
	if (update_callback)
	{
		if (update_timer<cycles)
		{
			void (*t)()=update_callback;
			update_callback=0;
			t();
		}
		update_timer-=cycles;
	}
	if(extsettings.mode==3)
		bba_periodical();
}

/*

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
					 )
{
    return TRUE;
}

*/
void EXPORT_CALL exthandle_about(u32 id,void* window,void* p)
{
//	MessageBox((HWND)window,"Made by the nullDC team\nThis plugin partialy emulates the modem for now.\n\nNow , go back before its too late ...","nullExtDev plugin",MB_OK | MB_ICONINFORMATION);
}
u32 mids[4];
void nide_set_selected()
{
	for (int i=0;i<4;i++)
	{
		if (i==extsettings.mode)
			extemu.SetMenuItemStyle(mids[i],MIS_Checked,MIS_Checked);
		else
			extemu.SetMenuItemStyle(mids[i],0,MIS_Checked);
	}
}
template<u32 v>
void EXPORT_CALL handle_mode(u32 id,void* window,void* p)
{
	extsettings.mode=v;
	nide_set_selected();
	extSaveSettings();
}

//called when plugin is used by emu (you should do first time init here)
s32 FASTCALL extLoad(emu_info* param)
{
	memcpy(&extemu,param,sizeof(extemu));

	extLoadSettings();

	mids[0]=extemu.AddMenuItem(extemu.RootMenu,-1,"None",handle_mode<0>,0);
	mids[1]=extemu.AddMenuItem(extemu.RootMenu,-1,"Modem",handle_mode<1>,0);
	mids[2]=extemu.AddMenuItem(extemu.RootMenu,-1,"Lan Adapter",handle_mode<2>,0);
	mids[3]=extemu.AddMenuItem(extemu.RootMenu,-1,"BBA",handle_mode<3>,0);

	nide_set_selected();
	
	extemu.AddMenuItem(extemu.RootMenu,-1,0,0,extsettings.mode==0);
	
	extemu.AddMenuItem(extemu.RootMenu,-1,"About",exthandle_about,0);
	return rv_ok;
}

//called when plugin is unloaded by emu , olny if dcInit is called (eg , not called to enumerate plugins)
void FASTCALL extUnload()
{

}

//It's suposed to reset anything 
void FASTCALL extedReset(bool Manual)
{

}

//called when entering sh4 thread , from the new thread context (for any thread speciacific init)
s32 FASTCALL extedInit(ext_device_init_params* p)
{
	extparams=*p;
	if (extsettings.mode!=0)
	{
/*		int nd=pcap_io_get_dev_num();
		printf("%d adapters\n",nd);
		for(int i=0;i<nd;i++)
		{
			printf("%d : %s :",i,pcap_io_get_dev_name(i));
			printf("\t%s\n",pcap_io_get_dev_desc(i));
		}

		printf("using :%d : %s :",extsettings.adapter,pcap_io_get_dev_name(extsettings.adapter));
		printf("\t%s\n",pcap_io_get_dev_desc(extsettings.adapter));
		pcap_io_init(pcap_io_get_dev_name(extsettings.adapter));*/
	}
	if (extsettings.mode==3)
		bba_init();

	return rv_ok;
}

//called when exiting from sh4 thread , from the new thread context (for any thread speciacific de init) :P
void FASTCALL extedTerm()
{
/*	if (extsettings.mode!=0)
		pcap_io_close();*/
}


//Give to the emu pointers for the PowerVR interface
bool EXPORT_CALL extGetInterface(plugin_interface* info)
{
	#define c info->common 
	#define ed info->ext_dev

	info->InterfaceVersion=PLUGIN_I_F_VERSION;

	c.Type=Plugin_ExtDevice;
	c.InterfaceVersion=EXTDEVICE_PLUGIN_I_F_VERSION;

	strcpy(c.Name,"nullExtDev (" __DATE__ ")");
	
	c.Load=extLoad;
	c.Unload=extUnload;
	

	ed.Init=extedInit;
	ed.Reset=extedReset;
	ed.Term=extedTerm;

	ed.ReadMem_A0_006=ReadMem_A0_006;
	ed.WriteMem_A0_006=WriteMem_A0_006;
	
	ed.ReadMem_A0_010=ReadMem_A0_010;
	ed.WriteMem_A0_010=WriteMem_A0_010;

	ed.ReadMem_A5=ReadMem_A5;
	ed.WriteMem_A5=WriteMem_A5;

	ed.UpdateExtDevice=extUpdate;

	return true;
}