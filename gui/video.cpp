/****************************************************************************
 * libwiigui Template
 * Tantric 2009
 *
 * video.cpp
 * Video routines
 ***************************************************************************/

#include <xetypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <input/input.h>
#include <console/console.h>

#include <ppc/timebase.h>
#include <time/time.h>

#include <xenos/xe.h>
#include <xenos/xenos.h>
#include <xenos/edram.h>
#include <xenos/xenos.h>

#include "input.h"
#include "gui/gui.h"
#include "emu.h"
#include <Vec.h>

#include <debug.h>

int LoadTextureFromFile(char * pSrcFile, XenosSurface **ppTexture);

typedef unsigned int DWORD;
//#include "ps.h"
//#include "vs.h"
#include "shaders/vs.h"
#include "shaders/ps.t.h"
#include "shaders/ps.c.h"
//#include "shaders/ps.snes.h"

#include "shaders/simple.ps.h"
#include "shaders/simple.vs.h"

#define DEFAULT_FIFO_SIZE 256 * 1024

//static GXRModeObj *vmode; // Menu video mode
//static unsigned char gp_fifo[DEFAULT_FIFO_SIZE] ATTRIBUTE_ALIGN(32);
//static Mtx GXmodelView2D;
int screenheight;
int screenwidth;
u32 FrameTimer = 0;

static void * fb_ptr;

#define r32(o) g_pVideoDevice->regs[(o)/4]
#define w32(o, v) g_pVideoDevice->regs[(o)/4] = (v)

#define MAX_VERTEX_COUNT 65536

static struct XenosDevice _xe;
static struct XenosVertexBuffer *vb = NULL;

struct XenosDevice * g_pVideoDevice = NULL;
static struct XenosShader * g_pVertexShader = NULL;
static struct XenosShader * g_pPixelTexturedShader = NULL;
static struct XenosShader * g_pPixelColoredShader = NULL;
static struct XenosSurface * g_pEmuSurface = NULL;
//XenosSurface * g_pTexture;

matrix4x4 modelView2D;
matrix4x4 projection;
//matrix4x4 WVP;
static int nb_vertices = 0;

typedef struct {
    float x, y, z, w; // 32
    unsigned int color; // 36
    unsigned int padding; // 40
    float u, v; // 48
} __attribute__((packed)) DrawVerticeFormats;

// Init Matrices

void InitMatrices() {
    matrix4x4 WVP;
    matrixLoadIdentity(&WVP);

    matrixLoadIdentity(&projection);
    matrixOrthoRH(&projection, 640, 480, 0, 300);

    matrixLoadIdentity(&modelView2D);
    matrixTranslation(&modelView2D, 0, 0, -50.f);
}

/****************************************************************************
 * ResetVideo_Menu
 *
 * Reset the video/rendering mode for the menu
 ****************************************************************************/
void
ResetVideo_Menu() {
    // Init Matrices
    InitMatrices();
}
void CreateVbText(float x, float y, float w, float h, uint32_t color, DrawVerticeFormats * Rect) {
    // bottom left
    Rect[0].x = x - w;
    Rect[0].y = y + h;
    Rect[0].u = 0;
    Rect[0].v = 1;
    Rect[0].color = color;

    // bottom right
    Rect[1].x = x + w;
    Rect[1].y = y + h;
    Rect[1].u = 1;
    Rect[1].v = 1;
    Rect[1].color = color;

    // top right
    Rect[2].x = x + w;
    Rect[2].y = y - h;
    Rect[2].u = 1;
    Rect[2].v = 0;
    Rect[2].color = color;

    // Top left
    Rect[3].x = x - w;
    Rect[3].y = y - h;
    Rect[3].u = 0;
    Rect[3].v = 0;
    Rect[3].color = color;

    int i = 0;
    for (i = 0; i < 4; i++) {
        Rect[i].z = 0.0;
        Rect[i].w = 1.0;
    }
}

void CreateVb(float x, float y, float w, float h, uint32_t color, DrawVerticeFormats * Rect) {
    // bottom left
    Rect[0].x = x - w;
    Rect[0].y = y - h;
    Rect[0].u = 0;
    Rect[0].v = 0;
    Rect[0].color = color;

    // bottom right
    Rect[1].x = x + w;
    Rect[1].y = y - h;
    Rect[1].u = 1;
    Rect[1].v = 0;
    Rect[1].color = color;

    // top right
    Rect[2].x = x + w;
    Rect[2].y = y + h;
    Rect[2].u = 1;
    Rect[2].v = 1;
    Rect[2].color = color;

    // Top left
    Rect[3].x = x - w;
    Rect[3].y = y + h;
    Rect[3].u = 0;
    Rect[3].v = 1;
    Rect[3].color = color;

    int i = 0;
    for (i = 0; i < 4; i++) {
        Rect[i].z = 0.0;
        Rect[i].w = 1.0;
    }
}

