##
## This file is part of the libsigrok project.
##
## Copyright (C) 2010-2012 Bert Vermeulen <bert@biot.com>
## Copyright (C) 2012 Alexandru Gagniuc <mr.nuke.me@gmail.com>
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
m4_define([sr_package_version_major], [0])
m4_define([sr_package_version_minor], [3])
m4_define([sr_package_version_micro], [0])
m4_define([sr_package_version], [sr_package_version_major.sr_package_version_minor.sr_package_version_micro])

AC_INIT([libsigrok], [sr_package_version], [sigrok-devel@lists.sourceforge.net],
	[libsigrok], [http://www.sigrok.org])
AC_CONFIG_HEADER([config.h])
AC_CONFIG_MACRO_DIR([autostuff])
AC_CONFIG_AUX_DIR([autostuff])

# We require at least automake 1.11 (needed for 'silent rules').
AM_INIT_AUTOMAKE([1.11 -Wall -Werror subdir-objects check-news color-tests])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])
m4_ifdef([AM_PROG_AR], [AM_PROG_AR])

AH_TOP([#ifndef SR_CONFIG_H
#define SR_CONFIG_H    /* To stop multiple inclusions. */])
AH_BOTTOM([#endif /* SR_CONFIG_H */])

# Enable more compiler warnings via -Wall and -Wextra. Add -fvisibility=hidden
# and enforce use of SR_API to explicitly mark all public API functions.
COMMON_FLAGS="$CFLAGS -Wall -Wextra -fvisibility=hidden"
CFLAGS="$COMMON_FLAGS -Wmissing-prototypes"
CXXFLAGS="-std=c++11 $COMMON_FLAGS"

# Checks for programs.
AC_PROG_CC
AC_PROG_CPP
AC_PROG_CXX
AC_PROG_INSTALL
AC_PROG_LN_S

# Required for per-target flags or subdir-objects with C sources.
AM_PROG_CC_C_O

# Initialize libtool.
LT_INIT

# Initialize pkg-config.
# We require at least 0.22, as "Requires.private" behaviour changed there.
PKG_PROG_PKG_CONFIG([0.22])

# Library version for libsigrok (NOT the same as the package version).
# Carefully read the libtool docs before updating these numbers!
# The algorithm for determining which number to change (and how) is nontrivial!
# http://www.gnu.org/software/libtool/manual/libtool.html#Updating-version-info
SR_LIB_VERSION_CURRENT=2
SR_LIB_VERSION_REVISION=0
SR_LIB_VERSION_AGE=0
SR_LIB_VERSION="$SR_LIB_VERSION_CURRENT:$SR_LIB_VERSION_REVISION:$SR_LIB_VERSION_AGE"
SR_LIB_LDFLAGS="-version-info $SR_LIB_VERSION"
AC_SUBST(SR_LIB_VERSION_CURRENT)
AC_SUBST(SR_LIB_VERSION_REVISION)
AC_SUBST(SR_LIB_VERSION_AGE)
AC_SUBST(SR_LIB_VERSION)
AC_SUBST(SR_LIB_LDFLAGS)

# Hardware support '--enable' options.

AC_ARG_ENABLE(all-drivers, AC_HELP_STRING([--enable-all-drivers],
	[enable all drivers by default [default=yes]]),
	[HW_ENABLED_DEFAULT="$enableval"],
	[HW_ENABLED_DEFAULT="yes"])

# Usage: DRIVER([Device name], [driver-name])
# An optional third argument [no] disables that driver.
m4_define([driverize], [m4_tolower(m4_bpatsubst([$1], [[^0-9a-zA-Z-]+], [-]))])
m4_define([upperize], [m4_toupper(m4_bpatsubst([$1], [[^0-9a-zA-Z_]+], [_]))])
AC_DEFUN([DRIVER], [
	if test -z "$3"; then
		default=$HW_ENABLED_DEFAULT
	else
		default="$3"
	fi
	AC_ARG_ENABLE($2, AC_HELP_STRING([--enable-$2], [enable $1 support]),
		[m4_join([], [HW_], upperize([$2]))="$enableval"],
		[m4_join([], [HW_], upperize([$2]))=$default])
	dots=$(expr substr ".............................." 1 $(expr 32 - length $2))
	driver_summary="${driver_summary}  - $2${dots} ${m4_join([], [HW_], upperize([$2]))}\n"
])

DRIVER([Agilent DMM], [agilent-dmm])
DRIVER([Appa 55II], [appa-55ii])
DRIVER([ASIX SIGMA/SIGMA2], [asix-sigma])
DRIVER([Atten PPS3xxx], [atten-pps3xxx])
DRIVER([Brymen BM86x], [brymen-bm86x])
DRIVER([Brymen DMM], [brymen-dmm])
DRIVER([CEM DT-885x], [cem-dt-885x])
DRIVER([Center 3xx], [center-3xx])
DRIVER([ChronoVu LA], [chronovu-la])
DRIVER([Colead SLM], [colead-slm])
DRIVER([Conrad DIGI 35 CPU], [conrad-digi-35-cpu])
DRIVER([demo], [demo])
DRIVER([Fluke DMM], [fluke-dmm])
DRIVER([fx2lafw], [fx2lafw])
DRIVER([GMC MH 1x/2x], [gmc-mh-1x-2x])
DRIVER([Hameg HMO], [hameg-hmo])
DRIVER([Hantek DSO], [hantek-dso])
DRIVER([Ikalogic Scanalogic-2], [ikalogic-scanalogic2])
DRIVER([Ikalogic Scanaplus], [ikalogic-scanaplus])
DRIVER([Kecheng KC-330B], [kecheng-kc-330b])
DRIVER([Lascar EL-USB], [lascar-el-usb])
DRIVER([Manson HCS-3xxx], [manson-hcs-3xxx])
DRIVER([MIC 985xx], [mic-985xx])
DRIVER([Motech LPS 30x], [motech-lps-30x])
DRIVER([Norma DMM], [norma-dmm])
DRIVER([OpenBench Logic Sniffer], [openbench-logic-sniffer])
DRIVER([Rigol DS], [rigol-ds])
DRIVER([Saleae Logic16], [saleae-logic16])
DRIVER([serial DMM], [serial-dmm])
DRIVER([Sysclk LWLA], [sysclk-lwla])
DRIVER([Teleinfo], [teleinfo])
DRIVER([Testo], [testo])
DRIVER([Tondaj SL-814], [tondaj-sl-814])
DRIVER([UNI-T DMM], [uni-t-dmm])
DRIVER([UNI-T UT32x], [uni-t-ut32x])
DRIVER([Victor DMM], [victor-dmm])
DRIVER([ZEROPLUS Logic Cube], [zeroplus-logic-cube])

AC_ARG_ENABLE(libserialport,
	AC_HELP_STRING([--disable-libserialport],
	[disable libserialport support [default=detect]]))

AC_ARG_ENABLE(libftdi,
	AC_HELP_STRING([--disable-libftdi],
	[disable libftdi support [default=detect]]))

AC_ARG_ENABLE(libusb,
	AC_HELP_STRING([--disable-libusb],
	[disable libusb support [default=detect]]))

# Language binding '--enable' options

AC_ARG_ENABLE(c++,
	AC_HELP_STRING([--enable-cxx],
	[build C++ bindings [default=yes]]),
	[BINDINGS_CXX="$enableval"], [BINDINGS_CXX="yes"])

AC_ARG_ENABLE(python,
	AC_HELP_STRING([--enable-python],
	[build Python bindings [default=yes]]),
	[BINDINGS_PYTHON="$enableval"], [BINDINGS_PYTHON="yes"])

AC_ARG_ENABLE(java,
	AC_HELP_STRING([--enable-java],
	[build Java bindings [default=yes]]),
	[BINDINGS_JAVA="$enableval"], [BINDINGS_JAVA="yes"])

# Check if the C++ compiler supports the C++11 standard.

m4_ifdef([AX_CXX_COMPILE_STDCXX_11], [AX_CXX_COMPILE_STDCXX_11(,[optional])],
	[AC_MSG_NOTICE([Missing macro m4_toupper(aX_CXX_COMPILE_STDCXX_11), no C++11 check])])

if test "x$HAVE_CXX11" != "x1"; then
	BINDINGS_CXX="no"
fi

# The C++ bindings use gccxml to parse libsigrok symbols.
AC_CHECK_PROG([HAVE_GCCXML], [gccxml], [yes])
if test "x$HAVE_GCCXML" != "xyes"; then
	BINDINGS_CXX="no"
fi

# Python is needed for the C++ (and Python) bindings.
PKG_CHECK_MODULES([python], [python3 >= 2.7],
        [CXXFLAGS="$CXXFLAGS $python_CFLAGS";
        CXXLIBS="$CXXLIBS $python_LIBS"],
		[BINDINGS_CXX="no"])

# The C++ bindings need the pygccxml Python module.
m4_ifdef([AX_PYTHON_MODULE], [AX_PYTHON_MODULE([pygccxml])],
	# We'll let it go through even if the macro wasn't found, the python
	# module may still be there.
	[HAVE_PYTHON_PYGCCXML="yes";
		AC_MSG_NOTICE([Missing macro m4_toupper(aX_PYTHON_MODULE), no pygccxml check])]
)
if test "x$HAVE_PYMOD_PYGCCXML" != "xyes"; then
	BINDINGS_CXX="no"
fi

# Python and Java bindings depend on C++ bindings.

if test "x$BINDINGS_CXX" != "xyes"; then
	BINDINGS_PYTHON="no"
	BINDINGS_JAVA="no"
fi

# Checks for libraries.

case "$host" in
*mingw*)
	# We need to link against the Winsock2 library for SCPI over TCP.
	LIBS="$LIBS -lws2_32";;
esac

# This variable collects the pkg-config names of all detected libs.
# It is then used to construct the "Requires.private:" field in the
# libsigrok.pc file.
SR_PKGLIBS=""

# libm (the standard math library) is always needed.
AC_SEARCH_LIBS([pow], [m])

# RPC is only needed for VXI support.
AC_MSG_CHECKING([for RPC support])
AC_LINK_IFELSE([AC_LANG_PROGRAM([#include <rpc/rpc.h>],
				[CLIENT *rpc_test(void)],
				[{ return clnt_create("", 0, 0, ""); }])],
	       [AC_MSG_RESULT([yes]); have_rpc=1],
	       [AC_MSG_RESULT([no]);  have_rpc=0])
# Define HAVE_RPC in config.h if we found RPC support.
AC_DEFINE_UNQUOTED(HAVE_RPC, [$have_rpc], [Specifies whether we have RPC support.])
# VXI support is only compiled if RPC support was found.
AM_CONDITIONAL(NEED_RPC, test "x$have_rpc" != "x0")

# libglib-2.0 is always needed. Abort if it's not found.
# Note: glib-2.0 is part of the libsigrok API (hard pkg-config requirement).
# We require at least 2.32.0 due to e.g. g_variant_new_fixed_array().
AM_PATH_GLIB_2_0([2.32.0],
	[LIB_CFLAGS="$LIB_CFLAGS $GLIB_CFLAGS"; LIBS="$LIBS $GLIB_LIBS"])

# libzip is always needed. Abort if it's not found.
PKG_CHECK_MODULES([libzip], [libzip >= 0.10],
	[LIB_CFLAGS="$LIB_CFLAGS $libzip_CFLAGS"; LIBS="$LIBS $libzip_LIBS";
	SR_PKGLIBS="$SR_PKGLIBS libzip"])

# libserialport is only needed for some hardware drivers. Disable the
# respective drivers if it is not found.
if test "x$enable_libserialport" != "xno"; then
	PKG_CHECK_MODULES([libserialport], [libserialport >= 0.1.0],
		[have_libserialport="yes"; LIB_CFLAGS="$LIB_CFLAGS $libserialport_CFLAGS";
		LIBS="$LIBS $libserialport_LIBS";
		SR_PKGLIBS="$SR_PKGLIBS libserialport"],
		[have_libserialport="no"])
	if test "x$enable_libserialport$have_libserialport" = "xyesno"; then
		AC_MSG_ERROR([libserialport support requested, but it was not found])
	fi
else
	have_libserialport="no"
fi

# Define HAVE_LIBSERIALPORT in config.h if we found libserialport.
if test "x$have_libserialport" = "xno"; then
	HW_AGILENT_DMM="no"; HW_APPA_55II="no";
	HW_ATTEN_PPS3XXX="no"; HW_BRYMEN_DMM="no"; HW_CEM_DT_885X="no";
	HW_CENTER_3XX="no"; HW_COLEAD_SLM="no"; HW_CONRAD_DIGI_35_CPU="no";
	HW_FLUKE_DMM="no"; HW_GMC_MH_1X_2X="no"; HW_HAMEG_HMO="no";
	HW_MANSON_HCS_3XXX="no"; HW_MIC_985XX="no"; HW_MOTECH_LPS_30X="no";
	HW_NORMA_DMM="no"; HW_OPENBENCH_LOGIC_SNIFFER="no"; HW_SERIAL_DMM="no";
	HW_TELEINFO="no"; HW_TONDAJ_SL_814="no";
else
	AC_DEFINE_UNQUOTED(HAVE_LIBSERIALPORT, [1],
		[Specifies whether we have libserialport.])
fi

# Serial port helper code is only compiled in if libserialport was found.
AM_CONDITIONAL(NEED_SERIAL, test "x$have_libserialport" != xno)

PKG_CHECK_MODULES([librevisa], [librevisa >= 0.0.20130812],
	[have_librevisa="yes"; LIB_CFLAGS="$LIB_CFLAGS $librevisa_CFLAGS";
	LIBS="$LIBS $librevisa_LIBS";
	SR_PKGLIBS="$SR_PKGLIBS librevisa"],
	[have_librevisa="no"])

# VISA SCPI backend is only compiled in if librevisa was found.
AM_CONDITIONAL(NEED_VISA, test "x$have_librevisa" != xno)

# Define HAVE_LIBREVISA in config.h if we found librevisa.
if test "x$have_librevisa" != "xno"; then
	AC_DEFINE_UNQUOTED(HAVE_LIBREVISA, [1],
		[Specifies whether we have librevisa.])
fi

# libusb-1.0 is only needed for some hardware drivers. Disable the respective
# drivers if it is not found.
if test "x$enable_libusb" != "xno"; then
	case "$host" in
	*freebsd*)
		# FreeBSD comes with an "integrated" libusb-1.0-style USB API.
		# This means libusb-1.0 is always available, no need to check for it,
		# and no need to (potentially) disable any drivers if it's not found.
		have_libusb1_0="yes"
		;;
	*)
		PKG_CHECK_MODULES([libusb], [libusb-1.0 >= 1.0.16],
			[have_libusb1_0="yes"; LIB_CFLAGS="$LIB_CFLAGS $libusb_CFLAGS";
			LIBS="$LIBS $libusb_LIBS";
			SR_PKGLIBS="$SR_PKGLIBS libusb-1.0"],
			[have_libusb1_0="no"])
		if test "x$enable_libusb$have_libusb1_0" = "xyesno"; then
			AC_MSG_ERROR([libusb support requested, but it was not found])
		fi
		;;
	esac
else
	have_libusb1_0="no"
fi

# Define HAVE_LIBUSB_1_0 in config.h if we found libusb-1.0.
if test "x$have_libusb1_0" = "xno"; then
	HW_BRYMEN_BM86X="no"; HW_FX2LAFW="no";
	HW_HANTEK_DSO="no"; HW_IKALOGIC_SCANALOGIC2="no";
	HW_KECHENG_KC_330B="no"; HW_LASCAR_EL_USB="no";
	HW_SYSCLK_LWLA="no"; HW_TESTO="no"; HW_UNI_T_DMM="no";
	HW_UNI_T_UT32X="no"; HW_VICTOR_DMM="no";
	HW_ZEROPLUS_LOGIC_CUBE="no"; HW_SALEAE_LOGIC16="no";
else
	AC_DEFINE_UNQUOTED(HAVE_LIBUSB_1_0, [1],
		[Specifies whether we have a libusb.h header.])
fi

# USB + FX2 firmware helper code is only compiled in if libusb-1.0 was found.
AM_CONDITIONAL(NEED_USB, test "x$have_libusb1_0" != xno)

# libftdi is only needed for some hardware drivers. Disable them if not found.
if test "x$enable_libftdi" != "xno"; then
	PKG_CHECK_MODULES([libftdi], [libftdi >= 0.16],
		[have_libftdi="yes"; LIB_CFLAGS="$LIB_CFLAGS $libftdi_CFLAGS";
		LIBS="$LIBS $libftdi_LIBS";
		SR_PKGLIBS="$SR_PKGLIBS libftdi"],
		[PKG_CHECK_MODULES([libftdi1], [libftdi1 >= 1.0],
			[LIB_CFLAGS="$LIB_CFLAGS $libftdi1_CFLAGS";
			LIBS="$LIBS $libftdi1_LIBS";
			SR_PKGLIBS="$SR_PKGLIBS libftdi1"],
			[have_libftdi="no"])])
	if test "x$enable_libftdi$have_libftdi" = "xyesno"; then
		AC_MSG_ERROR([libftdi support requested, but it was not found])
	fi
else
	have_libftdi="no"
fi

if test "x$have_libftdi" = "xno"; then
	HW_ASIX_SIGMA="no"; HW_CHRONOVU_LA="no"; HW_IKALOGIC_SCANAPLUS="no";
fi

# glibmm-2.4 is needed for the C++ bindings.
PKG_CHECK_MODULES([glibmm], [glibmm-2.4 >= 2.32.0],
	[CXXFLAGS="$CXXFLAGS $glibmm_CFLAGS";
	CXXLIBS="$CXXLIBS $glibmm_LIBS"], [BINDINGS_CXX="no"])

# PyGObject is needed for the Python bindings.
PKG_CHECK_MODULES([pygobject], [pygobject-3.0],
        [CXXFLAGS="$CXXFLAGS $pygobject_CFLAGS";
        CXXLIBS="$CXXLIBS $pygobject_LIBS"], [BINDINGS_PYTHON="no"])

# The Check unit testing framework is optional. Disable if not found.
PKG_CHECK_MODULES([check], [check >= 0.9.4],
	[have_check="yes"], [have_check="no"])
AM_CONDITIONAL(HAVE_CHECK, test x"$have_check" = "xyes")

# The OLS driver uses serial port file descriptors directly, and therefore
# will not currently work on Windows.
case "$host" in
*mingw*)
	HW_OPENBENCH_LOGIC_SNIFFER="no"
	;;
esac

AC_SUBST(SR_PKGLIBS)

CFLAGS="$CFLAGS -I./include/libsigrok $LIB_CFLAGS"
CXXFLAGS="$CXXFLAGS -I./include -I./bindings/cxx/include $LIB_CFLAGS"

# Find Java compiler and JNI includes for Java bindings.

AC_CHECK_PROG([HAVE_JAVAC], [javac], [yes], [no])

if test "x$HAVE_JAVAC" = "xyes"; then
	AX_PROG_JAVAC
	AX_JNI_INCLUDE_DIR
	for JNI_INCLUDE_DIR in $JNI_INCLUDE_DIRS; do
		CXXFLAGS="$CXXFLAGS -I$JNI_INCLUDE_DIR"
	done
else
	BINDINGS_JAVA="no"
fi

# Now set AM_CONDITIONALs and AC_DEFINEs for the enabled/disabled drivers.

AM_CONDITIONAL(HW_AGILENT_DMM, test x$HW_AGILENT_DMM = xyes)
if test "x$HW_AGILENT_DMM" = "xyes"; then
	AC_DEFINE(HAVE_HW_AGILENT_DMM, 1, [Agilent DMM support])
fi

AM_CONDITIONAL(HW_APPA_55II, test x$HW_APPA_55II = xyes)
if test "x$HW_APPA_55II" = "xyes"; then
	AC_DEFINE(HAVE_HW_APPA_55II, 1, [APPA 55II support])
fi

AM_CONDITIONAL(HW_ASIX_SIGMA, test x$HW_ASIX_SIGMA = xyes)
if test "x$HW_ASIX_SIGMA" = "xyes"; then
	AC_DEFINE(HAVE_HW_ASIX_SIGMA, 1, [ASIX SIGMA/SIGMA2 support])
fi

AM_CONDITIONAL(HW_ATTEN_PPS3XXX, test x$HW_ATTEN_PPS3XXX = xyes)
if test "x$HW_ATTEN_PPS3XXX" = "xyes"; then
	AC_DEFINE(HAVE_HW_ATTEN_PPS3XXX, 1, [Atten PPS3xxx support])
fi

AM_CONDITIONAL(HW_BRYMEN_BM86X, test x$HW_BRYMEN_BM86X = xyes)
if test "x$HW_BRYMEN_BM86X" = "xyes"; then
	AC_DEFINE(HAVE_HW_BRYMEN_BM86X, 1, [Brymen BM86X support])
fi

AM_CONDITIONAL(HW_BRYMEN_DMM, test x$HW_BRYMEN_DMM = xyes)
if test "x$HW_BRYMEN_DMM" = "xyes"; then
	AC_DEFINE(HAVE_HW_BRYMEN_DMM, 1, [Brymen DMM support])
fi

AM_CONDITIONAL(HW_CEM_DT_885X, test x$HW_CEM_DT_885X = xyes)
if test "x$HW_CEM_DT_885X" = "xyes"; then
	AC_DEFINE(HAVE_HW_CEM_DT_885X, 1, [CEM DT-885x support])
fi

AM_CONDITIONAL(HW_CENTER_3XX, test x$HW_CENTER_3XX = xyes)
if test "x$HW_CENTER_3XX" = "xyes"; then
	AC_DEFINE(HAVE_HW_CENTER_3XX, 1, [Center 3xx support])
fi

AM_CONDITIONAL(HW_CHRONOVU_LA, test x$HW_CHRONOVU_LA = xyes)
if test "x$HW_CHRONOVU_LA" = "xyes"; then
	AC_DEFINE(HAVE_HW_CHRONOVU_LA, 1, [ChronoVu LA support])
fi

AM_CONDITIONAL(HW_COLEAD_SLM, test x$HW_COLEAD_SLM = xyes)
if test "x$HW_COLEAD_SLM" = "xyes"; then
	AC_DEFINE(HAVE_HW_COLEAD_SLM, 1, [Colead SLM support])
fi

AM_CONDITIONAL(HW_CONRAD_DIGI_35_CPU, test x$HW_CONRAD_DIGI_35_CPU = xyes)
if test "x$HW_CONRAD_DIGI_35_CPU" = "xyes"; then
	AC_DEFINE(HAVE_HW_CONRAD_DIGI_35_CPU, 1, [Conrad DIGI 35 CPU support])
fi

AM_CONDITIONAL(HW_DEMO, test x$HW_DEMO = xyes)
if test "x$HW_DEMO" = "xyes"; then
	AC_DEFINE(HAVE_HW_DEMO, 1, [Demo driver support])
fi

AM_CONDITIONAL(HW_FLUKE_DMM, test x$HW_FLUKE_DMM = xyes)
if test "x$HW_FLUKE_DMM" = "xyes"; then
	AC_DEFINE(HAVE_HW_FLUKE_DMM, 1, [Fluke DMM support])
fi

AM_CONDITIONAL(HW_FX2LAFW, test x$HW_FX2LAFW = xyes)
if test "x$HW_FX2LAFW" = "xyes"; then
	AC_DEFINE(HAVE_HW_FX2LAFW, 1, [fx2lafw support])
fi

AM_CONDITIONAL(HW_GMC_MH_1X_2X, test x$HW_GMC_MH_1X_2X = xyes)
if test "x$HW_GMC_MH_1X_2X" = "xyes"; then
	AC_DEFINE(HAVE_HW_GMC_MH_1X_2X, 1, [gmc-mh-1x-2x support])
fi

AM_CONDITIONAL(HW_HANTEK_DSO, test x$HW_HANTEK_DSO = xyes)
if test "x$HW_HANTEK_DSO" = "xyes"; then
	AC_DEFINE(HAVE_HW_HANTEK_DSO, 1, [Hantek DSO support])
fi

AM_CONDITIONAL(HW_HAMEG_HMO, test x$HW_HAMEG_HMO = xyes)
if test "x$HW_HAMEG_HMO" = "xyes"; then
	AC_DEFINE(HAVE_HW_HAMEG_HMO, 1, [Hameg HMO support])
fi

AM_CONDITIONAL(HW_IKALOGIC_SCANALOGIC2, test x$HW_IKALOGIC_SCANALOGIC2 = xyes)
if test "x$HW_IKALOGIC_SCANALOGIC2" = "xyes"; then
	AC_DEFINE(HAVE_HW_IKALOGIC_SCANALOGIC2, 1, [IKALOGIC Scanalogic-2 support])
fi

AM_CONDITIONAL(HW_IKALOGIC_SCANAPLUS, test x$HW_IKALOGIC_SCANAPLUS = xyes)
if test "x$HW_IKALOGIC_SCANAPLUS" = "xyes"; then
	AC_DEFINE(HAVE_HW_IKALOGIC_SCANAPLUS, 1, [IKALOGIC ScanaPLUS support])
fi

AM_CONDITIONAL(HW_KECHENG_KC_330B, test x$HW_KECHENG_KC_330B = xyes)
if test "x$HW_KECHENG_KC_330B" = "xyes"; then
	AC_DEFINE(HAVE_HW_KECHENG_KC_330B, 1, [Kecheng KC-330B support])
fi

AM_CONDITIONAL(HW_LASCAR_EL_USB, test x$HW_LASCAR_EL_USB = xyes)
if test "x$HW_LASCAR_EL_USB" = "xyes"; then
	AC_DEFINE(HAVE_HW_LASCAR_EL_USB, 1, [Lascar EL-USB support])
fi

AM_CONDITIONAL(HW_MANSON_HCS_3XXX, test x$HW_MANSON_HCS_3XXX = xyes)
if test "x$HW_MANSON_HCS_3XXX" = "xyes"; then
	AC_DEFINE(HAVE_HW_MANSON_HCS_3XXX, 1, [Manson HCS-3xxx support])
fi

AM_CONDITIONAL(HW_MIC_985XX, test x$HW_MIC_985XX = xyes)
if test "x$HW_MIC_985XX" = "xyes"; then
	AC_DEFINE(HAVE_HW_MIC_985XX, 1, [MIC 985xx support])
fi

AM_CONDITIONAL(HW_MOTECH_LPS_30X, test x$HW_MOTECH_LPS_30X = xyes)
if test "x$HW_MOTECH_LPS_30X" = "xyes"; then
	AC_DEFINE(HAVE_HW_MOTECH_LPS_30X, 1, [motech-lps-30x support])
fi

AM_CONDITIONAL(HW_NORMA_DMM, test x$HW_NORMA_DMM = xyes)
if test "x$HW_NORMA_DMM" = "xyes"; then
	AC_DEFINE(HAVE_HW_NORMA_DMM, 1, [Norma DMM support])
fi

AM_CONDITIONAL(HW_OPENBENCH_LOGIC_SNIFFER, test x$HW_OPENBENCH_LOGIC_SNIFFER = xyes)
if test "x$HW_OPENBENCH_LOGIC_SNIFFER" = "xyes"; then
	AC_DEFINE(HAVE_HW_OPENBENCH_LOGIC_SNIFFER, 1, [OpenBench Logic Sniffer (OLS) support])
fi

AM_CONDITIONAL(HW_RIGOL_DS, test x$HW_RIGOL_DS = xyes)
if test "x$HW_RIGOL_DS" = "xyes"; then
	AC_DEFINE(HAVE_HW_RIGOL_DS, 1, [Rigol DS support])
fi

AM_CONDITIONAL(HW_SALEAE_LOGIC16, test x$HW_SALEAE_LOGIC16 = xyes)
if test "x$HW_SALEAE_LOGIC16" = "xyes"; then
	AC_DEFINE(HAVE_HW_SALEAE_LOGIC16, 1, [Saleae Logic16 support])
fi

AM_CONDITIONAL(HW_SERIAL_DMM, test x$HW_SERIAL_DMM = xyes)
if test "x$HW_SERIAL_DMM" = "xyes"; then
	AC_DEFINE(HAVE_HW_SERIAL_DMM, 1, [Serial DMM support])
fi

AM_CONDITIONAL(HW_SYSCLK_LWLA, test x$HW_SYSCLK_LWLA = xyes)
if test "x$HW_SYSCLK_LWLA" = "xyes"; then
	AC_DEFINE(HAVE_HW_SYSCLK_LWLA, 1, [Sysclk LWLA support])
fi

AM_CONDITIONAL(HW_TELEINFO, test x$HW_TELEINFO = xyes)
if test "x$HW_TELEINFO" = "xyes"; then
	AC_DEFINE(HAVE_HW_TELEINFO, 1, [Teleinfo support])
fi

AM_CONDITIONAL(HW_TESTO, test x$HW_TESTO = xyes)
if test "x$HW_TESTO" = "xyes"; then
	AC_DEFINE(HAVE_HW_TESTO, 1, [Testo support])
fi

AM_CONDITIONAL(HW_TONDAJ_SL_814, test x$HW_TONDAJ_SL_814 = xyes)
if test "x$HW_TONDAJ_SL_814" = "xyes"; then
	AC_DEFINE(HAVE_HW_TONDAJ_SL_814, 1, [Tondaj SL-814 support])
fi

AM_CONDITIONAL(HW_UNI_T_DMM, test x$HW_UNI_T_DMM = xyes)
if test "x$HW_UNI_T_DMM" = "xyes"; then
	AC_DEFINE(HAVE_HW_UNI_T_DMM, 1, [UNI-T DMM support])
fi

AM_CONDITIONAL(HW_UNI_T_UT32X, test x$HW_UNI_T_UT32X = xyes)
if test "x$HW_UNI_T_UT32X" = "xyes"; then
	AC_DEFINE(HAVE_HW_UNI_T_UT32X, 1, [UNI-T UT32x support])
fi

AM_CONDITIONAL(HW_VICTOR_DMM, test x$HW_VICTOR_DMM = xyes)
if test "x$HW_VICTOR_DMM" = "xyes"; then
	AC_DEFINE(HAVE_HW_VICTOR_DMM, 1, [Victor DMM support])
fi

AM_CONDITIONAL(HW_ZEROPLUS_LOGIC_CUBE, test x$HW_ZEROPLUS_LOGIC_CUBE = xyes)
if test "x$HW_ZEROPLUS_LOGIC_CUBE" = "xyes"; then
	AC_DEFINE(HAVE_HW_ZEROPLUS_LOGIC_CUBE, 1, [ZEROPLUS Logic Cube support])
fi

AM_CONDITIONAL(BINDINGS_CXX, test x$BINDINGS_CXX = xyes)

AM_CONDITIONAL(BINDINGS_PYTHON, test x$BINDINGS_PYTHON = xyes)

AM_CONDITIONAL(BINDINGS_JAVA, test x$BINDINGS_JAVA = xyes)

# Checks for header files.
# These are already checked: inttypes.h stdint.h stdlib.h string.h unistd.h.

# Checks for typedefs, structures, and compiler characteristics.
AC_C_BIGENDIAN

AC_SUBST(FIRMWARE_DIR, "$datadir/sigrok-firmware")
AC_SUBST(MAKEFLAGS, '--no-print-directory')
AC_SUBST(AM_LIBTOOLFLAGS, '--silent')

SR_PACKAGE_VERSION_MAJOR=sr_package_version_major
SR_PACKAGE_VERSION_MINOR=sr_package_version_minor
SR_PACKAGE_VERSION_MICRO=sr_package_version_micro
SR_PACKAGE_VERSION=sr_package_version

AC_SUBST(SR_PACKAGE_VERSION_MAJOR)
AC_SUBST(SR_PACKAGE_VERSION_MINOR)
AC_SUBST(SR_PACKAGE_VERSION_MICRO)
AC_SUBST(SR_PACKAGE_VERSION)

AC_CONFIG_FILES([Makefile include/libsigrok/version.h libsigrok.pc
	bindings/cxx/libsigrokxx.pc])

AC_OUTPUT

echo
echo "libsigrok configuration summary:"
echo
echo "  - Package version (major.minor.micro):    $SR_PACKAGE_VERSION"
echo "  - Library version (current:revision:age): $SR_LIB_VERSION"
echo "  - Prefix: $prefix"
echo "  - Building on: $build"
echo "  - Building for: $host"
echo
echo "Detected libraries:"
echo

# Note: This only works for libs with pkg-config integration.
for lib in "glib-2.0 >= 2.32.0" "libzip >= 0.10" "libserialport >= 0.1.0" "librevisa >= 0.0.20130812" "libusb-1.0 >= 1.0.16" "libftdi >= 0.16" "libftdi1 >= 1.0" "check >= 0.9.4"; do
	optional="OPTIONAL"
	if test "x$lib" = "xglib-2.0 >= 2.32.0"; then optional="REQUIRED"; fi
	if test "x$lib" = "xlibzip >= 0.10"; then optional="REQUIRED"; fi
	if `$PKG_CONFIG --exists $lib`; then
		ver=`$PKG_CONFIG --modversion $lib`
		answer="yes ($ver)"
	else
		answer="no"
	fi
	echo "  - ($optional) $lib: $answer"
done

echo -e "\nEnabled hardware drivers:\n${driver_summary}"

echo -e "\nEnabled language bindings:\n"
echo "  - C++............................. $BINDINGS_CXX"
echo "  - Python.......................... $BINDINGS_PYTHON"
echo "  - Java............................ $BINDINGS_JAVA"
echo
