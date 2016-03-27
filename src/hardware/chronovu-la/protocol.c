/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2011-2014 Uwe Hermann <uwe@hermann-uwe.de>
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

#include <config.h>
#include "protocol.h"

SR_PRIV const struct cv_profile cv_profiles[] = {
	{ CHRONOVU_LA8,  "LA8",  "ChronoVu LA8",  8,  SR_MHZ(100), 2, 0.8388608 },
	{ CHRONOVU_LA16, "LA16", "ChronoVu LA16", 16, SR_MHZ(200), 4, 0.042 },
	ALL_ZERO
};

/* LA8: channels are numbered 0-7. LA16: channels are numbered 0-15. */
SR_PRIV const char *cv_channel_names[] = {
	"0", "1", "2", "3", "4", "5", "6", "7",
	"8", "9", "10", "11", "12", "13", "14", "15",
};

static int close_usb_reset_sequencer(struct dev_context *devc);

SR_PRIV void cv_fill_samplerates_if_needed(const struct sr_dev_inst *sdi)
{
	int i;
	struct dev_context *devc;

	devc = sdi->priv;

	if (devc->samplerates[0] != 0)
		return;

	for (i = 0; i < 255; i++)
		devc->samplerates[254 - i] = devc->prof->max_samplerate / (i + 1);
}

/**
 * Check if the given samplerate is supported by the hardware.
 *
 * @param sdi Device instance.
 * @param samplerate The samplerate (in Hz) to check.
 *
 * @return 1 if the samplerate is supported/valid, 0 otherwise.
 */
static int is_valid_samplerate(const struct sr_dev_inst *sdi,
			       uint64_t samplerate)
{
	int i;
	struct dev_context *devc;

	devc = sdi->priv;

	cv_fill_samplerates_if_needed(sdi);

	for (i = 0; i < 255; i++) {
		if (devc->samplerates[i] == samplerate)
			return 1;
	}

	sr_err("Invalid samplerate (%" PRIu64 "Hz).", samplerate);

	return 0;
}

/**
 * Convert a samplerate (in Hz) to the 'divcount' value the device wants.
 *
 * The divcount value can be 0x00 - 0xfe (0xff is not valid).
 *
 * LA8:
 * sample period = (divcount + 1) * 10ns.
 * divcount = 0x00: 10ns period, 100MHz samplerate.
 * divcount = 0xfe: 2550ns period, 392.15kHz samplerate.
 *
 * LA16:
 * sample period = (divcount + 1) * 5ns.
 * divcount = 0x00: 5ns period, 200MHz samplerate.
 * divcount = 0xfe: 1275ns period, ~784.31kHz samplerate.
 *
 * @param sdi Device instance.
 * @param samplerate The samplerate in Hz.
 *
 * @return The divcount value as needed by the hardware, or 0xff upon errors.
 */
SR_PRIV uint8_t cv_samplerate_to_divcount(const struct sr_dev_inst *sdi,
					  uint64_t samplerate)
{
	struct dev_context *devc;

	devc = sdi->priv;

	if (samplerate == 0) {
		sr_err("Can't convert invalid samplerate of 0 Hz.");
		return 0xff;
	}

	if (!is_valid_samplerate(sdi, samplerate)) {
		sr_err("Can't get divcount, samplerate invalid.");
		return 0xff;
	}

	return (devc->prof->max_samplerate / samplerate) - 1;
}

/**
 * Write data of a certain length to the FTDI device.
 *
 * @param devc The struct containing private per-device-instance data. Must not
 *             be NULL. devc->ftdic must not be NULL either.
 * @param buf The buffer containing the data to write. Must not be NULL.
 * @param size The number of bytes to write. Must be > 0.
 *
 * @return The number of bytes written, or a negative value upon errors.
 */
SR_PRIV int cv_write(struct dev_context *devc, uint8_t *buf, int size)
{
	int bytes_written;

	/* Note: Caller ensures devc/devc->ftdic/buf != NULL and size > 0. */

	bytes_written = ftdi_write_data(devc->ftdic, buf, size);

	if (bytes_written < 0) {
		sr_err("Failed to write data (%d): %s.",
		       bytes_written, ftdi_get_error_string(devc->ftdic));
		(void) close_usb_reset_sequencer(devc); /* Ignore errors. */
	} else if (bytes_written != size) {
		sr_err("Failed to write data, only %d/%d bytes written.",
		       size, bytes_written);
		(void) close_usb_reset_sequencer(devc); /* Ignore errors. */
	}

	return bytes_written;
}

