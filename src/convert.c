#include "sleber.h"
#include "palette.h"
#include "gpx_load.h"
#include "Edit.h"
#include "convert.h"

typedef struct ColorWeight {
	uint64_t count[COLOR_COUNT];
} ColorWeight;

typedef struct TwoColors {
	uint8_t col0, col1;
} TwoColors;

typedef struct ThreeColors {
	uint8_t col0, col1, col2;
} ThreeColors;

static const uint8_t Dither8x8[8][8] = {
	{ 0, 48, 12, 60, 3, 51, 15, 63 },
	{ 32, 16, 44, 28, 35, 19, 47, 31 },
	{ 8, 56, 4, 52, 11, 59, 7, 55 },
	{ 40, 24, 36, 20, 43, 27, 39, 23 },
	{ 2, 50, 14, 62, 1, 49, 13, 61 },
	{ 34, 18, 46, 30, 33, 17, 45, 29 },
	{ 10, 58, 6, 54, 9, 57, 5, 53 },
	{ 42, 26, 38, 22, 41, 25, 37, 21 }
};

static uint8_t* GetRGBImage(Sleber* app) {
	LockMutex(&app->canvas_mutex);
	const size_t pixel_count = CanvasMemorySize(app->width, app->height);
	if (pixel_count == 0) {
		UnlockMutex(&app->canvas_mutex);
		return NULL;
	}
	uint8_t* rgb_image = (uint8_t*)malloc(pixel_count * 3);
	if (!rgb_image) {
		DebugLog("Failed to allocate memory for RGB image");
		UnlockMutex(&app->canvas_mutex);
		return NULL;
	}
	const uint8_t* pixels = app->pixels;
	const uint8_t* Palette = GetC64Palette();
	uint8_t* rgb_ptr = rgb_image;
	for (uint32_t i = 0, n = (uint32_t)pixel_count; i < n; ++i) {
		const uint8_t* color = &Palette[*pixels++ * 3];
		*rgb_ptr++ = color[0];
		*rgb_ptr++ = color[1];
		*rgb_ptr++ = color[2];
	}
	UnlockMutex(&app->canvas_mutex);
	return rgb_image;
}


static ColorWeight CalculateColorWeight(const uint8_t* image, int w, int h, int stride, int channels, uint32_t* weights) {
	ColorWeight hist = { 0 };
	for(int py = 0; py < h; ++py) {
		for(int px = 0; px < w; ++px) {
			uint8_t r = image[px * channels], g = image[px * channels + 1], b = image[px * channels + 2];
			ColorLab lab_pixel = RGB8ToLab(r, g, b);
			uint32_t dist[4] = { ~0U, ~0U, ~0U, ~0U };
			uint8_t best_colors[4] = { 0, 1, 2, 3 };

			for (uint8_t i = 0; i < COLOR_COUNT; ++i) {
				uint32_t d = ColorDistanceLab(i, lab_pixel);
				if (d < dist[0]) {
					dist[3] = dist[2]; best_colors[3] = best_colors[2];
					dist[2] = dist[1]; best_colors[2] = best_colors[1];
					dist[1] = dist[0]; best_colors[1] = best_colors[0];
					dist[0] = d; best_colors[0] = i;
				} else if (d < dist[1]) {
					dist[3] = dist[2]; best_colors[3] = best_colors[2];
					dist[2] = dist[1]; best_colors[2] = best_colors[1];
					dist[1] = d; best_colors[1] = i;
				} else if (d < dist[2]) {
					dist[3] = dist[2]; best_colors[3] = best_colors[2];
					dist[2] = d; best_colors[2] = i;
				} else if (d < dist[3]) {
					dist[3] = d; best_colors[3] = i;
				}
			}
			hist.count[best_colors[0]] += weights[0];
			hist.count[best_colors[1]] += weights[1];
			hist.count[best_colors[2]] += weights[2];
			hist.count[best_colors[3]] += weights[3];
		}
		image += stride * channels;
	}
	return hist;
}

static uint32_t TwoColorWeights[] = { 1 << 16, 1 << 9, 1 << 2, 1 };
static uint32_t MulticolorBGWeights[] = { 1 << 12, 1 << 8, 1 << 4, 1 };

static uint8_t FindBestColor(const uint8_t *pixels, uint8_t bg, int w, int h, int stride, int channels) {
	DebugLog("Finding best color for image chunk: %dx%d, stride: %d, channels: %d", w, h, stride, channels);
	uint64_t closest = 0;
	uint8_t best_color = 0;
	ColorWeight priority = CalculateColorWeight(pixels, w, h, stride, channels, TwoColorWeights);
	for(uint8_t i = 0; i < COLOR_COUNT; ++i) {
		if(priority.count[i] > closest && i != bg) {
			closest = priority.count[i];
			best_color = i;
		}
	}
	return best_color;
}

static uint8_t FindBestLowerColor(const uint8_t *pixels, uint8_t bg, uint8_t mc0, uint8_t mc1, int w, int h, int stride, int channels) {
	DebugLog("Finding best lower color for image chunk: %dx%d, stride: %d, channels: %d", w, h, stride, channels);
	uint64_t closest = 0;
	uint8_t best_color = 0;
	ColorWeight priority = CalculateColorWeight(pixels, w, h, stride, channels, TwoColorWeights);
	for(uint8_t i = 0; i < COLOR_COUNT/2; ++i) {
		if(priority.count[i] > closest && i != bg && i != mc0 && i != mc1) {
			closest = priority.count[i];
			best_color = i;
		}
	}
	return best_color;
}

static TwoColors FindBestTwoColors(const uint8_t *pixels, int w, int h, int stride, int channels) {
	DebugLog("Finding best two colors for image chunk: %dx%d, stride: %d, channels: %d", w, h, stride, channels);
	TwoColors col = { 0, 1 };

	ColorWeight priority = CalculateColorWeight(pixels, w, h, stride, channels, TwoColorWeights);
	uint8_t c0 = 0, c1 = 1;
	uint64_t max0 = 0, max1 = 0;

	for(uint8_t i = 0; i < COLOR_COUNT; ++i) {
		if(priority.count[i] > max0) {
			max1 = max0;
			c1 = c0;
			max0 = priority.count[i];
			c0 = i;
		} else if(priority.count[i] > max1) {
			max1 = priority.count[i];
			c1 = i;
		}
	}
	col.col0 = c0;
	col.col1 = c1;
	return col;
}

