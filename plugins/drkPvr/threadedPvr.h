#pragma once

#include "drkPvr.h"

extern volatile bool threaded_pvr;

void threaded_Wait(bool ta,bool render,bool set_srcp,bool set_ercp);
void threaded_TADma(u32* data,u32 size);
void threaded_TASQ(u32* data);
void threaded_init();
void threaded_term();
