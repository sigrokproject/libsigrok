/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2016 danselmi <da@da>
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

static const uint32_t ipdbg_org_la_drvopts[] = {
    SR_CONF_LOGIC_ANALYZER,
};

static const uint32_t ipdbg_org_la_scanopts[] = {
    SR_CONF_CONN,
    SR_CONF_SERIALCOMM,
};

static const uint32_t ipdbg_org_la_devopts[] = {
    SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
    SR_CONF_CAPTURE_RATIO | SR_CONF_GET | SR_CONF_SET,
    SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
};

static const int32_t ipdbg_org_la_trigger_matches[] = {
    SR_TRIGGER_ZERO,
    SR_TRIGGER_ONE,
    SR_TRIGGER_RISING,
    SR_TRIGGER_FALLING,
    //SR_TRIGGER_EDGE,
};

SR_PRIV struct sr_dev_driver ipdbg_la_driver_info;



static void ipdbg_org_la_split_addr_port(const char *conn, char **addr, char **port)
{
    char **strs = g_strsplit(conn, "/", 3);

    *addr = g_strdup(strs[1]);
    *port = g_strdup(strs[2]);

    g_strfreev(strs);
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
    sr_err("scan\n");
    struct drv_context *drvc;
    GSList *devices;

    (void)options;

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

    struct ipdbg_org_la_tcp *tcp = ipdbg_org_la_new_tcp();

    ipdbg_org_la_split_addr_port(conn, &tcp->address, &tcp->port);

    if (!tcp->address)
        return NULL;


    if(ipdbg_org_la_tcp_open(tcp) != SR_OK)
        return NULL;

    sr_err("set Reset\n");
//////////////////////////////////////////////////////////////////////////////////////////
    ipdbg_org_la_sendReset(tcp);
    ipdbg_org_la_sendReset(tcp);

    ipdbg_org_la_requestID(tcp);

    struct sr_dev_inst *sdi = g_malloc0(sizeof(struct sr_dev_inst));
    if(!sdi){
        sr_err("no possible to allocate sr_dev_inst");
        return NULL;
    }
    sdi->status = SR_ST_INACTIVE;
    sdi->vendor = g_strdup("ipdbg.org");
    sdi->model = g_strdup("Logic Analyzer");
    sdi->version = g_strdup("v1.0");
    sdi->driver = di;
    const size_t bufSize = 16;
    char buff[bufSize];


    struct ipdbg_org_la_dev_context *devc = ipdbg_org_la_dev_new();
    sdi->priv = devc;

    ipdbg_org_la_get_addrwidth_and_datawidth(tcp, devc);

    sr_err("addr_width = %d, data_width = %d\n", devc->ADDR_WIDTH, devc->DATA_WIDTH);
    sr_err("limit samples = %d\n", devc->limit_samples);
    /////////////////////////////////////////////////////////////////////////////////////////////////////////

    for (int i = 0; i < devc->DATA_WIDTH; i++)
    {
        snprintf(buff, bufSize, "ch%d", i);
        sr_channel_new(sdi, i, SR_CHANNEL_LOGIC, TRUE, buff);
    }

    sdi->inst_type = SR_INST_USER;
    sdi->conn = tcp;

    drvc->instances = g_slist_append(drvc->instances, sdi);
    devices = g_slist_append(devices, sdi);

    ipdbg_org_la_tcp_close(tcp);

    return devices;
}

static int dev_clear(const struct sr_dev_driver *di)
{
    sr_err("dev_clear\n");

    struct drv_context *drvc = di->context;
    struct sr_dev_inst *sdi;
    GSList *l;

    if (drvc) {
        for (l = drvc->instances; l; l = l->next) {
            sdi = l->data;
            struct ipdbg_org_la_tcp *tcp = sdi->conn;
            if(tcp)
            {
                ipdbg_org_la_tcp_close(tcp);
                ipdbg_org_la_tcp_free(tcp);
                g_free(tcp);
            }
            sdi->conn = NULL;
        }
    }

    return std_dev_clear(di);
}

static int dev_open(struct sr_dev_inst *sdi)
{
    sr_err("dev_open\n");
    (void)sdi;

    /* TODO: get handle from sdi->conn and open it. */
    sdi->status = SR_ST_INACTIVE;

    struct ipdbg_org_la_tcp *tcp = sdi->conn;

    if (!tcp)
    {
        sr_err("Out of memory\n");
        return SR_ERR;
    }
    sdi->conn = tcp;

    if(ipdbg_org_la_tcp_open(tcp) != SR_OK)
        return SR_ERR;

    sdi->status = SR_ST_ACTIVE;

    return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
    (void)sdi;

    /* TODO: get handle from sdi->conn and close it. */
    sr_err("dev_close\n");
    /// should be called before a new call to scan()
    struct ipdbg_org_la_tcp *tcp = sdi->conn;
    ipdbg_org_la_tcp_close(tcp);

    sdi->conn = NULL;

    sdi->status = SR_ST_INACTIVE;

    return SR_OK;
}

