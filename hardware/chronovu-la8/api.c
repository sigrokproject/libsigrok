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
#include <string.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"
#include "protocol.h"

SR_PRIV struct sr_dev_driver chronovu_la8_driver_info;
static struct sr_dev_driver *di = &chronovu_la8_driver_info;

/*
 * This will be initialized via config_list()/SR_CONF_SAMPLERATE.
 *
 * Min: 1 sample per 0.01us -> sample time is 0.084s, samplerate 100MHz
 * Max: 1 sample per 2.55us -> sample time is 21.391s, samplerate 392.15kHz
 */
SR_PRIV uint64_t chronovu_la8_samplerates[255] = { 0 };

/* Note: Continuous sampling is not supported by the hardware. */
SR_PRIV const int32_t chronovu_la8_hwcaps[] = {
	SR_CONF_LOGIC_ANALYZER,
	SR_CONF_SAMPLERATE,
	SR_CONF_LIMIT_MSEC, /* TODO: Not yet implemented. */
	SR_CONF_LIMIT_SAMPLES, /* TODO: Not yet implemented. */
};

/*
 * The ChronoVu LA8 can have multiple PIDs. Older versions shipped with
 * a standard FTDI USB VID/PID of 0403:6001, newer ones have 0403:8867.
 */ 
static const uint16_t usb_pids[] = {
	0x6001,
	0x8867,
};

/* Function prototypes. */
static int hw_dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data);

static int clear_instances(void)
{
	GSList *l;
	struct sr_dev_inst *sdi;
	struct drv_context *drvc;
	struct dev_context *devc;

	drvc = di->priv;

	/* Properly close all devices. */
	for (l = drvc->instances; l; l = l->next) {
		if (!(sdi = l->data)) {
			/* Log error, but continue cleaning up the rest. */
			sr_err("%s: sdi was NULL, continuing.", __func__);
			continue;
		}
		if (sdi->priv) {
			devc = sdi->priv;
			ftdi_free(devc->ftdic);
		}
		sr_dev_inst_free(sdi);
	}
	g_slist_free(drvc->instances);
	drvc->instances = NULL;

	return SR_OK;
}

static int hw_init(struct sr_context *sr_ctx)
{
	return std_hw_init(sr_ctx, di, DRIVER_LOG_DOMAIN);
}

