/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2018-2019 Gerhard Sittig <gerhard.sittig@gmx.net>
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

/*
 * Scan support for Bluetooth LE devices is modelled after the MIT licensed
 * https://github.com/carsonmcdonald/bluez-experiments experiments/scantest.c
 * example source code which is:
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013 Carson McDonald
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * This file implements an internal platform agnostic API of libsigrok
 * for Bluetooth communication, as well as the first implementation on a
 * specific platform which is based on the BlueZ library and got tested
 * on Linux.
 *
 * TODO
 * - Separate the "common" from the "bluez specific" parts. The current
 *   implementation uses the fact that HAVE_BLUETOOTH exclusively depends
 *   on HAVE_LIBBLUEZ, and thus both are identical.
 * - Add missing features to the Linux platform support: Scan without
 *   root privileges, UUID to handle translation.
 * - Add support for other platforms.
 */

#include "config.h"

/* Unconditionally compile the source, optionally end up empty. */
#ifdef HAVE_BLUETOOTH

#ifdef HAVE_LIBBLUEZ
#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include <bluetooth/l2cap.h>
#include <bluetooth/rfcomm.h>
#endif
#include <ctype.h>
#include <errno.h>
#include <glib.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "bt-bluez"

#define CONNECT_BLE_TIMEOUT	20	/* Connect timeout in seconds. */
#define STORE_MAC_REVERSE	1
#define ACCEPT_NONSEP_MAC	1

#define CONNECT_RFCOMM_TRIES	3
#define CONNECT_RFCOMM_RETRY_MS	100

/* Silence warning about (currently) unused routine. */
#define WITH_WRITE_TYPE_HANDLE	0

/* {{{ compat decls */
/*
 * The availability of conversion helpers in <bluetooth/bluetooth.h>
 * appears to be version dependent. Let's provide the helper here if
 * the header doesn't.
 */

/* }}} compat decls */
/* {{{ Linux socket specific decls */

#define BLE_ATT_ERROR_RESP		0x01
#define BLE_ATT_EXCHANGE_MTU_REQ	0x02
#define BLE_ATT_EXCHANGE_MTU_RESP	0x03
#define BLE_ATT_FIND_INFORMATION_REQ	0x04
#define BLE_ATT_FIND_INFORMATION_RESP	0x05
#define BLE_ATT_FIND_BY_TYPE_REQ	0x06
#define BLE_ATT_FIND_BY_TYPE_RESP	0x07
#define BLE_ATT_READ_BY_TYPE_REQ	0x08
#define BLE_ATT_READ_BY_TYPE_RESP	0x09
#define BLE_ATT_READ_REQ		0x0a
#define BLE_ATT_READ_RESP		0x0b
#define BLE_ATT_READ_BLOB_REQ		0x0c
#define BLE_ATT_READ_BLOB_RESP		0x0d
#define BLE_ATT_READ_MULTIPLE_REQ	0x0e
#define BLE_ATT_READ_MULTIPLE_RESP	0x0f
#define BLE_ATT_READ_BY_GROUP_REQ	0x10
#define BLE_ATT_READ_BY_GROUP_RESP	0x11
#define BLE_ATT_WRITE_REQ		0x12
#define BLE_ATT_WRITE_RESP		0x13
#define BLE_ATT_WRITE_CMD		0x16
#define BLE_ATT_HANDLE_NOTIFICATION	0x1b
#define BLE_ATT_HANDLE_INDICATION	0x1d
#define BLE_ATT_HANDLE_CONFIRMATION	0x1e
#define BLE_ATT_SIGNED_WRITE_CMD	0x52

/* }}} Linux socket specific decls */
/* {{{ conversion */

/*
 * Convert textual MAC presentation to array of bytes. In contrast to
 * BlueZ conversion, accept colon or dash separated input as well as a
 * dense format without separators (001122334455). We expect to use the
 * library in an environment where colons are not always available as a
 * separator in user provided specs, while users do want to use some
 * separator for readability.
 *
 * TODO Instead of doing the actual conversion here (and dealing with
 * BlueZ' internal byte order for device address bytes), we might as
 * well just transform the input string to an output string, and always
 * use the officially provided str2ba() conversion routine.
 */
