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
#include "driver.h"

SR_PRIV struct sr_dev_driver chronovu_la8_driver_info;
static struct sr_dev_driver *cdi = &chronovu_la8_driver_info;

/*
 * The ChronoVu LA8 can have multiple PIDs. Older versions shipped with
 * a standard FTDI USB VID/PID of 0403:6001, newer ones have 0403:8867.
 */ 
static const uint16_t usb_pids[] = {
	0x6001,
	0x8867,
};

/* Function prototypes. */
static int hw_dev_acquisition_stop(const struct sr_dev_inst *sdi,
		void *cb_data);

static void clear_instances(void)
{
	GSList *l;
	struct sr_dev_inst *sdi;
	struct drv_context *drvc;
	struct dev_context *devc;

	drvc = cdi->priv;

	/* Properly close all devices. */
	for (l = drvc->instances; l; l = l->next) {
		if (!(sdi = l->data)) {
			/* Log error, but continue cleaning up the rest. */
			sr_err("la8: %s: sdi was NULL, continuing", __func__);
			continue;
		}
		if (sdi->priv) {
			devc = sdi->priv;
			ftdi_free(devc->ftdic);
			g_free(devc);
		}
		sr_dev_inst_free(sdi);
	}
	g_slist_free(drvc->instances);
	drvc->instances = NULL;

}

