#include "process.h"
#include "json_util.h"
#include "hash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ARGS 32
#define MAX_ACTIVE_PROCS 16
#define PID_TABLE_SIZE 32

static int g_active_pids[PID_TABLE_SIZE] = {0};
static int g_active_count = 0;

static int track_pid(int pid)
{
    if (pid <= 0 || g_active_count>=MAX_ACTIVE_PROCS) return 1;
    int idx = hash_u32((uint32_t)pid) >> 27;
    for(int i=0;i<PID_TABLE_SIZE;i++)
    {
        int slot = (idx + i) & (PID_TABLE_SIZE - 1);
        if(g_active_pids[slot]<=0)
        {
            g_active_pids[slot] = pid;
            g_active_count++;
            break;
        }
    }
    return 0;
}

static bool is_owned_pid(int pid)
{
    if (pid <= 0) return false;
    int idx = hash_u32((uint32_t)pid) >> 27;
    for (int i = 0; i < MAX_ACTIVE_PROCS; i++)
    {
        int slot = (idx + i) & (PID_TABLE_SIZE - 1);
        if (g_active_pids[slot] == 0) return false;
        if (g_active_pids[slot] == pid) return true;
    }
    return false;
}

void process_on_child_exit(int pid, int exit_code)
{
    int idx = hash_u32((uint32_t)pid) >> 27;
    for (int i = 0; i < MAX_ACTIVE_PROCS; i++)
    {
        int slot = (idx + i) & (PID_TABLE_SIZE - 1);
        if (g_active_pids[slot] == 0) break;
        if (g_active_pids[slot] == pid) 
        { 
            g_active_pids[slot] = -1;
            g_active_count--;
            break; 
        }
    }
    char payload[96];
    snprintf(payload, sizeof(payload), "{\"pid\":%d,\"exitCode\":%d}", pid, exit_code);
    platform_app_emit("process:exit", payload);
}

// Parse JSON string array "args":["a","b",...] into packed flat arena[4096]
static int parse_args_array(const char *json, const char *cmd, char *arena, size_t arena_cap, const char *argv_out[MAX_ARGS + 2])
{
    int argc = 0;
    size_t used = 0;
    argv_out[argc++] = cmd;

    const char *arr = strstr(json, "\"args\"");
    if (arr) arr = strchr(arr, '[');
    if (arr)
    {
        const char *p = arr + 1;
        while (*p && *p != ']' && argc <= MAX_ARGS)
        {
            while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ',') p++;
            if (*p == ']' || !*p) break;
            if (*p == '"')
            {
                const char *start = ++p;
                const char *end = start;
                bool has_escape = false;

                while (*end && *end != '"')
                {
                    if (*end == '\\' && *(end + 1))
                    {
                        has_escape = true;
                        end += 2;
                        continue;
                    }
                    end++;
                }

                size_t raw_len = (size_t)(end - start);
                if (used + raw_len >= arena_cap) break; // Arena full

                char *dst = &arena[used];
                argv_out[argc++] = dst;

                if (!has_escape)
                {
                    memcpy(dst, start, raw_len);
                    used += raw_len;
                }
                else
                {
                    for (const char *s = start; s < end; s++)
                    {
                        if (*s == '\\' && (s + 1) < end)
                        {
                            s++;
                            if (*s == 'n')      arena[used++] = '\n';
                            else if (*s == 'r') arena[used++] = '\r';
                            else if (*s == 't') arena[used++] = '\t';
                            else                arena[used++] = *s;
                        }
                        else arena[used++] = *s;
                    }
                }
                arena[used++] = '\0';
                p = (*end == '"') ? (end + 1) : end;
            }
            else
            {
                p++;
            }
        }
    }
    argv_out[argc] = NULL;
    return argc;
}

static const uint8_t JSON_ESC_LUT[256] = {
    [0x00]=1,[0x01]=1,[0x02]=1,[0x03]=1,[0x04]=1,[0x05]=1,[0x06]=1,[0x07]=1,
    [0x08]=1,[0x09]='t',[0x0a]='n',[0x0b]=1,[0x0c]=1,[0x0d]='r',[0x0e]=1,[0x0f]=1,
    [0x10]=1,[0x11]=1,[0x12]=1,[0x13]=1,[0x14]=1,[0x15]=1,[0x16]=1,[0x17]=1,
    [0x18]=1,[0x19]=1,[0x1a]=1,[0x1b]=1,[0x1c]=1,[0x1d]=1,[0x1e]=1,[0x1f]=1,
    ['"'] = '"', ['\\'] = '\\'
};

