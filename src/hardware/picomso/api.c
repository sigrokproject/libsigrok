/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2026
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

static const struct picomso_profile supported_picomso[] = {
    { 0x2E8A, 0x2040, "PicoMSO", "PicoMSO-RP2040", NULL,
      "PicoMSO", "PicoMSO-RP2040" },
    { 0x2E8A, 0x2350, "PicoMSO", "PicoMSO-RP2350", NULL,
      "PicoMSO", "PicoMSO-RP2350" },
    ALL_ZERO
};

static const uint32_t scanopts[] = {
    SR_CONF_CONN,
    SR_CONF_PROBE_NAMES,
};

static const uint32_t drvopts[] = {
    SR_CONF_LOGIC_ANALYZER,
    SR_CONF_OSCILLOSCOPE,
};

static const uint32_t devopts[] = {
    SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
    SR_CONF_CONN | SR_CONF_GET,
    SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
    SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
    SR_CONF_CAPTURE_RATIO | SR_CONF_GET | SR_CONF_SET,
};

static const int32_t trigger_matches[] = {
    SR_TRIGGER_ZERO,
    SR_TRIGGER_ONE,
    SR_TRIGGER_RISING,
    SR_TRIGGER_FALLING,
};

static const uint64_t samplerates[] = {
    SR_KHZ(5),
    SR_KHZ(10),
    SR_KHZ(20),
    SR_KHZ(50),
    SR_KHZ(100),
    SR_KHZ(200),
    SR_KHZ(500),
    SR_MHZ(1),
    SR_MHZ(2),
    SR_MHZ(5),
    SR_MHZ(10),
    SR_MHZ(20),
    SR_MHZ(50),
    SR_MHZ(100),
    SR_MHZ(200),
};

static const char *channel_names_logic[] = {
    "D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
    "D8", "D9", "D10", "D11", "D12", "D13", "D14", "D15",
};

static const char *channel_names_analog[] = {
    "A0", "A1",
};

