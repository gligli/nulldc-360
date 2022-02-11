//new memory mapping code ..."_vmem" ... Don't ask where i got the name , it somehow poped on my head :p
//
/*
_vmem v2.5 :
	Translated remaping with indirect functions
	
	Mapping: 
	
	Handler
		(0x8001_0000 + entry*0x4_0000)-PAGE_BASE

	Buffer
		ptr-PAGE_BASE

*/

#include "_vmem.h"
#include "dc/aica/aica_if.h"
#include "log/logging_interface.h"
#include <byteswap.h>
#include <malloc.h>

extern "C" {
#include <ppc/vm.h>
#include <ppc/timebase.h>
}

//top registed handler
_vmem_handler			_vmem_lrp;

//handler tables
_vmem_ReadMem8FP*		_vmem_RF8[0x1000];
_vmem_WriteMem8FP*		_vmem_WF8[0x1000];

_vmem_ReadMem16FP*		_vmem_RF16[0x1000];
_vmem_WriteMem16FP*		_vmem_WF16[0x1000];

_vmem_ReadMem32FP*		_vmem_RF32[0x1000];
_vmem_WriteMem32FP*		_vmem_WF32[0x1000];

//upper 16b of the address
void* _vmem_MemInfo[0x10000];
u8* sh4_reserved_mem;
u8* sh4_ram_alt;	//alternative ram space map

bool INLINE _vmem_translate(u32 addr,unat& entry_or_fptr)
{
	entry_or_fptr = (unat)_vmem_MemInfo[(u16)((u32)(addr >> 16))]+addr;

	if ((s32)entry_or_fptr < 0)
	{
		entry_or_fptr=(entry_or_fptr>>16)-0x8001;
		return true;
	}

	return false;
}
/*
template<typename T>
T __fastcall _vmem_readt(u32 addr)
{
	unat data_ptr;
	if (_vmem_translate(addr,data_ptr))
	{
		if (sizeof(T)==1)
			return (u8)(*(_vmem_ReadMem8FP**)((u8*)_vmem_RF8+data_ptr))(addr);
		else if (sizeof(T)==2)
			return (u16)(*(_vmem_ReadMem16FP**)((u8*)_vmem_RF16+data_ptr))(addr);
		else if (sizeof(T)==4)
			return (u32)(*(_vmem_ReadMem32FP**)((u8*)_vmem_RF32+data_ptr))(addr);
		else if (sizeof(T)==8)
		{
			//VS2k10: REALLY BAD CODE GENERATED
			_vmem_ReadMem32FP* handler=*(_vmem_ReadMem32FP**)((u8*)_vmem_RF32+data_ptr);
			return (u64)(handler(addr) | (((u64)handler(addr+4))<<32));
		}
	}
	else
		return *(T*)data_ptr;

}

template<typename T>
void __fastcall _vmem_writet(u32 addr,T data)
{
	unat data_ptr;
	if (_vmem_translate(addr,data_ptr))
	{
		if (sizeof(T)==1)
			(*(_vmem_WriteMem8FP**)((u8*)_vmem_WF8+data_ptr))(addr,data);
		else if (sizeof(T)==2)
			(*(_vmem_WriteMem16FP**)((u8*)_vmem_WF16+data_ptr))(addr,data);
		else if (sizeof(T)==4)
			(*(_vmem_WriteMem32FP**)((u8*)_vmem_WF32+data_ptr))(addr,data);
		else if (sizeof(T)==8)
		{
			//VS2k10: REALLY BAD CODE GENERATED
			_vmem_WriteMem32FP* handler=*(_vmem_WriteMem32FP**)((u8*)_vmem_WF32+data_ptr);
			handler(addr,(u32)data);
			handler(addr+4,data>>32);
		}
	}
	else
		*(T*)data_ptr=data;
}
*/

//ReadMem/WriteMem functions
//ReadMem

extern u32 pc;

//#define CHECK_ALIGN
//#define MEM_VERBOSE
//#define MEM_PROF

u64 time_rw_regs=0;


