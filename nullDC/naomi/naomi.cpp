/*
	This file is a mix of my code, Zezu's, and duno wtf-else (most likely ElSemi's ?)
*/
#include "../types.h"
#include "../dc/mem/sb.h"
#include "../dc/mem/sh4_mem.h"
#include "../dc/asic/asic.h"
#include "naomi.h"
#include "naomi_regs.h"
#include "config/config.h"

u32 naomi_updates;
//For file memory mapping :p
#include <windows.h>

//#define NAOMI_COMM

#if defined(BUILD_NAOMI	) || defined(BUILD_ATOMISWAVE)

u8* RomPtr;
HANDLE*	RomCacheMap;
u32		RomCacheMapCount;

u32 RomPioOffset=0;

DWORD DmaOffset;
DWORD DmaCount;

DWORD BoardID=0x980055AA;
DWORD GSerialBuffer=0,BSerialBuffer=0;
int GBufPos=0,BBufPos=0;
int GState=0,BState=0;
int GOldClk=0,BOldClk=0;
int BControl=0,BCmd=0,BLastCmd=0;
int GControl=0,GCmd=0,GLastCmd=0;
int SerStep=0,SerStep2=0;

#ifdef NAOMI_COMM
	u32 CommOffset;
	u32* CommSharedMem;
	HANDLE CommMapFile=INVALID_HANDLE_VALUE;
#endif

/*
El numero de serie solo puede contener:
0-9		(0x30-0x39)
A-H		(0x41-0x48)
J-N		(0x4A-0x4E)
P-Z		(0x50-0x5A)
*/
static unsigned char BSerial[]="\xB7"/*CRC1*/"\x19"/*CRC2*/"0123234437897584372973927387463782196719782697849162342198671923649";
static unsigned char GSerial[]="\xB7"/*CRC1*/"\x19"/*CRC2*/"0123234437897584372973927387463782196719782697849162342198671923649";

unsigned int ShiftCRC(unsigned int CRC,unsigned int rounds)
{
	const unsigned int Magic=0x10210000;
	unsigned int i;
	for(i=0;i<rounds;++i)
	{
		if(CRC&0x80000000)
			CRC=(CRC<<1)+Magic;
		else
			CRC=(CRC<<1);
	}
	return CRC;
}

unsigned short CRCSerial(unsigned char *Serial,unsigned int len)
{
	unsigned int CRC=0xDEBDEB00;
	unsigned int i;

	for(i=0;i<len;++i)
	{
		unsigned char c=Serial[i];
		//CRC&=0xFFFFFF00;
		CRC|=c;
		CRC=ShiftCRC(CRC,8);
	}
	CRC=ShiftCRC(CRC,8);
	return (u16)(CRC>>16);
}


void NaomiInit()
{
	//RomOffset=0;
	DmaCount=0xffff;
	DmaOffset=0;

	WORD CRC;
	CRC=CRCSerial(BSerial+2,0x2E);
	BSerial[0]=(u8)(CRC>>8);
	BSerial[1]=(u8)(CRC);

	CRC=CRCSerial(GSerial+2,0x2E);
	GSerial[0]=(u8)(CRC>>8);
	GSerial[1]=(u8)(CRC);
}




void NaomiBoardIDWrite(const u16 Data)
{
	int Dat=Data&8;
	int Clk=Data&4;
	int Rst=Data&0x20;
	int Sta=Data&0x10;
	

	if(Rst)
	{
		BState=0;
		BBufPos=0;
	}

	
	if(Clk!=BOldClk && !Clk)	//Falling Edge clock
	{
		//State change
		if(BState==0 && Sta) 
			BState=1;		
		if(BState==1 && !Sta)
			BState=2;

		if((BControl&0xfff)==0xFF0)	//Command mode
		{
			BCmd<<=1;
			if(Dat)
				BCmd|=1;
			else
				BCmd&=0xfffffffe;
		}

		//State processing
		if(BState==1)		//LoadBoardID
		{
			BSerialBuffer=BoardID;
			BBufPos=0;		//??
		}
		if(BState==2)		//ShiftBoardID
		{
			BBufPos++;
		}
	}
	BOldClk=Clk;
}