void CreateVbQuad(float width, float height, uint32_t color, DrawVerticeFormats * Rect) {
    // bottom left
    Rect[0].x = -width;
    Rect[0].y = -height;
    Rect[0].u = 0;
    Rect[0].v = 0;
    Rect[0].color = color;

    // bottom right
    Rect[1].x = width;
    Rect[1].y = -height;
    Rect[1].u = 1;
    Rect[1].v = 0;
    Rect[1].color = color;

    // top right
    Rect[2].x = width;
    Rect[2].y = height;
    Rect[2].u = 1;
    Rect[2].v = 1;
    Rect[2].color = color;

    // Top left
    Rect[3].x = -width;
    Rect[3].y = height;
    Rect[3].u = 0;
    Rect[3].v = 1;
    Rect[3].color = color;

    int i = 0;
    for (i = 0; i < 4; i++) {
        Rect[i].z = 0.0;
        Rect[i].w = 1.0;
    }
}

void oCreateVbQuad(float width, float height, uint32_t color, DrawVerticeFormats * Rect) {
    // bottom left
    Rect[0].x = -width;
    Rect[0].y = -height;
    Rect[0].u = 0;
    Rect[0].v = 0;
    Rect[0].color = color;

    // bottom right
    Rect[1].x = width;
    Rect[1].y = -height;
    Rect[1].u = 1;
    Rect[1].v = 0;
    Rect[1].color = color;

    // top right
    Rect[2].x = width;
    Rect[2].y = height;
    Rect[2].u = 1;
    Rect[2].v = 1;
    Rect[2].color = color;

    // Top left
    Rect[3].x = -width;
    Rect[3].y = height;
    Rect[3].u = 0;
    Rect[3].v = 1;
    Rect[3].color = color;

    int i = 0;
    for (i = 0; i < 4; i++) {
        Rect[i].z = 0.0;
        Rect[i].w = 1.0;
    }
}

extern "C" struct XenosDevice * GetVideoDevice() {
    return g_pVideoDevice;
}

/****************************************************************************
 * InitVideo
 *
 * This function MUST be called at startup.
 * - also sets up menu video mode
 ***************************************************************************/
void
InitVideo() {
    xenos_init(VIDEO_MODE_AUTO);
    //console_init();

    g_pVideoDevice = &_xe;

    Xe_Init(g_pVideoDevice);

    // save real fb
    fb_ptr = (void*) r32(0x6110);

    XenosSurface * fb = Xe_GetFramebufferSurface(g_pVideoDevice);

    screenheight = ((float) fb->height)*(720.f / (float) fb->height);
    screenwidth = ((float) fb->width)*(1280.f / (float) fb->width);

    Xe_Init(g_pVideoDevice);

    Xe_SetRenderTarget(g_pVideoDevice, Xe_GetFramebufferSurface(g_pVideoDevice));
    
    static const struct XenosVBFFormat vbf = {
        4,
        {
            {XE_USAGE_POSITION, 0, XE_TYPE_FLOAT4},
            {XE_USAGE_COLOR, 0, XE_TYPE_UBYTE4},
            {XE_USAGE_COLOR, 1, XE_TYPE_UBYTE4}, //padding
            {XE_USAGE_TEXCOORD, 0, XE_TYPE_FLOAT2},
        }
    };

    g_pPixelTexturedShader = Xe_LoadShaderFromMemory(g_pVideoDevice, (void*) g_xps_psT);
    Xe_InstantiateShader(g_pVideoDevice, g_pPixelTexturedShader, 0);

    g_pPixelColoredShader = Xe_LoadShaderFromMemory(g_pVideoDevice, (void*) g_xps_psC);
    Xe_InstantiateShader(g_pVideoDevice, g_pPixelColoredShader, 0);

    g_pVertexShader = Xe_LoadShaderFromMemory(g_pVideoDevice, (void*) g_xvs_VSmain);
    Xe_InstantiateShader(g_pVideoDevice, g_pVertexShader, 0);

    Xe_ShaderApplyVFetchPatches(g_pVideoDevice, g_pVertexShader, 0, &vbf);

    edram_init(g_pVideoDevice);

    vb = Xe_CreateVertexBuffer(g_pVideoDevice, MAX_VERTEX_COUNT * sizeof (DrawVerticeFormats));

    Xe_SetClearColor(g_pVideoDevice, 0xFFFFFFFF);

    Xe_InvalidateState(g_pVideoDevice);
	
	g_pEmuSurface = Xe_CreateTexture(g_pVideoDevice, fb->width, fb->height, 0, XE_FMT_8888 | XE_FMT_BGRA, 1);
	memset(g_pEmuSurface->base, 0, g_pEmuSurface->wpitch * g_pEmuSurface->hpitch);

    InitEmuVideo();
	int m = 500;
	while(m--){
		Xe_InvalidateState(g_pVideoDevice);
		Xe_Sync(g_pVideoDevice);
	}

    ResetVideo_Menu();
}

