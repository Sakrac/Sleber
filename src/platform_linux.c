#ifdef __linux__
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>

#include "stb/stb_image.h"
#include "stb/stb_image_write.h"

#include "platform.h"

#include "Edit.h"
#include "files.h"
#include "gpx_load.h"
#include "palette.h"
#include "sbx_file.h"
#include "sleber.h"
#include "convert.h"

struct PlatformGui {
    GtkWidget *window;
    GtkWidget *content_box;
    GtkWidget *drawing_area;
    GtkWidget *status_label;
    GtkWidget *palette_window;
    GtkWidget *palette_area;
    GtkWidget *palette_host;
    GtkWidget *dock_palette_item;
    GtkWidget *color_picker_window;
    GtkWidget *color_picker_area;
};

static void OnOpenActivate(GtkMenuItem *menuitem, gpointer user_data);
static void OnSaveSBXActivate(GtkMenuItem *menuitem, gpointer user_data);
static void OnImportActivate(GtkMenuItem *menuitem, gpointer user_data);

static void AddFileChooserFilter(GtkWidget *dialog, const char *name,
    const char *const *patterns, const char *default_extension) {
    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, name);
    if (default_extension) {
        g_object_set_data(G_OBJECT(filter), "default-extension", (gpointer)default_extension);
    }
    for (size_t i = 0; patterns[i]; ++i) {
        gtk_file_filter_add_pattern(filter, patterns[i]);
    }
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
}

static char *AddDefaultCanvasExtension(GtkFileChooser *chooser, char *filename) {
    GtkFileFilter *filter = gtk_file_chooser_get_filter(chooser);
    const char *extension = filter ?
        (const char *)g_object_get_data(G_OBJECT(filter), "default-extension") : NULL;
    if (!extension || !filename) {
        return filename;
    }

    const char *separator = strrchr(filename, G_DIR_SEPARATOR);
    const char *dot = strrchr(filename, '.');
    if (dot && (!separator || dot > separator + 1)) {
        return filename;
    }

    char *result = g_strconcat(filename, extension, NULL);
    g_free(filename);
    return result;
}

static void BrushToClipboard(Sleber *app) {
    if (!app->brush || app->brush_w <= 0 || app->brush_h <= 0) {
        return;
    }

    GdkPixbuf *pixbuf = gdk_pixbuf_new(
        GDK_COLORSPACE_RGB, TRUE, 8, app->brush_w, app->brush_h);
    if (!pixbuf) {
        UpdateStatus(app, "Unable to allocate clipboard image.");
        return;
    }

    int rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    guchar *pixels = gdk_pixbuf_get_pixels(pixbuf);
    for (int y = 0; y < app->brush_h; ++y) {
        guchar *row = pixels + (size_t)y * rowstride;
        for (int x = 0; x < app->brush_w; ++x) {
            uint8_t color = app->brush[(size_t)y * app->brush_w + x];
            guchar *pixel = row + (size_t)x * 4;
            if (color == 0xff) {
                pixel[0] = pixel[1] = pixel[2] = pixel[3] = 0;
            } else {
                GetPaletteColor(color, &pixel[0], &pixel[1], &pixel[2]);
                pixel[3] = 0xff;
            }
        }
    }

    gtk_clipboard_set_image(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), pixbuf);
    g_object_unref(pixbuf);
}

static bool ClipboardBitmapToBrush(Sleber *app) {
    GdkPixbuf *pixbuf = gtk_clipboard_wait_for_image(
        gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));
    if (!pixbuf) {
        return false;
    }

    const int width = gdk_pixbuf_get_width(pixbuf);
    const int height = gdk_pixbuf_get_height(pixbuf);
    const int channels = gdk_pixbuf_get_n_channels(pixbuf);
    const int rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    const bool has_alpha = gdk_pixbuf_get_has_alpha(pixbuf);
    const guchar *pixels = gdk_pixbuf_read_pixels(pixbuf);
    if (width <= 0 || height <= 0 || channels < 3 ||
        (size_t)width > SIZE_MAX / (size_t)height / sizeof(uint32_t)) {
        g_object_unref(pixbuf);
        return false;
    }

    const size_t pixel_count = (size_t)width * (size_t)height;
    uint32_t *argb = (uint32_t *)malloc(pixel_count * sizeof(*argb));
    if (!argb) {
        g_object_unref(pixbuf);
        UpdateStatus(app, "Unable to allocate clipboard image.");
        return false;
    }
    for (int y = 0; y < height; ++y) {
        const guchar *row = pixels + (size_t)y * rowstride;
        for (int x = 0; x < width; ++x) {
            const guchar *pixel = row + (size_t)x * channels;
            const uint8_t alpha = has_alpha ? pixel[3] : 0xff;
            const size_t index = (size_t)y * width + x;
            argb[index] = (uint32_t)alpha << 24 |
                (uint32_t)pixel[0] << 16 |
                (uint32_t)pixel[1] << 8 |
                pixel[2];
        }
    }

    const bool success = SetBrushFromARGB(app, argb, width, height);
    g_object_unref(pixbuf);
    return success;
}

/* ---- GUI app data ---- */

PlatformGui* CreateGuiData() {
	PlatformGui* gui = (PlatformGui*)calloc(1, sizeof(PlatformGui));
	return gui;
}

void DestroyGuiData(PlatformGui* gui) {
    if (!gui) {
        return;
    }
    free(gui);
}

BitmapType CreateUIBitmap(int width, int height, void **pixels, WindowContext context) {
    (void)context; // unused parameter
    void *pixels_mem = calloc(width * height * sizeof(uint32_t), 1);
    if (!pixels_mem) {
        return (BitmapType){0};
    }

    *pixels = pixels_mem;
    return (BitmapType){
        .pPixels = (uint32_t *)pixels_mem,
        .width = (uint16_t)width,
        .height = (uint16_t)height
    };
}

WindowContext GetWindowContext(Sleber* app) {
	return app->gui->window;
}

WindowContext GetPaletteContext(Sleber* app) {
	return app->gui->palette_window;
}

WindowContext GetColorPickerContext(Sleber* app) {
    return app->gui->color_picker_window;
}

void ReleaseWindowContext(Sleber* app, WindowContext context) {
    (void)app; // unused parameter
    (void)context; // unused parameter
}

struct DrawInvalidation {
    GtkWidget *widget;
    Area area;
};

static gboolean QueueDrawOnMainContext(gpointer user_data) {
    struct DrawInvalidation *invalidation = (struct DrawInvalidation *)user_data;
    if (!gtk_widget_in_destruction(invalidation->widget) &&
        invalidation->area.w && invalidation->area.h) {
        gtk_widget_queue_draw_area(
            invalidation->widget,
            invalidation->area.x,
            invalidation->area.y,
            invalidation->area.w,
            invalidation->area.h);
    } else if (!gtk_widget_in_destruction(invalidation->widget)) {
        gtk_widget_queue_draw(invalidation->widget);
    }
    return G_SOURCE_REMOVE;
}

static void ReleaseDrawInvalidation(gpointer user_data) {
    struct DrawInvalidation *invalidation = (struct DrawInvalidation *)user_data;
    g_object_unref(invalidation->widget);
    free(invalidation);
}

static void QueueWidgetDraw(GtkWidget *widget, Area area) {
    if (!widget) {
        return;
    }

    struct DrawInvalidation *invalidation = (struct DrawInvalidation *)malloc(sizeof(*invalidation));
    if (!invalidation) {
        fprintf(stderr, "Failed to queue GTK redraw: out of memory.\n");
        return;
    }
    invalidation->widget = g_object_ref(widget);
    invalidation->area = area;

    if (!g_idle_add_full(
        G_PRIORITY_DEFAULT,
        QueueDrawOnMainContext,
        invalidation,
        ReleaseDrawInvalidation)) {
        ReleaseDrawInvalidation(invalidation);
        fprintf(stderr, "Failed to queue GTK redraw.\n");
    }
}

void InvalidateDraw(Sleber *app, Area area) {
    if (!app || !app->gui) {
        return;
    }
    QueueWidgetDraw(app->gui->drawing_area, area);
}