u16 NaomiBoardIDRead()
{
	if((BControl&0xff)==0xFE)
		return 0xffff;
	return (BSerialBuffer&(1<<(31-BBufPos)))?8:0;
}

static DWORD AdaptByte(BYTE val)
{
	return val<<24;
}

void NaomiBoardIDWriteControl(const u16 Data)
{
	if((Data&0xfff)==0xF30 && BCmd!=BLastCmd)
	{
		if((BCmd&0x81)==0x81)
		{
			SerStep2=(BCmd>>1)&0x3f;

			BSerialBuffer=0x00000000;	//First block contains CRC
			BBufPos=0;
		}
		if((BCmd&0xff)==0x55)	//Load Offset 0
		{
			BState=2;
			BBufPos=0;
			BSerialBuffer=AdaptByte(BSerial[8*SerStep2])>>1;
		}
		if((BCmd&0xff)==0xAA)	//Load Offset 1
		{
			BState=2;
			BBufPos=0;
			BSerialBuffer=AdaptByte(BSerial[8*SerStep2+1]);
		}
		if((BCmd&0xff)==0x54)
		{
			BState=2;
			BBufPos=0;
			BSerialBuffer=AdaptByte(BSerial[8*SerStep2+2]);
		}
		if((BCmd&0xff)==0xA8)
		{
			BState=2;
			BBufPos=0;
			BSerialBuffer=AdaptByte(BSerial[8*SerStep2+3]);
		}
		if((BCmd&0xff)==0x50)
		{
			BState=2;
			BBufPos=0;
			BSerialBuffer=AdaptByte(BSerial[8*SerStep2+4]);
		}
		if((BCmd&0xff)==0xA0)
		{
			BState=2;
			BBufPos=0;
			BSerialBuffer=AdaptByte(BSerial[8*SerStep2+5]);
		}
		if((BCmd&0xff)==0x40)
		{
			BState=2;
			BBufPos=0;
			BSerialBuffer=AdaptByte(BSerial[8*SerStep2+6]);
		}
		if((BCmd&0xff)==0x80)
		{
			BState=2;
			BBufPos=0;
			BSerialBuffer=AdaptByte(BSerial[8*SerStep2+7]);
		}
		BLastCmd=BCmd;
	}
	BControl=Data;
}

void NaomiGameIDProcessCmd()
{
	if(GCmd!=GLastCmd)
	{
		if((GCmd&0x81)==0x81)
		{
			SerStep=(GCmd>>1)&0x3f;

			GSerialBuffer=0x00000000;	//First block contains CRC
			GBufPos=0;
		}
		if((GCmd&0xff)==0x55)	//Load Offset 0
		{
			GState=2;
			GBufPos=0;
			GSerialBuffer=AdaptByte(GSerial[8*SerStep])>>0;
		}
		if((GCmd&0xff)==0xAA)	//Load Offset 1
		{
			GState=2;
			GBufPos=0;
			GSerialBuffer=AdaptByte(GSerial[8*SerStep+1]);
		}
		if((GCmd&0xff)==0x54)
		{
			GState=2;
			GBufPos=0;
			GSerialBuffer=AdaptByte(GSerial[8*SerStep+2]);
		}
		if((GCmd&0xff)==0xA8)
		{
			GState=2;
			GBufPos=0;
			GSerialBuffer=AdaptByte(GSerial[8*SerStep+3]);
		}
		if((GCmd&0xff)==0x50)
		{
			GState=2;
			GBufPos=0;
			GSerialBuffer=AdaptByte(GSerial[8*SerStep+4]);
		}
		if((GCmd&0xff)==0xA0)
		{
			GState=2;
			GBufPos=0;
			GSerialBuffer=AdaptByte(GSerial[8*SerStep+5]);
		}
		if((GCmd&0xff)==0x40)
		{
			GState=2;
			GBufPos=0;
			GSerialBuffer=AdaptByte(GSerial[8*SerStep+6]);
		}
		if((GCmd&0xff)==0x80)
		{
			GState=2;
			GBufPos=0;
			GSerialBuffer=AdaptByte(GSerial[8*SerStep+7]);
		}
		GLastCmd=GCmd;
	}
}


