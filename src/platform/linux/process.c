#include "internal.h"
#include "process.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/prctl.h>
#include <unistd.h>

typedef struct {
    GCancellable *cancel;
    gboolean done;
    gboolean timed_out;
    char *stdout_str;
    char *stderr_str;
} exec_ctx_t;

static gboolean on_exec_timeout(gpointer user_data)
{
    exec_ctx_t *ctx = (exec_ctx_t *)user_data;
    ctx->timed_out = TRUE;
    g_cancellable_cancel(ctx->cancel);
    return G_SOURCE_REMOVE; // Auto-removes timer!
}

static void on_exec_finish(GObject *source, GAsyncResult *res, gpointer user_data)
{
    exec_ctx_t *ctx = (exec_ctx_t *)user_data;
    GError *err = NULL;
    g_subprocess_communicate_utf8_finish(G_SUBPROCESS(source), res, &ctx->stdout_str, &ctx->stderr_str, &err);
    if (err) g_error_free(err);
    ctx->done = TRUE;
}

static void on_child_setup(gpointer user_data)
{
    (void)user_data;
    // Kernel kills child immediately if parent Vyom process ever dies or crashes
    prctl(PR_SET_PDEATHSIG, SIGKILL);
    setpgid(0,0);
}

static GSubprocess* launch_child(const char *const *argv, GError **err)
{
    GSubprocessLauncher *launcher = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE);
    g_subprocess_launcher_set_child_setup(launcher, on_child_setup, NULL, NULL);
    GSubprocess *proc = g_subprocess_launcher_spawnv(launcher, argv, err);
    g_object_unref(launcher);
    return proc;
}

bool platform_process_exec(const char *cmd, const char *const *argv, int timeout_ms, process_result_t *out)
{
    (void)cmd;
    GError *err = NULL;
    GSubprocess *proc = launch_child(argv, &err);

    if (!proc)
    {
        if (err) g_error_free(err);
        return false;
    }

    exec_ctx_t ctx = { .cancel = g_cancellable_new(), .done = FALSE, .timed_out = FALSE, .stdout_str = NULL, .stderr_str = NULL };
    guint timer_id = g_timeout_add((guint)timeout_ms, on_exec_timeout, &ctx);

    g_subprocess_communicate_utf8_async(proc, NULL, ctx.cancel, on_exec_finish, &ctx);
    while (!ctx.done)
        g_main_context_iteration(NULL, TRUE);

    // Only remove timer if it didn't already fire & auto-remove itself
    if (!ctx.timed_out)
        g_source_remove(timer_id);
    else
    {
        g_subprocess_force_exit(proc);
        g_subprocess_wait(proc, NULL, NULL);
    }

    out->timed_out = ctx.timed_out ? true : false;
    out->exit_code = g_subprocess_get_if_exited(proc) ? g_subprocess_get_exit_status(proc) : -1;
    out->stdout_buf = ctx.stdout_str;
    out->stderr_buf = ctx.stderr_str;

    g_object_unref(ctx.cancel);
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
    GSubprocess *proc = launch_child(argv, &err);

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
    return kill((pid_t)-pid, SIGTERM) == 0;
}
