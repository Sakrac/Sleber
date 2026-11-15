#include "platform.h"
#include "sleber.h"
#include "Edit.h"
#include "gpx_load.h"
#include "palette.h"
#include <assert.h>
#include "convert.h"

#define MAX_CHANGE_ACTIONS 256

typedef struct { uint32_t size; uint8_t type; uint8_t __pad[3]; } UndoSizeType;

typedef union {
	uint8_t row[8];
	uint64_t chr;
} CharMask;

typedef union {
	uint8_t bts[63];
	uint64_t spr[8];
} SpriteMask;

// each change action builds up for one edit action and then it can be chained
static int sNumChangeActions = 0;
static I16Vec2 sChangeActionPos[MAX_CHANGE_ACTIONS];
static I16Vec2 sChangeActionSize[MAX_CHANGE_ACTIONS];
static I16Vec2 sChangeMin = { 0xffff, 0xffff }, sChangeMax = { 0, 0 };

static UndoSizeType CalculateSizeType(Area area) {
	uint8_t type = UNDO_NONE;
	uint16_t x = area.x, y = area.y, w = area.w, h = area.h;
	uint32_t size = 1 + 2; // type + x.b, y.b
	if (x >= 256 || y >= 256) {
		type |= UNDO_WIDE_POSITION;
		size += 2; // x.w, y.w
	}
	if (w >= 256 || h >= 256) {
		type |= UNDO_WIDE_SIZE;
	}

	if (h == 1 && w <= 255) {
		if (w == 1) { type |= UNDO_POINT; }
		else { type |= UNDO_ROW; size += (type & UNDO_WIDE_SIZE) ? 2 : 1; }
		size += w * h;
	} else if (w == 1) {
		type |= UNDO_COLUMN;
		size += w * h + ((type & UNDO_WIDE_SIZE) ? 2 : 1);
	} else if ((w * h) > 512) {
		type |= UNDO_RECTANGLE_REF;
		size += (type & UNDO_WIDE_SIZE) ? 4 : 2; // w,h
		size += 8; // pointer to REF data
	} else {
		type |= UNDO_RECTANGLE;
		size += (type & UNDO_WIDE_SIZE) ? 4 : 2; // w,h
		size += w * h;
	}
	return (UndoSizeType) { .size = size, .type = type };
}

static uint8_t* AddUndoDraw(uint8_t type, uint8_t* undo_buffer, Area area) {
	if (!undo_buffer) { return NULL; }
	uint16_t h = area.h, w = area.w, y = area.y, x = area.x;
	// store size
	uint8_t base = type & ~(UNDO_WIDE_POSITION | UNDO_WIDE_SIZE);
	if (type & UNDO_WIDE_SIZE) {
		if (base == UNDO_COLUMN || base == UNDO_RECTANGLE || base == UNDO_RECTANGLE_REF) {
			*undo_buffer++ = (uint8_t)(h >> 8);
			*undo_buffer++ = (uint8_t)(h & 0xff);
		}
		if (base == UNDO_ROW || base == UNDO_RECTANGLE || base == UNDO_RECTANGLE_REF) {
			*undo_buffer++ = (uint8_t)(w >> 8);
			*undo_buffer++ = (uint8_t)(w & 0xff);
		}
	} else {
		if (base == UNDO_COLUMN || base == UNDO_RECTANGLE || base == UNDO_RECTANGLE_REF) {
			*undo_buffer++ = (uint8_t)h;
		}
		if (base == UNDO_ROW || base == UNDO_RECTANGLE || base == UNDO_RECTANGLE_REF) {
			*undo_buffer++ = (uint8_t)w;
		}
	}

	// store position
	if (type & UNDO_WIDE_POSITION) {
		*undo_buffer++ = (uint8_t)(y >> 8);
		*undo_buffer++ = (uint8_t)(y & 0xff);
		*undo_buffer++ = (uint8_t)(x >> 8);
		*undo_buffer++ = (uint8_t)(x & 0xff);
	} else {
		*undo_buffer++ = (uint8_t)y;
		*undo_buffer++ = (uint8_t)x;
	}

	// store type
	*undo_buffer++ = type;
	return undo_buffer;
}

typedef union {
	uint8_t* ptr;
	uint8_t bts[8];
} ptr2bts;

// Undo saves the data from the previous rectangle and updates the previous pixels with the current pixels
static uint8_t* AddUndoRectangle(Sleber* app, uint8_t* undo_buffer, Area undoArea, uint8_t type) {
	if (undo_buffer == NULL) { return NULL; }
	const int stride = app->width;
	const uint8_t* pixels = app->pixels + undoArea.y * stride + undoArea.x;
	const uint8_t* undo_source_info = app->pixels_info + undoArea.y * stride + undoArea.x;
	uint8_t* prev_area = app->pixels_prev + undoArea.y * stride + undoArea.x;
	uint8_t* undo_ret = undo_buffer + undoArea.w * undoArea.h;

	if ((type & ~(UNDO_WIDE_POSITION | UNDO_WIDE_SIZE)) == UNDO_RECTANGLE_REF) {
		uint8_t* save_area = (uint8_t*)calloc(1, undoArea.w * undoArea.h);
		if (save_area == NULL) { return NULL; }
		*(ptr2bts*)undo_buffer = (ptr2bts){ .ptr = save_area };
		undo_ret = undo_buffer + sizeof(ptr2bts);
		undo_buffer = save_area;
	}

	for (int iy = 0; iy < undoArea.h; ++iy) {
		for (int ix = 0; ix < undoArea.w; ++ix) {
			*undo_buffer++ = prev_area[ix];
			prev_area[ix] = pixels[ix] | (undo_source_info[ix] << 4);
		}
		prev_area += stride;
		pixels += stride;
		undo_source_info += stride;
	}
	return undo_ret;
}

// Redo saves the data from the active frame buffer and does not restore the previous state
static uint8_t* AddRedoRectangle(Sleber* app, uint8_t* redo_buffer, Area redoArea, uint8_t type) {
	if (redo_buffer == NULL) { return NULL; }

	const int stride = app->width;
	const uint8_t* pixels = app->pixels + redoArea.y * stride + redoArea.x;
	const uint8_t* info = app->pixels_info + redoArea.y * stride + redoArea.x;
	uint8_t* undo_ret = redo_buffer + redoArea.w * redoArea.h;

	if ((type & ~(UNDO_WIDE_POSITION | UNDO_WIDE_SIZE)) == UNDO_RECTANGLE_REF) {
		uint8_t* save_area = (uint8_t*)calloc(1, redoArea.w * redoArea.h);
		if (save_area == NULL) { return NULL; }
		*(ptr2bts*)redo_buffer = (ptr2bts){ .ptr = save_area };
		undo_ret = redo_buffer + sizeof(ptr2bts);
		redo_buffer = save_area;
	}

	for (int iy = 0; iy < redoArea.h; ++iy) {
		for (int ix = 0; ix < redoArea.w; ++ix) {
			*redo_buffer++ = pixels[ix] | (info[ix] << 4);
		}
		pixels += stride;
		info += stride;
	}
	return undo_ret;
}

static void AddUndoArea(Sleber* app, const Area undoArea) {
	UndoSizeType size_type = CalculateSizeType(undoArea);

	DEBUG_ASSERT(undoArea.x + undoArea.w <= app->width && undoArea.y + undoArea.h <= app->height);

	uint8_t* undo_buffer = ReserveUndoCommandBuffer(app, size_type.size, false);
	undo_buffer = AddUndoRectangle(app, undo_buffer, undoArea, size_type.type);
	undo_buffer = AddUndoDraw(size_type.type, undo_buffer, undoArea);
}

static Area CheckUndoArea(Sleber* app, Area baseRect) {
	const uint32_t ux = HorizontalUnit(app);
	const uint32_t uy = VerticalUnit(app);
	const uint32_t x = SnapToUnit(baseRect.x, ux), y = SnapToUnit(baseRect.y, uy);
	const uint32_t w = SnapToUnitCeil(baseRect.x + baseRect.w, ux) - x;
	const uint32_t h = SnapToUnitCeil(baseRect.y + baseRect.h, uy) - y;
	const uint32_t stride = app->width;
	const uint8_t* pixels = app->pixels + y * stride + x;
	const uint8_t* pixels_info = app->pixels_info + y * stride + x;
	const uint8_t* pixels_prev = app->pixels_prev + y * stride + x;
	uint32_t x0 = UINT32_MAX, x1 = 0, y0 = UINT32_MAX, y1 = 0;
	for (uint32_t cy = 0; cy < h; cy++) {
		for (uint32_t cx = 0; cx < w; cx++) {
			if ((pixels[cx] | (pixels_info[cx] << 4)) != pixels_prev[cx]) {
				if (cx < x0) { x0 = cx; }
				if (cx >= x1) { x1 = cx + 1; }
				if (cy < y0) { y0 = cy; }
				if (cy >= y1) { y1 = cy + 1; }
			}
		}
		pixels += stride;
		pixels_info += stride;
		pixels_prev += stride;
	}

	return (Area) {
		.x = (uint16_t)(x0 == UINT32_MAX ? 0 : x0 + x),
			.y = (uint16_t)(y0 == UINT32_MAX ? 0 : y0 + y),
			.w = x1 == 0 ? 0 : (uint16_t)(x1 - x0),
			.h = y1 == 0 ? 0 : (uint16_t)(y1 - y0)
	};
}

static bool AreaIsEmpty(Area area) {
	return area.w == 0 || area.h == 0;
}

static bool DestroyUndoBuffer(Sleber* app, bool redo) {
	UndoCommandBuffer* buffer = redo ? app->redo_command_buffer : app->undo_command_buffer;
	while (buffer) {
		const uint8_t* data = buffer->data;
		uint32_t buffer_size = buffer->size;

		const uint8_t* buffer_start = data;
		const uint8_t* undo_ptr = data + buffer_size;

		while (undo_ptr != buffer_start) {
			uint8_t type = *--undo_ptr;
			uint8_t base = type & ~(UNDO_WIDE_POSITION | UNDO_WIDE_SIZE);

			if (base == UNDO_NONE || base >= UNDO_COUNT) {
				return false;
			}

			if (type == UNDO_FRAME || type == UNDO_STOP) { continue; }
			if (type == UNDO_MODE) {
				if ((uint32_t)(undo_ptr - buffer_start) < 1) { return false; }
				--undo_ptr;
				continue;
			}
			if (type == UNDO_SIZE) {
				if ((uint32_t)(undo_ptr - buffer_start) < 4) { return false; }
				undo_ptr -= 4;
				continue;
			}

			if (type & UNDO_WIDE_POSITION) {
				if ((uint32_t)(undo_ptr - buffer_start) < 4) { return false; }
				undo_ptr -= 4;
			} else {
				if ((uint32_t)(undo_ptr - buffer_start) < 2) { return false; }
				undo_ptr -= 2;
			}

			int w = 1, h = 1;
			if (type & UNDO_WIDE_SIZE) {
				if (base == UNDO_ROW || base == UNDO_RECTANGLE || base == UNDO_RECTANGLE_REF) {
					if ((uint32_t)(undo_ptr - buffer_start) < 2) { return false; }
					w = (uint16_t)(undo_ptr[-1] | ((uint16_t)undo_ptr[-2] << 8));
					undo_ptr -= 2;
				}
				if (base == UNDO_COLUMN || base == UNDO_RECTANGLE || base == UNDO_RECTANGLE_REF) {
					if ((uint32_t)(undo_ptr - buffer_start) < 2) { return false; }
					h = (uint16_t)(undo_ptr[-1] | ((uint16_t)undo_ptr[-2] << 8));
					undo_ptr -= 2;
				}
			} else {
				if (base == UNDO_ROW || base == UNDO_RECTANGLE || base == UNDO_RECTANGLE_REF) {
					if ((uint32_t)(undo_ptr - buffer_start) < 1) { return false; }
					w = *--undo_ptr;
				}
				if (base == UNDO_COLUMN || base == UNDO_RECTANGLE || base == UNDO_RECTANGLE_REF) {
					if ((uint32_t)(undo_ptr - buffer_start) < 1) { return false; }
					h = *--undo_ptr;
				}
			}

			uint32_t payload_size = base == UNDO_RECTANGLE_REF ? sizeof(ptr2bts) : (uint32_t)w * h;
			if ((uint32_t)(undo_ptr - buffer_start) < payload_size) { return false; }
			if (base == UNDO_RECTANGLE_REF) {
				uint8_t* ptr = (*(ptr2bts*)(undo_ptr - sizeof(ptr2bts))).ptr;
				if (ptr) { free(ptr); }
				else { return false; }
			}
			undo_ptr -= payload_size;
		}
		UndoCommandBuffer* next_buffer = buffer->next;
		free(buffer);
		buffer = next_buffer;
	}
	if (redo) { app->redo_command_buffer = NULL; }
	else { app->undo_command_buffer = NULL; }
	return true;
}

#ifdef DEBUG

typedef struct {
	uint32_t width;
	uint32_t height;
} UndoValidationContext;

