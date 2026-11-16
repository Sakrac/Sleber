#ifdef _WIN32

#ifndef COBJMACROS
#define COBJMACROS
#endif

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <limits.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <winuser.h>

#include "stb/stb_image.h"
#include "stb/stb_image_write.h"

#include "platform.h"
#include "palette.h"
#include "sleber.h"
#include "Edit.h"
#include "convert.h"
#include "files.h"
#include "sbx_file.h"

#define IDM_FILE_OPEN 1001
#define IDM_FILE_IMPORT 1002
#define IDM_FILE_EXPORT 1003
#define IDM_FILE_SAVE 1004
#define IDM_FILE_QUIT 1005
#define IDM_FILE_OPEN_SBX 1006
#define IDM_FILE_SAVE_SBX 1007
#define IDM_FILE_SAVE_CURRENT 1008
#define IDM_HELP_ABOUT 1010

#define IDM_MODE_CHAR 1031
#define IDM_MODE_CHAR_MC 1032
#define IDM_MODE_BITMAP 1033
#define IDM_MODE_BITMAP_MC 1034
#define IDM_MODE_SPRITE 1035
#define IDM_MODE_SPRITE_MC 1036
#define IDM_MODE_CHAR_ECBM 1037
#define IDM_MODE_CHAR_MIXED 1038
#define IDM_MODE_UNRESTRICTED 1039
#define IDM_MODE_UNRESTRICTED_WIDE 1040
#define IDM_SNAP_TO_GRID 1041
#define IDM_DITHER_OFF 1042
#define IDM_DITHER_LOW 1043
#define IDM_DITHER_MEDIUM 1044
#define IDM_DITHER_HIGH 1045
#define IDM_DOCK_PALETTE 1046
#define IDM_SIZE_NEW 1047

struct PlatformGui {
	HWND window;
	HWND status_label;
	HWND palette_window;
	HWND color_picker_window;

	/* Off-screen canvas buffer, avoids flicker from painting directly on screen. */
//	HDC backbuffer_dc;
//	HBITMAP backbuffer;
//	LONG_PTR palette_style;
//	int backbuffer_w;
//	int backbuffer_h;
};

static void SetWindowsPalette(RGBQUAD* pal) {
	const uint8_t* palc64 = GetC64Palette();
	// Fill palette
	for (int i = 0; i < 16; i++) {
		pal[i].rgbRed = palc64[i * 3 + 0];
		pal[i].rgbGreen = palc64[i * 3 + 1];
		pal[i].rgbBlue = palc64[i * 3 + 2];
		pal[i].rgbReserved = 0;
	}

	const uint8_t* palui = GetUIPalette();
	for (int u = 0, n = UIPaletteSize(); u < n; ++u) {
		int i = 255 - u;
		pal[i].rgbRed = palui[u * 3 + 0];
		pal[i].rgbGreen = palui[u * 3 + 1];
		pal[i].rgbBlue = palui[u * 3 + 2];
	}
}

BitmapType CreateUIBitmap(int width, int height, void** pixels, WindowContext context) {
	char bmimem[sizeof(BITMAPINFOHEADER) + sizeof(RGBQUAD) * 256];
	BITMAPINFO* bmi = (BITMAPINFO*)bmimem;
	BITMAPINFOHEADER* bih = &bmi->bmiHeader;
	bih->biSize = sizeof(BITMAPINFOHEADER);
	bih->biWidth = width & (~3UL);
	bih->biHeight = -height;
	bih->biPlanes = 1;
	bih->biBitCount = 8;
	bih->biCompression = BI_RGB;

	// Fill palette
	SetWindowsPalette(bmi->bmiColors); // points into memory after the header

	HBITMAP hbmp = CreateDIBSection(context.hdc, bmi, DIB_RGB_COLORS, pixels, NULL, 0);
	return (BitmapType) { hbmp, (uint16_t)width, (uint16_t)height };
}

WindowContext GetWindowContext(Sleber* app) {
	HWND hwnd = app->gui->window;
	return (WindowContext) { hwnd, GetDC(hwnd) };
}

WindowContext GetPaletteContext(Sleber* app) {
	HWND hwnd = app->gui->palette_window;
	return (WindowContext) { hwnd, GetDC(hwnd) };
}

WindowContext GetColorPickerContext(Sleber* app) {
	HWND hwnd = app->gui->color_picker_window;
	return (WindowContext) { hwnd, GetDC(hwnd) };
}

void ReleaseWindowContext(Sleber* app, WindowContext context) {
	(void)app;
	ReleaseDC(context.hwnd, context.hdc);
}

void InvalidateDraw(Sleber* app, Area area) {
	RECT rect = (RECT){.left = area.x, .top = area.y, .right = area.x + area.w, .bottom = area.y + area.h};
	if (area.w && area.h) { DebugLog("InvalidateDraw: %d,%d %dx%d\n", area.x, area.y, area.w, area.h); }
	InvalidateRect(app->gui->window, (area.w && area.h) ? &rect : NULL, TRUE);
}

void InvalidatePalette(Sleber* app, Area area) {
	RECT rect = (RECT){.left = area.x, .top = area.y, .right = area.x + area.w, .bottom = area.y + area.h};
	InvalidateRect(app->gui->palette_window, (area.w && area.h) ? &rect : NULL, TRUE);
}

void InvalidateColorPicker(Sleber* app) {
	InvalidateRect(app->gui->color_picker_window, NULL, TRUE);
}

struct Int2 WindowSize(HWND Window) {
	RECT client;
	GetClientRect(Window, &client);
	return (struct Int2) { (client.right - client.left)& (~3UL), client.bottom - client.top };
}

/* ---- UTF-8 / UTF-16 helpers (all app-facing paths and messages are UTF-8) ---- */

static wchar_t* UTF8ToWide(const char* s) {
	if (!s) {
		return NULL;
	}
	int len = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
	if (len <= 0) {
		return NULL;
	}
	wchar_t* w = (wchar_t*)malloc(sizeof(wchar_t) * (size_t)len);
	if (w) {
		MultiByteToWideChar(CP_UTF8, 0, s, -1, w, len);
	}
	return w;
}

static char* WideToUTF8(const wchar_t* w) {
	if (!w) {
		return NULL;
	}
	int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
	if (len <= 0) {
		return NULL;
	}
	char* s = (char*)malloc((size_t)len);
	if (s) {
		WideCharToMultiByte(CP_UTF8, 0, w, -1, s, len, NULL, NULL);
	}
	return s;
}

FILE* OpenFileUTF8(const char* path_utf8, const char* mode) {
	wchar_t* wpath = UTF8ToWide(path_utf8);
	if (!wpath) {
		return NULL;
	}
	wchar_t wmode[16];
	MultiByteToWideChar(CP_UTF8, 0, mode, -1, wmode, 16);
	FILE* fp = NULL;
	_wfopen_s(&fp, wpath, wmode);
	free(wpath);
	return fp;
}

int StartThread(ThreadType* thread, size_t stack, ThreadFunction func, void* arg, const char* name) {
	(void)name; // unused parameter
	ThreadType newThread = CreateThread(NULL, stack, (LPTHREAD_START_ROUTINE)func, arg, 0, NULL);
	if (thread) {
		*thread = newThread;
	}
	return newThread == NULL ? 1 : 0;
}

int JoinThread(ThreadType* thread) {
	if (!thread || !*thread) {
		return 1;
	}
	DWORD result = WaitForSingleObject(*thread, INFINITE);
	if (result != WAIT_OBJECT_0) {
		return (int)GetLastError();
	}
	CloseHandle(*thread);
	*thread = NULL;
	return 0;
}

void InitMutex(MutexType* m) {
	InitializeCriticalSection(m);
}

void DestroyMutex(MutexType* m) {
	DeleteCriticalSection(m);
}

void LockMutex(MutexType* m) {
	EnterCriticalSection(m);
}

void UnlockMutex(MutexType* m) {
	LeaveCriticalSection(m);
}

void InitConditionVariable(ConditionVariable* c) {
	InitializeConditionVariable(c);
}

void DestroyConditionVariable(ConditionVariable* c) {
	(void)c;
}

void WaitConditionVariable(ConditionVariable* c, MutexType* m) {
	SleepConditionVariableCS(c, m, INFINITE);
}

void AwakeConditionVariable(ConditionVariable* c) {
	WakeConditionVariable(c);
}



void UpdateStatus(Sleber* app, const char* message_utf8) {
	if (!app->gui->status_label) {
		return;
	}
	wchar_t* wmessage = UTF8ToWide(message_utf8);
	if (wmessage) {
		SetWindowTextW(app->gui->status_label, wmessage);
		free(wmessage);
	}
}

