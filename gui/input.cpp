/****************************************************************************
 * libwiigui Template
 * Tantric 2009
 * Modified by Ced2911, 2011
 *
 * input.cpp
 * Wii/GameCube controller management
 ***************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <input/input.h>
#include <usb/usbmain.h>
#include <xetypes.h>
//#include "menu.h"
#include "video.h"
#include "input.h"
#include "gui/gui.h"

#include "snes9xgx.h"
#include "button_mapping.h"


extern "C" void doScreenCapture();
extern "C" void enableCapture();

#define MAX_INPUTS 4

int rumbleRequest[4] = {0, 0, 0, 0};
GuiTrigger userInput[4];
//static int rumbleCount[4] = {0, 0, 0, 0};


static WPADData wpad_xenon[MAX_INPUTS];

static struct controller_data_s ctrl[MAX_INPUTS];
static struct controller_data_s old_ctrl[MAX_INPUTS];

#define ASSIGN_BUTTON_TRUE( keycode, snescmd ) \
	  S9xMapButton( keycode, cmd = S9xGetCommandT(snescmd), true)

#define ASSIGN_BUTTON_FALSE( keycode, snescmd ) \
	  S9xMapButton( keycode, cmd = S9xGetCommandT(snescmd), false)

#define	STICK_DEAD_ZONE (13107)
#define HANDLE_STICK_DEAD_ZONE(x) ((((x)>-STICK_DEAD_ZONE) && (x)<STICK_DEAD_ZONE)?0:(x-x/abs(x)*STICK_DEAD_ZONE))

static void XenonInputInit()
{
	memset(ctrl, 0, MAX_INPUTS * sizeof (controller_data_s));
	memset(old_ctrl, 0, MAX_INPUTS * sizeof (controller_data_s));

	memset(wpad_xenon, 0, MAX_INPUTS * sizeof (WPADData));
}

#define PUSHED(x) ((ctrl[ictrl].x)&&(old_ctrl[ictrl].x==0))
#define RELEASED(x) ((ctrl[ictrl].x==0)&&(old_ctrl[ictrl].x==1))
#define HELD(x) ((ctrl[ictrl].x==1))

static uint16_t WPAD_ButtonsDown(int ictrl)
{
	uint16_t btn = 0;

	if (PUSHED(a)) {
		btn |= WPAD_CLASSIC_BUTTON_A;
	}

	if (PUSHED(b)) {
		btn |= WPAD_CLASSIC_BUTTON_B;
	}

	if (PUSHED(x)) {
		btn |= WPAD_CLASSIC_BUTTON_X;
	}

	if (PUSHED(y)) {
		btn |= WPAD_CLASSIC_BUTTON_Y;
	}

	if (PUSHED(up)) {
		btn |= WPAD_CLASSIC_BUTTON_UP;
	}

	if (PUSHED(down)) {
		btn |= WPAD_CLASSIC_BUTTON_DOWN;
	}

	if (PUSHED(left)) {
		btn |= WPAD_CLASSIC_BUTTON_LEFT;
	}

	if (PUSHED(right)) {
		btn |= WPAD_CLASSIC_BUTTON_RIGHT;
	}

	//    if (PUSHED(start)) {
	//        btn |= WPAD_CLASSIC_BUTTON_START;
	//    }
	//
	//    if (PUSHED(back)) {
	//        btn |= WPAD_CLASSIC_BUTTON_BACK;
	//    }
	//
	//    if (PUSHED(logo)) {
	//        btn |= WPAD_CLASSIC_BUTTON_LOGO;
	//        enableCapture();
	//    }
	//
	//    if (PUSHED(rb)) {
	//        btn |= WPAD_CLASSIC_BUTTON_RB;
	//    }
	//
	//    if (PUSHED(lb)) {
	//        btn |= WPAD_CLASSIC_BUTTON_LB;
	//    }
	//
	//    if (PUSHED(s1_z)) {
	//        btn |= WPAD_CLASSIC_BUTTON_RSTICK;
	//    }
	//
	//    if (PUSHED(s2_z)) {
	//        btn |= WPAD_CLASSIC_BUTTON_LSTICK;
	//    }
	return btn;
}

static uint16_t WPAD_ButtonsUp(int ictrl)
{
	uint16_t btn = 0;

	if (RELEASED(a)) {
		btn |= WPAD_CLASSIC_BUTTON_A;
	}

	if (RELEASED(b)) {
		btn |= WPAD_CLASSIC_BUTTON_B;
	}

	if (RELEASED(x)) {
		btn |= WPAD_CLASSIC_BUTTON_X;
	}

	if (RELEASED(y)) {
		btn |= WPAD_CLASSIC_BUTTON_Y;
	}

	if (RELEASED(up)) {
		btn |= WPAD_CLASSIC_BUTTON_UP;
	}

	if (RELEASED(down)) {
		btn |= WPAD_CLASSIC_BUTTON_DOWN;
	}

	if (RELEASED(left)) {
		btn |= WPAD_CLASSIC_BUTTON_LEFT;
	}

	if (RELEASED(right)) {
		btn |= WPAD_CLASSIC_BUTTON_RIGHT;
	}

	//    if (RELEASED(start)) {
	//        btn |= WPAD_CLASSIC_BUTTON_START;
	//    }
	//
	//    if (RELEASED(back)) {
	//        btn |= WPAD_CLASSIC_BUTTON_BACK;
	//    }
	//
	//    if (RELEASED(logo)) {
	//        btn |= WPAD_CLASSIC_BUTTON_LOGO;
	//    }
	//
	//    if (RELEASED(rb)) {
	//        btn |= WPAD_CLASSIC_BUTTON_RB;
	//    }
	//
	//    if (RELEASED(lb)) {
	//        btn |= WPAD_CLASSIC_BUTTON_LB;
	//    }
	//
	//    if (RELEASED(s1_z)) {
	//        btn |= WPAD_CLASSIC_BUTTON_RSTICK;
	//    }
	//
	//    if (RELEASED(s2_z)) {
	//        btn |= WPAD_CLASSIC_BUTTON_LSTICK;
	//    }

	return btn;
}

static uint16_t WPAD_ButtonsHeld(int ictrl)
{
	uint16_t btn = 0;

	if (HELD(a)) {
		btn |= WPAD_CLASSIC_BUTTON_A;
	}

	if (HELD(b)) {
		btn |= WPAD_CLASSIC_BUTTON_B;
	}

	if (HELD(x)) {
		btn |= WPAD_CLASSIC_BUTTON_X;
	}

	if (HELD(y)) {
		btn |= WPAD_CLASSIC_BUTTON_Y;
	}

	if (HELD(up)) {
		btn |= WPAD_CLASSIC_BUTTON_UP;
	}

	if (HELD(down)) {
		btn |= WPAD_CLASSIC_BUTTON_DOWN;
	}

	if (HELD(left)) {
		btn |= WPAD_CLASSIC_BUTTON_LEFT;
	}

	if (HELD(right)) {
		btn |= WPAD_CLASSIC_BUTTON_RIGHT;
	}
	//
	//    if (HELD(start)) {
	//        btn |= WPAD_CLASSIC_BUTTON_START;
	//    }
	//
	//    if (HELD(back)) {
	//        btn |= WPAD_CLASSIC_BUTTON_BACK;
	//    }
	//
	//    if (HELD(logo)) {
	//        btn |= WPAD_CLASSIC_BUTTON_LOGO;
	//    }
	//
	//    if (HELD(rb)) {
	//        btn |= WPAD_CLASSIC_BUTTON_RB;
	//    }
	//
	//    if (HELD(lb)) {
	//        btn |= WPAD_CLASSIC_BUTTON_LB;
	//    }
	//
	//    if (HELD(s1_z)) {
	//        btn |= WPAD_CLASSIC_BUTTON_RSTICK;
	//    }
	//
	//    if (HELD(s2_z)) {
	//        btn |= WPAD_CLASSIC_BUTTON_LSTICK;
	//    }

	return btn;
}

static uint16_t PAD_ButtonsDown(int ictrl)
{
	uint16_t btn = 0;

	if (PUSHED(a)) {
		btn |= PAD_BUTTON_A;
	}

	if (PUSHED(b)) {
		btn |= PAD_BUTTON_B;
	}

	if (PUSHED(x)) {
		btn |= PAD_BUTTON_X;
	}

	if (PUSHED(y)) {
		btn |= PAD_BUTTON_Y;
	}

	if (PUSHED(up)) {
		btn |= PAD_BUTTON_UP;
	}

	if (PUSHED(down)) {
		btn |= PAD_BUTTON_DOWN;
	}

	if (PUSHED(left)) {
		btn |= PAD_BUTTON_LEFT;
	}

	if (PUSHED(right)) {
		btn |= PAD_BUTTON_RIGHT;
	}

	if (PUSHED(start)) {
		btn |= PAD_BUTTON_START;
	}

	if (PUSHED(back)) {
		btn |= PAD_BUTTON_BACK;
	}

	if (PUSHED(logo)) {
		btn |= PAD_BUTTON_LOGO;
	}

	if (PUSHED(rb)) {
		btn |= PAD_BUTTON_RB;
	}

	if (PUSHED(lb)) {
		btn |= PAD_BUTTON_LB;
	}

	if (PUSHED(s1_z)) {
		btn |= PAD_BUTTON_RSTICK;
	}

	if (PUSHED(s2_z)) {
		btn |= PAD_BUTTON_LSTICK;
	}
	return btn;
}

static uint16_t PAD_ButtonsUp(int ictrl)
{
	uint16_t btn = 0;

	if (RELEASED(a)) {
		btn |= PAD_BUTTON_A;
	}

	if (RELEASED(b)) {
		btn |= PAD_BUTTON_B;
	}

	if (RELEASED(x)) {
		btn |= PAD_BUTTON_X;
	}

	if (RELEASED(y)) {
		btn |= PAD_BUTTON_Y;
	}

	if (RELEASED(up)) {
		btn |= PAD_BUTTON_UP;
	}

	if (RELEASED(down)) {
		btn |= PAD_BUTTON_DOWN;
	}

	if (RELEASED(left)) {
		btn |= PAD_BUTTON_LEFT;
	}

	if (RELEASED(right)) {
		btn |= PAD_BUTTON_RIGHT;
	}

	if (RELEASED(start)) {
		btn |= PAD_BUTTON_START;
	}

	if (RELEASED(back)) {
		btn |= PAD_BUTTON_BACK;
	}

	if (RELEASED(logo)) {
		btn |= PAD_BUTTON_LOGO;
	}

	if (RELEASED(rb)) {
		btn |= PAD_BUTTON_RB;
	}

	if (RELEASED(lb)) {
		btn |= PAD_BUTTON_LB;
	}

	if (RELEASED(s1_z)) {
		btn |= PAD_BUTTON_RSTICK;
	}

	if (RELEASED(s2_z)) {
		btn |= PAD_BUTTON_LSTICK;
	}

	if (HELD(start) && HELD(back)) {
		doScreenCapture();
	}

	return btn;
}

static uint16_t PAD_ButtonsHeld(int ictrl)
{
	uint16_t btn = 0;

	if (HELD(a)) {
		btn |= PAD_BUTTON_A;
	}

	if (HELD(b)) {
		btn |= PAD_BUTTON_B;
	}

	if (HELD(x)) {
		btn |= PAD_BUTTON_X;
	}

	if (HELD(y)) {
		btn |= PAD_BUTTON_Y;
	}

	if (HELD(up)) {
		btn |= PAD_BUTTON_UP;
	}

	if (HELD(down)) {
		btn |= PAD_BUTTON_DOWN;
	}

	if (HELD(left)) {
		btn |= PAD_BUTTON_LEFT;
	}

	if (HELD(right)) {
		btn |= PAD_BUTTON_RIGHT;
	}

	if (HELD(start)) {
		btn |= PAD_BUTTON_START;
	}

	if (HELD(back)) {
		btn |= PAD_BUTTON_BACK;
	}

	if (HELD(logo)) {
		btn |= PAD_BUTTON_LOGO;
	}

	if (HELD(rb)) {
		btn |= PAD_BUTTON_RB;
	}

	if (HELD(lb)) {
		btn |= PAD_BUTTON_LB;
	}

	if (HELD(s1_z)) {
		btn |= PAD_BUTTON_RSTICK;
	}

	if (HELD(s2_z)) {
		btn |= PAD_BUTTON_LSTICK;
	}

	return btn;
}

s8 PAD_StickX(int i)
{
	return HANDLE_STICK_DEAD_ZONE(ctrl[i].s1_x) >> 8;
}

s8 PAD_StickY(int i)
{
	return HANDLE_STICK_DEAD_ZONE(ctrl[i].s1_y) >> 8;
}

s8 PAD_SubStickX(int i)
{
	return HANDLE_STICK_DEAD_ZONE(ctrl[i].s2_y) >> 8;
}

s8 PAD_SubStickY(int i)
{
	return HANDLE_STICK_DEAD_ZONE(ctrl[i].s2_y) >> 8;
}

u8 PAD_TriggerL(int i)
{
	return ctrl[i].lt;
}

u8 PAD_TriggerR(int i)
{
	return ctrl[i].rt;
}

static void XenonInputUpdate()
{
	usb_do_poll();
	for (int i = 0; i < MAX_INPUTS; i++) {
		old_ctrl[i] = ctrl[i];
		get_controller_data(&ctrl[i], i);
	}
	// update wpad
	for (int i = 0; i < MAX_INPUTS; i++) {
		wpad_xenon[i].btns_d = WPAD_ButtonsDown(i);
		wpad_xenon[i].btns_u = WPAD_ButtonsUp(i);
		wpad_xenon[i].btns_h = WPAD_ButtonsHeld(i);

		//float irx = (float)((float)PAD_StickX(i)/128.f);
		//float iry = (float)(-(float)PAD_StickY(i)/128.f)-0.5f;
		//        float iry = 0.5f-((float)PAD_StickY(i)/128.f);
		float iry = 0.5f + ((float) -PAD_StickY(i) / 128.f);
		float irx = 0.5f + ((float) PAD_StickX(i) / 128.f);

		irx *= screenwidth;
		iry *= screenheight;

		wpad_xenon[i].ir.x = irx;
		wpad_xenon[i].ir.y = iry;

		wpad_xenon[i].ir.valid = 0;
	}
}

/****************************************************************************
 * UpdatePads
 *
 * Scans pad and wpad
 ***************************************************************************/
