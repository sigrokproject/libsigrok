/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2026 Thomas Griffiths <thomas@tgriffiths.dev>
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
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <unistd.h>

#include "protocol.h"

static const uint32_t drvopts[] = {
	SR_CONF_LOGIC_ANALYZER,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
	SR_CONF_CAPTURE_RATIO | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_NUM_LOGIC_CHANNELS | SR_CONF_GET,
};

static const int32_t trigger_matches[] = {
	SR_TRIGGER_ZERO,    SR_TRIGGER_ONE,  SR_TRIGGER_RISING,
	SR_TRIGGER_FALLING, SR_TRIGGER_EDGE,
};

// these are just some arbituary values, the real rates are given by
// an integer divider of the PIO clock (200MHz)
static const uint64_t samplerates[] = {
	SR_KHZ(10),  SR_KHZ(100), SR_MHZ(1),  SR_MHZ(2),  SR_MHZ(4),
	SR_MHZ(5),   SR_MHZ(8),	  SR_MHZ(10), SR_MHZ(16), SR_MHZ(20),
	SR_MHZ(25),  SR_MHZ(32),  SR_MHZ(40), SR_MHZ(50), SR_MHZ(80),
	SR_MHZ(100), SR_MHZ(200),
};

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	unsigned gpio;

	(void)options;

	if (!g_file_test(PIO_DEV_NODE, G_FILE_TEST_EXISTS))
		return NULL;

	sdi = g_new0(struct sr_dev_inst, 1);
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup("Raspberry Pi");
	sdi->model = g_strdup("RP1");

	devc = g_malloc0(sizeof(struct dev_context));
	devc->fd = -1;
	devc->cur_samplerate = SR_MHZ(10);
	devc->tick_step = pio_step_for_hz((double)SR_MHZ(10));
	sdi->priv = devc;

	for (gpio = 0; gpio < MAX_GPIOS; gpio++) {
		char name[16];

		snprintf(name, sizeof(name), "GPIO%u", gpio);
		sr_channel_new(sdi, gpio, SR_CHANNEL_LOGIC, TRUE, name);
	}

	return std_scan_complete(di, g_slist_append(NULL, sdi));
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	devc->fd = open(PIO_DEV_NODE, O_RDWR);
	if (devc->fd < 0) {
		sr_err("Cannot open " PIO_DEV_NODE ": %s.", g_strerror(errno));
		return SR_ERR_IO;
	}
	sdi->status = SR_ST_ACTIVE;
	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	if (devc->fd >= 0) {
		close(devc->fd);
		devc->fd = -1;
	}
	sdi->status = SR_ST_INACTIVE;
	return SR_OK;
}

static void clear_helper(struct dev_context *devc)
{
	unsigned i;

	for (i = 0; i < MAX_SMS; i++)
		g_free(devc->sm[i].buffer);
	g_free(devc->outbuf);
}

static int dev_clear(const struct sr_dev_driver *di)
{
	return std_dev_clear_with_callback(
		di, (std_dev_clear_callback)clear_helper);
}

static int config_get(uint32_t key, GVariant **data,
		      const struct sr_dev_inst *sdi,
		      const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	(void)cg;

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;

	switch (key) {
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(devc->cur_samplerate);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		*data = g_variant_new_uint64(devc->limit_samples);
		break;
	case SR_CONF_CAPTURE_RATIO:
		*data = g_variant_new_uint64(devc->capture_ratio);
		break;
	case SR_CONF_NUM_LOGIC_CHANNELS:
		*data = g_variant_new_uint32(g_slist_length(sdi->channels));
		break;
	default:
		return SR_ERR_NA;
	}
	return SR_OK;
}

static int config_set(uint32_t key, GVariant *data,
		      const struct sr_dev_inst *sdi,
		      const struct sr_channel_group *cg)
{
	struct dev_context *devc = sdi->priv;
	uint64_t hz;
	unsigned step;

	(void)cg;

	switch (key) {
	case SR_CONF_SAMPLERATE:
		hz = g_variant_get_uint64(data);
		step = pio_step_for_hz((double)hz);
		if (!step) {
			sr_err("Samplerate %" PRIu64 " Hz out of range "
			       "(%.0f-%.0f Hz).",
			       hz, pio_step_hz(PIO_STEP_MAX),
			       pio_step_hz(PIO_STEP_MIN));
			return SR_ERR_SAMPLERATE;
		}
		devc->tick_step = step;
		devc->cur_samplerate = (uint64_t)(pio_step_hz(step) + 0.5);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		devc->limit_samples = g_variant_get_uint64(data);
		break;
	case SR_CONF_CAPTURE_RATIO:
		devc->capture_ratio = g_variant_get_uint64(data);
		break;
	default:
		return SR_ERR_NA;
	}
	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data,
		       const struct sr_dev_inst *sdi,
		       const struct sr_channel_group *cg)
{
	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
	case SR_CONF_DEVICE_OPTIONS:
		return STD_CONFIG_LIST(key, data, sdi, cg, NULL, drvopts,
				       devopts);
	case SR_CONF_SAMPLERATE:
		*data = std_gvar_samplerates(ARRAY_AND_SIZE(samplerates));
		break;
	case SR_CONF_TRIGGER_MATCH:
		*data = std_gvar_array_i32(ARRAY_AND_SIZE(trigger_matches));
		break;
	default:
		return SR_ERR_NA;
	}
	return SR_OK;
}

