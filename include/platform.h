#ifndef PLATFORM_H
#define PLATFORM_H

#include "app.h"

// 1. Starts the native OS event loop (GTK4 on Linux, Win32 on Windows, Cocoa on Mac)
int platform_app_run(const app_config_t *config, int argc, char *argv[]);

// 2. Evaluates JavaScript in the active webview
void platform_app_emit(const char *event, const char *json_data);

// 3. Basic controls of open window
// Window controls (Direct native calls)
void platform_window_minimize(void);
void platform_window_toggle_maximize(void);
void platform_window_close(void);
void platform_window_start_drag(void);
// Window Geometry & State
void platform_window_set_size(int width, int height);
void platform_window_get_size(int *width, int *height);
void platform_window_set_position(int x, int y);
void platform_window_get_position(int *x, int *y);
void platform_window_center(void);
void platform_window_set_always_on_top(bool always_on_top);
void platform_window_set_resizable(bool resizable);
void platform_window_set_min_size(int width, int height);
void platform_window_set_max_size(int width, int height);
void platform_window_set_title(const char *title);
bool platform_window_is_maximized(void);
bool platform_window_is_minimized(void);

// 4. Native Dialogs: returns dynamically allocated UTF-8 path string, or NULL if cancelled
char* platform_dialog_open_file(const char *title, const char *filter);

char* platform_dialog_save_file(const char *title, const char *filter, const char *default_name);

char* platform_dialog_open_directory(const char *title);

void  platform_dialog_message(const char *title, const char *message, const char *type);

bool  platform_dialog_confirm(const char *title, const char *message);

// 5. Native Clipboard
void  platform_clipboard_write_text(const char *text);

char* platform_clipboard_read_text(void);

void  platform_clipboard_clear(void);

bool  platform_clipboard_has_text(void);

// 6. System Special Paths & System Storage Drives
char* platform_path_get_special(const char *name);

char* platform_fs_get_drives(void);

// 7. Native Notifications & System Tray
typedef struct {
    char id[64];
    char label[128];
    bool separator;
} tray_menu_item_t;

void platform_notification_send(const char *title, const char *body, const char *icon);
void platform_tray_create(const char *tooltip, const char *icon);
void platform_tray_set_tooltip(const char *tooltip);
void platform_tray_set_menu(const tray_menu_item_t *items, int count);
void platform_tray_destroy(void);

// 8. Native Subprocess Execution
typedef struct {
    int exit_code;
    bool timed_out;
    char *stdout_buf; // Caller frees
    char *stderr_buf; // Caller frees
} process_result_t;

bool platform_process_exec(const char *cmd, const char *const *argv, int timeout_ms, process_result_t *out);
int  platform_process_spawn(const char *cmd, const char *const *argv);
bool platform_process_kill(int pid);
void process_cleanup_all(void);

#endif