static int sr_bt_mac_text_to_bytes(const char *text, uint8_t *buf)
{
	size_t len;
	long v;
	char *endp;
	char numbuf[3];

	len = 6;
	if (STORE_MAC_REVERSE)
		buf += len;
	endp = (char *)text;
	while (len && endp && *endp) {
		text = endp;
		if (ACCEPT_NONSEP_MAC) {
			numbuf[0] = endp[0];
			numbuf[1] = endp[0] ? endp[1] : '\0';
			numbuf[2] = '\0';
		}
		endp = NULL;
		v = strtol(ACCEPT_NONSEP_MAC ? numbuf : text, &endp, 16);
		if (!endp)
			break;
		if (*endp != ':' && *endp != '-' && *endp != '\0')
			break;
		if (v < 0 || v > 255)
			break;
		if (STORE_MAC_REVERSE)
			*(--buf) = v;
		else
			*buf++ = v;
		len--;
		if (ACCEPT_NONSEP_MAC)
			endp = (char *)text + (endp - numbuf);
		if (*endp == ':' || *endp == '-')
			endp++;
	}

	if (len) {
		sr_err("Failed to parse MAC, too few bytes in '%s'", text);
		return -1;
	}
	while (isspace(*endp))
		endp++;
	if (*endp) {
		sr_err("Failed to parse MAC, excess data in '%s'", text);
		return -1;
	}

	return 0;
}

/* }}} conversion */
/* {{{ helpers */

SR_PRIV const char *sr_bt_adapter_get_address(size_t idx)
{
	int rc;
	struct hci_dev_info info;
	char addr[20];

	rc = hci_devinfo(idx, &info);
	sr_spew("DIAG: hci_devinfo(%zu) => rc %d", idx, rc);
	if (rc < 0)
		return NULL;

	rc = ba2str(&info.bdaddr, addr);
	sr_spew("DIAG: ba2str() => rc %d", rc);
	if (rc < 0)
		return NULL;

	return g_strdup(addr);
}

/* }}} helpers */
/* {{{ descriptor */

struct sr_bt_desc {
	/* User servicable options. */
	sr_bt_scan_cb scan_cb;
	void *scan_cb_data;
	sr_bt_data_cb data_cb;
	void *data_cb_data;
	char local_addr[20];
	char remote_addr[20];
	size_t rfcomm_channel;
	uint16_t read_handle;
	uint16_t write_handle;
	uint16_t cccd_handle;
	uint16_t cccd_value;
	/* Internal state. */
	int devid;
	int fd;
	struct hci_filter orig_filter;
};

static int sr_bt_desc_open(struct sr_bt_desc *desc, int *id_ref);
static void sr_bt_desc_close(struct sr_bt_desc *desc);
static int sr_bt_check_socket_usable(struct sr_bt_desc *desc);
static ssize_t sr_bt_write_type(struct sr_bt_desc *desc, uint8_t type);
#if WITH_WRITE_TYPE_HANDLE
static ssize_t sr_bt_write_type_handle(struct sr_bt_desc *desc,
	uint8_t type, uint16_t handle);
#endif
static ssize_t sr_bt_write_type_handle_bytes(struct sr_bt_desc *desc,
	uint8_t type, uint16_t handle, const uint8_t *data, size_t len);
static ssize_t sr_bt_char_write_req(struct sr_bt_desc *desc,
	uint16_t handle, const void *data, size_t len);

SR_PRIV struct sr_bt_desc *sr_bt_desc_new(void)
{
	struct sr_bt_desc *desc;

	desc = g_malloc0(sizeof(*desc));
	if (!desc)
		return NULL;

	desc->devid = -1;
	desc->fd = -1;

	return desc;
}

