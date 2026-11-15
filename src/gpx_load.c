/*

This is a re-implementation of the GPX file format parser,
which is used to read and extract metadata from GPX files.
The parser decompresses the input data using the miniz library
and reads various metadata fields such as version, mode,
xsize, ysize, mapsize, colorsize, screensize, backbuffers,
backbufsel, par, and overflow.
The extracted information is stored in a gpx_info structure.

GPX is the proprietary graphics format used by Pixcen that
is developed by John Hammarberg: https://github.com/Hammarberg/pixcen/

Pixcen downloads: https://hammarberg.github.io/pixcen/

This implementation uses the miniz library for decompression,
see miniz/miniz.c for license and other information.

No part of the Pixcen source code is used in this implementation.

This implementation is free to copy, use and modify for any
purpose. It first appeared in https://github.com/sakrac/C64Gfx
(by Carl-Henrik Skårstedt and is released under the MIT license.)

*/

/* Keep MSVC from warning on fopen in the file writer. */
#define _CRT_SECURE_NO_WARNINGS

#include "gpx_load.h"
#include "sleber_limits.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <miniz.h>

// header + 128 bytes of metadata
#define GPX_MAX_HEADER_SIZE 1024
#define GPX_MAX_COMPRESSED_SIZE (16U * 1024U * 1024U)
#define GPX_MAX_DECOMPRESSED_SIZE (16U * 1024U * 1024U)

static bool IsGPXMulticolor(gpx_mode mode) {
	return mode == GPX_MODE_MULTICOLOR_BITMAP ||
		mode == GPX_MODE_MULTICOLOR_CHAR || mode == GPX_MODE_MULTICOLOR_SPRITE;
}

static uint32_t AssignGPXU32(uint32_t value, uint8_t* out_bytes) {
	out_bytes[0] = (uint8_t)(value & 0xffu);
	out_bytes[1] = (uint8_t)((value >> 8) & 0xffu);
	out_bytes[2] = (uint8_t)((value >> 16) & 0xffu);
	out_bytes[3] = (uint8_t)((value >> 24) & 0xffu);
	return 4;
}

static uint32_t AssignGPXWideNum(uint32_t value, uint8_t* out_bytes) {
	uint32_t digit_count = 1, digit_value = 1;
	while (value >= (10 * digit_value)) {
		digit_count++;
		digit_value *= 10;
	}
	for (uint32_t index = 0; index < digit_count; ++index) {
		uint32_t digit = (value / digit_value) % 10;
		out_bytes[index * 2] = (uint8_t)('0' + digit);
		out_bytes[index * 2 + 1] = 0;
		digit_value /= 10;
	}
	out_bytes[digit_count * 2] = 0x00;
	out_bytes[digit_count * 2 + 1] = 0x00;
	return (digit_count + 1) * 2;
}

static uint32_t AssignGPXStringWideNum(const char* key, uint32_t value,
	uint8_t* out_bytes) {
	uint32_t key_length = (uint32_t)strlen(key);
	memcpy(out_bytes, key, key_length);
	out_bytes[key_length] = 0;
	return AssignGPXWideNum(value, out_bytes + key_length + 1) + key_length +
		1;
}

static uint32_t MakeGPXHeader(uint8_t* header, const gpx_data* data,
	uint32_t chrSize, uint32_t colSize, uint32_t scrSize) {
	uint32_t offset = 0;
	offset += AssignGPXU32(4, header + offset);
	offset += AssignGPXU32((uint32_t)data->mode, header + offset);
	offset += AssignGPXU32(14, header + offset);

	uint32_t gpx_width = data->width / (IsGPXMulticolor(data->mode) ? 2 : 1);
	offset += AssignGPXStringWideNum("autoselect", 0, header + offset);
	offset += AssignGPXStringWideNum("backbuffers", 1, header + offset);
	offset += AssignGPXStringWideNum("backbufsel", 0, header + offset);
	offset += AssignGPXStringWideNum("cellsnap", 0, header + offset);
	offset += AssignGPXStringWideNum("colorsize", colSize, header + offset);
	offset += AssignGPXStringWideNum("mapsize", chrSize, header + offset);
	offset += AssignGPXStringWideNum("overflow", 0, header + offset);
	offset += AssignGPXStringWideNum("par", 0, header + offset);
	offset += AssignGPXStringWideNum("pcol", 25600, header + offset);
	offset += AssignGPXStringWideNum("pmap", 16384, header + offset);
	offset += AssignGPXStringWideNum("pscr", 24576, header + offset);
	offset += AssignGPXStringWideNum("screensize", scrSize, header + offset);
	offset += AssignGPXStringWideNum("xsize", gpx_width, header + offset);
	offset += AssignGPXStringWideNum("ysize", (uint32_t)data->height,
		header + offset);
	return offset;
}