void InvalidatePalette(Sleber *app, Area area) {
    if (!app || !app->gui) {
        return;
    }
    QueueWidgetDraw(app->gui->palette_area, area);
}

void InvalidateColorPicker(Sleber *app) {
    if (!app || !app->gui) {
        return;
    }
    QueueWidgetDraw(app->gui->color_picker_area, (Area){0});
}


int StartThread(ThreadType* thread, size_t stack, ThreadFunction func, void* arg, const char* name) {
	(void)stack;
	(void)name;

	pthread_t newThread = 0;
    const int result = pthread_create(&newThread, NULL, func, arg);
	if (result != 0) {
		fprintf(stderr, "Failed to start thread %s: %d\n", name ? name : "unknown", result);
		return result;
	}
	if (thread) {
		*thread = newThread;
	}
	return 0;
}

int JoinThread(ThreadType* thread) {
    return thread ? pthread_join(*thread, NULL) : EINVAL;
}

void InitMutex(MutexType* m) {
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutex_init(m, &attr);
}

void DestroyMutex(MutexType* m) {
	pthread_mutex_destroy(m);
}

void LockMutex(MutexType* m) {
	pthread_mutex_lock(m);
}

void UnlockMutex(MutexType* m) {
	pthread_mutex_unlock(m);
}

void InitConditionVariable(ConditionVariable* c) {
	pthread_cond_init(c, 0);
}

void DestroyConditionVariable(ConditionVariable* c) {
	pthread_cond_destroy(c);
}

void WaitConditionVariable(ConditionVariable* c, MutexType* m) {
	pthread_cond_wait(c, m);
}

void AwakeConditionVariable(ConditionVariable* c) {
	pthread_cond_signal(c);
}

void UpdateStatus(Sleber *app, const char *message_utf8) {
    gtk_label_set_text(GTK_LABEL(app->gui->status_label), message_utf8);
}

struct Int2 WindowSize(GtkWidget *window) {
    int width  = gtk_widget_get_allocated_width(window);
    int height = gtk_widget_get_allocated_height(window);
	return (struct Int2) { width, height };
}

FILE* OpenFileUTF8(const char* path_utf8, const char* mode) {
    return fopen(path_utf8, mode);
}
static void DrawCanvas(GtkWidget *widget, cairo_t *cr, Sleber *app) {
    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);

    LockMutex(&app->redraw_mutex);
	BitmapType backbuffer = GetDrawBitmap();
    if (!BitmapValid(backbuffer)) {
        cairo_set_source_rgb(cr, 0.12, 0.12, 0.12);
        cairo_paint(cr);
        UnlockMutex(&app->redraw_mutex);
        return;
    }

    cairo_surface_t *surface = cairo_image_surface_create_for_data(
        (unsigned char *)backbuffer.pPixels,
        CAIRO_FORMAT_ARGB32,
        backbuffer.width,
        backbuffer.height,
        backbuffer.width * 4
    );
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        cairo_set_source_rgb(cr, 0.12, 0.12, 0.12);
        cairo_paint(cr);
        UnlockMutex(&app->redraw_mutex);
        return;
    }

    cairo_set_source_surface(cr, surface, 0.0, 0.0);
    cairo_paint(cr);
    cairo_surface_destroy(surface);
    UnlockMutex(&app->redraw_mutex);

    if (app->selection_mode != SELECT_MODE_NONE && app->select_w != 0 && app->select_h != 0) {
        int step = PixelSize(app);
        double x = app->select_x * step + app->offset_x;
        double y = app->select_y * step + app->offset_y;
        double width = app->select_w * step;
        double height = app->select_h * step;
        if (width < 0) { x += width; width = -width; }
        if (height < 0) { y += height; height = -height; }

        const double dashes[] = { 4.0, 4.0 };
        cairo_save(cr);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
        cairo_set_line_width(cr, 1.0);
        cairo_set_dash(cr, dashes, 2, 0.0);
        cairo_rectangle(cr, x + 0.5, y + 0.5, width - 1.0, height - 1.0);
        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
        cairo_stroke(cr);
        cairo_set_dash(cr, dashes, 2, 4.0);
        cairo_rectangle(cr, x + 0.5, y + 0.5, width - 1.0, height - 1.0);
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_stroke(cr);
        cairo_restore(cr);
    }
}

static gboolean OnDraw(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    Sleber *app = (Sleber *)user_data;
    DrawCanvas(widget, cr, app);
    return FALSE;
}

static gboolean OnButtonPress(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    (void)widget;
    Sleber *app = (Sleber *)user_data;
    if ((app->modifier_keys & MODIFIER_KEY_SHIFT) || (event->state & GDK_SHIFT_MASK)) {
        int step = PixelSize(app);
        I32Point p = SnapToGrid(app, (I32Point) {
            ((int)event->x - app->offset_x) / step,
            ((int)event->y - app->offset_y) / step
        });
        if (event->button == GDK_BUTTON_PRIMARY || event->button == GDK_BUTTON_SECONDARY) {
            PickColorAt(app, p, event->button == GDK_BUTTON_SECONDARY);
            gtk_widget_grab_focus(app->gui->drawing_area);
            return TRUE;
        }
    }
    if (event->button == GDK_BUTTON_PRIMARY) {
        UpdateMouseDown(app, (I32Point){ (int)event->x, (int)event->y }, FALSE);
        gtk_widget_grab_focus(app->gui->drawing_area);
        return TRUE;
    }
    if (event->button == GDK_BUTTON_SECONDARY) {
        UpdateMouseDown(app, (I32Point){ (int)event->x, (int)event->y }, TRUE);
        gtk_widget_grab_focus(app->gui->drawing_area);
        return TRUE;
    }
    if (event->button == GDK_BUTTON_MIDDLE) {
        app->dragging = MOUSE_BUTTON_MIDDLE;
        app->drag_last_x = (int)event->x;
        app->drag_last_y = (int)event->y;
        gtk_widget_grab_focus(app->gui->drawing_area);
        return TRUE;
    }
    return FALSE;
}

static gboolean OnButtonRelease(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    (void)widget;
    Sleber *app = (Sleber *)user_data;
	app->fill_mode = false;
    if (app->paste_mode == PASTE_MODE_PASTED) {
        SetPasteMode(app, PASTE_MODE_NONE);
    }
    if (app->dragging == MOUSE_BUTTON_LEFT && event->button == GDK_BUTTON_PRIMARY) {
        app->dragging = MOUSE_BUTTON_NONE;
        app->mouse_drawn = false;
        if (app->selection_mode == SELECT_MODE_RECTANGLE_DRAG) {
            app->selection_mode = SELECT_MODE_RECTANGLE;
        }
        RequestRedraw(app);
        return TRUE;
    }
    if (app->dragging == MOUSE_BUTTON_RIGHT && event->button == GDK_BUTTON_SECONDARY) {
        app->dragging = MOUSE_BUTTON_NONE;
        app->mouse_drawn = false;
        if (app->selection_mode == SELECT_MODE_RECTANGLE_DRAG) {
            app->selection_mode = SELECT_MODE_RECTANGLE;
        }
        RequestRedraw(app);
        return TRUE;
    }
    if (app->dragging == MOUSE_BUTTON_MIDDLE && event->button == GDK_BUTTON_MIDDLE) {
        app->dragging = MOUSE_BUTTON_NONE;
        return TRUE;
    }
    return FALSE;
}

static gboolean OnMotion(GtkWidget *widget, GdkEventMotion *event, gpointer user_data) {
    (void)widget;
    Sleber *app = (Sleber *)user_data;
    UICanvasMouseMove(app, (I32Point){ (int)event->x, (int)event->y });
    return TRUE;
}

