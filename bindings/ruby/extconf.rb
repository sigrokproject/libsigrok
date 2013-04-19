##
## This file is part of the libsigrok project.
##
## Copyright (C) 2013 Uwe Hermann <uwe@hermann-uwe.de>
##
## This program is free software; you can redistribute it and/or modify
## it under the terms of the GNU General Public License as published by
## the Free Software Foundation; either version 2 of the License, or
## (at your option) any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program; if not, write to the Free Software
## Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
##

require 'mkmf'

$CFLAGS += " " + `pkg-config --cflags libsigrok`.strip
$LDFLAGS += " " + `pkg-config --libs libsigrok`.strip

have_library("sigrok", "sr_init") or exit(false)
find_header("libsigrok/libsigrok.h") or exit(false)

create_makefile('sigrok_lowlevel')

