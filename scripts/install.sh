#!/usr/bin/env bash
# Builds assets and installs (or upgrades) the wallpaper package for the current user.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
"$ROOT/scripts/build-shaders.sh"
python3 "$ROOT/scripts/make-test-fish.py"
if kpackagetool6 -t Plasma/Wallpaper -s desktocean.aquarium >/dev/null 2>&1; then
    kpackagetool6 -t Plasma/Wallpaper -u "$ROOT/package"
    echo "Upgraded. Plasma caches QML: run scripts/reload-plasma.sh to see changes."
else
    kpackagetool6 -t Plasma/Wallpaper -i "$ROOT/package"
    echo "Installed. Pick it in: right-click desktop → Desktop and Wallpaper → Wallpaper type → Desktocean"
fi