uint8_t* CreateGPX(const gpx_data* data, size_t* out_size) {

	uint32_t chrSize = 0, colSize = 0, scrSize = 0;
	switch (data->mode) {
		case GPX_MODE_BITMAP:
			scrSize = (uint32_t)(data->width * data->height / 64);
			chrSize = scrSize * 8;
			break;
		case GPX_MODE_MULTICOLOR_BITMAP:
			scrSize = (uint32_t)(data->width * data->height / 64);
			colSize = scrSize;
			chrSize = scrSize * 8;
			break;
		case GPX_MODE_SPRITE:
		case GPX_MODE_MULTICOLOR_SPRITE:
			colSize = ((uint32_t)data->width / 24) * ((uint32_t)data->height / 21);
			chrSize = colSize * 64;
			break;
		case GPX_MODE_CHAR:
		case GPX_MODE_MULTICOLOR_CHAR:
			colSize = (uint32_t)(data->width * data->height / 64);
			chrSize = colSize * 8;
			break;
		case GPX_MODE_UNUSED1:
			break;
		case GPX_MODE_UNUSED2:
			break;
		case GPX_MODE_UNRESTRICTED:
			chrSize = data->width * data->height;
			break;
		case GPX_MODE_WIDE_UNRESTRICTED:
			chrSize = data->width * data->height / 2;
			break;
	}

	size_t total_size =
		GPX_MAX_HEADER_SIZE + 64 + 64 + chrSize + colSize + scrSize + 12;

	uint8_t* out = (uint8_t*)calloc(1, total_size);
	if (out == NULL) {
		return NULL;
	}

	uint32_t offset = MakeGPXHeader(out, data, chrSize, colSize, scrSize);

	offset += 64;
	out[offset + 59] = 14; // l blue border
	out[offset + 60] = data->background_color;
	out[offset + 61] = data->multicolor0;
	out[offset + 62] = data->multicolor1;

	offset += 64; // slot payload

	if (chrSize) {
		if (data->pScreen && (data->mode == GPX_MODE_CHAR ||
			data->mode == GPX_MODE_MULTICOLOR_CHAR)) {
// if char mode and screen data applied unpack into bitmap
			uint8_t* map = out + offset;
			for (uint32_t y = 0, h = data->height >> 3; y < h; ++y) {
				for (uint32_t x = 0, w = data->width >> 3; x < w; ++x) {
					const uint8_t* chr = data->pChars + data->pScreen[y * w + x] * 8;
					for (uint32_t b = 0; b < 8; ++b) {
						*map++ = *chr++;
					}
				}
			}
		} else if (data->mode == GPX_MODE_WIDE_UNRESTRICTED) {
		  // unrestricted wide just picks every second map byte
			for (uint32_t i = 0; i < chrSize; i++) {
				out[offset + i] = data->pChars[i << 1];
			}
		} else {
			memcpy(out + offset, data->pChars, chrSize);
		}
	}
	offset += chrSize;
	if (colSize) {
		memcpy(out + offset, data->pColors, colSize);
		// pixcen uses the literal color values in multicolor char mode
		// not the multicolor bitmask (8) to signify multicolor char.
		if (data->mode == GPX_MODE_MULTICOLOR_CHAR) {
			for (uint32_t i = 0; i < colSize; ++i) {
				out[offset + i] &= 0x07;
			}
		}
	}
	offset += colSize;
	if (scrSize && data->pScreen) {
		memcpy(out + offset, data->pScreen, scrSize);
	}
	offset += scrSize;

	offset += 12; // history pos, undo_count, redo_count

	mz_ulong compressed_size = compressBound((mz_ulong)offset);
	uint8_t* compressed_data = (uint8_t*)malloc(compressed_size);
	if (!compressed_data) {
		free(out);
		return NULL;
	}

	compressed_size = (mz_ulong)compressed_size;
	uint32_t status = compress2(compressed_data, &compressed_size, out,
		(mz_ulong)offset, MZ_BEST_COMPRESSION);
	if (status != MZ_OK) {
		free(out);
		free(compressed_data);
		return NULL;
	}

	*out_size = compressed_size;
	free(out);
	return compressed_data;
}

