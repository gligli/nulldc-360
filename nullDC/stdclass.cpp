#include "types.h"
#include <string.h>
#include "dc/mem/_vmem.h"
#include "plugins/plugin_manager.h"
#include <stdarg.h>
#include "fake_pluginsystem.h"
#include <emitter/emitter.h>

extern "C"{
#include <ppc/cache.h>
#include <ppc/vm.h>
}

//comonly used classes across the project

//bah :P since it's template needs to be on .h pfftt
//anyhow , this is needed here ;P
u32 Array_T_id_count=0;

u32 fastrand_seed=0xDEADCAFE;

u32 fastrand()
{
	fastrand_seed=(fastrand_seed>>9)^(fastrand_seed<<11)^(fastrand_seed>>24);//^1 is there 
	return fastrand_seed++;//if it got 0 , take good care of it :)
}

//Misc function to get relative source directory for log's
char temp[1000];
char* GetNullDCSoruceFileName(char* full)
{
	size_t len = strlen(full);
	while(len>18)
	{
		if (full[len]=='/')
		{
			memcpy(&temp[0],&full[len-14],15*sizeof(char));
			temp[15]=0;
			if (strcmp(&temp[0],"/nulldc/nulldc/")==0)
			{
				strcpy(temp,&full[len+1]);
				return temp;
			}
		}
		len--;
	}
	strcpy(temp,full);
	return &temp[0];
}

char* GetPathFromFileNameTemp(char* full)
{
	size_t len = strlen(full);
	while(len>2)
	{
		if (full[len]=='/')
		{
			memcpy(&temp[0],&full[0],(len+1)*sizeof(char));
			temp[len+1]=0;
			return temp;	
		}
		len--;
	}
	strcpy(temp,full);
	return &temp[0];
}

void GetPathFromFileName(char* path)
{
	strcpy(path,GetPathFromFileNameTemp(path));
}

void GetFileNameFromPath(char* path,char* outp)
{
	
	size_t i=strlen(path);
	
	while (i>0)
	{
		i--;
		if (path[i]=='/')
		{
			strcpy(outp,&path[i+1]);
			return;
		}
	}

	strcpy(outp,path);
}

#ifndef USE_GUI
char AppPath[1024] = "uda:/nulldc-360/";
void GetApplicationPath(char* path,u32 size)
{
	strcpy(path,AppPath);
}
#else
char AppPath[1024] = "nulldc-360/";
void GetApplicationPath(char* path,u32 size)
{
	strcpy(path,AppPath);
}
#endif

char* GetEmuPath(const char* subpath)
{
	char* temp=(char*)malloc(1024);
	GetApplicationPath(temp,1024);
	strcat(temp,subpath);
	return temp;
}

//Windoze Code implementation of commong classes from here and after ..

//Thread class
cThread::cThread(ThreadEntryFP* function,void* prm)
{
	Entry=function;
	param=prm;
//gli	hThread=CreateThread(NULL,NULL,(LPTHREAD_START_ROUTINE)function,prm,CREATE_SUSPENDED,NULL);
}
cThread::~cThread()
{
	//gota think of something !
}
	
void cThread::Start()
{
//gli	ResumeThread(hThread);
}
void cThread::Suspend()
{
//gli	SuspendThread(hThread);
}
void cThread::WaitToEnd(u32 msec)
{
//gli	WaitForSingleObject(hThread,msec);
}
//End thread class

//cResetEvent Calss
cResetEvent::cResetEvent(bool State,bool Auto)
{
/*gli		hEvent = CreateEvent( 
        NULL,             // default security attributes
		Auto?FALSE:TRUE,  // auto-reset event?
		State?TRUE:FALSE, // initial state is State
        NULL			  // unnamed object
        );*/
    auto_=Auto;
    state=State;
}
cResetEvent::~cResetEvent()
{
	//Destroy the event object ?
//gli	 CloseHandle(hEvent);
}
void cResetEvent::Set()//Signal
{
//gli	SetEvent(hEvent);
    state=true;
}
void cResetEvent::Reset()//reset
{
//gli	ResetEvent(hEvent);
    state=false;
}
void cResetEvent::Wait(u32 msec)//Wait for signal , then reset
{
//gli	WaitForSingleObject(hEvent,msec);
    Wait();
}
void cResetEvent::Wait()//Wait for signal , then reset
{
//gli	WaitForSingleObject(hEvent,(u32)-1);
//    while(!state);
    if (auto_) state=false;
}
//End AutoResetEvent


//Dll loader/unloader
//.ctor
cDllHandler::cDllHandler()
{
	lib=0;
}

//.dtor
cDllHandler::~cDllHandler()
{
	if (lib)
	{
		#ifdef DEBUG_DLL
		EMUWARN("cDllHandler::~cDllHandler() -> dll still loaded , unloading it..");
		#endif
		Unload();
	}
}

bool cDllHandler::Load(char* dll)
{
	lib=LoadLibrary(dll);
	if (lib==0)
	{
	#ifdef DEBUG_DLL
		EMUERROR2("void cDllHandler::Load(char* dll) -> dll %s could not be loaded",dll);
	#endif
	}

	return IsLoaded();
}

bool cDllHandler::IsLoaded()
{
	return lib!=0;
}

