/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2016 Alexandru Gagniuc <mr.nuke.me@gmail.com>
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
#include <scpi.h>
#include <string.h>
#include "protocol.h"

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t drvopts[] = {
	SR_CONF_MULTIMETER,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_SET,
	SR_CONF_MEASURED_QUANTITY | SR_CONF_SET,
	SR_CONF_ADC_POWERLINE_CYCLES | SR_CONF_SET | SR_CONF_GET,
};

static struct sr_dev_driver hp_3457a_driver_info;

static int create_front_channel(struct sr_dev_inst *sdi, int chan_idx)
{
	struct sr_channel *channel;
	struct sr_channel_group *front;
	struct channel_context *chanc;

	chanc = g_malloc(sizeof(*chanc));
	chanc->location = CONN_FRONT;

	channel = sr_channel_new(sdi, chan_idx++, SR_CHANNEL_ANALOG,
				 TRUE, "Front");
	channel->priv = chanc;

	front = g_malloc0(sizeof(*front));
	front->name = g_strdup("Front");
	front->channels = g_slist_append(front->channels, channel);

	sdi->channel_groups = g_slist_append(sdi->channel_groups, front);

	return chan_idx;
}

static int create_rear_channels(struct sr_dev_inst *sdi, int chan_idx,
				 const struct rear_card_info *card)
{
	unsigned int i;
	struct sr_channel *channel;
	struct sr_channel_group *group;
	struct channel_context *chanc;
	char name[16];

	/* When card is NULL, we couldn't identify the type of card. */
	if (!card)
		return chan_idx;

	group = g_malloc0(sizeof(*group));
	group->priv = NULL;
	group->name = g_strdup(card->cg_name);
	sdi->channel_groups = g_slist_append(sdi->channel_groups, group);

	for (i = 0; i < card->num_channels; i++) {

		chanc = g_malloc(sizeof(*chanc));
		chanc->location = CONN_REAR;

		if (card->type == REAR_TERMINALS) {
			chanc->index = -1;
			g_snprintf(name, sizeof(name), "%s", card->cg_name);
		} else {
			chanc->index = i;
			g_snprintf(name, sizeof(name), "%s%u", card->cg_name, i);
		}

		channel = sr_channel_new(sdi, chan_idx++, SR_CHANNEL_ANALOG,
					FALSE, name);
		channel->priv = chanc;
		group->channels = g_slist_append(group->channels, channel);
	}

	return chan_idx;
}

static gchar *get_revision(struct sr_scpi_dev_inst *scpi)
{
	int ret, major, minor;
	GArray *rev_numbers;

	/* Report a version of '0.0' if we can't parse the response. */
	major = minor = 0;

	ret = sr_scpi_get_floatv(scpi, "REV?", &rev_numbers);
	if ((ret == SR_OK) && (rev_numbers->len >= 2)) {
		major = (int)g_array_index(rev_numbers, float, 0);
		minor = (int)g_array_index(rev_numbers, float, 1);
	}

	g_array_free(rev_numbers, TRUE);

	return g_strdup_printf("%d.%d", major, minor);
}

static struct sr_dev_inst *probe_device(struct sr_scpi_dev_inst *scpi)
{
	int ret, idx;
	char *response;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;

	/*
	 * This command ensures we receive an EOI after every response, so that
	 * we don't wait the entire timeout after the response is received.
	 */
	if (sr_scpi_send(scpi, "END ALWAYS") != SR_OK)
		return NULL;

	ret = sr_scpi_get_string(scpi, "ID?", &response);
	if ((ret != SR_OK) || !response)
		return NULL;

	if (strcmp(response, "HP3457A"))
		return NULL;

	g_free(response);

	devc = g_malloc0(sizeof(struct dev_context));
	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->vendor = g_strdup("Hewlett-Packard");
	sdi->model = g_strdup("3457A");
	sdi->version = get_revision(scpi);
	sdi->conn = scpi;
	sdi->driver = &hp_3457a_driver_info;
	sdi->inst_type = SR_INST_SCPI;
	sdi->priv = devc;

