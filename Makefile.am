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

ACLOCAL_AMFLAGS = -I m4
AM_LIBTOOLFLAGS = --silent
GNUMAKEFLAGS = --no-print-directory

# distutils/setuptools cause trouble on distcheck. Disable for now.
DISTCHECK_CONFIGURE_FLAGS = --disable-python

FIRMWARE_DIR = $(datadir)/sigrok-firmware

local_includes = -Iinclude -I$(srcdir)/include -I$(srcdir)/src -I.
if BINDINGS_CXX
local_includes += -Ibindings/cxx/include -I$(srcdir)/bindings/cxx/include -Ibindings/cxx
endif
# Do not hard-code the firmware location on Windows.
if WIN32
global_defs =
else
global_defs = -DFIRMWARE_DIR='"$(FIRMWARE_DIR)"'
endif
# Ensure that local include directories are always searched first.
AM_CPPFLAGS = $(local_includes) $(global_defs)

# The tests CFLAGS are a superset of the libsigrok CFLAGS, and the
# python bindings CFLAGS are a superset of the C++ bindings CFLAGS.
AM_CFLAGS = $(SR_EXTRA_CFLAGS) $(SR_WFLAGS) $(TESTS_CFLAGS)
AM_CXXFLAGS = $(SR_WXXFLAGS) $(LIBSIGROKCXX_CFLAGS)

lib_LTLIBRARIES = libsigrok.la

# Backend files
libsigrok_la_SOURCES = \
	src/backend.c \
	src/device.c \
	src/session.c \
	src/session_file.c \
	src/session_driver.c \
	src/hwdriver.c \
	src/trigger.c \
	src/soft-trigger.c \
	src/analog.c \
	src/fallback.c \
	src/resource.c \
	src/strutil.c \
	src/log.c \
	src/version.c \
	src/error.c \
	src/std.c \
	src/sw_limits.c

# Input modules
libsigrok_la_SOURCES += \
	src/input/input.c \
	src/input/binary.c \
	src/input/chronovu_la8.c \
	src/input/csv.c \
	src/input/raw_analog.c \
	src/input/trace32_ad.c \
	src/input/vcd.c \
	src/input/wav.c

# Output modules
libsigrok_la_SOURCES += \
	src/output/output.c \
	src/output/analog.c \
	src/output/ascii.c \
	src/output/bits.c \
	src/output/binary.c \
	src/output/csv.c \
	src/output/chronovu_la8.c \
	src/output/wav.c \
	src/output/gnuplot.c \
	src/output/hex.c \
	src/output/ols.c \
	src/output/srzip.c \
	src/output/vcd.c

# Transform modules
libsigrok_la_SOURCES += \
	src/transform/transform.c \
	src/transform/nop.c \
	src/transform/scale.c \
	src/transform/invert.c

# SCPI support
libsigrok_la_SOURCES += \
	src/scpi.h \
	src/scpi/scpi.c \
	src/scpi/helpers.c \
	src/scpi/scpi_tcp.c
if NEED_RPC
libsigrok_la_SOURCES += \
	src/scpi/scpi_vxi.c \
	src/scpi/vxi_clnt.c \
	src/scpi/vxi_xdr.c \
	src/scpi/vxi.h
endif
if NEED_SERIAL
libsigrok_la_SOURCES += \
	src/serial.c \
	src/scpi/scpi_serial.c
endif
if NEED_USB
libsigrok_la_SOURCES += \
	src/ezusb.c \
	src/usb.c \
	src/scpi/scpi_usbtmc_libusb.c
endif
if NEED_VISA
libsigrok_la_SOURCES += \
	src/scpi/scpi_visa.c
endif
if NEED_GPIB
libsigrok_la_SOURCES += \
	src/scpi/scpi_libgpib.c
endif

# Modbus support
libsigrok_la_SOURCES += \
	src/modbus/modbus.c
if NEED_SERIAL
libsigrok_la_SOURCES += \
	src/modbus/modbus_serial_rtu.c
endif

# Hardware (DMM chip parsers)
libsigrok_la_SOURCES += \
	src/dmm/es519xx.c \
	src/dmm/fs9721.c \
	src/dmm/fs9922.c \
	src/dmm/m2110.c \
	src/dmm/metex14.c \
	src/dmm/rs9lcd.c \
	src/dmm/bm25x.c \
	src/dmm/ut71x.c \
	src/dmm/ut372.c \
	src/dmm/vc870.c \
	src/dmm/dtm0660.c

