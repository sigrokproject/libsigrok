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

AM_CPPFLAGS = -I $(top_srcdir)/libsigrok \
	-DFIRMWARE_DIR='"$(FIRMWARE_DIR)"'

lib_LTLIBRARIES = libsigrok.la

libsigrok_la_SOURCES = \
	backend.c \
	datastore.c \
	device.c \
	session.c \
	hwplugin.c \
	filter.c \
	hardware/common/ezusb.c \
	hardware/common/serial.c \
	hardware/openbench-logic-sniffer/ols.c \
	hardware/saleae-logic/saleae-logic.c \
	hardware/zeroplus-logic-cube/analyzer.c \
	hardware/zeroplus-logic-cube/analyzer.h \
	hardware/zeroplus-logic-cube/gl_usb.c \
	hardware/zeroplus-logic-cube/gl_usb.h \
	hardware/zeroplus-logic-cube/zeroplus.c \
	output/output_text.c \
	output/output_vcd.c \
	output/output_gnuplot.c \
	output/output.c

libsigrok_la_LIBADD = $(LIBOBJS)

include_HEADERS = sigrok.h

