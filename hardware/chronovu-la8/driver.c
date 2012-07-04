/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2011-2012 Uwe Hermann <uwe@hermann-uwe.de>
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

#include <ftdi.h>
#include <glib.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"
#include "driver.h"

/* Probes are numbered 0-7. */
SR_PRIV const char *probe_names[NUM_PROBES + 1] = {
	"0",
	"1",
	"2",
	"3",
	"4",
	"5",
	"6",
	"7",
	NULL,
};

/* This will be initialized via hw_dev_info_get()/SR_DI_SAMPLERATES. */
SR_PRIV uint64_t supported_samplerates[255 + 1] = { 0 };

/*
 * Min: 1 sample per 0.01us -> sample time is 0.084s, samplerate 100MHz
 * Max: 1 sample per 2.55us -> sample time is 21.391s, samplerate 392.15kHz
 */
const struct sr_samplerates samplerates = {
	.low  = 0,
	.high = 0,
	.step = 0,
	.list = supported_samplerates,
};

/* Note: Continuous sampling is not supported by the hardware. */
SR_PRIV const int hwcaps[] = {
	SR_HWCAP_LOGIC_ANALYZER,
	SR_HWCAP_SAMPLERATE,
	SR_HWCAP_LIMIT_MSEC, /* TODO: Not yet implemented. */
	SR_HWCAP_LIMIT_SAMPLES, /* TODO: Not yet implemented. */
	0,
};

SR_PRIV void fill_supported_samplerates_if_needed(void)
{
	int i;

	/* Do nothing if supported_samplerates[] is already filled. */
	if (supported_samplerates[0] != 0)
		return;

	/* Fill supported_samplerates[] with the proper values. */
	for (i = 0; i < 255; i++)
		supported_samplerates[254 - i] = SR_MHZ(100) / (i + 1);
	supported_samplerates[255] = 0;
}

/**
 * Check if the given samplerate is supported by the LA8 hardware.
 *
 * @param samplerate The samplerate (in Hz) to check.
 * @return 1 if the samplerate is supported/valid, 0 otherwise.
 */
SR_PRIV int is_valid_samplerate(uint64_t samplerate)
{
	int i;

	fill_supported_samplerates_if_needed();

	for (i = 0; i < 255; i++) {
		if (supported_samplerates[i] == samplerate)
			return 1;
	}

	sr_err("la8: %s: invalid samplerate (%" PRIu64 "Hz)",
	       __func__, samplerate);

	return 0;
}

/**
 * Convert a samplerate (in Hz) to the 'divcount' value the LA8 wants.
 *
 * LA8 hardware: sample period = (divcount + 1) * 10ns.
 * Min. value for divcount: 0x00 (10ns sample period, 100MHz samplerate).
 * Max. value for divcount: 0xfe (2550ns sample period, 392.15kHz samplerate).
 *
 * @param samplerate The samplerate in Hz.
 * @return The divcount value as needed by the hardware, or 0xff upon errors.
 */
SR_PRIV uint8_t samplerate_to_divcount(uint64_t samplerate)
{
	if (samplerate == 0) {
		sr_err("la8: %s: samplerate was 0", __func__);
		return 0xff;
	}

	if (!is_valid_samplerate(samplerate)) {
		sr_err("la8: %s: can't get divcount, samplerate invalid",
		       __func__);
		return 0xff;
	}

	return (SR_MHZ(100) / samplerate) - 1;
}

/**
 * Write data of a certain length to the LA8's FTDI device.
 *
 * @param ctx The struct containing private per-device-instance data. Must not
 *            be NULL. ctx->ftdic must not be NULL either.
 * @param buf The buffer containing the data to write. Must not be NULL.
 * @param size The number of bytes to write. Must be >= 0.
 * @return The number of bytes written, or a negative value upon errors.
 */
