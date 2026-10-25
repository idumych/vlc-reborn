#!/bin/sh
# build-appimage.sh — VLC Reborn for any x86_64 or aarch64 Linux desktop, as
# an AppImage, with Hokku (https://github.com/idumych/hokku, a fork of Cosmoe:
# the Haiku API as a library on X11 and Wayland). From the beos-app-platforms
# template.
#
#   sh packaging/appimage/build-appimage.sh
#
# Run it in CI (.github/workflows/appimage.yml, Ubuntu 24.04), not on a
# rolling-release host: the AppImage needs the oldest glibc it should run on.
# It builds for the machine it runs on (uname -m).
set -eu
cd "$(dirname "$0")/../.."
ROOT=$(pwd)
. ./haiku-app.env

COSMOE_REPO=${COSMOE_REPO:-idumych/hokku}
COSMOE_TAG=${COSMOE_TAG:-hokku}
LINUXDEPLOY_TAG=1-alpha-20251107-1
APPIMAGETOOL_TAG=1.9.1
case "$(uname -m)" in
	x86_64) ARCH=x86_64 ;;
	aarch64|arm64) ARCH=aarch64 ;;
	*) echo "build-appimage: unsupported architecture $(uname -m)" >&2; exit 1 ;;
esac

WORK="$ROOT/.cosmoe"
OUT="$ROOT/${APP_NAME}-${VERSION}-${ARCH}.AppImage"
mkdir -p "$WORK/tools"

# 1. Cosmoe (Hokku)
if [ -z "${COSMOE_STAGE:-}" ]; then
	COSMOE_STAGE="$WORK/stage"
	if [ ! -f "$COSMOE_STAGE/usr/lib/libbe.so" ]; then
		[ -d "$WORK/src" ] || git clone --depth 1 --branch "$COSMOE_TAG" \
			"https://github.com/$COSMOE_REPO.git" "$WORK/src"
		meson setup "$WORK/build" "$WORK/src" --prefix=/usr --libdir=lib \
			--buildtype=release -Denable_unit_test_compile=false
		ninja -C "$WORK/build"
		DESTDIR="$COSMOE_STAGE" meson install -C "$WORK/build" --no-rebuild
	fi
fi
CROOT="$COSMOE_STAGE/usr"

# 2. VLC, into the AppDir, against Hokku's Haiku API
APPDIR="$ROOT/obj/AppDir"
rm -rf "$APPDIR"
INC="-I$CROOT/include"
for d in config os os/app os/game os/interface os/kernel os/locale os/media os/net \
		os/storage os/support os/translation os/be_apps/Tracker os/opengl private/interface \
		private/shared private/support posix; do
	INC="$INC -I$CROOT/include/$d"
done
# vlc-cache-gen runs during the build and needs Hokku's libbe
LD_LIBRARY_PATH="$CROOT/lib" \
HAIKU_API_CFLAGS="$INC -DAPP_COSMOE -fPIC" \
HAIKU_API_LIBS="-L$CROOT/lib -lbe" \
	sh packaging/build-vlc.sh /usr "$APPDIR" HAIKU_GL_LIBS="-L$CROOT/lib -lopengl"
find "$APPDIR/usr" -name '*.la' -delete
rm -rf "$APPDIR/usr/include" "$APPDIR/usr/lib/pkgconfig" \
	"$APPDIR/usr/share/applications" "$APPDIR/usr/share/metainfo" "$APPDIR/usr/share/kde4"
rm -f "$APPDIR/usr/bin/cvlc" "$APPDIR/usr/bin/rvlc" "$APPDIR/usr/bin/nvlc" \
	"$APPDIR/usr/bin/qvlc" "$APPDIR/usr/bin/svlc"

# 3. Cosmoe's libraries, add-ons, data and fonts
DOC="$APPDIR/usr/share/doc/$APP_CMD"
mkdir -p "$APPDIR/usr/share/cosmoe" "$APPDIR/usr/share/fonts/noto" "$APPDIR/etc/fonts" "$DOC"
# libopengl: BGLView, for VLC's OpenGL output (the host's libEGL renders)
for lib in libbe.so libtracker.so libopengl.so libcosmoe-x11.so libcosmoe-wayland.so; do
	install -m 0755 "$CROOT/lib/$lib" "$APPDIR/usr/lib/$lib"
done
cp -a "$CROOT/lib/addons" "$APPDIR/usr/lib/"
for d in mime_db artwork locale; do
	cp -a "$CROOT/share/cosmoe/$d" "$APPDIR/usr/share/cosmoe/"