void UpdatePads()
{
	XenonInputUpdate();

	for (int i = 3; i >= 0; i--) {
		userInput[i].pad.btns_d = PAD_ButtonsDown(i);
		userInput[i].pad.btns_u = PAD_ButtonsUp(i);
		userInput[i].pad.btns_h = PAD_ButtonsHeld(i);
		//        userInput[i].pad.stickX = PAD_StickX(i);
		//        userInput[i].pad.stickY = PAD_StickY(i);
		userInput[i].pad.substickX = PAD_SubStickX(i);
		userInput[i].pad.substickY = PAD_SubStickY(i);
		userInput[i].pad.triggerL = PAD_TriggerL(i);
		userInput[i].pad.triggerR = PAD_TriggerR(i);
	}
}

/****************************************************************************
 * SetupPads
 *
 * Sets up userInput triggers for use
 ***************************************************************************/
void SetupPads()
{
	XenonInputInit();


	for (int i = 0; i < 4; i++) {
		userInput[i].chan = i;
		userInput[i].wpad = &wpad_xenon[i];
		userInput[i].wpad->exp.type = EXP_CLASSIC;
	}
}

/****************************************************************************
 * ShutoffRumble
 ***************************************************************************/

void ShutoffRumble()
{

}

/****************************************************************************
 * DoRumble
 ***************************************************************************/

