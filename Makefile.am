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

ACLOCAL_AMFLAGS = -I autostuff

AM_CPPFLAGS = -I$(top_srcdir)

SUBDIRS = contrib hardware input output

lib_LTLIBRARIES = libsigrok.la

libsigrok_la_SOURCES = \
	backend.c \
	datastore.c \
	device.c \
	session.c \
	session_file.c \
	session_driver.c \
	hwdriver.c \
	filter.c \
	strutil.c \
	log.c \
	version.c

libsigrok_la_LIBADD = \
	$(LIBOBJS) \
	hardware/libsigrokhardware.la \
	input/libsigrokinput.la \
	output/libsigrokoutput.la

libsigrok_la_LDFLAGS = $(SR_LIB_LDFLAGS)

library_includedir = $(includedir)/libsigrok
library_include_HEADERS = libsigrok.h proto.h version.h
noinst_HEADERS = libsigrok-internal.h

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libsigrok.pc

EXTRA_DIST = Doxyfile

MAINTAINERCLEANFILES = ChangeLog

.PHONY: ChangeLog
ChangeLog:
	git --git-dir $(top_srcdir)/../.git log > ChangeLog || touch ChangeLog

dist-hook: ChangeLog

