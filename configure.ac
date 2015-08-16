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

AC_INIT([libsigrok], [sr_package_version_major.sr_package_version_minor.sr_package_version_micro],
	[sigrok-devel@lists.sourceforge.net],
	[libsigrok], [http://www.sigrok.org])
AC_CONFIG_HEADERS([config.h include/libsigrok/version.h])
AC_CONFIG_MACRO_DIR([autostuff])
AC_CONFIG_AUX_DIR([autostuff])

# We require at least automake 1.11 (needed for 'silent rules').
AM_INIT_AUTOMAKE([1.11 -Wall -Werror no-define nostdinc subdir-objects check-news color-tests])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])
m4_ifdef([AM_PROG_AR], [AM_PROG_AR])

AH_TOP([#ifndef SR_CONFIG_H
#define SR_CONFIG_H    /* To stop multiple inclusions. */])
AH_BOTTOM([#endif /* SR_CONFIG_H */])

# Enable more compiler warnings via -Wall and -Wextra. Add -fvisibility=hidden
# and enforce use of SR_API to explicitly mark all public API functions.
CFLAGS="$CFLAGS -std=c11"
CFLAGS="$CFLAGS -Wall -Wextra -Wmissing-prototypes"
CFLAGS="$CFLAGS -fvisibility=hidden"
CFLAGS="$CFLAGS -D_POSIX_C_SOURCE=200112L"

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

CONFIG_STATUS_DEPENDENCIES=
SR_PACKAGE_VERSION="AC_PACKAGE_VERSION"

# Check if we can get revision information from git.
sr_head=`git -C "$srcdir" rev-parse --verify --short HEAD 2>&AS_MESSAGE_LOG_FD`

AS_IF([test "$?" -eq 0 && test -n "$sr_head"], [
	CONFIG_STATUS_DEPENDENCIES='$(top_srcdir)/.git/HEAD'
	sr_head_name=`git -C "$srcdir" rev-parse --symbolic-full-name HEAD 2>&AS_MESSAGE_LOG_FD`
	AS_IF([test "$?" -eq 0 && test -f "$srcdir/.git/$sr_head_name"],
		[CONFIG_STATUS_DEPENDENCIES="$CONFIG_STATUS_DEPENDENCIES \$(top_srcdir)/.git/$sr_head_name"])

	# Append the revision hash unless we are exactly on a tagged release.
	git -C "$srcdir" describe --match 'AC_PACKAGE_NAME-AC_PACKAGE_VERSION' \
		--exact-match >&AS_MESSAGE_LOG_FD 2>&AS_MESSAGE_LOG_FD \
	|| SR_PACKAGE_VERSION="$SR_PACKAGE_VERSION-git-$sr_head"
])
AC_SUBST([CONFIG_STATUS_DEPENDENCIES])
AC_SUBST([SR_PACKAGE_VERSION])

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
AC_SUBST([SR_LIB_VERSION],
	["$SR_LIB_VERSION_CURRENT:$SR_LIB_VERSION_REVISION:$SR_LIB_VERSION_AGE"])

AC_DEFINE_UNQUOTED([SR_LIB_VERSION_CURRENT], [$SR_LIB_VERSION_CURRENT], [.])
AC_DEFINE_UNQUOTED([SR_LIB_VERSION_REVISION], [$SR_LIB_VERSION_REVISION], [.])
AC_DEFINE_UNQUOTED([SR_LIB_VERSION_AGE], [$SR_LIB_VERSION_AGE], [.])
AC_DEFINE_UNQUOTED([SR_LIB_VERSION_STRING], ["$SR_LIB_VERSION"], [.])

AC_ARG_ENABLE([all-drivers],
	[AS_HELP_STRING([--enable-all-drivers],
			[enable all drivers by default [default=yes]])],
	[HW_ENABLED_DEFAULT=$enableval],
	[HW_ENABLED_DEFAULT=yes])

## _SR_DRIVER(Device name, driver-name, enable, var-name)
m4_define([_SR_DRIVER], [
	AC_ARG_ENABLE([$4],
		[AS_HELP_STRING([--enable-$2], [enable $1 support])],
		[$4=$enableval], [$4=$3])
	m4_append([SR_DRIVER_SUMMARY],
		[ - m4_translit(m4_format([%-32s], [$2]), [ ], [.]) [$]$4],
		m4_newline)
	AC_CONFIG_COMMANDS_PRE([dnl
		AM_CONDITIONAL([$4], [test "x[$]$4" = xyes])
		AM_COND_IF([$4], [AC_DEFINE([HAVE_$4], [1], [Whether to support $1 device.])])
	])
])
## SR_DRIVER(Device name, driver-name, [no])
m4_define([SR_DRIVER],
	[_SR_DRIVER([$1], [$2],
		m4_default_quoted([$3], [$HW_ENABLED_DEFAULT]),
		m4_expand([AS_TR_CPP([HW_$2])]))])

SR_DRIVER([Agilent DMM], [agilent-dmm])
SR_DRIVER([Appa 55II], [appa-55ii])
SR_DRIVER([ASIX SIGMA/SIGMA2], [asix-sigma])
SR_DRIVER([Atten PPS3xxx], [atten-pps3xxx])
SR_DRIVER([BayLibre ACME], [baylibre-acme])
SR_DRIVER([BeagleLogic], [beaglelogic])
SR_DRIVER([Brymen BM86x], [brymen-bm86x])
SR_DRIVER([Brymen DMM], [brymen-dmm])
SR_DRIVER([CEM DT-885x], [cem-dt-885x])
SR_DRIVER([Center 3xx], [center-3xx])
SR_DRIVER([ChronoVu LA], [chronovu-la])
SR_DRIVER([Colead SLM], [colead-slm])
SR_DRIVER([Conrad DIGI 35 CPU], [conrad-digi-35-cpu])
SR_DRIVER([DER EE DE-5000], [deree-de5000])
SR_DRIVER([demo], [demo])
SR_DRIVER([Fluke DMM], [fluke-dmm])
SR_DRIVER([fx2lafw], [fx2lafw])
SR_DRIVER([GMC MH 1x/2x], [gmc-mh-1x-2x])
SR_DRIVER([Hameg HMO], [hameg-hmo])
SR_DRIVER([Hantek DSO], [hantek-dso])
SR_DRIVER([Ikalogic Scanalogic-2], [ikalogic-scanalogic2])
SR_DRIVER([Ikalogic Scanaplus], [ikalogic-scanaplus])
SR_DRIVER([Kecheng KC-330B], [kecheng-kc-330b])
SR_DRIVER([Lascar EL-USB], [lascar-el-usb])
SR_DRIVER([Manson HCS-3xxx], [manson-hcs-3xxx])
SR_DRIVER([maynuo-m97], [maynuo-m97])
SR_DRIVER([MIC 985xx], [mic-985xx])
SR_DRIVER([Motech LPS 30x], [motech-lps-30x])
SR_DRIVER([Norma DMM], [norma-dmm])
SR_DRIVER([OpenBench Logic Sniffer], [openbench-logic-sniffer])
SR_DRIVER([Pipistrello-OLS], [pipistrello-ols])
SR_DRIVER([Rigol DS], [rigol-ds])
SR_DRIVER([Saleae Logic16], [saleae-logic16])
SR_DRIVER([SCPI PPS], [scpi-pps])
SR_DRIVER([serial DMM], [serial-dmm])
SR_DRIVER([Sysclk LWLA], [sysclk-lwla])
SR_DRIVER([Teleinfo], [teleinfo])
SR_DRIVER([Testo], [testo])
SR_DRIVER([Tondaj SL-814], [tondaj-sl-814])
SR_DRIVER([UNI-T DMM], [uni-t-dmm])
SR_DRIVER([UNI-T UT32x], [uni-t-ut32x])
SR_DRIVER([Victor DMM], [victor-dmm])
SR_DRIVER([Yokogawa DL/DLM], [yokogawa-dlm])
SR_DRIVER([ZEROPLUS Logic Cube], [zeroplus-logic-cube])

#############################
##  Optional dependencies  ##
#############################

# Initialize pkg-config.
# We require at least 0.22, as "Requires.private" behaviour changed there.
PKG_PROG_PKG_CONFIG([0.22])

# This variable collects the pkg-config module names of all dependencies.
# These are used to derive the compiler flags and for the "Requires.private"
# field in the generated libsigrok.pc file.
SR_PKGLIBS=
SR_PKGLIBS_CHECK=
SR_PKGLIBS_CXX=
SR_PKGLIBS_PYTHON=

# Keep track of all checked modules so we can list them at the end.
sr_pkglibs_summary=
sr_nl='
'
## SR_PKG_CHECK(tag, [collect-var], module...)
m4_define([SR_PKG_CHECK], [
	sr_mod_leader=`printf '%.32s' "$3................................"`
	PKG_CHECK_EXISTS([$3], [
		sr_have_$1=yes
		m4_ifval([$2], [$2="[$]$2[$]{$2:+ }$3"])
		sr_$1_version=`$PKG_CONFIG --modversion "$3" 2>&AS_MESSAGE_LOG_FD`
		AS_VAR_APPEND([sr_pkglibs_summary], [" - $sr_mod_leader $sr_$1_version$sr_nl"])
	], [
		sr_$1_version=
		AS_VAR_APPEND([sr_pkglibs_summary], [" - $sr_mod_leader no$sr_nl"])
		m4_ifval([$4], [SR_PKG_CHECK([$1], [$2], m4_shift3($@))], [sr_have_$1=no])
	])
])

## SR_ARG_OPT_PKG(opt-name, [cpp-name], [cond-name], module...)
m4_define([SR_ARG_OPT_PKG], [
	AC_ARG_WITH([$1], [AS_HELP_STRING([--without-$1],
			[disable $1 support [default=detect]])])
	AS_IF([test "x$with_$1" = xno], [sr_have_$1=no],
		[test "x$sr_have_$1" != xyes],
			[SR_PKG_CHECK([$1], [SR_PKGLIBS], m4_shift3($@))])
	AS_IF([test "x$with_$1$sr_have_$1" = xyesno],
		[AC_MSG_ERROR([$1 support requested, but it was not found.])])
	m4_ifval([$2], [AS_IF([test "x$sr_have_$1" = xyes], [
		AC_DEFINE([HAVE_$2], [1], [Whether $1 is available.])
		AC_DEFINE_UNQUOTED([CONF_$2_VERSION], ["$sr_$1_version"], [Build-time version of $1.])
	])])
	m4_ifval([$3], [AM_CONDITIONAL([$3], [test "x$sr_have_$1" = xyes])])
])

SR_ARG_OPT_PKG([libserialport], [LIBSERIALPORT], [NEED_SERIAL],
	[libserialport >= 0.1.1])
AS_IF([test "x$sr_have_libserialport" != xyes], [
	HW_AGILENT_DMM="no"; HW_APPA_55II="no";
	HW_ATTEN_PPS3XXX="no"; HW_BRYMEN_DMM="no"; HW_CEM_DT_885X="no";
	HW_CENTER_3XX="no"; HW_COLEAD_SLM="no"; HW_CONRAD_DIGI_35_CPU="no";
	HW_DEREE_DE5000="no"; HW_FLUKE_DMM="no"; HW_GMC_MH_1X_2X="no";
	HW_HAMEG_HMO="no"; HW_MANSON_HCS_3XXX="no"; HW_MIC_985XX="no";
	HW_MOTECH_LPS_30X="no"; HW_NORMA_DMM="no";
	HW_OPENBENCH_LOGIC_SNIFFER="no"; HW_SERIAL_DMM="no";
	HW_TELEINFO="no"; HW_TONDAJ_SL_814="no";
])

SR_ARG_OPT_PKG([libftdi], [LIBFTDI],,
	[libftdi >= 0.16], [libftdi1 >= 1.0])
AS_IF([test "x$sr_have_libftdi" != xyes], [
	HW_ASIX_SIGMA="no"; HW_CHRONOVU_LA="no"; HW_IKALOGIC_SCANAPLUS="no";
	HW_PIPISTRELLO_OLS="no";
])

# FreeBSD comes with an "integrated" libusb-1.0-style USB API.
# This means libusb-1.0 is always available; no need to check for it.
AS_CASE([$host], [*freebsd*], [sr_have_libusb=yes])

SR_ARG_OPT_PKG([libusb], [LIBUSB_1_0], [NEED_USB],
	[libusb-1.0 >= 1.0.16])
AS_IF([test "x$sr_have_libusb" != xyes], [
	HW_BRYMEN_BM86X="no"; HW_FX2LAFW="no";
	HW_HANTEK_DSO="no"; HW_IKALOGIC_SCANALOGIC2="no";
	HW_KECHENG_KC_330B="no"; HW_LASCAR_EL_USB="no";
	HW_SYSCLK_LWLA="no"; HW_TESTO="no"; HW_UNI_T_DMM="no";
	HW_UNI_T_UT32X="no"; HW_VICTOR_DMM="no";
	HW_ZEROPLUS_LOGIC_CUBE="no"; HW_SALEAE_LOGIC16="no";
])

SR_ARG_OPT_PKG([librevisa], [LIBREVISA], [NEED_VISA],
	[librevisa >= 0.0.20130412])

SR_ARG_OPT_PKG([libgpib], [LIBGPIB], [NEED_GPIB],
	[libgpib])

######################
##  Feature checks  ##
######################

# The Check unit testing framework is optional. Disable if not found.
SR_PKG_CHECK([check], [SR_PKGLIBS_CHECK], [check >= 0.9.4])
AM_CONDITIONAL([HAVE_CHECK], [test "x$sr_have_check" = xyes])

AC_LANG([C])

# Check host characteristics.
AC_C_BIGENDIAN

# The BeagleLogic driver needs sys/mman.h and sys/ioctl.h. Don't try to
# build it if these headers aren't available.
AC_CHECK_HEADERS([sys/mman.h sys/ioctl.h],, [HW_BEAGLELOGIC=no])

# The BayLibre ACME driver needs sys/timerfd.h. Don't try to build it if
# this header isn't available.
AC_CHECK_HEADERS([sys/timerfd.h],, [HW_BAYLIBRE_ACME=no])

# RPC is only needed for VXI support.
AC_CACHE_CHECK([for RPC support], [sr_cv_have_rpc],
	[AC_LINK_IFELSE([AC_LANG_PROGRAM(
			[[#include <rpc/rpc.h>]m4_newline[CLIENT *rpc_test(void);]],
			[[(void) clnt_create("", 0, 0, "");]])],
		[sr_cv_have_rpc=yes], [sr_cv_have_rpc=no])])
AS_IF([test "x$sr_cv_have_rpc" = xyes],
	[AC_DEFINE([HAVE_RPC], [1], [Specifies whether we have RPC support.])])
# VXI support is only compiled if RPC support was found.
AM_CONDITIONAL([NEED_RPC], [test "x$sr_cv_have_rpc" = xyes])

AC_LANG([C++])

###############################
##  Language bindings setup  ##
###############################

AC_ARG_ENABLE([bindings],
	[AS_HELP_STRING([--enable-bindings], [build language bindings [default=yes]])],
	[], [enable_bindings=yes])

AC_ARG_ENABLE([cxx],
	[AS_HELP_STRING([--enable-cxx], [build C++ bindings [default=yes]])],
	[], [enable_cxx=$enable_bindings])

AC_ARG_ENABLE([python],
	[AS_HELP_STRING([--enable-python], [build Python bindings [default=yes]])],
	[], [enable_python=$enable_bindings])

AC_ARG_ENABLE([java],
	[AS_HELP_STRING([--enable-java], [build Java bindings [default=yes]])],
	[], [enable_java=$enable_bindings])

####################
##  C++ bindings  ##
####################

sr_cxx_missing=

# Check if the C++ compiler supports the C++11 standard.
m4_ifdef([AX_CXX_COMPILE_STDCXX_11],
	[AX_CXX_COMPILE_STDCXX_11(, [optional])],
	[m4_warn(, [[Missing macro AX_CXX_COMPILE_STDCXX_11: no C++11 check possible]])])
test "x$HAVE_CXX11" = x1 || sr_cxx_missing="$sr_cxx_missing C++11"

# The C++ bindings need glibmm.
SR_PKG_CHECK([glibmm], [SR_PKGLIBS_CXX], [glibmm-2.4 >= 2.32.0])
test "x$sr_have_glibmm" = xyes || sr_cxx_missing="$sr_cxx_missing glibmm"

# The C++ bindings use Doxygen to parse libsigrok symbols.
AC_CHECK_PROG([HAVE_DOXYGEN], [doxygen], [yes], [no])
test "x$HAVE_DOXYGEN" = xyes || sr_cxx_missing="$sr_cxx_missing Doxygen"

# Python is needed for the C++ bindings.
AM_PATH_PYTHON([2.7],
	[HAVE_PYTHON=yes],
	[HAVE_PYTHON=no sr_cxx_missing="$sr_cxx_missing Python"])

AS_IF([test -z "$sr_cxx_missing"],
	[BINDINGS_CXX=$enable_cxx], [BINDINGS_CXX=no])
AM_CONDITIONAL([BINDINGS_CXX], [test "x$BINDINGS_CXX" = xyes])

# C++ bindings want stoi and stod.
AM_COND_IF([BINDINGS_CXX], [
	AC_CACHE_CHECK([for stoi and stod], [sr_cv_have_stoi_stod],
		[AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <string>]],
				[[(void) std::stoi("1"); (void) std::stod("1.0");]])],
			[sr_cv_have_stoi_stod=yes], [sr_cv_have_stoi_stod=no])])
	AS_IF([test "x$sr_cv_have_stoi_stod" = xyes],
		[AC_DEFINE([HAVE_STOI_STOD], [1],
			[Specifies whether we have the stoi and stod functions.])])
])

#######################
##  Python bindings  ##
#######################

sr_python_missing=
test "x$BINDINGS_CXX" = xyes || sr_python_missing=" C++ bindings"

# The Python bindings need Python development files.
SR_PKG_CHECK([python_dev], [SR_PKGLIBS_PYTHON],
	[python >= 2.7], [python2 >= 2.7], [python27 >= 2.7])
test "x$sr_have_python_dev" = xyes \
	|| sr_python_missing="$sr_python_missing Headers"

# PyGObject is needed for the Python bindings.
SR_PKG_CHECK([pygobject], [SR_PKGLIBS_PYTHON], [pygobject-3.0 >= 3.0.0])
test "x$sr_have_python_dev" = xyes \
	|| sr_python_missing="$sr_python_missing PyGObject"

PKG_CHECK_EXISTS([pygobject-3.0 < 3.7.91],
	[AC_DEFINE([PYGOBJECT_FLAGS_SIGNED], [1],
		[Whether last argument to pyg_flags_get_value() is signed.])])

# The Python bindings need the setuptools Python module.
# We'll let it go through even if the AX macro wasn't found,
# as the Python module may still be there.
HAVE_PYMOD_SETUPTOOLS=yes
m4_ifdef([AX_PYTHON_MODULE],
	[AX_PYTHON_MODULE([setuptools])],
	[m4_warn(, [[Missing macro AX_PYTHON_MODULE: no setuptools check]])])
test "x$HAVE_PYMOD_SETUPTOOLS" = xyes \
	|| sr_python_missing="$sr_python_missing Setuptools"

# The Python bindings use SWIG to generate code.
AC_CHECK_PROG([HAVE_SWIG], [swig], [yes], [no])
test "x$HAVE_SWIG" = xyes || sr_python_missing="$sr_python_missing SWIG"

AS_IF([test -z "$sr_python_missing"],
	[BINDINGS_PYTHON=$enable_python], [BINDINGS_PYTHON=no])
AM_CONDITIONAL([BINDINGS_PYTHON], [test "x$BINDINGS_PYTHON" = xyes])

####################
##  Java bindings ##
####################

sr_java_missing=
test "x$BINDINGS_CXX" = xyes || sr_java_missing=" C++ bindings"

# The Java bindings use SWIG to generate code.
test "x$HAVE_SWIG" = xyes || sr_java_missing="$sr_java_missing SWIG"

# Find Java compiler and JNI includes for Java bindings.
AC_CHECK_PROG([HAVE_JAVAC], [javac], [yes], [no])
test "x$HAVE_JAVAC" = xyes || sr_java_missing="$sr_java_missing JavaC"

AC_ARG_WITH([jni-include-path],
	[AS_HELP_STRING([[--with-jni-include-path=DIR-LIST (space-separated)]],
		[specify JNI include directories [default=detect]])],
	[JNI_INCLUDE_DIRS=" $withval"], [JNI_INCLUDE_DIRS=])

JNI_CPPFLAGS=
AS_IF([test "x$enable_java$HAVE_JAVAC" = xyesyes], [
	AX_PROG_JAVAC
	AS_IF([test -z "$JNI_INCLUDE_DIRS" && test "x$cross_compiling" != xyes], [
		## Work around the totally broken logic in AX_JNI_INCLUDE_DIR:
		## If we can find jni.h without any special search path, skip
		## the execution of the broken macro to increase our chances of
		## success.
		AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <jni.h>]], [])],,
			[AX_JNI_INCLUDE_DIR])
	])
])
for sr_dir in $JNI_INCLUDE_DIRS
do
	JNI_CPPFLAGS="$JNI_CPPFLAGS -I$sr_dir"