static void BrushToClipboard(Sleber* app, HWND hwnd) {
	LockMutex(&app->canvas_mutex);
	if (!app->brush || app->brush_w <= 0 || app->brush_h <= 0) {
		UnlockMutex(&app->canvas_mutex);
		return;
	}
	const int w = app->brush_w, h = app->brush_h;
	if ((size_t)w > (SIZE_MAX - sizeof(BITMAPINFOHEADER)) / 4 / (size_t)h) {
		UnlockMutex(&app->canvas_mutex);
		return;
	}
	HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + (size_t)w * h * 4);
	if (!mem) {
		UnlockMutex(&app->canvas_mutex);
		return;
	}
	BITMAPINFOHEADER* bih = (BITMAPINFOHEADER*)GlobalLock(mem);
	if (!bih) {
		GlobalFree(mem);
		UnlockMutex(&app->canvas_mutex);
		return;
	}
	memset(bih, 0, sizeof(*bih));
	bih->biSize = sizeof(BITMAPINFOHEADER);
	bih->biWidth = w;
	bih->biHeight = h; // Windows love to store bitmaps upside down
	bih->biPlanes = 1;
	bih->biBitCount = 32;
	bih->biCompression = BI_RGB;
	const uint32_t* color32 = GetC64PaletteABGR();
	uint32_t* dst = (uint32_t*)(bih + 1);
	const uint8_t* src = app->brush + (size_t)(h) * w;
	for (int y = 0; y < h; ++y) {
		src -= w;
		for (int x = 0; x < w; ++x) {
			*dst++ = src[x] == 0xff ? 0 : color32[src[x] & (COLOR_COUNT - 1)];
		}
	}
	GlobalUnlock(mem);
	UnlockMutex(&app->canvas_mutex);
	if (OpenClipboard(hwnd)) {
		EmptyClipboard();
		if (!SetClipboardData(CF_DIB, mem)) {
			GlobalFree(mem);
		}
		CloseClipboard();
	} else {
		GlobalFree(mem);
	}
}

static bool ClipboardDibToBrush(Sleber* app) {
		HGLOBAL mem = (HGLOBAL)GetClipboardData(CF_DIB);
	if (!mem) {
		return false;
	}

	const SIZE_T global_size = GlobalSize(mem);
	BITMAPINFOHEADER* header = (BITMAPINFOHEADER*)GlobalLock(mem);
	if (!header || global_size < sizeof(BITMAPINFOHEADER) ||
		header->biSize < sizeof(BITMAPINFOHEADER) ||
		header->biSize > global_size || header->biWidth <= 0 ||
		header->biHeight == 0 || header->biHeight == (-2147483647L - 1L) ||
		header->biPlanes != 1 || header->biBitCount != 32 ||
		header->biCompression != BI_RGB) {
		if (header) {
			GlobalUnlock(mem);
		}
		return false;
	}

	const int width = header->biWidth;
	const int height = header->biHeight < 0 ? -header->biHeight : header->biHeight;
	if ((size_t)width > SIZE_MAX / 4 ||
		(size_t)width * 4 > SIZE_MAX - 3 ||
		(size_t)height > SIZE_MAX / (((size_t)width * 4 + 3) & ~((size_t)3))) {
		GlobalUnlock(mem);
		return false;
	}

	const size_t row_stride = ((size_t)width * 4 + 3) & ~((size_t)3);
	const size_t image_size = row_stride * (size_t)height;
	if (image_size > global_size ||
		(size_t)header->biSize > global_size - image_size) {
		GlobalUnlock(mem);
		return false;
	}

	uint32_t* argb = (uint32_t*)malloc((size_t)width * (size_t)height * sizeof(*argb));
	if (!argb) {
		GlobalUnlock(mem);
		UpdateStatus(app, "Unable to allocate clipboard image.");
		return false;
	}

	const uint8_t* source = (const uint8_t*)header + header->biSize;
	for (int y = 0; y < height; ++y) {
		const int source_y = header->biHeight < 0 ? y : height - 1 - y;
		memcpy(argb + (size_t)y * width,
			source + (size_t)source_y * row_stride, (size_t)width * sizeof(*argb));
	}
	GlobalUnlock(mem);

	const bool success = SetBrushFromARGB(app, argb, width, height);
	return success;
}

static bool ClipboardBitmapToBrush(Sleber* app, HWND hwnd) {
	if ((!IsClipboardFormatAvailable(CF_DIB) && !IsClipboardFormatAvailable(CF_BITMAP)) ||
		!OpenClipboard(hwnd)) {
		return false;
	}
	bool ok = false;
	if (IsClipboardFormatAvailable(CF_DIB)) {
		ok = ClipboardDibToBrush(app);
	}
	HBITMAP hbm = ok ? NULL : (HBITMAP)GetClipboardData(CF_BITMAP);
	BITMAP bm;
	if (!ok && hbm && GetObject(hbm, sizeof(bm), &bm) && bm.bmWidth > 0 && bm.bmHeight > 0 &&
		(size_t)bm.bmWidth <= SIZE_MAX / sizeof(uint32_t) / (size_t)bm.bmHeight) {
		int w = bm.bmWidth, h = bm.bmHeight;
		uint32_t* argb = (uint32_t*)malloc((size_t)w * h * sizeof(*argb));
		HDC hdc = GetDC(NULL);
		if (argb && hdc) {
			BITMAPINFO bi = { 0 };
			bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			bi.bmiHeader.biWidth = w;
			bi.bmiHeader.biHeight = -h;
			bi.bmiHeader.biPlanes = 1;
			bi.bmiHeader.biBitCount = 32;
			bi.bmiHeader.biCompression = BI_RGB;
			if (GetDIBits(hdc, hbm, 0, h, argb, &bi, DIB_RGB_COLORS)) {
				ok = SetBrushFromARGB(app, argb, w, h);
				argb = NULL;
			}
		}
		if (hdc) { ReleaseDC(NULL, hdc); }
		if (argb) { free(argb); }
	}
	CloseClipboard();
	return ok;
}

/* ---- GUI app data ---- */

PlatformGui* CreateGuiData(void) {
	PlatformGui* gui = (PlatformGui*)calloc(1, sizeof(PlatformGui));
	return gui;
}

/* ---- File dialogs (Unicode) ---- */

static char* RunFileDialog(Sleber* app, BOOL save, const wchar_t* title, const wchar_t* filter, const wchar_t* default_name) {
	wchar_t path[MAX_PATH];
	path[0] = L'\0';
	if (default_name) {
		wcsncpy_s(path, MAX_PATH, default_name, _TRUNCATE);
	}

	OPENFILENAMEW ofn;
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = app->gui->window;
	ofn.lpstrFilter = filter;
	ofn.lpstrFile = path;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrTitle = title;
	ofn.Flags = save ? (OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST)
		: (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST);

	BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
	if (!ok) {
		return NULL;
	}
	return WideToUTF8(path);
}

static char* RunCanvasFileDialog(Sleber* app, BOOL save, SleberFileType* type) {
	static const wchar_t filter[] =
		L"SBX and GPX Files\0*.sbx;*.gpx\0"
		L"SBX Files\0*.sbx\0"
		L"GPX Files\0*.gpx\0"
		L"All Files\0*.*\0\0";
	wchar_t path[MAX_PATH] = L"";
	if (save) {
		wcscpy_s(path, MAX_PATH, L"bitmap.sbx");
	}

	OPENFILENAMEW ofn;
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = app->gui->window;
	ofn.lpstrFilter = filter;
	ofn.lpstrFile = path;
	ofn.nMaxFile = MAX_PATH;
	ofn.nFilterIndex = 1;
	ofn.lpstrDefExt = L"sbx";
	ofn.Flags = save ? (OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST)
		: (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST);

	if (!(save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn))) {
		return NULL;
	}

	if (save) {
		const wchar_t* extension = ofn.nFilterIndex == 3 ? L".gpx" : L".sbx";
		wchar_t* separator = wcsrchr(path, L'\\');
		wchar_t* dot = wcsrchr(path, L'.');
		const BOOL has_canvas_extension = dot && (!separator || dot > separator) &&
			(_wcsicmp(dot, L".sbx") == 0 || _wcsicmp(dot, L".gpx") == 0);
		if (!has_canvas_extension) {
			if (!dot || (separator && dot < separator)) {
				dot = path + wcslen(path);
			}
			*dot = L'\0';
			if (wcscat_s(path, MAX_PATH, extension) != 0) {
				return NULL;
			}
		}
		if (type) {
			*type = ofn.nFilterIndex == 3 ? SLEBER_FILE_GPX : SLEBER_FILE_SBX;
		}
	}
	return WideToUTF8(path);
}

