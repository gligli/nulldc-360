/****************************************************************************
 * libwiigui
 *
 * Tantric 2009
 *
 * gui_image.cpp
 *
 * GUI class definitions
 ***************************************************************************/

#include "gui.h"
#include <debug.h>

/**
 * set data for small textures
 */
void XXsetTextureData(XenosSurface * surf, void * buffer) {
	u8 * buf = (u8*) buffer;

	u8 * surfbuf;
	int j, i;
	surfbuf = (u8*) Xe_Surface_LockRect(g_pVideoDevice, surf, 0, 0, 0, 0, XE_LOCK_WRITE);
	for (j = 0; j < surf->hpitch; ++j)
		for (i = 0; i < surf->wpitch; i += surf->width * 4)
			memcpy(&surfbuf[surf->wpitch * j + i], &buf[surf->width * (j % surf->height)*4], surf->width * 4);

	Xe_Surface_Unlock(g_pVideoDevice, surf);
}

/**
 * Constructor for the GuiImage class.
 */
GuiImage::GuiImage() {
	image = NULL;
	width = 0;
	height = 0;
	imageangle = 0;
	tile = -1;
	stripe = 0;
	imgType = IMAGE_DATA;
}

GuiImage::GuiImage(GuiImageData * img) {
	//TR;
	image = NULL;
	width = 0;
	height = 0;
	if (img) {
		image = img->GetImage();
		width = img->GetWidth();
		height = img->GetHeight();
	}
	imageangle = 0;
	tile = -1;
	stripe = 0;
	imgType = IMAGE_DATA;
}

GuiImage::GuiImage(struct XenosSurface * img, int w, int h) {
	image = img;
	width = w;
	height = h;
	imageangle = 0;
	tile = -1;
	stripe = 0;
	imgType = IMAGE_TEXTURE;
}

GuiImage::GuiImage(int w, int h, GXColor c) {
	image = Xe_CreateTexture(g_pVideoDevice, w, h, 1, (XE_FMT_8888 | XE_FMT_ARGB), 0);
	width = w;
	height = h;
	imgType == IMAGE_COLOR;

	u8 * buf = (u8*) Xe_Surface_LockRect(g_pVideoDevice, image, 0, 0, 0, 0, XE_LOCK_WRITE);

	int x, y;
	for (y = 0; y < image->height; y++) {
		for (x = 0; x < image->width; x++) {
			*buf++ = c.a;
			*buf++ = c.r;
			*buf++ = c.g;
			*buf++ = c.b;
		}
	}

	Xe_Surface_Unlock(g_pVideoDevice, image);
}

/**
 * Destructor for the GuiImage class.
 */
GuiImage::~GuiImage() {
	if (imgType == IMAGE_COLOR && image)
		Xe_DestroyTexture(g_pVideoDevice, image);
}

struct XenosSurface * GuiImage::GetImage() {
	return image;
}

void GuiImage::SetImage(GuiImageData * img) {
	image = NULL;
	width = 0;
	height = 0;
	if (img) {
		image = img->GetImage();
		width = img->GetWidth();
		height = img->GetHeight();
	}
	imgType = IMAGE_DATA;
}

void GuiImage::SetImage(struct XenosSurface * img) {
	image = img;
	width = img->width;
	height = img->height;
	imgType = IMAGE_TEXTURE;
}

void GuiImage::SetAngle(float a) {
	imageangle = a;
}

void GuiImage::SetTile(int t) {
	tile = t;
}

GXColor GuiImage::GetPixel(int x, int y) {
	if (!image || this->GetWidth() <= 0 || x < 0 || y < 0)
		return (GXColor) {
		0, 0, 0, 0
	};

	//    u32 offset = (((y >> 2) << 4) * this->GetWidth()) + ((x >> 2) << 6) + (((y % 4 << 2) + x % 4) << 1);
	//    
	//    u32 * src = (u32*)(&buf[offset]);
	//    
	//    union {
	//        u32 c;
	//        GXColor color;
	//    }c;
	//    
	//    c.c = src[0];
	//    
	//    return c.color;

	return (GXColor) {
		0, 0, 0, 0
	};
}

void GuiImage::SetPixel(int x, int y, GXColor color) {
	//    if (!buf || this->GetWidth() <= 0 || x < 0 || y < 0)
	//        return;

	//    u32 offset = (((y >> 2) << 4) * this->GetWidth()) + ((x >> 2) << 6) + (((y % 4 << 2) + x % 4) << 1);
	//    
	//    u32 * dst = (u32*)(&buf[offset]);
	//    
	//    union {
	//        u32 c;
	//        GXColor color;
	//    }c;
	//    
	//    c.color = color;
	//    
	//    dst[0] = c.c;
	return;
}

void GuiImage::SetStripe(int s) {
	stripe = s;
}

void GuiImage::ColorStripe(int shift) {
	GXColor color;
	int x, y = 0;
	int alt = 0;

	int thisHeight = this->GetHeight();
	int thisWidth = this->GetWidth();

	for (; y < thisHeight; ++y) {
		if (y % 3 == 0)
			alt ^= 1;

		if (alt) {
			for (x = 0; x < thisWidth; ++x) {
				color = GetPixel(x, y);

				if (color.r < 255 - shift)
					color.r += shift;
				else
					color.r = 255;
				if (color.g < 255 - shift)
					color.g += shift;
				else
					color.g = 255;
				if (color.b < 255 - shift)
					color.b += shift;
				else
					color.b = 255;

				color.a = 255;
				SetPixel(x, y, color);
			}
		} else {
			for (x = 0; x < thisWidth; ++x) {
				color = GetPixel(x, y);

				if (color.r > shift)
					color.r -= shift;
				else
					color.r = 0;
				if (color.g > shift)
					color.g -= shift;
				else
					color.g = 0;
				if (color.b > shift)
					color.b -= shift;
				else
					color.b = 0;

				color.a = 255;
				SetPixel(x, y, color);
			}
		}
	}
}

/**
 * Draw the button on screen
 */
void GuiImage::Draw() {
	if (!image || !this->IsVisible() || tile == 0)
		return;

	float currScaleX = this->GetScaleX();
	float currScaleY = this->GetScaleY();
	int currLeft = this->GetLeft();
	int thisTop = this->GetTop();

	if (tile > 0) {
		int alpha = this->GetAlpha();
		for (int i = 0; i < tile; ++i) {
			Menu_DrawImg(currLeft + width*i, thisTop, width, height, image, imageangle, currScaleX, currScaleY, alpha);
		}
	} else {
		Menu_DrawImg(currLeft, thisTop, width, height, image, imageangle, currScaleX, currScaleY, this->GetAlpha());
	}
/*
	if (stripe > 0) {
		int thisHeight = this->GetHeight();
		int thisWidth = this->GetWidth();

		for (int y = 0; y < thisHeight; y += 6)
			Menu_DrawRectangle(currLeft, thisTop + y, thisWidth, 3, (GXColor) {
				0, 0, 0, stripe
			}, 1);
	}
 */ 
	this->UpdateEffects();
}
