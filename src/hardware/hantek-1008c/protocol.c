/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2026 VK4TMZ
 *
 * Hantek 1008C USB protocol support.
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
#include "protocol.h"

static int usb_write(const struct sr_dev_inst *sdi, const uint8_t *buf, int len)
{
	struct sr_usb_dev_inst *usb = sdi->conn;
	int transferred, ret;

	transferred = 0;
	ret = libusb_bulk_transfer(usb->devhdl, H1008C_EP_OUT,
			(unsigned char *)buf, len, &transferred, H1008C_USB_TIMEOUT_MS);
	if (ret != LIBUSB_SUCCESS || transferred != len) {
		sr_err("USB write failed: %s (sent %d/%d).",
			libusb_error_name(ret), transferred, len);
		return SR_ERR;
	}
	return SR_OK;
}

static int usb_read(const struct sr_dev_inst *sdi, uint8_t *buf, int len, int *actual)
{
	struct sr_usb_dev_inst *usb = sdi->conn;
	int ret;

	*actual = 0;
	ret = libusb_bulk_transfer(usb->devhdl, H1008C_EP_IN,
			buf, len, actual, H1008C_USB_TIMEOUT_MS);
	if (ret != LIBUSB_SUCCESS) {
		sr_err("USB read failed: %s.", libusb_error_name(ret));
		return SR_ERR;
	}
	return SR_OK;
}

static int transact(const struct sr_dev_inst *sdi, const uint8_t *tx, int tx_len,
		uint8_t *rx, int rx_len, int *actual)
{
	int ret;

	ret = usb_write(sdi, tx, tx_len);
	if (ret != SR_OK)
		return ret;
	return usb_read(sdi, rx, rx_len, actual);
}

static int command(const struct sr_dev_inst *sdi, const uint8_t *tx, int tx_len)
{
	uint8_t rx[H1008C_USB_PACKET];
	int actual, ret;

	ret = transact(sdi, tx, tx_len, rx, sizeof(rx), &actual);
	if (ret != SR_OK)
		return ret;
	if (actual <= 0) {
		sr_err("Command 0x%02x returned no data.", tx[0]);
		return SR_ERR;
	}
	return SR_OK;
}

SR_PRIV int h1008c_open(struct sr_dev_inst *sdi)
{
	struct drv_context *drvc = sdi->driver->context;
	struct sr_usb_dev_inst *usb = sdi->conn;
	struct libusb_device_descriptor des;
	libusb_device **devlist;
	char connection_id[64];
	int count, i, ret;

	count = libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &devlist);
	if (count < 0)
		return SR_ERR;

	ret = SR_ERR;
	for (i = 0; i < count; i++) {
		libusb_get_device_descriptor(devlist[i], &des);
		if (des.idVendor != H1008C_USB_VID || des.idProduct != H1008C_USB_PID)
			continue;
		if (usb_get_port_path(devlist[i], connection_id, sizeof(connection_id)) < 0)
			continue;
		if (strcmp(sdi->connection_id, connection_id))
			continue;
		if (libusb_open(devlist[i], &usb->devhdl) != LIBUSB_SUCCESS)
			continue;
		usb->bus = libusb_get_bus_number(devlist[i]);
		usb->address = libusb_get_device_address(devlist[i]);
		ret = SR_OK;
		break;
	}
	libusb_free_device_list(devlist, 1);
	if (ret != SR_OK)
		return ret;

	ret = libusb_claim_interface(usb->devhdl, H1008C_USB_INTERFACE);
	if (ret != LIBUSB_SUCCESS) {
		sr_err("Unable to claim USB interface: %s.", libusb_error_name(ret));
		libusb_close(usb->devhdl);
		usb->devhdl = NULL;
		return SR_ERR;
	}
	sr_dbg("USB connection active at %u.%u (%s).", usb->bus, usb->address,
		sdi->connection_id ? sdi->connection_id : "unknown-port");
	return SR_OK;
}

