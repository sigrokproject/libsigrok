##
## This file is part of the sigrok project.
##
## Copyright (C) 2010-2012 Bert Vermeulen <bert@biot.com>
##
## This program is free software: you can redistribute it and/or modify
## it under the terms of the GNU General Public License as published by
## the Free Software Foundation, either version 3 of the License, or
## (at your option) any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program.  If not, see <http://www.gnu.org/licenses/>.
##

# We require at least autoconf 2.63 (AC_INIT format changed there).
AC_PREREQ([2.63])

# libsigrok package version number (NOT the same as shared lib version!).
m4_define([libsigrok_major_version], [0])
m4_define([libsigrok_minor_version], [2])
m4_define([libsigrok_micro_version], [0])
m4_define([libsigrok_version], [libsigrok_major_version.libsigrok_minor_version.libsigrok_micro_version])

AC_INIT([libsigrok], [libsigrok_version], [sigrok-devel@lists.sourceforge.net],
	[libsigrok], [http://www.sigrok.org])
AC_CONFIG_HEADER([config.h])
AC_CONFIG_MACRO_DIR([autostuff])
AC_CONFIG_AUX_DIR([autostuff])

# We require at least automake 1.11 (needed for 'silent rules').
AM_INIT_AUTOMAKE([1.11 -Wall -Werror foreign std-options])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AH_TOP([#ifndef LIBSIGROK_CONFIG_H
#define LIBSIGROK_CONFIG_H    /* To stop multiple inclusions. */])
AH_BOTTOM([#endif /* LIBSIGROK_CONFIG_H */])

# Enable more compiler warnings via -Wall and -Wextra. Add -fvisibility=hidden
# and enforce use of SR_API to explicitly mark all public API functions.
CFLAGS="-g -Wall -Wextra -fvisibility=hidden"

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC
AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_LN_S

# Initialize libtool.
LT_INIT

# Initialize pkg-config.
# We require at least 0.22, as "Requires.private" behaviour changed there.
PKG_PROG_PKG_CONFIG([0.22])

# Library version for libsigrok (NOT the same as the package version).
# Carefully read the libtool docs before updating these numbers!
# The algorithm for determining which number to change (and how) is nontrivial!
# http://www.gnu.org/software/libtool/manual/libtool.html#Updating-version-info
LIBSIGROK_LT_CURRENT=0
LIBSIGROK_LT_REVISION=0
LIBSIGROK_LT_AGE=0
LIBSIGROK_LT_VERSION="$LIBSIGROK_LT_CURRENT:$LIBSIGROK_LT_REVISION:$LIBSIGROK_LT_AGE"
LIBSIGROK_LT_LDFLAGS="-version-info $LIBSIGROK_LT_VERSION"
AC_SUBST(LIBSIGROK_LT_LDFLAGS)

# Logic analyzer hardware support '--enable' options.

# Disabled by default, unfinished.
AC_ARG_ENABLE(alsa, AC_HELP_STRING([--enable-alsa],
	      [enable ALSA driver support [default=no]]),
	      [LA_ALSA="$enableval"],
	      [LA_ALSA=no])
AM_CONDITIONAL(LA_ALSA, test x$LA_ALSA = xyes)
if test "x$LA_ALSA" = "xyes"; then
	AC_DEFINE(HAVE_LA_ALSA, 1, [ALSA driver support])
fi

AC_ARG_ENABLE(asix-sigma, AC_HELP_STRING([--enable-asix-sigma],
	      [enable ASIX Sigma support [default=yes]]),
	      [LA_ASIX_SIGMA="$enableval"],
	      [LA_ASIX_SIGMA=yes])
AM_CONDITIONAL(LA_ASIX_SIGMA, test x$LA_ASIX_SIGMA = xyes)
if test "x$LA_ASIX_SIGMA" = "xyes"; then
	AC_DEFINE(HAVE_LA_ASIX_SIGMA, 1, [ASIX Sigma support])
fi

AC_ARG_ENABLE(chronovu-la8, AC_HELP_STRING([--enable-chronovu-la8],
	      [enable ChronoVu LA8 support [default=yes]]),
	      [LA_CHRONOVU_LA8="$enableval"],
	      [LA_CHRONOVU_LA8=yes])
AM_CONDITIONAL(LA_CHRONOVU_LA8, test x$LA_CHRONOVU_LA8 = xyes)
if test "x$LA_CHRONOVU_LA8" = "xyes"; then
	AC_DEFINE(HAVE_LA_CHRONOVU_LA8, 1, [ChronoVu LA8 support])
fi

# Disabled by default, unfinished.
AC_ARG_ENABLE(fx2lafw, AC_HELP_STRING([--enable-fx2lafw],
	      [enable open source fx2lafw driver support for fx2 devices. [default=no]]),
	      [LA_FX2LAFW="$enableval"],
	      [LA_FX2LAFW=no])
AM_CONDITIONAL(LA_FX2LAFW, test x$LA_FX2LAFW = xyes)
if test "x$LA_FX2LAFW" = "xyes"; then
	AC_DEFINE(HAVE_LA_FX2LAFW, 1, [fx2lafw support])
fi

AC_ARG_ENABLE(demo, AC_HELP_STRING([--enable-demo],
	      [enable demo driver support [default=yes]]),
	      [LA_DEMO="$enableval"],
	      [LA_DEMO=yes])
AM_CONDITIONAL(LA_DEMO, test x$LA_DEMO = xyes)
if test "x$LA_DEMO" = "xyes"; then
	AC_DEFINE(HAVE_LA_DEMO, 1, [Demo driver support])
fi

# Disabled by default, unfinished.
AC_ARG_ENABLE(link-mso19, AC_HELP_STRING([--enable-link-mso19],
	      [enable Link Instruments MSO-19 support [default=no]]),
	      [LA_LINK_MSO19="$enableval"],
	      [LA_LINK_MSO19=no])
AM_CONDITIONAL(LA_LINK_MSO19, test x$LA_LINK_MSO19 = xyes)
if test "x$LA_LINK_MSO19" = "xyes"; then
	AC_DEFINE(HAVE_LA_LINK_MSO19, 1, [Link Instruments MSO-19 support])
fi

AC_ARG_ENABLE(ols, AC_HELP_STRING([--enable-ols],
	      [enable OpenBench Logic Sniffer (OLS) support [default=yes]]),
	      [LA_OLS="$enableval"],
	      [LA_OLS=yes])
AM_CONDITIONAL(LA_OLS, test x$LA_OLS = xyes)
if test "x$LA_OLS" = "xyes"; then
	AC_DEFINE(HAVE_LA_OLS, 1, [OpenBench Logic Sniffer (OLS) support])
fi

AC_ARG_ENABLE(saleae-logic, AC_HELP_STRING([--enable-saleae-logic],
	      [enable Saleae Logic support [default=yes]]),
	      [LA_SALEAE_LOGIC="$enableval"],
	      [LA_SALEAE_LOGIC=yes])
AM_CONDITIONAL(LA_SALEAE_LOGIC, test x$LA_SALEAE_LOGIC = xyes)
if test "x$LA_SALEAE_LOGIC" = "xyes"; then
	AC_DEFINE(HAVE_LA_SALEAE_LOGIC, 1, [Saleae Logic support])
fi

AC_ARG_ENABLE(zeroplus-logic-cube,
	      AC_HELP_STRING([--enable-zeroplus-logic-cube],
	      [enable Zeroplus Logic Cube support [default=yes]]),
	      [LA_ZEROPLUS_LOGIC_CUBE="$enableval"],
	      [LA_ZEROPLUS_LOGIC_CUBE=yes])
AM_CONDITIONAL(LA_ZEROPLUS_LOGIC_CUBE, test x$LA_ZEROPLUS_LOGIC_CUBE = xyes)
if test "x$LA_ZEROPLUS_LOGIC_CUBE" = "xyes"; then
	AC_DEFINE(HAVE_LA_ZEROPLUS_LOGIC_CUBE, 1, [Zeroplus Logic Cube support])
fi

# Checks for libraries.

# This variable collects the pkg-config names of all detected libs.
# It is then used to construct the "Requires.private:" field in the
# libsigrok.pc file.
LIBSIGROK_PKGLIBS=""

# libglib-2.0 is always needed.
# Note: glib-2.0 is part of the libsigrok API (hard pkg-config requirement).
AM_PATH_GLIB_2_0([2.22.0],
	[CFLAGS="$CFLAGS $GLIB_CFLAGS"; LIBS="$LIBS $GLIB_LIBS"])

# libgthread-2.0 is always needed (e.g. for the demo hardware driver).
PKG_CHECK_MODULES([gthread], [gthread-2.0 >= 2.22.0],
	[CFLAGS="$CFLAGS $gthread_CFLAGS"; LIBS="$LIBS $gthread_LIBS";
	LIBSIGROK_PKGLIBS="$LIBSIGROK_PKGLIBS gthread-2.0"])

# libusb is only needed for some hardware drivers.
if test "x$LA_SALEAE_LOGIC" != xno \
   -o "x$LA_ASIX_SIGMA" != xno \
   -o "x$LA_CHRONOVU_LA8" != xno \
   -o "x$LA_ZEROPLUS_LOGIC_CUBE" != xno \
   -o "x$LA_FX2LAFW" != xno; then
	case "$build" in
	*freebsd*)
		# FreeBSD comes with an "integrated" libusb-1.0-style USB API.
		AC_DEFINE_UNQUOTED(HAVE_LIBUSB_1_0, [1],
			[Specifies whether we have a libusb.h header.])
		;;
	*)
		PKG_CHECK_MODULES([libusb], [libusb-1.0 >= 1.0.5],
			[CFLAGS="$CFLAGS $libusb_CFLAGS";
			LIBS="$LIBS $libusb_LIBS";
			LIBSIGROK_PKGLIBS="$LIBSIGROK_PKGLIBS libusb-1.0"])
		AC_CHECK_LIB(usb-1.0, libusb_init)
		;;
	esac
fi

# libzip is always needed.
PKG_CHECK_MODULES([libzip], [libzip >= 0.8],
	[CFLAGS="$CFLAGS $libzip_CFLAGS"; LIBS="$LIBS $libzip_LIBS";
	LIBSIGROK_PKGLIBS="$LIBSIGROK_PKGLIBS libzip"])

# zlib is only needed for some hardware drivers.
if test "x$LA_ASIX_SIGMA" != xno; then
	case "$build" in
	*freebsd*)
		# FreeBSD has zlib, but no pkg-config file for it.
		# Check manually, and do NOT add it to LIBSIGROK_PKGLIBS.
		AC_CHECK_LIB(z, uncompress)
		;;
	*)
		PKG_CHECK_MODULES([zlib], [zlib >= 1.2.3.1],
			[CFLAGS="$CFLAGS $zlib_CFLAGS"; LIBS="$LIBS $zlib_LIBS";
			LIBSIGROK_PKGLIBS="$LIBSIGROK_PKGLIBS zlib"])
		;;
	esac
fi

# libftdi is only needed for some hardware drivers.
if test "x$LA_ASIX_SIGMA" != xno \
     -o "x$LA_CHRONOVU_LA8" != xno; then
	PKG_CHECK_MODULES([libftdi], [libftdi >= 0.16],
		[CFLAGS="$CFLAGS $libftdi_CFLAGS";
		LIBS="$LIBS $libftdi_LIBS";
		LIBSIGROK_PKGLIBS="$LIBSIGROK_PKGLIBS libftdi"])
fi

# libudev is only needed for some hardware drivers.
if test "x$LA_LINK_MSO19" != xno; then
	PKG_CHECK_MODULES([libudev], [libudev >= 151],
		[CFLAGS="$CFLAGS $libudev_CFLAGS"; LIBS="$LIBS $libudev_LIBS";
		LIBSIGROK_PKGLIBS="$LIBSIGROK_PKGLIBS libudev"])
fi

# ALSA is only needed for some hardware drivers.
if test "x$LA_ALSA" != xno; then
	PKG_CHECK_MODULES([alsa], [alsa >= 1.0],
		[CFLAGS="$CFLAGS $alsa_CFLAGS"; LIBS="$LIBS $alsa_LIBS";
		LIBSIGROK_PKGLIBS="$LIBSIGROK_PKGLIBS alsa"])
fi

AC_SUBST(LIBSIGROK_PKGLIBS)

# Checks for header files.
# These are already checked: inttypes.h stdint.h stdlib.h string.h unistd.h.
AC_CHECK_HEADERS([fcntl.h sys/time.h termios.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_INLINE
AC_TYPE_INT8_T
AC_TYPE_INT16_T
AC_TYPE_INT32_T
AC_TYPE_INT64_T
AC_TYPE_UINT8_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_SIZE_T

# Checks for library functions.
AC_CHECK_FUNCS([gettimeofday memset strchr strcspn strdup strerror strncasecmp strstr strtol strtoul strtoull])

AC_SUBST(FIRMWARE_DIR, "$datadir/libsigrok/firmware")
AC_SUBST(MAKEFLAGS, '--no-print-directory')
AC_SUBST(AM_LIBTOOLFLAGS, '--silent')

LIBSIGROK_MAJOR_VERSION=libsigrok_major_version
LIBSIGROK_MINOR_VERSION=libsigrok_minor_version
LIBSIGROK_MICRO_VERSION=libsigrok_micro_version
LIBSIGROK_VERSION=libsigrok_version

AC_SUBST(LIBSIGROK_MAJOR_VERSION)
AC_SUBST(LIBSIGROK_MINOR_VERSION)
AC_SUBST(LIBSIGROK_MICRO_VERSION)
AC_SUBST(LIBSIGROK_VERSION)

AC_DEFINE(LIBSIGROK_MAJOR_VERSION, [libsigrok_major_version],
	  [Define to the libsigrok major package version])
AC_DEFINE(LIBSIGROK_MINOR_VERSION, [libsigrok_minor_version],
	  [Define to the libsigrok minor package version])
AC_DEFINE(LIBSIGROK_MICRO_VERSION, [libsigrok_micro_version],
	  [Define to the libsigrok micro package version])
AC_DEFINE(LIBSIGROK_VERSION, [libsigrok_version],
	  [Define to the libsigrok package version])

AC_CONFIG_FILES([Makefile
		 firmware/Makefile
		 hardware/Makefile
		 hardware/alsa/Makefile
		 hardware/asix-sigma/Makefile
		 hardware/chronovu-la8/Makefile
		 hardware/common/Makefile
		 hardware/demo/Makefile
		 hardware/fx2lafw/Makefile
		 hardware/link-mso19/Makefile
		 hardware/openbench-logic-sniffer/Makefile
		 hardware/saleae-logic/Makefile
		 hardware/zeroplus-logic-cube/Makefile
		 input/Makefile
		 output/Makefile
		 output/text/Makefile
		 libsigrok.pc
		 contrib/Makefile
		])

AC_OUTPUT

echo
echo "libsigrok configuration summary:"
echo
echo "  - Package version (major.minor.micro):    $LIBSIGROK_VERSION"
echo "  - Library version (current:revision:age): $LIBSIGROK_LT_VERSION"
echo "  - Prefix: $prefix"
echo
echo "Detected libraries:"
echo

# Note: This only works for libs with pkg-config integration.
for lib in "glib-2.0" "gthread-2.0" "libusb-1.0" "libzip" "zlib" "libftdi" "libudev" "alsa"; do
	if `$PKG_CONFIG --exists $lib`; then
		ver=`$PKG_CONFIG --modversion $lib`
		answer="yes ($ver)"
	else
		answer="no"
	fi
	echo "  - $lib: $answer"
done

echo
echo "Enabled hardware drivers:"
echo
echo "  - ASIX SIGMA...................... $LA_ASIX_SIGMA"
echo "  - ChronoVu LA8.................... $LA_CHRONOVU_LA8"
echo "  - Demo driver..................... $LA_DEMO"
echo "  - fx2lafw......................... $LA_FX2LAFW"
echo "  - Link MSO-19..................... $LA_LINK_MSO19"
echo "  - Openbench Logic Sniffer......... $LA_OLS"
echo "  - Saleae Logic.................... $LA_SALEAE_LOGIC"
echo "  - Zeroplus Logic Cube............. $LA_ZEROPLUS_LOGIC_CUBE"
echo

