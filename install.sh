#!/usr/bin/env sh
# Installs the TinyGrammar launcher into the desktop applications menu.
# The app self-elevates to root when started (password prompt).

set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BIN="$SCRIPT_DIR/build/tinygrammar"

if [ ! -x "$BIN" ]; then
  echo "ERROR: build/tinygrammar not found. Build first:" >&2
  echo "  ./start.sh  (or  cmake -B build -S . && cmake --build build)" >&2
  exit 1
fi

# Prefer user-local, fall back to system-wide applications dir.
for APPL_DIR in "$HOME/.local/share/applications" "/usr/share/applications"; do
  if [ -d "$APPL_DIR" ] || mkdir -p "$APPL_DIR" 2>/dev/null; then
    DEST="$APPL_DIR/tinygrammar.desktop"
    cat > "$DEST" <<EOF
[Desktop Entry]
Type=Application
Name=TinyGrammar
Comment=Local neural grammar correction (runs as root)
Exec=$BIN --tray
Icon=edit-paste
Terminal=false
Categories=Utility;Text;
EOF
    echo "Installed launcher: $DEST"
    exit 0
  fi
done

echo "ERROR: no writable applications directory found" >&2
exit 1