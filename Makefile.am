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

ACLOCAL_AMFLAGS = -I autostuff

AM_CPPFLAGS = -DFIRMWARE_DIR='"$(FIRMWARE_DIR)"'

lib_LTLIBRARIES = libsigrok.la

# Backend files
libsigrok_la_SOURCES = \
	backend.c \
	device.c \
	session.c \
	session_file.c \
	session_driver.c \
	drivers.c \
	hwdriver.c \
	trigger.c \
	soft-trigger.c \
	strutil.c \
	log.c \
	version.c \
	error.c \
	std.c

# Input formats
libsigrok_la_SOURCES += \
	input/binary.c \
	input/chronovu_la8.c \
	input/csv.c \
	input/input.c \
	input/vcd.c \
	input/wav.c

# Output formats
libsigrok_la_SOURCES += \
	output/output.c \
	output/analog.c \
	output/ascii.c \
	output/bits.c \
	output/binary.c \
	output/csv.c \
	output/chronovu_la8.c \
	output/gnuplot.c \
	output/hex.c \
	output/ols.c \
	output/vcd.c

# Hardware (common files)
libsigrok_la_SOURCES += \
	hardware/common/scpi.c \
	hardware/common/scpi_tcp.c
if NEED_RPC
libsigrok_la_SOURCES += \
	hardware/common/scpi_vxi.c \
	hardware/common/vxi_clnt.c \
	hardware/common/vxi_xdr.c \
	hardware/common/vxi.h
endif
if NEED_SERIAL
libsigrok_la_SOURCES += \
	hardware/common/serial.c \
	hardware/common/scpi_serial.c
endif
if NEED_USB
libsigrok_la_SOURCES += \
	hardware/common/ezusb.c \
	hardware/common/usb.c \
	hardware/common/scpi_usbtmc_libusb.c
endif
if NEED_VISA
libsigrok_la_SOURCES += \
	hardware/common/scpi_visa.c
endif

# Hardware (DMM parsers)
libsigrok_la_SOURCES += \
	hardware/common/dmm/es519xx.c \
	hardware/common/dmm/fs9721.c \
	hardware/common/dmm/fs9922.c \
	hardware/common/dmm/m2110.c \
	hardware/common/dmm/metex14.c \
	hardware/common/dmm/rs9lcd.c

# Hardware drivers
if HW_AGILENT_DMM
libsigrok_la_SOURCES += \
	hardware/agilent-dmm/api.c \
	hardware/agilent-dmm/agilent-dmm.h \
	hardware/agilent-dmm/sched.c
endif
if HW_APPA_55II
libsigrok_la_SOURCES += \
	hardware/appa-55ii/protocol.h \
	hardware/appa-55ii/protocol.c \
	hardware/appa-55ii/api.c
endif
if HW_ASIX_SIGMA
libsigrok_la_SOURCES += \
	hardware/asix-sigma/asix-sigma.h \
	hardware/asix-sigma/asix-sigma.c
endif
if HW_ATTEN_PPS3XXX
libsigrok_la_SOURCES += \
	hardware/atten-pps3xxx/protocol.h \
	hardware/atten-pps3xxx/protocol.c \
	hardware/atten-pps3xxx/api.c
endif
if HW_BRYMEN_BM86X
libsigrok_la_SOURCES += \
	hardware/brymen-bm86x/protocol.h \
	hardware/brymen-bm86x/protocol.c \
	hardware/brymen-bm86x/api.c
endif
if HW_BRYMEN_DMM
libsigrok_la_SOURCES += \
	hardware/brymen-dmm/parser.c \
	hardware/brymen-dmm/protocol.h \
	hardware/brymen-dmm/protocol.c \
	hardware/brymen-dmm/api.c
endif
if HW_CEM_DT_885X
libsigrok_la_SOURCES += \
	hardware/cem-dt-885x/protocol.h \
	hardware/cem-dt-885x/protocol.c \
	hardware/cem-dt-885x/api.c
endif
if HW_CENTER_3XX
libsigrok_la_SOURCES += \
	hardware/center-3xx/protocol.h \
	hardware/center-3xx/protocol.c \
	hardware/center-3xx/api.c
endif
if HW_CHRONOVU_LA
libsigrok_la_SOURCES += \
	hardware/chronovu-la/protocol.h \
	hardware/chronovu-la/protocol.c \
	hardware/chronovu-la/api.c
endif
if HW_COLEAD_SLM
libsigrok_la_SOURCES += \
	hardware/colead-slm/protocol.h \
	hardware/colead-slm/protocol.c \
	hardware/colead-slm/api.c
endif
if HW_CONRAD_DIGI_35_CPU
libsigrok_la_SOURCES += \
	hardware/conrad-digi-35-cpu/protocol.h \
	hardware/conrad-digi-35-cpu/protocol.c \
	hardware/conrad-digi-35-cpu/api.c
endif
if HW_DEMO
libsigrok_la_SOURCES += \
	hardware/demo/demo.c
