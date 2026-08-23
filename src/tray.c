#include "tray.h"
#include "grammar.h"
#include "diff.h"
#include "ipc.h"
#include "hotkey.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <gdk/gdk.h>

// --- AppIndicator dynamic loader definitions ---
typedef enum {
    APP_INDICATOR_CATEGORY_APPLICATION_STATUS,
    APP_INDICATOR_CATEGORY_COMMUNICATIONS,
    APP_INDICATOR_CATEGORY_SYSTEM_SERVICES,
    APP_INDICATOR_CATEGORY_HARDWARE,
    APP_INDICATOR_CATEGORY_OTHER
} AppIndicatorCategory;

typedef enum {
    APP_INDICATOR_STATUS_PASSIVE,
    APP_INDICATOR_STATUS_ACTIVE,
    APP_INDICATOR_STATUS_ATTENTION
} AppIndicatorStatus;

typedef void* (*fn_app_indicator_new)(const gchar*, const gchar*, AppIndicatorCategory);
typedef void  (*fn_app_indicator_set_status)(void*, AppIndicatorStatus);
typedef void  (*fn_app_indicator_set_menu)(void*, GtkMenu*);
typedef void  (*fn_app_indicator_set_title)(void*, const gchar*);
typedef void  (*fn_app_indicator_set_secondary_activate_target)(void*, GtkWidget*);

// --- Global UI state ---
typedef struct {
    GtkWidget*     window;
    GtkWidget*     card_container;
    GtkWidget*     text_view;
    GtkTextBuffer* text_buffer;
    GtkTextTag*    tag_normal;
    GtkTextTag*    tag_delete;
    GtkTextTag*    tag_insert;

    GtkWidget*     hint_copy_label;
    GtkWidget*     hint_close_label;
    GtkWidget*     copied_msg_label;

    GtkStatusIcon* status_icon;
    void*          app_indicator;
    void*          indicator_lib;

    gboolean       is_processing;
    gchar*         last_corrected_text;
    guint          feedback_timeout_id;
} TrayApp;

static TrayApp g_app;

// --- CSS styling for dark theme ---
static const gchar* CSS_STYLE =
    "window.tg-root-window {"
    "    background-color: #1e1e2e;"
    "    border: 1px solid #313244;"
    "    border-radius: 14px;"
    "}"
    ".tg-card {"
    "    background-color: #181825;"
    "    border: 1px solid #313244;"
    "    border-radius: 10px;"
    "    padding: 12px 14px;"
    "}"
    ".tg-card scrolledwindow, .tg-card viewport {"
    "    background-color: #181825;"
    "    border: none;"
    "}"
    ".tg-textview, .tg-textview text {"
    "    background-color: #181825;"
    "    color: #cdd6f4;"
    "    font-size: 15px;"
    "    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Ubuntu, 'Cantarell', 'DejaVu Sans', sans-serif;"
    "}"
    ".tg-keycap {"
    "    background-color: #313244;"
    "    color: #cdd6f4;"
    "    border: 1px solid #45475a;"
    "    border-radius: 4px;"
    "    padding: 2px 7px;"
    "    font-size: 11px;"
    "    font-weight: bold;"
    "    font-family: -apple-system, BlinkMacSystemFont, monospace, sans-serif;"
    "}"
    ".tg-hint-text {"
    "    color: #a6adc8;"
    "    font-size: 12px;"
    "    margin-left: 4px;"
    "}"
    ".tg-copied-text {"
    "    color: #a6e3a1;"
    "    font-size: 12px;"
    "    font-weight: bold;"
    "    margin-left: 8px;"
    "}";

// Forward declarations
static void on_copy_action(void);
static void on_close_action(void);
static gboolean on_window_key_press(GtkWidget* widget, GdkEventKey* event, gpointer user_data);
static gboolean on_window_key_release(GtkWidget* widget, GdkEventKey* event, gpointer user_data);
static gboolean on_window_delete(GtkWidget* widget, GdkEvent* event, gpointer user_data);

