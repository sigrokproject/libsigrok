/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2019-2020 Gerhard Sittig <gerhard.sittig@gmx.net>
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

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const uint32_t drvopts[] = {
	SR_CONF_MULTIMETER,
	SR_CONF_THERMOMETER, /* Supports two temperature probes and diffs. */
};

static const uint32_t devopts[] = {
	SR_CONF_CONN | SR_CONF_GET,
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_FRAMES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_DATA_SOURCE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_DATALOG | SR_CONF_GET | SR_CONF_SET,
	/* TODO SR_CONF_DATALOG is bool only, how to setup interval/duration? */
	SR_CONF_MEASURED_QUANTITY | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_RANGE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const char *channel_names[] = {
	[UT181A_CH_MAIN] = "P1",
	[UT181A_CH_AUX1] = "P2",
	[UT181A_CH_AUX2] = "P3",
	[UT181A_CH_AUX3] = "P4",
	[UT181A_CH_BAR] = "bar",
#if UT181A_WITH_TIMESTAMP
	[UT181A_CH_TIME] = "TS",
#endif
};

/*
 * (Re-)retrieve the list of recordings and their names. These can change
 * without the driver's being aware, the set is under user control.
 *
 * TODO Need to re-allocate the list of recording names when a larger
 * recordings count is seen than previously allocated? This implementation
 * assumes a known maximum number of recordings, the manual is vague on
 * these limits.
 */
static int ut181a_update_recordings(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	size_t rec_count, rec_idx;
	int ret;

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;
	serial = sdi->conn;

	ret = ut181a_send_cmd_get_recs_count(serial);
	if (ret < 0)
		return ret;
	ret = ut181a_configure_waitfor(devc, FALSE, 0, 0,
		FALSE, TRUE, FALSE, FALSE);
	if (ret < 0)
		return ret;
	ret = ut181a_waitfor_response(sdi, 100);
	if (ret < 0)
		return ret;

	rec_count = devc->wait_state.data_value;
	if (rec_count > ARRAY_SIZE(devc->record_names))
		rec_count = ARRAY_SIZE(devc->record_names);
	for (rec_idx = 0; rec_idx < rec_count; rec_idx++) {
		devc->info.rec_info.rec_idx = rec_idx;
		ret = ut181a_send_cmd_get_rec_info(serial, rec_idx);
		if (ret < 0)
			return ret;
		ret = ut181a_configure_waitfor(devc,
			FALSE, CMD_CODE_GET_REC_INFO, 0,
			FALSE, FALSE, FALSE, FALSE);
		if (ret < 0)
			return ret;
		ret = ut181a_waitfor_response(sdi, 100);
		if (ret < 0)
			return ret;
	}
	devc->record_count = rec_count;
	devc->data_source_count = DATA_SOURCE_REC_FIRST + devc->record_count;

	return SR_OK;
}

/*
 * Retrieve the device's current state. Run monitor mode for some time
 * until the 'mode' (meter's current function) became available. There
 * is no other way of querying the meter's current state.
 */
static int ut181a_query_initial_state(struct sr_dev_inst *sdi, int timeout_ms)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	gint64 deadline;
	int ret;

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;
	serial = sdi->conn;

	devc->info.meas_head.mode = 0;
	ret = ut181a_send_cmd_monitor(serial, TRUE);
	if (ret < 0)
		return ret;
	ret = ut181a_configure_waitfor(devc, FALSE, 0, 0,
		TRUE, FALSE, FALSE, FALSE);
	if (ret < 0)
		return ret;
	deadline = g_get_monotonic_time();
	deadline += timeout_ms * 1000;
	while (1) {
		ret = ut181a_waitfor_response(sdi, 100);
		if (ret < 0)
			return ret;
		if (devc->info.meas_head.mode)
			break;
		if (g_get_monotonic_time() >= deadline)
			return SR_ERR_DATA;
	}
	(void)ut181a_send_cmd_monitor(serial, FALSE);
	ret = ut181a_configure_waitfor(devc, TRUE, 0, 0,
		FALSE, FALSE, FALSE, FALSE);
	if (ret < 0)
		return ret;
	(void)ut181a_waitfor_response(sdi, 100);

	return SR_OK;
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	const char *conn, *serialcomm;
	struct sr_config *src;
	GSList *l, *devices;
	struct sr_serial_dev_inst *serial;
	int ret;
	char conn_id[64];
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	size_t idx, ds_idx;

	/*
	 * Implementor's note:
	 * Do _not_ add a default conn value here. Always expect users to
	 * specify the connection. Never match in the absence of a user spec.
	 *
	 * Motivation: There is no way to identify the DMM itself. Neither
	 * are the cable nor its chip unique to the device. They are not even
	 * specific to the series or the vendor. The DMM ships with a generic
	 * CP2110 USB-to-UART bridge. Attempts to auto probe will disturb
	 * other types of devices which may be attached to the probed conn.
	 *
	 * On the other hand it's perfectly fine to communicate to the
	 * device and assume that the device model will accept the requests,
	 * once the user specified the connection (and the driver), and thus
	 * instructed this driver to start such activity.
	 */
	conn = NULL;
	serialcomm = "9600/8n1";
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

	devices = NULL;
	serial = sr_serial_dev_inst_new(conn, serialcomm);
	ret = serial_open(serial, SERIAL_RDWR);
	snprintf(conn_id, sizeof(conn_id), "%s", serial->port);
	serial_flush(serial);
	/*
	 * We cannot identify the device at this point in time.
	 * Successful open shall suffice for now. More activity
	 * will communicate to the device later, after the driver
	 * instance got created. See below for details.
	 */
	if (ret != SR_OK) {
		serial_close(serial);
		sr_serial_dev_inst_free(serial);
		return devices;
	}

	sdi = g_malloc0(sizeof(*sdi));
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup("UNI-T");
	sdi->model = g_strdup("UT181A");
	sdi->inst_type = SR_INST_SERIAL;
	sdi->conn = serial;
	sdi->connection_id = g_strdup(conn_id);
	devc = g_malloc0(sizeof(*devc));
	sdi->priv = devc;
	sr_sw_limits_init(&devc->limits);
	for (idx = 0; idx < ARRAY_SIZE(channel_names); idx++) {
		sr_channel_new(sdi, idx, SR_CHANNEL_ANALOG, TRUE,
			channel_names[idx]);
	}

	/*
	 * Run monitor mode for a while to determine the current state
	 * of the device (which cannot get queried by other means). This
	 * also deals with devices which happen to already be in monitor
	 * mode when we connect to them. As a byproduct this query drains
	 * potentially pending RX data, before getting recording details.
	 */
	devc->disable_feed = 1;
	ret = ut181a_query_initial_state(sdi, 2000);
	if (ret < 0) {
		serial_close(serial);
		sr_serial_dev_inst_free(serial);
		return devices;
	}

	/*
	 * Number of recordings and their names are dynamic and under
	 * the user's control. Prepare for a maximum number of string
	 * labels, and fetch (and re-fetch) their names and current
	 * count on demand.
	 */
	devc->data_source_names[DATA_SOURCE_LIVE] = "Live";
	devc->data_source_names[DATA_SOURCE_SAVE] = "Save";
	for (idx = 0; idx < MAX_REC_COUNT; idx++) {
		ds_idx = DATA_SOURCE_REC_FIRST + idx;
		devc->data_source_names[ds_idx] = &devc->record_names[idx][0];
	}
	devc->data_source_count = DATA_SOURCE_REC_FIRST;
	ret = ut181a_update_recordings(sdi);
	devc->data_source_count = DATA_SOURCE_REC_FIRST + devc->record_count;
	if (ret < 0) {
		serial_close(serial);
		sr_serial_dev_inst_free(serial);
		return devices;
	}

	devc->disable_feed = 0;
	serial_close(serial);

	devices = g_slist_append(devices, sdi);

	return std_scan_complete(di, devices);
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	const struct mqopt_item *mqitem;
	GVariant *arr[2];
	const char *range;

	(void)cg;

	devc = sdi->priv;
	switch (key) {
	case SR_CONF_CONN:
		*data = g_variant_new_string(sdi->connection_id);
		break;
	case SR_CONF_LIMIT_FRAMES:
	case SR_CONF_LIMIT_SAMPLES:
	case SR_CONF_LIMIT_MSEC:
		if (!devc)
			return SR_ERR_ARG;
		return sr_sw_limits_config_get(&devc->limits, key, data);
	case SR_CONF_DATA_SOURCE:
		if (!devc)
			return SR_ERR_ARG;
		*data = g_variant_new_string(devc->data_source_names[devc->data_source]);
		break;
	case SR_CONF_DATALOG:
		if (!devc)
			return SR_ERR_ARG;
		*data = g_variant_new_boolean(devc->is_recording ? TRUE : FALSE);
		break;
	case SR_CONF_MEASURED_QUANTITY:
		if (!devc)
			return SR_ERR_ARG;
		mqitem = ut181a_get_mqitem_from_mode(devc->info.meas_head.mode);
		if (!mqitem)
			return SR_ERR_NA;
		arr[0] = g_variant_new_uint32(mqitem->mq);
		arr[1] = g_variant_new_uint64(mqitem->mqflags);
		*data = g_variant_new_tuple(arr, ARRAY_SIZE(arr));
		break;
	case SR_CONF_RANGE:
		if (!devc)
			return SR_ERR_ARG;
		range = ut181a_get_range_from_packet_bytes(devc);
		if (!range || !*range)
			return SR_ERR_NA;
		*data = g_variant_new_string(range);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	ssize_t idx;
	gboolean on;
	GVariant *tuple_child;
	enum sr_mq mq;
	enum sr_mqflag mqflags;
	uint16_t mode;
	int ret;
	size_t rec_no;
	const char *range;

	(void)cg;

	devc = sdi->priv;
	switch (key) {
	case SR_CONF_LIMIT_FRAMES:
	case SR_CONF_LIMIT_SAMPLES:
	case SR_CONF_LIMIT_MSEC:
		if (!devc)
			return SR_ERR_ARG;
		return sr_sw_limits_config_set(&devc->limits, key, data);
	case SR_CONF_DATA_SOURCE:
		if (!devc)
			return SR_ERR_ARG;
		/* Prefer data source names for the lookup. */
		idx = std_str_idx(data, devc->data_source_names, devc->data_source_count);
		if (idx >= 0) {
			devc->data_source = idx;
			break;
		}
		/*
		 * Support record number (1-based) as a fallback. The DMM
		 * "supports" ambiguous recording names (keeps offering a
		 * previously stored name for each new recording, neither
		 * automatically increments nor suggests timestamps).
		 */
		if (sr_atoi(g_variant_get_string(data, NULL), &ret) != SR_OK)
			return SR_ERR_ARG;
		if (ret <= 0)
			return SR_ERR_ARG;
		rec_no = ret;
		if (rec_no > devc->record_count)
			return SR_ERR_ARG;
		devc->data_source = DATA_SOURCE_REC_FIRST + rec_no - 1;
		break;
	case SR_CONF_DATALOG:
		if (!devc)
			return SR_ERR_ARG;
		on = g_variant_get_boolean(data);
		sr_err("DIAG: record start/stop %d, currently ENOIMPL", on);
		return SR_ERR_NA;
		/*
		 * TODO Send command 0x0a (start) or 0x0b (stop). Though
		 * start needs a name (ymd timestamp?) and interval and
		 * duration (arbitrary choice? 1s for 1d?). Or shall this
		 * SET request control "save" items instead? Take one
		 * sample each for every 'datalog=on' request? Combine
		 * limit_samples and limit_msec with datalog to configure
		 * a recording's parameters?
		 */
		break;
	case SR_CONF_MEASURED_QUANTITY:
		if (!devc)
			return SR_ERR_ARG;
		tuple_child = g_variant_get_child_value(data, 0);
		mq = g_variant_get_uint32(tuple_child);
		g_variant_unref(tuple_child);
		tuple_child = g_variant_get_child_value(data, 1);
		mqflags = g_variant_get_uint64(tuple_child);
		g_variant_unref(tuple_child);
		mode = ut181a_get_mode_from_mq_flags(mq, mqflags);
		if (!mode)
			return SR_ERR_NA;
		ret = ut181a_send_cmd_setmode(sdi->conn, mode);
		if (ret < 0)
			return ret;
		ret = ut181a_waitfor_response(sdi->conn, 100);
		if (ret < 0)
			return ret;
		if (devc->info.rsp_head.rsp_type != RSP_TYPE_REPLY_CODE)
			return SR_ERR_DATA;
		if (!devc->info.reply_code.ok)
			return SR_ERR_DATA;
		break;
	case SR_CONF_RANGE:
		range = g_variant_get_string(data, NULL);
		return ut181a_set_range_from_text(sdi, range);
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	int ret;

	devc = sdi ? sdi->priv : NULL;
	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
	case SR_CONF_DEVICE_OPTIONS:
		return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
	case SR_CONF_DATA_SOURCE:
		if (!devc)
			return SR_ERR_NA;
		ret = ut181a_update_recordings(sdi);
		if (ret < 0)
			return ret;
		*data = g_variant_new_strv(devc->data_source_names, devc->data_source_count);
		break;
	case SR_CONF_MEASURED_QUANTITY:
		*data = ut181a_get_mq_flags_list();
		break;
	case SR_CONF_RANGE:
		*data = ut181a_get_ranges_list();
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	int ret;
	size_t rec_idx;

	devc = sdi->priv;
	serial = sdi->conn;
	serial_flush(serial);

	/*
	 * Send an acquisition start command which depends on the
	 * currently selected data source. Enter monitor mode for
	 * Live readings, get saved or recorded data otherwise. The
	 * latter require queries for sample counts, then run chunked
	 * download sequences (single item for Save, set of samples
	 * for Recordings).
	 */
	if (devc->data_source == DATA_SOURCE_LIVE) {
		ret = ut181a_send_cmd_monitor(serial, TRUE);
	} else if (devc->data_source == DATA_SOURCE_SAVE) {
		/*
		 * There is only one sequence of saved measurements in
		 * the device, but its length is yet unknown. Determine
		 * the number of saved items, and initiate the reception
		 * of the first value. Completion of data reception will
		 * drive subsequent progress.
		 */
		ret = ut181a_send_cmd_get_save_count(serial);
		if (ret < 0)
			return ret;
		ret = ut181a_configure_waitfor(devc, FALSE, 0, 0,
			FALSE, FALSE, TRUE, FALSE);
		if (ret < 0)
			return ret;
		ret = ut181a_waitfor_response(sdi, 200);
		if (ret < 0)
			return ret;
		devc->info.save_info.save_count = devc->wait_state.data_value;
		devc->info.save_info.save_idx = 0;
		ret = ut181a_send_cmd_get_saved_value(serial, 0);
	} else if (devc->data_source >= DATA_SOURCE_REC_FIRST) {
		/*
		 * When we get here, the data source got selected, which
		 * includes an update of the device's list of recordings.
		 * So the index should be good, just the number of samples
		 * in that recording is yet unknown. Get the sample count
		 * and initiate the reception of the first chunk, completed
		 * reception of a chunk advances through the sequence.
		 */
		rec_idx = devc->data_source - DATA_SOURCE_REC_FIRST;
		if (rec_idx >= devc->record_count)
			return SR_ERR_DATA;
		devc->info.rec_info.rec_count = devc->record_count;
		devc->info.rec_info.rec_idx = rec_idx;
		devc->info.rec_info.auto_next = 0;
		devc->info.rec_info.auto_feed = 1;
		ret = ut181a_send_cmd_get_rec_info(serial, rec_idx);
		if (ret < 0)
			return ret;
		ret = ut181a_configure_waitfor(devc,
			FALSE, CMD_CODE_GET_REC_INFO, 0,
			FALSE, FALSE, FALSE, FALSE);
		if (ret < 0)
			return ret;
		ret = ut181a_waitfor_response(sdi, 200);
		if (ret < 0)
			return ret;
		devc->info.rec_data.samples_total = devc->wait_state.data_value;
		devc->info.rec_data.samples_curr = 0;
		ret = ut181a_send_cmd_get_rec_samples(serial, rec_idx, 0);
	}
	if (ret < 0)
		return ret;

	sr_sw_limits_acquisition_start(&devc->limits);
	devc->recv_count = 0;
	std_session_send_df_header(sdi);

	serial_source_add(sdi->session, serial, G_IO_IN, 10,
		ut181a_handle_events, (void *)sdi);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{

	sdi->status = SR_ST_STOPPING;
	/* Initiate stop here. Activity happens in ut181a_handle_events(). */

	return SR_OK;
}

static struct sr_dev_driver uni_t_ut181a_driver_info = {
	.name = "uni-t-ut181a",
	.longname = "UNI-T UT181A",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = std_dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = std_serial_dev_open,
	.dev_close = std_serial_dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(uni_t_ut181a_driver_info);