done
AC_SUBST([JNI_CPPFLAGS])

sr_save_cppflags=$CPPFLAGS
CPPFLAGS=$CPPFLAGS$JNI_CPPFLAGS
AC_CHECK_HEADER([jni.h],, [sr_java_missing="$sr_java_missing JNI headers"])
CPPFLAGS=$sr_save_cppflags

AS_IF([test -z "$sr_java_missing"],
	[BINDINGS_JAVA=$enable_java], [BINDINGS_JAVA=no])
AM_CONDITIONAL([BINDINGS_JAVA], [test "x$BINDINGS_JAVA" = xyes])

##############################
##  Finalize configuration  ##
##############################

# Add mandatory dependencies to module list.
SR_PKGLIBS="$SR_PKGLIBS${SR_PKGLIBS:+ }libzip >= 0.10"
AC_SUBST([SR_PKGLIBS])

# Retrieve the compile and link flags for all modules combined.
# Also, bail out at this point if any module dependency is not met.
PKG_CHECK_MODULES([LIBSIGROK], [glib-2.0 >= 2.32.0 $SR_PKGLIBS])
PKG_CHECK_MODULES([LIBSIGROKCXX], [$SR_PKGLIBS_CXX])
PKG_CHECK_MODULES([PYSIGROK], [$SR_PKGLIBS_PYTHON $SR_PKGLIBS_CXX])
PKG_CHECK_MODULES([CHECK], [$SR_PKGLIBS_CHECK glib-2.0 $SR_PKGLIBS])

