#pragma once

//#define THREADED_PVR

#include <xetypes.h>

void threaded_TADma(u32* data,u32 size);
void threaded_TASQ(u32* data);
void threaded_init();
void threaded_term();