endif
if HW_FLUKE_DMM
libsigrok_la_SOURCES += \
	hardware/fluke-dmm/fluke-dmm.h \
	hardware/fluke-dmm/fluke.c \
	hardware/fluke-dmm/api.c
endif
if HW_FX2LAFW
libsigrok_la_SOURCES += \
	hardware/fx2lafw/protocol.h \
	hardware/fx2lafw/protocol.c \
	hardware/fx2lafw/api.c
endif
if HW_GMC_MH_1X_2X
libsigrok_la_SOURCES += \
	hardware/gmc-mh-1x-2x/protocol.h \
	hardware/gmc-mh-1x-2x/protocol.c \
	hardware/gmc-mh-1x-2x/api.c
endif
if HW_HAMEG_HMO
libsigrok_la_SOURCES += \
	hardware/hameg-hmo/protocol.h \
	hardware/hameg-hmo/protocol.c \
	hardware/hameg-hmo/api.c
endif
if HW_HANTEK_DSO
libsigrok_la_SOURCES += \
	hardware/hantek-dso/dso.h \
	hardware/hantek-dso/dso.c \
	hardware/hantek-dso/api.c
endif
if HW_IKALOGIC_SCANALOGIC2
libsigrok_la_SOURCES += \
	hardware/ikalogic-scanalogic2/protocol.h \
	hardware/ikalogic-scanalogic2/protocol.c \
	hardware/ikalogic-scanalogic2/api.c
endif
if HW_IKALOGIC_SCANAPLUS
libsigrok_la_SOURCES += \
	hardware/ikalogic-scanaplus/protocol.h \
	hardware/ikalogic-scanaplus/protocol.c \
	hardware/ikalogic-scanaplus/api.c
endif
if HW_KECHENG_KC_330B
libsigrok_la_SOURCES += \
	hardware/kecheng-kc-330b/protocol.h \
	hardware/kecheng-kc-330b/protocol.c \
	hardware/kecheng-kc-330b/api.c
endif
if HW_LASCAR_EL_USB
libsigrok_la_SOURCES += \
	hardware/lascar-el-usb/protocol.h \
	hardware/lascar-el-usb/protocol.c \
	hardware/lascar-el-usb/api.c
endif
if HW_MANSON_HCS_3XXX
libsigrok_la_SOURCES += \
	hardware/manson-hcs-3xxx/protocol.h \
	hardware/manson-hcs-3xxx/protocol.c \
	hardware/manson-hcs-3xxx/api.c
endif
if HW_MIC_985XX
libsigrok_la_SOURCES += \
	hardware/mic-985xx/protocol.h \
	hardware/mic-985xx/protocol.c \
	hardware/mic-985xx/api.c
endif
if HW_MOTECH_LPS_30X
libsigrok_la_SOURCES += \
	hardware/motech-lps-30x/protocol.h \
	hardware/motech-lps-30x/protocol.c \
	hardware/motech-lps-30x/api.c
endif
if HW_NORMA_DMM
libsigrok_la_SOURCES += \
	hardware/norma-dmm/protocol.h \
	hardware/norma-dmm/protocol.c \
	hardware/norma-dmm/api.c
endif
if HW_OPENBENCH_LOGIC_SNIFFER
libsigrok_la_SOURCES += \
	hardware/openbench-logic-sniffer/protocol.h \
	hardware/openbench-logic-sniffer/protocol.c \
	hardware/openbench-logic-sniffer/api.c
endif
if HW_RIGOL_DS
libsigrok_la_SOURCES += \
	hardware/rigol-ds/protocol.h \
	hardware/rigol-ds/protocol.c \
	hardware/rigol-ds/api.c
endif
if HW_SALEAE_LOGIC16
libsigrok_la_SOURCES += \
	hardware/saleae-logic16/protocol.h \
	hardware/saleae-logic16/protocol.c \
	hardware/saleae-logic16/api.c
endif
if HW_SERIAL_DMM
libsigrok_la_SOURCES += \
	hardware/serial-dmm/protocol.h \
	hardware/serial-dmm/protocol.c \
	hardware/serial-dmm/api.c
endif
if HW_SYSCLK_LWLA
libsigrok_la_SOURCES += \
	hardware/sysclk-lwla/lwla.h \
	hardware/sysclk-lwla/lwla.c \
	hardware/sysclk-lwla/protocol.h \
	hardware/sysclk-lwla/protocol.c \
	hardware/sysclk-lwla/api.c
endif
if HW_TELEINFO
libsigrok_la_SOURCES += \
	hardware/teleinfo/protocol.h \
	hardware/teleinfo/protocol.c \
	hardware/teleinfo/api.c
endif
if HW_TESTO
libsigrok_la_SOURCES += \
	hardware/testo/protocol.h \
	hardware/testo/protocol.c \
	hardware/testo/api.c