SR_PRIV int h1008c_close(struct sr_dev_inst *sdi)
{
	struct sr_usb_dev_inst *usb = sdi->conn;

	if (!usb || !usb->devhdl)
		return SR_OK;
	libusb_release_interface(usb->devhdl, H1008C_USB_INTERFACE);
	libusb_close(usb->devhdl);
	usb->devhdl = NULL;
	return SR_OK;
}

SR_PRIV int h1008c_reopen(struct sr_dev_inst *sdi)
{
	h1008c_close(sdi);
	if (h1008c_open(sdi) != SR_OK)
		return SR_ERR;
	sr_dbg("Reacquired Hantek 1008C for new acquisition.");
	return SR_OK;
}

static int get_buffer_size(const struct sr_dev_inst *sdi, uint8_t selector,
		uint16_t *size)
{
	uint8_t tx[] = { 0xc6, selector };
	uint8_t rx[H1008C_USB_PACKET];
	int actual;

	if (transact(sdi, tx, sizeof(tx), rx, sizeof(rx), &actual) != SR_OK)
		return SR_ERR;
	if (actual != 2) {
		sr_err("C6 %02x returned %d bytes, expected 2.", selector, actual);
		return SR_ERR;
	}
	*size = ((uint16_t)rx[0] << 8) | rx[1];
	if (*size & 1) {
		sr_err("C6 %02x returned odd size %u.", selector, *size);
		return SR_ERR;
	}
	return SR_OK;
}

static int read_buffer(const struct sr_dev_inst *sdi, uint8_t selector,
		uint8_t *buf, uint16_t size)
{
	uint8_t tx[] = { 0xa6, selector };
	uint8_t pkt[H1008C_USB_PACKET];
	size_t done;
	int actual;

	done = 0;
	while (done < size) {
		if (usb_write(sdi, tx, sizeof(tx)) != SR_OK)
			return SR_ERR;
		if (usb_read(sdi, pkt, sizeof(pkt), &actual) != SR_OK)
			return SR_ERR;
		if (actual != H1008C_USB_PACKET) {
			sr_err("A6 %02x returned %d bytes, expected 64.", selector, actual);
			return SR_ERR;
		}
		memcpy(buf + done, pkt, MIN((size_t)actual, size - done));
		done += MIN((size_t)actual, size - done);
	}
	return SR_OK;
}

static int poll_ready(const struct sr_dev_inst *sdi, gboolean *ready,
		uint8_t *state_out)
{
	static const uint8_t a5[] = { 0xa5, 0x5a };
	uint8_t rx[H1008C_USB_PACKET];
	int actual;
	uint8_t state;

	*ready = FALSE;
	if (transact(sdi, a5, sizeof(a5), rx, sizeof(rx), &actual) != SR_OK)
		return SR_ERR;
	state = actual > 0 ? rx[actual - 1] : 0xff;
	if (state_out)
		*state_out = state;
	if (state == 0x02 || state == 0x03)
		*ready = TRUE;
	return SR_OK;
}

static int wait_ready(const struct sr_dev_inst *sdi)
{
	int i;
	gboolean ready;
	uint8_t state;

	for (i = 0; i < H1008C_A5_READY_POLLS; i++) {
		if (poll_ready(sdi, &ready, &state) != SR_OK)
			return SR_ERR;
		if (ready) {
			sr_dbg("A5 ready state=%u after %d poll(s).", state, i + 1);
			return SR_OK;
		}
		g_usleep(H1008C_A5_POLL_DELAY_US);
	}

	sr_err("A5 never reached ready state 2/3 within readiness window.");
	return SR_ERR;
}

static int read_current_buffers(const struct sr_dev_inst *sdi,
		uint8_t **raw, size_t *total)
{
	uint16_t size2, size3;
	uint8_t *buf;
	size_t len;

	*raw = NULL;
	*total = 0;
	if (get_buffer_size(sdi, 0x02, &size2) != SR_OK ||
	    get_buffer_size(sdi, 0x03, &size3) != SR_OK)
		return SR_ERR;
	len = (size_t)size2 + size3;
	if (!len || (len & 1)) {
		sr_err("Invalid combined capture size %zu.", len);
		return SR_ERR;
	}
	buf = g_try_malloc(len);
	if (!buf)
		return SR_ERR_MALLOC;
	if ((size2 && read_buffer(sdi, 0x02, buf, size2) != SR_OK) ||
	    (size3 && read_buffer(sdi, 0x03, buf + size2, size3) != SR_OK)) {
		g_free(buf);
		return SR_ERR;
	}
	*raw = buf;
	*total = len;
	return SR_OK;
}

