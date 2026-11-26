#include "internal.h"
#include "process.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

typedef struct {
    GCancellable *cancel;
    gboolean timed_out;
} exec_timeout_ctx_t;

static gboolean on_exec_timeout(gpointer user_data)
{
    exec_timeout_ctx_t *ctx = (exec_timeout_ctx_t *)user_data;
    ctx->timed_out = TRUE;
    g_cancellable_cancel(ctx->cancel);
    return G_SOURCE_REMOVE;
}

bool platform_process_exec(const char *cmd, const char *const *argv, int timeout_ms, process_result_t *out)
{
    (void)cmd;
    GError *err = NULL;
    GSubprocess *proc = g_subprocess_newv(argv,G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE,&err);

    if (!proc)
    {
        if (err) g_error_free(err);
        return false;
    }

    exec_timeout_ctx_t tctx = { .cancel = g_cancellable_new(), .timed_out = FALSE };
    guint timer_id = g_timeout_add((guint)timeout_ms, on_exec_timeout, &tctx);

    char *stdout_str = NULL;
    char *stderr_str = NULL;
    g_subprocess_communicate_utf8(proc, NULL, tctx.cancel, &stdout_str, &stderr_str, &err);

    g_source_remove(timer_id);
    if (tctx.timed_out)
    {
        g_subprocess_force_exit(proc);
    }
    if (err) g_error_free(err);

    out->timed_out = tctx.timed_out ? true : false;
    out->exit_code = g_subprocess_get_if_exited(proc) ? g_subprocess_get_exit_status(proc) : -1;
    out->stdout_buf = stdout_str;
    out->stderr_buf = stderr_str;

    g_object_unref(tctx.cancel);
    g_object_unref(proc);
    return true;
}

typedef struct {
    int pid;
    const char *event_name;
    GInputStream *stream;
    char buf[4096];
} pipe_read_ctx_t;

static void emit_pipe_chunk(const char *event, int pid, const char *data, gssize len)
{
    size_t cap = (size_t)len * 2 + 64;
    char *json = malloc(cap);
    if (!json) return;

    size_t p = (size_t)snprintf(json, cap, "{\"pid\":%d,\"data\":\"", pid);
    for (gssize i = 0; i < len && p + 8 < cap; i++)
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
    platform_app_emit(event, json);
    free(json);
}

static void on_pipe_read(GObject *source, GAsyncResult *res, gpointer user_data)
{
    pipe_read_ctx_t *ctx = (pipe_read_ctx_t *)user_data;
    GError *err = NULL;
    gssize n = g_input_stream_read_finish(G_INPUT_STREAM(source), res, &err);

    if (n > 0)
    {
        emit_pipe_chunk(ctx->event_name, ctx->pid, ctx->buf, n);
        g_input_stream_read_async(ctx->stream, ctx->buf, sizeof(ctx->buf), G_PRIORITY_DEFAULT, NULL, on_pipe_read, ctx);
        return;
    }

    if (err) g_error_free(err);
    g_object_unref(ctx->stream);
    free(ctx);
}

static void on_spawn_wait(GObject *source, GAsyncResult *res, gpointer user_data)
{
    GSubprocess *proc = G_SUBPROCESS(source);
    int pid = GPOINTER_TO_INT(user_data);
    GError *err = NULL;
    g_subprocess_wait_finish(proc, res, &err);
    if (err) g_error_free(err);

    int code = g_subprocess_get_if_exited(proc) ? g_subprocess_get_exit_status(proc) : -1;
    process_on_child_exit(pid, code);
    g_object_unref(proc);
}

int platform_process_spawn(const char *cmd, const char *const *argv)
{
    (void)cmd;
    GError *err = NULL;
    GSubprocess *proc = g_subprocess_newv(argv,G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE,&err);

    if (!proc)
    {
        if (err) g_error_free(err);
        return -1;
    }

    const char *pid_str = g_subprocess_get_identifier(proc);
    int pid = pid_str ? atoi(pid_str) : -1;

    GInputStream *out_s = g_subprocess_get_stdout_pipe(proc);
    if (out_s)
    {
        pipe_read_ctx_t *ctx = malloc(sizeof(pipe_read_ctx_t));
        if (ctx)
        {
            ctx->pid = pid;
            ctx->event_name = "process:stdout";
            ctx->stream = g_object_ref(out_s);
            g_input_stream_read_async(ctx->stream, ctx->buf, sizeof(ctx->buf),G_PRIORITY_DEFAULT, NULL, on_pipe_read, ctx);
        }
    }

    GInputStream *err_s = g_subprocess_get_stderr_pipe(proc);
    if (err_s)
    {
        pipe_read_ctx_t *ctx = malloc(sizeof(pipe_read_ctx_t));
        if (ctx)
        {
            ctx->pid = pid;
            ctx->event_name = "process:stderr";
            ctx->stream = g_object_ref(err_s);
            g_input_stream_read_async(ctx->stream, ctx->buf, sizeof(ctx->buf),G_PRIORITY_DEFAULT, NULL, on_pipe_read, ctx);
        }
    }

    g_subprocess_wait_async(proc, NULL, on_spawn_wait, GINT_TO_POINTER(pid));
    return pid;
}

bool platform_process_kill(int pid)
{
    if (pid <= 0) return false;
    return kill((pid_t)pid, SIGTERM) == 0;
}