##
## This file is part of the sigrok project.
##
## Copyright (C) 2011 Uwe Hermann <uwe@hermann-uwe.de>
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

SUBDIRS = \
	asix-sigma \
	common \
	demo \
	link-mso19 \
	openbench-logic-sniffer \
	saleae-logic \
	zeroplus-logic-cube

noinst_LTLIBRARIES = libsigrokhardware.la

libsigrokhardware_la_SOURCES =

libsigrokhardware_la_LIBADD = \
	common/libsigrokhwcommon.la

if LA_ASIX_SIGMA
libsigrokhardware_la_LIBADD += asix-sigma/libsigrokhwasixsigma.la
endif

if LA_DEMO
libsigrokhardware_la_LIBADD += demo/libsigrokhwdemo.la
endif

if LA_LINK_MSO19
libsigrokhardware_la_LIBADD += link-mso19/libsigrokhwlinkmso19.la
endif

if LA_OLS
libsigrokhardware_la_LIBADD += openbench-logic-sniffer/libsigrokhwols.la
endif

if LA_SALEAE_LOGIC
libsigrokhardware_la_LIBADD += saleae-logic/libsigrokhwsaleaelogic.la
endif

if LA_ZEROPLUS_LOGIC_CUBE
libsigrokhardware_la_LIBADD += zeroplus-logic-cube/libsigrokhwzeroplus.la
endif

