#!/bin/sh
# build-deb.sh — builds vlc_<version>-<revision>_<arch>.deb for VitruvianOS
# (a BeOS-like OS based on Linux), from the beos-app-platforms template.
#
#   . <vos-sdk>/sdk/env.sh && sh packaging/build-deb.sh     (from the repo root)
#
# VLC goes to /opt/vlc, the application to /system/apps/VLC (with the app
# resources and, through postinst, its attributes), plus a Deskbar entry and
# /usr/bin/vlc. Runtime libraries come from Debian trixie, which is
# VitruvianOS's userland.
set -eu
cd "$(dirname "$0")/.."
ROOT=$(pwd)
. ./haiku-app.env

ARCH=$(dpkg --print-architecture)
VOS_DEPENDS="vos (>= 0.6.2), vos (<< 0.7~)"
PREFIX=/opt/vlc
export SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-$(git log -1 --format=%ct 2>/dev/null || date +%s)}"

BUILD="$ROOT/obj/deb"
PKG="$BUILD/${APP_CMD}_${VERSION}-${REVISION}_${ARCH}"
OUT="$ROOT/${APP_CMD}_${VERSION}-${REVISION}_${ARCH}.deb"

for tool in rc xres resattr dpkg-deb dpkg-shlibdeps tar pkg-config; do
	command -v "$tool" >/dev/null 2>&1 || {
		echo "build-deb: '$tool' not found in PATH (source the SDK's env.sh?)" >&2; exit 1; }
done

# 1. VLC, against the VitruvianOS Haiku API
rm -rf "$BUILD"
# The SDK's libraries only link: libroot needs VitruvianOS's Nexus device to
# load ("Can't open Nexus IPC"), so nothing linked to them runs here. The
# plugin cache is made by postinst on VitruvianOS.
VLC_SKIP_CACHE_GEN=1 \
HAIKU_API_CFLAGS="$(pkg-config --cflags vos) -fPIC" \
HAIKU_API_LIBS="$(pkg-config --libs vos)" \
	sh packaging/build-vlc.sh "$PREFIX" "$PKG"
find "$PKG$PREFIX" -name '*.la' -delete
rm -rf "$PKG$PREFIX/include" "$PKG$PREFIX/lib/pkgconfig" \
	"$PKG$PREFIX/share/applications" "$PKG$PREFIX/share/icons" \
	"$PKG$PREFIX/share/metainfo" "$PKG$PREFIX/share/kde4"
rm -f "$PKG$PREFIX/bin/cvlc" "$PKG$PREFIX/bin/rvlc" "$PKG$PREFIX/bin/nvlc" \
	"$PKG$PREFIX/bin/qvlc" "$PKG$PREFIX/bin/svlc"

# 2. The application, with resources. xres appends them after the ELF data,
#    so it runs after install-strip.
RDEF="$BUILD/vlc.rdef"
RSRC="$BUILD/vlc.rsrc"
MAJOR=$(echo "$VERSION" | cut -d. -f1); MIDDLE=$(echo "$VERSION" | cut -d. -f2)
MINOR=$(echo "$VERSION" | cut -d. -f3)
sed -e "s|@APP_SIGNATURE@|$APP_SIG|" -e "s|@MAJOR@|$MAJOR|" -e "s|@MIDDLE@|$MIDDLE|" \
	-e "s|@MINOR@|$MINOR|" -e "s|@LONG_INFO@|$SUMMARY|" \
	haikuports/media-video/vlc/additional-files/vlc.rdef.in > "$RDEF"
rc -o "$RSRC" "$RDEF"
[ -s "$RSRC" ] || { echo "build-deb: rc made no resources" >&2; exit 1; }
mkdir -p "$PKG/system/apps" "$PKG/system/data/deskbar/menu/Applications" "$PKG/usr/bin"
install -m 0755 "$PKG$PREFIX/bin/vlc" "$PKG/system/apps/$APP_NAME"
rm "$PKG$PREFIX/bin/vlc"
ln -s "/system/apps/$APP_NAME" "$PKG$PREFIX/bin/vlc"
xres -o "$PKG/system/apps/$APP_NAME" "$RSRC"
ln -s "/system/apps/$APP_NAME" "$PKG/system/data/deskbar/menu/Applications/$APP_NAME"
ln -s "/system/apps/$APP_NAME" "$PKG/usr/bin/$APP_CMD"

