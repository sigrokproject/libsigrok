/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2012 Bert Vermeulen <bert@biot.com>
 * With protocol information from the hantekdso project,
 * Copyright (C) 2008 Oleg Khudyakov <prcoder@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LIBSIGROK_HARDWARE_HANTEK_DSO_H
#define LIBSIGROK_HARDWARE_HANTEK_DSO_H

#define USB_INTERFACE          0
#define USB_CONFIGURATION      1
#define DSO_EP_IN              0x86
#define DSO_EP_OUT             0x02

/* FX2 renumeration delay in ms */
#define MAX_RENUM_DELAY        3000

#define MAX_CAPTURE_EMPTY      3

#define DEFAULT_VOLTAGE            VDIV_500MV
#define DEFAULT_FRAMESIZE          FRAMESIZE_SMALL
#define DEFAULT_TIMEBASE           TIME_100us
#define DEFAULT_TRIGGER_SOURCE     "CH1"
#define DEFAULT_COUPLING           COUPLING_DC
#define DEFAULT_HORIZ_TRIGGERPOS   0.5
#define DEFAULT_VERT_OFFSET        0.5
#define DEFAULT_VERT_TRIGGERPOS    0.5

#define MAX_VERT_TRIGGER           0xfe

/* Hantek DSO-specific protocol values */
#define EEPROM_CHANNEL_OFFSETS     0x08

#define FRAMESIZE_SMALL        10240
#define FRAMESIZE_LARGE        32768


enum control_requests {
	CTRL_READ_EEPROM = 0xa2,
	CTRL_GETSPEED = 0xb2,
	CTRL_BEGINCOMMAND = 0xb3,
	CTRL_SETOFFSET = 0xb4,
	CTRL_SETRELAYS = 0xb5
};

enum dso_commands {
	CMD_SET_FILTERS = 0,
	CMD_SET_TRIGGER_SAMPLERATE,
	CMD_FORCE_TRIGGER,
	CMD_CAPTURE_START,
	CMD_ENABLE_TRIGGER,
	CMD_GET_CHANNELDATA,
	CMD_GET_CAPTURESTATE,
	CMD_SET_VOLTAGE,
	/* unused */
	cmdSetLogicalData,
	cmdGetLogicalData
};

/* Must match the coupling table. */
enum couplings {
	COUPLING_AC = 0,
	COUPLING_DC,
	/* TODO not used, how to enable? */
	COUPLING_GND
};

/* Must match the timebases table. */
enum time_bases {
	TIME_10us = 0,
	TIME_20us,
	TIME_40us,
	TIME_100us,
	TIME_200us,
	TIME_400us,
	TIME_1ms,
	TIME_2ms,
	TIME_4ms,
	TIME_10ms,
	TIME_20ms,
	TIME_40ms,
	TIME_100ms,
	TIME_200ms,
	TIME_400ms
};

/* Must match the vdivs table. */
enum {
	VDIV_10MV,
	VDIV_20MV,
	VDIV_50MV,
	VDIV_100MV,
	VDIV_200MV,
	VDIV_500MV,
	VDIV_1V,
	VDIV_2V,
	VDIV_5V,
};

enum trigger_slopes {
	SLOPE_POSITIVE = 0,
	SLOPE_NEGATIVE
};

enum trigger_sources {
	TRIGGER_CH2 = 0,
	TRIGGER_CH1,
	TRIGGER_EXT,
};

enum capturestates {
	CAPTURE_EMPTY = 0,
	CAPTURE_FILLING = 1,
	CAPTURE_READY_8BIT = 2,
	CAPTURE_READY_9BIT = 7,
	CAPTURE_TIMEOUT = 127,
	CAPTURE_UNKNOWN = 255
};

enum triggermodes {
	TRIGGERMODE_AUTO,
	TRIGGERMODE_NORMAL,
	TRIGGERMODE_SINGLE
};

enum states {
	IDLE,
	NEW_CAPTURE,
	CAPTURE,
	FETCH_DATA
};

struct dso_profile {
	/* VID/PID after cold boot */
	uint16_t orig_vid;
	uint16_t orig_pid;
	/* VID/PID after firmware upload */
	uint16_t fw_vid;
	uint16_t fw_pid;
	char *vendor;
	char *model;
	char *model_version;
	int num_probes;
	char *firmware;
};

struct context {
	const struct dso_profile *profile;
	struct sr_usb_dev_inst *usb;
	void *cb_data;
	uint64_t limit_frames;
	uint64_t num_frames;
	/* We can't keep track of an FX2-based device after upgrading
	 * the firmware (it re-enumerates into a different device address
	 * after the upgrade) this is like a global lock. No device will open
	 * until a proper delay after the last device was upgraded.
	 */
	GTimeVal fw_updated;
	int epin_maxpacketsize;
	int capture_empty_count;
	int dev_state;

	/* Oscilloscope settings. */
	int timebase;
	gboolean ch1_enabled;
	gboolean ch2_enabled;
	int voltage_ch1;
	int voltage_ch2;
	int coupling_ch1;
	int coupling_ch2;
	// voltage offset (vertical position)
	float voffset_ch1;
	float voffset_ch2;
	float voffset_trigger;
	uint16_t channel_levels[2][9][2];
	unsigned int framesize;
	gboolean filter_ch1;
	gboolean filter_ch2;
	gboolean filter_trigger;
	int triggerslope;
	char *triggersource;
	float triggerposition;
	int triggermode;

	/* Frame transfer */
	unsigned int samp_received;
	unsigned int samp_buffered;
	unsigned int trigger_offset;
	unsigned char *framebuf;
};

SR_PRIV int dso_open(int dev_index);
SR_PRIV void dso_close(struct sr_dev_inst *sdi);
SR_PRIV int dso_enable_trigger(struct context *ctx);
SR_PRIV int dso_force_trigger(struct context *ctx);
SR_PRIV int dso_init(struct context *ctx);
SR_PRIV int dso_get_capturestate(struct context *ctx, uint8_t *capturestate,
		uint32_t *trigger_offset);
SR_PRIV int dso_capture_start(struct context *ctx);
SR_PRIV int dso_get_channeldata(struct context *ctx, libusb_transfer_cb_fn cb);

#endif