static gboolean is_plausible(const struct libusb_device_descriptor *des)
{
    int i;

    for (i = 0; supported_picomso[i].vid; i++) {
        if (des->idVendor != supported_picomso[i].vid)
            continue;
        if (des->idProduct == supported_picomso[i].pid)
            return TRUE;
    }

    return FALSE;
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
    struct drv_context *drvc;
    struct dev_context *devc;
    struct sr_dev_inst *sdi;
    struct sr_usb_dev_inst *usb;
    struct sr_channel *ch;
    struct sr_channel_group *cg;
    struct sr_config *src;
    const struct picomso_profile *prof;
    GSList *l, *devices, *conn_devices;
    struct libusb_device_descriptor des;
    libusb_device **devlist;
    struct libusb_device_handle *hdl;
    int ret, i;
    size_t j, num_logic_channels, num_analog_channels;
    const char *conn;
    const char *probe_names;
    char manufacturer[64], product[64], serial_num[64], connection_id[64];
    size_t ch_max, ch_idx;
    const char *channel_name;

    drvc = di->context;

    conn = NULL;
    probe_names = NULL;
    for (l = options; l; l = l->next) {
        src = l->data;
        switch (src->key) {
        case SR_CONF_CONN:
            conn = g_variant_get_string(src->data, NULL);
            break;
        case SR_CONF_PROBE_NAMES:
            probe_names = g_variant_get_string(src->data, NULL);
            break;
        }
    }

    if (conn)
        conn_devices = sr_usb_find(drvc->sr_ctx->libusb_ctx, conn);
    else
        conn_devices = NULL;

    devices = NULL;
    libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &devlist);
    for (i = 0; devlist[i]; i++) {
        if (conn) {
            usb = NULL;
            for (l = conn_devices; l; l = l->next) {
                usb = l->data;
                if (usb->bus == libusb_get_bus_number(devlist[i]) &&
                    usb->address == libusb_get_device_address(devlist[i]))
                    break;
            }
            if (!l)
                continue;
        }

        libusb_get_device_descriptor(devlist[i], &des);
        if (!is_plausible(&des))
            continue;

        if ((ret = libusb_open(devlist[i], &hdl)) < 0) {
            sr_warn("Failed to open potential device with VID:PID %04x:%04x: %s.",
                des.idVendor, des.idProduct, libusb_error_name(ret));
            continue;
        }

        if (des.iManufacturer == 0) {
            manufacturer[0] = '\0';
        } else if ((ret = libusb_get_string_descriptor_ascii(hdl,
                des.iManufacturer, (unsigned char *)manufacturer,
                sizeof(manufacturer))) < 0) {
            sr_warn("Failed to get manufacturer string descriptor: %s.",
                libusb_error_name(ret));
            libusb_close(hdl);
            continue;
        }

        if (des.iProduct == 0) {
            product[0] = '\0';
        } else if ((ret = libusb_get_string_descriptor_ascii(hdl,
                des.iProduct, (unsigned char *)product,
                sizeof(product))) < 0) {
            sr_warn("Failed to get product string descriptor: %s.",
                libusb_error_name(ret));
            libusb_close(hdl);
            continue;
        }

        if (des.iSerialNumber == 0) {
            serial_num[0] = '\0';
        } else if ((ret = libusb_get_string_descriptor_ascii(hdl,
                des.iSerialNumber, (unsigned char *)serial_num,
                sizeof(serial_num))) < 0) {
            sr_warn("Failed to get serial number string descriptor: %s.",
                libusb_error_name(ret));
            libusb_close(hdl);
            continue;
        }

        libusb_close(hdl);

        if (usb_get_port_path(devlist[i], connection_id,
                sizeof(connection_id)) < 0)
            continue;

        prof = NULL;
        for (j = 0; supported_picomso[j].vid; j++) {
            if (des.idVendor == supported_picomso[j].vid &&
                des.idProduct == supported_picomso[j].pid &&
                (!supported_picomso[j].usb_manufacturer ||
                 !strcmp(manufacturer, supported_picomso[j].usb_manufacturer)) &&
                (!supported_picomso[j].usb_product ||
                 !strcmp(product, supported_picomso[j].usb_product))) {
                prof = &supported_picomso[j];
                break;
            }
        }
        if (!prof)
            continue;

        sdi = g_malloc0(sizeof(struct sr_dev_inst));
        sdi->status = SR_ST_INACTIVE;
        sdi->vendor = g_strdup(prof->vendor);
        sdi->model = g_strdup(prof->model);
        sdi->version = g_strdup(prof->model_version);
        sdi->serial_num = g_strdup(serial_num);
        sdi->connection_id = g_strdup(connection_id);
        sdi->inst_type = SR_INST_USB;
        sdi->conn = sr_usb_dev_inst_new(libusb_get_bus_number(devlist[i]),
            libusb_get_device_address(devlist[i]), NULL);

        devc = picomso_dev_new();
        devc->profile = prof;
        devc->samplerates = samplerates;
        devc->num_samplerates = ARRAY_SIZE(samplerates);
        sdi->priv = devc;
        devices = g_slist_append(devices, sdi);

        num_logic_channels = ARRAY_SIZE(channel_names_logic);
        num_analog_channels = ARRAY_SIZE(channel_names_analog);

        ch_max = num_logic_channels;
        devc->channel_names = sr_parse_probe_names(probe_names,
            channel_names_logic, ch_max, ch_max, &ch_max);

        ch_idx = 0;

        cg = sr_channel_group_new(sdi, "Logic", NULL);
        for (j = 0; j < num_logic_channels; j++) {
            channel_name = devc->channel_names[j];
            ch = sr_channel_new(sdi, ch_idx++, SR_CHANNEL_LOGIC,
                TRUE, channel_name);
            cg->channels = g_slist_append(cg->channels, ch);
        }

        for (j = 0; j < num_analog_channels; j++) {
            channel_name = channel_names_analog[j];
            ch = sr_channel_new(sdi, ch_idx++, SR_CHANNEL_ANALOG,
                FALSE, channel_name);

            cg = sr_channel_group_new(sdi, channel_name, NULL);
            cg->channels = g_slist_append(NULL, ch);
        }
    }

    libusb_free_device_list(devlist, 1);
    g_slist_free_full(conn_devices, (GDestroyNotify)sr_usb_dev_inst_free);

    return std_scan_complete(di, devices);
}

static void clear_helper(struct dev_context *devc)
{
    g_strfreev(devc->channel_names);
    g_slist_free(devc->enabled_analog_channels);
    devc->enabled_analog_channels = NULL;
}

static int dev_clear(const struct sr_dev_driver *di)
{
    return std_dev_clear_with_callback(di,
        (std_dev_clear_callback)clear_helper);
}

