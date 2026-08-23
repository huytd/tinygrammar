#include "hotkey.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <glob.h>
#include <poll.h>
#include <pthread.h>
#include <linux/input.h>
#include <gio/gio.h>
#include <gdk/gdk.h>

#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#endif

#define BITS_PER_LONG (sizeof(long) * 8)
#define NBITS(x) ((((x)-1)/BITS_PER_LONG)+1)
#define OFF(x)  ((x)%BITS_PER_LONG)
#define BIT(x)  (1UL<<OFF(x))
#define LONG(x) ((x)/BITS_PER_LONG)
#define test_bit(bit, array) ((array[LONG(bit)] >> OFF(bit)) & 1)

static HotkeyTriggerCallback g_callback = NULL;
static pthread_t g_evdev_thread;
static int g_evdev_running = 0;
static int g_evdev_pipe[2] = {-1, -1};

static gboolean on_trigger_idle(gpointer user_data) {
    if (g_callback) {
        g_callback();
    }
    return G_SOURCE_REMOVE;
}

static int is_keyboard_device(int fd) {
    unsigned long ev_bits[NBITS(EV_MAX)] = {0};
    unsigned long key_bits[NBITS(KEY_MAX)] = {0};

    if (ioctl(fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits) < 0) return 0;
    if (!test_bit(EV_KEY, ev_bits)) return 0;
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) < 0) return 0;

    // Check if it has letter G and Super/Meta key
    if (test_bit(KEY_G, key_bits) && (test_bit(KEY_LEFTMETA, key_bits) || test_bit(KEY_RIGHTMETA, key_bits))) {
        return 1;
    }
    return 0;
}

static void* evdev_listener_thread(void* arg) {
    glob_t g;
    if (glob("/dev/input/event*", 0, NULL, &g) != 0) {
        return NULL;
    }

    int fds[32];
    int num_fds = 0;

    for (size_t i = 0; i < g.gl_pathc && num_fds < 30; i++) {
        int fd = open(g.gl_pathv[i], O_RDONLY | O_NONBLOCK);
        if (fd >= 0) {
            if (is_keyboard_device(fd)) {
                fds[num_fds++] = fd;
            } else {
                close(fd);
            }
        }
    }
    globfree(&g);

    if (num_fds == 0) {
        return NULL;
    }

    struct pollfd pfd[34];
    for (int i = 0; i < num_fds; i++) {
        pfd[i].fd = fds[i];
        pfd[i].events = POLLIN;
    }
    // Pipe for clean thread shutdown
    pfd[num_fds].fd = g_evdev_pipe[0];
    pfd[num_fds].events = POLLIN;
    int total_poll_fds = num_fds + 1;

    int meta_pressed = 0;
    struct input_event ev[64];

    while (g_evdev_running) {
        int ret = poll(pfd, total_poll_fds, 500);
        if (ret <= 0) continue;

        if (pfd[num_fds].revents & POLLIN) {
            break; // Shutdown signal received
        }

        for (int i = 0; i < num_fds; i++) {
            if (pfd[i].revents & POLLIN) {
                ssize_t len = read(pfd[i].fd, ev, sizeof(ev));
                if (len <= 0) continue;

                int count = len / sizeof(struct input_event);
                for (int j = 0; j < count; j++) {
                    if (ev[j].type == EV_KEY) {
                        int code = ev[j].code;
                        int val  = ev[j].value; // 0=release, 1=press, 2=repeat

                        if (code == KEY_LEFTMETA || code == KEY_RIGHTMETA) {
                            meta_pressed = (val > 0) ? 1 : 0;
                        } else if (code == KEY_G && val == 1) { // G pressed
                            if (meta_pressed) {
                                g_idle_add(on_trigger_idle, NULL);
                            }
                        }
                    }
                }
            }
        }
    }

    for (int i = 0; i < num_fds; i++) {
        close(fds[i]);
    }

    return NULL;
}

static void start_evdev_listener(void) {
    if (pipe(g_evdev_pipe) != 0) return;
    g_evdev_running = 1;
    pthread_create(&g_evdev_thread, NULL, evdev_listener_thread, NULL);
}

static void stop_evdev_listener(void) {
    if (g_evdev_running) {
        g_evdev_running = 0;
        if (g_evdev_pipe[1] >= 0) {
            char b = 1;
            ssize_t res = write(g_evdev_pipe[1], &b, 1);
            (void)res;
        }
        pthread_join(g_evdev_thread, NULL);
        close(g_evdev_pipe[0]);
        close(g_evdev_pipe[1]);
        g_evdev_pipe[0] = -1;
        g_evdev_pipe[1] = -1;
    }
}

