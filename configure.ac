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

AC_INIT([libsigrok], [0.6.0],
	[sigrok-devel@lists.sourceforge.net],
	[libsigrok], [http://www.sigrok.org])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([autostuff])
AC_CONFIG_HEADERS([config.h include/libsigrok/version.h])

# We require at least automake 1.11 (needed for 'silent rules').
AM_INIT_AUTOMAKE([1.11 -Wall -Werror no-define nostdinc subdir-objects check-news color-tests])
AM_SILENT_RULES([yes])
m4_ifdef([AM_PROG_AR], [AM_PROG_AR])

AC_CANONICAL_HOST

# Checks for programs.
AC_PROG_CC
AC_PROG_CXX
AC_PROG_INSTALL
AC_PROG_LN_S

# Required for per-target flags or subdir-objects with C sources.
AM_PROG_CC_C_O

# Set the standard the C library headers should conform to.
AH_VERBATIM([_POSIX_C_SOURCE], [/* The targeted POSIX standard. */
#ifndef _POSIX_C_SOURCE
# define _POSIX_C_SOURCE 200112L
#endif])

# Get compiler versions.
SR_PROG_VERSION([$CC], [sr_cc_version])
SR_PROG_VERSION([$CXX], [sr_cxx_version])

# Check for optional make features.
SR_PROG_MAKE_ORDER_ONLY

# Initialize libtool.
LT_INIT

# Set up the libsigrok version defines.
SR_PKG_VERSION_SET([SR_PACKAGE_VERSION], [AC_PACKAGE_VERSION])

# Library version for libsigrok (NOT the same as the package version).
# Carefully read the libtool docs before updating these numbers!
# The algorithm for determining which number to change (and how) is nontrivial!
# http://www.gnu.org/software/libtool/manual/libtool.html#Updating-version-info
# Format: current:revision:age.
SR_LIB_VERSION_SET([SR_LIB_VERSION], [4:0:0])

AM_CONDITIONAL([WIN32], [test -z "${host_os##mingw*}" || test -z "${host_os##cygwin*}"])

#############################
##  Optional dependencies  ##
#############################

AC_LANG([C])

# Initialize pkg-config.
# We require at least 0.22, as "Requires.private" behaviour changed there.
PKG_PROG_PKG_CONFIG([0.22])

# Keep track of all checked modules so we can list them at the end.
SR_PKG_CHECK_SUMMARY([sr_pkglibs_summary])

# Collect the pkg-config module names of all dependencies in SR_PKGLIBS.
# These are used to derive the compiler flags and for the "Requires.private"
# field in the generated libsigrok.pc file.
SR_VAR_OPT_PKG([SR_PKGLIBS], [sr_deps_avail])
SR_PKGLIBS_TESTS=
SR_PKGLIBS_CXX=
SR_PKGLIBS_PYTHON=
SR_PKGLIBS_RUBY=
SR_EXTRA_LIBS=
SR_EXTRA_CXX_LIBS=

SR_ARG_OPT_PKG([libserialport], [LIBSERIALPORT], ,
	[libserialport >= 0.1.1])

SR_ARG_OPT_PKG([libftdi], [LIBFTDI], , [libftdi1 >= 1.0])

# pkg-config file names: MinGW/MacOSX: hidapi; Linux: hidapi-hidraw/-libusb
SR_ARG_OPT_PKG([libhidapi], [LIBHIDAPI], ,
	[hidapi >= 0.8.0], [hidapi-hidraw >= 0.8.0], [hidapi-libusb >= 0.8.0])

SR_ARG_OPT_PKG([libbluez], [LIBBLUEZ], , [bluez >= 4.0])

SR_ARG_OPT_PKG([libnettle], [LIBNETTLE], , [nettle])

# FreeBSD comes with an "integrated" libusb-1.0-style USB API.
# This means libusb-1.0 is always available; no need to check for it.
# On Windows, require the latest version we can get our hands on,
# until the new generic event handling has seen an official release.
AS_CASE([$host_os],
	[freebsd*], [sr_libusb_req='libusb-1.0' sr_have_libusb=yes],
	[mingw*], [sr_libusb_req='libusb-1.0 >= 1.0.20'],
	[sr_libusb_req='libusb-1.0 >= 1.0.16'])

SR_ARG_OPT_PKG([libusb], [LIBUSB_1_0], [NEED_USB],
	[$sr_libusb_req])

SR_ARG_OPT_PKG([librevisa], [LIBREVISA], [NEED_VISA],
	[librevisa >= 0.0.20130412])

SR_ARG_OPT_PKG([libgpib], [LIBGPIB], [NEED_GPIB],
	[libgpib])

SR_ARG_OPT_CHECK([libieee1284], [LIBIEEE1284],, [
	sr_save_LIBS=$LIBS
	LIBS="-lieee1284 $LIBS"
	AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <ieee1284.h>]],
				[[(void) ieee1284_open(0, 0, 0);]])],
		[sr_have_libieee1284=yes], [sr_have_libieee1284=no])
	LIBS=$sr_save_LIBS
])
AS_IF([test "x$sr_have_libieee1284" = xyes],
	[SR_PREPEND([SR_EXTRA_LIBS], [-lieee1284])])

