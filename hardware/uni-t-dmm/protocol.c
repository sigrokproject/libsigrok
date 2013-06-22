/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2012-2013 Uwe Hermann <uwe@hermann-uwe.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include <string.h>
#include <glib.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"
#include "protocol.h"

/*
 * Driver for various UNI-T multimeters (and rebranded ones).
 *
 * Most UNI-T DMMs can be used with two (three) different PC interface cables:
 *  - The UT-D04 USB/HID cable, old version with Hoitek HE2325U chip.
 *  - The UT-D04 USB/HID cable, new version with WCH CH9325 chip.
 *  - The UT-D02 RS232 cable.
 *
 * This driver is meant to support all USB/HID cables, and various DMMs that
 * can be attached to a PC via these cables. Currently only the UT-D04 cable
 * (new version) is supported/tested.
 * The UT-D02 RS232 cable is handled by the 'serial-dmm' driver.
 *
 * The data for one DMM packet (e.g. 14 bytes if the respective DMM uses a
 * Fortune Semiconductor FS9922-DMM4 chip) is spread across multiple
 * 8-byte chunks.
 *
 * An 8-byte chunk looks like this:
 *  - Byte 0: 0xfz, where z is the number of actual data bytes in this chunk.
 *  - Bytes 1-7: z data bytes, the rest of the bytes should be ignored.
 *
 * Example:
 *  f0 00 00 00 00 00 00 00 (no data bytes)
 *  f2 55 77 00 00 00 00 00 (2 data bytes, 0x55 and 0x77)
 *  f1 d1 00 00 00 00 00 00 (1 data byte, 0xd1)
 *
 * Chips and serial settings used in UNI-T DMMs (and rebranded ones):
 *  - Tecpel DMM-8060: ? (very likely Fortune Semiconductor FS9721_LP3)
 *  - Tecpel DMM-8061: ? (very likely Fortune Semiconductor FS9721_LP3)
 *  - UNI-T UT108: ?
 *  - UNI-T UT109: ?
 *  - UNI-T UT30A: ?
 *  - UNI-T UT30E: ?
 *  - UNI-T UT60E: Fortune Semiconductor FS9721_LP3
 *  - UNI-T UT60G: ?
 *  - UNI-T UT61B: ?
 *  - UNI-T UT61C: ?
 *  - UNI-T UT61D: Fortune Semiconductor FS9922-DMM4
 *  - UNI-T UT61E: Cyrustek ES51922
 *  - UNI-T UT70B: ?
 *  - Voltcraft VC-820: Fortune Semiconductor FS9721_LP3
 *  - Voltcraft VC-830: Fortune Semiconductor FS9922-DMM4
 *  - Voltcraft VC-840: Fortune Semiconductor FS9721_LP3
 *  - ...
 */

static void decode_packet(struct sr_dev_inst *sdi, int dmm, const uint8_t *buf,
			  void *info)
{
	struct dev_context *devc;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	float floatval;
	int ret;

	devc = sdi->priv;
	memset(&analog, 0, sizeof(struct sr_datafeed_analog));

	/* Parse the protocol packet. */
	ret = udmms[dmm].packet_parse(buf, &floatval, &analog, info);
	if (ret != SR_OK) {
		sr_dbg("Invalid DMM packet, ignoring.");
		return;
	}

	/* If this DMM needs additional handling, call the resp. function. */
	if (udmms[dmm].dmm_details)
		udmms[dmm].dmm_details(&analog, info);

	/* Send a sample packet with one analog value. */
	analog.probes = sdi->probes;
	analog.num_samples = 1;
	analog.data = &floatval;
	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	sr_session_send(devc->cb_data, &packet);

	/* Increase sample count. */
	devc->num_samples++;
}