static gboolean OnScroll(GtkWidget *widget, GdkEventScroll *event, gpointer user_data) {
    (void)widget;
    Sleber *app = (Sleber *)user_data;
    int old_size = PixelSize(app);

    int new_zoom = app->zoom;
    if (event->direction == GDK_SCROLL_UP) {
        new_zoom += 8;
    } else if (event->direction == GDK_SCROLL_DOWN) {
        new_zoom -= 8;
    } else if (event->direction == GDK_SCROLL_SMOOTH) {
        gdouble delta_x = 0, delta_y = 0;
        if (gdk_event_get_scroll_deltas((GdkEvent *)event, &delta_x, &delta_y)) {
            int zoom_change = (int)(delta_y * -8.0);
            if (zoom_change == 0 && delta_y != 0) {
                zoom_change = delta_y < 0 ? 8 : -8;
            }
            new_zoom += zoom_change;
        }
    }

    if (new_zoom > MaxZoomValue()) new_zoom = MaxZoomValue();
    if (new_zoom < MinZoomValue()) new_zoom = MinZoomValue();
    app->zoom = new_zoom;

    if (PixelSize(app) != old_size) {
        double world_x = (event->x - app->offset_x) / (double)old_size;
        double world_y = (event->y - app->offset_y) / (double)old_size;
        int step = PixelSize(app);
        app->offset_x = (int)(event->x - world_x * step);
        app->offset_y = (int)(event->y - world_y * step);
        //gtk_widget_queue_draw(widget);
        RequestRedraw(app);
    }

    return TRUE;
}

static gboolean OnKeyPress(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
    (void)widget;
    Sleber *app = (Sleber *)user_data;
    switch (event->keyval) {
        case GDK_KEY_Shift_L:
            app->modifier_keys |= MODIFIER_KEY_LEFT_SHIFT;
            break;
        case GDK_KEY_Shift_R:
            app->modifier_keys |= MODIFIER_KEY_RIGHT_SHIFT;
            break;
        case GDK_KEY_Control_L:
            app->modifier_keys |= MODIFIER_KEY_LEFT_CTRL;
            break;
        case GDK_KEY_Control_R:
            app->modifier_keys |= MODIFIER_KEY_RIGHT_CTRL;
            break;
        case GDK_KEY_Alt_L:
            app->modifier_keys |= MODIFIER_KEY_LEFT_ALT;
            break;
        case GDK_KEY_Alt_R:
            app->modifier_keys |= MODIFIER_KEY_RIGHT_ALT;
            break;
        default:
            break;
    }
    guint keyval = gdk_keyval_to_upper(event->keyval);

    if (!(event->state & GDK_CONTROL_MASK) && keyval >= GDK_KEY_0 && keyval <= GDK_KEY_9) {
        if (event->state & GDK_SHIFT_MASK) {
            SetBackgroundColor(app, (int)(keyval - GDK_KEY_0));
        } else {
            SetForegroundColor(app, (int)(keyval - GDK_KEY_0));
        }
        RequestRedrawPalette(app);
        return TRUE;
    }

    if (!(event->state & GDK_CONTROL_MASK) && keyval >= GDK_KEY_A && keyval <= GDK_KEY_F) {
        if (event->state & GDK_SHIFT_MASK) {
            SetBackgroundColor(app, (int)(keyval - GDK_KEY_A + 10));
		} else {
            SetForegroundColor(app, (int)(keyval - GDK_KEY_A + 10));
        }
        RequestRedrawPalette(app);
        return TRUE;
    }

    switch (keyval) {
        case GDK_KEY_Left:
            app->offset_x += 16;
            break;
        case GDK_KEY_Right:
            app->offset_x -= 16;
            break;
        case GDK_KEY_Up:
            app->offset_y += 16;
            break;
        case GDK_KEY_Down:
            app->offset_y -= 16;
            break;
        case GDK_KEY_Escape:
            app->fill_mode = false;
            app->selection_mode = SELECT_MODE_NONE;
            app->draw_pixel_info = false;
            app->draw_pixel_prev = false;
            app->paste_mode = PASTE_MODE_NONE;
            if (app->color_picker_visible) {
                gtk_widget_hide(app->gui->color_picker_window);
                app->color_picker_visible = false;
            }
            break;
        case GDK_KEY_Delete:
            if (app->selection_mode != SELECT_MODE_NONE) {
                FillSelection(app, app->background_color);
            }
            break;
        case GDK_KEY_A:
            if (event->state & GDK_CONTROL_MASK) {
                SelectAll(app);
            }
            break;
        case GDK_KEY_C:
            if ((event->state & GDK_CONTROL_MASK) && app->selection_mode != SELECT_MODE_NONE) {
                CopySelectionToBrush(app, true);
                BrushToClipboard(app);
            }
            break;
        case GDK_KEY_G:
            app->selection_mode = SELECT_MODE_NONE;
            app->fill_mode = true;
            break;
        case GDK_KEY_M:
            StartSelection(app);
            break;
        case GDK_KEY_O:
        case GDK_KEY_L:
            if (event->state & GDK_CONTROL_MASK) {
                OnOpenActivate(NULL, app);
                return TRUE;
            }
            return FALSE;
        case GDK_KEY_I:
            if (event->state & GDK_CONTROL_MASK) {
                OnImportActivate(NULL, app);
                return TRUE;
            }
            return FALSE;
        case GDK_KEY_S:
            if (event->state & GDK_CONTROL_MASK) {
                if ((event->state & GDK_SHIFT_MASK) || !SaveCurrentFile(app)) {
                    OnSaveSBXActivate(NULL, app);
                }
                return TRUE;
            }
            return FALSE;
        case GDK_KEY_T:
            app->draw_pixel_info = true;
            RequestRedraw(app);
            return TRUE;
        case GDK_KEY_U:
            if ((event->state & GDK_SHIFT_MASK) && (event->state & GDK_CONTROL_MASK)) {
                DoUndo(app, true, true);
                RequestRedraw(app);
            } else if (event->state & GDK_CONTROL_MASK) {
                DoUndo(app, true, false);
                RequestRedraw(app);
            } else {
                app->draw_pixel_prev = true;
                RequestRedraw(app);
            }
            return TRUE;
        case GDK_KEY_V:
            if (event->state & GDK_CONTROL_MASK) {
                if (ClipboardBitmapToBrush(app)) {
                    SetPasteMode(app, (event->state & GDK_SHIFT_MASK) ? PASTE_MODE_SKIP_BG_COLOR : PASTE_MODE_OVERWRITE);
                    RequestDrawBrush(app, (I32Point){ app->mouse_x, app->mouse_y });
                } else {
                    SetPasteMode(app, PASTE_MODE_NONE);
                    UpdateStatus(app, "Unable to paste an image from the clipboard.");
                }
            }
            break;
        case GDK_KEY_X:
            if ((event->state & GDK_CONTROL_MASK) && app->selection_mode != SELECT_MODE_NONE) {
                CopySelectionToBrush(app, false);
                BrushToClipboard(app);
                FillSelection(app, app->background_color);
            }
            break;
        case GDK_KEY_Z:
            if ((event->state & GDK_SHIFT_MASK) && (event->state & GDK_CONTROL_MASK)) {
                DoUndo(app, false, true);
                RequestRedraw(app);
            } else if (event->state & GDK_CONTROL_MASK) {
                DoUndo(app, false, false);
                RequestRedraw(app);
            }
            return TRUE;
        default:
            return FALSE;
    }

    RequestRedraw(app);
    return TRUE;
}

static gboolean OnKeyRelease(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
    (void)widget;
    Sleber *app = (Sleber *)user_data;
    switch (event->keyval) {
        case GDK_KEY_Shift_L:
            app->modifier_keys &= (uint8_t)~MODIFIER_KEY_LEFT_SHIFT;
            break;
        case GDK_KEY_Shift_R:
            app->modifier_keys &= (uint8_t)~MODIFIER_KEY_RIGHT_SHIFT;
            break;
        case GDK_KEY_Control_L:
            app->modifier_keys &= (uint8_t)~MODIFIER_KEY_LEFT_CTRL;
            break;
        case GDK_KEY_Control_R:
            app->modifier_keys &= (uint8_t)~MODIFIER_KEY_RIGHT_CTRL;
            break;
        case GDK_KEY_Alt_L:
            app->modifier_keys &= (uint8_t)~MODIFIER_KEY_LEFT_ALT;
            break;
        case GDK_KEY_Alt_R:
            app->modifier_keys &= (uint8_t)~MODIFIER_KEY_RIGHT_ALT;
            break;
        default:
            break;
    }
    guint keyval = gdk_keyval_to_upper(event->keyval);
    if (keyval == GDK_KEY_T) {
        app->draw_pixel_info = false;
        RequestRedraw(app);
        return TRUE;
    }
    if (keyval == GDK_KEY_U && !(event->state & GDK_SHIFT_MASK) && !(event->state & GDK_CONTROL_MASK)) {
        app->draw_pixel_prev = false;
        RequestRedraw(app);
        return TRUE;
    }
    return FALSE;
}

