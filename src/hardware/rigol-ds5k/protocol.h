/*
 * Protocol declarations for the Rigol DS5000 (DS5102MA) driver.
 *
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2026 audiossis <audiossis@users.noreply.github.com>
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

#ifndef LIBSIGROK_HARDWARE_RIGOL_DS5K_PROTOCOL_H
#define LIBSIGROK_HARDWARE_RIGOL_DS5K_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "rigol-ds5k"

#define DS5K_VID 0x0400
#define DS5K_PID 0xc55d

/*
 * Return the front panel to local (operator) control.
 *
 * The DS5000 enters a remote/locked mode as soon as the host sends it any
 * command ("rmt" appears on screen, keys and knobs are ignored). The mode is
 * left by sending this command as the *last* thing on the wire and then going
 * silent; it is idempotent, so sending it when the panel is already local is
 * harmless. Sending anything else afterwards re-asserts remote mode.
 */
#define DS5K_CMD_PANEL_LOCAL ":KEY:LOCK:ENABLE"

/* Number of samples the scope delivers per channel, per acquisition. */
#define DS5K_WINDOW_SAMPLES 2400

/* Maximum real-time sample rate of the DS5000MA series. */
#define DS5K_MAX_SAMPLERATE 1000000000ULL

/* The DS5000 graticule is 12 horizontal by 8 vertical divisions. */
#define DS5K_HDIV 12
#define DS5K_VDIV 8

/*
 * ADC codes per vertical division. The 8-bit ADC spans somewhat more than
 * the 8 visible divisions. Measured on a DS5102MA: ~25.0 (vertical offset
 * sweep) to ~26.6 (LCD pixel measurement); 25.6 (= 256 / 10) is the value
 * used by the DS1000 family and sits right in that range.
 */
#define DS5K_CODES_PER_DIV 25.6

/* ADC code that corresponds to 0 V (the vertical screen centre). */
#define DS5K_CODE_ZERO 128.0

#define DS5K_NUM_VDIV_STEPS 8
#define DS5K_NUM_CHANNELS 2

struct rigol_ds5k_state {
	int probe[DS5K_NUM_CHANNELS];	/* probe attenuation: 1 or 10 */
	int vdiv_idx[DS5K_NUM_CHANNELS];/* index into the vdiv table, -1 unknown */
	int coupling[DS5K_NUM_CHANNELS];/* 0 = AC, 1 = DC, 2 = GND */
	uint64_t limit_frames;
	uint64_t num_frames;
	uint64_t samplerate;
	/*
	 * USB interface 0 is claimed only while an acquisition is running.
	 * The firmware switches the scope into remote mode (front panel
	 * locked) as soon as any command is sent to it. Releasing the
	 * interface alone does NOT bring the panel back; the panel is
	 * returned to local by sending DS5K_CMD_PANEL_LOCAL as the final
	 * command, which dev_acquisition_stop(), dev_close() and the
	 * non-acquisition config paths (ds5k_quiesce) all do.
	 */
	gboolean claimed;
	/* TRUE while the poll source is installed and must be removed. */
	gboolean polling;
	/*
	 * TRUE only between dev_acquisition_start() and
	 * dev_acquisition_stop()/dev_close(). While an acquisition runs we
	 * talk to the scope freely; outside it we hand the front panel back
	 * after every command so the operator stays in control locally.
	 */
	gboolean acquiring;
};

SR_PRIV int rigol_ds5k_send(struct sr_usb_dev_inst *usb, const char *cmd);
/*
 * Send the "return to local" command, releasing the front panel lock.
 * Must be the last command sent to the scope; idempotent and fail-safe.
 */
SR_PRIV void rigol_ds5k_release_panel(struct sr_usb_dev_inst *usb);
SR_PRIV int rigol_ds5k_query(struct sr_usb_dev_inst *usb, const char *cmd,
	char *buf, size_t len);
SR_PRIV int rigol_ds5k_get_double(struct sr_usb_dev_inst *usb,
	const char *cmd, double *val);
SR_PRIV int rigol_ds5k_download(struct sr_usb_dev_inst *usb, int channel,
	uint8_t *samples, size_t max_samples, size_t *num_samples);

#endif