u8 INLINE _vmem_ReadMem8(u32 addr) 
{
#ifdef MEM_PROF
	u64 rwt=mftb();
#endif
#ifdef MEM_VERBOSE	
	printf("_vmem_ReadMem8 %08x\n",addr);
#endif
	unat data_ptr;
	u8 res= (_vmem_translate(addr,data_ptr)) ? (u8)(*(_vmem_ReadMem8FP**)((u8*)_vmem_RF8+data_ptr))(addr) : *(u8*)(data_ptr^3);
	
#ifdef MEM_PROF
	rwt=mftb()-rwt;
	time_rw_regs+=rwt;
#endif
	return res;
}

u16 INLINE _vmem_ReadMem16(u32 addr)
{ 
#ifdef MEM_PROF
	u64 rwt=mftb();
#endif
#ifdef MEM_VERBOSE	
	printf("_vmem_ReadMem16 %08x\n",addr);
#endif
#ifdef CHECK_ALIGN
	if (addr&1) die("misaligned");
#endif
	unat data_ptr;
	u16 res= (_vmem_translate(addr,data_ptr)) ? (u16)(*(_vmem_ReadMem16FP**)((u8*)_vmem_RF16+data_ptr))(addr) : *(u16*)(data_ptr^2);
	
#ifdef MEM_PROF
	rwt=mftb()-rwt;
	time_rw_regs+=rwt;
#endif
	return res;
}

u32 INLINE _vmem_ReadMem32(u32 addr)
{ 
#ifdef MEM_PROF
	u64 rwt=mftb();
#endif
#ifdef MEM_VERBOSE	
	printf("_vmem_ReadMem32 %08x\n",addr);
#endif
#ifdef CHECK_ALIGN
	if (addr&3) die("misaligned");
#endif
	unat data_ptr;
	u32 res= (_vmem_translate(addr,data_ptr)) ? (u32)(*(_vmem_ReadMem32FP**)((u8*)_vmem_RF32+data_ptr))(addr) : *(u32*)data_ptr;

#ifdef MEM_PROF
	rwt=mftb()-rwt;
	time_rw_regs+=rwt;
#endif
	return res;
}

u64 INLINE _vmem_ReadMem64(u32 addr)
{
#ifdef MEM_PROF
	u64 rwt=mftb();
#endif
#ifdef MEM_VERBOSE	
	printf("_vmem_ReadMem64 %08x\n",addr);
#endif
#ifdef CHECK_ALIGN
	if (addr&7) die("misaligned");
#endif
	unat data_ptr;
	if(_vmem_translate(addr,data_ptr))
	{
		_vmem_ReadMem32FP* handler=*(_vmem_ReadMem32FP**)((u8*)_vmem_RF32+data_ptr);
		return (u64)(handler(addr+4) | (((u64)handler(addr))<<32));
	}

#ifdef MEM_PROF
	rwt=mftb()-rwt;
	time_rw_regs+=rwt;
#endif
	
	return *(u64*)data_ptr;
}

//WriteMem
void INLINE _vmem_WriteMem8(u32 addr,u8 data)
{
#ifdef MEM_VERBOSE	
	printf("_vmem_WriteMem8 %08x %02x\n",addr,data);
#endif
	unat data_ptr;

	if (_vmem_translate(addr,data_ptr))
	{
#ifdef MEM_PROF
		u64 rwt=mftb();
#endif
		(*(_vmem_WriteMem8FP**)((u8*)_vmem_WF8+data_ptr))(addr,data);
#ifdef MEM_PROF
		rwt=mftb()-rwt;
		time_rw_regs+=rwt;
#endif
		return;
	}

	*(u8*)(data_ptr^3) = data;
}

void INLINE _vmem_WriteMem16(u32 addr,u16 data)
{
#ifdef MEM_VERBOSE	
	printf("_vmem_WriteMem16 %08x %04x\n",addr,data);
#endif
#ifdef CHECK_ALIGN
	if (addr&1) die("misaligned");
#endif
	unat data_ptr;
	
	if (_vmem_translate(addr,data_ptr))
	{
#ifdef MEM_PROF
		u64 rwt=mftb();
#endif
		(*(_vmem_WriteMem16FP**)((u8*)_vmem_WF16+data_ptr))(addr,data);
#ifdef MEM_PROF
		rwt=mftb()-rwt;
		time_rw_regs+=rwt;
#endif
		return;
	}

	*(u16*)(data_ptr^2) = data;
}

