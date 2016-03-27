/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2015 Daniel Glöckner <daniel-gl@gmx.net>
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
#include <ieee1284.h>
#include <string.h>
#include "protocol.h"

SR_PRIV struct sr_dev_driver hung_chang_dso_2100_driver_info;

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t drvopts[] = {
	SR_CONF_OSCILLOSCOPE,
};

static const uint32_t devopts[] = {
	SR_CONF_CONN | SR_CONF_GET,
	SR_CONF_LIMIT_FRAMES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_SOURCE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_SLOPE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_BUFFERSIZE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const uint32_t cgopts[] = {
	SR_CONF_VDIV | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_COUPLING | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_PROBE_FACTOR | SR_CONF_GET | SR_CONF_SET,
};

static const uint64_t samplerates[] = {
	SR_MHZ(100), SR_MHZ(50),  SR_MHZ(25),   SR_MHZ(20),
	SR_MHZ(10),  SR_MHZ(5),   SR_KHZ(2500), SR_MHZ(2),
	SR_MHZ(1),   SR_KHZ(500), SR_KHZ(250),  SR_KHZ(200),
	SR_KHZ(100), SR_KHZ(50),  SR_KHZ(25),   SR_KHZ(20),
	SR_KHZ(10),  SR_KHZ(5),   SR_HZ(2500),  SR_KHZ(2),
	SR_KHZ(1),   SR_HZ(500),  SR_HZ(250),   SR_HZ(200),
	SR_HZ(100),  SR_HZ(50),   SR_HZ(25),    SR_HZ(20)
};

/* must be in sync with readout_steps[] in protocol.c */
static const uint64_t buffersizes[] = {
	2 * 500, 3 * 500, 4 * 500, 5 * 500,
	6 * 500, 7 * 500, 8 * 500, 9 * 500, 10 * 500,
	12 * 500 - 2, 14 * 500 - 2, 16 * 500 - 2,
	18 * 500 - 2, 20 * 500 - 2, 10240 - 2
};

static const uint64_t vdivs[][2] = {
	{ 10, 1000 },
	{ 20, 1000 },
	{ 50, 1000 },
	{ 100, 1000 },
	{ 200, 1000 },
	{ 500, 1000 },
	{ 1, 1 },
	{ 2, 1 },
	{ 5, 1 },
};

/* Bits 4 and 5 enable relays that add /10 filters to the chain
 * Bits 0 and 1 select an output from a resistor array */
static const uint8_t vdivs_map[] = {
	0x01, 0x02, 0x03, 0x21, 0x22, 0x23, 0x31, 0x32, 0x33
};


static const char *trigger_sources[] = {
	"A", "B", "EXT"
};

static const uint8_t trigger_sources_map[] = {
	0x00, 0x80, 0x40
};

static const char *trigger_slopes[] = {
	"f", "r"
};

static const char *coupling[] = {
	"DC", "AC", "GND"
};

static const uint8_t coupling_map[] = {
	0x00, 0x08, 0x04
};

static int init(struct sr_dev_driver *di, struct sr_context *sr_ctx)
{
	return std_init(sr_ctx, di, LOG_PREFIX);
}

static GSList *scan_port(GSList *devices, struct sr_dev_driver *di,
			 struct parport *port)
{
	struct sr_dev_inst *sdi;
	struct sr_channel *ch;
	struct sr_channel_group *cg;
	struct dev_context *devc;
	struct drv_context *drvc;
	int i;

	if (ieee1284_open(port, 0, &i) != E1284_OK) {
		sr_err("Can't open parallel port %s", port->name);
		goto fail1;
	}

	if ((i & (CAP1284_RAW | CAP1284_BYTE)) != (CAP1284_RAW | CAP1284_BYTE)) {
		sr_err("Parallel port %s does not provide low-level bidirection access",
		       port->name);
		goto fail2;
	}

	if (ieee1284_claim(port) != E1284_OK) {
		sr_err("Parallel port %s already in use", port->name);
		goto fail2;
	}

	if (!hung_chang_dso_2100_check_id(port))
		goto fail3;

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup("Hung-Chang");
	sdi->model = g_strdup("DSO-2100");
	sdi->driver = di;
	drvc = di->context;
	sdi->inst_type = 0; /* FIXME */
	sdi->conn = port;
	ieee1284_ref(port);

	for (i = 0; i < NUM_CHANNELS; i++) {
		cg = g_malloc0(sizeof(struct sr_channel_group));
		cg->name = g_strdup(trigger_sources[i]);
		ch = sr_channel_new(sdi, i, SR_CHANNEL_ANALOG, FALSE, trigger_sources[i]);
		cg->channels = g_slist_append(cg->channels, ch);
		sdi->channel_groups = g_slist_append(sdi->channel_groups, cg);
	}

	devc = g_malloc0(sizeof(struct dev_context));
	devc->enabled_channel = g_slist_append(NULL, NULL);
	devc->channel = 0;
	devc->rate = 0;
	devc->probe[0] = 10;
	devc->probe[1] = 10;
	devc->cctl[0] = 0x31; /* 1V/div, DC coupling, trigger on channel A*/
	devc->cctl[1] = 0x31; /* 1V/div, DC coupling, no tv sync trigger */
	devc->edge = 0;
	devc->tlevel = 0x80;
	devc->pos[0] = 0x80;
	devc->pos[1] = 0x80;
	devc->offset[0] = 0x80;
	devc->offset[1] = 0x80;
	devc->gain[0] = 0x80;
	devc->gain[1] = 0x80;
	devc->frame_limit = 0;
	devc->last_step = 0; /* buffersize = 1000 */
	sdi->priv = devc;

	drvc->instances = g_slist_append(drvc->instances, sdi);
	devices = g_slist_append(devices, sdi);

fail3:
	ieee1284_release(port);
fail2:
	ieee1284_close(port);
fail1:
	return devices;
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct parport_list ports;
	struct sr_config *src;
	const char *conn = NULL;
	GSList *devices, *option;
	gboolean port_found;
	int i;


	for (option = options; option; option = option->next) {
		src = option->data;
		if (src->key == SR_CONF_CONN) {
			conn = g_variant_get_string(src->data, NULL);
			break;
		}
	}

	if (!conn)
		return NULL;

	if (ieee1284_find_ports(&ports, 0) != E1284_OK)
		return NULL;

	devices = NULL;
	port_found = FALSE;
	for (i = 0; i < ports.portc; i++)
		if (!strcmp(ports.portv[i]->name, conn)) {
			port_found = TRUE;
			devices = scan_port(devices, di, ports.portv[i]);
		}

	if (!port_found) {
		sr_err("Parallel port %s not found. Valid names are:", conn);
		for (i = 0; i < ports.portc; i++)
			sr_err("\t%s", ports.portv[i]->name);
	}

	ieee1284_free_ports(&ports);

	return devices;
}

static GSList *dev_list(const struct sr_dev_driver *di)
{
	return ((struct drv_context *)(di->context))->instances;
}

static void clear_private(void *priv)
{
	struct dev_context *devc = priv;

	g_slist_free(devc->enabled_channel);
}

static int dev_clear(const struct sr_dev_driver *di)
{
	struct drv_context *drvc = di->context;
	struct sr_dev_inst *sdi;
	GSList *l;

	if (drvc) {
		for (l = drvc->instances; l; l = l->next) {
			sdi = l->data;
			ieee1284_unref(sdi->conn);
		}
	}

	return std_dev_clear(di, clear_private);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	int i;

	if (sdi->status != SR_ST_INACTIVE)
		goto fail1;

	if (ieee1284_open(sdi->conn, 0, &i) != E1284_OK)
		goto fail1;

	if (ieee1284_claim(sdi->conn) != E1284_OK)
		goto fail2;

	if (ieee1284_data_dir(sdi->conn, 1) != E1284_OK)
		goto fail3;

	if (hung_chang_dso_2100_move_to(sdi, 1))
		goto fail3;

	devc->samples = g_try_malloc(1000 * sizeof(*devc->samples));
	if (!devc->samples)
		goto fail3;

	sdi->status = SR_ST_ACTIVE;

	return SR_OK;

fail3:
	hung_chang_dso_2100_reset_port(sdi->conn);
	ieee1284_release(sdi->conn);
fail2:
	ieee1284_close(sdi->conn);
fail1:
	return SR_ERR;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_OK;

	g_free(devc->samples);
	hung_chang_dso_2100_reset_port(sdi->conn);
	ieee1284_release(sdi->conn);
	ieee1284_close(sdi->conn);
	sdi->status = SR_ST_INACTIVE;

	return SR_OK;
}

static int cleanup(const struct sr_dev_driver *di)
{
	struct drv_context *drvc = di->context;
	int ret;

	ret = dev_clear(di);

	g_free(drvc);

	return ret;
}

static int find_in_array(GVariant *data, const GVariantType *type,
			 const void *arr, int n)
{
	const char * const *sarr;
	const char *s;
	const uint64_t *u64arr;
	const uint8_t *u8arr;
	uint64_t u64;
	uint8_t u8;
	int i;

	if (!g_variant_is_of_type(data, type))
		return -1;

	switch (g_variant_classify(data)) {
	case G_VARIANT_CLASS_STRING:
		s = g_variant_get_string(data, NULL);
		sarr = arr;

		for (i = 0; i < n; i++)
			if (!strcmp(s, sarr[i]))
				return i;
		break;
	case G_VARIANT_CLASS_UINT64:
		u64 = g_variant_get_uint64(data);
		u64arr = arr;

		for (i = 0; i < n; i++)
			if (u64 == u64arr[i])
				return i;
		break;
	case G_VARIANT_CLASS_BYTE:
		u8 = g_variant_get_byte(data);
		u8arr = arr;

		for (i = 0; i < n; i++)
			if (u8 == u8arr[i])
				return i;
	default:
		break;
	}

	return -1;
}

static int reverse_map(uint8_t u, const uint8_t *arr, int n)
{
	GVariant *v = g_variant_new_byte(u);
	int i = find_in_array(v, G_VARIANT_TYPE_BYTE, arr, n);
	g_variant_unref(v);
	return i;
}

static int config_get(uint32_t key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc = sdi->priv;
	struct parport *port;
	int ret, i, ch = -1;

	if (cg) /* sr_config_get will validate cg using config_list */
		ch = ((struct sr_channel *)cg->channels->data)->index;

	ret = SR_OK;
	switch (key) {
	case SR_CONF_CONN:
		port = sdi->conn;
		*data = g_variant_new_string(port->name);
		break;
	case SR_CONF_LIMIT_FRAMES:
		*data = g_variant_new_uint64(devc->frame_limit);
		break;
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(samplerates[devc->rate]);
		break;
	case SR_CONF_TRIGGER_SOURCE:
		i = reverse_map(devc->cctl[0] & 0xC0, trigger_sources_map,
				ARRAY_SIZE(trigger_sources_map));
		if (i == -1)
			ret = SR_ERR;
		else
			*data = g_variant_new_string(trigger_sources[i]);
		break;
	case SR_CONF_TRIGGER_SLOPE:
		if (devc->edge >= ARRAY_SIZE(trigger_slopes))
			ret = SR_ERR;
		else
			*data = g_variant_new_string(trigger_slopes[devc->edge]);
		break;
	case SR_CONF_BUFFERSIZE:
		*data = g_variant_new_uint64(buffersizes[devc->last_step]);
		break;
	case SR_CONF_VDIV:
		if (ch == -1) {
			ret = SR_ERR_CHANNEL_GROUP;
		} else {
			i = reverse_map(devc->cctl[ch] & 0x33, vdivs_map,
					ARRAY_SIZE(vdivs_map));
			if (i == -1)
				ret = SR_ERR;
			else
				*data = g_variant_new("(tt)", vdivs[i][0],
						      vdivs[i][1]);
		}
		break;
	case SR_CONF_COUPLING:
		if (ch == -1) {
			ret = SR_ERR_CHANNEL_GROUP;
		} else {
			i = reverse_map(devc->cctl[ch] & 0x0C, coupling_map,
					ARRAY_SIZE(coupling_map));
			if (i == -1)
				ret = SR_ERR;
			else
				*data = g_variant_new_string(coupling[i]);
		}
		break;
	case SR_CONF_PROBE_FACTOR:
		if (ch == -1)
			ret = SR_ERR_CHANNEL_GROUP;
		else
			*data = g_variant_new_uint64(devc->probe[ch]);
		break;
	default:
		ret = SR_ERR_NA;
	}

	return ret;
}

static int config_set(uint32_t key, GVariant *data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc = sdi->priv;
	int ret, i, ch = -1;
	uint64_t u, v;

	if (cg) /* sr_config_set will validate cg using config_list */
		ch = ((struct sr_channel *)cg->channels->data)->index;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	ret = SR_OK;
	switch (key) {
	case SR_CONF_LIMIT_FRAMES:
		devc->frame_limit = g_variant_get_uint64(data);
		break;
	case SR_CONF_SAMPLERATE:
		i = find_in_array(data, G_VARIANT_TYPE_UINT64,
				  samplerates, ARRAY_SIZE(samplerates));
		if (i == -1)
			ret = SR_ERR_ARG;
		else
			devc->rate = i;
		break;
	case SR_CONF_TRIGGER_SOURCE:
		i = find_in_array(data, G_VARIANT_TYPE_STRING,
				  trigger_sources, ARRAY_SIZE(trigger_sources));
		if (i == -1)
			ret = SR_ERR_ARG;
		else
			devc->cctl[0] = (devc->cctl[0] & 0x3F)
				      | trigger_sources_map[i];
		break;
	case SR_CONF_TRIGGER_SLOPE:
		i = find_in_array(data, G_VARIANT_TYPE_STRING,
				  trigger_slopes, ARRAY_SIZE(trigger_slopes));
		if (i == -1)
			ret = SR_ERR_ARG;
		else
			devc->edge = i;
		break;
	case SR_CONF_BUFFERSIZE:
		i = find_in_array(data, G_VARIANT_TYPE_UINT64,
				  buffersizes, ARRAY_SIZE(buffersizes));
		if (i == -1)
			ret = SR_ERR_ARG;
		else
			devc->last_step = i;
		break;
	case SR_CONF_VDIV:
		if (ch == -1) {
			ret = SR_ERR_CHANNEL_GROUP;
		} else if (!g_variant_is_of_type(data, G_VARIANT_TYPE("(tt)"))) {
			ret = SR_ERR_ARG;
		} else {
			g_variant_get(data, "(tt)", &u, &v);
			for (i = 0; i < (int)ARRAY_SIZE(vdivs); i++)
				if (vdivs[i][0] == u && vdivs[i][1] == v)
					break;
			if (i == ARRAY_SIZE(vdivs))
				ret = SR_ERR_ARG;
			else
				devc->cctl[ch] = (devc->cctl[ch] & 0xCC)
					       | vdivs_map[i];
		}
		break;
	case SR_CONF_COUPLING:
		if (ch == -1) {
			ret = SR_ERR_CHANNEL_GROUP;
		} else {
			i = find_in_array(data, G_VARIANT_TYPE_STRING,
					  coupling, ARRAY_SIZE(coupling));
			if (i == -1)
				ret = SR_ERR_ARG;
			else
				devc->cctl[ch] = (devc->cctl[ch] & 0xF3)
					       | coupling_map[i];
		}
		break;
	case SR_CONF_PROBE_FACTOR:
		if (ch == -1) {
			ret = SR_ERR_CHANNEL_GROUP;
		} else {
			u = g_variant_get_uint64(data);
			if (!u)
				ret = SR_ERR_ARG;
			else
				devc->probe[ch] = u;
		}
		break;
	default:
		ret = SR_ERR_NA;
	}

	return ret;
}

static int config_channel_set(const struct sr_dev_inst *sdi,
			      struct sr_channel *ch,
			      unsigned int changes)
{
	struct dev_context *devc = sdi->priv;
	uint8_t v;

	if (changes & SR_CHANNEL_SET_ENABLED) {
		if (ch->enabled) {
			v = devc->channel | (1 << ch->index);
			if (v & (v - 1))
				return SR_ERR;
			devc->channel = v;
			devc->enabled_channel->data = ch;
		} else {
			devc->channel &= ~(1 << ch->index);
		}
	}
	return SR_OK;
}

static int config_commit(const struct sr_dev_inst *sdi)
{
	uint8_t state = hung_chang_dso_2100_read_mbox(sdi->conn, 0.02);
	int ret;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	switch (state) {
	case 0x03:
	case 0x14:
	case 0x21:
		/* we will travel the complete config path on our way to state 1 */
		break;
	case 0x00:
		state = 0x01;
	default:
		ret = hung_chang_dso_2100_move_to(sdi, 1);
		if (ret != SR_OK)
			return ret;
	case 0x01:
		hung_chang_dso_2100_write_mbox(sdi->conn, 4);
	}
	ret = hung_chang_dso_2100_move_to(sdi, 1);
	if (ret != SR_OK)
		return ret;
	return hung_chang_dso_2100_move_to(sdi, state);
}

static int config_list(uint32_t key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	GVariantBuilder gvb;
	GVariant *gvar, *rational[2];
	GSList *l;
	int i;

	switch (key) {
		case SR_CONF_SCAN_OPTIONS:
	case SR_CONF_DEVICE_OPTIONS:
		break;
	case SR_CONF_SAMPLERATE:
	case SR_CONF_TRIGGER_SOURCE:
	case SR_CONF_TRIGGER_SLOPE:
	case SR_CONF_BUFFERSIZE:
		if (!sdi || cg)
			return SR_ERR_NA;
		break;
	case SR_CONF_VDIV:
	case SR_CONF_COUPLING:
		if (!sdi)
			return SR_ERR_NA;
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		l = g_slist_find(sdi->channel_groups, cg);
		if (!l)
			return SR_ERR_ARG;
		break;
	default:
		return SR_ERR_NA;
	}

	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				scanopts, ARRAY_SIZE(scanopts), sizeof(uint32_t));
		break;
	case SR_CONF_DEVICE_OPTIONS:
		if (!sdi)
			*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
					drvopts, ARRAY_SIZE(drvopts), sizeof(uint32_t));
		else if (!cg)
			*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
					devopts, ARRAY_SIZE(devopts), sizeof(uint32_t));
		else
			*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
					cgopts, ARRAY_SIZE(cgopts), sizeof(uint32_t));
		break;
	case SR_CONF_SAMPLERATE:
		g_variant_builder_init(&gvb, G_VARIANT_TYPE("a{sv}"));
		gvar = g_variant_new_fixed_array(G_VARIANT_TYPE("t"),
				samplerates, ARRAY_SIZE(samplerates), sizeof(uint64_t));
		g_variant_builder_add(&gvb, "{sv}", "samplerates", gvar);
		*data = g_variant_builder_end(&gvb);
		break;
	case SR_CONF_TRIGGER_SOURCE:
		*data = g_variant_new_strv(trigger_sources, ARRAY_SIZE(trigger_sources));
		break;
	case SR_CONF_TRIGGER_SLOPE:
		*data = g_variant_new_strv(trigger_slopes, ARRAY_SIZE(trigger_slopes));
		break;
	case SR_CONF_BUFFERSIZE:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT64,
				buffersizes, ARRAY_SIZE(buffersizes), sizeof(uint64_t));
		break;
	case SR_CONF_VDIV:
		g_variant_builder_init(&gvb, G_VARIANT_TYPE_ARRAY);
		for (i = 0; i < (int)ARRAY_SIZE(vdivs); i++) {
			rational[0] = g_variant_new_uint64(vdivs[i][0]);
			rational[1] = g_variant_new_uint64(vdivs[i][1]);
			gvar = g_variant_new_tuple(rational, 2);
			g_variant_builder_add_value(&gvb, gvar);
		}
		*data = g_variant_builder_end(&gvb);
		break;
	case SR_CONF_COUPLING:
		*data = g_variant_new_strv(coupling, ARRAY_SIZE(coupling));
		break;
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi, void *cb_data)
{
	struct dev_context *devc = sdi->priv;
	int ret;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	if (devc->channel) {
		static const float res_array[] = {0.5, 1, 2, 5};
		static const uint8_t relays[] = {100, 10, 10, 1};
		devc->factor = devc->probe[devc->channel - 1] / 32.0;
		devc->factor *= res_array[devc->cctl[devc->channel - 1] & 0x03];
		devc->factor /= relays[(devc->cctl[devc->channel - 1] >> 4) & 0x03];
	}
	devc->frame = 0;
	devc->cb_data = cb_data;
	devc->state_known = TRUE;
	devc->step = 0;
	devc->adc2 = FALSE;
	devc->retries = MAX_RETRIES;

	ret = hung_chang_dso_2100_move_to(sdi, 0x21);
	if (ret != SR_OK)
		return ret;

	std_session_send_df_header(cb_data, LOG_PREFIX);

	sr_session_source_add(sdi->session, -1, 0, 8,
			      hung_chang_dso_2100_poll, (void *)sdi);

	return SR_OK;
}

SR_PRIV int hung_chang_dso_2100_dev_acquisition_stop(const struct sr_dev_inst *sdi,
		void *cb_data)
{
	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	std_session_send_df_end(cb_data, LOG_PREFIX);
	sr_session_source_remove(sdi->session, -1);
	hung_chang_dso_2100_move_to(sdi, 1);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{
	return hung_chang_dso_2100_dev_acquisition_stop(sdi, cb_data);
}

SR_PRIV struct sr_dev_driver hung_chang_dso_2100_driver_info = {
	.name = "hung-chang-dso-2100",
	.longname = "Hung-Chang DSO-2100",
	.api_version = 1,
	.init = init,
	.cleanup = cleanup,
	.scan = scan,
	.dev_list = dev_list,
	.dev_clear = dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_channel_set = config_channel_set,
	.config_commit = config_commit,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};
