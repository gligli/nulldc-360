#include <xetypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <input/input.h>
#include <console/console.h>
#include <png.h>
#include <_pnginfo.h>
#include <ppc/timebase.h>
#include <time/time.h>
#include <ppc/atomic.h>

#include <xenos/xe.h>
#include <xenos/xenos.h>
#include <xenos/edram.h>
#include <xenos/xenos.h>

#include <debug.h>
#define TR {printf("[Trace] in function %s, line %d, file %s\n",__FUNCTION__,__LINE__,__FILE__);}

#include "Vec.h"
#include "video.h"
#include "input.h"
#include "gui/gui.h"

#if 0

struct ati_info {
	uint32_t unknown1[4];
	uint32_t base;
	uint32_t unknown2[8];
	uint32_t width;
	uint32_t height;
} __attribute__((__packed__));

#define MAX_SHADER 10

#define SNES_THUMBNAIL_W 64
#define SNES_THUMBNAIL_H 48

typedef unsigned int DWORD;

typedef void (*video_callback)(void*);

typedef struct {
	char name[30];
	struct XenosShader * ps;
	struct XenosShader * vs;
	video_callback callback;
} SnesShader;

typedef struct {
	float x, y, z, w;
	float u, v;
} SnesVerticeFormats;

typedef struct {
	float w;
	float h;
} float2;

typedef struct {
	float2 video_size;
	float2 texture_size;
	float2 output_size;
} _shaderParameters;

#include "shaders/5xBR-v3.7a.ps.h"
#include "shaders/5xBR-v3.7a.vs.h"

#include "shaders/2xBR-v3.5a.ps.h"
#include "shaders/2xBR-v3.5a.vs.h"

#include "shaders/5xBR-v3.7b.ps.h"
#include "shaders/5xBR-v3.7b.vs.h"

#include "shaders/5xBR-v3.7c.ps.h"
#include "shaders/5xBR-v3.7c.vs.h"

//#include "shaders/5xBR-v3.7c_crt.ps.h"
//#include "shaders/5xBR-v3.7c_crt.vs.h"

#include "shaders/scanline.ps.h"
#include "shaders/scanline.vs.h"

#include "shaders/simple.ps.h"
#include "shaders/simple.vs.h"

static matrix4x4 modelViewProj;
static struct XenosVertexBuffer *snes_vb = NULL;
static struct XenosVertexBuffer *render_to_target_vb = NULL;
// bitmap emulation
static struct XenosSurface * g_SnesSurface = NULL;
// fb copy of snes display used by gui
static struct XenosSurface * g_SnesSurfaceShadow = NULL;
// used for double buffering
static struct XenosSurface * g_framebuffer[2] = {NULL};

#define r32(o) g_pVideoDevice->regs[(o)/4]
#define w32(o, v) g_pVideoDevice->regs[(o)/4] = (v)

// 64 * 48 -- tiled
u8 * gameScreenPng = NULL;
u8 * gameScreenThumbnail = NULL;
int gameScreenPngSize = 0;
static struct XenosSurface * g_SnesThumbnail = NULL;

static int selected_snes_shader = 0;
static int nb_snes_shaders = 0;
static SnesShader SnesShaders[MAX_SHADER];

static _shaderParameters shaderParameters;

static void default_callback(void*) {
	Xe_SetVertexShaderConstantF(g_pVideoDevice, 0, (float*) &modelViewProj, 4);
	Xe_SetVertexShaderConstantF(g_pVideoDevice, 4, (float*) &shaderParameters, 2);
	Xe_SetPixelShaderConstantF(g_pVideoDevice, 0, (float*) &shaderParameters, 2);
}

static void no_filtering_callback(void*) {
	// Disable filtering for xbr
	g_SnesSurface->use_filtering = 0;

	// Shader constant
	Xe_SetVertexShaderConstantF(g_pVideoDevice, 0, (float*) &modelViewProj, 4);
	Xe_SetVertexShaderConstantF(g_pVideoDevice, 4, (float*) &shaderParameters, 2);
	Xe_SetPixelShaderConstantF(g_pVideoDevice, 0, (float*) &shaderParameters, 2);
}