# Hardware (LCR chip parsers)
if NEED_SERIAL
libsigrok_la_SOURCES += \
	src/lcr/es51919.c
endif

# Hardware (Scale protocol parsers)
libsigrok_la_SOURCES += \
	src/scale/kern.c

# Hardware drivers

# This entry must be placed before all drivers
libsigrok_la_SOURCES += src/hardware/driver_list_start.c

if HW_AGILENT_DMM
libsigrok_la_SOURCES += \
	src/hardware/agilent-dmm/api.c \
	src/hardware/agilent-dmm/agilent-dmm.h \
	src/hardware/agilent-dmm/sched.c
endif
if HW_APPA_55II
libsigrok_la_SOURCES += \
	src/hardware/appa-55ii/protocol.h \
	src/hardware/appa-55ii/protocol.c \
	src/hardware/appa-55ii/api.c
endif
if HW_ARACHNID_LABS_RE_LOAD_PRO
libsigrok_la_SOURCES += \
	src/hardware/arachnid-labs-re-load-pro/protocol.h \
	src/hardware/arachnid-labs-re-load-pro/protocol.c \
	src/hardware/arachnid-labs-re-load-pro/api.c
endif
if HW_ASIX_SIGMA
libsigrok_la_SOURCES += \
	src/hardware/asix-sigma/protocol.h \
	src/hardware/asix-sigma/protocol.c \
	src/hardware/asix-sigma/api.c
endif
if HW_ATTEN_PPS3XXX
libsigrok_la_SOURCES += \
	src/hardware/atten-pps3xxx/protocol.h \
	src/hardware/atten-pps3xxx/protocol.c \
	src/hardware/atten-pps3xxx/api.c
endif
if HW_BAYLIBRE_ACME
libsigrok_la_SOURCES += \
	src/hardware/baylibre-acme/protocol.h \
	src/hardware/baylibre-acme/protocol.c \
	src/hardware/baylibre-acme/api.c \
	src/hardware/baylibre-acme/gpio.h \
	src/hardware/baylibre-acme/gpio.c
endif
if HW_BEAGLELOGIC
libsigrok_la_SOURCES += \
	src/hardware/beaglelogic/beaglelogic.h \
	src/hardware/beaglelogic/protocol.h \
	src/hardware/beaglelogic/protocol.c \
	src/hardware/beaglelogic/api.c
endif
if HW_BRYMEN_BM86X
libsigrok_la_SOURCES += \
	src/hardware/brymen-bm86x/protocol.h \
	src/hardware/brymen-bm86x/protocol.c \
	src/hardware/brymen-bm86x/api.c
endif
if HW_BRYMEN_DMM
libsigrok_la_SOURCES += \
	src/hardware/brymen-dmm/parser.c \
	src/hardware/brymen-dmm/protocol.h \
	src/hardware/brymen-dmm/protocol.c \
	src/hardware/brymen-dmm/api.c
endif
if HW_CEM_DT_885X
libsigrok_la_SOURCES += \
	src/hardware/cem-dt-885x/protocol.h \
	src/hardware/cem-dt-885x/protocol.c \
	src/hardware/cem-dt-885x/api.c
endif
if HW_CENTER_3XX
libsigrok_la_SOURCES += \
	src/hardware/center-3xx/protocol.h \
	src/hardware/center-3xx/protocol.c \
	src/hardware/center-3xx/api.c
endif
if HW_CHRONOVU_LA
libsigrok_la_SOURCES += \
	src/hardware/chronovu-la/protocol.h \
	src/hardware/chronovu-la/protocol.c \
	src/hardware/chronovu-la/api.c
endif
if HW_COLEAD_SLM
libsigrok_la_SOURCES += \
	src/hardware/colead-slm/protocol.h \
	src/hardware/colead-slm/protocol.c \
	src/hardware/colead-slm/api.c
endif
if HW_CONRAD_DIGI_35_CPU
libsigrok_la_SOURCES += \
	src/hardware/conrad-digi-35-cpu/protocol.h \
	src/hardware/conrad-digi-35-cpu/protocol.c \
	src/hardware/conrad-digi-35-cpu/api.c
endif
if HW_DEMO
libsigrok_la_SOURCES += \
	src/hardware/demo/demo.c
endif
if HW_DEREE_DE5000
libsigrok_la_SOURCES += \
	src/hardware/deree-de5000/api.c
