/* 
 * File:   emu.h
 * Author: ced
 *
 * Created on 7 septembre 2012, 11:52
 */

#ifndef EMU_H
#define	EMU_H

// filename without dir and extention
extern char ROMFilename[2048];
extern char SaveDevice[2048];
extern char SaveFolder[2048];
extern char EmuFilename[2048];

extern int EmuConfigRequested;
extern int EmuResetRequested;
extern int EmuRunning;

void InitEmuVideo();
void EmuReset();
void EmuPrepareLaunch(char * filename);
void EmuLaunch();
void EmuInit();
int EmuSaveState(char * filename);
int EmuLoadState(char * filename);
void EmuStop();
void EmuResume();
void EmuTerm();

#endif	/* EMU_H */