done
cp -a "$CROOT"/share/cosmoe/*.png "$APPDIR/usr/share/cosmoe/" 2>/dev/null || :
for f in NotoSans-Regular NotoSans-Bold NotoSans-Italic NotoSans-BoldItalic \
		NotoSansMono-Regular NotoSansMono-Bold; do
	find "$CROOT/share/fonts" -name "$f.ttf" -exec cp {} "$APPDIR/usr/share/fonts/noto/" \;
done
cp packaging/appimage/fonts.conf "$APPDIR/etc/fonts/fonts.conf"
cp COPYING "$DOC/COPYING"
cp "$WORK/src/License.md" "$DOC/COSMOE-LICENSE.md" 2>/dev/null \
	|| curl -fsSL "https://raw.githubusercontent.com/$COSMOE_REPO/$COSMOE_TAG/License.md" \
		-o "$DOC/COSMOE-LICENSE.md"
for so in libbe.so libtracker.so libcosmoe-x11.so libcosmoe-wayland.so; do
	strip --strip-unneeded "$APPDIR/usr/lib/$so"
done
find "$APPDIR/usr/lib/addons" -name '*.so' -exec strip --strip-unneeded {} \;

# desktop entry, icon and AppRun
GEN="$ROOT/obj/appimage-gen"
rm -rf "$GEN" && mkdir -p "$GEN"
cat > "$GEN/$APP_CMD.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=$APP_NAME
GenericName=Media Player
Comment=$SUMMARY
Exec=vlc %U
Icon=$APP_CMD
Terminal=false
Categories=$DESKTOP_CATEGORIES
MimeType=video/mp4;video/x-matroska;video/webm;video/ogg;video/mpeg;video/x-msvideo;video/quicktime;audio/mpeg;audio/ogg;audio/flac;audio/x-wav;
StartupWMClass=vlc
DESKTOP
cp share/icons/256x256/vlc.png "$GEN/$APP_CMD.png"
cp packaging/appimage/AppRun.in "$GEN/AppRun"
chmod +x "$GEN/AppRun"

# 4. Bundle the libraries VLC, its plugins and Cosmoe need, leaving out the
#    ones every desktop has (glibc, X11, GL...). NO_STRIP: linuxdeploy's
#    strip is older than some distros' libraries.
fetch() {
	[ -x "$WORK/tools/$1" ] || { curl -fsSL "$2" -o "$WORK/tools/$1"; chmod +x "$WORK/tools/$1"; }
}
fetch linuxdeploy "https://github.com/linuxdeploy/linuxdeploy/releases/download/$LINUXDEPLOY_TAG/linuxdeploy-$ARCH.AppImage"
fetch appimagetool "https://github.com/AppImage/appimagetool/releases/download/$APPIMAGETOOL_TAG/appimagetool-$ARCH.AppImage"
export APPIMAGE_EXTRACT_AND_RUN=1

LDROOT="$WORK/tools/linuxdeploy-root"
if [ ! -x "$LDROOT/AppRun" ]; then
	( cd "$WORK/tools" && rm -rf squashfs-root && ./linuxdeploy --appimage-extract >/dev/null \
		&& mv squashfs-root linuxdeploy-root )
fi
PATCHELF=$(find "$LDROOT" -name patchelf -type f | head -1)
# Run paths inside the AppDir only: libraries in usr/lib, plugins two and
# three levels below it, Cosmoe add-ons below usr/lib/addons
for so in "$APPDIR"/usr/lib/*.so*; do
	[ -L "$so" ] || "$PATCHELF" --set-rpath '$ORIGIN' "$so"
done
"$PATCHELF" --set-rpath '$ORIGIN/../lib' "$APPDIR/usr/bin/vlc"
for so in $(find "$APPDIR/usr/lib/vlc" -name '*.so'); do
	"$PATCHELF" --set-rpath '$ORIGIN/../../..:$ORIGIN/../..' "$so"
done
for so in $(find "$APPDIR/usr/lib/addons" -name '*.so'); do
	"$PATCHELF" --set-rpath '$ORIGIN/../..' "$so"
done
DEPS_ONLY=""
for so in $(find "$APPDIR/usr/lib/addons" "$APPDIR/usr/lib/vlc" -name '*.so') \
		"$APPDIR/usr/lib/libcosmoe-x11.so" "$APPDIR/usr/lib/libcosmoe-wayland.so"; do
	DEPS_ONLY="$DEPS_ONLY --deploy-deps-only=$so"
done
# shellcheck disable=SC2086
NO_STRIP=1 LD_LIBRARY_PATH="$APPDIR/usr/lib" "$LDROOT/AppRun" --appdir "$APPDIR" \
	--executable "$APPDIR/usr/bin/vlc" $DEPS_ONLY \
	--desktop-file "$GEN/$APP_CMD.desktop" \
	--icon-file "$GEN/$APP_CMD.png" \
	--custom-apprun "$GEN/AppRun"

# The app's resources (signature, version, icon) as Cosmoe reads them: a
# .cosmoe_rsrc section in the executable. Added after linuxdeploy so nothing
# strips it; Cosmoe finds it by its magic, so it needn't be loaded.
LD_LIBRARY_PATH="$CROOT/lib" "$CROOT/bin/rc" -o "$GEN/$APP_NAME.rsrc" "$ROOT/$RDEF"
objcopy --add-section .cosmoe_rsrc="$GEN/$APP_NAME.rsrc" \
	--set-section-flags .cosmoe_rsrc=readonly,data "$APPDIR/usr/bin/vlc"

# The plugin cache, for the paths inside the AppImage
LD_LIBRARY_PATH="$APPDIR/usr/lib" "$APPDIR/usr/lib/vlc/vlc-cache-gen" \
	"$APPDIR/usr/lib/vlc/plugins" || :

# 5. Pack
rm -f "$OUT"
ARCH=$ARCH "$WORK/tools/appimagetool" --no-appstream "$APPDIR" "$OUT"
echo "Built $OUT"