	/* There is no way to probe the measurement mode. It must be set. */
	devc->measurement_mq = 0;
	devc->measurement_unit = 0;

	/* Probe rear card option and create channels accordingly (TODO). */
	devc->rear_card = hp_3457a_probe_rear_card(scpi);
	idx = 0;
	idx = create_front_channel(sdi, idx);
	create_rear_channels(sdi, idx, devc->rear_card);

	return sdi;
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	return sr_scpi_scan(di->context, options, probe_device);
}

/*
 * We need to set the HP 3457A to a known state, and there are quite a number
 * of knobs to tweak. Here's a brief explanation of what's going on. For more
 * details, print out and consult the user manual.
 *   PRESET
 *     Set the instrument to a pre-determined state. This is easier and faster
 *     than sending a few dozen commands. Some of the PRESET defaults include
 *     ASCII output format, and synchronous triggering. See user manual for
 *     more details.
 *
 * After the PRESET command, the instrument is in a known state, and only those
 * parameters for which the default is unsuitable are modified:
 *   INBUF ON
 *     Enable the HP-IB input buffer. This allows the instrument to release the
 *     HP-IB bus before processing the command, and increases throughput on
 *     GPIB buses with more than one device.
 *   TRIG HOLD
 *     Do not trigger new measurements until instructed to do so.
 */
static int dev_open(struct sr_dev_inst *sdi)
{
	struct sr_scpi_dev_inst *scpi = sdi->conn;
	struct dev_context *devc;

	if (sr_scpi_open(scpi) != SR_OK)
		return SR_ERR;

	devc = sdi->priv;

	sr_scpi_send(scpi, "PRESET");
	sr_scpi_send(scpi, "INBUF ON");
	sr_scpi_send(scpi, "TRIG HOLD");
	sr_scpi_get_float(scpi, "NPLC?", &devc->nplc);

	sdi->status = SR_ST_ACTIVE;

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct sr_scpi_dev_inst *scpi = sdi->conn;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	/* Disable scan-advance (preserve relay life). */
	sr_scpi_send(scpi, "SADV HOLD");
	/* Switch back to auto-triggering. */
	sr_scpi_send(scpi, "TRIG AUTO");

	sr_scpi_close(scpi);

	sdi->status = SR_ST_INACTIVE;

	return SR_OK;
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int ret;
	struct dev_context *devc;

	(void)cg;

	devc = sdi->priv;

	ret = SR_OK;
	switch (key) {
	case SR_CONF_ADC_POWERLINE_CYCLES:
		*data = g_variant_new_double(devc->nplc);
		break;
	default:
		return SR_ERR_NA;
	}

	return ret;
}

static int config_set(uint32_t key, GVariant *data,
		      const struct sr_dev_inst *sdi,
		      const struct sr_channel_group *cg)
{
	int ret;
	enum sr_mq mq;
	enum sr_mqflag mq_flags;
	struct dev_context *devc;
	GVariant *tuple_child;

	(void)cg;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	devc = sdi->priv;

	ret = SR_OK;
	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
		devc->limit_samples = g_variant_get_uint64(data);
		break;
	case SR_CONF_MEASURED_QUANTITY:
		tuple_child = g_variant_get_child_value(data, 0);
		mq = g_variant_get_uint32(tuple_child);
		tuple_child = g_variant_get_child_value(data, 1);
		mq_flags = g_variant_get_uint64(tuple_child);
		ret = hp_3457a_set_mq(sdi, mq, mq_flags);
		g_variant_unref(tuple_child);
		break;
	case SR_CONF_ADC_POWERLINE_CYCLES:
		ret = hp_3457a_set_nplc(sdi, g_variant_get_double(data));
		break;
	default:
		ret = SR_ERR_NA;
	}

	return ret;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int ret;

	if (key == SR_CONF_SCAN_OPTIONS) {
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
			scanopts, ARRAY_SIZE(scanopts), sizeof(uint32_t));
		return SR_OK;
	} else if ((key == SR_CONF_DEVICE_OPTIONS) && !sdi) {
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
			drvopts, ARRAY_SIZE(drvopts), sizeof(uint32_t));
		return SR_OK;
	} else if ((key == SR_CONF_DEVICE_OPTIONS) && !cg) {
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
			devopts, ARRAY_SIZE(devopts), sizeof(uint32_t));
		return SR_OK;
	}

	/* From here on, we're only concerned with channel group config. */
	if (!cg)
		return SR_ERR_NA;

	/*
	 * TODO: Implement channel group configuration when adding support for
	 * plug-in cards.
	 */

	ret = SR_OK;
	switch (key) {
	default:
		ret = SR_ERR_NA;
	}

	return ret;
}

