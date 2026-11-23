

#include <stdint.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#endif

#include "palette.h"
#include "platform.h"
#include "sleber.h"
#include "Edit.h"
#include "convert.h"
#include "gpx_load.h"
#include "c64_data.h"

#define PALETTE_HOVER_MARGIN 3
#define PALETTE_HOVER_HIRES_WIDTH 20
#define PALETTE_SHARED_ECBM_ROW ((PALETTE_SHARED_COLOR_HEIGHT-2*PALETTE_HOVER_MARGIN) / 3)

#ifndef _WIN32
// just do this as a global palette for now
static uint32_t s8BitPalette[256];
#endif

void InitBitmapPalette(void)
{
#ifndef _WIN32
	const uint8_t* palc64 = GetC64Palette();
	// Fill palette
	for (int i = 0; i < 16; i++) {
		s8BitPalette[i] = 0xff000000 | (palc64[i * 3 + 0] << 16) | (palc64[i * 3 + 1] << 8) | (palc64[i * 3 + 2]);
	}

	const uint8_t* palui = GetUIPalette();
	for (int u = 0, n = UIPaletteSize(); u < n; ++u) {
		int i = 255 - u;
		s8BitPalette[i] = 0xff000000 |(palui[u * 3 + 0] << 16) | (palui[u * 3 + 1] << 8) | (palui[u * 3 + 2]);
	}
#endif
}


int PixelSize(Sleber *app)
{
	return app->zoom >> 3;
}

// TODO: Review shared code from platform implementation
int ScreenModeHorizontalUnit(uint8_t screen_mode) {
	return (screen_mode == SCREEN_MODE_SPRITE || screen_mode == SCREEN_MODE_SPRITE_MULTICOLOR) ? 24 : 8;
}

// TODO: Review shared code from platform implementation
int ScreenModeVerticalUnit(uint8_t screen_mode) {
	return (screen_mode == SCREEN_MODE_SPRITE || screen_mode == SCREEN_MODE_SPRITE_MULTICOLOR) ? 21 : 8;
}

Area ClampArea(const Area area, const Area bounds) {
	uint16_t x0 = Clamp(area.x, bounds.x, bounds.x + bounds.w);
	uint16_t x1 = Clamp(area.x + area.w, bounds.x, bounds.x + bounds.w);
	uint16_t y0 = Clamp(area.y, bounds.y, bounds.y + bounds.h);
	uint16_t y1 = Clamp(area.y + area.h, bounds.y, bounds.y + bounds.h);
	return (Area) { x0, y0, x1 - x0, y1 - y0 };
}

Area UnionArea(const Area a, const Area b) {
	uint16_t x0 = MIN(a.x, b.x);
	uint16_t y0 = MIN(a.y, b.y);
	uint16_t x1 = MAX(a.x + a.w, b.x + b.w);
	uint16_t y1 = MAX(a.y + a.h, b.y + b.h);
	return (Area) { x0, y0, x1 - x0, y1 - y0 };
}

I32Point SnapToGrid(Sleber* app, const I32Point point) {
	if (!app->snap_to_grid) {
		return point;
	}
	if(app->screen_mode == SCREEN_MODE_SPRITE || app->screen_mode == SCREEN_MODE_SPRITE_MULTICOLOR) {
		return (I32Point) { (point.x / 24) * 24, (point.y / 21) * 21 };
	}
	return (I32Point) { point.x & ~7, point.y & ~7 };
}

// Note: This snaps to grid unconditionally since this is not related to the user option
Area SnapAreaToUnit(uint8_t screen_mode, const Area area) {
	uint16_t x0 = area.x, y0 = area.y, x1 = area.x + area.w, y1 = area.y + area.h;

	if(screen_mode == SCREEN_MODE_SPRITE || screen_mode == SCREEN_MODE_SPRITE_MULTICOLOR) {
		x0 = (x0 / 24) * 24; y0 = (y0 / 21) * 21; x1 = ((x1 + 23) / 24) * 24; y1 = ((y1 + 20) / 21) * 21;
	} else {
		x0 &= ~7; y0 &= ~7; x1 = (x1 + 7) & ~7; y1 = (y1 + 7) & ~7;
	}

	return (Area) { x0, y0, x1 - x0, y1 - y0 };
}

int MaxZoomValue(void)
{
	return (32 << 3) - 1;
}

int MinZoomValue(void)
{
	return 1<<3;
}

void SetForegroundColor(Sleber* app, int index) {
	if (index < 0 || index >= COLOR_COUNT) {
		return;
	}
	const uint8_t old_color = app->foreground_color;
	app->foreground_color = (uint8_t)index;
	RequestRedrawPaletteColors(app, (uint16_t)(1u << index) |
		(uint16_t)(1u << old_color));
	RefreshPaletteUI(app);
	char message[64];
	snprintf(message, sizeof(message), "Foreground: %X", index);
	UpdateStatus(app, message);
}

void SetBackgroundColor(Sleber* app, int index) {
	if (index < 0 || index >= COLOR_COUNT) {
		return;
	}
	const uint8_t old_color = app->background_color;
	app->background_color = (uint8_t)index;
	RequestRedrawPaletteColors(app, (uint16_t)(1u << index) |
		(uint16_t)(1u << old_color));
	RefreshPaletteUI(app);
	char message[64];
	snprintf(message, sizeof(message), "Background: %X", index);
	UpdateStatus(app, message);
}


#ifdef _WIN32
#define ToUIPixel(x) ((UIPixel)(x))
#else
#define ToUIPixel(x) (s8BitPalette[x])
#endif

static void DrawCurrCharColors(Sleber* app, UIPixel* pixels, int width, int height, uint8_t fg_color_index) {
	UIPixel border_color = ToUIPixel(0xfd);
	UIPixel bg_color = ToUIPixel(app->hover_background_color);
	UIPixel fg_color = ToUIPixel(fg_color_index);

	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			if (y > PALETTE_HOVER_MARGIN && y<height - PALETTE_HOVER_MARGIN && x>PALETTE_HOVER_MARGIN && x < width - PALETTE_HOVER_MARGIN) {
				if (y < (PALETTE_HOVER_HIRES_WIDTH + PALETTE_HOVER_MARGIN) &&
					x < (PALETTE_HOVER_HIRES_WIDTH + PALETTE_HOVER_MARGIN)) {
					*pixels++ = fg_color;
				} else if (y > (height - (PALETTE_HOVER_HIRES_WIDTH + PALETTE_HOVER_MARGIN)) &&
					x > ((width - (PALETTE_HOVER_HIRES_WIDTH + PALETTE_HOVER_MARGIN)))) {
					*pixels++ = bg_color;
				} else { *pixels++ = border_color; }
			} else { *pixels++ = border_color; }
		}
	}
}

static void DrawCurrMCCharColors(Sleber* app, UIPixel* pixel_hover, int width, int height, uint8_t fg_color, uint8_t mc0_color, uint8_t mc1_color) {
	UIPixel bg_color = ToUIPixel(app->hover_background_color);
	UIPixel border_color = ToUIPixel(0xfd);
	UIPixel fg = ToUIPixel(fg_color);
	UIPixel mc0 = ToUIPixel(mc0_color);
	UIPixel mc1 = ToUIPixel(mc1_color);

	for (int y = 0; y < height; ++y) {
		UIPixel* pixel_x = pixel_hover + y * width;
		for (int x = 0; x < width; ++x) {
			if (y > PALETTE_HOVER_MARGIN && y<height - PALETTE_HOVER_MARGIN && x>PALETTE_HOVER_MARGIN && x < width - PALETTE_HOVER_MARGIN) {
				*pixel_x++ = (2 * y) < height ? ((2 * x) < width) ? bg_color : fg : ((2 * x) < width) ? mc0 : mc1;
			} else { *pixel_x++ = border_color; }
		}
	}
}

static void DrawCurrECBMCharColors(Sleber* app, UIPixel* pixel_hover, int width, int height, uint8_t fg_color) {
	UIPixel border_color = ToUIPixel(0xfd);
	UIPixel ec0 = ToUIPixel(app->shared_colors[0]);
	UIPixel ec1 = ToUIPixel(app->shared_colors[1]);
	UIPixel ec2 = ToUIPixel(app->shared_colors[2]);
	UIPixel ec3 = ToUIPixel(app->shared_colors[3]);

	int shared_width = width - 2 * PALETTE_HOVER_MARGIN;
	int shared_height = height - 2 * PALETTE_HOVER_MARGIN;

	for (int y = 0; y < height; ++y) {
		UIPixel* pixel_x = pixel_hover + y * width;
		for (int x = 0; x < width; ++x) {
			int yc = y - PALETTE_HOVER_MARGIN, xc = x - PALETTE_HOVER_MARGIN;
			if (yc >= 0 && xc >= 0 && yc < shared_height && xc < shared_width) {
				if (yc >= (2 * PALETTE_SHARED_ECBM_ROW)) {
					*pixel_x++ = fg_color;
				} else if (yc >= PALETTE_SHARED_ECBM_ROW) {
					*pixel_x++ = (xc < (shared_width / 2)) ? ec2 : ec3;
				} else {
					*pixel_x++ = (xc < (shared_width / 2)) ? ec0 : ec1;
				}
			} else {
				*pixel_x++ = border_color;
			}
		}
	}
}

