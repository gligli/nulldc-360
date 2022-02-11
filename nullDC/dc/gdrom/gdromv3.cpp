﻿/*
	gdrom, v3
	Overly complex implementation of a very ugly device
*/

#include "gdromv3.h"
#include "../../log/logging_interface.h"
#include "plugins/plugin_manager.h"
#include "dc/mem/sh4_mem.h"
#include "dc/mem/memutil.h"
#include "dc/mem/sb.h"
#include "dc/sh4/dmac.h"
#include "dc/sh4/intc.h"
#include "dc/sh4/sh4_registers.h"
#include "dc/asic/asic.h"
#include <byteswap.h>

enum gd_states
{
	//Generic
	gds_waitcmd,
	gds_procata,
	gds_waitpacket,
	gds_procpacket,
	gds_pio_send_data,
	gds_pio_get_data,
	gds_pio_end,
	gds_procpacketdone,

	//Command spec.
	gds_readsector_pio,
	gds_readsector_dma,
	gds_process_set_mode,
};

struct 
{
	u32 start_sector;
	u32 remaining_sectors;
	u32 sector_type;
} read_params;

struct packet_cmd_t
{
	u32 index;
	union
	{
		u16 data_16[6];
		u8 data_8[12];
		//Spi command structs
        struct 
        {
            u8 cc;

#ifdef XENON
            u8 head		: 1 ; //4 bytes, main cdrom header
            u8 subh		: 1 ; //8 bytes, mode2 subheader
            u8 data		: 1 ; //user data. 2048 for mode1, 2048 for m2f1, 2324 for m2f2
            u8 other	: 1 ; //"other" data. I guess that means SYNC/ECC/EDC ?
            //	u8 datasel	: 4 ;
            u8 expdtype	: 3 ;
            u8 prmtype	: 1 ;	
#else
            u8 prmtype	: 1 ;	
            u8 expdtype	: 3 ;
            //	u8 datasel	: 4 ;
            u8 other	: 1 ; //"other" data. I guess that means SYNC/ECC/EDC ?
            u8 data		: 1 ; //user data. 2048 for mode1, 2048 for m2f1, 2324 for m2f2
            u8 subh		: 1 ; //8 bytes, mode2 subheader
            u8 head		: 1 ; //4 bytes, main cdrom header
#endif

            u8 block[10];
        }GDReadBlock;
	};
};

packet_cmd_t packet_cmd;

//Buffer for sector reads [ dma ]
struct 
{
	u32 cache_index;
	u32 cache_size;
	u8 cache[2352*32];	//up to 32 sectors
} read_buff;

//pio buffer
struct 
{
	gd_states next_state;
	u32 index;
	u32 size;
	u16 data[0x10000>>1];	//64 kb
} pio_buff;

u32 set_mode_offset;
struct 
{
	u8 command;
} ata_cmd;

struct 
{
	bool playing;
	u32 repeats;
	union
	{
		u32 FAD;
		struct
		{
#ifdef XENON
			u8 res;
			u8 B2;		//lsb
			u8 B1;		//midle byte
			u8 B0;		//msb
#else
			u8 B0;		//msb
			u8 B1;		//midle byte
			u8 B2;		//lsb
			u8 res;
#endif
		};
	}CurrAddr,EndAddr,StartAddr;
} cdda;


gd_states gd_state;
DiscType gd_disk_type;
/*
	GD rom reset -> GDS_WAITCMD

	GDS_WAITCMD -> ATA/SPI command [Command code is on ata_cmd]
	SPI Command -> GDS_WAITPACKET -> GDS_SPI_* , depending on input

	GDS_SPI_READSECTOR -> Depending on features , it can do quite a few things
*/
u32 data_write_mode=0;

//Registers
	u32 DriveSel;
	GD_ErrRegT Error;
	GD_InterruptReasonT IntReason;
	GD_FeaturesT Features;
	GD_SecCountT SecCount;
	GD_SecNumbT SecNumber;
	
	GD_StatusT GDStatus;

	union
	{
		struct
		{
#ifdef XENON
		u8 hi;
		u8 low;
#else
		u8 low;
		u8 hi;
#endif
		};
		u16 full;
	} ByteCount;
	
//end

#define nilprintf(...)

//void nilprintf(...){}

#if 0
#define printf_rm printf
#define printf_ata printf
#define printf_spi printf
#define printf_spicmd printf
#define printf_subcode printf 
#else
#define printf_rm nilprintf
#define printf_ata nilprintf
#define printf_spi nilprintf
#define printf_spicmd nilprintf
#define printf_subcode nilprintf 
#endif
	