endif
if HW_TONDAJ_SL_814
libsigrok_la_SOURCES += \
	hardware/tondaj-sl-814/protocol.h \
	hardware/tondaj-sl-814/protocol.c \
	hardware/tondaj-sl-814/api.c
endif
if HW_UNI_T_DMM
libsigrok_la_SOURCES += \
	hardware/uni-t-dmm/protocol.h \
	hardware/uni-t-dmm/protocol.c \
	hardware/uni-t-dmm/api.c
endif
if HW_UNI_T_UT32X
libsigrok_la_SOURCES += \
	hardware/uni-t-ut32x/protocol.h \
	hardware/uni-t-ut32x/protocol.c \
	hardware/uni-t-ut32x/api.c
endif
if HW_VICTOR_DMM
libsigrok_la_SOURCES += \
	hardware/victor-dmm/protocol.h \
	hardware/victor-dmm/protocol.c \
	hardware/victor-dmm/api.c
endif
if HW_ZEROPLUS_LOGIC_CUBE
libsigrok_la_SOURCES += \
	hardware/zeroplus-logic-cube/analyzer.c \
	hardware/zeroplus-logic-cube/analyzer.h \
	hardware/zeroplus-logic-cube/gl_usb.h \
	hardware/zeroplus-logic-cube/gl_usb.c \
	hardware/zeroplus-logic-cube/protocol.h \
	hardware/zeroplus-logic-cube/protocol.c \
	hardware/zeroplus-logic-cube/api.c
endif

libsigrok_la_LIBADD = $(LIBOBJS)

libsigrok_la_LDFLAGS = $(SR_LIB_LDFLAGS)

library_includedir = $(includedir)/libsigrok
library_include_HEADERS = \
	include/libsigrok/libsigrok.h \
	include/libsigrok/proto.h \
	include/libsigrok/version.h
noinst_HEADERS = libsigrok-internal.h

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libsigrok.pc

EXTRA_DIST = \
	Doxyfile \
	HACKING \
	README.devices \
	contrib/gnuplot_chronovu_la8.gpi \
	contrib/gnuplot_rigol_ds1xx2.gpi \
	contrib/gnuplot_usbeesx.gpi \
	contrib/gnuplot_usbeedx8.gpi \
	contrib/gnuplot_usbeedx16.gpi \
	contrib/sigrok-logo-notext.png \
	contrib/z60_libsigrok.rules

if HAVE_CHECK

TESTS = tests/check_main

check_PROGRAMS = ${TESTS}

tests_check_main_SOURCES = \
	libsigrok.h \
	tests/lib.c \
	tests/lib.h \
	tests/check_main.c \
	tests/check_core.c \
	tests/check_input_all.c \
	tests/check_input_binary.c \
	tests/check_output_all.c \
	tests/check_strutil.c \
	tests/check_version.c \
	tests/check_driver_all.c

tests_check_main_CFLAGS = @check_CFLAGS@

tests_check_main_LDADD = $(top_builddir)/libsigrok.la @check_LIBS@

endif

if BINDINGS_CXX

lib_LTLIBRARIES += bindings/cxx/libsigrokxx.la

bindings_cxx_libsigrokxx_la_SOURCES = bindings/cxx/classes.cpp

bindings_cxx_libsigrokxx_la_LIBADD = -lsigrok $(glibmm_LIBS)

bindings_cxx_libsigrokxx_la_LDFLAGS = $(SR_LIB_LDFLAGS)

library_include_HEADERS += \
	bindings/cxx/include/libsigrok/libsigrok.hpp \
	bindings/cxx/include/libsigrok/enums.hpp

pkgconfig_DATA += bindings/cxx/libsigrokxx.pc

bindings/cxx/classes.lo: bindings/cxx/enums.cpp

bindings/cxx/enums.cpp: bindings/cxx/enums.py bindings/cxx/libsigrok.xml \
		bindings/cxx/ConfigKey_methods.cpp bindings/cxx/QuantityFlag_methods.cpp
	python $<

bindings/cxx/libsigrok.xml: include/libsigrok/libsigrok.h
	gccxml $(CFLAGS) -DGLIB_DISABLE_DEPRECATION_WARNINGS $< -fxml=$@

endif

BUILD_EXTRA =
INSTALL_EXTRA =
CLEAN_EXTRA =

if BINDINGS_PYTHON

python-build: bindings/cxx/libsigrokxx.la
	cd bindings/python && python setup.py build

python-install:
	cd bindings/python && python setup.py install

python-clean:
	cd bindings/python && python setup.py clean --all

BUILD_EXTRA += python-build
INSTALL_EXTRA += python-install
CLEAN_EXTRA += python-clean

endif

all-local: $(BUILD_EXTRA)
install-exec-local: $(INSTALL_EXTRA)
clean-extra: $(CLEAN_EXTRA)

MAINTAINERCLEANFILES = ChangeLog

.PHONY: ChangeLog
ChangeLog:
	git --git-dir $(top_srcdir)/.git log > ChangeLog || touch ChangeLog

dist-hook: ChangeLog

