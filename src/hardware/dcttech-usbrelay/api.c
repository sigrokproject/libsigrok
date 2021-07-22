/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2021 Gerhard Sittig <gerhard.sittig@gmx.net>
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

#include <hidapi.h>
#include <string.h>

#include "protocol.h"

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t drvopts[] = {
	SR_CONF_MULTIPLEXER,
};

static const uint32_t devopts[] = {
	SR_CONF_CONN | SR_CONF_GET,
	SR_CONF_ENABLED | SR_CONF_SET, /* Enable/disable all relays at once. */
};

static const uint32_t devopts_cg[] = {
	SR_CONF_ENABLED | SR_CONF_GET | SR_CONF_SET,
};

static struct sr_dev_driver dcttech_usbrelay_driver_info;

static struct sr_dev_inst *probe_device(struct hid_device_info *dev,
	size_t relay_count)
{
	hid_device *hid;
	int ret;
	char serno[SERNO_LENGTH + 1];
	uint8_t curr_state;
	uint8_t report[1 + REPORT_BYTECOUNT];
	GString *txt;
	size_t snr_pos;
	char c;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct channel_group_context *cgc;
	size_t idx, nr;
	struct sr_channel_group *cg;

	/* Open device, need to communicate to identify. */
	hid = hid_open_path(dev->path);
	if (!hid) {
		sr_err("Cannot open %s: %ls.", dev->path, hid_error(NULL));
		return NULL;
	}

	/* Get an HID report. */
	hid_set_nonblocking(hid, 0);
	memset(&report, 0, sizeof(report));
	report[0] = REPORT_NUMBER;
	ret = hid_get_feature_report(hid, report, sizeof(report));
	hid_close(hid);
	if (sr_log_loglevel_get() >= SR_LOG_SPEW) {
		txt = sr_hexdump_new(report, sizeof(report));
		sr_spew("raw report, rc %d, bytes %s", ret, txt->str);
		sr_hexdump_free(txt);
	}
	if (ret < 0) {
		sr_err("Cannot read %s: %ls.", dev->path, hid_error(NULL));
		return NULL;
	}
	if (ret != sizeof(report)) {
		sr_err("Unexpected HID report length: %s.", dev->path);
		return NULL;
	}

	/*
	 * Serial number must be all printable characters. Relay state
	 * is for information only, gets re-retrieved before configure
	 * API calls (get/set).
	 */
	memset(serno, 0, sizeof(serno));
	for (snr_pos = 0; snr_pos < SERNO_LENGTH; snr_pos++) {
		c = report[1 + snr_pos];
		serno[snr_pos] = c;
		if (c < 0x20 || c > 0x7e) {
			sr_dbg("non-printable serno");
			return NULL;
		}
	}
	curr_state = report[1 + STATE_INDEX];
	sr_spew("report data, serno[%s], relays 0x%02x.", serno, curr_state);

	/* Create a device instance. */
	sdi = g_malloc0(sizeof(*sdi));
	sdi->vendor = g_strdup_printf("%ls", dev->manufacturer_string);
	sdi->model = g_strdup_printf("%ls", dev->product_string);
	sdi->serial_num = g_strdup(serno);
	sdi->connection_id = g_strdup(dev->path);
	sdi->driver = &dcttech_usbrelay_driver_info;
	sdi->inst_type = SR_INST_USB;

	/* Create channels (groups). */
	devc = g_malloc0(sizeof(*devc));
	sdi->priv = devc;
	devc->hid_path = g_strdup(dev->path);
	devc->relay_count = relay_count;
	devc->relay_mask = (1U << relay_count) - 1;
	for (idx = 0; idx < devc->relay_count; idx++) {
		nr = idx + 1;
		cg = g_malloc0(sizeof(*cg));
		cg->name = g_strdup_printf("R%zu", nr);
		cgc = g_malloc0(sizeof(*cgc));
		cg->priv = cgc;
		cgc->number = nr;
		sdi->channel_groups = g_slist_append(sdi->channel_groups, cg);
	}

	return sdi;
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	const char *conn;
	GSList *devices;
	struct drv_context *drvc;
	struct hid_device_info *devs, *curdev;
	int ret;
	wchar_t *ws;
	char nonws[32];
	char *s, *endp;
	unsigned long relay_count;
	struct sr_dev_inst *sdi;

	/* Get optional conn= spec when provided. */
	conn = NULL;
	(void)sr_serial_extract_options(options, &conn, NULL);
	if (conn && !*conn)
		conn = NULL;
	/*
	 * TODO Accept different types of conn= specs? Either paths that
	 * hidapi(3) can open. Or bus.addr specs that we can check for
	 * during USB enumeration. Derive want_path, want_bus, want_addr
	 * here from the optional conn= spec.
	 */

	devices = NULL;
	drvc = di->context;
	drvc->instances = NULL;

	/*
	 * The firmware is V-USB based. The USB VID:PID identification
	 * is shared across several projects. Need to inspect the vendor
	 * and product _strings_ to actually identify the device.
	 *
	 * The USB serial number need not be present nor reliable. The
	 * HID report contains a five character string which may serve
	 * as an identification for boards (is said to differ between
	 * boards). The last byte encodes the current relays state.
	 */
	devs = hid_enumerate(VENDOR_ID, PRODUCT_ID);
	for (curdev = devs; curdev; curdev = curdev->next) {
		if (!curdev->vendor_id || !curdev->product_id)
			continue;
		if (!curdev->manufacturer_string || !curdev->product_string)
			continue;
		if (!*curdev->manufacturer_string || !*curdev->product_string)
			continue;
		if (conn && strcmp(curdev->path, conn) != 0) {
			sr_dbg("skipping %s, conn= mismatch", curdev->path);
			continue;
		}
		sr_dbg("checking %04hx:%04hx, vendor %ls, product %ls.",
			curdev->vendor_id, curdev->product_id,
			curdev->manufacturer_string, curdev->product_string);

		/* Check USB details retrieved by enumeration. */
		ws = curdev->manufacturer_string;
		if (!ws || !wcslen(ws))
			continue;
		snprintf(nonws, sizeof(nonws), "%ls", ws);
		if (strcmp(nonws, VENDOR_STRING) != 0)
			continue;
		ws = curdev->product_string;
		if (!ws || !wcslen(ws))
			continue;
		snprintf(nonws, sizeof(nonws), "%ls", ws);
		s = nonws;
		if (!g_str_has_prefix(s, PRODUCT_STRING_PREFIX))
			continue;
		s += strlen(PRODUCT_STRING_PREFIX);
		ret = sr_atoul_base(s, &relay_count, &endp, 10);
		if (ret != SR_OK || !endp || *endp)
			continue;
		sr_info("Found: HID path %s, relay count %lu.",
			curdev->path, relay_count);

		/* Identify device by communicating to it. */
		sdi = probe_device(curdev, relay_count);
		if (!sdi) {
			sr_warn("Failed to communicate to %s.", curdev->path);
			continue;
		}
		devices = g_slist_append(devices, sdi);
	}
	hid_free_enumeration(devs);

	return devices;
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = sdi->priv;

	if (devc->hid_dev) {
		hid_close(devc->hid_dev);
		devc->hid_dev = NULL;
	}

	devc->hid_dev = hid_open_path(devc->hid_path);
	if (!devc->hid_dev)
		return SR_ERR_IO;

	(void)dcttech_usbrelay_update_state(sdi);

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = sdi->priv;

	if (devc->hid_dev) {
		hid_close(devc->hid_dev);
		devc->hid_dev = NULL;
	}

	return SR_OK;
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	gboolean on;
	int ret;

	if (!cg) {
		switch (key) {
		case SR_CONF_CONN:
			if (!sdi->connection_id)
				return SR_ERR_NA;
			*data = g_variant_new_string(sdi->connection_id);
			return SR_OK;
		default:
			return SR_ERR_NA;
		}
	}

	switch (key) {
	case SR_CONF_ENABLED:
		ret = dcttech_usbrelay_query_cg(sdi, cg, &on);
		if (ret != SR_OK)
			return ret;
		*data = g_variant_new_boolean(on);
		return SR_OK;
	default:
		return SR_ERR_NA;
	}
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	gboolean on;

	if (!cg) {
		switch (key) {
		case SR_CONF_ENABLED:
			/* Enable/disable all channels at the same time. */
			on = g_variant_get_boolean(data);
			return dcttech_usbrelay_switch_cg(sdi, cg, on);
		default:
			return SR_ERR_NA;
		}
	} else {
		switch (key) {
		case SR_CONF_ENABLED:
			on = g_variant_get_boolean(data);
			return dcttech_usbrelay_switch_cg(sdi, cg, on);
		default:
			return SR_ERR_NA;
		}
	}

	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{

	if (!cg) {
		switch (key) {
		case SR_CONF_SCAN_OPTIONS:
		case SR_CONF_DEVICE_OPTIONS:
			return STD_CONFIG_LIST(key, data, sdi, cg,
				scanopts, drvopts, devopts);
		default:
			return SR_ERR_NA;
		}
	}

	switch (key) {
	case SR_CONF_DEVICE_OPTIONS:
		*data = std_gvar_array_u32(ARRAY_AND_SIZE(devopts_cg));
		return SR_OK;
	default:
		return SR_ERR_NA;
	}
}

static struct sr_dev_driver dcttech_usbrelay_driver_info = {
	.name = "dcttech-usbrelay",
	.longname = "dcttech usbrelay",
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
	.dev_acquisition_start = std_dummy_dev_acquisition_start,
	.dev_acquisition_stop = std_dummy_dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(dcttech_usbrelay_driver_info);