void cDllHandler::Unload()
{
	if (lib==0)
	{
		#ifdef DEBUG_DLL
		EMUWARN("void cDllHandler::Unload() -> dll is not loaded");
		#endif
	}
	else
	{
		u32 rv =FreeLibrary(lib);
		if (!rv)
		{
			dlog("FreeLibrary -- failed %d\n",GetLastError());
		}
		lib=0;
	}
}

void* cDllHandler::GetProcAddress(char* name)
{
	if (lib==0)
	{
		EMUERROR("void* cDllHandler::GetProcAddress(char* name) -> dll is not loaded");
		return 0;
	}
	else
	{
		void* rv = ::GetProcAddress(lib,name);

		if (rv==0)
		{
			//EMUERROR3("void* cDllHandler::GetProcAddress(char* name) :  Export named %s is not found on lib %p",name,lib);
		}

		return rv;
	}
}

//File Enumeration
void FindAllFiles(FileFoundCB* callback,char* dir,void* param)
{
/*gli	WIN32_FIND_DATA FindFileData;
	HANDLE hFind = INVALID_HANDLE_VALUE;
	char DirSpec[MAX_PATH + 1];  // directory specification
	DWORD dwError;

	wcsncpy (DirSpec, dir, wcslen(dir)+1);
	//strncat (DirSpec, "\\*", 3);

	hFind = FindFirstFile( DirSpec, &FindFileData);

	if (hFind == INVALID_HANDLE_VALUE) 
	{
		return;
	} 
	else 
	{

		if ((FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)==0)
		{
			callback(FindFileData.cFileName,param);
		}
u32 rv;
		while ( (rv=FindNextFile(hFind, &FindFileData)) != 0) 
		{ 
			if ((FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)==0)
			{
				callback(FindFileData.cFileName,param);
			}
		}
		dwError = GetLastError();
		FindClose(hFind);
		if (dwError != ERROR_NO_MORE_FILES) 
		{
			return ;
		}
	}
	return ;*/
}

void VArray2::LockRegion(u32 offset,u32 size)
{
	u32 addr=(u32)data+offset;
	
//    printf("LockRegion %p %d\n",addr,size);
    
	if (addr&0xffff || size&0xffff)
	{
		vm_set_user_mapping_flags(addr&~0xffff,(size+0xffff)&~0xffff,VM_WIMG_CACHED_READ_ONLY);
	}
	else
	{
		vm_set_user_mapping_flags(addr,size,VM_WIMG_CACHED_READ_ONLY);
	}
}
void VArray2::UnLockRegion(u32 offset,u32 size)
{
	u32 addr=(u32)data+offset;
	
//    printf("UnLockRegion %p %d\n",addr,size);

	if (addr&0xffff || size&0xffff)
	{
		vm_set_user_mapping_flags(addr&~0xffff,(size+0xffff)&~0xffff,VM_WIMG_CACHED);
	}
	else
	{
		vm_set_user_mapping_flags(addr,size,VM_WIMG_CACHED);
	}
}

#include "dc/sh4/rec_v1/compiledblock.h"
#include "dc/sh4/rec_v1/blockmanager.h"

void * roml_patch_for_reg_access(u32 addr, bool jump_only);

bool VramLockedWrite(u8* address);
bool RamLockedWrite(u8* address,u32* sp);
void * ExeptionHandler(int pir,void * srr0,void * dar,int write)
{
	u8* address=(u8*)dar;

	if (VramLockedWrite(address))
	{
//		printf("VramLockedWrite\n");
		return NULL;
	}
	else if (RamLockedWrite(address,NULL))
	{
//		printf("RamLockedWrite\n");
		return NULL;
	}
	else if (((u32)(address-sh4_reserved_mem))<((512+48)*1024*1024))
	{
		CompiledBlockInfo* cbi=bm_ReverseLookup(srr0);

		if (!cbi)
		{
			dlog("**DYNAREC_BUG: bm_ReverseLookup failed to resolve %08X, will blindly patch due to %08X**\n",srr0,address);
			return NULL;
		}

#ifdef DEBUG
		else
			dlog("Except in block %X | %X\n",cbi->Code,cbi);
#endif

		//printf("Rewrite %d %p %p %d\n",pir,srr0,dar,write);
        
        return roml_patch_for_reg_access((u32)srr0,false);
	}
	else if (((u32)(address-sh4_reserved_mem))<(768*1024*1024))
	{
//        printf("reg access from SQ mapping %d %p %p %d\n",pir,srr0,dar,write);
        
        return roml_patch_for_reg_access((u32)srr0,true);
    }
	else
	{
		dlog("[GPF]Unhandled access to : 0x%X\n",address);
    	return ((PowerPC_instr*)srr0)+1;
	}
}

#ifdef USE_GUI
void ErrorPrompt(const char *msg);
void InfoPrompt(const char *msg);
#endif

int msgboxf(char* text,unsigned int type,...)
{
	va_list args;

	char temp[2048];
	va_start(args, type);
	vsprintf(temp, text, args);
	va_end(args);

	printf("[msgboxf] %s\n",temp);

	return 0;
}

void bswap_block(void * addr,int size)
{
	verify(!(size&3));
	verify(!((u32)addr&3));
	
	u32 * p= (u32*) addr;
	
	for(int i=0;i<size/4;++i)
		p[i]=__builtin_bswap32(p[i]);
}