static void loadShader(char * name, const XenosVBFFormat * vbf, const void * ps_main, const void * vs_main, video_callback callback) {
	strcpy(SnesShaders[nb_snes_shaders].name, name);

	SnesShaders[nb_snes_shaders].ps = Xe_LoadShaderFromMemory(g_pVideoDevice, (void*) ps_main);
	Xe_InstantiateShader(g_pVideoDevice, SnesShaders[nb_snes_shaders].ps, 0);

	SnesShaders[nb_snes_shaders].vs = Xe_LoadShaderFromMemory(g_pVideoDevice, (void*) vs_main);
	Xe_InstantiateShader(g_pVideoDevice, SnesShaders[nb_snes_shaders].vs, 0);
	Xe_ShaderApplyVFetchPatches(g_pVideoDevice, SnesShaders[nb_snes_shaders].vs, 0, vbf);

	SnesShaders[nb_snes_shaders].callback = callback;

	nb_snes_shaders++;

	if (nb_snes_shaders >= MAX_SHADER) {
		printf("Too much shader created !!!\n");
		exit(0);
	}
}

void initSnesVideo() {
	static const struct XenosVBFFormat vbf = {
		2,
		{
			{XE_USAGE_POSITION, 0, XE_TYPE_FLOAT4},
			{XE_USAGE_TEXCOORD, 0, XE_TYPE_FLOAT2},
		}
	};
        
        XenosSurface * fb = Xe_GetFramebufferSurface(g_pVideoDevice);


	loadShader("Normal", &vbf, PSSimple, VSSimple, default_callback);
	//loadShader("Xbr 2x 3.5a", &vbf, PS2xBRa, VS2xBRa, no_filtering_callback); /// too slow ?
	loadShader("Xbr 5x 3.7a", &vbf, PS5xBRa, VS5xBRa, no_filtering_callback);
	loadShader("Xbr 5x 3.7b", &vbf, PS5xBRb, VS5xBRb, no_filtering_callback);
	loadShader("Xbr 5x 3.7c", &vbf, PS5xBRc, VS5xBRc, no_filtering_callback);

	//loadShader("Scanlines", &vbf, PSScanline, VSScanline, no_filtering_callback);

	snes_vb = Xe_CreateVertexBuffer(g_pVideoDevice, 4096);
	render_to_target_vb = Xe_CreateVertexBuffer(g_pVideoDevice, 4096);

	// Create surfaces
	g_SnesSurface = Xe_CreateTexture(g_pVideoDevice, MAX_SNES_WIDTH, MAX_SNES_HEIGHT, 1, XE_FMT_565 | XE_FMT_16BE, 0);
	g_SnesThumbnail = Xe_CreateTexture(g_pVideoDevice, SNES_THUMBNAIL_W, SNES_THUMBNAIL_H, 0, XE_FMT_8888 | XE_FMT_BGRA, 1);
	
	// Create surface for double buffering
	g_framebuffer[0] = Xe_CreateTexture(g_pVideoDevice, fb->width, fb->height, 0, XE_FMT_8888 | XE_FMT_BGRA, 1);
	g_framebuffer[1] = Xe_CreateTexture(g_pVideoDevice, fb->width, fb->height, 0, XE_FMT_8888 | XE_FMT_BGRA, 1);
	
	
	g_SnesSurfaceShadow = g_framebuffer[0];	

	g_SnesSurface->u_addressing = XE_TEXADDR_WRAP;
	g_SnesSurface->v_addressing = XE_TEXADDR_WRAP;

	GFX.Screen = (uint16*) g_SnesSurface->base;
	GFX.Pitch = g_SnesSurface->wpitch;

	memset(g_SnesSurface->base, 0, g_SnesSurface->wpitch * g_SnesSurface->hpitch);
	memset(g_SnesSurfaceShadow->base, 0, g_SnesSurfaceShadow->wpitch * g_SnesSurfaceShadow->hpitch);

	// init fake matrices
	matrixLoadIdentity(&modelViewProj);

	// thumbnail png
	gameScreenPng = (u8*) malloc(SNES_THUMBNAIL_W * SNES_THUMBNAIL_H * sizeof (uint32));
	gameScreenThumbnail = (u8*) malloc(SNES_THUMBNAIL_W * SNES_THUMBNAIL_H * sizeof (uint32));
}