static int startup_range_pass(const struct sr_dev_inst *sdi, uint8_t range)
{
	uint8_t a2[] = { 0xa2, range, range, range, range, range, range, range, range };
	static const uint8_t f3[] = { 0xf3 };
	static const uint8_t a4[] = { 0xa4, 0x01 };
	static const uint8_t c0[] = { 0xc0 };
	static const uint8_t c2[] = { 0xc2 };
	uint8_t *raw;
	size_t total;

	if (command(sdi, f3, sizeof(f3)) != SR_OK ||
	    command(sdi, a2, sizeof(a2)) != SR_OK ||
	    command(sdi, a4, sizeof(a4)) != SR_OK ||
	    command(sdi, c0, sizeof(c0)) != SR_OK)
		return SR_ERR;
	g_usleep(H1008C_CAL_CAPTURE_DELAY_US);
	if (command(sdi, c2, sizeof(c2)) != SR_OK || wait_ready(sdi) != SR_OK)
		return SR_ERR;
	if (read_current_buffers(sdi, &raw, &total) != SR_OK)
		return SR_ERR;
	sr_dbg("Startup range pass %u consumed %zu bytes (%zu words).",
		range, total, total / 2);
	g_free(raw);
	return SR_OK;
}

SR_PRIV int h1008c_startup(const struct sr_dev_inst *sdi, uint8_t selected_a3,
		uint8_t selected_range, unsigned int enabled_count,
		const uint8_t enabled_mask[H1008C_NUM_HW_CHANNELS])
{
	/* Full direct-ADC initialization validated against mfg92/hantek1008py. */
	static const uint8_t b0[] = { 0xb0 };
	static const uint8_t f3[] = { 0xf3 };
	static const uint8_t b9[] = { 0xb9, 0x01, 0xb0, 0x04, 0x00, 0x00 };
	static const uint8_t b7[] = { 0xb7, 0x00 };
	static const uint8_t bb[] = { 0xbb, 0x08, 0x00 };
	static const uint8_t b5[] = { 0xb5 };
	static const uint8_t b6[] = { 0xb6 };
	static const uint8_t e5[] = { 0xe5 };
	static const uint8_t f7[] = { 0xf7 };
	static const uint8_t f8[] = { 0xf8 };
	static const uint8_t fa[] = { 0xfa };
	static const uint8_t f5[] = { 0xf5 };
	static const uint8_t a0_all[] = { 0xa0, 0x08 };
	static const uint8_t aa_all[] = { 0xaa, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01 };
	static const uint8_t a3_default[] = { 0xa3, 0x11 };
	static const uint8_t c1[] = { 0xc1, 0x00, 0x00 };
	static const uint8_t a7[] = { 0xa7, 0x00, 0x00 };
	static const uint8_t ac_init[] = { 0xac, 0x01, 0xf4, 0x00, 0x09, 0xc5, 0x00, 0x09, 0xc5 };
	static const uint8_t f6[] = { 0xf6 };
	uint8_t a3_selected[] = { 0xa3, selected_a3 };
	static const uint8_t ac_pre[] = { 0xac, 0x00, 0xc8, 0x00, 0x02, 0xbd, 0x00, 0x02, 0xbd };
	static const uint8_t e4[] = { 0xe4, 0x01 };
	static const uint8_t e6[] = { 0xe6, 0x01 };
	uint8_t a0_selected[] = { 0xa0, (uint8_t)enabled_count };
	uint8_t aa_selected[] = { 0xaa, 0, 0, 0, 0, 0, 0, 0, 0 };
	uint8_t a2_selected[] = { 0xa2, 0, 0, 0, 0, 0, 0, 0, 0 };
	static const uint8_t ac_final[] = { 0xac, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x05, 0x79 };
	static const uint8_t ab[] = { 0xab, 0x08, 0x00 };
	static const uint8_t e9[] = { 0xe9 };
	struct cmd { const uint8_t *data; int len; };
	static const struct cmd init1_tail[] = {
		{ f3, sizeof(f3) }, { b9, sizeof(b9) }, { b7, sizeof(b7) },
		{ bb, sizeof(bb) }, { b5, sizeof(b5) }, { b6, sizeof(b6) },
		{ e5, sizeof(e5) }, { f7, sizeof(f7) }, { f8, sizeof(f8) },
		{ fa, sizeof(fa) }, { f5, sizeof(f5) }, { a0_all, sizeof(a0_all) },
		{ aa_all, sizeof(aa_all) }, { a3_default, sizeof(a3_default) },
		{ c1, sizeof(c1) }, { a7, sizeof(a7) }, { ac_init, sizeof(ac_init) },
	};
	const struct cmd init3_tail[] = {
		{ e5, sizeof(e5) }, { f7, sizeof(f7) }, { f8, sizeof(f8) },
		{ fa, sizeof(fa) }, { a3_selected, sizeof(a3_selected) },
		{ ac_pre, sizeof(ac_pre) }, { e4, sizeof(e4) }, { e6, sizeof(e6) },
		{ f3, sizeof(f3) }, { a0_selected, sizeof(a0_selected) },
		{ aa_selected, sizeof(aa_selected) }, { a2_selected, sizeof(a2_selected) },
		{ a3_selected, sizeof(a3_selected) }, { c1, sizeof(c1) }, { a7, sizeof(a7) },
		{ ac_final, sizeof(ac_final) }, { ab, sizeof(ab) }, { e9, sizeof(e9) },
	};
	size_t i;

	for (i = 0; i < H1008C_NUM_HW_CHANNELS; i++) {
		aa_selected[i + 1] = enabled_mask[i] ? 1 : 0;
		a2_selected[i + 1] = selected_range;
	}

	if (command(sdi, b0, sizeof(b0)) != SR_OK)
		return SR_ERR;
	g_usleep(H1008C_INIT_B0_DELAY_US);
	if (command(sdi, b0, sizeof(b0)) != SR_OK)
		return SR_ERR;
	for (i = 0; i < ARRAY_SIZE(init1_tail); i++) {
		if (command(sdi, init1_tail[i].data, init1_tail[i].len) != SR_OK)
			return SR_ERR;
	}

	for (i = 1; i <= 3; i++) {
		if (startup_range_pass(sdi, (uint8_t)i) != SR_OK)
			return SR_ERR;
	}

	if (command(sdi, f6, sizeof(f6)) != SR_OK)
		return SR_ERR;
	g_usleep(H1008C_F6_DELAY_US);
	for (i = 0; i < ARRAY_SIZE(init3_tail); i++) {
		if (command(sdi, init3_tail[i].data, init3_tail[i].len) != SR_OK)
			return SR_ERR;
	}
	sr_info("Hantek 1008C direct-ADC initialization complete "
		"(%u channel(s), A3=%02x, range=%s, A2=%02x).",
		enabled_count, selected_a3,
		selected_range == 1 ? "Narrow" :
		selected_range == 2 ? "Medium" : "Wide", selected_range);
	return SR_OK;
}

