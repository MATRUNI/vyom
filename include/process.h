#ifndef PROCESS_H
#define PROCESS_H

#include "platform.h"

void handle_process_exec(const char *args, ipc_reply_t *reply);
void handle_process_spawn(const char *args, ipc_reply_t *reply);
void handle_process_kill(const char *args, ipc_reply_t *reply);

// Called by platform backends when a spawned process exits
void process_on_child_exit(int pid, int exit_code);

#endif