SR_PRIV void sr_bt_desc_free(struct sr_bt_desc *desc)
{
	if (!desc)
		return;

	sr_bt_desc_close(desc);
	g_free(desc);
}

SR_PRIV int sr_bt_config_cb_scan(struct sr_bt_desc *desc,
	sr_bt_scan_cb cb, void *cb_data)
{
	if (!desc)
		return -1;

	desc->scan_cb = cb;
	desc->scan_cb_data = cb_data;

	return 0;
}

SR_PRIV int sr_bt_config_cb_data(struct sr_bt_desc *desc,
	sr_bt_data_cb cb, void *cb_data)
{
	if (!desc)
		return -1;

	desc->data_cb = cb;
	desc->data_cb_data = cb_data;

	return 0;
}

SR_PRIV int sr_bt_config_addr_local(struct sr_bt_desc *desc, const char *addr)
{
	bdaddr_t mac_bytes;
	int rc;

	if (!desc)
		return -1;

	if (!addr || !addr[0]) {
		desc->local_addr[0] = '\0';
		return 0;
	}

	rc = sr_bt_mac_text_to_bytes(addr, &mac_bytes.b[0]);
	if (rc < 0)
		return -1;

	rc = ba2str(&mac_bytes, desc->local_addr);
	if (rc < 0)
		return -1;

	return 0;
}

SR_PRIV int sr_bt_config_addr_remote(struct sr_bt_desc *desc, const char *addr)
{
	bdaddr_t mac_bytes;
	int rc;

	if (!desc)
		return -1;

	if (!addr || !addr[0]) {
		desc->remote_addr[0] = '\0';
		return 0;
	}

	rc = sr_bt_mac_text_to_bytes(addr, &mac_bytes.b[0]);
	if (rc < 0)
		return -1;

	rc = ba2str(&mac_bytes, desc->remote_addr);
	if (rc < 0)
		return -1;

	return 0;
}

SR_PRIV int sr_bt_config_rfcomm(struct sr_bt_desc *desc, size_t channel)
{
	if (!desc)
		return -1;

	desc->rfcomm_channel = channel;

	return 0;
}

SR_PRIV int sr_bt_config_notify(struct sr_bt_desc *desc,
	uint16_t read_handle, uint16_t write_handle,
	uint16_t cccd_handle, uint16_t cccd_value)
{

	if (!desc)
		return -1;

	desc->read_handle = read_handle;
	desc->write_handle = write_handle;
	desc->cccd_handle = cccd_handle;
	desc->cccd_value = cccd_value;

	return 0;
}

static int sr_bt_desc_open(struct sr_bt_desc *desc, int *id_ref)
{
	int id, sock;
	bdaddr_t mac;

	if (!desc)
		return -1;
	sr_dbg("BLE open");

	if (desc->local_addr[0]) {
		id = hci_devid(desc->local_addr);
	} else if (desc->remote_addr[0]) {
		str2ba(desc->remote_addr, &mac);
		id = hci_get_route(&mac);
	} else {
		id = hci_get_route(NULL);
	}
	if (id < 0) {
		sr_err("devid failed");
		return -1;
	}
	desc->devid = id;
	if (id_ref)
		*id_ref = id;

	sock = hci_open_dev(id);
	if (sock < 0) {
		perror("open HCI socket");
		return -1;
	}
	desc->fd = sock;

	return sock;
}

static void sr_bt_desc_close(struct sr_bt_desc *desc)
{
	if (!desc)
		return;

	sr_dbg("BLE close");
	if (desc->fd >= 0) {
		hci_close_dev(desc->fd);
		desc->fd = -1;
	}
	desc->devid = -1;
}

/* }}} descriptor */
/* {{{ scan */

#define EIR_NAME_COMPLETE	9