endif
if HW_FLUKE_DMM
libsigrok_la_SOURCES += \
	src/hardware/fluke-dmm/fluke-dmm.h \
	src/hardware/fluke-dmm/fluke.c \
	src/hardware/fluke-dmm/api.c
endif
if HW_FTDI_LA
libsigrok_la_SOURCES += \
	src/hardware/ftdi-la/protocol.h \
	src/hardware/ftdi-la/protocol.c \
	src/hardware/ftdi-la/api.c
endif
if HW_FX2LAFW
libsigrok_la_SOURCES += \
	src/hardware/fx2lafw/protocol.h \
	src/hardware/fx2lafw/protocol.c \
	src/hardware/fx2lafw/api.c \
	src/hardware/fx2lafw/dslogic.c \
	src/hardware/fx2lafw/dslogic.h
endif
if HW_GMC_MH_1X_2X
libsigrok_la_SOURCES += \
	src/hardware/gmc-mh-1x-2x/protocol.h \
	src/hardware/gmc-mh-1x-2x/protocol.c \
	src/hardware/gmc-mh-1x-2x/api.c
endif
if HW_GWINSTEK_GDS_800
libsigrok_la_SOURCES += \
	src/hardware/gwinstek-gds-800/protocol.h \
	src/hardware/gwinstek-gds-800/protocol.c \
	src/hardware/gwinstek-gds-800/api.c
endif
if HW_HAMEG_HMO
libsigrok_la_SOURCES += \
	src/hardware/hameg-hmo/protocol.h \
	src/hardware/hameg-hmo/protocol.c \
	src/hardware/hameg-hmo/api.c
endif
if HW_HANTEK_6XXX
libsigrok_la_SOURCES += \
	src/hardware/hantek-6xxx/protocol.h \
	src/hardware/hantek-6xxx/protocol.c \
	src/hardware/hantek-6xxx/api.c
endif
if HW_HANTEK_DSO
libsigrok_la_SOURCES += \
	src/hardware/hantek-dso/dso.h \
	src/hardware/hantek-dso/dso.c \
	src/hardware/hantek-dso/api.c
endif
if HW_HP_3457A
libsigrok_la_SOURCES += \
	src/hardware/hp-3457a/protocol.h \
	src/hardware/hp-3457a/protocol.c \
	src/hardware/hp-3457a/api.c
endif
if HW_HUNG_CHANG_DSO_2100
libsigrok_la_SOURCES += \
	src/hardware/hung-chang-dso-2100/protocol.h \
	src/hardware/hung-chang-dso-2100/protocol.c \
	src/hardware/hung-chang-dso-2100/api.c
endif
if HW_IKALOGIC_SCANALOGIC2
libsigrok_la_SOURCES += \
	src/hardware/ikalogic-scanalogic2/protocol.h \
	src/hardware/ikalogic-scanalogic2/protocol.c \
	src/hardware/ikalogic-scanalogic2/api.c
endif
if HW_IKALOGIC_SCANAPLUS
libsigrok_la_SOURCES += \
	src/hardware/ikalogic-scanaplus/protocol.h \
	src/hardware/ikalogic-scanaplus/protocol.c \
	src/hardware/ikalogic-scanaplus/api.c
endif
if HW_KECHENG_KC_330B
libsigrok_la_SOURCES += \
	src/hardware/kecheng-kc-330b/protocol.h \
	src/hardware/kecheng-kc-330b/protocol.c \
	src/hardware/kecheng-kc-330b/api.c
endif
if HW_KERN_SCALE
libsigrok_la_SOURCES += \
	src/hardware/kern-scale/protocol.h \
	src/hardware/kern-scale/protocol.c \
	src/hardware/kern-scale/api.c
endif
if HW_KORAD_KAXXXXP
libsigrok_la_SOURCES += \
	src/hardware/korad-kaxxxxp/protocol.h \
	src/hardware/korad-kaxxxxp/protocol.c \
	src/hardware/korad-kaxxxxp/api.c
endif
if HW_LASCAR_EL_USB
libsigrok_la_SOURCES += \
	src/hardware/lascar-el-usb/protocol.h \
	src/hardware/lascar-el-usb/protocol.c \
	src/hardware/lascar-el-usb/api.c
endif
if HW_LECROY_LOGICSTUDIO
libsigrok_la_SOURCES += \
	src/hardware/lecroy-logicstudio/protocol.h \
	src/hardware/lecroy-logicstudio/protocol.c \
	src/hardware/lecroy-logicstudio/api.c
