/*
 * This file is part of the libsigrok project.
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

#ifndef LIBSIGROK_HARDWARE_HANTEK_DSO_PROTOCOL_H
#define LIBSIGROK_HARDWARE_HANTEK_DSO_PROTOCOL_H

#define LOG_PREFIX "hantek-dso"

#define USB_INTERFACE           0
#define USB_CONFIGURATION       1
#define DSO_EP_IN               0x86
#define DSO_EP_OUT              0x02

/* FX2 renumeration delay in ms */
#define MAX_RENUM_DELAY_MS      3000

#define MAX_CAPTURE_EMPTY       3

#define DEFAULT_VOLTAGE         VDIV_500MV
#define DEFAULT_FRAMESIZE       FRAMESIZE_SMALL
#define DEFAULT_TIMEBASE        TIME_100us
#define DEFAULT_SAMPLERATE      SR_KHZ(10)
#define DEFAULT_TRIGGER_SOURCE  "CH1"
#define DEFAULT_COUPLING        COUPLING_DC
#define DEFAULT_CAPTURE_RATIO   100
#define DEFAULT_VERT_OFFSET     0.5
#define DEFAULT_VERT_TRIGGERPOS 0.5

#define MAX_VERT_TRIGGER        0xfe

/* Hantek DSO-specific protocol values */
#define EEPROM_CHANNEL_OFFSETS  0x08

/* All models have this for their "fast" mode. */
#define FRAMESIZE_SMALL         (10 * 1024)

#define NUM_CHANNELS            2

enum control_requests {
	CTRL_READ_EEPROM = 0xa2,
	CTRL_GETSPEED = 0xb2,
	CTRL_BEGINCOMMAND = 0xb3,
	CTRL_SETOFFSET = 0xb4,
	CTRL_SETRELAYS = 0xb5,
};

enum dso_commands {
	CMD_SET_FILTERS                     = 0x0,
	CMD_SET_TRIGGER_SAMPLERATE          = 0x1,
	CMD_FORCE_TRIGGER                   = 0x2,
	CMD_CAPTURE_START                   = 0x3,
	CMD_ENABLE_TRIGGER                  = 0x4,
	CMD_GET_CHANNELDATA                 = 0x5,
	CMD_GET_CAPTURESTATE                = 0x6,
	CMD_SET_VOLTAGE                     = 0x7,
	/* unused */
	CMD_SET_LOGICALDATA                 = 0x8,
	CMD_GET_LOGICALDATA                 = 0x9,
	CMD__UNUSED1                        = 0xa,
	/*
	 * For the following and other specials please see
	 * http://openhantek.sourceforge.net/doc/namespaceHantek.html#ac1cd181814cf3da74771c29800b39028
	 */
	CMD_2250_SET_CHANNELS               = 0xb,
	CMD_2250_SET_TRIGGERSOURCE          = 0xc,
	CMD_2250_SET_RECORD_LENGTH          = 0xd,
	CMD_2250_SET_SAMPLERATE             = 0xe,
	CMD_2250_SET_TRIGGERPOS_AND_BUFFER  = 0xf,
};

/* Must match the coupling table. */
enum couplings {
	COUPLING_AC = 0,
	COUPLING_DC,
	/* TODO not used, how to enable? */
	COUPLING_GND,
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
	TIME_400ms,
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
	SLOPE_NEGATIVE,
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
	CAPTURE_READY_2250 = 3,
	CAPTURE_READY_9BIT = 7,
	CAPTURE_TIMEOUT = 127,
	CAPTURE_UNKNOWN = 255,
};

enum triggermodes {
	TRIGGERMODE_AUTO,
	TRIGGERMODE_NORMAL,
	TRIGGERMODE_SINGLE,
};

enum states {
	IDLE,
	NEW_CAPTURE,
	CAPTURE,
	FETCH_DATA,
	STOPPING,
};

struct dso_profile {
	/* VID/PID after cold boot */
	uint16_t orig_vid;
	uint16_t orig_pid;
	/* VID/PID after firmware upload */
	uint16_t fw_vid;
	uint16_t fw_pid;
	const char *vendor;
	const char *model;
	const uint64_t *buffersizes;
	const char *firmware;
};

struct dev_context {
	const struct dso_profile *profile;
	uint64_t limit_frames;
	uint64_t num_frames;
	GSList *enabled_channels;
	/* We can't keep track of an FX2-based device after upgrading
	 * the firmware (it re-enumerates into a different device address
	 * after the upgrade) this is like a global lock. No device will open
	 * until a proper delay after the last device was upgraded.
	 */
	int64_t fw_updated;
	int epin_maxpacketsize;
	int capture_empty_count;
	int dev_state;

	/* Oscilloscope settings. */
	uint64_t samplerate;
	int timebase;
	gboolean ch_enabled[2];
	int voltage[2];
	int coupling[2];
	// voltage offset (vertical position)
	float voffset_ch1;
	float voffset_ch2;
	float voffset_trigger;
	uint16_t channel_levels[2][9][2];
	unsigned int framesize;
	gboolean filter[2];
	int triggerslope;
	char *triggersource;
	int capture_ratio;
	int triggermode;

	/* Frame transfer */
	unsigned int samp_received;
	unsigned int samp_buffered;
	unsigned int trigger_offset;
	unsigned char *framebuf;
};

SR_PRIV int dso_open(struct sr_dev_inst *sdi);
SR_PRIV void dso_close(struct sr_dev_inst *sdi);
SR_PRIV int dso_enable_trigger(const struct sr_dev_inst *sdi);
SR_PRIV int dso_force_trigger(const struct sr_dev_inst *sdi);
SR_PRIV int dso_init(const struct sr_dev_inst *sdi);
SR_PRIV int dso_get_capturestate(const struct sr_dev_inst *sdi,
		uint8_t *capturestate, uint32_t *trigger_offset);
SR_PRIV int dso_capture_start(const struct sr_dev_inst *sdi);
SR_PRIV int dso_get_channeldata(const struct sr_dev_inst *sdi,
		libusb_transfer_cb_fn cb);
SR_PRIV int dso_set_trigger_samplerate(const struct sr_dev_inst *sdi);
SR_PRIV int dso_set_voffsets(const struct sr_dev_inst *sdi);

#endif