static bool ValidateUndoCommands(Sleber* app, bool redo) {
	UndoValidationContext validation = { (uint32_t)app->width, (uint32_t)app->height };
	UndoCommandBuffer* buffer = redo ? app->redo_command_buffer : app->undo_command_buffer;
	while (buffer) {
		const uint8_t* data = buffer->data;
		uint32_t buffer_size = buffer->size;

		const uint8_t* buffer_start = data;
		const uint8_t* undo_ptr = data + buffer_size;

		while (undo_ptr != buffer_start) {
			uint8_t type = *--undo_ptr;
			uint8_t base = type & ~(UNDO_WIDE_POSITION | UNDO_WIDE_SIZE);
			uint16_t x = 0, y = 0, w = 1, h = 1;

			if (base == UNDO_NONE || base >= UNDO_COUNT) {
				debugstop; return false;
			}

			if (type == UNDO_FRAME || type == UNDO_STOP) { continue; }
			else if (type == UNDO_MODE || type == UNDO_BORDER_COLOR) {
				if ((uint32_t)(undo_ptr - buffer_start) < 1) { debugstop; return false; }
				--undo_ptr;
				continue;
			} else if (type == UNDO_SHARED_COLOR) {
				if ((uint32_t)(undo_ptr - buffer_start) < 2) { debugstop; return false; }
				undo_ptr -= 2;
				continue;
			} else if (type == UNDO_SIZE) {
				if ((uint32_t)(undo_ptr - buffer_start) < 4) { debugstop; return false; }
				uint16_t width = (uint16_t)(undo_ptr[-3] | ((uint16_t)undo_ptr[-4] << 8));
				uint16_t height = (uint16_t)(undo_ptr[-1] | ((uint16_t)undo_ptr[-2] << 8));
				if (width == 0 || height == 0) { debugstop; return false; }
				validation.width = width;
				validation.height = height;
				undo_ptr -= 4;
				continue;
			}

			if (base != UNDO_POINT && base != UNDO_ROW && base != UNDO_COLUMN &&
				base != UNDO_RECTANGLE && base != UNDO_RECTANGLE_REF && 
				base != UNDO_BORDER_COLOR && base != UNDO_SHARED_COLOR) {
				debugstop; return false;
			}
			if (base == UNDO_POINT && (type & UNDO_WIDE_SIZE)) { debugstop; return false; }

			if (type & UNDO_WIDE_POSITION) {
				if ((uint32_t)(undo_ptr - buffer_start) < 4) { debugstop; return false; }
				x = (uint16_t)(undo_ptr[-1] | ((uint16_t)undo_ptr[-2] << 8));
				y = (uint16_t)(undo_ptr[-3] | ((uint16_t)undo_ptr[-4] << 8));
				undo_ptr -= 4;
			} else {
				if ((uint32_t)(undo_ptr - buffer_start) < 2) { debugstop; return false; }
				x = undo_ptr[-1];
				y = undo_ptr[-2];
				undo_ptr -= 2;
			}

			if (type & UNDO_WIDE_SIZE) {
				if (base == UNDO_ROW || base == UNDO_RECTANGLE || base == UNDO_RECTANGLE_REF) {
					if ((uint32_t)(undo_ptr - buffer_start) < 2) { debugstop; return false; }
					w = (uint16_t)(undo_ptr[-1] | ((uint16_t)undo_ptr[-2] << 8));
					undo_ptr -= 2;
				}
				if (base == UNDO_COLUMN || base == UNDO_RECTANGLE || base == UNDO_RECTANGLE_REF) {
					if ((uint32_t)(undo_ptr - buffer_start) < 2) { debugstop; return false; }
					h = (uint16_t)(undo_ptr[-1] | ((uint16_t)undo_ptr[-2] << 8));
					undo_ptr -= 2;
				}
			} else {
				if (base == UNDO_ROW || base == UNDO_RECTANGLE || base == UNDO_RECTANGLE_REF) {
					if ((uint32_t)(undo_ptr - buffer_start) < 1) { debugstop; return false; }
					w = *--undo_ptr;
				}
				if (base == UNDO_COLUMN || base == UNDO_RECTANGLE || base == UNDO_RECTANGLE_REF) {
					if ((uint32_t)(undo_ptr - buffer_start) < 1) { debugstop; return false; }
					h = *--undo_ptr;
				}
			}

			if (w == 0 || h == 0 || x >= validation.width || y >= validation.height ||
				(uint32_t)x + w > validation.width ||
				(uint32_t)y + h > validation.height) {
				debugstop; return false;
			}
			if ((base == UNDO_POINT && (w != 1 || h != 1)) ||
				(base == UNDO_ROW && h != 1) || (base == UNDO_COLUMN && w != 1)) {
				debugstop; return false;
			}

			uint32_t payload_size = base == UNDO_RECTANGLE_REF ? sizeof(ptr2bts) : (uint32_t)w * h;
			if ((uint32_t)(undo_ptr - buffer_start) < payload_size) { debugstop; return false; }
			if (base == UNDO_RECTANGLE_REF && (*(ptr2bts*)(undo_ptr - sizeof(ptr2bts))).ptr == NULL) {
				debugstop; return false;
			}
			undo_ptr -= payload_size;
		}
		buffer = buffer->next;
	}
	return true;
}

bool IsUndoEnd(Sleber* app, const uint8_t* buffer, bool redo);
#endif

static bool CheckAndAddUndoArea(Sleber* app, Area changeArea, bool addStop) {
	const Area undoArea = CheckUndoArea(app, changeArea);
	if (AreaIsEmpty(undoArea)) { return false; }
#ifdef DEBUG
	ValidateUndoCommands(app, false);
#endif
	if (addStop) {
		uint8_t* undo_buffer = ReserveUndoCommandBuffer(app, 1, false);
		if (undo_buffer) { *undo_buffer = UNDO_STOP; }
	}
	AddUndoArea(app, undoArea);
#ifdef DEBUG
	ValidateUndoCommands(app, false);
#endif
	return true;
}

static void UpdateChangeActions(Sleber* app) {
	if (sNumChangeActions > 0) {
		uint8_t Skip[MAX_CHANGE_ACTIONS>>3] = { 0 };

		// remove actions that are contained inside any other action
		for(int i=0; i < sNumChangeActions; ++i) {
			uint16_t x = sChangeActionPos[i].x, y = sChangeActionPos[i].y;
			uint16_t z = x + sChangeActionSize[i].x, w = y + sChangeActionSize[i].y;
			for(int j=i+1; j < sNumChangeActions; ++j) {
				if (Skip[j>>3] & (1 << (j&7))) { continue; }
				uint16_t jx = sChangeActionPos[j].x, jy = sChangeActionPos[j].y;
				uint16_t jz = jx + sChangeActionSize[j].x, jw = jy + sChangeActionSize[j].y;

				if(x>=jx && y>=jy && z<=jz && w<=jw) {
					Skip[i>>3] |= (1 << (i&7));
					break;
				}

				if (jx>=x && jy>=y && jz<=z && jw<=w) {
					Skip[j>>3] |= (1 << (j&7));
				}
			}
		}

		// attempt to connect neighbors
		for (int i = 0; i < sNumChangeActions; ++i) {
			if (Skip[i >> 3] & (1 << (i & 7))) { continue; }
			uint16_t ix = sChangeActionPos[i].x, iy = sChangeActionPos[i].y;
			uint16_t iw = sChangeActionSize[i].x, ih = sChangeActionSize[i].y;
			for (int j = i + 1; j < sNumChangeActions; ++j) {
				if (Skip[j >> 3] & (1 << (j & 7))) { continue; }
				uint16_t jx = sChangeActionPos[j].x, jy = sChangeActionPos[j].y;
				uint16_t jw = sChangeActionSize[j].x, jh = sChangeActionSize[j].y;
				bool drop = false;

				if (iw == jw && ix == jx) {
					if(iy + ih == jy) {
						sChangeActionSize[i].y += jh;
						drop = true;
					} else if (jy + jh == iy) {
						sChangeActionPos[i].y = jy;
						sChangeActionSize[i].y += jh;
						drop = true;
					}
				} else if (ih == jh && iy == jy) {
					if (ix + iw == jx) {
						sChangeActionSize[i].x += jw;
						drop = true;
					} else if (jx + jw == ix) {
						sChangeActionPos[i].x = jx;
						sChangeActionSize[i].x += jw;
						drop = true;
					}
				}
				if (drop) {
					Skip[j >> 3] |= (1 << (j & 7));
				}
			}
		}

		// log the change actions
		int change_count = 0;
		for(int i=0; i<sNumChangeActions; ++i) {
			if (Skip[i >> 3] & (1 << (i & 7))) { continue; }
			++change_count;
		}

		if (change_count > 0) {
			uint8_t* undo_buffer = ReserveUndoCommandBuffer(app, 1, false);
			if (undo_buffer) {
				undo_buffer[0] = !app->mouse_drawn ? UNDO_STOP : UNDO_FRAME;
			}
			app->mouse_drawn = true;
		}

		for (int i = 0; i < sNumChangeActions; ++i) {
			if (Skip[i >> 3] & (1 << (i & 7))) { continue; }
			uint16_t x = sChangeActionPos[i].x, y = sChangeActionPos[i].y;
			uint16_t w = sChangeActionSize[i].x, h = sChangeActionSize[i].y;
			UndoSizeType size_type = CalculateSizeType((Area) { x, y, w, h });

			// store previous pixels
			uint8_t* undo_buffer = ReserveUndoCommandBuffer(app, size_type.size, false);
			undo_buffer = AddUndoRectangle(app, undo_buffer, (Area) { x, y, w, h }, size_type.type);
			undo_buffer = AddUndoDraw(size_type.type, undo_buffer, (Area) { x, y, w, h });
#ifdef DEBUG
			DEBUG_ASSERT(IsUndoEnd(app, undo_buffer, false));
#endif
		}
		sNumChangeActions = 0;
		sChangeMin = (I16Vec2){ 0xffff, 0xffff };
		sChangeMax = (I16Vec2){ 0, 0 };
		DestroyUndoBuffer(app, true);
	}
}

// thinky thoughts:
// 1. When undoing and reaching UDNO_STOP, put an UNDO_STOP into the redo buffer
// 2. If the undo buffer is non-empty insert an UNDO_FRAME into the redo buffer
// 3. each time encountering an UNDO_FRAME in the undo buffer, insert an UNDO_FRAME into the redo buffer
// 4. When making any edit erase the redo buffer

static void DoUndoLocked(Sleber* app, bool frame, bool redo) {
#ifdef DEBUG
	ValidateUndoCommands(app, redo);
#endif
	bool first_in_frame = true;
	if (SneakUndoCommand(app, redo) == UNDO_STOP) {
		DrainUndoBuffer(app, 1, redo);
		uint8_t* undo_buffer = ReserveUndoCommandBuffer(app, 1, !redo);
		if (undo_buffer) { *undo_buffer = UNDO_STOP; }
	}

	for(;;) {
#ifdef DEBUG
		ValidateUndoCommands(app, redo);
#endif

		uint8_t* undo_start = NULL;
		uint8_t* undo_ptr = GrabUndoCommand(app, &undo_start, redo);
		uint32_t size = 1;

		if (!undo_ptr || undo_ptr == undo_start) {
			break;
		}

		uint8_t type = *--undo_ptr;
		uint8_t base = type & ~(UNDO_WIDE_POSITION | UNDO_WIDE_SIZE);
		uint16_t x = 0, y = 0, w = 1, h = 1;

		if (first_in_frame) {
			uint8_t* undo_buffer = ReserveUndoCommandBuffer(app, 1, !redo);
			if (undo_buffer) { *undo_buffer = UNDO_FRAME; }
			first_in_frame = false;
		}

		if (type == UNDO_FRAME) {
			DrainUndoBuffer(app, 1,	redo);
			if (frame) { break; }
			first_in_frame = true;
			continue;
		} else if (type == UNDO_STOP) {
			DrainUndoBuffer(app, 1, redo);
			uint8_t* undo_buffer = ReserveUndoCommandBuffer(app, 1, !redo);
			if (undo_buffer) { *undo_buffer = UNDO_STOP; }
			break;
		}

		DEBUG_ASSERT(base != UNDO_NONE && (type < (UNDO_WIDE_POSITION | UNDO_WIDE_SIZE) || base <= UNDO_RECTANGLE_REF));

		switch(type) {
			case UNDO_MODE:
				DEBUG_ASSERT((undo_ptr - undo_start) >= 1);
				SetScreenModeLocked(app, *--undo_ptr);
				DrainUndoBuffer(app, 2, redo);
				continue;
			case UNDO_SIZE:
				DEBUG_ASSERT((undo_ptr - undo_start) >= 4);
				h = (uint16_t)(*--undo_ptr);
				h |= (uint16_t)(*--undo_ptr) << 8;
				w = (uint16_t)(*--undo_ptr);
				w |= (uint16_t)(*--undo_ptr) << 8;
				if(w != app->width || h != app->height) {
					ResizePixelBuffersLocked(app, w, h);
				}
				DrainUndoBuffer(app, 5, redo);
				continue;
			case UNDO_BORDER_COLOR:
				DEBUG_ASSERT((undo_ptr - undo_start) >= 1);
				app->border_color = *--undo_ptr;
				DrainUndoBuffer(app, 2, redo);
				RequestRedraw(app);
				RequestRedrawPalette(app);
				continue;
			case UNDO_SHARED_COLOR: {
				DEBUG_ASSERT((undo_ptr - undo_start) >= 2);
				uint8_t color_index = *--undo_ptr;
				DEBUG_ASSERT(color_index < MAX_SCREEN_BACKGROUND_COUNT);
				app->shared_colors[color_index] = *--undo_ptr;
				DrainUndoBuffer(app, 3, redo);
				RequestRedraw(app);
				RequestRedrawPalette(app);
				continue;
			}
		}

		if (type & UNDO_WIDE_POSITION) {
			DEBUG_ASSERT((undo_ptr - undo_start) >= 4);
			x = (uint16_t)(*--undo_ptr);
			x |= (uint16_t)(*--undo_ptr) << 8;
			y = (uint16_t)(*--undo_ptr);
			y |= (uint16_t)(*--undo_ptr) << 8;
			size += 4;
		} else {
			DEBUG_ASSERT((undo_ptr - undo_start) >= 2);
			x = *--undo_ptr;
			y = *--undo_ptr;
			size += 2;
		}

		if (type & UNDO_WIDE_SIZE) {
			if (base == UNDO_ROW || base == UNDO_RECTANGLE || base == UNDO_RECTANGLE_REF) {
				DEBUG_ASSERT((undo_ptr - undo_start) >= 2);
				w = (uint16_t)(*--undo_ptr);
				w |= (uint16_t)(*--undo_ptr) << 8;
				size += 2;
			}
			if (base == UNDO_COLUMN || base == UNDO_RECTANGLE || base == UNDO_RECTANGLE_REF) {
				DEBUG_ASSERT((undo_ptr - undo_start) >= 2);
				h = (uint16_t)(*--undo_ptr);
				h |= (uint16_t)(*--undo_ptr) << 8;
				size += 2;
			}
		} else {
			if (base == UNDO_ROW || base == UNDO_RECTANGLE || base == UNDO_RECTANGLE_REF) {
				DEBUG_ASSERT((undo_ptr - undo_start) >= 1);
				w = *--undo_ptr;
				size += 1;
			}
			if (base == UNDO_COLUMN || base == UNDO_RECTANGLE || base == UNDO_RECTANGLE_REF) {
				DEBUG_ASSERT((undo_ptr - undo_start) >= 1);
				h = *--undo_ptr;
				size += 1;
			}
		}
		size += base == UNDO_RECTANGLE_REF ? sizeof(ptr2bts) : (w * h);

		DEBUG_ASSERT(x < app->width && y < app->height);
		DEBUG_ASSERT((x + w) <= app->width && (y + h) <= app->height);

		// Redo = create an undo based on the current undo command
		// store current pixels
		uint8_t* redo_buffer = ReserveUndoCommandBuffer(app, size, !redo);
		if (redo_buffer) {
			redo_buffer = AddRedoRectangle(app, redo_buffer, (Area) { x, y, w, h }, type);
			AddUndoDraw(type, redo_buffer, (Area) { x, y, w, h });
		}

		uint8_t* undo_data_ref = NULL;
		if ((type & ~(UNDO_WIDE_POSITION | UNDO_WIDE_SIZE)) == UNDO_RECTANGLE_REF) {
			undo_data_ref = undo_ptr = (*(ptr2bts*)(undo_ptr-sizeof(ptr2bts))).ptr;
		} else {
			undo_ptr -= w * h;
		}

		uint8_t* pixels = app->pixels + y * app->width + x;
		uint8_t* pixels_info = app->pixels_info + y * app->width + x;

		for (uint16_t iy = 0; iy < h; ++iy) {
			for (uint16_t ix = 0; ix < w; ++ix) {
				uint8_t pixel = *undo_ptr;
				uint8_t prev_pixel = pixels[ix] | (pixels_info[ix] << 4);
				*undo_ptr++ = prev_pixel;
				pixels[ix] = pixel & 0x0f;
				pixels_info[ix] = (pixel >> 4) & 0x0f;
			}
			pixels += app->width;
			pixels_info += app->width;
		}
		DrainUndoBuffer(app, size, redo);
		if (undo_data_ref) {
			free(undo_data_ref);
		}
	}
#ifdef DEBUG
	ValidateUndoCommands(app, redo);
#endif
}

