##
## This file is part of the libsigrok project.
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

if HW_LINK_MSO19

# Local lib, this is NOT meant to be installed!
noinst_LTLIBRARIES = libsigrokhwlinkmso19.la

libsigrokhwlinkmso19_la_SOURCES = \
	api.c \
	protocol.c \
	protocol.h 
#link-mso19.c 

libsigrokhwlinkmso19_la_CFLAGS = \
	-I$(top_srcdir)

endif