u32 FASTCALL gdrom_get_sector_type(const packet_cmd_t& cmd)
{
	if((cmd.GDReadBlock.head == 1) && (cmd.GDReadBlock.subh == 1) 
		&& (cmd.GDReadBlock.data == 1) && (cmd.GDReadBlock.expdtype == 3) && (cmd.GDReadBlock.other == 0))
		return 2340;
	else if( cmd.GDReadBlock.head || cmd.GDReadBlock.subh || cmd.GDReadBlock.other || (!cmd.GDReadBlock.data) )
	{
		dlog("GDROM: *FIXME* ADD MORE CD READ SETTINGS %d %d %d %d 0x%01X\n",
			cmd.GDReadBlock.head,cmd.GDReadBlock.subh,
			cmd.GDReadBlock.other,cmd.GDReadBlock.data,
			cmd.GDReadBlock.expdtype);
	}
	
	return 2048;
}
void FASTCALL gdrom_get_cdda(s16* sector)
{
	//silence ! :p
	if (cdda.playing)
	{
		if((cdda.CurrAddr.FAD == 0) && (cdda.StartAddr.FAD == 0) )
		{
			dlog("CDDA : Force-Shutup\n");
			cdda.playing=false;
			cdda.repeats = 0;
			SecNumber.Status=GD_STANDBY;
			goto cleanup_cdda;
		}

		libGDR.ReadSector((u8*)sector,cdda.CurrAddr.FAD,1,2352);
		cdda.CurrAddr.FAD++;
		if (cdda.CurrAddr.FAD>=cdda.EndAddr.FAD)
		{
			if (cdda.repeats <= 0)
			{
				//stop
				cdda.playing=false;
				SecNumber.Status=GD_STANDBY;
				
				return;
			}
			//else
			{
				//Repeat ;)
				cdda.repeats -= (cdda.repeats!=0xf);				
				cdda.CurrAddr.FAD=cdda.StartAddr.FAD;
			}
		}
		
		return;
	}
	
	cleanup_cdda:
	{
		register const u64 zeroReg = 0x0000000000000000;
		register u64* sec = (u64*)sector;
		register u32 steps = (2352 >> 3);

		while(steps--)
			*(sec++) = zeroReg;
	}
}

void gd_spi_pio_end(u8* buffer,u32 len,gd_states next_state=gds_pio_end);
void gd_process_spi_cmd();
void gd_process_ata_cmd();

void FillReadBuffer()
{
	u32 count = (read_params.remaining_sectors > 32) ? 32 : read_params.remaining_sectors;

	read_buff.cache_index=0;
	read_buff.cache_size=count*read_params.sector_type;

	libGDR.ReadSector(read_buff.cache,read_params.start_sector,count,read_params.sector_type);
	read_params.start_sector+=count;
	read_params.remaining_sectors-=count;
}

void gd_set_state(gd_states state)
{
	gd_states prev=gd_state;
	gd_state=state;
	switch(state)
	{
		case gds_waitcmd:
			GDStatus.DRDY=1;	//Can accept ata cmd :)
			GDStatus.BSY=0;		//Does not access command block
			break;

		case gds_procata:
			//verify(prev==gds_waitcmd);	//validate the previous cmd ;)

			GDStatus.DRDY=0;	//can't accept ata cmd
			GDStatus.BSY=1;		//accessing command block to process cmd
			gd_process_ata_cmd();
			break;

		case gds_waitpacket:
			verify(prev==gds_procata);	//validate the previous cmd ;)

			//prepare for packet cmd
			packet_cmd.index=0;

			//Set CoD, clear BSY and IO
			IntReason.CoD=1;
			GDStatus.BSY = 0;
			IntReason.IO=0;

			//Make DRQ valid
			GDStatus.DRQ = 1;

			//ATA can optionaly raise the interrupt ...
			//RaiseInterrupt(holly_GDROM_CMD);
			break;

		case gds_procpacket:
			verify(prev==gds_waitpacket);	//validate the previous state ;)

			GDStatus.DRQ=0;		//can't accept ata cmd
			GDStatus.BSY=1;		//accessing command block to process cmd
			gd_process_spi_cmd();
			break;
			//yep , get/set are the same !
		case gds_pio_get_data:
		case gds_pio_send_data:
			//	When preparations are complete, the following steps are carried out at the device.
			//(1)	Number of bytes to be read is set in "Byte Count" register. 
			ByteCount.full =(u16)(pio_buff.size<<1);
			//(2)	IO bit is set and CoD bit is cleared. 
			IntReason.IO=1;
			IntReason.CoD=0;
			//(3)	DRQ bit is set, BSY bit is cleared. 
			GDStatus.DRQ=1;
			GDStatus.BSY=0;
			//(4)	INTRQ is set, and a host interrupt is issued.
			asic_RaiseInterrupt(holly_GDROM_CMD);
			/*
			The number of bytes normally is the byte number in the register at the time of receiving 
			the command, but it may also be the total of several devices handled by the buffer at that point.
			*/
			break;

		case gds_readsector_pio:
			{
				/*
				If more data are to be sent, the device sets the BSY bit and repeats the above sequence 
				from step 7. 
				*/
				GDStatus.BSY=1;

				u32 sector_count = read_params.remaining_sectors;
				gd_states next_state=gds_pio_end;

				if (sector_count>27)
				{
					sector_count=27;
					next_state=gds_readsector_pio;
				}

				libGDR.ReadSector((u8*)&pio_buff.data[0],read_params.start_sector,sector_count,
											read_params.sector_type);
				read_params.start_sector+=sector_count;
				read_params.remaining_sectors-=sector_count;

				gd_spi_pio_end(0,sector_count*read_params.sector_type,next_state);
			}
			break;
			
		case gds_readsector_dma:
			FillReadBuffer();
			break;

		case gds_pio_end:
			
			GDStatus.DRQ=0;//all data is sent !

			gd_set_state(gds_procpacketdone);
			break;

		case gds_procpacketdone:
			/*
			7.	When the device is ready to send the status, it writes the 
			final status (IO, CoD, DRDY set, BSY, DRQ cleared) to the "Status" register before making INTRQ valid. 
			After checking INTRQ, the host reads the "Status" register to check the completion status. 
			*/
			//Set IO, CoD, DRDY
			GDStatus.DRDY=1;
			IntReason.CoD=1;
			IntReason.IO=1;

			//Clear DRQ,BSY
			GDStatus.DRQ=0;
			GDStatus.BSY=0;
			//Make INTRQ valid
			asic_RaiseInterrupt(holly_GDROM_CMD);

			//command finished !
			gd_set_state(gds_waitcmd);
			break;

		case gds_process_set_mode:
			memcpy(&reply_11[set_mode_offset],pio_buff.data ,pio_buff.size<<1);
			//end pio transfer ;)
			gd_set_state(gds_pio_end);
			break;
		default :
			die("Unhandled gdrom state ...");
			break;
	}
}


