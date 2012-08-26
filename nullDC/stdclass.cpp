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
wchar temp[1000];
wchar* GetNullDCSoruceFileName(wchar* full)
{
	size_t len = strlen(full);
	while(len>18)
	{
		if (full[len]=='/')
		{
			memcpy(&temp[0],&full[len-14],15*sizeof(wchar));
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

wchar* GetPathFromFileNameTemp(wchar* full)
{
	size_t len = strlen(full);
	while(len>2)
	{
		if (full[len]=='/')
		{
			memcpy(&temp[0],&full[0],(len+1)*sizeof(wchar));
			temp[len+1]=0;
			return temp;	
		}
		len--;
	}
	strcpy(temp,full);
	return &temp[0];
}

void GetPathFromFileName(wchar* path)
{
	strcpy(path,GetPathFromFileNameTemp(path));
}

void GetFileNameFromPath(wchar* path,wchar* outp)
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

wchar AppPath[1024] = "sda:/nulldc-360/";
void GetApplicationPath(wchar* path,u32 size)
{
	strcpy(path,AppPath);
}

wchar* GetEmuPath(const wchar* subpath)
{
	wchar* temp=(wchar*)malloc(1024);
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

bool cDllHandler::Load(wchar* dll)
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
			log("FreeLibrary -- failed %d\n",GetLastError());
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
void FindAllFiles(FileFoundCB* callback,wchar* dir,void* param)
{
/*gli	WIN32_FIND_DATA FindFileData;
	HANDLE hFind = INVALID_HANDLE_VALUE;
	wchar DirSpec[MAX_PATH + 1];  // directory specification
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

bool VramLockedWrite(u8* address);
bool RamLockedWrite(u8* address,u32* sp);
extern u8* sh4_mem_marks;
extern u8* DynarecCache;
extern u32 DynarecCacheSize;
void * ExeptionHandler(int pir_,void * srr0,void * dar,int write)
{
	u8* address=(u8*)dar;

	if (VramLockedWrite(address))
	{
//		printf("VramLockedWrite\n");
		return NULL;// EXCEPTION_CONTINUE_EXECUTION;
	}
	else if (RamLockedWrite(address,NULL))
	{
		printf("RamLockedWrite\n");
		return NULL;// EXCEPTION_CONTINUE_EXECUTION;
	}
	else if (((u32)(address-sh4_reserved_mem))<(512*1024*1024) || ((u32)(address-sh4_mem_marks))<(64*2*PAGE_SIZE))
	{
		//printf("Rewrite %d %p %p %d ",pir,srr0,dar,write);
		
		u32 pos=(u32)srr0;
		CompiledBlockInfo* cbi=bm_ReverseLookup((void*)pos);

		if (!cbi)
		{
			log("**DYNAREC_BUG: bm_ReverseLookup failed to resolve %08X, will blindly patch due to %08X**\n",pos,address);
			log("**PLEASE REPORT THIS IF NOT ON THE ISSUE TRACKER ALREADY --raz**\n");
			return NULL;// EXCEPTION_CONTINUE_SEARCH;
		}

#ifdef DEBUG
		else
			log("Except in block %X | %X\n",cbi->Code,cbi);
#endif

#if 0
		
		//cbb->Rewrite
		// find last branch and make it always branch (never load directly)
		PowerPC_instr * branch=(PowerPC_instr*)pos;
		PowerPC_instr branch_op;
//		disassemble((u32)branch,* branch);
		do{
			--branch;
			branch_op=*branch;
		}while(branch_op>>PPC_OPCODE_SHIFT!=PPC_OPCODE_BC);
		
		branch_op&=~(0x1f<<21);
		branch_op|=PPC_CC_A<<21;
//		disassemble((u32)branch,branch_op);
		branch_op-=4; // skip apply_roml_patch SQ stuff
		*branch=branch_op;
		memicbi(branch,4);
		return branch+((branch_op>>2)&0x3fff);
	
#else
		
		PowerPC_instr * memop=(PowerPC_instr*)pos;
		PowerPC_instr * branch;
		PowerPC_instr supposed_branch_op;
		PowerPC_instr cur_op;
		PowerPC_instr branch_op;
		
		branch=memop;
		do{

			++branch;
			
			cur_op=*branch;
			
			GEN_B(supposed_branch_op,-((u32)branch-pos)>>2,0,0);

		}while(cur_op!=supposed_branch_op);
		
		++branch; // skip dummy reverse branch
		
		GEN_B(branch_op,((u32)branch-pos)>>2,0,0);
		
		*memop=branch_op;
		memicbi(memop,4);
		return branch;
		
#endif

	}
	else
	{
		log("[GPF]Unhandled access to : 0x%X\n",address);
	}
	return NULL;// EXCEPTION_CONTINUE_SEARCH;
}

int msgboxf(char* text,unsigned int type,...)
{
	va_list args;

	char temp[2048];
	va_start(args, type);
	vsprintf(temp, text, args);
	va_end(args);

	printf("[msgboxf] %s\n",temp);
	
	if (libgui.MsgBox!=0)
	{
		return libgui.MsgBox(temp,type);
	}
/*gli	else
		return MessageBox(NULL,temp,VER_SHORTNAME,type | MB_TASKMODAL);*/
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