void DoUndo(Sleber* app, bool frame, bool redo) {
	LockMutex(&app->canvas_mutex);
	DoUndoLocked(app, frame, redo);
	UnlockMutex(&app->canvas_mutex);
}

void SetScreenModeLocked(Sleber* app, uint8_t mode) {
	if (mode > SCREEN_MODE_WIDE_UNRESTRICTED) {
		return;
	}
	app->screen_mode = mode;
	switch (mode) {
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
	DebugLog("Screen mode changed to %d\n", mode);
}

void SetScreenMode(Sleber* app, uint8_t mode) {
	LockMutex(&app->canvas_mutex);
	SetScreenModeLocked(app, mode);
	UnlockMutex(&app->canvas_mutex);
}

void ClearUndoHistory(Sleber* app) {
	DestroyUndoBuffer(app, true);
	DestroyUndoBuffer(app, false);
}

// TODO: Hold a mutex while changing canvas settings that is blocking / blocked by bitmap draw
bool ApplyCanvasSettings(Sleber* app, int width, int height, uint8_t mode,
	bool clear_canvas, bool clear_undo) {
	// TODO: Check real size restrictions
	if (!app || width < 1 || height < 1 || width > MAX_CANVAS_WIDTH || height > MAX_CANVAS_HEIGHT ||
		mode > SCREEN_MODE_WIDE_UNRESTRICTED) {
		return false;
	}

	bool size_changed = width != app->width || height != app->height;
	uint8_t* new_pixels = NULL;
	uint8_t* new_pixels_info = NULL;
	uint8_t* new_pixels_prev = NULL;
	if (size_changed && !AllocatePixelBuffers(width, height,
		&new_pixels, &new_pixels_info, &new_pixels_prev)) {
		return false;
	}

	if (size_changed && mode == app->screen_mode) {
		LockMutex(&app->canvas_mutex);
		AddUndoScreenMode(app, false);
		UnlockMutex(&app->canvas_mutex);
	}

	if (mode != app->screen_mode && !SwitchScreenMode(app, mode) &&
		app->screen_mode != mode) {
		free(new_pixels);
		free(new_pixels_info);
		free(new_pixels_prev);
		return false;
	}
	if (size_changed) {
		LockMutex(&app->canvas_mutex);
		const int copy_width = width < app->width ? width : app->width;
		const int copy_height = height < app->height ? height : app->height;
		for (int y = 0; y < copy_height; ++y) {
			memcpy(new_pixels + (size_t)y * width,
				app->pixels + (size_t)y * app->width, (size_t)copy_width);
			memcpy(new_pixels_info + (size_t)y * width,
				app->pixels_info + (size_t)y * app->width, (size_t)copy_width);
			memcpy(new_pixels_prev + (size_t)y * width,
				app->pixels_prev + (size_t)y * app->width, (size_t)copy_width);
		}
		free(app->pixels);
		free(app->pixels_info);
		free(app->pixels_prev);
		app->pixels = new_pixels;
		app->pixels_info = new_pixels_info;
		app->pixels_prev = new_pixels_prev;
		app->width = width;
		app->height = height;
		UnlockMutex(&app->canvas_mutex);
	}
	if (clear_canvas) {
		// TODO: Clear with actual clear color
		LockMutex(&app->canvas_mutex);
		memset(app->pixels, 0, (size_t)app->width * (size_t)app->height);
		memset(app->pixels_info, 0, (size_t)app->width * (size_t)app->height);
		memset(app->pixels_prev, 0, (size_t)app->width * (size_t)app->height);
		UnlockMutex(&app->canvas_mutex);
	}
	if (clear_undo) {
		ClearUndoHistory(app);
	}
	RequestRedraw(app);
	return true;
}

void AddUndoScreenMode(Sleber* app, bool redo) {
	DestroyUndoBuffer(app, true);

	// store the full mode change
	// store size, mode and full frame as a rect
	StoreUndoCommand(app, UNDO_STOP, redo);

	AddUndoArea(app, ToArea(0, 0, app->width, app->height));

	uint8_t* size_buffer = StoreUndoCommandWithBuffer(app, UNDO_SIZE, 5, redo);
	if (size_buffer) {
		*size_buffer++ = (uint8_t)(app->width >> 8);
		*size_buffer++ = (uint8_t)(app->width & 0xff);
		*size_buffer++ = (uint8_t)(app->height >> 8);
		*size_buffer++ = (uint8_t)(app->height & 0xff);
	}

	uint8_t* mode_buffer = StoreUndoCommandWithBuffer(app, UNDO_MODE, 2, redo);
	if (mode_buffer) {
		*mode_buffer++ = app->screen_mode;
	}
}

// change the color of a specific type of pixels in an area
static void ChangeTypedColor(Sleber* app, uint8_t new_color, uint8_t type, int x, int y, int w, int h) {
	// check coordinates
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (w <= 0 || h <= 0 || x >= app->width || y >= app->height) { return; }
	if (x + w > app->width) { w = app->width - x; }
	if (y + h > app->height) { h = app->height - y; }
	int x0 = w, x1 = -1, y0 = h, y1 = -1;

	bool changed = false;
	uint8_t* pixels = app->pixels + y * app->width + x;
	uint8_t* types = app->pixels_info + y * app->width + x;
	for (int j = 0; j < h; ++j) {
		for (int i = 0; i < w; ++i) {
			if (types[i] == type && pixels[i] != new_color) {
				if (i < x0) { x0 = i; }
				if (i > x1) { x1 = i; }
				if (j < y0) { y0 = j; }
				if (j > y1) { y1 = j; }
				changed = true;
				pixels[i] = new_color;
			}
		}
		pixels += app->width;
		types += app->width;
	}
	// record the changed area
	if (changed && sNumChangeActions < MAX_CHANGE_ACTIONS) {
		sChangeActionPos[sNumChangeActions] = ToI16Vec2(x + x0, y + y0);
		sChangeActionSize[sNumChangeActions] = ToI16Vec2(x1 - x0 + 1, y1 - y0 + 1);
		sChangeMin.x = MIN(sChangeMin.x, (uint16_t)(x + x0));
		sChangeMin.y = MIN(sChangeMin.y, (uint16_t)(y + y0));
		sChangeMax.x = MAX(sChangeMax.x, (uint16_t)(x + x1));
		sChangeMax.y = MAX(sChangeMax.y, (uint16_t)(y + y1));
		++sNumChangeActions;
	}
}

// change the color of a specific type of pixels matching a specific color in an area
static bool ChangeTypedColorMatch(Sleber* app, uint8_t new_color, uint8_t prev_color, uint8_t type, int x, int y, int w, int h) {
	// check coordinates
	if (prev_color == new_color) { return false; }
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (w <= 0 || h <= 0 || x >= app->width || y >= app->height) { return false; }
	if (x + w > app->width) { w = app->width - x; }
	if (y + h > app->height) { h = app->height - y; }

	int x0 = w, x1 = -1, y0 = h, y1 = -1;
	uint8_t* pixels = app->pixels + y * app->width + x;
	uint8_t* types = app->pixels_info + y * app->width + x;
	bool changed = false;
	for (int j = 0; j < h; ++j) {
		for (int i = 0; i < w; ++i) {
			if (types[i] == type && pixels[i] == prev_color) {
				if (i < x0) { x0 = i; }
				if (i > x1) { x1 = i; }
				if (j < y0) { y0 = j; }
				if (j > y1) { y1 = j; }
				pixels[i] = new_color;
				changed = true;
			}
		}
		pixels += app->width;
		types += app->width;
	}
	if (changed && sNumChangeActions < MAX_CHANGE_ACTIONS) {
		sChangeActionPos[sNumChangeActions] = ToI16Vec2(x + x0, y + y0);
		sChangeActionSize[sNumChangeActions] = ToI16Vec2(x1 - x0 + 1, y1 - y0 + 1);
		sChangeMin.x = MIN(sChangeMin.x, (uint16_t)(x + x0));
		sChangeMin.y = MIN(sChangeMin.y, (uint16_t)(y + y0));
		sChangeMax.x = MAX(sChangeMax.x, (uint16_t)(x + x1));
		sChangeMax.y = MAX(sChangeMax.y, (uint16_t)(y + y1));
		++sNumChangeActions;
	}

	return changed;
}

struct CellCols GetCellColsAt(Sleber* app, int x, int y) {
	struct CellCols cols = { 0xff, 0xff };
	if (x < 0 || x >= app->width || y < 0 || y >= app->height) { return cols; }

	int stride = app->width;
	int cx = x & ~7, cy = y & ~7;
	int cw = MIN(8, app->width - cx), ch = MIN(8, app->height - cy);
	uint8_t* char_pixel = app->pixels + (cy * stride + cx);
	uint8_t* type_pixel = app->pixels_info + (cy * stride + cx);
	for (int chr_y = 0; chr_y < ch; ++chr_y) {
		for (int chr_x = 0; chr_x < cw; ++chr_x) {
			if (type_pixel[chr_x] == GPX_PIXEL_BG && cols.bg == 0xff) {
				cols.bg = char_pixel[chr_x];
			} else if (type_pixel[chr_x] == GPX_PIXEL_FG && cols.fg == 0xff) {
				cols.fg = char_pixel[chr_x];
			}
		}
		if (cols.bg != 0xff && cols.fg != 0xff) { break; }
		char_pixel += stride;
		type_pixel += stride;
	}
	return cols;
}

struct CellCols GetSpriteColsAt(Sleber* app, int x, int y) {
	struct CellCols cols = { 0xff, 0xff };
	if (x < 0 || x >= app->width || y < 0 || y >= app->height) { return cols; }

	int stride = app->width;
	int cx = SnapToUnit(x, 24), cy = SnapToUnit(y, 21);
	int cw = MIN(24, app->width - cx), ch = MIN(21, app->height - cy);
	uint8_t* sprite_pixel = app->pixels + (cy * stride + cx);
	uint8_t* type_pixel = app->pixels_info + (cy * stride + cx);
	for (int spr_y = 0; spr_y < ch; ++spr_y) {
		for (int spr_x = 0; spr_x < cw; ++spr_x) {
			if (type_pixel[spr_x] == GPX_PIXEL_BG && cols.bg == 0xff) {
				cols.bg = sprite_pixel[spr_x];
			} else if (type_pixel[spr_x] == GPX_PIXEL_FG && cols.fg == 0xff) {
				cols.fg = sprite_pixel[spr_x];
			}
		}
		if (cols.bg != 0xff && cols.fg != 0xff) { break; }
		sprite_pixel += stride;
		type_pixel += stride;
	}
	return cols;
}

static struct CellColsMC GetAreaColsMCAtInt(Sleber* app, int cx, int cy, int cw, int ch) {
	struct CellColsMC cols = { 0xff, 0xff, 0xff, 0xff };

	int stride = app->width;
	uint8_t* char_pixel = app->pixels + (cy * stride + cx);
	uint8_t* type_pixel = app->pixels_info + (cy * stride + cx);
	for (int chr_y = 0; chr_y < ch; ++chr_y) {
		for (int chr_x = 0; chr_x < cw; ++chr_x) {
			if (type_pixel[chr_x] == GPX_PIXEL_BG && cols.bg == 0xff) {
				cols.bg = char_pixel[chr_x];
			} else if (type_pixel[chr_x] == GPX_PIXEL_FG && cols.fg == 0xff) {
				cols.fg = char_pixel[chr_x];
			} else if (type_pixel[chr_x] == GPX_PIXEL_MC0 && cols.m0 == 0xff) {
				cols.m0 = char_pixel[chr_x];
			} else if (type_pixel[chr_x] == GPX_PIXEL_MC1 && cols.m1 == 0xff) {
				cols.m1 = char_pixel[chr_x];
			}
		}
		if (cols.bg != 0xff && cols.fg != 0xff && cols.m0 != 0xff && cols.m1 != 0xff) { break; }
		char_pixel += stride;
		type_pixel += stride;
	}
	return cols;
}

struct CellColsMC GetCellColsMCAt(Sleber* app, int x, int y) {
	if (x < 0 || x >= app->width || y < 0 || y >= app->height) { return (struct CellColsMC) { 0xff, 0xff, 0xff, 0xff }; }

	int cx = x & ~7, cy = y & ~7;
	int cw = MIN(8, app->width - cx), ch = MIN(8, app->height - cy);
	return GetAreaColsMCAtInt(app, cx, cy, cw, ch);
}

struct CellColsMC GetSpriteColsMCAt(Sleber* app, int x, int y) {
	if (x < 0 || x >= app->width || y < 0 || y >= app->height) { return (struct CellColsMC) { 0xff, 0xff, 0xff, 0xff }; }

	int cx = SnapToUnit(x, 24), cy = SnapToUnit(y, 21);
	int cw = MIN(24, app->width - cx), ch = MIN(21, app->height - cy);
	return GetAreaColsMCAtInt(app, cx, cy, cw, ch);
}

static struct CellColsMC GetCellColsMcAtMaskInt(Sleber* app, int cx, int cy, int cw, int ch, const CharMask mask) {
	struct CellColsMC cols = { 0xff, 0xff, 0xff, 0xff };
	int stride = app->width;
	uint8_t* char_pixel = app->pixels + (cy * stride + cx);
	uint8_t* type_pixel = app->pixels_info + (cy * stride + cx);
	for (int chr_y = 0; chr_y < ch; ++chr_y) {
		uint8_t mask_byte = mask.row[chr_y];
		for (int chr_x = 0; chr_x < cw; ++chr_x) {
			if (!(mask_byte & (1 << chr_x))) {
				if (type_pixel[chr_x] == GPX_PIXEL_BG && cols.bg == 0xff) {
					cols.bg = char_pixel[chr_x];
				} else if (type_pixel[chr_x] == GPX_PIXEL_FG && cols.fg == 0xff) {
					cols.fg = char_pixel[chr_x];
				} else if (type_pixel[chr_x] == GPX_PIXEL_MC0 && cols.m0 == 0xff) {
					cols.m0 = char_pixel[chr_x];
				} else if (type_pixel[chr_x] == GPX_PIXEL_MC1 && cols.m1 == 0xff) {
					cols.m1 = char_pixel[chr_x];
				}
			}
		}
		if (cols.bg != 0xff && cols.fg != 0xff && cols.m0 != 0xff && cols.m1 != 0xff) { break; }
		char_pixel += stride;
		type_pixel += stride;
	}
	return cols;
}
static struct CellColsMC GetCellColsMcAtMask(Sleber* app, int x, int y, const CharMask mask) {
	if (x < 0 || x >= app->width || y < 0 || y >= app->height) { return (struct CellColsMC) { 0xff, 0xff, 0xff, 0xff }; }
	int cx = x & ~7, cy = y & ~7;
	int cw = MIN(8, app->width - cx), ch = MIN(8, app->height - cy);
	return GetCellColsMcAtMaskInt(app, cx, cy, cw, ch, mask);
}


static struct CellCols GetCellColsAtMaskInt(Sleber* app, int cx, int cy, int cw, int ch, const CharMask mask) {
	struct CellCols cols = { 0xff, 0xff };
	int stride = app->width;
	uint8_t* char_pixel = app->pixels + (cy * stride + cx);
	uint8_t* type_pixel = app->pixels_info + (cy * stride + cx);
	for (int chr_y = 0; chr_y < ch; ++chr_y) {
		uint8_t mask_byte = mask.row[chr_y];
		for (int chr_x = 0; chr_x < cw; ++chr_x) {
			if (!(mask_byte & (1 << chr_x))) {
				if (type_pixel[chr_x] == GPX_PIXEL_BG && cols.bg == 0xff) {
					cols.bg = char_pixel[chr_x];
				} else if (type_pixel[chr_x] == GPX_PIXEL_FG && cols.fg == 0xff) {
					cols.fg = char_pixel[chr_x];
				}
			}
		}
		if (cols.bg != 0xff && cols.fg != 0xff) { break; }
		char_pixel += stride;
		type_pixel += stride;
	}
	return cols;
}

static struct CellCols GetCellColsAtMask(Sleber* app, int x, int y, const CharMask mask) {
	if (x < 0 || x >= app->width || y < 0 || y >= app->height) { return (struct CellCols) { 0xff, 0xff }; }
	int cx = x & ~7, cy = y & ~7;
	int cw = MIN(8, app->width - cx), ch = MIN(8, app->height - cy);
	return GetCellColsAtMaskInt(app, cx, cy, cw, ch, mask);
}

static struct CellCols GetSpriteColsAtMask(Sleber* app, int x, int y, const SpriteMask mask) {
	struct CellCols cols = { 0xff, 0xff };
	if (x < 0 || x >= app->width || y < 0 || y >= app->height) { return cols; }

	int stride = app->width;
	int cx = SnapToUnit(x, 24), cy = SnapToUnit(y, 21);
	int cw = MIN(24, app->width - cx), ch = MIN(21, app->height - cy);
	uint8_t* char_pixel = app->pixels + (cy * stride + cx);
	uint8_t* type_pixel = app->pixels_info + (cy * stride + cx);
	for (int chr_y = 0; chr_y < ch; ++chr_y) {
		const uint8_t* mask_row = mask.bts + chr_y * 3;
		const int spr_mask = (mask_row[0] << 16) | (mask_row[1] << 8) | mask_row[2];
		for (int chr_x = 0; chr_x < cw; ++chr_x) {
			if (!(spr_mask & (1 << chr_x))) {
				if (type_pixel[chr_x] == GPX_PIXEL_BG && cols.bg == 0xff) {
					cols.bg = char_pixel[chr_x];
				} else if (type_pixel[chr_x] == GPX_PIXEL_FG && cols.fg == 0xff) {
					cols.fg = char_pixel[chr_x];
				}
			}
		}
		if (cols.bg != 0xff && cols.fg != 0xff) { break; }
		char_pixel += stride;
		type_pixel += stride;
	}
	return cols;
}

static void MakeCharMixedMC(Sleber* app, int x, int y) {
	const int cx = x & ~7, cy = y & ~7;
	const int ex = min(cx + 8, app->width), ey = min(cy + 8, app->height);
	const int stride = app->width;

	uint8_t* char_pixel = app->pixels + cy * stride;
	uint8_t* type_pixel = app->pixels_info + cy * stride;
	int min_x = 1 << 30, min_y = 1 << 30, max_x = 0, max_y = 0;
	for(int py=cy; py<ey; ++py) {
		for(int px=cx; px<ex; px+=2) {
			if (char_pixel[px + 1] != char_pixel[px] || type_pixel[px + 1] != type_pixel[px]) {
				char_pixel[px + 1] = char_pixel[px];
				type_pixel[px + 1] = type_pixel[px];
				if (px < min_x) { min_x = px; }
				if (px > max_x) { max_x = px; }
				if (py < min_y) { min_y = py; }
				if (py > max_y) { max_y = py; }
			}
		}
		char_pixel += stride;
		type_pixel += stride;
	}
	if (min_x <= max_x) {
		sChangeActionPos[sNumChangeActions] = ToI16Vec2(min_x+1, min_y);
		sChangeActionSize[sNumChangeActions] = ToI16Vec2(max_x-min_x+2, max_y-min_y+1);
		sChangeMin.x = MIN(sChangeMin.x, (uint16_t)(min_x + 1));
		sChangeMin.y = MIN(sChangeMin.y, (uint16_t)(min_y + 1));
		sChangeMax.x = MAX(sChangeMax.x, (uint16_t)(max_x + 2));
		sChangeMax.y = MAX(sChangeMax.y, (uint16_t)(max_y + 2));
		++sNumChangeActions;
	}
}

static uint8_t GetAreaColorOfTypeInt(Sleber *app, int cx, int cy, int cw, int ch, uint8_t type) {
	int stride = app->width;
	uint8_t* char_pixel = app->pixels + (cy * stride + cx);
	uint8_t* type_pixel = app->pixels_info + (cy * stride + cx);
	for (int chr_y = 0; chr_y < ch; ++chr_y) {
		for (int chr_x = 0; chr_x < cw; ++chr_x) {
			if (type_pixel[chr_x] == type) {
				return char_pixel[chr_x];
			}
		}
		char_pixel += stride;
		type_pixel += stride;
	}
	return 0xff;
}

static uint8_t GetCellColorOfType(Sleber *app, int x, int y, uint8_t type) {
	if (x < 0 || x >= app->width || y < 0 || y >= app->height) { return 0xff; }
	int cx = x & ~7, cy = y & ~7;
	int cw = MIN(8, app->width - cx), ch = MIN(8, app->height - cy);
	return GetAreaColorOfTypeInt(app, cx, cy, cw, ch, type);
}

static uint8_t GetSpriteColorOfType(Sleber* app, int x, int y, uint8_t type) {
	if (x < 0 || x >= app->width || y < 0 || y >= app->height) { return 0xff; }
	int cx = SnapToUnit(x, 24), cy = SnapToUnit(y, 21);
	int cw = MIN(24, app->width - cx), ch = MIN(21, app->height - cy);
	return GetAreaColorOfTypeInt(app, cx, cy, cw, ch, type);
}

static void SetPixelAtInt(uint8_t *pixels, uint8_t *pixels_info, int stride, int x, int y, uint8_t value, uint8_t type) {
	if ((pixels[y * stride + x] != value || pixels_info[y * stride + x] != type) && sNumChangeActions < MAX_CHANGE_ACTIONS) {
		sChangeActionPos[sNumChangeActions] = ToI16Vec2(x, y);
		sChangeActionSize[sNumChangeActions] = ToI16Vec2(1, 1);
		sChangeMin.x = MIN(sChangeMin.x, (uint16_t)x);
		sChangeMin.y = MIN(sChangeMin.y, (uint16_t)y);
		sChangeMax.x = MAX(sChangeMax.x, (uint16_t)(x + 1));
		sChangeMax.y = MAX(sChangeMax.y, (uint16_t)(y + 1));
		++sNumChangeActions;
	}

	pixels[y * stride + x] = value;
	pixels_info[y * stride + x] = type;
}

void HoverChar(Sleber* app, int x, int y) {
	// app->width <= 0xffff, check draw position
	if (x < 0 || x >= app->width || y < 0 || y >= app->height) { return; }

	uint8_t bg = app->shared_colors[0];
	uint8_t fg = GetCellColorOfType(app, x, y, GPX_PIXEL_FG);
	app->hover_background_color = bg;
	app->hover_foreground_color = fg == 0xff ? app->foreground_color : fg;
}

void HoverCharMC(Sleber* app, int x, int y) {
	// app->width <= 0xffff, check draw position
	if (x < 0 || x >= app->width || y < 0 || y >= app->height) { return; }

	uint8_t bg = app->shared_colors[0];
	uint8_t fg = GetCellColorOfType(app, x, y, GPX_PIXEL_FG);
	app->hover_background_color = bg;
	app->hover_foreground_color = fg == 0xff ? app->foreground_color : fg;
}

static void SetPixelAtMC(uint8_t *pixels, uint8_t *pixels_info, int stride, int x, int y, uint8_t value, uint8_t type) {
	if ((pixels[y * stride + (x & ~1)] != value || pixels_info[y * stride + (x & ~1)] != type) && sNumChangeActions < MAX_CHANGE_ACTIONS) {
		sChangeActionPos[sNumChangeActions] = ToI16Vec2(x & ~1, y);
		sChangeActionSize[sNumChangeActions] = ToI16Vec2(2, 1);
		sChangeMin.x = MIN(sChangeMin.x, (uint16_t)(x & ~1));
		sChangeMin.y = MIN(sChangeMin.y, (uint16_t)y);
		sChangeMax.x = MAX(sChangeMax.x, (uint16_t)(x + 2));
		sChangeMax.y = MAX(sChangeMax.y, (uint16_t)(y + 1));
		++sNumChangeActions;
	}

	pixels[y * stride + (x & ~1)] = value;
	pixels[y * stride + (x | 1)] = value;
	pixels_info[y * stride + (x & ~1)] = type;
	pixels_info[y * stride + (x | 1)] = type;
}

// check the rules to apply pixel to char mode
static void SetPixelAtChar(Sleber* app, int x, int y, uint8_t value) {
	// app->width <= 0xffff, check draw position
	if (x < 0 || x >= app->width || y < 0 || y >= app->height) { return; }

	if (app->pixels[x + y * app->width] == value) { return; }

	uint8_t bg = app->shared_colors[0];
	if (value == bg) {
		// record undo previous color / type
		SetPixelAtInt(app->pixels, app->pixels_info, app->width, x, y, value, GPX_PIXEL_BG);
	} else {
		// record undo either fg pixel or whole char
		SetPixelAtInt(app->pixels, app->pixels_info, app->width, x, y, value, GPX_PIXEL_FG);
		ChangeTypedColor(app, value, GPX_PIXEL_FG, x & ~7, y & ~7, 8, 8);
	}
}

static void SetPixelAtCharMC(Sleber* app, int x, int y, uint8_t value) {
	// app->width <= 0xffff, check draw position
	if (x < 0 || x >= app->width || y < 0 || y >= app->height) { return; }

	// check that it is a valid color for multicolor mode
	if (value >= 8 && value != app->shared_colors[1] && value != app->shared_colors[2]) {
		return;
	}

	if (value == app->shared_colors[0]) {
		// record undo previous color / type
		SetPixelAtMC(app->pixels, app->pixels_info, app->width, x, y, value, GPX_PIXEL_BG);
	} else if (value == app->shared_colors[1]) {
		// record undo previous color / type
		SetPixelAtMC(app->pixels, app->pixels_info, app->width, x, y, value, GPX_PIXEL_MC0);
	} else if (value == app->shared_colors[2]) {
		// record undo previous color / type
		SetPixelAtMC(app->pixels, app->pixels_info, app->width, x, y, value, GPX_PIXEL_MC1);
	} else {
		// record undo either fg pixel or whole char
		SetPixelAtMC(app->pixels, app->pixels_info, app->width, x, y, value, GPX_PIXEL_FG);
		ChangeTypedColor(app, value, GPX_PIXEL_FG, x & ~7, y & ~7, 8, 8);
	}
}

static void SetPixelAtCharMixed(Sleber* app, int x, int y, uint8_t value) {
	// app->width <= 0xffff, check draw position
	if (x < 0 || x >= app->width || y < 0 || y >= app->height) { return; }

	const struct CellColsMC c = GetCellColsMCAt(app, x, y);

	const uint8_t bg = app->shared_colors[0];
	const bool is_mc = (value == app->shared_colors[1] || value == app->shared_colors[2]);

	// hires?
	if ((value < 8 || value == bg) && (c.fg == 0xff || !is_mc) && c.m0 == 0xff && c.m1 == 0xff) {
		if (value == bg) {
			// record undo previous color / type
			SetPixelAtInt(app->pixels, app->pixels_info, app->width, x, y, value, GPX_PIXEL_BG);
		} else {
			// record undo either fg pixel or whole char
			SetPixelAtInt(app->pixels, app->pixels_info, app->width, x, y, value, GPX_PIXEL_FG);
			ChangeTypedColor(app, value, GPX_PIXEL_FG, x & ~7, y & ~7, 8, 8);
		}
	} else {
		if (value == bg) {
			// record undo previous color / type
			SetPixelAtMC(app->pixels, app->pixels_info, app->width, x, y, value, GPX_PIXEL_BG);
			MakeCharMixedMC(app, x, y);
		} else if (value == app->shared_colors[1]) {
			// record undo previous color / type
			SetPixelAtMC(app->pixels, app->pixels_info, app->width, x, y, value, GPX_PIXEL_MC0);
			MakeCharMixedMC(app, x, y);
		} else if (value == app->shared_colors[2]) {
			// record undo previous color / type
			SetPixelAtMC(app->pixels, app->pixels_info, app->width, x, y, value, GPX_PIXEL_MC1);
			MakeCharMixedMC(app, x, y);
		} else if (value < 8) {
			// record undo either fg pixel or whole char
			SetPixelAtMC(app->pixels, app->pixels_info, app->width, x, y, value, GPX_PIXEL_FG);
			ChangeTypedColor(app, value, GPX_PIXEL_FG, x & ~7, y & ~7, 8, 8);
			MakeCharMixedMC(app, x, y);
		}
	}
}

void HoverBitmap(Sleber* app, int x, int y) {
	struct CellCols cols = GetCellColsAt(app, x, y);
	app->hover_background_color = cols.bg == 0xff ? app->background_color : cols.bg;
	app->hover_foreground_color = cols.fg == 0xff ? app->foreground_color : cols.fg;
}

static void SetPixelAtBitmap(Sleber* app, int x, int y, uint8_t value) {
	// app->width <= 0xffff, check draw position
	if (x < 0 || x >= app->width || y < 0 || y >= app->height) { return; }

	struct CellCols c = GetCellColsAt(app, x, y);

	if (c.bg == value || (c.bg == 0xff && c.fg != value)) {
		SetPixelAtInt(app->pixels, app->pixels_info, app->width, x, y, value, GPX_PIXEL_BG);
		return;
	}

	if (c.fg == value || c.fg == 0xff) {
		SetPixelAtInt(app->pixels, app->pixels_info, app->width, x, y, value, GPX_PIXEL_FG);
		return;
	}
	ChangeTypedColor(app, value, app->pixels_info[x + y * app->width], x & ~7, y & ~7, 8, 8);
}

static void SetCharMaskToColor(Sleber* app, int cx, int cy, CharMask mask, uint8_t color, uint8_t type) {
	// cx, cy are already checked to be within
	uint32_t stride = app->width;
	uint8_t* pixels = app->pixels + cy * stride + cx;
	uint8_t* pixels_info = app->pixels_info + cy * stride + cx;
	for (int y = 0; y < 8; ++y) {
		uint8_t row_mask = mask.row[y];
		for (int x = 0, m=1; x < 8; ++x, m <<= 1) {
			if (row_mask & m) {
				pixels[x] = color;
				pixels_info[x] = type;
			}
		}
		pixels += stride;
		pixels_info += stride;
	}
}

static void SetSpriteMaskToColor(Sleber* app, int cx, int cy, SpriteMask mask, uint8_t color, uint8_t type, int w, int h) {
	// cx, cy are already checked to be within
	uint32_t stride = app->width;
	uint8_t* pixels = app->pixels + cy * stride + cx;
	uint8_t* pixels_info = app->pixels_info + cy * stride + cx;
	for (int y = 0; y < h; ++y) {
		const uint8_t* row_mask = &mask.bts[y * 3];
		int spr_mask = row_mask[0] | (row_mask[1] << 8) | (row_mask[2] << 16);
		for (int x = 0, m = 1; x < w; ++x, m <<= 1) {
			if (spr_mask & m) {
				pixels[x] = color;
				pixels_info[x] = type;
			}
		}
		pixels += stride;
		pixels_info += stride;
	}
}

static CharMask GetCharMaskFromBitmapMask(const uint8_t* bitfield, int stride8, int cx, int cy) {
	CharMask mask = { 0 };
	for (int y = 0; y < 8; ++y) {
		mask.row[y] = bitfield[(cy + y) * stride8 + (cx / 8)];
	}
	return mask;
}

static SpriteMask GetSpriteMaskFromBitmapMask(const uint8_t* bitfield, int stride8, int cx, int cy, int w, int h) {
	SpriteMask mask = { 0 };
	int iy = cy * stride8 + (cx / 8);
	int nx = w >> 3;
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < nx; ++x) {
			mask.bts[x + y * 3] = bitfield[iy + x];
		}
		iy += stride8;
	}
	return mask;
}

