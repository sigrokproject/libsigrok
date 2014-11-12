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

#include "protocol.h"
#include "beaglelogic.h"

SR_PRIV struct sr_dev_driver beaglelogic_driver_info;
static struct sr_dev_driver *di = &beaglelogic_driver_info;

/* Scan options */
static const uint32_t scanopts[] = {
	SR_CONF_NUM_LOGIC_CHANNELS,
};

/* Hardware capabilities */
static const uint32_t devopts[] = {
	SR_CONF_LOGIC_ANALYZER,
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
	SR_CONF_NUM_LOGIC_CHANNELS | SR_CONF_GET,
};

/* Trigger matching capabilities */
static const int32_t soft_trigger_matches[] = {
	SR_TRIGGER_ZERO,
	SR_TRIGGER_ONE,
	SR_TRIGGER_RISING,
	SR_TRIGGER_FALLING,
	SR_TRIGGER_EDGE,
};

/* Channels are numbered 0-13 */
SR_PRIV const char *beaglelogic_channel_names[NUM_CHANNELS + 1] = {
	"P8_45", "P8_46", "P8_43", "P8_44", "P8_41", "P8_42", "P8_39", "P8_40",
	"P8_27", "P8_29", "P8_28", "P8_30", "P8_21", "P8_20", NULL,
};

/* Possible sample rates : 10 Hz to 100 MHz = (100 / x) MHz */
static const uint64_t samplerates[] = {
	SR_HZ(10),
	SR_MHZ(100),
	SR_HZ(1),
};

static int init(struct sr_context *sr_ctx)
{
	return std_init(sr_ctx, di, LOG_PREFIX);
}

static struct dev_context * beaglelogic_devc_alloc(void)
{
	struct dev_context *devc;

	/* Allocate zeroed structure */
	devc = g_try_malloc0(sizeof(*devc));

	/* Default non-zero values (if any) */
	devc->fd = -1;
	devc->limit_samples = (uint64_t)-1;

	return devc;
}

static GSList *scan(GSList *options)
{
	struct drv_context *drvc;
	GSList *devices, *l;
	struct sr_config *src;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_channel *ch;
	int i, maxch;

	devices = NULL;
	drvc = di->priv;
	drvc->instances = NULL;

	/* Probe for /dev/beaglelogic */
	if (!g_file_test(BEAGLELOGIC_DEV_NODE, G_FILE_TEST_EXISTS))
		return NULL;

	sdi = sr_dev_inst_new();
	sdi->status = SR_ST_INACTIVE;
	sdi->model = g_strdup("BeagleLogic");
	sdi->version = g_strdup("1.0");
	sdi->driver = di;

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

	/* Signal */
	sr_info("BeagleLogic device found at "BEAGLELOGIC_DEV_NODE);

	/* Fill the channels */
	for (i = 0; i < maxch; i++) {
		if (!(ch = sr_channel_new(i, SR_CHANNEL_LOGIC, TRUE,
				beaglelogic_channel_names[i])))
			return NULL;
		sdi->channels = g_slist_append(sdi->channels, ch);
	}

	sdi->priv = devc;
	drvc->instances = g_slist_append(drvc->instances, sdi);
	devices = g_slist_append(devices, sdi);

	return devices;
}

static GSList *dev_list(void)
{
	return ((struct drv_context *)(di->priv))->instances;
}

static int dev_clear(void)
{
	return std_dev_clear(di, NULL);
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

	/* We're good to go now */
	sdi->status = SR_ST_ACTIVE;
	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	if (sdi->status == SR_ST_ACTIVE) {
		/* Close the memory mapping and the file */
		beaglelogic_munmap(devc);
		beaglelogic_close(devc);
	}
	sdi->status = SR_ST_INACTIVE;
	return SR_OK;
}

static int cleanup(void)
{
	struct drv_context *drvc;
	struct sr_dev_inst *sdi;
	GSList *l;

	/* unused driver */
	if (!(drvc = di->priv))
		return SR_OK;

	/* Clean up the instances */
	for (l = drvc->instances; l; l = l->next) {
		sdi = l->data;
		di->dev_close(sdi);
		g_free(sdi->priv);
		sr_dev_inst_free(sdi);
	}
	g_slist_free(drvc->instances);
	drvc->instances = NULL;

	di->priv = NULL;

	return SR_OK;
}

