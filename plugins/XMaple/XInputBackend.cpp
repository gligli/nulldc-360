#define _WIN32_WINNT 0x500
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "XInputBackend.h"
#include "XMaple.h"
#include "FT8.h"

#include <input/input.h>
#include <usb/usbmain.h>

#include "../drkPvr/screencapture.h"

extern xmaple_settings maplesettings;

namespace XInput
{		

static void ScaleStickValues(unsigned char* outx, unsigned char* outy, short inx, short iny)
{
	const float kDeadZone = (float)maplesettings.Controller.Deadzone * 327.68f;
	const int	center 	  = 0x80;

	float magnitude = sqrtf( (float)inx*inx + (float)iny*iny );		
	
	float x = inx / magnitude;
	float y = iny / magnitude;

	if (magnitude < kDeadZone)
		magnitude = 0;
	else
	{
		magnitude  = (magnitude - kDeadZone) * 32767.0f / (32767.0f - kDeadZone);
		magnitude /= 256.0f; // Reducing to 0-128 values
		
		if ( magnitude > 128.0f ) magnitude = 128; // Just for safety
	}
	
	x *= magnitude;
	y *= magnitude;
	
	*outx = (unsigned char)(center + x);
	*outy = (unsigned char)(center - y);
}

bool Read(int XPadPlayer, u32 deviceType, EmulatedDevices::FT0::SStatus* status)
{
	static struct controller_data_s ctrlrs[4] = {{0}};
	
	usb_do_poll();
	
	get_controller_data(&ctrlrs[XPadPlayer],XPadPlayer);
	
	struct controller_data_s * c= &ctrlrs[XPadPlayer];

	ScaleStickValues(&status->joyx, &status->joyy, c->s1_x, c->s1_y);
	ScaleStickValues(&status->joy2x, &status->joy2y, c->s2_x, c->s2_y);

	status->ltrig = c->lt;
	status->rtrig = c->rt;

	if (c->a)		{status->buttons ^= CONT_BUTTON_A;}
	if (c->b)		{status->buttons ^= CONT_BUTTON_B;}
	if (c->x)		{status->buttons ^= CONT_BUTTON_X;}
	if (c->y)		{status->buttons ^= CONT_BUTTON_Y;}
	if (c->start)	{status->buttons ^= CONT_BUTTON_START;}
	if (c->up)		{status->buttons ^= CONT_DPAD_UP;}
	if (c->down)	{status->buttons ^= CONT_DPAD_DOWN;}
	if (c->left)	{status->buttons ^= CONT_DPAD_LEFT;}
	if (c->right)	{status->buttons ^= CONT_DPAD_RIGHT;}
	
	if(c->select && c->logo) exit(0);
	
	if(c->lb){
		enableCapture();
		doScreenCapture();
	}
	
	return true;
}

bool IsConnected(int XPadPlayer)
{
/*gli	XINPUT_STATE xstate;
	DWORD xresult = XInputGetState(XPadPlayer, &xstate);
	return (xresult == ERROR_SUCCESS);*/
	return XPadPlayer==0;
}

void StopRumble(int XPadPlayer)
{
//gli	XInputSetState(XPadPlayer, 0);
}

void VibrationThread(void* _status)
{
/*gli	DEBUG_LOG("   VIBRATION THREAD STARTED\n");

	EmulatedDevices::FT8::SStatus* status = (EmulatedDevices::FT8::SStatus*)_status;	

	XINPUT_VIBRATION vib;

	EmulatedDevices::FT8::UVibConfig* config = &status->config;	

	int timeLength = maplesettings.PuruPuru.Length;	
	int intensity, intensityH;
	int intensityX, intensityHX;		
	int fm, step, stepH;	
	
	int directionOld = 0;
	int directionNew = 0; 	
	int direction;

	int cdVergence;

	while (true)
	{			
		// intensity is 0 when there's no direction == autostop.
		direction = config->Mpow - config->Ppow;					
		
		if ( direction )		
		{											
			// Get middle frequency for usage with Real Freq.
			fm = (status->srcmaplesettings.FM1 + status->srcmaplesettings.FM0)/2; 

			intensity = abs(direction) * 9362;
			intensityH = abs(direction) * 8192 + 8190;		

			intensityX = (intensity * maplesettings.PuruPuru.Intensity) / 100;
			intensityHX = (intensityX * maplesettings.PuruPuru.Intensity) / 100;

			if (intensityX > 65535) intensityX = 65535;
			if (intensityHX > 65535) intensityHX = 65535;
			
			directionOld = directionNew;
			directionNew = direction;					
								
			// Impact when motor changes direction.
			if ( directionNew * directionOld < 0 )
			{																
				vib.wRightMotorSpeed = intensityHX;
				vib.wLeftMotorSpeed  = intensityX;
			
				XInputSetState(status->currentXPad, &vib);
				Sleep(timeLength);
			}			

			// Convergence/Divergence
			if ( config->INC ) 
			{								
				cdVergence = config->EXH - config->INH;
				
				switch(cdVergence)
				{
					case -1:
						{
							step = intensity / config->INC;
							stepH = (intensityH - 8190) / config->INC;
						} break;
					case  0: step = stepH = 0; break;
					case  1:
						{
							step = (65534 - intensity) / config->INC;
							stepH = (65534 - intensityH) / config->INC;
						} break;
				}

				for(int i=0; i<=config->INC; i++)
				{
					// Intensity setting...
					
					intensityX = (intensity * maplesettings.PuruPuru.Intensity) / 100;
					intensityHX = (intensityH * maplesettings.PuruPuru.Intensity) / 100;

					if (intensityX > 65535) intensityX = 65535;
					if (intensityHX > 65535) intensityHX = 65535;					
					
					// Real Frequency
					if (maplesettings.PuruPuru.UseRealFreq)
					{										
						if ( config->Freq > (u8)((fm*3)/2))	// Top 1/4
						{
							// Level up high freq motor.
							vib.wRightMotorSpeed = intensityHX;
							vib.wLeftMotorSpeed  = 0;
						}
						else if ( config->Freq < (u8)((fm*2)/3))	// Low 1/3
						{
							vib.wRightMotorSpeed = 0;	
							vib.wLeftMotorSpeed  = intensityX;	
						}
						else
						{							
							vib.wRightMotorSpeed = intensityHX;
							vib.wLeftMotorSpeed  = intensityX;
						}
					}
					else
					{
						vib.wRightMotorSpeed = intensityHX;
						vib.wLeftMotorSpeed = intensityX;
					}
	
					XInputSetState(status->currentXPad, &vib);

					intensity  = intensity  + step  * cdVergence;										
					intensityH = intensityH + stepH * cdVergence;

					Sleep(timeLength);					
				} 				

			} 
			else // End of Convergence/Divergence	
			{
				// Intensity setting
				intensityX = (intensity * maplesettings.PuruPuru.Intensity) / 100;
				intensityHX = (intensityH * maplesettings.PuruPuru.Intensity) / 100;

				if (intensityX > 65535) intensityX = 65535;
				if (intensityHX > 65535) intensityHX = 65535;				
				
				// Real Frequency
				if (maplesettings.PuruPuru.UseRealFreq)
				{								
					if ( config->Freq > (u8)((fm*3)/2))	// Top 1/4
					{					
						vib.wRightMotorSpeed = intensityHX;
						vib.wLeftMotorSpeed  = 0;
					}
					else if ( config->Freq < (u8)((fm*2)/3))	// Low 1/3
					{
						vib.wRightMotorSpeed = 0;	
						vib.wLeftMotorSpeed  = intensityX;	
					}
					else
					{																				
						vib.wRightMotorSpeed = intensityHX;
						vib.wLeftMotorSpeed  = intensityX;
					}
				}
				else
				{
					vib.wRightMotorSpeed = intensityHX;
					vib.wLeftMotorSpeed = intensityX;
				}
	
				XInputSetState(status->currentXPad, &vib);
				Sleep(timeLength);
			}			

			// Stop once it's done.
			config->Ppow = config->Mpow = 0;
			
			vib.wRightMotorSpeed = 0;
			vib.wLeftMotorSpeed = 0;

			XInputSetState(status->currentXPad, &vib);			
		}	
		else Sleep(10);

	} //while(true)
*/
} // Vibration Thread

} //namespace