static void SetAreaToColor(Sleber* app, int x0, int x1, int y0, int y1, uint8_t color, uint8_t type) {
	// x0, x1, y0, y1 are already checked to be within
	uint32_t stride = app->width;
	uint8_t* pixels = app->pixels + y0 * app->width + x0;
	uint8_t* pixels_info = app->pixels_info + y0 * app->width + x0;
	for (int y = y0; y < y1; ++y) {
		for (int x = 0, n=x1-x0; x < n; ++x) {
			pixels[x] = color;
			pixels_info[x] = type;
		}
		pixels += stride;
		pixels_info += stride;
	}
}

static void SetBitfieldChar(Sleber* app, int x0, int x1, int y0, int y1, uint8_t color, uint8_t color_orig, const uint8_t* bitfield, uint32_t stride8) {
	// x0, x1, y0, y1 are already checked to be within
	int ey = (y1 + 7) & ~7, ex = (x1 + 7) & ~7;
	for (int cy = y0 & ~7; cy < ey; cy += 8) {
		for (int cx = x0 & ~7; cx < ex; cx += 8) {
			CharMask mask = GetCharMaskFromBitmapMask(bitfield, stride8, cx, cy);
			struct CellCols c = GetCellColsAtMask(app, cx, cy, mask);
			// empty
			if (!mask.chr) { continue; }
			else if (mask.chr == 0xffffffffffffffff) {
				SetAreaToColor(app, cx, cx + 8, cy, cy + 8, color, app->shared_colors[0] == color ? GPX_PIXEL_BG : GPX_PIXEL_FG);
			} else {				if (app->shared_colors[0] == color) {//} || (c.bg == 0xff && c.fg != color)) {
					SetCharMaskToColor(app, cx, cy, mask, color, GPX_PIXEL_BG);
				} else if (c.fg == color || c.fg == 0xff) { //} || c.fg == 0xff) {
					SetCharMaskToColor(app, cx, cy, mask, color, GPX_PIXEL_FG);
				} else if (c.fg == color_orig && color_orig != app->shared_colors[0]) {
					ChangeTypedColor(app, color, GPX_PIXEL_FG, cx & ~7, cy & ~7, 8, 8);
				}
			}
		}
	}
	CheckAndAddUndoArea(app, (Area) { (uint16_t)x0, (uint16_t)y0, (uint16_t)(x1 - x0), (uint16_t)(y1 - y0) }, true);
}