static int sr_bt_scan_prep(struct sr_bt_desc *desc)
{
	int rc;
	uint8_t type, owntype, filter;
	uint16_t ival, window;
	int timeout;
	uint8_t enable, dup;
	socklen_t slen;
	struct hci_filter scan_filter;

	if (!desc)
		return -1;

	/* TODO Replace magic values with symbolic identifiers. */
	type = 0x01;	/* LE public? */
	ival = htobs(0x0010);
	window = htobs(0x0010);
	owntype = 0x00;	/* any? */
	filter = 0x00;
	timeout = 1000;
	rc = hci_le_set_scan_parameters(desc->fd,
		type, ival, window, owntype, filter, timeout);
	if (rc < 0) {
		perror("set LE scan params");
		return -1;
	}

	enable = 1;
	dup = 1;
	timeout = 1000;
	rc = hci_le_set_scan_enable(desc->fd, enable, dup, timeout);
	if (rc < 0) {
		perror("set LE scan enable");
		return -1;
	}

	/* Save the current filter. For later restoration. */
	slen = sizeof(desc->orig_filter);
	rc = getsockopt(desc->fd, SOL_HCI, HCI_FILTER,
		&desc->orig_filter, &slen);
	if (rc < 0) {
		perror("getsockopt(HCI_FILTER)");
		return -1;
	}

	hci_filter_clear(&scan_filter);
	hci_filter_set_ptype(HCI_EVENT_PKT, &scan_filter);
	hci_filter_set_event(EVT_LE_META_EVENT, &scan_filter);
	rc = setsockopt(desc->fd, SOL_HCI, HCI_FILTER,
		&scan_filter, sizeof(scan_filter));
	if (rc < 0) {
		perror("setsockopt(HCI_FILTER)");
		return -1;
	}

	return 0;
}

static int sr_bt_scan_post(struct sr_bt_desc *desc)
{
	int rc;
	uint8_t enable, dup;
	int timeout;

	if (!desc)
		return -1;

	/* Restore previous HCI filter. */
	rc = setsockopt(desc->fd, SOL_HCI, HCI_FILTER,
		&desc->orig_filter, sizeof(desc->orig_filter));
	if (rc < 0) {
		perror("setsockopt(HCI_FILTER)");
		return -1;
	}

	enable = 0;
	dup = 1;
	timeout = 1000;
	rc = hci_le_set_scan_enable(desc->fd, enable, dup, timeout);
	if (rc < 0)
		return -1;

	return 0;
}

static int sr_bt_scan_proc(struct sr_bt_desc *desc,
	sr_bt_scan_cb scan_cb, void *cb_data,
	uint8_t *data, size_t dlen, le_advertising_info *info)
{
	uint8_t type;
	char addr[20];
	const char *name;

	(void)desc;

	type = data[0];
	if (type == EIR_NAME_COMPLETE) {
		ba2str(&info->bdaddr, addr);
		name = g_strndup((const char *)&data[1], dlen - 1);
		if (scan_cb)
			scan_cb(cb_data, addr, name);
		free((void *)name);
		return 0;
	}

	/* Unknown or unsupported type, ignore silently. */
	return 0;
}