static int arm_triggered_capture(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	static const uint8_t f3[] = { 0xf3 };
	static const uint8_t e4[] = { 0xe4, 0x01 };
	static const uint8_t e6[] = { 0xe6, 0x01 };
	static const uint8_t a4[] = { 0xa4, 0x01 };
	static const uint8_t c0[] = { 0xc0 };
	uint8_t c1[] = { 0xc1, 0x00, 0x00 };
	uint8_t ab[] = { 0xab, 0x08, 0x00 };

	c1[1] = devc->trigger_source == H1008C_TRIGGER_SOURCE_NONE ?
		0 : devc->trigger_source;
	c1[2] = devc->trigger_slope == H1008C_TRIGGER_RISING ? 0x00 : 0x01;
	ab[1] = (uint8_t)(devc->trigger_level_adc >> 8);
	ab[2] = (uint8_t)(devc->trigger_level_adc & 0xff);
	if (command(sdi, c1, sizeof(c1)) != SR_OK ||
	    command(sdi, ab, sizeof(ab)) != SR_OK ||
	    command(sdi, f3, sizeof(f3)) != SR_OK ||
	    command(sdi, e4, sizeof(e4)) != SR_OK ||
	    command(sdi, e6, sizeof(e6)) != SR_OK ||
	    command(sdi, a4, sizeof(a4)) != SR_OK)
		return SR_ERR;
	if (H1008C_TRIGGERED_ARM_DELAY_US)
		g_usleep(H1008C_TRIGGERED_ARM_DELAY_US);
	if (command(sdi, c0, sizeof(c0)) != SR_OK)
		return SR_ERR;
	devc->triggered_armed = TRUE;
	devc->triggered_forced = FALSE;
	devc->triggered_arm_us = g_get_monotonic_time();
	if (!devc->triggered_count)
		sr_dbg("Triggered acquisition armed: policy=%s source=%s slope=%s "
			"AB=%04x.", devc->trigger_enabled ? "normal" : "auto",
			h1008c_trigger_source_name(devc->trigger_source),
			devc->trigger_slope == H1008C_TRIGGER_RISING ?
				"rising" : "falling", devc->trigger_level_adc);
	return SR_OK;
}

