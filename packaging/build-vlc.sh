#!/bin/sh
# build-vlc.sh — configures, builds and installs this VLC tree for a Linux
# system with the Haiku API (VitruvianOS, or Cosmoe/Hokku for the AppImage).
#
#   HAIKU_API_CFLAGS=... HAIKU_API_LIBS=... \
#     sh packaging/build-vlc.sh PREFIX DESTDIR [extra configure options]
#
# Used by .github/workflows/deb.yml and appimage.yml. The Haiku build uses the
# HaikuPorts recipe in haikuports/media-video/vlc instead.
set -eu
cd "$(dirname "$0")/.."
PREFIX=$1
DESTDIR=$2
shift 2

: "${HAIKU_API_CFLAGS:?set HAIKU_API_CFLAGS}"
: "${HAIKU_API_LIBS:?set HAIKU_API_LIBS}"
JOBS=$(nproc 2>/dev/null || echo 4)

# configure.ac and the Makefile.am files are patched; the generated files in
# the release tarball are not
[ -f .reborn-bootstrapped ] || { autoreconf -fi; touch .reborn-bootstrapped; }

mkdir -p obj/vlc-build
cd obj/vlc-build
# Strip at install; the run path lets /opt installs find libvlc
../../configure --prefix="$PREFIX" \
	--enable-haiku-api \
	HAIKU_API_CFLAGS="$HAIKU_API_CFLAGS" HAIKU_API_LIBS="$HAIKU_API_LIBS" \
	LDFLAGS="${LDFLAGS:-} -Wl,-rpath,$PREFIX/lib" \
	--disable-qt --disable-skins2 --disable-xcb --disable-wayland \
	--disable-update-check --disable-static \
	--disable-dbus --disable-gst-decode \
	"$@"
# VLC_SKIP_CACHE_GEN=1: don't run vlc-cache-gen (it skips the plugin cache
# when build and host differ). For targets whose Haiku API libraries can't
# run on the build machine; generate the cache when installing instead.
if [ "${VLC_SKIP_CACHE_GEN:-}" = 1 ]; then
	set -- build=skip-plugin-cache
else
	set --
fi
make -j"$JOBS" "$@"
make install-strip DESTDIR="$DESTDIR" "$@"
