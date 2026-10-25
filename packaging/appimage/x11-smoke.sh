#!/bin/sh
# x11-smoke.sh — starts the AppImage on a headless X server (Cosmoe's X11 backend), checks it
# stays up, and saves a screenshot and the app log.
#
#   sh packaging/appimage/x11-smoke.sh <App>-<version>-<arch>.AppImage [OUT_DIR]
#
# The app's window title and process name are taken to start with <App>; set SMOKE_WINDOW
# (start of a window title) and SMOKE_PROCESS (process name) when they don't, e.g.
# SMOKE_WINDOW="VideoLAN Client" SMOKE_PROCESS=vlc.
#
# Uses Xvfb with Openbox (without a window manager Cosmoe windows get no focus) and
# ImageMagick's import for the screenshot. Ubuntu: apt-get install --no-install-recommends
# xvfb openbox imagemagick xdotool. Generic beos-app-platforms template: appimage.yml runs it after
# building the AppImage, on every architecture, so the aarch64 build is started on real
# arm64 hardware too.
set -u
APP=$(readlink -f "$1")
OUT=$(mkdir -p "${2:-x11-smoke}" && cd "${2:-x11-smoke}" && pwd)
DPY=:77
Xvfb $DPY -screen 0 1280x800x24 -nolisten tcp > "$OUT/xvfb.log" 2>&1 &
XPID=$!
for i in $(seq 40); do [ -e /tmp/.X11-unix/X77 ] && break; sleep 0.25; done
[ -e /tmp/.X11-unix/X77 ] || { echo "x11-smoke: Xvfb did not start"; cat "$OUT/xvfb.log"; exit 1; }
DISPLAY=$DPY openbox > "$OUT/wm.log" 2>&1 &
WM=$!
# Start the app only once Openbox manages the display (it then answers EWMH queries). On a
# slow runner a window mapped while the WM is still starting can stay unmapped for good.
for i in $(seq 40); do DISPLAY=$DPY xdotool get_desktop >/dev/null 2>&1 && break; sleep 0.25; done
DISPLAY=$DPY xdotool get_desktop >/dev/null 2>&1 || { echo "x11-smoke: openbox did not start"; cat "$OUT/wm.log"; exit 1; }

HOME_DIR=$(mktemp -d)
env -u WAYLAND_DISPLAY DISPLAY=$DPY COSMOE_BACKEND=x11 APPIMAGE_EXTRACT_AND_RUN=1 \
	HOME="$HOME_DIR" "$APP" > "$OUT/app.log" 2>&1 &
PID=$!
# Wait for the app's top-level window (up to 60 s; first runs build font caches).
APPNAME=$(basename "$APP" | sed 's/-[0-9].*//')
WINDOW=${SMOKE_WINDOW:-$APPNAME}
PROCESS=${SMOKE_PROCESS:-$APPNAME}
for i in $(seq 60); do
	DISPLAY=$DPY xdotool search --onlyvisible --name "^$WINDOW" >/dev/null 2>&1 && break
	kill -0 $PID 2>/dev/null || break
	sleep 1
done
sleep 3
# Every named window, and which are mapped (visible), for the record
{ echo "visible:"; for w in $(DISPLAY=$DPY xdotool search --onlyvisible --name . 2>/dev/null); do
	echo "  $w $(DISPLAY=$DPY xdotool getwindowname $w 2>/dev/null) $(DISPLAY=$DPY xdotool getwindowgeometry $w 2>/dev/null | tr '\n' ' ')"; done
  echo "all named:"; for w in $(DISPLAY=$DPY xdotool search --name . 2>/dev/null); do
	echo "  $w $(DISPLAY=$DPY xdotool getwindowname $w 2>/dev/null)"; done; } > "$OUT/window-tree.txt" 2>&1
if ! kill -0 $PID 2>/dev/null; then
	wait $PID; status=$?
	echo "x11-smoke: the app exited with status $status"; cat "$OUT/app.log"
	kill $WM $XPID 2>/dev/null; exit 1
fi
echo "x11-smoke: the app is running under X11 ($(uname -m))"
DISPLAY=$DPY timeout 30 import -window root "$OUT/x11-screenshot.png" || echo "x11-smoke: screenshot failed"
# Stop the app before the X server, so its log doesn't end with "X connection broken".
# $PID is the AppImage runtime; the app itself is its child, so stop that by name too.
kill $PID 2>/dev/null; pkill -TERM -x "$PROCESS" 2>/dev/null
for i in $(seq 20); do pgrep -x "$PROCESS" >/dev/null || break; sleep 0.25; done
pkill -KILL -x "$PROCESS" 2>/dev/null; kill $WM $XPID 2>/dev/null
ls -l "$OUT"
if ! sed -n '/^visible:/,/^all named:/p' "$OUT/window-tree.txt" | grep -q " $WINDOW"; then
	echo "x11-smoke: no visible window named $WINDOW* appeared:"; cat "$OUT/window-tree.txt"; exit 1
fi
COLORS=$(identify -format %k "$OUT/x11-screenshot.png" 2>/dev/null || echo 0)
if [ "${COLORS:-0}" -le 1 ]; then
	echo "x11-smoke: the screenshot is blank ($COLORS colour): the window drew nothing"; exit 1
fi
if grep -qiE 'error|assert|crash|segment|abort|exception' "$OUT/app.log"; then
	echo "x11-smoke: problems in the app log:"; grep -iE 'error|assert|crash|segment|abort|exception' "$OUT/app.log"
	exit 1
fi
echo "x11-smoke: PASS"
