#pragma once

#include <xetypes.h>

extern volatile bool threaded_pvr;

void threaded_TADma(u32* data,u32 size);
void threaded_TASQ(u32* data);
void threaded_init();
void threaded_term();
