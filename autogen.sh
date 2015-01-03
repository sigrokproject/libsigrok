#!/bin/sh
##
## This file is part of the libsigrok project.
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

OS=`uname`

LIBTOOLIZE=libtoolize
ACLOCAL_DIR=

if [ "x$OS" = "xDarwin" ]; then
	LIBTOOLIZE=glibtoolize

	if [ -d /sw/share/aclocal ]; then
		# fink installs aclocal macros here
		ACLOCAL_DIR="-I /sw/share/aclocal"
	elif [ -d /opt/local/share/aclocal ]; then
		# Macports installs aclocal macros here
		ACLOCAL_DIR="-I /opt/local/share/aclocal"
	elif [ -d /usr/local/share/aclocal ]; then
		# Homebrew installs aclocal macros here
		ACLOCAL_DIR="-I /usr/local/share/aclocal"
	elif [ -d /usr/share/aclocal ]; then
		# Xcode installs aclocal macros here
		ACLOCAL_DIR="-I /usr/share/aclocal"
	fi

elif [ "x$OS" = "xMINGW32_NT-5.1" ]; then
	# Windows XP
	ACLOCAL_DIR="-I /usr/local/share/aclocal"
elif [ "x$OS" = "xMINGW32_NT-6.0" ]; then
	# Windows Vista
	ACLOCAL_DIR="-I /usr/local/share/aclocal"
elif [ "x$OS" = "xMINGW32_NT-6.1" ]; then
	# Windows 7
	ACLOCAL_DIR="-I /usr/local/share/aclocal"
fi

echo "Generating build system..."
${LIBTOOLIZE} --install --copy --quiet || exit 1
aclocal ${ACLOCAL_DIR} || exit 1

# Check the version of a specific autoconf macro that tends to cause problems.
CXXMACROVERSION=$(
	grep -B 5 'm4_define(\[_AX_CXX_COMPILE_STDCXX_11_testbody\]' aclocal.m4 |
	sed -nr 's/.*serial[[:space:]]+([[:digit:]]+).*/\1/p'
)
if [ "x$CXXMACROVERSION" = "x" ]; then
	echo "--- Warning: AX_CXX_COMPILE_STDCXX_11 macro not found."
	echo "--- You won't be able to build the language bindings!"
	echo "--- More info: http://sigrok.org/wiki/Building#FAQ"
fi
if [ "x$CXXMACROVERSION" != "x" ] && [ "$CXXMACROVERSION" -lt 4 ]; then
	echo "--- Warning: AX_CXX_COMPILE_STDCXX_11 macro is too old."
	echo "--- (found version $CXXMACROVERSION, at least 4 is required)"
	echo "--- You won't be able to build the language bindings!"
	echo "--- More info: http://sigrok.org/wiki/Building#FAQ"
fi

autoheader || exit 1
automake --add-missing --copy || exit 1
autoconf || exit 1

