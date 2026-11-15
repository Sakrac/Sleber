#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <gtk/gtk.h>
#include <signal.h>
#endif

struct Sleber;
typedef struct Sleber Sleber;
/* Opaque, per-platform GUI state (defined in platform_linux.c / platform_windows.c). */
typedef struct PlatformGui PlatformGui;

#ifdef _WIN32
typedef LARGE_INTEGER PerfTimerType;
typedef HANDLE ThreadType;
typedef DWORD ThreadReturn;
typedef void* ThreadArg;
//typedef LPTHREAD_START_ROUTINE ThreadFunction;
typedef ThreadReturn(*ThreadFunction)(ThreadArg);
typedef LONG AtomicIntType;
typedef CRITICAL_SECTION MutexType;
typedef CONDITION_VARIABLE ConditionVariable;

typedef struct WindowContext {
    HWND hwnd;
    HDC hdc;
} WindowContext;

typedef struct BitmapType {
    HBITMAP bitmap;
    uint16_t width;
    uint16_t height;
} BitmapType;
#ifdef DEBUG
#define debugstop __debugbreak()
#else
#define debugstop
#endif

#else
typedef struct timespec PerfTimerType;
typedef pthread_t ThreadType;
typedef void* (*ThreadFunction)(void*);
typedef void* ThreadReturn;
typedef void* ThreadArg;
typedef int AtomicIntType;
typedef pthread_mutex_t MutexType;
typedef pthread_cond_t ConditionVariable;

typedef struct BitmapType {
    uint32_t* pPixels;
    uint16_t width;
    uint16_t height;
} BitmapType;

typedef GtkWidget* WindowContext;
#define debugstop raise(SIGTRAP)
#endif

#ifdef DEBUG
#define DEBUG_ASSERT(condition, ...) assert(condition)
#else
#define DEBUG_ASSERT(condition, ...) ((void)0)
#endif

typedef struct { int32_t x, y; } I32Point;
typedef struct { uint16_t x, y; } I16Vec2;
typedef struct { uint16_t x, y, w, h; } Area;
static inline I16Vec2 ToI16Vec2(int x, int y) { return (I16Vec2) { (uint16_t)x, (uint16_t)y }; }
static inline Area ToArea(int x, int y, int w, int h) { return (Area) { (uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h }; }

FILE* OpenFileUTF8(const char* path_utf8, const char* mode);
struct PlatformGui* CreateGuiData(void);
Sleber* AppNew(void);

int StartThread(ThreadType* thread, size_t stack, ThreadFunction func, void* arg, const char* name);
int JoinThread(ThreadType* thread);
void InitMutex(MutexType* m);
void DestroyMutex(MutexType* m);
void LockMutex(MutexType* m);
void UnlockMutex(MutexType* m);
void InitConditionVariable(ConditionVariable* c);
void DestroyConditionVariable(ConditionVariable* c);
void WaitConditionVariable(ConditionVariable* c, MutexType* m);
void AwakeConditionVariable(ConditionVariable* c);
PerfTimerType GetPerfCounter(void);
double PerfCounterToSeconds(PerfTimerType start, PerfTimerType end);

struct Int2 {
	int x;
	int y;
};

void UpdateStatus(Sleber* app, const char* message_utf8);
WindowContext GetWindowContext(Sleber* app);
WindowContext GetPaletteContext(Sleber* app);
WindowContext GetColorPickerContext(Sleber* app);
void ReleaseWindowContext(Sleber* app, WindowContext context);

BitmapType CreateUIBitmap(int width, int height, void **pixels, WindowContext context);

void OpenColorPickerAt(Sleber* app, const I32Point sp);

void InvalidateDraw(Sleber *app, Area area);
void InvalidatePalette(Sleber *app, Area area);
void InvalidateColorPicker(Sleber* app);

#ifdef DEBUG
static inline void DebugLog(const char* format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), format, args);
    puts(buffer);
#ifdef _WIN32
    OutputDebugStringA(buffer);
#endif
    va_end(args);
}
#else
#define DebugLog(...)
#endif

#endif


