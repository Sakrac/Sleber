#ifndef _SLEBER_H_
#define _SLEBER_H_

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include "platform.h"
#include "sleber_limits.h"
#include "undo.h"

#ifndef __linux__
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#else
#define max(a,b) ((a) > (b) ? (a) : (b))
#define min(a,b) ((a) < (b) ? (a) : (b))
#endif

#define Clamp(x, mn, mx) ((x) < (mn) ? (mn) : ((x) > (mx) ? (mx) : (x)))
#define SnapToUnit(value, unit) ((value) - (value) % (unit))
#define SnapToUnitCeil(value, unit) (((value) + (unit) - 1) - ((value) + (unit) - 1) % (unit))

// Initial canvas size
#define CANVAS_WIDTH 320
#define CANVAS_HEIGHT 200

#define STATUS_HEIGHT 24
#define PALETTE_SQUARE 20
#define PALETTE_GAP 0
#define PALETTE_SHARED_COLOR_HEIGHT (PALETTE_SQUARE * 2)
#define PALETTE_WIDTH (PALETTE_SQUARE * 2 + PALETTE_GAP)
#define PALETTE_HEIGHT (PALETTE_SQUARE * 8 + PALETTE_GAP * 7 + PALETTE_SHARED_COLOR_HEIGHT)

#define STARTUP_VERTICAL_BORDER 24
#define STARTUP_HORIZONTAL_BORDER 32

#define COLOR_PICKER_SQUARE 10
#define COLOR_PICKER_WIDTH (COLOR_PICKER_SQUARE * 2)
#define COLOR_PICKER_HEIGHT (COLOR_PICKER_SQUARE * 8)

#define COLOR_COUNT 16
#define ECBM_BACKGROUND_COUNT 4
#define MAX_SCREEN_BACKGROUND_COUNT 4

enum {
    MOUSE_BUTTON_NONE = 0,
    MOUSE_BUTTON_LEFT,
    MOUSE_BUTTON_RIGHT,
	MOUSE_BUTTON_MIDDLE
};

enum {
	SCREEN_MODE_CHAR,
	SCREEN_MODE_CHAR_MULTICOLOR,
    SCREEN_MODE_BITMAP,
    SCREEN_MODE_BITMAP_MULTICOLOR,
    SCREEN_MODE_SPRITE,
    SCREEN_MODE_SPRITE_MULTICOLOR,
    SCREEN_MODE_ECBM,
    SCREEN_MODE_CHAR_MIXED,
    SCREEN_MODE_UNRESTRICTED,
	SCREEN_MODE_WIDE_UNRESTRICTED
};

enum {
	SELECT_MODE_NONE, // not a selection
	SELECT_MODE_RECTANGLE_DRAG, // dragging a rectangle selection
	SELECT_MODE_RECTANGLE, // selected a rectangle
	SELECT_MODE_FREEHAND, // dragging a freehand selection
	SELECT_MODE_COLOR_RANGE, // picking a color for selection
	SELECT_MODE_NON_RECTANGLE, // non-rectangle selection (freehand or color range)
};

enum {
	PASTE_MODE_NONE,
	PASTE_MODE_OVERWRITE,
	PASTE_MODE_SKIP_BG_COLOR,

	PASTE_MODE_PASTED,
};

typedef enum {
	SLEBER_FILE_NONE,
	SLEBER_FILE_GPX,
	SLEBER_FILE_SBX
} SleberFileType;

// color picker target color
enum {
	COLOR_TYPE_NONE = 0,
	COLOR_TYPE_BORDER,
	COLOR_TYPE_BACKGROUND, // ecbm has 4 background colors
	COLOR_TYPE_BACKGROUND1,
	COLOR_TYPE_BACKGROUND2,
	COLOR_TYPE_BACKGROUND3,
	COLOR_TYPE_FOREGROUND,
	COLOR_TYPE_MULTICOLOR, // two multicolors
	COLOR_TYPE_MULTICOLOR1,
};

enum RedrawSurface {
	REDRAW_SURFACE_NONE = 0,
	REDRAW_SURFACE_CANVAS = 1,
	REDRAW_SURFACE_PALETTE = 2,
	REDRAW_SURFACE_COLOR_PICKER = 4
};

enum ModifierKeys {
	MODIFIER_KEY_NONE = 0,
	MODIFIER_KEY_LEFT_SHIFT = 1,
	MODIFIER_KEY_RIGHT_SHIFT = 2,
	MODIFIER_KEY_SHIFT = 3, // combination of left and right shift
	MODIFIER_KEY_LEFT_CTRL = 4,
	MODIFIER_KEY_RIGHT_CTRL = 8,
	MODIFIER_KEY_CTRL = 12, // combination of left and right ctrl
	MODIFIER_KEY_LEFT_ALT = 16,
	MODIFIER_KEY_RIGHT_ALT = 32,
	MODIFIER_KEY_ALT = 48, // combination of left and right alt
};

