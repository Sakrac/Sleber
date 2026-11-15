#ifndef CONVERT_H
#define CONVERT_H

#include "sleber.h"

typedef enum ImportImageDither {
	IMPORT_IMAGE_DITHER_OFF,
	IMPORT_IMAGE_DITHER_LOW,
	IMPORT_IMAGE_DITHER_MEDIUM,
	IMPORT_IMAGE_DITHER_HIGH
} ImportImageDither;

typedef struct {
	uint8_t* pixels;
	uint8_t* types;
	int width;
	int height;
} SleberImage;

bool ImportImage(Sleber* app, uint8_t *image, int image_width, int image_height, int image_channels);
SleberImage ConvertArea(Sleber* app, const uint8_t* image, int image_width, int image_height, int image_channels);
bool SwitchScreenMode(Sleber* app, uint8_t new_mode);

#endif // CONVERT_H