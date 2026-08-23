#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*IpcFixCallback)(void);

// Sends a FIX command to an already running tinygrammar instance.
// Returns 0 on success, -1 if no instance is running.
int ipc_send_fix_command(void);

// Starts the IPC server on the GLib main loop.
// Calls on_fix when a FIX command is received.
int ipc_server_start(IpcFixCallback on_fix);

// Stops the IPC server and removes the socket file.
void ipc_server_stop(void);

#ifdef __cplusplus
}
#endif
