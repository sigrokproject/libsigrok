##
## This file is part of the libsigrok project.
##
## Copyright (C) 2011 Uwe Hermann <uwe@hermann-uwe.de>
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

SUBDIRS = \
	agilent-dmm \
	alsa \
	asix-sigma \
	brymen-dmm \
	cem-dt-885x \
	center-3xx \
	chronovu-la8 \
	colead-slm \
	common \
	demo \
	fluke-dmm \
	fx2lafw \
	hantek-dso \
	ikalogic-scanalogic2 \
	ikalogic-scanaplus \
	kecheng-kc-330b \
	lascar-el-usb \
	link-mso19 \
	mic-985xx \
	openbench-logic-sniffer \
	rigol-ds1xx2 \
	saleae-logic16 \
	serial-dmm \
	tondaj-sl-814 \
	uni-t-dmm \
	uni-t-ut32x \
	victor-dmm \
	zeroplus-logic-cube

noinst_LTLIBRARIES = libsigrokhardware.la

libsigrokhardware_la_SOURCES =

libsigrokhardware_la_LIBADD = \
	common/libsigrok_hw_common.la

if HW_AGILENT_DMM
libsigrokhardware_la_LIBADD += agilent-dmm/libsigrok_hw_agilent_dmm.la
endif

if HW_ALSA
libsigrokhardware_la_LIBADD += alsa/libsigrok_hw_alsa.la
endif

if HW_ASIX_SIGMA
libsigrokhardware_la_LIBADD += asix-sigma/libsigrok_hw_asix_sigma.la
endif

if HW_BRYMEN_DMM
libsigrokhardware_la_LIBADD += brymen-dmm/libsigrok_hw_brymen_dmm.la
endif

if HW_CEM_DT_885X
libsigrokhardware_la_LIBADD += cem-dt-885x/libsigrok_hw_cem_dt_885x.la
endif

if HW_CENTER_3XX
libsigrokhardware_la_LIBADD += center-3xx/libsigrok_hw_center_3xx.la
endif

if HW_CHRONOVU_LA8
libsigrokhardware_la_LIBADD += chronovu-la8/libsigrok_hw_chronovu_la8.la
endif

if HW_COLEAD_SLM
libsigrokhardware_la_LIBADD += colead-slm/libsigrok_hw_colead_slm.la
endif

if HW_DEMO
libsigrokhardware_la_LIBADD += demo/libsigrok_hw_demo.la
endif

if HW_FLUKE_DMM
libsigrokhardware_la_LIBADD += fluke-dmm/libsigrok_hw_fluke_dmm.la
endif

if HW_FX2LAFW
libsigrokhardware_la_LIBADD += fx2lafw/libsigrok_hw_fx2lafw.la
endif

if HW_HANTEK_DSO
libsigrokhardware_la_LIBADD += hantek-dso/libsigrok_hw_hantek_dso.la
endif

if HW_IKALOGIC_SCANALOGIC2
libsigrokhardware_la_LIBADD += ikalogic-scanalogic2/libsigrok_hw_ikalogic_scanalogic2.la
endif

if HW_IKALOGIC_SCANAPLUS
libsigrokhardware_la_LIBADD += ikalogic-scanaplus/libsigrok_hw_ikalogic_scanaplus.la
endif

if HW_KECHENG_KC_330B
libsigrokhardware_la_LIBADD += kecheng-kc-330b/libsigrok_hw_kecheng_kc_330b.la
endif

if HW_LASCAR_EL_USB
libsigrokhardware_la_LIBADD += lascar-el-usb/libsigrok_hw_lascar_el_usb.la
endif

if HW_LINK_MSO19
libsigrokhardware_la_LIBADD += link-mso19/libsigrok_hw_link_mso_19.la
endif

if HW_MIC_985XX
libsigrokhardware_la_LIBADD += mic-985xx/libsigrok_hw_mic_985xx.la
endif

if HW_OLS
libsigrokhardware_la_LIBADD += openbench-logic-sniffer/libsigrok_hw_ols.la
endif

if HW_RIGOL_DS1XX2
libsigrokhardware_la_LIBADD += rigol-ds1xx2/libsigrok_hw_rigol_ds1xx2.la
endif

if HW_SALEAE_LOGIC16
libsigrokhardware_la_LIBADD += saleae-logic16/libsigrok_hw_saleae_logic16.la
endif

if HW_SERIAL_DMM
libsigrokhardware_la_LIBADD += serial-dmm/libsigrok_hw_serial_dmm.la
endif

if HW_TONDAJ_SL_814
libsigrokhardware_la_LIBADD += tondaj-sl-814/libsigrok_hw_tondaj_sl_814.la
endif

if HW_UNI_T_DMM
libsigrokhardware_la_LIBADD += uni-t-dmm/libsigrok_hw_uni_t_dmm.la
endif

if HW_UNI_T_UT32X
libsigrokhardware_la_LIBADD += uni-t-ut32x/libsigrok_hw_uni_t_ut32x.la
endif

if HW_VICTOR_DMM
libsigrokhardware_la_LIBADD += victor-dmm/libsigrok_hw_victor_dmm.la
endif

if HW_ZEROPLUS_LOGIC_CUBE
libsigrokhardware_la_LIBADD += zeroplus-logic-cube/libsigrok_hw_zeroplus.la
endif