void INLINE _vmem_WriteMem32(u32 addr,u32 data)
{
#ifdef MEM_VERBOSE	
	printf("_vmem_WriteMem32 %08x %08x\n",addr,data);
#endif
#ifdef CHECK_ALIGN
	if (addr&3) die("misaligned");
#endif
	unat data_ptr;

	if (_vmem_translate(addr,data_ptr))
	{
#ifdef MEM_PROF
		u64 rwt=mftb();
#endif
		(*(_vmem_WriteMem32FP**)((u8*)_vmem_WF32+data_ptr))(addr,data);
#ifdef MEM_PROF
		rwt=mftb()-rwt;
		time_rw_regs+=rwt;
#endif
		return;
	}

	*(u32*)data_ptr = data;
}

void INLINE _vmem_WriteMem64(u32 addr,u64 data)
{
#ifdef MEM_VERBOSE	
	printf("_vmem_WriteMem64 %08x %016llx\n",addr,data);
#endif
#ifdef CHECK_ALIGN
	if (addr&7) die("misaligned");
#endif
	unat data_ptr;

	if (_vmem_translate(addr,data_ptr))
	{
#ifdef MEM_PROF
		u64 rwt=mftb();
#endif
		_vmem_WriteMem32FP* handler=*(_vmem_WriteMem32FP**)((u8*)_vmem_WF32+data_ptr);
		handler(addr+4,(u32)data);
		handler(addr,data>>32);
#ifdef MEM_PROF
		rwt=mftb()-rwt;
		time_rw_regs+=rwt;
#endif
		return;
	}

	*(u64*)data_ptr = data;
}

//0xDEADC0D3 or 0
#define MEM_ERROR_RETURN_VALUE 0xDEADC0D3

//phew .. that was lota asm code ;) lets go back to C :D
//default mem handlers ;)
//defualt read handlers
u8 fastcall _vmem_ReadMem8_not_mapped(u32 addresss)
{
	dlog("[sh4]Read8 from 0x%X, not mapped [_vmem default handler]\n",addresss);
	return (u8)MEM_ERROR_RETURN_VALUE;
}
u16 fastcall _vmem_ReadMem16_not_mapped(u32 addresss)
{
	dlog("[sh4]Read16 from 0x%X, not mapped [_vmem default handler]\n",addresss);
	return (u16)MEM_ERROR_RETURN_VALUE;
}
u32 fastcall _vmem_ReadMem32_not_mapped(u32 addresss)
{
	dlog("[sh4]Read32 from 0x%X, not mapped [_vmem default handler]\n",addresss);
	return (u32)MEM_ERROR_RETURN_VALUE;
}
//defualt write handers
void fastcall _vmem_WriteMem8_not_mapped(u32 addresss,u8 data)
{
	dlog("[sh4]Write8 to 0x%X=0x%X, not mapped [_vmem default handler]\n",addresss,data);
}
void fastcall _vmem_WriteMem16_not_mapped(u32 addresss,u16 data)
{
	dlog("[sh4]Write16 to 0x%X=0x%X, not mapped [_vmem default handler]\n",addresss,data);
}
void fastcall _vmem_WriteMem32_not_mapped(u32 addresss,u32 data)
{
	dlog("[sh4]Write32 to 0x%X=0x%X, not mapped [_vmem default handler]\n",addresss,data);
}
//code to register handlers
//0 is considered error :)
_vmem_handler _vmem_register_handler(
									 _vmem_ReadMem8FP* read8, 
									 _vmem_ReadMem16FP* read16,
									 _vmem_ReadMem32FP* read32,

									 _vmem_WriteMem8FP* write8,
									 _vmem_WriteMem16FP* write16,
									 _vmem_WriteMem32FP* write32
									 )
{
	_vmem_handler rv=_vmem_lrp++;

	_vmem_RF8[rv] =read8==0  ?	_vmem_ReadMem8_not_mapped  :	read8;
	_vmem_RF16[rv]=read16==0 ?	_vmem_ReadMem16_not_mapped :	read16;
	_vmem_RF32[rv]=read32==0 ?	_vmem_ReadMem32_not_mapped :	read32;

	_vmem_WF8[rv] =write8==0 ?	_vmem_WriteMem8_not_mapped :	write8;
	_vmem_WF16[rv]=write16==0?	_vmem_WriteMem16_not_mapped:	write16;
	_vmem_WF32[rv]=write32==0?	_vmem_WriteMem32_not_mapped:	write32;

	return rv;
}

