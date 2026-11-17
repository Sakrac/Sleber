#ifndef _EDIT_H_
#define _EDIT_H_

#include <stdbool.h>

struct CellCols { uint8_t bg, fg; };
struct CellColsMC { uint8_t bg, fg, m0, m1; };

void DoUndo(Sleber* app, bool frame, bool redo);
void AddUndoScreenMode(Sleber* app, bool redo);
void SetScreenMode(Sleber* app, uint8_t mode);
void SetScreenModeLocked(Sleber* app, uint8_t mode);
bool ApplyCanvasSettings(Sleber* app, int width, int height, uint8_t mode,
	bool clear_canvas, bool clear_undo);
void ClearUndoHistory(Sleber* app);

void HoverChar(Sleber* app, int x, int y);
void HoverCharMC(Sleber* app, int x, int y);
void HoverBitmap(Sleber* app, int x, int y);
void HoverBitmapMC(Sleber* app, int x, int y);
void HoverSprite(Sleber* app, int x, int y);
void HoverSpriteMC(Sleber* app, int x, int y);
void HoverECBM(Sleber* app, int x, int y);
void PaintAt(Sleber* app, int x, int y, int use_background);
void PaintLine(Sleber* app, int x0, int y0, int x1, int y1, int use_background);
void PasteBrush(Sleber* app, I32Point point);
void Fill(Sleber* app, int x_pos, int y_pos, uint8_t color);
void FillSelection(Sleber* app, uint8_t color);
void ChangeColor(Sleber* app, uint8_t target, uint8_t new_color);

struct CellCols GetCellColsAt(Sleber* app, int x, int y);
struct CellCols GetSpriteColsAt(Sleber* app, int x, int y);
struct CellColsMC GetCellColsMCAt(Sleber* app, int x, int y);
struct CellColsMC GetSpriteColsMCAt(Sleber* app, int x, int y);

uint8_t GetColorAt(Sleber* app, int x, int y);

void ShutdownAppUndo(Sleber* app);
#endif //_EDIT_H_