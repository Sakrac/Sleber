#include <stdint.h>
#include <math.h>
#include "palette.h"

typedef struct {
    double r, g, b;
} RGB;

static const uint8_t palette[16][3] = {
    {0, 0, 0},
    {255, 255, 255},
    {137, 64, 54},
    {122, 191, 199},
    {138, 70, 174},
    {104, 169, 65},
    {62, 49, 162},
    {208, 220, 113},
    {144, 95, 37},
    {92, 71, 0},
    {187, 119, 109},
    {85, 85, 85},
    {128, 128, 128},
    {172, 234, 136},
    {124, 112, 218},
    {171, 171, 171}
};

static int32_t col_diff[16][16];

static ColorLab cielab[16];

static uint32_t palette_abgr[16];
static uint32_t palette_argb[16];

static const uint8_t palette_ui[][3] = {
    {32,32,32}, // pixel grid $ff
    {0,128,48}, // char grid $fe
	{90, 68, 130}, // non-editable background $fd
};

const uint8_t *GetUIPalette(void) {
    return (uint8_t*)palette_ui;
}

uint32_t UIPaletteSize(void) {
    return sizeof(palette_ui) / sizeof(palette_ui[0]);
}

const uint8_t *GetC64Palette(void) {
    return (uint8_t*)palette;
}

const uint32_t *GetC64PaletteABGR(void) {
    return palette_abgr;
}

const uint32_t* GetC64PaletteARGB(void) {
	return palette_argb;
}

void GetPaletteColor(int index, uint8_t* r, uint8_t* g, uint8_t* b) {
    if (index < 0 || index >= 16) {
        index = 0;
    }
    if (r) *r = palette[index][0];
    if (g) *g = palette[index][1];
    if (b) *b = palette[index][2];
}    
    
static double SRGBToLinear(double value)
{
    value /= 255.0;

    if (value <= 0.04045)
        return value / 12.92;

    return pow((value + 0.055) / 1.055, 2.4);
}

static double LabF(double t)
{
    const double delta = 6.0 / 29.0;

    if (t > delta * delta * delta)
        return cbrt(t);

    return t / (3.0 * delta * delta) + 4.0 / 29.0;
}

ColorLab RGBToLab(RGB rgb)
{
    // Convert sRGB [0, 255] to linear RGB [0, 1]
    double r = SRGBToLinear(rgb.r);
    double g = SRGBToLinear(rgb.g);
    double b = SRGBToLinear(rgb.b);

    // Linear sRGB to XYZ, using the D65 matrix.
    // XYZ values here are scaled to a 0-100 range.
    double X = (0.4124564 * r +
                0.3575761 * g +
                0.1804375 * b) * 100.0;

    double Y = (0.2126729 * r +
                0.7151522 * g +
                0.0721750 * b) * 100.0;

    double Z = (0.0193339 * r +
                0.1191920 * g +
                0.9503041 * b) * 100.0;

    // D65 reference white, also scaled to 0-100.
    const double white_X = 95.047;
    const double white_Y = 100.000;
    const double white_Z = 108.883;

    double fx = LabF(X / white_X);
    double fy = LabF(Y / white_Y);
    double fz = LabF(Z / white_Z);

    ColorLab lab;

    lab.L = 116.0 * fy - 16.0;
    lab.a = 500.0 * (fx - fy);
    lab.b = 200.0 * (fy - fz);

    return lab;
}

ColorLab RGB8ToLab(uint8_t r, uint8_t g, uint8_t b) {
    return RGBToLab((RGB){.r = r, .g = g, .b = b});
}

ColorLab ColorToLab(uint8_t color) {
    return cielab[color];
}

uint32_t LabDistance(ColorLab lab1, ColorLab lab2) {
    double dL = lab1.L - lab2.L;
    double da = lab1.a - lab2.a;
    double db = lab1.b - lab2.b;
    double dist2 = sqrt(dL * dL + da * da + db * db);
    return (uint32_t)(dist2 * 256.0);
}


static double ColorDifferenceLab(int col_a, int col_b) {
    ColorLab lab1 = RGBToLab((RGB){.r = palette[col_a][0], .g = palette[col_a][1], .b = palette[col_a][2]});
    ColorLab lab2 = RGBToLab((RGB){.r = palette[col_b][0], .g = palette[col_b][1], .b = palette[col_b][2]});
    return LabDistance(lab1, lab2);
}

void CalculatePaletteDistances(void) {
    for (int c0=0; c0<16; ++c0) {
        for (int c1=0; c1<16; ++c1) {
            col_diff[c0][c1] = (int32_t)(ColorDifferenceLab(c0, c1) * 256.0);
        }
    }
    for(int i=0; i<16; ++i) {
        cielab[i] = RGBToLab((RGB){.r = palette[i][0], .g = palette[i][1], .b = palette[i][2]});
    }
    for(int i=0; i<16; ++i) {
        palette_abgr[i] = 0xff000000 | (palette[i][0] << 16) | (palette[i][1] << 8) | (palette[i][2]);
		palette_argb[i] = 0xff000000 | (palette[i][2] << 16) | (palette[i][1] << 8) | (palette[i][0]);
	}
}

int ColorDistance(int col_a, int col_b) {
    return col_diff[col_a][col_b];
}

uint32_t ColorDistanceLab(int col, ColorLab lab) {
    return LabDistance(lab, cielab[col]);
}

uint8_t GetClosestPaletteColor(uint8_t r, uint8_t g, uint8_t b) {
    ColorLab lab = RGBToLab((RGB){.r = r, .g = g, .b = b});
    uint8_t best_index = 0;
    double best_distance = 1e37;

    for (uint8_t i = 0; i < 16; ++i) {
        double dist2 = LabDistance(lab, cielab[i]);
        if (dist2 < best_distance) {
            best_index = i;
            best_distance = dist2;
        }
    }
    return best_index;
}