static uint8_t IsBright(uint8_t color) {
	switch (color) {
		case 1:
		case 3:
		case 7:
		case 10:
		case 13:
		case 14:
		case 15:
			return 1;
		default:
			return 0;
	}
}

static void DrawPalette(Sleber* app, UIPixel* pixels, int width, int height,
	uint16_t dirty_colors, bool dirty_preview) {
	int palhgt = height - PALETTE_SHARED_COLOR_HEIGHT;
	if (palhgt <= 0) { return; }

	const int cell_hgt = PALETTE_SQUARE;
	const int cell_wid = PALETTE_SQUARE;
	UIPixel* pixel_row = pixels;
	for (int row = 0; row < 8; ++row) {
		if (row * cell_hgt >= height) { break; }
		for (int col = 0; col < 2; ++col) {
			const uint8_t color = (uint8_t)(row + (col << 3));
			if (!(dirty_colors & (uint16_t)(1u << color))) { continue; }
			UIPixel* pixel_x = pixel_row + col * cell_wid;
			UIPixel c = ToUIPixel(color);
			int draw_hgt = (row + 1) * cell_hgt > height ? height - row * cell_hgt : cell_hgt;
			for (int y = 0; y < draw_hgt; ++y) {
				for (int x = 0; x < cell_wid; ++x) {
					pixel_x[x] = c;
				}
				pixel_x += width;
			}
		}
		pixel_row += width * cell_hgt;
	}

	uint8_t fg = app->foreground_color;
	uint8_t bg = app->background_color;

	UIPixel fc = ToUIPixel(IsBright(fg) ? 0 : 1);
	UIPixel bc = ToUIPixel(IsBright(bg) ? 0 : 1);

	int select_wid = cell_wid - 2;

	if (dirty_colors & (uint16_t)(1u << fg)) {
		UIPixel* pixel_fg = pixels +
			(fg & 7) * cell_hgt * width + (fg >> 3) * cell_wid + width + 1;
		UIPixel* pixel_fl = pixel_fg + (select_wid - 1) * width;
		for (int y = 0; y < select_wid; ++y) {
			pixel_fg[y] = fc;
			pixel_fl[y] = fc;
			pixel_fg[y * width] = fc;
			pixel_fg[y * width + select_wid - 1] = fc;
		}
	}

	if (dirty_colors & (uint16_t)(1u << bg)) {
		UIPixel* pixel_bg = pixels +
			(bg & 7) * cell_hgt * width + (bg >> 3) * cell_wid + width + 1;
		for (int y = 0; y < select_wid; ++y) {
			pixel_bg[y * (1 + width)] = bc;
			pixel_bg[y * (width - 1) + select_wid - 1] = bc;
		}
	}

	if (!dirty_preview) { return; }
	int height_hover = height - (8 * (PALETTE_SQUARE + PALETTE_GAP));
	if (height_hover <= 0) { return; }

	UIPixel* pixel_hover = pixels + width * (8 * (PALETTE_SQUARE + PALETTE_GAP));
	switch (app->screen_mode) {
		case SCREEN_MODE_CHAR:
			DrawCurrCharColors(app, pixel_hover, width, height_hover,
				app->hover_foreground_color & 0xf);
			break;
		case SCREEN_MODE_CHAR_MIXED:
		case SCREEN_MODE_CHAR_MULTICOLOR:
			DrawCurrMCCharColors(app, pixel_hover, width, height_hover,
				app->hover_foreground_color & 0xf,
				app->shared_colors[1] & 0xf, app->shared_colors[2] & 0xf);
			break;
		case SCREEN_MODE_BITMAP:
			DrawCurrCharColors(app, pixel_hover, width, height_hover,
				app->hover_foreground_color & 0xf);
			break;
		case SCREEN_MODE_BITMAP_MULTICOLOR:
			DrawCurrMCCharColors(app, pixel_hover, width, height_hover,
				app->hover_foreground_color & 0xf,
				app->hover_multicolor0, app->hover_multicolor1);
			break;
		case SCREEN_MODE_SPRITE:
			DrawCurrCharColors(app, pixel_hover, width, height_hover,
				app->hover_foreground_color & 0xf);
			break;
		case SCREEN_MODE_SPRITE_MULTICOLOR:
			DrawCurrMCCharColors(app, pixel_hover, width, height_hover,
				app->hover_foreground_color & 0xf,
				app->shared_colors[1] & 0xf, app->shared_colors[2] & 0xf);
			break;
		case SCREEN_MODE_ECBM:
			DrawCurrECBMCharColors(app, pixel_hover, width, height_hover,
				app->hover_foreground_color & 0xf);
			break;
		case SCREEN_MODE_UNRESTRICTED:
		case SCREEN_MODE_WIDE_UNRESTRICTED:
			break;
		default:
			break;
	}
}

static void DrawBrushInBitmap(Sleber* app, int step, int width, int height, UIPixel* pixels, int inc, Area DrawArea) {
	// grab the values from app
	const int cw = app->width, ch = app->height;
	const int bw = app->brush_w, bh = app->brush_h;
	const int ox = app->offset_x, oy = app->offset_y;

	// clamp x0/y0 to pixel or snap boundary
	const int snap_x = app->snap_to_grid ? HorizontalUnit(app) : (app->wide_pixels ? 2 : 1);
	const int snap_y = app->snap_to_grid ? VerticalUnit(app) : 1;

	// mouse canvas pixel * scale
	const int mx = app->mouse_x * step;
	const int my = app->mouse_y * step;

	// window offset for drawing - this should match the draw area
	const int x0 = SnapToUnit(mx, step * snap_x) + ox;
	const int y0 = SnapToUnit(my, step * snap_y) + oy;

	// draw space
	const int win_left = min(DrawArea.x, width);
	const int win_right = min(DrawArea.x + DrawArea.w, width);
	const int win_top = min(DrawArea.y, height);
	const int win_bottom = min(DrawArea.y + DrawArea.h, height);

	// window space
	const int draw_left = Clamp(Clamp(x0 - ox, 0, cw * step) + ox, win_left, win_right);
	const int draw_right = Clamp(Clamp(x0 + bw * step - ox, 0, cw * step) + ox, win_left, win_right);
	const int draw_top = Clamp(Clamp(y0 - oy, 0, ch * step) + oy, win_top, win_bottom);
	const int draw_bottom = Clamp(Clamp(y0 + bh * step - oy, 0, ch * step) + oy, win_top, win_bottom);

	// draw the brush
	if (draw_left < draw_right && draw_top < draw_bottom) {
		app->brush_prev_area = ToArea(draw_left, draw_top, draw_right - draw_left, draw_bottom - draw_top);

		const uint8_t* brush = app->brush;
		int32_t brush_s = (int32_t)(((int64_t)(draw_top - y0) << 16) / step);

		uint8_t skip_color = app->paste_mode == PASTE_MODE_SKIP_BG_COLOR ? app->background_color : 0xff;
		const int32_t brush_x_start = (int32_t)(((int64_t)(draw_left - x0) << 16) / step);
		for (int y = draw_top; y < draw_bottom; ++y) {
			UIPixel* t = pixels + y * width;
			int32_t brush_x = brush_x_start;
			if (brush_s >= 0) {
				const uint8_t* brush_row = brush + (brush_s >> 16) * app->brush_w;
				for (int x = draw_left; x < draw_right; ++x) {
					if (brush_x >= 0) {
						uint8_t c = brush_row[brush_x >> 16];
						if (c != 0xff && c != skip_color) {
							t[x] = ToUIPixel(c);
						}
					}
					brush_x += inc;
				}
			}
			brush_s += inc;
		}
	}
}

static void DrawGridInBitmap(Sleber* app, int step, int height, int width, UIPixel* pixels, int left_t, int right_t, Area DrawArea) {
	const int ox = app->offset_x, oy = app->offset_y, h = app->height, w = app->width;
	const int xs = app->wide_pixels ? 2 : 1;
	const int vu = VerticalUnit(app);
	const int hu = HorizontalUnit(app);

	(void)left_t; (void)right_t;
	(void)height; (void)xs;

	// cy = (ty-oy) / step
	// ty = cy * step + oy

	const UIPixel col_fe = ToUIPixel(0xfe), col_ff = ToUIPixel(0xff);

	// horizontal grid lines
	const int first_col = SnapToUnitCeil(max((DrawArea.x - ox + step - 1) / step, 0), xs);
	const int last_col = SnapToUnit(min((DrawArea.x + DrawArea.w - ox) / step, w), xs);
	const int top_y = max(oy, DrawArea.y);
	const int bottom_y = Clamp(oy + h * step, top_y, DrawArea.y + DrawArea.h);
	for(int col = first_col; col < last_col; col += xs) {
		UIPixel* col_pixels = pixels + top_y * width + (col * step + ox);
		UIPixel color = (col % hu) ? col_ff : col_fe;
		for (int y = top_y; y < bottom_y; ++y) {
			*col_pixels = color;
			col_pixels += width;
		}
	}

	// vertical grid lines
	const int first_row = max((DrawArea.y - oy + step - 1) / step, 0);
	const int last_row = min((DrawArea.y + DrawArea.h - oy) / step, h);
	const int left_x = max(ox, DrawArea.x);
	const int right_x = Clamp(ox + w * step, left_x, DrawArea.x + DrawArea.w);
	const int draw_width = right_x - left_x;

	for(int row = first_row; row < last_row; row++) {
		UIPixel* row_pixels = pixels + (row * step + oy) * width + left_x;
		UIPixel color = (row % vu) ? col_ff : col_fe;
		for (int x = 0; x < draw_width; ++x) {
			*row_pixels++ = color;
		}
	}
}

