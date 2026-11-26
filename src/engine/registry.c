#include "hash.h"
#include "registry.h"
#include "platform.h"
#include "fs.h"
#include "process.h"
#include "tray.h"
#include "window.h"
#include "native_path.h"
#include "clipboard.h"
#include "json_util.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define HASH_WIN_MINIMIZE          0x0d48ed56u // wyhash32("vyom:window:minimize")
#define HASH_WIN_TOGGLE_MAX        0xfe1b2c5bu // wyhash32("vyom:window:toggle_maximize")
#define HASH_WIN_CLOSE             0xafd91676u // wyhash32("vyom:window:close")
#define HASH_WIN_START_DRAG        0x018bb464u // wyhash32("vyom:window:start_drag")
#define HASH_DIALOG_OPEN_FILE      0x8f8202e3u // wyhash32("vyom:dialog:open_file")
#define HASH_DIALOG_SAVE_FILE      0x148805c7u // wyhash32("vyom:dialog:save_file")
#define HASH_FS_FILE_INFO          0x2bfcb39bu // wyhash32("vyom:fs:file_info")
#define HASH_FS_READ_CHUNK         0x79ea54d3u // wyhash32("vyom:fs:read_chunk")
#define HASH_FS_WRITE_TEXT         0xb0e8eb90u // wyhash32("vyom:fs:write_text")
#define HASH_FS_READ_DIR           0xd11449e8u // wyhash32("vyom:fs:read_dir")
#define HASH_FS_CREATE_FILE        0xe59c953fu // wyhash32("vyom:fs:create_file")
#define HASH_FS_CREATE_DIR         0xedad688au // wyhash32("vyom:fs:create_dir")
#define HASH_FS_REMOVE             0x1354a270u // wyhash32("vyom:fs:remove")
#define HASH_FS_RENAME             0x9462544eu // wyhash32("vyom:fs:rename")
#define HASH_FS_MOVE               0xae3da64du // wyhash32("vyom:fs:move")
#define HASH_CLIPBOARD_WRITE_TEXT  0xfd14bb60u // wyhash32("vyom:clipboard:write_text")
#define HASH_CLIPBOARD_READ_TEXT   0x989ead8eu // wyhash32("vyom:clipboard:read_text")
#define HASH_CLIPBOARD_CLEAR       0x8eee9d42u // wyhash32("vyom:clipboard:clear")
#define HASH_CLIPBOARD_HAS_TEXT    0xe86a74f5u // wyhash32("vyom:clipboard:has_text")
#define HASH_DIALOG_OPEN_DIRECTORY   0xce005053u // wyhash32("vyom:dialog:open_directory")
#define HASH_DIALOG_MESSAGE          0x10585f47u // wyhash32("vyom:dialog:message")
#define HASH_DIALOG_CONFIRM          0xc7c9ec2cu // wyhash32("vyom:dialog:confirm")
#define HASH_WIN_SET_SIZE          0x94dc9436u // wyhash32("vyom:window:set_size")
#define HASH_WIN_GET_SIZE          0x670a5d04u // wyhash32("vyom:window:get_size")
#define HASH_WIN_SET_POSITION      0x1469b813u // wyhash32("vyom:window:set_position")
#define HASH_WIN_GET_POSITION      0x1dbf97abu // wyhash32("vyom:window:get_position")
#define HASH_WIN_CENTER            0x781a24b9u // wyhash32("vyom:window:center")
#define HASH_WIN_SET_ALWAYS_ON_TOP 0x3db31c00u // wyhash32("vyom:window:set_always_on_top")
#define HASH_WIN_SET_RESIZABLE     0x246c845au // wyhash32("vyom:window:set_resizable")
#define HASH_WIN_SET_MIN_SIZE      0xefdddabfu // wyhash32("vyom:window:set_min_size")
#define HASH_WIN_SET_MAX_SIZE      0xaf73f68eu // wyhash32("vyom:window:set_max_size")
#define HASH_WIN_SET_TITLE         0xa926b002u // wyhash32("vyom:window:set_title")
#define HASH_WIN_IS_MAXIMIZED      0x88bb433eu // wyhash32("vyom:window:is_maximized")
#define HASH_WIN_IS_MINIMIZED      0xec78c398u // wyhash32("vyom:window:is_minimized")
#define HASH_PATH_GET_SPECIAL      0x084180b6u // wyhash32("vyom:path:get_special")
#define HASH_PATH_GET_ALL          0x475cb045u // wyhash32("vyom:path:get_all")
#define HASH_FS_GET_DRIVES         0xd154a90cu // wyhash32("vyom:fs:get_drives")
#define HASH_NOTIFICATION_SEND       0x7d521837u // wyhash32("vyom:notification:send")
#define HASH_TRAY_CREATE             0x3c05eaf3u // wyhash32("vyom:tray:create")
#define HASH_TRAY_SET_TOOLTIP        0x9dff0604u // wyhash32("vyom:tray:set_tooltip")
#define HASH_TRAY_SET_MENU           0x1f827d68u // wyhash32("vyom:tray:set_menu")
#define HASH_TRAY_DESTROY            0xceaf0e8cu // wyhash32("vyom:tray:destroy")
#define HASH_PROCESS_EXEC          0x17d40edbu // wyhash32("vyom:process:exec")
#define HASH_PROCESS_SPAWN         0x5bc6abecu // wyhash32("vyom:process:spawn")
#define HASH_PROCESS_KILL          0x00995c6au // wyhash32("vyom:process:kill")
typedef struct {
    char name[32];
    uint32_t hash;
    app_command_fn handler;
} cmd_entry_t;

