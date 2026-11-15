# GTK3 debugging notes

By default, GTK outputs errors as text logs (Gtk-CRITICAL or GLib-GObject-CRITICAL) in the console, but the app keeps running blindly until it deadlocks or crashes.

## 1. Force the App to Crash on Warnings (The Most Effective Tool)To catch cross-threading issues instantly, you want your application to crash the exact microsecond a background thread forces GTK to spit out a warning. You can do this by setting an environment variable when you launch your app in your terminal: ```bashG_DEBUG=fatal-criticals ./your_program```


If you want to catch even milder formatting issues, go a step further:```bashG_DEBUG=fatal-warnings ./your_program```

### How to use it with a debugger (GDB):

When combined with gdb, the program will trap exactly on the offending line of code, providing a full stack trace (bt):```bashG_DEBUG=fatal-criticals gdb ./your_program```

(gdb) run
Wait for it to hit the thread violation and halt...
(gdb) bt


## 2. Turn on GDK Thread Assertions (GTK3 Native)

GTK3 includes a built-in macro initialization check designed to explicitly protect multi-threaded environments. If you want your application to strictly enforce thread safety checks, run it with the following configuration:```GDK_SYNCHRONIZE=1 G_DEBUG=fatal-criticals ./your_program```

```GDK_SYNCHRONIZE=1``` forces the underlying X11/Wayland window graphics pipelines to process synchronously. If a background thread steps on the main thread's toes, the server throws an error immediately instead of failing silently downstream.

##3. Add Compile-Time Assertions inside your Code

If you want to bake safety assertions directly into your codebase so they catch mistakes during development without relying on environment variables, add g_return_if_fail assertions.GLib provides a macro called g_main_context_is_owner(). You can drop this into functions like draw_canvas or anywhere your engine updates to explicitly verify that the executing thread is indeed the main thread:
```
#include <glib.h>

void trigger_ui_update(App *app) {
    // RUNTIME ASSERT: If this runs on a background thread, 
    // it will throw a Gtk-CRITICAL error and stop executing.
    g_return_if_fail(g_main_context_is_owner(g_main_context_default()));

    gtk_widget_queue_draw(app->drawing_area);
}
```