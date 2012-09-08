#pragma once
#include "types.h"

#include <emitter/emitter.h>

#define FRZERO 14
#define FRONE 15
#define RCYCLES 13
#define RPR 14
#define RPC 15
#define CPU_TIMESLICE	(BLOCKLIST_MAX_CYCLES)

//interface
void rec_Sh4_int_Run();
void rec_Sh4_int_Stop();
void rec_Sh4_int_Step();
void rec_Sh4_int_Skip();
void rec_Sh4_int_Reset(bool Manual);
void rec_Sh4_int_Init();
void rec_Sh4_int_Term();
bool rec_Sh4_int_IsCpuRunning();  
void rec_sh4_ResetCache();
void __fastcall rec_sh4_int_RaiseExeption(u32 ExeptionCode,u32 VectorAddress);

void DynaLookupMap(u32 start_pc, u32 end_pc,void * ppc_code);
void DynaLookupUnmap(u32 start_pc, u32 end_pc);
void DynaLookupReset();
