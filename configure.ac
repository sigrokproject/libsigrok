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
m4_define([sr_package_version_minor], [4])
m4_define([sr_package_version_micro], [0])
m4_define([sr_package_version_suffix], [-git])
m4_define([sr_package_version], [sr_package_version_major.sr_package_version_minor.sr_package_version_micro[]sr_package_version_suffix])

AC_INIT([libsigrok], [sr_package_version], [sigrok-devel@lists.sourceforge.net],
	[libsigrok], [http://www.sigrok.org])
AC_CONFIG_HEADERS([config.h include/libsigrok/version.h])
AC_CONFIG_MACRO_DIR([autostuff])
AC_CONFIG_AUX_DIR([autostuff])

# We require at least automake 1.11 (needed for 'silent rules').
AM_INIT_AUTOMAKE([1.11 -Wall -Werror no-define subdir-objects check-news color-tests])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])
m4_ifdef([AM_PROG_AR], [AM_PROG_AR])

AH_TOP([#ifndef SR_CONFIG_H
#define SR_CONFIG_H    /* To stop multiple inclusions. */])
AH_BOTTOM([#endif /* SR_CONFIG_H */])

# Enable more compiler warnings via -Wall and -Wextra. Add -fvisibility=hidden
# and enforce use of SR_API to explicitly mark all public API functions.
COMMON_FLAGS="$CFLAGS -Wall -Wextra -fvisibility=hidden"
CFLAGS="$COMMON_FLAGS -std=c11 -D_POSIX_C_SOURCE=200112L -Wmissing-prototypes"
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

AC_SUBST([SR_PACKAGE_VERSION], [sr_package_version])

AC_DEFINE([SR_PACKAGE_VERSION_MAJOR], [sr_package_version_major], [.])
AC_DEFINE([SR_PACKAGE_VERSION_MINOR], [sr_package_version_minor], [.])
AC_DEFINE([SR_PACKAGE_VERSION_MICRO], [sr_package_version_micro], [.])
AC_DEFINE_UNQUOTED([SR_PACKAGE_VERSION_STRING], ["$SR_PACKAGE_VERSION"], [.])

# Library version for libsigrok (NOT the same as the package version).
# Carefully read the libtool docs before updating these numbers!
# The algorithm for determining which number to change (and how) is nontrivial!
# http://www.gnu.org/software/libtool/manual/libtool.html#Updating-version-info
SR_LIB_VERSION_CURRENT=2
SR_LIB_VERSION_REVISION=0
SR_LIB_VERSION_AGE=0

AC_SUBST([SR_LIB_VERSION], ["$SR_LIB_VERSION_CURRENT:$SR_LIB_VERSION_REVISION:$SR_LIB_VERSION_AGE"])
AC_SUBST([SR_LIB_LDFLAGS], ["-version-info $SR_LIB_VERSION"])

AC_DEFINE_UNQUOTED([SR_LIB_VERSION_CURRENT], [$SR_LIB_VERSION_CURRENT], [.])
AC_DEFINE_UNQUOTED([SR_LIB_VERSION_REVISION], [$SR_LIB_VERSION_REVISION], [.])
AC_DEFINE_UNQUOTED([SR_LIB_VERSION_AGE], [$SR_LIB_VERSION_AGE], [.])
AC_DEFINE_UNQUOTED([SR_LIB_VERSION_STRING], ["$SR_LIB_VERSION"], [.])

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
	dots=$(echo -n ".............................." | cut -c 1-$(expr 32 - $(echo -n $2|wc -c)))
	driver_summary="${driver_summary}  - $2${dots} \${m4_join([], [HW_], upperize([$2]))}@"
])

DRIVER([Agilent DMM], [agilent-dmm])
DRIVER([Appa 55II], [appa-55ii])
DRIVER([ASIX SIGMA/SIGMA2], [asix-sigma])
DRIVER([Atten PPS3xxx], [atten-pps3xxx])
DRIVER([BayLibre ACME], [baylibre-acme])
DRIVER([BeagleLogic], [beaglelogic])
DRIVER([Brymen BM86x], [brymen-bm86x])
DRIVER([Brymen DMM], [brymen-dmm])
DRIVER([CEM DT-885x], [cem-dt-885x])
DRIVER([Center 3xx], [center-3xx])
DRIVER([ChronoVu LA], [chronovu-la])
DRIVER([Colead SLM], [colead-slm])
DRIVER([Conrad DIGI 35 CPU], [conrad-digi-35-cpu])
DRIVER([DER EE DE-5000], [deree-de5000])
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
DRIVER([maynuo-m97], [maynuo-m97])
DRIVER([MIC 985xx], [mic-985xx])
DRIVER([Motech LPS 30x], [motech-lps-30x])
DRIVER([Norma DMM], [norma-dmm])
DRIVER([OpenBench Logic Sniffer], [openbench-logic-sniffer])
DRIVER([Pipistrello-OLS], [pipistrello-ols])
DRIVER([Rigol DS], [rigol-ds])
DRIVER([Saleae Logic16], [saleae-logic16])
DRIVER([SCPI PPS], [scpi-pps])
DRIVER([serial DMM], [serial-dmm])
DRIVER([Sysclk LWLA], [sysclk-lwla])
DRIVER([Teleinfo], [teleinfo])
DRIVER([Testo], [testo])
DRIVER([Tondaj SL-814], [tondaj-sl-814])
DRIVER([UNI-T DMM], [uni-t-dmm])
DRIVER([UNI-T UT32x], [uni-t-ut32x])
DRIVER([Victor DMM], [victor-dmm])
DRIVER([Yokogawa DL/DLM], [yokogawa-dlm])
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

# Language binding '--enable' options:

AC_ARG_ENABLE(bindings,
	AC_HELP_STRING([--enable-bindings],
	[build language bindings [default=yes]]),
	[BINDINGS_ALL="$enableval"], [BINDINGS_ALL="yes"])

AC_ARG_ENABLE(cxx,
	AC_HELP_STRING([--enable-cxx],
	[build C++ bindings [default=yes]]),
	[BINDINGS_CXX="$enableval"], [BINDINGS_CXX=$BINDINGS_ALL])

AC_ARG_ENABLE(python,
	AC_HELP_STRING([--enable-python],
	[build Python bindings [default=yes]]),
	[BINDINGS_PYTHON="$enableval"], [BINDINGS_PYTHON=$BINDINGS_ALL])

AC_ARG_ENABLE(java,
	AC_HELP_STRING([--enable-java],
	[build Java bindings [default=yes]]),
	[BINDINGS_JAVA="$enableval"], [BINDINGS_JAVA=$BINDINGS_ALL])

AC_ARG_WITH(jni-include-path,
	AC_HELP_STRING([--with-jni-include-path=path],
	[specify include path for JNI [default=autodetect]]),
	[JNI_INCLUDE_DIRS="$withval"], [JNI_INCLUDE_DIRS=auto])

# Check if the C++ compiler supports the C++11 standard.

m4_ifdef([AX_CXX_COMPILE_STDCXX_11], [AX_CXX_COMPILE_STDCXX_11(,[optional])],
	[AC_MSG_NOTICE([Missing macro m4_toupper(aX_CXX_COMPILE_STDCXX_11), no C++11 check])])

if test "x$HAVE_CXX11" != "x1"; then
	BINDINGS_CXX="no"; cxx_msg="C++11 compiler required"
fi

# The C++ bindings use Doxygen to parse libsigrok symbols.
AC_CHECK_PROG([HAVE_DOXYGEN], [doxygen], [yes])
if test "x$HAVE_DOXYGEN" != "xyes"; then
	BINDINGS_CXX="no"; cxx_msg="Doxygen required"
fi

# Python is needed for the C++ bindings.
AM_PATH_PYTHON([2.7], [HAVE_PYTHON="yes"],
	[HAVE_PYTHON="no"; BINDINGS_CXX="no"; cxx_msg="Python required"])

# The Python bindings need Python development files.
PKG_CHECK_MODULES([python], [python >= 2.7],
	[CXXFLAGS="$CXXFLAGS $python_CFLAGS";
	CXXLIBS="$CXXLIBS $python_LIBS"],
[PKG_CHECK_MODULES([python27], [python-2.7 >= 2.7],
	[CXXFLAGS="$CXXFLAGS $python27_CFLAGS";
	CXXLIBS="$CXXLIBS $python27_LIBS"],
		[BINDINGS_PYTHON="no"; python_msg="Python headers required"])])

# The Python bindings need the setuptools Python module.
m4_ifdef([AX_PYTHON_MODULE], [AX_PYTHON_MODULE([setuptools])],
	# We'll let it go through even if the macro wasn't found, the Python
	# module may still be there.
	[HAVE_PYTHON_SETUPTOOLS="yes";
		AC_MSG_NOTICE([Missing macro m4_toupper(aX_PYTHON_MODULE), no setuptools check])]
)
if test "x$HAVE_PYMOD_SETUPTOOLS" != "xyes"; then
	BINDINGS_PYTHON="no"; python_msg="Python setuptools module required"
fi

# The Python and Java bindings use SWIG to generate code.
AC_CHECK_PROG([HAVE_SWIG], [swig], [yes])
if test "x$HAVE_SWIG" != "xyes"; then
	BINDINGS_PYTHON="no"; python_msg="SWIG required"
	BINDINGS_JAVA="no"; java_msg="SWIG required"
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

AC_DEFINE_UNQUOTED(HOST, ["$host"], [.])

# libglib-2.0 is always needed. Abort if it's not found.
# Note: glib-2.0 is part of the libsigrok API (hard pkg-config requirement).
# We require at least 2.32.0 due to e.g. g_variant_new_fixed_array().
AM_PATH_GLIB_2_0([2.32.0],
	[LIB_CFLAGS="$LIB_CFLAGS $GLIB_CFLAGS"; LIBS="$LIBS $GLIB_LIBS"])

# libzip is always needed. Abort if it's not found.
PKG_CHECK_MODULES([libzip], [libzip >= 0.10],
	[LIB_CFLAGS="$LIB_CFLAGS $libzip_CFLAGS"; LIBS="$LIBS $libzip_LIBS";
	SR_PKGLIBS="$SR_PKGLIBS libzip"])
libzip_ver=`$PKG_CONFIG --modversion libzip`
AC_DEFINE_UNQUOTED(HAVE_LIBZIP_VER, ["$libzip_ver"], [.])

# libserialport is only needed for some hardware drivers. Disable the
# respective drivers if it is not found.
if test "x$enable_libserialport" != "xno"; then
	PKG_CHECK_MODULES([libserialport], [libserialport >= 0.1.1],
		[have_libserialport="yes"; LIB_CFLAGS="$LIB_CFLAGS $libserialport_CFLAGS";
		LIBS="$LIBS $libserialport_LIBS";
		SR_PKGLIBS="$SR_PKGLIBS libserialport"],
		[have_libserialport="no"])
	if test "x$enable_libserialport$have_libserialport" = "xyesno"; then
		AC_MSG_ERROR([libserialport support requested, but it was not found.])
	fi
else
	have_libserialport="no"
fi
if test "x$have_libserialport" = "xno"; then
	HW_AGILENT_DMM="no"; HW_APPA_55II="no";
	HW_ATTEN_PPS3XXX="no"; HW_BRYMEN_DMM="no"; HW_CEM_DT_885X="no";
	HW_CENTER_3XX="no"; HW_COLEAD_SLM="no"; HW_CONRAD_DIGI_35_CPU="no";
	HW_DEREE_DE5000="no"; HW_FLUKE_DMM="no"; HW_GMC_MH_1X_2X="no";
	HW_HAMEG_HMO="no"; HW_MANSON_HCS_3XXX="no"; HW_MIC_985XX="no";
	HW_MOTECH_LPS_30X="no"; HW_NORMA_DMM="no";
	HW_OPENBENCH_LOGIC_SNIFFER="no"; HW_SERIAL_DMM="no";
	HW_TELEINFO="no"; HW_TONDAJ_SL_814="no";
else
	# Define HAVE_LIBSERIALPORT in config.h if we found libserialport.
	AC_DEFINE_UNQUOTED(HAVE_LIBSERIALPORT, [1],
		[Specifies whether we have libserialport.])
fi

# Serial port helper code is only compiled in if libserialport was found.
AM_CONDITIONAL(NEED_SERIAL, test "x$have_libserialport" != xno)

# The VISA SCPI backend is only compiled in if librevisa was found.
PKG_CHECK_MODULES([librevisa], [librevisa >= 0.0.20130412],
	[have_librevisa="yes"; LIB_CFLAGS="$LIB_CFLAGS $librevisa_CFLAGS";
	LIBS="$LIBS $librevisa_LIBS";
	SR_PKGLIBS="$SR_PKGLIBS librevisa"],
	[have_librevisa="no"])
AM_CONDITIONAL(NEED_VISA, test "x$have_librevisa" != xno)
if test "x$have_librevisa" != "xno"; then
	# Define HAVE_LIBREVISA in config.h if we found librevisa.
	AC_DEFINE_UNQUOTED(HAVE_LIBREVISA, [1],
		[Specifies whether we have librevisa.])
	librevisa_ver=`$PKG_CONFIG --modversion librevisa`
	AC_DEFINE_UNQUOTED(HAVE_LIBREVISA_VER, ["$librevisa_ver"], [.])
fi

# The GPIB SCPI backend is only compiled in if libgpib is found.
PKG_CHECK_MODULES([libgpib], [libgpib],
	[have_libgpib="yes"; LIB_CFLAGS="$LIB_CFLAGS $libgpib_CFLAGS";
	LIBS="$LIBS $libgpib_LIBS";
	SR_PKGLIBS="$SR_PKGLIBS libgpib"],
	[have_libgpib="no"])
AM_CONDITIONAL(NEED_GPIB, test "x$have_libgpib" != xno)
if test "x$have_libgpib" != "xno"; then
	# Define HAVE_LIBGPIB in config.h if we found libgpib.
	AC_DEFINE_UNQUOTED(HAVE_LIBGPIB, [1],
		[Specifies whether we have libgpib.])
	libgpib_ver=`$PKG_CONFIG --modversion libgpib`
	AC_DEFINE_UNQUOTED(HAVE_LIBGPIB_VER, ["$libgpib_ver"], [.])
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
			AC_MSG_ERROR([libusb support requested, but it was not found.])
		fi
		;;
	esac
else
	have_libusb1_0="no"
fi
if test "x$have_libusb1_0" = "xno"; then
	HW_BRYMEN_BM86X="no"; HW_FX2LAFW="no";
	HW_HANTEK_DSO="no"; HW_IKALOGIC_SCANALOGIC2="no";
	HW_KECHENG_KC_330B="no"; HW_LASCAR_EL_USB="no";
	HW_SYSCLK_LWLA="no"; HW_TESTO="no"; HW_UNI_T_DMM="no";
	HW_UNI_T_UT32X="no"; HW_VICTOR_DMM="no";
	HW_ZEROPLUS_LOGIC_CUBE="no"; HW_SALEAE_LOGIC16="no";
else
	# Define HAVE_LIBUSB_1_0 in config.h if we found libusb-1.0.
	AC_DEFINE_UNQUOTED(HAVE_LIBUSB_1_0, [1],
		[Specifies whether we have libusb-1.0.])
fi

# USB + FX2 firmware helper code is only compiled in if libusb-1.0 was found.
AM_CONDITIONAL(NEED_USB, test "x$have_libusb1_0" != xno)

# libftdi is only needed for some hardware drivers. Disable them if not found.
if test "x$enable_libftdi" != "xno"; then
	PKG_CHECK_MODULES([libftdi], [libftdi >= 0.16],
		[have_libftdi="yes"; LIB_CFLAGS="$LIB_CFLAGS $libftdi_CFLAGS";
		LIBS="$LIBS $libftdi_LIBS"; SR_PKGLIBS="$SR_PKGLIBS libftdi";
		libftdi_ver=`$PKG_CONFIG --modversion libftdi`],
		[PKG_CHECK_MODULES([libftdi1], [libftdi1 >= 1.0],
			[LIB_CFLAGS="$LIB_CFLAGS $libftdi1_CFLAGS";
			LIBS="$LIBS $libftdi1_LIBS";
			SR_PKGLIBS="$SR_PKGLIBS libftdi1";
			libftdi_ver=`$PKG_CONFIG --modversion libftdi1`],
			[have_libftdi="no"])])
	if test "x$enable_libftdi$have_libftdi" = "xyesno"; then
		AC_MSG_ERROR([libftdi support requested, but it was not found.])
	fi
else
	have_libftdi="no"
fi
if test "x$have_libftdi" = "xno"; then
	HW_ASIX_SIGMA="no"; HW_CHRONOVU_LA="no"; HW_IKALOGIC_SCANAPLUS="no";
	HW_PIPISTRELLO_OLS="no";
else
	# Define HAVE_LIBFTDI in config.h if we found libftdi.
	AC_DEFINE_UNQUOTED(HAVE_LIBFTDI, [1],
		[Specifies whether we have libftdi.])
	AC_DEFINE_UNQUOTED(HAVE_LIBFTDI_VER, ["$libftdi_ver"], [.])
fi

# glibmm-2.4 is needed for the C++ bindings.
PKG_CHECK_MODULES([glibmm], [glibmm-2.4 >= 2.32.0],
	[CXXFLAGS="$CXXFLAGS $glibmm_CFLAGS";
	CXXLIBS="$CXXLIBS $glibmm_LIBS"],
	[BINDINGS_CXX="no"; cxx_msg="glibmm required"])

# C++ bindings want stoi and stod.
if test "x$BINDINGS_CXX" = "xyes"; then
	AC_LANG_PUSH([C++])
	AC_MSG_CHECKING([for stoi and stod])
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([#include <string>],
		[{ return std::stoi("1") + std::stod("1.0"); }])],
		[AC_MSG_RESULT([yes]);
		AC_DEFINE_UNQUOTED(HAVE_STOI_STOD, [1],
		[Specifies whether we have the stoi and stod functions.])],
		[AC_MSG_RESULT([no])])
	AC_LANG_POP([C++])
fi

# PyGObject is needed for the Python bindings.
PKG_CHECK_MODULES([pygobject], [pygobject-3.0 >= 3.0.0],
	[CXXFLAGS="$CXXFLAGS $pygobject_CFLAGS";
	CXXLIBS="$CXXLIBS $pygobject_LIBS";
	PKG_CHECK_MODULES([pygobject_3_7_91], [pygobject-3.0 >= 3.7.91],
		[pyg_flags_signed=0], [pyg_flags_signed=1]);
	AC_DEFINE_UNQUOTED(PYGOBJECT_FLAGS_SIGNED, [$pyg_flags_signed],
		[Whether last argument to pyg_flags_get_value() is signed.])],
	[BINDINGS_PYTHON="no"; python_msg="Python pygobject module required"])

# The Check unit testing framework is optional. Disable if not found.
PKG_CHECK_MODULES([check], [check >= 0.9.4],
	[have_check="yes"], [have_check="no"])
AM_CONDITIONAL(HAVE_CHECK, test x"$have_check" = "xyes")

# The BeagleLogic driver needs sys/mman.h and sys/ioctl.h. Don't try to
# build it if these headers aren't available.
AC_CHECK_HEADERS([sys/mman.h sys/ioctl.h], [], [HW_BEAGLELOGIC="no"])

# The BayLibre ACME driver needs sys/timerfd.h. Don't try to build it if
# this header isn't available.
AC_CHECK_HEADERS([sys/timerfd.h], [], [HW_BAYLIBRE_ACME="no"])

# The ACME driver can only be built for Linux.
case "$host" in
	*linux*) ;;
	*) HW_BAYLIBRE_ACME="no" ;;