static int hid_chip_init(struct sr_dev_inst *sdi, uint16_t baudrate)
{
	int ret;
	uint8_t buf[5];
	struct sr_usb_dev_inst *usb;

	usb = sdi->conn;
	
	/* Detach kernel drivers which grabbed this device (if any). */
	if (libusb_kernel_driver_active(usb->devhdl, 0) == 1) {
		ret = libusb_detach_kernel_driver(usb->devhdl, 0);
		if (ret < 0) {
			sr_err("Failed to detach kernel driver: %s.",
			       libusb_error_name(ret));
			return SR_ERR;
		}
		sr_dbg("Successfully detached kernel driver.");
	} else {
		sr_dbg("No need to detach a kernel driver.");
	}

	/* Claim interface 0. */
	if ((ret = libusb_claim_interface(usb->devhdl, 0)) < 0) {
		sr_err("Failed to claim interface 0: %s.",
		       libusb_error_name(ret));
		return SR_ERR;
	}
	sr_dbg("Successfully claimed interface 0.");

	/* Set data for the HID feature report (e.g. baudrate). */
	buf[0] = baudrate & 0xff;        /* Baudrate, LSB */
	buf[1] = (baudrate >> 8) & 0xff; /* Baudrate, MSB */
	buf[2] = 0x00;                   /* Unknown/unused (?) */
	buf[3] = 0x00;                   /* Unknown/unused (?) */
	buf[4] = 0x03;                   /* Unknown, always 0x03. */

	/* Send HID feature report to setup the baudrate/chip. */
	sr_dbg("Sending initial HID feature report.");
	sr_spew("HID init = 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x (%d baud)",
		buf[0], buf[1], buf[2], buf[3], buf[4], baudrate);
	ret = libusb_control_transfer(
		usb->devhdl, /* libusb device handle */
		LIBUSB_REQUEST_TYPE_CLASS |
		LIBUSB_RECIPIENT_INTERFACE |
		LIBUSB_ENDPOINT_OUT,
		9, /* bRequest: HID set_report */
		0x300, /* wValue: HID feature, report number 0 */
		0, /* wIndex: interface 0 */
		(unsigned char *)&buf, /* payload buffer */
		5, /* wLength: 5 bytes payload */
		1000 /* timeout (ms) */);

	if (ret < 0) {
		sr_err("HID feature report error: %s.", libusb_error_name(ret));
		return SR_ERR;
	}

	if (ret != 5) {
		/* TODO: Handle better by also sending the remaining bytes. */
		sr_err("Short packet: sent %d/5 bytes.", ret);
		return SR_ERR;
	}

	sr_dbg("Successfully sent initial HID feature report.");

	return SR_OK;
}

static void log_8byte_chunk(const uint8_t *buf)
{
	sr_spew("8-byte chunk: %02x %02x %02x %02x %02x %02x %02x %02x "
		"(%d data bytes)", buf[0], buf[1], buf[2], buf[3],
		buf[4], buf[5], buf[6], buf[7], (buf[0] & 0x0f));
}

static void log_dmm_packet(const uint8_t *buf)
{
	sr_dbg("DMM packet:   %02x %02x %02x %02x %02x %02x %02x"
	       " %02x %02x %02x %02x %02x %02x %02x",
	       buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6],
	       buf[7], buf[8], buf[9], buf[10], buf[11], buf[12], buf[13]);
}

