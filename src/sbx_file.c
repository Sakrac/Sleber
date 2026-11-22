#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <miniz.h>
#include "sleber.h"
#include "Edit.h"
#include "files.h"
#include "c64_data.h"

static const char SBX_MAGIC[] = "Sleber";
static const uint32_t SBX_MAGIC_SIZE = 6;
#define SBX_MAX_FILE_SIZE (64U * 1024U * 1024U)
#define SBX_MAX_DECOMPRESSED_SIZE (64U * 1024U * 1024U)

typedef struct {
	uint8_t width[2];
	uint8_t height[2];
	uint8_t mode;
	uint8_t shared_colors[4];
	uint8_t border_color;
	uint8_t background_color;
	uint8_t foreground_color;
	bool snap_to_grid;
} SbxFile;

uint16_t ReadUInt16BE(const uint8_t* buffer) {
	return ((uint16_t)buffer[0] << 8) | (uint16_t)buffer[1];
}

void WriteUInt16BE(uint8_t* buffer, uint16_t value) {
	buffer[0] = (uint8_t)((value >> 8) & 0xff);
	buffer[1] = (uint8_t)(value & 0xff);
}

void WriteUInt32BE(uint8_t* buffer, uint32_t value) {
	buffer[0] = (uint8_t)((value >> 24) & 0xff);
	buffer[1] = (uint8_t)((value >> 16) & 0xff);
	buffer[2] = (uint8_t)((value >> 8) & 0xff);
	buffer[3] = (uint8_t)(value & 0xff);
}

