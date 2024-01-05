/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2023 Konrad Tagowski <konrad.tagowski@grinn-global.com>
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
};

static const uint32_t drvopts[] = {
    SR_CONF_MULTIMETER,
};

static const uint32_t devopts[] = {
    SR_CONF_CONTINUOUS,
    SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
    SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
    SR_CONF_DATA_SOURCE | SR_CONF_GET | SR_CONF_LIST,
    /* SWAP is used to imitate the SELECT button on the multimeter, is avaible a better option to handle this case? */
    SR_CONF_SWAP | SR_CONF_GET | SR_CONF_SET,
};


static const char *channel_names[] = {
    "Main",
};

static const char *data_sources[] = {
    "Live",
};

static struct sr_dev_driver uni_t_ut8803e_driver_info;

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
    struct sr_dev_inst *sdi;
    struct dev_context *devc;
    struct sr_serial_dev_inst *serial;
    struct sr_config *src;
    const char *conn, *serialcomm;
    char conn_id[64];
    int rc;
    size_t idx;
    GSList *l ,*devices;

    conn = NULL;
    serialcomm = "9600/8n1";
    for (l = options; l; l = l->next) {
        src = l->data;
        if (src->key == SR_CONF_CONN)
            conn = g_variant_get_string(src->data, NULL);
    }

    if (!conn)
        return NULL;

    devices = NULL;
    serial = sr_serial_dev_inst_new(conn, serialcomm);
    rc = serial_open(serial, SERIAL_RDWR);
    snprintf(conn_id, sizeof(conn_id), "%s", serial->port);
    if (rc != SR_OK) {
        serial_close(serial);
        sr_serial_dev_inst_free(serial);
        return devices;
    }

    sdi = g_malloc0(sizeof(*sdi));
    sdi->status = SR_ST_INACTIVE;
    sdi->vendor = g_strdup("UNI-T");
    sdi->model = g_strdup("UT8803E");
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
    devices = g_slist_append(devices, sdi);

    return std_scan_complete(di, devices);
}


static int config_get(uint32_t key, GVariant **data,
    const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
    struct dev_context *devc;

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
        *data = g_variant_new_string(data_sources[0]);
        break;
    //TO DO ADD SR_CONF_MEASURED_QUANTITY and SR_CONF_RANGE
    case SR_CONF_SWAP:
    /* SWAP Function in this case */
        *data = g_variant_new_boolean(FALSE);
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
    (void)cg;

    devc = sdi->priv;

    switch (key) {
    case SR_CONF_LIMIT_FRAMES:
    case SR_CONF_LIMIT_SAMPLES:
    case SR_CONF_LIMIT_MSEC:
        if (!devc)
            return SR_ERR_ARG;
        return sr_sw_limits_config_set(&devc->limits, key, data);
        break;
    case SR_CONF_SWAP:
    /* SWAP Function in this case */
        return ut8803e_send_cmd(sdi, CMD_CODE_SELECT);
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
    case SR_CONF_DATA_SOURCE:
        *data = g_variant_new_strv(ARRAY_AND_SIZE(data_sources));
        break;
    //TO DO: ADD MEASURED QUANTITY and RANGE
    default:
        return SR_ERR_NA;
    }

    return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
    struct dev_context *devc;
    struct sr_serial_dev_inst *serial;

    devc = sdi->priv;
    serial = sdi->conn;
    serial_flush(serial);

    sr_sw_limits_acquisition_start(&devc->limits);
    devc->packet_len = 0;
    std_session_send_df_header(sdi);

    serial_source_add(sdi->session, serial, G_IO_IN, 10,
            ut8803e_handle_events, (void *)sdi);

    return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
    sdi->status = SR_ST_STOPPING;
    /* Initiate stop here. Activity happens in ut8803e_handle_events(). */
    return SR_OK;
}

static struct sr_dev_driver uni_t_ut8803e_driver_info = {
    .name = "uni-t-ut8803e",
    .longname = "UNI-T UT8803E",
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
SR_REGISTER_DEV_DRIVER(uni_t_ut8803e_driver_info);
