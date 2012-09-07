/****************************************************************************
 * Snes9x Nintendo Wii/Gamecube Port
 *
 * softdev July 2006
 * crunchy2 May 2007-July 2007
 * Michniewski 2008
 * Tantric 2008-2010
 *
 * freeze.cpp
 ***************************************************************************/

#include <malloc.h>
#include <xetypes.h>
#include <stdio.h>

#include "snes9xgx.h"
#include "fileop.h"
#include "filebrowser.h"
#include "menu.h"
#include "video.h"

#include "emu.h"

// 64 * 48 -- tiled
extern u8 * gameScreenPng;
extern u8 * gameScreenThumbnail;
extern int gameScreenPngSize;

/****************************************************************************
 * SaveSnapshot
 ***************************************************************************/

int
SaveSnapshot(char * filepath, bool silent)
{
	if(EmuSaveState(filepath) == 0)
		return 1;
	
	if (!silent)
		ErrorPrompt("Save failed!");
	
	return 0;

}

int
SaveSnapshotAuto(bool silent)
{
	ErrorPrompt("TODO");
	//char filepath[1024];
	//return SaveSnapshot(filepath, silent);
	return 0;
}

/****************************************************************************
 * LoadSnapshot
 ***************************************************************************/
int
LoadSnapshot(char * filepath, bool silent)
{
	if(EmuLoadState(filepath) == 0)
		return 1;

	if (!silent)
		ErrorPrompt("Unable to open snapshot!");
	return 0;

}

int
LoadSnapshotAuto(bool silent)
{
	ErrorPrompt("TODO");
//	char filepath[1024];
//	return LoadSnapshot(filepath, silent);
	return 0;
}
