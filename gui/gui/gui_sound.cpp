/****************************************************************************
 * libwiigui
 *
 * Tantric 2009
 *
 * gui_sound.cpp
 *
 * GUI class definitions
 ***************************************************************************/

#include "gui.h"

/**
 * Constructor for the GuiSound class.
 */
GuiSound::GuiSound(const u8 * s, s32 l, int t) {
    sound = s;
    length = l;
    type = t;
    voice = -1;
    volume = 100;
    loop = false;
}

/**
 * Destructor for the GuiSound class.
 */
GuiSound::~GuiSound() {

}

void GuiSound::Play() {

}

void GuiSound::Stop() {

}

void GuiSound::Pause() {

}

void GuiSound::Resume() {

}

bool GuiSound::IsPlaying() {
    //	if(ASND_StatusVoice(voice) == SND_WORKING || ASND_StatusVoice(voice) == SND_WAITING)
    //		return true;
    //	else
    //		return false;
    return false;
}

void GuiSound::SetVolume(int vol) {

}

void GuiSound::SetLoop(bool l) {
    loop = l;
}
