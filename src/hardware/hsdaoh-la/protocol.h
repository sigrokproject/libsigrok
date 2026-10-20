/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2026 Do Van Dung <do_van_dzung@yahoo.com>
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

/*
 * hsdaoh-la: logic analyzer made of a Raspberry Pi Pico 2 (RP2350) running
 * the hsdaoh-rp2350 "la8" firmware (8 channels GP0..GP7, up to 140 MS/s) or
 * "la4" firmware (4 channels GP0..GP3, up to 280 MS/s) and an MS2130 USB3
 * HDMI capture stick. The Pico samples continuously and streams every sample
 * over HDMI; libhsdaoh receives the stream on the PC. The channel count comes
 * from the Pico's status line; each buffer is unpacked by its stream format
 * (PIO_8BIT: 2 samples per 16-bit word, PIO_4BIT: 4 samples per word).
 *
 * Acquisition model (kept simple on purpose):
 *   - the stream is scanned on the PC for the trigger condition
 *   - pre-trigger + post-trigger samples are collected in RAM
 *   - only when the capture is complete is it sent to the session
 *     (PulseView shows the data at the end of the acquisition)
 *
 * The sample rate is set by the firmware (system clock / integer divider).
 * If the Pico's USB serial port is given as "conn", the driver can change
 * it ("div <n>" command); otherwise the rate reported by the device in the
 * HDMI metadata is used as is.
 */

#ifndef LIBSIGROK_HARDWARE_HSDAOH_LA_PROTOCOL_H
#define LIBSIGROK_HARDWARE_HSDAOH_LA_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <hsdaoh.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "hsdaoh-la"

/* channels: la8 firmware 8 (GP0..GP7), la4 firmware 4 (GP0..GP3) */
#define HSDAOH_LA_NUM_CHANNELS		8
#define HSDAOH_LA_LA4_CHANNELS		4
/* stream formats (from libhsdaoh's hsdaoh.h; older headers have no
 * format in the callback data, la8 is assumed then) */
#ifdef HSDAOH_FORMAT_PIO_4BIT
#define HSDAOH_LA_HAVE_FORMAT		1
#else
#define HSDAOH_LA_HAVE_FORMAT		0
#endif
/* used until the device has reported its rate in the stream metadata */
#define HSDAOH_LA_DEFAULT_SAMPLERATE	SR_MHZ(140)
/* the la8 firmware's divider range */
#define HSDAOH_LA_MAX_DIVIDER		65535
/*
 * Largest capture, 1 byte per sample in the driver plus PulseView's own
 * copy. A 32-bit process (sigrok's default Windows build is i686) has
 * 2 GB of address space at most, so it gets a much lower limit.
 */
#if GLIB_SIZEOF_VOID_P >= 8
#define HSDAOH_LA_MAX_SAMPLES		4000000000ULL
#else
#define HSDAOH_LA_MAX_SAMPLES		400000000ULL
#endif
/* largest single logic packet sent to the session */
#define HSDAOH_LA_SEND_CHUNK		(4 * 1024 * 1024)

enum hsdaoh_la_state {
	HL_IDLE,		/* no acquisition running */
	HL_ARMED,		/* waiting for the trigger */
	HL_CAPTURING,		/* trigger found, collecting post-trigger samples */
	HL_DONE,		/* buffer complete, waiting to be sent */
	HL_FAILED,		/* device error, end the acquisition */
};

struct dev_context {
	/* device */
	uint32_t hsdaoh_index;
	hsdaoh_dev_t *hsdaoh;
	unsigned int channels;		/* 8 (la8) or 4 (la4; from the Pico's status) */
	/*
	 * The HDMI stream is started by the first acquisition and kept
	 * running until the device is closed: no new synchronization (and
	 * no lost first frames) for every Run. Between acquisitions the data
	 * callback ignores the data.
	 */
	gboolean streaming;
	volatile gboolean stream_error;	/* libhsdaoh reported a device error */
	int skip_bufs;			/* buffers to ignore after a rate change */

	/* optional Pico USB serial port for setting the sample rate */
	struct sr_serial_dev_inst *serial;
	uint64_t sys_clock;		/* Pico system clock in Hz (0 = unknown) */
	unsigned int min_div;		/* smallest divider the firmware accepts */
	unsigned int cur_div;
	uint64_t *samplerate_list;
	unsigned int num_samplerates;

	/* settings */
	uint64_t samplerate;		/* last known actual sample rate */
	uint64_t limit_samples;
	uint64_t capture_ratio;		/* percent of samples before the trigger */

	/* trigger: tbl[(prev << 8) | cur] != 0 where the condition is met */
	gboolean have_trigger;
	uint8_t *trig_tbl;

	/* capture (written by the libhsdaoh thread, read by the session thread
	 * once state is HL_DONE / HL_FAILED) */
	GMutex mutex;
	volatile int state;
	uint64_t pre;			/* samples before the trigger */
	uint64_t post;			/* samples from the trigger on */
	uint8_t *buf;			/* pre + post samples */
	uint64_t buf_fill;
	uint64_t trigger_pos;		/* index of the trigger sample in buf */
	uint8_t *hist;			/* pre-trigger ring buffer */
	uint64_t hist_pos, hist_len;
	gboolean have_prev;
	uint8_t prev;
	uint64_t next_cnt;		/* expected stream word counter */
	gboolean cnt_valid;
	uint64_t gaps;
	uint32_t stream_srate;		/* sample rate from the stream metadata */
	uint8_t *unpack;		/* scratch for one unpacked buffer */
	size_t unpack_size;
};

/* rate for a divider, rounded like the la8 firmware does (it reports this
 * value in the stream metadata) */
static inline uint64_t hsdaoh_la_rate(uint64_t sys_clock, uint64_t div)
{
	return (sys_clock + div / 2) / div;
}

SR_PRIV void hsdaoh_la_data_cb(hsdaoh_data_info_t *info);
SR_PRIV int hsdaoh_la_receive_data(int fd, int revents, void *cb_data);
SR_PRIV int hsdaoh_la_build_trigger(struct dev_context *devc, struct sr_trigger *trigger);
SR_PRIV void hsdaoh_la_reset_capture(struct dev_context *devc);
SR_PRIV void hsdaoh_la_send_capture(const struct sr_dev_inst *sdi, gboolean complete);
SR_PRIV void hsdaoh_la_end_acquisition(const struct sr_dev_inst *sdi, gboolean send);

SR_PRIV int hsdaoh_la_pico_command(struct dev_context *devc, const char *cmd,
		char *reply, size_t reply_size);
SR_PRIV int hsdaoh_la_pico_parse_status(struct dev_context *devc, const char *reply);

#endif