bool LoadSbxFile(Sleber* app, const char* path_utf8) {
	FILE* fp = OpenFileUTF8(path_utf8, "rb");
	if (!fp) {
		UpdateStatus(app, "Failed to open SBX file.");
		return false;
	}
	fseek(fp, 0, SEEK_END);
	long size = ftell(fp);
	rewind(fp);
	if (size <= (long)(SBX_MAGIC_SIZE + sizeof(uint32_t)) ||
		(uint64_t)size > SBX_MAX_FILE_SIZE) {
		fclose(fp);
		UpdateStatus(app, "The SBX file is empty or too large.");
		return false;
	}

	unsigned char* buffer = (unsigned char*)malloc((size_t)size);
	if (!buffer) {
		fclose(fp);
		UpdateStatus(app, "Out of memory while reading file.");
		return false;
	}

	if (fread(buffer, 1, (size_t)size, fp) != (size_t)size) {
		free(buffer);
		fclose(fp);
		UpdateStatus(app, "Failed to read file contents.");
		return false;
	}
	fclose(fp);

	const uint8_t* data = (uint8_t*)buffer;
	for(uint32_t i=0; i<SBX_MAGIC_SIZE; ++i) {
		if (data[i] != SBX_MAGIC[i]) {
			free(buffer);
			UpdateStatus(app, "Invalid SBX file format.");
			return false;
		}
	}

	data += SBX_MAGIC_SIZE;
	uint32_t decompressed_size = (((uint32_t)data[0]) << 24) | (((uint32_t)data[1]) << 16) | (((uint32_t)data[2]) << 8) | ((uint32_t)data[3]);
	data += 4;
	if (decompressed_size < sizeof(SbxFile) ||
		decompressed_size > SBX_MAX_DECOMPRESSED_SIZE) {
		free(buffer);
		UpdateStatus(app, "Invalid SBX file size.");
		return false;
	}
	uint8_t *decompressed = (uint8_t*)malloc(decompressed_size);
	if (!decompressed) {
		free(buffer);
		UpdateStatus(app, "Out of memory while decompressing SBX file.");
		return false;
	}
	mz_ulong tmp_len = decompressed_size;
	int status = uncompress(decompressed, &tmp_len, data, (mz_ulong)(size - 4 - SBX_MAGIC_SIZE));
	if (status != MZ_OK || tmp_len != decompressed_size) {
		free(decompressed);
		free(buffer);
		UpdateStatus(app, "Failed to decompress SBX file.");
		return false;
	}

	const SbxFile* sbx = (const SbxFile*)(decompressed);

	uint32_t width = ReadUInt16BE(sbx->width);
	uint32_t height = ReadUInt16BE(sbx->height);
	uint32_t num_pixels = width * height;

	if (width == 0 || height == 0 ||
		sbx->mode > SCREEN_MODE_WIDE_UNRESTRICTED ||
		sbx->border_color >= COLOR_COUNT ||
		sbx->background_color >= COLOR_COUNT ||
		sbx->foreground_color >= COLOR_COUNT) {
		free(decompressed);
		free(buffer);
		UpdateStatus(app, "Invalid SBX canvas metadata.");
		return false;
	}
	for (size_t i = 0; i < sizeof(sbx->shared_colors); ++i) {
		if (sbx->shared_colors[i] >= COLOR_COUNT) {
			free(decompressed);
			free(buffer);
			UpdateStatus(app, "Invalid SBX palette metadata.");
			return false;
		}
	}

	if ((uint64_t)decompressed_size != sizeof(SbxFile) + (uint64_t)num_pixels) {
		free(decompressed);
		free(buffer);
		UpdateStatus(app, "Invalid SBX file size.");
		return 0;
	}

	uint8_t* pixels = (uint8_t*)malloc((size_t)num_pixels);
	uint8_t* pixels_info = (uint8_t*)malloc((size_t)num_pixels);
	uint8_t* pixels_prev = (uint8_t*)malloc((size_t)num_pixels);
	if (pixels == NULL || pixels_info == NULL || pixels_prev == NULL) {
		free(decompressed);
		free(buffer);
		if (pixels) free(pixels);
		if (pixels_info) free(pixels_info);
		if (pixels_prev) free(pixels_prev);
		UpdateStatus(app, "Out of memory while loading SBX file.");
		return false;
	}

	LockMutex(&app->canvas_mutex);
	uint16_t old_palette_colors = (uint16_t)(1u << app->foreground_color) |
		(uint16_t)(1u << app->background_color);
	memcpy(pixels_prev, decompressed + sizeof(SbxFile), num_pixels);
	for(uint32_t i=0; i<num_pixels; ++i) {
		pixels[i] = pixels_prev[i] & 0x0f;
		pixels_info[i] = (pixels_prev[i] >> 4) & 0x0f;
	}

	AddUndoScreenMode(app, false);

	memcpy(app->shared_colors, sbx->shared_colors, sizeof(sbx->shared_colors));
	app->width = (uint16_t)width;
	app->height = (uint16_t)height;
	app->background_color = sbx->background_color;
	app->foreground_color = sbx->foreground_color;
	app->border_color = sbx->border_color;
	app->screen_mode = sbx->mode;
	app->snap_to_grid = sbx->snap_to_grid;
	switch (app->screen_mode) {
		case SCREEN_MODE_CHAR_MULTICOLOR:
		case SCREEN_MODE_BITMAP_MULTICOLOR:
		case SCREEN_MODE_SPRITE_MULTICOLOR:
		case SCREEN_MODE_WIDE_UNRESTRICTED:
			app->wide_pixels = 1;
			break;
		default:
			app->wide_pixels = 0;
			break;
	}

	if (app->pixels) free(app->pixels);
	app->pixels = pixels;
	if (app->pixels_info) free(app->pixels_info);
	app->pixels_info = pixels_info;
	if (app->pixels_prev) free(app->pixels_prev);
	app->pixels_prev = pixels_prev;
	if (app->screen_mode == SCREEN_MODE_CHAR || app->screen_mode == SCREEN_MODE_CHAR_MIXED || app->screen_mode == SCREEN_MODE_CHAR_MULTICOLOR) {
		app->char_count = CountC64Chars(app->pixels_info, app->width, app->height, app->width);
	}

	UnlockMutex(&app->canvas_mutex);
	RequestRedraw(app);
	RequestRedrawPaletteColors(app, old_palette_colors |
		(uint16_t)(1u << app->foreground_color) |
		(uint16_t)(1u << app->background_color));
	RequestRedrawPalette(app);
	char message[256];
	snprintf(message, sizeof(message), "Loaded %s", path_utf8);
	UpdateStatus(app, message);
	SetCurrentFile(app, path_utf8, SLEBER_FILE_SBX);
	return true;
}