static TwoColors FindBestTwoColorsECBM(const uint8_t* pixels, uint8_t* bg, int w, int h, int stride, int channels) {
	DebugLog("Finding best two colors for image chunk: %dx%d, stride: %d, channels: %d", w, h, stride, channels);
	TwoColors col = { 0, 1 };

	ColorWeight priority = CalculateColorWeight(pixels, w, h, stride, channels, TwoColorWeights);
	uint8_t c0 = 0, c1 = 1;
	uint64_t max0 = 0, max1 = 0;

	for (uint8_t i = 0; i < ECBM_BACKGROUND_COUNT; ++i) {
		if (priority.count[bg[i]] > max0) {
			max0 = priority.count[bg[i]];
			c0 = bg[i];
		}
	}
	for (uint8_t i = 0; i < COLOR_COUNT; ++i) {
		if (i != c0 && priority.count[i] > max1) {
			max1 = priority.count[i];
			c1 = i;
		}
	}
	col.col0 = c0;
	col.col1 = c1;
	return col;
}


static ThreeColors FindBestThreeColors(const uint8_t* pixels, uint8_t bg, int w, int h, int stride, int channels, int weigh_high) {
	DebugLog("Finding best three colors for image chunk: %dx%d, stride: %d, channels: %d", w, h, stride, channels);
	ThreeColors col = { 0, 1, 0 };

	ColorWeight priority = CalculateColorWeight(pixels, w, h, stride, channels, TwoColorWeights);
	uint8_t c0 = 0, c1 = 1, c2 = 3;
	uint64_t max0 = 0, max1 = 0, max2 = 0;

	for (uint8_t i = 0; i < COLOR_COUNT; ++i) {
		if (i == bg) { continue; }
		uint64_t weight = priority.count[i] + (weigh_high * priority.count[i]) / 256;
		if (weight > max0) {
			max2 = max1;
			max1 = max0;
			c2 = c1;
			c1 = c0;
			max0 = weight;
			c0 = i;
		} else if (weight > max1) {
			max2 = max1;
			max1 = weight;
			c2 = c1;
			c1 = i;
		} else if (weight > max2) {
			max2 = weight;
			c2 = i;
		}
	}
	col.col0 = c0;
	col.col1 = c1;
	col.col2 = c2;
	return col;
}

// counts number of foreground pixels in a chunk of an image, using LAB color distance to determine which pixels are closer to the background or foreground color
static uint32_t CountTwoColors(uint8_t *pixels, uint8_t bg, uint8_t fg, int w, int h, int stride, int channels) {
	DebugLog("Counting two colors in image chunk: %dx%d, stride: %d, channels: %d", w, h, stride, channels);
	uint32_t count = 0;
	for(int y = 0; y < h; ++y) {
		for(int x = 0; x < w; ++x) {
			uint8_t r = pixels[x * channels], g = pixels[x * channels + 1], b = pixels[x * channels + 2];
			ColorLab lab_pixel = RGB8ToLab(r, g, b);
			uint32_t d_bg = ColorDistanceLab(bg, lab_pixel);
			uint32_t d_fg = ColorDistanceLab(fg, lab_pixel);
			if(d_bg < d_fg) {
				++count;
			}
		}
		pixels += stride * channels;
	}
	return count;
}

static double DitherLevelMultiplier[] = { 0.0, 1.0 / 192.0, 1.0 / 64.0, 1.0 / 48.0 };

static double GetDitherMultiplier(int dither_level) {
	return DitherLevelMultiplier[Clamp(dither_level, 0,
		 (int)(sizeof(DitherLevelMultiplier) / sizeof(DitherLevelMultiplier[0])) - 1)];
}

static void ConvertToTwoColors(uint8_t* pixels, uint8_t* type, const uint8_t* rgb, uint8_t bg, uint8_t fg, int width, int height, int trg_stride, int rgb_channels, int rgb_stride, int dither_level) {
	DebugLog("Converting image chunk to two colors: %dx%d, trg_stride: %d, rgb_channels: %d, rgb_stride: %d", width, height, trg_stride, rgb_channels, rgb_stride);
	ColorLab lab_c_bg = ColorToLab(bg);
	ColorLab lab_c_fg = ColorToLab(fg);
	ColorLab lab_delta = { lab_c_fg.L - lab_c_bg.L, lab_c_fg.a - lab_c_bg.a, lab_c_fg.b - lab_c_bg.b };

	if(lab_delta.L < 0.0) {
		lab_delta.L = -lab_delta.L;
		lab_delta.a = -lab_delta.a;
		lab_delta.b = -lab_delta.b;
	}

	double dither_multiplier = GetDitherMultiplier(dither_level);

	for(int y = 0; y < height; ++y) {
		for(int x = 0; x < width; ++x) {
			double dither = ((int)Dither8x8[y & 7][x & 7] - 32) * dither_multiplier;
			uint8_t r = rgb[x * rgb_channels], g = rgb[x * rgb_channels + 1], b = rgb[x * rgb_channels + 2];
			ColorLab lab_pixel = RGB8ToLab(r, g, b);
			lab_pixel.L += lab_delta.L * dither;
			lab_pixel.a += lab_delta.a * dither;
			lab_pixel.b += lab_delta.b * dither;
			uint32_t d_bg = LabDistance(lab_pixel, lab_c_bg);
			uint32_t d_fg = LabDistance(lab_pixel, lab_c_fg);
			if(d_bg < d_fg) {
				pixels[x] = bg;
				type[x] = GPX_PIXEL_BG;
			} else {
				pixels[x] = fg;
				type[x] = GPX_PIXEL_FG;
			}
		}
		rgb += rgb_stride * rgb_channels;
		pixels += trg_stride;
		type += trg_stride;
	}
}

