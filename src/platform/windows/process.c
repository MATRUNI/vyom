#include "internal.h"
#include "process.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static HANDLE g_job = NULL;

static void ensure_job_object(void)
{
    if (g_job) return;
    g_job = CreateJobObjectW(NULL, NULL);
    if (g_job)
    {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = {0};
        jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(g_job, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
    }
}

static void build_cmdline_w(const char *const *argv, wchar_t *out, size_t max_chars)
{
    size_t pos = 0;
    for (int i = 0; argv && argv[i] && pos + 4 < max_chars; i++)
    {
        if (i > 0) out[pos++] = L' ';

        wchar_t arg_w[1024];
        int wlen = MultiByteToWideChar(CP_UTF8, 0, argv[i], -1, arg_w, 1024);
        if (wlen <= 0) continue;

        bool needs_quote = (wcschr(arg_w, L' ') != NULL || wcschr(arg_w, L'\t') != NULL || arg_w[0] == L'\0');
        if (needs_quote && pos + 1 < max_chars) out[pos++] = L'"';

        for (int j = 0; arg_w[j] && pos + 3 < max_chars; j++)
        {
            if (arg_w[j] == L'"') out[pos++] = L'\\';
            out[pos++] = arg_w[j];
        }

        if (needs_quote && pos + 1 < max_chars) out[pos++] = L'"';
    }
    out[pos] = L'\0';
}

// -----------------------------------------------------------------------------
// EXEC (Wait with timeout + threaded pipe readers to prevent pipe deadlock)
// -----------------------------------------------------------------------------
typedef struct {
    HANDLE hRead;
    char *buf;
    size_t cap;
    size_t len;
} exec_pipe_t;

static DWORD WINAPI exec_pipe_reader(LPVOID param)
{
    exec_pipe_t *p = (exec_pipe_t *)param;
    p->cap = 4096;
    p->len = 0;
    p->buf = malloc(p->cap);
    if (!p->buf) return 0;

    DWORD n = 0;
    char tmp[4096];
    while (ReadFile(p->hRead, tmp, sizeof(tmp), &n, NULL) && n > 0)
    {
        if (p->len + n + 1 > p->cap)
        {
            size_t new_cap = p->cap * 2;
            while (new_cap < p->len + n + 1) new_cap *= 2;
            if (new_cap > 512 * 1024) break;
            char *nb = realloc(p->buf, new_cap);
            if (!nb) break;
            p->buf = nb;
            p->cap = new_cap;
        }
        memcpy(p->buf + p->len, tmp, n);
        p->len += n;
    }
    p->buf[p->len] = '\0';
    return 0;
}

bool platform_process_exec(const char *cmd, const char *const *argv, int timeout_ms, process_result_t *out)
{
    (void)cmd;
    ensure_job_object();

    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
    HANDLE out_r = NULL, out_w = NULL, err_r = NULL, err_w = NULL;
    if (!CreatePipe(&out_r, &out_w, &sa, 0) || !CreatePipe(&err_r, &err_w, &sa, 0))
    {
        if (out_r) CloseHandle(out_r);
        if (out_w) CloseHandle(out_w);
        return false;
    }
    SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(err_r, HANDLE_FLAG_INHERIT, 0);

    wchar_t cmdline[4096];
    build_cmdline_w(argv, cmdline, 4096);

    STARTUPINFOW si = {0};
    si.cb = sizeof(STARTUPINFOW);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = out_w;
    si.hStdError  = err_w;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi = {0};
    BOOL ok = CreateProcessW(NULL, cmdline, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);

    // Must close write ends in parent immediately so ReadFile gets EOF!
    CloseHandle(out_w);
    CloseHandle(err_w);

    if (!ok)
    {
        CloseHandle(out_r);
        CloseHandle(err_r);
        return false;
    }

    if (g_job) AssignProcessToJobObject(g_job, pi.hProcess);

    exec_pipe_t p_out = { .hRead = out_r };
    exec_pipe_t p_err = { .hRead = err_r };
    HANDLE t_out = CreateThread(NULL, 0, exec_pipe_reader, &p_out, 0, NULL);
    HANDLE t_err = CreateThread(NULL, 0, exec_pipe_reader, &p_err, 0, NULL);

    DWORD wait = WaitForSingleObject(pi.hProcess, (DWORD)timeout_ms);
    out->timed_out = (wait == WAIT_TIMEOUT);
    if (out->timed_out)
        TerminateProcess(pi.hProcess, 1);

    if (t_out) { WaitForSingleObject(t_out, INFINITE); CloseHandle(t_out); }
    if (t_err) { WaitForSingleObject(t_err, INFINITE); CloseHandle(t_err); }

    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    out->exit_code = out->timed_out ? -1 : (int)code;
    out->stdout_buf = p_out.buf;
    out->stderr_buf = p_err.buf;

    CloseHandle(out_r);
    CloseHandle(err_r);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

// -----------------------------------------------------------------------------
// SPAWN (Background pipe/wait threads -> PostMessageW to UI thread)
// -----------------------------------------------------------------------------
typedef struct {
    bool is_exit;
    int pid;
    int exit_code;
    const char *event_name;
    char *json_payload;
} proc_ui_msg_t;

typedef struct {
    int pid;
    const char *event_name;
    HANDLE hRead;
} spawn_pipe_t;

typedef struct {
    int pid;
    HANDLE hProcess;
} spawn_wait_t;

static void post_ui_chunk(const char *event, int pid, const char *data, DWORD len)
{
    size_t cap = (size_t)len * 2 + 64;
    char *json = malloc(cap);
    if (!json) return;

    size_t p = (size_t)snprintf(json, cap, "{\"pid\":%d,\"data\":\"", pid);
    for (DWORD i = 0; i < len && p + 8 < cap; i++)
    {
        unsigned char c = (unsigned char)data[i];
        if (c == '"')       { json[p++] = '\\'; json[p++] = '"'; }
        else if (c == '\\') { json[p++] = '\\'; json[p++] = '\\'; }
        else if (c == '\n') { json[p++] = '\\'; json[p++] = 'n'; }
        else if (c == '\r') { json[p++] = '\\'; json[p++] = 'r'; }
        else if (c == '\t') { json[p++] = '\\'; json[p++] = 't'; }
        else if (c >= 0x20) { json[p++] = (char)c; }
    }
    snprintf(json + p, cap - p, "\"}");

    proc_ui_msg_t *msg = malloc(sizeof(proc_ui_msg_t));
    if (msg && g_hwnd)
    {
        msg->is_exit = false;
        msg->pid = pid;
        msg->event_name = event;
        msg->json_payload = json;
        if (!PostMessageW(g_hwnd, WM_PROCESS_EVENT, 0, (LPARAM)msg))
        {
            free(json);
            free(msg);
        }
    }
    else
    {
        free(json);
        free(msg);
    }
}

static DWORD WINAPI spawn_pipe_reader(LPVOID param)
{
    spawn_pipe_t *sp = (spawn_pipe_t *)param;
    char buf[4096];
    DWORD n = 0;
    while (ReadFile(sp->hRead, buf, sizeof(buf), &n, NULL) && n > 0)
        post_ui_chunk(sp->event_name, sp->pid, buf, n);

    CloseHandle(sp->hRead);
    free(sp);
    return 0;
}

static DWORD WINAPI spawn_exit_waiter(LPVOID param)
{
    spawn_wait_t *sw = (spawn_wait_t *)param;
    WaitForSingleObject(sw->hProcess, INFINITE);

    DWORD code = 0;
    GetExitCodeProcess(sw->hProcess, &code);
    CloseHandle(sw->hProcess);

    proc_ui_msg_t *msg = malloc(sizeof(proc_ui_msg_t));
    if (msg && g_hwnd)
    {
        msg->is_exit = true;
        msg->pid = sw->pid;
        msg->exit_code = (int)code;
        msg->event_name = NULL;
        msg->json_payload = NULL;
        if (!PostMessageW(g_hwnd, WM_PROCESS_EVENT, 0, (LPARAM)msg))
            free(msg);
    }
    else
    {
        free(msg);
    }

    free(sw);
    return 0;
}

bool win32_process_handle_message(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    (void)wParam;
    if (uMsg != WM_PROCESS_EVENT) return false;

    proc_ui_msg_t *msg = (proc_ui_msg_t *)lParam;
    if (msg)
    {
        if (msg->is_exit)
            process_on_child_exit(msg->pid, msg->exit_code);
        else
        {
            platform_app_emit(msg->event_name, msg->json_payload);
            free(msg->json_payload);
        }
        free(msg);
    }
    return true;
}

int platform_process_spawn(const char *cmd, const char *const *argv)
{
    (void)cmd;
    ensure_job_object();

    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
    HANDLE out_r = NULL, out_w = NULL, err_r = NULL, err_w = NULL;
    if (!CreatePipe(&out_r, &out_w, &sa, 0) || !CreatePipe(&err_r, &err_w, &sa, 0))
    {
        if (out_r) CloseHandle(out_r);
        if (out_w) CloseHandle(out_w);
        return -1;
    }
    SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(err_r, HANDLE_FLAG_INHERIT, 0);

    wchar_t cmdline[4096];
    build_cmdline_w(argv, cmdline, 4096);

    STARTUPINFOW si = {0};
    si.cb = sizeof(STARTUPINFOW);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = out_w;
    si.hStdError  = err_w;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi = {0};
    BOOL ok = CreateProcessW(NULL, cmdline, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    CloseHandle(out_w);
    CloseHandle(err_w);

    if (!ok)
    {
        CloseHandle(out_r);
        CloseHandle(err_r);
        return -1;
    }

    if (g_job) AssignProcessToJobObject(g_job, pi.hProcess);
    CloseHandle(pi.hThread);

    int pid = (int)pi.dwProcessId;

    spawn_pipe_t *sp_out = malloc(sizeof(spawn_pipe_t));
    if (sp_out)
    {
        sp_out->pid = pid;
        sp_out->event_name = "process:stdout";
        sp_out->hRead = out_r;
        HANDLE t = CreateThread(NULL, 0, spawn_pipe_reader, sp_out, 0, NULL);
        if (t) CloseHandle(t);
    }
    else CloseHandle(out_r);

    spawn_pipe_t *sp_err = malloc(sizeof(spawn_pipe_t));
    if (sp_err)
    {
        sp_err->pid = pid;
        sp_err->event_name = "process:stderr";
        sp_err->hRead = err_r;
        HANDLE t = CreateThread(NULL, 0, spawn_pipe_reader, sp_err, 0, NULL);
        if (t) CloseHandle(t);
    }
    else CloseHandle(err_r);

    spawn_wait_t *sw = malloc(sizeof(spawn_wait_t));
    if (sw)
    {
        sw->pid = pid;
        sw->hProcess = pi.hProcess;
        HANDLE t = CreateThread(NULL, 0, spawn_exit_waiter, sw, 0, NULL);
        if (t) CloseHandle(t);
    }
    else CloseHandle(pi.hProcess);

    return pid;
}

bool platform_process_kill(int pid)
{
    if (pid <= 0) return false;
    HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, (DWORD)pid);
    if (!h) return false;
    BOOL ok = TerminateProcess(h, 1);
    CloseHandle(h);
    return ok != 0;
}