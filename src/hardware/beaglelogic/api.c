/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 Kumar Abhishek <abhishek@theembeddedkitchen.net>
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
#include "protocol.h"
#include "beaglelogic.h"

static const uint32_t scanopts[] = {
	SR_CONF_NUM_LOGIC_CHANNELS,
};

static const uint32_t drvopts[] = {
	SR_CONF_LOGIC_ANALYZER,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
	SR_CONF_CAPTURE_RATIO | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_NUM_LOGIC_CHANNELS | SR_CONF_GET,
};

static const int32_t trigger_matches[] = {
	SR_TRIGGER_ZERO,
	SR_TRIGGER_ONE,
	SR_TRIGGER_RISING,
	SR_TRIGGER_FALLING,
	SR_TRIGGER_EDGE,
};

SR_PRIV const char *channel_names[] = {
	"P8_45", "P8_46", "P8_43", "P8_44", "P8_41", "P8_42", "P8_39",
	"P8_40", "P8_27", "P8_29", "P8_28", "P8_30", "P8_21", "P8_20",
};

/* Possible sample rates : 10 Hz to 100 MHz = (100 / x) MHz */
static const uint64_t samplerates[] = {
	SR_HZ(10),
	SR_MHZ(100),
	SR_HZ(1),
};

static struct dev_context *beaglelogic_devc_alloc(void)
{
	struct dev_context *devc;

	devc = g_malloc0(sizeof(struct dev_context));

	/* Default non-zero values (if any) */
	devc->fd = -1;
	devc->limit_samples = (uint64_t)-1;

	return devc;
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	GSList *l;
	struct sr_config *src;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	int i, maxch;

	/* Probe for /dev/beaglelogic */
	if (!g_file_test(BEAGLELOGIC_DEV_NODE, G_FILE_TEST_EXISTS))
		return NULL;

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->status = SR_ST_INACTIVE;
	sdi->model = g_strdup("BeagleLogic");
	sdi->version = g_strdup("1.0");

	/* Unless explicitly specified, keep max channels to 8 only */
	maxch = 8;
	for (l = options; l; l = l->next) {
		src = l->data;
		if (src->key == SR_CONF_NUM_LOGIC_CHANNELS)
			maxch = g_variant_get_int32(src->data);
	}

	/* We need to test for number of channels by opening the node */
	devc = beaglelogic_devc_alloc();

	if (beaglelogic_open_nonblock(devc) != SR_OK) {
		g_free(devc);
		sr_dev_inst_free(sdi);

		return NULL;
	}

	if (maxch > 8) {
		maxch = NUM_CHANNELS;
		devc->sampleunit = BL_SAMPLEUNIT_16_BITS;
	} else {
		maxch = 8;
		devc->sampleunit = BL_SAMPLEUNIT_8_BITS;
	}

	beaglelogic_set_sampleunit(devc);
	beaglelogic_close(devc);

	sr_info("BeagleLogic device found at "BEAGLELOGIC_DEV_NODE);

	for (i = 0; i < maxch; i++)
		sr_channel_new(sdi, i, SR_CHANNEL_LOGIC, TRUE,
				channel_names[i]);

	sdi->priv = devc;

	return std_scan_complete(di, g_slist_append(NULL, sdi));
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	/* Open BeagleLogic */
	if (beaglelogic_open_nonblock(devc))
		return SR_ERR;

	/* Set fd and local attributes */
	devc->pollfd.fd = devc->fd;
	devc->pollfd.events = G_IO_IN;
	devc->pollfd.revents = 0;

	/* Get the default attributes */
	beaglelogic_get_samplerate(devc);
	beaglelogic_get_sampleunit(devc);
	beaglelogic_get_triggerflags(devc);
	beaglelogic_get_buffersize(devc);
	beaglelogic_get_bufunitsize(devc);

	/* Map the kernel capture FIFO for reads, saves 1 level of memcpy */
	if (beaglelogic_mmap(devc) != SR_OK) {
		sr_err("Unable to map capture buffer");
		beaglelogic_close(devc);
		return SR_ERR;
	}

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	/* Close the memory mapping and the file */
	beaglelogic_munmap(devc);
	beaglelogic_close(devc);