static cmd_entry_t cmd_table[CMD_TABLE_SIZE];

void app_bind_command(const char *name, app_command_fn handler) 
{
    uint32_t hash = wyhash32(name);
    uint32_t idx = hash & (CMD_TABLE_SIZE - 1);

    for (int i = 0; i < CMD_TABLE_SIZE; i++)
    {
        uint32_t slot = (idx + i) & (CMD_TABLE_SIZE - 1);  // Linear probing

        if (cmd_table[slot].handler == NULL || strcmp(cmd_table[slot].name, name) == 0) 
        {
            strncpy(cmd_table[slot].name, name, sizeof(cmd_table[slot].name) - 1);
            cmd_table[slot].hash = hash;
            cmd_table[slot].handler = handler;
            return;
        }
    }
}

static void reply_with_path(ipc_reply_t *reply, char *path) {
    if (path) {
        char json[1024];
        snprintf(json, sizeof(json), "{\"canceled\": false, \"path\": \"%s\"}", path);
        for (char *p = json; *p; p++) {
            if (*p == '\\') *p = '/';
        }
        ipc_reply_send(reply, json);
        free(path);
    } else {
        ipc_reply_send(reply, "{\"canceled\": true, \"path\": null}");
    }
}

// Now both handlers become 2 lines!
static void handle_dialog_open_file(const char *args, ipc_reply_t *reply) {
    (void)args;
    reply_with_path(reply, platform_dialog_open_file("Select File", NULL));
}

static void handle_dialog_save_file(const char *args, ipc_reply_t *reply) 
{
    char default_name[128] = "untitled.txt"; // Fallback default
    // Extract "defaultName": "<string>" if provided in args
    const char *name_ptr = strstr(args, "\"defaultName\":");
    if (name_ptr) 
    {
        name_ptr = strchr(name_ptr + 14, '"');
        if (name_ptr) 
        {
            name_ptr++;
            const char *end_ptr = strchr(name_ptr, '"');
            if (end_ptr) 
            {
                size_t len = end_ptr - name_ptr;
                if (len < sizeof(default_name)) 
                {
                    strncpy(default_name, name_ptr, len);
                    default_name[len] = '\0';
                }
            }
        }
    }
    reply_with_path(reply, platform_dialog_save_file("Save File", NULL, default_name));
}

static void handle_dialog_open_directory(const char *args, ipc_reply_t *reply) 
{
    char title[128] = "Select Folder";
    json_get_string(args, "title", title, sizeof(title));
    reply_with_path(reply, platform_dialog_open_directory(title));
}

static void handle_dialog_message(const char *args, ipc_reply_t *reply) 
{
    char title[128] = "Alert";
    char message[512] = "";
    char type[32] = "info";
    json_get_string(args, "title", title, sizeof(title));
    json_get_string(args, "message", message, sizeof(message));
    json_get_string(args, "type", type, sizeof(type));
    platform_dialog_message(title, message, type);
    ipc_reply_send(reply, "{\"success\": true}");
}

static void handle_dialog_confirm(const char *args, ipc_reply_t *reply) 
{
    char title[128] = "Confirm";
    char message[512] = "";
    json_get_string(args, "title", title, sizeof(title));
    json_get_string(args, "message", message, sizeof(message));
    bool confirmed = platform_dialog_confirm(title, message);
    ipc_reply_send(reply, confirmed ? "{\"confirmed\": true}" : "{\"confirmed\": false}");
}