static size_t append_escaped_json_str(char *dst, size_t cap, size_t pos, const char *src)
{
    if (!src) return pos;
    const unsigned char *p = (const unsigned char *)src;

    while (*p && pos + 8 < cap)
    {
        const unsigned char *span = p;
        while (*p && JSON_ESC_LUT[*p] == 0) p++;

        size_t run = (size_t)(p - span);
        if (run > 0)
        {
            if (pos + run + 8 >= cap) run = (cap - 8) - pos;
            memcpy(dst + pos, span, run);
            pos += run;
        }

        if (!*p || pos + 8 >= cap) break;

        uint8_t esc = JSON_ESC_LUT[*p++];
        if (esc > 1)
        {
            dst[pos++] = '\\';
            dst[pos++] = (char)esc;
        }
    }
    return pos;
}

void handle_process_exec(const char *args, ipc_reply_t *reply)
{
    char cmd[128] = {0};
    if (!json_get_string(args, "cmd", cmd, sizeof(cmd)))
    {
        ipc_reply_error(reply, "Missing 'cmd' argument");
        return;
    }

    uint64_t timeout = json_get_uint64(args, "timeout", 5000);
    if (timeout == 0 || timeout > 30000) timeout = 30000;

    char arena[4096];
    const char *argv[MAX_ARGS + 2];
    parse_args_array(args, cmd, arena, sizeof(arena), argv);

    process_result_t res = { .exit_code = -1, .timed_out = false, .stdout_buf = NULL, .stderr_buf = NULL };
    if (!platform_process_exec(cmd, argv, (int)timeout, &res))
    {
        free(res.stdout_buf);
        free(res.stderr_buf);
        ipc_reply_error(reply, "Failed to execute process");
        return;
    }

    size_t out_len = res.stdout_buf ? strlen(res.stdout_buf) : 0;
    size_t err_len = res.stderr_buf ? strlen(res.stderr_buf) : 0;
    size_t cap = (out_len + err_len) * 2 + 128;
    if (cap > 512 * 1024) cap = 512 * 1024;

    char stack_buf[4096];
    char *json = (cap <= sizeof(stack_buf)) ? stack_buf : malloc(cap);

    if (json)
    {
        size_t p = (size_t)snprintf(json, cap, "{\"exitCode\":%d,\"timedOut\":%s,\"stdout\":\"",res.exit_code, res.timed_out ? "true" : "false");
        p = append_escaped_json_str(json, cap-32, p, res.stdout_buf);
        p += (size_t)snprintf(json + p, cap - p, "\",\"stderr\":\"");
        p = append_escaped_json_str(json, cap, p, res.stderr_buf);
        snprintf(json + p, cap - p, "\"}");
        ipc_reply_send(reply, json);
        if(json != stack_buf) free(json);
    }
    else
    {
        ipc_reply_error(reply, "Out of memory");
    }

    free(res.stdout_buf);
    free(res.stderr_buf);
}

void handle_process_spawn(const char *args, ipc_reply_t *reply)
{
    char cmd[128] = {0};
    if (!json_get_string(args, "cmd", cmd, sizeof(cmd)))
    {
        ipc_reply_error(reply, "Missing 'cmd' argument");
        return;
    }

    if(g_active_count>=MAX_ACTIVE_PROCS)
    {
        ipc_reply_error(reply,"Max concurrent child processes reached (16)");
        return;
    }
    char arena[4096];
    const char *argv[MAX_ARGS + 2];
    parse_args_array(args, cmd, arena, sizeof(arena), argv);


    int pid = platform_process_spawn(cmd, argv);
    if (pid <= 0)
    {
        ipc_reply_error(reply, "Failed to spawn process");
        return;
    }

    if (track_pid(pid) != 0)
    {
        platform_process_kill(pid); // Never leave an untracked process alive!
        ipc_reply_error(reply, "Failed to track child process");
        return;
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"pid\":%d}", pid);
    ipc_reply_send(reply, buf);
}

void handle_process_kill(const char *args, ipc_reply_t *reply)
{
    int pid = (int)json_get_uint64(args, "pid", 0);
    if (!is_owned_pid(pid))
    {
        ipc_reply_error(reply, "Kill denied: PID not owned by Vyom");
        return;
    }

    bool ok = platform_process_kill(pid);
    ipc_reply_send(reply, ok ? "{\"success\":true}" : "{\"success\":false}");
}

void process_cleanup_all(void)
{
    for(int i=0;i<PID_TABLE_SIZE;i++)
    {
        if(g_active_pids[i]>0)
        {
            platform_process_kill(g_active_pids[i]);
            g_active_pids[i] = -1;
        }
    }
    g_active_count = 0;
}