static void ConvertToFourColors(uint8_t* pixels, uint8_t* type, const uint8_t* rgb, uint8_t bg, uint8_t fg, uint8_t mc1, uint8_t mc2, int width, int height, int trg_stride, int rgb_channels, int rgb_stride, int dithadd, int dither_level) {
	ColorLab Labs[4] = { ColorToLab(bg), ColorToLab(fg), ColorToLab(mc1), ColorToLab(mc2) };
	uint8_t PInd[4] = { bg, fg, mc1, mc2 };
	static const uint8_t TInd[4] = { GPX_PIXEL_BG, GPX_PIXEL_FG, GPX_PIXEL_MC0, GPX_PIXEL_MC1 };

	double dither_multiplier = GetDitherMultiplier(dither_level);

	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; x+=2) {
			const uint8_t* a = &rgb[x * rgb_channels], *b = a + (x==width-1 ? 0 : rgb_channels);
			ColorLab lab_pixel = RGB8ToLab(a[0], a[1], a[2]);
			ColorLab lab_pixel2 = RGB8ToLab(b[0], b[1], b[2]);
			lab_pixel.L = (lab_pixel.L + lab_pixel2.L) * 0.5;
			lab_pixel.a = (lab_pixel.a + lab_pixel2.a) * 0.5;
			lab_pixel.b = (lab_pixel.b + lab_pixel2.b) * 0.5;
			uint8_t n0 = 0, n1 = 1;
			uint32_t d0 = LabDistance(lab_pixel, Labs[0]), d1 = UINT32_MAX ;
			for(uint8_t i = 1; i < 4; ++i) {
				uint32_t d = LabDistance(lab_pixel, Labs[i]);
				if(d < d0) {
					d1 = d0;
					n1 = n0;
					d0 = d;
					n0 = i;
				} else if(d < d1) {
					d1 = d;
					n1 = i;
				}
			}
			double dither = ((int)Dither8x8[y & 7][((x>>1) + dithadd) & 7] - 32) * dither_multiplier;
			ColorLab lab_delta = { Labs[n1].L - Labs[n0].L, Labs[n1].a - Labs[n0].a, Labs[n1].b - Labs[n0].b };
			if (lab_delta.L < 0.0) {
				lab_delta.L = -lab_delta.L;
				lab_delta.a = -lab_delta.a;
				lab_delta.b = -lab_delta.b;
			}
			lab_pixel.L += lab_delta.L * dither;
			lab_pixel.a += lab_delta.a * dither;
			lab_pixel.b += lab_delta.b * dither;

			uint32_t best = ~0U;
			uint8_t best_index = 0;
			for(uint8_t i = 0; i < 4; ++i) {
				uint32_t d = LabDistance(lab_pixel, Labs[i]);
				if(d < best) {
					best = d;
					best_index = i;
				}
			}
			pixels[x] = PInd[best_index];
			pixels[x+1] = PInd[best_index];
			type[x] = TInd[best_index];
			type[x + 1] = TInd[best_index];
		}
		rgb += rgb_stride * rgb_channels;
		pixels += trg_stride;
		type += trg_stride;
	}
}

static bool CheckTwoOrFourColors(const uint8_t* rgb, uint8_t bg, uint8_t fg, uint8_t mc1, uint8_t mc2, int width, int height, int rgb_channels, int rgb_stride, int dithadd, int dither_level) {
	ColorLab Labs[4] = { ColorToLab(bg), ColorToLab(fg), ColorToLab(mc1), ColorToLab(mc2) };
	uint8_t PInd[4] = { bg, fg, mc1, mc2 };
	bool used_colors[4] = { false, false, false, false };

	double dither_multiplier = GetDitherMultiplier(dither_level);

	int num_used_not_bg = 0;
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; x++) {
			const uint8_t* a = &rgb[x * rgb_channels], * b = a + rgb_channels;
			ColorLab lab_pixel = RGB8ToLab(a[0], a[1], a[2]);
			ColorLab lab_pixel2 = x == (width-1) ? lab_pixel : RGB8ToLab(b[0], b[1], b[2]);
			lab_pixel.L = (lab_pixel.L + lab_pixel2.L) * 0.5;
			lab_pixel.a = (lab_pixel.a + lab_pixel2.a) * 0.5;
			lab_pixel.b = (lab_pixel.b + lab_pixel2.b) * 0.5;
			uint8_t n0 = 0, n1 = 1;
			uint32_t d0 = LabDistance(lab_pixel, Labs[0]), d1 = UINT32_MAX;
			for (uint8_t i = 1; i < 4; ++i) {
				uint32_t d = LabDistance(lab_pixel, Labs[i]);
				if (d < d0) {
					d1 = d0;
					n1 = n0;
					d0 = d;
					n0 = i;
				} else if (d < d1) {
					d1 = d;
					n1 = i;
				}
			}
			double dither = ((int)Dither8x8[y & 7][((x >> 1) + dithadd) & 7] - 32) * dither_multiplier;
			ColorLab lab_delta = { Labs[n1].L - Labs[n0].L, Labs[n1].a - Labs[n0].a, Labs[n1].b - Labs[n0].b };
			if (lab_delta.L < 0.0) {
				lab_delta.L = -lab_delta.L;
				lab_delta.a = -lab_delta.a;
				lab_delta.b = -lab_delta.b;
			}
			lab_pixel.L += lab_delta.L * dither;
			lab_pixel.a += lab_delta.a * dither;
			lab_pixel.b += lab_delta.b * dither;

			uint32_t best = ~0U;
			uint8_t best_index = 0;
			for (uint8_t i = 0; i < 4; ++i) {
				uint32_t d = LabDistance(lab_pixel, Labs[i]);
				if (d < best) {
					best = d;
					best_index = i;
				}
			}
			if (!used_colors[best_index]) {
				used_colors[best_index] = true;
				if (best_index) {
					if (PInd[best_index] >= 8) {
						return false;
					}
					++num_used_not_bg;
					if(num_used_not_bg > 1) {
						return false;
					}
				}
			}
		}
		rgb += rgb_stride * rgb_channels;
	}
	return true;
}