void NaomiGameIDWrite(const WORD Data)
{
	int Dat=Data&0x01;
	int Clk=Data&0x02;
	int Rst=Data&0x04;
	int Sta=Data&0x08;
	int Cmd=Data&0x10;
	

	if(Rst)
	{
		GState=0;
		GBufPos=0;
	}

	
	if(Clk!=GOldClk && !Clk)	//Falling Edge clock
	{
		//State change
		if(GState==0 && Sta) 
			GState=1;		
		if(GState==1 && !Sta)
			GState=2;
		
		
		
		

		//State processing
		if(GState==1)		//LoadBoardID
		{
			GSerialBuffer=BoardID;
			GBufPos=0;		//??
		}
		if(GState==2)		//ShiftBoardID
			GBufPos++;

		if(GControl!=Cmd && !Cmd)
		{
			NaomiGameIDProcessCmd();
		}
		GControl=Cmd;
	}
	if(Clk!=GOldClk && Clk)	//Rising Edge clock
	{
		if(Cmd)	//Command mode
		{
			GCmd<<=1;
			if(Dat)
				GCmd|=1;
			else
				GCmd&=0xfffffffe;
			GControl=Cmd;
		}
		
	}

	GOldClk=Clk;

}

WORD NaomiGameIDRead()
{
	return (GSerialBuffer&(1<<(31-GBufPos)))?1:0;
}




u32  _ReadMem_naomi(u32 Addr, u32 sz)
{
	verify(sz!=1);

	dlog("naomi?WTF? ReadMem: %X, %d\n", Addr, sz);
	return 1;

}
void _WriteMem_naomi(u32 Addr, u32 data, u32 sz)
{
	dlog("naomi?WTF? WriteMem: %X <= %X, %d\n", Addr, data, sz);
}


//DIMM board
//Uses interrupt ext#3  (holly_EXT_PCI)

//status/flags ? 0x1 is some completion/init flag(?), 0x100 is the interrupt disable flag (?)
//n1 bios rev g (n2/epr-23605b has similar behavior of not same):
//3c=0x1E03
//40=0
//44=0
//48=0
//read 4c
//wait for 4c not 0
//4c=[4c]-1

//Naomi 2 bios epr-23609
//read 3c
//wait 4c to be non 0
//

//SO the writes to 3c/stuff are not relaced with 4c '1'
//If the dimm board has some internal cpu/pic logic 
//4c '1' seems to be the init done bit (?)
//n1/n2 clears it after getting a non 0 value
//n1 bios writes the value -1, meaning it expects the bit 0 to be set
//.//

u32 reg_dimm_3c;	//IO window ! writen, 0x1E03 some flag ?
u32 reg_dimm_40;	//parameters
u32 reg_dimm_44;	//parameters
u32 reg_dimm_48;	//parameters

u32 reg_dimm_4c=0x11;	//status/control reg ?