static void SetBitfieldCharMC(Sleber* app, int x0, int x1, int y0, int y1, uint8_t color, uint8_t color_orig, const uint8_t* bitfield, uint32_t stride8) {
	// x0, x1, y0, y1 are already checked to be within
	int ey = (y1 + 7) & ~7, ex = (x1 + 7) & ~7;
	for (int cy = y0 & ~7; cy < ey; cy += 8) {
		for (int cx = x0 & ~7; cx < ex; cx += 8) {
			CharMask mask = GetCharMaskFromBitmapMask(bitfield, stride8, cx, cy);
			struct CellColsMC c_mc = GetCellColsMcAtMask(app, cx, cy, mask);

			// empty ?
			if (!mask.chr) {
				continue;
			} else if (mask.chr == 0xffffffffffffffff) {
				uint8_t type = color == app->shared_colors[0] ? GPX_PIXEL_BG :
					(color == app->shared_colors[1] ? GPX_PIXEL_MC0 :
					(color == app->shared_colors[2] ? GPX_PIXEL_MC1 :
					(color == c_mc.fg || ( color < 8 && c_mc.fg == 0xff) ) ?  GPX_PIXEL_FG : 0xff));
				if (type != 0xff) {
					SetAreaToColor(app, cx, cx + 8, cy, cy + 8, color, type);
				}
			} else {
				if (color == app->shared_colors[0]) {
					SetCharMaskToColor(app, cx, cy, mask, color, GPX_PIXEL_BG);
				} else if (color == app->shared_colors[1]) {
					SetCharMaskToColor(app, cx, cy, mask, color, GPX_PIXEL_MC0);
				} else if (color == app->shared_colors[2]) {
					SetCharMaskToColor(app, cx, cy, mask, color, GPX_PIXEL_MC1);
				} else {
					if (color >= 8) {
					} else if (c_mc.fg == color || c_mc.fg == 0xff) {
						SetCharMaskToColor(app, cx, cy, mask, color, GPX_PIXEL_FG);
					} else if (c_mc.fg == color_orig) {
						ChangeTypedColor(app, color, GPX_PIXEL_FG, cx & ~7, cy & ~7, 8, 8);
					}
				}
			}
		}
	}
	CheckAndAddUndoArea(app, ToArea( x0, y0, x1 - x0, y1 - y0 ), true);
}

static void SetBitfieldCharMixed(Sleber* app, int x0, int x1, int y0, int y1, uint8_t color, uint8_t color_orig, const uint8_t* bitfield, uint32_t stride8) {
	// x0, x1, y0, y1 are already checked to be within
	int ey = (y1 + 7) & ~7, ex = (x1 + 7) & ~7;
	for (int cy = y0 & ~7; cy < ey; cy += 8) {
		for (int cx = x0 & ~7; cx < ex; cx += 8) {
			CharMask mask = GetCharMaskFromBitmapMask(bitfield, stride8, cx, cy);
			struct CellColsMC c = GetCellColsMcAtMask(app, cx, cy, mask);
			// empty ?
			if (!mask.chr) { continue; }
			else if (mask.chr == 0xffffffffffffffff) {
				uint8_t type = color == app->shared_colors[0] ? GPX_PIXEL_BG :
					(color == app->shared_colors[1] ? GPX_PIXEL_MC0 :
					(color == app->shared_colors[2] ? GPX_PIXEL_MC1 :
					(color == c.fg || (color < 8 && c.fg == 0xff)) ? GPX_PIXEL_FG : 0xff));
				if (type != 0xff) {
					SetAreaToColor(app, cx, cx + 8, cy, cy + 8, color, type);
					if(type == GPX_PIXEL_FG) { c.fg = color; }
					else if(type == GPX_PIXEL_MC0) { c.m0 = color; }
					else if (type == GPX_PIXEL_MC1) { c.m1 = color;	}
				}
			} else {
				if (color == app->shared_colors[0]) {
					SetCharMaskToColor(app, cx, cy, mask, color, GPX_PIXEL_BG);
				} else if (color == app->shared_colors[1]) {
					SetCharMaskToColor(app, cx, cy, mask, color, GPX_PIXEL_MC0);
					c.m0 = color;
				} else if (color == app->shared_colors[2]) {
					SetCharMaskToColor(app, cx, cy, mask, color, GPX_PIXEL_MC1);
					c.m1 = color;
				} else {
					if (color >= 8) {
					} else if (c.fg == color || c.fg == 0xff) {
						SetCharMaskToColor(app, cx, cy, mask, color, GPX_PIXEL_FG);
						c.fg = color;
					} else if (c.fg == color_orig) {
						ChangeTypedColor(app, color, GPX_PIXEL_FG, cx & ~7, cy & ~7, 8, 8);
						c.fg = color;
					}
				}
			}
			if ((c.m0 != 0xff || c.m1 != 0xff)) {
				uint8_t* chr_pix = app->pixels + cy * app->width + cx;
				uint8_t* chr_typ = app->pixels_info + cy * app->width + cx;
				int wx = min(cx + 8, app->width-1) - cx, hy = min(cy + 8, app->height);
				for (int py = cy; py < hy; ++py) {
					for (int px = 0; px < wx; px += 2) {
						chr_pix[px + 1] = chr_pix[px];
						chr_typ[px + 1] = chr_typ[px];
					}
					chr_pix += app->width;
					chr_typ += app->width;
				}
				MakeCharMixedMC(app, cx, cy);
			}
		}
	}
	CheckAndAddUndoArea(app, ToArea(x0, y0, x1 - x0, y1 - y0), true);
}