//map a registed handler to a mem region :)
void _vmem_map_handler(_vmem_handler Handler,u32 start,u32 end)
{
	verify(start<0x10000);
	verify(end<0x10000);
	verify(start<=end);
	for (u32 i=start;i<=end;i++)
	{
		_vmem_MemInfo[i]=((u8*)0 + 0x80010000 + Handler*0x40000)-(i*0x10000);
	}
}
//map a memory block to a mem region :)
void _vmem_map_block(void* base,u32 start,u32 end)
{
	verify(start<0x10000);
	verify(end<0x10000);
	verify(start<=end);
	u32 j=0;
	for (u32 i=start;i<=end;i++)
	{
		_vmem_MemInfo[i]=(u8*)base+j-(i*0x10000);
		j+=0x10000;
	}
}
void _vmem_mirror_mapping(u32 new_region,u32 start,u32 size)
{
	u32 end=start+size-1;
	verify(start<0x10000);
	verify(end<0x10000);
	verify(start<=end);
	verify(!((start>=new_region) && (end<=new_region)));

	u32 j=new_region;
	for (u32 i=start;i<=end;i++)
	{
		_vmem_MemInfo[j&0xFFFF]=(u8*)_vmem_MemInfo[i&0xFFFF]+(i*0x10000)-(j*0x10000);
		j++;
	}
}

//init/reset/term
void _vmem_init()
{
	_vmem_reset();
}

void _vmem_reset()
{
	//clear read tables
	memset(_vmem_RF8,0,sizeof(_vmem_RF8));
	memset(_vmem_RF16,0,sizeof(_vmem_RF16));
	memset(_vmem_RF32,0,sizeof(_vmem_RF32));
	//clear write tables
	memset(_vmem_WF8,0,sizeof(_vmem_WF8));
	memset(_vmem_WF16,0,sizeof(_vmem_WF16));
	memset(_vmem_WF32,0,sizeof(_vmem_WF32));
	//clear meminfo table
	memset(_vmem_MemInfo,0,sizeof(_vmem_MemInfo));

	//reset registation index
	_vmem_lrp=0;

	//register default functions (0) for slot 0 :)
	u32 test;
	test = _vmem_register_handler(0,0,0,0,0,0);
	verify(test==0);
	_vmem_map_handler(0,0,0xFFFF);
}

void _vmem_term()
{
	//empty :(
}
#include "dc/pvr/pvr_if.h"
#include "sh4_mem.h"
#include "sh4_internal_reg.h"

//i'm not sure not defining this works anymore
//file mapping is used to create read-only mirrors, to increase speedeh!
#define _VMEM_FILE_MAPPING
#ifdef _VMEM_FILE_MAPPING
#define MAP_RAM_START_OFFSET  0
#define MAP_VRAM_START_OFFSET (MAP_RAM_START_OFFSET+RAM_SIZE)
#define MAP_ARAM_START_OFFSET (MAP_VRAM_START_OFFSET+VRAM_SIZE)

void* _nvmem_map_buffer(u32 dst,u32 addrsz,u32 size,bool ro_mirrors)
{
	void * rv=&sh4_reserved_mem[dst];
	
	u32 map_times=addrsz/size;
	verify((addrsz%size)==0);
	verify(map_times>=1);

	u64 mem=((u64)memalign(VM_USER_PAGE_SIZE,size))&0x7fffffff;
	
	vm_create_user_mapping((u32)&sh4_reserved_mem[dst],mem,size,VM_WIMG_CACHED); //FILE_MAP_READ |FILE_MAP_WRITE
	
	for (u32 i=1;i<map_times;i++)
	{
		dst+=size;
        vm_create_user_mapping((u32)&sh4_reserved_mem[dst],mem,size,ro_mirrors?VM_WIMG_CACHED_READ_ONLY:VM_WIMG_CACHED); //FILE_MAP_READ
	}

	return rv;
}