static unsigned get_logic_unitsize(GSList *channels)
{
	int max_gpio = 0;
	GSList *l;

	for (l = channels; l; l = l->next) {
		struct sr_channel *ch = l->data;
		if (ch->enabled && ch->type == SR_CHANNEL_LOGIC) {
			if (ch->index > max_gpio)
				max_gpio = ch->index;
		}
	}

	if (max_gpio < 8)
		return 1;
	if (max_gpio < 16)
		return 2;
	return 4;
}

static int get_max_enabled_pin(const struct sr_dev_inst *sdi)
{
	int max_pin = -1;

	for (const GSList *l = sdi ? sdi->channels : NULL; l; l = l->next) {
		const struct sr_channel *ch = l->data;
		if (ch->enabled && ch->type == SR_CHANNEL_LOGIC)
			max_pin = MAX(max_pin, (int)ch->index);
	}
	return max_pin;
}

static int get_max_pin_for_freq(uint64_t hz)
{
	for (size_t i = 0; i < G_N_ELEMENTS(sm_pin_layouts); i++) {
		if (hz <= sm_pin_layouts[i].max_freq)
			return sm_pin_layouts[i].total_width - 1;
	}
	return -1;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	struct sr_trigger *trigger;

	if (get_max_enabled_pin(sdi) >
	    get_max_pin_for_freq(devc->cur_samplerate)) {
		sr_err("Frequency %" PRIu64
		       " Hz is too high for GPIO%d enabled.",
		       devc->cur_samplerate, get_max_enabled_pin(sdi));
		return SR_ERR_SAMPLERATE;
	}

	if (la_build_sms(sdi) != SR_OK)
		return SR_ERR;

	devc->unitsize = get_logic_unitsize(sdi->channels);
	devc->outbuf_cap = devc->samples_per_round * devc->unitsize;
	devc->outbuf = g_malloc(devc->outbuf_cap);
	devc->ended = FALSE;

	if ((trigger = sr_session_trigger_get(sdi->session))) {
		int pre_trigger_samples = 0;

		if (devc->limit_samples > 0)
			pre_trigger_samples =
				(devc->capture_ratio * devc->limit_samples) /
				100;
		devc->stl = soft_trigger_logic_new(sdi, trigger,
						   pre_trigger_samples);
		if (!devc->stl) {
			g_free(devc->outbuf);
			devc->outbuf = NULL;
			return SR_ERR_MALLOC;
		}
		devc->trigger_fired = FALSE;
	} else {
		devc->stl = NULL;
		devc->trigger_fired = TRUE;
	}

	if (la_start(sdi) != SR_OK) {
		if (devc->stl) {
			soft_trigger_logic_free(devc->stl);
			devc->stl = NULL;
		}
		g_free(devc->outbuf);
		devc->outbuf = NULL;
		return SR_ERR;
	}

	devc->event_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
	if (devc->event_fd < 0) {
		sr_err("Failed to create eventfd: %s", g_strerror(errno));
		return SR_ERR;
	}

	std_session_send_df_header(sdi);
	sr_session_send_meta(sdi, SR_CONF_SAMPLERATE,
			     g_variant_new_uint64(devc->cur_samplerate));

	sr_session_source_add(sdi->session, devc->event_fd, G_IO_IN, -1,
			      la_receive_data, (void *)sdi);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	if (devc->event_fd >= 0) {
		sr_session_source_remove(sdi->session, devc->event_fd);
		close(devc->event_fd);
		devc->event_fd = -1;
	}
	la_stop(devc);

	if (devc->stl) {
		soft_trigger_logic_free(devc->stl);
		devc->stl = NULL;
	}
	if (!devc->ended) {
		devc->ended = TRUE;
		std_session_send_df_end(sdi);
	}

	g_free(devc->outbuf);
	devc->outbuf = NULL;
	return SR_OK;
}

static struct sr_dev_driver raspberrypi_rp1_driver_info = {
	.name = "raspberrypi-rp1",
	.longname = "Raspberry Pi RP1",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(raspberrypi_rp1_driver_info);