	return SR_OK;
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc = sdi->priv;

	(void)cg;

	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
		*data = g_variant_new_uint64(devc->limit_samples);
		break;
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(devc->cur_samplerate);
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
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc = sdi->priv;
	uint64_t tmp_u64;

	(void)cg;

	switch (key) {
	case SR_CONF_SAMPLERATE:
		devc->cur_samplerate = g_variant_get_uint64(data);
		return beaglelogic_set_samplerate(devc);
	case SR_CONF_LIMIT_SAMPLES:
		tmp_u64 = g_variant_get_uint64(data);
		devc->limit_samples = tmp_u64;
		devc->triggerflags = BL_TRIGGERFLAGS_ONESHOT;

		/* Check if we have sufficient buffer size */
		tmp_u64 *= SAMPLEUNIT_TO_BYTES(devc->sampleunit);
		if (tmp_u64 > devc->buffersize) {
			sr_warn("Insufficient buffer space has been allocated.");
			sr_warn("Please use \'echo <size in bytes> > "\
				BEAGLELOGIC_SYSFS_ATTR(memalloc) \
				"\' as root to increase the buffer size, this"\
				" capture is now truncated to %d Msamples",
				devc->buffersize /
				(SAMPLEUNIT_TO_BYTES(devc->sampleunit) * 1000000));
		}
		return beaglelogic_set_triggerflags(devc);
	case SR_CONF_CAPTURE_RATIO:
		devc->capture_ratio = g_variant_get_uint64(data);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
	case SR_CONF_DEVICE_OPTIONS:
		return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
	case SR_CONF_SAMPLERATE:
		*data = std_gvar_samplerates_steps(ARRAY_AND_SIZE(samplerates));
		break;
	case SR_CONF_TRIGGER_MATCH:
		*data = std_gvar_array_i32(ARRAY_AND_SIZE(trigger_matches));
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

/* get a sane timeout for poll() */
#define BUFUNIT_TIMEOUT_MS(devc)	(100 + ((devc->bufunitsize * 1000) / \
				(uint32_t)(devc->cur_samplerate)))

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	struct sr_trigger *trigger;

	/* Clear capture state */
	devc->bytes_read = 0;
	devc->offset = 0;

	/* Configure channels */
	devc->sampleunit = g_slist_length(sdi->channels) > 8 ?
			BL_SAMPLEUNIT_16_BITS : BL_SAMPLEUNIT_8_BITS;
	beaglelogic_set_sampleunit(devc);

	/* Configure triggers & send header packet */
	if ((trigger = sr_session_trigger_get(sdi->session))) {
		int pre_trigger_samples = 0;
		if (devc->limit_samples > 0)
			pre_trigger_samples = (devc->capture_ratio * devc->limit_samples) / 100;
		devc->stl = soft_trigger_logic_new(sdi, trigger, pre_trigger_samples);
		if (!devc->stl)
			return SR_ERR_MALLOC;
		devc->trigger_fired = FALSE;
	} else
		devc->trigger_fired = TRUE;
	std_session_send_df_header(sdi);

	/* Trigger and add poll on file */
	beaglelogic_start(devc);
	sr_session_source_add_pollfd(sdi->session, &devc->pollfd,
			BUFUNIT_TIMEOUT_MS(devc), beaglelogic_receive_data,
			(void *)sdi);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	/* Execute a stop on BeagleLogic */
	beaglelogic_stop(devc);

	/* lseek to offset 0, flushes the cache */
	lseek(devc->fd, 0, SEEK_SET);

	/* Remove session source and send EOT packet */
	sr_session_source_remove_pollfd(sdi->session, &devc->pollfd);
	std_session_send_df_end(sdi);

	return SR_OK;
}

static struct sr_dev_driver beaglelogic_driver_info = {
	.name = "beaglelogic",
	.longname = "BeagleLogic",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = std_dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(beaglelogic_driver_info);
