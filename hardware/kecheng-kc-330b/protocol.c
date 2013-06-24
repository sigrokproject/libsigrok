/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Bert Vermeulen <bert@biot.com>
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

#include "protocol.h"

SR_PRIV int kecheng_kc_330b_handle_events(int fd, int revents, void *cb_data)
{
	const struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct timeval tv;

	(void)fd;
	(void)revents;

	sdi = cb_data;
	devc = sdi->priv;


	return TRUE;
}

SR_PRIV int kecheng_kc_330b_configure(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	int len, ret;
	unsigned char buf[7];

	sr_dbg("Configuring device.");

	usb = sdi->conn;
	devc = sdi->priv;

	buf[0] = CMD_CONFIGURE;
	buf[1] = devc->sample_interval;
	buf[2] = devc->alarm_low;
	buf[3] = devc->alarm_high;
	buf[4] = devc->mqflags & SR_MQFLAG_SPL_TIME_WEIGHT_F ? 0 : 1;
	buf[5] = devc->mqflags & SR_MQFLAG_SPL_FREQ_WEIGHT_A ? 0 : 1;
	buf[6] = devc->data_source;
	ret = libusb_bulk_transfer(usb->devhdl, EP_OUT, buf, 7, &len, 5);
	if (ret != 0 || len != 7) {
		sr_dbg("Failed to configure device: %s", libusb_error_name(ret));
		return SR_ERR;
	}

	/* The configure command ack takes about 32ms to come in. */
	ret = libusb_bulk_transfer(usb->devhdl, EP_IN, buf, 1, &len, 40);
	if (ret != 0 || len != 1) {
		sr_dbg("Failed to configure device (no ack): %s", libusb_error_name(ret));
		return SR_ERR;
	}
	if (buf[0] != (CMD_CONFIGURE | 0x80)) {
		sr_dbg("Failed to configure device: invalid response 0x%2.x", buf[0]);
		return SR_ERR;
	}

	return SR_OK;
}

SR_PRIV int kecheng_kc_330b_set_date_time(struct sr_dev_inst *sdi)
{
	struct sr_usb_dev_inst *usb;
	GDateTime *dt;
	int len, ret;
	unsigned char buf[7];

	sr_dbg("Setting device date/time.");

	usb = sdi->conn;

	dt = g_date_time_new_now_local();
	buf[0] = CMD_SET_DATE_TIME;
	buf[1] = g_date_time_get_year(dt) - 2000;
	buf[2] = g_date_time_get_month(dt);
	buf[3] = g_date_time_get_day_of_month(dt);
	buf[4] = g_date_time_get_hour(dt);
	buf[5] = g_date_time_get_minute(dt);
	buf[6] = g_date_time_get_second(dt);
	g_date_time_unref(dt);
	ret = libusb_bulk_transfer(usb->devhdl, EP_OUT, buf, 7, &len, 5);
	if (ret != 0 || len != 7) {
		sr_dbg("Failed to set date/time: %s", libusb_error_name(ret));
		return SR_ERR;
	}

	ret = libusb_bulk_transfer(usb->devhdl, EP_IN, buf, 1, &len, 10);
	if (ret != 0 || len != 1) {
		sr_dbg("Failed to set date/time (no ack): %s", libusb_error_name(ret));
		return SR_ERR;
	}
	if (buf[0] != (CMD_SET_DATE_TIME | 0x80)) {
		sr_dbg("Failed to set date/time: invalid response 0x%2.x", buf[0]);
		return SR_ERR;
	}



	return SR_OK;
}

SR_PRIV int kecheng_kc_330b_recording_get(const struct sr_dev_inst *sdi,
		gboolean *tmp)
{

	return SR_OK;
}