void gd_setdisc()
{
	DiscType newd = (DiscType)libGDR.GetDiscType();
	
	switch(newd)
	{
	case NoDisk:
		SecNumber.Status = GD_NODISC;
		//GDStatus.BSY=0;
		//GDStatus.DRDY=1;
		break;
	case Open:
		SecNumber.Status = GD_OPEN;
		//GDStatus.BSY=0;
		//GDStatus.DRDY=1;
		break;
	case Busy:
		SecNumber.Status = GD_BUSY;
		GDStatus.BSY=1;
		GDStatus.DRDY=0;
		break;
	default :
		if (SecNumber.Status==GD_BUSY)
			SecNumber.Status = GD_PAUSE;
		else
			SecNumber.Status = GD_STANDBY;
		//GDStatus.BSY=0;
		//GDStatus.DRDY=1;
		break;
	}
	if (gd_disk_type==Busy && newd!=Busy)
	{
		GDStatus.BSY=0;
		GDStatus.DRDY=1;
	}

	gd_disk_type=newd;

	SecNumber.DiscFormat=gd_disk_type>>4;
}

void gd_reset_containers()
{
	memset(&GDStatus,0,sizeof(GDStatus));
	memset(&SecNumber,0,sizeof(SecNumber));
	memset(&Features,0,sizeof(Features));
	memset(&packet_cmd,0,sizeof(packet_cmd));
	memset(&pio_buff,0,sizeof(pio_buff));
	memset(&ata_cmd,0,sizeof(ata_cmd));
	memset(&cdda,0,sizeof(cdda));
	memset(&read_buff,0,sizeof(read_buff));
	memset(&read_params,0,sizeof(read_params));
	gd_disk_type = Open;
}

void gd_reset()
{
	//Reset the drive
	gd_reset_containers();
	gd_setdisc();
	gd_set_state(gds_waitcmd);
}

u32 GetFAD(u8* data,bool msf)
{
	if( msf )	
	{
		dlog("GDROM: MSF FORMAT\n");
		return (u32)((data[0]*60*75) + (data[1]*75) + (data[2]));
	}

	return (u32)((data[0]<<16) | (data[1]<<8) | (data[2])) ;
}
//disk changes ect
void FASTCALL NotifyEvent_gdrom(u32 info,void* param)
{
	if (info == DiskChange)
		gd_setdisc();
}