static int get_and_handle_data(struct sr_dev_inst *sdi, int dmm, void *info)
{
	struct dev_context *devc;
	uint8_t buf[CHUNK_SIZE], *pbuf;
	int i, ret, len, num_databytes_in_chunk;
	struct sr_usb_dev_inst *usb;

	devc = sdi->priv;
	usb = sdi->conn;
	pbuf = devc->protocol_buf;

	/* On the first run, we need to init the HID chip. */
	if (devc->first_run) {
		if ((ret = hid_chip_init(sdi, udmms[dmm].baudrate)) != SR_OK) {
			sr_err("HID chip init failed: %d.", ret);
			return SR_ERR;
		}
		memset(pbuf, 0x00, DMM_BUFSIZE);
		devc->first_run = FALSE;
	}

	memset(&buf, 0x00, CHUNK_SIZE);

	/* Get data from EP2 using an interrupt transfer. */
	ret = libusb_interrupt_transfer(
		usb->devhdl, /* libusb device handle */
		LIBUSB_ENDPOINT_IN | 2, /* EP2, IN */
		(unsigned char *)&buf, /* receive buffer */
		CHUNK_SIZE, /* wLength */
		&len, /* actually received byte count */
		1000 /* timeout (ms) */);

	if (ret < 0) {
		sr_err("USB receive error: %s.", libusb_error_name(ret));
		return SR_ERR;
	}

	if (len != CHUNK_SIZE) {
		sr_err("Short packet: received %d/%d bytes.", len, CHUNK_SIZE);
		/* TODO: Print the bytes? */
		return SR_ERR;
	}

	log_8byte_chunk((const uint8_t *)&buf);

	/* If there are no data bytes just return (without error). */
	if (buf[0] == 0xf0)
		return SR_OK;

	devc->bufoffset = 0;

	/* Append the 1-7 data bytes of this chunk to pbuf. */
	num_databytes_in_chunk = buf[0] & 0x0f;
	for (i = 0; i < num_databytes_in_chunk; i++)
		pbuf[devc->buflen++] = buf[1 + i];

	/* Now look for packets in that data. */
	while ((devc->buflen - devc->bufoffset) >= udmms[dmm].packet_size) {
		if (udmms[dmm].packet_valid(pbuf + devc->bufoffset)) {
			log_dmm_packet(pbuf + devc->bufoffset);
			decode_packet(sdi, dmm, pbuf + devc->bufoffset, info);
			devc->bufoffset += udmms[dmm].packet_size;
		} else {
			devc->bufoffset++;
		}
	}

	/* Move remaining bytes to beginning of buffer. */
	for (i = 0; i < devc->buflen - devc->bufoffset; i++)
		pbuf[i] = pbuf[devc->bufoffset + i];
	devc->buflen -= devc->bufoffset;

	return SR_OK;
}

static int receive_data(int fd, int revents, int dmm, void *info, void *cb_data)
{
	int ret;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	int64_t time_ms;

	(void)fd;
	(void)revents;

	sdi = cb_data;
	devc = sdi->priv;

	if ((ret = get_and_handle_data(sdi, dmm, info)) != SR_OK)
		return FALSE;

	/* Abort acquisition if we acquired enough samples. */
	if (devc->limit_samples && devc->num_samples >= devc->limit_samples) {
		sr_info("Requested number of samples reached.");
		sdi->driver->dev_acquisition_stop(sdi, cb_data);
	}

	if (devc->limit_msec) {
		time_ms = (g_get_monotonic_time() - devc->starttime) / 1000;
		if (time_ms > (int64_t)devc->limit_msec) {
			sr_info("Requested time limit reached.");
			sdi->driver->dev_acquisition_stop(sdi, cb_data);
			return TRUE;
		}
	}

	return TRUE;
}

#define RECEIVE_DATA(ID_UPPER, DMM_DRIVER) \
SR_PRIV int receive_data_##ID_UPPER(int fd, int revents, void *cb_data) { \
	struct DMM_DRIVER##_info info; \
	return receive_data(fd, revents, ID_UPPER, &info, cb_data); }

/* Driver-specific receive_data() wrappers */
RECEIVE_DATA(TECPEL_DMM_8060, fs9721)
RECEIVE_DATA(TECPEL_DMM_8061, fs9721)
RECEIVE_DATA(UNI_T_UT61D, fs9922)
RECEIVE_DATA(UNI_T_UT61E, es51922)
RECEIVE_DATA(VOLTCRAFT_VC820, fs9721)
RECEIVE_DATA(VOLTCRAFT_VC830, fs9922)
RECEIVE_DATA(VOLTCRAFT_VC840, fs9721)
