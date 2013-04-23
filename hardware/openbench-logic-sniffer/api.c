/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Bert Vermeulen <bert@biot.com>
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

#include "protocol.h"

#define SERIALCOMM "115200/8n1"

static const int32_t hwopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const int32_t hwcaps[] = {
	SR_CONF_LOGIC_ANALYZER,
	SR_CONF_SAMPLERATE,
	SR_CONF_TRIGGER_TYPE,
	SR_CONF_CAPTURE_RATIO,
	SR_CONF_LIMIT_SAMPLES,
	SR_CONF_RLE,
};

/* Probes are numbered 0-31 (on the PCB silkscreen). */
SR_PRIV const char *ols_probe_names[NUM_PROBES + 1] = {
	"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12",
	"13", "14", "15", "16", "17", "18", "19", "20", "21", "22", "23",
	"24", "25", "26", "27", "28", "29", "30", "31",
	NULL,
};

/* Default supported samplerates, can be overridden by device metadata. */
static const uint64_t samplerates[] = {
	SR_HZ(10),
	SR_MHZ(200),
	SR_HZ(1),
};

SR_PRIV struct sr_dev_driver ols_driver_info;
static struct sr_dev_driver *di = &ols_driver_info;

static int hw_init(struct sr_context *sr_ctx)
{
	return std_hw_init(sr_ctx, di, DRIVER_LOG_DOMAIN);
}

static GSList *hw_scan(GSList *options)
{
	struct sr_config *src;
	struct sr_dev_inst *sdi;
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_probe *probe;
	struct sr_serial_dev_inst *serial;
	GPollFD probefd;
	GSList *l, *devices;
	int ret, i;
	const char *conn, *serialcomm;
	char buf[8];

	(void)options;

	drvc = di->priv;

	devices = NULL;

	conn = serialcomm = NULL;
	for (l = options; l; l = l->next) {
		src = l->data;
		switch (src->key) {
		case SR_CONF_CONN:
			conn = g_variant_get_string(src->data, NULL);
			break;
		case SR_CONF_SERIALCOMM:
			serialcomm = g_variant_get_string(src->data, NULL);
			break;
		}
	}
	if (!conn)
		return NULL;

	if (serialcomm == NULL)
		serialcomm = SERIALCOMM;

	if (!(serial = sr_serial_dev_inst_new(conn, serialcomm)))
		return NULL;

	/* The discovery procedure is like this: first send the Reset
	 * command (0x00) 5 times, since the device could be anywhere
	 * in a 5-byte command. Then send the ID command (0x02).
	 * If the device responds with 4 bytes ("OLS1" or "SLA1"), we
	 * have a match.
	 */
	sr_info("Probing %s.", conn);
	if (serial_open(serial, SERIAL_RDWR | SERIAL_NONBLOCK) != SR_OK)
		return NULL;

	ret = SR_OK;
	for (i = 0; i < 5; i++) {
		if ((ret = send_shortcommand(serial, CMD_RESET)) != SR_OK) {
			sr_err("Port %s is not writable.", conn);
			break;
		}
	}
	if (ret != SR_OK) {
		serial_close(serial);
		sr_err("Could not use port %s. Quitting.", conn);
		return NULL;
	}
	send_shortcommand(serial, CMD_ID);

	/* Wait 10ms for a response. */
	g_usleep(10000);

	probefd.fd = serial->fd;
	probefd.events = G_IO_IN;
	g_poll(&probefd, 1, 1);

	if (probefd.revents != G_IO_IN)
		return NULL;
	if (serial_read(serial, buf, 4) != 4)
		return NULL;
	if (strncmp(buf, "1SLO", 4) && strncmp(buf, "1ALS", 4))
		return NULL;

	/* Definitely using the OLS protocol, check if it supports
	 * the metadata command.
	 */
	send_shortcommand(serial, CMD_METADATA);
	if (g_poll(&probefd, 1, 10) > 0) {
		/* Got metadata. */
		sdi = get_metadata(serial);
		sdi->index = 0;
		devc = sdi->priv;
	} else {
		/* Not an OLS -- some other board that uses the sump protocol. */
		sr_info("Device does not support metadata.");
		sdi = sr_dev_inst_new(0, SR_ST_INACTIVE,
				"Sump", "Logic Analyzer", "v1.0");
		sdi->driver = di;
		for (i = 0; i < 32; i++) {
			if (!(probe = sr_probe_new(i, SR_PROBE_LOGIC, TRUE,
					ols_probe_names[i])))
				return 0;
			sdi->probes = g_slist_append(sdi->probes, probe);
		}
		devc = ols_dev_new();
		sdi->priv = devc;
	}
	/* Configure samplerate and divider. */
	if (ols_set_samplerate(sdi, DEFAULT_SAMPLERATE) != SR_OK)
		sr_dbg("Failed to set default samplerate (%"PRIu64").",
				DEFAULT_SAMPLERATE);
	/* Clear trigger masks, values and stages. */
	ols_configure_probes(sdi);
	sdi->inst_type = SR_INST_SERIAL;
	sdi->conn = serial;

	drvc->instances = g_slist_append(drvc->instances, sdi);
	devices = g_slist_append(devices, sdi);

	serial_close(serial);

	return devices;
}

