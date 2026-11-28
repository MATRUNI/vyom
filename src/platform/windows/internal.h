#ifndef WINDOWS_INTERNAL_H
#define WINDOWS_INTERNAL_H

#include <windows.h>
#include "platform.h"

#define WM_TRAYICON (WM_APP + 1)
#define TRAY_CMD_BASE 5000
#define WM_PROCESS_EVENT (WM_APP + 2)
extern HWND g_hwnd;
extern int g_min_width;
extern int g_min_height;
extern int g_max_width;
extern int g_max_height;

bool win32_tray_handle_message(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void win32_tray_cleanup(void);
bool win32_process_handle_message(UINT uMsg, WPARAM wParam, LPARAM lParam);

#endif