void naomi_process(u32 r3c,u32 r40,u32 r44, u32 r48)
{
	dlog("Naomi process 0x%04X 0x%04X 0x%04X 0x%04X\n",r3c,r40,r44,r48);
	dlog("Possible format 0 %d 0x%02X 0x%04X\n",r3c>>15,(r3c&0x7e00)>>9,r3c&0x1FF);
	dlog("Possible format 1 0x%02X 0x%02X\n",(r3c&0xFF00)>>8,r3c&0xFF);

	u32 param=(r3c&0xFF);
	if (param==0xFF)
	{
		dlog("invalid opcode or smth ?");
	}
	static int opcd=0;
	//else if (param!=3)
	if (opcd<255)
	{
		reg_dimm_3c=0x8000 | (opcd%12<<9) | (0x0);
		dlog("new reg is 0x%X\n",reg_dimm_3c);
		asic_RaiseInterrupt(holly_EXP_PCI);
		dlog("Interrupt raised\n");
		opcd++;
	}
}
u32  ReadMem_naomi(u32 Addr, u32 sz)
{
	verify(sz!=1);
//	dlog("+naomi?WTF? ReadMem: %X, %d\n", Addr, sz);
	switch(Addr&255)
	{
	case 0x3c:
		dlog("naomi GD? READ: %X, %d\n", Addr, sz);
		return reg_dimm_3c;
	case 0x40:
		dlog("naomi GD? READ: %X, %d\n", Addr, sz);
		return reg_dimm_40;
	case 0x44:
		dlog("naomi GD? READ: %X, %d\n", Addr, sz);
		return reg_dimm_44;
	case 0x48:
		dlog("naomi GD? READ: %X, %d\n", Addr, sz);
		return reg_dimm_48;

		//These are known to be valid on normal ROMs and DIMM board
	case NAOMI_ROM_OFFSETH_addr&255:
		return RomPioOffset>>16;

	case NAOMI_ROM_OFFSETL_addr&255:
		return RomPioOffset&0xFFFF;

	case NAOMI_ROM_DATA_addr&255:
		u32 rv;
		if (RomPtr)
			rv=*(u16*)&RomPtr[RomPioOffset&0x0FFFffff];
		else
			rv=0;
		RomPioOffset+=2;
		return rv;

	case NAOMI_DMA_COUNT_addr&255:
		return (WORD) DmaCount;

	case NAOMI_BOARDID_READ_addr&255:
		return NaomiGameIDRead()?0x8000:0x0000;
	
		//What should i do to emulate 'nothing' ?
	case NAOMI_COMM_OFFSET_addr&255:
		#ifdef NAOMI_COMM
		dlog("naomi COMM offs READ: %X, %d\n", Addr, sz);
		return CommOffset;
		#endif
	case NAOMI_COMM_DATA_addr&255:
		#ifdef NAOMI_COMM
		dlog("naomi COMM data read: %X, %d\n", CommOffset, sz);
		if (CommSharedMem)
		{
			return CommSharedMem[CommOffset&0xF];
		}
		#endif
		return 1;


		//This should be valid
	case NAOMI_DMA_OFFSETH_addr&255:
		return DmaOffset>>16;
	case NAOMI_DMA_OFFSETL_addr&255:
		return DmaOffset&0xFFFF;

	case NAOMI_BOARDID_WRITE_addr&255:
		dlog("naomi ReadMem: %X, %d\n", Addr, sz);
		return 1;

	case 0x04C:
		dlog("naomi GD? READ: %X, %d\n", Addr, sz);
		return reg_dimm_4c;

	case 0x18:
		dlog("naomi reg 0x18 : returning random data\n");
		return 0x4000^rand();
		break;

	default: break;
	}
	dlog("naomi?WTF? ReadMem: %X, %d\n", Addr, sz);
	return 0;

}
void WriteMem_naomi(u32 Addr, u32 data, u32 sz)
{
//	dlog("+naomi WriteMem: %X <= %X, %d\n", Addr, data, sz);
	switch(Addr&255)
	{
	case 0x3c:
		 if (0x1E03==data)
		 {
			 /*
			 if (!(reg_dimm_4c&0x100))
				asic_RaiseInterrupt(holly_EXP_PCI);
			 reg_dimm_4c|=1;*/
		 }
		 reg_dimm_3c=data;
		 dlog("naomi GD? Write: %X <= %X, %d\n", Addr, data, sz);
		 return;

	case 0x40:
		reg_dimm_40=data;
		dlog("naomi GD? Write: %X <= %X, %d\n", Addr, data, sz);
		return;
	case 0x44:
		reg_dimm_44=data;
		dlog("naomi GD? Write: %X <= %X, %d\n", Addr, data, sz);
		return;
	case 0x48:
		reg_dimm_48=data;
		dlog("naomi GD? Write: %X <= %X, %d\n", Addr, data, sz);
		return;

	case 0x4C:
		if (data&0x100)
		{
			asic_CancelInterrupt(holly_EXP_PCI);
			naomi_updates=100;
		}
		else if ((data&1)==0)
		{
			/*FILE* ramd=fopen("c:\\ndc.ram.bin","wb");
			fwrite(mem_b.data,1,RAM_SIZE,ramd);
			fclose(ramd);*/
			naomi_process(reg_dimm_3c,reg_dimm_40,reg_dimm_44,reg_dimm_48);
		}
		reg_dimm_4c=data&~0x100;
		dlog("naomi GD? Write: %X <= %X, %d\n", Addr, data, sz);
		return;

		//These are known to be valid on normal ROMs and DIMM board
	case NAOMI_ROM_OFFSETH_addr&255:
		RomPioOffset&=0x0000ffff;
		RomPioOffset|=(data<<16)&0x7fff0000;
		return;

	case NAOMI_ROM_OFFSETL_addr&255:
		RomPioOffset&=0xffff0000;
		RomPioOffset|=data;
		return;

	case NAOMI_ROM_DATA_addr&255:
		dlog("naomi WriteMem:Write to rom ? sure ? no , i dont think so %%) %X <= %X, %d\n", Addr, data, sz);
		return;

	case NAOMI_DMA_OFFSETH_addr&255:
		DmaOffset&=0x0000ffff;
		DmaOffset|=(data&0x7fff)<<16;
		return;

	case NAOMI_DMA_OFFSETL_addr&255:
		DmaOffset&=0xffff0000;
		DmaOffset|=data;
		return;

	case NAOMI_DMA_COUNT_addr&255:
		{
			DmaCount=data;
		}
		return;
	case NAOMI_BOARDID_WRITE_addr&255:
		NaomiGameIDWrite((u16)data);
		return;

		//What should i do to emulate 'nothing' ?
	case NAOMI_COMM_OFFSET_addr&255:
#ifdef NAOMI_COMM
		dlog("naomi COMM ofset Write: %X <= %X, %d\n", Addr, data, sz);
		CommOffset=data&0xFFFF;
#endif
		return;

	case NAOMI_COMM_DATA_addr&255:
		#ifdef NAOMI_COMM
		dlog("naomi COMM data Write: %X <= %X, %d\n", CommOffset, data, sz);
		if (CommSharedMem)
		{
			CommSharedMem[CommOffset&0xF]=data;
		}
		#endif
		return;

		//This should be valid
	case NAOMI_BOARDID_READ_addr&255:
		dlog("naomi WriteMem: %X <= %X, %d\n", Addr, data, sz);
		return;

	default: break;
	}
	dlog("naomi?WTF? WriteMem: %X <= %X, %d\n", Addr, data, sz);
}



