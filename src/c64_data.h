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

#endif // _C64_DATA_H