/****************************************************************************
 * StopGX
 *
 * Stops GX (when exiting)
 ***************************************************************************/
void StopGX() {
    //    GX_AbortFrame();
    //    GX_Flush();
    //
    //    VIDEO_SetBlack(TRUE);
    //    VIDEO_Flush();
}

/****************************************************************************
 * Menu_Render
 *
 * Renders everything current sent to GX, and flushes video
 ***************************************************************************/
void Menu_Render(bool vblank) {

	// disable scissor
	// Xe_SetScissor(g_pVideoDevice,0,0,0,0,0);
	
    FrameTimer++;

    // update the hole vb
    Xe_VB_Lock(g_pVideoDevice, vb, 0, MAX_VERTEX_COUNT * sizeof (DrawVerticeFormats), XE_LOCK_WRITE);
    Xe_VB_Unlock(g_pVideoDevice, vb);

    Xe_InvalidateState(g_pVideoDevice);

    Xe_Resolve(g_pVideoDevice);

    if (vblank)
        while (!Xe_IsVBlank(g_pVideoDevice));

    Xe_Sync(g_pVideoDevice);

    nb_vertices = 4096;
}

void SetRS() {
    Xe_SetBlendOp(g_pVideoDevice, XE_BLENDOP_ADD);
    Xe_SetSrcBlend(g_pVideoDevice, XE_BLEND_SRCALPHA);
    Xe_SetDestBlend(g_pVideoDevice, XE_BLEND_INVSRCALPHA);
    Xe_SetAlphaTestEnable(g_pVideoDevice, 1);

    Xe_SetCullMode(g_pVideoDevice, XE_CULL_NONE);
    Xe_SetStreamSource(g_pVideoDevice, 0, vb, nb_vertices, sizeof (DrawVerticeFormats));
}

void Draw() {
    SetRS();
    Xe_DrawPrimitive(g_pVideoDevice, XE_PRIMTYPE_RECTLIST, 0, 1);
    //nb_vertices += 4 * sizeof (DrawVerticeFormats);
    nb_vertices += 512; // fixe aligement
}

void UpdatesMatrices(f32 xpos, f32 ypos, f32 width, f32 height, f32 degrees, f32 scaleX, f32 scaleY) {
#define DegToRad(a)   ( (a) *  0.01745329252f )
    matrix4x4 m;
    matrix4x4 rotation;
    matrix4x4 scale;
    matrix4x4 translation;
    matrix4x4 WVP;

    matrixLoadIdentity(&WVP);
    matrixLoadIdentity(&m);
    matrixLoadIdentity(&rotation);
    matrixLoadIdentity(&scale);
    matrixLoadIdentity(&translation);

    matrixRotationZ(&rotation, DegToRad(degrees));
    matrixTranslation(&translation, xpos + width, ypos + height, 0);
    matrixScaling(&scale, scaleX, scaleY, 1.0f);
    //matrixScaling(&scale, width, height, 1.0f);

    //    // scale => rotate => translate
    matrixMultiply(&m, &scale, &rotation);
    matrixMultiply(&WVP, &m, &translation);

    Xe_SetVertexShaderConstantF(g_pVideoDevice, 0, (float*) &WVP, 4);
}

/****************************************************************************
 * Menu_DrawImg
 *
 * Draws the specified image on screen using GX
 ***************************************************************************/
