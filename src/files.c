#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include "stb/stb_image.h"
#include "stb/stb_image_write.h"
#include "sleber.h"
#include "Edit.h"
#include "files.h"
#include "gpx_load.h"
#include "palette.h"
#include "convert.h"
#include "sbx_file.h"
#include "c64_data.h"

// FILE OPS

static bool HasExtension(const char* path, const char* extension) {
	const char* actual = strrchr(path, '.');
	if (!actual) {
		return false;
	}
	while (*actual && *extension) {
		if (tolower((unsigned char)*actual) != tolower((unsigned char)*extension)) {
			return false;
		}
		++actual;
		++extension;
	}
	return *actual == '\0' && *extension == '\0';
}

bool SaveCurrentFile(Sleber* app) {
	if (!app || !app->current_file_path || app->current_file_type == SLEBER_FILE_NONE) {
		return false;
	}
	bool saved = false;
	if (app->current_file_type == SLEBER_FILE_GPX || app->current_file_type == SLEBER_FILE_SBX) {
		saved = SaveImage(app, app->current_file_path);
	}
	return saved;
}

bool SetCurrentFile(Sleber* app, const char* path_utf8, SleberFileType type) {
	if (!app || !path_utf8 || type == SLEBER_FILE_NONE) {
		return false;
	}
	size_t length = strlen(path_utf8);
	char* path = (char*)malloc(length + 1);
	if (!path) {
		UpdateStatus(app, "Unable to remember the current file path.");
		return false;
	}
	memcpy(path, path_utf8, length + 1);
	free(app->current_file_path);
	app->current_file_path = path;
	app->current_file_type = type;
	return true;
}

void ClearCurrentFile(Sleber* app) {
	if (!app) { return; }
	free(app->current_file_path);
	app->current_file_path = NULL;
	app->current_file_type = SLEBER_FILE_NONE;
}

bool OpenDroppedFile(Sleber* app, const char* path_utf8) {
	if (!app || !path_utf8) {
		return false;
	}
	if (HasExtension(path_utf8, ".gpx")) {
		return OpenGpxFile(app, path_utf8) != 0;
	}
	if (HasExtension(path_utf8, ".sbx")) {
		return LoadSbxFile(app, path_utf8);
	}
	return ImportImageFile(app, path_utf8) != 0;
}

bool OpenImage(Sleber* app, const char* path_utf8) {
	if (!app || !path_utf8) {
		return false;
	}
	if (HasExtension(path_utf8, ".gpx")) {
		return OpenGpxFile(app, path_utf8) != 0;
	}
	if (HasExtension(path_utf8, ".sbx")) {
		return LoadSbxFile(app, path_utf8);
	}
	UpdateStatus(app, "Please select an SBX or GPX file.");
	return false;
}

bool SaveImage(Sleber* app, const char* path_utf8) {
	if (!app || !path_utf8) {
		return false;
	}
	if (HasExtension(path_utf8, ".gpx")) {
		if (!IsGpxSupportedMode(app->screen_mode)) {
			UpdateStatus(app, "The current screen mode cannot be saved as GPX.");
			return false;
		}
		return SaveGpxFile(app, path_utf8) != 0;
	}
	if (HasExtension(path_utf8, ".sbx")) {
		return SaveSbxFile(app, path_utf8);
	}
	UpdateStatus(app, "Please use an .sbx or .gpx file extension.");
	return false;
}

bool SetBrushFromARGB(Sleber* app, uint32_t* argb, int width, int height) {
	if (!argb) {
		return false;
	}
	if (!app || width <= 0 || height <= 0 ||
		(size_t)width > SIZE_MAX / (size_t)height / sizeof(*argb)) {
		free(argb);
		return false;
	}

	const size_t pixel_count = (size_t)width * (size_t)height;
	uint8_t* brush = (uint8_t*)malloc(pixel_count);
	if (!brush) {
		free(argb);
		UpdateStatus(app, "Unable to allocate clipboard image.");
		return false;
	}

	for (size_t i = 0; i < pixel_count; ++i) {
		const uint32_t pixel = argb[i];
		brush[i] = (pixel >> 24) < 0x60 ? 0xff :
			GetClosestPaletteColor((pixel >> 16) & 0xff, (pixel >> 8) & 0xff, pixel & 0xff);
	}

	LockMutex(&app->canvas_mutex);
	free(app->brush);
	free(app->brush32);
	app->brush = brush;
	app->brush32 = argb;
	app->brush_w = width;
	app->brush_h = height;
	UnlockMutex(&app->canvas_mutex);
	return true;
}