SR_PRIV int sr_bt_scan_le(struct sr_bt_desc *desc, int duration)
{
	int rc;
	time_t deadline;
	uint8_t buf[HCI_MAX_EVENT_SIZE];
	ssize_t rdlen, rdpos;
	evt_le_meta_event *meta;
	le_advertising_info *info;
	uint8_t *dataptr;
	size_t datalen;

	if (!desc)
		return -1;
	sr_dbg("BLE scan (LE)");

	rc = sr_bt_desc_open(desc, NULL);
	if (rc < 0)
		return -1;

	rc = sr_bt_scan_prep(desc);
	if (rc < 0)
		return -1;

	deadline = time(NULL);
	deadline += duration;
	while (time(NULL) <= deadline) {

		if (sr_bt_check_socket_usable(desc) < 0)
			break;
		rdlen = sr_bt_read(desc, buf, sizeof(buf));
		if (rdlen < 0)
			break;
		if (!rdlen) {
			g_usleep(50000);
			continue;
		}
		if (rdlen < 1 + HCI_EVENT_HDR_SIZE)
			continue;
		meta = (void *)&buf[1 + HCI_EVENT_HDR_SIZE];
		rdlen -= 1 + HCI_EVENT_HDR_SIZE;
		if (meta->subevent != EVT_LE_ADVERTISING_REPORT)
			continue;
		info = (void *)&meta->data[1];
		sr_spew("evt: type %d, len %d", info->evt_type, info->length);
		if (!info->length)
			continue;

		rdpos = 0;
		while (rdpos < rdlen) {
			datalen = info->data[rdpos];
			dataptr = &info->data[1 + rdpos];
			if (rdpos + 1 + datalen > info->length)
				break;
			rdpos += 1 + datalen;
			rc = sr_bt_scan_proc(desc,
				desc->scan_cb, desc->scan_cb_data,
				dataptr, datalen, info);
			if (rc < 0)
				break;
		}
	}

	rc = sr_bt_scan_post(desc);
	if (rc < 0)
		return -1;

	sr_bt_desc_close(desc);

	return 0;
}

SR_PRIV int sr_bt_scan_bt(struct sr_bt_desc *desc, int duration)
{
	int dev_id, sock, rsp_max;
	long flags;
	inquiry_info *info;
	int inq_rc;
	size_t rsp_count, idx;
	char addr[20];
	char name[256];

	if (!desc)
		return -1;
	sr_dbg("BLE scan (BT)");

	sock = sr_bt_desc_open(desc, &dev_id);
	if (sock < 0)
		return -1;

	rsp_max = 255;
	info = g_malloc0(rsp_max * sizeof(*info));
	flags = 0 /* | IREQ_CACHE_FLUSH */;
	inq_rc = hci_inquiry(dev_id, duration, rsp_max, NULL, &info, flags);
	if (inq_rc < 0)
		perror("hci_inquiry");
	rsp_count = inq_rc;

	for (idx = 0; idx < rsp_count; idx++) {
		memset(addr, 0, sizeof(addr));
		ba2str(&info[idx].bdaddr, addr);
		memset(name, 0, sizeof(name));
		if (hci_read_remote_name(sock, &info[idx].bdaddr, sizeof(name), name, 0) < 0)
			snprintf(name, sizeof(name), "[unknown]");
		if (desc->scan_cb)
			desc->scan_cb(desc->scan_cb_data, addr, name);
	}
	g_free(info);

	sr_bt_desc_close(desc);

	return 0;
}

/* }}} scan */
/* {{{ connect/disconnect */

