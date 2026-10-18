/*
 * Protocol handling for the Rigol DS5000 (DS5102MA) driver.
 *
 * The DS5000 speaks a simple vendor text protocol over USB control
 * transfers (it is NOT USBTMC/SCPI):
 *
 *   send a character : control bmRequestType=0xC0 bRequest=1
 *                      wValue=<char> wIndex=0 wLength=0
 *   send a command   : the characters followed by CR (0x0d)
 *   read length      : control bmRequestType=0xC0 bRequest=0
 *                      wValue=0 wIndex=0 wLength=1  -> 1 length byte
 *   read data        : control bmRequestType=0xC0 bRequest=0
 *                      wValue=1 wIndex=0 wLength=N  -> N bytes
 *
 * Commands are written ":GROUP:FIELD:VALUE" to set and ":GROUP:FIELD:?"
 * to query; replies are ASCII terminated with "\n\r".
 *
 * Waveforms are fetched with the non-query command ":DATA:CHANNEL<n>",
 * after which the scope is read in a loop of (1 status byte, 255 data
 * bytes) chunks. The payload is ASCII hexadecimal, "0xNN 0xNN ...",
 * terminated with "\n\r" and padded with 0xaa55.
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

#include <config.h>
#include <string.h>
#include <stdlib.h>
#include <libusb.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "protocol.h"

#define DS5K_CTRL_TIMEOUT 1000
#define DS5K_DATA_TIMEOUT 2000
/*
 * 1020 = 204 * 5, i.e. a whole number of "0xNN " tokens per read. The
 * parser below works on the whole contiguous stream anyway, so this is
 * belt and braces rather than required for correctness.
 */
#define DS5K_READ_CHUNK 1020
#define DS5K_RAW_MAX 16384
#define DS5K_MAX_CHUNKS 64

SR_PRIV int rigol_ds5k_send(struct sr_usb_dev_inst *usb, const char *cmd)
{
	int ret, i;
	unsigned char c;

	for (i = 0; cmd[i]; i++) {
		c = (unsigned char)cmd[i];
		ret = libusb_control_transfer(usb->devhdl, 0xc0, 0x01,
			c, 0, NULL, 0, DS5K_CTRL_TIMEOUT);
		if (ret < 0) {
			sr_err("Failed to send command byte: %s",
				libusb_error_name(ret));
			return SR_ERR;
		}
	}

	ret = libusb_control_transfer(usb->devhdl, 0xc0, 0x01,
		'\r', 0, NULL, 0, DS5K_CTRL_TIMEOUT);
	if (ret < 0) {
		sr_err("Failed to terminate command: %s",
			libusb_error_name(ret));
		return SR_ERR;
	}

	return SR_OK;
}

/*
 * Hand the front panel back to the operator.
 *
 * The DS5000 locks the panel into remote mode as soon as it receives any
 * command; the only way to undo that from the host is to send
 * ":KEY:LOCK:ENABLE" and then stop talking (verified on a DS5102MA: with the
 * panel locked, sending this as the final command and going idle brings the
 * panel back, and it is idempotent). Releasing the USB interface does not do
 * it, and there is no separate unlock command.
 *
 * This is deliberately best-effort: freeing the panel is a nicety, so a
 * failure here must never abort a stop/close path.
 */
SR_PRIV void rigol_ds5k_release_panel(struct sr_usb_dev_inst *usb)
{
	if (!usb || !usb->devhdl)
		return;

	if (rigol_ds5k_send(usb, DS5K_CMD_PANEL_LOCAL) != SR_OK)
		sr_dbg("Could not return the front panel to local.");
	else
		sr_dbg("Front panel returned to local control.");
}