SR_ARG_OPT_PKG([libgio], [LIBGIO], , [gio-2.0 >= 2.24.0])

# See if any of the (potentially platform specific) libs are available
# which provide some means of Bluetooth communication.
AS_IF([test "x$sr_have_libbluez" = xyes],
	sr_have_bluetooth=yes, sr_have_bluetooth=no)
AS_IF([test "x$sr_have_bluetooth" = xyes],
	[AC_DEFINE([HAVE_BLUETOOTH], [1], [Specifies whether Bluetooth communication is supported.])])
AS_IF([test "x$sr_have_bluetooth" = xyes],
	[SR_APPEND([sr_deps_avail], [bluetooth_comm])])

AS_IF([test "x$sr_have_libserialport" = xyes -o "x$sr_have_libhidapi" = xyes -o "x$sr_have_bluetooth" = xyes],
	sr_have_serial_comm=yes, sr_have_serial_comm=no)
AS_IF([test "x$sr_have_serial_comm" = xyes],
	[AC_DEFINE([HAVE_SERIAL_COMM], [1], [Specifies whether serial communication is supported.])])
AS_IF([test "x$sr_have_serial_comm" = xyes],
	[SR_APPEND([sr_deps_avail], [serial_comm])])
AM_CONDITIONAL([NEED_SERIAL], [test "x$sr_have_serial_comm" = xyes])

######################
##  Feature checks  ##
######################

# The Check unit testing framework is optional. Disable if not found.
SR_PKG_CHECK([check], [SR_PKGLIBS_TESTS], [check >= 0.9.4])
AM_CONDITIONAL([HAVE_CHECK], [test "x$sr_have_check" = xyes])

# Enable the C99 standard if possible, and enforce the use
# of SR_API to explicitly mark all public API functions.
SR_EXTRA_CFLAGS=
SR_CHECK_COMPILE_FLAGS([SR_EXTRA_CFLAGS], [C99], [-std=c99 -c99 -AC99 -qlanglvl=extc99])
SR_CHECK_COMPILE_FLAGS([SR_EXTRA_CFLAGS], [visibility], [-fvisibility=hidden])

SR_ARG_ENABLE_WARNINGS([SR_WFLAGS], [-Wall], [-Wall -Wextra -Wmissing-prototypes])

# Check host characteristics.
AC_SYS_LARGEFILE
AC_C_BIGENDIAN

AC_CHECK_HEADERS([sys/mman.h], [SR_APPEND([sr_deps_avail], [sys_mman_h])])
AC_CHECK_HEADERS([sys/ioctl.h], [SR_APPEND([sr_deps_avail], [sys_ioctl_h])])
AC_CHECK_HEADERS([sys/timerfd.h], [SR_APPEND([sr_deps_avail], [sys_timerfd_h])])

# We need to link against the Winsock2 library for SCPI over TCP.
AS_CASE([$host_os], [mingw*], [SR_PREPEND([SR_EXTRA_LIBS], [-lws2_32])])

# libm (the standard math library) is always needed.
SR_SEARCH_LIBS([SR_EXTRA_LIBS], [pow], [m])