#define IMPORT_KEEP_BACKGROUND_ID 5001
#define IMPORT_OPTIONS_GROUP_ID 5002

static char* RunImportFileDialog(Sleber* app) {
	static const COMDLG_FILTERSPEC file_types[] = {
		{ L"Image Files", L"*.png;*.bmp;*.jpg;*.jpeg" },
		{ L"All Files", L"*.*" },
	};
	IFileOpenDialog* dialog = NULL;
	IFileDialogCustomize* customize = NULL;
	IShellItem* item = NULL;
	PWSTR path = NULL;
	char* path_utf8 = NULL;
	BOOL keep_background = FALSE;
	HRESULT hr = CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
		&IID_IFileOpenDialog, (void**)&dialog);
	if (FAILED(hr)) {
		return NULL;
	}

	dialog->lpVtbl->SetTitle(dialog, L"Import Image");
	dialog->lpVtbl->SetFileTypes(dialog, ARRAYSIZE(file_types), file_types);
	dialog->lpVtbl->SetFileTypeIndex(dialog, 1);
	dialog->lpVtbl->SetOptions(dialog, FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);
	dialog->lpVtbl->SetDefaultExtension(dialog, L"png");
	if (SUCCEEDED(dialog->lpVtbl->QueryInterface(dialog, &IID_IFileDialogCustomize, (void**)&customize))) {
		customize->lpVtbl->StartVisualGroup(customize, IMPORT_OPTIONS_GROUP_ID, L"Import options");
		customize->lpVtbl->AddCheckButton(customize, IMPORT_KEEP_BACKGROUND_ID,
			L"Keep background colors", app->keep_import_background_colors);
		customize->lpVtbl->EndVisualGroup(customize);
	}

	hr = dialog->lpVtbl->Show(dialog, app->gui->window);
	if (SUCCEEDED(hr)) {
		hr = dialog->lpVtbl->GetResult(dialog, &item);
	}
	if (SUCCEEDED(hr)) {
		hr = item->lpVtbl->GetDisplayName(item, SIGDN_FILESYSPATH, &path);
	}
	if (SUCCEEDED(hr) && customize) {
		customize->lpVtbl->GetCheckButtonState(customize, IMPORT_KEEP_BACKGROUND_ID, &keep_background);
		app->keep_import_background_colors = keep_background != FALSE;
	}
	if (SUCCEEDED(hr)) {
		path_utf8 = WideToUTF8(path);
	}

	if (path) { CoTaskMemFree(path); }
	if (item) { item->lpVtbl->Release(item); }
	if (customize) { customize->lpVtbl->Release(customize); }
	if (dialog) { dialog->lpVtbl->Release(dialog); }
	return path_utf8;
}

/* ---- Window procedures ---- */

void DockPalette(Sleber* app) {
	LONG_PTR style = GetWindowLongPtr(app->gui->palette_window, GWL_STYLE);

	style &= ~(WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_MINIMIZE | WS_MAXIMIZE | WS_SYSMENU);
	style |= (WS_CHILD | WS_CLIPSIBLINGS);

	SetWindowLongPtr(app->gui->palette_window, GWL_STYLE, style);
	SetParent(app->gui->palette_window, app->gui->window);

	int width = app->palette_window_width;
	int height = app->palette_window_height;

	// top right?
	SetWindowPos(app->gui->palette_window, NULL, app->draw_window_width - width, 0, width, height,
		SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);

	app->palette_docked = 1;
	CheckMenuItem(GetMenu(app->gui->window), IDM_DOCK_PALETTE, MF_BYCOMMAND | MF_CHECKED);
}

void UndockPalette(Sleber* app) {
	HWND palette = app->gui->palette_window;

	// A top-level window must not retain the WS_CHILD style.
	LONG_PTR style = GetWindowLongPtr(palette, GWL_STYLE);
	style &= ~WS_CHILD;
	style |= WS_POPUP | WS_CAPTION;

	SetWindowLongPtr(palette, GWL_STYLE, style);

	// Remove the parent. The saved screen coordinates can then be reused.
	SetParent(palette, NULL);

	RECT rect = {0};
	GetClientRect(palette, &rect);

	RECT parentRect;
	GetWindowRect(app->gui->window, &parentRect);

	SetWindowPos(
		palette,
		HWND_TOP,
		parentRect.right - app->palette_window_width_outer,
		parentRect.top,
		app->palette_window_width_outer,
		app->palette_window_height_outer,
		SWP_NOACTIVATE |
		SWP_SHOWWINDOW |
		SWP_FRAMECHANGED
	);

	// Optional: make it the active foreground window.
	SetForegroundWindow(palette);

	app->palette_docked = 0;
	CheckMenuItem(GetMenu(app->gui->window), IDM_DOCK_PALETTE, MF_BYCOMMAND | MF_UNCHECKED);
}


static LRESULT CALLBACK ColorPickerWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
	Sleber* app = (Sleber*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
	switch (msg) {
	case WM_CREATE: {
		CREATESTRUCTW* cs = (CREATESTRUCTW*)lparam;
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
		return 0;
	}
	case WM_DESTROY:
			PostQuitMessage(0);
			return 0;
		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hwnd, &ps);
			LockMutex(&app->redraw_mutex);
			BitmapType color_picker_bitmap = GetColorPickerBitmap();
			if (app && BitmapValid(color_picker_bitmap)) {
				HDC hdcMem = CreateCompatibleDC(hdc);
				HGDIOBJ old = SelectObject(hdcMem, color_picker_bitmap.bitmap);

				BitBlt(hdc, ps.rcPaint.left, ps.rcPaint.top,
					ps.rcPaint.right - ps.rcPaint.left,
					ps.rcPaint.bottom - ps.rcPaint.top,
					hdcMem, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);

				SelectObject(hdcMem, old);
				DeleteDC(hdcMem);
			}
			UnlockMutex(&app->redraw_mutex);

			EndPaint(hwnd, &ps);
			return 0;
		}
		case WM_LBUTTONDOWN:
		case WM_RBUTTONDOWN:
		{
			if (!app) {
				break;
			}
			int px = GET_X_LPARAM(lparam);
			int py = GET_Y_LPARAM(lparam);
			RECT cr;
			GetClientRect(app->gui->color_picker_window, &cr);

			int col = (px * 2) / (cr.right - cr.left);
			int row = (py * 8) / (cr.bottom - cr.top);
			int index = col * 8 + row;
			if (col >= 0 && col <= 1 && row >= 0 && row <= 7 && index >= 0 && index < COLOR_COUNT) {
				ChangeColor(app, app->color_picker_target_color, (uint8_t)index);

				if (msg == WM_LBUTTONDOWN) {
					ShowWindow(app->gui->color_picker_window, SW_HIDE);
				}
			}
			return 0;
		}
		case WM_MOUSEMOVE: {
			if (!app) { break; }
			int px = GET_X_LPARAM(lparam);
			int py = GET_Y_LPARAM(lparam);
			RECT cr;
			GetClientRect(hwnd, &cr);
			int col = (px * 2) / (cr.right - cr.left);
			int row = (py * 8) / (cr.bottom - cr.top);
			int index = col * 8 + row;
			if (col >= 0 && col <= 1 && row >= 0 && row <= 7 && index >= 0 && index < COLOR_COUNT) {
				if (index != app->color_picker_hover_color) {
					app->color_picker_hover_color = (uint8_t)index;
					RequestRedrawColorPicker(app);
				}
			} else {
				if (app->color_picker_hover_color != COLOR_COUNT) {
					app->color_picker_hover_color = COLOR_COUNT;
					RequestRedrawColorPicker(app);
				}
			}
			return 0;
		}
		case WM_KEYUP:
		{
			if (!app) {
				break;
			}
			switch (wparam) {
				case VK_ESCAPE:
					ShowWindow(app->gui->color_picker_window, SW_HIDE);
					app->color_picker_visible = false;
					break;
			}
			break;
		}
		default:
			break;
	}
	return DefWindowProc(hwnd, msg, wparam, lparam);
}

void OpenColorPickerAt(Sleber* app, const I32Point sp) {
	ShowWindow(app->gui->color_picker_window, SW_HIDE);
	SetWindowPos(app->gui->color_picker_window, HWND_TOPMOST,
		sp.x, sp.y, 0, 0,
		SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW
	);
	SetFocus(app->gui->color_picker_window);
	app->color_picker_visible = true;
}