static int config_get(uint32_t key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
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

	case SR_CONF_NUM_LOGIC_CHANNELS:
		*data = g_variant_new_uint32(g_slist_length(sdi->channels));
		break;

	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_set(uint32_t key, GVariant *data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc = sdi->priv;
	uint64_t tmp_u64;
	(void)cg;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

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

	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	int ret;
	GVariant *gvar;
	GVariantBuilder gvb;

	(void)sdi;
	(void)data;
	(void)cg;

	ret = SR_OK;
	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				scanopts, ARRAY_SIZE(scanopts), sizeof(uint32_t));
		break;
	case SR_CONF_DEVICE_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				devopts, ARRAY_SIZE(devopts), sizeof(uint32_t));
		break;
	case SR_CONF_SAMPLERATE:
		g_variant_builder_init(&gvb, G_VARIANT_TYPE("a{sv}"));
		gvar = g_variant_new_fixed_array(G_VARIANT_TYPE("t"),
			samplerates, ARRAY_SIZE(samplerates), sizeof(uint64_t));
		g_variant_builder_add(&gvb, "{sv}", "samplerate-steps", gvar);
		*data = g_variant_builder_end(&gvb);
		break;
	case SR_CONF_TRIGGER_MATCH:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_INT32,
				soft_trigger_matches, ARRAY_SIZE(soft_trigger_matches),
				sizeof(int32_t));
		break;
	default:
		return SR_ERR_NA;
	}

	return ret;
}

/* get a sane timeout for poll() */
#define BUFUNIT_TIMEOUT_MS(devc)	(100 + ((devc->bufunitsize * 1000) / \
				(uint32_t)(devc->cur_samplerate)))

static int dev_acquisition_start(const struct sr_dev_inst *sdi,
				    void *cb_data)
{
	(void)cb_data;
	struct dev_context *devc = sdi->priv;
	struct sr_trigger *trigger;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	/* Save user pointer */
	devc->cb_data = cb_data;

	/* Clear capture state */
	devc->bytes_read = 0;
	devc->offset = 0;

	/* Configure channels */
	devc->sampleunit = g_slist_length(sdi->channels) > 8 ?
			BL_SAMPLEUNIT_16_BITS : BL_SAMPLEUNIT_8_BITS;
	beaglelogic_set_sampleunit(devc);

	/* Configure triggers & send header packet */
	if ((trigger = sr_session_trigger_get(sdi->session))) {
		devc->stl = soft_trigger_logic_new(sdi, trigger);
		devc->trigger_fired = FALSE;
	} else
		devc->trigger_fired = TRUE;
	std_session_send_df_header(cb_data, LOG_PREFIX);

	/* Trigger and add poll on file */
	beaglelogic_start(devc);
	sr_session_source_add_pollfd(sdi->session, &devc->pollfd,
			BUFUNIT_TIMEOUT_MS(devc), beaglelogic_receive_data,
			(void *)sdi);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{
	struct dev_context *devc = sdi->priv;
	struct sr_datafeed_packet pkt;

	(void)cb_data;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	/* Execute a stop on BeagleLogic */
	beaglelogic_stop(devc);

	/* lseek to offset 0, flushes the cache */
	lseek(devc->fd, 0, SEEK_SET);

	/* Remove session source and send EOT packet */
	sr_session_source_remove_pollfd(sdi->session, &devc->pollfd);
	pkt.type = SR_DF_END;
	pkt.payload = NULL;
	sr_session_send(sdi, &pkt);

	return SR_OK;
}

SR_PRIV struct sr_dev_driver beaglelogic_driver_info = {
	.name = "beaglelogic",
	.longname = "BeagleLogic",
	.api_version = 1,
	.init = init,
	.cleanup = cleanup,
	.scan = scan,
	.dev_list = dev_list,
	.dev_clear = dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.priv = NULL,
};
