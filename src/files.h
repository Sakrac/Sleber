#include "sleber.h"

int ImportImageFile(Sleber* app, const char* path_utf8);
int ExportImageToFile(Sleber* app, const char* path_utf8);
int OpenGpxFile(Sleber* app, const char* path_utf8);
int SaveGpxFile(Sleber* app, const char* path_utf8);
bool OpenDroppedFile(Sleber* app, const char* path_utf8);
bool OpenImage(Sleber* app, const char* path_utf8);
bool SaveImage(Sleber* app, const char* path_utf8);
bool SetCurrentFile(Sleber* app, const char* path_utf8, SleberFileType type);
void ClearCurrentFile(Sleber* app);
bool SaveCurrentFile(Sleber* app);

// Takes ownership of argb, whether conversion succeeds or fails.
bool SetBrushFromARGB(Sleber* app, uint32_t* argb, int width, int height);