static LRESULT CALLBACK PaletteWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
	Sleber* app = (Sleber*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

	switch (msg) {
		case WM_CREATE:
		{
			CREATESTRUCTW* cs = (CREATESTRUCTW*)lparam;
			SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
			return 0;
		}
		case WM_ERASEBKGND:
		{
			return 1; /* WM_PAINT below always fills the whole client area */
		}
		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hwnd, &ps);
			LockMutex(&app->redraw_mutex);
			BitmapType palette_bitmap = GetPaletteBitmap();
			if (app && BitmapValid(palette_bitmap)) {
				HDC hdcMem = CreateCompatibleDC(hdc);
				HGDIOBJ old = SelectObject(hdcMem, palette_bitmap.bitmap);

				BitBlt(hdc, ps.rcPaint.left, ps.rcPaint.top,
					ps.rcPaint.right - ps.rcPaint.left,
					ps.rcPaint.bottom - ps.rcPaint.top,
					hdcMem, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);

				SelectObject(hdcMem, old);
				DeleteDC(hdcMem);
			}
			UnlockMutex(&app->redraw_mutex);

			EndPaint(hwnd, &ps);
			return 0;
		}
		case WM_LBUTTONDOWN:
		case WM_RBUTTONDOWN: {
			RECT cr;
			POINT screen_point = { .x = GET_X_LPARAM(lparam), .y = GET_Y_LPARAM(lparam) };
			GetClientRect(app->gui->palette_window, &cr);
			ClientToScreen(hwnd, &screen_point);

			UIPaletteButtonDown(app,
				(I32Point) { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) },
				(I32Point) { cr.left, cr.top },
				(I32Point) { cr.right, cr.bottom },
				(I32Point) { screen_point.x, screen_point.y },
				msg == WM_LBUTTONDOWN);
			return 0;
		}
		default:
			break;
	}
	return DefWindowProcW(hwnd, msg, wparam, lparam);
}

enum {
	SIZE_NEW_WIDTH = 1,
	SIZE_NEW_HEIGHT,
	SIZE_NEW_MODE,
	SIZE_NEW_CLEAR_CANVAS,
	SIZE_NEW_CLEAR_UNDO,
	SIZE_NEW_APPLY,
	SIZE_NEW_CANCEL
};

typedef struct {
	Sleber* app;
	HWND window;
	HWND width;
	HWND height;
	HWND width_label;
	HWND height_label;
	HWND width_pixels;
	HWND height_pixels;
	HWND units_label;
	HWND mode;
	HWND clear_canvas;
	HWND clear_undo;
	bool accepted;
} SizeNewDialog;

static const wchar_t* size_new_mode_labels[] = {
	L"Char", L"Char MC", L"Bitmap", L"Bitmap MC", L"Sprite",
	L"Sprite MC", L"Char ECBM", L"Char Mixed", L"Unrestricted",
	L"Unrestricted Wide"
};

static void UpdateSizeNewDimensions(SizeNewDialog* dialog) {
	int mode = (int)SendMessageW(dialog->mode, CB_GETCURSEL, 0, 0);
	int unit_width = ScreenModeHorizontalUnit((uint8_t)mode);
	int unit_height = ScreenModeVerticalUnit((uint8_t)mode);
	wchar_t value[32], width_text[16], height_text[16];

	swprintf(value, sizeof(value) / sizeof(value[0]), L"Unit: %d x %d px", unit_width, unit_height);
	SetWindowTextW(dialog->units_label, value);

	GetWindowTextW(dialog->width, width_text, sizeof(width_text) / sizeof(width_text[0]));
	GetWindowTextW(dialog->height, height_text, sizeof(height_text) / sizeof(height_text[0]));
	long width_units = wcstol(width_text, NULL, 10);
	long height_units = wcstol(height_text, NULL, 10);
	if (width_units < 1 || width_units > MAX_CANVAS_WIDTH / unit_width) {
		swprintf(value, sizeof(value) / sizeof(value[0]), L"Invalid size");
	} else {
		swprintf(value, sizeof(value) / sizeof(value[0]), L"%ld px", width_units * unit_width);
	}
	SetWindowTextW(dialog->width_pixels, value);
	if (height_units < 1 || height_units > MAX_CANVAS_HEIGHT / unit_height) {
		swprintf(value, sizeof(value) / sizeof(value[0]), L"Invalid size");
	} else {
		swprintf(value, sizeof(value) / sizeof(value[0]), L"%ld px", height_units * unit_height);
	}
	SetWindowTextW(dialog->height_pixels, value);
}

static LRESULT CALLBACK SizeNewWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
	SizeNewDialog* dialog = (SizeNewDialog*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
	switch (msg) {
		case WM_NCCREATE:
			dialog = (SizeNewDialog*)((CREATESTRUCTW*)lparam)->lpCreateParams;
			dialog->window = hwnd;
			SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)dialog);
			return TRUE;
		case WM_CREATE:
		{
			HINSTANCE instance = GetModuleHandleW(NULL);
			CreateWindowW(L"STATIC", L"Screen mode:", WS_CHILD | WS_VISIBLE,
				16, 80, 88, 24, hwnd, NULL, instance, NULL);
			dialog->width = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
				WS_CHILD | WS_VISIBLE | ES_NUMBER, 110, 16, 80, 24, hwnd,
				(HMENU)SIZE_NEW_WIDTH, instance, NULL);
			dialog->height = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
				WS_CHILD | WS_VISIBLE | ES_NUMBER, 110, 48, 80, 24, hwnd,
				(HMENU)SIZE_NEW_HEIGHT, instance, NULL);
			dialog->width_label = CreateWindowW(L"STATIC", L"",
				WS_CHILD | WS_VISIBLE, 16, 16, 88, 24, hwnd, NULL, instance, NULL);
			dialog->height_label = CreateWindowW(L"STATIC", L"",
				WS_CHILD | WS_VISIBLE, 16, 48, 88, 24, hwnd, NULL, instance, NULL);
			SetWindowTextW(dialog->width_label, L"Width units:");
			SetWindowTextW(dialog->height_label, L"Height units:");
			dialog->width_pixels = CreateWindowW(L"STATIC", L"",
				WS_CHILD | WS_VISIBLE, 200, 16, 130, 24, hwnd, NULL, instance, NULL);
			dialog->height_pixels = CreateWindowW(L"STATIC", L"",
				WS_CHILD | WS_VISIBLE, 200, 48, 130, 24, hwnd, NULL, instance, NULL);
			dialog->units_label = CreateWindowW(L"STATIC", L"",
				WS_CHILD | WS_VISIBLE, 300, 80, 140, 24, hwnd, NULL, instance, NULL);
			dialog->mode = CreateWindowW(L"COMBOBOX", L"",
				WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 110, 80, 180, 220, hwnd,
				(HMENU)SIZE_NEW_MODE, instance, NULL);
			dialog->clear_canvas = CreateWindowW(L"BUTTON", L"Clear canvas",
				WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 16, 120, 180, 24, hwnd,
				(HMENU)SIZE_NEW_CLEAR_CANVAS, instance, NULL);
			dialog->clear_undo = CreateWindowW(L"BUTTON", L"Clear undo",
				WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 16, 148, 180, 24, hwnd,
				(HMENU)SIZE_NEW_CLEAR_UNDO, instance, NULL);
			CreateWindowW(L"BUTTON", L"Ok", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
				210, 184, 80, 28, hwnd, (HMENU)SIZE_NEW_APPLY, instance, NULL);
			CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE,
				300, 184, 80, 28, hwnd, (HMENU)SIZE_NEW_CANCEL, instance, NULL);
			for (int i = 0; i < (int)(sizeof(size_new_mode_labels) / sizeof(size_new_mode_labels[0])); ++i) {
				SendMessageW(dialog->mode, CB_ADDSTRING, 0, (LPARAM)size_new_mode_labels[i]);
			}
			wchar_t value[16];
			SendMessageW(dialog->mode, CB_SETCURSEL, dialog->app->screen_mode, 0);
			int mode = (int)SendMessageW(dialog->mode, CB_GETCURSEL, 0, 0);
			int unit_width = ScreenModeHorizontalUnit((uint8_t)mode);
			int unit_height = ScreenModeVerticalUnit((uint8_t)mode);
			swprintf(value, sizeof(value) / sizeof(value[0]), L"%d",
				(dialog->app->width + unit_width - 1) / unit_width);
			SetWindowTextW(dialog->width, value);
			swprintf(value, sizeof(value) / sizeof(value[0]), L"%d",
				(dialog->app->height + unit_height - 1) / unit_height);
			SetWindowTextW(dialog->height, value);
			UpdateSizeNewDimensions(dialog);
			return 0;
		}
		case WM_COMMAND:
			if (LOWORD(wparam) == SIZE_NEW_MODE && HIWORD(wparam) == CBN_SELCHANGE) {
				UpdateSizeNewDimensions(dialog);
			} else if ((LOWORD(wparam) == SIZE_NEW_WIDTH || LOWORD(wparam) == SIZE_NEW_HEIGHT) &&
				HIWORD(wparam) == EN_CHANGE) {
				UpdateSizeNewDimensions(dialog);
			} else if (LOWORD(wparam) == SIZE_NEW_CANCEL) {
				DestroyWindow(hwnd);
			} else if (LOWORD(wparam) == SIZE_NEW_APPLY) {
				wchar_t width_text[16], height_text[16];
				GetWindowTextW(dialog->width, width_text, sizeof(width_text) / sizeof(width_text[0]));
				GetWindowTextW(dialog->height, height_text, sizeof(height_text) / sizeof(height_text[0]));
				long width_units = wcstol(width_text, NULL, 10);
				long height_units = wcstol(height_text, NULL, 10);
				int mode = (int)SendMessageW(dialog->mode, CB_GETCURSEL, 0, 0);
				int unit_width = ScreenModeHorizontalUnit((uint8_t)mode);
				int unit_height = ScreenModeVerticalUnit((uint8_t)mode);
				int width = width_units > 0 && width_units <= MAX_CANVAS_WIDTH / unit_width ?
					(int)width_units * unit_width : 0;
				int height = height_units > 0 && height_units <= MAX_CANVAS_HEIGHT / unit_height ?
					(int)height_units * unit_height : 0;
				if (ApplyCanvasSettings(dialog->app, width, height, (uint8_t)mode,
					IsDlgButtonChecked(hwnd, SIZE_NEW_CLEAR_CANVAS) == BST_CHECKED,
					IsDlgButtonChecked(hwnd, SIZE_NEW_CLEAR_UNDO) == BST_CHECKED)) {
					dialog->accepted = true;
					DestroyWindow(hwnd);
				} else {
					MessageBoxW(hwnd, L"Canvas size or screen mode is invalid.",
						L"Size/New", MB_OK | MB_ICONERROR);
				}
			}
			return 0;
		case WM_CLOSE:
			DestroyWindow(hwnd);
			return 0;
	}
	return DefWindowProcW(hwnd, msg, wparam, lparam);
}