/**
 * Read a certain amount of bytes from the FTDI device.
 *
 * @param devc The struct containing private per-device-instance data. Must not
 *             be NULL. devc->ftdic must not be NULL either.
 * @param buf The buffer where the received data will be stored. Must not
 *            be NULL.
 * @param size The number of bytes to read. Must be >= 1.
 *
 * @return The number of bytes read, or a negative value upon errors.
 */
static int cv_read(struct dev_context *devc, uint8_t *buf, int size)
{
	int bytes_read;

	/* Note: Caller ensures devc/devc->ftdic/buf != NULL and size > 0. */

	bytes_read = ftdi_read_data(devc->ftdic, buf, size);

	if (bytes_read < 0) {
		sr_err("Failed to read data (%d): %s.",
		       bytes_read, ftdi_get_error_string(devc->ftdic));
	} else if (bytes_read != size) {
		// sr_err("Failed to read data, only %d/%d bytes read.",
		//        bytes_read, size);
	}

	return bytes_read;
}

/**
 * Close the USB port and reset the sequencer logic.
 *
 * @param devc The struct containing private per-device-instance data.
 *
 * @return SR_OK upon success, SR_ERR_ARG upon invalid arguments.
 */
static int close_usb_reset_sequencer(struct dev_context *devc)
{
	/* Magic sequence of bytes for resetting the sequencer logic. */
	uint8_t buf[8] = {0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01};
	int ret;

	/* Note: Caller checked that devc and devc->ftdic != NULL. */

	if (devc->ftdic->usb_dev) {
		/* Reset the sequencer logic, then wait 100ms. */
		sr_dbg("Resetting sequencer logic.");
		(void) cv_write(devc, buf, 8); /* Ignore errors. */
		g_usleep(100 * 1000);

		/* Purge FTDI buffers, then reset and close the FTDI device. */
		sr_dbg("Purging buffers, resetting+closing FTDI device.");

		/* Log errors, but ignore them (i.e., don't abort). */
		if ((ret = ftdi_usb_purge_buffers(devc->ftdic)) < 0)
			sr_err("Failed to purge FTDI buffers (%d): %s.",
			       ret, ftdi_get_error_string(devc->ftdic));
		if ((ret = ftdi_usb_reset(devc->ftdic)) < 0)
			sr_err("Failed to reset FTDI device (%d): %s.",
			       ret, ftdi_get_error_string(devc->ftdic));
		if ((ret = ftdi_usb_close(devc->ftdic)) < 0)
			sr_err("Failed to close FTDI device (%d): %s.",
			       ret, ftdi_get_error_string(devc->ftdic));
	}

	/* Close USB device, deinitialize and free the FTDI context. */
	ftdi_free(devc->ftdic);
	devc->ftdic = NULL;

	return SR_OK;
}

/**
 * Reset the ChronoVu device.
 *
 * A reset is required after a failed read/write operation or upon timeouts.
 *
 * @param devc The struct containing private per-device-instance data.
 *
 * @return SR_OK upon success, SR_ERR upon failure.
 */
static int reset_device(struct dev_context *devc)
{
	uint8_t buf[BS];
	gint64 done, now;
	int bytes_read;

	/* Note: Caller checked that devc and devc->ftdic != NULL. */

	sr_dbg("Resetting the device.");

	/*
	 * Purge pending read data from the FTDI hardware FIFO until
	 * no more data is left, or a timeout occurs (after 20s).
	 */
	done = (20 * G_TIME_SPAN_SECOND) + g_get_monotonic_time();
	do {
		/* Try to read bytes until none are left (or errors occur). */
		bytes_read = cv_read(devc, (uint8_t *)&buf, BS);
		now = g_get_monotonic_time();
	} while ((done > now) && (bytes_read > 0));

	/* Reset the sequencer logic and close the USB port. */
	(void) close_usb_reset_sequencer(devc); /* Ignore errors. */

	sr_dbg("Device reset finished.");

	return SR_OK;
}

