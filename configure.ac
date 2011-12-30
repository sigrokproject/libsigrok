##
## This file is part of the sigrok project.
##
## Copyright (C) 2010 Bert Vermeulen <bert@biot.com>
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

AC_PREREQ(2.61)

m4_define([sigrok_major_version], [0])
m4_define([sigrok_minor_version], [2])
m4_define([sigrok_micro_version], [0])
m4_define([sigrok_version],
	  [sigrok_major_version.sigrok_minor_version.sigrok_micro_version])

AC_INIT([sigrok], [sigrok_version], [sigrok-devel@lists.sourceforge.net],
	[sigrok], [http://www.sigrok.org])
AC_CONFIG_HEADER([config.h])
AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE([-Wall -Werror foreign std-options])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AH_TOP([#ifndef SGIROK_CONFIG_H
#define SIGROK_CONFIG_H    /* To stop multiple inclusions. */])
AH_BOTTOM([#endif /* SIGROK_CONFIG_H */])

CFLAGS="-g -Wall -Wextra"

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

# Library version for libsigrok.
# Carefully read the libtool docs before updating these numbers!
# The algorithm for determining which number to change (and how) is nontrivial!
# http://www.gnu.org/software/libtool/manual/libtool.html#Updating-version-info
SIGROK_LT_CURRENT=0
SIGROK_LT_REVISION=0
SIGROK_LT_AGE=0
SIGROK_LT_VERSION="$SIGROK_LT_CURRENT:$SIGROK_LT_REVISION:$SIGROK_LT_AGE"
SIGROK_LT_LDFLAGS="-version-info $SIGROK_LT_VERSION"
AC_SUBST(SIGROK_LT_LDFLAGS)

# Library version for libsigrokdecode.
# Carefully read the libtool docs before updating these numbers!
# The algorithm for determining which number to change (and how) is nontrivial!
# http://www.gnu.org/software/libtool/manual/libtool.html#Updating-version-info
SIGROKDECODE_LT_CURRENT=0
SIGROKDECODE_LT_REVISION=0
SIGROKDECODE_LT_AGE=0
SIGROKDECODE_LT_VERSION="$SIGROKDECODE_LT_CURRENT:$SIGROKDECODE_LT_REVISION:$SIGROKDECODE_LT_AGE"
SIGROKDECODE_LT_LDFLAGS="-version-info $SIGROKDECODE_LT_VERSION"
AC_SUBST(SIGROKDECODE_LT_LDFLAGS)

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
   -o "x$LA_ZEROPLUS_LOGIC_CUBE" != xno; then
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
	PKG_CHECK_MODULES([zlib], [zlib >= 1.2.3.1],
		[CFLAGS="$CFLAGS $zlib_CFLAGS"; LIBS="$LIBS $zlib_LIBS";
		LIBSIGROK_PKGLIBS="$LIBSIGROK_PKGLIBS zlib"])
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

# Python support.
CPPFLAGS_PYTHON=""
LDFLAGS_PYTHON=""
case "$build" in
*linux*)
	# We know that Linux has 'python3-config'.
	CPPFLAGS_PYTHON="$(python3-config --includes)"
	LDFLAGS_PYTHON="$(python3-config --ldflags)"
	;;
*darwin*)
	# On Darwin, Macports has python3.x-config, fink has python3-config.
	# Mac OS X (Snow Leopard) ships with 'python-config' per default, but
	# that's Python 2.x, so not useful for us.
	if test -x python3-config; then
		# python3-config exists, use it.
		CPPFLAGS_PYTHON="$(python3-config --includes)"
		LDFLAGS_PYTHON="$(python3-config --ldflags)"
	else
		# No python3-config found, so try hardcoded python3.2-config.
		CPPFLAGS_PYTHON="$(python3.2-config --includes)"
		LDFLAGS_PYTHON="$(python3.2-config --ldflags)"
	fi
	;;
*mingw*)
	# We currently hardcode the paths to the Python 3.2 default install
	# location as there's no 'python-config' script on Windows, it seems.
	CPPFLAGS_PYTHON="-I/c/Python32/include"
	LDFLAGS_PYTHON="-L/c/Python32/libs -lpython32"
	;;
*)
	# Everything else is untested, we just hope 'python3-config' works.
	CPPFLAGS_PYTHON="$(python3-config --includes)"
	LDFLAGS_PYTHON="$(python3-config --ldflags)"
	;;
esac
AC_SUBST(CPPFLAGS_PYTHON)
AC_SUBST(LDFLAGS_PYTHON)

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

AC_SUBST(FIRMWARE_DIR, "$datadir/sigrok/firmware")
AC_SUBST(DECODERS_DIR, "$datadir/sigrok/decoders")
AC_SUBST(MAKEFLAGS, '--no-print-directory')
AC_SUBST(AM_LIBTOOLFLAGS, '--silent')

SIGROK_MAJOR_VERSION=sigrok_major_version
SIGROK_MINOR_VERSION=sigrok_minor_version
SIGROK_MICRO_VERSION=sigrok_micro_version
SIGROK_VERSION=sigrok_version

AC_SUBST(SIGROK_MAJOR_VERSION)
AC_SUBST(SIGROK_MINOR_VERSION)
AC_SUBST(SIGROK_MICRO_VERSION)
AC_SUBST(SIGROK_VERSION)

AC_DEFINE(SIGROK_MAJOR_VERSION, [sigrok_major_version],
	  [Define to the sigrok major version])
AC_DEFINE(SIGROK_MINOR_VERSION, [sigrok_minor_version],
	  [Define to the sigrok minor version])
AC_DEFINE(SIGROK_MICRO_VERSION, [sigrok_micro_version],
	  [Define to the sigrok micro version])
AC_DEFINE(SIGROK_VERSION, [sigrok_version],
	  [Define to the sigrok version])

AC_CONFIG_FILES([Makefile
		 contrib/Makefile
		 contrib/nsis/Makefile
		 contrib/nsis/sigrok.nsi
		 libsigrok/Makefile
		 libsigrok/sigrok.h
		 libsigrok/firmware/Makefile
		 libsigrok/hardware/Makefile
		 libsigrok/hardware/alsa/Makefile
		 libsigrok/hardware/asix-sigma/Makefile
		 libsigrok/hardware/chronovu-la8/Makefile
		 libsigrok/hardware/common/Makefile
		 libsigrok/hardware/demo/Makefile
		 libsigrok/hardware/link-mso19/Makefile
		 libsigrok/hardware/openbench-logic-sniffer/Makefile
		 libsigrok/hardware/saleae-logic/Makefile
		 libsigrok/hardware/zeroplus-logic-cube/Makefile
		 libsigrok/input/Makefile
		 libsigrok/output/Makefile
		 libsigrok/output/text/Makefile
		 libsigrok/libsigrok.pc
		 libsigrok/contrib/Makefile
		 libsigrokdecode/Makefile
		 libsigrokdecode/libsigrokdecode.pc
		 libsigrokdecode/decoders/Makefile
		])

AC_OUTPUT

echo
echo "Building drivers:"
echo
echo "ASIX SIGMA...................... " $LA_ASIX_SIGMA
echo "ChronoVu LA8.................... " $LA_CHRONOVU_LA8
echo "Link MSO-19..................... " $LA_LINK_MSO19
echo "Openbench Logic Sniffer......... " $LA_OLS
echo "Saleae Logic.................... " $LA_SALEAE_LOGIC
echo "Zeroplus Logic Cube............. " $LA_ZEROPLUS_LOGIC_CUBE
echo