static void SetBitfieldBitmap(Sleber* app, int x0, int x1, int y0, int y1, uint8_t color, uint8_t color_orig, const uint8_t* bitfield, uint32_t stride8) {
	// x0, x1, y0, y1 are already checked to be within
	int ey = (y1 + 7) & ~7, ex = (x1 + 7) & ~7;
	for(int cy=y0&~7; cy<ey; cy+=8) {
		for(int cx=x0&~7; cx<ex; cx+=8) {
			CharMask mask = GetCharMaskFromBitmapMask(bitfield, stride8, cx, cy);
			// empty
			if (!mask.chr) { continue; }
			else if (mask.chr == 0xffffffffffffffff) {
				SetAreaToColor(app, cx, cx+8, cy, cy+8, color, GPX_PIXEL_FG);
			} else {
				struct CellCols c = GetCellColsAtMask(app, cx, cy, mask);
				if (c.bg == color) {//} || (c.bg == 0xff && c.fg != color)) {
					SetCharMaskToColor(app, cx, cy, mask, color, GPX_PIXEL_BG);
				} else if (c.fg == color) { //} || c.fg == 0xff) {
					SetCharMaskToColor(app, cx, cy, mask, color, GPX_PIXEL_FG);
				} else if (c.fg == 0xff) {
					SetCharMaskToColor(app, cx, cy, mask, color, GPX_PIXEL_FG);
				} else if (c.bg == color_orig) {
					ChangeTypedColor(app, color, GPX_PIXEL_BG, cx & ~7, cy & ~7, 8, 8);
				} else if (c.fg == color_orig) {
					ChangeTypedColor(app, color, GPX_PIXEL_FG, cx & ~7, cy & ~7, 8, 8);
				}
			}
		}
	}
	CheckAndAddUndoArea(app, ToArea(x0, y0, x1 - x0, y1 - y0), true);
}

static void SetBitfieldBitmapMC(Sleber* app, int x0, int x1, int y0, int y1, uint8_t color, uint8_t color_orig, const uint8_t* bitfield, uint32_t stride8) {
	// x0, x1, y0, y1 are already checked to be within
	int ey = (y1 + 7) & ~7, ex = (x1 + 7) & ~7;
	for (int cy = y0 & ~7; cy < ey; cy += 8) {
		for (int cx = x0 & ~7; cx < ex; cx += 8) {
			CharMask mask = GetCharMaskFromBitmapMask(bitfield, stride8, cx, cy);
			// empty
			if (!mask.chr) { continue; }
			else if (mask.chr == 0xffffffffffffffff) {
				SetAreaToColor(app, cx, cx + 8, cy, cy + 8, color, GPX_PIXEL_FG);
			} else {
				struct CellColsMC c = GetCellColsMCAt(app, cx, cy);
				if (c.bg == color) {
					SetCharMaskToColor(app, cx, cy, mask, color, GPX_PIXEL_BG);
				} else if (c.fg == color) {
					SetCharMaskToColor(app, cx, cy, mask, color, GPX_PIXEL_FG);
				} else if (c.m0 == color) {
					SetCharMaskToColor(app, cx, cy, mask, color, GPX_PIXEL_MC0);
				} else if (c.m1 == color) {
					SetCharMaskToColor(app, cx, cy, mask, color, GPX_PIXEL_MC1);
				} else if (c.fg == 0xff || c.m0 == 0xff || c.m1 == 0xff) {
					SetCharMaskToColor(app, cx, cy, mask, color, (c.fg==0xff ? GPX_PIXEL_FG : (c.m0 == 0xff ? GPX_PIXEL_MC0 : GPX_PIXEL_MC1)));
				} else if (c.fg == color_orig) {
					ChangeTypedColor(app, color, GPX_PIXEL_FG, cx & ~7, cy & ~7, 8, 8);
				} else if (c.m0 == color_orig) {
					ChangeTypedColor(app, color, GPX_PIXEL_MC0, cx & ~7, cy & ~7, 8, 8);
				} else if (c.m1 == color_orig) {
					ChangeTypedColor(app, color, GPX_PIXEL_MC1, cx & ~7, cy & ~7, 8, 8);
				}
			}
		}
	}
	CheckAndAddUndoArea(app, ToArea( x0, y0, x1 - x0, y1 - y0 ), true);
}

static void SetBitfieldECBM(Sleber* app, int x0, int x1, int y0, int y1, uint8_t color, uint8_t color_orig, const uint8_t* bitfield, uint32_t stride8) {
	// x0, x1, y0, y1 are already checked to be within
	int ey = (y1 + 7) & ~7, ex = (x1 + 7) & ~7;
	bool is_bg = (color == app->shared_colors[0] || color == app->shared_colors[1] || color == app->shared_colors[2] || color == app->shared_colors[3]);
	for (int cy = y0 & ~7; cy < ey; cy += 8) {
		for (int cx = x0 & ~7; cx < ex; cx += 8) {
			CharMask mask = GetCharMaskFromBitmapMask(bitfield, stride8, cx, cy);
			// empty
			if (!mask.chr) { continue; } else if (mask.chr == 0xffffffffffffffff) {
				SetAreaToColor(app, cx, cx + 8, cy, cy + 8, color, is_bg ? GPX_PIXEL_BG : GPX_PIXEL_FG);
			} else {
				struct CellCols c = GetCellColsAt(app, cx, cy);
				if (c.bg == color) {//} || (c.bg == 0xff && c.fg != color)) {
					SetCharMaskToColor(app, cx, cy, mask, color, GPX_PIXEL_BG);
				} else if (c.fg == color) { //} || c.fg == 0xff) {
					SetCharMaskToColor(app, cx, cy, mask, color, GPX_PIXEL_FG);
				} else if (c.fg == 0xff) {
					ChangeTypedColor(app, color, GPX_PIXEL_FG, cx & ~7, cy & ~7, 8, 8);
				} else if (c.bg == color_orig) {
					ChangeTypedColor(app, color, GPX_PIXEL_BG, cx & ~7, cy & ~7, 8, 8);
				} else if (c.fg == color_orig) {
					ChangeTypedColor(app, color, GPX_PIXEL_FG, cx & ~7, cy & ~7, 8, 8);
				}
			}
		}
	}
	CheckAndAddUndoArea(app, ToArea( x0, y0, x1 - x0, y1 - y0 ), true);
}

static void SetBitfieldSprite(Sleber* app, int x0, int x1, int y0, int y1, uint8_t color, uint8_t color_orig, const uint8_t* bitfield, uint32_t stride8) {
	// x0, x1, y0, y1 are already checked to be within
	int ey = ((y1 + 20) / 21) * 21, ex = ((x1 + 23) / 24) * 24;
	int sy = (y0 / 21) * 21, sx = (x0 / 24) * 24;
	for (int cy = sy; cy < ey; cy += 21) {
		for (int cx = sx; cx < ex; cx += 24) {
			SpriteMask mask = GetSpriteMaskFromBitmapMask(bitfield, stride8, cx, cy, min(x1-cx, 24), min(y1-cy, 21));
			bool all_empty = true, all_set = true;
			for(int i=0; i<8; ++i) {
				if (mask.spr[i]) { all_empty = false; }
				if (~mask.spr[i]) { all_set = false; }
			}
			// empty?
			if (all_empty) { continue; }
			else if (all_set) {
				SetAreaToColor(app, cx, cx + 24, cy, cy + 21, color, GPX_PIXEL_BG);
			} else {
				struct CellCols c = GetSpriteColsAtMask(app, cx, cy, mask);
				if (app->shared_colors[0] == color) {//} || (c.bg == 0xff && c.fg != color)) {
					SetSpriteMaskToColor(app, cx, cy, mask, color, GPX_PIXEL_BG, min(x1-cx, 24), min(y1-cy, 21));
				} else if (c.fg == color || c.fg == 0xff) {
					SetSpriteMaskToColor(app, cx, cy, mask, color, GPX_PIXEL_FG, min(x1-cx, 24), min(y1-cy, 21));
				} else if (c.fg == color_orig) {
					ChangeTypedColor(app, color, GPX_PIXEL_FG, 
						SnapToUnit(cx, 24), SnapToUnit(cy, 21), 24, 21);
				}
			}
		}
	}
	CheckAndAddUndoArea(app, ToArea(x0, y0, x1 - x0, y1 - y0), true);
}

void HoverBitmapMC(Sleber* app, int x, int y) {
	struct CellColsMC cols = GetCellColsMCAt(app, x, y);
	app->hover_background_color = cols.bg == 0xff ? app->background_color : cols.bg;
	app->hover_foreground_color = cols.fg == 0xff ? app->foreground_color : cols.fg;
	app->hover_multicolor0 = cols.m0 == 0xff ? app->shared_colors[1] : cols.m0;
	app->hover_multicolor1 = cols.m1 == 0xff ? app->shared_colors[2] : cols.m1;
}

static void SetPixelAtBitmapMC(Sleber* app, int x, int y, uint8_t value) {
	// app->width <= 0xffff, check draw position
	if (x < 0 || x >= app->width || y < 0 || y >= app->height) { return; }
	struct CellColsMC c = GetCellColsMCAt(app, x, y);
	if (c.bg == value || (c.bg == 0xff && c.fg != value && c.m0 != value && c.m1 != value)) {
		SetPixelAtMC(app->pixels, app->pixels_info, app->width, x, y, value, GPX_PIXEL_BG);
	} else if (c.fg == value || (c.fg == 0xff && c.m0 != value && c.m1 != value)) {
		SetPixelAtMC(app->pixels, app->pixels_info, app->width, x, y, value, GPX_PIXEL_FG);
	} else if (c.m0 == value || (c.m0 == 0xff && c.m1 != value)) {
		SetPixelAtMC(app->pixels, app->pixels_info, app->width, x, y, value, GPX_PIXEL_MC0);
	} else if (c.m1 == value || c.m1 == 0xff) {
		SetPixelAtMC(app->pixels, app->pixels_info, app->width, x, y, value, GPX_PIXEL_MC1);
	} else if(app->pixels_info[y * app->width + x] == GPX_PIXEL_FG) {
		ChangeTypedColor(app, value, app->pixels_info[x + y * app->width], x & ~7, y & ~7, 8, 8);
	}
}

void HoverSprite(Sleber* app, int x, int y) {
	// app->width <= 0xffff, check draw position
	if (x < 0 || x >= app->width || y < 0 || y >= app->height) { return; }

	uint8_t bg = app->shared_colors[0];
	uint8_t fg = GetSpriteColorOfType(app, x, y, GPX_PIXEL_FG);
	app->hover_background_color = bg;
	app->hover_foreground_color = fg == 0xff ? app->foreground_color : fg;
}

static void SetPixelAtSprite(Sleber* app, int x, int y, uint8_t value) {
	// app->width <= 0xffff, check draw position
	if (x < 0 || x >= app->width || y < 0 || y >= app->height) { return; }

	uint8_t bg = app->shared_colors[0];
	if (value == bg) {
		SetPixelAtInt(app->pixels, app->pixels_info, app->width, x, y, value, GPX_PIXEL_BG);
	} else {
		SetPixelAtInt(app->pixels, app->pixels_info, app->width, x, y, value, GPX_PIXEL_FG);
		ChangeTypedColor(app, value, GPX_PIXEL_FG, SnapToUnit(x, 24), SnapToUnit(y, 21), 24, 21);
	}
}

void HoverSpriteMC(Sleber* app, int x, int y) {
	struct CellColsMC cols = GetSpriteColsMCAt(app, x, y);
	app->hover_background_color = cols.bg == 0xff ? app->background_color : cols.bg;
	app->hover_foreground_color = cols.fg == 0xff ? app->foreground_color : cols.fg;
	app->hover_multicolor0 = app->shared_colors[1];
	app->hover_multicolor1 = app->shared_colors[2];
}

static void SetPixelAtSpriteMC(Sleber* app, int x, int y, uint8_t value) {
	// app->width <= 0xffff, check draw position
	if (x < 0 || x >= app->width || y < 0 || y >= app->height) { return; }
	struct CellColsMC c = GetSpriteColsMCAt(app, x, y);
	c.m0 = app->shared_colors[1]; c.m1 = app->shared_colors[2];
	if (c.bg == value || (c.bg == 0xff && c.fg != value && c.m0 != value && c.m1 != value)) {
		SetPixelAtMC(app->pixels, app->pixels_info, app->width, x, y, value, GPX_PIXEL_BG);
	} else if (c.fg == value || (c.fg == 0xff && c.m0 != value && c.m1 != value)) {
		SetPixelAtMC(app->pixels, app->pixels_info, app->width, x, y, value, GPX_PIXEL_FG);
	} else if (c.m0 == value || (c.m0 == 0xff && c.m1 != value)) {
		SetPixelAtMC(app->pixels, app->pixels_info, app->width, x, y, value, GPX_PIXEL_MC0);
	} else if (c.m1 == value || c.m1 == 0xff) {
		SetPixelAtMC(app->pixels, app->pixels_info, app->width, x, y, value, GPX_PIXEL_MC1);
	} else {
		ChangeTypedColor(app, value, app->pixels_info[x + y * app->width],
			x - (x%24), y - (y%21), 24, 21);
	}
}

void HoverECBM(Sleber* app, int x, int y) {
	struct CellCols cols = GetCellColsAt(app, x, y);
	app->hover_background_color = cols.bg == 0xff ? app->background_color : cols.bg;
	app->hover_foreground_color = cols.fg == 0xff ? app->foreground_color : cols.fg;
}

static bool IsSharedColor(Sleber* app, uint8_t value) {
	return (value == app->shared_colors[0] || value == app->shared_colors[1] || value == app->shared_colors[2] || value == app->shared_colors[3]);
}