int ImportImageFile(Sleber* app, const char* path_utf8) {
	int width = 0, height = 0, channels = 0;
	unsigned char* image = stbi_load(path_utf8, &width, &height, &channels, 0);

	if (!image || width <= 0 || height <= 0) {
		UpdateStatus(app, "Unable to load image file.");
		return 0;
	}

	if(channels < 3) {
		stbi_image_free(image);
		UpdateStatus(app, "Image must have at least 3 channels (RGB).");
		return 0;
	}

	if (!ImportImage(app, image, width, height, channels)) {
		stbi_image_free(image);
		UpdateStatus(app, "Unable to import image into the current screen mode.");
		return 0;
	}
	if (app->screen_mode == SCREEN_MODE_CHAR || app->screen_mode == SCREEN_MODE_CHAR_MIXED ||
		app->screen_mode == SCREEN_MODE_CHAR_MULTICOLOR || app->screen_mode == SCREEN_MODE_ECBM) {
		app->char_count = CountC64Chars(app->pixels_info, app->width, app->height, app->width);
	}

	stbi_image_free(image);
	ClearCurrentFile(app);
	RequestRedraw(app);
	char message[256];
	snprintf(message, sizeof(message), "Imported %s", path_utf8);
	UpdateStatus(app, message);
	return 1;
}

static void WriteDataToFile(void* context, void* data, int size) {
	fwrite(data, 1, (size_t)size, (FILE*)context);
}

int ExportImageToFile(Sleber* app, const char* path_utf8) {
	const size_t pixel_count = CanvasMemorySize(app->width, app->height);
	if (pixel_count == 0) {
		UpdateStatus(app, "Invalid canvas size.");
		return 0;
	}
	unsigned char* image = (unsigned char*)calloc(pixel_count * 3, 1);
	if (!image) {
		UpdateStatus(app, "Out of memory while exporting image.");
		return 0;
	}

	LockMutex(&app->canvas_mutex);
	for (int i = 0; i < app->width * app->height; ++i) {
		uint8_t r = 0, g = 0, b = 0;
		GetPaletteColor(app->pixels[i] % COLOR_COUNT, &r, &g, &b);
		image[i * 3 + 0] = r;
		image[i * 3 + 1] = g;
		image[i * 3 + 2] = b;
	}
	UnlockMutex(&app->canvas_mutex);

	FILE* fp = OpenFileUTF8(path_utf8, "wb");
	if (!fp) {
		free(image);
		UpdateStatus(app, "Failed to write image file.");
		return 0;
	}

	int ok = stbi_write_png_to_func(WriteDataToFile, fp, app->width, app->height, 3, image, app->width * 3);
	fclose(fp);
	free(image);
	if (!ok) {
		UpdateStatus(app, "Failed to write image file.");
		return 0;
	}

	char message[256];
	snprintf(message, sizeof(message), "Exported %s", path_utf8);
	UpdateStatus(app, message);
	return 1;
}

static uint8_t GpxToSleberMode[] = {
	SCREEN_MODE_BITMAP,
	SCREEN_MODE_BITMAP_MULTICOLOR,
	SCREEN_MODE_SPRITE,
	SCREEN_MODE_SPRITE_MULTICOLOR,
	SCREEN_MODE_CHAR,
	SCREEN_MODE_CHAR_MULTICOLOR,
	SCREEN_MODE_CHAR,
	SCREEN_MODE_CHAR,
	SCREEN_MODE_UNRESTRICTED,
	SCREEN_MODE_WIDE_UNRESTRICTED
};