bool SaveSbxFile(Sleber* app, const char* path_utf8) {
	if (!app || !app->pixels || !app->pixels_info ||
		app->width <= 0 || app->height <= 0 ||
		app->width > MAX_CANVAS_WIDTH || app->height > MAX_CANVAS_HEIGHT ||
		(uint64_t)(uint32_t)app->width * (uint32_t)app->height > UINT32_MAX - sizeof(SbxFile)) {
		UpdateStatus(app, "Invalid canvas data for SBX file.");
		return false;
	}

	LockMutex(&app->canvas_mutex);
	uint32_t num_pixels = (uint32_t)CanvasMemorySize(app->width, app->height);
	uint32_t decompressed_size = (uint32_t)sizeof(SbxFile) + num_pixels;
	uint8_t* decompressed = (uint8_t*)malloc(decompressed_size);
	if (!decompressed) {
		UpdateStatus(app, "Out of memory while saving SBX file.");
		UnlockMutex(&app->canvas_mutex);
		return false;
	}

	SbxFile* sbx = (SbxFile*)decompressed;
	WriteUInt16BE(sbx->width, (uint16_t)app->width);
	WriteUInt16BE(sbx->height, (uint16_t)app->height);
	sbx->mode = app->screen_mode;
	memcpy(sbx->shared_colors, app->shared_colors, sizeof(sbx->shared_colors));
	sbx->border_color = app->border_color;
	sbx->background_color = app->background_color;
	sbx->foreground_color = app->foreground_color;
	sbx->snap_to_grid = app->snap_to_grid;
	for (uint32_t i = 0; i < num_pixels; ++i) {
		decompressed[sizeof(SbxFile) + i] =
			(uint8_t)((app->pixels[i] & 0x0f) | ((app->pixels_info[i] & 0x0f) << 4));
	}
	UnlockMutex(&app->canvas_mutex);

	mz_ulong compressed_capacity = mz_compressBound(decompressed_size);
	uint8_t* compressed = (uint8_t*)malloc((size_t)compressed_capacity);
	if (!compressed) {
		free(decompressed);
		UpdateStatus(app, "Out of memory while compressing SBX file.");
		return false;
	}

	mz_ulong compressed_size = compressed_capacity;
	int status = mz_compress(compressed, &compressed_size, decompressed, decompressed_size);
	free(decompressed);
	if (status != MZ_OK) {
		free(compressed);
		UpdateStatus(app, "Failed to compress SBX file.");
		return false;
	}

	size_t output_size = SBX_MAGIC_SIZE + sizeof(uint32_t) + (size_t)compressed_size;
	uint8_t* output = (uint8_t*)malloc(output_size);
	if (!output) {
		free(compressed);
		UpdateStatus(app, "Out of memory while writing SBX file.");
		return false;
	}

	memcpy(output, SBX_MAGIC, SBX_MAGIC_SIZE);
	WriteUInt32BE(output + SBX_MAGIC_SIZE, decompressed_size);
	memcpy(output + SBX_MAGIC_SIZE + sizeof(uint32_t), compressed, (size_t)compressed_size);
	free(compressed);

	FILE* fp = OpenFileUTF8(path_utf8, "wb");
	if (!fp) {
		free(output);
		char message[256];
		snprintf(message, sizeof(message), "Unable to save %s", path_utf8);
		UpdateStatus(app, message);
		return false;
	}

	size_t written = fwrite(output, 1, output_size, fp);
	fclose(fp);
	free(output);
	if (written != output_size) {
		UpdateStatus(app, "Failed to write SBX file.");
		return false;
	}

	char message[256];
	snprintf(message, sizeof(message), "Saved %s", path_utf8);
	UpdateStatus(app, message);
	SetCurrentFile(app, path_utf8, SLEBER_FILE_SBX);
	return true;
}