static void ShowSizeNewDialog(Sleber* app) {
	static const wchar_t class_name[] = L"SleberSizeNewDialog";
	static bool registered;
	if (!registered) {
		WNDCLASSEXW wc;
		ZeroMemory(&wc, sizeof(wc));
		wc.cbSize = sizeof(wc);
		wc.lpfnWndProc = SizeNewWndProc;
		wc.hInstance = GetModuleHandleW(NULL);
		wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
		wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
		wc.lpszClassName = class_name;
		if (!RegisterClassExW(&wc)) {
			return;
		}
		registered = true;
	}

	SizeNewDialog dialog = { .app = app };
	HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME, class_name, L"Size/New",
		WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 460, 255,
		app->gui->window, NULL, GetModuleHandleW(NULL), &dialog);
	if (!window) {
		return;
	}
	SetWindowTextW(window, L"Size/New");
	EnableWindow(app->gui->window, FALSE);
	ShowWindow(window, SW_SHOW);
	UpdateWindow(window);
	MSG message;
	while (IsWindow(window) && GetMessageW(&message, NULL, 0, 0) > 0) {
		if (!IsDialogMessageW(window, &message)) {
			TranslateMessage(&message);
			DispatchMessageW(&message);
		}
	}
	EnableWindow(app->gui->window, TRUE);
	SetActiveWindow(app->gui->window);
}