static gboolean OnColorPickerKeyPress(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
    (void)widget;
    Sleber *app = (Sleber *)user_data;
    if (event->keyval == GDK_KEY_Escape) {
        gtk_widget_hide(app->gui->color_picker_window);
        app->color_picker_visible = false;
        gtk_widget_grab_focus(app->gui->drawing_area);
        return TRUE;
    }
    return FALSE;
}

static void OnOpenActivate(GtkMenuItem *menuitem, gpointer user_data) {
    (void)menuitem;
    Sleber *app = (Sleber *)user_data;
    static const char *const canvas_patterns[] = {
        "*.sbx", "*.SBX", "*.gpx", "*.GPX", NULL
    };
    static const char *const sbx_patterns[] = { "*.sbx", "*.SBX", NULL };
    static const char *const gpx_patterns[] = { "*.gpx", "*.GPX", NULL };
    static const char *const all_patterns[] = { "*", NULL };
    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        "Open", GTK_WINDOW(app->gui->window), GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open", GTK_RESPONSE_ACCEPT,
        NULL);
    AddFileChooserFilter(dialog, "SBX and GPX files", canvas_patterns, NULL);
    AddFileChooserFilter(dialog, "SBX files", sbx_patterns, NULL);
    AddFileChooserFilter(dialog, "GPX files", gpx_patterns, NULL);
    AddFileChooserFilter(dialog, "All files", all_patterns, NULL);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (filename) {
            OpenImage(app, filename);
            g_free(filename);
        }
    }
    gtk_widget_destroy(dialog);
}

static void OnImportActivate(GtkMenuItem *menuitem, gpointer user_data) {
    (void)menuitem;
    Sleber *app = (Sleber *)user_data;
    static const char *const image_patterns[] = {
        "*.png", "*.PNG", "*.bmp", "*.BMP", "*.jpg", "*.JPG", "*.jpeg", "*.JPEG", NULL
    };
    static const char *const all_patterns[] = { "*", NULL };
    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        "Import Image", GTK_WINDOW(app->gui->window), GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Import", GTK_RESPONSE_ACCEPT,
        NULL);
    AddFileChooserFilter(dialog, "Image files", image_patterns, NULL);
    AddFileChooserFilter(dialog, "All files", all_patterns, NULL);
    GtkWidget *keep_background = gtk_check_button_new_with_label("Keep current background color(s)");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(keep_background), app->keep_import_background_colors);
    gtk_widget_show(keep_background);
    gtk_file_chooser_set_extra_widget(GTK_FILE_CHOOSER(dialog), keep_background);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        app->keep_import_background_colors = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(keep_background));
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (filename) {
            ImportImageFile(app, filename);
            g_free(filename);
        }
    }
    gtk_widget_destroy(dialog);
}

static void OnExportActivate(GtkMenuItem *menuitem, gpointer user_data) {
    (void)menuitem;
    Sleber *app = (Sleber *)user_data;
    static const char *const png_patterns[] = { "*.png", "*.PNG", NULL };
    static const char *const all_patterns[] = { "*", NULL };
    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        "Export Image", GTK_WINDOW(app->gui->window), GTK_FILE_CHOOSER_ACTION_SAVE,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Export", GTK_RESPONSE_ACCEPT,
        NULL);
    AddFileChooserFilter(dialog, "PNG images", png_patterns, NULL);
    AddFileChooserFilter(dialog, "All files", all_patterns, NULL);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), "bitmap.png");

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (filename) {
            ExportImageToFile(app, filename);
            g_free(filename);
        }
    }
    gtk_widget_destroy(dialog);
}

static void OnSaveActivate(GtkMenuItem *menuitem, gpointer user_data) {
    (void)menuitem;
    Sleber *app = (Sleber *)user_data;
    static const char *const canvas_patterns[] = {
        "*.sbx", "*.SBX", "*.gpx", "*.GPX", NULL
    };
    static const char *const sbx_patterns[] = { "*.sbx", "*.SBX", NULL };
    static const char *const gpx_patterns[] = { "*.gpx", "*.GPX", NULL };
    static const char *const all_patterns[] = { "*", NULL };
    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        "Save As", GTK_WINDOW(app->gui->window), GTK_FILE_CHOOSER_ACTION_SAVE,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Save", GTK_RESPONSE_ACCEPT,
        NULL);
    AddFileChooserFilter(dialog, "SBX and GPX files", canvas_patterns, ".sbx");
    AddFileChooserFilter(dialog, "SBX files", sbx_patterns, ".sbx");
    AddFileChooserFilter(dialog, "GPX files", gpx_patterns, ".gpx");
    AddFileChooserFilter(dialog, "All files", all_patterns, ".sbx");
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), "bitmap.sbx");

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (filename) {
            filename = AddDefaultCanvasExtension(GTK_FILE_CHOOSER(dialog), filename);
            SaveImage(app, filename);
            g_free(filename);
        }
    }
    gtk_widget_destroy(dialog);
}

static void OnCanvasDragDataReceived(
    GtkWidget *widget,
    GdkDragContext *context,
    gint x,
    gint y,
    GtkSelectionData *selection_data,
    guint info,
    guint time,
    gpointer user_data) {
    (void)widget;
    (void)x;
    (void)y;
    (void)info;
    Sleber *app = (Sleber *)user_data;
    gchar **uris = gtk_selection_data_get_uris(selection_data);
    gboolean success = FALSE;

    for (guint i = 0; uris && uris[i]; ++i) {
        GError *error = NULL;
        gchar *path = g_filename_from_uri(uris[i], NULL, &error);
        if (!path) {
            if (error) {
                UpdateStatus(app, error->message);
                g_error_free(error);
            }
            continue;
        }

        success |= OpenDroppedFile(app, path);
        g_free(path);
    }

    g_strfreev(uris);
    gtk_drag_finish(context, success, FALSE, time);
}

static void OnSaveSBXActivate(GtkMenuItem *menuitem, gpointer user_data) {
    OnSaveActivate(menuitem, user_data);
}

static void OnSaveCurrentActivate(GtkMenuItem *menuitem, gpointer user_data) {
    (void)menuitem;
    Sleber *app = (Sleber *)user_data;
    if (!SaveCurrentFile(app)) {
        OnSaveSBXActivate(NULL, app);
    }
}

static void OnQuitActivate(GtkMenuItem *menuitem, gpointer user_data) {
    (void)menuitem;
    Sleber *app = (Sleber *)user_data;
    if (StopRedrawThread(app)) {
        gtk_main_quit();
    }
}

static void OnAboutActivate(GtkMenuItem *menuitem, gpointer user_data) {
    Sleber *app = (Sleber *)user_data;
    GtkWidget *dialog = gtk_about_dialog_new();
    (void)menuitem;

    gtk_about_dialog_set_program_name(GTK_ABOUT_DIALOG(dialog), "Sleber Bitmap Editor");
    gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(dialog), "1.0");
    gtk_about_dialog_set_comments(
        GTK_ABOUT_DIALOG(dialog),
        "A small bitmap editor for Commodore 64 graphic modes.\n"
        "Sleber \xA9 2026 Carl-Henrik Sk\xE5rstedt\n"
        "\n"
        "Left button: draw with the foreground color\n"
        "Right button: draw with the background color\n"
        "Middle button: pan the canvas\n"
        "Mouse wheel: zoom in or out\n"
        "0-9, A-F: choose foreground color\n"
        "Shift + 0-9, A-F: choose background color\n"
        "Arrow keys: pan the canvas\n"
        "Escape: cancel the current operation\n"
        "Delete: fill the selection with the background color\n"
        "M: start a selection, G: fill mode\n"
        "T: show pixel information while held\n"
        "U: show previous pixels while held\n"
        "Ctrl+A: select all\n"
        "Ctrl+S: Save the current file\n"
        "Ctrl+Shift+S: Save file as\n"
        "Ctrl+L or Ctrl+O: open image file\n"
        "Ctrl+C / Ctrl+X: copy or cut the selection\n"
        "Ctrl+V: paste\n"
        "Shift+Ctrl+V: paste while skipping background color\n"
        "Ctrl+Z / Ctrl+Shift+Z: undo / redo a drawing step\n"
        "Ctrl+U / Ctrl+Shift+U: undo / redo one frame's edit\n"
        "Ctrl+I: import image\n");
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(app->gui->window));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static void OnModeActivate(GtkMenuItem *menuitem, gpointer user_data) {
    Sleber *app = (Sleber *)user_data;
    if (!app) {
        return;
    }

    guint mode = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(menuitem), "mode"));
    SwitchScreenMode(app, (uint8_t)mode);
}