esac

AC_SUBST(SR_PKGLIBS)

CFLAGS="$CFLAGS $LIB_CFLAGS"
CXXFLAGS="$CXXFLAGS $LIB_CFLAGS"

# Find Java compiler and JNI includes for Java bindings.
AC_CHECK_PROG([HAVE_JAVAC], [javac], [yes], [no])
if test "x$HAVE_JAVAC" = "xyes" && test "x$BINDINGS_JAVA" = "xyes"; then
	AX_PROG_JAVAC
	if test "x$JNI_INCLUDE_DIRS" = "xauto"; then
		if test "x$cross_compiling" = "xyes"; then
			JNI_INCLUDE_DIRS=
		else
			AX_JNI_INCLUDE_DIR
		fi
	fi
	for JNI_INCLUDE_DIR in $JNI_INCLUDE_DIRS; do
		CXXFLAGS="$CXXFLAGS -I$JNI_INCLUDE_DIR"
	done
	AC_LANG_PUSH([C++])
	AC_CHECK_HEADERS([jni.h], [], [BINDINGS_JAVA="no"; java_msg="Java JNI headers required"])
	AC_LANG_POP([C++])
else
	BINDINGS_JAVA="no"; java_msg="Java compiler required"
fi

# Python and Java bindings depend on C++ bindings.
if test "x$BINDINGS_CXX" != "xyes"; then
	BINDINGS_PYTHON="no"; python_msg="C++ bindings required"
	BINDINGS_JAVA="no"; java_msg="C++ bindings required"