// check the rules to apply pixel to char mode
static void SetPixelAtECBM(Sleber* app, int x, int y, uint8_t value) {
	// app->width <= 0xffff, check draw position
	if (x < 0 || x >= app->width || y < 0 || y >= app->height) { return; }

	// same pixel value, no change
	if (value == app->pixels[y * app->width + x]) { return; }

	struct CellCols c = GetCellColsAt(app, x, y);
	uint8_t target_type = app->pixels_info[y * app->width + x];
	bool is_shared_color = IsSharedColor(app, value);
	if (c.fg == 0xff) {
		SetPixelAtInt(app->pixels, app->pixels_info, app->width, x, y, value, GPX_PIXEL_FG);
	} else if (value == c.fg || value == c.bg) {
		// already using this color -> safe to just assign
		SetPixelAtInt(app->pixels, app->pixels_info, app->width, x, y, value, value == c.bg ? GPX_PIXEL_BG : GPX_PIXEL_FG);
	} else if (
		(target_type == GPX_PIXEL_BG && !is_shared_color && IsSharedColor(app, c.fg)) ||
		(target_type == GPX_PIXEL_FG && is_shared_color && !IsSharedColor(app, c.bg)) ) {
		// swap fg/bg and assign pixel to make this combo work
		int cx = x & ~7, cy = y & ~7, cw = MIN(8, app->width - cx), ch = MIN(8, app->height - cy);
		uint8_t* info = app->pixels_info + cy * app->width + cx;
		for(int j = 0; j < ch; ++j) {
			for (int i = 0; i < cw; ++i) {
				info[i] = info[i] == GPX_PIXEL_BG ? GPX_PIXEL_FG : GPX_PIXEL_BG;
			}
			info += app->width;
		}
		SetPixelAtInt(app->pixels, app->pixels_info, app->width, x, y, value, GPX_PIXEL_FG);
		ChangeTypedColor(app, value, GPX_PIXEL_FG, x & ~7, y & ~7, 8, 8);
	} else if (target_type == GPX_PIXEL_FG || c.fg == 0xff) {
		SetPixelAtInt(app->pixels, app->pixels_info, app->width, x, y, value, GPX_PIXEL_FG);
		ChangeTypedColor(app, value, GPX_PIXEL_FG, x & ~7, y & ~7, 8, 8);
	} else if (is_shared_color && target_type == GPX_PIXEL_BG) {
		SetPixelAtInt(app->pixels, app->pixels_info, app->width, x, y, value, GPX_PIXEL_BG);
		ChangeTypedColor(app, value, GPX_PIXEL_BG, x & ~7, y & ~7, 8, 8);
	}
}


static void SetPixelAt(Sleber* app, int x, int y, uint8_t value) {
	if (x < 0 || y < 0 || x >= app->width || y >= app->height) {
		return;
	}
	switch (app->screen_mode) {
		case SCREEN_MODE_CHAR:
			SetPixelAtChar(app, x, y, value);
			break;
		case SCREEN_MODE_CHAR_MULTICOLOR:
			SetPixelAtCharMC(app, x, y, value);
			break;
		case SCREEN_MODE_CHAR_MIXED:
			SetPixelAtCharMixed(app, x, y, value);
			break;
		case SCREEN_MODE_BITMAP:
			SetPixelAtBitmap(app, x, y, value);
			break;
		case SCREEN_MODE_BITMAP_MULTICOLOR:
			SetPixelAtBitmapMC(app, x, y, value);
			break;
		case SCREEN_MODE_SPRITE:
			SetPixelAtSprite(app, x, y, value);
			break;
		case SCREEN_MODE_SPRITE_MULTICOLOR:
			SetPixelAtSpriteMC(app, x, y, value);
			break;
		case SCREEN_MODE_ECBM:
			SetPixelAtECBM(app, x, y, value);
			break;
		case SCREEN_MODE_UNRESTRICTED:
			SetPixelAtInt(app->pixels, app->pixels_info, app->width, x, y, value, GPX_PIXEL_FG);
			break;
		case SCREEN_MODE_WIDE_UNRESTRICTED:
			SetPixelAtMC(app->pixels, app->pixels_info, app->width, x, y, value, GPX_PIXEL_FG);
			break;

	}
}

static void PaintAtLocked(Sleber* app, int x, int y, int use_background) {
	SetPixelAt(app, x, y, use_background ? app->background_color : app->foreground_color);
	if (sChangeMin.x < sChangeMax.x && sChangeMin.y < sChangeMax.y) {
		RequestRedrawArea(app, (Area) { sChangeMin.x, sChangeMin.y, sChangeMax.x - sChangeMin.x, sChangeMax.y - sChangeMin.y });
	}
#ifdef DEBUG
	ValidateUndoCommands(app, false);
#endif
	UpdateChangeActions(app);
#ifdef DEBUG
	ValidateUndoCommands(app, false);
#endif
}

void PaintAt(Sleber* app, int x, int y, int use_background) {
	LockMutex(&app->canvas_mutex);
	PaintAtLocked(app, x, y, use_background);
	UnlockMutex(&app->canvas_mutex);
}

static void PaintLineLocked(Sleber* app, int x0, int y0, int x1, int y1, int use_background) {
#ifdef DEBUG
	ValidateUndoCommands(app, false);
#endif
	uint8_t value = use_background ? app->background_color : app->foreground_color;
	int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
	int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
	int err = dx + dy;
	int x = x0, y = y0;
	for (;;) {
		SetPixelAt(app, x, y, value);
		if (x == x1 && y == y1) {
			break;
		}
		int e2 = 2 * err;
		if (e2 >= dy) { err += dy; x += sx; }
		if (e2 <= dx) { err += dx; y += sy; }
	}
//	int min_x = x0 < x1 ? x0 : x1, max_x = x0 > x1 ? x0 : x1;
//	int min_y = y0 < y1 ? y0 : y1, max_y = y0 > y1 ? y0 : y1;
	if (sChangeMin.x < sChangeMax.x && sChangeMin.y < sChangeMax.y) {
		RequestRedrawArea(app, (Area) { sChangeMin.x, sChangeMin.y, sChangeMax.x - sChangeMin.x, sChangeMax.y - sChangeMin.y });
	}
	UpdateChangeActions(app);
#ifdef DEBUG
	ValidateUndoCommands(app, false);
#endif
}

void PaintLine(Sleber* app, int x0, int y0, int x1, int y1, int use_background) {
	LockMutex(&app->canvas_mutex);
	PaintLineLocked(app, x0, y0, x1, y1, use_background);
	UnlockMutex(&app->canvas_mutex);
}

static void SetBitfield(Sleber* app, uint32_t x0, uint32_t x1, uint32_t y0, uint32_t y1, uint8_t color, uint8_t src_color, uint8_t* fill, uint32_t stride8) {
#ifdef DEBUG
	ValidateUndoCommands(app, false);
#endif
	// rectangle = (x0, y0) - (x1, y1)
	// fill is bitmask to replace with new color in image
	switch (app->screen_mode) {
		case SCREEN_MODE_CHAR:
			SetBitfieldChar(app, x0, x1, y0, y1, color, src_color, fill, stride8);
			break;
		case SCREEN_MODE_CHAR_MULTICOLOR:
			SetBitfieldCharMC(app, x0, x1, y0, y1, color, src_color, fill, stride8);
			break;
		case SCREEN_MODE_BITMAP:
			SetBitfieldBitmap(app, x0, x1, y0, y1, color, src_color, fill, stride8);
			break;
		case SCREEN_MODE_BITMAP_MULTICOLOR:
			SetBitfieldBitmapMC(app, x0, x1, y0, y1, color, src_color, fill, stride8);
			break;
		case SCREEN_MODE_SPRITE:
			SetBitfieldSprite(app, x0, x1, y0, y1, color, src_color, fill, stride8);
			break;
		case SCREEN_MODE_SPRITE_MULTICOLOR:
			break;
		case SCREEN_MODE_ECBM:
			SetBitfieldECBM(app, x0, x1, y0, y1, color, src_color, fill, stride8);
			break;
		case SCREEN_MODE_CHAR_MIXED:
			SetBitfieldCharMixed(app, x0, x1, y0, y1, color, src_color, fill, stride8);
			break;
		case SCREEN_MODE_UNRESTRICTED:
			for (uint32_t y = y0; y < y1; ++y) {
				for (uint32_t x = x0; x < x1; ++x) {
					if (fill[y * stride8 + (x >> 3)] & (1 << (x & 7))) {
						SetPixelAtInt(app->pixels, app->pixels_info, app->width,
							(int)x, (int)y, color, GPX_PIXEL_FG);
					}
				}
			}
			CheckAndAddUndoArea(app, ToArea(x0, y0, x1 - x0, y1 - y0), true);
			break;
		case SCREEN_MODE_WIDE_UNRESTRICTED:
			for (uint32_t y = y0; y < y1; ++y) {
				for (uint32_t x = x0; x < x1; ++x) {
					if (fill[y * stride8 + (x >> 3)] & (1 << (x & 7))) {
						SetPixelAtMC(app->pixels, app->pixels_info, app->width,
							(int)x, (int)y, color, GPX_PIXEL_FG);
					}
				}
			}
			CheckAndAddUndoArea(app, ToArea(x0, y0, x1 - x0, y1 - y0), true);
			break;

		default:
			break;
	}
#ifdef DEBUG
	ValidateUndoCommands(app, false);
#endif
}

typedef struct {
	uint16_t x, y;
} Point;

static void FillLocked(Sleber* app, int x_pos, int y_pos, uint8_t color) {
	// early out if filling outside the screen
	if (x_pos < 0 || x_pos >= app->width || y_pos < 0 || y_pos >= app->height) {
		return;
	}
	uint32_t stride = app->width;
	uint32_t stride8 = (stride + 7) >> 3;
	uint32_t stack_size = 256, sp = 0;
	uint8_t src_color = app->pixels[y_pos * stride + x_pos];
	if (src_color == color) { return; }

	// Perform flood fill into a bitmask, then apply that to each screen mode

	Point* stack = (Point*)malloc(sizeof(Point) * stack_size);
	if (!stack) { return; }
	uint8_t* fill = (uint8_t*)calloc(1, (size_t)stride8 * app->height);
	if (!fill) { free(stack); return; }

	uint32_t x0 = UINT32_MAX, x1 = 0, y0 = UINT32_MAX, y1 = 0;
	uint8_t* pixels = app->pixels;
	stack[sp++] = (Point){ (uint16_t)x_pos, (uint16_t)y_pos };
	while(sp) {
		Point p = stack[--sp];
		uint16_t x = p.x, y = p.y;
		if (fill[y * stride8 + (x >> 3)] & (1 << (x & 7))) { continue; }
		if (y < y0) { y0 = y; }
		if (y >= y1) { y1 = y + 1; }

		uint8_t above_color = 0xff, below_color = 0xff;
		uint8_t* row = pixels + y * stride;
		const uint8_t* row_above = y ? pixels + (y - 1) * stride : NULL;
		const uint8_t* row_below = (y < app->height - 1) ? pixels + (y + 1) * stride : NULL;
		// rewind to the leftmost pixel of the same color
		while (x && row[x - 1] == src_color) { x--; }
		if (x < x0) { x0 = x; }
		// fill to the right and check if color above or below changes to src_color
		while ( x < app->width && row[x] == src_color) {
			fill[y * stride8 + (x>>3)] |= (1 << (x&7));
			if(row_above) {
				if (above_color != src_color && row_above[x] == src_color &&
					!(fill[(y-1)*stride8+(x>>3)] & (1<<(x&7)))) {
					stack[sp++] = (Point){ x, y - 1 };
				}
				above_color = row_above[x];
			}
			if(row_below) {
				if (below_color != src_color && row_below[x] == src_color &&
					!(fill[(y + 1) * stride8 + (x >> 3)] & (1 << (x & 7)))) {
					stack[sp++] = (Point){ x, y + 1 };
				}
				below_color = row_below[x];
			}
			if (sp == stack_size) {
				stack_size *= 2;
				Point* new_stack = (Point*)realloc(stack, sizeof(Point) * stack_size);
				if (!new_stack) { free(stack); free(fill);  return; }
				stack = new_stack;
			}
			x++;
		}
		if (x > x1) { x1 = x; }
	}

	SetBitfield(app, x0, x1, y0, y1, color, src_color, fill, stride8);

	free(stack);
	free(fill);
}

void Fill(Sleber* app, int x_pos, int y_pos, uint8_t color) {
	LockMutex(&app->canvas_mutex);
	FillLocked(app, x_pos, y_pos, color);
	UnlockMutex(&app->canvas_mutex);
}

static void FillSelectionLocked(Sleber* app, uint8_t color) {
	if (app->selection_mode == SELECT_MODE_RECTANGLE) {
		uint32_t stride8 = (app->width + 7) >> 3;
		uint8_t* fill = (uint8_t*)calloc(1, (size_t)stride8 * app->height);
		if (!fill) { return; }
		int x = app->select_x, y = app->select_y;
		int w = app->select_w, h = app->select_h;
		if (w < 0) { x += w; w = -w; }
		if (h < 0) { y += h; h = -h; }
		if (x < 0) { w += x; x = 0; }
		if (y < 0) { h += y; y = 0; }
		if (x + w > app->width) { w = app->width - x; }
		if (y + h > app->height) { h = app->height - y; }

		// x&7 = 0 -> ff, 1 -> fe, 2 -> fc
		// x&7 = 0 -> 0, 1-> 01 2-> 
		uint8_t lm = (0xff << (x & 7)) & 0xff;
		uint8_t rm = (0xff >> (~(x+w-1)&7));
		for (int l = y; l < y + h; ++l) {
			uint8_t* row = fill + l * stride8;
			for (int c = x / 8, n = (x + w + 7) / 8; c < n; ++c) {
				row[c] = 0xff;
			}
			row[x / 8] &= lm;
			row[(x + w + 7) / 8 - 1] &= rm;
		}
		fill[x / 8] &= lm;
		fill[(x + w + 7) / 8 - 1] &= rm;
		SetBitfield(app, x, x + w, y, y + h, color, 0x80, fill, stride8);
		free(fill);
		app->selection_mode = SELECT_MODE_NONE;
	}
}

