/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2011 Uwe Hermann <uwe@hermann-uwe.de>
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
#include <string.h>
#include <stdlib.h>
#include "sigrok.h"
#include "sigrok-internal.h"

#define USB_VENDOR_ID			0x0403
#define USB_PRODUCT_ID			0x6001
#define USB_DESCRIPTION			"ChronoVu LA8"
#define USB_VENDOR_NAME			"ChronoVu"
#define USB_MODEL_NAME			"LA8"
#define USB_MODEL_VERSION		""

#define NUM_PROBES			8
#define TRIGGER_TYPES			"01"
#define SDRAM_SIZE			(8 * 1024 * 1024)
#define MIN_NUM_SAMPLES			1

#define BS				4096 /* Block size */
#define NUM_BLOCKS			2048 /* Number of blocks */

static GSList *dev_insts = NULL;

static const char *probe_names[NUM_PROBES + 1] = {
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

/* Private, per-device-instance driver context. */
struct context {
	/** FTDI device context (used by libftdi). */
	struct ftdi_context *ftdic;

	/** The currently configured samplerate of the device. */
	uint64_t cur_samplerate;

	/** The current sampling limit (in ms). */
	uint64_t limit_msec;

	/** The current sampling limit (in number of samples). */
	uint64_t limit_samples;

	/** TODO */
	gpointer session_id;

	/**
	 * A buffer containing some (mangled) samples from the device.
	 * Format: Pretty mangled-up (due to hardware reasons), see code.
	 */
	uint8_t mangled_buf[BS];

	/**
	 * An 8MB buffer where we'll store the de-mangled samples.
	 * Format: Each sample is 1 byte, MSB is channel 7, LSB is channel 0.
	 */
	uint8_t *final_buf;

	/**
	 * Trigger pattern (MSB = channel 7, LSB = channel 0).
	 * A 1 bit matches a high signal, 0 matches a low signal on a probe.
	 * Only low/high triggers (but not e.g. rising/falling) are supported.
	 */
	uint8_t trigger_pattern;

	/**
	 * Trigger mask (MSB = channel 7, LSB = channel 0).
	 * A 1 bit means "must match trigger_pattern", 0 means "don't care".
	 */
	uint8_t trigger_mask;

	/** Time (in seconds) before the trigger times out. */
	uint64_t trigger_timeout;

	/** Tells us whether an SR_DF_TRIGGER packet was already sent. */
	int trigger_found;

	/** TODO */
	time_t done;

	/** Counter/index for the data block to be read. */
	int block_counter;

	/** The divcount value (determines the sample period) for the LA8. */
	uint8_t divcount;
};

/* This will be initialized via hw_dev_info_get()/SR_DI_SAMPLERATES. */
static uint64_t supported_samplerates[255 + 1] = { 0 };

/*
 * Min: 1 sample per 0.01us -> sample time is 0.084s, samplerate 100MHz
 * Max: 1 sample per 2.55us -> sample time is 21.391s, samplerate 392.15kHz
 */
static struct sr_samplerates samplerates = {
	.low  = 0,
	.high = 0,
	.step = 0,
	.list = supported_samplerates,
};

/* Note: Continuous sampling is not supported by the hardware. */
static int hwcaps[] = {
	SR_HWCAP_LOGIC_ANALYZER,
	SR_HWCAP_SAMPLERATE,
	SR_HWCAP_LIMIT_MSEC, /* TODO: Not yet implemented. */
	SR_HWCAP_LIMIT_SAMPLES, /* TODO: Not yet implemented. */
	0,
};

/* Function prototypes. */
static int la8_close_usb_reset_sequencer(struct context *ctx);
static int hw_dev_acquisition_stop(int dev_index, gpointer session_data);
static int la8_reset(struct context *ctx);

static void fill_supported_samplerates_if_needed(void)
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
static int is_valid_samplerate(uint64_t samplerate)
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
static uint8_t samplerate_to_divcount(uint64_t samplerate)
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
 * @param ctx The struct containing private per-device-instance data.
 * @param buf The buffer containing the data to write.
 * @param size The number of bytes to write.
 * @return The number of bytes written, or a negative value upon errors.
 */
static int la8_write(struct context *ctx, uint8_t *buf, int size)
{
	int bytes_written;

	if (!ctx) {
		sr_err("la8: %s: ctx was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (!ctx->ftdic) {
		sr_err("la8: %s: ctx->ftdic was NULL", __func__);
		return SR_ERR_ARG;
	}

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
 * @param ctx The struct containing private per-device-instance data.
 * @param buf The buffer where the received data will be stored.
 * @param size The number of bytes to read.
 * @return The number of bytes read, or a negative value upon errors.
 */
static int la8_read(struct context *ctx, uint8_t *buf, int size)
{
	int bytes_read;

	if (!ctx) {
		sr_err("la8: %s: ctx was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (!ctx->ftdic) {
		sr_err("la8: %s: ctx->ftdic was NULL", __func__);
		return SR_ERR_ARG;
	}

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

static int la8_close(struct context *ctx)
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
 * @return SR_OK upon success, SR_ERR upon failure.
 */
static int la8_close_usb_reset_sequencer(struct context *ctx)
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
		sr_dbg("la8: resetting sequencer logic");
		(void) la8_write(ctx, buf, 8); /* Ignore errors. */
		g_usleep(100 * 1000);

		/* Purge FTDI buffers, then reset and close the FTDI device. */
		sr_dbg("la8: purging buffers, resetting+closing FTDI device");

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
static int la8_reset(struct context *ctx)
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

	sr_dbg("la8: resetting the device");

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

	sr_dbg("la8: device reset finished");

	return SR_OK;
}

static int configure_probes(struct context *ctx, GSList *probes)
{
	struct sr_probe *probe;
	GSList *l;
	uint8_t probe_bit;
	char *tc;

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

	sr_dbg("la8: %s: trigger_mask = 0x%x, trigger_pattern = 0x%x",
	       __func__, ctx->trigger_mask, ctx->trigger_pattern);

	return SR_OK;
}

static int hw_init(const char *devinfo)
{
	int ret;
	struct sr_dev_inst *sdi;
	struct context *ctx;

	/* Avoid compiler errors. */
	(void)devinfo;

	/* Allocate memory for our private driver context. */
	if (!(ctx = g_try_malloc(sizeof(struct context)))) {
		sr_err("la8: %s: struct context malloc failed", __func__);
		goto err_free_nothing;
	}

	/* Set some sane defaults. */
	ctx->ftdic = NULL;
	ctx->cur_samplerate = SR_MHZ(100); /* 100MHz == max. samplerate */
	ctx->limit_msec = 0;
	ctx->limit_samples = 0;
	ctx->session_id = NULL;
	memset(ctx->mangled_buf, 0, BS);
	ctx->final_buf = NULL;
	ctx->trigger_pattern = 0x00; /* Value irrelevant, see trigger_mask. */
	ctx->trigger_mask = 0x00; /* All probes are "don't care". */
	ctx->trigger_timeout = 10; /* Default to 10s trigger timeout. */
	ctx->trigger_found = 0;
	ctx->done = 0;
	ctx->block_counter = 0;
	ctx->divcount = 0; /* 10ns sample period == 100MHz samplerate */

	/* Allocate memory where we'll store the de-mangled data. */
	if (!(ctx->final_buf = g_try_malloc(SDRAM_SIZE))) {
		sr_err("la8: %s: final_buf malloc failed", __func__);
		goto err_free_ctx;
	}

	/* Allocate memory for the FTDI context (ftdic) and initialize it. */
	if (!(ctx->ftdic = ftdi_new())) {
		sr_err("la8: %s: ftdi_new failed", __func__);
		goto err_free_final_buf;
	}

	/* Check for the device and temporarily open it. */
	if ((ret = ftdi_usb_open_desc(ctx->ftdic, USB_VENDOR_ID,
			USB_PRODUCT_ID, USB_DESCRIPTION, NULL)) < 0) {
		(void) la8_close_usb_reset_sequencer(ctx); /* Ignore errors. */
		goto err_free_ftdic;
	}
	sr_dbg("la8: found device");

	/* Register the device with libsigrok. */
	sdi = sr_dev_inst_new(0, SR_ST_INITIALIZING,
			USB_VENDOR_NAME, USB_MODEL_NAME, USB_MODEL_VERSION);
	if (!sdi) {
		sr_err("la8: %s: sr_dev_inst_new failed", __func__);
		goto err_close_ftdic;
	}

	sdi->priv = ctx;

	dev_insts = g_slist_append(dev_insts, sdi);

	sr_spew("la8: %s finished successfully", __func__);

	/* Close device. We'll reopen it again when we need it. */
	(void) la8_close(ctx); /* Log, but ignore errors. */

	return 1;

err_close_ftdic:
	(void) la8_close(ctx); /* Log, but ignore errors. */
err_free_ftdic:
	free(ctx->ftdic); /* NOT g_free()! */
err_free_final_buf:
	g_free(ctx->final_buf);
err_free_ctx:
	g_free(ctx);
err_free_nothing:

	return 0;
}

static int hw_dev_open(int dev_index)
{
	int ret;
	struct sr_dev_inst *sdi;
	struct context *ctx;

	if (!(sdi = sr_dev_inst_get(dev_insts, dev_index))) {
		sr_err("la8: %s: sdi was NULL", __func__);
		return SR_ERR; /* TODO: SR_ERR_ARG? */
	}

	if (!(ctx = sdi->priv)) {
		sr_err("la8: %s: sdi->priv was NULL", __func__);
		return SR_ERR; /* TODO: SR_ERR_ARG? */
	}

	sr_dbg("la8: opening device");

	/* Open the device. */
	if ((ret = ftdi_usb_open_desc(ctx->ftdic, USB_VENDOR_ID,
			USB_PRODUCT_ID, USB_DESCRIPTION, NULL)) < 0) {
		sr_err("la8: %s: ftdi_usb_open_desc: (%d) %s",
		       __func__, ret, ftdi_get_error_string(ctx->ftdic));
		(void) la8_close_usb_reset_sequencer(ctx); /* Ignore errors. */
		return SR_ERR;
	}
	sr_dbg("la8: device opened successfully");

	/* Purge RX/TX buffers in the FTDI chip. */
	if ((ret = ftdi_usb_purge_buffers(ctx->ftdic)) < 0) {
		sr_err("la8: %s: ftdi_usb_purge_buffers: (%d) %s",
		       __func__, ret, ftdi_get_error_string(ctx->ftdic));
		(void) la8_close_usb_reset_sequencer(ctx); /* Ignore errors. */
		goto err_dev_open_close_ftdic;
	}
	sr_dbg("la8: FTDI buffers purged successfully");

	/* Enable flow control in the FTDI chip. */
	if ((ret = ftdi_setflowctrl(ctx->ftdic, SIO_RTS_CTS_HS)) < 0) {
		sr_err("la8: %s: ftdi_setflowcontrol: (%d) %s",
		       __func__, ret, ftdi_get_error_string(ctx->ftdic));
		(void) la8_close_usb_reset_sequencer(ctx); /* Ignore errors. */
		goto err_dev_open_close_ftdic;
	}
	sr_dbg("la8: FTDI flow control enabled successfully");

	/* Wait 100ms. */
	g_usleep(100 * 1000);

	sdi->status = SR_ST_ACTIVE;

	return SR_OK;

err_dev_open_close_ftdic:
	(void) la8_close(ctx); /* Log, but ignore errors. */
	return SR_ERR;
}

static int set_samplerate(struct sr_dev_inst *sdi, uint64_t samplerate)
{
	struct context *ctx;

	if (!sdi) {
		sr_err("la8: %s: sdi was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (!(ctx = sdi->priv)) {
		sr_err("la8: %s: sdi->priv was NULL", __func__);
		return SR_ERR_ARG;
	}

	sr_spew("la8: setting samplerate");

	fill_supported_samplerates_if_needed();

	/* Check if this is a samplerate supported by the hardware. */
	if (!is_valid_samplerate(samplerate))
		return SR_ERR;

	/* Set the new samplerate. */
	ctx->cur_samplerate = samplerate;

	sr_dbg("la8: samplerate set to %" PRIu64 "Hz", ctx->cur_samplerate);

	return SR_OK;
}

static int hw_dev_close(int dev_index)
{
	struct sr_dev_inst *sdi;
	struct context *ctx;

	if (!(sdi = sr_dev_inst_get(dev_insts, dev_index))) {
		sr_err("la8: %s: sdi was NULL", __func__);
		return SR_ERR; /* TODO: SR_ERR_ARG? */
	}

	if (!(ctx = sdi->priv)) {
		sr_err("la8: %s: sdi->priv was NULL", __func__);
		return SR_ERR; /* TODO: SR_ERR_ARG? */
	}

	sr_dbg("la8: closing device");

	if (sdi->status == SR_ST_ACTIVE) {
		sr_dbg("la8: %s: status ACTIVE, closing device", __func__);
		/* TODO: Really ignore errors here, or return SR_ERR? */
		(void) la8_close_usb_reset_sequencer(ctx); /* Ignore errors. */
	} else {
		sr_spew("la8: %s: status not ACTIVE, nothing to do", __func__);
	}

	sdi->status = SR_ST_INACTIVE;

	sr_dbg("la8: %s: freeing sample buffers", __func__);
	g_free(ctx->final_buf);

	return SR_OK;
}

static int hw_cleanup(void)
{
	GSList *l;
	struct sr_dev_inst *sdi;
	int ret = SR_OK;

	/* Properly close all devices. */
	for (l = dev_insts; l; l = l->next) {
		if (!(sdi = l->data)) {
			/* Log error, but continue cleaning up the rest. */
			sr_err("la8: %s: sdi was NULL, continuing", __func__);
			ret = SR_ERR_BUG;
			continue;
		}
		sr_dev_inst_free(sdi); /* Returns void. */
	}
	g_slist_free(dev_insts); /* Returns void. */
	dev_insts = NULL;

	return ret;
}

static void *hw_dev_info_get(int dev_index, int dev_info_id)
{
	struct sr_dev_inst *sdi;
	struct context *ctx;
	void *info;

	sr_spew("la8: entering %s", __func__);

	if (!(sdi = sr_dev_inst_get(dev_insts, dev_index))) {
		sr_err("la8: %s: sdi was NULL", __func__);
		return NULL;
	}

	if (!(ctx = sdi->priv)) {
		sr_err("la8: %s: sdi->priv was NULL", __func__);
		return NULL;
	}

	switch (dev_info_id) {
	case SR_DI_INST:
		info = sdi;
		break;
	case SR_DI_NUM_PROBES:
		info = GINT_TO_POINTER(NUM_PROBES);
		break;
	case SR_DI_PROBE_NAMES:
		info = probe_names;
		break;
	case SR_DI_SAMPLERATES:
		fill_supported_samplerates_if_needed();
		info = &samplerates;
		break;
	case SR_DI_TRIGGER_TYPES:
		info = (char *)TRIGGER_TYPES;
		break;
	case SR_DI_CUR_SAMPLERATE:
		info = &ctx->cur_samplerate;
		break;
	default:
		/* Unknown device info ID, return NULL. */
		sr_err("la8: %s: Unknown device info ID", __func__);
		info = NULL;
		break;
	}

	return info;
}

static int hw_dev_status_get(int dev_index)
{
	struct sr_dev_inst *sdi;

	if (!(sdi = sr_dev_inst_get(dev_insts, dev_index))) {
		sr_err("la8: %s: sdi was NULL, device not found", __func__);
		return SR_ST_NOT_FOUND;
	}

	sr_dbg("la8: %s: returning status %d", __func__, sdi->status);

	return sdi->status;
}

static int *hw_hwcap_get_all(void)
{
	sr_spew("la8: entering %s", __func__);

	return hwcaps;
}

static int hw_dev_config_set(int dev_index, int hwcap, void *value)
{
	struct sr_dev_inst *sdi;
	struct context *ctx;

	sr_spew("la8: entering %s", __func__);

	if (!(sdi = sr_dev_inst_get(dev_insts, dev_index))) {
		sr_err("la8: %s: sdi was NULL", __func__);
		return SR_ERR; /* TODO: SR_ERR_ARG? */
	}

	if (!(ctx = sdi->priv)) {
		sr_err("la8: %s: sdi->priv was NULL", __func__);
		return SR_ERR; /* TODO: SR_ERR_ARG? */
	}

	switch (hwcap) {
	case SR_HWCAP_SAMPLERATE:
		if (set_samplerate(sdi, *(uint64_t *)value) == SR_ERR)
			return SR_ERR;
		sr_dbg("la8: SAMPLERATE = %" PRIu64, ctx->cur_samplerate);
		break;
	case SR_HWCAP_PROBECONFIG:
		if (configure_probes(ctx, (GSList *)value) != SR_OK) {
			sr_err("la8: %s: probe config failed", __func__);
			return SR_ERR;
		}
		break;
	case SR_HWCAP_LIMIT_MSEC:
		if (*(uint64_t *)value == 0) {
			sr_err("la8: %s: LIMIT_MSEC can't be 0", __func__);
			return SR_ERR;
		}
		ctx->limit_msec = *(uint64_t *)value;
		sr_dbg("la8: LIMIT_MSEC = %" PRIu64, ctx->limit_msec);
		break;
	case SR_HWCAP_LIMIT_SAMPLES:
		if (*(uint64_t *)value < MIN_NUM_SAMPLES) {
			sr_err("la8: %s: LIMIT_SAMPLES too small", __func__);
			return SR_ERR;
		}
		ctx->limit_samples = *(uint64_t *)value;
		sr_dbg("la8: LIMIT_SAMPLES = %" PRIu64, ctx->limit_samples);
		break;
	default:
		/* Unknown capability, return SR_ERR. */
		sr_err("la8: %s: Unknown capability", __func__);
		return SR_ERR;
		break;
	}

	return SR_OK;
}

/**
 * Get a block of data from the LA8.
 *
 * @param ctx The struct containing private per-device-instance data.
 * @return SR_OK upon success, or SR_ERR upon errors.
 */
static int la8_read_block(struct context *ctx)
{
	int i, byte_offset, m, mi, p, index, bytes_read;
	time_t now;

	if (!ctx) {
		sr_err("la8: %s: ctx was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (!ctx->ftdic) {
		sr_err("la8: %s: ctx->ftdic was NULL", __func__);
		return SR_ERR_ARG;
	}

	sr_spew("la8: %s: reading block %d", __func__, ctx->block_counter);

	bytes_read = la8_read(ctx, ctx->mangled_buf, BS);

	/* If first block read got 0 bytes, retry until success or timeout. */
	if ((bytes_read == 0) && (ctx->block_counter == 0)) {
		do {
			sr_spew("la8: %s: reading block 0 again", __func__);
			bytes_read = la8_read(ctx, ctx->mangled_buf, BS);
			/* TODO: How to handle read errors here? */
			now = time(NULL);
		} while ((ctx->done > now) && (bytes_read == 0));
	}

	/* Check if block read was successful or a timeout occured. */
	if (bytes_read != BS) {
		sr_err("la8: %s: trigger timed out", __func__);
		(void) la8_reset(ctx); /* Ignore errors. */
		return SR_ERR;
	}

	/* De-mangle the data. */
	sr_spew("la8: de-mangling samples of block %d", ctx->block_counter);
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

static void send_block_to_session_bus(struct context *ctx, int block)
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
		sr_session_bus(ctx->session_id, &packet);
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
		sr_session_bus(ctx->session_id, &packet);
	}

	/* Send the SR_DF_TRIGGER packet to the session bus. */
	sr_spew("la8: sending SR_DF_TRIGGER packet, sample = %d",
		(block * BS) + trigger_point);
	packet.type = SR_DF_TRIGGER;
	packet.payload = NULL;
	sr_session_bus(ctx->session_id, &packet);

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
		sr_session_bus(ctx->session_id, &packet);
	}
}

static int receive_data(int fd, int revents, void *session_data)
{
	int i, ret;
	struct sr_dev_inst *sdi;
	struct context *ctx;

	/* Avoid compiler errors. */
	(void)fd;
	(void)revents;

	if (!(sdi = session_data)) {
		sr_err("la8: %s: session_data was NULL", __func__);
		return FALSE;
	}

	if (!(ctx = sdi->priv)) {
		sr_err("la8: %s: sdi->priv was NULL", __func__);
		return FALSE;
	}

	/* Get one block of data. */
	if ((ret = la8_read_block(ctx)) < 0) {
		sr_err("la8: %s: la8_read_block error: %d", __func__, ret);
		hw_dev_acquisition_stop(sdi->index, session_data);
		return FALSE;
	}

	/* We need to get exactly NUM_BLOCKS blocks (i.e. 8MB) of data. */
	if (ctx->block_counter != (NUM_BLOCKS - 1)) {
		ctx->block_counter++;
		return TRUE;
	}

	sr_dbg("la8: sampling finished, sending data to session bus now");

	/* All data was received and demangled, send it to the session bus. */
	for (i = 0; i < NUM_BLOCKS; i++)
		send_block_to_session_bus(ctx, i);

	hw_dev_acquisition_stop(sdi->index, session_data);

	// return FALSE; /* FIXME? */
	return TRUE;
}

static int hw_dev_acquisition_start(int dev_index, gpointer session_data)
{
	struct sr_dev_inst *sdi;
	struct context *ctx;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_header header;
	uint8_t buf[4];
	int bytes_written;

	sr_spew("la8: entering %s", __func__);

	if (!(sdi = sr_dev_inst_get(dev_insts, dev_index))) {
		sr_err("la8: %s: sdi was NULL", __func__);
		return SR_ERR; /* TODO: SR_ERR_ARG? */
	}

	if (!(ctx = sdi->priv)) {
		sr_err("la8: %s: sdi->priv was NULL", __func__);
		return SR_ERR; /* TODO: SR_ERR_ARG? */
	}

	if (!ctx->ftdic) {
		sr_err("la8: %s: ctx->ftdic was NULL", __func__);
		return SR_ERR_ARG;
	}

	ctx->divcount = samplerate_to_divcount(ctx->cur_samplerate);
	if (ctx->divcount == 0xff) {
		sr_err("la8: %s: invalid divcount/samplerate", __func__);
		return SR_ERR;
	}

	/* Fill acquisition parameters into buf[]. */
	buf[0] = ctx->divcount;
	buf[1] = 0xff; /* This byte must always be 0xff. */
	buf[2] = ctx->trigger_pattern;
	buf[3] = ctx->trigger_mask;

	/* Start acquisition. */
	bytes_written = la8_write(ctx, buf, 4);

	if (bytes_written < 0) {
		sr_err("la8: acquisition failed to start");
		return SR_ERR;
	} else if (bytes_written != 4) {
		sr_err("la8: acquisition failed to start");
		return SR_ERR; /* TODO: Other error and return code? */
	}

	sr_dbg("la8: acquisition started successfully");

	ctx->session_id = session_data;

	/* Send header packet to the session bus. */
	sr_dbg("la8: %s: sending SR_DF_HEADER", __func__);
	packet.type = SR_DF_HEADER;
	packet.payload = &header;
	header.feed_version = 1;
	gettimeofday(&header.starttime, NULL);
	header.samplerate = ctx->cur_samplerate;
	header.num_logic_probes = NUM_PROBES;
	sr_session_bus(session_data, &packet);

	/* Time when we should be done (for detecting trigger timeouts). */
	ctx->done = (ctx->divcount + 1) * 0.08388608 + time(NULL)
			+ ctx->trigger_timeout;
	ctx->block_counter = 0;
	ctx->trigger_found = 0;

	/* Hook up a dummy handler to receive data from the LA8. */
	sr_source_add(-1, G_IO_IN, 0, receive_data, sdi);

	return SR_OK;
}

static int hw_dev_acquisition_stop(int dev_index, gpointer session_data)
{
	struct sr_dev_inst *sdi;
	struct context *ctx;
	struct sr_datafeed_packet packet;

	sr_dbg("la8: stopping acquisition");

	if (!(sdi = sr_dev_inst_get(dev_insts, dev_index))) {
		sr_err("la8: %s: sdi was NULL", __func__);
		return SR_ERR_BUG;
	}

	if (!(ctx = sdi->priv)) {
		sr_err("la8: %s: sdi->priv was NULL", __func__);
		return SR_ERR_BUG;
	}

	/* Send end packet to the session bus. */
	sr_dbg("la8: %s: sending SR_DF_END", __func__);
	packet.type = SR_DF_END;
	sr_session_bus(session_data, &packet);

	return SR_OK;
}

SR_PRIV struct sr_dev_plugin chronovu_la8_plugin_info = {
	.name = "chronovu-la8",
	.longname = "ChronoVu LA8",
	.api_version = 1,
	.init = hw_init,
	.cleanup = hw_cleanup,
	.dev_open = hw_dev_open,
	.dev_close = hw_dev_close,
	.dev_info_get = hw_dev_info_get,
	.dev_status_get = hw_dev_status_get,
	.hwcap_get_all = hw_hwcap_get_all,
	.dev_config_set = hw_dev_config_set,
	.dev_acquisition_start = hw_dev_acquisition_start,
	.dev_acquisition_stop = hw_dev_acquisition_stop,
};