static bool FinishConversion(Sleber* app, uint8_t* pixels, uint8_t* types, int width, int height, uint8_t mode) {
	uint8_t* pixels_prev = malloc(width * height);
	if (!pixels_prev) {
		free(pixels);
		free(types);
		return false;
	}

	AddUndoScreenMode(app, false);
	LockMutex(&app->canvas_mutex);
	if (app->pixels) { free(app->pixels); }
	if (app->pixels_info) { free(app->pixels_info); }
	if (app->pixels_prev) { free(app->pixels_prev); }

	app->pixels = pixels;
	app->pixels_info = types;
	app->width = width;
	app->height = height;
	app->pixels_prev = pixels_prev;
	SetScreenModeLocked(app, mode);

	// update previous pixels if allocation worked
	if (pixels_prev) {
		for (uint32_t i = 0, n = width * height; i < n; ++i) {
			*pixels_prev++ = (*pixels++) | ((*types++) << 4);
		}
	}
	UnlockMutex(&app->canvas_mutex);

	RequestRedraw(app);
	return true;
}

static void TwoColorWeightedCount(int image_height, int image_width, uint8_t* image, int image_channels, uint32_t* col_count) {
	for (int cy = 0; cy < image_height; cy += 8) {
		int ch = (cy + 8) < image_height ? 8 : (image_height - cy);
		for (int cx = 0; cx < image_width; cx += 8) {
			int cw = (cx + 8) < image_width ? 8 : (image_width - cx);
			TwoColors c2 = FindBestTwoColors(
				image + (image_width * cy + cx) * image_channels,
				cw, ch, image_width, image_channels);

			int numFG = CountTwoColors(
				image + (image_width * cy + cx) * image_channels,
				c2.col0, c2.col1, cw, ch, image_width, image_channels);

			int weight = 64 - abs(numFG - 32);
			col_count[c2.col0] += weight;
			if (numFG < 64 && c2.col1 != c2.col0) { col_count[c2.col1] += weight; }
		}
	}
}

static SleberImage Reserve8x8Image(int width, int height) {
	int w = (width + 7) & ~7;
	int h = (height + 7) & ~7;
	uint8_t* pixels = (uint8_t*)calloc(1, w * h);
	if (!pixels) { return (SleberImage){0}; }
	uint8_t* types = (uint8_t*)calloc(1, w * h);
	if (!types) { free(pixels); return (SleberImage){0}; }
	return (SleberImage){ pixels, types, w, h };
}

static SleberImage ReserveSpriteImage(int width, int height) {
	int w = SnapToUnitCeil(width, 24);
	int h = SnapToUnitCeil(height, 21);
	uint8_t* pixels = (uint8_t*)calloc(1, w * h);
	if (!pixels) { return (SleberImage) { 0 }; }
	uint8_t* types = (uint8_t*)calloc(1, w * h);
	if (!types) { free(pixels); return (SleberImage) { 0 }; }
	return (SleberImage) { pixels, types, w, h };
}

static SleberImage ConvertAreaToChar(const uint8_t* image, int image_width, int image_height, int image_channels, int dither_level, uint8_t bg) {
	SleberImage si = Reserve8x8Image(image_width, image_height);
	if (!si.pixels) { return (SleberImage){0}; }

	uint8_t* pixels = si.pixels,* types = si.types;
	int width = si.width, height = si.height;

	for (int cy = 0; cy < image_height; cy += 8) {
		int ch = (cy + 8) < image_height ? 8 : (image_height - cy);
		for (int cx = 0; cx < image_width; cx += 8) {
			int cw = (cx + 8) < image_width ? 8 : (image_width - cx);
			uint8_t fg = FindBestColor(
				image + (image_width * cy + cx) * image_channels,
				bg, cw, ch, image_width, image_channels);
			ConvertToTwoColors(
				pixels + cy * width + cx, types + cy * width + cx,
				image + (image_width * cy + cx) * image_channels,
				bg, fg, cw, ch, width, image_channels, image_width, dither_level);
		}
	}
	return (SleberImage) { pixels, types, width, height };
}

static SleberImage ConvertAreaToCharMC(const uint8_t* image, int image_width, int image_height, int image_channels, int dither_level, ThreeColors c3) {
	SleberImage si = Reserve8x8Image(image_width, image_height);
	if (!si.pixels) { return (SleberImage){0}; }

	uint8_t* pixels = si.pixels,* types = si.types;
	int width = si.width, height = si.height;

	for (int cy = 0; cy < image_height; cy += 8) {
		int ch = (cy + 8) < image_height ? 8 : (image_height - cy);
		for (int cx = 0; cx < image_width; cx += 8) {
			int cw = (cx + 8) < image_width ? 8 : (image_width - cx);
			uint8_t fg = FindBestLowerColor(
				image + (image_width * cy + cx) * image_channels,
				c3.col0, c3.col1, c3.col2, cw, ch, image_width, image_channels);
			ConvertToFourColors(
				pixels + cy * width + cx, types + cy * width + cx,
				image + (image_width * cy + cx) * image_channels,
				c3.col0, fg, c3.col1, c3.col2, cw, ch, width,
				image_channels, image_width, (cx & 8) ? 4 : 0, dither_level);
		}
	}
	return (SleberImage) { pixels, types, width, height };
}

static SleberImage ConvertAreaToCharMixed(const uint8_t* image, int image_width, int image_height, int image_channels, int dither_level, ThreeColors c3) {
	SleberImage si = Reserve8x8Image(image_width, image_height);
	if (!si.pixels) { return (SleberImage) { 0 }; }

	uint8_t* pixels = si.pixels, * types = si.types;
	int width = si.width, height = si.height;

	for (int cy = 0; cy < image_height; cy += 8) {
		int ch = (cy + 8) < image_height ? 8 : (image_height - cy);
		for (int cx = 0; cx < image_width; cx += 8) {
			int cw = (cx + 8) < image_width ? 8 : (image_width - cx);
			uint8_t fg = FindBestLowerColor(
				image + (image_width * cy + cx) * image_channels,
				c3.col0, 0xff, 0xff, cw, ch, image_width, image_channels);
			if( CheckTwoOrFourColors(
				image + (image_width * cy + cx) * image_channels,
				c3.col0, fg, c3.col1, c3.col2, cw, ch,
				image_channels, image_width, (cx & 8) ? 4 : 0, dither_level)) {
				ConvertToTwoColors(
					pixels + cy * width + cx, types + cy * width + cx,
					image + (image_width * cy + cx) * image_channels,
					c3.col0, fg, cw, ch, width,
					image_channels, image_width, dither_level);
			} else {
				fg = FindBestLowerColor(
					image + (image_width * cy + cx) * image_channels,
					c3.col0, c3.col1, c3.col2, cw, ch, image_width, image_channels);
				ConvertToFourColors(
					pixels + cy * width + cx, types + cy * width + cx,
					image + (image_width * cy + cx) * image_channels,
					c3.col0, fg, c3.col1, c3.col2, cw, ch, width,
					image_channels, image_width, (cx & 8) ? 4 : 0, dither_level);
			}
		}
	}
	return (SleberImage) { pixels, types, width, height };
}

