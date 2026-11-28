#import "internal.h"
#include "process.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

static NSArray<NSString *>* build_env_args(const char *const *argv)
{
    NSMutableArray<NSString *> *arr = [NSMutableArray array];
    for (int i = 0; argv && argv[i]; i++)
    {
        NSString *s = [NSString stringWithUTF8String:argv[i]];
        if (s) [arr addObject:s];
    }
    return arr;
}

static void emit_ns_chunk(const char *event, int pid, NSData *data)
{
    if (!data || data.length == 0) return;
    const unsigned char *bytes = (const unsigned char *)data.bytes;
    NSUInteger len = data.length;

    size_t cap = (size_t)len * 2 + 64;
    char *json = malloc(cap);
    if (!json) return;

    size_t p = (size_t)snprintf(json, cap, "{\"pid\":%d,\"data\":\"", pid);
    for (NSUInteger i = 0; i < len && p + 8 < cap; i++)
    {
        unsigned char c = bytes[i];
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

bool platform_process_exec(const char *cmd, const char *const *argv, int timeout_ms, process_result_t *out)
{
    (void)cmd;
    NSTask *task = [[NSTask alloc] init];
    task.executableURL = [NSURL fileURLWithPath:@"/usr/bin/env"];
    task.arguments = build_env_args(argv);

    NSPipe *outPipe = [NSPipe pipe];
    NSPipe *errPipe = [NSPipe pipe];
    task.standardOutput = outPipe;
    task.standardError = errPipe;

    NSMutableData *outData = [NSMutableData data];
    NSMutableData *errData = [NSMutableData data];
    dispatch_group_t pipeGroup = dispatch_group_create();
    dispatch_semaphore_t exitSem = dispatch_semaphore_create(0);

    dispatch_group_enter(pipeGroup);
    outPipe.fileHandleForReading.readabilityHandler = ^(NSFileHandle *h) {
        NSData *d = [h availableData];
        if (d.length == 0) {
            h.readabilityHandler = nil;
            dispatch_group_leave(pipeGroup);
        } else {
            @synchronized(outData) { [outData appendData:d]; }
        }
    };

    dispatch_group_enter(pipeGroup);
    errPipe.fileHandleForReading.readabilityHandler = ^(NSFileHandle *h) {
        NSData *d = [h availableData];
        if (d.length == 0) {
            h.readabilityHandler = nil;
            dispatch_group_leave(pipeGroup);
        } else {
            @synchronized(errData) { [errData appendData:d]; }
        }
    };

    task.terminationHandler = ^(NSTask *t) {
        (void)t;
        dispatch_semaphore_signal(exitSem);
    };

    NSError *err = nil;
    if (![task launchAndReturnError:&err])
    {
        outPipe.fileHandleForReading.readabilityHandler = nil;
        errPipe.fileHandleForReading.readabilityHandler = nil;
        return false;
    }

    dispatch_time_t deadline = dispatch_time(DISPATCH_TIME_NOW, (int64_t)timeout_ms * NSEC_PER_MSEC);
    long waitRes = dispatch_semaphore_wait(exitSem, deadline);
    out->timed_out = (waitRes != 0);
    if (out->timed_out)
    {
        [task terminate];
        dispatch_semaphore_wait(exitSem, dispatch_time(DISPATCH_TIME_NOW, 1000 * NSEC_PER_MSEC));
    }

    dispatch_group_wait(pipeGroup, dispatch_time(DISPATCH_TIME_NOW, 500 * NSEC_PER_MSEC));
    outPipe.fileHandleForReading.readabilityHandler = nil;
    errPipe.fileHandleForReading.readabilityHandler = nil;

    out->exit_code = out->timed_out ? -1 : (int)task.terminationStatus;

    @synchronized(outData) {
        out->stdout_buf = malloc(outData.length + 1);
        if (out->stdout_buf) {
            memcpy(out->stdout_buf, outData.bytes, outData.length);
            out->stdout_buf[outData.length] = '\0';
        }
    }
    @synchronized(errData) {
        out->stderr_buf = malloc(errData.length + 1);
        if (out->stderr_buf) {
            memcpy(out->stderr_buf, errData.bytes, errData.length);
            out->stderr_buf[errData.length] = '\0';
        }
    }
    return true;
}

int platform_process_spawn(const char *cmd, const char *const *argv)
{
    (void)cmd;
    NSTask *task = [[NSTask alloc] init];
    task.executableURL = [NSURL fileURLWithPath:@"/usr/bin/env"];
    task.arguments = build_env_args(argv);

    NSPipe *outPipe = [NSPipe pipe];
    NSPipe *errPipe = [NSPipe pipe];
    task.standardOutput = outPipe;
    task.standardError = errPipe;

    NSError *err = nil;
    if (![task launchAndReturnError:&err])
        return -1;

    int pid = (int)task.processIdentifier;

    outPipe.fileHandleForReading.readabilityHandler = ^(NSFileHandle *h) {
        NSData *d = [h availableData];
        if (d.length == 0) h.readabilityHandler = nil;
        else emit_ns_chunk("process:stdout", pid, d);
    };

    errPipe.fileHandleForReading.readabilityHandler = ^(NSFileHandle *h) {
        NSData *d = [h availableData];
        if (d.length == 0) h.readabilityHandler = nil;
        else emit_ns_chunk("process:stderr", pid, d);
    };

    task.terminationHandler = ^(NSTask *t) {
        int code = (int)t.terminationStatus;
        dispatch_async(dispatch_get_main_queue(), ^{
            process_on_child_exit(pid, code);
        });
    };

    return pid;
}

bool platform_process_kill(int pid)
{
    if (pid <= 0) return false;
    return kill((pid_t)pid, SIGTERM) == 0;
}