#!/usr/bin/env bash
# Restarts plasmashell so it reloads the wallpaper QML. Desktop and panels blink for a second.
set -euo pipefail
systemctl --user restart plasma-plasmashell.service