static void apply_css(void) {
    GtkCssProvider* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, CSS_STYLE, -1, NULL);
    GdkScreen* screen = gdk_screen_get_default();
    if (screen) {
        gtk_style_context_add_provider_for_screen(
            screen,
            GTK_STYLE_PROVIDER(provider),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
        );
    }
    g_object_unref(provider);
}

static void position_window_top_right(GtkWindow* window) {
    GdkDisplay* display = gdk_display_get_default();
    if (!display) return;

    GdkMonitor* monitor = gdk_display_get_primary_monitor(display);
    if (!monitor) {
        int n_monitors = gdk_display_get_n_monitors(display);
        if (n_monitors > 0) {
            monitor = gdk_display_get_monitor(display, 0);
        }
    }

    if (monitor) {
        GdkRectangle geom;
        gdk_monitor_get_geometry(monitor, &geom);

        int win_w = 540;
        int win_h = 290;
        gtk_window_get_size(window, &win_w, &win_h);

        int x = geom.x + geom.width - win_w - 24;
        int y = geom.y + 36;

        gtk_window_move(window, x, y);
    }
}

void tray_show_window(void) {
    if (!g_app.window) return;
    if (g_app.copied_msg_label) {
        gtk_widget_hide(g_app.copied_msg_label);
    }
    gtk_widget_show_all(g_app.window);
    if (g_app.copied_msg_label) {
        gtk_widget_hide(g_app.copied_msg_label);
    }
    position_window_top_right(GTK_WINDOW(g_app.window));
    gtk_window_present(GTK_WINDOW(g_app.window));
}

void tray_hide_window(void) {
    if (!g_app.window) return;
    gtk_widget_hide(g_app.window);
}

// Background worker structure
typedef struct {
    gchar* original_text;
    gchar* corrected_text;
    gboolean success;
} WorkerData;

static gboolean on_inference_finished(gpointer user_data) {
    WorkerData* data = (WorkerData*)user_data;
    g_app.is_processing = FALSE;

    if (data->success && data->corrected_text) {
        if (g_app.last_corrected_text) {
            g_free(g_app.last_corrected_text);
        }
        g_app.last_corrected_text = g_strdup(data->corrected_text);

        // Compute word diff between original and corrected
        DiffResult diff = diff_words(data->original_text, data->corrected_text);
        diff_apply_to_buffer(g_app.text_buffer,
                             &diff,
                             g_app.tag_normal,
                             g_app.tag_delete,
                             g_app.tag_insert);
        diff_result_free(&diff);
    } else {
        gtk_text_buffer_set_text(g_app.text_buffer, "Grammar correction unavailable.", -1);
    }

    position_window_top_right(GTK_WINDOW(g_app.window));

    g_free(data->original_text);
    if (data->corrected_text) g_free(data->corrected_text);
    g_free(data);
    return G_SOURCE_REMOVE;
}

static gpointer inference_thread_func(gpointer user_data) {
    WorkerData* data = (WorkerData*)user_data;

    char* fixed = grammar_fix(data->original_text);
    if (fixed) {
        data->corrected_text = g_strdup(fixed);
        data->success = TRUE;
        free(fixed);
    } else {
        data->corrected_text = NULL;
        data->success = FALSE;
    }

    g_idle_add(on_inference_finished, data);
    return NULL;
}

static gchar* get_clipboard_text(void) {
    GtkClipboard* clip = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    gchar* text = NULL;
    if (clip) {
        text = gtk_clipboard_wait_for_text(clip);
    }
    if (!text || strlen(text) == 0) {
        if (text) g_free(text);
        GtkClipboard* prim = gtk_clipboard_get(GDK_SELECTION_PRIMARY);
        if (prim) {
            text = gtk_clipboard_wait_for_text(prim);
        }
    }
    return text;
}