static int detect_changes(int w, int h) {
	static int old_width = -2000;
	static int old_height = -2000;
	static int widescreen = -2000;
	static int xshift = -2000;
	static int yshift = -2000;
	static float zoomVert = -2000;
	static float zoomHor = -2000;

	int changed = 0;

	if (w != old_width) {
		changed = 1;
		goto end;
	}
	if (h != old_height) {
		changed = 1;
		goto end;
	}

	if (widescreen != GCSettings.widescreen) {
		changed = 1;
		goto end;
	}

	if (xshift != GCSettings.xshift) {
		changed = 1;
		goto end;
	}

	if (yshift != GCSettings.yshift) {
		changed = 1;
		goto end;
	}

	if (zoomVert != GCSettings.zoomVert) {
		changed = 1;
		goto end;
	}

	if (zoomHor != GCSettings.zoomHor) {
		changed = 1;
		goto end;
	}

	// save values for next loop        
end:
	old_width = w;
	old_height = h;
	widescreen = GCSettings.widescreen;
	xshift = GCSettings.xshift;
	yshift = GCSettings.yshift;
	zoomHor = GCSettings.zoomHor;
	zoomVert = GCSettings.zoomVert;

	return changed;
}

static void RenderInSurface(XenosSurface * source, XenosSurface * dest) {
	// Update Vb
	float x = -1, y = -1, w = 2, h = 2;
	float u = (screenwidth/dest->width);
	float v = (screenheight/dest->height);
	SnesVerticeFormats* Rect = (SnesVerticeFormats*) Xe_VB_Lock(g_pVideoDevice, render_to_target_vb, 0, 4096, XE_LOCK_WRITE);
	{
		Rect[0].x = x;
		Rect[0].y = y + h;
		Rect[0].u = 0;
		Rect[0].v = 0;

		// bottom left
		Rect[1].x = x;
		Rect[1].y = y;
		Rect[1].u = 0;
		Rect[1].v = v;

		// top right
		Rect[2].x = x + w;
		Rect[2].y = y + h;
		Rect[2].u = u;
		Rect[2].v = 0;

		// top right
		Rect[3].x = x + w;
		Rect[3].y = y + h;
		Rect[3].u = u;
		Rect[3].v = 0;

		// bottom left
		Rect[4].x = x;
		Rect[4].y = y;
		Rect[4].u = 0;
		Rect[4].v = v;

		// bottom right
		Rect[5].x = x + w;
		Rect[5].y = y;
		Rect[5].u = u;
		Rect[5].v = v;

		int i = 0;
		for (i = 0; i < 6; i++) {
			Rect[i].z = 0.0;
			Rect[i].w = 1.0;
		}
	}
	Xe_VB_Unlock(g_pVideoDevice, render_to_target_vb);
	
	// Begin draw
	Xe_InvalidateState(g_pVideoDevice);
	//Xe_SetAlphaTestEnable(g_pVideoDevice, 0);

	Xe_SetCullMode(g_pVideoDevice, XE_CULL_NONE);
	Xe_SetClearColor(g_pVideoDevice, 0);

	Xe_SetShader(g_pVideoDevice, SHADER_TYPE_PIXEL, SnesShaders[0].ps, 0);
	Xe_SetShader(g_pVideoDevice, SHADER_TYPE_VERTEX, SnesShaders[0].vs, 0);
	Xe_SetStreamSource(g_pVideoDevice, 0, render_to_target_vb, 0, sizeof (SnesVerticeFormats));

	Xe_SetTexture(g_pVideoDevice, 0, source);

	Xe_DrawPrimitive(g_pVideoDevice, XE_PRIMTYPE_RECTLIST, 0, 1);

	Xe_ResolveInto(g_pVideoDevice, dest, XE_SOURCE_COLOR, XE_CLEAR_COLOR | XE_CLEAR_DS);

	Xe_Sync(g_pVideoDevice);
}

// used to know which fb to use
static int ibuffer = 0;