# RPC is only needed for VXI support.
AC_CACHE_CHECK([for SunRPC support], [sr_cv_have_sunrpc],
	[AC_LINK_IFELSE([AC_LANG_PROGRAM(
			[[#include <rpc/rpc.h>]m4_newline[CLIENT *rpc_test(void);]],
			[[(void) clnt_create("", 0, 0, "");]])],
		[RPC_CFLAGS=""; RPC_LIBS=""; sr_cv_have_sunrpc=yes],
		[sr_cv_have_sunrpc=no])])
PKG_CHECK_MODULES([TIRPC],
	[libtirpc],
	[RPC_CFLAGS=$TIRPC_CFLAGS; SR_PREPEND([SR_EXTRA_LIBS], [$TIRPC_LIBS]); sr_cv_have_tirpc=yes],
	[sr_cv_have_tirpc=no])
AS_IF([test "x$sr_cv_have_sunrpc" = xyes -o "x$sr_cv_have_tirpc" = xyes],
	[sr_cv_have_rpc=yes], [sr_cv_have_rpc=no])

AC_SUBST(RPC_CFLAGS)
AC_SUBST(RPC_LIBS)

AS_IF([test "x$sr_cv_have_rpc" = xyes],
	[AC_DEFINE([HAVE_RPC], [1], [Specifies whether we have RPC support (either by SunRPC or libtirpc).])])

# VXI support is only compiled if RPC support was found.
AM_CONDITIONAL([NEED_RPC], [test "x$sr_cv_have_rpc" = xyes])

# Check for compiler support of 128 bit integers
AC_CHECK_TYPES([__int128_t, __uint128_t], [], [], [])

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
	AM_COND_IF([$3], [AC_DEFINE([HAVE_$3], [1], [Whether to support $1 device.]) AC_DEFINE([HAVE_DRIVERS], [1], [Whether at least one driver is enabled.])])
])

## SR_DRIVER(Device name, driver-name, [dependencies...])
m4_define([SR_DRIVER],
	[_SR_DRIVER([$1], [$2], m4_expand([AS_TR_CPP([HW_$2])]), [$3])])