static int finish_triggered_capture(const struct sr_dev_inst *sdi)
{
	static const uint8_t e4[] = { 0xe4, 0x01 };
	static const uint8_t e6[] = { 0xe6, 0x01 };

	if (command(sdi, e4, sizeof(e4)) != SR_OK ||
	    command(sdi, e6, sizeof(e6)) != SR_OK)
		return SR_ERR;
	return SR_OK;
}

SR_PRIV int h1008c_abort_frame(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	static const uint8_t c2[] = { 0xc2 };

	if (!devc->triggered_armed)
		return SR_OK;
	if (!devc->triggered_forced) {
		sr_dbg("Aborting armed Triggered acquisition with C2.");
		if (command(sdi, c2, sizeof(c2)) != SR_OK)
			return SR_ERR;
	} else {
		sr_dbg("Clearing already-forced Triggered acquisition state during stop.");
	}
	devc->triggered_armed = FALSE;
	devc->triggered_forced = FALSE;
	devc->triggered_arm_us = 0;
	return SR_OK;
}

SR_PRIV int h1008c_acquire_triggered_frame(const struct sr_dev_inst *sdi,
		float **samples, size_t *sample_count)
{
	struct dev_context *devc = sdi->priv;
	static const uint8_t c2[] = { 0xc2 };
	uint8_t *raw;
	float *out;
	size_t total, i, n;
	gboolean ready;
	uint8_t state;
	gint64 elapsed;
	int ret;

	*samples = NULL;
	*sample_count = 0;
	if (!devc->triggered_armed && arm_triggered_capture(sdi) != SR_OK)
		return SR_ERR;

	if (poll_ready(sdi, &ready, &state) != SR_OK)
		return SR_ERR;
	if (!ready) {
		elapsed = g_get_monotonic_time() - devc->triggered_arm_us;
		if (!devc->trigger_enabled && !devc->triggered_forced &&
		    elapsed >= H1008C_AUTO_TRIGGER_TIMEOUT_US) {
			if (command(sdi, c2, sizeof(c2)) != SR_OK)
				return SR_ERR;
			devc->triggered_forced = TRUE;
			sr_dbg("Auto trigger timeout after %.1f ms; C2 forced completion.",
				(double)elapsed / 1000.0);
		}
		return SR_OK;
	}

	sr_dbg("Triggered acquisition ready state=%u after %.1f ms (%s).", state,
		(double)(g_get_monotonic_time() - devc->triggered_arm_us) / 1000.0,
		devc->triggered_forced ? "forced" : "hardware");
	ret = read_current_buffers(sdi, &raw, &total);
	if (finish_triggered_capture(sdi) != SR_OK) {
		g_free(raw);
		return SR_ERR;
	}
	devc->triggered_armed = FALSE;
	devc->triggered_forced = FALSE;
	devc->triggered_arm_us = 0;
	if (ret != SR_OK)
		return ret;

	n = total / 2;
	if (!devc->enabled_count || !devc->acquisition_width) {
		g_free(raw);
		return SR_ERR_BUG;
	}
	/*
	 * Multi-channel Triggered direct ADC is a fixed-width physical stream.
	 * Enabled AA channels are compacted in ascending physical-channel order;
	 * widths 4/6/8 contain one final dummy slot for logical counts 3/5/7.
	 * Emit complete rows only so every visible channel has equal sample count.
	 */
	*sample_count = n / devc->acquisition_width;
	out = g_try_new(float, (*sample_count) * devc->enabled_count);
	if (!out) {
		g_free(raw);
		return SR_ERR_MALLOC;
	}
	for (i = 0; i < *sample_count; i++) {
		size_t lane;
		for (lane = 0; lane < devc->enabled_count; lane++) {
			size_t word = i * devc->acquisition_width + lane;
			out[i * devc->enabled_count + lane] =
				(float)(((uint16_t)raw[word * 2] |
				((uint16_t)raw[word * 2 + 1] << 8)) & 0x0fff);
		}
	}
	g_free(raw);

	*samples = out;
	return SR_OK;
}