SR_PRIV int sr_bt_connect_ble(struct sr_bt_desc *desc)
{
	struct sockaddr_l2 sl2;
	bdaddr_t mac;
	int s, ret;
	gint64 deadline;

	if (!desc)
		return -1;
	if (!desc->remote_addr[0])
		return -1;
	sr_dbg("BLE connect, remote addr %s", desc->remote_addr);

	s = socket(AF_BLUETOOTH, SOCK_SEQPACKET, 0);
	if (s < 0) {
		perror("socket create");
		return s;
	}
	desc->fd = s;

	memset(&sl2, 0, sizeof(sl2));
	sl2.l2_family = AF_BLUETOOTH;
	sl2.l2_psm = 0;
	if (desc->local_addr[0])
		str2ba(desc->local_addr, &mac);
	else
		mac = *BDADDR_ANY;
	memcpy(&sl2.l2_bdaddr, &mac, sizeof(sl2.l2_bdaddr));
	sl2.l2_cid = L2CAP_FC_CONNLESS;
	sl2.l2_bdaddr_type = BDADDR_LE_PUBLIC;
	ret = bind(s, (void *)&sl2, sizeof(sl2));
	if (ret < 0) {
		perror("bind");
		return ret;
	}

	if (0) {
		struct bt_security buf = {
			.level = BT_SECURITY_LOW,
			.key_size = 0,
		};
		ret = setsockopt(s, SOL_BLUETOOTH, BT_SECURITY, &buf, sizeof(buf));
		if (ret < 0) {
			perror("setsockopt");
			return ret;
		}
	}

	deadline = g_get_monotonic_time();
	deadline += CONNECT_BLE_TIMEOUT * 1000 * 1000;
	str2ba(desc->remote_addr, &mac);
	memcpy(&sl2.l2_bdaddr, &mac, sizeof(sl2.l2_bdaddr));
	sl2.l2_bdaddr_type = BDADDR_LE_PUBLIC;
	ret = connect(s, (void *)&sl2, sizeof(sl2));
	/*
	 * Cope with "in progress" condition. Keep polling the status
	 * until connect() completes, then get the error by means of
	 * getsockopt(). See the connect(2) manpage for details.
	 */
	if (ret < 0 && errno == EINPROGRESS) {
		struct pollfd fds[1];
		uint32_t soerror;
		socklen_t solen;

		/* TODO
		 * We seem to get here ("connect in progress") even when
		 * the specified peer is not around at all. Which results
		 * in extended periods of time where nothing happens, and
		 * an application timeout seems to be required.
		 */
		sr_spew("in progress ...");

		do {
			memset(fds, 0, sizeof(fds));
			fds[0].fd = s;
			fds[0].events = POLLOUT;
			ret = poll(fds, ARRAY_SIZE(fds), -1);
			if (ret < 0) {
				perror("poll(OUT)");
				return ret;
			}
			if (!ret)
				continue;
			if (!(fds[0].revents & POLLOUT))
				continue;
			if (g_get_monotonic_time() >= deadline) {
				sr_warn("Connect attempt timed out");
				return SR_ERR_IO;
			}
		} while (1);
		memset(fds, 0, sizeof(fds));
		fds[0].fd = s;
		fds[0].events = POLLNVAL;
		ret = poll(fds, 1, 0);
		if (ret < 0) {
			perror("poll(INVAL)");
			return ret;
		}
		if (ret) {
			/* socket fd is invalid(?) */
			desc->fd = -1;
			close(s);
			return -1;
		}
		solen = sizeof(soerror);
		ret = getsockopt(s, SOL_SOCKET, SO_ERROR, &soerror, &solen);
		if (ret < 0) {
			perror("getsockopt(SO_ERROR)");
			return ret;
		}
		if (soerror) {
			/* connect(2) failed, SO_ERROR has the error code. */
			errno = soerror;
			perror("connect(PROGRESS)");
			return soerror;
		}

		/*
		 * TODO Get the receive MTU here?
		 * getsockopt(SOL_BLUETOOTH, BT_RCVMTU, u16);
		 */
	}
	if (ret < 0) {
		perror("connect");
		return ret;
	}

	return 0;
}

SR_PRIV int sr_bt_connect_rfcomm(struct sr_bt_desc *desc)
{
	struct sockaddr_rc addr;
	int i, fd, rc;

	if (!desc)
		return -1;
	if (!desc->remote_addr[0])
		return -1;
	sr_dbg("RFCOMM connect, remote addr %s, channel %zu",
		desc->remote_addr, desc->rfcomm_channel);

	if (!desc->rfcomm_channel)
		desc->rfcomm_channel = 1;

	memset(&addr, 0, sizeof(addr));
	addr.rc_family = AF_BLUETOOTH;
	str2ba(desc->remote_addr, &addr.rc_bdaddr);
	addr.rc_channel = desc->rfcomm_channel;

	/*
	 * There are cases where connect returns EBUSY if we are re-connecting
	 * to a device. Try multiple times to work around this issue.
	 */
	for (i = 0; i < CONNECT_RFCOMM_TRIES; i++) {
		fd = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
		if (fd < 0) {
			perror("socket");
			return -1;
		}

		rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
		if (rc >= 0) {
			sr_spew("connected");
			desc->fd = fd;
			return 0;
		} else if (rc < 0 && errno == EBUSY) {
			close(fd);
			g_usleep(CONNECT_RFCOMM_RETRY_MS * 1000);
		} else {
			close(fd);
			perror("connect");
			return -2;
		}
	}

	sr_err("Connect failed, device busy.");

	return -2;
}

