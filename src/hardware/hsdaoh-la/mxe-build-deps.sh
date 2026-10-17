#!/bin/sh
##
## Build the hsdaoh-la driver's dependencies for sigrok's MinGW (MXE) cross
## build: libusb 1.0.30, libuvc and libhsdaoh, all static.
##
## Why a private libusb: sigrok's MXE uses an old libusb fork
## (1.0.20-rc3 "event-abstraction"), which libsigrok's own USB code needs on
## Windows, but which has no WinUSB isochronous support (added in 1.0.23) and
## is not what libhsdaoh is tested with. So libuvc and libhsdaoh are built
## against libusb 1.0.30, and every symbol that libusb defines is renamed
## (libusb_init -> hsdaoh_priv_libusb_init, ...) in all three libraries.
## Both libusb versions can then be linked into the same program.
##
## Installed into $PREFIX:
##   lib/libhsdaoh.a, lib/libhsdaoh_uvc.a, lib/libhsdaoh_usb.a
##   include/hsdaoh.h, include/hsdaoh_export.h
##   lib/pkgconfig/libhsdaoh.pc   (links all three, nothing else needed)
##
## Variables (the sigrok MXE scripts set them; export them for standalone use):
##   TOOLCHAIN_TRIPLET  e.g. i686-w64-mingw32.static.posix
##   PREFIX             install prefix (sigrok-build calls it INSTALL_DIR)
##   CMAKE              default $TOOLCHAIN_TRIPLET-cmake (MXE's cmake wrapper)
##   PARALLEL, V        optional make flags
##   HSDAOH_SRC, LIBUVC_SRC, LIBUSB_SRC   local source trees (else git clone)
##
## Standalone example:
##   export PATH=$HOME/mxe/usr/bin:$PATH
##   export TOOLCHAIN_TRIPLET=i686-w64-mingw32.static.posix
##   export PREFIX=$HOME/sr_mingw_release_32
##   sh mxe-build-deps.sh
##

set -e

: "${TOOLCHAIN_TRIPLET:?set TOOLCHAIN_TRIPLET (see the top of this script)}"
PREFIX=${PREFIX:-$INSTALL_DIR}
: "${PREFIX:?set PREFIX (see the top of this script)}"
CMAKE=${CMAKE:-$TOOLCHAIN_TRIPLET-cmake}
PARALLEL=${PARALLEL:--j2}
NM=${NM:-$TOOLCHAIN_TRIPLET-nm}
OBJCOPY=${OBJCOPY:-$TOOLCHAIN_TRIPLET-objcopy}