// Note: max width and height is 0xffff, which is within the range of int, even when multiplied by 16 (MaxZoomValue() = 16 << 3 - 1). So we can use int for all calculations here.
// width / height refer to the destination bitmap, DrawArea is the selection to draw
static void DrawBitmap(Sleber* app, UIPixel* pixels, int width, int height, Area DrawArea) {
	LockMutex(&app->canvas_mutex);
	const uint8_t* p =
		app->draw_pixel_prev ? app->pixels_prev : (
		app->draw_pixel_info ? app->pixels_info : app->pixels);

	// Note: Max step value is 16 (MaxZoomValue() = 16 << 3 - 1), so the maximum value of left_s and top_s is 0xffff, which is within the range of int.
	const int step = PixelSize(app);
	const int inc = (1 << 16) / step;

	// if draw area is empty, draw the whole bitmap
	int target_x = 0;
	int target_y = 0;
	int trgend_x = width;
	int trgend_y = height;

	if (DrawArea.w > 0 && DrawArea.h > 0) {
		target_x = Clamp(DrawArea.x, 0, width);
		target_y = Clamp(DrawArea.y, 0, height);
		trgend_x = Clamp(DrawArea.x + DrawArea.w, 0, width);
		trgend_y = Clamp(DrawArea.y + DrawArea.h, 0, height);
	}
	int trgwid = trgend_x - target_x;

	// t = da * step + offset
	// da = (t - offset) / step

	// if drawing canvas pixels then this is the source top left
	int source_x = Clamp(((target_x - app->offset_x) << 16) / step, 0, app->width << 16);
	int source_y = Clamp(((target_y - app->offset_y) << 16) / step, 0, app->height << 16);

	// At this point the draw area fractional part should probably be derived from the target area

	// draw border areas?
	const UIPixel border_color = ToUIPixel(app->border_color);
	if(app->offset_y > target_y) {
		for(int y = target_y, n = MIN(app->offset_y, trgend_y); y < n; ++y) {
			UIPixel* row = pixels + y * width + target_x;
			for(int x = 0; x < trgwid; ++x) {
				*row++ = border_color;
			}
		}
	}
	if ((app->height * step + app->offset_y) < trgend_y) {
		for(int y = (app->height * step + app->offset_y), n = trgend_y; y < n; ++y) {
			UIPixel* row = pixels + y * width + target_x;
			for(int x = 0; x < trgwid; ++x) {
				*row++ = border_color;
			}
		}
	}
	int vert_border_lines = app->height * step;
	if(app->offset_x > target_x) {
		const int xw = MIN(app->offset_x, trgend_x) - target_x;
		for(int y = MAX(app->offset_y, target_y), e = MIN(trgend_y, app->offset_y + vert_border_lines); y < e; ++y) {
			UIPixel* row = pixels + y * width + target_x;
			for(int x = 0; x < xw; ++x) {
				*row++ = border_color;
			}
		}
	}
	if ((app->width * step + app->offset_x) < trgend_x) {
		int xs = MAX(app->width * step + app->offset_x, target_x);
		if (xs < trgend_x ) {
			int xw = trgend_x - xs;
			for (int y = MAX(app->offset_y, target_y), e = MIN(trgend_y, app->offset_y + vert_border_lines); y < e; ++y) {
				UIPixel* row = pixels + y * width + xs;
				for (int x = 0; x < xw; ++x) {
					*row++ = border_color;
				}
			}
		}
	}

	// draw body part
	const int x0 = Clamp(app->offset_x, target_x, trgend_x);
	const int y0 = Clamp(app->offset_y, target_y, trgend_y);
	const int x1 = Clamp(app->offset_x + app->width * step, target_x, trgend_x);
	const int y1 = Clamp(app->offset_y + app->height * step, target_y, trgend_y);
	const int xm = app->width << 16, ym = app->height << 16;
	int src_y = source_y;
	for(int y = y0; y < y1; ++y) {
		if (src_y >= ym) { break; }
		const uint8_t* src = p + (src_y >> 16) * app->width;
		UIPixel* row = pixels + y * width;
		int src_x = source_x;
		for(int x = x0; x < x1; ++x) {
			if (src_x >= xm) { break; }
			row[x] = ToUIPixel(src[src_x >> 16] & 0xf);
			src_x += inc;
		}
		src_y += inc;
	}
	if (app->brush && (app->paste_mode == PASTE_MODE_OVERWRITE || app->paste_mode == PASTE_MODE_SKIP_BG_COLOR)) {
		DrawBrushInBitmap(app, step, width, height, pixels, inc, DrawArea);
	}

	if(step >= 6) {
		DrawGridInBitmap(app, step, height, width, pixels, target_x, target_y, DrawArea);
	}

	UnlockMutex(&app->canvas_mutex);
}

static void DrawColorPicker(Sleber* app, UIPixel* pixels, int width, int height) {
	if (width <= 0 || height <= 0) { return; }
	int cell_hgt = height / 8;
	int cell_wid = width / 2;
	UIPixel *pixel_row = pixels;
	for (int row = 0; row < 8; ++row) {
		for (int y = 0; y < cell_hgt; ++y) {
			UIPixel* pixel_x = pixel_row;
			for (int col = 0; col < 2; ++col) {
				uint8_t c = (uint8_t)(row + (col << 3));
				for (int x = 0; x < cell_wid; ++x) {
					*pixel_x++ = ToUIPixel(c);
				}
			}
			pixel_row += width;
		}
	}
	if (app->color_picker_hover_color< COLOR_COUNT) {
		UIPixel* pixel_fg = pixels +
			(app->color_picker_hover_color & 7) * cell_hgt * width +
			(app->color_picker_hover_color >> 3) * cell_wid;
		UIPixel* pixel_fl = pixel_fg + (cell_wid-1) * width;
		uint8_t fc = IsBright(app->color_picker_hover_color) ? 0 : 1;
		for(int y = 0; y < cell_hgt; ++y) {
			pixel_fg[y] = ToUIPixel(fc);
			pixel_fl[y] = ToUIPixel(fc);
			pixel_fg[y * width] = ToUIPixel(fc);
			pixel_fg[y * width+cell_wid-1] = ToUIPixel(fc);
		}
	}
	if (app->color_picker_current_color < COLOR_COUNT) {
		UIPixel* pixel_hv = pixels +
			(app->color_picker_current_color & 7) * cell_hgt * width +
			(app->color_picker_current_color >> 3) * cell_wid +
			(cell_hgt / 2 - 1) * width + (cell_wid / 2 - 1);
		uint8_t hc = IsBright(app->color_picker_current_color) ? 0 : 1;
		pixel_hv[0] = ToUIPixel(hc);
		pixel_hv[1] = ToUIPixel(hc);
		pixel_hv[width] = ToUIPixel(hc);
		pixel_hv[width + 1] = ToUIPixel(hc);
	}
}

uint8_t GetColorPickerTargetColor(Sleber* app, int x, int y) {
	y -= ( PALETTE_SQUARE + PALETTE_GAP ) * 8;

	if (y < PALETTE_HOVER_MARGIN || y >= ( PALETTE_SHARED_COLOR_HEIGHT - PALETTE_HOVER_MARGIN ) ) {
		return COLOR_TYPE_NONE;
	}
	if (x < PALETTE_HOVER_MARGIN || x >= ( PALETTE_SQUARE * 2 + PALETTE_GAP * 1 - PALETTE_HOVER_MARGIN)) {
		return COLOR_TYPE_NONE;
	}

	bool mc = (app->screen_mode == SCREEN_MODE_CHAR_MULTICOLOR) || (app->screen_mode == SCREEN_MODE_BITMAP_MULTICOLOR) || (app->screen_mode == SCREEN_MODE_SPRITE_MULTICOLOR);
	if (app->screen_mode == SCREEN_MODE_CHAR_MIXED) {
		mc = true;
	}

	// Multicolor modes
	if (mc) {
		return y < (PALETTE_SHARED_COLOR_HEIGHT / 2) ?
			( (2*x) < PALETTE_WIDTH ? COLOR_TYPE_BACKGROUND : COLOR_TYPE_FOREGROUND ) :
			( (2*x) < PALETTE_WIDTH ? COLOR_TYPE_MULTICOLOR : COLOR_TYPE_MULTICOLOR1 );
	}

	// Special ECBM mode
	if (app->screen_mode == SCREEN_MODE_ECBM) {
		return (y >= (PALETTE_HOVER_MARGIN + PALETTE_SHARED_ECBM_ROW * 2 ) ? COLOR_TYPE_FOREGROUND : 
		(y < (PALETTE_HOVER_MARGIN + PALETTE_SHARED_ECBM_ROW) ?
		( (2*x) < PALETTE_WIDTH ? COLOR_TYPE_BACKGROUND : COLOR_TYPE_BACKGROUND1 ) :
		( (2*x) < PALETTE_WIDTH ? COLOR_TYPE_BACKGROUND2 : COLOR_TYPE_BACKGROUND3 )));
	}

	// Hires modes
	if (x < (PALETTE_HOVER_HIRES_WIDTH + PALETTE_HOVER_MARGIN) && y < (PALETTE_HOVER_HIRES_WIDTH + PALETTE_HOVER_MARGIN)) {
		return COLOR_TYPE_FOREGROUND;
	} else if (x > ((PALETTE_SQUARE * 2 + PALETTE_GAP * 1) - (PALETTE_HOVER_HIRES_WIDTH + PALETTE_HOVER_MARGIN)) &&
		y > ((PALETTE_SHARED_COLOR_HEIGHT) - (PALETTE_HOVER_HIRES_WIDTH + PALETTE_HOVER_MARGIN))) {
		return COLOR_TYPE_BACKGROUND;
	}

	return COLOR_TYPE_BORDER;
}

