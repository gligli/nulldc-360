#include "fake_pluginsystem.h"

void EXPORT_CALL guiGetInterface(gui_plugin_info* info);
void EXPORT_CALL drkPvrGetInterface(plugin_interface* info);
void EXPORT_CALL irGetInterface(plugin_interface* info);
void EXPORT_CALL aicaGetInterface(plugin_interface* info);
void EXPORT_CALL armGetInterface(plugin_interface* info);
bool EXPORT_CALL extGetInterface(plugin_interface* info);
void EXPORT_CALL mapleGetInterface(plugin_interface* info);

#define MAX_PROCS_PER_DLL 64

struct psProc_s{
	char * name;
	void * address;
};

struct psDll_s{
	char * name;
	struct psProc_s procs[MAX_PROCS_PER_DLL];
};

struct psDll_s dlls[]={
	{
		"nullDC_GUI_Win32.dll",
		{
			{"ndcGetInterface",(void *)guiGetInterface},
		}
	},
	{
		"drkPvr_Win32.dll",
		{
			{"dcGetInterface",(void *)drkPvrGetInterface},
		}
	},
	{
		"ImgReader_Win32.dll",
		{
			{"dcGetInterface",(void *)irGetInterface},
		}
	},
	{
		"nullAica_Win32.dll",
		{
			{"dcGetInterface",(void *)aicaGetInterface},
		}
	},
	{
		"vbaARM_Win32.dll",
		{
			{"dcGetInterface",(void *)armGetInterface},
		}
	},
	{
		"nullExtDev_Win32.dll",
		{
			{"dcGetInterface",(void *)extGetInterface},
		}
	},
	{
		"drkMapleDevices_Win32.dll",
		{
			{"dcGetInterface",(void *)mapleGetInterface},
		}
	},
	
	// list termination
	{NULL,{}}
};

int GetLastError()
{
	return 0;
}


DLLHANDLE LoadLibrary(char * dllname)
{
//	printf("LoadLibrary %s\n",dllname);
	int i=0;
	while(dlls[i].name){
		if (strstr(dllname,dlls[i].name)) return &dlls[i];
		++i;
	}
	
	return NULL;
}

int FreeLibrary(DLLHANDLE lib)
{
	return TRUE;
}

void * GetProcAddress(DLLHANDLE lib,char * procname)
{
	if(!lib) return NULL;
			
	int i=0;
	struct psDll_s * dll = (struct psDll_s *) lib;
	
//	printf("GetProcAddress %s %s\n",dll->name,procname);

	while(dll->procs[i].name){
		if (!strcmp(procname,dll->procs[i].name)) return dll->procs[i].address;
		++i;
	}
	
	return NULL;
}