static SleberImage ConvertAreaToBitmap(const uint8_t* image, int image_width, int image_height, int image_channels, int dither_level) {
	SleberImage si = Reserve8x8Image(image_width, image_height);
	if (!si.pixels) { return (SleberImage){0}; }
	uint8_t* pixels = si.pixels,* types = si.types;
	int width = si.width, height = si.height;
	for (int cy = 0; cy < image_height; cy += 8) {
		int ch = (cy + 8) < image_height ? 8 : (image_height - cy);
		for (int cx = 0; cx < image_width; cx += 8) {
			int cw = (cx + 8) < image_width ? 8 : (image_width - cx);
			TwoColors c2 = FindBestTwoColors(
				image + (image_width * cy + cx) * image_channels,
				cw, ch, image_width, image_channels);
			ConvertToTwoColors(
				pixels + cy * width + cx, types + cy * width + cx,
				image + (image_width * cy + cx) * image_channels,
				c2.col0, c2.col1, cw, ch, width, image_channels, image_width, dither_level);
		}
	}
	return (SleberImage) { pixels, types, width, height };
}

static SleberImage ConvertAreaToBitmapMC(const uint8_t* image, int image_width, int image_height, int image_channels, int dither_level, uint8_t bg) {
	SleberImage si = Reserve8x8Image(image_width, image_height);
	if (!si.pixels) { return (SleberImage){0}; }
	uint8_t* pixels = si.pixels,* types = si.types;
	int width = si.width, height = si.height;

	for (int cy = 0; cy < image_height; cy += 8) {
		int ch = (cy + 8) < image_height ? 8 : (image_height - cy);
		for (int cx = 0; cx < image_width; cx += 8) {
			int cw = (cx + 8) < image_width ? 8 : (image_width - cx);
			ThreeColors c3 = FindBestThreeColors(
				image + (image_width * cy + cx) * image_channels,
				bg, cw, ch, image_width, image_channels, 0);
			ConvertToFourColors(
				pixels + cy * width + cx, types + cy * width + cx,
				image + (image_width * cy + cx) * image_channels,
				bg, c3.col0, c3.col1, c3.col2, cw, ch, width,
				image_channels, image_width, (cx & 8) ? 4 : 0, dither_level);
		}
	}
	return (SleberImage) { pixels, types, width, height };
}

static SleberImage ConvertAreaToCharECBM(const uint8_t* image, int image_width, int image_height, int image_channels, int dither_level, uint8_t* bg) {
	SleberImage si = Reserve8x8Image(image_width, image_height);
	if (!si.pixels) { return (SleberImage){0}; }
	uint8_t* pixels = si.pixels,* types = si.types;
	int width = si.width, height = si.height;

	for (int cy = 0; cy < image_height; cy += 8) {
		int ch = (cy + 8) < image_height ? 8 : (image_height - cy);
		for (int cx = 0; cx < image_width; cx += 8) {
			int cw = (cx + 8) < image_width ? 8 : (image_width - cx);
			TwoColors col = FindBestTwoColorsECBM(
				image + (image_width * cy + cx) * image_channels,
				bg, cw, ch, image_width, image_channels);
			ConvertToTwoColors(
				pixels + cy * width + cx, types + cy * width + cx,
				image + (image_width * cy + cx) * image_channels,
				col.col0, col.col1, cw, ch, width, image_channels, image_width, dither_level);
		}
	}
	return (SleberImage) { pixels, types, width, height };
}

static SleberImage ConvertAreaToSprite(const uint8_t* image, int image_width, int image_height, int image_channels, int dither_level, uint8_t bg) {
	SleberImage si = ReserveSpriteImage(image_width, image_height);
	if (!si.pixels) { return (SleberImage) { 0 }; }
	uint8_t* pixels = si.pixels, * types = si.types;
	int width = si.width, height = si.height;
	for (int cy = 0; cy < image_height; cy += 21) {
		int ch = (cy + 21) < image_height ? 21 : (image_height - cy);
		for (int cx = 0; cx < image_width; cx += 24) {
			int cw = (cx + 24) < image_width ? 24 : (image_width - cx);
			uint8_t fg = FindBestColor(
				image + (image_width * cy + cx) * image_channels,
				bg, cw, ch, image_width, image_channels);
			ConvertToTwoColors(
				pixels + cy * width + cx, types + cy * width + cx,
				image + (image_width * cy + cx) * image_channels,
				bg, fg, cw, ch, width, image_channels, image_width, dither_level);
		}
	}
	return (SleberImage) { pixels, types, width, height };
}

static SleberImage ConvertAreaToSpriteMC(const uint8_t* image, int image_width, int image_height, int image_channels, int dither_level, ThreeColors c3) {
	SleberImage si = ReserveSpriteImage(image_width, image_height);
	if (!si.pixels) { return (SleberImage) { 0 }; }

	uint8_t* pixels = si.pixels, * types = si.types;
	int width = si.width, height = si.height;

	for (int cy = 0; cy < image_height; cy += 21) {
		int ch = (cy + 21) < image_height ? 21 : (image_height - cy);
		for (int cx = 0; cx < image_width; cx += 24) {
			int cw = (cx + 24) < image_width ? 24 : (image_width - cx);
			uint8_t fg = FindBestLowerColor(
				image + (image_width * cy + cx) * image_channels,
				c3.col0, c3.col1, c3.col2, cw, ch, image_width, image_channels);
			ConvertToFourColors(
				pixels + cy * width + cx, types + cy * width + cx,
				image + (image_width * cy + cx) * image_channels,
				c3.col0, fg, c3.col1, c3.col2, cw, ch, width,
				image_channels, image_width, (cx & 8) ? 4 : 0, dither_level);
		}
	}
	return (SleberImage) { pixels, types, width, height };
}

