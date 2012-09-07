/****************************************************************************
 * libwiigui
 *
 * Tantric 2009
 *
 * gui_imagedata.cpp
 *
 * GUI class definitions
 ***************************************************************************/

#include "gui.h"
extern "C" {
#include <ppc/cache.h>
}
#include <png.h>
#include <_pnginfo.h>

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

static void png_mem_read(png_structp png_ptr, png_bytep data, png_size_t length) {
	struct file_buffer_t *src = (struct file_buffer_t *) png_get_io_ptr(png_ptr);
	/* Copy data from image buffer */
	memcpy(data, src->data + src->offset, length);
	/* Advance in the file */
	src->offset += length;
}

static void xeGfx_setTextureData(void * tex, void * buffer) {
	int w, h, length;

	struct XenosSurface * surf = (struct XenosSurface *) tex;

	uint8_t * surfbuf = (uint8_t*) Xe_Surface_LockRect(g_pVideoDevice, surf, 0, 0, 0, 0, XE_LOCK_WRITE);
	//memset(surfbuf, 0, surf->hpitch * surf->wpitch);

	uint8_t * src = NULL;
	uint8_t * dst = (uint8_t *) surfbuf;

	uint8_t * dst_limit = (uint8_t *) surfbuf + ((surf->hpitch) * (surf->wpitch));
	int hpitch = 0;
	int wpitch = 0;

	for (hpitch = 0; hpitch < surf->hpitch; hpitch += surf->height) {
		//        for (int y = 0; y < bmp->rows; y++)
		int y, dsty = 0;
		//        int y_offset = charData->glyphDataTexture->height;
		int y_offset = 0;

		int bmp_pitch = surf->width * 4;

		int x = 0;

		for (y = 0, dsty = y_offset; y < surf->height; y++, dsty++) {
			for (wpitch = 0; wpitch < surf->wpitch; wpitch += surf->width * 4) {
				src = (uint8_t *) buffer + ((y) * bmp_pitch);
				dst = (uint8_t *) surfbuf + ((dsty + hpitch) * (surf->wpitch)) + wpitch;
				for (x = 0; x < surf->width * 4; x++) {
					if (dst < dst_limit) {
						dst[0] = src[0];
						dst[1] = src[1];
						dst[2] = src[2];
						dst[3] = src[3];

						src += 4;
						dst += 4;
					}
				}
			}
		}
	}

	Xe_Surface_Unlock(g_pVideoDevice, surf);
}

//static buffer
static uint8_t png_buffer[1280 * 720 * 4];

static struct XenosSurface *loadPNGFromMemory(unsigned char *PNGdata) {
	int y = 0;
	int width, height;
	png_byte color_type;
	png_byte bit_depth;

	png_structp png_ptr;
	png_infop info_ptr;
	//        int number_of_passes;
	png_bytep * row_pointers;

	offset = 0;

	struct file_buffer_t *file;
	file = (struct file_buffer_t *) malloc(sizeof (struct file_buffer_t));
	file->length = 1024 * 1024 * 5;
	file->data = (unsigned char *) malloc(file->length); //5mo ...
	file->offset = 0;
	memcpy(file->data, PNGdata, file->length);

	/* initialize stuff */
	png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

	if (!png_ptr) {
		printf("[read_png_file] png_create_read_struct failed\n");
		return 0;
	}

	info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr) {
		printf("[read_png_file] png_create_info_struct failed\n");
		return 0;
	}

	png_set_read_fn(png_ptr, (png_voidp *) file, png_mem_read); //permet de lire à  partir de pngfile buff

	//png_set_sig_bytes(png_ptr, 8);//on avance de 8 ?

	png_read_info(png_ptr, info_ptr);

	width = info_ptr->width;
	height = info_ptr->height;
	color_type = info_ptr->color_type;
	bit_depth = info_ptr->bit_depth;

	//        number_of_passes = png_set_interlace_handling(png_ptr);

	if (color_type != PNG_COLOR_TYPE_RGB && color_type != PNG_COLOR_TYPE_RGB_ALPHA) {
		printf("no support :( \n bit_depth = %08x\n", bit_depth);
		return 0;
	}

	if (color_type == PNG_COLOR_TYPE_RGB)
		png_set_filler(png_ptr, 0xFF, PNG_FILLER_BEFORE);

	png_set_swap_alpha(png_ptr);

	png_read_update_info(png_ptr, info_ptr);

	//On créer la surface
	struct XenosSurface *surface = Xe_CreateTexture(g_pVideoDevice, width, height, 1, XE_FMT_8888 | XE_FMT_ARGB, 0);

	//uint8_t *data = (uint8_t*) Xe_Surface_LockRect(g_pVideoDevice, surface, 0, 0, 0, 0, XE_LOCK_WRITE);

	uint8_t *data = png_buffer;

	row_pointers = new png_byte*[surface->height];
	for (y = 0; y < surface->height; y++)
		row_pointers[y] = data + (surface->width * 4) * y;

	png_read_image(png_ptr, row_pointers);

	//Xe_Surface_Unlock(g_pVideoDevice, surface);

	xeGfx_setTextureData(surface, data);

	free(file->data);
	free(file);
	//delete(data);
	delete(row_pointers);

	return (surface);
}

/**
 * Constructor for the GuiImageData class.
 */
GuiImageData::GuiImageData(const u8 * i, int maxw, int maxh) {
	data = NULL;
	width = 0;
	height = 0;

	if (i) {
		//data = DecodePNG(i, &width, &height, maxw, maxh);
		data = loadPNGFromMemory((unsigned char*) i);
		data->use_filtering = 0; // obligatoire sinon ca "bave"
		data->u_addressing = XE_TEXADDR_BORDER;
		data->v_addressing = XE_TEXADDR_BORDER;
		width = data->width;
		height = data->height;
	}
}

/**
 * Destructor for the GuiImageData class.
 */
GuiImageData::~GuiImageData() {
	if (data) {
		//free(data);
		Xe_DestroyTexture(g_pVideoDevice, data);
		data = NULL;
	}
}

XenosSurface * GuiImageData::GetImage() {
	return data;
}

int GuiImageData::GetWidth() {
	return width;
}

int GuiImageData::GetHeight() {
	return height;
}