endif
if HW_MANSON_HCS_3XXX
libsigrok_la_SOURCES += \
	src/hardware/manson-hcs-3xxx/protocol.h \
	src/hardware/manson-hcs-3xxx/protocol.c \
	src/hardware/manson-hcs-3xxx/api.c
endif
if HW_MAYNUO_M97
libsigrok_la_SOURCES += \
	src/hardware/maynuo-m97/protocol.h \
	src/hardware/maynuo-m97/protocol.c \
	src/hardware/maynuo-m97/api.c
endif
if HW_MIC_985XX
libsigrok_la_SOURCES += \
	src/hardware/mic-985xx/protocol.h \
	src/hardware/mic-985xx/protocol.c \
	src/hardware/mic-985xx/api.c
endif
if HW_MOTECH_LPS_30X
libsigrok_la_SOURCES += \
	src/hardware/motech-lps-30x/protocol.h \
	src/hardware/motech-lps-30x/protocol.c \
	src/hardware/motech-lps-30x/api.c
endif
if HW_NORMA_DMM
libsigrok_la_SOURCES += \
	src/hardware/norma-dmm/protocol.h \
	src/hardware/norma-dmm/protocol.c \
	src/hardware/norma-dmm/api.c
endif
if HW_OPENBENCH_LOGIC_SNIFFER
libsigrok_la_SOURCES += \
	src/hardware/openbench-logic-sniffer/protocol.h \
	src/hardware/openbench-logic-sniffer/protocol.c \
	src/hardware/openbench-logic-sniffer/api.c
endif
if HW_PIPISTRELLO_OLS
libsigrok_la_SOURCES += \
	src/hardware/pipistrello-ols/protocol.h \
	src/hardware/pipistrello-ols/protocol.c \
	src/hardware/pipistrello-ols/api.c
endif
if HW_RIGOL_DS
libsigrok_la_SOURCES += \
	src/hardware/rigol-ds/protocol.h \
	src/hardware/rigol-ds/protocol.c \
	src/hardware/rigol-ds/api.c
endif
if HW_SALEAE_LOGIC16
libsigrok_la_SOURCES += \
	src/hardware/saleae-logic16/protocol.h \
	src/hardware/saleae-logic16/protocol.c \
	src/hardware/saleae-logic16/api.c
endif
if HW_SCPI_PPS
libsigrok_la_SOURCES += \
	src/hardware/scpi-pps/protocol.h \
	src/hardware/scpi-pps/protocol.c \
	src/hardware/scpi-pps/profiles.c \
	src/hardware/scpi-pps/api.c
endif
if HW_SERIAL_DMM
libsigrok_la_SOURCES += \
	src/hardware/serial-dmm/protocol.h \
	src/hardware/serial-dmm/protocol.c \
	src/hardware/serial-dmm/api.c
endif
if HW_SYSCLK_LWLA
libsigrok_la_SOURCES += \
	src/hardware/sysclk-lwla/lwla.h \
	src/hardware/sysclk-lwla/lwla.c \
	src/hardware/sysclk-lwla/lwla1016.c \
	src/hardware/sysclk-lwla/lwla1034.c \
	src/hardware/sysclk-lwla/protocol.h \
	src/hardware/sysclk-lwla/protocol.c \
	src/hardware/sysclk-lwla/api.c
endif
if HW_TELEINFO
libsigrok_la_SOURCES += \
	src/hardware/teleinfo/protocol.h \
	src/hardware/teleinfo/protocol.c \
	src/hardware/teleinfo/api.c
endif
if HW_TESTO
libsigrok_la_SOURCES += \
	src/hardware/testo/protocol.h \
	src/hardware/testo/protocol.c \
	src/hardware/testo/api.c
endif
if HW_TONDAJ_SL_814
libsigrok_la_SOURCES += \
	src/hardware/tondaj-sl-814/protocol.h \
	src/hardware/tondaj-sl-814/protocol.c \
	src/hardware/tondaj-sl-814/api.c
endif
if HW_UNI_T_DMM
libsigrok_la_SOURCES += \
	src/hardware/uni-t-dmm/protocol.h \
	src/hardware/uni-t-dmm/protocol.c \
	src/hardware/uni-t-dmm/api.c
endif
if HW_UNI_T_UT32X
libsigrok_la_SOURCES += \
	src/hardware/uni-t-ut32x/protocol.h \
	src/hardware/uni-t-ut32x/protocol.c \
	src/hardware/uni-t-ut32x/api.c