typedef struct {
    GtkWidget *width;
    GtkWidget *height;
    GtkWidget *mode;
    GtkWidget *width_label;
    GtkWidget *height_label;
    GtkWidget *units_label;
    GtkWidget *width_pixels;
    GtkWidget *height_pixels;
} SizeNewControls;

static void UpdateSizeNewDimensions(SizeNewControls *controls) {
    int mode = gtk_combo_box_get_active(GTK_COMBO_BOX(controls->mode));
    int unit_width = ScreenModeHorizontalUnit((uint8_t)mode);
    int unit_height = ScreenModeVerticalUnit((uint8_t)mode);
    char text[64];

    gtk_label_set_text(GTK_LABEL(controls->width_label), "Width units:");
    gtk_label_set_text(GTK_LABEL(controls->height_label), "Height units:");
    g_snprintf(text, sizeof(text), "Unit: %d x %d px", unit_width, unit_height);
    gtk_label_set_text(GTK_LABEL(controls->units_label), text);

    int width_units = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(controls->width));
    int height_units = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(controls->height));
    g_snprintf(text, sizeof(text), "%d px", width_units * unit_width);
    gtk_label_set_text(GTK_LABEL(controls->width_pixels), text);
    g_snprintf(text, sizeof(text), "%d px", height_units * unit_height);
    gtk_label_set_text(GTK_LABEL(controls->height_pixels), text);
}

static void OnSizeNewDimensionChanged(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    UpdateSizeNewDimensions((SizeNewControls *)user_data);
}

static void OnSizeNewModeChanged(GtkComboBox *widget, gpointer user_data) {
    SizeNewControls *controls = (SizeNewControls *)user_data;
    uint8_t mode = (uint8_t)gtk_combo_box_get_active(widget);
    gtk_spin_button_set_range(GTK_SPIN_BUTTON(controls->width), 1,
        MAX_CANVAS_WIDTH / ScreenModeHorizontalUnit(mode));
    gtk_spin_button_set_range(GTK_SPIN_BUTTON(controls->height), 1,
        MAX_CANVAS_HEIGHT / ScreenModeVerticalUnit(mode));
    UpdateSizeNewDimensions(controls);
}

static void OnSizeNewActivate(GtkMenuItem *menuitem, gpointer user_data) {
    (void)menuitem;
    Sleber *app = (Sleber *)user_data;
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Size/New", GTK_WINDOW(app->gui->window), GTK_DIALOG_MODAL,
        "_Cancel", GTK_RESPONSE_CANCEL, "_Ok", GTK_RESPONSE_ACCEPT, NULL);
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *grid = gtk_grid_new();
    GtkWidget *width = gtk_spin_button_new_with_range(1, MAX_CANVAS_WIDTH, 1);
    GtkWidget *height = gtk_spin_button_new_with_range(1, MAX_CANVAS_HEIGHT, 1);
    GtkWidget *mode = gtk_combo_box_text_new();
    GtkWidget *width_label = gtk_label_new("Width units:");
    GtkWidget *height_label = gtk_label_new("Height units:");
    GtkWidget *units_label = gtk_label_new("");
    GtkWidget *width_pixels = gtk_label_new("");
    GtkWidget *height_pixels = gtk_label_new("");
    GtkWidget *clear_canvas = gtk_check_button_new_with_label("Clear canvas");
    GtkWidget *clear_undo = gtk_check_button_new_with_label("Clear undo");
    SizeNewControls controls = {
        .width = width, .height = height, .mode = mode,
        .width_label = width_label, .height_label = height_label,
        .units_label = units_label, .width_pixels = width_pixels,
        .height_pixels = height_pixels
    };
    static const char *mode_labels[] = {
        "Char", "Char MC", "Bitmap", "Bitmap MC", "Sprite",
        "Sprite MC", "Char ECBM", "Char Mixed", "Unrestricted",
        "Unrestricted Wide"
    };

    for (guint i = 0; i < G_N_ELEMENTS(mode_labels); ++i) {
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode), mode_labels[i]);
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(mode), app->screen_mode);
    int unit_width = ScreenModeHorizontalUnit(app->screen_mode);
    int unit_height = ScreenModeVerticalUnit(app->screen_mode);
    gtk_spin_button_set_range(GTK_SPIN_BUTTON(width), 1, MAX_CANVAS_WIDTH / unit_width);
    gtk_spin_button_set_range(GTK_SPIN_BUTTON(height), 1, MAX_CANVAS_HEIGHT / unit_height);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(width), (app->width + unit_width - 1) / unit_width);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(height), (app->height + unit_height - 1) / unit_height);

    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 12);
    gtk_grid_attach(GTK_GRID(grid), width_label, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), width, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), width_pixels, 2, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), height_label, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), height, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), height_pixels, 2, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Screen mode:"), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), mode, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), units_label, 2, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), clear_canvas, 0, 3, 3, 1);
    gtk_grid_attach(GTK_GRID(grid), clear_undo, 0, 4, 3, 1);
    UpdateSizeNewDimensions(&controls);
    g_signal_connect(mode, "changed", G_CALLBACK(OnSizeNewModeChanged), &controls);
    g_signal_connect(width, "value-changed", G_CALLBACK(OnSizeNewDimensionChanged), &controls);
    g_signal_connect(height, "value-changed", G_CALLBACK(OnSizeNewDimensionChanged), &controls);
    gtk_container_add(GTK_CONTAINER(content), grid);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        int selected_mode = gtk_combo_box_get_active(GTK_COMBO_BOX(mode));
        int selected_unit_width = ScreenModeHorizontalUnit((uint8_t)selected_mode);
        int selected_unit_height = ScreenModeVerticalUnit((uint8_t)selected_mode);
        bool applied = ApplyCanvasSettings(app,
            gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(width)) * selected_unit_width,
            gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(height)) * selected_unit_height,
            (uint8_t)selected_mode,
            gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(clear_canvas)),
            gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(clear_undo)));
        if (!applied) {
            GtkWidget *error = gtk_message_dialog_new(GTK_WINDOW(dialog),
                GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
                "Canvas size or screen mode is invalid.");
            gtk_dialog_run(GTK_DIALOG(error));
            gtk_widget_destroy(error);
        }
    }
    gtk_widget_destroy(dialog);
}

static void OnSnapToGridActivate(GtkCheckMenuItem *menuitem, gpointer user_data) {
    Sleber *app = (Sleber *)user_data;
    app->snap_to_grid = gtk_check_menu_item_get_active(menuitem);
}

static void PositionPaletteWindow(Sleber *app);

static void OnDockPaletteActivate(GtkCheckMenuItem *menuitem, gpointer user_data) {
    Sleber *app = (Sleber *)user_data;
    bool docked = gtk_check_menu_item_get_active(menuitem);
    if (app->palette_docked == docked) {
        return;
    }
    app->palette_docked = docked;
    PositionPaletteWindow(app);
    RefreshPaletteUI(app);
}

static void OnDitherActivate(GtkCheckMenuItem *menuitem, gpointer user_data) {
    Sleber *app = (Sleber *)user_data;
    app->import_image_dither = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(menuitem), "dither"));
}