fi

# Now set AM_CONDITIONALs and AC_DEFINEs for the enabled/disabled drivers.

AC_DEFUN([DRIVER2], [
	AM_CONDITIONAL($1, test "x$2" = "xyes")
	if test "x$2" = "xyes"; then AC_DEFINE($3, 1, [.]) fi
])

DRIVER2([HW_AGILENT_DMM], [$HW_AGILENT_DMM], [HAVE_HW_AGILENT_DMM])
DRIVER2([HW_APPA_55II], [$HW_APPA_55II], [HAVE_HW_APPA_55II])
DRIVER2([HW_ASIX_SIGMA], [$HW_ASIX_SIGMA], [HAVE_HW_ASIX_SIGMA])
DRIVER2([HW_ATTEN_PPS3XXX], [$HW_ATTEN_PPS3XXX], [HAVE_HW_ATTEN_PPS3XXX])
DRIVER2([HW_BAYLIBRE_ACME], [$HW_BAYLIBRE_ACME], [HAVE_HW_BAYLIBRE_ACME])
DRIVER2([HW_BEAGLELOGIC], [$HW_BEAGLELOGIC], [HAVE_HW_BEAGLELOGIC])
DRIVER2([HW_BRYMEN_BM86X], [$HW_BRYMEN_BM86X], [HAVE_HW_BRYMEN_BM86X])
DRIVER2([HW_BRYMEN_DMM], [$HW_BRYMEN_DMM], [HAVE_HW_BRYMEN_DMM])
DRIVER2([HW_CEM_DT_885X], [$HW_CEM_DT_885X], [HAVE_HW_CEM_DT_885X])
DRIVER2([HW_CENTER_3XX], [$HW_CENTER_3XX], [HAVE_HW_CENTER_3XX])
DRIVER2([HW_CHRONOVU_LA], [$HW_CHRONOVU_LA], [HAVE_HW_CHRONOVU_LA])
DRIVER2([HW_COLEAD_SLM], [$HW_COLEAD_SLM], [HAVE_HW_COLEAD_SLM])
DRIVER2([HW_CONRAD_DIGI_35_CPU], [$HW_CONRAD_DIGI_35_CPU], [HAVE_HW_CONRAD_DIGI_35_CPU])
DRIVER2([HW_DEMO], [$HW_DEMO], [HAVE_HW_DEMO])
DRIVER2([HW_DEREE_DE5000], [$HW_DEREE_DE5000], [HAVE_HW_DEREE_DE5000])
DRIVER2([HW_FLUKE_DMM], [$HW_FLUKE_DMM], [HAVE_HW_FLUKE_DMM])
DRIVER2([HW_FX2LAFW], [$HW_FX2LAFW], [HAVE_HW_FX2LAFW])
DRIVER2([HW_GMC_MH_1X_2X], [$HW_GMC_MH_1X_2X], [HAVE_HW_GMC_MH_1X_2X])
DRIVER2([HW_HAMEG_HMO], [$HW_HAMEG_HMO], [HAVE_HW_HAMEG_HMO])
DRIVER2([HW_HANTEK_DSO], [$HW_HANTEK_DSO], [HAVE_HW_HANTEK_DSO])
DRIVER2([HW_IKALOGIC_SCANALOGIC2], [$HW_IKALOGIC_SCANALOGIC2], [HAVE_HW_IKALOGIC_SCANALOGIC2])
DRIVER2([HW_IKALOGIC_SCANAPLUS], [$HW_IKALOGIC_SCANAPLUS], [HAVE_HW_IKALOGIC_SCANAPLUS])
DRIVER2([HW_KECHENG_KC_330B], [$HW_KECHENG_KC_330B], [HAVE_HW_KECHENG_KC_330B])
DRIVER2([HW_LASCAR_EL_USB], [$HW_LASCAR_EL_USB], [HAVE_HW_LASCAR_EL_USB])
DRIVER2([HW_MANSON_HCS_3XXX], [$HW_MANSON_HCS_3XXX], [HAVE_HW_MANSON_HCS_3XXX])
DRIVER2([HW_MAYNUO_M97], [$HW_MAYNUO_M97], [HAVE_HW_MAYNUO_M97])
DRIVER2([HW_MIC_985XX], [$HW_MIC_985XX], [HAVE_HW_MIC_985XX])
DRIVER2([HW_MOTECH_LPS_30X], [$HW_MOTECH_LPS_30X], [HAVE_HW_MOTECH_LPS_30X])
DRIVER2([HW_NORMA_DMM], [$HW_NORMA_DMM], [HAVE_HW_NORMA_DMM])
DRIVER2([HW_OPENBENCH_LOGIC_SNIFFER], [$HW_OPENBENCH_LOGIC_SNIFFER], [HAVE_HW_OPENBENCH_LOGIC_SNIFFER])
DRIVER2([HW_PIPISTRELLO_OLS], [$HW_PIPISTRELLO_OLS], [HAVE_HW_PIPISTRELLO_OLS])
DRIVER2([HW_RIGOL_DS], [$HW_RIGOL_DS], [HAVE_HW_RIGOL_DS])
DRIVER2([HW_SALEAE_LOGIC16], [$HW_SALEAE_LOGIC16], [HAVE_HW_SALEAE_LOGIC16])
DRIVER2([HW_SCPI_PPS], [$HW_SCPI_PPS], [HAVE_HW_SCPI_PPS])
DRIVER2([HW_SERIAL_DMM], [$HW_SERIAL_DMM], [HAVE_HW_SERIAL_DMM])
DRIVER2([HW_SYSCLK_LWLA], [$HW_SYSCLK_LWLA], [HAVE_HW_SYSCLK_LWLA])
DRIVER2([HW_TELEINFO], [$HW_TELEINFO], [HAVE_HW_TELEINFO])
DRIVER2([HW_TESTO], [$HW_TESTO], [HAVE_HW_TESTO])
DRIVER2([HW_TONDAJ_SL_814], [$HW_TONDAJ_SL_814], [HAVE_HW_TONDAJ_SL_814])
DRIVER2([HW_UNI_T_DMM], [$HW_UNI_T_DMM], [HAVE_HW_UNI_T_DMM])
DRIVER2([HW_UNI_T_UT32X], [$HW_UNI_T_UT32X], [HAVE_HW_UNI_T_UT32X])
DRIVER2([HW_VICTOR_DMM], [$HW_VICTOR_DMM], [HAVE_HW_VICTOR_DMM])
DRIVER2([HW_YOKOGAWA_DLM], [$HW_YOKOGAWA_DLM], [HAVE_HW_YOKOGAWA_DLM])
DRIVER2([HW_ZEROPLUS_LOGIC_CUBE], [$HW_ZEROPLUS_LOGIC_CUBE], [HAVE_HW_ZEROPLUS_LOGIC_CUBE])