static GSList *hw_dev_list(void)
{
	return ((struct drv_context *)(di->priv))->instances;
}

static int hw_dev_open(struct sr_dev_inst *sdi)
{
	struct sr_serial_dev_inst *serial;

	serial = sdi->conn;
	if (serial_open(serial, SERIAL_RDWR) != SR_OK)
		return SR_ERR;

	sdi->status = SR_ST_ACTIVE;

	return SR_OK;
}

static int hw_dev_close(struct sr_dev_inst *sdi)
{
	struct sr_serial_dev_inst *serial;

	serial = sdi->conn;
	if (serial && serial->fd != -1) {
		serial_close(serial);
		sdi->status = SR_ST_INACTIVE;
	}

	return SR_OK;
}

static int hw_cleanup(void)
{
	GSList *l;
	struct sr_dev_inst *sdi;
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	int ret = SR_OK;

	if (!(drvc = di->priv))
		return SR_OK;

	/* Properly close and free all devices. */
	for (l = drvc->instances; l; l = l->next) {
		if (!(sdi = l->data)) {
			/* Log error, but continue cleaning up the rest. */
			sr_err("%s: sdi was NULL, continuing", __func__);
			ret = SR_ERR_BUG;
			continue;
		}
		if (!(devc = sdi->priv)) {
			/* Log error, but continue cleaning up the rest. */
			sr_err("%s: sdi->priv was NULL, continuing", __func__);
			ret = SR_ERR_BUG;
			continue;
		}
		hw_dev_close(sdi);
		serial = sdi->conn;
		sr_serial_dev_inst_free(serial);
		sr_dev_inst_free(sdi);
	}
	g_slist_free(drvc->instances);
	drvc->instances = NULL;

	return ret;
}