SR_PRIV int rigol_ds5k_query(struct sr_usb_dev_inst *usb, const char *cmd,
	char *buf, size_t len)
{
	int ret;
	uint8_t n;
	char *p;

	if (len < 2)
		return SR_ERR_ARG;

	if (rigol_ds5k_send(usb, cmd) != SR_OK)
		return SR_ERR;

	ret = libusb_control_transfer(usb->devhdl, 0xc0, 0x00, 0, 0,
		&n, 1, DS5K_CTRL_TIMEOUT);
	if (ret < 0) {
		sr_err("Failed to read reply length: %s",
			libusb_error_name(ret));
		return SR_ERR;
	}
	if (n == 0)
		return SR_ERR;

	if ((size_t)n >= len) {
		/* Reply longer than the caller's buffer; truncate. */
		n = len - 1;
	}

	ret = libusb_control_transfer(usb->devhdl, 0xc0, 0x00, 1, 0,
		(unsigned char *)buf, n, DS5K_DATA_TIMEOUT);
	if (ret < 0) {
		sr_err("Failed to read reply data: %s",
			libusb_error_name(ret));
		return SR_ERR;
	}
	if (ret < n)
		n = ret;

	buf[n] = '\0';

	/*
	 * The scope's reply buffer is not always cleared between transfers,
	 * so a stale "123456789\r\n" artefact can precede the real reply.
	 * It may appear more than once and can sit behind leading
	 * whitespace, so strip it repeatedly rather than just once -
	 * missing a repetition makes e.g. atoi() read 123456789 as a
	 * probe factor and poison the channel scaling.
	 */
	p = buf;
	for (;;) {
		while (*p == '\r' || *p == '\n' || *p == ' ' || *p == '\t')
			p++;
		if (strncmp(p, "123456789", 9) != 0)
			break;
		p += 9;
	}

	/* Strip surrounding CR/LF and spaces. */
	n = strlen(p);
	while (n > 0 && (p[n - 1] == '\r' || p[n - 1] == '\n' ||
			p[n - 1] == ' ' || p[n - 1] == '\t'))
		p[--n] = '\0';

	if (p != buf)
		memmove(buf, p, n + 1);

	return SR_OK;
}

/*
 * Query a numeric setting. The scope answers with an SI-prefixed unit,
 * e.g. "20.0V", "100mV", "500.0us", "0.00div", "10X". strtod() reads the
 * leading number and the prefix on the unit suffix scales it to base
 * units (volts, seconds), so "100mV" comes back as 0.1 rather than 100.
 * A reply with no recognised prefix is taken at face value.
 */
SR_PRIV int rigol_ds5k_get_double(struct sr_usb_dev_inst *usb,
	const char *cmd, double *val)
{
	char buf[64];
	char *end;
	double v;

	if (rigol_ds5k_query(usb, cmd, buf, sizeof(buf)) != SR_OK)
		return SR_ERR;

	v = strtod(buf, &end);
	if (end == buf)
		return SR_ERR;

	switch (*end) {
	case 'n':
		v *= 1e-9;
		break;
	case 'u':
		v *= 1e-6;
		break;
	case 'm':
		v *= 1e-3;
		break;
	case 'k':
		v *= 1e3;
		break;
	case 'M':
		v *= 1e6;
		break;
	}

	*val = v;

	return SR_OK;
}

static int hexval(uint8_t c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

/*
 * Trigger a waveform download for one channel and decode the ASCII hex
 * sample stream. Returns the number of samples collected in *num_samples.
 */
SR_PRIV int rigol_ds5k_download(struct sr_usb_dev_inst *usb, int channel,
	uint8_t *samples, size_t max_samples, size_t *num_samples)
{
	char cmd[32];
	uint8_t *raw;
	size_t total = 0, count = 0;
	int i, j, r, done = 0;

	raw = g_malloc(DS5K_RAW_MAX);
	if (!raw)
		return SR_ERR_MALLOC;

	snprintf(cmd, sizeof(cmd), ":DATA:CHANNEL%d", channel);

	if (rigol_ds5k_send(usb, cmd) != SR_OK) {
		g_free(raw);
		return SR_ERR;
	}

	for (i = 0; i < DS5K_MAX_CHUNKS && !done; i++) {
		if (total + DS5K_READ_CHUNK > DS5K_RAW_MAX)
			break;

		r = libusb_control_transfer(usb->devhdl, 0xc0, 0x00, 1, 0,
			raw + total, DS5K_READ_CHUNK, DS5K_DATA_TIMEOUT);
		if (r < 0) {
			sr_err("Waveform data read failed: %s",
				libusb_error_name(r));
			g_free(raw);
			return SR_ERR;
		}
		if (r == 0)
			break;

		total += r;

		if (memchr(raw + total - r, '\n', r))
			done = 1;
	}

	/*
	 * Decode the whole stream in one pass. Reading in fixed-size chunks
	 * can split a "0xNN " token across a read boundary; parsing the
	 * contiguous stream avoids dropping those samples.
	 */
	for (j = 0; j + 4 <= (int)total; ) {
		if (raw[j] == '0' &&
		    (raw[j + 1] == 'x' || raw[j + 1] == 'X')) {
			int hi = hexval(raw[j + 2]);
			int lo = hexval(raw[j + 3]);
			if (hi >= 0 && lo >= 0 && count < max_samples)
				samples[count++] = (hi << 4) | lo;
			j += 5;
		} else {
			j++;
		}
	}

	g_free(raw);

	*num_samples = count;

	return SR_OK;
}