static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
	Sleber* app = (Sleber*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

	switch (msg) {
		case WM_CREATE:
		{
			CREATESTRUCTW* cs = (CREATESTRUCTW*)lparam;
			SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
			DragAcceptFiles(hwnd, TRUE);
			return 0;
		}
		case WM_CLOSE:
			if (!StopRedrawThread(app)) {
				return 0;
			}
			break;
		case WM_TIMER:
		{
//			InvalidateRect(hwnd, NULL, TRUE); // Invalidate the entire client area
			break;
		}
		case WM_ERASEBKGND:
			return 1; /* WM_PAINT below always fills the whole canvas area */
		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hwnd, &ps);

			LockMutex(&app->redraw_mutex);
			BitmapType canvas_bitmap = GetDrawBitmap();

			if (app && BitmapValid(canvas_bitmap)) {
				RECT status = { 0 };
				GetWindowRect(app->gui->status_label, &status);

				RECT rect;
				GetClientRect(hwnd, &rect);

				int w = rect.right - rect.left;
				int h = rect.bottom - rect.top - (status.bottom - status.top);

				HDC hdcMem = CreateCompatibleDC(hdc);
				HGDIOBJ old = SelectObject(hdcMem, canvas_bitmap.bitmap);

				Area dirty_rect = ClampArea(app->canvas_bitmap_dirty_rect, ToArea(0, 0, w, h));
				if (!dirty_rect.w || !dirty_rect.h) {
					dirty_rect = ToArea(0, 0, w, h);
				}
				if (dirty_rect.w && dirty_rect.h) {
					BitBlt(hdc, dirty_rect.x, dirty_rect.y,
						dirty_rect.w, dirty_rect.h, hdcMem,
						dirty_rect.x, dirty_rect.y, SRCCOPY);
				}

				SelectObject(hdcMem, old);
				DeleteDC(hdcMem);
			}
			UnlockMutex(&app->redraw_mutex);


			// ------
			// SELECTION OUTLINE
			if (app && app->selection_mode != SELECT_MODE_NONE && app->select_w !=0 && app->select_h != 0) {

				LOGBRUSH brush = {
					BS_SOLID,
					RGB(255, 255, 255),
					0
				};
				LOGBRUSH brush_black = {
					BS_SOLID,
					RGB(0, 0, 0),
					0
				};

				static int count = 0;
				++count;
				DWORD pattern[] = {
					(count & 4) ? ((count & 3) + 1) : 0,
					(count & 4) ? 4 : ((count & 3) + 1),
					(count & 4) ? (~count & 3) : 4,
					(count & 4) ? 0 : (~count & 3) };

				DWORD pattern_black[] = {
					(count & 4) ? 0 : ((count & 3) + 1),
					(count & 4) ? ((count & 3) + 1) : 4,
					(count & 4) ? 4 : (~count & 3),
					(count & 4) ? (~count & 3) : 0 };

				HPEN hPen = ExtCreatePen(
					PS_GEOMETRIC | PS_USERSTYLE | PS_ENDCAP_FLAT,
					1,
					&brush,
					ARRAYSIZE(pattern),
					pattern
				);

				HPEN hPenBlack = ExtCreatePen(
					PS_GEOMETRIC | PS_USERSTYLE | PS_ENDCAP_FLAT,
					1,
					&brush_black,
					ARRAYSIZE(pattern_black),
					pattern_black
				);

				int sc = PixelSize(app),
					ox = app->offset_x,
					oy = app->offset_y;
				int x = app->select_x * sc + ox, y = app->select_y * sc + oy, w = app->select_w * sc, h = app->select_h * sc;
				if (w < 0) { x += w; w = -w; }
				if (h < 0) { y += h; h = -h; }

				HPEN oldPen = (HPEN)SelectObject(hdc, hPen);
				MoveToEx(hdc, x, y, NULL);
				LineTo(hdc, x+w, y);
				LineTo(hdc, x+w, y+h);
				LineTo(hdc, x, y+h);
				LineTo(hdc, x, y);

				HPEN oldPenBlack = (HPEN)SelectObject(hdc, hPenBlack);
				LineTo(hdc, x + w, y);
				LineTo(hdc, x + w, y + h);
				LineTo(hdc, x, y + h);
				LineTo(hdc, x, y);

				SelectObject(hdc, oldPen);
				SelectObject(hdc, oldPenBlack);
				DeleteObject(hPen);
				DeleteObject(hPenBlack);



			}
			// ------


			EndPaint(hwnd, &ps);
			return 0;
		}
		case WM_SIZE:
		{
			if (!app) {
				break;
			}
			app->draw_window_width = LOWORD(lparam);
			app->draw_window_height = HIWORD(lparam) - STATUS_HEIGHT;
			if (app->gui->status_label) {
				RECT client;
				GetClientRect(hwnd, &client);
				MoveWindow(app->gui->status_label, 0, client.bottom - STATUS_HEIGHT,
					client.right, STATUS_HEIGHT, TRUE);

				if (app->gui->palette_window && app->palette_docked) {
					int width = app->palette_window_width;
					int height = app->palette_window_height;
					MoveWindow(
						app->gui->palette_window,
						app->draw_window_width - width,
						0,
						width,
						height,
						TRUE
					);
				}

				RequestRedraw(app);
			}
			return 0;
		}
		case WM_LBUTTONDOWN:
		case WM_RBUTTONDOWN:
		{
			if (!app) {
				break;
			}
			int px = GET_X_LPARAM(lparam);
			int py = GET_Y_LPARAM(lparam);
			int step = PixelSize(app);
			I32Point p = SnapToGrid(app, (I32Point) { (px - app->offset_x) / step, (py - app->offset_y) / step });

			UpdateMouseDown(app, (I32Point) { px, py }, msg == WM_RBUTTONDOWN);
			app->dragging = msg == WM_RBUTTONDOWN ? MOUSE_BUTTON_RIGHT : MOUSE_BUTTON_LEFT;
			app->drag_last_x = p.x;
			app->drag_last_y = p.y;
			SetCapture(hwnd);

			if (app->dragging) { RequestRedraw(app); }

			SetFocus(hwnd);
			return 0;
		}
		case WM_MBUTTONDOWN:
			app->dragging = MOUSE_BUTTON_MIDDLE;;
			app->drag_last_x = GET_X_LPARAM(lparam);
			app->drag_last_y = GET_Y_LPARAM(lparam);
			break;

		case WM_MBUTTONUP:
			if (app && app->paste_mode == PASTE_MODE_PASTED) {
				SetPasteMode(app, PASTE_MODE_NONE);
			}
			if (app && app->dragging == MOUSE_BUTTON_MIDDLE) {
				app->dragging = MOUSE_BUTTON_NONE;
			}
			break;

		case WM_LBUTTONUP: {
			if (app){
				if (app->paste_mode == PASTE_MODE_PASTED) {
					SetPasteMode(app, PASTE_MODE_NONE);
				}
				if (app->dragging == MOUSE_BUTTON_LEFT) {
					app->dragging = MOUSE_BUTTON_NONE;
					app->mouse_drawn = false;
					ReleaseCapture();
				}
				app->fill_mode = false;
				if(app->selection_mode == SELECT_MODE_RECTANGLE_DRAG) {
					app->selection_mode = SELECT_MODE_RECTANGLE;
				}
			}
			return 0;
		}
		case WM_RBUTTONUP: {
			if (app) {
				if (app && app->paste_mode == PASTE_MODE_PASTED) {
					SetPasteMode(app, PASTE_MODE_NONE);
				}
				if (app->dragging == MOUSE_BUTTON_RIGHT) {
					app->dragging = MOUSE_BUTTON_NONE;
					app->mouse_drawn = false;
					ReleaseCapture();
				}
				app->fill_mode = false;
			}
			return 0;
		}

		case WM_MOUSEMOVE: {
			UICanvasMouseMove(app, (I32Point) { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) });
			return 0;
		}
		case WM_MOUSEWHEEL:
		{
			if (!app) {
				break;
			}
			int old_size = PixelSize(app);
			int delta = GET_WHEEL_DELTA_WPARAM(wparam);
			int zoom_step = (delta / 120) * 8;
			if (zoom_step == 0 && delta != 0) {
				zoom_step = delta > 0 ? 8 : -8;
			}
			int new_zoom = app->zoom + zoom_step;
			if (new_zoom > MaxZoomValue()) new_zoom = MaxZoomValue();
			if (new_zoom < MinZoomValue()) new_zoom = MinZoomValue();
			app->zoom = new_zoom;
			if (PixelSize(app) != old_size) {
				POINT pt = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
				ScreenToClient(hwnd, &pt);
				double world_x = (pt.x - app->offset_x) / (double)old_size;
				double world_y = (pt.y - app->offset_y) / (double)old_size;
				int step = PixelSize(app);
				app->offset_x = (int)(pt.x - world_x * step);
				app->offset_y = (int)(pt.y - world_y * step);
				RequestRedraw(app);
			}
			return 0;
		}
		case WM_KEYDOWN:
		{
			if (!app) {
				break;
			}
			if (!(GetKeyState(VK_CONTROL) & 0x8000)) {
				if (wparam >= '0' && wparam <= '9') {
					if(GetKeyState(VK_SHIFT) & 0x8000) {
						SetBackgroundColor(app, (int)(wparam - '0'));
					} else {
						SetForegroundColor(app, (int)(wparam - '0'));
					}
					RequestRedrawPalette(app);
					return 0;
				}
				if (wparam >= 'A' && wparam <= 'F') {
					if(GetKeyState(VK_SHIFT) & 0x8000) {
						SetBackgroundColor(app, (int)(wparam - 'A' + 10));
					} else {
						SetForegroundColor(app, (int)(wparam - 'A' + 10));
					}
					RequestRedrawPalette(app);
					return 0;
				}
			}
			switch (wparam) {
				case VK_LEFT:
					app->offset_x += 16;
					RequestRedraw(app);
					break;
				case VK_RIGHT:
					app->offset_x -= 16;
					RequestRedraw(app);
					break;
				case VK_UP:
					app->offset_y += 16;
					RequestRedraw(app);
					break;
				case VK_DOWN:
					app->offset_y -= 16;
					RequestRedraw(app);
					break;
				case VK_ESCAPE:
					app->fill_mode = false;
					app->selection_mode = SELECT_MODE_NONE;
					app->draw_pixel_info = false;
					app->draw_pixel_prev = false;
					app->paste_mode = PASTE_MODE_NONE;
					RequestRedraw(app);
					break;
				case VK_DELETE:
					if (app->selection_mode != SELECT_MODE_NONE) {
						FillSelection(app, app->background_color);
						RequestRedraw(app);
					}
					break;
				case 'A':
					if (GetKeyState(VK_CONTROL) & 0x8000) {
						SelectAll(app);
						RequestRedraw(app);
					}
					break;
				case 'C':
					if (GetKeyState(VK_CONTROL) & 0x8000) {
						if (app->selection_mode != SELECT_MODE_NONE) {
							CopySelectionToBrush(app, true);
							BrushToClipboard(app, hwnd);
							RequestRedraw(app);
						}
					}
					break;
				case 'G':
					app->selection_mode = SELECT_MODE_NONE;
					app->fill_mode = true;
					break;
				case 'M':
					StartSelection(app);
					break;
				case 'O':
				case 'L':
					if (GetKeyState(VK_CONTROL) & 0x8000) {
						char* path = RunCanvasFileDialog(app, FALSE, NULL);
						if (path) {
							OpenImage(app, path);
							free(path);
						}
					}
					break;
				case 'I':
					if (GetKeyState(VK_CONTROL) & 0x8000) {
						char* path = RunImportFileDialog(app);
						if (path) {
							ImportImageFile(app, path);
							free(path);
						}
					}
					break;
				case 'S':
					if (GetKeyState(VK_CONTROL) & 0x8000) {
						if (!(GetKeyState(VK_SHIFT) & 0x8000) || !SaveCurrentFile(app)) {
							char* path = RunCanvasFileDialog(app, TRUE, NULL);
							if (path) {
								SaveImage(app, path);
								free(path);
							}
						}
					}
					break;
				case 'T':
					app->draw_pixel_info = true;
					RequestRedraw(app);
					break;
				case 'U':
					if ((GetKeyState(VK_SHIFT) & 0x8000) && (GetKeyState(VK_CONTROL) & 0x8000)) {
						DoUndo(app, true, true);
						RequestRedraw(app);
					} else if (GetKeyState(VK_CONTROL) & 0x8000) {
						DoUndo(app, true, false);
						RequestRedraw(app);
					} else {
						app->draw_pixel_prev = true;
						RequestRedraw(app);
					}
					break;
				case 'V':
					if (GetKeyState(VK_CONTROL) & 0x8000) {
						if (ClipboardBitmapToBrush(app, hwnd)) {
							SetPasteMode(app, (GetKeyState(VK_SHIFT) & 0x8000) ? PASTE_MODE_SKIP_BG_COLOR : PASTE_MODE_OVERWRITE);
							RequestDrawBrush(app, (I32Point) { app->mouse_x, app->mouse_y });
						} else {
							SetPasteMode(app, PASTE_MODE_NONE);
							UpdateStatus(app, "Unable to paste an image from the clipboard.");
						}
						return 0;
					}
					break;
				case 'X':
					if (GetKeyState(VK_CONTROL) & 0x8000) {
						if (app->selection_mode != SELECT_MODE_NONE) {
							CopySelectionToBrush(app, false);
							BrushToClipboard(app, hwnd);
							FillSelection(app, app->background_color);
							RequestRedraw(app);
						}
					}
					break;
				case 'Z':
					if ((GetKeyState(VK_SHIFT) & 0x8000) && (GetKeyState(VK_CONTROL) & 0x8000)) {
						DoUndo(app, false, true);
						RequestRedraw(app);
					} else if (GetKeyState(VK_CONTROL) & 0x8000) {
						DoUndo(app, false, false);
						RequestRedraw(app);
					}
					return 0;
				default:
					return DefWindowProcW(hwnd, msg, wparam, lparam);
			}
			return 0;
		}
		case WM_KEYUP: {
			if (!app) {
				break;
			}
			switch (wparam) {
				case 'T':
					app->draw_pixel_info = false;
					RequestRedraw(app);
					return 0;
				case 'U':
					if (!(GetKeyState(VK_SHIFT) & 0x8000) && !(GetKeyState(VK_CONTROL) & 0x8000)) {
						app->draw_pixel_prev = false;
						RequestRedraw(app);
					}
					break;
				default:
					return DefWindowProcW(hwnd, msg, wparam, lparam);
			}
			return 0;
		}
		case WM_DROPFILES:
		{
			HDROP drop = (HDROP)wparam;
			UINT file_count = DragQueryFileW(drop, 0xFFFFFFFF, NULL, 0);
			for (UINT i = 0; i < file_count; ++i) {
				UINT path_length = DragQueryFileW(drop, i, NULL, 0);
				wchar_t* path = (wchar_t*)malloc(sizeof(*path) * ((size_t)path_length + 1));
				if (!path) {
					UpdateStatus(app, "Unable to allocate dropped file path.");
					continue;
				}
				if (DragQueryFileW(drop, i, path, path_length + 1)) {
					char* path_utf8 = WideToUTF8(path);
					if (path_utf8) {
						OpenDroppedFile(app, path_utf8);
						free(path_utf8);
					} else {
						UpdateStatus(app, "Unable to convert dropped file path.");
					}
				}
				free(path);
			}
			DragFinish(drop);
			return 0;
		}
		case WM_COMMAND:
		{
			if (!app) {
				break;
			}
			switch (LOWORD(wparam)) {
				case IDM_FILE_OPEN:
				{
					char* path = RunCanvasFileDialog(app, FALSE, NULL);
					if (path) {
						OpenImage(app, path);
						free(path);
					}
					return 0;
				}
				case IDM_FILE_OPEN_SBX:
				{
					char* path = RunFileDialog(app, FALSE, L"Open SBX",
						L"SBX Files\0*.sbx\0All Files\0*.*\0\0", NULL);
					if (path) {
						LoadSbxFile(app, path);
						free(path);
					}
					return 0;
				}
				case IDM_FILE_IMPORT:
				{
					char* path = RunImportFileDialog(app);
					if (path) {
						ImportImageFile(app, path);
						free(path);
					}
					return 0;
				}
				case IDM_FILE_EXPORT:
				{
					char* path = RunFileDialog(app, TRUE, L"Export Image",
						L"PNG Image\0*.png\0All Files\0*.*\0\0", L"bitmap.png");
					if (path) {
						ExportImageToFile(app, path);
						free(path);
					}
					return 0;
				}
				case IDM_FILE_SAVE:
				{
					char* path = RunCanvasFileDialog(app, TRUE, NULL);
					if (path) {
						SaveImage(app, path);
						free(path);
					}
					return 0;
				}
				case IDM_FILE_SAVE_SBX:
				{
					char* path = RunFileDialog(app, TRUE, L"Save SBX",
						L"SBX Files\0*.sbx\0All Files\0*.*\0\0", L"bitmap.sbx");
					if (path) {
						SaveSbxFile(app, path);
						free(path);
					}
					return 0;
				}
				case IDM_FILE_SAVE_CURRENT:
				{
					if (!SaveCurrentFile(app)) {
						char* path = RunCanvasFileDialog(app, TRUE, NULL);
						if (path) {
							SaveImage(app, path);
							free(path);
						}
					}
					return 0;
				}
				case IDM_FILE_QUIT:
					PostMessageW(hwnd, WM_CLOSE, 0, 0);
					return 0;
				case IDM_HELP_ABOUT:
					MessageBoxW(hwnd,
						L"A small bitmap editor for Commodore 64 graphic modes.\n"
						L"Sleber \xA9 2026 Carl-Henrik Sk\xE5rstedt\n"
						L"\n"
						L"Left button: draw with the foreground color\n"
						L"Right button: draw with the background color\n"
						L"Middle button: pan the canvas\n"
						L"Mouse wheel: zoom in or out\n"
						L"0-9, A-F: choose foreground color\n"
						L"Shift + 0-9, A-F: choose background color\n"
						L"Arrow keys: pan the canvas\n"
						L"Escape: cancel the current operation\n"
						L"Delete: fill the selection with the background color\n"
						L"M: start a selection, G: fill mode\n"
						L"T: show pixel information while held\n"
						L"U: show previous pixels while held\n"
						L"Ctrl+A: select all\n"
						L"Ctrl+S: Save the current file\n"
						L"Ctrl+Shift+S: Save file as\n"
						L"Ctrl+L or Ctrl+O: open image file\n"
						L"Ctrl+C / Ctrl+X: copy or cut the selection\n"
						L"Ctrl+V: paste brush\n"
						L"Shift+Ctrl+V: paste while skipping background color\n"
						L"Ctrl+Z / Ctrl+Shift+Z: undo / redo a drawing step\n"
						L"Ctrl+U / Ctrl+Shift+U: undo / redo one frame's edit\n"
						L"Ctrl+I: import image\n",
						L"About Sleber pixel editor", MB_OK | MB_ICONINFORMATION);
					return 0;

				case IDM_MODE_CHAR:
					SwitchScreenMode(app, SCREEN_MODE_CHAR);
					return 0;
				case IDM_MODE_CHAR_MC:
					SwitchScreenMode(app, SCREEN_MODE_CHAR_MULTICOLOR);
					return 0;
				case IDM_MODE_BITMAP:
					SwitchScreenMode(app, SCREEN_MODE_BITMAP);
					return 0;
				case IDM_MODE_BITMAP_MC:
					SwitchScreenMode(app, SCREEN_MODE_BITMAP_MULTICOLOR);
					return 0;
				case IDM_MODE_SPRITE:
					SwitchScreenMode(app, SCREEN_MODE_SPRITE);
					return 0;
				case IDM_MODE_SPRITE_MC:
					SwitchScreenMode(app, SCREEN_MODE_SPRITE_MULTICOLOR);
					return 0;
				case IDM_MODE_CHAR_ECBM:
					SwitchScreenMode(app, SCREEN_MODE_ECBM);
					return 0;
				case IDM_MODE_CHAR_MIXED:
					SwitchScreenMode(app, SCREEN_MODE_CHAR_MIXED);
					return 0;
				case IDM_MODE_UNRESTRICTED:
					SwitchScreenMode(app, SCREEN_MODE_UNRESTRICTED);
					return 0;
				case IDM_MODE_UNRESTRICTED_WIDE:
					SwitchScreenMode(app, SCREEN_MODE_WIDE_UNRESTRICTED);
					return 0;
				case IDM_SIZE_NEW:
					ShowSizeNewDialog(app);
					return 0;
				case IDM_SNAP_TO_GRID:
					app->snap_to_grid = !app->snap_to_grid;
					CheckMenuItem(GetMenu(hwnd), IDM_SNAP_TO_GRID,
						MF_BYCOMMAND | (app->snap_to_grid ? MF_CHECKED : MF_UNCHECKED));
					return 0;
				case IDM_DOCK_PALETTE:
					if (app->palette_docked) { UndockPalette(app); }
					else { DockPalette(app); }
					return 0;
				case IDM_DITHER_OFF:
				case IDM_DITHER_LOW:
				case IDM_DITHER_MEDIUM:
				case IDM_DITHER_HIGH:
					app->import_image_dither = (uint8_t)(LOWORD(wparam) - IDM_DITHER_OFF);
					CheckMenuRadioItem(GetMenu(hwnd), IDM_DITHER_OFF, IDM_DITHER_HIGH,
						LOWORD(wparam), MF_BYCOMMAND);
					return 0;
				default:
					break;
			}
			break;
		}
		case WM_DESTROY:
			DragAcceptFiles(hwnd, FALSE);
			PostQuitMessage(0);
			return 0;
		default:
			break;
	}
	return DefWindowProcW(hwnd, msg, wparam, lparam);
}

