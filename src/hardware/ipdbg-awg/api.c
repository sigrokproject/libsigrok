/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2021 Daniel Anselmi <danselmi@gmx.ch>
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

/** since the ipdbg-awg is used with different analog front-ends we use
  * the following mapping for the parameters:
  *
  * SR_CONF_AMPLITUDE: the value 1.0 maps to the maximum
  * presentable value with the given word width of the synthesized awg.
  * The generated values are in two's complement.
  *
  * SR_CONF_OFFSET: -1.0 .. 1.0 is mapped over the whole range of the
  * given word width. This matches with the amplitude parameter where
  * a peak-peak value is configured.
  *
  * SR_CONF_SAMPLERATE: must be configured to the sample-rate of the
  * synthesized awg. This is used to be able to get the right
  * SR_CONF_OUTPUT_FREQUENCY frequency calculations.
  *
  * SR_CONF_CENTER_FREQUENCY: for the calculation of the waveform it is
  * assumed that DC will be mixed to this configured value.
  *
  * SR_CONF_DUTY_CYCLE is used for the following patterns:
  * Square: the ratio of pulse-duration to the period in %
  * Triangle: the ratio of the rise time t_r to the half of
  * the period t_h.
  *
  * a ^
  *   | t_r
  *   |<-->|          0% -> falling ramp
  *   |    /\        50% -> symmetric triangle
  *   |   /  \      100% -> rising ramp
  *   |  /    \
  *   | /      \
  *   +/--------\----------> t
  *   |         |\      /
  *   |<------->| \    /
  *   |     t_h    \  /
  *   |             \/
  */

/**
  * open points:
  * 1) The ipdbg-awg is sometimes used to generate complex valued signals.
  * In this case the output of the I and Q sample is either parallel or
  * time-multiplexed.
  * At the moment there is no SR_CONF_xyz parameter to select one of these.
  * ("off", "parallel", "time-multiplex")
  *
  *
  * 2) Are there any plans for an interface to feed the generators
  * in ARB mode with data? For example from one of the many source file
  * formats already supported by sigrok? or simply by giving a number
  * array?
  */

#include <config.h>
#include "protocol.h"

static struct sr_dev_driver ipdbg_awg_driver_info;

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t drvopts[] = {
	SR_CONF_SIGNAL_GENERATOR,
};