static const char* GetColorName(uint8_t color) {
	switch(color & 0xf) {
		case 0: return "Black";
		case 1: return "White";
		case 2: return "Red";
		case 3: return "Cyan";
		case 4: return "Purple";
		case 5: return "Green";
		case 6: return "Blue";
		case 7: return "Yellow";
		case 8: return "Orange";
		case 9: return "Brown";
		case 10: return "Light Red";
		case 11: return "Dark Grey";
		case 12: return "Grey";
		case 13: return "Light Green";
		case 14: return "Light Blue";
		case 15: return "Light Grey";
		default:
			return "Unknown Color";
	}
}

uint8_t GetCurrentTargetColor(Sleber *app, uint8_t target) {
	switch(target) {
		case COLOR_TYPE_BACKGROUND: return app->shared_colors[0];
		case COLOR_TYPE_BACKGROUND1: return app->shared_colors[1];
		case COLOR_TYPE_BACKGROUND2: return app->shared_colors[2];
		case COLOR_TYPE_BACKGROUND3: return app->shared_colors[3];
		case COLOR_TYPE_FOREGROUND:
			if (app->screen_mode == SCREEN_MODE_UNRESTRICTED) {
				return app->pixels[app->hover_y * app->width + app->hover_x];
			}
			if (app->screen_mode == SCREEN_MODE_WIDE_UNRESTRICTED) {
				return app->pixels[app->hover_y * app->width + (app->hover_x & ~1)];
			}
			return app->foreground_color;
		case COLOR_TYPE_MULTICOLOR: return app->shared_colors[1];
		case COLOR_TYPE_MULTICOLOR1: return app->shared_colors[2];
		default:
			return 0xff;
	}
}

void SetColorPickerStatus(Sleber *app, uint8_t target, uint8_t currColor) {
	const char* targetName = "None";
	switch(target) {
		case COLOR_TYPE_BACKGROUND: targetName = "Background"; break;
		case COLOR_TYPE_BACKGROUND1: targetName = "Background 2"; break;
		case COLOR_TYPE_BACKGROUND2: targetName = "Background 3"; break;
		case COLOR_TYPE_BACKGROUND3: targetName = "Background 4"; break;
		case COLOR_TYPE_FOREGROUND: targetName = "Foreground"; break;
		case COLOR_TYPE_MULTICOLOR: targetName = "Multicolor 1"; break;
		case COLOR_TYPE_MULTICOLOR1: targetName = "Multicolor 2"; break;
	}

	char message[256];
	snprintf(message, sizeof(message), "Select %s Color, current: %s",
		targetName, GetColorName(currColor));

	UpdateStatus(app, message);
}



static ThreadType g_redraw_thread = 0;
static ConditionVariable g_redraw_condition;
static bool g_redraw_shutdown = false;
static bool g_redraw_thread_started = false;
static bool g_redraw_sync_initialized = false;

static BitmapType g_redraw_bitmap = {0,0,0};
static BitmapType g_redraw_bitmap_palette = {0,0,0};
static BitmapType g_redraw_bitmap_color_picker = {0,0,0};

static BitmapType g_redraw_bitmap_work = {0,0,0};
static BitmapType g_redraw_bitmap_palette_work = {0,0,0};
static BitmapType g_redraw_bitmap_color_picker_work = {0,0,0};

int BitmapValid(BitmapType bitmap) {
#ifdef _WIN32
	return bitmap.bitmap != NULL;
#else
	return bitmap.pPixels != NULL && bitmap.width > 0 && bitmap.height > 0;
#endif
}

BitmapType GetDrawBitmap() {
	BitmapType bitmap = g_redraw_bitmap;
	return bitmap;
}

BitmapType GetPaletteBitmap() {
	BitmapType bitmap = g_redraw_bitmap_palette;
	return bitmap;
}

BitmapType GetColorPickerBitmap() {
	BitmapType bitmap = g_redraw_bitmap_color_picker;
	return bitmap;
}

static void DestroyBitmapType(Sleber *app, BitmapType *bitmap) {
	LockMutex(&app->redraw_mutex);
#ifdef _WIN32
	DeleteObject(bitmap->bitmap);
#else
	free(bitmap->pPixels);
#endif
	*bitmap = (BitmapType){0, 0, 0};
	UnlockMutex(&app->redraw_mutex);
}

static struct Int2 WindowSizeDraw(Sleber* app) {
#ifdef _WIN32
	return (struct Int2) { app->draw_window_width & ~3UL, app->draw_window_height };
#else
	return (struct Int2) { app->draw_window_width, app->draw_window_height };
#endif
}

static void* CheckBitmapForRedraw(Sleber* app, BitmapType* workBitmap, BitmapType *result, WindowContext window_dc, int width, int height) {
	void* pixels = NULL;
	BitmapType bitmap = *workBitmap;

#ifdef _WIN32
	width = (width + 3) & ~3UL; // ensure width is a multiple of 4 for DIBs
#endif

	if (!BitmapValid(bitmap) || bitmap.width != width || bitmap.height != height) {
		if (BitmapValid(bitmap)) { DestroyBitmapType(app, workBitmap); }
		bitmap = CreateUIBitmap(width, height, &pixels, window_dc);
	} else {
#ifdef _WIN32
		DIBSECTION dib;
		if (GetObject(bitmap.bitmap, sizeof(DIBSECTION), &dib) == sizeof(DIBSECTION)) {
			pixels = dib.dsBm.bmBits;
		}
#else
		pixels = bitmap.pPixels;
#endif
	}
	*result = bitmap;
	return pixels;
}

static void SwapBitmaps(Sleber *app, BitmapType* currBitmap, BitmapType* workBitmap, BitmapType newBitmap) {
	LockMutex(&app->redraw_mutex);
	BitmapType prev_bitmap = *currBitmap;
	*currBitmap = newBitmap;
	*workBitmap = prev_bitmap;
	UnlockMutex(&app->redraw_mutex);
}

static void SwapCanvasBitmaps(Sleber *app, BitmapType* currBitmap, BitmapType* workBitmap,
	BitmapType newBitmap, Area dirty_rect) {
	LockMutex(&app->redraw_mutex);
	BitmapType prev_bitmap = *currBitmap;
	*currBitmap = newBitmap;
	*workBitmap = prev_bitmap;
	app->canvas_bitmap_dirty_rect = dirty_rect;
	UnlockMutex(&app->redraw_mutex);
}