static void register_gnome_shortcut(void) {
    char exe_path[1024];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len <= 0) return;
    exe_path[len] = '\0';

    char cmd[1200];
    snprintf(cmd, sizeof(cmd), "%s --fix", exe_path);

    GSettingsSchemaSource* source = g_settings_schema_source_get_default();
    if (!source) return;

    GSettingsSchema* parent_schema = g_settings_schema_source_lookup(source, "org.gnome.settings-daemon.plugins.media-keys", TRUE);
    if (!parent_schema) return;
    g_settings_schema_unref(parent_schema);

    GSettingsSchema* custom_schema = g_settings_schema_source_lookup(source, "org.gnome.settings-daemon.plugins.media-keys.custom-keybinding", TRUE);
    if (!custom_schema) return;
    g_settings_schema_unref(custom_schema);

    GSettings* media_keys = g_settings_new("org.gnome.settings-daemon.plugins.media-keys");
    gchar** existing = g_settings_get_strv(media_keys, "custom-keybindings");

    gchar* target_path = NULL;
    int count = 0;
    if (existing) {
        for (int i = 0; existing[i]; i++) {
            count++;
            GSettings* item = g_settings_new_with_path("org.gnome.settings-daemon.plugins.media-keys.custom-keybinding", existing[i]);
            gchar* name = g_settings_get_string(item, "name");
            if (name && (strstr(name, "TinyGrammar") != NULL)) {
                target_path = g_strdup(existing[i]);
                g_free(name);
                g_object_unref(item);
                break;
            }
            if (name) g_free(name);
            g_object_unref(item);
        }
    }

    if (!target_path) {
        int idx = 0;
        while (1) {
            char candidate[256];
            snprintf(candidate, sizeof(candidate), "/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/custom%d/", idx);
            gboolean found = FALSE;
            if (existing) {
                for (int i = 0; existing[i]; i++) {
                    if (strcmp(existing[i], candidate) == 0) {
                        found = TRUE;
                        break;
                    }
                }
            }
            if (!found) {
                target_path = g_strdup(candidate);
                break;
            }
            idx++;
        }

        gchar** updated = (gchar**)malloc((count + 2) * sizeof(gchar*));
        for (int i = 0; i < count; i++) updated[i] = existing[i];
        updated[count] = target_path;
        updated[count + 1] = NULL;
        g_settings_set_strv(media_keys, "custom-keybindings", (const gchar* const*)updated);
        free(updated);
    }

    GSettings* target = g_settings_new_with_path("org.gnome.settings-daemon.plugins.media-keys.custom-keybinding", target_path);
    g_settings_set_string(target, "name", "TinyGrammar Fix");
    g_settings_set_string(target, "command", cmd);
    g_settings_set_string(target, "binding", "<Super>g");

    g_object_unref(target);
    g_free(target_path);
    if (existing) g_strfreev(existing);
    g_object_unref(media_keys);
}

#ifdef GDK_WINDOWING_X11
static GdkFilterReturn x11_event_filter(GdkXEvent* xevent, GdkEvent* event, gpointer data) {
    XEvent* xev = (XEvent*)xevent;
    if (xev->type == KeyPress) {
        Display* dpy = xev->xkey.display;
        KeyCode kc = xev->xkey.keycode;
        KeySym ks = XkbKeycodeToKeysym(dpy, kc, 0, 0);

        if ((ks == XK_g || ks == XK_G) && (xev->xkey.state & Mod4Mask)) {
            if (g_callback) {
                g_callback();
            }
            return GDK_FILTER_REMOVE;
        }
    }
    return GDK_FILTER_CONTINUE;
}

static void setup_x11_grab(void) {
    GdkDisplay* gdk_display = gdk_display_get_default();
    if (!gdk_display || !GDK_IS_X11_DISPLAY(gdk_display)) return;

    Display* dpy = GDK_DISPLAY_XDISPLAY(gdk_display);
    Window root = DefaultRootWindow(dpy);
    KeyCode keycode = XKeysymToKeycode(dpy, XK_g);
    if (!keycode) keycode = XKeysymToKeycode(dpy, XK_G);
    if (!keycode) return;

    unsigned int modifiers[] = {
        Mod4Mask,
        Mod4Mask | Mod2Mask,
        Mod4Mask | LockMask,
        Mod4Mask | Mod2Mask | LockMask
    };

    for (int i = 0; i < 4; i++) {
        XGrabKey(dpy, keycode, modifiers[i], root, True, GrabModeAsync, GrabModeAsync);
    }

    GdkWindow* gdk_root = gdk_get_default_root_window();
    if (gdk_root) {
        gdk_window_add_filter(gdk_root, x11_event_filter, NULL);
    }
}
#endif

void hotkey_setup(HotkeyTriggerCallback callback) {
    g_callback = callback;

    // 1. Direct hardware evdev input listener (zero-config, works everywhere in user-space)
    start_evdev_listener();

    // 2. Register with GNOME settings daemon as fallback
    register_gnome_shortcut();

    // 3. Setup X11/Xwayland XGrabKey fallback
#ifdef GDK_WINDOWING_X11
    setup_x11_grab();
#endif
}

void hotkey_cleanup(void) {
    stop_evdev_listener();
    g_callback = NULL;
}
