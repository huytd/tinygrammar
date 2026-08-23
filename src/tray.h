#pragma once

#include <gtk/gtk.h>

#ifdef __cplusplus
extern "C" {
#endif

// Starts the GTK tray application loop.
// If auto_fix_on_start is TRUE, immediately triggers clipboard grammar fix.
int tray_run(int argc, char* argv[], gboolean auto_fix_on_start);

// Triggers grammar fix on current clipboard and shows the popup window.
void tray_activate_fix(void);

// Shows the popup window at the top right of the screen.
void tray_show_window(void);

// Hides the popup window.
void tray_hide_window(void);

#ifdef __cplusplus
}
#endif