AM_CONDITIONAL(BINDINGS_CXX, test x$BINDINGS_CXX = xyes)

AM_CONDITIONAL(BINDINGS_PYTHON, test x$BINDINGS_PYTHON = xyes)

AM_CONDITIONAL(BINDINGS_JAVA, test x$BINDINGS_JAVA = xyes)

# Checks for header files.
# These are already implicitly checked: inttypes.h stdint.h stdlib.h string.h
# unistd.h memory.h strings.h dlfcn.h sys/types.h sys/stat.h.

# Checks for typedefs, structures, and compiler characteristics.
AC_C_BIGENDIAN

AC_SUBST([FIRMWARE_DIR], ['$(datadir)/sigrok-firmware'])
AC_SUBST(MAKEFLAGS, '--no-print-directory')
AC_SUBST(AM_LIBTOOLFLAGS, '--silent')

AC_CONFIG_FILES([Makefile libsigrok.pc bindings/cxx/libsigrokcxx.pc])

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
for lib in "glib-2.0 >= 2.32.0" "libzip >= 0.10" "libserialport >= 0.1.1" \
	"librevisa >= 0.0.20130412" "libusb-1.0 >= 1.0.16" "libftdi >= 0.16" \
	"libftdi1 >= 1.0" "libgpib" "glibmm-2.4 >= 2.32.0" \
	"pygobject-3.0 >= 3.0.0" "check >= 0.9.4"
