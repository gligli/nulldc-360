#pragma once

#include "drkPvr.h"

extern volatile bool threaded_pvr;

void threaded_Call(void (*call)());
void threaded_TADma(u32* data,u32 size);
void threaded_TASQ(u32* data);
void threaded_init();
void threaded_term();
