#pragma once

#include "nullDC/types.h"
#include <memory>
#include <math.h>

#include "MapleInterface.h"
#include "XInputBackend.h"


//////////////////////////////////////////////////////////////////////////
// should probably be moved to MapleInterface or smth
// Handy write funcs for DMA....needs rethinking (requires buffer_out_b in each DMA)
#define w32(data) *(u32*)buffer_out_b=(data);buffer_out_b+=4;buffer_out_len+=4
#define w16(data) *(u16*)buffer_out_b=(data);buffer_out_b+=2;buffer_out_len+=2
#define w8(data) *(u8*)buffer_out_b=(data);buffer_out_b+=1;buffer_out_len+=1
// write str to buffer, pad with 0x20 (space) if str < len
#define wString(str, len) for (u32 i = 0; i < len; ++i){if (str[i] != 0){w8((u8)str[i]);}else{while (i < len){w8(0x20);++i;}}}

#define DEBUG_LOG(p...) printf("[Maple] " p)

extern emu_info host;

//////////////////////////////////////////////////////////////////////////
// For recognizing which device the emu requests
enum
{
	ID_STDCONTROLLER,
	ID_TWINSTICK,
	ID_ARCADESTICK,
	ID_PURUPURUPACK,
	ID_MIC,
	ID_DREAMEYEMIC,
};

extern const char* deviceNames[];

struct xmaple_settings
{
	struct 
	{
		int Deadzone;		
	} Controller;
	
	struct 
	{
		bool UseRealFreq;
		int Length;
		int Intensity;
	} PuruPuru;	
};

void loadConfig();
void saveConfig();