endif
if HW_VICTOR_DMM
libsigrok_la_SOURCES += \
	src/hardware/victor-dmm/protocol.h \
	src/hardware/victor-dmm/protocol.c \
	src/hardware/victor-dmm/api.c
endif
if HW_YOKOGAWA_DLM
libsigrok_la_SOURCES += \
	src/hardware/yokogawa-dlm/protocol.h \
	src/hardware/yokogawa-dlm/protocol.c \
	src/hardware/yokogawa-dlm/protocol_wrappers.h \
	src/hardware/yokogawa-dlm/protocol_wrappers.c \
	src/hardware/yokogawa-dlm/api.c
endif
if HW_ZEROPLUS_LOGIC_CUBE
libsigrok_la_SOURCES += \
	src/hardware/zeroplus-logic-cube/analyzer.c \
	src/hardware/zeroplus-logic-cube/analyzer.h \
	src/hardware/zeroplus-logic-cube/gl_usb.h \
	src/hardware/zeroplus-logic-cube/gl_usb.c \
	src/hardware/zeroplus-logic-cube/protocol.h \
	src/hardware/zeroplus-logic-cube/protocol.c \
	src/hardware/zeroplus-logic-cube/api.c
endif

# This entry must be placed after all drivers
libsigrok_la_SOURCES += src/hardware/driver_list_end.c

libsigrok_la_LIBADD = $(SR_EXTRA_LIBS) $(LIBSIGROK_LIBS)
libsigrok_la_LDFLAGS = -version-info $(SR_LIB_VERSION) -no-undefined

library_includedir = $(includedir)/libsigrok
library_include_HEADERS = \
	include/libsigrok/libsigrok.h \
	include/libsigrok/proto.h
nodist_library_include_HEADERS = \
	include/libsigrok/version.h
noinst_HEADERS = src/libsigrok-internal.h

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libsigrok.pc

EXTRA_DIST = \
	Doxyfile \
	HACKING \
	README.devices \
	bindings/cxx/ConfigKey_methods.cpp \
	bindings/cxx/ConfigKey_methods.hpp \
	bindings/cxx/ConfigKey_methods.i \
	bindings/cxx/Doxyfile \
	bindings/cxx/QuantityFlag_methods.cpp \
	bindings/cxx/QuantityFlag_methods.hpp \
	bindings/cxx/enums.py \
	bindings/python/Doxyfile \
	bindings/python/setup.py \
	bindings/python/sigrok/__init__.py \
	bindings/python/sigrok/core/__init__.py \
	bindings/python/sigrok/core/classes.i \
	bindings/ruby/classes.i \
	bindings/java/Doxyfile \
	bindings/java/org/sigrok/core/classes/classes.i \
	bindings/java/org/sigrok/core/interfaces/DatafeedCallback.java \
	bindings/java/org/sigrok/core/interfaces/LogCallback.java \
	bindings/swig/classes.i \
	bindings/swig/doc.py \
	bindings/swig/templates.i \
	contrib/gnuplot_chronovu_la8.gpi \
	contrib/gnuplot_rigol_ds1xx2.gpi \
	contrib/gnuplot_usbeesx.gpi \
	contrib/gnuplot_usbeedx8.gpi \
	contrib/gnuplot_usbeedx16.gpi \
	contrib/sigrok-logo-notext.png \
	contrib/z60_libsigrok.rules

if HAVE_CHECK
TESTS = tests/main
check_PROGRAMS = ${TESTS}
endif

tests_main_SOURCES = \
	include/libsigrok/libsigrok.h \
	tests/lib.c \
	tests/lib.h \
	tests/main.c \
	tests/core.c \
	tests/input_all.c \
	tests/input_binary.c \
	tests/output_all.c \
	tests/transform_all.c \
	tests/session.c \
	tests/strutil.c \
	tests/version.c \
	tests/driver_all.c \
	tests/device.c \
	tests/trigger.c \
	tests/analog.c

tests_main_LDADD = libsigrok.la $(SR_EXTRA_LIBS) $(TESTS_LIBS)

BUILD_EXTRA =
INSTALL_EXTRA =
UNINSTALL_EXTRA =
CLEAN_EXTRA =

if BINDINGS_CXX

lib_LTLIBRARIES += bindings/cxx/libsigrokcxx.la

bindings_cxx_libsigrokcxx_la_SOURCES = bindings/cxx/classes.cpp

bindings_cxx_libsigrokcxx_la_LIBADD = libsigrok.la $(SR_EXTRA_LIBS) $(LIBSIGROKCXX_LIBS)
bindings_cxx_libsigrokcxx_la_LDFLAGS = -version-info $(SR_LIB_VERSION) -no-undefined