static ThreadReturn RedrawThreadFunc(ThreadArg arg) {
	Sleber* app = (Sleber*)arg;
	while (app) {
		bool redraw_color_picker;
		bool redraw_palette;
		bool redraw_canvas;

		LockMutex(&app->redraw_mutex);
		while (!g_redraw_shutdown && !app->color_picker_redraw && !app->palette_redraw && !app->canvas_redraw) {
			WaitConditionVariable(&g_redraw_condition, &app->redraw_mutex);
		}
		if (g_redraw_shutdown) {
			UnlockMutex(&app->redraw_mutex);
			break;
		}
		redraw_color_picker = app->color_picker_redraw;
		redraw_palette = app->palette_redraw;
		redraw_canvas = app->canvas_redraw;
		app->color_picker_redraw = 0;
		app->palette_redraw = 0;
		app->palette_dirty_colors = 0;
		app->palette_dirty_preview = false;
		app->canvas_redraw = 0;
		UnlockMutex(&app->redraw_mutex);

		// Color Picker
		if (redraw_color_picker) {
			WindowContext window_dc = GetColorPickerContext(app);
			BitmapType bitmap = g_redraw_bitmap_color_picker_work;
			void* pixels = CheckBitmapForRedraw(app, &g_redraw_bitmap_color_picker_work, &bitmap, window_dc, COLOR_PICKER_WIDTH, COLOR_PICKER_HEIGHT);
			if (pixels && BitmapValid(bitmap)) {
				DrawColorPicker(app, pixels, COLOR_PICKER_WIDTH, COLOR_PICKER_HEIGHT);
				SwapBitmaps(app, &g_redraw_bitmap_color_picker, &g_redraw_bitmap_color_picker_work, bitmap);
				InvalidateColorPicker(app);
			}
			else if (BitmapValid(bitmap)) {
				DebugLog("Destroying color picker bitmap with missing pixels?\n");
				DestroyBitmapType(app, &g_redraw_bitmap_color_picker_work);
			}
			ReleaseWindowContext(app, window_dc);
		}

		// Palette
		if (redraw_palette) {

			int vizWidth = app->palette_window_width;
			int vizHeight = app->palette_window_height;

			WindowContext window_dc = GetPaletteContext(app);
			BitmapType bitmap = g_redraw_bitmap_palette_work;
			void* pixels = CheckBitmapForRedraw(app, &g_redraw_bitmap_palette_work, &bitmap, window_dc, vizWidth, vizHeight);

			if (pixels && BitmapValid(bitmap)) {
				// just draw straight into pixels!

				uint16_t dirty_colors = UINT16_MAX;
				bool dirty_preview = true;
				LockMutex(&app->canvas_mutex);
				DrawPalette(app, pixels, vizWidth, vizHeight, dirty_colors, dirty_preview);
				UnlockMutex(&app->canvas_mutex);

				SwapBitmaps(app, &g_redraw_bitmap_palette, &g_redraw_bitmap_palette_work, bitmap);
				Area dirty_area = dirty_preview
					? ToArea(0, 0, vizWidth, vizHeight)
					: ToArea(0, 0, PALETTE_WIDTH, PALETTE_SQUARE * 8);
				InvalidatePalette(app, dirty_area);
			} else if (BitmapValid(bitmap)) {
				DebugLog("Destroying palette bitmap with missing pixels?\n");
				DestroyBitmapType(app, &g_redraw_bitmap_palette_work);
			}
			ReleaseWindowContext(app, window_dc);
		}

		// Main view
		if (redraw_canvas) {
			struct Int2 size = WindowSizeDraw(app);
			int vizWidth = size.x;
			int vizHeight = size.y;
			Area draw = app->canvas_redraw_area;

			WindowContext window_dc = GetWindowContext(app);
			BitmapType bitmap = g_redraw_bitmap_work;
			void* pixels = CheckBitmapForRedraw(app, &g_redraw_bitmap_work, &bitmap, window_dc, vizWidth, vizHeight);
			if (pixels && BitmapValid(bitmap)) {
				// just draw straight into pixels!
				DrawBitmap(app, pixels, vizWidth, vizHeight, draw.w && draw.h ? draw : ToArea(0,0,vizWidth,vizHeight) );
				SwapCanvasBitmaps(app, &g_redraw_bitmap, &g_redraw_bitmap_work, bitmap,
					draw.w && draw.h ? draw : ToArea(0, 0, vizWidth, vizHeight));
				if (app->pixels_changed) {
					app->pixels_changed = false;
					if (!app->recount_triggerd) {
						if (app->screen_mode == SCREEN_MODE_CHAR || app->screen_mode == SCREEN_MODE_CHAR_MIXED ||
							app->screen_mode == SCREEN_MODE_CHAR_MULTICOLOR || app->screen_mode == SCREEN_MODE_ECBM) {
							app->recount_triggerd = true;
							StartRecountCountdown(app, 1000);
						}
					}
				}
				InvalidateDraw(app, draw);
			} else if (BitmapValid(bitmap)) {
				DebugLog("Destroying bitmap with missing pixels?\n");
				DestroyBitmapType(app, &g_redraw_bitmap_work);
			}
			ReleaseWindowContext(app, window_dc);
		}
		if (app->request_recount) {
			app->request_recount = false;
			if (app->screen_mode == SCREEN_MODE_CHAR || app->screen_mode == SCREEN_MODE_CHAR_MIXED ||
				app->screen_mode == SCREEN_MODE_CHAR_MULTICOLOR || app->screen_mode == SCREEN_MODE_ECBM) {
				LockMutex(&app->canvas_mutex);
				app->char_count = CountC64Chars(app->pixels_info, app->width, app->height, app->width);
				UnlockMutex(&app->canvas_mutex);
			}
		}
	}


	return 0;
}

void RequestRedrawPalette(Sleber* app) {
	LockMutex(&app->redraw_mutex);
	app->palette_redraw = 1;
	app->palette_dirty_preview = true;
	AwakeConditionVariable(&g_redraw_condition);
	UnlockMutex(&app->redraw_mutex);
}

void RequestRedrawPaletteColors(Sleber* app, uint16_t colors) {
	LockMutex(&app->redraw_mutex);
	app->palette_redraw = 1;
	app->palette_dirty_colors |= colors;
	AwakeConditionVariable(&g_redraw_condition);
	UnlockMutex(&app->redraw_mutex);
}

void RequestRedrawColorPicker(Sleber* app) {
	LockMutex(&app->redraw_mutex);
	app->color_picker_redraw = 1;
	AwakeConditionVariable(&g_redraw_condition);
	UnlockMutex(&app->redraw_mutex);
}

void RequestRedraw(Sleber* app) {
	LockMutex(&app->redraw_mutex);
	app->canvas_redraw = 1;
	app->canvas_redraw_area = (Area){0, 0, 0, 0};
	AwakeConditionVariable(&g_redraw_condition);
	UnlockMutex(&app->redraw_mutex);
}

void RequestRecount(Sleber* app) {
	LockMutex(&app->redraw_mutex);
	app->request_recount = true;
	AwakeConditionVariable(&g_redraw_condition);
	UnlockMutex(&app->redraw_mutex);
}

void RequestRedrawArea(Sleber* app, const Area area) {
	LockMutex(&app->redraw_mutex);

	// convert area to window coordinates
	const int step = PixelSize(app);
	int target_x = Clamp((int)area.x * step + app->offset_x, 0, app->draw_window_width);
	int target_y = Clamp((int)area.y * step + app->offset_y, 0, app->draw_window_height);
	int trgend_x = Clamp((int)(area.x + area.w) * step + app->offset_x, 0, app->draw_window_width);
	int trgend_y = Clamp((int)(area.y + area.h) * step + app->offset_y, 0, app->draw_window_height);
	app->canvas_redraw_area = ToArea(target_x, target_y, trgend_x - target_x, trgend_y - target_y);

	app->canvas_redraw = 1;

	AwakeConditionVariable(&g_redraw_condition);
	UnlockMutex(&app->redraw_mutex);
}

void RequestRedrawCanvasWindow(Sleber* app, const Area area) {
	LockMutex(&app->redraw_mutex);

	// convert area to window coordinates
	app->canvas_redraw_area = area;
	app->canvas_redraw = 1;

	AwakeConditionVariable(&g_redraw_condition);
	UnlockMutex(&app->redraw_mutex);
}


void StartRedrawThread(Sleber* app) {
	InitMutex(&app->redraw_mutex);
	InitConditionVariable(&g_redraw_condition);
	g_redraw_shutdown = false;
	g_redraw_thread_started = StartThread(&g_redraw_thread, 0, RedrawThreadFunc, app, "redraw_thread") == 0;
	g_redraw_sync_initialized = true;
}

static void DestroyGuiData(Sleber* app) {
	// clean up back buffers etc.
	g_redraw_shutdown = true;

	DestroyBitmapType(app, &g_redraw_bitmap_work);
	DestroyBitmapType(app, &g_redraw_bitmap_palette_work);
	DestroyBitmapType(app, &g_redraw_bitmap_color_picker_work);
	DestroyBitmapType(app, &g_redraw_bitmap);
	DestroyBitmapType(app, &g_redraw_bitmap_palette);
	DestroyBitmapType(app, &g_redraw_bitmap_color_picker);
}

bool StopRedrawThread(Sleber* app) {
	if (!g_redraw_sync_initialized) {
		return true;
	}
	if (g_redraw_thread_started) {
		LockMutex(&app->redraw_mutex);
		g_redraw_shutdown = true;
		AwakeConditionVariable(&g_redraw_condition);
		UnlockMutex(&app->redraw_mutex);

		if (JoinThread(&g_redraw_thread) != 0) {
			DebugLog("Failed to join redraw thread\n");
			return false;
		}
		g_redraw_thread_started = false;
	}

	DestroyGuiData(app);

	DestroyConditionVariable(&g_redraw_condition);
	DestroyMutex(&app->redraw_mutex);
	g_redraw_sync_initialized = false;
	return true;
}

bool AllocatePixelBuffers(int w, int h, uint8_t** pixels, uint8_t** pixels_info, uint8_t** pixels_prev) {
	if (!pixels || !pixels_info || !pixels_prev || w <= 0 || h <= 0) {
		return false;
	}
	*pixels = NULL;
	*pixels_info = NULL;
	*pixels_prev = NULL;
	const size_t pixel_count = CanvasMemorySize(w, h);
	if (pixel_count == 0) return false;
	*pixels = (uint8_t*)calloc(pixel_count, 1);
	*pixels_info = (uint8_t*)calloc(pixel_count, 1);
	*pixels_prev = (uint8_t*)calloc(pixel_count, 1);
	if (!*pixels || !*pixels_info || !*pixels_prev) {
		free(*pixels);
		free(*pixels_info);
		free(*pixels_prev);
		*pixels = NULL;
		*pixels_info = NULL;
		*pixels_prev = NULL;
		return false;
	}
	return true;
}

bool ResizePixelBuffersLocked(Sleber* app, int w, int h) {
	if (!app) {
		return true;
	}
	if (w == app->width && h == app->height) {
		return true;
	}
	int pw = app->width;
	int ph = app->height;

	uint8_t* pixels;
	uint8_t* pixels_info;
	uint8_t* pixels_prev;
	if (!AllocatePixelBuffers(w, h, &pixels, &pixels_info, &pixels_prev)) {
		return false;
	}

	int cw = w < pw ? w : pw;
	int ch = h < ph ? h : ph;
	if (pixels && pixels_info && pixels_prev) {
		for (int y = 0; y < ch; ++y) {
			memcpy(pixels + y * w, app->pixels + y * pw, (size_t)cw);
			memcpy(pixels_info + y * w, app->pixels_info + y * pw, (size_t)cw);
			memcpy(pixels_prev + y * w, app->pixels_prev + y * pw, (size_t)cw);
		}

		free(app->pixels);
		free(app->pixels_info);
		free(app->pixels_prev);

		app->pixels = pixels;
		app->pixels_info = pixels_info;
		app->pixels_prev = pixels_prev;

		app->width = w;
		app->height = h;
		return true;
	}
	return false;
}

