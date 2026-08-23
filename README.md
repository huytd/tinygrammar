# tinygrammar

Local T5 inference engine and GTK system tray for fast grammar correction.

## Features

- **GTK System Tray App**: Resides in your system tray / notification area.
- **Top-Right HUD Popup**: Undecorated, floating window with word-level inline diff (deletions highlighted in red with strikethrough, additions in green).
- **Dark Theme**: Unified modern dark theme matching system dark modes.
- **Global Hotkey (Super+G)**: Press `Super+G` anytime from any application to fix the clipboard and popup the result.
- **Direct Paste (Ctrl+Enter)**: Copies the corrected text, dismisses the popup, and automatically pastes into the active application.
- **IPC Support**: Run `tinygrammar --fix` (can be bound to any custom shortcut in GNOME/KDE/WM settings) to trigger grammar fix instantly.
- **Fast & Responsive**: Model preloading and asynchronous background inference so the UI never blocks.

## How to compile

The project is setup to be compiled on x64 Linux. Use the `start.sh` script to automatically download the ONNXRuntime libraries, as well as the model files before compiling:

```bash
./start.sh
```

Or build manually with CMake:

```bash
cmake -B build -S .
cmake --build build
```

## How to run

### 1. System Tray Mode

Start the system tray application:

```bash
./build/tinygrammar
# or
./build/tinygrammar --tray
```

- **Trigger Fix**:
  - Press `Super+G` anywhere
  - Or run `tinygrammar --fix`
  - Or click the tray icon / select **"✨ Fix Clipboard"** from the tray menu.
- **Window Actions**:
  - `Enter ↵`: Copies corrected text to clipboard and displays `"✓ Text copied to clipboard"`.
  - `Ctrl + Enter ↵`: Copies corrected text, dismisses popup, and pastes directly into active app.
  - `Esc`: Dismisses / closes the window.

### 2. Command Line (CLI) Mode

Pass the text you want to fix as arguments:

```bash
./build/tinygrammar "She no went to school yesterday"
# Output: She didn't go to school yesterday.
```
