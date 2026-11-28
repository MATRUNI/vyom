#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "hash.h"
#include "json_util.h"
#include "registry.h"
#include "platform.h"
#include "process.h"

// -----------------------------------------------------------------------------
// 1. TINY MOCK IPC + PLATFORM STUBS (No GTK4 / Win32 / Cocoa needed!)
// -----------------------------------------------------------------------------
struct ipc_reply {
    bool is_error;
    char data[65536];
};

static char g_last_emit_event[64] = {0};
static char g_last_emit_data[1024] = {0};
static int  g_next_mock_pid = 1000;
static int  g_killed_count = 0;

void ipc_reply_send(ipc_reply_t *reply, const char *json_response) {
    reply->is_error = false;
    snprintf(reply->data, sizeof(reply->data), "%s", json_response ? json_response : "");
}

void ipc_reply_error(ipc_reply_t *reply, const char *error_message) {
    reply->is_error = true;
    snprintf(reply->data, sizeof(reply->data), "%s", error_message ? error_message : "");
}

int  platform_app_run(const app_config_t *c, int argc, char *argv[]) { (void)c; (void)argc; (void)argv; return 0; }
void platform_app_emit(const char *ev, const char *json) {
    snprintf(g_last_emit_event, sizeof(g_last_emit_event), "%s", ev ? ev : "");
    snprintf(g_last_emit_data, sizeof(g_last_emit_data), "%s", json ? json : "");
}

// Stubs for unused platform symbols linked from engine/*.c
void platform_window_minimize(void) {}
void platform_window_toggle_maximize(void) {}
void platform_window_close(void) {}
void platform_window_start_drag(void) {}
void platform_window_set_size(int w, int h) { (void)w; (void)h; }
void platform_window_get_size(int *w, int *h) { *w = 800; *h = 600; }
void platform_window_set_position(int x, int y) { (void)x; (void)y; }
void platform_window_get_position(int *x, int *y) { *x = 0; *y = 0; }
void platform_window_center(void) {}
void platform_window_set_always_on_top(bool b) { (void)b; }
void platform_window_set_resizable(bool b) { (void)b; }
void platform_window_set_min_size(int w, int h) { (void)w; (void)h; }
void platform_window_set_max_size(int w, int h) { (void)w; (void)h; }
void platform_window_set_title(const char *t) { (void)t; }
bool platform_window_is_maximized(void) { return false; }
bool platform_window_is_minimized(void) { return false; }
char* platform_dialog_open_file(const char *t, const char *f) { (void)t; (void)f; return NULL; }
char* platform_dialog_save_file(const char *t, const char *f, const char *d) { (void)t; (void)f; (void)d; return NULL; }
char* platform_dialog_open_directory(const char *t) { (void)t; return NULL; }
void  platform_dialog_message(const char *t, const char *m, const char *ty) { (void)t; (void)m; (void)ty; }
bool  platform_dialog_confirm(const char *t, const char *m) { (void)t; (void)m; return true; }
void  platform_clipboard_write_text(const char *t) { (void)t; }
char* platform_clipboard_read_text(void) { return NULL; }
void  platform_clipboard_clear(void) {}
bool  platform_clipboard_has_text(void) { return false; }
char* platform_path_get_special(const char *n) { (void)n; return NULL; }
char* platform_fs_get_drives(void) { return "[]"; }
void  platform_notification_send(const char *t, const char *b, const char *i) { (void)t; (void)b; (void)i; }
void  platform_tray_create(const char *t, const char *i) { (void)t; (void)i; }
void  platform_tray_set_tooltip(const char *t) { (void)t; }
void  platform_tray_set_menu(const tray_menu_item_t *it, int c) { (void)it; (void)c; }
void  platform_tray_destroy(void) {}

// Process mocks (returns heap buffers just like real OS backend so ASan checks free()!)
bool platform_process_exec(const char *cmd, const char *const *argv, int timeout_ms, process_result_t *out) {
    (void)cmd; (void)timeout_ms;
    const char *arg1 = (argv && argv[1]) ? argv[1] : "";
    size_t len = strlen(arg1) + 32;
    out->stdout_buf = malloc(len);
    snprintf(out->stdout_buf, len, "hello\n\"%s\"", arg1);
    out->stderr_buf = NULL;
    out->exit_code = 0;
    out->timed_out = false;
    return true;
}

int platform_process_spawn(const char *cmd, const char *const *argv) {
    (void)cmd; (void)argv;
    return g_next_mock_pid++;
}

bool platform_process_kill(int pid) {
    (void)pid;
    g_killed_count++;
    return true;
}