static bool ConvertToChar(Sleber* app, uint8_t* image, int image_width, int image_height, int image_channels) {
	DebugLog("Converting image to char: %dx%d, channels: %d", image_width, image_height, image_channels);

	// pick the background color
	uint32_t col_count[COLOR_COUNT] = { 0 };
	TwoColorWeightedCount(image_height, image_width, image, image_channels, &col_count[0]);

	uint8_t bg = 0;
	if (app->keep_import_background_colors) {
		bg = app->shared_colors[0];
	} else {
		uint32_t most = 0;
		for (uint8_t i = 0; i < COLOR_COUNT; ++i) {
			if (col_count[i] > most) {
				most = col_count[i];
				bg = i;
			}
		}
	}
	int dither_level = app->import_image_dither;

	SleberImage si = ConvertAreaToChar(image, image_width, image_height, image_channels, dither_level, bg);
	if (!si.pixels || !si.types) { return false; }
	
	if ( FinishConversion(app, si.pixels, si.types, si.width, si.height, SCREEN_MODE_CHAR)) {
		app->shared_colors[0] = bg;
		return true;
	}
	return false;
}

static bool ConvertToCharMC(Sleber* app, uint8_t* image, int image_width, int image_height, int image_channels) {
	DebugLog("Converting image to char multicolor: %dx%d, channels: %d", image_width, image_height, image_channels);

	ThreeColors c3 = { 0, 8, 13 };
	if (app->keep_import_background_colors) {
		c3.col0 = app->shared_colors[0];
		c3.col1 = app->shared_colors[1];
		c3.col2 = app->shared_colors[2];
	} else {
		c3 = FindBestThreeColors(
			image, 0xff, image_width, image_height, image_width, image_channels, 64);
	}

	int dither_level = app->import_image_dither;

	SleberImage si = ConvertAreaToCharMC(image, image_width, image_height, image_channels, dither_level, c3);
	if (!si.pixels || !si.types) { return false; }

	if (FinishConversion(app, si.pixels, si.types, si.width, si.height, SCREEN_MODE_CHAR_MULTICOLOR)) {
		app->shared_colors[0] = c3.col0;
		app->shared_colors[1] = c3.col1;
		app->shared_colors[2] = c3.col2;
		return true;
	}
	return false;
}

static bool ConvertToCharMixed(Sleber* app, uint8_t* image, int image_width, int image_height, int image_channels) {
	DebugLog("Converting image to char multicolor: %dx%d, channels: %d", image_width, image_height, image_channels);

	ThreeColors c3 = { 0, 8, 13 };
	if (app->keep_import_background_colors) {
		c3.col0 = app->shared_colors[0];
		c3.col1 = app->shared_colors[1];
		c3.col2 = app->shared_colors[2];
	} else {
		c3 = FindBestThreeColors(
			image, 0xff, image_width, image_height, image_width, image_channels, 64);
	}

	int dither_level = app->import_image_dither;

	SleberImage si = ConvertAreaToCharMixed(image, image_width, image_height, image_channels, dither_level, c3);
	if (!si.pixels || !si.types) { return false; }

	if (FinishConversion(app, si.pixels, si.types, si.width, si.height, SCREEN_MODE_CHAR_MIXED)) {
		app->shared_colors[0] = c3.col0;
		app->shared_colors[1] = c3.col1;
		app->shared_colors[2] = c3.col2;
		return true;
	}
	return false;
}

static bool ConvertToBitmap(Sleber* app, uint8_t* image, int image_width, int image_height, int image_channels) {
	DebugLog("Converting image to bitmap: %dx%d, channels: %d", image_width, image_height, image_channels);
	int dither_level = app->import_image_dither;

	SleberImage si = ConvertAreaToBitmap(image, image_width, image_height, image_channels, dither_level);
	if (!si.pixels || !si.types) { return false; }

	return FinishConversion(app, si.pixels, si.types, si.width, si.height, SCREEN_MODE_BITMAP);
}

static bool ConvertToBitmapMC(Sleber* app, uint8_t* image, int image_width, int image_height, int image_channels) {
	DebugLog("Converting image to bitmap: %dx%d, channels: %d", image_width, image_height, image_channels);

	uint8_t bg = 0;

	if (app->keep_import_background_colors) {
		bg = app->shared_colors[0];
	} else {
		ColorWeight hist = CalculateColorWeight(image, image_width, image_height, image_width, image_channels, MulticolorBGWeights);
		uint64_t bg_best = 0;
		for (uint8_t i = 0; i < COLOR_COUNT; ++i) {
			if (hist.count[i] > bg_best) {
				bg = i;
				bg_best = hist.count[i];
			}
		}
	}

	int dither_level = app->import_image_dither;

	SleberImage si = ConvertAreaToBitmapMC(image, image_width, image_height, image_channels, dither_level, bg);
	if (!si.pixels || !si.types) { return false; }

	if (FinishConversion(app, si.pixels, si.types, si.width, si.height, SCREEN_MODE_BITMAP_MULTICOLOR)) {
		app->shared_colors[0] = bg;
		return true;
	}
	return false;
}