bool ResizePixelBuffers(Sleber* app, int w, int h) {
	if (!app) {
		return true;
	}
	LockMutex(&app->canvas_mutex);
	bool result = ResizePixelBuffersLocked(app, w, h);
	UnlockMutex(&app->canvas_mutex);
	return result;
}

static void CopySelectionToBrushLocked(Sleber* app, bool consume_selection) {
	if(app->selection_mode == SELECT_MODE_RECTANGLE && app->select_w && app->select_h) {
		int x = app->select_x;
		int y = app->select_y;
		int w = app->select_w;
		int h = app->select_h;
		if (w < 0) { x += w; w = -w; }
		if (h < 0) { y += h; h = -h; }
		if (x < 0) { w += x; x = 0; }
		if (y < 0) { h += y; y = 0; }
		if (x + w > app->width) { w = app->width - x; }
		if (y + h > app->height) { h = app->height - y; }
		if (w <= 0 || h <= 0 || (size_t)w > SIZE_MAX / (size_t)h) {
			return;
		}

		uint8_t* brush = (uint8_t*)calloc((size_t)w * (size_t)h, 1);
		if (!brush) {
			return;
		}
		uint8_t* src = app->pixels + y * app->width + x;
		uint8_t* dst = brush;
		for(int row = 0; row < h; ++row) {
			memcpy(dst, src, (size_t)w);
			src += app->width;
			dst += w;
		}
		free(app->brush);
		if (app->brush32) {
			free(app->brush32);
			app->brush32 = NULL;
		}
		app->brush = brush;
		app->brush_w = w;
		app->brush_h = h;
		if (consume_selection) {
			app->selection_mode = SELECT_MODE_NONE;
			app->select_w = 0;
			app->select_h = 0;
		}
	}
}

void CopySelectionToBrush(Sleber* app, bool consume_selection) {
	LockMutex(&app->canvas_mutex);
	CopySelectionToBrushLocked(app, consume_selection);
	UnlockMutex(&app->canvas_mutex);
}

void StartSelection(Sleber* app) {
	app->selection_mode = SELECT_MODE_RECTANGLE_DRAG;
	app->fill_mode = false;
	app->select_w = 0; app->select_h = 0;
}

static void SetPasteModeLocked(Sleber *app, uint8_t paste_mode) {
	if (app->brush) {
		app->paste_mode = paste_mode;
		app->brush_prev_area = (Area){ 0,0,0,0 };
		return;
	}
	app->paste_mode = PASTE_MODE_NONE;
}

void SetPasteMode(Sleber *app, uint8_t paste_mode) {
	LockMutex(&app->canvas_mutex);
	SetPasteModeLocked(app, paste_mode);
	UnlockMutex(&app->canvas_mutex);
}

void SelectAll(Sleber* app) {
	app->selection_mode = SELECT_MODE_RECTANGLE;
	app->select_x = 0;
	app->select_y = 0;
	app->select_w = app->width;
	app->select_h = app->height;
}

bool IsGpxSupportedMode(uint8_t mode) {
	switch (mode) {
		case SCREEN_MODE_CHAR:
		case SCREEN_MODE_CHAR_MULTICOLOR:
		case SCREEN_MODE_BITMAP:
		case SCREEN_MODE_BITMAP_MULTICOLOR:
		case SCREEN_MODE_SPRITE:
		case SCREEN_MODE_SPRITE_MULTICOLOR:
			return true;
		default:
			return false;
	}
}

static uint8_t Sleber2GPXMode[] = {
	GPX_MODE_CHAR,
	GPX_MODE_MULTICOLOR_CHAR,
	GPX_MODE_BITMAP,
	GPX_MODE_MULTICOLOR_BITMAP,
	GPX_MODE_SPRITE,
	GPX_MODE_MULTICOLOR_SPRITE,
	GPX_MODE_UNUSED1,
	GPX_MODE_UNUSED1,
	GPX_MODE_UNRESTRICTED,
	GPX_MODE_WIDE_UNRESTRICTED
};

static gpx_data* MakeGPXHeader(Sleber *app)
{
	const bool sprite = app->screen_mode == SCREEN_MODE_SPRITE || app->screen_mode == SCREEN_MODE_SPRITE_MULTICOLOR;
	const int unit_w = sprite ? 24 : 8;
	const int unit_h = sprite ? 21 : 8;
	const uint32_t width_chars = (uint32_t)app->width / unit_w;
	const uint32_t height_chars = (uint32_t)app->height / unit_h;

	const uint32_t size = sizeof(gpx_data) + 
		(width_chars * height_chars * (sprite ? 64 : 8)) +
		2 * (width_chars * height_chars);
	gpx_data* gpx = (gpx_data*)calloc(1, size);

	if (!gpx) {
		return NULL;
	}

	uint8_t* pChars = (uint8_t*)(gpx + 1);
	uint8_t* pColors = pChars + (width_chars * height_chars * (sprite ? 64 : 8));
	uint8_t* pScreen = pColors + (width_chars * height_chars);

	gpx->pChars = pChars;
	gpx->pColors = pColors;
	gpx->pScreen = pScreen;
	gpx->width = (uint16_t)app->width;
	gpx->height = (uint16_t)app->height;
	gpx->mode = Sleber2GPXMode[app->screen_mode];
	gpx->background_color = app->shared_colors[0];
	gpx->multicolor0 = app->shared_colors[1];
	gpx->multicolor1 = app->shared_colors[2];
	return gpx;
}

static gpx_data* MakeGPXFileChar(Sleber* app) {
	gpx_data* gpx = MakeGPXHeader(app);
	if (!gpx) {
		return NULL;
	}
	uint8_t** ppData = (uint8_t**)gpx;
	const uint8_t* types = app->pixels_info;
	const uint32_t stride = app->width;
	uint8_t* pChar = ppData[0];
	uint8_t* pColor = ppData[1];
	gpx->pScreen = NULL;
	const uint32_t width_chars = (uint32_t)app->width / 8;
	const uint32_t height_chars = (uint32_t)app->height / 8;
	for(uint32_t cy=0; cy<height_chars; ++cy) {
		for(uint32_t cx=0; cx<width_chars; ++cx) {
			const uint8_t* char_types = types + cx * 8;
			struct CellCols cols = GetCellColsAt(app, cx * 8, cy * 8);
			for(int i=0; i<8; ++i) {
				uint8_t bits = 0;
				for(int j=0; j<8; ++j) {
					bits <<= 1;
					if(char_types[j] == GPX_PIXEL_FG) {
						bits |= 1;
					}
				}
				*pChar++ = bits;
				char_types += stride;
			}
			*pColor++ = cols.fg;
		}
		types += stride * 8;
	}
	return gpx;
}

static gpx_data* MakeGPXFileCharMC(Sleber* app) {
	gpx_data* gpx = MakeGPXHeader(app);
	if (!gpx) {
		return NULL;
	}
	uint8_t** ppData = (uint8_t**)gpx;
	const uint8_t* types = app->pixels_info;
	const uint32_t stride = app->width;
	uint8_t* pChar = ppData[0];
	uint8_t* pColor = ppData[1];
	gpx->pScreen = NULL;
	const uint8_t rebit[4] = { 0, 3, 1, 2 };
	const uint32_t width_chars = (uint32_t)app->width / 8;
	const uint32_t height_chars = (uint32_t)app->height / 8;
	for (uint32_t cy = 0; cy < height_chars; ++cy) {
		for (uint32_t cx = 0; cx < width_chars; ++cx) {
			const uint8_t* char_types = types + cx * 8;
			struct CellColsMC cols = GetCellColsMCAt(app, cx * 8, cy * 8);
			for (int i = 0; i < 8; ++i) {
				uint8_t bits = 0;
				for (int j = 0; j < 8; j += 2) {
					bits <<= 2;
					uint8_t type = char_types[j];
					bits |= rebit[type & 3];
				}
				*pChar++ = bits;
				char_types += stride;
			}
			*pColor++ = cols.fg;
		}
		types += stride * 8;
	}
	return gpx;
}

static gpx_data* MakeGPXFileBitmap(Sleber *app) {
	gpx_data* gpx = MakeGPXHeader(app);
	if (!gpx) {
		return NULL;
	}

	uint8_t** ppData = (uint8_t**)gpx;
	const uint8_t* types = app->pixels_info;
	const uint32_t stride = app->width;
	uint8_t* pChar = ppData[0];
	uint8_t* pColor = ppData[1];
	uint8_t* pScr = ppData[2];
	const uint32_t width_chars = (uint32_t)app->width / 8;
	const uint32_t height_chars = (uint32_t)app->height / 8;
	for(uint32_t cy=0; cy<height_chars; ++cy) {
		for(uint32_t cx=0; cx<width_chars; ++cx) {
			const uint8_t* char_types = types + cx * 8;
			struct CellCols cols = GetCellColsAt(app, cx * 8, cy * 8);

			for(int i=0; i<8; ++i) {
				uint8_t bits = 0;
				for(int j=0; j<8; ++j) {
					bits <<= 1;
					if(char_types[j] == GPX_PIXEL_FG) {
						bits |= 1;
					}
				}
				*pChar++ = bits;
				char_types += stride;
			}
			*pColor++ = 0;
			*pScr++ = (cols.bg&0xf) | ((cols.fg&0xf)<<4);
		}
		types += stride * 8;
	}
	return gpx;
}