int OpenGpxFile(Sleber* app, const char* path_utf8) {
	FILE* fp = OpenFileUTF8(path_utf8, "rb");
	if (!fp) {
		char message[256];
		snprintf(message, sizeof(message), "Unable to open %s", path_utf8);
		UpdateStatus(app, message);
		return 0;
	}

	fseek(fp, 0, SEEK_END);
	long size = ftell(fp);
	rewind(fp);
	if (size <= 0) {
		fclose(fp);
		UpdateStatus(app, "The selected file is empty.");
		return 0;
	}

	unsigned char* buffer = (unsigned char*)malloc((size_t)size);
	if (!buffer) {
		fclose(fp);
		UpdateStatus(app, "Out of memory while reading file.");
		return 0;
	}

	if (fread(buffer, 1, (size_t)size, fp) != (size_t)size) {
		free(buffer);
		fclose(fp);
		UpdateStatus(app, "Failed to read file contents.");
		return 0;
	}
	fclose(fp);

	gpx_data* parsed = NULL;
	gpx_status status = ParseGPX(buffer, (size_t)size, &parsed);
	free(buffer);
	if (status != GPX_OK || !parsed) {
		char message[256];
		snprintf(message, sizeof(message), "Failed to load GPX: %s", GetGPXStatusString(status));
		UpdateStatus(app, message);
		return 0;
	}

	if (parsed->mode < GPX_MODE_BITMAP ||
		parsed->mode > GPX_MODE_WIDE_UNRESTRICTED ||
		(parsed->mode == GPX_MODE_UNUSED1 || parsed->mode == GPX_MODE_UNUSED2)) {
		free(parsed);
		UpdateStatus(app, "The GPX file contains an unsupported screen mode.");
		return 0;
	}
	const uint8_t target_mode = GpxToSleberMode[parsed->mode];
	size_t bitmap_size = 0;
	uint8_t* bitmap = CreateGPXImage(*parsed, &bitmap_size, true);
	if (!bitmap) {
		free(parsed);
		free(bitmap);
		UpdateStatus(app, "The GPX file did not contain a compatible bitmap payload.");
		return 0;
	}

	const size_t pixel_count = (size_t)parsed->width * (size_t)parsed->height;
	if (bitmap_size != pixel_count) {
		free(parsed);
		free(bitmap);
		UpdateStatus(app, "The GPX bitmap dimensions are invalid.");
		return 0;
	}

	uint8_t* new_pixels = (uint8_t*)malloc(pixel_count);
	uint8_t* new_pixels_info = (uint8_t*)malloc(pixel_count);
	uint8_t* new_pixels_prev = (uint8_t*)malloc(pixel_count);
	if (!new_pixels || !new_pixels_info || !new_pixels_prev) {
		free(new_pixels);
		free(new_pixels_info);
		free(new_pixels_prev);
		free(parsed);
		free(bitmap);
		UpdateStatus(app, "Out of memory while loading GPX.");
		return 0;
	}
	memcpy(new_pixels, bitmap, pixel_count);
	memcpy(new_pixels_info, bitmap + bitmap_size, pixel_count);
	for (size_t i = 0; i < pixel_count; ++i) {
		new_pixels_prev[i] = new_pixels[i] | (new_pixels_info[i] << 4);
	}

	LockMutex(&app->canvas_mutex);
	if (target_mode != app->screen_mode) {
		AddUndoScreenMode(app, false);
	}
	uint8_t *old_pixels = app->pixels;
	uint8_t *old_pixels_info = app->pixels_info;
	uint8_t *old_pixels_prev = app->pixels_prev;
	app->pixels = new_pixels;
	app->pixels_info = new_pixels_info;
	app->pixels_prev = new_pixels_prev;
	app->width = parsed->width;
	app->height = parsed->height;
	app->shared_colors[0] = parsed->background_color;
	app->shared_colors[1] = parsed->multicolor0;
	app->shared_colors[2] = parsed->multicolor1;
	SetScreenModeLocked(app, target_mode);
	if (app->screen_mode == SCREEN_MODE_CHAR || app->screen_mode == SCREEN_MODE_CHAR_MIXED ||
		app->screen_mode == SCREEN_MODE_CHAR_MULTICOLOR || app->screen_mode == SCREEN_MODE_ECBM) {
		app->char_count = CountC64Chars(app->pixels_info, app->width, app->height, app->width);
	}
	free(old_pixels);
	free(old_pixels_info);
	free(old_pixels_prev);
	free(bitmap);
	free(parsed);
	UnlockMutex(&app->canvas_mutex);

	RequestRedraw(app);
	char message[256];
	snprintf(message, sizeof(message), "Loaded %s", path_utf8);
	UpdateStatus(app, message);
	SetCurrentFile(app, path_utf8, SLEBER_FILE_GPX);
	return 1;
}

int SaveGpxFile(Sleber* app, const char* path_utf8) {
	LockMutex(&app->canvas_mutex);
	gpx_data* gpx = MakeGPXFile(app);
	if (!gpx) {
		UnlockMutex(&app->canvas_mutex);
		UpdateStatus(app, "Failed to create GPX data.");
		return 0;
	}
	size_t out_size = 0;
	uint8_t* encoded = CreateGPX(gpx, &out_size);
	free(gpx);
	UnlockMutex(&app->canvas_mutex);
	if (!encoded) {
		UpdateStatus(app, "Failed to encode GPX data.");
		return 0;
	}
	FILE* fp = OpenFileUTF8(path_utf8, "wb");
	if (!fp) {
		free(encoded);
		char message[256];
		snprintf(message, sizeof(message), "Unable to save %s", path_utf8);
		UpdateStatus(app, message);
		return 0;
	}

	size_t written = fwrite(encoded, 1, out_size, fp);
	fclose(fp);
	free(encoded);

	if (written != out_size) {
		UpdateStatus(app, "Failed to write GPX file.");
		return 0;
	}

	char message[256];
	snprintf(message, sizeof(message), "Saved %s", path_utf8);
	UpdateStatus(app, message);
	SetCurrentFile(app, path_utf8, SLEBER_FILE_GPX);
	return 1;
}