SR_DRIVER([Agilent DMM], [agilent-dmm], [serial_comm])
SR_DRIVER([Appa 55II], [appa-55ii], [serial_comm])
SR_DRIVER([Arachnid Labs Re:load Pro], [arachnid-labs-re-load-pro], [serial_comm])
SR_DRIVER([ASIX SIGMA/SIGMA2], [asix-sigma], [libftdi])
SR_DRIVER([Atten PPS3xxx], [atten-pps3xxx], [serial_comm])
SR_DRIVER([BayLibre ACME], [baylibre-acme], [sys_timerfd_h])
SR_DRIVER([BeagleLogic], [beaglelogic], [sys_mman_h sys_ioctl_h])
SR_DRIVER([CEM DT-885x], [cem-dt-885x], [serial_comm])
SR_DRIVER([Center 3xx], [center-3xx], [serial_comm])
SR_DRIVER([ChronoVu LA], [chronovu-la], [libusb libftdi])
SR_DRIVER([Colead SLM], [colead-slm], [serial_comm])
SR_DRIVER([Conrad DIGI 35 CPU], [conrad-digi-35-cpu], [serial_comm])
SR_DRIVER([demo], [demo])
SR_DRIVER([DreamSourceLab DSLogic], [dreamsourcelab-dslogic], [libusb])
SR_DRIVER([Fluke 45], [fluke-45])
SR_DRIVER([Fluke DMM], [fluke-dmm], [serial_comm])
SR_DRIVER([FTDI LA], [ftdi-la], [libusb libftdi])
SR_DRIVER([fx2lafw], [fx2lafw], [libusb])
SR_DRIVER([GMC MH 1x/2x], [gmc-mh-1x-2x], [serial_comm])
SR_DRIVER([GW Instek GDS-800], [gwinstek-gds-800], [serial_comm])
SR_DRIVER([GW Instek GPD], [gwinstek-gpd], [serial_comm])
SR_DRIVER([Hameg HMO], [hameg-hmo], [serial_comm])
SR_DRIVER([Hantek 4032L], [hantek-4032l], [libusb])
SR_DRIVER([Hantek 6xxx], [hantek-6xxx], [libusb])
SR_DRIVER([Hantek DSO], [hantek-dso], [libusb])
SR_DRIVER([HP 3457A], [hp-3457a])
SR_DRIVER([HP 3478A], [hp-3478a], [libgpib])
SR_DRIVER([hp-59306a], [hp-59306a])
SR_DRIVER([Hung-Chang DSO-2100], [hung-chang-dso-2100], [libieee1284])
SR_DRIVER([Ikalogic Scanalogic-2], [ikalogic-scanalogic2], [libusb])
SR_DRIVER([Ikalogic Scanaplus], [ikalogic-scanaplus], [libftdi])
SR_DRIVER([IPDBG LA], [ipdbg-la])
SR_DRIVER([ITECH IT8500], [itech-it8500], [serial_comm])
SR_DRIVER([Kecheng KC-330B], [kecheng-kc-330b], [libusb])
SR_DRIVER([KERN scale], [kern-scale], [serial_comm])
SR_DRIVER([Kingst LA2016], [kingst-la2016], [libusb])
SR_DRIVER([Korad KAxxxxP], [korad-kaxxxxp], [serial_comm])
SR_DRIVER([Lascar EL-USB], [lascar-el-usb], [libusb])
SR_DRIVER([LeCroy LogicStudio], [lecroy-logicstudio], [libusb])
SR_DRIVER([LeCroy X-Stream], [lecroy-xstream])
SR_DRIVER([Manson HCS-3xxx], [manson-hcs-3xxx], [serial_comm])
SR_DRIVER([Mastech MS6514], [mastech-ms6514], [serial_comm])
SR_DRIVER([maynuo-m97], [maynuo-m97])
SR_DRIVER([MIC 985xx], [mic-985xx], [serial_comm])
SR_DRIVER([Microchip PICkit2], [microchip-pickit2], [libusb])
SR_DRIVER([Mooshimeter DMM], [mooshimeter-dmm], [bluetooth_comm libgio])
SR_DRIVER([Motech LPS 30x], [motech-lps-30x], [serial_comm])
SR_DRIVER([Norma DMM], [norma-dmm], [serial_comm])
SR_DRIVER([OpenBench Logic Sniffer], [openbench-logic-sniffer], [serial_comm])
SR_DRIVER([PCE PCE-322A], [pce-322a], [serial_comm])
SR_DRIVER([Pipistrello-OLS], [pipistrello-ols], [libftdi])
SR_DRIVER([PSLab Oscilloscope], [pslab-oscilloscope])
SR_DRIVER([RDTech DPSxxxx/DPHxxxx], [rdtech-dps], [serial_comm])
SR_DRIVER([RDTech UMXX], [rdtech-um], [serial_comm])
SR_DRIVER([RDTech TCXX], [rdtech-tc], [serial_comm libnettle])
SR_DRIVER([Rigol DS], [rigol-ds])
SR_DRIVER([Rigol DG], [rigol-dg])
SR_DRIVER([Rohde&Schwarz SME-0x], [rohde-schwarz-sme-0x], [serial_comm])
SR_DRIVER([Saleae Logic16], [saleae-logic16], [libusb])
SR_DRIVER([Saleae Logic Pro], [saleae-logic-pro], [libusb])
SR_DRIVER([SCPI DMM], [scpi-dmm])
SR_DRIVER([SCPI PPS], [scpi-pps])
SR_DRIVER([serial DMM], [serial-dmm], [serial_comm])
SR_DRIVER([serial LCR], [serial-lcr], [serial_comm])
SR_DRIVER([Siglent SDS], [siglent-sds])
SR_DRIVER([Sysclk LWLA], [sysclk-lwla], [libusb])
SR_DRIVER([Sysclk SLA5032], [sysclk-sla5032], [libusb])
SR_DRIVER([Teleinfo], [teleinfo], [serial_comm])
SR_DRIVER([Testo], [testo], [libusb])
SR_DRIVER([Tondaj SL-814], [tondaj-sl-814], [serial_comm])
SR_DRIVER([UNI-T DMM], [uni-t-dmm], [libusb])
SR_DRIVER([UNI-T UT181A], [uni-t-ut181a], [serial_comm])
SR_DRIVER([UNI-T UT32x], [uni-t-ut32x], [serial_comm])
SR_DRIVER([Yokogawa DL/DLM], [yokogawa-dlm])
SR_DRIVER([ZEROPLUS Logic Cube], [zeroplus-logic-cube], [libusb])
SR_DRIVER([ZKETECH EBD-USB], [zketech-ebd-usb], [serial_comm])

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