do
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

eval echo -e "@Enabled hardware drivers:@@${driver_summary}" | tr @ \\n

# Prepare final bindings report messages.
final_cxx=""
final_python=""
final_java=""
if test "$enable_bindings" = "no"; then
	final_cxx=" (disabled)"
	final_python=" (disabled)"
	final_java=" (disabled)"
else
	if test "$enable_cxx" = "no"; then
		final_cxx=" (disabled)"
	elif test "$BINDINGS_CXX" = "no"; then
		final_cxx=" ($cxx_msg)"
	fi
	if test "$enable_python" = "no"; then
		final_python=" (disabled)"
	elif test "$BINDINGS_PYTHON" = "no"; then
		final_python=" ($python_msg)"
	fi
	if test "$enable_java" = "no"; then
		final_java=" (disabled)"
	elif test "$BINDINGS_JAVA" = "no"; then
		final_java=" ($java_msg)"
	fi
fi

echo "Enabled SCPI backends:"
echo " - TCP............................. yes"
if test "x$have_rpc" = "x0"; then
echo " - RPC............................. no"
else
echo " - RPC............................. yes"
fi
echo " - serial.......................... $have_libserialport"
echo " - VISA............................ $have_librevisa"
echo " - GPIB............................ $have_libgpib"
echo " - USBTMC.......................... $have_libusb1_0"
echo

echo "Enabled language bindings:"
echo " - C++............................. $BINDINGS_CXX$final_cxx"
echo " - Python.......................... $BINDINGS_PYTHON$final_python"
echo " - Java............................ $BINDINGS_JAVA$final_java"
echo