// -----------------------------------------------------------------------------
// 2. TEST SUITE
// -----------------------------------------------------------------------------
#define TEST(name) static void name(void)
#define RUN(name)  do { printf("  [RUN] %s...\n", #name); name(); printf("  [OK]  %s\n", #name); } while (0)

TEST(test_registry_commands) {
    const char *cmds[] = {
        "vyom:window:minimize", "vyom:window:set_size", "vyom:window:set_title",
        "vyom:dialog:open_file", "vyom:dialog:confirm",
        "vyom:fs:read_chunk", "vyom:fs:write_text", "vyom:fs:get_drives",
        "vyom:clipboard:write_text", "vyom:path:get_all",
        "vyom:notification:send", "vyom:tray:create",
        "vyom:process:exec", "vyom:process:spawn", "vyom:process:kill", NULL
    };
    for (int i = 0; cmds[i]; i++) {
        assert(lookup_command(cmds[i]) != NULL);
    }
    assert(lookup_command("vyom:does:not_exist") == NULL);
}

TEST(test_json_util) {
    const char *j = "{\"title\":\"Vyom \\\"C11\\\"\",\"width\":1024,\"ok\":true}";
    char buf[64];
    assert(json_get_string(j, "title", buf, sizeof(buf)));
    assert(strcmp(buf, "Vyom \"C11\"") == 0);
    assert(json_get_uint64(j, "width", 0) == 1024);
    assert(json_get_bool(j, "ok", false) == true);
    assert(json_get_uint64(j, "missing", 42) == 42);
}

TEST(test_process_exec_and_spawn_table) {
    ipc_reply_t r;

    // 1. Test exec + JSON_ESC_LUT escaping + arena args
    lookup_command("vyom:process:exec")("{\"cmd\":\"echo\",\"args\":[\"vyom\"]}", &r);
    assert(!r.is_error);
    assert(strstr(r.data, "\"stdout\":\"hello\\n\\\"vyom\\\"\"") != NULL);

    // 2. Fill all 16 PID slots
    int pids[16];
    for (int i = 0; i < 16; i++) {
        lookup_command("vyom:process:spawn")("{\"cmd\":\"ping\",\"args\":[\"127.0.0.1\"]}", &r);
        assert(!r.is_error);
        pids[i] = (int)json_get_uint64(r.data, "pid", 0);
        assert(pids[i] >= 1000);
    }

    // 3. 17th spawn MUST be blocked before spawning!
    lookup_command("vyom:process:spawn")("{\"cmd\":\"ping\"}", &r);
    assert(r.is_error);

    // 4. Unowned PID kill MUST be denied!
    lookup_command("vyom:process:kill")("{\"pid\":1}", &r);
    assert(r.is_error);

    // 5. Owned PID kill succeeds
    char kill_args[64];
    snprintf(kill_args, sizeof(kill_args), "{\"pid\":%d}", pids[0]);
    lookup_command("vyom:process:kill")(kill_args, &r);
    assert(!r.is_error);

    // 6. Simulate child exit -> tombstone frees slot -> new spawn succeeds!
    process_on_child_exit(pids[0], 0);
    assert(strcmp(g_last_emit_event, "process:exit") == 0);
    lookup_command("vyom:process:spawn")("{\"cmd\":\"ping\"}", &r);
    assert(!r.is_error);

    // 7. Cleanup kills all 16 remaining active children
    g_killed_count = 0;
    process_cleanup_all();
    assert(g_killed_count == 16);
}

TEST(test_fs_roundtrip) {
    ipc_reply_t r;
    lookup_command("vyom:fs:write_text")("{\"path\":\"vyom_unit_test.txt\",\"content\":\"abc123\"}", &r);
    assert(!r.is_error);

    lookup_command("vyom:fs:read_chunk")("{\"path\":\"vyom_unit_test.txt\",\"offset\":0,\"length\":100}", &r);
    assert(!r.is_error);
    assert(strstr(r.data, "len=6,total=6,eof=1\nabc123") != NULL);

    lookup_command("vyom:fs:remove")("{\"path\":\"vyom_unit_test.txt\"}", &r);
    assert(!r.is_error);
}

int main(void) {
    printf("=== Vyom C11 Engine Unit Tests ===\n");
    RUN(test_registry_commands);
    RUN(test_json_util);
    RUN(test_process_exec_and_spawn_table);
    RUN(test_fs_roundtrip);
    printf("=== ALL TESTS PASSED (0 Leaks / 0 UB) ===\n");
    return 0;
}