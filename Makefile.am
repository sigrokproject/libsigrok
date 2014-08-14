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

AM_CPPFLAGS = -I$(srcdir)/src -DFIRMWARE_DIR='"$(FIRMWARE_DIR)"'

lib_LTLIBRARIES = libsigrok.la

# Backend files
libsigrok_la_SOURCES = \
	src/backend.c \
	src/device.c \
	src/session.c \
	src/session_file.c \
	src/session_driver.c \
	src/drivers.c \
	src/hwdriver.c \
	src/trigger.c \
	src/soft-trigger.c \
	src/strutil.c \
	src/log.c \
	src/version.c \
	src/error.c \
	src/std.c

# Input modules
libsigrok_la_SOURCES += \
	src/input/binary.c \
	src/input/chronovu_la8.c \
	src/input/csv.c \
	src/input/input.c \
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
	src/output/vcd.c

# SCPI support
libsigrok_la_SOURCES += \
	src/scpi/scpi.c \
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

# Hardware (DMM chip parsers)
libsigrok_la_SOURCES += \
	src/dmm/es519xx.c \
	src/dmm/fs9721.c \
	src/dmm/fs9922.c \
	src/dmm/m2110.c \
	src/dmm/metex14.c \
	src/dmm/rs9lcd.c \
	src/dmm/bm25x.c

# Hardware drivers
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
if HW_ASIX_SIGMA
libsigrok_la_SOURCES += \
	src/hardware/asix-sigma/asix-sigma.h \
	src/hardware/asix-sigma/asix-sigma.c
endif
if HW_ATTEN_PPS3XXX
libsigrok_la_SOURCES += \
	src/hardware/atten-pps3xxx/protocol.h \
	src/hardware/atten-pps3xxx/protocol.c \
	src/hardware/atten-pps3xxx/api.c
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
if HW_FLUKE_DMM
libsigrok_la_SOURCES += \
	src/hardware/fluke-dmm/fluke-dmm.h \
	src/hardware/fluke-dmm/fluke.c \
	src/hardware/fluke-dmm/api.c
endif
if HW_FX2LAFW
libsigrok_la_SOURCES += \
	src/hardware/fx2lafw/protocol.h \
	src/hardware/fx2lafw/protocol.c \
	src/hardware/fx2lafw/api.c
endif
if HW_GMC_MH_1X_2X
libsigrok_la_SOURCES += \
	src/hardware/gmc-mh-1x-2x/protocol.h \
	src/hardware/gmc-mh-1x-2x/protocol.c \
	src/hardware/gmc-mh-1x-2x/api.c
endif
if HW_HAMEG_HMO
libsigrok_la_SOURCES += \
	src/hardware/hameg-hmo/protocol.h \
	src/hardware/hameg-hmo/protocol.c \
	src/hardware/hameg-hmo/api.c
endif
if HW_HANTEK_DSO
libsigrok_la_SOURCES += \
	src/hardware/hantek-dso/dso.h \
	src/hardware/hantek-dso/dso.c \
	src/hardware/hantek-dso/api.c
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
if HW_LASCAR_EL_USB
libsigrok_la_SOURCES += \
	src/hardware/lascar-el-usb/protocol.h \
	src/hardware/lascar-el-usb/protocol.c \
	src/hardware/lascar-el-usb/api.c
endif
if HW_MANSON_HCS_3XXX
libsigrok_la_SOURCES += \
	src/hardware/manson-hcs-3xxx/protocol.h \
	src/hardware/manson-hcs-3xxx/protocol.c \
	src/hardware/manson-hcs-3xxx/api.c
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

libsigrok_la_LIBADD = $(LIBOBJS)

libsigrok_la_LDFLAGS = $(SR_LIB_LDFLAGS)

library_includedir = $(includedir)/libsigrok
library_include_HEADERS = \
	include/libsigrok/libsigrok.h \
	include/libsigrok/proto.h \
	include/libsigrok/version.h
noinst_HEADERS = src/libsigrok-internal.h

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
	include/libsigrok/libsigrok.h \
	tests/lib.c \
	tests/lib.h \
	tests/check_main.c \
	tests/check_core.c \
	tests/check_input_all.c \
	tests/check_input_binary.c \
	tests/check_output_all.c \
	tests/check_session.c \
	tests/check_strutil.c \
	tests/check_version.c \
	tests/check_driver_all.c

tests_check_main_CFLAGS = @check_CFLAGS@

tests_check_main_LDADD = $(top_builddir)/libsigrok.la @check_LIBS@

endif

BUILD_EXTRA =
INSTALL_EXTRA =
CLEAN_EXTRA =

if BINDINGS_CXX

lib_LTLIBRARIES += bindings/cxx/libsigrokxx.la

bindings_cxx_libsigrokxx_la_SOURCES = bindings/cxx/classes.cpp

bindings_cxx_libsigrokxx_la_LIBADD = libsigrok.la $(glibmm_LIBS)

bindings_cxx_libsigrokxx_la_LDFLAGS = $(SR_LIB_LDFLAGS)

library_include_HEADERS += \
	bindings/cxx/include/libsigrok/libsigrok.hpp \
	bindings/cxx/include/libsigrok/enums.hpp

pkgconfig_DATA += bindings/cxx/libsigrokxx.pc

doxy/xml/index.xml: include/libsigrok/libsigrok.h
	$(AM_V_GEN)cd $(srcdir) && BUILDDIR=$(abs_builddir)/ doxygen Doxyfile 2>/dev/null

