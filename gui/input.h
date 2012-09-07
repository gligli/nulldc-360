/****************************************************************************
 * libwiigui Template
 * Tantric 2009
 * Modified by Ced2911, 2011
 *
 * input.h
 * Wii/GameCube controller management
 ***************************************************************************/

#ifndef _INPUT_H_
#define _INPUT_H_
#include "w_input.h"
#include <input/input.h>

#define PI 				3.14159265f
#define PADCAL			50
#define MAXJP 			12 // # of mappable controller buttons

extern u32 btnmap[4][4][12];
extern int rumbleRequest[4];

void ResetControls(int cc = -1, int wc = -1);
void ShutoffRumble();
void DoRumble(int i);
void ReportButtons ();
void SetControllers ();
void SetDefaultButtonMap ();
bool MenuRequested();
void SetupPads();
void UpdatePads();

#endif