SR_PRIV int h1008c_start_roll(const struct sr_dev_inst *sdi, uint8_t a3_id)
{
	static const uint8_t f3[] = { 0xf3 };
	static const uint8_t a4[] = { 0xa4, 0x02 };
	static const uint8_t c0[] = { 0xc0 };
	static const uint8_t c2[] = { 0xc2 };
	uint8_t a3[] = { 0xa3, a3_id };

	/*
	 * Public hantek1008py sequence for continuous/roll acquisition:
	 * A3 <roll-rate-id>, short settle, F3, A4 02, C0, C2.  Data is then
	 * queried with C7 and drained with C8. Roll rows contain one little-endian
	 * ADC word for every enabled channel in ascending physical order, followed
	 * by one auxiliary word. The auxiliary slot is structural and is not sent
	 * as an analogue channel.
	 */
	if (command(sdi, a3, sizeof(a3)) != SR_OK)
		return SR_ERR;
	g_usleep(10 * 1000);
	if (command(sdi, f3, sizeof(f3)) != SR_OK ||
	    command(sdi, a4, sizeof(a4)) != SR_OK ||
	    command(sdi, c0, sizeof(c0)) != SR_OK ||
	    command(sdi, c2, sizeof(c2)) != SR_OK)
		return SR_ERR;

	sr_info("Hantek 1008C roll mode started (A3=%02x).", a3_id);
	return SR_OK;
}

SR_PRIV int h1008c_decode_stream_rows(const uint8_t *input, size_t input_len,
		unsigned int enabled_count, unsigned int trailing_words,
		uint8_t *carry, size_t *carry_len, size_t carry_capacity,
		float **samples, size_t *row_count)
{
	uint8_t *framed;
	float *out;
	size_t total, complete, row_bytes, rows, row, lane;

	if (!carry || !carry_len || !samples || !row_count ||
		(!input && input_len) || !enabled_count)
		return SR_ERR_ARG;
	*samples = NULL;
	*row_count = 0;
	row_bytes = 2 * (enabled_count + trailing_words);
	if (!row_bytes || *carry_len >= row_bytes ||
		carry_capacity < row_bytes - 1 || *carry_len > carry_capacity)
		return SR_ERR_ARG;

	total = *carry_len + input_len;
	if (!total)
		return SR_OK;
	framed = g_try_malloc(total);
	if (!framed)
		return SR_ERR_MALLOC;
	memcpy(framed, carry, *carry_len);
	memcpy(framed + *carry_len, input, input_len);
	complete = total - total % row_bytes;
	*carry_len = total - complete;
	if (*carry_len)
		memcpy(carry, framed + complete, *carry_len);
	if (!complete) {
		g_free(framed);
		return SR_OK;
	}

	rows = complete / row_bytes;
	out = g_try_new(float, rows * enabled_count);
	if (!out) {
		g_free(framed);
		return SR_ERR_MALLOC;
	}
	for (row = 0; row < rows; row++) {
		for (lane = 0; lane < enabled_count; lane++) {
			size_t offset = row * row_bytes + lane * 2;

			out[row * enabled_count + lane] =
				(float)(((uint16_t)framed[offset] |
				((uint16_t)framed[offset + 1] << 8)) & 0x0fff);
		}
	}
	g_free(framed);
	*samples = out;
	*row_count = rows;
	return SR_OK;
}