//This handles the work of setting up the pio regs/state :)
void gd_spi_pio_end(u8* buffer,u32 len,gd_states next_state)
{
	verify(len<0xFFFF);
	pio_buff.index=0;
	pio_buff.size=len>>1;
	pio_buff.next_state=next_state;
	if (buffer!=0)
		memcpy(pio_buff.data,buffer,len);
	if (len==0)
		gd_set_state(next_state);
	else
		gd_set_state(gds_pio_send_data);
}
void gd_spi_pio_read_end(u32 len,gd_states next_state)
{
	verify(len<0xFFFF);
	pio_buff.index=0;
	pio_buff.size=len>>1;
	pio_buff.next_state=next_state;
	if (len==0)
		gd_set_state(next_state);
	else
		gd_set_state(gds_pio_get_data);
}
void gd_process_ata_cmd()
{
	//Any ata cmd clears these bits , unless aborted/error :p
	Error.ABRT=0;
	GDStatus.CHECK=0;

	switch(ata_cmd.command)
	{
	case ATA_NOP:
		printf_ata("ATA_NOP\n");
		/*
			Setting "abort" in the error register 
			Setting an error in the status register 
			Clearing "busy" in the status register 
			Asserting the INTRQ signal
		*/

		//this is all very hacky, i don't know if the abort is correct actually
		//the above comment is from a wrong place in the docs ...
		
		Error.ABRT=1;
		//Error.Sense=0x00; //fixme ?
		GDStatus.BSY=0;
		GDStatus.CHECK=1;

		asic_RaiseInterrupt(holly_GDROM_CMD);
		gd_set_state(gds_waitcmd);
		break;

	case ATA_SOFT_RESET:
		{
			printf_ata("ATA_SOFT_RESET\n");
			//DRV -> preserved -> wtf is it anyway ?
			gd_reset();
		}
		break;

	case ATA_EXEC_DIAG:
		printf_ata("ATA_EXEC_DIAG\n");
		dlog("ATA_EXEC_DIAG -- not implemented\n");
		break;

	case ATA_SPI_PACKET:
		printf_ata("ATA_SPI_PACKET\n");
		gd_set_state(gds_waitpacket);
		break;

	case ATA_IDENTIFY_DEV:
		printf_ata("ATA_IDENTIFY_DEV\n");
		gd_spi_pio_end((u8*)&reply_a1[packet_cmd.data_8[2]>>1],packet_cmd.data_8[4]);
		break;

	case ATA_SET_FEATURES:
		printf_ata("ATA_SET_FEATURES\n");

		//Set features sets :
		//Error : ABRT
		Error.ABRT=0;	//command was not aborted ;) [hopefully ...]

		//status : DRDY , DSC , DF , CHECK
		//DRDY is set on state change
		GDStatus.DSC=0;
		GDStatus.DF=0;
		GDStatus.CHECK=0;
		asic_RaiseInterrupt(holly_GDROM_CMD);  //???
		gd_set_state(gds_waitcmd);
		break;

	default:
		die("unknown ATA command..");
		break;
	};
}