static void DrawSnes(XenosSurface * data) {
	if (data == NULL)
		return;

	// double buffering
	ibuffer ^= 1;	
	
	// detect if something changed
	if (detect_changes(g_SnesSurface->width, g_SnesSurface->height)) {
		// work on vb
		float x, y, w, h;
		float scale = 1.f;

		if (GCSettings.widescreen) {
			scale = 3.f / 4.f;
		}

		w = (scale * 2.f) * GCSettings.zoomHor;
		h = 2.f * GCSettings.zoomVert;

		x = (GCSettings.xshift / (float) screenwidth) - (w / 2.f);
		y = (-GCSettings.yshift / (float) screenheight) - (h / 2.f);

		// Update Vb
		SnesVerticeFormats* Rect = (SnesVerticeFormats*) Xe_VB_Lock(g_pVideoDevice, snes_vb, 0, 4096, XE_LOCK_WRITE);
		{
			Rect[0].x = x;
			Rect[0].y = y + h;
			Rect[0].u = 0;
			Rect[0].v = 0;

			// bottom left
			Rect[1].x = x;
			Rect[1].y = y;
			Rect[1].u = 0;
			Rect[1].v = 1;

			// top right
			Rect[2].x = x + w;
			Rect[2].y = y + h;
			Rect[2].u = 1;
			Rect[2].v = 0;

			// top right
			Rect[3].x = x + w;
			Rect[3].y = y + h;
			Rect[3].u = 1;
			Rect[3].v = 0;

			// bottom left
			Rect[4].x = x;
			Rect[4].y = y;
			Rect[4].u = 0;
			Rect[4].v = 1;

			// bottom right
			Rect[5].x = x + w;
			Rect[5].y = y;
			Rect[5].u = 1;
			Rect[5].v = 1;

			int i = 0;
			for (i = 0; i < 6; i++) {
				Rect[i].z = 0.0;
				Rect[i].w = 1.0;
			}
		}
		Xe_VB_Unlock(g_pVideoDevice, snes_vb);
	}

	// Begin draw
	Xe_InvalidateState(g_pVideoDevice);

	Xe_SetCullMode(g_pVideoDevice, XE_CULL_NONE);
	Xe_SetClearColor(g_pVideoDevice, 0);

	// Refresh  texture cache
	Xe_Surface_LockRect(g_pVideoDevice, data, 0, 0, 0, 0, XE_LOCK_WRITE);
	Xe_Surface_Unlock(g_pVideoDevice, data);

	// Set Stream, shader, textures	
	Xe_SetShader(g_pVideoDevice, SHADER_TYPE_PIXEL, SnesShaders[selected_snes_shader].ps, 0);
	Xe_SetShader(g_pVideoDevice, SHADER_TYPE_VERTEX, SnesShaders[selected_snes_shader].vs, 0);
	Xe_SetStreamSource(g_pVideoDevice, 0, snes_vb, 0, sizeof (SnesVerticeFormats));

	// use the callback related to selected shader
	SnesShaders[selected_snes_shader].callback(NULL);

	Xe_SetTexture(g_pVideoDevice, 0, data);

	// Draw
	Xe_DrawPrimitive(g_pVideoDevice, XE_PRIMTYPE_RECTLIST, 0, 1);

	// Display
	Xe_Resolve(g_pVideoDevice);

	while (!Xe_IsVBlank(g_pVideoDevice));
	
	// must be called between vblank and vsync
	if(ibuffer ==0) {
		// hack !!
		w32(0x6110,g_framebuffer[0]->base);
		g_pVideoDevice->tex_fb.base = g_framebuffer[0]->base;
		Xe_SetRenderTarget(g_pVideoDevice,g_framebuffer[0]);
	}
	else{
		w32(0x6110,g_framebuffer[1]->base);
		g_pVideoDevice->tex_fb.base = g_framebuffer[1]->base;
		Xe_SetRenderTarget(g_pVideoDevice,g_framebuffer[1]);
	}
	Xe_Sync(g_pVideoDevice);
}

XenosSurface * get_snes_surface() {
	Xe_Surface_LockRect(g_pVideoDevice, g_SnesSurfaceShadow, 0, 0, 0, 0, XE_LOCK_WRITE);
	Xe_Surface_Unlock(g_pVideoDevice, g_SnesSurfaceShadow);
	return g_SnesSurfaceShadow;
}

static void ShowFPS(void) {
	static unsigned long lastTick = 0;
	static int frames = 0;
	unsigned long nowTick;
	frames++;
	nowTick = mftb() / (PPC_TIMEBASE_FREQ / 1000);
	if (lastTick + 1000 <= nowTick) {

		printf("%d fps\r\n", frames);

		frames = 0;
		lastTick = nowTick;
	}
}

static int frame = 0;

struct file_buffer_t {
	char name[256];
	unsigned char *data;
	long length;
	long offset;
};

struct pngMem {
	unsigned char *png_end;
	unsigned char *data;
	int size;
	int offset; //pour le parcours
};

static int offset = 0;