sr_glib_version=`$PKG_CONFIG --modversion glib-2.0 2>&AS_MESSAGE_LOG_FD`
sr_libzip_version=`$PKG_CONFIG --modversion libzip 2>&AS_MESSAGE_LOG_FD`

AC_DEFINE_UNQUOTED([CONF_LIBZIP_VERSION], ["$sr_libzip_version"],
	[Build-time version of libzip.])
AC_DEFINE_UNQUOTED([CONF_HOST], ["$host"],
	[The canonical host libsigrok will run on.])

AC_LANG([C])

# libm (the standard math library) is always needed.
AC_SEARCH_LIBS([pow], [m])

# We need to link against the Winsock2 library for SCPI over TCP.
AS_CASE([$host], [*mingw*], [LIBS="$LIBS -lws2_32"])

AC_SUBST([FIRMWARE_DIR], ['$(datadir)/sigrok-firmware'])
AC_SUBST([MAKEFLAGS], ['--no-print-directory'])
AC_SUBST([AM_LIBTOOLFLAGS], ['--silent'])

AC_CONFIG_FILES([Makefile libsigrok.pc bindings/cxx/libsigrokcxx.pc])

AC_OUTPUT

# Prepare bindings report messages.
m4_define([sr_prepare_binding_report], [
	AS_IF([test "x$enable_$1" = xyes],
		[sr_report_$1=$sr_$1_missing],
		[sr_report_$1=' (disabled)'])
])
m4_map_args([sr_prepare_binding_report], [cxx], [python], [java])

cat >&AS_MESSAGE_FD <<_EOF

libsigrok configuration summary:
 - Package version................. $SR_PACKAGE_VERSION
 - Library ABI version............. $SR_LIB_VERSION
 - Prefix.......................... $prefix
 - Building on..................... $build
 - Building for.................... $host

Detected libraries (required):
 - glib-2.0 >= 2.32.0.............. $sr_glib_version
 - libzip >= 0.10.................. $sr_libzip_version

Detected libraries (optional):
$sr_pkglibs_summary

Enabled hardware drivers:
SR_DRIVER_SUMMARY

Enabled SCPI backends:
 - TCP............................. yes
 - RPC............................. $sr_cv_have_rpc
 - serial.......................... $sr_have_libserialport
 - VISA............................ $sr_have_librevisa
 - GPIB............................ $sr_have_libgpib
 - USBTMC.......................... $sr_have_libusb

Enabled language bindings:
 - C++............................. $BINDINGS_CXX$sr_report_cxx
 - Python.......................... $BINDINGS_PYTHON$sr_report_python
 - Java............................ $BINDINGS_JAVA$sr_report_java

_EOF
