#ifndef _UNDO_H_
#define _UNDO_H_
#include <stdint.h>
#include <stdbool.h>

#ifndef NULL
#define NULL ((void*)0)
#endif

struct Sleber;
typedef struct Sleber Sleber;

#define UNDO_COMMAND_CHUNK (64 * 1024)
struct UndoCommandBuffer {
    struct UndoCommandBuffer* next;
    uint32_t size;
    uint8_t data[UNDO_COMMAND_CHUNK - sizeof(struct UndoCommandBuffer*) - sizeof(uint32_t)];
};
typedef struct UndoCommandBuffer UndoCommandBuffer;

// TODO: Create UNDO_RECTANGLE_BUF with separate memory allocation for large buffers
enum {
    UNDO_NONE = 0,

    UNDO_POINT,
    UNDO_COLOR, // palette change
    UNDO_ROW, // one row x,y,n,pixels
    UNDO_COLUMN, // one column x,y,n,pixels
    UNDO_RECTANGLE, // a square x,y,w,h,pixels
	UNDO_RECTANGLE_REF, // a square x,y,w,h,pixels separate allocation

    UNDO_SIZE, // change of width/height
    UNDO_MODE, // change of screen mode
    UNDO_FRAME, // end of frame commands
    UNDO_STOP, // end of undo commands in this undo step

    UNDO_BORDER_COLOR,
	UNDO_SHARED_COLOR, // change of shared color

    UNDO_COUNT,

	UNDO_WIDE_POSITION = 0x80,
	UNDO_WIDE_SIZE = 0x40,
};

// Undo note: If the memory runs out for an undo buffer,
// silently fail to record undo to let the user complete
// work without further straining the system.

uint8_t* ReserveUndoCommandBuffer(Sleber* app, uint32_t size, bool redo);
uint8_t* GrabUndoCommand(Sleber* app, uint8_t** ppBufferStart, bool redo);
uint8_t SneakUndoCommand(Sleber* app, bool redo);
void StoreUndoCommand(Sleber* app, uint8_t undo_command, bool redo);
uint8_t* StoreUndoCommandWithBuffer(Sleber* app, uint8_t undo_command, uint32_t size, bool redo);
void DrainUndoBuffer(Sleber* app, uint32_t size, bool redo);
#endif