static gpx_data* MakeGPXFileBitmapMC(Sleber* app) {
	gpx_data* gpx = MakeGPXHeader(app);
	if (!gpx) {
		return NULL;
	}

	uint8_t** ppData = (uint8_t**)gpx;
	const uint8_t* types = app->pixels_info;
	const uint32_t stride = app->width;
	uint8_t* pChar = ppData[0];
	uint8_t* pColor = ppData[1];
	uint8_t* pScr = ppData[2];
	const uint32_t width_chars = (uint32_t)app->width / 8;
	const uint32_t height_chars = (uint32_t)app->height / 8;
	const uint8_t rebit[4] = { 0, 3, 1, 2 };
	for (uint32_t cy = 0; cy < height_chars; ++cy) {
		for (uint32_t cx = 0; cx < width_chars; ++cx) {
			const uint8_t* char_types = types + cx * 8;

			for (int i = 0; i < 8; ++i) {
				uint8_t bits = 0;
				for (int j = 0; j < 8; j+=2) {
					bits <<= 2;
					uint8_t type = char_types[j];
					bits |= rebit[type & 3];
				}
				*pChar++ = bits;
				char_types += stride;
			}
			struct CellColsMC cols = GetCellColsMCAt(app, cx * 8, cy * 8);
			if (cols.m0 == 0xff && cols.m1 != 0xff) { cols.m0 = cols.m1; }
			if (cols.m0 != 0xff && cols.m1 == 0xff) { cols.m1 = cols.m0; }
			*pColor++ = (cols.fg&0xf);
			*pScr++ = (cols.m1&0xf) | ((cols.m0&0xf) << 4);
		}
		types += stride * 8;
	}
	return gpx;
}

static gpx_data* MakeGPXFileSprite(Sleber* app) {
	gpx_data* gpx = MakeGPXHeader(app);
	if (!gpx) {
		return NULL;
	}

	uint8_t** ppData = (uint8_t**)gpx;
	const uint8_t* types = app->pixels_info;
	const uint32_t stride = app->width;
	uint8_t* pChar = ppData[0];
	uint8_t* pColor = ppData[1];
	gpx->pScreen = NULL;
	const uint32_t width_sprites = (uint32_t)app->width / 24;
	const uint32_t height_sprites = (uint32_t)app->height / 21;
	for (uint32_t cy = 0; cy < height_sprites; ++cy) {
		for (uint32_t cx = 0; cx < width_sprites; ++cx) {
			const uint8_t* sprite_types = types + cx * 24;
			struct CellCols cols = GetSpriteColsAt(app, cx * 24, cy * 21);

			for (int i = 0; i < 21; ++i) {
				uint32_t bits = 0;
				for (int j = 0; j < 24; ++j) {
					bits <<= 1;
					if (sprite_types[j] == GPX_PIXEL_FG) {
						bits |= 1;
					}
				}
				*pChar++ = (uint8_t)(bits >> 16);
				*pChar++ = (uint8_t)(bits >> 8);
				*pChar++ = (uint8_t)(bits);
				sprite_types += stride;
			}
			*pChar++ = 0;
			*pColor++ = cols.fg & 0xf;
		}
		types += stride * 21;
	}
	return gpx;
}

static gpx_data* MakeGPXFileSpriteMC(Sleber* app) {
	gpx_data* gpx = MakeGPXHeader(app);
	if (!gpx) {
		return NULL;
	}

	uint8_t** ppData = (uint8_t**)gpx;
	const uint8_t* types = app->pixels_info;
	const uint32_t stride = app->width;
	uint8_t* pChar = ppData[0];
	uint8_t* pColor = ppData[1];
	gpx->pScreen = NULL;
	const uint32_t width_sprites = (uint32_t)app->width / 24;
	const uint32_t height_sprites = (uint32_t)app->height / 21;
	const uint8_t rebit[4] = { 0, 3, 1, 2 };
	for(uint32_t cy=0; cy<height_sprites; ++cy) {
		for(uint32_t cx=0; cx<width_sprites; ++cx) {
			const uint8_t* sprite_types = types + cx * 24;
			struct CellColsMC cols = GetSpriteColsMCAt(app, cx * 24, cy * 21);
			for(int i=0; i<21; ++i) {
				uint32_t bits = 0;
				for(int j=0; j<24; j+=2) {
					bits <<= 2;
					uint8_t type = sprite_types[j];
					bits |= rebit[type & 3];
				}
				*pChar++ = (uint8_t)(bits >> 16);
				*pChar++ = (uint8_t)(bits >> 8);
				*pChar++ = (uint8_t)(bits);
				sprite_types += stride;
			}
			*pChar++ = 0;
			*pColor++ = cols.fg & 0xf;
		}
		types += stride * 21;
	}
	return gpx;
}

gpx_data* MakeGPXFile(Sleber *app)
{
	switch (app->screen_mode) {
		case SCREEN_MODE_CHAR:
			return MakeGPXFileChar(app);
		case SCREEN_MODE_CHAR_MULTICOLOR:
			return MakeGPXFileCharMC(app);
		case SCREEN_MODE_BITMAP:
			return MakeGPXFileBitmap(app);
		case SCREEN_MODE_BITMAP_MULTICOLOR:
			return MakeGPXFileBitmapMC(app);
		case SCREEN_MODE_SPRITE:
			return MakeGPXFileSprite(app);
		case SCREEN_MODE_SPRITE_MULTICOLOR:
			return MakeGPXFileSpriteMC(app);
		default:
			return NULL;
	}
}


/* ---- UI calls */
// set foreground or background from canvas pixel
void PickColorAt(Sleber* app, I32Point p, bool bg) {
	uint8_t c = app->border_color;
	if (p.x >= 0 && p.y >= 0 && p.x < app->width && p.y < app->height) {
		c = GetColorAt(app, p.x, p.y);
	}
	if (bg) { SetBackgroundColor(app, c); } else { SetForegroundColor(app, c); }
}

// assign a color to a pixel
void UpdateMouseDown(Sleber* app, I32Point pw, int right) {
	int step = PixelSize(app);
	I32Point p = SnapToGrid(app, (I32Point){(pw.x - app->offset_x) / step,(pw.y - app->offset_y) / step});

	if (app->selection_mode == SELECT_MODE_RECTANGLE_DRAG) {
		if (!app->dragging) {
			app->select_x = max(min(p.x,app->width), 0) & (app->wide_pixels ? ~1 : ~0);
			app->select_y = max(min(p.y,app->height), 0);
			app->select_w = 0;
			app->select_h = 0;
			app->dragging = right ? MOUSE_BUTTON_RIGHT : MOUSE_BUTTON_LEFT;
		} else {
			app->select_w = (max(min(p.x, app->width), 0) - app->select_x) & (app->wide_pixels ? ~1 : ~0);
			app->select_h = max(min(p.y, app->height), 0) - app->select_y;
		}
	} else if (app->modifier_keys & MODIFIER_KEY_SHIFT) {
		return;
	} else if (app->paste_mode == PASTE_MODE_OVERWRITE || app->paste_mode == PASTE_MODE_SKIP_BG_COLOR) {
		PasteBrush(app, p);
	} else if (app->paste_mode == PASTE_MODE_PASTED) {
	} else if(p.x < 0 || p.y < 0 || p.x >= app->width || p.y >= app->height) {
		app->drag_last_x = Clamp(p.x, 0, app->width - 1);
		app->drag_last_y = Clamp(p.y, 0, app->height - 1);
		return;
	} else if (app->fill_mode) {
		Fill(app, p.x, p.y, right ? app->background_color : app->foreground_color);
	} else if (app->dragging) {
		PaintLine(app, app->drag_last_x, app->drag_last_y, p.x, p.y, right);
	} else {
		PaintAt(app, p.x, p.y, right);
	}
#ifdef DEBUG
	if (app->dragging != (right ? MOUSE_BUTTON_RIGHT : MOUSE_BUTTON_LEFT)) {
		printf("Mouse button changed during drag: %d -> %d\n", app->dragging, right ? MOUSE_BUTTON_RIGHT : MOUSE_BUTTON_LEFT);
	}
#endif
	app->dragging = right ? MOUSE_BUTTON_RIGHT : MOUSE_BUTTON_LEFT;
	app->drag_last_x = p.x;
	app->drag_last_y = p.y;
	app->mouse_last_x = pw.x;
	app->mouse_last_y = pw.y;
}