void gd_process_spi_cmd()
{
	printf_spi("SPI cmd %02x;",packet_cmd.data_8[0]);
	printf_spi("params: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x \n",
		packet_cmd.data_8[0], packet_cmd.data_8[1], packet_cmd.data_8[2], packet_cmd.data_8[3], packet_cmd.data_8[4], packet_cmd.data_8[5],
		packet_cmd.data_8[6], packet_cmd.data_8[7], packet_cmd.data_8[8], packet_cmd.data_8[9], packet_cmd.data_8[10], packet_cmd.data_8[11] );

	GDStatus.CHECK=0;

	switch(packet_cmd.data_8[0])
	{
	case SPI_TEST_UNIT	:
		printf_spicmd("SPI_TEST_UNIT\n");

		GDStatus.CHECK=SecNumber.Status==GD_BUSY;	//drive is ready ;)

		gd_set_state(gds_procpacketdone);
		break;

	case SPI_REQ_MODE:
		printf_spicmd("SPI_REQ_MODE\n");
		gd_spi_pio_end((u8*)&reply_11[packet_cmd.data_8[2]>>1],packet_cmd.data_8[4]);
		break;

		/////////////////////////////////////////////////
		// *FIXME* CHECK FOR DMA, Diff Settings !?!@$#!@%
	case SPI_CD_READ:
		{
			read_params.start_sector = GetFAD(&packet_cmd.data_8[2],packet_cmd.GDReadBlock.prmtype);
			read_params.remaining_sectors = (u32)(packet_cmd.data_8[8]<<16) | (packet_cmd.data_8[9]<<8) | (packet_cmd.data_8[10]);
			read_params.sector_type = gdrom_get_sector_type(packet_cmd);

			printf_spicmd("SPI_CD_READ sec=%d sz=%d/%d dma=%d\n",read_params.start_sector,read_params.remaining_sectors,read_params.sector_type,Features.CDRead.DMA);

			gd_set_state( (Features.CDRead.DMA==1) ? gds_readsector_dma : gds_readsector_pio);
		}
		break;

	case SPI_GET_TOC:
		{
			u32 toc_gd[102];
			printf_spicmd("SPI_GET_TOC\n");
			//dlog("SPI_GET_TOC - %d\n",(packet_cmd.data_8[4]) | (packet_cmd.data_8[3]<<8) );
			
			//toc - dd/sd
			libGDR.GetToc(&toc_gd[0],packet_cmd.data_8[1]&0x1);
			gd_spi_pio_end((u8*)&toc_gd[0], (packet_cmd.data_8[4]) | (packet_cmd.data_8[3]<<8) );
		}
		break;

		//mount/map drive ? some kind of reset/unlock ??
		//seems like a non data command :)
	case 0x70:
		printf_spicmd("SPI : unknown ? [0x70]\n");
		dlog("SPI : unknown ? [0x70]\n");
		/*GDStatus.full=0x50; //FIXME
		RaiseInterrupt(holly_GDROM_CMD);*/

		gd_set_state(gds_procpacketdone);
		break;


	// Command 71 seems to trigger some sort of authentication check(?).
	// Update Sept 1st 2010: It looks like after a sequence of events the drive ends up having a specific state.
	// If the drive is fed with a "bootable" disc it ends up in "PAUSE" state. On all other cases it ends up in "STANDBY".
	// Cmd 70 and Error Handling / Sense also seem to take part in the above mentioned sequence of events.
	// This is more or less a hack until more info about this command becomes available. ~Psy
	case 0x71:
		{
			extern u32 reply_71_sz;
			printf_spicmd("SPI : unknown ? [0x71]\n");
			dlog("SPI : unknown ? [0x71]\n");
			
			gd_spi_pio_end((u8*)&reply_71[0],reply_71_sz);//uCount
			SecNumber.Status = ((libGDR.GetDiscType() == GdRom) || (libGDR.GetDiscType()==CdRom_XA)) ? GD_PAUSE : GD_STANDBY;
			/*if (libGDR.GetDiscType()==GdRom || libGDR.GetDiscType()==CdRom_XA)
				SecNumber.Status=GD_PAUSE;
			else
				SecNumber.Status=GD_STANDBY;*/
		}
		break;
	case SPI_SET_MODE:
		{
			printf_spicmd("SPI_SET_MODE\n");
			u32 Offset = packet_cmd.data_8[2];
			u32 Count = packet_cmd.data_8[4];
			verify((Offset+Count)<11);	//cant set write olny things :P
			set_mode_offset=Offset;
			gd_spi_pio_read_end(Count,gds_process_set_mode);
		}

		break;

	case SPI_CD_READ2:
		printf_spicmd("SPI_CD_READ2\n");
		dlog("GDROM: Unhandled Sega SPI frame: SPI_CD_READ2\n");

		gd_set_state(gds_procpacketdone);
		break;

		
	case SPI_REQ_STAT:
		{
			printf_spicmd("SPI_REQ_STAT\n");
			//dlog("GDROM: Unhandled Sega SPI frame: SPI_REQ_STAT\n");
			static u8 stat[10];

			//0	0	0	0	0	STATUS
			stat[0]=SecNumber.Status;	//low nibble 
			//1	Disc Format	Repeat Count
			stat[1]=(u8)((u8)(SecNumber.DiscFormat<<4) | (cdda.repeats));
			//2	Address	Control
			stat[2]=0x4;
			//3	TNO
			stat[3]=2;
			//4	X
			stat[4]=0;
			//5	FAD
			stat[5]=cdda.CurrAddr.B0;
			//6	FAD
			stat[6]=cdda.CurrAddr.B1;
			//7	FAD
			stat[7]=cdda.CurrAddr.B2;
			//8	Max Read Error Retry Times
			stat[8]=0;
			//9	0	0	0	0	0	0	0	0
			stat[9]=0;

			verify((packet_cmd.data_8[2]+packet_cmd.data_8[4])<11);
			gd_spi_pio_end(&stat[packet_cmd.data_8[2]],packet_cmd.data_8[4]);
		}
		break;

	case SPI_REQ_ERROR:
	{
		printf_spicmd("SPI_REQ_ERROR\n");
		dlog("GDROM: Unhandled Sega SPI frame: SPI_REQ_ERROR\n");
		
		u8 resp[10];
		resp[0]=0xF0;
		resp[1]=0;
		resp[2]= SecNumber.Status==GD_BUSY ? 2:0;//sense
		resp[3]=0;
		resp[4]=resp[5]=resp[6]=resp[7]=0; //Command Specific Information
		resp[8]=0;//Additional Sense Code
		resp[9]=0;//Additional Sense Code Qualifier

		gd_spi_pio_end(resp,packet_cmd.data_8[4]);
	}
	break;

	case SPI_REQ_SES:
	{
		printf_spicmd("SPI_REQ_SES\n");

		u8 ses_inf[6];
		libGDR.GetSessionInfo(ses_inf,packet_cmd.data_8[2]);
		ses_inf[0]=SecNumber.Status;
		gd_spi_pio_end((u8*)&ses_inf[0],packet_cmd.data_8[4]);
	}
	break;

	case SPI_CD_OPEN:
		printf_spicmd("SPI_CD_OPEN\n");
		dlog("GDROM: Unhandled Sega SPI frame: SPI_CD_OPEN\n");
		
		
		gd_set_state(gds_procpacketdone);
		break;

	case SPI_CD_PLAY:
		{
			printf_spicmd("SPI_CD_PLAY\n");
			dlog("GDROM: Unhandled Sega SPI frame: SPI_CD_PLAY\n");
			//cdda.CurrAddr.FAD=60000;

			cdda.playing=true;
			SecNumber.Status=GD_PLAY;

			u32 param_type=packet_cmd.data_8[1]&0x7;
			dlog("param_type=%d\n",param_type);
			if (param_type==1)
			{
				cdda.StartAddr.FAD=cdda.CurrAddr.FAD=GetFAD(&packet_cmd.data_8[2],0);
				cdda.EndAddr.FAD=GetFAD(&packet_cmd.data_8[8],0);
				GDStatus.DSC=1;	//we did the seek xD lol
			}
			else if (param_type==2)
			{
				cdda.StartAddr.FAD=cdda.CurrAddr.FAD=GetFAD(&packet_cmd.data_8[2],1);
				cdda.EndAddr.FAD=GetFAD(&packet_cmd.data_8[8],1);
				GDStatus.DSC=1;	//we did the seek xD lol
			}
			else if (param_type==7)
			{
				//Resume from previous pos :)
			}
			else
			{
				die("SPI_CD_SEEK  : not known parameter..");
			}
			cdda.repeats=packet_cmd.data_8[6]&0xF;
			dlog("cdda.StartAddr=%d\n",cdda.StartAddr.FAD);
			dlog("cdda.EndAddr=%d\n",cdda.EndAddr.FAD);
			dlog("cdda.repeats=%d\n",cdda.repeats);
			dlog("cdda.playing=%d\n",cdda.playing);
			dlog("cdda.CurrAddr=%d\n",cdda.CurrAddr.FAD);

			gd_set_state(gds_procpacketdone);
		}
		break;

	case SPI_CD_SEEK:
		{
			printf_spicmd("SPI_CD_SEEK\n");
			dlog("GDROM: Unhandled Sega SPI frame: SPI_CD_SEEK\n");

			SecNumber.Status=GD_PAUSE;
			cdda.playing=false;

			u32 param_type=packet_cmd.data_8[1]&0x7;
			dlog("param_type=%d\n",param_type);
			if (param_type==1)
			{
				cdda.StartAddr.FAD=cdda.CurrAddr.FAD=GetFAD(&packet_cmd.data_8[2],0);
				GDStatus.DSC=1;	//we did the seek xD lol
			}
			else if (param_type==2)
			{
				cdda.StartAddr.FAD=cdda.CurrAddr.FAD=GetFAD(&packet_cmd.data_8[2],1);
				GDStatus.DSC=1;	//we did the seek xD lol
			}
			else if (param_type==3)
			{
				//stop audio , goto home
				SecNumber.Status=GD_STANDBY;
				cdda.StartAddr.FAD=cdda.CurrAddr.FAD=150;
				GDStatus.DSC=1;	//we did the seek xD lol
			}
			else if (param_type==4)
			{
				//pause audio -- nothing more
			}
			else
			{
				die("SPI_CD_SEEK  : not known parameter..");
			}

			dlog("cdda.StartAddr=%d\n",cdda.StartAddr.FAD);
			dlog("cdda.EndAddr=%d\n",cdda.EndAddr.FAD);
			dlog("cdda.repeats=%d\n",cdda.repeats);
			dlog("cdda.playing=%d\n",cdda.playing);
			dlog("cdda.CurrAddr=%d\n",cdda.CurrAddr.FAD);


			gd_set_state(gds_procpacketdone);
		}
		break;

	case SPI_CD_SCAN:
		printf_spicmd("SPI_CD_SCAN\n");
		dlog("GDROM: Unhandled Sega SPI frame: SPI_CD_SCAN\n");
		

		gd_set_state(gds_procpacketdone);
		break;

	case SPI_GET_SCD:
		{
			printf_spicmd("SPI_GET_SCD\n");
			//dlog("\nGDROM:\tUnhandled Sega SPI frame: SPI_GET_SCD\n");

			u32 format;
			format=packet_cmd.data_8[1]&0xF;
			u32 sz;
			static u8 subc_info[100];


			//0	Reserved
			subc_info[0]=0;
			//1	Audio status
			if (SecNumber.Status==GD_STANDBY)
			{
				//13h	Audio playback ended normally
				subc_info[1]=0x13;
			}
			else if (SecNumber.Status==GD_PAUSE)
			{
				//12h	Audio playback paused
				subc_info[1]=0x12;
			}
			else if (SecNumber.Status==GD_PLAY)
			{
				//11h	Audio playback in progress
				subc_info[1]=0x11;
			}
			else
			{
				if (cdda.playing)
					subc_info[1]=0x11;//11h	Audio playback in progress
				else
					subc_info[1]=0x15;//15h	No audio status information
			}
			
			subc_info[1]=0x15;

			if (format==0)
			{
				sz=100;
				subc_info[2]=0;
				subc_info[3]=100;
				libGDR.ReadSubChannel(subc_info+4,0,96);
			}
			else
			{
				//2	DATA Length MSB (0 = 0h)
				subc_info[2]=0;
				//3	DATA Length LSB (14 = Eh)
				subc_info[3]=0xE;
				//4	Control	ADR
				subc_info[4]=(4<<4) | (1);	//Audio :p
				//5-13	DATA-Q
				u8* data_q=&subc_info[5-1];
				//-When ADR = 1
				//Byte	Description
				//1	TNO
				data_q[1]=1;//Track number .. duno whats it :P gota parse toc xD ;p
				//2	X
				data_q[2]=1;//gap #1 (main track)
				//3-5	Elapsed FAD within track
				//u32 FAD_el=cdda.CurrAddr.FAD-cdda.StartAddr.FAD;
				data_q[3]=0;//(u8)(FAD_el>>16);
				data_q[4]=0;//(u8)(FAD_el>>8);
				data_q[5]=0;//(u8)(FAD_el>>0);
				//6	0	0	0	0	0	0	0	0
				data_q[6]=0;//
				//7-9	-> seems to be FAD
				data_q[7]=0;//(u8)(cdda.CurrAddr.FAD>>16);
				data_q[8]=0x0;//(u8)(cdda.CurrAddr.FAD>>8);
				data_q[9]=0x96;//(u8)(cdda.CurrAddr.FAD>>0);
				sz=0xE;
				printf_subcode("NON raw subcode read -- partialy wrong [fmt=%d]\n",format);
			}

			gd_spi_pio_end((u8*)&subc_info[0],sz);
		}
		break;

	default:
		dlog("GDROM: Unhandled Sega SPI frame: %X\n", packet_cmd.data_8[0]);

		gd_set_state(gds_procpacketdone);
		break;
	}
}
//Read handler
u32 ReadMem_gdrom(u32 Addr, u32 sz)
{	
//	printf("ReadMem_gdrom %08x %d %02x\n",Addr,sz,GDStatus.full);
	switch (Addr)
	{
		//cancel interrupt
	case GD_STATUS_Read :
		asic_CancelInterrupt(holly_GDROM_CMD);	//Clear INTRQ signal
		printf_rm("GDROM: STATUS [cancel int](v=%X)\n",GDStatus.full);
		return GDStatus.full | (1<<4);

	case GD_ALTSTAT_Read:
		printf_rm("GDROM: Read From AltStatus (v=%X)\n",GDStatus.full);
		return GDStatus.full | (1<<4);

	case GD_BYCTLLO	:
		printf_rm("GDROM: Read From GD_BYCTLLO\n");
		return ByteCount.low;

	case GD_BYCTLHI	:
		printf_rm("GDROM: Read From GD_BYCTLHI\n");
		return ByteCount.hi;

	case GD_DATA:
		if(2!=sz)
			dlog("GDROM: Bad size on DATA REG Read\n");

		if (gd_state == gds_pio_send_data)
		{
			if (pio_buff.index==pio_buff.size)
				dlog("GDROM: Illegal Read From DATA (underflow)\n");
			else
			{
				u32 rv= pio_buff.data[pio_buff.index];
				pio_buff.index+=1;
				ByteCount.full-=2;
				if (pio_buff.index==pio_buff.size)
				{
					verify(pio_buff.next_state != gds_pio_send_data);
					//end of pio transfer !
					gd_set_state(pio_buff.next_state);
				}
				return rv;
			}

		}
		else
			dlog("GDROM: Illegal Read From DATA (wrong mode)\n");

		return 0;

	case GD_DRVSEL:
		printf_rm("GDROM: Read From DriveSel\n");
		return DriveSel;

	case GD_ERROR_Read:
		printf_rm("GDROM: Read from ERROR Register\n");
		return Error.full;

	case GD_IREASON_Read:
		printf_rm("GDROM: Read from INTREASON Register\n");
		return IntReason.full;

	case GD_SECTNUM:
		printf_rm("GDROM: Read from SecNumber Register (v=%X)\n", SecNumber.full);
		return SecNumber.full;

	default:
		dlog("GDROM: Unhandled Read From %X sz:%X\n",Addr,sz);
		return 0;
	}
}

