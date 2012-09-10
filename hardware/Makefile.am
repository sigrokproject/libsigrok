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
	agilent-dmm \
	alsa \
	asix-sigma \
	chronovu-la8 \
	common \
	demo \
	fluke-dmm \
	fx2lafw \
	genericdmm \
	link-mso19 \
	openbench-logic-sniffer \
	zeroplus-logic-cube \
	hantek-dso

noinst_LTLIBRARIES = libsigrokhardware.la

libsigrokhardware_la_SOURCES =

libsigrokhardware_la_LIBADD = \
	common/libsigrokhwcommon.la

if HW_AGILENT_DMM
libsigrokhardware_la_LIBADD += agilent-dmm/libsigrokhwagilentdmm.la
endif

if LA_ALSA
libsigrokhardware_la_LIBADD += alsa/libsigrokhwalsa.la
endif

if LA_ASIX_SIGMA
libsigrokhardware_la_LIBADD += asix-sigma/libsigrokhwasixsigma.la
endif

if LA_CHRONOVU_LA8
libsigrokhardware_la_LIBADD += chronovu-la8/libsigrokhwchronovula8.la
endif

if LA_DEMO
libsigrokhardware_la_LIBADD += demo/libsigrokhwdemo.la
endif

if HW_FLUKE_DMM
libsigrokhardware_la_LIBADD += fluke-dmm/libsigrokhwflukedmm.la
endif

if LA_FX2LAFW
libsigrokhardware_la_LIBADD += fx2lafw/libsigrokhwfx2lafw.la
endif

if HW_GENERICDMM
libsigrokhardware_la_LIBADD += genericdmm/libsigrokhwgenericdmm.la
endif

if LA_LINK_MSO19
libsigrokhardware_la_LIBADD += link-mso19/libsigrokhwlinkmso19.la
endif

if LA_OLS
libsigrokhardware_la_LIBADD += openbench-logic-sniffer/libsigrokhwols.la
endif

if LA_ZEROPLUS_LOGIC_CUBE
libsigrokhardware_la_LIBADD += zeroplus-logic-cube/libsigrokhwzeroplus.la
endif

if HW_HANTEK_DSO
libsigrokhardware_la_LIBADD += hantek-dso/libsigrokhw_hantek_dso.la
endif