static void OnCanvasSizeAllocate(GtkWidget *widget, GdkRectangle *allocation, Sleber *app) {
    (void)widget;
    static int first = 1;
    if (!first && app->draw_window_width == allocation->width && app->draw_window_height == allocation->height) {
        return;
    }
    first = 0;

    DebugLog("draw size changed: (%d, %d) -> (%d, %d)\n",
        app->draw_window_width, app->draw_window_height,
        allocation->width, allocation->height);
    app->draw_window_width = allocation->width;
    app->draw_window_height = allocation->height;
    RequestRedraw(app);
}

static gboolean OnMainWindowDeleteEvent(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
    (void)widget;
    (void)event;
    (void)user_data;
    Sleber *app = (Sleber *)user_data;
    return !StopRedrawThread(app);
}

static gboolean OnPaletteDraw(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    Sleber *app = (Sleber *)user_data;
    const int width = PALETTE_WIDTH;
    const int height = PALETTE_HEIGHT;
    gtk_widget_set_size_request(widget, width, height);

    LockMutex(&app->redraw_mutex);
    BitmapType bitmap = GetPaletteBitmap();
    if (!BitmapValid(bitmap)) {
        cairo_set_source_rgb(cr, 0.12, 0.12, 0.12);
        cairo_paint(cr);
        UnlockMutex(&app->redraw_mutex);
        return FALSE;
    }

    cairo_surface_t *surface = cairo_image_surface_create_for_data(
        (unsigned char *)bitmap.pPixels,
        CAIRO_FORMAT_ARGB32,
        bitmap.width,
        bitmap.height,
        bitmap.width * 4
    );
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        cairo_set_source_rgb(cr, 0.12, 0.12, 0.12);
        cairo_paint(cr);
        UnlockMutex(&app->redraw_mutex);
        return FALSE;
    }

    cairo_set_source_surface(cr, surface, 0.0, 0.0);
    cairo_paint(cr);
    cairo_surface_destroy(surface);
    UnlockMutex(&app->redraw_mutex);
    return FALSE;
}

static void PositionPaletteWindow(Sleber *app) {
    if (!app || !app->gui || !app->gui->window || !app->gui->palette_window) {
        return;
    }

    int palette_width = app->palette_window_width > 0 ? app->palette_window_width : PALETTE_WIDTH;
    int palette_height = app->palette_window_height > 0 ? app->palette_window_height : PALETTE_HEIGHT;

    if (app->palette_docked) {
        if (app->gui->palette_area && app->gui->palette_host && gtk_widget_get_parent(app->gui->palette_area) != app->gui->palette_host) {
            GtkWidget *parent = gtk_widget_get_parent(app->gui->palette_area);
            g_object_ref(app->gui->palette_area);
            if (parent) {
                gtk_container_remove(GTK_CONTAINER(parent), app->gui->palette_area);
            }
            gtk_container_add(GTK_CONTAINER(app->gui->palette_host), app->gui->palette_area);
            g_object_unref(app->gui->palette_area);
        }

        gtk_widget_set_size_request(app->gui->palette_host, palette_width, palette_height);
        gtk_widget_set_size_request(app->gui->palette_area, palette_width, palette_height);
        gtk_widget_show(app->gui->palette_area);
        gtk_widget_show(app->gui->palette_host);
        gtk_widget_hide(app->gui->palette_window);
    } else {
        if (app->gui->palette_area && app->gui->palette_host && gtk_widget_get_parent(app->gui->palette_area) == app->gui->palette_host) {
            g_object_ref(app->gui->palette_area);
            gtk_container_remove(GTK_CONTAINER(app->gui->palette_host), app->gui->palette_area);
            gtk_container_add(GTK_CONTAINER(app->gui->palette_window), app->gui->palette_area);
            g_object_unref(app->gui->palette_area);
        }
        if (app->gui->palette_area && !gtk_widget_get_parent(app->gui->palette_area)) {
            gtk_container_add(GTK_CONTAINER(app->gui->palette_window), app->gui->palette_area);
        }

        gtk_window_set_transient_for(GTK_WINDOW(app->gui->palette_window), GTK_WINDOW(app->gui->window));
        gtk_window_set_decorated(GTK_WINDOW(app->gui->palette_window), TRUE);
        gtk_window_set_type_hint(GTK_WINDOW(app->gui->palette_window), GDK_WINDOW_TYPE_HINT_NORMAL);
        gtk_window_set_skip_taskbar_hint(GTK_WINDOW(app->gui->palette_window), FALSE);
        gtk_window_resize(GTK_WINDOW(app->gui->palette_window), palette_width, palette_height);
        gtk_widget_show_all(app->gui->palette_window);
    }
}

static gboolean OnPaletteDeleteEvent(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
    (void)widget;
    (void)event;
    Sleber *app = (Sleber *)user_data;
    app->palette_docked = 1;
    if (app->gui->dock_palette_item) {
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(app->gui->dock_palette_item), TRUE);
    }
    PositionPaletteWindow(app);
    RefreshPaletteUI(app);
    return TRUE;
}

static gboolean OnMainWindowConfigure(GtkWidget *widget, GdkEventConfigure *event, gpointer user_data) {
    (void)widget;
    (void)event;
    Sleber *app = (Sleber *)user_data;
    if (app->palette_docked) {
        PositionPaletteWindow(app);
    }
    return FALSE;
}

static gboolean OnPaletteButtonPress(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    Sleber *app = (Sleber *)user_data;
    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);

    if (event->button != GDK_BUTTON_PRIMARY && event->button != GDK_BUTTON_SECONDARY) {
        return FALSE;
    }

    UIPaletteButtonDown(
        app,
        (I32Point) { (int)event->x, (int)event->y },
        (I32Point) { 0, 0 },
        (I32Point) { allocation.width, allocation.height },
        (I32Point) { (int)event->x_root, (int)event->y_root },
        event->button == GDK_BUTTON_PRIMARY);
    return TRUE;
}

static gboolean OnColorPickerDraw(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    (void)widget;
    Sleber *app = (Sleber *)user_data;
    LockMutex(&app->redraw_mutex);
    BitmapType bitmap = GetColorPickerBitmap();
    if (!BitmapValid(bitmap)) {
        cairo_set_source_rgb(cr, 0.12, 0.12, 0.12);
        cairo_paint(cr);
        UnlockMutex(&app->redraw_mutex);
        return FALSE;
    }

    cairo_surface_t *surface = cairo_image_surface_create_for_data(
        (unsigned char *)bitmap.pPixels,
        CAIRO_FORMAT_ARGB32,
        bitmap.width,
        bitmap.height,
        bitmap.width * 4
    );
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        cairo_set_source_rgb(cr, 0.12, 0.12, 0.12);
        cairo_paint(cr);
        UnlockMutex(&app->redraw_mutex);
        return FALSE;
    }

    cairo_set_source_surface(cr, surface, 0.0, 0.0);
    cairo_paint(cr);
    cairo_surface_destroy(surface);
    UnlockMutex(&app->redraw_mutex);

    return FALSE;
}

static gboolean OnColorPickerMotion(GtkWidget *widget, GdkEventMotion *event, gpointer user_data) {
    (void)widget;
    Sleber *app = (Sleber *)user_data;
    int width = app->color_picker_window_width > 0 ? app->color_picker_window_width : COLOR_PICKER_WIDTH;
    int col = (int)(event->x / (width / 2));
    int row = (int)(event->y / COLOR_PICKER_SQUARE);
    int index = row + (col << 3);
    uint8_t hover_color = (col >= 0 && col < 2 && row >= 0 && row < 8 && index < COLOR_COUNT) ? index : COLOR_COUNT;

    if (app->color_picker_hover_color != hover_color) {
        app->color_picker_hover_color = hover_color;
        RequestRedrawColorPicker(app);
    }
    return TRUE;
}

