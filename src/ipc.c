#include "ipc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <glib.h>

static char g_sock_path[512] = {0};
static int g_server_fd = -1;
static guint g_io_watch_id = 0;
static IpcFixCallback g_on_fix = NULL;

static const char* get_socket_path(void) {
    if (g_sock_path[0] != '\0') return g_sock_path;

    const char* runtime_dir = getenv("XDG_RUNTIME_DIR");
    if (runtime_dir && strlen(runtime_dir) > 0) {
        snprintf(g_sock_path, sizeof(g_sock_path), "%s/tinygrammar.sock", runtime_dir);
    } else {
        snprintf(g_sock_path, sizeof(g_sock_path), "/tmp/tinygrammar-%d.sock", getuid());
    }
    return g_sock_path;
}

int ipc_send_fix_command(void) {
    const char* path = get_socket_path();
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    const char* msg = "FIX\n";
    ssize_t written = write(fd, msg, strlen(msg));
    close(fd);

    return (written > 0) ? 0 : -1;
}

static gboolean on_client_data(GIOChannel* source, GIOCondition condition, gpointer data) {
    if (condition & (G_IO_IN | G_IO_PRI)) {
        gchar buf[64];
        gsize bytes_read = 0;
        GError* error = NULL;
        GIOStatus status = g_io_channel_read_chars(source, buf, sizeof(buf) - 1, &bytes_read, &error);

        if (status == G_IO_STATUS_NORMAL && bytes_read > 0) {
            buf[bytes_read] = '\0';
            if (strncmp(buf, "FIX", 3) == 0 && g_on_fix) {
                g_on_fix();
            }
        }
        if (error) g_error_free(error);
    }

    // Client connection handled, close and remove channel
    g_io_channel_shutdown(source, TRUE, NULL);
    g_io_channel_unref(source);
    return G_SOURCE_REMOVE;
}

static gboolean on_server_incoming(GIOChannel* source, GIOCondition condition, gpointer data) {
    if (condition & (G_IO_IN | G_IO_PRI)) {
        int client_fd = accept(g_server_fd, NULL, NULL);
        if (client_fd >= 0) {
            GIOChannel* client_channel = g_io_channel_unix_new(client_fd);
            g_io_channel_set_close_on_unref(client_channel, TRUE);
            g_io_channel_set_encoding(client_channel, NULL, NULL);
            g_io_add_watch(client_channel, G_IO_IN | G_IO_HUP | G_IO_ERR, on_client_data, NULL);
        }
    }
    return G_SOURCE_CONTINUE;
}

int ipc_server_start(IpcFixCallback on_fix) {
    g_on_fix = on_fix;
    const char* path = get_socket_path();

    // Remove stale socket if present
    unlink(path);

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    if (listen(fd, 5) < 0) {
        close(fd);
        unlink(path);
        return -1;
    }

    g_server_fd = fd;
    GIOChannel* channel = g_io_channel_unix_new(fd);
    g_io_channel_set_close_on_unref(channel, FALSE);
    g_io_watch_id = g_io_add_watch(channel, G_IO_IN, on_server_incoming, NULL);
    g_io_channel_unref(channel);

    return 0;
}

void ipc_server_stop(void) {
    if (g_io_watch_id > 0) {
        g_source_remove(g_io_watch_id);
        g_io_watch_id = 0;
    }
    if (g_server_fd >= 0) {
        close(g_server_fd);
        g_server_fd = -1;
    }
    const char* path = get_socket_path();
    if (path && path[0]) {
        unlink(path);
    }
}