void UpdateHover(Sleber* app, int px, int py) {
	const int step = PixelSize(app);
	const I32Point p = SnapToGrid(app, (I32Point) { (px-app->offset_x)/step, (py-app->offset_y)/step });
	app->mouse_x = p.x; app->mouse_y = p.y;
	if (p.x < 0 || p.y < 0 || p.x >= app->width || p.y >= app->height) {
		app->hover_background_color = 0xff;
		app->hover_foreground_color = 0xff;
		app->hover_multicolor0 = 0xff;
		app->hover_multicolor1 = 0xff;
		return;
	}

	uint8_t hover_bg = app->hover_background_color;
	uint8_t hover_fg = app->hover_foreground_color;
	uint8_t hover_m0 = app->hover_multicolor0;
	uint8_t hover_m1 = app->hover_multicolor1;

	switch (app->screen_mode) {
		case SCREEN_MODE_CHAR:
			HoverChar(app, p.x, p.y);
			break;
		case SCREEN_MODE_CHAR_MULTICOLOR:
			HoverCharMC(app, p.x, p.y);
			break;
		case SCREEN_MODE_CHAR_MIXED:
			HoverCharMC(app, p.x, p.y);
			break;
		case SCREEN_MODE_BITMAP:
			HoverBitmap(app, p.x, p.y);
			break;
		case SCREEN_MODE_BITMAP_MULTICOLOR:
			HoverBitmapMC(app, p.x, p.y);
			break;
		case SCREEN_MODE_SPRITE:
			HoverSprite(app, p.x, p.y);
			break;
		case SCREEN_MODE_SPRITE_MULTICOLOR:
			HoverSpriteMC(app, p.x, p.y);
			break;
		case SCREEN_MODE_ECBM:
			HoverECBM(app, p.x, p.y);
			break;
		case SCREEN_MODE_UNRESTRICTED:
			app->hover_foreground_color = app->pixels[p.y * app->width + p.x];
			break;
		case SCREEN_MODE_WIDE_UNRESTRICTED:
			app->hover_foreground_color = app->pixels[p.y * app->width + (p.x & ~1)];
			break;
		default:
			break;
	}

	if (p.x != app->hover_x || p.y != app->hover_y) {
		char message[256];
		if (app->screen_mode == SCREEN_MODE_CHAR || app->screen_mode == SCREEN_MODE_CHAR_MULTICOLOR ||
			app->screen_mode == SCREEN_MODE_CHAR_MIXED || app->screen_mode == SCREEN_MODE_ECBM) {
			snprintf(message, sizeof(message), "X: %d Y:%d XC: %d YC: %d Chars: %d Zoom: %d%%",
				p.x, p.y, p.x / 8, p.y / 8, app->char_count, PixelSize(app) * 100);
		} else {
			snprintf(message, sizeof(message), "X: %d Y:%d XC: %d YC: %d Zoom: %d%%",
				p.x, p.y, p.x / 8, p.y / 8, PixelSize(app) * 100);
		}
		UpdateStatus(app, message);
	}

	app->hover_x = p.x;
	app->hover_y = p.y;

	if (hover_bg != app->hover_background_color || hover_fg != app->hover_foreground_color || hover_m0 != app->hover_multicolor0 || hover_m1 != app->hover_multicolor1) {
		RequestRedrawPalette(app);
	}
}

void UIPaletteButtonDown(Sleber* app, I32Point p, I32Point ul, I32Point br, I32Point sp, bool left_button) {
	if (!app) { return; }

	int col = (p.x * 2) / (br.x - ul.x);
	int row = (p.y * 8) / (br.y - ul.y - PALETTE_SHARED_COLOR_HEIGHT);
	int index = col * 8 + row;
	uint8_t fg = app->foreground_color, bg = app->background_color;
	if (row < (COLOR_COUNT / 2)) {
		if (col >= 0 && col <= 1 && row >= 0 && row <= 7 && index >= 0) {
			if (left_button) {
				SetForegroundColor(app, index);
			} else {
				SetBackgroundColor(app, index);
			}
		}
		if (fg != app->foreground_color || bg != app->background_color) {
			RequestRedrawPalette(app);
		}
	} else {
		uint8_t target = GetColorPickerTargetColor(app, p.x, p.y);
		if (left_button) {
			if (target != COLOR_TYPE_NONE) {
				app->foreground_color = GetCurrentTargetColor(app, target);
				RequestRedrawPalette(app);
			}
		} else {
			if (target != COLOR_TYPE_NONE) {
				app->color_picker_current_color = GetCurrentTargetColor(app, target);
				app->color_picker_hover_color = 0;
				app->color_picker_target_color = target;
				SetColorPickerStatus(app, target, app->color_picker_current_color);
				RequestRedrawColorPicker(app);
				OpenColorPickerAt(app, sp);
			}
		}
	}
}

void RequestDrawBrush(Sleber* app, I32Point p) {
	if (!app) { return; }
	LockMutex(&app->canvas_mutex);
	if (!app->brush) {
		UnlockMutex(&app->canvas_mutex);
		return;
	}
	const int step = PixelSize(app);

	const int snap_y = app->snap_to_grid ?
		(app->screen_mode == SCREEN_MODE_SPRITE || app->screen_mode == SCREEN_MODE_SPRITE_MULTICOLOR ? 21 : 8) :
		1;
	const int snap_x = app->snap_to_grid ?
		(app->screen_mode == SCREEN_MODE_SPRITE || app->screen_mode == SCREEN_MODE_SPRITE_MULTICOLOR ? 24 : 8) :
		(app->wide_pixels ? 2 : 1);

	const int x0 = p.x - (p.x - app->offset_x) % (step * snap_x);
	const int y0 = p.y - (p.y - app->offset_y) % (step * snap_y);

	const Area bounds = ToArea(0, 0, app->draw_window_width, app->draw_window_height);
	const Area brush_area = ClampArea(ToArea(x0, y0, app->brush_w * step, app->brush_h * step), bounds);

	const Area combined_area = (app->brush_prev_area.w && app->brush_prev_area.h) ? UnionArea(brush_area, app->brush_prev_area) : brush_area;

	app->brush_prev_area = (Area){0, 0, 0, 0};
	UnlockMutex(&app->canvas_mutex);

	if (combined_area.w > 0 && combined_area.h > 0) {
		RequestRedrawCanvasWindow(app, combined_area);
	}
}

void UICanvasMouseMove(Sleber* app, I32Point p) {
	if (app) {
		if (app->paste_mode != PASTE_MODE_NONE) {
			RequestDrawBrush(app, p);
		}
		if (app->dragging == MOUSE_BUTTON_MIDDLE) {
			p.x -= app->drag_last_x;
			p.y -= app->drag_last_y;
			if (p.x || p.y) {
				app->drag_last_x += p.x;
				app->drag_last_y += p.y;
				app->offset_x += p.x;
				app->offset_y += p.y;
				RequestRedraw(app);
			}
		} else if (app->dragging) {//(wparam & (MK_LBUTTON | MK_LBUTTON)) != 0) {
			UpdateMouseDown(app, p, app->dragging == MOUSE_BUTTON_RIGHT);// (wparam & MK_RBUTTON) != 0);
			RequestRedraw(app);
		}
		UpdateHover(app, p.x, p.y);
	}
}

/* ---- App lifecycle ---- */

Sleber* AppNew(void) {
	Sleber* app = (Sleber*)calloc(1, sizeof(*app));
	if (!app) {
		return NULL;
	}
	InitMutex(&app->canvas_mutex);
	app->gui = CreateGuiData();
	if (!app->gui) {
		free(app);
		return NULL;
	}
	app->brush = NULL;
	app->width = CANVAS_WIDTH;
	app->height = CANVAS_HEIGHT;
	app->char_count = 0;
	app->zoom = MinZoomValue() * 2;
	int pixel_size = PixelSize(app);
	app->draw_window_width = app->width * pixel_size;
	app->draw_window_height = app->height * pixel_size;
	app->offset_x = STARTUP_HORIZONTAL_BORDER * pixel_size;
	app->offset_y = STARTUP_VERTICAL_BORDER * pixel_size;
	app->paste_mode = PASTE_MODE_NONE;
	app->modifier_keys = MODIFIER_KEY_NONE;
	app->foreground_color = 1;
	app->background_color = 0;
	app->border_color = 14;
	app->canvas_bitmap_dirty_rect = (Area){ 0, 0, 0, 0 };

	// checked after call so full cleanup will run if any of these fail
	app->pixels = (uint8_t*)calloc((size_t)(app->width * app->height), 1);
	app->pixels_info = (uint8_t*)calloc((size_t)(app->width * app->height), 1);
	app->pixels_prev = (uint8_t*)calloc((size_t)(app->width * app->height), 1);

	app->canvas_redraw = 0;
	app->palette_redraw = 0;

	app->shared_colors[0] = 0; // background
	app->shared_colors[1] = 8; // multicolor 1
	app->shared_colors[2] = 10; // multicolor 2
	app->shared_colors[3] = 4; // ecbm color 4

	app->color_picker_visible = false;
	app->draw_pixel_info = false;
	app->fill_mode = false;
	app->import_image_dither = IMPORT_IMAGE_DITHER_MEDIUM;
	app->screen_mode = SCREEN_MODE_BITMAP_MULTICOLOR;

	CalculatePaletteDistances();
	return app;
}

void AppFree(Sleber* app) {
	if (!app) {
		return;
	}
	ClearUndoHistory(app);
	if (!StopRedrawThread(app)) {
		return;
	}
	StopRecountCountdown(app);
	if (app->brush) {
		free(app->brush);
		app->brush = NULL;
	}
	if (app->brush32) {
		free(app->brush32);
		app->brush32 = NULL;
	}
	free(app->current_file_path);
	app->current_file_path = NULL;
	if (app->gui) {
		free(app->gui);
		app->gui = NULL;
	}
	ShutdownAppUndo(app);
	DestroyMutex(&app->canvas_mutex);
	free(app->pixels);
	free(app->pixels_info);
	free(app->pixels_prev);
	free(app);
}