static int config_get(int id, GVariant **data, const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;
	switch (id) {
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(devc->cur_samplerate);
		break;
	case SR_CONF_CAPTURE_RATIO:
		*data = g_variant_new_uint64(devc->capture_ratio);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		*data = g_variant_new_uint64(devc->limit_samples);
		break;
	case SR_CONF_RLE:
		*data = g_variant_new_boolean(devc->flag_reg & FLAG_RLE ? TRUE : FALSE);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_set(int id, GVariant *data, const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int ret;
	uint64_t tmp_u64;

	devc = sdi->priv;

	switch (id) {
	case SR_CONF_SAMPLERATE:
		tmp_u64 = g_variant_get_uint64(data);
		if (tmp_u64 < samplerates[0] || tmp_u64 > samplerates[1])
			return SR_ERR_SAMPLERATE;
		ret = ols_set_samplerate(sdi, g_variant_get_uint64(data));
		break;
	case SR_CONF_LIMIT_SAMPLES:
		tmp_u64 = g_variant_get_uint64(data);
		if (tmp_u64 < MIN_NUM_SAMPLES)
			return SR_ERR;
		devc->limit_samples = tmp_u64;
		ret = SR_OK;
		break;
	case SR_CONF_CAPTURE_RATIO:
		devc->capture_ratio = g_variant_get_uint64(data);
		if (devc->capture_ratio < 0 || devc->capture_ratio > 100) {
			devc->capture_ratio = 0;
			ret = SR_ERR;
		} else
			ret = SR_OK;
		break;
	case SR_CONF_RLE:
		if (g_variant_get_boolean(data)) {
			sr_info("Enabling RLE.");
			devc->flag_reg |= FLAG_RLE;
		} else {
			sr_info("Disabling RLE.");
			devc->flag_reg &= ~FLAG_RLE;
		}
		ret = SR_OK;
		break;
	default:
		ret = SR_ERR_NA;
	}

	return ret;
}

static int config_list(int key, GVariant **data, const struct sr_dev_inst *sdi)
{
	GVariant *gvar;
	GVariantBuilder gvb;

	(void)sdi;

	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_INT32,
				hwopts, ARRAY_SIZE(hwopts), sizeof(int32_t));
		break;
	case SR_CONF_DEVICE_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_INT32,
				hwcaps, ARRAY_SIZE(hwcaps), sizeof(int32_t));
		break;
	case SR_CONF_SAMPLERATE:
		g_variant_builder_init(&gvb, G_VARIANT_TYPE("a{sv}"));
		gvar = g_variant_new_fixed_array(G_VARIANT_TYPE("t"), samplerates,
				ARRAY_SIZE(samplerates), sizeof(uint64_t));
		g_variant_builder_add(&gvb, "{sv}", "samplerate-steps", gvar);
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

static int hw_dev_acquisition_start(const struct sr_dev_inst *sdi,
		void *cb_data)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	uint32_t trigger_config[4];
	uint32_t data;
	uint16_t readcount, delaycount;
	uint8_t changrp_mask;
	int num_channels;
	int i;

	devc = sdi->priv;
	serial = sdi->conn;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR;

	if (ols_configure_probes(sdi) != SR_OK) {
		sr_err("Failed to configure probes.");
		return SR_ERR;
	}

	/*
	 * Enable/disable channel groups in the flag register according to the
	 * probe mask. Calculate this here, because num_channels is needed
	 * to limit readcount.
	 */
	changrp_mask = 0;
	num_channels = 0;
	for (i = 0; i < 4; i++) {
		if (devc->probe_mask & (0xff << (i * 8))) {
			changrp_mask |= (1 << i);
			num_channels++;
		}
	}

	/*
	 * Limit readcount to prevent reading past the end of the hardware
	 * buffer.
	 */
	readcount = MIN(devc->max_samples / num_channels, devc->limit_samples) / 4;

	memset(trigger_config, 0, 16);
	trigger_config[devc->num_stages] |= 0x08;
	if (devc->trigger_mask[0]) {
		delaycount = readcount * (1 - devc->capture_ratio / 100.0);
		devc->trigger_at = (readcount - delaycount) * 4 - devc->num_stages;

		if (send_longcommand(serial, CMD_SET_TRIGGER_MASK_0,
			reverse32(devc->trigger_mask[0])) != SR_OK)
			return SR_ERR;
		if (send_longcommand(serial, CMD_SET_TRIGGER_VALUE_0,
			reverse32(devc->trigger_value[0])) != SR_OK)
			return SR_ERR;
		if (send_longcommand(serial, CMD_SET_TRIGGER_CONFIG_0,
			trigger_config[0]) != SR_OK)
			return SR_ERR;

		if (send_longcommand(serial, CMD_SET_TRIGGER_MASK_1,
			reverse32(devc->trigger_mask[1])) != SR_OK)
			return SR_ERR;
		if (send_longcommand(serial, CMD_SET_TRIGGER_VALUE_1,
			reverse32(devc->trigger_value[1])) != SR_OK)
			return SR_ERR;
		if (send_longcommand(serial, CMD_SET_TRIGGER_CONFIG_1,
			trigger_config[1]) != SR_OK)
			return SR_ERR;

		if (send_longcommand(serial, CMD_SET_TRIGGER_MASK_2,
			reverse32(devc->trigger_mask[2])) != SR_OK)
			return SR_ERR;
		if (send_longcommand(serial, CMD_SET_TRIGGER_VALUE_2,
			reverse32(devc->trigger_value[2])) != SR_OK)
			return SR_ERR;
		if (send_longcommand(serial, CMD_SET_TRIGGER_CONFIG_2,
			trigger_config[2]) != SR_OK)
			return SR_ERR;

		if (send_longcommand(serial, CMD_SET_TRIGGER_MASK_3,
			reverse32(devc->trigger_mask[3])) != SR_OK)
			return SR_ERR;
		if (send_longcommand(serial, CMD_SET_TRIGGER_VALUE_3,
			reverse32(devc->trigger_value[3])) != SR_OK)
			return SR_ERR;
		if (send_longcommand(serial, CMD_SET_TRIGGER_CONFIG_3,
			trigger_config[3]) != SR_OK)
			return SR_ERR;
	} else {
		if (send_longcommand(serial, CMD_SET_TRIGGER_MASK_0,
				devc->trigger_mask[0]) != SR_OK)
			return SR_ERR;
		if (send_longcommand(serial, CMD_SET_TRIGGER_VALUE_0,
				devc->trigger_value[0]) != SR_OK)
			return SR_ERR;
		if (send_longcommand(serial, CMD_SET_TRIGGER_CONFIG_0,
		     0x00000008) != SR_OK)
			return SR_ERR;
		delaycount = readcount;
	}

	sr_info("Setting samplerate to %" PRIu64 "Hz (divider %u, "
		"demux %s)", devc->cur_samplerate, devc->cur_samplerate_divider,
		devc->flag_reg & FLAG_DEMUX ? "on" : "off");
	if (send_longcommand(serial, CMD_SET_DIVIDER,
			reverse32(devc->cur_samplerate_divider)) != SR_OK)
		return SR_ERR;

	/* Send sample limit and pre/post-trigger capture ratio. */
	data = ((readcount - 1) & 0xffff) << 16;
	data |= (delaycount - 1) & 0xffff;
	if (send_longcommand(serial, CMD_CAPTURE_SIZE, reverse16(data)) != SR_OK)
		return SR_ERR;

	/* The flag register wants them here, and 1 means "disable channel". */
	devc->flag_reg |= ~(changrp_mask << 2) & 0x3c;
	devc->flag_reg |= FLAG_FILTER;
	devc->rle_count = 0;
	data = (devc->flag_reg << 24) | ((devc->flag_reg << 8) & 0xff0000);
	if (send_longcommand(serial, CMD_SET_FLAGS, data) != SR_OK)
		return SR_ERR;

	/* Start acquisition on the device. */
	if (send_shortcommand(serial, CMD_RUN) != SR_OK)
		return SR_ERR;

	/* Reset all operational states. */
	devc->num_transfers = devc->num_samples = devc->num_bytes = 0;

	/* Send header packet to the session bus. */
	std_session_send_df_header(cb_data, DRIVER_LOG_DOMAIN);

	sr_source_add(serial->fd, G_IO_IN, -1, ols_receive_data, cb_data);

	return SR_OK;
}

static int hw_dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{
	/* Avoid compiler warnings. */
	(void)cb_data;

	abort_acquisition(sdi);

	return SR_OK;
}

SR_PRIV struct sr_dev_driver ols_driver_info = {
	.name = "ols",
	.longname = "Openbench Logic Sniffer",
	.api_version = 1,
	.init = hw_init,
	.cleanup = hw_cleanup,
	.scan = hw_scan,
	.dev_list = hw_dev_list,
	.dev_clear = hw_cleanup,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = hw_dev_open,
	.dev_close = hw_dev_close,
	.dev_acquisition_start = hw_dev_acquisition_start,
	.dev_acquisition_stop = hw_dev_acquisition_stop,
	.priv = NULL,
};