void* _nvmem_unused_buffer(u32 start,u32 end)
{
	vm_destroy_user_mapping((u32)&sh4_reserved_mem[start],end-start); //MEM_RESERVE,PAGE_NOACCESS
	return &sh4_reserved_mem[start];
}

#define map_buffer(dsts,dste,sz,ro_mirrors) {ptr=_nvmem_map_buffer(dsts,dste-dsts,sz,ro_mirrors);if (!ptr) return false;}
#define unused_buffer(start,end) {ptr=_nvmem_unused_buffer(start,end);if (!ptr) return false;}


bool _vmem_reserve()
{
	vm_set_user_mapping_segfault_handler(ExeptionHandler);
	
	void* ptr=0;
	sh4_reserved_mem=(u8*)0x40000000; //512*1024*1024,MEM_RESERVE,PAGE_NOACCESS
	if (sh4_reserved_mem==0)
		return false;
	
	//Area 0
	//[0x00000000 ,0x00800000) -> unused
	unused_buffer(0x00000000,0x00800000);

	//i wonder, aica ram warps here ?.?
	//i realy should check teh docs before codin ;p
	//[0x00800000,0x00A00000);
	unused_buffer(0x00000000,0x01000000);
	
	ptr=_nvmem_map_buffer(0x18000000,0x00800000,ARAM_SIZE,true);
	aica_ram.size=ARAM_SIZE;
	aica_ram.data=(u8*)ptr;
	//[0x01000000 ,0x04000000) -> unused
	unused_buffer(0x01000000,0x04000000);
	
	//Area 1
	//[0x04000000,0x05000000) -> vram (16mb, warped on dc)
	map_buffer(0x04000000,0x05000000,VRAM_SIZE,true);
	
	vram.size=VRAM_SIZE;
	vram.data=(u8*)ptr;

	//[0x05000000,0x06000000) -> unused (32b path)
	unused_buffer(0x05000000,0x06000000);

	//[0x06000000,0x07000000) -> vram   mirror
	map_buffer(0x06000000,0x07000000,VRAM_SIZE,true);

	//[0x07000000,0x08000000) -> unused (32b path) mirror
	unused_buffer(0x07000000,0x08000000);
	
	//Area 2
	//[0x08000000,0x0C000000) -> unused
	unused_buffer(0x08000000,0x0C000000);
	
	//Area 3
	//[0x0C000000,0x0D000000) -> main ram
	//[0x0D000000,0x0E000000) -> main ram mirror
	//[0x0E000000,0x0F000000) -> main ram mirror
	//[0x0F000000,0x10000000) -> main ram mirror
	map_buffer(0x0C000000,0x10000000,RAM_SIZE,true);
	
	mem_b.size=RAM_SIZE;
	mem_b.data=(u8*)ptr;
	
	//Area 4
	//Area 5
	//Area 6
	//Area 7
	//all -> Unused 
	//[0x10000000,0x20000000) -> unused
	unused_buffer(0x10000000,0x18000000);
	unused_buffer(0x1C000000,0x20000000);

    //second mapping (for SQ access)
    map_buffer(0x20000000,0x23000000,VM_USER_PAGE_SIZE,false);
    sq_page=(u8*)ptr;
    
    memset(sq_page,0x42,VM_USER_PAGE_SIZE);

	return sh4_reserved_mem!=0;
}