static int hw_init(void)
{
	struct drv_context *drvc;

	if (!(drvc = g_try_malloc0(sizeof(struct drv_context)))) {
		sr_err("chronovu-la8: driver context malloc failed.");
		return SR_ERR;
	}
	cdi->priv = drvc;

	return SR_OK;
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
	drvc = cdi->priv;
	devices = NULL;

	/* Allocate memory for our private device context. */
	if (!(devc = g_try_malloc(sizeof(struct dev_context)))) {
		sr_err("la8: %s: struct context malloc failed", __func__);
		goto err_free_nothing;
	}

	/* Set some sane defaults. */
	devc->ftdic = NULL;
	devc->cur_samplerate = SR_MHZ(100); /* 100MHz == max. samplerate */
	devc->limit_msec = 0;
	devc->limit_samples = 0;
	devc->session_dev_id = NULL;
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
		sr_err("la8: %s: final_buf malloc failed", __func__);
		goto err_free_devc;
	}

	/* Allocate memory for the FTDI context (ftdic) and initialize it. */
	if (!(devc->ftdic = ftdi_new())) {
		sr_err("la8: %s: ftdi_new failed", __func__);
		goto err_free_final_buf;
	}

	/* Check for the device and temporarily open it. */
	for (i = 0; i < ARRAY_SIZE(usb_pids); i++) {
		sr_dbg("la8: Probing for VID/PID %04x:%04x.", USB_VENDOR_ID,
		       usb_pids[i]);
		ret = ftdi_usb_open_desc(devc->ftdic, USB_VENDOR_ID,
					 usb_pids[i], USB_DESCRIPTION, NULL);
		if (ret == 0) {
			sr_dbg("la8: Found LA8 device (%04x:%04x).",
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
		sr_err("la8: %s: sr_dev_inst_new failed", __func__);
		goto err_close_ftdic;
	}
	sdi->driver = cdi;
	sdi->priv = devc;

	for (i = 0; probe_names[i]; i++) {
		if (!(probe = sr_probe_new(i, SR_PROBE_ANALOG, TRUE,
				probe_names[i])))
			return NULL;
		sdi->probes = g_slist_append(sdi->probes, probe);
	}

	devices = g_slist_append(devices, sdi);
	drvc->instances = g_slist_append(drvc->instances, sdi);

	sr_spew("la8: Device init successful.");

	/* Close device. We'll reopen it again when we need it. */
	(void) la8_close(devc); /* Log, but ignore errors. */

	return devices;

err_close_ftdic:
	(void) la8_close(devc); /* Log, but ignore errors. */
err_free_ftdic:
	free(devc->ftdic); /* NOT g_free()! */
err_free_final_buf:
	g_free(devc->final_buf);
err_free_devc:
	g_free(devc);
err_free_nothing:

	return NULL;
}

static int hw_dev_open(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int ret;

	if (!(devc = sdi->priv)) {
		sr_err("la8: %s: sdi->priv was NULL", __func__);
		return SR_ERR_BUG;
	}

	sr_dbg("la8: Opening LA8 device (%04x:%04x).", USB_VENDOR_ID,
	       devc->usb_pid);

	/* Open the device. */
	if ((ret = ftdi_usb_open_desc(devc->ftdic, USB_VENDOR_ID,
			devc->usb_pid, USB_DESCRIPTION, NULL)) < 0) {
		sr_err("la8: %s: ftdi_usb_open_desc: (%d) %s",
		       __func__, ret, ftdi_get_error_string(devc->ftdic));
		(void) la8_close_usb_reset_sequencer(devc); /* Ignore errors. */
		return SR_ERR;
	}
	sr_dbg("la8: Device opened successfully.");

	/* Purge RX/TX buffers in the FTDI chip. */
	if ((ret = ftdi_usb_purge_buffers(devc->ftdic)) < 0) {
		sr_err("la8: %s: ftdi_usb_purge_buffers: (%d) %s",
		       __func__, ret, ftdi_get_error_string(devc->ftdic));
		(void) la8_close_usb_reset_sequencer(devc); /* Ignore errors. */
		goto err_dev_open_close_ftdic;
	}
	sr_dbg("la8: FTDI buffers purged successfully.");

	/* Enable flow control in the FTDI chip. */
	if ((ret = ftdi_setflowctrl(devc->ftdic, SIO_RTS_CTS_HS)) < 0) {
		sr_err("la8: %s: ftdi_setflowcontrol: (%d) %s",
		       __func__, ret, ftdi_get_error_string(devc->ftdic));
		(void) la8_close_usb_reset_sequencer(devc); /* Ignore errors. */
		goto err_dev_open_close_ftdic;
	}
	sr_dbg("la8: FTDI flow control enabled successfully.");

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

	if (!(devc = sdi->priv)) {
		sr_err("la8: %s: sdi->priv was NULL", __func__);
		return SR_ERR_BUG;
	}

	sr_dbg("la8: Closing device.");

	if (sdi->status == SR_ST_ACTIVE) {
		sr_dbg("la8: Status ACTIVE, closing device.");
		(void) la8_close_usb_reset_sequencer(devc); /* Ignore errors. */
	} else {
		sr_spew("la8: Status not ACTIVE, nothing to do.");
	}

	sdi->status = SR_ST_INACTIVE;

	sr_dbg("la8: Freeing sample buffer.");
	g_free(devc->final_buf);

	return SR_OK;
}

static int hw_cleanup(void)
{

	if (!cdi->priv)
		return SR_OK;

	clear_instances();

	return SR_OK;
}

static int hw_info_get(int info_id, const void **data,
       const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	switch (info_id) {
	case SR_DI_HWCAPS:
		*data = hwcaps;
		break;
	case SR_DI_NUM_PROBES:
		*data = GINT_TO_POINTER(NUM_PROBES);
		sr_spew("la8: %s: Returning number of probes: %d.", __func__,
			NUM_PROBES);
		break;
	case SR_DI_PROBE_NAMES:
		*data = probe_names;
		sr_spew("la8: %s: Returning probenames.", __func__);
		break;
	case SR_DI_SAMPLERATES:
		fill_supported_samplerates_if_needed();
		*data = &samplerates;
		sr_spew("la8: %s: Returning samplerates.", __func__);
		break;
	case SR_DI_TRIGGER_TYPES:
		*data = (char *)TRIGGER_TYPES;
		sr_spew("la8: %s: Returning trigger types: %s.", __func__,
			TRIGGER_TYPES);
		break;
	case SR_DI_CUR_SAMPLERATE:
		if (sdi) {
			devc = sdi->priv;
			*data = &devc->cur_samplerate;
			sr_spew("la8: %s: Returning samplerate: %" PRIu64 "Hz.",
				__func__, devc->cur_samplerate);
		} else
			return SR_ERR;
		break;
	default:
		return SR_ERR_ARG;
	}

	return SR_OK;
}

static int hw_dev_config_set(const struct sr_dev_inst *sdi, int hwcap,
		const void *value)
{
	struct dev_context *devc;

	if (!(devc = sdi->priv)) {
		sr_err("la8: %s: sdi->priv was NULL", __func__);
		return SR_ERR_BUG;
	}

	switch (hwcap) {
	case SR_HWCAP_SAMPLERATE:
		if (set_samplerate(sdi, *(const uint64_t *)value) == SR_ERR) {
			sr_err("la8: %s: setting samplerate failed.", __func__);
			return SR_ERR;
		}
		sr_dbg("la8: SAMPLERATE = %" PRIu64, devc->cur_samplerate);
		break;
	case SR_HWCAP_PROBECONFIG:
		if (configure_probes(devc, (const GSList *)value) != SR_OK) {
			sr_err("la8: %s: probe config failed.", __func__);
			return SR_ERR;
		}
		break;
	case SR_HWCAP_LIMIT_MSEC:
		if (*(const uint64_t *)value == 0) {
			sr_err("la8: %s: LIMIT_MSEC can't be 0.", __func__);
			return SR_ERR;
		}
		devc->limit_msec = *(const uint64_t *)value;
		sr_dbg("la8: LIMIT_MSEC = %" PRIu64, devc->limit_msec);
		break;
	case SR_HWCAP_LIMIT_SAMPLES:
		if (*(const uint64_t *)value < MIN_NUM_SAMPLES) {
			sr_err("la8: %s: LIMIT_SAMPLES too small.", __func__);
			return SR_ERR;
		}
		devc->limit_samples = *(const uint64_t *)value;
		sr_dbg("la8: LIMIT_SAMPLES = %" PRIu64, devc->limit_samples);
		break;
	default:
		/* Unknown capability, return SR_ERR. */
		sr_err("la8: %s: Unknown capability.", __func__);
		return SR_ERR;
		break;
	}

	return SR_OK;
}

static int receive_data(int fd, int revents, void *cb_data)
{
	int i, ret;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;

	/* Avoid compiler errors. */
	(void)fd;
	(void)revents;

	if (!(sdi = cb_data)) {
		sr_err("la8: %s: cb_data was NULL", __func__);
		return FALSE;
	}

	if (!(devc = sdi->priv)) {
		sr_err("la8: %s: sdi->priv was NULL", __func__);
		return FALSE;
	}

	if (!devc->ftdic) {
		sr_err("la8: %s: devc->ftdic was NULL", __func__);
		return FALSE;
	}

	/* Get one block of data. */
	if ((ret = la8_read_block(devc)) < 0) {
		sr_err("la8: %s: la8_read_block error: %d", __func__, ret);
		hw_dev_acquisition_stop(sdi, sdi);
		return FALSE;
	}

	/* We need to get exactly NUM_BLOCKS blocks (i.e. 8MB) of data. */
	if (devc->block_counter != (NUM_BLOCKS - 1)) {
		devc->block_counter++;
		return TRUE;
	}

	sr_dbg("la8: Sampling finished, sending data to session bus now.");

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
	struct sr_datafeed_packet packet;
	struct sr_datafeed_header header;
	struct sr_datafeed_meta_logic meta;
	uint8_t buf[4];
	int bytes_written;

	if (!(devc = sdi->priv)) {
		sr_err("la8: %s: sdi->priv was NULL", __func__);
		return SR_ERR_BUG;
	}

	if (!devc->ftdic) {
		sr_err("la8: %s: devc->ftdic was NULL", __func__);
		return SR_ERR_BUG;
	}

	devc->divcount = samplerate_to_divcount(devc->cur_samplerate);
	if (devc->divcount == 0xff) {
		sr_err("la8: %s: invalid divcount/samplerate", __func__);
		return SR_ERR;
	}

	sr_dbg("la8: Starting acquisition.");

	/* Fill acquisition parameters into buf[]. */
	buf[0] = devc->divcount;
	buf[1] = 0xff; /* This byte must always be 0xff. */
	buf[2] = devc->trigger_pattern;
	buf[3] = devc->trigger_mask;

	/* Start acquisition. */
	bytes_written = la8_write(devc, buf, 4);

	if (bytes_written < 0) {
		sr_err("la8: Acquisition failed to start.");
		return SR_ERR;
	} else if (bytes_written != 4) {
		sr_err("la8: Acquisition failed to start.");
		return SR_ERR;
	}

	sr_dbg("la8: Acquisition started successfully.");

	devc->session_dev_id = cb_data;

	/* Send header packet to the session bus. */
	sr_dbg("la8: Sending SR_DF_HEADER.");
	packet.type = SR_DF_HEADER;
	packet.payload = &header;
	header.feed_version = 1;
	gettimeofday(&header.starttime, NULL);
	sr_session_send(devc->session_dev_id, &packet);

	/* Send metadata about the SR_DF_LOGIC packets to come. */
	packet.type = SR_DF_META_LOGIC;
	packet.payload = &meta;
	meta.samplerate = devc->cur_samplerate;
	meta.num_probes = NUM_PROBES;
	sr_session_send(devc->session_dev_id, &packet);

	/* Time when we should be done (for detecting trigger timeouts). */
	devc->done = (devc->divcount + 1) * 0.08388608 + time(NULL)
			+ devc->trigger_timeout;
	devc->block_counter = 0;
	devc->trigger_found = 0;

	/* Hook up a dummy handler to receive data from the LA8. */
	sr_source_add(-1, G_IO_IN, 0, receive_data, (void *)sdi);

	return SR_OK;
}

static int hw_dev_acquisition_stop(const struct sr_dev_inst *sdi,
		void *cb_data)
{
	struct sr_datafeed_packet packet;

	(void)sdi;

	sr_dbg("la8: Stopping acquisition.");
	sr_source_remove(-1);

	/* Send end packet to the session bus. */
	sr_dbg("la8: Sending SR_DF_END.");
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
	.dev_open = hw_dev_open,
	.dev_close = hw_dev_close,
	.info_get = hw_info_get,
	.dev_config_set = hw_dev_config_set,
	.dev_acquisition_start = hw_dev_acquisition_start,
	.dev_acquisition_stop = hw_dev_acquisition_stop,
	.priv = NULL,
};