void tray_activate_fix(void) {
    tray_show_window();

    if (g_app.is_processing) return;

    gchar* text = get_clipboard_text();

    // Check if empty
    gboolean is_empty = TRUE;
    if (text) {
        for (const gchar* p = text; *p; p++) {
            if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
                is_empty = FALSE;
                break;
            }
        }
    }

    if (is_empty) {
        gtk_text_buffer_set_text(g_app.text_buffer, "Clipboard is empty. Copy some text and try again.", -1);
        if (text) g_free(text);
        return;
    }

    // Set temporary loading state
    g_app.is_processing = TRUE;
    gtk_text_buffer_set_text(g_app.text_buffer, text, -1);

    // Spawn background inference worker
    WorkerData* data = g_new0(WorkerData, 1);
    data->original_text = text; // transferred ownership
    g_thread_new("grammar-worker", inference_thread_func, data);
}

static gboolean hide_copied_msg_callback(gpointer user_data) {
    if (g_app.copied_msg_label) {
        gtk_widget_hide(g_app.copied_msg_label);
    }
    g_app.feedback_timeout_id = 0;
    return G_SOURCE_REMOVE;
}

static void on_copy_action(void) {
    const char* text_to_copy = NULL;
    gchar* heap_text = NULL;

    if (g_app.last_corrected_text && strlen(g_app.last_corrected_text) > 0) {
        text_to_copy = g_app.last_corrected_text;
    } else {
        GtkTextIter start, end;
        gtk_text_buffer_get_bounds(g_app.text_buffer, &start, &end);
        heap_text = gtk_text_buffer_get_text(g_app.text_buffer, &start, &end, FALSE);
        text_to_copy = heap_text;
    }

    if (text_to_copy) {
        GtkClipboard* clip = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
        if (clip) {
            gtk_clipboard_set_text(clip, text_to_copy, -1);
            gtk_clipboard_store(clip);
        }
        GtkClipboard* prim = gtk_clipboard_get(GDK_SELECTION_PRIMARY);
        if (prim) {
            gtk_clipboard_set_text(prim, text_to_copy, -1);
        }
    }

    if (heap_text) g_free(heap_text);

    // Show label: "Text copied to clipboard"
    if (g_app.copied_msg_label) {
        gtk_label_set_text(GTK_LABEL(g_app.copied_msg_label), "✓ Text copied to clipboard");
        gtk_widget_show(g_app.copied_msg_label);

        if (g_app.feedback_timeout_id > 0) {
            g_source_remove(g_app.feedback_timeout_id);
        }
        g_app.feedback_timeout_id = g_timeout_add(2500, hide_copied_msg_callback, NULL);
    }
}

static void on_close_action(void) {
    tray_hide_window();
}

static gboolean on_window_key_press(GtkWidget* widget, GdkEventKey* event, gpointer user_data) {
    // 1. ESC: Swallow press event completely
    if (event->keyval == GDK_KEY_Escape) {
        return TRUE;
    }

    // 2. Enter: Copy to clipboard and swallow press event
    if (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter || event->keyval == GDK_KEY_ISO_Enter) {
        on_copy_action();
        return TRUE;
    }

    // Optional Ctrl+C support
    if ((event->state & GDK_CONTROL_MASK) && (event->keyval == GDK_KEY_c || event->keyval == GDK_KEY_C)) {
        on_copy_action();
        return TRUE;
    }

    return FALSE;
}

static gboolean on_window_key_release(GtkWidget* widget, GdkEventKey* event, gpointer user_data) {
    // 1. Dismiss on ESC release so the release event never leaks to the underlying app
    if (event->keyval == GDK_KEY_Escape) {
        on_close_action();
        return TRUE;
    }

    if (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter || event->keyval == GDK_KEY_ISO_Enter) {
        return TRUE;
    }

    return FALSE;
}

static gboolean on_window_delete(GtkWidget* widget, GdkEvent* event, gpointer user_data) {
    tray_hide_window();
    return TRUE;
}

static void on_menu_fix_activate(GtkMenuItem* item, gpointer user_data) {
    tray_activate_fix();
}

