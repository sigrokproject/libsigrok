#!/bin/sh
##
## Build the hsdaoh-la driver's dependencies (libuvc and libhsdaoh, both
## static) for sigrok's MinGW cross build:
## sigrok-util/cross-compile/mingw/sigrok-cross-mingw (MXE, *.static.posix).
##
## Run it from sigrok-cross-mingw right before the libsigrok component,
## inside its build directory. It uses these variables of that script:
##
##   TOOLCHAIN_TRIPLET  e.g. i686-w64-mingw32.static.posix
##   PREFIX             install prefix ($HOME/sr_mingw_release_32 etc.)
##   CMAKE              $TOOLCHAIN_TRIPLET-cmake (MXE's cmake wrapper)
##   PARALLEL, V        optional make flags
##
## and PKG_CONFIG_PATH_<triplet>, which that script sets so that MXE's
## pkg-config finds libusb-1.0 (MXE package libusb1) and $PREFIX/lib/pkgconfig.
##
## Standalone use: export those yourself, e.g.
##   export PATH=$HOME/mxe-git/usr/bin:$PATH
##   export TOOLCHAIN_TRIPLET=i686-w64-mingw32.static.posix
##   export PREFIX=$HOME/sr_mingw_release_32 CMAKE=$TOOLCHAIN_TRIPLET-cmake
##   export PKG_CONFIG_PATH_i686_w64_mingw32_static_posix=$PREFIX/lib/pkgconfig:$HOME/mxe-git/usr/$TOOLCHAIN_TRIPLET/lib/pkgconfig
##   sh mxe-build-deps.sh
##

set -e

: "${TOOLCHAIN_TRIPLET:?set TOOLCHAIN_TRIPLET (see the top of this script)}"
: "${PREFIX:?set PREFIX (see the top of this script)}"
CMAKE=${CMAKE:-$TOOLCHAIN_TRIPLET-cmake}
PARALLEL=${PARALLEL:--j 2}

LIBUVC_REPO=${LIBUVC_REPO:-https://github.com/libuvc/libuvc}
LIBUVC_BRANCH=${LIBUVC_BRANCH:-master}
HSDAOH_REPO=${HSDAOH_REPO:-https://github.com/dovandung/hsdaoh}
HSDAOH_BRANCH=${HSDAOH_BRANCH:-la}

TOP=$(pwd)

# libuvc's cmake/FindLibUSB.cmake returns without doing anything on MinGW,
# so its LibUSB::LibUSB target would be missing. Define it from pkg-config.
cat > "$TOP/hsdaoh-libusb-target.cmake" <<'EOF'
if(NOT TARGET LibUSB::LibUSB)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(HSDAOH_LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)
  add_library(LibUSB::LibUSB INTERFACE IMPORTED)
  set_target_properties(LibUSB::LibUSB PROPERTIES
    INTERFACE_LINK_LIBRARIES PkgConfig::HSDAOH_LIBUSB)
endif()
EOF

echo "component libuvc ..."
rm -rf libuvc
git clone --depth=1 -b "$LIBUVC_BRANCH" "$LIBUVC_REPO" libuvc
mkdir -p libuvc/build
cd libuvc/build
$CMAKE \
	-DCMAKE_INSTALL_PREFIX:PATH="$PREFIX" \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_BUILD_TARGET=Static \
	-DBUILD_EXAMPLE=OFF \
	-DBUILD_TEST=OFF \
	-DBUILD_TESTING=OFF \
	-DDISABLE_JPEG=ON \
	-DENABLE_WARNINGS=OFF \
	-DCMAKE_PROJECT_INCLUDE="$TOP/hsdaoh-libusb-target.cmake" \
	..
make $PARALLEL $V
make install $V
cd "$TOP"

echo "component libhsdaoh ..."
rm -rf hsdaoh
git clone --depth=1 -b "$HSDAOH_BRANCH" "$HSDAOH_REPO" hsdaoh
mkdir -p hsdaoh/build
cd hsdaoh/build
$CMAKE \
	-DCMAKE_INSTALL_PREFIX:PATH="$PREFIX" \
	-DCMAKE_BUILD_TYPE=Release \
	-DHSDAOH_BUILD_SHARED=OFF \
	-DHSDAOH_BUILD_TOOLS=OFF \
	-DINSTALL_UDEV_RULES=OFF \
	..
make $PARALLEL $V
make install $V
cd "$TOP"

echo "libuvc and libhsdaoh installed to $PREFIX"
