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

#include <ctype.h>
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

static struct sr_dev_inst *probe_device_common(const char *path,
	uint16_t vid, uint16_t pid, const char *want_serno,
	const wchar_t *vendor, const wchar_t *product)
{
	char nonws[16], *s, *endp;
	unsigned long relay_count;
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
	char cg_name[24];

	/*
	 * Get relay count from product string. Weak condition,
	 * accept any trailing number regardless of preceeding text.
	 */
	snprintf(nonws, sizeof(nonws), "%ls", product);
	s = nonws;
	s += strlen(s);
	while (s > nonws && isdigit((int)s[-1]))
		s--;
	ret = sr_atoul_base(s, &relay_count, &endp, 10);
	if (ret != SR_OK || !endp || *endp)
		return NULL;
	if (!relay_count)
		return NULL;
	sr_info("Relay count %lu from product string %s.", relay_count, nonws);

	/* Open device, need to communicate to identify. */
	if (vid && pid)
		hid = hid_open(vid, pid, NULL);
	else
		hid = hid_open_path(path);
	if (!hid) {
		sr_err("Cannot open %s.", path);
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
		sr_spew("Got report bytes: %s, rc %d.", txt->str, ret);
		sr_hexdump_free(txt);
	}
	if (ret < 0) {
		sr_err("Cannot read %s: %ls.", path, hid_error(NULL));
		return NULL;
	}
	if (ret != sizeof(report)) {
		sr_err("Unexpected HID report length %d from %s.", ret, path);
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
			sr_warn("Skipping %s, non-printable serial.", path);
			return NULL;
		}
	}
	curr_state = report[1 + STATE_INDEX];
	sr_info("HID report data: serial number %s, relay state 0x%02x.",
		serno, curr_state);

	/* Optionally filter by serial number. */
	if (want_serno && *want_serno && strcmp(serno, want_serno) != 0) {
		sr_dbg("Serial number does not match user spec. Skipping.");
		return NULL;
	}

	/* Create a device instance. */
	sdi = g_malloc0(sizeof(*sdi));
	sdi->vendor = g_strdup_printf("%ls", vendor);
	sdi->model = g_strdup_printf("%ls", product);
	sdi->serial_num = g_strdup(serno);
	sdi->connection_id = g_strdup(path);
	sdi->driver = &dcttech_usbrelay_driver_info;
	sdi->inst_type = SR_INST_USB;

	/* Create channels (groups). */
	devc = g_malloc0(sizeof(*devc));
	sdi->priv = devc;
	devc->hid_path = g_strdup(path);
	devc->usb_vid = vid;
	devc->usb_pid = pid;
	devc->relay_count = relay_count;
	devc->relay_mask = (1U << relay_count) - 1;
	for (idx = 0; idx < devc->relay_count; idx++) {
		nr = idx + 1;
		snprintf(cg_name, sizeof(cg_name), "R%zu", nr);
		cgc = g_malloc0(sizeof(*cgc));
		cgc->number = nr;
		cg = sr_channel_group_new(sdi, cg_name, cgc);
		(void)cg;
	}

	return sdi;
}

static struct sr_dev_inst *probe_device_enum(struct hid_device_info *dev,
	const char *want_serno)
{
	return probe_device_common(dev->path, 0, 0, want_serno,
		dev->manufacturer_string, dev->product_string);
}

static struct sr_dev_inst *probe_device_conn(const char *path)
{
	char vid_pid[12];
	uint16_t vid, pid;
	const char *s;
	char *endp;
	unsigned long num;
	hid_device *dev;
	gboolean ok;
	int ret;
	wchar_t vendor[32], product[32];

	/*
	 * The hidapi(3) library's API strives for maximum portability,
	 * thus won't provide ways of getting a path from alternative
	 * presentations like VID:PID pairs, bus.addr specs, etc. The
	 * typical V-USB setup neither provides reliable serial numbers
	 * (that USB enumeration would cover). So this driver's support
	 * for conn= specs beyond Unix style path names is limited, too.
	 * This implementation tries "VID.PID" then assumes "path". The
	 * inability to even get the path for a successfully opened HID
	 * results in redundancy across the places which open devices.
	 */