static const uint32_t devopts[] = {
	SR_CONF_ENABLED          | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_CENTER_FREQUENCY | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_OUTPUT_FREQUENCY | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_AMPLITUDE        | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_SAMPLERATE       | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_PHASE            | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_OFFSET           | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_DUTY_CYCLE       | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_PATTERN_MODE     | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static void ipdbg_awg_split_addr_port(const char *conn, char **addr,
	char **port)
{
	char **strs = g_strsplit(conn, "/", 3);

	*addr = g_strdup(strs[1]);
	*port = g_strdup(strs[2]);

	g_strfreev(strs);
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct drv_context *drvc;
	GSList *devices;

	devices = NULL;
	drvc = di->context;
	drvc->instances = NULL;
	const char *conn;
	struct sr_config *src;
	GSList *l;

	conn = NULL;
	for (l = options; l; l = l->next) {
		src = l->data;
		switch (src->key) {
		case SR_CONF_CONN:
			conn = g_variant_get_string(src->data, NULL);
			break;
		}
	}

	if (!conn)
		return NULL;

	struct ipdbg_awg_tcp *tcp = ipdbg_awg_tcp_new();

	ipdbg_awg_split_addr_port(conn, &tcp->address, &tcp->port);

	if (!tcp->address)
		return NULL;

	if (ipdbg_awg_tcp_open(tcp) != SR_OK)
		return NULL;

	ipdbg_awg_send_reset(tcp);
	ipdbg_awg_send_reset(tcp);

//	if (ipdbg_awg_request_id(tcp) != SR_OK)
//		return NULL;

	struct sr_dev_inst *sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup("ipdbg.org");
	sdi->model = g_strdup("IPDBG AWG");
	sdi->version = g_strdup("v1.0");
	sdi->driver = di;

	struct dev_context *devc = g_malloc0(sizeof(struct dev_context));
	devc->is_running = FALSE;
	devc->wave_buffer = NULL;
	devc->sample_rate = SR_KHZ(92160);
	devc->center_freq = 0;
	sdi->priv = devc;
	devc->waveform = IPDBG_AWG_WAVEFORM_SINE;
	devc->amplitude = 1.0;
	devc->frequency = 0.5;
	devc->phase = 0.0;
	devc->offset = 0.0;
	devc->dutycycle = 0.5;
	devc->periods = 1;
	devc->complex_part_parallel = FALSE;

	ipdbg_awg_get_addrwidth_and_datawidth(tcp, devc);

	ipdbg_awg_get_isrunning(tcp, devc);

	ipdbg_awg_init_waveform(devc);

	sr_dbg("addr_width = %d, data_width = %d\n", devc->addr_width,
		devc->data_width);

	sdi->inst_type = SR_INST_USER;
	sdi->conn = tcp;

	ipdbg_awg_tcp_close(tcp);

	/* workaround: opening the device again just after
	   closing it sometimes failed. Specially with JtagHostSim*/
	g_usleep(500000);

	devices = g_slist_append(devices, sdi);

	return std_scan_complete(di, devices);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct ipdbg_awg_tcp *tcp = sdi->conn;

	if (!tcp)
		return SR_ERR;

	if (ipdbg_awg_tcp_open(tcp) != SR_OK)
		return SR_ERR;

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	/* Should be called before a new call to scan(). */
	struct ipdbg_awg_tcp *tcp;

	tcp = sdi->conn;
	if (tcp)
		ipdbg_awg_tcp_close(tcp);

	sdi->conn = NULL;
	if (!(devc = sdi->priv))
		return SR_ERR;

	if (devc->wave_buffer)
		g_free(devc->wave_buffer);
	devc->wave_buffer = NULL;

	return SR_OK;
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	(void)cg;

	if (!sdi || !(devc = sdi->priv))
		return SR_ERR_ARG;

	switch (key) {
	case SR_CONF_CENTER_FREQUENCY:
		*data = g_variant_new_uint64(ipdbg_awg_get_center_freq(devc));
		break;
	case SR_CONF_OUTPUT_FREQUENCY:
		*data = g_variant_new_double(ipdbg_awg_get_frequency(devc));
		break;
	case SR_CONF_AMPLITUDE:
		*data = g_variant_new_double(ipdbg_awg_get_amplitude(devc));
		break;
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(ipdbg_awg_get_sample_rate(devc));
		break;
	case SR_CONF_ENABLED:
		*data = g_variant_new_boolean(devc->is_running);
		break;
	case SR_CONF_PHASE:
		*data = g_variant_new_double(ipdbg_awg_get_phase(devc));
		break;
	case SR_CONF_OFFSET:
		*data = g_variant_new_double(ipdbg_awg_get_offset(devc));
		break;
	case SR_CONF_DUTY_CYCLE:
		*data = g_variant_new_double(ipdbg_awg_get_dutycycle(devc));
		break;
	case SR_CONF_PATTERN_MODE:
		*data = g_variant_new_string(
				ipdbg_awg_waveform_to_string(devc->waveform));
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	(void)cg;
	int ret, i;
	const char *mode, *mode_name;

	if (!sdi)
		return SR_ERR_ARG;

	ret = SR_OK;
	switch (key) {
	case SR_CONF_CENTER_FREQUENCY:
		ret = ipdbg_awg_set_center_freq(sdi, g_variant_get_uint64(data));
		break;
	case SR_CONF_OUTPUT_FREQUENCY:
		ret = ipdbg_awg_set_frequency(sdi, g_variant_get_double(data));
		break;
	case SR_CONF_AMPLITUDE:
		ret = ipdbg_awg_set_amplitude(sdi, g_variant_get_double(data));
		break;
	case SR_CONF_SAMPLERATE:
		ret = ipdbg_awg_set_sample_rate(sdi, g_variant_get_uint64(data));
		break;
	case SR_CONF_ENABLED:
		ret = ipdbg_awg_set_enable(sdi, g_variant_get_boolean(data));
		break;
	case SR_CONF_PHASE:
		ret = ipdbg_awg_set_phase(sdi, g_variant_get_double(data));
		break;
	case SR_CONF_OFFSET:
		ret = ipdbg_awg_set_offset(sdi, g_variant_get_double(data));
		break;
	case SR_CONF_DUTY_CYCLE:
		ret = ipdbg_awg_set_dutycycle(sdi, g_variant_get_double(data));
		break;
	case SR_CONF_PATTERN_MODE:
		ret = SR_ERR_NA;
		mode = g_variant_get_string(data, NULL);
		for (i = 0; i < IPDBG_AWG_NUM_WAVEFORM_TYPES; ++i) {
			mode_name = ipdbg_awg_waveform_to_string(i);
			if (g_ascii_strncasecmp(mode, mode_name, strlen(mode_name)) == 0)
				ret = ipdbg_awg_set_waveform(sdi, i);
		}
		break;
	default:
		return SR_ERR_NA;
	}

	return ret;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	GVariantBuilder *b;
	int i;
	double spec[3];
	struct dev_context *devc;
	static const double phase_min_max_step[] = { 0.0, 360.0, 0.001 };


	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
	case SR_CONF_DEVICE_OPTIONS:
		return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
	case SR_CONF_OUTPUT_FREQUENCY:
		devc = sdi->priv;
		spec[0] = (double)devc->sample_rate / (double)devc->limit_samples_max;
		spec[1] = (double)devc->sample_rate / 2.0;
		spec[2] = spec[0]/100.0;
		*data = std_gvar_min_max_step_array(spec);
		break;
	case SR_CONF_AMPLITUDE:
		devc = sdi->priv;
		spec[0] = 0.0;
		spec[1] = 1.0;
		spec[2] = 1.0 / ((double)(1ul << (devc->data_width-1))-1.0);
		if (devc->complex_part_parallel) spec[2] /= 2.0;
		*data = std_gvar_min_max_step_array(spec);
		break;
	case SR_CONF_PHASE:
		*data = std_gvar_min_max_step_array(phase_min_max_step);
		break;
	case SR_CONF_PATTERN_MODE:
		b = g_variant_builder_new(G_VARIANT_TYPE("as"));
		for (i = 0; i < IPDBG_AWG_NUM_WAVEFORM_TYPES; ++i) {
			g_variant_builder_add(b, "s", ipdbg_awg_waveform_to_string(i));
		}
		*data = g_variant_new("as", b);
		g_variant_builder_unref(b);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct ipdbg_awg_tcp *tcp = sdi->conn;

	ipdbg_awg_stop(sdi);
	ipdbg_awg_send_reset(tcp);
	ipdbg_awg_abort_acquisition(sdi);

	return SR_OK;
}

static int dev_clear(const struct sr_dev_driver *di)
{
	struct drv_context *drvc = di->context;
	struct sr_dev_inst *sdi;
	GSList *l;

	if (drvc) {
		for (l = drvc->instances; l; l = l->next) {
			sdi = l->data;
			struct ipdbg_awg_tcp *tcp = sdi->conn;
			if (tcp) {
				ipdbg_awg_tcp_close(tcp);
				ipdbg_awg_tcp_free(tcp);
				g_free(tcp);
			}
			sdi->conn = NULL;
		}
	}

	return std_dev_clear(di);
}

static struct sr_dev_driver ipdbg_awg_driver_info = {
	.name = "ipdbg-awg",
	.longname = "IPDBG AWG",
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
	.dev_acquisition_start = std_dummy_dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};

SR_REGISTER_DEV_DRIVER(ipdbg_awg_driver_info);