# 3. Attributes: dpkg drops xattrs, so they ship on an EMPTY placeholder in a
#    tarball, and postinst copies the attributes (not the file) onto the app.
DOCDIR="$PKG/usr/share/doc/$APP_CMD"
mkdir -p "$PKG/usr/share/$APP_CMD" "$DOCDIR"
STAGE="$BUILD/attrs"
mkdir -p "$STAGE/system/apps"
: > "$STAGE/system/apps/$APP_NAME"
resattr -O -o "$STAGE/system/apps/$APP_NAME" "$RSRC"
TYPE_HEX=534d494d$(printf '%s' application/x-vnd.Be-elfexecutable | od -A n -t x1 | tr -d ' \n')00
python3 -c 'import os,sys; os.setxattr(sys.argv[1], "user.beos.BEOS:TYPE", bytes.fromhex(sys.argv[2]))' \
	"$STAGE/system/apps/$APP_NAME" "$TYPE_HEX"
tar --xattrs --xattrs-include='user.*' --owner=0 --group=0 --numeric-owner \
	--mtime="@$SOURCE_DATE_EPOCH" -cf "$PKG/usr/share/$APP_CMD/apps_attrs.tar" \
	-C "$STAGE" "./system/apps/$APP_NAME"

# 4. Docs
cp COPYING "$DOCDIR/copyright"
gzip -9n -c packaging/debian/changelog > "$DOCDIR/changelog.Debian.gz"
gzip -9n -c README.md > "$DOCDIR/README.md.gz"

# 5. Runtime dependencies from Debian; libvlc and the Haiku API libraries
#    (VitruvianOS's own, from the vos package) are left out
SHLIB="$BUILD/shlibdeps"
mkdir -p "$SHLIB/debian"
printf 'Source: vlc\n\nPackage: vlc\nArchitecture: any\n' > "$SHLIB/debian/control"
ELFS=$(find "$PKG" -type f \( -name '*.so*' -o -perm -u+x \) -exec sh -c \
	'head -c4 "$1" | grep -q ELF && echo "$1"' _ {} \;)
# shellcheck disable=SC2086
( cd "$SHLIB" && dpkg-shlibdeps -O --ignore-missing-info -l"$PKG$PREFIX/lib" \
	-xlibvlc5 -xlibvlccore9 $ELFS 2>"$BUILD/shlibdeps.log" ) \
	| sed -n 's/^shlibs:Depends=//p' > "$BUILD/depends"
grep -v -E 'libbe|libroot|libtracker' "$BUILD/shlibdeps.log" | grep -i -E 'error|warning: package' | head -5 || :
DEPENDS="$VOS_DEPENDS, $(cat "$BUILD/depends"), attr"

mkdir -p "$PKG/DEBIAN"
sed -e "s|@APP_NAME@|$APP_NAME|g" -e "s|@APP_CMD@|$APP_CMD|g" \
	packaging/debian/postinst.in > "$PKG/DEBIAN/postinst"
chmod 0755 "$PKG/DEBIAN/postinst"
SIZE=$(du -sk --apparent-size --exclude=DEBIAN "$PKG" | cut -f1)
LONGDESC=$(sed -e 's/^$/./' -e 's/^/ /' "$DESCRIPTION_FILE")
cat > "$PKG/DEBIAN/control" <<CTRL
Package: $APP_CMD
Version: ${VERSION}-${REVISION}
Architecture: ${ARCH}
Maintainer: ${PACKAGER}
Installed-Size: ${SIZE}
Depends: ${DEPENDS}
Conflicts: vlc-bin
Replaces: vlc-bin
Section: ${DEB_SECTION}
Priority: optional
Homepage: ${HOMEPAGE}
Description: ${SUMMARY}
${LONGDESC}
CTRL
( cd "$PKG" && find . -type f ! -path './DEBIAN/*' -printf '%P\0' \
	| sort -z | xargs -0 md5sum ) > "$PKG/DEBIAN/md5sums"
find "$PKG" -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} +
chmod -R u+rwX,go+rX,go-w "$PKG"

dpkg-deb --root-owner-group -Zxz --build "$PKG" "$OUT"
echo "Built $OUT"
