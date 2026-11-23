#include "c64_data.h"
#include "sleber.h"

typedef union {
	uint8_t bytes[63];
	uint64_t unit[8];
} C64Sprite;

typedef struct {
	int8_t color[4];
} CharColors;

typedef struct {
	C64Char* chars;
	int numChars;
} C64CharData;

CharColors C64CharColor(const uint8_t* pixels, const uint8_t* types, int stride) {
	CharColors charColors = {{-1, -1, -1, -1}};
	for(int y = 0; y < 8; y++) {
		for(int x = 0; x < 8; x++) {
			uint8_t type = *types++;
			if (type >= 4 ||charColors.color[type] != -1) {
				++pixels;
			} else {
				charColors.color[type] = *pixels++;
			}
		}
		types += stride-8;
		pixels += stride-8;
	}
	return charColors;
}

int CountC64Chars(const uint8_t* types, int width, int height, int stride) {
	int charsWide = width / 8;
	int charsHigh = height / 8;
	int numChars = charsWide * charsHigh;
	C64Char *chars = (C64Char*)malloc(numChars * sizeof(C64Char));
	if (!chars) { return 0; }
	int uniqueChars = 0;
	const uint8_t* t = types;
	for(int y = 0; y < charsHigh; y++) {
		for(int x = 0; x < charsWide; x++) {
			const uint8_t *row = t;
			C64Char current = { 0 };
			for(int py=0; py<8; py++) {
				uint8_t b = 0;
				for(int px=0; px<8; px++) {
					b |= (*row++ & 1) << (7 - px);
				}
				current.bytes[py] = b;
				row += stride - 8;
			}
			t += 8;
			bool match = false;
			for (int u = 0; u < uniqueChars; u++) {
				if (current.unit == chars[u].unit) {
					match = true;
					break;
				}
			}
			if (!match) {
				chars[uniqueChars] = current;
				++uniqueChars;
			}
		}
		t += stride * 8 - charsWide * 8;
	}
	free(chars);
	return uniqueChars;
}

C64CharData CreateC64SingleColorData(const uint8_t* types, int width, int height) {
	int charsWide = width / 8;
	int charsHigh = height / 8;
	int numChars = charsWide * charsHigh;
	C64Char *chars = (C64Char*)malloc(numChars * sizeof(C64Char));
	if (!chars) { return (C64CharData) { 0 }; }

	C64Char *ch = chars;
	for(int y = 0; y < charsHigh; y++) {
		const uint8_t *t = types + y * width * 8;
		for(int x = 0; x < charsWide; x++) {
			const uint8_t *row = t;
			for(int py=0; py<8; py++) {
				uint8_t b = 0;
				for(int px=0; px<8; px++) {
					b |= (*row++ & 1) << (7 - px);
				}
				ch->bytes[py] = b;
				row += width;
			}
			t += 8;
			ch++;
		}
	}
	return (C64CharData){chars, numChars};
}

C64CharData CreateC64MixedCharData(const uint8_t* types, int width, int height) {
	int charsWide = width / 8;
	int charsHigh = height / 8;
	int numChars = charsWide * charsHigh;
	C64Char *chars = (C64Char*)malloc(numChars * sizeof(C64Char));
	if (!chars) { return (C64CharData) { 0 }; }
	C64Char *ch = chars;
	for(int y = 0; y < charsHigh; y++) {
		const uint8_t *t = types + y * width * 8;
		for(int x = 0; x < charsWide; x++) {
			const uint8_t *row = t;
			bool multiColor = false;
			for(int py=0; py<8 && !multiColor; py++) {
				for(int px=0; px<8; px++) {
					if (row[px]>1) {
						multiColor = true;
						break;
					}
				}
				row += width;
			}
			if(multiColor) {
				for(int py=0; py<8 && !multiColor; py++) {
					uint8_t b = 0;
					for(int px=0; px<8; px+=2) {
						b |= (*row++ & 3) << (6 - px);
					}
					ch->bytes[py] = b;
					row += width;
				}
			} else {
				for(int py=0; py<8 && !multiColor; py++) {
					uint8_t b = 0;
					for(int px=0; px<8; px++) {
						b |= (*row++ & 1) << (7 - px);
					}
					ch->bytes[py] = b;
					row += width;
				}
			}
			t += 8;
			ch++;
		}
	}
	return (C64CharData){chars, numChars};
}