static void png_mem_write(png_structp png_ptr, png_bytep data, png_size_t length) {
	struct file_buffer_t *dst = (struct file_buffer_t *) png_get_io_ptr(png_ptr);
	/* Copy data from image buffer */
	memcpy(dst->data + dst->offset, data, length);
	/* Advance in the file */
	dst->offset += length;
}

static struct XenosSurface *savePNGToMemory(XenosSurface * surface, unsigned char *PNGdata, int * size) {
	png_structp png_ptr_w;
	png_infop info_ptr_w;
	//        int number_of_passes;
	png_bytep * row_pointers;

	offset = 0;

	struct file_buffer_t *file;
	file = (struct file_buffer_t *) malloc(sizeof (struct file_buffer_t));
	file->length = 1024 * 1024 * 5;
	file->data = PNGdata; //5mo ...
	file->offset = 0;

	/* initialize stuff */
	png_ptr_w = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

	if (!png_ptr_w) {
		printf("[write_png_file] png_create_read_struct failed\n");
		return 0;
	}

	info_ptr_w = png_create_info_struct(png_ptr_w);
	if (!info_ptr_w) {
		printf("[write_png_file] png_create_info_struct failed\n");
		return 0;
	}

	png_set_write_fn(png_ptr_w, (png_voidp *) file, png_mem_write, NULL);
	png_set_IHDR(png_ptr_w, info_ptr_w, surface->width, surface->height, 8, PNG_COLOR_TYPE_RGB_ALPHA, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

	uint32_t *data = (uint32_t *) surface->base;
	uint32_t * untile_buffer = new uint32_t[surface->width * surface->height];
	row_pointers = new png_bytep[surface->height];

	int y, x;
	for (y = 0; y < surface->height; ++y) {
		for (x = 0; x < surface->width; ++x) {
			unsigned int base = ((((y & ~31) * surface->width) + (x & ~31)*32) +
					(((x & 3) + ((y & 1) << 2) + ((x & 28) << 1) + ((y & 30) << 5)) ^ ((y & 8) << 2)));
			untile_buffer[y * surface->width + x] = 0xFF | __builtin_bswap32(data[base] >> 8);
		}
		row_pointers[y] = (png_bytep) (untile_buffer + y * surface->width);
	}

	png_set_rows(png_ptr_w, info_ptr_w, row_pointers);
	png_write_png(png_ptr_w, info_ptr_w, PNG_TRANSFORM_IDENTITY, 0);
	png_write_end(png_ptr_w, info_ptr_w);
	png_destroy_write_struct(&png_ptr_w, &info_ptr_w);

	*size = file->offset;

	//free(file->data);
	free(file);
	//delete(data);
	delete(row_pointers);

	return (surface);
}

void update_video(int width, int height) {			
	g_SnesSurface->width = width;
	g_SnesSurface->height = height;

	shaderParameters.texture_size.w = width;
	shaderParameters.texture_size.h = height;
	shaderParameters.video_size.w = width;
	shaderParameters.video_size.h = height;
	shaderParameters.output_size.w = width;
	shaderParameters.output_size.h = height;

	if (GCSettings.render == 0 || GCSettings.render == 2 || selected_snes_shader)
		g_SnesSurface->use_filtering = 0;
	else
		g_SnesSurface->use_filtering = 1;

	DrawSnes(g_SnesSurface);

	// Display Menu ?
	if (ScreenshotRequested) {
		// thumbnail
		RenderInSurface(g_SnesSurface, g_SnesThumbnail);
		// fb shadow
		//RenderInSurface(g_SnesSurface, g_SnesSurfaceShadow);
		if(ibuffer ==0) {
			g_SnesSurfaceShadow = g_framebuffer[1];
		}
		else {
			g_SnesSurfaceShadow = g_framebuffer[0];
		}
		
		// convert to png
		savePNGToMemory(g_SnesThumbnail, gameScreenPng, &gameScreenPngSize);
		

		ScreenshotRequested = 0;
		//TakeScreenshot();
		EmuConfigRequested = 1;
	}

	ShowFPS();
}

const char* GetFilterName(int filterID) {
	if (filterID >= nb_snes_shaders) {
		return "Unknown";
	} else {
		return SnesShaders[filterID].name;
	}
}

void SelectFilterMethod() {
	if (GCSettings.FilterMethod >= nb_snes_shaders) {
		GCSettings.FilterMethod = nb_snes_shaders - 1;
	}
	selected_snes_shader = GCSettings.FilterMethod;
}

int GetFilterNumber() {
	return nb_snes_shaders;
}

#endif