SR_PRIV int cv_convert_trigger(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_trigger *trigger;
	struct sr_trigger_stage *stage;
	struct sr_trigger_match *match;
	const GSList *l, *m;
	uint16_t channel_bit;

	devc = sdi->priv;
	devc->trigger_pattern = 0x0000; /* Default to "low" trigger. */
	devc->trigger_mask = 0x0000; /* Default to "don't care". */
	devc->trigger_edgemask = 0x0000; /* Default to "state triggered". */

	if (!(trigger = sr_session_trigger_get(sdi->session)))
		return SR_OK;

	if (g_slist_length(trigger->stages) > 1) {
		sr_err("This device only supports 1 trigger stage.");
		return SR_ERR;
	}

	for (l = trigger->stages; l; l = l->next) {
		stage = l->data;
		for (m = stage->matches; m; m = m->next) {
			match = m->data;
			if (!match->channel->enabled)
				/* Ignore disabled channels with a trigger. */
				continue;
			if (devc->prof->model == CHRONOVU_LA8 &&
					(match->match == SR_TRIGGER_RISING
					|| match->match == SR_TRIGGER_FALLING)) {
				sr_err("This model supports only simple triggers.");
				return SR_ERR;
			}
			channel_bit = (1 << (match->channel->index));

			/* state: 1 == high, edge: 1 == rising edge. */
			if (match->match == SR_TRIGGER_ONE
					|| match->match == SR_TRIGGER_RISING)
				devc->trigger_pattern |= channel_bit;

			/* LA16 (but not LA8) supports edge triggering. */
			if ((devc->prof->model == CHRONOVU_LA16)) {
				if (match->match == SR_TRIGGER_RISING
						|| match->match == SR_TRIGGER_FALLING)
						devc->trigger_edgemask |= channel_bit;
			}
		}
	}

	sr_dbg("Trigger pattern/mask/edgemask = 0x%04x / 0x%04x / 0x%04x.",
			devc->trigger_pattern, devc->trigger_mask,
			devc->trigger_edgemask);

	return SR_OK;
}

SR_PRIV int cv_set_samplerate(const struct sr_dev_inst *sdi, uint64_t samplerate)
{
	struct dev_context *devc;

	/* Note: Caller checked that sdi and sdi->priv != NULL. */

	devc = sdi->priv;

	sr_spew("Trying to set samplerate to %" PRIu64 "Hz.", samplerate);

	cv_fill_samplerates_if_needed(sdi);

	/* Check if this is a samplerate supported by the hardware. */
	if (!is_valid_samplerate(sdi, samplerate)) {
		sr_dbg("Failed to set invalid samplerate (%" PRIu64 "Hz).",
		       samplerate);
		return SR_ERR;
	}

	devc->cur_samplerate = samplerate;

	sr_dbg("Samplerate set to %" PRIu64 "Hz.", devc->cur_samplerate);

	return SR_OK;
}

/**
 * Get a block of data from the device.
 *
 * @param devc The struct containing private per-device-instance data. Must not
 *             be NULL. devc->ftdic must not be NULL either.
 *
 * @return SR_OK upon success, or SR_ERR upon errors.
 */
SR_PRIV int cv_read_block(struct dev_context *devc)
{
	int i, byte_offset, m, mi, p, q, index, bytes_read;
	gint64 now;

	/* Note: Caller checked that devc and devc->ftdic != NULL. */

	sr_spew("Reading block %d.", devc->block_counter);

	bytes_read = cv_read(devc, devc->mangled_buf, BS);

	/* If first block read got 0 bytes, retry until success or timeout. */
	if ((bytes_read == 0) && (devc->block_counter == 0)) {
		do {
			sr_spew("Reading block 0 (again).");
			/* Note: If bytes_read < 0 cv_read() will log errors. */
			bytes_read = cv_read(devc, devc->mangled_buf, BS);
			now = g_get_monotonic_time();
		} while ((devc->done > now) && (bytes_read == 0));
	}

	/* Check if block read was successful or a timeout occurred. */
	if (bytes_read != BS) {
		sr_err("Trigger timed out. Bytes read: %d.", bytes_read);
		(void) reset_device(devc); /* Ignore errors. */
		return SR_ERR;
	}

	/* De-mangle the data. */
	sr_spew("Demangling block %d.", devc->block_counter);
	byte_offset = devc->block_counter * BS;
	m = byte_offset / (1024 * 1024);
	mi = m * (1024 * 1024);
	for (i = 0; i < BS; i++) {
		if (devc->prof->model == CHRONOVU_LA8) {
			p = i & (1 << 0);
			index = m * 2 + (((byte_offset + i) - mi) / 2) * 16;
			index += (devc->divcount == 0) ? p : (1 - p);
		} else {
			p = i & (1 << 0);
			q = i & (1 << 1);
			index = m * 4 + (((byte_offset + i) - mi) / 4) * 32;
			index += q + (1 - p);
		}
		devc->final_buf[index] = devc->mangled_buf[i];
	}

	return SR_OK;
}