u32 NAOMI_ROM_OFFSETH;
u32 NAOMI_ROM_OFFSETL;
u32 NAOMI_ROM_DATA;
u32 NAOMI_DMA_OFFSETH;
u32 NAOMI_DMA_OFFSETL;
u32 NAOMI_DMA_COUNT;
u32 NAOMI_BOARDID_WRITE;
u32 NAOMI_BOARDID_READ;
u32 NAOMI_COMM_OFFSET;
u32 NAOMI_COMM_DATA;

char SelectedFile[512];

OPENFILENAME ofn;



bool naomi_LoadRom(char* file)
{
	
	dlog("\nnullDC-Naomi rom loader v1.2\n");

	size_t folder_pos=wcslen(file)-1;
	while(folder_pos>1 && file[folder_pos]!='\\')
		folder_pos--;

	folder_pos++;

	char t[512];
	wcscpy(t,file);
	FILE* fl=_tfopen(t,L"r");
	if (!fl)
		return false;

	char* line=fgetws(t,512,fl);
	if (!line)	
	{
		fclose(fl);
		return false;
	}

	char* eon=wcswcs(line,L"\n");
	if (!eon)
		wprintf(L"+Loading naomi rom that has no name\n",line);
	else
		*eon=0;

	wprintf(L"+Loading naomi rom : %s\n",line);

	line=fgetws(t,512,fl);
	if (!line)	
	{
		fclose(fl);
		return false;
	}

	vector<wstring> files;
	vector<u32> fstart;
	vector<u32> fsize;

	u32 setsize=0;
	u32 max_load_addr=0;

	while(line)
	{
		char filename[512];
		u32 addr,sz;
		swscanf(line,L"\"%[^\"]\",%x,%x",filename,&addr,&sz);
		files.push_back(filename);
		fstart.push_back(addr);
		fsize.push_back(sz);
		setsize+=sz;
		max_load_addr=max(max_load_addr,(addr+sz));
		line=fgetws(t,512,fl);
	}
	fclose(fl);

	dlog("+%d romfiles, %.2f MB set size, %.2f MB set address space\n",files.size(),setsize/1024.f/1024.f,max_load_addr/1024.f/1024.f);

	if (RomCacheMap)
	{
		RomCacheMapCount=0;
		delete RomCacheMap;
	}

	RomCacheMapCount = (u32)files.size();
	RomCacheMap = new HANDLE[files.size()];

	wcscpy(t,file);
	t[folder_pos]=0;
	wcscat(t,L"ndcn-composed.cache");

	//Allocate space for the ram, so we are sure we have a segment of continius ram
	RomPtr=(u8*)VirtualAlloc(0,max_load_addr,MEM_RESERVE,PAGE_NOACCESS);
	verify(RomPtr!=0);

	wcscpy(t,file);
	
	//Create File Mapping Objects
	for (size_t i=0;i<files.size();i++)
	{
		t[folder_pos]=0;
		wcscat(t,files[i].c_str());
		HANDLE RomCache;
		
		if (wcscmp(files[i].c_str(),L"null")==0)
		{
			RomCacheMap[i]=INVALID_HANDLE_VALUE;
			continue;
		}

		RomCache=CreateFile(t,FILE_READ_ACCESS,FILE_SHARE_READ,0,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,0);
		
		if (RomCache==INVALID_HANDLE_VALUE)
		{
			wprintf(L"-Unable to read file %s\n",files[i].c_str());
			RomCacheMap[i]=INVALID_HANDLE_VALUE;
			continue;
		}


		RomCacheMap[i]=CreateFileMapping(RomCache,0,PAGE_READONLY,0,fsize[i],0);
		verify(RomCacheMap[i]!=INVALID_HANDLE_VALUE);
		wprintf(L"-Preparing \"%s\" at 0x%08X, size 0x%08X\n",files[i].c_str(),fstart[i],fsize[i]);
		
		CloseHandle(RomCache);
	}

	//We have all file mapping objects, we start to map the ram
	dlog("+Mapping ROM\n");
	
	//Release the segment we reserved so we can map the files there
	VirtualFree(RomPtr,0,MEM_RELEASE);

	//Map the files into the segment of the ram that was reserved
	for (size_t i=0;i<RomCacheMapCount;i++)
	{
		u8* RomDest=RomPtr+fstart[i];

		if (RomCacheMap[i]==INVALID_HANDLE_VALUE)
		{
			wprintf(L"-Reserving ram at 0x%08X, size 0x%08X\n",fstart[i],fsize[i]);
			LPVOID test = VirtualAlloc(RomDest,fsize[i],MEM_RESERVE,PAGE_NOACCESS);
			verify(0 != test);
		}
		else
		{
			wprintf(L"-Mapping \"%s\" at 0x%08X, size 0x%08X\n",files[i].c_str(),fstart[i],fsize[i]);
			if (RomDest!=MapViewOfFileEx(RomCacheMap[i],FILE_MAP_READ,0,0,fsize[i],RomDest))
			{
				dlog("-Mapping ROM FAILED\n");
				//unmap file
				return false;
			}
		}
	}

	//done :)
	dlog("\nMapped ROM Successfully !\n\n");
	
	
	return true;
}

