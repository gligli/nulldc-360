#include <png.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time/time.h>
#include <byteswap.h>

static int do_capture = 0;

struct ati_info {
    uint32_t unknown1[4];
    uint32_t base;
    uint32_t unknown2[8];
    uint32_t width;
    uint32_t height;
} __attribute__((__packed__));

void enableCapture() {
    do_capture = 1;
}

void doScreenCapture() {
    if (do_capture) {
        struct ati_info *ai = (struct ati_info*) 0xec806100ULL;

        int width = ai->width;
        int height = ai->height;

        volatile unsigned int *screen = (unsigned int*) (long) (ai->base | 0x80000000);

        unsigned int * screen2 =(unsigned int *)malloc(width * height * sizeof(unsigned int));
        png_bytep * row_pointers=(png_bytep *)malloc(height* sizeof(png_bytep));

        int y, x;
        for (y = 0; y < height; ++y) {
            for (x = 0; x < width; ++x) {
                unsigned int base = ((((y & ~31) * width) + (x & ~31)*32) +
                        (((x & 3) + ((y & 1) << 2) + ((x & 28) << 1) + ((y & 30) << 5)) ^ ((y & 8) << 2)));
                screen2[y * width + x] = 0xFF | __builtin_bswap32(screen[base] >> 8);
            }
            row_pointers[y] = (png_bytep) (screen2 + y * width);
        }

        char filename[256];
        sprintf(filename, "uda:/sshot_%08x.png", rand());

        FILE *outfp = fopen(filename, "wb");

        png_structp png_ptr_w = png_create_write_struct(PNG_LIBPNG_VER_STRING, 0, 0, 0);
        png_infop info_ptr_w = png_create_info_struct(png_ptr_w);

        png_init_io(png_ptr_w, outfp);
        png_set_IHDR(png_ptr_w, info_ptr_w, width, height, 8, PNG_COLOR_TYPE_RGB_ALPHA, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

        png_set_rows(png_ptr_w, info_ptr_w, row_pointers);
        png_write_png(png_ptr_w, info_ptr_w, PNG_TRANSFORM_IDENTITY, 0);
        png_write_end(png_ptr_w, info_ptr_w);
        png_destroy_write_struct(&png_ptr_w, &info_ptr_w);

        fclose(outfp);

        printf("ScreenCapture : File saved to : %s\r\n", filename);
        
        free(screen2);
        free(row_pointers);
    }

    do_capture = 0;
}