SR_PRIV int h1008c_read_roll(const struct sr_dev_inst *sdi,
		float **samples, size_t *sample_count)
{
	static const uint8_t f3[] = { 0xf3 };
	static const uint8_t c7[] = { 0xc7 };
	static const uint8_t c8[] = { 0xc8 };
	struct dev_context *devc = sdi->priv;
	uint8_t rx[H1008C_USB_PACKET], *raw;
	float *out;
	uint16_t ready_len;
	size_t done, rows;
	int actual;

	*samples = NULL;
	*sample_count = 0;

	if (command(sdi, f3, sizeof(f3)) != SR_OK)
		return SR_ERR;
	if (transact(sdi, c7, sizeof(c7), rx, sizeof(rx), &actual) != SR_OK)
		return SR_ERR;
	if (actual != 2) {
		sr_err("C7 returned %d bytes, expected 2.", actual);
		return SR_ERR;
	}
	ready_len = ((uint16_t)rx[0] << 8) | rx[1];
	if (!ready_len)
		return SR_OK;

	raw = g_try_malloc(ready_len);
	if (!raw)
		return SR_ERR_MALLOC;

	done = 0;
	while (done < ready_len) {
		if (usb_write(sdi, c8, sizeof(c8)) != SR_OK ||
		    usb_read(sdi, rx, sizeof(rx), &actual) != SR_OK) {
			g_free(raw);
			return SR_ERR;
		}
		if (actual != H1008C_USB_PACKET) {
			sr_err("C8 returned %d bytes, expected 64.", actual);
			g_free(raw);
			return SR_ERR;
		}
		memcpy(raw + done, rx, MIN((size_t)actual, (size_t)ready_len - done));
		done += MIN((size_t)actual, (size_t)ready_len - done);
	}

	actual = h1008c_decode_stream_rows(raw, ready_len,
		devc->enabled_count, 1, devc->roll_carry,
		&devc->roll_carry_len, sizeof(devc->roll_carry), &out, &rows);
	g_free(raw);
	if (actual != SR_OK)
		return actual;

	*samples = out;
	*sample_count = rows;
	return SR_OK;
}