static GSList *hw_scan(GSList *options)
{
	struct sr_dev_inst *sdi;
	struct sr_probe *probe;
	struct drv_context *drvc;
	struct dev_context *devc;
	GSList *devices;
	unsigned int i;
	int ret;

	(void)options;

	drvc = di->priv;

	devices = NULL;

	/* Allocate memory for our private device context. */
	if (!(devc = g_try_malloc(sizeof(struct dev_context)))) {
		sr_err("Device context malloc failed.");
		goto err_free_nothing;
	}

	/* Set some sane defaults. */
	devc->ftdic = NULL;
	devc->cur_samplerate = SR_MHZ(100); /* 100MHz == max. samplerate */
	devc->limit_msec = 0;
	devc->limit_samples = 0;
	devc->cb_data = NULL;
	memset(devc->mangled_buf, 0, BS);
	devc->final_buf = NULL;
	devc->trigger_pattern = 0x00; /* Value irrelevant, see trigger_mask. */
	devc->trigger_mask = 0x00; /* All probes are "don't care". */
	devc->trigger_timeout = 10; /* Default to 10s trigger timeout. */
	devc->trigger_found = 0;
	devc->done = 0;
	devc->block_counter = 0;
	devc->divcount = 0; /* 10ns sample period == 100MHz samplerate */
	devc->usb_pid = 0;

	/* Allocate memory where we'll store the de-mangled data. */
	if (!(devc->final_buf = g_try_malloc(SDRAM_SIZE))) {
		sr_err("final_buf malloc failed.");
		goto err_free_devc;
	}

	/* Allocate memory for the FTDI context (ftdic) and initialize it. */
	if (!(devc->ftdic = ftdi_new())) {
		sr_err("%s: ftdi_new failed.", __func__);
		goto err_free_final_buf;
	}

	/* Check for the device and temporarily open it. */
	for (i = 0; i < ARRAY_SIZE(usb_pids); i++) {
		sr_dbg("Probing for VID/PID %04x:%04x.", USB_VENDOR_ID,
		       usb_pids[i]);
		ret = ftdi_usb_open_desc(devc->ftdic, USB_VENDOR_ID,
					 usb_pids[i], USB_DESCRIPTION, NULL);
		if (ret == 0) {
			sr_dbg("Found LA8 device (%04x:%04x).",
			       USB_VENDOR_ID, usb_pids[i]);
			devc->usb_pid = usb_pids[i];
		}
	}

	if (devc->usb_pid == 0)
		goto err_free_ftdic;

	/* Register the device with libsigrok. */
	sdi = sr_dev_inst_new(0, SR_ST_INITIALIZING,
			USB_VENDOR_NAME, USB_MODEL_NAME, USB_MODEL_VERSION);
	if (!sdi) {
		sr_err("%s: sr_dev_inst_new failed.", __func__);
		goto err_close_ftdic;
	}
	sdi->driver = di;
	sdi->priv = devc;

	for (i = 0; chronovu_la8_probe_names[i]; i++) {
		if (!(probe = sr_probe_new(i, SR_PROBE_LOGIC, TRUE,
					   chronovu_la8_probe_names[i])))
			return NULL;
		sdi->probes = g_slist_append(sdi->probes, probe);
	}

	devices = g_slist_append(devices, sdi);
	drvc->instances = g_slist_append(drvc->instances, sdi);

	/* Close device. We'll reopen it again when we need it. */
	(void) la8_close(devc); /* Log, but ignore errors. */

	return devices;

err_close_ftdic:
	(void) la8_close(devc); /* Log, but ignore errors. */
err_free_ftdic:
	ftdi_free(devc->ftdic); /* NOT free() or g_free()! */
err_free_final_buf:
	g_free(devc->final_buf);
err_free_devc:
	g_free(devc);
err_free_nothing:

	return NULL;
}

static GSList *hw_dev_list(void)
{
	return ((struct drv_context *)(di->priv))->instances;
}

static int hw_dev_open(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int ret;

	if (!(devc = sdi->priv)) {
		sr_err("%s: sdi->priv was NULL.", __func__);
		return SR_ERR_BUG;
	}

	sr_dbg("Opening LA8 device (%04x:%04x).", USB_VENDOR_ID,
	       devc->usb_pid);

	/* Open the device. */
	if ((ret = ftdi_usb_open_desc(devc->ftdic, USB_VENDOR_ID,
			devc->usb_pid, USB_DESCRIPTION, NULL)) < 0) {
		sr_err("%s: ftdi_usb_open_desc: (%d) %s",
		       __func__, ret, ftdi_get_error_string(devc->ftdic));
		(void) la8_close_usb_reset_sequencer(devc); /* Ignore errors. */
		return SR_ERR;
	}
	sr_dbg("Device opened successfully.");

	/* Purge RX/TX buffers in the FTDI chip. */
	if ((ret = ftdi_usb_purge_buffers(devc->ftdic)) < 0) {
		sr_err("%s: ftdi_usb_purge_buffers: (%d) %s",
		       __func__, ret, ftdi_get_error_string(devc->ftdic));
		(void) la8_close_usb_reset_sequencer(devc); /* Ignore errors. */
		goto err_dev_open_close_ftdic;
	}
	sr_dbg("FTDI buffers purged successfully.");

	/* Enable flow control in the FTDI chip. */
	if ((ret = ftdi_setflowctrl(devc->ftdic, SIO_RTS_CTS_HS)) < 0) {
		sr_err("%s: ftdi_setflowcontrol: (%d) %s",
		       __func__, ret, ftdi_get_error_string(devc->ftdic));
		(void) la8_close_usb_reset_sequencer(devc); /* Ignore errors. */
		goto err_dev_open_close_ftdic;
	}
	sr_dbg("FTDI flow control enabled successfully.");

	/* Wait 100ms. */
	g_usleep(100 * 1000);

	sdi->status = SR_ST_ACTIVE;

	return SR_OK;

err_dev_open_close_ftdic:
	(void) la8_close(devc); /* Log, but ignore errors. */
	return SR_ERR;
}