bool NaomiSelectFile(void* handle)
{
	if(cfgLoadInt(L"Naomi", L"LoadDefaultImage",0)!=0)
	{
		cfgLoadStr(L"Naomi", L"DefaultImage", SelectedFile, L"default.lst");
		wprintf(L"Loading default image: %s\n", SelectedFile);
	}
	else
	{
		ZeroMemory(&ofn, sizeof(OPENFILENAME));
		ofn.lStructSize		= sizeof(OPENFILENAME);
		ofn.hInstance		= (HINSTANCE)GetModuleHandle(0);
		ofn.lpstrFile		= SelectedFile;
		ofn.nMaxFile		= MAX_PATH;
		ofn.lpstrFilter		= L"*.lst\0*.lst\0\0";
		ofn.nFilterIndex	= 0;
		ofn.hwndOwner		=(HWND)handle;
		ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

		if(GetOpenFileName(&ofn)<=0)
			return true;
	}

	if (!naomi_LoadRom(SelectedFile))
	{
		cfgSaveStr(L"emu",L"gamefile",L"naomi_bios");
	}
	else
	{
		cfgSaveStr(L"emu",L"gamefile",SelectedFile);
	}


	wprintf(L"EEPROM file : %s.eeprom\n",SelectedFile);

	return true;
}
//Dma Start
void Naomi_DmaStart(u32 data)
{
	if (SB_GDEN==0)
	{
		dlog("Invalid (NAOMI)GD-DMA start, SB_GDEN=0.Ingoring it.\n");
		return;
	}
	SB_GDST|=data&1;

	if (SB_GDST==1)
	{
		verify(1 == SB_GDDIR );
	
		SB_GDSTARD=SB_GDSTAR+SB_GDLEN;
		
		SB_GDLEND=SB_GDLEN;
		SB_GDST=0;
		WriteMemBlock_nommu_ptr(SB_GDSTAR,(u32*)(RomPtr+(DmaOffset&0x0FFFffff)),SB_GDLEN);

		asic_RaiseInterrupt(holly_GDROM_DMA);
	}
}