typedef struct Sleber {
	MutexType canvas_mutex;
	MutexType redraw_mutex;

    PlatformGui *gui;
    uint8_t *pixels;	// palette indices
	uint8_t *pixels_info; // pixel type
	uint8_t *pixels_prev;

	uint8_t *brush;
	uint32_t *brush32; // to handle pasted bitmaps from other programs

	UndoCommandBuffer* undo_command_buffer;
	UndoCommandBuffer* redo_command_buffer;

	// Canvas redraw area
	Area canvas_redraw_area;
	Area canvas_bitmap_dirty_rect;

	// Brush previous area
	Area brush_prev_area;

	// selection area
	int select_x, select_y, select_w, select_h;

	// brush area
	int brush_w, brush_h;
	int paste_x, paste_y;

    int zoom;
    int offset_x;
    int offset_y;
	int width;
	int height;
	int char_count;

	int draw_window_width;
	int draw_window_height;

	int palette_window_width;
	int palette_window_height;
	int palette_window_width_outer;
	int palette_window_height_outer;
	int color_picker_window_width;
	int color_picker_window_height;

	int mouse_x, mouse_y;
	// cursor hover colors
	int hover_x, hover_y;
	uint8_t hover_background_color;
	uint8_t hover_foreground_color;
	uint8_t hover_multicolor0;
	uint8_t hover_multicolor1;

	uint8_t screen_mode;
	char *current_file_path;
	SleberFileType current_file_type;

	// user painting colors
    uint8_t foreground_color;
    uint8_t background_color;

	// color picker info
	uint8_t color_picker_current_color;
	uint8_t color_picker_hover_color;
	uint8_t color_picker_target_color;

	uint8_t border_color;
    uint8_t wide_pixels;

	uint8_t shared_colors[MAX_SCREEN_BACKGROUND_COUNT];   // $d021, mc0, mc1 or $d021-$d025 depending on screen mode

    uint8_t palette_redraw;
	uint16_t palette_dirty_colors;
	bool palette_dirty_preview;
	uint8_t canvas_redraw;
	uint8_t color_picker_redraw;
	bool pixels_changed; // trigger recount etc.
	bool recount_triggerd;
	bool request_recount;

    uint8_t palette_docked;

	uint8_t selection_mode;

	uint8_t paste_mode;
	uint8_t modifier_keys; // combination of ModifierKeys enum values

	bool color_picker_visible;
	bool draw_pixel_info;
	bool draw_pixel_prev;
	bool fill_mode;
	bool snap_to_grid;
	bool keep_import_background_colors;
	uint8_t import_image_dither;

	uint8_t dragging; // none, left, right
	bool mouse_drawn; // true when dragging & drawing, false when mouse released
	int drag_last_x;
	int drag_last_y;
	int mouse_last_x;
	int mouse_last_y;
} Sleber;

#ifdef _WIN32
typedef uint8_t UIPixel;
#else
typedef uint32_t UIPixel;
#endif

int ScreenModeHorizontalUnit(uint8_t screen_mode);
int ScreenModeVerticalUnit(uint8_t screen_mode);

static inline uint32_t HorizontalUnit(Sleber* app) {
	return ScreenModeHorizontalUnit(app->screen_mode);
}

static inline uint32_t VerticalUnit(Sleber* app) {
	return ScreenModeVerticalUnit(app->screen_mode);
}

static inline size_t CanvasMemorySize(int w, int h) {
	if (w <= 0 || h <= 0 || w > MAX_CANVAS_WIDTH || h > MAX_CANVAS_HEIGHT) {
		return 0;
	}
	return (size_t)w * (size_t)h * sizeof(uint8_t);
}

void UpdateMouseDown(Sleber* app, I32Point p, int right);
void UIPaletteButtonDown(Sleber* app, I32Point p, I32Point ul, I32Point br, I32Point sp, bool left_button);
void RequestDrawBrush(Sleber* app, I32Point p);
void UICanvasMouseMove(Sleber* app, I32Point p);
I32Point SnapToGrid(Sleber* app, I32Point point);
Area SnapAreaToUnit(uint8_t screen_mode, const Area area);
int PixelSize(Sleber* app);
Area ClampArea(const Area area, const Area bounds);
int MaxZoomValue(void);
int MinZoomValue(void);
void SetForegroundColor(Sleber* app, int index);
void SetBackgroundColor(Sleber* app, int index);

void RequestRedrawPalette(Sleber* app);
void RequestRedrawPaletteColors(Sleber* app, uint16_t colors);
void RequestRedrawColorPicker(Sleber* app);
void RequestRedraw(Sleber* app);
void RequestRedrawArea(Sleber* app, const Area area);
void RequestRedrawCanvasWindow(Sleber* app, const Area area);
void StartRedrawThread(Sleber* app);
bool StopRedrawThread(Sleber* app);
bool ResizePixelBuffers(Sleber* app, int w, int h);
bool ResizePixelBuffersLocked(Sleber* app, int w, int h);
bool AllocatePixelBuffers(int w, int h, uint8_t** pixels, uint8_t** pixels_info, uint8_t** pixels_prev);
void CopySelectionToBrush(Sleber* app, bool consume_selection);
void StartSelection(Sleber* app);
void SetPasteMode(Sleber* app, uint8_t paste_mode);
void SelectAll(Sleber* app);
void AppFree(Sleber* app);
static inline void RefreshPaletteUI(Sleber* app) { RequestRedrawPalette(app); }

uint8_t GetCurrentTargetColor(Sleber* app, uint8_t target);
void SetColorPickerStatus(Sleber* app, uint8_t target, uint8_t currColor);
uint8_t GetColorPickerTargetColor(Sleber * app, int x, int y);
void InitBitmapPalette(void);
int BitmapValid(BitmapType bitmap);
BitmapType GetDrawBitmap();
BitmapType GetPaletteBitmap();
BitmapType GetColorPickerBitmap();

// GPX conversion
struct gpx_data;
typedef struct gpx_data gpx_data;
bool IsGpxSupportedMode(uint8_t mode);
gpx_data* MakeGPXFile(Sleber* app);
void PickColorAt(Sleber* app, I32Point p, bool bg);
#endif