C64ExportData BuildC64CharData(const uint8_t* pixels, const uint8_t* types, int width, int height) {
	C64CharData data = CreateC64SingleColorData(types, width, height);
	if (!data.chars) { return (C64ExportData) { 0 }; }

	C64Char* unique = (C64Char*)malloc(data.numChars * sizeof(C64Char));
	if (!unique) { free(data.chars); return (C64ExportData) { 0 }; }
	uint8_t* screen = (uint8_t*)malloc(data.numChars), *s = screen;
	if (!screen) { free(data.chars); free(unique); return (C64ExportData) { 0 }; }
	uint8_t* colors = (uint8_t*)malloc(data.numChars);
	if (!colors) { free(data.chars); free(unique); free(screen); return (C64ExportData) { 0 }; }

	int charsWide = width / 8;

	// just count unique characters before allocating
	int uniqueChars = 0;
	for(int c = 0; c < data.numChars; c++) {
		int match = uniqueChars;
		uint64_t curr = data.chars[c].unit;
		for(int u = 0; u < uniqueChars; u++) {
			if (unique[u].unit == curr) {
				match = u;
				break;
			}
		}
		*s++ = (uint8_t)match;  // this might overflow but that's a later problem.
		if(match == uniqueChars) {
			unique[uniqueChars].unit = curr;
			++uniqueChars;
		}

		int offset = (c / charsWide) * 8 * width + (c % charsWide) * 8;
		CharColors charColors = C64CharColor(pixels + offset, types + offset, width);
		colors[c] = charColors.color[1];
	}
	free(data.chars);
	return (C64ExportData){unique, screen, colors, uniqueChars, uniqueChars, width / 8, height / 8};
}



C64ExportData BuildC64MixedCharData(const uint8_t* pixels, const uint8_t* types, int width, int height) {
	C64CharData data = CreateC64SingleColorData(types, width, height);
	
	if (!data.chars) { return (C64ExportData) { 0 }; }

	C64Char* unique = (C64Char*)malloc(data.numChars * sizeof(C64Char));
	if (!unique) { free(data.chars); return (C64ExportData) { 0 }; }
	uint8_t* screen = (uint8_t*)malloc(data.numChars), *s = screen;
	if (!screen) { free(data.chars); free(unique); return (C64ExportData) { 0 }; }
	uint8_t* colors = (uint8_t*)malloc(data.numChars);
	if (!colors) { free(data.chars); free(unique); free(screen); return (C64ExportData) { 0 }; }

	int charsWide = width / 8;

	int uniqueChars = 0;
	for(int c = 0; c < data.numChars; c++) {
		int match = uniqueChars;
		uint64_t curr = data.chars[c].unit;
		for(int u = 0; u < uniqueChars; u++) {
			if (unique[u].unit == curr) {
				match = u;
				break;
			}
		}
		*s++ = (uint8_t)match;  // this might overflow but that's a later problem.
		if(match == uniqueChars) {
			unique[uniqueChars].unit = curr;
			++uniqueChars;
		}
		int offset = (c / charsWide) * 8 * width + (c % charsWide) * 8;
		CharColors charColors = C64CharColor(pixels + offset, types + offset, width);
		colors[c] = charColors.color[1];
	}
	free(data.chars);
	return (C64ExportData){unique, screen, colors, uniqueChars, uniqueChars, width / 8, height / 8};
}

C64ExportData CreateC64CharData(uint8_t mode, const uint8_t* pixels, const uint8_t* types, int width, int height) {
	switch(mode) {
		case SCREEN_MODE_CHAR:
			return BuildC64CharData(pixels, types, width, height);
			break;
		case SCREEN_MODE_CHAR_MIXED:
			return BuildC64MixedCharData(pixels, types, width, height);
			break;
		case SCREEN_MODE_CHAR_MULTICOLOR:
		default:
			break;
	}
	return (C64ExportData) { 0 };
}
