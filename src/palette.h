#ifndef PALETTE_H
#define PALETTE_H

#include <stdint.h>

typedef struct {
    double L, a, b;
} ColorLab;

const uint8_t* GetUIPalette();
uint32_t UIPaletteSize(void);
const uint8_t* GetC64Palette();
const uint32_t* GetC64PaletteABGR(void);
const uint32_t* GetC64PaletteARGB(void);
void GetPaletteColor(int index, uint8_t* r, uint8_t* g, uint8_t* b);
uint8_t GetClosestPaletteColor(uint8_t r, uint8_t g, uint8_t b);
void CalculatePaletteDistances();
ColorLab RGB8ToLab(uint8_t r, uint8_t g, uint8_t b);
ColorLab ColorToLab(uint8_t color);
uint32_t LabDistance(ColorLab lab1, ColorLab lab2);
int ColorDistance(int col_a, int col_b);
uint32_t ColorDistanceLab(int col, ColorLab lab);

#endif