//Write Handler
void WriteMem_gdrom(u32 Addr, u32 data, u32 sz)
{
//	printf("WriteMem_gdrom %08x %08x %d\n",Addr,data,sz);
	switch(Addr)
	{
	case GD_BYCTLLO:
		printf_rm("GDROM: Write to GD_BYCTLLO = %X sz:%X\n",data,sz);
		ByteCount.low =(u8) data;
		break;

	case GD_BYCTLHI: 
		printf_rm("GDROM: Write to GD_BYCTLHI = %X sz:%X\n",data,sz);
		ByteCount.hi =(u8) data;
		break;

	case GD_DATA: 
		{
			if(2!=sz)
				dlog("GDROM: Bad size on DATA REG\n");
			if (gd_state == gds_waitpacket)
			{
				packet_cmd.data_16[packet_cmd.index]=bswap_16((u16)data);
				packet_cmd.index+=1;
				if (packet_cmd.index==6)
					gd_set_state(gds_procpacket);
			}
			else if (gd_state == gds_pio_get_data)
			{
				pio_buff.data[pio_buff.index]=(u16)data;
				pio_buff.index+=1;
				if (pio_buff.size==pio_buff.index)
				{
					verify(pio_buff.next_state!=gds_pio_get_data);
					gd_set_state(pio_buff.next_state);
				}
			}
			else
				dlog("GDROM: Illegal Write to DATA\n");
			return;
		}
	case GD_DEVCTRL_Write:
		dlog("GDROM: Write GD_DEVCTRL ( Not implemented on dc)\n");
		break;

	case GD_DRVSEL: 
		dlog("GDROM: Write to GD_DRVSEL\n");
		DriveSel = data; 
		break;


		//	By writing "3" as Feature Number and issuing the Set Feature command,
		//	the PIO or DMA transfer mode set in the Sector Count register can be selected.
		//	The actual transfer mode is specified by the Sector Counter Register. 

	case GD_FEATURES_Write:
		printf_rm("GDROM: Write to GD_FEATURES\n");
		Features.full =(u8) data;
		break;

	case GD_SECTCNT_Write:
		dlog("GDROM: Write to SecCount = %X\n", data);
		SecCount.full =(u8) data;
		break;

	case GD_SECTNUM	:
		dlog("GDROM: Write to SecNum; not possible = %X\n", data);
		break;

	case GD_COMMAND_Write:
	{
		verify(sz==1);

		if ((data !=ATA_NOP) && (data != ATA_SOFT_RESET))
			verify(gd_state==gds_waitcmd);

		//dlog("\nGDROM:\tCOMMAND: %X !\n", data);
		ata_cmd.command=(u8)data;
		gd_set_state(gds_procata);
	}
	break;

	default:
		dlog("\nGDROM:\tUnhandled Write to %X <= %X sz:%X\n",Addr,data,sz);
		break;
	}
}