static bool ConvertToCharECBM(Sleber* app, uint8_t* image, int image_width, int image_height, int image_channels) {
	DebugLog("Converting image to char: %dx%d, channels: %d", image_width, image_height, image_channels);
	// pick the background color
	uint8_t bg[ECBM_BACKGROUND_COUNT] = { 0, 1, 2, 3 };
	if (!app->keep_import_background_colors) {
		for(int i = 0; i < ECBM_BACKGROUND_COUNT; ++i) {
			bg[i] = app->shared_colors[i];
		}
	} else {
		uint32_t col_count[COLOR_COUNT] = { 0 };
		TwoColorWeightedCount(image_height, image_width, image, image_channels, col_count);

		uint32_t most[ECBM_BACKGROUND_COUNT] = { 0, 0, 0, 0 };
		for (uint8_t i = 0; i < COLOR_COUNT; ++i) {
			for (int j = 0; j < ECBM_BACKGROUND_COUNT; ++j) {
				if (col_count[i] > most[j]) {
					for (int k = ECBM_BACKGROUND_COUNT - 1; k > j; --k) {
						most[k] = most[k - 1];
						bg[k] = bg[k - 1];
					}
					most[j] = col_count[i];
					bg[j] = i;
					break;
				}
			}
		}
	}
	int dither_level = app->import_image_dither;

	SleberImage si = ConvertAreaToCharECBM(image, image_width, image_height, image_channels, dither_level, bg);
	if (!si.pixels || !si.types) { return false; }
	if (FinishConversion(app, si.pixels, si.types, si.width, si.height, SCREEN_MODE_ECBM)) {
		for (int i = 0; i < ECBM_BACKGROUND_COUNT; ++i) {
			app->shared_colors[i] = bg[i];
		}
		return true;
	}
	return false;
}

static bool ConvertToSprite(Sleber* app, uint8_t* image, int image_width, int image_height, int image_channels) {
	DebugLog("Converting image to sprite: %dx%d, channels: %d", image_width, image_height, image_channels);

	// pick the background color
	uint32_t col_count[COLOR_COUNT] = { 0 };
	TwoColorWeightedCount(image_height, image_width, image, image_channels, &col_count[0]);

	uint8_t bg = 0;
	if (app->keep_import_background_colors) {
		bg = app->shared_colors[0];
	} else {
		uint32_t most = 0;
		for (uint8_t i = 0; i < COLOR_COUNT; ++i) {
			if (col_count[i] > most) {
				most = col_count[i];
				bg = i;
			}
		}
	}
	int dither_level = app->import_image_dither;

	SleberImage si = ConvertAreaToSprite(image, image_width, image_height, image_channels, dither_level, bg);
	if (!si.pixels || !si.types) { return false; }

	if (FinishConversion(app, si.pixels, si.types, si.width, si.height, SCREEN_MODE_SPRITE)) {
		app->shared_colors[0] = bg;
		return true;
	}
	return false;
}

static bool ConvertToSpriteMC(Sleber* app, uint8_t* image, int image_width, int image_height, int image_channels) {
	DebugLog("Converting image to sprite multicolor: %dx%d, channels: %d", image_width, image_height, image_channels);

	ThreeColors c3 = { 0, 8, 13 };
	if (app->keep_import_background_colors) {
		c3.col0 = app->shared_colors[0];
		c3.col1 = app->shared_colors[1];
		c3.col2 = app->shared_colors[2];
	} else {
		c3 = FindBestThreeColors(
			image, 0xff, image_width, image_height, image_width, image_channels, 64);
	}

	int dither_level = app->import_image_dither;

	SleberImage si = ConvertAreaToSpriteMC(image, image_width, image_height, image_channels, dither_level, c3);
	if (!si.pixels || !si.types) { return false; }

	if (FinishConversion(app, si.pixels, si.types, si.width, si.height, SCREEN_MODE_SPRITE_MULTICOLOR)) {
		app->shared_colors[0] = c3.col0;
		app->shared_colors[1] = c3.col1;
		app->shared_colors[2] = c3.col2;
		return true;
	}
	return false;
}

static SleberImage ConvertImageToUnrestricted(int xs, const uint8_t *image, int image_width, int image_height, int image_channels) {
	int w = (image_width + (xs - 1)) & ~(xs - 1);
	int h = image_height;
	uint8_t* pixels = (uint8_t*)calloc(1, w * h), *row = pixels;
	if (!pixels) { return (SleberImage) { 0 }; }
	uint8_t* types = (uint8_t*)calloc(1, w * h);
	if (!types) { free(pixels); return (SleberImage) { 0 }; }
	const uint8_t *src = image;
	if (xs == 1) {
		for (int y = 0; y < h; ++y) {
			for (int x = 0; x < image_width; ++x) {
				uint8_t r = *src++;
				uint8_t g = *src++;
				uint8_t b = *src++;
				src += image_channels - 3;
				*row++ = (uint8_t)GetClosestPaletteColor(r, g, b);
			}
			row += w - image_width;
		}
	} else {
		for (int y = 0; y < h; ++y) {
			for (int x = 0; x < image_width; x+=2) {
				uint8_t r = *src++;
				uint8_t g = *src++;
				uint8_t b = *src++;
				src += image_channels - 3;
				if ((x + 1) < image_width) {
					r = (uint8_t)(((uint16_t)r + (uint16_t)*src++ + 1) >> 1);
					g = (uint8_t)(((uint16_t)g + (uint16_t)*src++ + 1) >> 1);
					b = (uint8_t)(((uint16_t)b + (uint16_t)*src++ + 1) >> 1);
					src += image_channels - 3;
				}
				*row++ = (uint8_t)GetClosestPaletteColor(r, g, b);
			}
			row += w - image_width;
		}
	}
	return (SleberImage) { pixels, types, w, h };
}

static bool ImportUnrestricted(Sleber *app, int xs, uint8_t *image, int image_width, int image_height, int image_channels) {
	SleberImage si = ConvertImageToUnrestricted(xs, image, image_width, image_height, image_channels);
	if (!si.pixels || !si.types) { return false; }
	return FinishConversion(app, si.pixels, si.types, si.width, si.height,
		xs == 1 ? SCREEN_MODE_UNRESTRICTED : SCREEN_MODE_WIDE_UNRESTRICTED);
}