static int hw_dev_close(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = sdi->priv;

	if (sdi->status == SR_ST_ACTIVE) {
		sr_dbg("Status ACTIVE, closing device.");
		(void) la8_close_usb_reset_sequencer(devc); /* Ignore errors. */
	} else {
		sr_spew("Status not ACTIVE, nothing to do.");
	}

	sdi->status = SR_ST_INACTIVE;

	g_free(devc->final_buf);

	return SR_OK;
}

static int hw_cleanup(void)
{
	if (!di->priv)
		/* Can get called on an unused driver, doesn't matter. */
		return SR_OK;

	clear_instances();

	return SR_OK;
}

static int config_get(int id, GVariant **data, const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	switch (id) {
	case SR_CONF_SAMPLERATE:
		if (sdi) {
			devc = sdi->priv;
			*data = g_variant_new_uint64(devc->cur_samplerate);
			sr_spew("%s: Returning samplerate: %" PRIu64 "Hz.",
				__func__, devc->cur_samplerate);
		} else
			return SR_ERR;
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_set(int id, GVariant *data, const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	if (!(devc = sdi->priv)) {
		sr_err("%s: sdi->priv was NULL.", __func__);
		return SR_ERR_BUG;
	}

	switch (id) {
	case SR_CONF_SAMPLERATE:
		if (set_samplerate(sdi, g_variant_get_uint64(data)) == SR_ERR) {
			sr_err("%s: setting samplerate failed.", __func__);
			return SR_ERR;
		}
		sr_dbg("SAMPLERATE = %" PRIu64, devc->cur_samplerate);
		break;
	case SR_CONF_LIMIT_MSEC:
		if (g_variant_get_uint64(data) == 0) {
			sr_err("%s: LIMIT_MSEC can't be 0.", __func__);
			return SR_ERR;
		}
		devc->limit_msec = g_variant_get_uint64(data);
		sr_dbg("LIMIT_MSEC = %" PRIu64, devc->limit_msec);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		if (g_variant_get_uint64(data) < MIN_NUM_SAMPLES) {
			sr_err("%s: LIMIT_SAMPLES too small.", __func__);
			return SR_ERR;
		}
		devc->limit_samples = g_variant_get_uint64(data);
		sr_dbg("LIMIT_SAMPLES = %" PRIu64, devc->limit_samples);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_list(int key, GVariant **data, const struct sr_dev_inst *sdi)
{
	GVariant *gvar;
	GVariantBuilder gvb;

	(void)sdi;

	switch (key) {
	case SR_CONF_DEVICE_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_INT32,
				chronovu_la8_hwcaps,
				ARRAY_SIZE(chronovu_la8_hwcaps),
				sizeof(int32_t));
		break;
	case SR_CONF_SAMPLERATE:
		fill_supported_samplerates_if_needed();
		g_variant_builder_init(&gvb, G_VARIANT_TYPE("a{sv}"));
		gvar = g_variant_new_fixed_array(G_VARIANT_TYPE("t"),
				chronovu_la8_samplerates,
				ARRAY_SIZE(chronovu_la8_samplerates),
				sizeof(uint64_t));
		g_variant_builder_add(&gvb, "{sv}", "samplerates", gvar);
		*data = g_variant_builder_end(&gvb);
		break;
	case SR_CONF_TRIGGER_TYPE:
		*data = g_variant_new_string(TRIGGER_TYPE);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int receive_data(int fd, int revents, void *cb_data)
{
	int i, ret;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;

	(void)fd;
	(void)revents;

	if (!(sdi = cb_data)) {
		sr_err("%s: cb_data was NULL.", __func__);
		return FALSE;
	}

	if (!(devc = sdi->priv)) {
		sr_err("%s: sdi->priv was NULL.", __func__);
		return FALSE;
	}

	if (!devc->ftdic) {
		sr_err("%s: devc->ftdic was NULL.", __func__);
		return FALSE;
	}

	/* Get one block of data. */
	if ((ret = la8_read_block(devc)) < 0) {
		sr_err("%s: la8_read_block error: %d.", __func__, ret);
		hw_dev_acquisition_stop(sdi, sdi);
		return FALSE;
	}

	/* We need to get exactly NUM_BLOCKS blocks (i.e. 8MB) of data. */
	if (devc->block_counter != (NUM_BLOCKS - 1)) {
		devc->block_counter++;
		return TRUE;
	}

	sr_dbg("Sampling finished, sending data to session bus now.");

	/* All data was received and demangled, send it to the session bus. */
	for (i = 0; i < NUM_BLOCKS; i++)
		send_block_to_session_bus(devc, i);

	hw_dev_acquisition_stop(sdi, sdi);

	return TRUE;
}

static int hw_dev_acquisition_start(const struct sr_dev_inst *sdi,
				    void *cb_data)
{
	struct dev_context *devc;
	uint8_t buf[4];
	int bytes_written;

	if (!(devc = sdi->priv)) {
		sr_err("%s: sdi->priv was NULL.", __func__);
		return SR_ERR_BUG;
	}

	if (!devc->ftdic) {
		sr_err("%s: devc->ftdic was NULL.", __func__);
		return SR_ERR_BUG;
	}

	devc->divcount = samplerate_to_divcount(devc->cur_samplerate);
	if (devc->divcount == 0xff) {
		sr_err("%s: Invalid divcount/samplerate.", __func__);
		return SR_ERR;
	}

	if (configure_probes(sdi) != SR_OK) {
		sr_err("Failed to configure probes.");
		return SR_ERR;
	}

	/* Fill acquisition parameters into buf[]. */
	buf[0] = devc->divcount;
	buf[1] = 0xff; /* This byte must always be 0xff. */
	buf[2] = devc->trigger_pattern;
	buf[3] = devc->trigger_mask;

	/* Start acquisition. */
	bytes_written = la8_write(devc, buf, 4);

	if (bytes_written < 0) {
		sr_err("Acquisition failed to start: %d.", bytes_written);
		return SR_ERR;
	} else if (bytes_written != 4) {
		sr_err("Acquisition failed to start: %d.", bytes_written);
		return SR_ERR;
	}

	sr_dbg("Hardware acquisition started successfully.");

	devc->cb_data = cb_data;

	/* Send header packet to the session bus. */
	std_session_send_df_header(cb_data, DRIVER_LOG_DOMAIN);

	/* Time when we should be done (for detecting trigger timeouts). */
	devc->done = (devc->divcount + 1) * 0.08388608 + time(NULL)
			+ devc->trigger_timeout;
	devc->block_counter = 0;
	devc->trigger_found = 0;

	/* Hook up a dummy handler to receive data from the LA8. */
	sr_source_add(-1, G_IO_IN, 0, receive_data, (void *)sdi);

	return SR_OK;
}

static int hw_dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{
	struct sr_datafeed_packet packet;

	(void)sdi;

	sr_dbg("Stopping acquisition.");
	sr_source_remove(-1);

	/* Send end packet to the session bus. */
	sr_dbg("Sending SR_DF_END.");
	packet.type = SR_DF_END;
	sr_session_send(cb_data, &packet);

	return SR_OK;
}

SR_PRIV struct sr_dev_driver chronovu_la8_driver_info = {
	.name = "chronovu-la8",
	.longname = "ChronoVu LA8",
	.api_version = 1,
	.init = hw_init,
	.cleanup = hw_cleanup,
	.scan = hw_scan,
	.dev_list = hw_dev_list,
	.dev_clear = clear_instances,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = hw_dev_open,
	.dev_close = hw_dev_close,
	.dev_acquisition_start = hw_dev_acquisition_start,
	.dev_acquisition_stop = hw_dev_acquisition_stop,
	.priv = NULL,
};