app_command_fn lookup_command(const char *name) 
{
    uint32_t hash = wyhash32(name);
    // 1. Fast CPU register integer check (Takes 1 CPU cycle, ZERO strcmp!)
    switch (hash) 
    {
        case HASH_WIN_MINIMIZE:          return (app_command_fn)handle_window_minimize;
        case HASH_WIN_TOGGLE_MAX:        return (app_command_fn)handle_window_toggle_maximize;
        case HASH_WIN_CLOSE:             return (app_command_fn)handle_window_close;
        case HASH_WIN_START_DRAG:        return (app_command_fn)handle_window_start_drag;
        case HASH_WIN_SET_SIZE:          return (app_command_fn)handle_window_set_size;
        case HASH_WIN_GET_SIZE:          return (app_command_fn)handle_window_get_size;
        case HASH_WIN_SET_POSITION:      return (app_command_fn)handle_window_set_position;
        case HASH_WIN_GET_POSITION:      return (app_command_fn)handle_window_get_position;
        case HASH_WIN_CENTER:            return (app_command_fn)handle_window_center;
        case HASH_WIN_SET_ALWAYS_ON_TOP: return (app_command_fn)handle_window_set_always_on_top;
        case HASH_WIN_SET_RESIZABLE:     return (app_command_fn)handle_window_set_resizable;
        case HASH_WIN_SET_MIN_SIZE:      return (app_command_fn)handle_window_set_min_size;
        case HASH_WIN_SET_MAX_SIZE:      return (app_command_fn)handle_window_set_max_size;
        case HASH_WIN_SET_TITLE:         return (app_command_fn)handle_window_set_title;
        case HASH_WIN_IS_MAXIMIZED:      return (app_command_fn)handle_window_is_maximized;
        case HASH_WIN_IS_MINIMIZED:      return (app_command_fn)handle_window_is_minimized;
        case HASH_DIALOG_OPEN_FILE:      return (app_command_fn)handle_dialog_open_file;
        case HASH_DIALOG_SAVE_FILE:      return (app_command_fn)handle_dialog_save_file;
        case HASH_FS_FILE_INFO:          return (app_command_fn)handle_fs_file_info;
        case HASH_FS_READ_CHUNK:         return (app_command_fn)handle_fs_read_chunk;
        case HASH_FS_WRITE_TEXT:         return (app_command_fn)handle_fs_write_text;
        case HASH_FS_READ_DIR:           return (app_command_fn)handle_fs_read_dir;
        case HASH_FS_CREATE_FILE:        return (app_command_fn)handle_fs_create_file;
        case HASH_FS_CREATE_DIR:         return (app_command_fn)handle_fs_create_dir;
        case HASH_FS_REMOVE:             return (app_command_fn)handle_fs_remove;
        case HASH_FS_RENAME: // rename and move are same operations under the hood
        case HASH_FS_MOVE:               return (app_command_fn)handle_fs_rename;
        case HASH_CLIPBOARD_WRITE_TEXT:  return (app_command_fn)handle_clipboard_write;
        case HASH_CLIPBOARD_READ_TEXT:   return (app_command_fn)handle_clipboard_read;
        case HASH_CLIPBOARD_CLEAR:       return (app_command_fn)handle_clipboard_clear;
        case HASH_CLIPBOARD_HAS_TEXT:    return (app_command_fn)handle_clipboard_has_text;
        case HASH_DIALOG_OPEN_DIRECTORY: return (app_command_fn)handle_dialog_open_directory;
        case HASH_DIALOG_MESSAGE:        return (app_command_fn)handle_dialog_message;
        case HASH_DIALOG_CONFIRM:        return (app_command_fn)handle_dialog_confirm;
        case HASH_PATH_GET_SPECIAL:      return (app_command_fn)handle_path_get_special;
        case HASH_PATH_GET_ALL:          return (app_command_fn)handle_path_get_all;              
        case HASH_FS_GET_DRIVES:         return (app_command_fn)handle_fs_get_drives;
        case HASH_NOTIFICATION_SEND:     return (app_command_fn)handle_notification_send;
        case HASH_TRAY_CREATE:           return (app_command_fn)handle_tray_create;
        case HASH_TRAY_SET_TOOLTIP:      return (app_command_fn)handle_tray_set_tooltip;
        case HASH_TRAY_SET_MENU:         return (app_command_fn)handle_tray_set_menu;
        case HASH_TRAY_DESTROY:          return (app_command_fn)handle_tray_destroy;
        case HASH_PROCESS_EXEC:          return (app_command_fn)handle_process_exec;
        case HASH_PROCESS_SPAWN:         return (app_command_fn)handle_process_spawn;
        case HASH_PROCESS_KILL:          return (app_command_fn)handle_process_kill;
        default:
        {
            // User commands (search 64-slot cmd_table)
            uint32_t idx = hash & (CMD_TABLE_SIZE - 1);
        
            for (int i = 0; i < CMD_TABLE_SIZE; i++) 
            {
                uint32_t slot = (idx + i) & (CMD_TABLE_SIZE - 1);
                if (cmd_table[slot].handler == NULL) 
                    return NULL;
        
                if (cmd_table[slot].hash == hash && strcmp(cmd_table[slot].name, name) == 0)
                    return cmd_table[slot].handler;
            }
            return NULL;
        }
    }
}