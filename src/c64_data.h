#ifndef _C64_DATA_H
#define _C64_DATA_H

#include <stdint.h>

typedef union {
	uint8_t bytes[8];
	uint64_t unit;
} C64Char;

typedef struct {
	C64Char* chars;
	uint8_t* screen;
	uint8_t* colors; // NULL for bitmap
	int numChars;
	int screenSize;
	int charsWide;
	int charsHigh;
} C64ExportData;

// count unique chars for screen indexed modes
int CountC64Chars(const uint8_t* types, int width, int height, int stride);

// build the binary data for exporting
C64ExportData CreateC64CharData(uint8_t mode, const uint8_t* pixels, const uint8_t* types, int width, int height);

#endif // _C64_DATA_H