	/* Check for "<vid>.<pid>" specs. */
	vid = pid = 0;
	s = path;
	ret = sr_atoul_base(s, &num, &endp, 16);
	if (ret == SR_OK && endp && endp == s + 4 && *endp == '.' && num) {
		vid = num;
		s = ++endp;
	}
	ret = sr_atoul_base(s, &num, &endp, 16);
	if (ret == SR_OK && endp && endp == s + 4 && *endp == '\0' && num) {
		pid = num;
		s = ++endp;
	}
	if (vid && pid) {
		snprintf(vid_pid, sizeof(vid_pid), "%04x.%04x", vid, pid);
		path = vid_pid;
		sr_dbg("Using VID.PID %s.", path);
	}

	/* Open the device, get vendor and product strings. */
	if (vid && pid)
		dev = hid_open(vid, pid, NULL);
	else
		dev = hid_open_path(path);
	if (!dev) {
		sr_err("Cannot open %s.", path);
		return NULL;
	}
	ok = TRUE;
	ret = hid_get_manufacturer_string(dev, vendor, ARRAY_SIZE(vendor));
	if (ret != 0)
		ok = FALSE;
	if (!wcslen(vendor))
		ok = FALSE;
	ret = hid_get_product_string(dev, product, ARRAY_SIZE(product));
	if (ret != 0)
		ok = FALSE;
	if (!wcslen(product))
		ok = FALSE;
	hid_close(dev);
	if (!ok)
		return NULL;

	return probe_device_common(path, vid, pid, NULL, vendor, product);
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	const char *conn;
	GSList *devices;
	struct drv_context *drvc;
	char want_serno[SERNO_LENGTH + 1];
	struct hid_device_info *devs, *curdev;
	wchar_t *ws;
	char nonws[32];
	struct sr_dev_inst *sdi;

	/* Get optional conn= spec when provided. */
	conn = NULL;
	(void)sr_serial_extract_options(options, &conn, NULL);
	if (conn && !*conn)
		conn = NULL;

	devices = NULL;
	drvc = di->context;
	drvc->instances = NULL;

	/*
	 * The firmware is V-USB based. The USB VID:PID identification
	 * is shared across several projects. Need to inspect the vendor
	 * and product _strings_ to actually identify the device.
	 *
	 * The USB serial number need not be present nor reliable. The
	 * HID report content will carry the board's serial number.
	 * When users specify "sn=..." connection strings, then run a
	 * regular USB enumation, and filter the result set by serial
	 * numbers which only become available with HID reports.
	 *
	 * When other connection strings were specified, then have
	 * HIDAPI open _this_ device and skip the enumeration. Which
	 * allows users to specify paths that need not match the
	 * enumeration's details.
	 */
	memset(want_serno, 0, sizeof(want_serno));
	if (conn && g_str_has_prefix(conn, "sn=")) {
		conn += strlen("sn=");
		snprintf(want_serno, sizeof(want_serno), "%s", conn);
		conn = NULL;
	}
	if (conn) {
		sr_info("Checking HID path %s.", conn);
		sdi = probe_device_conn(conn);
		if (!sdi)
			sr_warn("Failed to communicate to %s.", conn);
		else
			devices = g_slist_append(devices, sdi);
	}
	devs = hid_enumerate(VENDOR_ID, PRODUCT_ID);
	for (curdev = devs; curdev; curdev = curdev->next) {
		if (conn)
			break;
		if (!curdev->vendor_id || !curdev->product_id)
			continue;
		if (!curdev->manufacturer_string || !curdev->product_string)
			continue;
		if (!*curdev->manufacturer_string || !*curdev->product_string)
			continue;
		sr_dbg("Checking %04hx:%04hx, vendor %ls, product %ls.",
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
		if (!g_str_has_prefix(nonws, PRODUCT_STRING_PREFIX))
			continue;

		/* Identify device by communicating to it. */
		sr_info("Checking HID path %s.", curdev->path);
		sdi = probe_device_enum(curdev, want_serno);
		if (!sdi)
			continue;
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

	if (devc->usb_vid && devc->usb_pid)
		devc->hid_dev = hid_open(devc->usb_vid, devc->usb_pid, NULL);
	else
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
