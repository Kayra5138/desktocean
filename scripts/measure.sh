#!/usr/bin/env bash
# Samples plasmashell CPU and memory for N seconds (default 30).
# Run once with Desktocean and once with a static wallpaper to compare.
set -euo pipefail
SECS="${1:-30}"
PID="$(pgrep -x plasmashell | head -n1)"
echo "plasmashell pid $PID, sampling ${SECS}s..."
top -b -d 1 -n "$SECS" -p "$PID" | awk -v pid="$PID" '
    $1 == pid { cpu += $9; n++; rss = $6 }
    END { printf "avg CPU: %.2f%%  (one core = 100%%)\nRES: %s KiB\n", cpu / n, rss }
'