bindings_cxx_libsigrokcxx_la_includedir = $(includedir)/libsigrokcxx
bindings_cxx_libsigrokcxx_la_include_HEADERS = \
	bindings/cxx/include/libsigrokcxx/libsigrokcxx.hpp
nodist_bindings_cxx_libsigrokcxx_la_include_HEADERS = \
	bindings/cxx/include/libsigrokcxx/enums.hpp

pkgconfig_DATA += bindings/cxx/libsigrokcxx.pc

doxy/xml/index.xml: include/libsigrok/libsigrok.h
	$(AM_V_GEN)cd $(srcdir) && BUILDDIR=$(abs_builddir)/ doxygen Doxyfile 2>/dev/null

bindings/swig/enums.i: bindings/cxx/enums.timestamp
bindings/cxx/enums.cpp: bindings/cxx/enums.timestamp
bindings/cxx/include/libsigrokcxx/enums.hpp: bindings/cxx/enums.timestamp

bindings/cxx/enums.timestamp: $(srcdir)/bindings/cxx/enums.py doxy/xml/index.xml \
		bindings/cxx/ConfigKey_methods.cpp bindings/cxx/QuantityFlag_methods.cpp
	$(AM_V_GEN)$(PYTHON) $(srcdir)/bindings/cxx/enums.py doxy/xml/index.xml
	$(AM_V_at)touch $@

bindings/cxx/classes.lo: bindings/cxx/classes.cpp bindings/cxx/enums.cpp \
		$(library_include_HEADERS) $(nodist_library_include_HEADERS)

cxx-clean:
	rm -rf doxy/
	rm -rf bindings/cxx/doxy/
	rm -f bindings/swig/enums.i
	rm -f bindings/cxx/enums.cpp
	rm -f bindings/cxx/include/libsigrokcxx/enums.hpp
	rm -f bindings/cxx/enums.timestamp

CLEAN_EXTRA += cxx-clean

endif

CPPXMLDOC = bindings/cxx/doxy/xml/index.xml

$(CPPXMLDOC): bindings/cxx/include/libsigrokcxx/libsigrokcxx.hpp \
		bindings/cxx/enums.timestamp
	$(AM_V_GEN)cd $(srcdir)/bindings/cxx && BUILDDIR=$(abs_builddir)/bindings/cxx/ doxygen Doxyfile 2>/dev/null

# Macro definitions to be used by the SWIG parser.
swig_defs = -Dnoexcept= -Dprivate=protected -DG_GNUC_BEGIN_IGNORE_DEPRECATIONS= -DG_GNUC_END_IGNORE_DEPRECATIONS=

if BINDINGS_PYTHON

PDIR = bindings/python
PDOC_START = bindings/python/sigrok/core/doc_start.i
PDOC_END = bindings/python/sigrok/core/doc_end.i

setup_vars = VERSION='$(PACKAGE_VERSION)' CC='$(CXX)' CFLAGS='$(CXXFLAGS) $(SR_WXXFLAGS) $(PYSIGROK_CFLAGS)' CXXFLAGS='$(CXXFLAGS) $(SR_WXXFLAGS) $(PYSIGROK_CFLAGS)' LDADD='$(PYSIGROK_LIBS)'
setup_quiet = --quiet
setup_py = $(PYTHON) $(srcdir)/$(PDIR)/setup.py $(setup_vars) $(setup_quiet)

$(PDOC_START): $(srcdir)/bindings/swig/doc.py $(CPPXMLDOC)
	$(AM_V_at)test -d $(PDIR)/sigrok/core || $(MKDIR_P) $(PDIR)/sigrok/core
	$(AM_V_GEN)$(PYTHON) $(srcdir)/bindings/swig/doc.py python $(CPPXMLDOC) start > $@

$(PDOC_END): $(srcdir)/bindings/swig/doc.py $(CPPXMLDOC)
	$(AM_V_at)test -d $(PDIR)/sigrok/core || $(MKDIR_P) $(PDIR)/sigrok/core
	$(AM_V_GEN)$(PYTHON) $(srcdir)/bindings/swig/doc.py python $(CPPXMLDOC) end > $@

python-build: $(PDIR)/timestamp

