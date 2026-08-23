#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*HotkeyTriggerCallback)(void);

// Initializes global hotkeys (GNOME custom keybinding and X11 XGrabKey)
void hotkey_setup(HotkeyTriggerCallback callback);

// Cleans up hotkey listeners
void hotkey_cleanup(void);

#ifdef __cplusplus
}
#endif