#else
bool _vmem_reserve()
{
	sh4_reserved_mem=0;
	void* ptr=0;
/*gli	sh4_reserved_mem=(u8*)VirtualAlloc(0,512*1024*1024,MEM_RESERVE,PAGE_NOACCESS);
	if (sh4_reserved_mem==0)
		return false;*/
	//VirtualFree(sh4_reserved_mem,0,MEM_RELEASE);
	
	//Area 0
	//[0x00800000,0x00A00000);
//gli	ptr=VirtualAlloc(&sh4_reserved_mem[0x00800000],0x00200000,MEM_COMMIT,PAGE_READWRITE);
	ptr=memalign(0x10000,0x00200000);
	if (ptr==0)
		return false;
	aica_ram.size=0x00200000;
	aica_ram.data=(u8*)ptr;
	//[0 ,0x04000000) -> unused
	//ptr=VirtualAlloc(&sh4_reserved_mem[0x00000000],0x04000000,MEM_RESERVE,PAGE_NOACCESS);
	//if (ptr==0)
	//	return false;
	//Area 1
	//[0x04000000,0x05000000) -> vram | mirror
	//[0x05000000,0x06000000) -> unused
	//[0x06000000,0x07000000) -> vram   mirror
	//[0x07000000,0x08000000) -> unused mirror
//gli	ptr=VirtualAlloc(&sh4_reserved_mem[0x04000000],VRAM_SIZE,MEM_COMMIT,PAGE_READWRITE);
	ptr=memalign(0x10000,VRAM_SIZE);
	if (ptr==0)
		return false;
	vram.size=VRAM_SIZE;
	vram.data=(u8*)ptr;
/*
	//vram #0
	ptr= MapViewOfFileEx(mem_handle,FILE_MAP_READ |FILE_MAP_WRITE,0,RAM_SIZE,VRAM_SIZE,&sh4_reserved_mem[0x04000000]);
	ptr= MapViewOfFileEx(mem_handle,FILE_MAP_READ |FILE_MAP_WRITE,0,RAM_SIZE,VRAM_SIZE,&sh4_reserved_mem[0x05000000]);
	if (ptr==0)
		return false;
	vram.size=VRAM_SIZE;
	vram.data=(u8*)ptr;

	//vram #1
	ptr= MapViewOfFileEx(mem_handle,FILE_MAP_READ |FILE_MAP_WRITE,0,RAM_SIZE,VRAM_SIZE,&sh4_reserved_mem[0x06000000]);
	if (ptr==0)
		return false;
*/
	//Area 2
	//[0x08000000,0x0C000000) -> unused
	/*
	ptr=VirtualAlloc(&sh4_reserved_mem[0x08000000],0x04000000,MEM_RESERVE,PAGE_NOACCESS);
	if (ptr==0)
		return false;
	*/
	//Area 3
	//[0x0C000000,0x0D000000) -> main ram
	//[0x0D000000,0x0E000000) -> main ram mirror
	//[0x0E000000,0x0F000000) -> main ram mirror
	//[0x0F000000,0x10000000) -> main ram mirror
		//ram #0
//gli	ptr=VirtualAlloc(&sh4_reserved_mem[0x0C000000],RAM_SIZE,MEM_COMMIT,PAGE_READWRITE);
	ptr=memalign(0x10000,RAM_SIZE);
	if (ptr==0)
		return false;
	mem_b.size=RAM_SIZE;
	mem_b.data=(u8*)ptr;
	/*
	ptr= MapViewOfFileEx(mem_handle,FILE_MAP_READ |FILE_MAP_WRITE,0,0,RAM_SIZE,&sh4_reserved_mem[0x0C000000]);
	if (ptr==0)
		return false;
	mem_b.size=RAM_SIZE;
	mem_b.data=(u8*)ptr;

	//ram #1
	ptr= MapViewOfFileEx(mem_handle,FILE_MAP_READ |FILE_MAP_WRITE,0,0,RAM_SIZE,&sh4_reserved_mem[0x0D000000]);
	if (ptr==0)
		return false;
*/
	//Area 4
	//Area 5
	//Area 6
	//Area 7
	//all -> Unused 
	//[0x10000000,0x20000000) -> unused
/*	
	ptr=VirtualAlloc(&sh4_reserved_mem[0x10000000],0x10000000,MEM_RESERVE,PAGE_NOACCESS);
	if (ptr==0)
		return false;
*/

//gli	return sh4_reserved_mem!=0;
	return true;
}

#endif
void _vmem_release()
{
//gli	VirtualFree(sh4_reserved_mem,0,MEM_RELEASE);
	
#ifndef _VMEM_FILE_MAPPING
	free(aica_ram.data);
	free(vram.data);
	free(mem_b.data);
#endif
		
	//This might be leaking, but its done at the destruction of the process anyway ... so it matters litle
}