//is this needed ?
void UpdateGDRom()
{
	if(!(SB_GDST&1) || !(SB_GDEN &1) || (read_buff.cache_size==0 && read_params.remaining_sectors==0))
		return;

	//SB_GDST=0;

	//TODO : Fix dmaor
	u32 dmaor	= DMAC_DMAOR.full;

	u32	src		= SB_GDSTARD,
		len		= SB_GDLEN-SB_GDLEND ;
	
	if(SB_GDLEN & 0x1F) 
	{
		dlog("\n!\tGDROM: SB_GDLEN has invalid size (%X) !\n", len);
		return;
	}

	//if we don't have any more sectors to read
	if (read_params.remaining_sectors==0)
	{
		//make sure we don't underrun the cache :)
		len=min(len,read_buff.cache_size);
	}

	len=min(len,(u32)32);
	// do we need to do this for gdrom dma ?
	if(0x8201 != (dmaor &DMAOR_MASK)) {
		dlog("\n!\tGDROM: DMAOR has invalid settings (%X) !\n", dmaor);
		//return;
	}

	if(0 == len) 
		dlog("\n!\tGDROM: Len: %X, Abnormal Termination !\n", len);

	u32 len_backup=len;

	if( 1 == SB_GDDIR ) 
	{
		while(len)
		{
			u32 buff_size =read_buff.cache_size;
			if (buff_size==0)
			{
				verify(read_params.remaining_sectors>0);
				//buffer is empty , fill it :)
				FillReadBuffer();
			}

			//transfer up to len bytes
			if (buff_size>len)
			{
				buff_size=len;
			}
			WriteMemBlock_nommu_ptr(src,(u32*)&read_buff.cache[read_buff.cache_index], buff_size);
			read_buff.cache_index+=buff_size;
			read_buff.cache_size-=buff_size;
			src+=buff_size;
			len-=buff_size;
		}
	}
	else
		msgboxf("GDROM: SB_GDDIR %X (TO AICA WAVE MEM?)",MBX_ICONERROR, SB_GDDIR);

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
		//And all buffer :p
		if (read_buff.cache_size==0)
		{
			//verify(!SB_GDST&1)	-> dc can do multi read dma
			gd_set_state(gds_procpacketdone);
		}
	}
}
//Dma Start
void GDROM_DmaStart(u32 data)
{
	if (SB_GDEN==0)
	{
		dlog("Invalid GD-DMA start, SB_GDEN=0.Ingoring it.\n");
		return;
	}
	SB_GDST|=data&1;

	if (SB_GDST==1)
	{
		SB_GDSTARD=SB_GDSTAR;
		SB_GDLEND=0;
		//dlog("Streamed GDMA start\n");
		UpdateGDRom();
	}
}


void GDROM_DmaEnable(u32 data)
{
	SB_GDEN=data&1;
	if (SB_GDEN==0 && SB_GDST==1)
	{
		printf_spi("GD-DMA aborted\n");
		SB_GDST=0;
	}
}
//Init/Term/Res
void gdrom_reg_Init()
{
	sb_regs[(u32)(SB_GDST_addr-SB_BASE)>>2].flags=REG_32BIT_READWRITE | REG_READ_DATA;
	sb_regs[(u32)(SB_GDST_addr-SB_BASE)>>2].writeFunction=GDROM_DmaStart;

	sb_regs[(u32)(SB_GDEN_addr-SB_BASE)>>2].flags=REG_32BIT_READWRITE | REG_READ_DATA;
	sb_regs[(u32)(SB_GDEN_addr-SB_BASE)>>2].writeFunction=GDROM_DmaEnable;
}
void gdrom_reg_Term()
{
	
}

void gdrom_reg_Reset(bool Manual)
{
	//huh?
}