SR_PRIV void sr_bt_disconnect(struct sr_bt_desc *desc)
{
	sr_dbg("BLE disconnect");

	if (!desc)
		return;
	sr_bt_desc_close(desc);
}

static int sr_bt_check_socket_usable(struct sr_bt_desc *desc)
{
	struct pollfd fds[1];
	int ret;

	if (!desc)
		return -1;
	if (desc->fd < 0)
		return -1;

	memset(fds, 0, sizeof(fds));
	fds[0].fd = desc->fd;
	fds[0].events = POLLERR | POLLHUP;
	ret = poll(fds, ARRAY_SIZE(fds), 0);
	if (ret < 0)
		return ret;
	if (!ret)
		return 0;
	if (fds[0].revents & POLLHUP)
		return -1;
	if (fds[0].revents & POLLERR)
		return -2;
	if (fds[0].revents & POLLNVAL)
		return -3;

	return 0;
}

/* }}} connect/disconnect */
/* {{{ indication/notification */

SR_PRIV int sr_bt_start_notify(struct sr_bt_desc *desc)
{
	uint8_t buf[sizeof(desc->cccd_value)];
	ssize_t wrlen;

	if (!desc)
		return -1;
	sr_dbg("BLE start notify");

	if (sr_bt_check_socket_usable(desc) < 0)
		return -2;

	write_u16le(buf, desc->cccd_value);
	wrlen = sr_bt_char_write_req(desc, desc->cccd_handle, buf, sizeof(buf));
	if (wrlen != sizeof(buf))
		return -2;

	return 0;
}

SR_PRIV int sr_bt_check_notify(struct sr_bt_desc *desc)
{
	uint8_t buf[1024];
	ssize_t rdlen;
	uint8_t packet_type;
	uint16_t packet_handle;
	uint8_t *packet_data;
	size_t packet_dlen;

	if (!desc)
		return -1;

	if (sr_bt_check_socket_usable(desc) < 0)
		return -2;

	/* Get another message from the Bluetooth socket. */
	rdlen = sr_bt_read(desc, buf, sizeof(buf));
	if (rdlen < 0)
		return -2;
	if (!rdlen)
		return 0;

	/* Get header fields and references to the payload data. */
	packet_type = 0x00;
	packet_handle = 0x0000;
	packet_data = NULL;
	packet_dlen = 0;
	if (rdlen >= 1)
		packet_type = buf[0];
	if (rdlen >= 3) {
		packet_handle = bt_get_le16(&buf[1]);
		packet_data = &buf[3];
		packet_dlen = rdlen - 3;
	}

	/* Dispatch according to the message type. */
	switch (packet_type) {
	case BLE_ATT_ERROR_RESP:
		sr_spew("read() len %zd, type 0x%02x (%s)", rdlen, buf[0], "error response");
		/* EMPTY */
		break;
	case BLE_ATT_WRITE_RESP:
		sr_spew("read() len %zd, type 0x%02x (%s)", rdlen, buf[0], "write response");
		/* EMPTY */
		break;
	case BLE_ATT_HANDLE_INDICATION:
		sr_spew("read() len %zd, type 0x%02x (%s)", rdlen, buf[0], "handle indication");
		sr_bt_write_type(desc, BLE_ATT_HANDLE_CONFIRMATION);
		if (packet_handle != desc->read_handle)
			return -4;
		if (!packet_data)
			return -4;
		if (!desc->data_cb)
			return 0;
		return desc->data_cb(desc->data_cb_data, packet_data, packet_dlen);
	case BLE_ATT_HANDLE_NOTIFICATION:
		sr_spew("read() len %zd, type 0x%02x (%s)", rdlen, buf[0], "handle notification");
		if (packet_handle != desc->read_handle)
			return -4;
		if (!packet_data)
			return -4;
		if (!desc->data_cb)
			return 0;
		return desc->data_cb(desc->data_cb_data, packet_data, packet_dlen);
	default:
		sr_spew("unsupported type 0x%02x", packet_type);
		return -3;
	}

	return 0;
}

