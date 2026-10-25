#!/bin/sh
# make-sdk.sh — builds a link-time SDK for VitruvianOS 0.6.2 (headers, libbe, libroot,
# libtracker, libtranslation, libtextencoding, and the rc/xres/resattr resource tools), so an
# app can be compiled, linked and packaged as a .deb without a VitruvianOS machine.
#
#   sh tools/vos-sdk/make-sdk.sh [WORK_DIR]        (default ./.vos-sdk)
#   . WORK_DIR/sdk/env.sh && make deb
#
# Runs natively on amd64 or arm64 Debian trixie (VitruvianOS's userland, glibc 2.41), with the
# apt packages listed in .github/workflows/deb.yml. GitHub's ubuntu-24.04 and ubuntu-24.04-arm
# runners, each in a debian:trixie container, build both architectures.
#
# None of these libraries go into the package: the app uses the ones installed on
# VitruvianOS. The link proves every symbol the app needs is exported by 0.6.2.
# Adapted from Folio's and ivpn-vitruvian's make-sdk.sh.
set -eu

VOS_TAG=0.6.2
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=$(mkdir -p "${1:-.vos-sdk}" && cd "${1:-.vos-sdk}" && pwd)
SRC=$WORK/src
SDK=$WORK/sdk
LINKDEPS=$WORK/linkdeps
JOBS=$(nproc 2>/dev/null || echo 4)

case "$(dpkg --print-architecture)" in
	amd64) ARCH=amd64; LD_SO=/lib64/ld-linux-x86-64.so.2 ;;
	arm64) ARCH=arm64; LD_SO=/lib/ld-linux-aarch64.so.1 ;;
	*) echo "make-sdk: unsupported architecture $(dpkg --print-architecture)" >&2; exit 1 ;;
esac

say() { printf '\n== %s\n' "$*"; }

say "VitruvianOS $VOS_TAG source"
mkdir -p "$SRC"
if [ ! -d "$SRC/vos/.git" ]; then
	git clone -q --depth 1 --branch "$VOS_TAG" \
		https://github.com/VitruvianOS/Vitruvian.git "$SRC/vos"
	git -C "$SRC/vos" submodule update --init --depth 1 src/system/kernel/nexus
fi
git -C "$SRC/vos" diff --quiet && git -C "$SRC/vos" apply "$HERE/vos-$VOS_TAG-linkonly.patch"
CMAKE_OPTS="-DVOS_DYNAMIC_LINKER=$LD_SO"

say "Buildtools (rc, xres, resattr)"
if [ ! -x "$SRC/vos/buildtools/src/bin/xres" ]; then
	mkdir -p "$SRC/vos/buildtools"
	( cd "$SRC/vos/buildtools"
	  cmake -DBUILDTOOLS_MODE=1 $CMAKE_OPTS .. -GNinja > /dev/null
	  ninja -j"$JOBS" > /dev/null )
fi

# libtracker links libgame (game kit), and libgame links libmedia2, which needs PipeWire and
# GStreamer. Placeholders with the real names stand in so libtracker links; the one symbol
# libtracker takes from libgame is defined.
mkdir -p "$LINKDEPS"
if [ ! -e "$LINKDEPS/libmedia2.so" ]; then
	echo '' > "$WORK/empty.c"
	gcc -shared -fPIC -Wl,-soname,libmedia2.so "$WORK/empty.c" -o "$LINKDEPS/libmedia2.so"
	printf '#include <stdint.h>\nvoid set_mouse_position(int32_t, int32_t) {}\n' > "$WORK/game.cpp"
	g++ -shared -fPIC -Wl,-soname,libgame.so "$WORK/game.cpp" -o "$LINKDEPS/libgame.so"
fi

say "libroot, libbe, libtranslation, libtracker ($ARCH)"
G=$SRC/vos/generated
mkdir -p "$G"
( cd "$G"
  cmake -DCMAKE_BUILD_TYPE=Release -DBUILDTOOLS_DIR=../buildtools \
	-DHOST_KERNEL="$(uname -r)" -DKERNEL_RELEASE="$(uname -r)" \
	-DVITRUVIAN_TARGET_ARCH=$ARCH $CMAKE_OPTS \
	-DCMAKE_SHARED_LINKER_FLAGS="-L$LINKDEPS" .. -GNinja > /dev/null
  LIBRARY_PATH=$LINKDEPS ninja -j"$JOBS" root be translation tracker )

say "SDK in $SDK"
rm -rf "$SDK"; mkdir -p "$SDK/include/vos" "$SDK/lib" "$SDK/bin" "$SDK/linkdeps"
( cd "$SRC/vos/headers" && tar cf - --exclude=./private --exclude=./libs \
	--exclude=./tools . ) | tar xf - -C "$SDK/include/vos"
cp "$G/src/system/libroot.so" "$G/src/kits/libbe.so" \
	"$G/src/kits/tracker/libtracker.so" "$G/src/kits/translation/libtranslation.so" \
	"$G/src/kits/textencoding/libtextencoding.so" "$SDK/lib/"
cp -a "$LINKDEPS"/*.so "$SDK/linkdeps/"
B=$SRC/vos/buildtools/src/bin
cp "$B/rc/rc" "$B/xres" "$B/resattr" "$SDK/bin/"
ls "$SDK/lib" "$SDK/bin"

# vos.pc the way newer VitruvianOS (vos-dev) generates it
python3 - "$SRC/vos" "$SDK" "$ARCH" <<'EOF'
import re, sys
vos, sdk, arch = sys.argv[1:]
s = open(vos + '/build/headers.cmake').read()
body = s[s.index('PUBLIC_HEADERS'):]
body = body[:body.index(')')]
flags = []
for d in re.findall(r'"([^"]+)"', body):
    d = d.rstrip('/').replace('os/arch/x86_64', 'os/arch/' + ('arm64' if arch == 'arm64' else 'x86_64'))
    f = '-I${includedir}' if d == 'headers' else '-I${includedir}/' + d[len('headers/'):]
    if f not in flags:
        flags.append(f)
flags += ['-I${includedir}/build', '-I${includedir}/build/config_headers',
          '-I${includedir}/config', '-include LinuxBuildCompatibility.h']
open(sdk + '/lib/vos.pc', 'w').write(f"""prefix={sdk}
includedir=${{prefix}}/include/vos
libdir=${{prefix}}/lib

Name: vos
Description: VitruvianOS 0.6.2 link-time SDK ({arch})
Version: 0.6.2
Libs: -L${{libdir}} -lbe -lroot
Cflags: {' '.join(flags)}
""")
EOF

cat > "$SDK/env.sh" <<EOF
# source me: . $SDK/env.sh
export PATH="$SDK/bin:\$PATH"
export PKG_CONFIG_PATH="$SDK/lib"
export LDFLAGS="-L$SDK/lib -Wl,-rpath-link=$SDK/lib:$SDK/linkdeps"
export VOS_VERSION=$VOS_TAG
EOF
say "done ($ARCH)"
echo "Use it with:   . $SDK/env.sh && make deb"
