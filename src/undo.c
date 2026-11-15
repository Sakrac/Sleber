#include "sleber.h"
#include "undo.h"
#include <stdlib.h>
#include <assert.h>

static_assert(UNDO_COMMAND_CHUNK == sizeof(UndoCommandBuffer), "Size of undo command buffer mismatch");

static struct UndoCommandBuffer* CreateUndoCommandBuffer(struct UndoCommandBuffer **ppBuffer) {
	// check if there is a stowed away empty buffer to reuse
	struct UndoCommandBuffer* last = *ppBuffer, *scnd = last;
	while (last && last->next) { scnd = last;  last = last->next; }
	if (last && last->size == 0) {
		scnd->next = NULL;
		last->next = *ppBuffer;
		*ppBuffer = last;
		return last;
	}

	// if the last buffer is not empty, create a new one
	struct UndoCommandBuffer* UndoBuf = (struct UndoCommandBuffer*)calloc(1, sizeof(struct UndoCommandBuffer));
	if (UndoBuf) {
		UndoBuf->next = *ppBuffer;
		UndoBuf->size = 0;
		*ppBuffer = UndoBuf;
	}
	return UndoBuf;
}

uint8_t* ReserveUndoCommandBuffer(Sleber* app, uint32_t size, bool redo) {
	struct UndoCommandBuffer** ppBuffers = redo ? &app->redo_command_buffer : &app->undo_command_buffer;

	if (size > sizeof(app->undo_command_buffer->data)) {
		return NULL;
	}

	struct UndoCommandBuffer* UndoBuf = *ppBuffers;
	uint32_t left = UndoBuf ? sizeof(UndoBuf->data) - UndoBuf->size : 0;
	if (size > left) { UndoBuf = CreateUndoCommandBuffer(ppBuffers); }
	if (!UndoBuf) { return NULL; }
	uint8_t* ptr = UndoBuf->data + UndoBuf->size;
	UndoBuf->size += (uint32_t)size;
	return ptr;
}

#ifdef DEBUG
bool IsUndoEnd(Sleber* app, const uint8_t* buffer, bool redo) {
	struct UndoCommandBuffer* pUndo = redo ? app->redo_command_buffer : app->undo_command_buffer;
	if (!pUndo) { return false; }
	return buffer == pUndo->data + pUndo->size;
}
#endif

uint8_t* GrabUndoCommand(Sleber* app, uint8_t** ppBufferStart, bool redo) {
	struct UndoCommandBuffer** ppBuffers = redo ? &app->redo_command_buffer : &app->undo_command_buffer;
	if (!*ppBuffers) { return NULL; }
	struct UndoCommandBuffer* UndoBuf = *ppBuffers;

	*ppBufferStart = UndoBuf->data;
	uint8_t* ptr = UndoBuf->data + UndoBuf->size;
	return ptr;
}

uint8_t SneakUndoCommand(Sleber* app, bool redo) {
	struct UndoCommandBuffer** ppBuffers = redo ? &app->redo_command_buffer : &app->undo_command_buffer;
	if (!*ppBuffers) { return UNDO_NONE; }
	struct UndoCommandBuffer* UndoBuf = *ppBuffers;

	if (UndoBuf->size) {
		return UndoBuf->data[UndoBuf->size - 1];
	}
	return UNDO_NONE;
}

void StoreUndoCommand(Sleber* app, uint8_t undo_commadn, bool redo) {
	struct UndoCommandBuffer** ppBuffers = redo ? &app->redo_command_buffer : &app->undo_command_buffer;
	struct UndoCommandBuffer* UndoBuf = *ppBuffers;
	if (!UndoBuf) { return; }
	DEBUG_ASSERT(1 <= sizeof(UndoBuf->data) - UndoBuf->size);
	UndoBuf->data[UndoBuf->size++] = undo_commadn;
}

uint8_t* StoreUndoCommandWithBuffer(Sleber* app, uint8_t undo_command, uint32_t size, bool redo) {
	uint8_t* buffer = ReserveUndoCommandBuffer(app, size, redo);
	if (!buffer) { return 0; }
	buffer[size - 1] = undo_command;
	return buffer;
}

void DrainUndoBuffer(Sleber* app, uint32_t size, bool redo) {
	struct UndoCommandBuffer** ppBuffers = redo ? &app->redo_command_buffer : &app->undo_command_buffer;
	struct UndoCommandBuffer* UndoBuf = *ppBuffers;
	if (!UndoBuf) { return; }
	DEBUG_ASSERT(size <= UndoBuf->size);
	UndoBuf->size -= size;
	if (UndoBuf->size == 0 && (*ppBuffers)->next) {
		*ppBuffers = (*ppBuffers)->next;
		UndoBuf->next = NULL;
		struct UndoCommandBuffer* last = *ppBuffers;
		while (last->next) { last = last->next; }
		last->next = UndoBuf;
	}
}