static void BuildMenuBar(HWND window) {
	HMENU menubar = CreateMenu();
	HMENU file_menu = CreatePopupMenu();

	AppendMenuW(file_menu, MF_STRING, IDM_FILE_OPEN, L"Open\tCtrl+O / Ctrl+L");
	AppendMenuW(file_menu, MF_STRING, IDM_FILE_SAVE_CURRENT, L"Save\tCtrl+S");
	AppendMenuW(file_menu, MF_STRING, IDM_FILE_SAVE, L"Save As\tCtrl+Shift+S");
	AppendMenuW(file_menu, MF_STRING, IDM_FILE_IMPORT, L"Import Image\tCtrl+I");
	AppendMenuW(file_menu, MF_STRING, IDM_FILE_EXPORT, L"Export Image");
	AppendMenuW(file_menu, MF_SEPARATOR, 0, NULL);
	AppendMenuW(file_menu, MF_STRING, IDM_FILE_QUIT, L"Quit");

	AppendMenuW(menubar, MF_POPUP, (UINT_PTR)file_menu, L"File");

	HMENU edit_menu = CreatePopupMenu();

	HMENU mode_menu = CreatePopupMenu();
	AppendMenu(mode_menu, MF_STRING, IDM_MODE_CHAR, L"Char");
	AppendMenu(mode_menu, MF_STRING, IDM_MODE_CHAR_MC, L"Char MC");
	AppendMenu(mode_menu, MF_STRING, IDM_MODE_BITMAP, L"Bitmap");
	AppendMenu(mode_menu, MF_STRING, IDM_MODE_BITMAP_MC, L"Bitmap MC");
	AppendMenu(mode_menu, MF_STRING, IDM_MODE_SPRITE, L"Sprite");
	AppendMenu(mode_menu, MF_STRING, IDM_MODE_SPRITE_MC, L"Sprite MC");
	AppendMenu(mode_menu, MF_STRING, IDM_MODE_CHAR_ECBM, L"Char ECBM");
	AppendMenu(mode_menu, MF_STRING, IDM_MODE_CHAR_MIXED, L"Char Mixed");
	AppendMenu(mode_menu, MF_STRING, IDM_MODE_UNRESTRICTED, L"Unrestricted");
	AppendMenu(mode_menu, MF_STRING, IDM_MODE_UNRESTRICTED_WIDE, L"Unrestricted Wide");

	AppendMenu(edit_menu, MF_POPUP, (UINT_PTR)mode_menu, L"Mode");
	AppendMenuW(edit_menu, MF_STRING, IDM_SIZE_NEW, L"Size/New");
	AppendMenuW(edit_menu, MF_STRING, IDM_SNAP_TO_GRID, L"Snap to Grid");
	AppendMenuW(edit_menu, MF_STRING | MF_CHECKED, IDM_DOCK_PALETTE, L"Dock Palette");
	HMENU dither_menu = CreatePopupMenu();
	AppendMenuW(dither_menu, MF_STRING, IDM_DITHER_OFF, L"Off");
	AppendMenuW(dither_menu, MF_STRING, IDM_DITHER_LOW, L"Low");
	AppendMenuW(dither_menu, MF_STRING | MF_CHECKED, IDM_DITHER_MEDIUM, L"Medium");
	AppendMenuW(dither_menu, MF_STRING, IDM_DITHER_HIGH, L"High");
	AppendMenuW(edit_menu, MF_POPUP, (UINT_PTR)dither_menu, L"Import Image Dither");
	AppendMenuW(menubar, MF_POPUP, (UINT_PTR)edit_menu, L"Edit");

	HMENU help_menu = CreatePopupMenu();
	AppendMenuW(help_menu, MF_STRING, IDM_HELP_ABOUT, L"About");
	AppendMenuW(menubar, MF_POPUP, (UINT_PTR)help_menu, L"Help");

	SetMenu(window, menubar);
}