static int dev_open(struct sr_dev_inst *sdi)
{
    struct sr_dev_driver *di;
    struct dev_context *devc;
    int ret;

    di = sdi->driver;
    devc = sdi->priv;

    ret = picomso_dev_open(sdi, di);
    if (ret != SR_OK) {
        sr_err("Unable to open device.");
        return ret;
    }

    if (devc->cur_samplerate == 0)
        devc->cur_samplerate = devc->samplerates[0];

    return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
    struct sr_usb_dev_inst *usb;

    usb = sdi->conn;
    if (!usb->devhdl)
        return SR_ERR_BUG;

    sr_info("Closing device on %d.%d (physical %s) interface %d.",
        usb->bus, usb->address, sdi->connection_id, USB_INTERFACE);
    libusb_release_interface(usb->devhdl, USB_INTERFACE);
    libusb_close(usb->devhdl);
    usb->devhdl = NULL;

    return SR_OK;
}

static int config_get(uint32_t key, GVariant **data,
    const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
    struct dev_context *devc;
    struct sr_usb_dev_inst *usb;

    (void)cg;

    if (!sdi)
        return SR_ERR_ARG;

    devc = sdi->priv;

    switch (key) {
    case SR_CONF_CONN:
        if (!sdi->conn)
            return SR_ERR_ARG;
        usb = sdi->conn;
        *data = g_variant_new_printf("%d.%d", usb->bus, usb->address);
        break;
    case SR_CONF_LIMIT_SAMPLES:
        *data = g_variant_new_uint64(devc->limit_samples);
        break;
    case SR_CONF_SAMPLERATE:
        *data = g_variant_new_uint64(devc->cur_samplerate);
        break;
    case SR_CONF_CAPTURE_RATIO:
        *data = g_variant_new_uint64(devc->capture_ratio);
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
    uint64_t value;
    int idx;

    (void)cg;

    if (!sdi)
        return SR_ERR_ARG;

    devc = sdi->priv;

    switch (key) {
    case SR_CONF_SAMPLERATE:
        if ((idx = std_u64_idx(data, devc->samplerates,
                devc->num_samplerates)) < 0)
            return SR_ERR_ARG;
        devc->cur_samplerate = devc->samplerates[idx];
        break;
    case SR_CONF_LIMIT_SAMPLES:
        value = g_variant_get_uint64(data);
        uint32_t max_samples = devc->fw_caps.max_samples_logic;

    if (value == 0 || value > max_samples)
        return SR_ERR_ARG;
    devc->limit_samples = value;
        break;
    case SR_CONF_CAPTURE_RATIO:
        value = g_variant_get_uint64(data);
        if (value > 10)
            return SR_ERR_ARG;
        devc->capture_ratio = value;
        break;
    default:
        return SR_ERR_NA;
    }

    return SR_OK;
}

static int config_list(uint32_t key, GVariant **data,
    const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
    struct dev_context *devc;

    devc = sdi ? sdi->priv : NULL;

    switch (key) {
    case SR_CONF_SCAN_OPTIONS:
    case SR_CONF_DEVICE_OPTIONS:
        if (cg)
            return SR_ERR_NA;
        return STD_CONFIG_LIST(key, data, sdi, cg,
            scanopts, drvopts, devopts);
    case SR_CONF_LIMIT_SAMPLES:
    *data = g_variant_new("(tt)",
        (guint64)1,
        (guint64)devc->fw_caps.max_samples_logic);
    break;
    case SR_CONF_SAMPLERATE:
        if (!devc)
            return SR_ERR_NA;
        *data = std_gvar_samplerates(devc->samplerates,
            devc->num_samplerates);
        break;
    case SR_CONF_TRIGGER_MATCH:
        *data = std_gvar_array_i32(ARRAY_AND_SIZE(trigger_matches));
        break;
    default:
        return SR_ERR_NA;
    }

    return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
    picomso_abort_acquisition(sdi->priv);

    return SR_OK;
}

static struct sr_dev_driver picomso_driver_info = {
    .name = "picomso",
    .longname = "PicoMSO mixed-signal oscilloscope / logic analyzer",
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
    .dev_acquisition_start = picomso_start_acquisition,
    .dev_acquisition_stop = dev_acquisition_stop,
    .context = NULL,
};

SR_REGISTER_DEV_DRIVER(picomso_driver_info);