$(PDIR)/timestamp: $(PDIR)/sigrok/core/classes.i \
		bindings/swig/classes.i bindings/swig/templates.i \
		bindings/swig/enums.i $(PDOC_START) $(PDOC_END) \
		$(library_include_HEADERS) \
		$(nodist_library_include_HEADERS) \
		$(bindings_cxx_libsigrokcxx_la_include_HEADERS) \
		$(nodist_bindings_cxx_libsigrokcxx_la_include_HEADERS) \
		@ORDER@ bindings/cxx/libsigrokcxx.la
	$(AM_V_at)$(setup_py) clean --all 2>/dev/null
	$(AM_V_GEN)$(setup_py) build_ext --swig "$(SWIG)" --swig-opts '$(swig_defs)' build_py
	$(AM_V_at): >$@

python-install:
	$(AM_V_at)$(MKDIR_P) "$(DESTDIR)$(prefix)" "$(DESTDIR)$(exec_prefix)"
	destdir='$(DESTDIR)'; $(setup_py) install $${destdir:+"--root=$$destdir"} \
		--prefix "$(prefix)" --exec-prefix "$(exec_prefix)"

python-clean:
	-$(AM_V_at)rm -f $(PDIR)/timestamp
	-$(AM_V_at)rm -fr $(PDIR)/doxy
	-$(AM_V_at)$(setup_py) clean --all 2>/dev/null

python-doc:
	$(AM_V_at)cd $(srcdir)/$(PDIR) && BUILDDIR="$(abs_builddir)/$(PDIR)/" doxygen Doxyfile 2>/dev/null

BUILD_EXTRA += python-build
INSTALL_EXTRA += python-install
CLEAN_EXTRA += python-clean

endif

if BINDINGS_RUBY

RDIR = bindings/ruby
RDOC = $(RDIR)/doc.i
RWRAP = $(RDIR)/classes_wrap.cpp
ROBJ = $(RWRAP:.cpp=.o)
REXT = $(RDIR)/sigrok.$(RUBY_DLEXT)

$(RDOC): $(srcdir)/bindings/swig/doc.py $(CPPXMLDOC)
	$(AM_V_at)test -d $(RDIR) || $(MKDIR_P) $(RDIR)
	$(AM_V_GEN)$(PYTHON) $(srcdir)/bindings/swig/doc.py ruby $(CPPXMLDOC) > $@

$(RWRAP): $(RDIR)/classes.i $(RDOC) \
		bindings/swig/classes.i bindings/swig/templates.i \
		bindings/swig/enums.i
	$(AM_V_GEN)$(SWIG) -ruby -c++ -Ibindings -Ibindings/cxx/include -I$(RDIR) $(local_includes) $(swig_defs) -o $@ $<

$(ROBJ): $(RWRAP) \
		$(library_include_HEADERS) \
		$(nodist_library_include_HEADERS) \
		$(bindings_cxx_libsigrokcxx_la_include_HEADERS) \
		$(nodist_bindings_cxx_libsigrokcxx_la_include_HEADERS)
	$(AM_V_CXX)$(CXX) $(RBSIGROK_CFLAGS) -I. -Iinclude -Ibindings/cxx/include $(local_includes) -fPIC -o $@ -c $<

$(REXT): $(ROBJ) @ORDER@ bindings/cxx/libsigrokcxx.la
	$(AM_V_CXXLD)$(CXX) -shared -o $@ $< -lsigrokcxx -Lbindings/cxx/.libs $(RBSIGROK_LIBS)

ruby-build: $(REXT)

ruby-install: $(REXT)
	$(INSTALL) -d $(DESTDIR)$(prefix)/$(RBSIGROK_EXTDIR)
	$(INSTALL) $< $(DESTDIR)$(prefix)/$(RBSIGROK_EXTDIR)

ruby-uninstall:
	rm -f $(DESTDIR)$(prefix)/$(RBSIGROK_EXTDIR)/sigrok.$(RUBY_DLEXT)

ruby-clean:
	-$(AM_V_at)rm -fr $(RDIR)/doc
	-$(AM_V_at)rm -f $(REXT) $(ROBJ) $(RWRAP) $(RDOC)

ruby-doc: $(RWRAP)
	$(AM_V_at)yard doc -o $(RDIR)/doc $<

BUILD_EXTRA += ruby-build
INSTALL_EXTRA += ruby-install
UNINSTALL_EXTRA += ruby-uninstall
CLEAN_EXTRA += ruby-clean

endif

if BINDINGS_JAVA