void Naomi_DmaEnable(u32 data)
{
	SB_GDEN=data&1;
	if (SB_GDEN==0 && SB_GDST==1)
	{
		dlog("(NAOMI)GD-DMA aborted\n");
		SB_GDST=0;
	}
}
void naomi_reg_Init()
{
	#ifdef NAOMI_COMM
	CommMapFile = CreateFileMapping(
		INVALID_HANDLE_VALUE,    // use paging file
		NULL,                    // default security 
		PAGE_READWRITE,          // read/write access
		0,                       // max. object size 
		0x1000*4,                // buffer size  
		L"Global\\nullDC_103_naomi_comm");                 // name of mapping object

	if (CommMapFile == NULL || CommMapFile==INVALID_HANDLE_VALUE) 
	{ 
		_tprintf(TEXT("Could not create file mapping object (%d).\nTrying to open existing one\n"), 	GetLastError());
		
		CommMapFile=OpenFileMapping(
                   FILE_MAP_ALL_ACCESS,   // read/write access
                   FALSE,                 // do not inherit the name
                   L"Global\\nullDC_103_naomi_comm");               // name of mapping object 
	}
	
	if (CommMapFile == NULL || CommMapFile==INVALID_HANDLE_VALUE) 
	{ 
		_tprintf(TEXT("Could not open existing file either\n"), 	GetLastError());
		CommMapFile=INVALID_HANDLE_VALUE;
	}
	else
	{
		dlog("NAOMI: Created \"Global\\nullDC_103_naomi_comm\"\n");
		CommSharedMem = (u32*) MapViewOfFile(CommMapFile,   // handle to map object
			FILE_MAP_ALL_ACCESS, // read/write permission
			0,                   
			0,                   
			0x1000*4);           

		if (CommSharedMem == NULL) 
		{ 
			_tprintf(TEXT("Could not map view of file (%d).\n"), 
				GetLastError()); 

			CloseHandle(CommMapFile);
			CommMapFile=INVALID_HANDLE_VALUE;
		}
		else
			dlog("NAOMI: Mapped CommSharedMem\n");
	}
	#endif
	NaomiInit();

	sb_regs[(SB_GDST_addr-SB_BASE)>>2].flags=REG_32BIT_READWRITE | REG_READ_DATA;
	sb_regs[(SB_GDST_addr-SB_BASE)>>2].writeFunction=Naomi_DmaStart;

	sb_regs[(SB_GDEN_addr-SB_BASE)>>2].flags=REG_32BIT_READWRITE | REG_READ_DATA;
	sb_regs[(SB_GDEN_addr-SB_BASE)>>2].writeFunction=Naomi_DmaEnable;
}
void naomi_reg_Term()
{
	#ifdef NAOMI_COMM
	if (CommSharedMem)
	{
		UnmapViewOfFile(CommSharedMem);
	}
	if (CommMapFile!=INVALID_HANDLE_VALUE)
	{
		CloseHandle(CommMapFile);
	}
	#endif
}
void naomi_reg_Reset(bool Manual)
{
}