static inline int32_t ReadLE32(const unsigned char* p) {
	return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) |
		((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

// Internal data struct
typedef struct {
	int32_t version;
	int32_t mode;
	int32_t xsize;
	int32_t ysize;
	int32_t mapsize;
	int32_t colorsize;
	int32_t screensize;
	int32_t backbuffers;
	int32_t backbufsel;
	int32_t par;
	int32_t overflow;
	int32_t palette;
	int32_t pcol;
	int32_t pmap;
	int32_t pscr;
	uint8_t autoselect;
	uint8_t cellsnap;
	uint8_t background_color;
	uint8_t multicolor0;
	uint8_t multicolor1;
	uint8_t __pad[3];
	uint32_t payload_offset;
} gpx_info;

static bool IsSupportedGPXMode(int32_t mode) {
	switch (mode) {
		case GPX_MODE_BITMAP:
		case GPX_MODE_MULTICOLOR_BITMAP:
		case GPX_MODE_SPRITE:
		case GPX_MODE_MULTICOLOR_SPRITE:
		case GPX_MODE_CHAR:
		case GPX_MODE_MULTICOLOR_CHAR:
		case GPX_MODE_UNRESTRICTED:
		case GPX_MODE_WIDE_UNRESTRICTED:
			return true;
		default:
			return false;
	}
}

static bool ValidateGPXPlanes(const gpx_info* info) {
	const uint64_t width = (uint32_t)info->xsize;
	const uint64_t height = (uint32_t)info->ysize;
	uint64_t cells;
	uint64_t required_map;
	uint64_t required_colors;
	uint64_t required_screen;

	switch (info->mode) {
		case GPX_MODE_BITMAP:
		case GPX_MODE_MULTICOLOR_BITMAP:
			if ((width & 7) != 0 || (height & 7) != 0) {
				return false;
			}
			cells = (width / 8) * (height / 8);
			required_map = cells * 8;
			required_colors = info->mode == GPX_MODE_MULTICOLOR_BITMAP ? cells : 0;
			required_screen = cells;
			break;
		case GPX_MODE_SPRITE:
		case GPX_MODE_MULTICOLOR_SPRITE:
			if (width % 24 != 0 || height % 21 != 0) {
				return false;
			}
			cells = (width / 24) * (height / 21);
			required_map = cells * 64;
			required_colors = cells;
			required_screen = 0;
			break;
		case GPX_MODE_CHAR:
		case GPX_MODE_MULTICOLOR_CHAR:
			if ((width & 7) != 0 || (height & 7) != 0) {
				return false;
			}
			cells = (width / 8) * (height / 8);
			required_map = cells * 8;
			required_colors = cells;
			required_screen = cells;
			break;
		case GPX_MODE_UNRESTRICTED:
			required_map = width * height;
			required_colors = 0;
			required_screen = 0;
			break;
		case GPX_MODE_WIDE_UNRESTRICTED:
			if (width & 1) {
				return false;
			}
			required_map = (width * height + 1) / 2;
			required_colors = 0;
			required_screen = 0;
			break;
		default:
			return false;
	}

	if ((uint64_t)info->mapsize < required_map ||
		(info->colorsize > 0 && (uint64_t)info->colorsize < required_colors) ||
		(info->screensize > 0 && (uint64_t)info->screensize < required_screen)) {
		return false;
	}
	if ((required_colors == 0 && info->colorsize > 0) ||
		(required_screen == 0 && info->screensize > 0)) {
		return false;
	}
	return true;
}

static gpx_data ExtractGPXData(const unsigned char* buffer, size_t buffer_size,
	const gpx_info* info) {
	gpx_data data = { 0 };
	size_t slot_size;
	size_t slot_index;
	size_t slot_offset;
	size_t map_offset;
	if (!buffer || !info) {
		return data;
	}
	if (info->xsize <= 0 || info->ysize <= 0 || info->mapsize <= 0 ||
		!IsSupportedGPXMode(info->mode)) {
		return data;
	}
	if (info->backbuffers <= 0) {
		return data;
	}
	slot_size = 64 + (size_t)info->mapsize + (size_t)info->screensize +
		(size_t)info->colorsize;
	slot_index = (size_t)(info->backbufsel >= 0 ? info->backbufsel : 0);
	if (slot_index >= (size_t)info->backbuffers) {
		return data;
	}
	slot_offset = 64 + slot_size * slot_index;
	map_offset = info->payload_offset + slot_offset + 64;
	size_t expected_size = map_offset + (size_t)info->mapsize +
		(size_t)info->colorsize + (size_t)info->screensize;

	if (expected_size > buffer_size) {
		return data;
	}

	data.pChars = buffer + map_offset;
	data.pColors = info->colorsize ? data.pChars + info->mapsize : 0;
	data.pScreen =
		info->screensize ? data.pChars + info->mapsize + info->colorsize : 0;
	data.width = (uint16_t)info->xsize;
	data.height = (uint16_t)info->ysize;
	data.mode = (gpx_mode)info->mode;
	data.background_color = info->background_color;
	data.multicolor0 = info->multicolor0;
	data.multicolor1 = info->multicolor1;
	return data;
}

gpx_status ParseGPX(const unsigned char* in_data, size_t in_size,
	gpx_data** ppData) {
	const unsigned char* p;
	const unsigned char* end;
	unsigned char* decompressed = NULL;
	mz_ulong decompressed_size = 0;
	int status;

	if (!ppData) {
		return GPX_ERR_INVALID_INPUT;
	}
	*ppData = NULL;

	if (!in_data || in_size < 16 || in_size > GPX_MAX_COMPRESSED_SIZE) {
		return GPX_ERR_INVALID_INPUT;
	}

	gpx_info info = { 0 };

	decompressed_size = (mz_ulong)(in_size > GPX_MAX_DECOMPRESSED_SIZE / 8 ?
		GPX_MAX_DECOMPRESSED_SIZE : in_size * 8);
	decompressed = (unsigned char*)malloc(decompressed_size);
	if (!decompressed) {
		return GPX_ERR_OUT_OF_MEMORY;
	}

	for (;;) {
		mz_ulong tmp_len = decompressed_size;
		status = uncompress(decompressed, &tmp_len, in_data, (mz_ulong)in_size);
		if (status == MZ_OK) {
			decompressed_size = tmp_len;
			break;
		}
		if (status == MZ_BUF_ERROR) {
			if (decompressed_size >= GPX_MAX_DECOMPRESSED_SIZE) {
				free(decompressed);
				return GPX_ERR_DECOMPRESSION_FAILED;
			}
			decompressed_size = decompressed_size > GPX_MAX_DECOMPRESSED_SIZE / 2 ?
				GPX_MAX_DECOMPRESSED_SIZE : decompressed_size * 2;
			unsigned char* new_decompressed = (unsigned char*)realloc(decompressed, decompressed_size);
			if (!new_decompressed) {
				if (decompressed) { free(decompressed); }
				return GPX_ERR_OUT_OF_MEMORY;
			}
			decompressed = new_decompressed;
			continue;
		}
		free(decompressed);
		return GPX_ERR_DECOMPRESSION_FAILED;
	}

	p = decompressed;
	end = decompressed + decompressed_size;

	if (decompressed_size < 12) {
		free(decompressed);
		return GPX_ERR_INVALID_INPUT;
	}

	info.version = ReadLE32(p);
	p += 4;
	info.mode = ReadLE32(p);
	p += 4;

	if (info.version < 0 || info.version > 4) {
		free(decompressed);
		return GPX_ERR_UNSUPPORTED_VERSION;
	}

	if (info.version == 0) {
		free(decompressed);
		return GPX_ERR_LEGACY_UNSUPPORTED;
	}

	if (info.version >= 4) {
		int32_t metacount = ReadLE32(p);
		int32_t i;
		p += 4;
		if (metacount < 0 || metacount > GPX_MAX_HEADER_SIZE) {
			free(decompressed);
			return GPX_ERR_BAD_METADATA;
		}

		for (i = 0; i < metacount; ++i) {
			if (p >= end) {
				free(decompressed);
				return GPX_ERR_TRUNCATED_METADATA;
			}
			const char* key = (const char*)p;
			while (p < end && *p) {
				p++;
			}
			if ( ( p + 1 ) >= end) {
				free(decompressed);
				return GPX_ERR_TRUNCATED_METADATA;
			}
			p++; // skip 0

			int32_t arg = 0;
			while (p < end && *p) {
				if (p + 1 >= end || p[1] != 0 || *p < '0' || *p > '9' ||
					arg > (INT32_MAX - (*p - '0')) / 10) {
					free(decompressed);
					return GPX_ERR_TRUNCATED_METADATA;
				}
				arg = arg * 10 + (*p - '0');
				p += 2;
			}
			if (p >= (end - 1)) {
				free(decompressed);
				return GPX_ERR_TRUNCATED_METADATA;
			}
			p += 2;

			if (strcmp(key, "xsize") == 0) {
				info.xsize = arg;
			} else if (strcmp(key, "ysize") == 0) {
				info.ysize = arg;
			} else if (strcmp(key, "mapsize") == 0) {
				info.mapsize = arg;
			} else if (strcmp(key, "colorsize") == 0) {
				info.colorsize = arg;
			} else if (strcmp(key, "screensize") == 0) {
				info.screensize = arg;
			} else if (strcmp(key, "backbuffers") == 0) {
				info.backbuffers = arg;
			} else if (strcmp(key, "backbufsel") == 0) {
				info.backbufsel = arg;
			} else if (strcmp(key, "par") == 0) {
				info.par = arg;
			} else if (strcmp(key, "overflow") == 0) {
				info.overflow = arg;
			} else if (strcmp(key, "autoselect") == 0) {
				info.autoselect = (uint8_t)arg;
			} else if (strcmp(key, "cellsnap") == 0) {
				info.cellsnap = (uint8_t)arg;
			} else if (strcmp(key, "palette") == 0) {
				info.palette = arg;
			} else if (strcmp(key, "pcol") == 0) {
				info.pcol = arg;
			} else if (strcmp(key, "pmap") == 0) {
				info.pmap = arg;
			} else if (strcmp(key, "pscr") == 0) {
				info.pscr = arg;
#ifdef DEBUG
			} else {
				printf("GPX Key: %s, Arg: %d\n", key, arg);
#endif
			}
		}
	}

	if (info.colorsize < 0 || info.mapsize < 0 || info.screensize < 0) {
		free(decompressed);
		return GPX_ERR_BAD_METADATA;
	}

	if (!IsSupportedGPXMode(info.mode)) {
		free(decompressed);
		return GPX_ERR_BAD_METADATA;
	}

	// give multicolor double width so it matches Pixcen exported png
	if (IsGPXMulticolor((gpx_mode)info.mode)) {
		if (info.xsize > INT32_MAX / 2) {
			free(decompressed);
			return GPX_ERR_BAD_METADATA;
		}
		info.xsize *= 2;
	}

	if (info.xsize <= 0 || info.xsize > MAX_CANVAS_WIDTH ||
		info.ysize <= 0 || info.ysize > MAX_CANVAS_HEIGHT ||
		info.mapsize <= 0 || info.backbuffers <= 0 ||
		(info.backbufsel >= info.backbuffers)) {
		free(decompressed);
		return GPX_ERR_BAD_METADATA;
	}

	if (!ValidateGPXPlanes(&info)) {
		free(decompressed);
		return GPX_ERR_BAD_METADATA;
	}

	const size_t payload_offset = (size_t)(p - decompressed);
	const uint64_t plane_size64 = (uint64_t)(uint32_t)info.mapsize +
		(uint64_t)(uint32_t)info.colorsize + (uint64_t)(uint32_t)info.screensize;
	const uint64_t slot_size64 = 64 + plane_size64;
	const size_t slot_index = (size_t)(info.backbufsel >= 0 ? info.backbufsel : 0);
	if (plane_size64 > SIZE_MAX - sizeof(gpx_data) ||
		slot_size64 > SIZE_MAX ||
		(uint64_t)payload_offset + 64 + slot_size64 * (uint64_t)info.backbuffers >
			(uint64_t)decompressed_size) {
		free(decompressed);
		return GPX_ERR_BAD_METADATA;
	}
	const size_t plane_size = (size_t)plane_size64;
	const size_t slot_size = (size_t)slot_size64;
	if (payload_offset > (size_t)decompressed_size ||
		slot_size > SIZE_MAX / (size_t)info.backbuffers) {
		free(decompressed);
		return GPX_ERR_BAD_METADATA;
	}
	info.payload_offset = (uint32_t)payload_offset;

	{
		size_t slot_offset = 64 + slot_size * slot_index;
		size_t slot_base = payload_offset + slot_offset;

		info.background_color = decompressed[slot_base + 60];
		info.multicolor0 = decompressed[slot_base + 61];
		info.multicolor1 = decompressed[slot_base + 62];
		if (info.background_color >= 16 || info.multicolor0 >= 16 ||
			info.multicolor1 >= 16) {
			free(decompressed);
			return GPX_ERR_BAD_METADATA;
		}
	}

	// allocate the gpx_data together with the display data
	const size_t data_size = sizeof(gpx_data) + plane_size;
	gpx_data* data = (gpx_data*)malloc(data_size);
	if (!data) {
		free(decompressed);
		return GPX_ERR_OUT_OF_MEMORY;
	}

	*data = ExtractGPXData(decompressed, decompressed_size, &info);

	// check that the extracted data is valid
	if (data->pChars == NULL || (info.colorsize > 0 && data->pColors == NULL) ||
		(info.screensize > 0 && data->pScreen == NULL)) {
		free(decompressed);
		free(data);
		return GPX_ERR_BAD_METADATA;
	}

	if (info.mapsize) {
		memcpy((uint8_t*)(data + 1), data->pChars, info.mapsize);
		data->pChars = (uint8_t*)(data + 1);
	}
	if (info.colorsize) {
		memcpy((uint8_t*)(data + 1) + info.mapsize, data->pColors, info.colorsize);
		data->pColors = (uint8_t*)(data + 1) + info.mapsize;
	}
	if (info.screensize) {
		memcpy((uint8_t*)(data + 1) + info.mapsize + info.colorsize, data->pScreen,
			info.screensize);
		data->pScreen = (uint8_t*)(data + 1) + info.mapsize + info.colorsize;
	}
	free(decompressed);

	*ppData = data;
	return GPX_OK;
}

uint8_t* CreateGPXImage(const gpx_data data, size_t* out_size, bool with_info) {
	const uint8_t* chars = data.pChars;
	const uint8_t* colors = data.pColors;
	const uint8_t* screen = data.pScreen;
	int w = data.width;
	int h = data.height;
	gpx_mode mode = data.mode;
	uint8_t bg = data.background_color;
	uint8_t mc0 = data.multicolor0;
	uint8_t mc1 = data.multicolor1;

	uint8_t* image = (uint8_t*)malloc((with_info ? 2 : 1) * w * h);
	if (!image) {
		return NULL;
	}
	if (out_size) {
		*out_size = (size_t)(w * h);
	}

	uint8_t* info = with_info ? ( image + w * h ) : NULL;

	if (mode == GPX_MODE_UNRESTRICTED) {
		memcpy(image, chars, w * h);
		if (info) { memset(info, GPX_PIXEL_UNRESTRICTED, w * h); }
		return image;
	} else if (mode == GPX_MODE_WIDE_UNRESTRICTED) {
		for (int i = 0, n = w * h; i < n; i++) {
			image[i] = chars[i >> 1];
		}
		if (info) { memset(info, GPX_PIXEL_UNRESTRICTED, w * h); }
		return image;
	}

	bool mc = mode == GPX_MODE_MULTICOLOR_BITMAP ||
		mode == GPX_MODE_MULTICOLOR_CHAR ||
		mode == GPX_MODE_MULTICOLOR_SPRITE;

	// Sprite based modes
	if (mode == GPX_MODE_SPRITE || mode == GPX_MODE_MULTICOLOR_SPRITE) {
		uint8_t color_lookup[4] = { bg, mc0, 0, mc1 };
		int sw = w / 24;
		for (int sy = 0, sh = h / 21; sy < sh; sy++) {
			for (int sx = 0; sx < sw; sx++) {
				color_lookup[mc ? 2 : 1] = *colors++;
				for (int y = 0; y < 21; y++) {
					for (int xb = 0; xb < 3; xb++) {
						uint8_t b = *chars++;
						for (int x = 0; x < 8; x++) {
							uint8_t color_index =
								mc ? ((b >> (~x & 6)) & 3) : ((b >> (~x & 7)) & 1);
							image[(sy * 21 + y) * w + (sx * 24 + xb * 8 + x)] =
								color_lookup[color_index];
							if (info) {
								info[(sy * 21 + y) * w + (sx * 24 + xb * 8 + x)] =
									color_index == 0 ? GPX_PIXEL_BG :
									(mc ? (color_index == 1 ? GPX_PIXEL_MC0 :
									(color_index == 2 ? GPX_PIXEL_FG : GPX_PIXEL_MC1)) :
									GPX_PIXEL_FG);
							}
						}
					}
				}
				chars++; // 1 extra byte per sprite
			}
		}
		return image;
	}

	// character based modes
	int cw = w >> 3;
	const uint8_t info_map[4] = { GPX_PIXEL_BG, mc ? GPX_PIXEL_MC0 : GPX_PIXEL_FG, GPX_PIXEL_MC1, GPX_PIXEL_FG };

	for (int cy = 0, ch = h >> 3; cy < ch; cy++) {
		for (int cx = 0; cx < cw; cx++) {
			const uint8_t screen_value = screen ? *screen++ : 0;
			const uint8_t color_value = colors ? *colors++ : 0;
			const uint8_t* chardata = chars;
			chars += 8;

			uint8_t color_map[4] = { bg, color_value, mc0, mc1 };
			switch (mode) {
				case GPX_MODE_MULTICOLOR_BITMAP:
					color_map[1] = screen_value >> 4;
					color_map[2] = screen_value & 0xf;
					color_map[3] = color_value & 0xf;
					break;
				case GPX_MODE_BITMAP:
					color_map[0] = screen_value & 0x0f;
					color_map[1] = (screen_value >> 4) & 0x0f;
					break;
				case GPX_MODE_CHAR:
					color_map[1] = color_value & 0x0f;
					break;
				case GPX_MODE_MULTICOLOR_CHAR:
					color_map[1] = mc0;
					color_map[2] = mc1;
					color_map[3] = color_value & 0xf;
					break;
				case GPX_MODE_SPRITE:
				case GPX_MODE_MULTICOLOR_SPRITE:
				case GPX_MODE_UNUSED1:
				case GPX_MODE_UNUSED2:
				case GPX_MODE_UNRESTRICTED:
				case GPX_MODE_WIDE_UNRESTRICTED:
				default:
					break;
			}
			for (int y = 0; y < 8; y++) {
				int index = (cy * 8 + y) * w + (cx * 8);
				for (int x = 0; x < 8; x++) {
					uint8_t b = chardata[y];
					if (mc) {
						uint8_t select = (b >> ((~x) & 6)) & 3;
						image[index] = color_map[select];
						if(info) { info[index] = info_map[select]; }
					} else {
						uint8_t select = (b >> ((~x) & 7)) & 1;
						image[index] = color_map[select];
						if (info) { info[index] = info_map[select]; }
					}
					++index;
				}
			}
		}
	}
	return image;

}

gpx_status GenerateGPXBitmap(const gpx_data* data, unsigned char** out_buffer,
	size_t* out_size) {

	if (!out_buffer) {
		return GPX_ERR_INVALID_INPUT;
	}

	*out_buffer = CreateGPXImage(*data, out_size, false);
	return *out_buffer ? GPX_OK : GPX_ERR_OUT_OF_MEMORY;
}

const char* GetGPXStatusString(gpx_status status) {
	switch (status) {
		case GPX_OK:
			return "ok";
		case GPX_ERR_INVALID_INPUT:
			return "invalid input";
		case GPX_ERR_OUT_OF_MEMORY:
			return "out of memory";
		case GPX_ERR_DECOMPRESSION_FAILED:
			return "decompression failed";
		case GPX_ERR_UNSUPPORTED_VERSION:
			return "unsupported version";
		case GPX_ERR_LEGACY_UNSUPPORTED:
			return "legacy GPX unsupported";
		case GPX_ERR_TRUNCATED_METADATA:
			return "truncated metadata";
		case GPX_ERR_BAD_METADATA:
			return "bad metadata";
		default:
			return "unknown error";
	}
}
