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

AC_INIT([libsigrok], [0.4.0],
	[sigrok-devel@lists.sourceforge.net],
	[libsigrok], [http://www.sigrok.org])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([autostuff])
AC_CONFIG_HEADERS([config.h include/libsigrok/version.h])

# We require at least automake 1.11 (needed for 'silent rules').
AM_INIT_AUTOMAKE([1.11 -Wall -Werror no-define nostdinc subdir-objects check-news color-tests])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])
m4_ifdef([AM_PROG_AR], [AM_PROG_AR])

AH_TOP([#ifndef SR_CONFIG_H
#define SR_CONFIG_H    /* To stop multiple inclusions. */])
AH_BOTTOM([#endif /* SR_CONFIG_H */])

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

# Set up the libsigrok version defines.
SR_PKG_VERSION_SET([SR_PACKAGE_VERSION], [AC_PACKAGE_VERSION])

# Library version for libsigrok (NOT the same as the package version).
# Carefully read the libtool docs before updating these numbers!
# The algorithm for determining which number to change (and how) is nontrivial!
# http://www.gnu.org/software/libtool/manual/libtool.html#Updating-version-info
SR_LIB_VERSION_SET([SR_LIB_VERSION], [2:0:0])

#############################
##  Optional dependencies  ##
#############################

# Initialize pkg-config.
# We require at least 0.22, as "Requires.private" behaviour changed there.
PKG_PROG_PKG_CONFIG([0.22])

# Keep track of all checked modules so we can list them at the end.
SR_PKG_CHECK_SUMMARY([sr_pkglibs_summary])

# Collect the pkg-config module names of all dependencies in SR_PKGLIBS.
# These are used to derive the compiler flags and for the "Requires.private"
# field in the generated libsigrok.pc file.
SR_VAR_OPT_PKG([SR_PKGLIBS], [sr_deps_avail])
SR_PKGLIBS_CHECK=
SR_PKGLIBS_CXX=
SR_PKGLIBS_PYTHON=

SR_ARG_OPT_PKG([libserialport], [LIBSERIALPORT], [NEED_SERIAL],
	[libserialport >= 0.1.1])

SR_ARG_OPT_PKG([libftdi], [LIBFTDI],,
	[libftdi1 >= 1.0], [libftdi >= 0.16])

# FreeBSD comes with an "integrated" libusb-1.0-style USB API.
# This means libusb-1.0 is always available; no need to check for it.
AS_CASE([$host], [*freebsd*], [sr_have_libusb=yes])

SR_ARG_OPT_PKG([libusb], [LIBUSB_1_0], [NEED_USB],
	[libusb-1.0 >= 1.0.16])

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

# Enable the C11 standard if possible, and enforce the use
# of SR_API to explicitly mark all public API functions.
SR_EXTRA_CFLAGS=
SR_CHECK_COMPILE_FLAGS([SR_EXTRA_CFLAGS], [C11], [-std=gnu11 -std=c11])
SR_CHECK_COMPILE_FLAGS([SR_EXTRA_CFLAGS], [visibility], [-fvisibility=hidden])

SR_ARG_ENABLE_WARNINGS([SR_WFLAGS], [-Wall], [-Wall -Wextra -Wmissing-prototypes])

# Check host characteristics.
AC_C_BIGENDIAN

AC_CHECK_HEADERS([sys/mman.h], [SR_APPEND([sr_deps_avail], [sys_mman_h])])
AC_CHECK_HEADERS([sys/ioctl.h], [SR_APPEND([sr_deps_avail], [sys_ioctl_h])])
AC_CHECK_HEADERS([sys/timerfd.h], [SR_APPEND([sr_deps_avail], [sys_timerfd_h])])

# We need to link against the Winsock2 library for SCPI over TCP.
AS_CASE([$host], [*-mingw*], [SR_EXTRA_LIBS='-lws2_32'], [SR_EXTRA_LIBS=])

# libm (the standard math library) is always needed.
SR_SEARCH_LIBS([SR_EXTRA_LIBS], [pow], [m])

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

########################
##  Hardware drivers  ##
########################

# Keep track of all drivers so we can list them at the end.
SR_VAR_SUMMARY([sr_driver_summary])

# Check whether the sr_deps_avail list contains all of the arguments.
# Unavailable dependencies are collected in sr_deps_missing.
sr_check_driver_deps() {
	sr_deps_missing=
	for sr_dep
	do
		AS_CASE([" $sr_deps_avail "], [*" $sr_dep "*],,
			[SR_APPEND([sr_deps_missing], [', '], [$sr_dep])])
	done
	test -z "$sr_deps_missing" || return 1
}

AC_ARG_ENABLE([all-drivers],
	[AS_HELP_STRING([--enable-all-drivers],
			[enable all drivers by default [default=yes]])],
	[], [enable_all_drivers=yes])

## _SR_DRIVER(Device name, driver-name, var-name, [dependencies...])
m4_define([_SR_DRIVER], [
	AC_ARG_ENABLE([$2],
		[AS_HELP_STRING([--enable-$2], [enable $1 support])],
		[$3=$enableval], [$3=$enable_all_drivers])

	AS_IF([test "x[$]$3" = xyes], [sr_hw_info=yes[]m4_ifval([$4], [
		sr_check_driver_deps $4 \
			|| $3=no sr_hw_info="no (missing: $sr_deps_missing)"
	])], [sr_hw_info='no (disabled)'])
	sr_driver_summary_append "$2" "$sr_hw_info"

	AM_CONDITIONAL([$3], [test "x[$]$3" = xyes])
	AM_COND_IF([$3], [AC_DEFINE([HAVE_$3], [1], [Whether to support $1 device.])])
])

## SR_DRIVER(Device name, driver-name, [dependencies...])
m4_define([SR_DRIVER],
	[_SR_DRIVER([$1], [$2], m4_expand([AS_TR_CPP([HW_$2])]), [$3])])

SR_DRIVER([Agilent DMM], [agilent-dmm], [libserialport])
SR_DRIVER([Appa 55II], [appa-55ii], [libserialport])
SR_DRIVER([ASIX SIGMA/SIGMA2], [asix-sigma], [libftdi])
SR_DRIVER([Atten PPS3xxx], [atten-pps3xxx], [libserialport])
SR_DRIVER([BayLibre ACME], [baylibre-acme], [sys_timerfd_h])
SR_DRIVER([BeagleLogic], [beaglelogic], [sys_mman_h sys_ioctl_h])
SR_DRIVER([Brymen BM86x], [brymen-bm86x], [libusb])
SR_DRIVER([Brymen DMM], [brymen-dmm], [libserialport])
SR_DRIVER([CEM DT-885x], [cem-dt-885x], [libserialport])
SR_DRIVER([Center 3xx], [center-3xx], [libserialport])
SR_DRIVER([ChronoVu LA], [chronovu-la], [libftdi])
SR_DRIVER([Colead SLM], [colead-slm], [libserialport])
SR_DRIVER([Conrad DIGI 35 CPU], [conrad-digi-35-cpu], [libserialport])
SR_DRIVER([DER EE DE-5000], [deree-de5000], [libserialport])
SR_DRIVER([demo], [demo])
SR_DRIVER([Fluke DMM], [fluke-dmm], [libserialport])
SR_DRIVER([fx2lafw], [fx2lafw], [libusb])
SR_DRIVER([GMC MH 1x/2x], [gmc-mh-1x-2x], [libserialport])
SR_DRIVER([Hameg HMO], [hameg-hmo], [libserialport])
SR_DRIVER([Hantek DSO], [hantek-dso], [libusb])
SR_DRIVER([Ikalogic Scanalogic-2], [ikalogic-scanalogic2], [libusb])
SR_DRIVER([Ikalogic Scanaplus], [ikalogic-scanaplus], [libftdi])
SR_DRIVER([Kecheng KC-330B], [kecheng-kc-330b], [libusb])
SR_DRIVER([Lascar EL-USB], [lascar-el-usb], [libusb])
SR_DRIVER([Manson HCS-3xxx], [manson-hcs-3xxx], [libserialport])
SR_DRIVER([maynuo-m97], [maynuo-m97])
SR_DRIVER([MIC 985xx], [mic-985xx], [libserialport])
SR_DRIVER([Motech LPS 30x], [motech-lps-30x], [libserialport])
SR_DRIVER([Norma DMM], [norma-dmm], [libserialport])
SR_DRIVER([OpenBench Logic Sniffer], [openbench-logic-sniffer], [libserialport])
SR_DRIVER([Pipistrello-OLS], [pipistrello-ols], [libftdi])
SR_DRIVER([Rigol DS], [rigol-ds])
SR_DRIVER([Saleae Logic16], [saleae-logic16], [libusb])
SR_DRIVER([SCPI PPS], [scpi-pps])
SR_DRIVER([serial DMM], [serial-dmm], [libserialport])
SR_DRIVER([Sysclk LWLA], [sysclk-lwla], [libusb])
SR_DRIVER([Teleinfo], [teleinfo], [libserialport])
SR_DRIVER([Testo], [testo], [libusb])
SR_DRIVER([Tondaj SL-814], [tondaj-sl-814], [libserialport])
SR_DRIVER([UNI-T DMM], [uni-t-dmm], [libusb])
SR_DRIVER([UNI-T UT32x], [uni-t-ut32x], [libusb])
SR_DRIVER([Victor DMM], [victor-dmm], [libusb])
SR_DRIVER([Yokogawa DL/DLM], [yokogawa-dlm])
SR_DRIVER([ZEROPLUS Logic Cube], [zeroplus-logic-cube], [libusb])

###############################
##  Language bindings setup  ##
###############################

AC_LANG([C++])
SR_ARG_ENABLE_WARNINGS([SR_WXXFLAGS], [-Wall], [-Wall -Wextra])

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
AX_CXX_COMPILE_STDCXX_11(, [optional])
AS_IF([test "x$HAVE_CXX11" != x1],
	[SR_APPEND([sr_cxx_missing], [', '], ['C++11'])])

# The C++ bindings need glibmm.
SR_PKG_CHECK([glibmm], [SR_PKGLIBS_CXX], [glibmm-2.4 >= 2.32.0])
AS_IF([test "x$sr_have_glibmm" != xyes],
	[SR_APPEND([sr_cxx_missing], [', '], [glibmm])])

# The C++ bindings use Doxygen to parse libsigrok symbols.
AC_CHECK_PROG([HAVE_DOXYGEN], [doxygen], [yes], [no])
AS_IF([test "x$HAVE_DOXYGEN" != xyes],
	[SR_APPEND([sr_cxx_missing], [', '], [Doxygen])])

# Python is needed for the C++ bindings.
AM_PATH_PYTHON([2.7],
	[HAVE_PYTHON=yes],
	[HAVE_PYTHON=no
	SR_APPEND([sr_cxx_missing], [', '], [Python])])

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

AS_IF([test "x$BINDINGS_CXX" = xyes],
	[sr_python_missing=],
	[sr_python_missing='C++ bindings'])

# The Python bindings need Python development files.
SR_PKG_CHECK([python_dev], [SR_PKGLIBS_PYTHON],
	[python >= 2.7], [python2 >= 2.7], [python27 >= 2.7])
AS_IF([test "x$sr_have_python_dev" != xyes],
	[SR_APPEND([sr_python_missing], [', '], [Headers])])

# PyGObject is needed for the Python bindings.
SR_PKG_CHECK([pygobject], [SR_PKGLIBS_PYTHON], [pygobject-3.0 >= 3.0.0])
AS_IF([test "x$sr_have_pygobject" != xyes],
	[SR_APPEND([sr_python_missing], [', '], [PyGObject])])

PKG_CHECK_EXISTS([pygobject-3.0 < 3.7.91],
	[AC_DEFINE([PYGOBJECT_FLAGS_SIGNED], [1],
		[Whether last argument to pyg_flags_get_value() is signed.])])

# The Python bindings need the setuptools Python module.
# We'll let it go through even if the AX macro wasn't found,
# as the Python module may still be there.
HAVE_PYMOD_SETUPTOOLS=yes
m4_ifdef([AX_PYTHON_MODULE],
	[AX_PYTHON_MODULE([setuptools])],
	[m4_warn([unsupported], [Missing macro AX_PYTHON_MODULE: no setuptools check])])
AS_IF([test "x$HAVE_PYMOD_SETUPTOOLS" != xyes],
	[SR_APPEND([sr_python_missing], [', '], [setuptools])])

# The Python bindings use SWIG to generate code.
AC_CHECK_PROGS([SWIG], [swig swig3.0 swig2.0])
AS_IF([test "x$SWIG" = x],
	[SR_APPEND([sr_python_missing], [', '], [SWIG])])

AS_IF([test -z "$sr_python_missing"],
	[BINDINGS_PYTHON=$enable_python], [BINDINGS_PYTHON=no])
AM_CONDITIONAL([BINDINGS_PYTHON], [test "x$BINDINGS_PYTHON" = xyes])

####################
##  Java bindings ##
####################

AS_IF([test "x$BINDINGS_CXX" = xyes],
	[sr_java_missing=],
	[sr_java_missing='C++ bindings'])

# The Java bindings use SWIG to generate code.
AS_IF([test "x$SWIG" = x],
	[SR_APPEND([sr_java_missing], [', '], [SWIG])])

# Find Java compiler and JNI includes for Java bindings.
AC_CHECK_PROG([HAVE_JAVAC], [javac], [yes], [no])
AS_IF([test "x$HAVE_JAVAC" = xno],
	[SR_APPEND([sr_java_missing], [', '], [JavaC])])

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
	SR_APPEND([JNI_CPPFLAGS], ["-I$sr_dir"])
done
AC_SUBST([JNI_CPPFLAGS])

sr_save_cppflags=$CPPFLAGS
SR_APPEND([CPPFLAGS], [$JNI_CPPFLAGS])
AC_CHECK_HEADER([jni.h],,
	[SR_APPEND([sr_java_missing], [', '], ['JNI headers'])])
CPPFLAGS=$sr_save_cppflags

AS_IF([test -z "$sr_java_missing"],
	[BINDINGS_JAVA=$enable_java], [BINDINGS_JAVA=no])
AM_CONDITIONAL([BINDINGS_JAVA], [test "x$BINDINGS_JAVA" = xyes])

##############################
##  Finalize configuration  ##
##############################

# Add mandatory dependencies to module list.
SR_APPEND([SR_PKGLIBS], ['libzip >= 0.10'])
AC_SUBST([SR_PKGLIBS])

# Retrieve the compile and link flags for all modules combined.
# Also, bail out at this point if any module dependency is not met.
PKG_CHECK_MODULES([LIBSIGROK], [glib-2.0 >= 2.32.0 $SR_PKGLIBS])
PKG_CHECK_MODULES([CHECK], [$SR_PKGLIBS_CHECK glib-2.0 $SR_PKGLIBS])

# SR_PKGLIBS_CXX may be empty, so only invoke these checks when
# the C++ bindings are enabled.
AM_COND_IF([BINDINGS_CXX], [
	PKG_CHECK_MODULES([LIBSIGROKCXX], [$SR_PKGLIBS_CXX])
	PKG_CHECK_MODULES([PYSIGROK], [$SR_PKGLIBS_PYTHON $SR_PKGLIBS_CXX])
])
sr_glib_version=`$PKG_CONFIG --modversion glib-2.0 2>&AS_MESSAGE_LOG_FD`
sr_libzip_version=`$PKG_CONFIG --modversion libzip 2>&AS_MESSAGE_LOG_FD`

AC_DEFINE_UNQUOTED([CONF_LIBZIP_VERSION], ["$sr_libzip_version"],
	[Build-time version of libzip.])
AC_DEFINE_UNQUOTED([CONF_HOST], ["$host"],
	[The canonical host libsigrok will run on.])

AC_SUBST([FIRMWARE_DIR], ['$(datadir)/sigrok-firmware'])
AC_SUBST([MAKEFLAGS], ['--no-print-directory'])
AC_SUBST([AM_LIBTOOLFLAGS], ['--silent'])

AC_CONFIG_FILES([Makefile libsigrok.pc bindings/cxx/libsigrokcxx.pc])

AC_OUTPUT

# Prepare bindings report messages.
m4_define([SR_PREPARE_BINDING_REPORT], [
	sr_report_$1=
	test -z "$sr_$1_missing" || sr_report_$1=" (missing: $sr_$1_missing)"
	test "x$enable_$1" = xyes || sr_report_$1=' (disabled)'
])
m4_map_args([SR_PREPARE_BINDING_REPORT], [cxx], [python], [java])

cat >&AS_MESSAGE_FD <<_EOF

libsigrok configuration summary:
 - Package version................. $SR_PACKAGE_VERSION
 - Library ABI version............. $SR_LIB_VERSION
 - Prefix.......................... $prefix
 - Building on..................... $build
 - Building for.................... $host
 - Additional C compiler flags..... $SR_EXTRA_CFLAGS
 - C compiler warnings............. $SR_WFLAGS
 - C++ compiler warnings........... $SR_WXXFLAGS

Detected libraries (required):
 - glib-2.0 >= 2.32.0.............. $sr_glib_version
 - libzip >= 0.10.................. $sr_libzip_version

Detected libraries (optional):
$sr_pkglibs_summary
Enabled hardware drivers:
$sr_driver_summary
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