static void on_menu_quit_activate(GtkMenuItem* item, gpointer user_data) {
    gtk_main_quit();
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
static void on_status_icon_activate(GtkStatusIcon* icon, gpointer user_data) {
    tray_activate_fix();
}

static void on_status_icon_popup(GtkStatusIcon* icon, guint button, guint32 activate_time, gpointer user_data) {
    GtkMenu* menu = GTK_MENU(user_data);
    gtk_menu_popup(menu, NULL, NULL, gtk_status_icon_position_menu, icon, button, activate_time);
}
#pragma GCC diagnostic pop

static GtkWidget* create_tray_menu(void) {
    GtkWidget* menu = gtk_menu_new();

    GtkWidget* item_fix = gtk_menu_item_new_with_label("✨ Fix Clipboard (Super+G)");
    g_signal_connect(item_fix, "activate", G_CALLBACK(on_menu_fix_activate), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_fix);

    GtkWidget* sep = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), sep);

    GtkWidget* item_quit = gtk_menu_item_new_with_label("❌ Quit");
    g_signal_connect(item_quit, "activate", G_CALLBACK(on_menu_quit_activate), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_quit);

    gtk_widget_show_all(menu);
    return menu;
}

static void init_tray_indicator(GtkWidget* menu) {
    const char* libs[] = {
        "libayatana-appindicator3.so.1",
        "libappindicator3.so.1",
        "libayatana-appindicator3.so",
        "libappindicator3.so",
        NULL
    };

    void* handle = NULL;
    for (int i = 0; libs[i]; i++) {
        handle = dlopen(libs[i], RTLD_LAZY);
        if (handle) break;
    }

    if (handle) {
        fn_app_indicator_new f_new = (fn_app_indicator_new)dlsym(handle, "app_indicator_new");
        fn_app_indicator_set_status f_set_status = (fn_app_indicator_set_status)dlsym(handle, "app_indicator_set_status");
        fn_app_indicator_set_menu f_set_menu = (fn_app_indicator_set_menu)dlsym(handle, "app_indicator_set_menu");
        fn_app_indicator_set_title f_set_title = (fn_app_indicator_set_title)dlsym(handle, "app_indicator_set_title");
        fn_app_indicator_set_secondary_activate_target f_set_secondary =
            (fn_app_indicator_set_secondary_activate_target)dlsym(handle, "app_indicator_set_secondary_activate_target");

        if (f_new && f_set_status && f_set_menu) {
            void* indicator = f_new("tinygrammar-tray", "tools-check-spelling", APP_INDICATOR_CATEGORY_APPLICATION_STATUS);
            if (indicator) {
                f_set_status(indicator, APP_INDICATOR_STATUS_ACTIVE);
                f_set_menu(indicator, GTK_MENU(menu));
                if (f_set_title) f_set_title(indicator, "TinyGrammar");

                GList* children = gtk_container_get_children(GTK_CONTAINER(menu));
                if (children && f_set_secondary) {
                    f_set_secondary(indicator, GTK_WIDGET(children->data));
                    g_list_free(children);
                }

                g_app.app_indicator = indicator;
                g_app.indicator_lib = handle;
                return;
            }
        }
        dlclose(handle);
    }

    // Fallback: GtkStatusIcon
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    g_app.status_icon = gtk_status_icon_new_from_icon_name("tools-check-spelling");
    gtk_status_icon_set_title(g_app.status_icon, "TinyGrammar");
    gtk_status_icon_set_tooltip_text(g_app.status_icon, "TinyGrammar - Click to Fix Clipboard (Super+G)");
    gtk_status_icon_set_visible(g_app.status_icon, TRUE);

    g_signal_connect(g_app.status_icon, "activate", G_CALLBACK(on_status_icon_activate), NULL);
    g_signal_connect(g_app.status_icon, "popup-menu", G_CALLBACK(on_status_icon_popup), menu);
#pragma GCC diagnostic pop
}

// Helpers for footer keycap shortcuts
static GtkWidget* create_keycap(const gchar* label_text) {
    GtkWidget* lbl = gtk_label_new(label_text);
    GtkStyleContext* ctx = gtk_widget_get_style_context(lbl);
    gtk_style_context_add_class(ctx, "tg-keycap");
    return lbl;
}

