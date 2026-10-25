#!/bin/bash
# eeschema GUI의 공식 ERC를 가상 화면에서 실행하고 ERC.rpt를 저장한다.
set -e
DIR="$1"; export DISPLAY=:99
pkill -x eeschema || true; sleep 1
pgrep Xvfb >/dev/null || (Xvfb :99 -screen 0 1920x1080x24 >/dev/null 2>&1 &); sleep 1
rm -f "$DIR/ERC.rpt" "$DIR"/~*.lck "$DIR"/*.lck
cd "$DIR"; (eeschema deskpet.kicad_sch >/dev/null 2>&1 &); sleep 14
W=$(xdotool search --name "Schematic Editor" | head -1); xdotool windowactivate $W 2>/dev/null || true
xdotool mousemove 207 11 click 1; sleep 1.2
xdotool mousemove 282 37 click 1; sleep 3
xdotool mousemove 923 540 click 1; sleep 7
import -window root /tmp/erc_dialog.png
xdotool mousemove 923 490 click 1; sleep 2
xdotool key Return; sleep 2
pkill -x eeschema || true
cat "$DIR/ERC.rpt"