JDIR = bindings/java
JPKG = org/sigrok/core
JCLS = $(JDIR)/$(JPKG)/classes
JINT = $(JDIR)/$(JPKG)/interfaces
JSRC = $(JCLS)/*.java $(srcdir)/$(JINT)/*.java
JSWG = $(JCLS)/classes.i
JDOC = $(JCLS)/doc.i
JCXX = $(JCLS)/classes_wrap.cxx
JLIB = $(JDIR)/libsigrok_java_core_classes.so
JJAR = $(JDIR)/sigrok-core.jar

java_cleanfiles = $(JCXX) $(JCLS)/*.java $(JCLS)/*.class $(JINT)/*.class $(JJAR) $(JLIB)

java-build: $(JJAR) $(JLIB)

$(JDOC): $(srcdir)/bindings/swig/doc.py $(CPPXMLDOC)
	$(AM_V_at)test -d $(JCLS) || $(MKDIR_P) $(JCLS)
	$(AM_V_GEN)$(PYTHON) $(srcdir)/bindings/swig/doc.py java $(CPPXMLDOC) > $@

$(JCXX): $(srcdir)/$(JSWG) $(JDOC) bindings/swig/classes.i \
		bindings/swig/templates.i bindings/swig/enums.i \
		$(bindings_cxx_libsigrokcxx_la_include_HEADERS) \
		$(nodist_bindings_cxx_libsigrokcxx_la_include_HEADERS)
	-$(AM_V_at)rm -f $(java_cleanfiles)
	$(AM_V_GEN)$(SWIG) -c++ $(swig_defs) \
		-java -package org.sigrok.core.classes \
		-Ibindings -I$(JCLS) $(local_includes) -I$(srcdir) $(JNI_CPPFLAGS) \
		-outdir $(JCLS) -o $@ $(srcdir)/$(JSWG)

$(JJAR): $(JCXX)
	$(AM_V_GEN)$(JAVAC) -d $(JDIR) $(JSRC)
	$(AM_V_at)jar cf $(JJAR) -C $(JDIR) $(JPKG)

$(JLIB): $(JCXX) \
		$(library_include_HEADERS) $(nodist_library_include_HEADERS) \
		$(bindings_cxx_libsigrokcxx_la_include_HEADERS) \
		$(nodist_bindings_cxx_libsigrokcxx_la_include_HEADERS) \
		@ORDER@ bindings/cxx/libsigrokcxx.la
	$(AM_V_GEN)$(CXXCOMPILE) $(JNI_CPPFLAGS) -L.libs -Lbindings/cxx/.libs \
		-fno-strict-aliasing -fPIC -shared $(JCLS)/classes_wrap.cxx \
		-lsigrokcxx $(LIBSIGROKCXX_LIBS) -o $(JLIB)

java-install:
	$(INSTALL) -d $(DESTDIR)$(libdir)/jni
	$(INSTALL) $(JLIB) $(DESTDIR)$(libdir)/jni
	$(INSTALL) -d $(DESTDIR)$(datadir)/java
	$(INSTALL) $(JJAR) $(DESTDIR)$(datadir)/java

java-uninstall:
	-rm -f $(DESTDIR)$(datadir)/java/sigrok-core.jar
	-rm -f $(DESTDIR)$(libdir)/jni/libsigrok_java_core_classes.so

java-clean:
	-$(AM_V_at)rm -f $(java_cleanfiles) $(JDOC)
	-$(AM_V_at)rm -fr $(JDIR)/doxy

java-doc:
	$(AM_V_at)cd $(srcdir)/$(JDIR) && BUILDDIR="$(abs_builddir)/$(JDIR)/" doxygen Doxyfile

BUILD_EXTRA += java-build
INSTALL_EXTRA += java-install
UNINSTALL_EXTRA += java-uninstall
CLEAN_EXTRA += java-clean

endif

all-local: $(BUILD_EXTRA)
install-exec-local: $(INSTALL_EXTRA)
uninstall-local: $(UNINSTALL_EXTRA)
clean-local: $(CLEAN_EXTRA)

.PHONY: dist-changelog

dist-hook: dist-changelog

dist-changelog:
	$(AM_V_at)if test ! -d '$(top_srcdir)/.git'; then \
		cp -f '$(top_srcdir)/ChangeLog' "$(top_distdir)/ChangeLog"; \
	elif git -C '$(top_srcdir)' log >.ChangeLog.tmp; then \
		mv -f .ChangeLog.tmp "$(top_distdir)/ChangeLog"; \
	else \
		rm -f .ChangeLog.tmp; exit 1; \
	fi

