#!/usr/bin/env bash
# Builds everything and installs it for the current user:
#   - Desktocean.Core QML plugin (Rust core + C++) → ~/.local/lib/qt6/qml
#   - wallpaper package                            → ~/.local/share/plasma/wallpapers
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
if kpackagetool6 -t Plasma/Wallpaper -s desktocean.aquarium >/dev/null 2>&1; then
    kpackagetool6 -t Plasma/Wallpaper -u "$ROOT/package"
    echo "Upgraded. Plasma caches QML and plugins: run scripts/reload-plasma.sh to see changes."
else
    kpackagetool6 -t Plasma/Wallpaper -i "$ROOT/package"
    echo "Installed. Pick it in: right-click desktop → Desktop and Wallpaper → Wallpaper type → Desktocean"
fi