SR_PRIV int h1008c_start_scan(const struct sr_dev_inst *sdi, uint8_t a3_id)
{
	static const uint8_t ac[] = {
		0xac, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01
	};
	static const uint8_t f3[] = { 0xf3 };
	static const uint8_t a4[] = { 0xa4, 0x01 };
	static const uint8_t e4[] = { 0xe4, 0x01 };
	static const uint8_t e6[] = { 0xe6, 0x01 };
	static const uint8_t c0[] = { 0xc0 };
	static const uint8_t c2[] = { 0xc2 };
	static const uint8_t a5[] = { 0xa5, 0x5a };
	uint8_t a3[] = { 0xa3, a3_id };
	gint64 deadline;

	/*
	 * Official Windows Scan Mode boundary and the Python protocol reference
	 * agree on this sequence for A3=1A..22: selected A3, AC 0/1/1,
	 * F3, A4 01, E4 01, E6 01, C0, then about 1.87 s of F3/A5 polling
	 * before C2.  Keep this path independent of diagnostic A4 02 + C7/C8
	 * ROLL; their identically sized 4-byte rows have different semantics.
	 */
	if (command(sdi, a3, sizeof(a3)) != SR_OK ||
	    command(sdi, ac, sizeof(ac)) != SR_OK ||
	    command(sdi, f3, sizeof(f3)) != SR_OK ||
	    command(sdi, a4, sizeof(a4)) != SR_OK ||
	    command(sdi, e4, sizeof(e4)) != SR_OK ||
	    command(sdi, e6, sizeof(e6)) != SR_OK ||
	    command(sdi, c0, sizeof(c0)) != SR_OK)
		return SR_ERR;

	deadline = g_get_monotonic_time() + G_TIME_SPAN_MILLISECOND * 1870;
	while (g_get_monotonic_time() < deadline) {
		if (command(sdi, f3, sizeof(f3)) != SR_OK ||
		    command(sdi, a5, sizeof(a5)) != SR_OK)
			return SR_ERR;
		g_usleep(10 * 1000);
	}
	if (command(sdi, c2, sizeof(c2)) != SR_OK)
		return SR_ERR;

	sr_info("Hantek 1008C official Scan mode started (A3=%02x).", a3_id);
	return SR_OK;
}

SR_PRIV int h1008c_read_scan(const struct sr_dev_inst *sdi,
		float **samples, size_t *sample_count)
{
	static const uint8_t f3[] = { 0xf3 };
	static const uint8_t a5[] = { 0xa5, 0x5a };
	static const uint8_t c9[] = { 0xc9 };
	static const uint8_t ca[] = { 0xca };
	struct dev_context *devc = sdi->priv;
	uint8_t rx[H1008C_USB_PACKET];
	float *out;
	uint16_t available;
	size_t rows;
	int actual;

	*samples = NULL;
	*sample_count = 0;

	if (command(sdi, f3, sizeof(f3)) != SR_OK ||
	    command(sdi, a5, sizeof(a5)) != SR_OK)
		return SR_ERR;
	if (transact(sdi, c9, sizeof(c9), rx, sizeof(rx), &actual) != SR_OK)
		return SR_ERR;
	if (actual != 2) {
		sr_err("C9 returned %d bytes, expected 2.", actual);
		return SR_ERR;
	}
	available = ((uint16_t)rx[0] << 8) | rx[1];
	if (!available)
		return SR_OK;

	/*
	 * Steady-state C9 values 1..64 describe the valid prefix of exactly one
	 * fixed 64-byte CA response.  Larger startup values have different,
	 * unresolved semantics; consume one CA packet to preserve device state but
	 * quarantine it from the analogue stream.
	 */
	if (usb_write(sdi, ca, sizeof(ca)) != SR_OK ||
	    usb_read(sdi, rx, sizeof(rx), &actual) != SR_OK)
		return SR_ERR;
	if (actual != H1008C_USB_PACKET) {
		sr_err("CA returned %d bytes, expected 64.", actual);
		return SR_ERR;
	}
	if (available > H1008C_USB_PACKET) {
		sr_dbg("C9 startup/oversize value %u quarantined after one CA packet.",
			available);
		if (devc->scan_carry_len) {
			sr_warn("Discarding %zu partial Scan byte(s) across "
				"quarantined C9 discontinuity.", devc->scan_carry_len);
			devc->scan_carry_len = 0;
		}
		return SR_OK;
	}

	/*
	 * A3=1A captures with one through eight enabled channels establish a stream
	 * of little-endian ADC words interleaved across exactly the enabled channel
	 * count. Unlike Triggered mode, odd Scan counts have no dummy lane. Decode
	 * only that structural ordering; do not filter, average, interpolate, or
	 * otherwise modify acquired values.
	 */
	actual = h1008c_decode_stream_rows(rx, available,
		devc->enabled_count, 0, devc->scan_carry,
		&devc->scan_carry_len, sizeof(devc->scan_carry), &out, &rows);
	if (actual != SR_OK)
		return actual;

	*samples = out;
	*sample_count = rows;
	return SR_OK;
}
