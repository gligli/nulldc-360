/* 
 * File:   emu.cpp
 * Author: ced
 * 
 * Created on 7 septembre 2012, 11:52
 */

#include "emu.h"
#include "dc/dc.h"

#include <sys/iosupport.h>
#include <diskio/disc_io.h>

extern int nulldc_run(char * filename);
extern int nulldc_init();

char ROMFilename[2048] = {0};
char SaveDevice[2048] = {0};
char SaveFolder[2048] = {0};
char EmuFilename[2048] = {0};

int EmuConfigRequested = 0;
int EmuRunning = 0;

void EmuReset()
{
	Reset_DC(false);
}

void InitEmuVideo()
{
	
}

void EmuInit()
{
	nulldc_init();
}

void EmuStop()
{
	Stop_DC();
}

void EmuResume()
{
    Start_DC();
}

void EmuTerm()
{
 	Term_DC();
}

int EmuSaveState(char * filename)
{
	return -1;
}

int EmuLoadState(char * filename)
{
	return -1;
}

void EmuPrepareLaunch(char * filename)
{
	// set default device
	setDefaultDevice(3);
	
	// copy filename
	strcpy(EmuFilename, filename);
}

void EmuLaunch()
{	
    EmuRunning = 1;
    EmuConfigRequested = 0;

    EmuInit();
    nulldc_run(EmuFilename);
}
