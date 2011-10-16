#include "drkPvr.h"
#include "helper_classes.h"

u8* buffer_pool[(64*1024*1024)/ChunkSize];
u32 buffer_pool_count;

u8* GetBuffer()
{
	if (buffer_pool_count!=0)
	{
		u8* rv=buffer_pool[--buffer_pool_count];
		//VirtualLock(rv,ChunkSize);
		return rv;
	}
	else
	{
		u8* ptr=(u8*)malloc(ChunkSize/*+8192*/);
		/*DWORD old;
		VirtualProtect(ptr,4096,PAGE_NOACCESS,&old);
		VirtualProtect(&ptr[ChunkSize+4096],4096,PAGE_NOACCESS,&old);*/
		
		//VirtualAlloc(ptr,ChunkSize,MEM_RESET,PAGE_READWRITE);
		//VirtualLock(ptr,ChunkSize);
		return ptr /*+4096*/;
	}
}

void FreeBuffer(u8* buffer)
{
	if (buffer_pool_count!=1024)
	{
		//VirtualUnlock(buffer,ChunkSize);
		//VirtualAlloc(buffer,ChunkSize,MEM_RESET,PAGE_READWRITE);
		buffer_pool[buffer_pool_count++]=buffer;
	}
	else
	{
		free(buffer/*-4096*/);
	}
}