bool ImportImage(Sleber* app, uint8_t *image, int image_width, int image_height, int image_channels) {
	switch (app->screen_mode) {
		case SCREEN_MODE_CHAR:
			return ConvertToChar(app, image, image_width, image_height, image_channels);
		case SCREEN_MODE_CHAR_MULTICOLOR:
			return ConvertToCharMC(app, image, image_width, image_height, image_channels);
		case SCREEN_MODE_CHAR_MIXED:
			return ConvertToCharMixed(app, image, image_width, image_height, image_channels);
		case SCREEN_MODE_BITMAP:
			return ConvertToBitmap(app, image, image_width, image_height, image_channels);
		case SCREEN_MODE_BITMAP_MULTICOLOR:
			return ConvertToBitmapMC(app, image, image_width, image_height, image_channels);
		case SCREEN_MODE_ECBM:
			return ConvertToCharECBM(app, image, image_width, image_height, image_channels);
		case SCREEN_MODE_SPRITE:
			return ConvertToSprite(app, image, image_width, image_height, image_channels);
		case SCREEN_MODE_SPRITE_MULTICOLOR:
			return ConvertToSpriteMC(app, image, image_width, image_height, image_channels);
		case SCREEN_MODE_UNRESTRICTED:
			return ImportUnrestricted(app, 1, image, image_width, image_height, image_channels);
		case SCREEN_MODE_WIDE_UNRESTRICTED:
			return ImportUnrestricted(app, 2, image, image_width, image_height, image_channels);
		default:
			return false;
	}
}

SleberImage ConvertArea(Sleber* app, const uint8_t* image, int image_width, int image_height, int image_channels) {
	switch (app->screen_mode) {
		case SCREEN_MODE_CHAR:
			return ConvertAreaToChar(image, image_width, image_height, image_channels, app->import_image_dither, app->shared_colors[0]);
		case SCREEN_MODE_CHAR_MULTICOLOR:
			return ConvertAreaToCharMC(image, image_width, image_height, image_channels, app->import_image_dither,
				(ThreeColors) {
				app->shared_colors[0], app->shared_colors[1], app->shared_colors[2]
			});
		case SCREEN_MODE_CHAR_MIXED:
			return ConvertAreaToCharMixed(image, image_width, image_height, image_channels,
				app->import_image_dither, (ThreeColors) {
				app->shared_colors[0], app->shared_colors[1], app->shared_colors[2]
			});
		case SCREEN_MODE_BITMAP:
			return ConvertAreaToBitmap(image, image_width, image_height, image_channels, app->import_image_dither);
		case SCREEN_MODE_BITMAP_MULTICOLOR:
			return ConvertAreaToBitmapMC(image, image_width, image_height, image_channels, app->import_image_dither, app->shared_colors[0]);
		case SCREEN_MODE_ECBM:
			return ConvertAreaToCharECBM(image, image_width, image_height, image_channels, app->import_image_dither, app->shared_colors);
		case SCREEN_MODE_SPRITE:
			return ConvertAreaToSprite(image, image_width, image_height, image_channels, app->import_image_dither, app->shared_colors[0]);
		case SCREEN_MODE_SPRITE_MULTICOLOR:
			return ConvertAreaToSpriteMC(image, image_width, image_height, image_channels, app->import_image_dither, (ThreeColors) {
				app->shared_colors[0], app->shared_colors[1], app->shared_colors[2] });
		case SCREEN_MODE_UNRESTRICTED:
			return ConvertImageToUnrestricted(1, image, image_width, image_height, image_channels);
		case SCREEN_MODE_WIDE_UNRESTRICTED:
			return ConvertImageToUnrestricted(2, image, image_width, image_height, image_channels);
		default:
			break;
	}
	return (SleberImage) { 0 };
}


bool SwitchScreenMode(Sleber* app, uint8_t new_mode) {
	if (new_mode == app->screen_mode) { return true; }
	switch(new_mode) {
		case SCREEN_MODE_CHAR: {
			uint8_t* rgb_image = GetRGBImage(app);
			if (!rgb_image) { return false; }
			bool result = ConvertToChar(app, rgb_image, app->width, app->height, 3);
			free(rgb_image);
			return result;
		}
		case SCREEN_MODE_CHAR_MULTICOLOR: {
			uint8_t* rgb_image = GetRGBImage(app);
			if (!rgb_image) { return false; }
			bool result = ConvertToCharMC(app, rgb_image, app->width, app->height, 3);
			free(rgb_image);
			return result;
		}
		case SCREEN_MODE_BITMAP: {
			uint8_t* rgb_image = GetRGBImage(app);
			if (!rgb_image) { return false; }
			bool result = ConvertToBitmap(app, rgb_image, app->width, app->height, 3);
			free(rgb_image);
			return result;
		}
		case SCREEN_MODE_BITMAP_MULTICOLOR: {
			uint8_t* rgb_image = GetRGBImage(app);
			if (!rgb_image) { return false; }
			bool result = ConvertToBitmapMC(app, rgb_image, app->width, app->height, 3);
			free(rgb_image);
			return result;
		}
		case SCREEN_MODE_ECBM: {
			uint8_t* rgb_image = GetRGBImage(app);
			if (!rgb_image) { return false; }
			bool result = ConvertToCharECBM(app, rgb_image, app->width, app->height, 3);
			free(rgb_image);
			return result;
		}
		case SCREEN_MODE_CHAR_MIXED: {
			uint8_t* rgb_image = GetRGBImage(app);
			if (!rgb_image) { return false; }
			bool result = ConvertToCharMixed(app, rgb_image, app->width, app->height, 3);
			free(rgb_image);
			return result;
		}
		case SCREEN_MODE_SPRITE: {
			uint8_t* rgb_image = GetRGBImage(app);
			if (!rgb_image) { return false; }
			bool result = ConvertToSprite(app, rgb_image, app->width, app->height, 3);
			free(rgb_image);
			return result;
		}
		case SCREEN_MODE_SPRITE_MULTICOLOR: {
			uint8_t* rgb_image = GetRGBImage(app);
			if (!rgb_image) { return false; }
			bool result = ConvertToSpriteMC(app, rgb_image, app->width, app->height, 3);
			free(rgb_image);
			return result;
		}
		case SCREEN_MODE_UNRESTRICTED:
		case SCREEN_MODE_WIDE_UNRESTRICTED:
		{
			uint8_t* rgb_image = GetRGBImage(app);
			if (!rgb_image) { return false; }
			bool result = ImportUnrestricted(
				app, new_mode == SCREEN_MODE_WIDE_UNRESTRICTED ? 2 : 1,
				rgb_image, app->width, app->height, 3);
			free(rgb_image);
			return result;
		}
	}
	return false;
}