bindings/cxx/enums.cpp: bindings/cxx/enums.timestamp

bindings/cxx/include/libsigrok/enums.hpp: bindings/cxx/enums.timestamp

bindings/cxx/enums.timestamp: bindings/cxx/enums.py doxy/xml/index.xml \
		bindings/cxx/ConfigKey_methods.cpp bindings/cxx/QuantityFlag_methods.cpp
	$(AM_V_GEN)python $< doxy/xml/index.xml
	$(AM_V_at)touch $@

bindings/cxx/classes.lo: bindings/cxx/classes.cpp bindings/cxx/enums.cpp \
		$(library_include_HEADERS)

cxx-clean:
	rm -rf doxy/
	rm -rf bindings/cxx/doxy/
	rm -f bindings/cxx/enums.cpp
	rm -f bindings/cxx/include/libsigrok/enums.hpp
	rm -f bindings/cxx/enums.timestamp

CLEAN_EXTRA += cxx-clean

endif

CPPXMLDOC = bindings/cxx/doxy/xml/index.xml

$(CPPXMLDOC): bindings/cxx/include/libsigrok/libsigrok.hpp \
		bindings/cxx/enums.timestamp
	$(AM_V_GEN)cd $(srcdir)/bindings/cxx && BUILDDIR=$(abs_builddir)/bindings/cxx/ doxygen Doxyfile 2>/dev/null

if BINDINGS_PYTHON

PDIR = bindings/python
PDOC = bindings/python/sigrok/core/doc.i

$(PDOC): bindings/swig/doc.py $(CPPXMLDOC)
	$(AM_V_GEN)python bindings/swig/doc.py python $(CPPXMLDOC) > $@

python-build: $(PDIR)/timestamp

python-quietclean:
	$(AM_V_at)cd $(PDIR) && python setup.py --quiet clean --all 3>&1 1>&2 2>&3 \
		| grep -v "can.t clean it"; true

$(PDIR)/timestamp: bindings/cxx/libsigrokxx.la $(PDIR)/sigrok/core/classes.i \
		bindings/swig/classes.i $(PDOC) $(library_include_HEADERS)
	$(AM_V_at)$(MAKE) python-quietclean
	$(AM_V_GEN)cd $(PDIR) && python setup.py --quiet build 3>&1 1>&2 2>&3 \
		| grep -v "command line option.*Wstrict-prototypes"; true
	$(AM_V_at)touch $(PDIR)/timestamp

python-install:
	cd $(PDIR) && python setup.py --quiet install --prefix $(prefix)

python-clean:
	$(AM_V_at)$(MAKE) python-quietclean
	$(AM_V_at)rm -f $(PDIR)/timestamp
	$(AM_V_at)rm -rf $(PDIR)/doxy/

python-doc:
	$(AM_V_at)cd $(PDIR) && doxygen Doxyfile 2>/dev/null

BUILD_EXTRA += python-build
INSTALL_EXTRA += python-install
CLEAN_EXTRA += python-clean

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

java-build: $(JJAR) $(JLIB)

$(JDOC): bindings/swig/doc.py $(CPPXMLDOC)
	$(AM_V_at)test -d $(JCLS) || mkdir -p $(JCLS)
	$(AM_V_GEN)python $< java $(CPPXMLDOC) > $@

$(JCXX): $(JSWG) $(JDOC) bindings/swig/classes.i $(library_include_HEADERS)
	$(AM_V_GEN)swig -c++ -java -package org.sigrok.core.classes \
		-I$(srcdir)/include -I$(srcdir)/bindings/cxx/include -I$(srcdir) -I$(JCLS) -Ibindings/cxx/include -outdir $(JCLS) -o $@ $<

$(JJAR): $(JCXX)
	$(AM_V_GEN)$(JAVAC) -sourcepath $(JDIR) -d $(JDIR) $(JSRC)
	$(AM_V_at)jar cf $(JJAR) -C $(JDIR) $(JPKG)

$(JLIB): $(JCXX) bindings/cxx/libsigrokxx.la $(library_include_HEADERS)
	$(AM_V_GEN)$(CXX) $(CXXFLAGS) -L.libs -Lbindings/cxx/.libs \
		-fno-strict-aliasing -fPIC -shared $(JCLS)/classes_wrap.cxx \
		-lsigrokxx -o $(JLIB)

java-install:
	$(INSTALL) -d $(libdir)/jni
	$(INSTALL) $(JLIB) -t $(libdir)/jni
	$(INSTALL) -d $(datadir)/java
	$(INSTALL) $(JJAR) -t $(datadir)/java

java-clean:
	rm -f $(JCXX)
	rm -f $(JCLS)/*.java
	rm -f $(JCLS)/*.class
	rm -f $(JINT)/*.class
	rm -f $(JJAR)
	rm -f $(JLIB)
	rm -rf $(JDIR)/doxy/

java-doc:
	$(AM_V_at)cd $(srcdir)/$(JDIR) && BUILDDIR=$(abs_builddir)/$(JDIR)/ doxygen Doxyfile 2>/dev/null

BUILD_EXTRA += java-build
INSTALL_EXTRA += java-install
CLEAN_EXTRA += java-clean

endif

all-local: $(BUILD_EXTRA)
install-exec-local: $(INSTALL_EXTRA)
clean-local: $(CLEAN_EXTRA)

MAINTAINERCLEANFILES = ChangeLog

.PHONY: ChangeLog
ChangeLog:
	git --git-dir $(top_srcdir)/.git log > ChangeLog || touch ChangeLog

dist-hook: ChangeLog