SR_PRIV int la8_write(struct context *ctx, uint8_t *buf, int size)
{
	int bytes_written;

	/* Note: Caller checked that ctx and ctx->ftdic != NULL. */

	if (!buf) {
		sr_err("la8: %s: buf was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (size < 0) {
		sr_err("la8: %s: size was < 0", __func__);
		return SR_ERR_ARG;
	}

	bytes_written = ftdi_write_data(ctx->ftdic, buf, size);

	if (bytes_written < 0) {
		sr_err("la8: %s: ftdi_write_data: (%d) %s", __func__,
		       bytes_written, ftdi_get_error_string(ctx->ftdic));
		(void) la8_close_usb_reset_sequencer(ctx); /* Ignore errors. */
	} else if (bytes_written != size) {
		sr_err("la8: %s: bytes to write: %d, bytes written: %d",
		       __func__, size, bytes_written);
		(void) la8_close_usb_reset_sequencer(ctx); /* Ignore errors. */
	}

	return bytes_written;
}

/**
 * Read a certain amount of bytes from the LA8's FTDI device.
 *
 * @param ctx The struct containing private per-device-instance data. Must not
 *            be NULL. ctx->ftdic must not be NULL either.
 * @param buf The buffer where the received data will be stored. Must not
 *            be NULL.
 * @param size The number of bytes to read. Must be >= 1.
 * @return The number of bytes read, or a negative value upon errors.
 */
SR_PRIV int la8_read(struct context *ctx, uint8_t *buf, int size)
{
	int bytes_read;

	/* Note: Caller checked that ctx and ctx->ftdic != NULL. */

	if (!buf) {
		sr_err("la8: %s: buf was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (size <= 0) {
		sr_err("la8: %s: size was <= 0", __func__);
		return SR_ERR_ARG;
	}

	bytes_read = ftdi_read_data(ctx->ftdic, buf, size);

	if (bytes_read < 0) {
		sr_err("la8: %s: ftdi_read_data: (%d) %s", __func__,
		       bytes_read, ftdi_get_error_string(ctx->ftdic));
	} else if (bytes_read != size) {
		// sr_err("la8: %s: bytes to read: %d, bytes read: %d",
		//        __func__, size, bytes_read);
	}

	return bytes_read;
}

SR_PRIV int la8_close(struct context *ctx)
{
	int ret;

	if (!ctx) {
		sr_err("la8: %s: ctx was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (!ctx->ftdic) {
		sr_err("la8: %s: ctx->ftdic was NULL", __func__);
		return SR_ERR_ARG;
	}

	if ((ret = ftdi_usb_close(ctx->ftdic)) < 0) {
		sr_err("la8: %s: ftdi_usb_close: (%d) %s",
		       __func__, ret, ftdi_get_error_string(ctx->ftdic));
	}

	return ret;
}

/**
 * Close the ChronoVu LA8 USB port and reset the LA8 sequencer logic.
 *
 * @param ctx The struct containing private per-device-instance data.
 * @return SR_OK upon success, SR_ERR_ARG upon invalid arguments.
 */
SR_PRIV int la8_close_usb_reset_sequencer(struct context *ctx)
{
	/* Magic sequence of bytes for resetting the LA8 sequencer logic. */
	uint8_t buf[8] = {0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01};
	int ret;

	if (!ctx) {
		sr_err("la8: %s: ctx was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (!ctx->ftdic) {
		sr_err("la8: %s: ctx->ftdic was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (ctx->ftdic->usb_dev) {
		/* Reset the LA8 sequencer logic, then wait 100ms. */
		sr_dbg("la8: Resetting sequencer logic.");
		(void) la8_write(ctx, buf, 8); /* Ignore errors. */
		g_usleep(100 * 1000);

		/* Purge FTDI buffers, then reset and close the FTDI device. */
		sr_dbg("la8: Purging buffers, resetting+closing FTDI device.");

		/* Log errors, but ignore them (i.e., don't abort). */
		if ((ret = ftdi_usb_purge_buffers(ctx->ftdic)) < 0)
			sr_err("la8: %s: ftdi_usb_purge_buffers: (%d) %s",
			    __func__, ret, ftdi_get_error_string(ctx->ftdic));
		if ((ret = ftdi_usb_reset(ctx->ftdic)) < 0)
			sr_err("la8: %s: ftdi_usb_reset: (%d) %s", __func__,
			       ret, ftdi_get_error_string(ctx->ftdic));
		if ((ret = ftdi_usb_close(ctx->ftdic)) < 0)
			sr_err("la8: %s: ftdi_usb_close: (%d) %s", __func__,
			       ret, ftdi_get_error_string(ctx->ftdic));
	}

	/* Close USB device, deinitialize and free the FTDI context. */
	ftdi_free(ctx->ftdic); /* Returns void. */
	ctx->ftdic = NULL;

	return SR_OK;
}

/**
 * Reset the ChronoVu LA8.
 *
 * The LA8 must be reset after a failed read/write operation or upon timeouts.
 *
 * @param ctx The struct containing private per-device-instance data.
 * @return SR_OK upon success, SR_ERR upon failure.
 */
SR_PRIV int la8_reset(struct context *ctx)
{
	uint8_t buf[BS];
	time_t done, now;
	int bytes_read;

	if (!ctx) {
		sr_err("la8: %s: ctx was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (!ctx->ftdic) {
		sr_err("la8: %s: ctx->ftdic was NULL", __func__);
		return SR_ERR_ARG;
	}

	sr_dbg("la8: Resetting the device.");

	/*
	 * Purge pending read data from the FTDI hardware FIFO until
	 * no more data is left, or a timeout occurs (after 20s).
	 */
	done = 20 + time(NULL);
	do {
		/* TODO: Ignore errors? Check for < 0 at least! */
		bytes_read = la8_read(ctx, (uint8_t *)&buf, BS);
		now = time(NULL);
	} while ((done > now) && (bytes_read > 0));

	/* Reset the LA8 sequencer logic and close the USB port. */
	(void) la8_close_usb_reset_sequencer(ctx); /* Ignore errors. */

	sr_dbg("la8: Device reset finished.");

	return SR_OK;
}

SR_PRIV int configure_probes(struct context *ctx, const GSList *probes)
{
	const struct sr_probe *probe;
	const GSList *l;
	uint8_t probe_bit;
	char *tc;

	/* Note: Caller checked that ctx != NULL. */

	ctx->trigger_pattern = 0;
	ctx->trigger_mask = 0; /* Default to "don't care" for all probes. */

	for (l = probes; l; l = l->next) {
		probe = (struct sr_probe *)l->data;

		if (!probe) {
			sr_err("la8: %s: probe was NULL", __func__);
			return SR_ERR;
		}

		/* Skip disabled probes. */
		if (!probe->enabled)
			continue;

		/* Skip (enabled) probes with no configured trigger. */
		if (!probe->trigger)
			continue;

		/* Note: Must only be run if probe->trigger != NULL. */
		if (probe->index < 0 || probe->index > 7) {
			sr_err("la8: %s: invalid probe index %d, must be "
			       "between 0 and 7", __func__, probe->index);
			return SR_ERR;
		}

		probe_bit = (1 << (probe->index - 1));

		/* Configure the probe's trigger mask and trigger pattern. */
		for (tc = probe->trigger; tc && *tc; tc++) {
			ctx->trigger_mask |= probe_bit;

			/* Sanity check, LA8 only supports low/high trigger. */
			if (*tc != '0' && *tc != '1') {
				sr_err("la8: %s: invalid trigger '%c', only "
				       "'0'/'1' supported", __func__, *tc);
				return SR_ERR;
			}

			if (*tc == '1')
				ctx->trigger_pattern |= probe_bit;
		}
	}

	sr_dbg("la8: trigger_mask = 0x%x, trigger_pattern = 0x%x",
	       ctx->trigger_mask, ctx->trigger_pattern);

	return SR_OK;
}

SR_PRIV int set_samplerate(struct sr_dev_inst *sdi, uint64_t samplerate)
{
	struct context *ctx;

	/* Note: Caller checked that sdi and sdi->priv != NULL. */

	ctx = sdi->priv;

	sr_spew("la8: Trying to set samplerate to %" PRIu64 "Hz.", samplerate);

	fill_supported_samplerates_if_needed();

	/* Check if this is a samplerate supported by the hardware. */
	if (!is_valid_samplerate(samplerate))
		return SR_ERR;

	/* Set the new samplerate. */
	ctx->cur_samplerate = samplerate;

	sr_dbg("la8: Samplerate set to %" PRIu64 "Hz.", ctx->cur_samplerate);

	return SR_OK;
}

/**
 * Get a block of data from the LA8.
 *
 * @param ctx The struct containing private per-device-instance data. Must not
 *            be NULL. ctx->ftdic must not be NULL either.
 * @return SR_OK upon success, or SR_ERR upon errors.
 */
SR_PRIV int la8_read_block(struct context *ctx)
{
	int i, byte_offset, m, mi, p, index, bytes_read;
	time_t now;

	/* Note: Caller checked that ctx and ctx->ftdic != NULL. */

	sr_spew("la8: Reading block %d.", ctx->block_counter);

	bytes_read = la8_read(ctx, ctx->mangled_buf, BS);

	/* If first block read got 0 bytes, retry until success or timeout. */
	if ((bytes_read == 0) && (ctx->block_counter == 0)) {
		do {
			sr_spew("la8: Reading block 0 (again).");
			bytes_read = la8_read(ctx, ctx->mangled_buf, BS);
			/* TODO: How to handle read errors here? */
			now = time(NULL);
		} while ((ctx->done > now) && (bytes_read == 0));
	}

	/* Check if block read was successful or a timeout occured. */
	if (bytes_read != BS) {
		sr_err("la8: Trigger timed out. Bytes read: %d.", bytes_read);
		(void) la8_reset(ctx); /* Ignore errors. */
		return SR_ERR;
	}

	/* De-mangle the data. */
	sr_spew("la8: Demangling block %d.", ctx->block_counter);
	byte_offset = ctx->block_counter * BS;
	m = byte_offset / (1024 * 1024);
	mi = m * (1024 * 1024);
	for (i = 0; i < BS; i++) {
		p = i & (1 << 0);
		index = m * 2 + (((byte_offset + i) - mi) / 2) * 16;
		index += (ctx->divcount == 0) ? p : (1 - p);
		ctx->final_buf[index] = ctx->mangled_buf[i];
	}

	return SR_OK;
}

SR_PRIV void send_block_to_session_bus(struct context *ctx, int block)
{
	int i;
	uint8_t sample, expected_sample;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	int trigger_point; /* Relative trigger point (in this block). */

	/* Note: No sanity checks on ctx/block, caller is responsible. */

	/* Check if we can find the trigger condition in this block. */
	trigger_point = -1;
	expected_sample = ctx->trigger_pattern & ctx->trigger_mask;
	for (i = 0; i < BS; i++) {
		/* Don't continue if the trigger was found previously. */
		if (ctx->trigger_found)
			break;

		/*
		 * Also, don't continue if triggers are "don't care", i.e. if
		 * no trigger conditions were specified by the user. In that
		 * case we don't want to send an SR_DF_TRIGGER packet at all.
		 */
		if (ctx->trigger_mask == 0x00)
			break;

		sample = *(ctx->final_buf + (block * BS) + i);

		if ((sample & ctx->trigger_mask) == expected_sample) {
			trigger_point = i;
			ctx->trigger_found = 1;
			break;
		}
	}

	/* If no trigger was found, send one SR_DF_LOGIC packet. */
	if (trigger_point == -1) {
		/* Send an SR_DF_LOGIC packet to the session bus. */
		sr_spew("la8: sending SR_DF_LOGIC packet (%d bytes) for "
		        "block %d", BS, block);
		packet.type = SR_DF_LOGIC;
		packet.payload = &logic;
		logic.length = BS;
		logic.unitsize = 1;
		logic.data = ctx->final_buf + (block * BS);
		sr_session_send(ctx->session_dev_id, &packet);
		return;
	}

	/*
	 * We found the trigger, so some special handling is needed. We have
	 * to send an SR_DF_LOGIC packet with the samples before the trigger
	 * (if any), then the SD_DF_TRIGGER packet itself, then another
	 * SR_DF_LOGIC packet with the samples after the trigger (if any).
	 */

	/* TODO: Send SR_DF_TRIGGER packet before or after the actual sample? */

	/* If at least one sample is located before the trigger... */
	if (trigger_point > 0) {
		/* Send pre-trigger SR_DF_LOGIC packet to the session bus. */
		sr_spew("la8: sending pre-trigger SR_DF_LOGIC packet, "
			"start = %d, length = %d", block * BS, trigger_point);
		packet.type = SR_DF_LOGIC;
		packet.payload = &logic;
		logic.length = trigger_point;
		logic.unitsize = 1;
		logic.data = ctx->final_buf + (block * BS);
		sr_session_send(ctx->session_dev_id, &packet);
	}

	/* Send the SR_DF_TRIGGER packet to the session bus. */
	sr_spew("la8: sending SR_DF_TRIGGER packet, sample = %d",
		(block * BS) + trigger_point);
	packet.type = SR_DF_TRIGGER;
	packet.payload = NULL;
	sr_session_send(ctx->session_dev_id, &packet);

	/* If at least one sample is located after the trigger... */
	if (trigger_point < (BS - 1)) {
		/* Send post-trigger SR_DF_LOGIC packet to the session bus. */
		sr_spew("la8: sending post-trigger SR_DF_LOGIC packet, "
			"start = %d, length = %d",
			(block * BS) + trigger_point, BS - trigger_point);
		packet.type = SR_DF_LOGIC;
		packet.payload = &logic;
		logic.length = BS - trigger_point;
		logic.unitsize = 1;
		logic.data = ctx->final_buf + (block * BS) + trigger_point;
		sr_session_send(ctx->session_dev_id, &packet);
	}
}