SR_PRIV void cv_send_block_to_session_bus(const struct sr_dev_inst *sdi, int block)
{
	int i, idx;
	uint8_t sample, expected_sample, tmp8;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	int trigger_point; /* Relative trigger point (in this block). */
	struct dev_context *devc;

	/* Note: Caller ensures devc/devc->ftdic != NULL and block > 0. */

	devc = sdi->priv;

	/* TODO: Implement/test proper trigger support for the LA16. */

	/* Check if we can find the trigger condition in this block. */
	trigger_point = -1;
	expected_sample = devc->trigger_pattern & devc->trigger_mask;
	for (i = 0; i < BS; i++) {
		/* Don't continue if the trigger was found previously. */
		if (devc->trigger_found)
			break;

		/*
		 * Also, don't continue if triggers are "don't care", i.e. if
		 * no trigger conditions were specified by the user. In that
		 * case we don't want to send an SR_DF_TRIGGER packet at all.
		 */
		if (devc->trigger_mask == 0x0000)
			break;

		sample = *(devc->final_buf + (block * BS) + i);

		if ((sample & devc->trigger_mask) == expected_sample) {
			trigger_point = i;
			devc->trigger_found = 1;
			break;
		}
	}

	/* Swap low and high bytes of the 16-bit LA16 samples. */
	if (devc->prof->model == CHRONOVU_LA16) {
		for (i = 0; i < BS; i += 2) {
			idx = (block * BS) + i;
			tmp8 = devc->final_buf[idx];
			devc->final_buf[idx] = devc->final_buf[idx + 1];
			devc->final_buf[idx + 1] = tmp8;
		}
	}

	/* If no trigger was found, send one SR_DF_LOGIC packet. */
	if (trigger_point == -1) {
		/* Send an SR_DF_LOGIC packet to the session bus. */
		sr_spew("Sending SR_DF_LOGIC packet (%d bytes) for "
		        "block %d.", BS, block);
		packet.type = SR_DF_LOGIC;
		packet.payload = &logic;
		logic.length = BS;
		logic.unitsize = devc->prof->num_channels / 8;
		logic.data = devc->final_buf + (block * BS);
		sr_session_send(sdi, &packet);
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
		sr_spew("Sending pre-trigger SR_DF_LOGIC packet, "
			"start = %d, length = %d.", block * BS, trigger_point);
		packet.type = SR_DF_LOGIC;
		packet.payload = &logic;
		logic.length = trigger_point;
		logic.unitsize = devc->prof->num_channels / 8;
		logic.data = devc->final_buf + (block * BS);
		sr_session_send(sdi, &packet);
	}

	/* Send the SR_DF_TRIGGER packet to the session bus. */
	sr_spew("Sending SR_DF_TRIGGER packet, sample = %d.",
		(block * BS) + trigger_point);
	packet.type = SR_DF_TRIGGER;
	packet.payload = NULL;
	sr_session_send(sdi, &packet);

	/* If at least one sample is located after the trigger... */
	if (trigger_point < (BS - 1)) {
		/* Send post-trigger SR_DF_LOGIC packet to the session bus. */
		sr_spew("Sending post-trigger SR_DF_LOGIC packet, "
			"start = %d, length = %d.",
			(block * BS) + trigger_point, BS - trigger_point);
		packet.type = SR_DF_LOGIC;
		packet.payload = &logic;
		logic.length = BS - trigger_point;
		logic.unitsize = devc->prof->num_channels / 8;
		logic.data = devc->final_buf + (block * BS) + trigger_point;
		sr_session_send(sdi, &packet);
	}
}
