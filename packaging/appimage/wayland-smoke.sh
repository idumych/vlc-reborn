#!/bin/sh
# wayland-smoke.sh — starts the AppImage on a headless Wayland compositor (Cosmoe's Wayland
# backend), checks it stays up, and saves a screenshot, the app log and a protocol trace.
#
#   sh packaging/appimage/wayland-smoke.sh <App>-<version>-<arch>.AppImage [OUT_DIR]
#
# Uses sway on wlroots' headless backend, which always offers a wl_seat like a real desktop
# (headless Weston has none, and Cosmoe exits without one), and grim for the screenshot.
# Ubuntu: apt-get install --no-install-recommends sway grim imagemagick. Generic beos-app-platforms template:
# appimage.yml runs it after building the AppImage.
set -u
APP=$(readlink -f "$1")
OUT=$(mkdir -p "${2:-wayland-smoke}" && cd "${2:-wayland-smoke}" && pwd)
export XDG_RUNTIME_DIR=$(mktemp -d)
chmod 700 "$XDG_RUNTIME_DIR"

CONF=$(mktemp)
printf 'output HEADLESS-1 resolution 1280x800\ndefault_border none\n' > "$CONF"
env -u DISPLAY -u WAYLAND_DISPLAY WLR_BACKENDS=headless WLR_RENDERER=pixman \
	WLR_LIBINPUT_NO_DEVICES=1 sway -c "$CONF" > "$OUT/compositor.log" 2>&1 &
COMP=$!
SOCK=""
for i in $(seq 40); do
	SOCK=$(ls "$XDG_RUNTIME_DIR" 2>/dev/null | grep -E '^wayland-[0-9]+$' | head -1)
	[ -n "$SOCK" ] && break; sleep 0.25
done
if [ -z "$SOCK" ]; then
	echo "wayland-smoke: the compositor did not start"; cat "$OUT/compositor.log"; exit 1
fi

HOME_DIR=$(mktemp -d)
env -u DISPLAY WAYLAND_DISPLAY=$SOCK COSMOE_BACKEND=wayland APPIMAGE_EXTRACT_AND_RUN=1 \
	WAYLAND_DEBUG=client HOME="$HOME_DIR" "$APP" > "$OUT/app.log" 2> "$OUT/wayland-debug.log" &
PID=$!
sleep 15
if ! kill -0 $PID 2>/dev/null; then
	wait $PID; status=$?
	echo "wayland-smoke: the app exited with status $status"
	cat "$OUT/app.log"; tail -30 "$OUT/wayland-debug.log"
	kill $COMP 2>/dev/null; exit 1
fi
echo "wayland-smoke: the app is running under Wayland"
grep -m3 -E 'wl_seat|xdg_toplevel@[0-9]+\.set_title' "$OUT/wayland-debug.log"

WAYLAND_DISPLAY=$SOCK timeout 30 grim "$OUT/wayland-screenshot.png" || echo "wayland-smoke: screenshot failed"
kill $PID 2>/dev/null; sleep 1; kill $COMP 2>/dev/null
ls -l "$OUT"
COLORS=$(identify -format %k "$OUT/wayland-screenshot.png" 2>/dev/null || echo unknown)
if [ "$COLORS" = 1 ] || [ ! -s "$OUT/wayland-screenshot.png" ]; then
	echo "wayland-smoke: the screenshot is missing or blank: the window drew nothing"; exit 1
fi
if grep -qiE 'error|assert|crash|segment|abort|exception' "$OUT/app.log"; then
	echo "wayland-smoke: problems in the app log:"; grep -iE 'error|assert|crash|segment|abort|exception' "$OUT/app.log"
	exit 1
fi
echo "wayland-smoke: PASS"