static void BuildPaletteWindow(Sleber* app, HINSTANCE instance) {
	static const wchar_t class_name[] = L"SleberPaletteWindow";
	WNDCLASSEXW wc;
	ZeroMemory(&wc, sizeof(wc));
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = PaletteWndProc;
	wc.hInstance = instance;
	wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wc.lpszClassName = class_name;
	RegisterClassExW(&wc);

	int width = 2 * PALETTE_SQUARE + PALETTE_GAP;
	int height = 8 * PALETTE_SQUARE + 7 * PALETTE_GAP + PALETTE_SHARED_COLOR_HEIGHT;

	app->palette_window_width = width;
	app->palette_window_height = height;

	RECT wr = { 0, 0, width, height }; // Desired client area size
	AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE); // Adjust for window borders

	app->palette_window_width_outer = wr.right - wr.left;
	app->palette_window_height_outer = wr.bottom - wr.top;

	app->gui->palette_window = CreateWindowExW(
		WS_EX_TOOLWINDOW, class_name, L"Palette",
		WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
		CW_USEDEFAULT, CW_USEDEFAULT, wr.right - wr.left, wr.bottom - wr.top,
		app->gui->window, NULL, instance, app);

	ShowWindow(app->gui->palette_window, SW_SHOW);
}

static void BuildColorPickerWindow(Sleber* app, HINSTANCE instance) {
	static const wchar_t class_name[] = L"SleberColorPickerWindow";
	WNDCLASSEXW wc;
	ZeroMemory(&wc, sizeof(wc));
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = ColorPickerWndProc;
	wc.hInstance = instance;
	wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wc.lpszClassName = class_name;
	RegisterClassExW(&wc);

	app->gui->color_picker_window = CreateWindowExW(
		WS_EX_TOPMOST, class_name, L"Color Picker",
		WS_POPUP,
		CW_USEDEFAULT, CW_USEDEFAULT, COLOR_PICKER_WIDTH, COLOR_PICKER_HEIGHT,
		app->gui->window, NULL, instance, app);

	ShowWindow(app->gui->color_picker_window, SW_HIDE);
}

int WINAPI WinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE prev_instance, _In_ LPSTR cmd_line, _In_ int show_cmd) {
	(void)prev_instance;
	(void)cmd_line;
	if (FAILED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) {
		MessageBoxW(NULL, L"Failed to initialize Windows dialog support.", L"Sleber", MB_ICONERROR);
		return 1;
	}

	Sleber* app = AppNew();
	if (!app || !app->pixels || !app->pixels_info || !app->pixels_prev || !app->gui) {
		MessageBoxW(NULL, L"Failed to initialize editor state.", L"Sleber", MB_ICONERROR);
		AppFree(app);
		CoUninitialize();
		return 1;
	}

	InitBitmapPalette();
	SetScreenMode(app, SCREEN_MODE_BITMAP);
	StartRedrawThread(app);

	static const wchar_t class_name[] = L"SleberMainWindow";
	WNDCLASSEXW wc;
	ZeroMemory(&wc, sizeof(wc));
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = MainWndProc;
	wc.hInstance = instance;
	wc.hIcon = LoadIconW(instance, L"IDI_ICON1");
	wc.hIconSm = LoadIconW(instance, L"IDI_ICON1");
	wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wc.lpszClassName = class_name;
	RegisterClassExW(&wc);

	RECT rect = { 0, 0, (app->width + STARTUP_HORIZONTAL_BORDER*2) * PixelSize(app),
		(app->height + STARTUP_VERTICAL_BORDER*2) * PixelSize(app) + STATUS_HEIGHT };
	AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, TRUE);

	app->gui->window = CreateWindowExW(
		0, class_name, L"Sleber Pixel Editor", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
		CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
		NULL, NULL, instance, app);

	if (!app->gui->window) {
		AppFree(app);
		CoUninitialize();
		return 1;
	}

	BuildMenuBar(app->gui->window);

	RECT client;
	GetClientRect(app->gui->window, &client);
	app->gui->status_label = CreateWindowExW(
		0, L"STATIC", L"Ready", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
		0, client.bottom - STATUS_HEIGHT, client.right, STATUS_HEIGHT,
		app->gui->window, NULL, instance, NULL);

	BuildPaletteWindow(app, instance);
	DockPalette(app);

	BuildColorPickerWindow(app, instance);
	RequestRedrawColorPicker(app);

	RequestRedraw(app);
	RequestRedrawPalette(app);

	ShowWindow(app->gui->window, show_cmd);
	UpdateWindow(app->gui->window);

	MSG msg;
	while (GetMessageW(&msg, NULL, 0, 0) > 0) {
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}

	AppFree(app);
	CoUninitialize();
	return (int)msg.wParam;
}

PerfTimerType GetPerfCounter(void) {
	LARGE_INTEGER timer;
	QueryPerformanceCounter(&timer);
	return timer;
}

double PerfCounterToSeconds(PerfTimerType start, PerfTimerType end) {
	LARGE_INTEGER freq;
	QueryPerformanceFrequency(&freq);

	return (double)(end.QuadPart - start.QuadPart) / (double)freq.QuadPart;
}

#endif