/* }}} indication/notification */
/* {{{ read/write */

SR_PRIV ssize_t sr_bt_write(struct sr_bt_desc *desc,
	const void *data, size_t len)
{
	if (!desc)
		return -1;
	if (desc->fd < 0)
		return -1;

	if (sr_bt_check_socket_usable(desc) < 0)
		return -2;

	/* Send TX data to the writable characteristics for BLE UART services. */
	if (desc->write_handle)
		return sr_bt_char_write_req(desc, desc->write_handle, data, len);

	/* Send raw TX data to the RFCOMM socket for BT Classic channels. */
	return write(desc->fd, data, len);
}

static ssize_t sr_bt_write_type(struct sr_bt_desc *desc, uint8_t type)
{
	ssize_t wrlen;

	if (!desc)
		return -1;
	if (desc->fd < 0)
		return -1;

	if (sr_bt_check_socket_usable(desc) < 0)
		return -2;

	wrlen = write(desc->fd, &type, sizeof(type));
	if (wrlen < 0)
		return wrlen;
	if (wrlen < (ssize_t)sizeof(type))
		return -1;

	return 0;
}

#if WITH_WRITE_TYPE_HANDLE
static ssize_t sr_bt_write_type_handle(struct sr_bt_desc *desc,
	uint8_t type, uint16_t handle)
{
	return sr_bt_write_type_handle_bytes(desc, type, handle, NULL, 0);
}
#endif

static ssize_t sr_bt_write_type_handle_bytes(struct sr_bt_desc *desc,
	uint8_t type, uint16_t handle, const uint8_t *data, size_t len)
{
	uint8_t header[sizeof(uint8_t) + sizeof(uint16_t)];
	struct iovec iov[2] = {
		{ .iov_base = header, .iov_len = sizeof(header), },
		{ .iov_base = (void *)data, .iov_len = len, },
	};
	ssize_t wrlen;

	if (!desc)
		return -1;
	if (desc->fd < 0)
		return -1;

	if (sr_bt_check_socket_usable(desc) < 0)
		return -2;

	header[0] = type;
	write_u16le(&header[1], handle);

	if (data && len)
		wrlen = writev(desc->fd, iov, ARRAY_SIZE(iov));
	else
		wrlen = write(desc->fd, header, sizeof(header));

	if (wrlen < 0)
		return wrlen;
	if (wrlen < (ssize_t)sizeof(header))
		return -1;
	wrlen -= sizeof(header);

	return wrlen;
}

/* Returns negative upon error, or returns the number of _payload_ bytes written. */
static ssize_t sr_bt_char_write_req(struct sr_bt_desc *desc,
	uint16_t handle, const void *data, size_t len)
{
	return sr_bt_write_type_handle_bytes(desc, BLE_ATT_WRITE_REQ,
		handle, data, len);
}

SR_PRIV ssize_t sr_bt_read(struct sr_bt_desc *desc, void *data, size_t len)
{
	struct pollfd fds[1];
	int ret;
	ssize_t rdlen;

	if (!desc)
		return -1;
	if (desc->fd < 0)
		return -1;

	if (sr_bt_check_socket_usable(desc) < 0)
		return -2;

	memset(fds, 0, sizeof(fds));
	fds[0].fd = desc->fd;
	fds[0].events = POLLIN;
	ret = poll(fds, ARRAY_SIZE(fds), 0);
	if (ret < 0)
		return ret;
	if (!ret)
		return 0;
	if (!(fds[0].revents & POLLIN))
		return 0;

	rdlen = read(desc->fd, data, len);

	return rdlen;
}

/* }}} indication/notification */

#endif