static gboolean OnColorPickerButtonPress(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    (void)widget;
    Sleber *app = (Sleber *)user_data;
    if (event->button == GDK_BUTTON_PRIMARY) {
        int width = app->color_picker_window_width > 0 ? app->color_picker_window_width : COLOR_PICKER_WIDTH;
        int col = (int)(event->x / (width / 2));
        int row = (int)(event->y / COLOR_PICKER_SQUARE);
        int index = row + (col << 3);
        if (col >= 0 && col < 2 && row >= 0 && row < 8 && index >= 0 && index < COLOR_COUNT) {
            ChangeColor(app, app->color_picker_target_color, index);
            gtk_widget_hide(app->gui->color_picker_window);
            app->color_picker_visible = false;
            gtk_widget_grab_focus(app->gui->drawing_area);
            return TRUE;
        }
    }
    return FALSE;
}

static void RefreshColorPickerUI(Sleber* app) {
    RequestRedrawColorPicker(app);
}

void OpenColorPickerAt(Sleber* app, const I32Point sp) {
    gtk_window_move(GTK_WINDOW(app->gui->color_picker_window), sp.x, sp.y);
    gtk_widget_show_all(app->gui->color_picker_window);
    gtk_widget_grab_focus(app->gui->color_picker_area);
    app->color_picker_visible = true;
}