static void create_channel_index_list(GSList *channels, GArray **arr)
{
	struct sr_channel *channel;
	struct channel_context *chanc;
	GSList *list_elem;

	*arr = g_array_new(FALSE, FALSE, sizeof(unsigned int));

	for (list_elem = channels; list_elem; list_elem = list_elem->next) {
		channel = list_elem->data;
		chanc = channel->priv;
		g_array_append_val(*arr, chanc->index);
	}
}

/*
 * TRIG SGL
 *   Trigger the first measurement, then hold. We can't let the instrument
 *   auto-trigger because we read several registers to make a complete
 *   reading. If the instrument were auto-triggering, we could get the
 *   reading for sample N, but a new measurement is made and when we read the
 *   HIRES register, it contains data for sample N+1. This would produce
 *   wrong readings.
 * SADV AUTO
 *   Activate the scan-advance feature. This automatically connects the next
 *   channel in the scan list to the A/D converter. This way, we do not need to
 *   occupy the HP-IB bus to send channel select commands.
 */
static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	int ret;
	gboolean front_selected, rear_selected;
	struct sr_scpi_dev_inst *scpi;
	struct sr_channel *channel;
	struct dev_context *devc;
	struct channel_context *chanc;
	GArray *ch_list;
	GSList *channels;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	scpi = sdi->conn;
	devc = sdi->priv;

	ret = sr_scpi_source_add(sdi->session, scpi, G_IO_IN, 100,
				 hp_3457a_receive_data, (void *)sdi);
	if (ret != SR_OK)
		return ret;

	std_session_send_df_header(sdi, LOG_PREFIX);

	front_selected = rear_selected = FALSE;
	devc->active_channels = NULL;

	for (channels = sdi->channels; channels; channels = channels->next) {
		channel = channels->data;

		if (!channel->enabled)
			continue;

		chanc = channel->priv;

		if (chanc->location == CONN_FRONT)
			front_selected = TRUE;
		if (chanc->location == CONN_REAR)
			rear_selected = TRUE;

		devc->active_channels = g_slist_append(devc->active_channels, channel);
	}

	if (front_selected && rear_selected) {
		sr_err("Can not use front and rear channels at the same time!");
		g_slist_free(devc->active_channels);
		return SR_ERR_ARG;
	}

	devc->current_channel = devc->active_channels->data;
	devc->num_active_channels = g_slist_length(devc->active_channels);

	hp_3457a_select_input(sdi, front_selected ? CONN_FRONT : CONN_REAR);

	/* For plug-in cards, use the scan-advance features to scan channels. */
	if (rear_selected && (devc->rear_card->card_id != REAR_TERMINALS)) {
		create_channel_index_list(devc->active_channels, &ch_list);
		hp_3457a_send_scan_list(sdi, (void *)ch_list->data, ch_list->len);
		sr_scpi_send(scpi, "SADV AUTO");
		g_array_free(ch_list, TRUE);
	}

	/* Start first measurement. */
	sr_scpi_send(scpi, "TRIG SGL");
	devc->acq_state = ACQ_TRIGGERED_MEASUREMENT;
	devc->num_samples = 0;

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = sdi->priv;

	g_slist_free(devc->active_channels);

	return SR_OK;
}

static struct sr_dev_driver hp_3457a_driver_info = {
	.name = "hp-3457a",
	.longname = "HP 3457A",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(hp_3457a_driver_info);
