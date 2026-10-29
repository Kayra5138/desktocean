#!/usr/bin/env bash
# Builds everything and installs it for the current user:
#   - Desktocean.Core QML plugin (Rust core + C++) → ~/.local/lib/qt6/qml
#   - desktocean-editor (drawing app)              → ~/.local/bin + app menu entry + *.desktocean type
#   - wallpaper package                            → ~/.local/share/plasma/wallpapers
#   - control widget                               → ~/.local/share/plasma/plasmoids
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
QML_DIR="$HOME/.local/lib/qt6/qml"
ENV_FILE="$HOME/.config/environment.d/50-desktocean.conf"

# --- native plugin -----------------------------------------------------------
GENERATOR="Unix Makefiles"
command -v ninja >/dev/null 2>&1 && GENERATOR="Ninja"
[ -f "$ROOT/build/CMakeCache.txt" ] || cmake -S "$ROOT/native" -B "$ROOT/build" -G "$GENERATOR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$ROOT/build"

# plasmashell keeps the old .so mapped: never overwrite it in place. Copy each
# file next to its destination, then rename over it (atomic, old inode survives).
SRC="$ROOT/build/qml/Desktocean/Core"
DST="$QML_DIR/Desktocean/Core"
mkdir -p "$DST"
for f in qmldir desktocean_core.qmltypes libdesktocean_core.so; do
    cp "$SRC/$f" "$DST/.$f.new"
    mv -f "$DST/.$f.new" "$DST/$f"
done
echo "Installed Desktocean.Core → $DST"

# --- editor ------------------------------------------------------------------
BIN="$HOME/.local/bin"
APPS="$HOME/.local/share/applications"
mkdir -p "$BIN" "$APPS"
cp "$ROOT/build/desktocean-editor" "$BIN/.desktocean-editor.new"
mv -f "$BIN/.desktocean-editor.new" "$BIN/desktocean-editor"
cp "$ROOT/native/editor/desktocean-editor.desktop" "$APPS/desktocean-editor.desktop"
# Shared fish (*.desktocean) open in the editor
MIME="$HOME/.local/share/mime"
mkdir -p "$MIME/packages"
cp "$ROOT/native/editor/desktocean-fish.xml" "$MIME/packages/desktocean-fish.xml"
command -v update-mime-database >/dev/null 2>&1 && update-mime-database "$MIME" || true
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database -q "$APPS" || true
echo "Installed desktocean-editor → $BIN"

# --- make plasmashell find it ------------------------------------------------
if [ ! -f "$ENV_FILE" ]; then
    mkdir -p "$(dirname "$ENV_FILE")"
    printf 'QML_IMPORT_PATH=%s${QML_IMPORT_PATH:+:$QML_IMPORT_PATH}\n' "$QML_DIR" > "$ENV_FILE"
    echo "Wrote $ENV_FILE (applies to new logins)"
fi
case ":$(systemctl --user show-environment | sed -n 's/^QML_IMPORT_PATH=//p'):" in
    *":$QML_DIR:"*) ;;
    *)
        systemctl --user set-environment "QML_IMPORT_PATH=$QML_DIR"
        echo "Set QML_IMPORT_PATH for this session's user services"
        ;;
esac

# --- wallpaper package -------------------------------------------------------
"$ROOT/scripts/build-shaders.sh"
python3 "$ROOT/scripts/make-test-fish.py"
cp "$ROOT/applet/contents/icons/desktocean.svg" "$ROOT/package/contents/images/desktocean-icon.svg"
python3 "$ROOT/scripts/make-starter-decor.py"
python3 "$ROOT/scripts/make-sounds.py"
# --- control widget ----------------------------------------------------------
if kpackagetool6 -t Plasma/Applet -s desktocean.control >/dev/null 2>&1; then
    kpackagetool6 -t Plasma/Applet -u "$ROOT/applet" >/dev/null
    echo "Upgraded the Desktocean widget"
else
    kpackagetool6 -t Plasma/Applet -i "$ROOT/applet" >/dev/null
    echo "Installed the Desktocean widget: add it from the panel's or desktop's \"Add Widgets…\""
fi

if kpackagetool6 -t Plasma/Wallpaper -s desktocean.aquarium >/dev/null 2>&1; then
    kpackagetool6 -t Plasma/Wallpaper -u "$ROOT/package"
    echo "Upgraded. Plasma caches QML and plugins: run scripts/reload-plasma.sh to see changes."
else
    kpackagetool6 -t Plasma/Wallpaper -i "$ROOT/package"
    echo "Installed. Pick it in: right-click desktop → Desktop and Wallpaper → Wallpaper type → Desktocean"
fi