static void BuildPaletteWindow(Sleber *app) {
	int width = PALETTE_WIDTH;
	int height = PALETTE_HEIGHT;

    app->gui->palette_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	app->palette_window_width = width;
	app->palette_window_height = height;
    
    gtk_window_set_title(GTK_WINDOW(app->gui->palette_window), "Palette");
    gtk_window_set_default_size(GTK_WINDOW(app->gui->palette_window), width, height);
    gtk_window_set_resizable(GTK_WINDOW(app->gui->palette_window), FALSE);
    gtk_window_set_keep_above(GTK_WINDOW(app->gui->palette_window), TRUE);
    gtk_window_set_transient_for(GTK_WINDOW(app->gui->palette_window), GTK_WINDOW(app->gui->window));
    g_signal_connect(app->gui->palette_window, "delete-event", G_CALLBACK(OnPaletteDeleteEvent), app);
    app->palette_docked = 1;

    app->gui->palette_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(app->gui->palette_area, width, height);
    gtk_widget_add_events(app->gui->palette_area, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(app->gui->palette_area, "draw", G_CALLBACK(OnPaletteDraw), app);
    g_signal_connect(app->gui->palette_area, "button-press-event", G_CALLBACK(OnPaletteButtonPress), app);

    app->gui->palette_host = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(app->gui->palette_host, width, height);
    gtk_widget_set_vexpand(app->gui->palette_host, FALSE);
    gtk_widget_set_valign(app->gui->palette_host, GTK_ALIGN_START);
    gtk_widget_set_halign(app->gui->palette_host, GTK_ALIGN_END);
    gtk_overlay_add_overlay(GTK_OVERLAY(app->gui->content_box), app->gui->palette_host);
    RefreshPaletteUI(app);
    PositionPaletteWindow(app);
}

static void BuildColorPickerWindow(Sleber *app) {
    int width  = 2 * COLOR_PICKER_SQUARE;
    int height = 8 * COLOR_PICKER_SQUARE;

    app->gui->color_picker_window = gtk_window_new(GTK_WINDOW_POPUP);

    app->color_picker_window_width  = width;
    app->color_picker_window_height = height;

    gtk_window_set_title( GTK_WINDOW(app->gui->color_picker_window), "Color Picker" );
    gtk_window_set_default_size( GTK_WINDOW(app->gui->color_picker_window), width, height );
    gtk_window_set_resizable( GTK_WINDOW(app->gui->color_picker_window), FALSE );
    gtk_widget_set_size_request(app->gui->color_picker_window, width, height);
    gtk_window_set_keep_above( GTK_WINDOW(app->gui->color_picker_window), TRUE);
    gtk_window_set_transient_for(GTK_WINDOW(app->gui->color_picker_window), GTK_WINDOW(app->gui->window));
    app->gui->color_picker_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(app->gui->color_picker_area, width, height);

    gtk_widget_add_events( app->gui->color_picker_area, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK | GDK_KEY_PRESS_MASK);
    gtk_widget_set_can_focus(app->gui->color_picker_area, TRUE);
    g_signal_connect( app->gui->color_picker_area, "draw", G_CALLBACK(OnColorPickerDraw), app );
    g_signal_connect( app->gui->color_picker_area, "button-press-event", G_CALLBACK(OnColorPickerButtonPress), app );
    g_signal_connect( app->gui->color_picker_area, "motion-notify-event", G_CALLBACK(OnColorPickerMotion), app );
    g_signal_connect( app->gui->color_picker_area, "key-press-event", G_CALLBACK(OnColorPickerKeyPress), app );

    gtk_container_add( GTK_CONTAINER(app->gui->color_picker_window), app->gui->color_picker_area );

    RefreshColorPickerUI(app);
    gtk_widget_hide(app->gui->color_picker_window);
}


static void BuildMenuBar(Sleber *app, GtkWidget *container) {
    GtkWidget *menubar = gtk_menu_bar_new();

    GtkWidget *file_menu_item = gtk_menu_item_new_with_label("File");
    GtkWidget *file_menu = gtk_menu_new();
    GtkWidget *save_current_item = gtk_menu_item_new_with_label("Save");
    GtkWidget *open_item = gtk_menu_item_new_with_label("Open");
    GtkWidget *save_item = gtk_menu_item_new_with_label("Save As");
    GtkWidget *import_item = gtk_menu_item_new_with_label("Import Image");
    GtkWidget *export_item = gtk_menu_item_new_with_label("Export Image");
    GtkWidget *quit_item = gtk_menu_item_new_with_label("Quit");

    g_signal_connect(open_item, "activate", G_CALLBACK(OnOpenActivate), app);
    g_signal_connect(save_current_item, "activate", G_CALLBACK(OnSaveCurrentActivate), app);
    g_signal_connect(save_item, "activate", G_CALLBACK(OnSaveActivate), app);
    g_signal_connect(import_item, "activate", G_CALLBACK(OnImportActivate), app);
    g_signal_connect(export_item, "activate", G_CALLBACK(OnExportActivate), app);
    g_signal_connect(quit_item, "activate", G_CALLBACK(OnQuitActivate), app);

    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), open_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), save_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), save_current_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), import_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), export_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), gtk_separator_menu_item_new());
    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), quit_item);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(file_menu_item), file_menu);

    GtkAccelGroup *accel_group = gtk_accel_group_new();
    gtk_window_add_accel_group(GTK_WINDOW(app->gui->window), accel_group);
    gtk_widget_add_accelerator(open_item, "activate", accel_group,
        GDK_KEY_O, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    gtk_widget_add_accelerator(open_item, "activate", accel_group,
        GDK_KEY_L, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    gtk_widget_add_accelerator(save_current_item, "activate", accel_group,
        GDK_KEY_S, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    gtk_widget_add_accelerator(save_item, "activate", accel_group,
        GDK_KEY_S, GDK_CONTROL_MASK | GDK_SHIFT_MASK , GTK_ACCEL_VISIBLE);
    gtk_widget_add_accelerator(import_item, "activate", accel_group,
        GDK_KEY_I, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

    GtkWidget *edit_menu_item = gtk_menu_item_new_with_label("Edit");
    GtkWidget *edit_menu = gtk_menu_new();
    GtkWidget *mode_menu_item = gtk_menu_item_new_with_label("Mode");
    GtkWidget *mode_menu = gtk_menu_new();

    static const struct {
        const char *label;
        uint8_t mode;
    } mode_items[] = {
        { "Char", SCREEN_MODE_CHAR },
        { "Char MC", SCREEN_MODE_CHAR_MULTICOLOR },
        { "Bitmap", SCREEN_MODE_BITMAP },
        { "Bitmap MC", SCREEN_MODE_BITMAP_MULTICOLOR },
        { "Sprite", SCREEN_MODE_SPRITE },
        { "Sprite MC", SCREEN_MODE_SPRITE_MULTICOLOR },
        { "Char ECBM", SCREEN_MODE_ECBM },
        { "Char Mixed", SCREEN_MODE_CHAR_MIXED },
        { "Unrestricted", SCREEN_MODE_UNRESTRICTED },
        { "Unrestricted Wide", SCREEN_MODE_WIDE_UNRESTRICTED },
    };

    for (size_t i = 0; i < sizeof(mode_items) / sizeof(mode_items[0]); ++i) {
        GtkWidget *item = gtk_menu_item_new_with_label(mode_items[i].label);
        g_object_set_data(G_OBJECT(item), "mode", GUINT_TO_POINTER(mode_items[i].mode));
        g_signal_connect(item, "activate", G_CALLBACK(OnModeActivate), app);
        gtk_menu_shell_append(GTK_MENU_SHELL(mode_menu), item);
    }

    gtk_menu_item_set_submenu(GTK_MENU_ITEM(mode_menu_item), mode_menu);
    GtkWidget *size_new_item = gtk_menu_item_new_with_label("Size/New");
    g_signal_connect(size_new_item, "activate", G_CALLBACK(OnSizeNewActivate), app);
    gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), size_new_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), mode_menu_item);
    GtkWidget *snap_to_grid_item = gtk_check_menu_item_new_with_label("Snap to Grid");
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(snap_to_grid_item), app->snap_to_grid);
    g_signal_connect(snap_to_grid_item, "toggled", G_CALLBACK(OnSnapToGridActivate), app);
    gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), snap_to_grid_item);
    app->gui->dock_palette_item = gtk_check_menu_item_new_with_label("Dock Palette");
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(app->gui->dock_palette_item), TRUE);
    g_signal_connect(app->gui->dock_palette_item, "toggled", G_CALLBACK(OnDockPaletteActivate), app);
    gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), app->gui->dock_palette_item);
    GtkWidget *dither_menu_item = gtk_menu_item_new_with_label("Import Image Dither");
    GtkWidget *dither_menu = gtk_menu_new();
    GtkRadioMenuItem *dither_group = NULL;
    static const char *dither_labels[] = { "Off", "Low", "Medium", "High" };
    for (guint i = 0; i < G_N_ELEMENTS(dither_labels); ++i) {
        GtkWidget *item = gtk_radio_menu_item_new_with_label_from_widget(dither_group, dither_labels[i]);
        dither_group = GTK_RADIO_MENU_ITEM(item);
        g_object_set_data(G_OBJECT(item), "dither", GUINT_TO_POINTER(i));
        g_signal_connect(item, "toggled", G_CALLBACK(OnDitherActivate), app);
        if (i == app->import_image_dither) {
            gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), TRUE);
        }
        gtk_menu_shell_append(GTK_MENU_SHELL(dither_menu), item);
    }
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(dither_menu_item), dither_menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), dither_menu_item);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(edit_menu_item), edit_menu);

    GtkWidget *help_menu_item = gtk_menu_item_new_with_label("Help");
    GtkWidget *help_menu = gtk_menu_new();
    GtkWidget *about_item = gtk_menu_item_new_with_label("About");
    g_signal_connect(about_item, "activate", G_CALLBACK(OnAboutActivate), app);
    gtk_menu_shell_append(GTK_MENU_SHELL(help_menu), about_item);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(help_menu_item), help_menu);

    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), file_menu_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), edit_menu_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), help_menu_item);
    gtk_box_pack_start(GTK_BOX(container), menubar, FALSE, FALSE, 0);
}

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);

    Sleber *app = AppNew();
    if (!app || !app->pixels) {
        fprintf(stderr, "Failed to initialize editor state.\n");
        AppFree(app);
        return 1;
    }

    InitBitmapPalette();
    SetScreenMode(app, SCREEN_MODE_BITMAP);
    StartRedrawThread(app);

    int step = PixelSize(app);
    app->gui->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app->gui->window), "Sleber Bitmap Editor");
    GError *icon_error = NULL;
    if (!gtk_window_set_icon_from_file(GTK_WINDOW(app->gui->window), SLEBER_ICON_PATH, &icon_error)) {
        fprintf(stderr, "Failed to load window icon: %s\n", icon_error->message);
        g_error_free(icon_error);
    }
    gtk_window_set_default_size(GTK_WINDOW(app->gui->window), app->draw_window_width, app->draw_window_height);
    g_signal_connect(app->gui->window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(app->gui->window, "delete-event", G_CALLBACK(OnMainWindowDeleteEvent), app);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(app->gui->window), box);

    BuildMenuBar(app, box);
    app->gui->content_box = gtk_overlay_new();
    gtk_box_pack_start(GTK_BOX(box), app->gui->content_box, TRUE, TRUE, 0);
    BuildPaletteWindow(app);
    g_signal_connect(app->gui->window, "configure-event", G_CALLBACK(OnMainWindowConfigure), app);
	BuildColorPickerWindow(app);
    
    app->gui->drawing_area = gtk_drawing_area_new();
    // Tell the widget to track motion when ANY button is pressed
    gtk_widget_set_size_request(app->gui->drawing_area, app->width * step, app->height * step);
    gtk_widget_set_hexpand(app->gui->drawing_area, TRUE);
    gtk_widget_set_vexpand(app->gui->drawing_area, TRUE);
    gtk_widget_set_halign(app->gui->drawing_area, GTK_ALIGN_FILL);
    gtk_widget_set_valign(app->gui->drawing_area, GTK_ALIGN_FILL);
    gtk_container_add(GTK_CONTAINER(app->gui->content_box), app->gui->drawing_area);

    app->gui->status_label = gtk_label_new("Ready");
    gtk_widget_set_halign(app->gui->status_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), app->gui->status_label, FALSE, FALSE, 0);

    g_signal_connect(app->gui->drawing_area, "draw", G_CALLBACK(OnDraw), app);
    g_signal_connect(app->gui->drawing_area, "button-press-event", G_CALLBACK(OnButtonPress), app);
    g_signal_connect(app->gui->drawing_area, "button-release-event", G_CALLBACK(OnButtonRelease), app);
    g_signal_connect(app->gui->drawing_area, "motion-notify-event", G_CALLBACK(OnMotion), app);
    g_signal_connect(app->gui->drawing_area, "scroll-event", G_CALLBACK(OnScroll), app);
    g_signal_connect(app->gui->drawing_area, "key-press-event", G_CALLBACK(OnKeyPress), app);
    g_signal_connect(app->gui->drawing_area, "key-release-event", G_CALLBACK(OnKeyRelease), app);
    g_signal_connect(app->gui->drawing_area, "size-allocate", G_CALLBACK(OnCanvasSizeAllocate), app);
    static GtkTargetEntry drag_targets[] = {
        { "text/uri-list", 0, 0 }
    };
    gtk_drag_dest_set(app->gui->drawing_area, GTK_DEST_DEFAULT_ALL,
        drag_targets, G_N_ELEMENTS(drag_targets), GDK_ACTION_COPY);
    g_signal_connect(app->gui->drawing_area, "drag-data-received",
        G_CALLBACK(OnCanvasDragDataReceived), app);

    gtk_widget_add_events(app->gui->drawing_area, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK | GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK | GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK);
    gtk_widget_set_can_focus(app->gui->drawing_area, TRUE);
    gtk_widget_grab_focus(app->gui->drawing_area);

    {
        // Window = canvas plus startup borders (as on Windows) plus menu/status chrome.
        int min_h = 0, nat_h = 0;
        gtk_widget_show_all(box); // hidden widgets report zero size
        gtk_widget_get_preferred_height(box, &min_h, &nat_h);
        int chrome = min_h - app->height * step;
        if (chrome < 0) chrome = 0;
        gtk_window_set_default_size(GTK_WINDOW(app->gui->window),
            (app->width + STARTUP_HORIZONTAL_BORDER * 2) * step,
            (app->height + STARTUP_VERTICAL_BORDER * 2) * step + chrome);
    }

    gtk_widget_show_all(app->gui->window);
    gtk_main();

    AppFree(app);
    return 0;
}

PerfTimerType GetPerfCounter() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts;
}

double PerfCounterToSeconds(PerfTimerType start, PerfTimerType end) {
    return (double)(end.tv_sec - start.tv_sec) + (double)(end.tv_nsec - start.tv_nsec) / 1e9;
}
    


#endif