void Menu_DrawImg(f32 xpos, f32 ypos, u16 width, u16 height, XenosSurface * data,
        f32 degrees, f32 scaleX, f32 scaleY, u8 alpha) {

    if (data == NULL)
        return;

    float x, y, w, h;

    x = (float) xpos;
    y = (float) ypos;
    w = (float) width;
    h = (float) height;

    x = (x / ((float) (screenwidth - 1) / 2.f)) - 1.f; // 1280/2
    y = (y / ((float) (screenheight - 1) / 2.f)) - 1.f; // 720/2

    w = (float) w / ((float) screenwidth - 1);
    h = (float) h / ((float) screenheight - 1);

    XeColor color;

    color.a = alpha;
    color.r = 0xFF;
    color.g = 0xFF;
    color.b = 0xFF;

    DrawVerticeFormats* Rect = (DrawVerticeFormats*) Xe_VB_Lock(g_pVideoDevice, vb, nb_vertices, 4 * sizeof (DrawVerticeFormats), XE_LOCK_WRITE);
    {
        CreateVbQuad(w, h, color.lcol, Rect);
    }
    Xe_VB_Unlock(g_pVideoDevice, vb);

    Xe_SetTexture(g_pVideoDevice, 0, data);

    UpdatesMatrices(x, y, w, h, degrees, scaleX, scaleY);

    Xe_SetShader(g_pVideoDevice, SHADER_TYPE_PIXEL, g_pPixelTexturedShader, 0);
    Xe_SetShader(g_pVideoDevice, SHADER_TYPE_VERTEX, g_pVertexShader, 0);

    Draw();
}

/****************************************************************************
 * Menu_DrawRectangle
 *
 * Draws a rectangle at the specified coordinates using GX
 ***************************************************************************/
void Menu_DrawRectangle(f32 x, f32 y, f32 width, f32 height, GXColor color, u8 filled) {
    float w, h;

    x = (float) x;
    y = (float) y;
    w = (float) width;
    h = (float) height;

    x = (x / ((float) (screenwidth - 1) / 2.f)) - 1.f; // 1280/2
    y = (y / ((float) (screenheight - 1) / 2.f)) - 1.f; // 720/2

    w = (float) w / ((float) screenwidth - 1);
    h = (float) h / ((float) screenheight - 1);


    XeColor _color;

    _color.a = color.a;
    _color.r = color.r;
    _color.g = color.g;
    _color.b = color.b;

    DrawVerticeFormats* Rect = (DrawVerticeFormats*) Xe_VB_Lock(g_pVideoDevice, vb, nb_vertices, 4 * sizeof (DrawVerticeFormats), XE_LOCK_WRITE);
    {
        CreateVbQuad(w, h, _color.lcol, Rect);
        //CreateVb(x,y,w,h,_color.lcol,Rect);
    }
    Xe_VB_Unlock(g_pVideoDevice, vb);

    Xe_SetTexture(g_pVideoDevice, 0, NULL);

    UpdatesMatrices(x, y, w, h, 0, 1, 1);

    Xe_SetShader(g_pVideoDevice, SHADER_TYPE_PIXEL, g_pPixelColoredShader, 0);
    Xe_SetShader(g_pVideoDevice, SHADER_TYPE_VERTEX, g_pVertexShader, 0);

    Draw();
}

void Menu_T(XenosSurface * surf, f32 texWidth, f32 texHeight, int16_t screenX, int16_t screenY, GXColor color) {
    float x, y, w, h;
    if (surf == NULL)
        return;

    x = (float) screenX;
    y = (float) screenY;
    w = (float) texWidth;
    h = (float) texHeight;

    x = (x / ((float) (screenwidth - 1) / 2.f)) - 1.f; // 1280/2
    y = (y / ((float) (screenheight - 1) / 2.f)) - 1.f; // 720/2

    w = (float) w / ((float) screenwidth - 1);
    h = (float) h / ((float) screenheight - 1);

    DrawVerticeFormats* Rect = (DrawVerticeFormats*) Xe_VB_Lock(g_pVideoDevice, vb, nb_vertices, 4 * sizeof (DrawVerticeFormats), XE_LOCK_WRITE);
    {
        XeColor _color;

        _color.a = color.a;
        _color.r = color.r;
        _color.g = color.g;
        _color.b = color.b;

        CreateVbText(0, 0, w, h, _color.lcol, Rect);
    }
    Xe_VB_Unlock(g_pVideoDevice, vb);

    Xe_SetTexture(g_pVideoDevice, 0, surf);

    UpdatesMatrices(x, y, w, h, 0, 1, 1);

    Xe_SetShader(g_pVideoDevice, SHADER_TYPE_PIXEL, g_pPixelTexturedShader, 0);
    Xe_SetShader(g_pVideoDevice, SHADER_TYPE_VERTEX, g_pVertexShader, 0);

    Draw();
}

/****************************************************************************
 * Update Video
 ***************************************************************************/
XenosSurface *  get_video_surface()
{
	return g_pEmuSurface;
}