static GtkWidget* create_hint_label(const gchar* text) {
    GtkWidget* lbl = gtk_label_new(text);
    GtkStyleContext* ctx = gtk_widget_get_style_context(lbl);
    gtk_style_context_add_class(ctx, "tg-hint-text");
    return lbl;
}

static gboolean on_click_copy_event(GtkWidget* widget, GdkEventButton* event, gpointer user_data) {
    if (event->type == GDK_BUTTON_PRESS && event->button == 1) {
        on_copy_action();
        return TRUE;
    }
    return FALSE;
}

static gboolean on_click_close_event(GtkWidget* widget, GdkEventButton* event, gpointer user_data) {
    if (event->type == GDK_BUTTON_PRESS && event->button == 1) {
        on_close_action();
        return TRUE;
    }
    return FALSE;
}

static void create_popup_window(void) {
    GtkWidget* win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    g_app.window = win;

    // Window properties: No titlebar / undecorated, always on top
    gtk_window_set_decorated(GTK_WINDOW(win), FALSE);
    gtk_window_set_title(GTK_WINDOW(win), "TinyGrammar");
    gtk_window_set_default_size(GTK_WINDOW(win), 540, 290);
    gtk_window_set_position(GTK_WINDOW(win), GTK_WIN_POS_NONE);
    gtk_window_set_keep_above(GTK_WINDOW(win), TRUE);
    gtk_window_set_type_hint(GTK_WINDOW(win), GDK_WINDOW_TYPE_HINT_UTILITY);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(win), TRUE);

    GtkStyleContext* win_ctx = gtk_widget_get_style_context(win);
    gtk_style_context_add_class(win_ctx, "tg-root-window");

    g_signal_connect(win, "key-press-event", G_CALLBACK(on_window_key_press), NULL);
    g_signal_connect(win, "key-release-event", G_CALLBACK(on_window_key_release), NULL);
    g_signal_connect(win, "delete-event", G_CALLBACK(on_window_delete), NULL);

    // Root container with padding
    GtkWidget* root_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(root_box), 12);
    gtk_container_add(GTK_CONTAINER(win), root_box);

    // --- Main Dark Card Area ---
    GtkWidget* card_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    g_app.card_container = card_box;
    GtkStyleContext* card_ctx = gtk_widget_get_style_context(card_box);
    gtk_style_context_add_class(card_ctx, "tg-card");

    GtkWidget* scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll), GTK_SHADOW_NONE);
    gtk_widget_set_size_request(scroll, -1, 210);

    g_app.text_view = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(g_app.text_view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(g_app.text_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(g_app.text_view), FALSE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(g_app.text_view), 2);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(g_app.text_view), 2);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(g_app.text_view), 2);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(g_app.text_view), 2);

    GtkStyleContext* tv_ctx = gtk_widget_get_style_context(g_app.text_view);
    gtk_style_context_add_class(tv_ctx, "tg-textview");

    g_app.text_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(g_app.text_view));

    // Normal text tag
    g_app.tag_normal = gtk_text_buffer_create_tag(g_app.text_buffer, "normal",
        "foreground", "#cdd6f4",
        NULL);

    // Delete tag: Dark red pill with strikethrough
    g_app.tag_delete = gtk_text_buffer_create_tag(g_app.text_buffer, "delete",
        "background", "#451a1a",
        "foreground", "#fca5a5",
        "strikethrough", TRUE,
        "weight", PANGO_WEIGHT_MEDIUM,
        NULL);

    // Insert tag: Dark green pill
    g_app.tag_insert = gtk_text_buffer_create_tag(g_app.text_buffer, "insert",
        "background", "#133e28",
        "foreground", "#86efac",
        "strikethrough", FALSE,
        "weight", PANGO_WEIGHT_MEDIUM,
        NULL);

    gtk_container_add(GTK_CONTAINER(scroll), g_app.text_view);
    gtk_box_pack_start(GTK_BOX(card_box), scroll, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(root_box), card_box, TRUE, TRUE, 0);

    // --- Footer: Exactly 2 actions: Enter to copy, Esc to close ---
    GtkWidget* footer_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_start(footer_box, 4);
    gtk_widget_set_margin_end(footer_box, 4);
    gtk_widget_set_margin_bottom(footer_box, 2);

    // Action 1: [Enter ↵] to copy
    GtkWidget* eb_copy = gtk_event_box_new();
    GtkWidget* box_copy = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
    gtk_box_pack_start(GTK_BOX(box_copy), create_keycap("Enter ↵"), FALSE, FALSE, 0);
    g_app.hint_copy_label = create_hint_label("to copy");
    gtk_box_pack_start(GTK_BOX(box_copy), g_app.hint_copy_label, FALSE, FALSE, 0);

    // Label that appears: "✓ Text copied to clipboard"
    g_app.copied_msg_label = gtk_label_new("✓ Text copied to clipboard");
    GtkStyleContext* cm_ctx = gtk_widget_get_style_context(g_app.copied_msg_label);
    gtk_style_context_add_class(cm_ctx, "tg-copied-text");
    gtk_widget_set_no_show_all(g_app.copied_msg_label, TRUE);
    gtk_box_pack_start(GTK_BOX(box_copy), g_app.copied_msg_label, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(eb_copy), box_copy);
    g_signal_connect(eb_copy, "button-press-event", G_CALLBACK(on_click_copy_event), NULL);
    gtk_box_pack_start(GTK_BOX(footer_box), eb_copy, FALSE, FALSE, 0);

    // Spacer
    GtkWidget* sp = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(footer_box), sp, TRUE, TRUE, 0);

    // Action 2: [Esc] to close
    GtkWidget* eb_close = gtk_event_box_new();
    GtkWidget* box_close = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
    gtk_box_pack_start(GTK_BOX(box_close), create_keycap("Esc"), FALSE, FALSE, 0);
    g_app.hint_close_label = create_hint_label("to close");
    gtk_box_pack_start(GTK_BOX(box_close), g_app.hint_close_label, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(eb_close), box_close);
    g_signal_connect(eb_close, "button-press-event", G_CALLBACK(on_click_close_event), NULL);
    gtk_box_pack_start(GTK_BOX(footer_box), eb_close, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(root_box), footer_box, FALSE, FALSE, 0);
}