void Update_naomi()
{
	/*
	if (naomi_updates>1)
	{
		naomi_updates--;
	}
	else if (naomi_updates==1)
	{
		naomi_updates=0;
		asic_RaiseInterrupt(holly_EXP_PCI);
	}*/
#if 0
	if(!(SB_GDST&1) || !(SB_GDEN &1))
		return;

	//SB_GDST=0;

	//TODO : Fix dmaor
	u32 dmaor	= DMAC_DMAOR.full;

	u32	src		= SB_GDSTARD,
		len		= SB_GDLEN-SB_GDLEND ;

	//len=min(len,(u32)32);
	// do we need to do this for gdrom dma ?
	if(0x8201 != (dmaor &DMAOR_MASK)) {
		dlog("\n!\tGDROM: DMAOR has invalid settings (%X) !\n", dmaor);
		//return;
	}
	if(len & 0x1F) {
		dlog("\n!\tGDROM: SB_GDLEN has invalid size (%X) !\n", len);
		return;
	}

	if(0 == len) 
	{
		dlog("\n!\tGDROM: Len: %X, Abnormal Termination !\n", len);
	}
	u32 len_backup=len;
	if( 1 == SB_GDDIR ) 
	{
		WriteMemBlock_nommu_ptr(dst,NaomiRom+(DmaOffset&0x7ffffff),size);

		DmaCount=0xffff;
	}
	else
		msgboxf(L"GDROM: SB_GDDIR %X (TO AICA WAVE MEM?)",MBX_ICONERROR, SB_GDDIR);

	//SB_GDLEN = 0x00000000; //13/5/2k7 -> acording to docs these regs are not updated by hardware
	//SB_GDSTAR = (src + len_backup);

	SB_GDLEND+= len_backup;
	SB_GDSTARD+= len_backup;//(src + len_backup)&0x1FFFFFFF;

	if (SB_GDLEND==SB_GDLEN)
	{
		//dlog("Streamed GDMA end - %d bytes trasnfered\n",SB_GDLEND);
		SB_GDST=0;//done
		// The DMA end interrupt flag
		asic_RaiseInterrupt(holly_GDROM_DMA);
	}
	//Readed ALL sectors
	if (read_params.remaining_sectors==0)
	{
		u32 buff_size =read_buff.cache_size - read_buff.cache_index;
		//And all buffer :p
		if (buff_size==0)
		{
			verify(!SB_GDST&1)		
			gd_set_state(gds_procpacketdone);
		}
	}
#endif
}
#endif // BUILD_NAOMI