void FillSelection(Sleber* app, uint8_t color) {
	LockMutex(&app->canvas_mutex);
	FillSelectionLocked(app, color);
	UnlockMutex(&app->canvas_mutex);
}

static void PasteBrushLocked(Sleber* app, I32Point in_point) {
	if (!app->brush32) { return; }

	const I32Point point = SnapToGrid(app, in_point);
	const Area PasteBox = ClampArea(
		SnapAreaToUnit(app->screen_mode, 
		ToArea(max(point.x,0), max(point.y,0), max(point.x + app->brush_w,0), max(point.y + app->brush_h,0))),
		ToArea(0, 0, app->width, app->height));
	if (PasteBox.w == 0 || PasteBox.h == 0) { return; }

	uint32_t* copy = (uint32_t*)malloc((size_t)PasteBox.w * (size_t)PasteBox.h * sizeof(uint32_t));
	if (copy) {
		const uint32_t* pal32 = GetC64PaletteARGB();
		const uint8_t* src = app->pixels + PasteBox.y * app->width + PasteBox.x;
		uint32_t* dst = copy;
		for (uint16_t y = 0; y < PasteBox.h; ++y) {
			for (uint16_t x = 0; x < PasteBox.w; ++x) {
				*dst++ = pal32[src[x]];
			}
			src += app->width;
		}
	} else { return; }

	{	// Note: point.x/y >= PasteBox.x/y, so the following is safe
		uint32_t* dst = copy + (max(point.y, 0) - (int32_t)PasteBox.y) * PasteBox.w +
			(max(point.x, 0) - (int32_t)PasteBox.x);

		const uint32_t skip_x = max(0, -point.x), skip_y = max(0, -point.y);
		const uint32_t clip_x = max(0, point.x + app->brush_w - app->width );
		const uint32_t clip_y = max(0, point.y + app->brush_h - app->height );
		const uint32_t* brush = app->brush32 + skip_y * app->brush_w + skip_x;
		const uint32_t bw = app->brush_w - skip_x - clip_x, bh = app->brush_h - skip_y - clip_y;
		const uint8_t hide = app->paste_mode == PASTE_MODE_SKIP_BG_COLOR ? app->background_color : 0xff;
		for (uint32_t y = 0; y < bh; ++y) {
			for (uint32_t x = 0; x < bw; ++x) {
				const uint32_t value_abgr = brush[x];
				const uint32_t value = (value_abgr & 0xff00ff00) | ((value_abgr & 0xff) << 16) | ((value_abgr >> 16) & 0xff);
				if (value >= 0x80u << 24 && (hide == 0xff || GetClosestPaletteColor(
					(uint8_t)(value >> 16), (uint8_t)(value >> 8), (uint8_t)value) != hide)) {
					dst[x] = value;
				}
			}
			dst += PasteBox.w;
			brush += app->brush_w;
		}
	}

	SleberImage img = ConvertArea(app, (uint8_t*)copy, PasteBox.w, PasteBox.h, 4);
	free(copy);

	if (!img.pixels) { return; }

	DEBUG_ASSERT(img.width == PasteBox.w && img.height == PasteBox.h);
	{	// Copy back to the canvas
		const uint8_t* src_p = (const uint8_t*)img.pixels;
		const uint8_t* src_t = (const uint8_t*)img.types;
		uint8_t* dst_p = app->pixels + PasteBox.y * app->width + PasteBox.x;
		uint8_t* dst_t = app->pixels_info + PasteBox.y * app->width + PasteBox.x;
		for (uint16_t y = 0; y < PasteBox.h; ++y) {
			for (uint16_t x = 0; x < PasteBox.w; ++x) {
				dst_p[x] = *src_p++;
				dst_t[x] = *src_t++;
			}
			dst_p += app->width;
			dst_t += app->width;
		}
	}

	CheckAndAddUndoArea(app, PasteBox, true);

	free(img.pixels);
	free(img.types);

	app->paste_mode = PASTE_MODE_PASTED;
}

void PasteBrush(Sleber* app, I32Point in_point) {
	LockMutex(&app->canvas_mutex);
	PasteBrushLocked(app, in_point);
	UnlockMutex(&app->canvas_mutex);
}

void UndoSharedColor(Sleber* app, uint8_t color) {
	uint8_t* undo_buffer = ReserveUndoCommandBuffer(app, 4, false);
	if (undo_buffer) {
		*undo_buffer++ = UNDO_STOP;
		*undo_buffer++ = app->shared_colors[color];
		*undo_buffer++ = color;
		*undo_buffer = UNDO_SHARED_COLOR;
	}
}

static void ChangeColorLocked(Sleber* app, uint8_t target, uint8_t new_color) {
	if (target == COLOR_TYPE_BORDER) {

		uint8_t* undo_buffer = ReserveUndoCommandBuffer(app, 3, false);
		if (undo_buffer) {
			*undo_buffer++ = UNDO_STOP;
			*undo_buffer++ = app->border_color;
			*undo_buffer = UNDO_BORDER_COLOR; 
		}

		app->border_color = new_color;
		RequestRedraw(app);
		RequestRedrawPalette(app);
		return;
	}
	switch(app->screen_mode) {
		case SCREEN_MODE_CHAR:
			if (target == COLOR_TYPE_BACKGROUND) {
				UndoSharedColor(app, 0);
				app->shared_colors[0] = new_color;
				ChangeTypedColor(app, new_color, GPX_PIXEL_BG, 0, 0, app->width, app->height);
			} else if (target == COLOR_TYPE_FOREGROUND) {
				ChangeTypedColor(app, new_color, GPX_PIXEL_FG, app->hover_x & ~7, app->hover_y & ~7, 8, 8);
			}
			CheckAndAddUndoArea(app, ToArea(0, 0, app->width, app->height), true);
			RequestRedraw(app);
			break;
		case SCREEN_MODE_CHAR_MULTICOLOR:
			if (target == COLOR_TYPE_BACKGROUND) {
				UndoSharedColor(app, 0);
				app->shared_colors[0] = new_color;
				ChangeTypedColor(app, new_color, GPX_PIXEL_BG, 0, 0, app->width, app->height);
			} else if (target == COLOR_TYPE_FOREGROUND) {
				ChangeTypedColor(app, new_color, GPX_PIXEL_FG, app->hover_x & ~7, app->hover_y & ~7, 8, 8);
			} else if (target == COLOR_TYPE_MULTICOLOR) {
				UndoSharedColor(app, 1);
				app->shared_colors[1] = new_color;
				ChangeTypedColor(app, new_color, GPX_PIXEL_MC0, 0, 0, app->width, app->height);
			} else if (target == COLOR_TYPE_MULTICOLOR1) {
				UndoSharedColor(app, 2);
				app->shared_colors[2] = new_color;
				ChangeTypedColor(app, new_color, GPX_PIXEL_MC1, 0, 0, app->width, app->height);
			}
			CheckAndAddUndoArea(app, ToArea(0, 0, app->width, app->height), true);
			RequestRedraw(app);
			break;
		case SCREEN_MODE_BITMAP:
			if (target == COLOR_TYPE_BACKGROUND) {
				ChangeTypedColor(app, new_color, GPX_PIXEL_BG, app->hover_x & ~7, app->hover_y & ~7, 8, 8);
			} else if (target == COLOR_TYPE_FOREGROUND) {
				ChangeTypedColor(app, new_color, GPX_PIXEL_FG, app->hover_x & ~7, app->hover_y & ~7, 8, 8);
			}
			CheckAndAddUndoArea(app, ToArea(0, 0, app->width, app->height), true);
			RequestRedraw(app);
			break;
		case SCREEN_MODE_CHAR_MIXED:
		case SCREEN_MODE_BITMAP_MULTICOLOR:
			if (target == COLOR_TYPE_BACKGROUND) {
				UndoSharedColor(app, 0);
				app->shared_colors[0] = new_color;
				ChangeTypedColor(app, new_color, GPX_PIXEL_BG, 0, 0, app->width, app->height);
			} else if (target == COLOR_TYPE_FOREGROUND) {
				ChangeTypedColor(app, new_color, GPX_PIXEL_FG, app->hover_x & ~7, app->hover_y & ~7, 8, 8);
			} else if (target == COLOR_TYPE_MULTICOLOR) {
				UndoSharedColor(app, 1);
				app->shared_colors[1] = new_color;
				ChangeTypedColor(app, new_color, GPX_PIXEL_MC0, app->hover_x & ~7, app->hover_y & ~7, 8, 8);
			} else if (target == COLOR_TYPE_MULTICOLOR1) {
				UndoSharedColor(app, 2);
				app->shared_colors[2] = new_color;
				ChangeTypedColor(app, new_color, GPX_PIXEL_MC1, app->hover_x & ~7, app->hover_y & ~7, 8, 8);
			}
			CheckAndAddUndoArea(app, ToArea(0, 0, app->width, app->height), true);
			RequestRedraw(app);
			break;
		case SCREEN_MODE_SPRITE:
			if (target == COLOR_TYPE_BACKGROUND) {
				UndoSharedColor(app, 0);
				app->shared_colors[0] = new_color;
				ChangeTypedColor(app, new_color, GPX_PIXEL_BG, 0, 0, app->width, app->height);
			} else if (target == COLOR_TYPE_FOREGROUND) {
				ChangeTypedColor(app, new_color, GPX_PIXEL_FG,
					SnapToUnit(app->hover_x, 24),
					SnapToUnit(app->hover_y, 21), 24, 21);
			}
			CheckAndAddUndoArea(app, ToArea(0, 0, app->width, app->height), true);
			RequestRedraw(app);
			break;
		case SCREEN_MODE_SPRITE_MULTICOLOR:
			if (target == COLOR_TYPE_BACKGROUND) {
				UndoSharedColor(app, 0);
				app->shared_colors[0] = new_color;
				ChangeTypedColor(app, new_color, GPX_PIXEL_BG, 0, 0, app->width, app->height);
			} else if (target == COLOR_TYPE_FOREGROUND) {
				ChangeTypedColor(app, new_color, GPX_PIXEL_FG, app->hover_x - (app->hover_x%24), app->hover_y - (app->hover_y%21), 24, 21);
			} else if (target == COLOR_TYPE_MULTICOLOR) {
				UndoSharedColor(app, 1);
				app->shared_colors[1] = new_color;
				ChangeTypedColor(app, new_color, GPX_PIXEL_MC0, 0, 0, app->width, app->height);
			} else if (target == COLOR_TYPE_MULTICOLOR1) {
				UndoSharedColor(app, 2);
				app->shared_colors[2] = new_color;
				ChangeTypedColor(app, new_color, GPX_PIXEL_MC1, 0, 0, app->width, app->height);
			}
			CheckAndAddUndoArea(app, ToArea(0, 0, app->width, app->height), true);
			RequestRedraw(app);
			break;
		case SCREEN_MODE_ECBM:
			if (target == COLOR_TYPE_BACKGROUND) {
				ChangeTypedColorMatch(app, new_color, app->shared_colors[0], GPX_PIXEL_BG,
					0, 0, app->width, app->height);
				UndoSharedColor(app, 0);
				app->shared_colors[0] = new_color;
			} else if (target == COLOR_TYPE_FOREGROUND) {
				ChangeTypedColor(app, new_color, GPX_PIXEL_FG, app->hover_x & ~7, app->hover_y & ~7, 8, 8);
			} else if (target == COLOR_TYPE_BACKGROUND1) {
				ChangeTypedColorMatch(app, new_color, app->shared_colors[1], GPX_PIXEL_BG,
					0, 0, app->width, app->height);
				UndoSharedColor(app, 1);
				app->shared_colors[1] = new_color;
			} else if (target == COLOR_TYPE_BACKGROUND2) {
				ChangeTypedColorMatch(app, new_color, app->shared_colors[2], GPX_PIXEL_BG,
					0, 0, app->width, app->height);
				UndoSharedColor(app, 2);
				app->shared_colors[2] = new_color;
			} else if (target == COLOR_TYPE_BACKGROUND3) {
				ChangeTypedColorMatch(app, new_color, app->shared_colors[3], GPX_PIXEL_BG,
					0, 0, app->width, app->height);
				UndoSharedColor(app, 3);
				app->shared_colors[3] = new_color;
			}
			CheckAndAddUndoArea(app, ToArea(0, 0, app->width, app->height), true);
			RequestRedraw(app);
			break;
		case SCREEN_MODE_UNRESTRICTED:
			if (target == COLOR_TYPE_FOREGROUND) {
				ChangeTypedColor(app, new_color, GPX_PIXEL_FG, app->hover_x, app->hover_y, 1, 1);
				CheckAndAddUndoArea(app, ToArea(app->hover_x, app->hover_y, 1, 1), true);
				RequestRedraw(app);
			}
			break;
		case SCREEN_MODE_WIDE_UNRESTRICTED:
			if (target == COLOR_TYPE_FOREGROUND) {
				int x = app->hover_x & ~1;
				ChangeTypedColor(app, new_color, GPX_PIXEL_FG, x, app->hover_y, 2, 1);
				CheckAndAddUndoArea(app, ToArea(x, app->hover_y, 2, 1), true);
				RequestRedraw(app);
			}
			break;
		default:
			break;
	}
	RequestRedrawPalette(app);
}

void ChangeColor(Sleber* app, uint8_t target, uint8_t new_color) {
	LockMutex(&app->canvas_mutex);
	ChangeColorLocked(app, target, new_color);
	UnlockMutex(&app->canvas_mutex);
}

void ShutdownAppUndo(Sleber* app) {
	DestroyUndoBuffer(app, true);
	DestroyUndoBuffer(app, false);
}