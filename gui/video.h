/****************************************************************************
 * libwiigui Template
 * Tantric 2009
 * Modified by Ced2911, 2011
 *
 * video.h
 * Video routines
 ***************************************************************************/

#ifndef _VIDEO_H_
#define _VIDEO_H_

#include <xetypes.h>
#include <xenos/xe.h>
#if 0

typedef union {

    struct {
        u8 a, r, g, b;
    };
    int c;
} GXColor;
#else

typedef struct {
    u8 r, g, b, a;
}
GXColor;

typedef union {
    struct {
        unsigned char a;
        unsigned char r;
        unsigned char g;
        unsigned char b;
    };
    unsigned int lcol;
} XeColor;

#endif

extern struct XenosDevice * g_pVideoDevice;

extern XenosSurface *  get_video_surface();

void InitVideo();
void StopGX();
void ResetVideo_Menu();
void Menu_Render(bool);
void Menu_DrawImg(f32 xpos, f32 ypos, u16 width, u16 height, XenosSurface * data, f32 degrees, f32 scaleX, f32 scaleY, u8 alphaF);
void Menu_DrawRectangle(f32 x, f32 y, f32 width, f32 height, GXColor color, u8 filled);
void Menu_T(XenosSurface * surf, f32 texWidth, f32 texHeight, int16_t screenX, int16_t screenY, GXColor color);
void Menu_TD(XenosSurface * surf, f32 texWidth, f32 texHeight, int16_t screenX, int16_t screenY, GXColor color);
void Menu_TD2(XenosSurface * surf, f32 texWidth, f32 texHeight, int16_t screenX, int16_t screenY, GXColor color);

void update_video (int width, int height);

void initSnesVideo();

extern int screenheight;
extern int screenwidth;
extern u32 FrameTimer;

#endif