// Background preload worker for grammar model
static gpointer preload_model_worker(gpointer user_data) {
    grammar_init();
    return NULL;
}

static gboolean auto_fix_callback(gpointer user_data) {
    tray_activate_fix();
    return G_SOURCE_REMOVE;
}

int tray_run(int argc, char* argv[], gboolean auto_fix_on_start) {
    gtk_init(&argc, &argv);

    memset(&g_app, 0, sizeof(g_app));

    apply_css();

    GtkWidget* menu = create_tray_menu();
    init_tray_indicator(menu);
    create_popup_window();

    // Start IPC server to allow triggering via `tinygrammar --fix` or custom shortcuts
    ipc_server_start(tray_activate_fix);

    // Setup global hotkeys (Hardware evdev + GNOME settings daemon registration + X11 XGrabKey)
    hotkey_setup(tray_activate_fix);

    // Asynchronously preload ONNX models in background so first fix is instant
    g_thread_new("model-preload", preload_model_worker, NULL);

    if (auto_fix_on_start) {
        g_idle_add(auto_fix_callback, NULL);
    }

    printf("TinyGrammar running.\n");
    printf("  • Global Hotkey: Super+G (zero-config hardware listener + GNOME integration)\n");
    printf("  • IPC trigger:   tinygrammar --fix\n");

    gtk_main();

    // Cleanup
    hotkey_cleanup();
    ipc_server_stop();

    if (g_app.feedback_timeout_id > 0) {
        g_source_remove(g_app.feedback_timeout_id);
    }
    if (g_app.last_corrected_text) {
        g_free(g_app.last_corrected_text);
    }
    if (g_app.indicator_lib) {
        dlclose(g_app.indicator_lib);
    }

    grammar_cleanup();
    return 0;
}