AC_ARG_ENABLE([ruby],
	[AS_HELP_STRING([--enable-ruby], [build Ruby bindings [default=yes]])],
	[], [enable_ruby=$enable_bindings])

AC_ARG_ENABLE([java],
	[AS_HELP_STRING([--enable-java], [build Java bindings [default=yes]])],
	[], [enable_java=$enable_bindings])

####################
##  C++ bindings  ##
####################

sr_cxx_missing=

# Check if the C++ compiler supports the C++11 standard.
AX_CXX_COMPILE_STDCXX([11], [noext], [optional])
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

# Link C++ code with gnustl_shared if it exists (Android)
SR_SEARCH_LIBS([SR_EXTRA_CXX_LIBS], [__cxa_throw], [gnustl_shared])

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

# Extract major and minor version number of the Python interpreter.
sr_pymajor=${PYTHON_VERSION%%.*}
sr_pyminor=${PYTHON_VERSION#*.}

# The Python bindings need Python development files. Check for either
# Python 3 or Python 2 headers depending on the interpreter version.
SR_PKG_CHECK([python_dev], [SR_PKGLIBS_PYTHON],
	[python = $PYTHON_VERSION],
	[python$sr_pymajor = $PYTHON_VERSION],
	[python$sr_pymajor$sr_pyminor = $PYTHON_VERSION],
	[python-$PYTHON_VERSION = $PYTHON_VERSION])

AS_IF([test "x$sr_have_python_dev" != xyes],
	[SR_APPEND([sr_python_missing], [', '], [Headers])])

# PyGObject is needed for the Python bindings.
SR_PKG_CHECK([pygobject], [SR_PKGLIBS_PYTHON], [pygobject-3.0 >= 3.0.0])
AS_IF([test "x$sr_have_pygobject" != xyes],
	[SR_APPEND([sr_python_missing], [', '], [PyGObject])])

PKG_CHECK_EXISTS([pygobject-3.0 < 3.7.91],
	[AC_DEFINE([PYGOBJECT_FLAGS_SIGNED], [1],
		[Whether last argument to pyg_flags_get_value() is signed.])])

# The Python bindings need the setuptools and numpy Python modules.
# We'll let it go through even if the AX macro wasn't found, as the
# Python modules may still be there.
m4_ifdef([AX_PYTHON_MODULE], [
	AX_PYTHON_MODULE([setuptools])
	AX_PYTHON_MODULE([numpy])
], [
	HAVE_PYMOD_SETUPTOOLS=yes
	HAVE_PYMOD_NUMPY=yes
	m4_warn([unsupported],
		[Missing macro AX_PYTHON_MODULE: no check for setuptools and numpy])
])
AS_IF([test "x$HAVE_PYMOD_SETUPTOOLS" != xyes],
	[SR_APPEND([sr_python_missing], [', '], [setuptools])])
AS_IF([test "x$HAVE_PYMOD_NUMPY" != xyes],
	[SR_APPEND([sr_python_missing], [', '], [numpy])])

# The Python bindings use SWIG to generate code.
AC_CHECK_PROGS([SWIG], [swig swig4.0 swig3.0 swig2.0])
AS_IF([test "x$SWIG" != x],
    AC_MSG_CHECKING([for $SWIG version])
    [SWIG_VERSION=`$SWIG -version 2>&1 | sed -n 's/SWIG Version //p'`]
    AC_MSG_RESULT([$SWIG_VERSION]))
AS_IF([test "x$SWIG" = x],
	[SR_APPEND([sr_python_missing], [', '], [SWIG])])

AS_IF([test -z "$sr_python_missing"],
	[BINDINGS_PYTHON=$enable_python], [BINDINGS_PYTHON=no])
AM_CONDITIONAL([BINDINGS_PYTHON], [test "x$BINDINGS_PYTHON" = xyes])

#####################
##  Ruby bindings  ##
#####################

AS_IF([test "x$BINDINGS_CXX" = xyes],
	[sr_ruby_missing=],
	[sr_ruby_missing='C++ bindings'])

AC_PATH_PROGS(RUBY, ["${RUBY-ruby}"], [])
AS_IF([test "x$RUBY" != x],
	AC_MSG_CHECKING([for Ruby version])
    [RUBY_VERSION=`$RUBY -e 'puts RUBY_VERSION'`]
    AC_MSG_RESULT([$RUBY_VERSION])
    [RUBY_DLEXT=`$RUBY -rrbconfig -e 'puts RbConfig::CONFIG[["DLEXT"]]'`]
    AC_SUBST(RUBY_DLEXT))

AS_IF([test "x$RUBY" = x],
	[SR_APPEND([sr_ruby_missing], [', '], [Ruby])])

# Extract major and minor version number of the Ruby interpreter.
sr_rbmajor=${RUBY_VERSION%%.*}
sr_rbminor=${RUBY_VERSION#*.}
sr_rbminor=${sr_rbminor%%.*}

# The Ruby bindings need Ruby development files.
SR_PKG_CHECK([ruby_dev], [SR_PKGLIBS_RUBY],
	[ruby >= 2.5.0],
	[ruby-$sr_rbmajor.$sr_rbminor >= 2.5.0])

AS_IF([test "x$sr_have_ruby_dev" != xyes],
	[SR_APPEND([sr_ruby_missing], [', '], [Headers])])

# The Ruby bindings use SWIG >= 3.0.8 to generate code.
AS_IF([test "x$SWIG" = x],
	[SR_APPEND([sr_ruby_missing], [', '], [SWIG])],
	[AS_VERSION_COMPARE($SWIG_VERSION, "3.0.8",
		[SR_APPEND([sr_ruby_missing], [', '], ['SWIG >= 3.0.8'])])])

AS_IF([test -z "$sr_ruby_missing"],
	[BINDINGS_RUBY=$enable_ruby], [BINDINGS_RUBY=no])
AM_CONDITIONAL([BINDINGS_RUBY], [test "x$BINDINGS_RUBY" = xyes])

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
PKG_CHECK_MODULES([TESTS], [$SR_PKGLIBS_TESTS glib-2.0 $SR_PKGLIBS])

# SR_PKGLIBS_CXX may be empty, so only invoke these checks when
# the C++ bindings are enabled.
AM_COND_IF([BINDINGS_CXX], [
	PKG_CHECK_MODULES([LIBSIGROKCXX], [$SR_PKGLIBS_CXX])
	PKG_CHECK_MODULES([PYSIGROK], [$SR_PKGLIBS_PYTHON $SR_PKGLIBS_CXX])
	PKG_CHECK_MODULES([RBSIGROK], [$SR_PKGLIBS_RUBY $SR_PKGLIBS_CXX], [AC_SUBST(RBSIGROK_EXTDIR, "lib/$($PKG_CONFIG --variable=sitearch $SR_PKGLIBS_RUBY)/$($PKG_CONFIG --variable=RUBY_BASE_NAME $SR_PKGLIBS_RUBY)/vendor_ruby/$($PKG_CONFIG --variable=ruby_version $SR_PKGLIBS_RUBY)")])
])

# Check for specific libusb features, now that we know the CFLAGS.
AC_LANG([C])
sr_save_cflags=$CFLAGS
sr_save_libs=$LIBS
CFLAGS="$LIBSIGROK_CFLAGS $CFLAGS"
LIBS="$LIBSIGROK_LIBS $LIBS"
AC_CHECK_TYPES([libusb_os_handle],
	[sr_have_libusb_os_handle=yes], [sr_have_libusb_os_handle=no],
	[[#include <libusb.h>]])
AC_CHECK_FUNCS([zip_discard])
LIBS=$sr_save_libs
CFLAGS=$sr_save_cflags

AM_COND_IF([NEED_USB], [AS_CASE([$sr_have_libusb_os_handle:$host_os], [no:mingw*],
	[AC_MSG_ERROR([Windows builds require the event-abstraction branch of libusb])])])

sr_glib_version=`$PKG_CONFIG --modversion glib-2.0 2>&AS_MESSAGE_LOG_FD`
sr_libzip_version=`$PKG_CONFIG --modversion libzip 2>&AS_MESSAGE_LOG_FD`

AC_DEFINE_UNQUOTED([CONF_LIBZIP_VERSION], ["$sr_libzip_version"],
	[Build-time version of libzip.])
AC_DEFINE_UNQUOTED([CONF_HOST], ["$host"],
	[The canonical host libsigrok will run on.])

AC_CONFIG_FILES([Makefile libsigrok.pc bindings/cxx/libsigrokcxx.pc])

AC_OUTPUT

# Prepare bindings report messages.
m4_define([SR_PREPARE_BINDING_REPORT], [
	sr_report_$1=
	test -z "$sr_$1_missing" || sr_report_$1=" (missing: $sr_$1_missing)"
	test "x$enable_$1" = xyes || sr_report_$1=' (disabled)'
])
m4_map_args([SR_PREPARE_BINDING_REPORT], [cxx], [python], [ruby], [java])

cat >&AS_MESSAGE_FD <<_EOF

libsigrok configuration summary:
 - Package version................. $SR_PACKAGE_VERSION
 - Library ABI version............. $SR_LIB_VERSION
 - Prefix.......................... $prefix
 - Building on..................... $build
 - Building for.................... $host
 - Building shared / static........ $enable_shared / $enable_static

Compile configuration:
 - C compiler...................... $CC
 - C compiler version.............. $sr_cc_version
 - C compiler flags................ $CFLAGS
 - Additional C compiler flags..... $SR_EXTRA_CFLAGS
 - C compiler warnings............. $SR_WFLAGS
 - C++ compiler.................... $CXX
 - C++ compiler version............ $sr_cxx_version
 - C++ compiler flags.............. $CXXFLAGS
 - C++ compiler warnings........... $SR_WXXFLAGS
 - Linker flags.................... $LDFLAGS

Detected libraries (required):
 - glib-2.0 >= 2.32.0.............. $sr_glib_version
 - libzip >= 0.10.................. $sr_libzip_version

Detected libraries (optional):
$sr_pkglibs_summary
Enabled hardware drivers:
$sr_driver_summary
Enabled serial communication transports:
  - serial comm ................... $sr_have_serial_comm
  - libserialport ................. $sr_have_libserialport
  - hidapi ........................ $sr_have_libhidapi
  - bluetooth ..................... $sr_have_bluetooth
  - bluez ......................... $sr_have_libbluez

Enabled SCPI backends:
 - TCP............................. yes
 - SunRPC ......................... $sr_cv_have_sunrpc
 - TI-RPC ......................... $sr_cv_have_tirpc
 - RPC............................. $sr_cv_have_rpc
 - serial.......................... $sr_have_serial_comm
 - VISA............................ $sr_have_librevisa
 - GPIB............................ $sr_have_libgpib
 - USBTMC.......................... $sr_have_libusb

Enabled language bindings:
 - C++............................. $BINDINGS_CXX$sr_report_cxx
 - Python.......................... $BINDINGS_PYTHON$sr_report_python
 - Ruby............................ $BINDINGS_RUBY$sr_report_ruby
 - Java............................ $BINDINGS_JAVA$sr_report_java

_EOF

# Emit a warning if the C++ bindings are not being built.
AM_COND_IF([BINDINGS_CXX], [], [
cat >&AS_MESSAGE_FD <<_EOF
===============================================================================
WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING
===============================================================================
===                                                                         ===
===         The libsigrok C++ bindings are not being built since you        ===
===             are missing one or more dependencies (see above)!           ===
===                                                                         ===
===      This means you won't be able to compile frontends that require     ===
===  the C++ bindings (such as PulseView)! You also won't be able to build  ===
===     other bindings and frontends using those (such as sigrok-meter)!    ===
===                                                                         ===
===============================================================================
WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING
===============================================================================

_EOF
])