void DoRumble(int i)
{

}

/****************************************************************************
 * Set the default mapping
 ***************************************************************************/
void SetDefaultButtonMap()
{

}

void ResetControls(int consoleCtrl, int wiiCtrl)
{
}

bool MenuRequested()
{
	for (int i = 0; i < 4; i++) {
		if (
			(userInput[i].pad.substickX < -70) ||
			(userInput[i].pad.btns_h & PAD_TRIGGER_L &&
			userInput[i].pad.btns_h & PAD_TRIGGER_R &&
			userInput[i].pad.btns_h & PAD_BUTTON_X &&
			userInput[i].pad.btns_h & PAD_BUTTON_Y
			) ||
			userInput[i].pad.btns_h & PAD_BUTTON_LOGO
			) {
			return true;
		}
	}
	return false;
}

/****************************************************************************
 * ReportButtons
 *
 * Called on each rendered frame
 * Our way of putting controller input into Snes9x
 ***************************************************************************/
void ReportButtons()
{
//	int i, j;

	UpdatePads();

	/* Check for menu:
	 * CStick left
	 * OR "L+R+X+Y" (eg. Homebrew/Adapted SNES controllers)
	 * OR "Home" on the wiimote or classic controller
	 * OR Left on classic right analog stick
	 */
	if (MenuRequested())
		ScreenshotRequested = 1; // go to the menu
}

void SetControllers()
{

}