static int config_get(uint32_t key, GVariant **data,
    const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
    int ret;

    (void)sdi;
    (void)data;
    (void)cg;

    struct ipdbg_org_la_dev_context *devc = sdi->priv;

    ret = SR_OK;
    switch (key) {
    case SR_CONF_CAPTURE_RATIO:
        *data = g_variant_new_uint64(devc->capture_ratio);
        break;
    case SR_CONF_LIMIT_SAMPLES:
        *data = g_variant_new_uint64(devc->limit_samples);
        break;
    default:
        return SR_ERR_NA;
    }

    return ret;
}

static int config_set(uint32_t key, GVariant *data,
    const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
    int ret;

    (void)data;
    (void)cg;

    if (sdi->status != SR_ST_ACTIVE)
        return SR_ERR_DEV_CLOSED;

    sr_err("config_set\n");
    struct ipdbg_org_la_dev_context *devc = sdi->priv;

    ret = SR_OK;
    switch (key) {
    case SR_CONF_CAPTURE_RATIO:
        devc->capture_ratio = g_variant_get_uint64(data);
        if (devc->capture_ratio < 0 || devc->capture_ratio > 100)
        {
            devc->capture_ratio = 50;
            ret = SR_ERR;
        }
        else
            ret = SR_OK;
        break;
    case SR_CONF_LIMIT_SAMPLES:
        devc->limit_samples = g_variant_get_uint64(data);
        if(devc->limit_samples > devc->limit_samples_max)
        {
            devc->limit_samples = devc->limit_samples_max;
            ret = SR_ERR;
        }
        else
            ret = SR_OK;
        break;
    default:
        ret = SR_ERR_NA;
    }

    return ret;
}

static int config_list(uint32_t key, GVariant **data,
    const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
    (void)cg;

    switch (key) {
    case SR_CONF_SCAN_OPTIONS:
        *data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32, ipdbg_org_la_scanopts, ARRAY_SIZE(ipdbg_org_la_scanopts), sizeof(uint32_t));
        break;
    case SR_CONF_DEVICE_OPTIONS:
        if (!sdi)
            *data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32, ipdbg_org_la_drvopts, ARRAY_SIZE(ipdbg_org_la_drvopts), sizeof(uint32_t));
        else
            *data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32, ipdbg_org_la_devopts, ARRAY_SIZE(ipdbg_org_la_devopts), sizeof(uint32_t));
        break;
    case SR_CONF_TRIGGER_MATCH:
        *data = g_variant_new_fixed_array(G_VARIANT_TYPE_INT32, ipdbg_org_la_trigger_matches, ARRAY_SIZE(ipdbg_org_la_trigger_matches), sizeof(int32_t));
        break;
    default:
        return SR_ERR_NA;
    }

    return SR_OK;
}


static int init(struct sr_dev_driver *di, struct sr_context *sr_ctx)
{
    return std_init(di, sr_ctx);
}

static int cleanup(const struct sr_dev_driver *di)
{
    sr_err("cleanup\n");
    dev_clear(di);

    //return std_cleanup(di);
    return SR_OK;
}

static GSList *dev_list(const struct sr_dev_driver *di)
{
    //return std_dev_list(di);
    return ((struct drv_context *) (di->context))-> instances;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{

    if (sdi->status != SR_ST_ACTIVE)
        return SR_ERR_DEV_CLOSED;
    struct ipdbg_org_la_tcp *tcp = sdi->conn;

    struct ipdbg_org_la_dev_context *devc = sdi->priv;

    ipdbg_org_la_convert_trigger(sdi);
    sr_err("dev_acquisition_start\n");

    /* Send Triggerkonviguration */
    ipdbg_org_la_sendTrigger(devc, tcp);
    sr_err("dev_acquisition_start1\n");

    /* Send Delay */
    ipdbg_org_la_sendDelay(devc, tcp);
    sr_err("dev_acquisition_start2\n");

    //std_session_send_df_header(sdi, LOG_PREFIX);
    std_session_send_df_header(sdi);
    sr_err("dev_acquisition_start3\n");
    /* If the device stops sending for longer than it takes to send a byte,
     * that means it's finished. But wait at least 100 ms to be safe.
     */
    //sr_session_source_add(sdi->session, -1, G_IO_IN, 100, ipdbg_receive_data, (struct sr_dev_inst *)sdi);
    //sr_session_source_add(sdi->session, -1, G_IO_IN, 100, ipdbg_org_la_receive_data, NULL);
    sr_session_source_add(sdi->session, tcp->socket, G_IO_IN, 100, ipdbg_org_la_receive_data, (struct sr_dev_inst *)sdi);
    sr_err("dev_acquisition_start4\n");

    ipdbg_org_la_sendStart(tcp);
    sr_err("dev_acquisition_start5\n");
    /* TODO: configure hardware, reset acquisition state, set up
     * callbacks and send header packet. */

    return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{

    sr_err("dev_acquisition_stop\n");

    if (sdi->status != SR_ST_ACTIVE)
        return SR_ERR_DEV_CLOSED;

    ipdbg_org_la_abort_acquisition(sdi);

    return SR_OK;
}

SR_PRIV struct sr_dev_driver ipdbg_la_driver_info = {
    .name = "ipdbg-org-la",
    .longname = "ipdbg.org logic analyzer",
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
    .context = NULL,
};

SR_REGISTER_DEV_DRIVER(ipdbg_la_driver_info);