LIBUSB_REPO=${LIBUSB_REPO:-https://github.com/libusb/libusb}
LIBUSB_BRANCH=${LIBUSB_BRANCH:-v1.0.30}
LIBUVC_REPO=${LIBUVC_REPO:-https://github.com/libuvc/libuvc}
LIBUVC_BRANCH=${LIBUVC_BRANCH:-master}
HSDAOH_REPO=${HSDAOH_REPO:-https://github.com/dovandung/hsdaoh}
HSDAOH_BRANCH=${HSDAOH_BRANCH:-la}

SYMPFX=hsdaoh_priv_

TOP=$(pwd)/hsdaoh-deps
PRIV=$TOP/prefix		# private install, never seen by libsigrok
rm -rf "$TOP"
mkdir -p "$TOP" "$PRIV"
cd "$TOP"

# The private prefix comes first for pkg-config, so libuvc and hsdaoh find
# libusb 1.0.30 instead of MXE's libusb. (MXE's pkg-config wrapper reads
# PKG_CONFIG_PATH_<triplet>; plain PKG_CONFIG_PATH covers other setups.)
PCVAR=PKG_CONFIG_PATH_$(echo "$TOOLCHAIN_TRIPLET" | tr '.-' '__')
eval "OLD_PCPATH=\${$PCVAR}"
eval "export $PCVAR=\"$PRIV/lib/pkgconfig\""
OLD_PKG_CONFIG_PATH=$PKG_CONFIG_PATH
export PKG_CONFIG_PATH="$PRIV/lib/pkgconfig"

getsrc() {	# name src-dir repo branch
	if [ -n "$2" ]; then
		cp -a "$2" "$1"
	else
		git clone --depth=1 -b "$4" "$3" "$1"
	fi
}

echo "component libusb (private copy for libhsdaoh) ..."
getsrc libusb "$LIBUSB_SRC" "$LIBUSB_REPO" "$LIBUSB_BRANCH"
cd libusb
[ -f configure ] || sh ./bootstrap.sh
./configure --host="$TOOLCHAIN_TRIPLET" --prefix="$PRIV" \
	--disable-shared --enable-static --disable-examples-build --disable-tests-build
make $PARALLEL $V
make install $V
cd "$TOP"

# Included into the libuvc and hsdaoh CMake projects:
# - The cross toolchain only searches its own root for libraries, so add the
#   private prefix (else pkg_check_modules would resolve libusb/libuvc to
#   MXE's libraries, or to nothing).
# - libuvc's cmake/FindLibUSB.cmake does nothing on MinGW, so its
#   LibUSB::LibUSB target would be missing. Define it for the private libusb.
cat > "$TOP/libusb-target.cmake" <<EOF
list(INSERT CMAKE_FIND_ROOT_PATH 0 "$PRIV")
if(NOT TARGET LibUSB::LibUSB)
  add_library(LibUSB::LibUSB STATIC IMPORTED)
  set_target_properties(LibUSB::LibUSB PROPERTIES
    IMPORTED_LOCATION "$PRIV/lib/libusb-1.0.a"
    INTERFACE_INCLUDE_DIRECTORIES "$PRIV/include/libusb-1.0")
endif()
EOF

echo "component libuvc ..."
getsrc libuvc "$LIBUVC_SRC" "$LIBUVC_REPO" "$LIBUVC_BRANCH"
mkdir -p libuvc/build
cd libuvc/build
$CMAKE \
	-DCMAKE_INSTALL_PREFIX:PATH="$PRIV" \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_BUILD_TARGET=Static \
	-DBUILD_EXAMPLE=OFF \
	-DBUILD_TEST=OFF \
	-DBUILD_TESTING=OFF \
	-DDISABLE_JPEG=ON \
	-DENABLE_WARNINGS=OFF \
	-DCMAKE_PROJECT_INCLUDE="$TOP/libusb-target.cmake" \
	..
make $PARALLEL $V
make install $V
cd "$TOP"

echo "component libhsdaoh ..."
getsrc hsdaoh "$HSDAOH_SRC" "$HSDAOH_REPO" "$HSDAOH_BRANCH"
mkdir -p hsdaoh/build
cd hsdaoh/build
$CMAKE \
	-DCMAKE_INSTALL_PREFIX:PATH="$PRIV" \
	-DCMAKE_BUILD_TYPE=Release \
	-DHSDAOH_BUILD_SHARED=OFF \
	-DHSDAOH_BUILD_TOOLS=OFF \
	-DINSTALL_UDEV_RULES=OFF \
	-DCMAKE_PROJECT_INCLUDE="$TOP/libusb-target.cmake" \
	..
make $PARALLEL $V
make install $V
cd "$TOP"

eval "export $PCVAR=\"\$OLD_PCPATH\""
export PKG_CONFIG_PATH="$OLD_PKG_CONFIG_PATH"

echo "renaming the private libusb's symbols ..."
# Every external symbol libusb-1.0.a defines (code and data). On 32-bit
# Windows C symbols carry a leading underscore, and GCC/clang may emit
# .refptr.<symbol> helpers: the prefix goes after both.
$NM -g --defined-only "$PRIV/lib/libusb-1.0.a" |
	awk 'NF == 3 { print $3 }' | sort -u |
	sed -E "s/^(\.refptr\.)?(_?)(.*)$/\1\2\3 \1\2$SYMPFX\3/" > "$TOP/libusb.syms"
n=$(wc -l < "$TOP/libusb.syms")
[ "$n" -gt 50 ] || { echo "only $n libusb symbols found, giving up" >&2; exit 1; }
echo "$n symbols"

mkdir -p "$PREFIX/lib/pkgconfig" "$PREFIX/include"
$OBJCOPY --redefine-syms="$TOP/libusb.syms" "$PRIV/lib/libusb-1.0.a" "$PREFIX/lib/libhsdaoh_usb.a"
$OBJCOPY --redefine-syms="$TOP/libusb.syms" "$PRIV/lib/libuvc.a" "$PREFIX/lib/libhsdaoh_uvc.a"
$OBJCOPY --redefine-syms="$TOP/libusb.syms" "$PRIV/lib/libhsdaoh.a" "$PREFIX/lib/libhsdaoh.a"
cp "$PRIV/include/hsdaoh.h" "$PRIV/include/hsdaoh_export.h" "$PREFIX/include/"

# Nothing may still refer to an unrenamed libusb symbol.
left=$($NM -u "$PREFIX/lib/libhsdaoh.a" "$PREFIX/lib/libhsdaoh_uvc.a" 2>/dev/null |
	awk '{ print $NF }' | grep -E '^_?libusb_' || true)
if [ -n "$left" ]; then
	echo "unrenamed libusb references left:" >&2
	echo "$left" >&2
	exit 1
fi

HSDAOH_VERSION=$(sed -n 's/^Version: *//p' "$PRIV/lib/pkgconfig/libhsdaoh.pc")
cat > "$PREFIX/lib/pkgconfig/libhsdaoh.pc" <<EOF
prefix=$PREFIX
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: hsdaoh Library
Description: hsdaoh (static, with private libuvc and libusb 1.0.30)
Version: ${HSDAOH_VERSION:-0.0.0}
Cflags: -I\${includedir} -Dhsdaoh_STATIC
Libs: -L\${libdir} -lhsdaoh -lhsdaoh_uvc -lhsdaoh_usb -lpthread
EOF

cd "$TOP/.."
echo "libhsdaoh (with private libuvc and libusb) installed to $PREFIX"
