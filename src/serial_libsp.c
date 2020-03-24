/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2010-2012 Bert Vermeulen <bert@biot.com>
 * Copyright (C) 2010-2012 Uwe Hermann <uwe@hermann-uwe.de>
 * Copyright (C) 2012 Alexandru Gagniuc <mr.nuke.me@gmail.com>
 * Copyright (C) 2014 Uffe Jakobsen <uffe@uffe.org>
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
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#ifdef HAVE_LIBSERIALPORT
#include <libserialport.h>
#endif
#ifdef G_OS_WIN32
#include <windows.h> /* for HANDLE */
#endif

#define LOG_PREFIX "serial-libsp"

/**
 * @file
 *
 * Serial port handling, wraps the external libserialport dependency.
 */

#ifdef HAVE_LIBSERIALPORT

/**
 * @defgroup grp_serial_libsp Serial port handling, libserialport group
 *
 * Serial port handling functions, based on libserialport.
 *
 * @{
 */

static int sr_ser_libsp_open(struct sr_serial_dev_inst *serial, int flags)
{
	int ret;
	char *error;
	int sp_flags;

	sp_get_port_by_name(serial->port, &serial->sp_data);

	sp_flags = 0;
	if (flags & SERIAL_RDWR)
		sp_flags = (SP_MODE_READ | SP_MODE_WRITE);
	else if (flags & SERIAL_RDONLY)
		sp_flags = SP_MODE_READ;

	ret = sp_open(serial->sp_data, sp_flags);

	switch (ret) {
	case SP_ERR_ARG:
		sr_err("Attempt to open serial port with invalid parameters.");
		return SR_ERR_ARG;
	case SP_ERR_FAIL:
		error = sp_last_error_message();
		sr_err("Error opening port (%d): %s.",
			sp_last_error_code(), error);
		sp_free_error_message(error);
		return SR_ERR;
	}

	return SR_OK;
}

static int sr_ser_libsp_close(struct sr_serial_dev_inst *serial)
{
	int ret;
	char *error;

	if (!serial->sp_data) {
		sr_dbg("Cannot close unopened serial port %s.", serial->port);
		return SR_ERR;
	}

	ret = sp_close(serial->sp_data);

	switch (ret) {
	case SP_ERR_ARG:
		sr_err("Attempt to close an invalid serial port.");
		return SR_ERR_ARG;
	case SP_ERR_FAIL:
		error = sp_last_error_message();
		sr_err("Error closing port (%d): %s.",
			sp_last_error_code(), error);
		sp_free_error_message(error);
		return SR_ERR;
	}

	sp_free_port(serial->sp_data);
	serial->sp_data = NULL;

	return SR_OK;
}

static int sr_ser_libsp_flush(struct sr_serial_dev_inst *serial)
{
	int ret;
	char *error;

	if (!serial->sp_data) {
		sr_dbg("Cannot flush unopened serial port %s.", serial->port);
		return SR_ERR;
	}

	ret = sp_flush(serial->sp_data, SP_BUF_BOTH);

	switch (ret) {
	case SP_ERR_ARG:
		sr_err("Attempt to flush an invalid serial port.");
		return SR_ERR_ARG;
	case SP_ERR_FAIL:
		error = sp_last_error_message();
		sr_err("Error flushing port (%d): %s.",
			sp_last_error_code(), error);
		sp_free_error_message(error);
		return SR_ERR;
	}

	return SR_OK;
}

static int sr_ser_libsp_drain(struct sr_serial_dev_inst *serial)
{
	int ret;
	char *error;

	if (!serial->sp_data) {
		sr_dbg("Cannot drain unopened serial port %s.", serial->port);
		return SR_ERR;
	}

	ret = sp_drain(serial->sp_data);

	if (ret == SP_ERR_FAIL) {
		error = sp_last_error_message();
		sr_err("Error draining port (%d): %s.",
			sp_last_error_code(), error);
		sp_free_error_message(error);
		return SR_ERR;
	}

	return SR_OK;
}

static int sr_ser_libsp_write(struct sr_serial_dev_inst *serial,
	const void *buf, size_t count,
	int nonblocking, unsigned int timeout_ms)
{
	ssize_t ret;
	char *error;

	if (!serial->sp_data) {
		sr_dbg("Cannot use unopened serial port %s.", serial->port);
		return SR_ERR;
	}

	if (nonblocking)
		ret = sp_nonblocking_write(serial->sp_data, buf, count);
	else
		ret = sp_blocking_write(serial->sp_data, buf, count, timeout_ms);

	switch (ret) {
	case SP_ERR_ARG:
		sr_err("Attempted serial port write with invalid arguments.");
		return SR_ERR_ARG;
	case SP_ERR_FAIL:
		error = sp_last_error_message();
		sr_err("Write error (%d): %s.", sp_last_error_code(), error);
		sp_free_error_message(error);
		return SR_ERR;
	}

	return ret;
}

static int sr_ser_libsp_read(struct sr_serial_dev_inst *serial,
	void *buf, size_t count,
	int nonblocking, unsigned int timeout_ms)
{
	ssize_t ret;
	char *error;

	if (!serial->sp_data) {
		sr_dbg("Cannot use unopened serial port %s.", serial->port);
		return SR_ERR;
	}

	if (nonblocking)
		ret = sp_nonblocking_read(serial->sp_data, buf, count);
	else
		ret = sp_blocking_read(serial->sp_data, buf, count, timeout_ms);

	switch (ret) {
	case SP_ERR_ARG:
		sr_err("Attempted serial port read with invalid arguments.");
		return SR_ERR_ARG;
	case SP_ERR_FAIL:
		error = sp_last_error_message();
		sr_err("Read error (%d): %s.", sp_last_error_code(), error);
		sp_free_error_message(error);
		return SR_ERR;
	}

	return ret;
}

static int sr_ser_libsp_set_params(struct sr_serial_dev_inst *serial,
	int baudrate, int bits, int parity, int stopbits,
	int flowcontrol, int rts, int dtr)
{
	int ret;
	char *error;
	struct sp_port_config *config;
	int cts, xonoff;

	if (!serial->sp_data) {
		sr_dbg("Cannot configure unopened serial port %s.", serial->port);
		return SR_ERR;
	}

	sp_new_config(&config);
	sp_set_config_baudrate(config, baudrate);
	sp_set_config_bits(config, bits);
	switch (parity) {
	case 0:
		sp_set_config_parity(config, SP_PARITY_NONE);
		break;
	case 1:
		sp_set_config_parity(config, SP_PARITY_EVEN);
		break;
	case 2:
		sp_set_config_parity(config, SP_PARITY_ODD);
		break;
	default:
		return SR_ERR_ARG;
	}
	sp_set_config_stopbits(config, stopbits);
	rts = flowcontrol == 1 ? SP_RTS_FLOW_CONTROL : rts;
	sp_set_config_rts(config, rts);
	cts = flowcontrol == 1 ? SP_CTS_FLOW_CONTROL : SP_CTS_IGNORE;
	sp_set_config_cts(config, cts);
	sp_set_config_dtr(config, dtr);
	sp_set_config_dsr(config, SP_DSR_IGNORE);
	xonoff = flowcontrol == 2 ? SP_XONXOFF_INOUT : SP_XONXOFF_DISABLED;
	sp_set_config_xon_xoff(config, xonoff);

	ret = sp_set_config(serial->sp_data, config);
	sp_free_config(config);

	switch (ret) {
	case SP_ERR_ARG:
		sr_err("Invalid arguments for setting serial port parameters.");
		return SR_ERR_ARG;
	case SP_ERR_FAIL:
		error = sp_last_error_message();
		sr_err("Error setting serial port parameters (%d): %s.",
			sp_last_error_code(), error);
		sp_free_error_message(error);
		return SR_ERR;
	}

	return SR_OK;
}

#ifdef G_OS_WIN32
typedef HANDLE event_handle;
#else
typedef int event_handle;
#endif

static int sr_ser_libsp_source_add_int(struct sr_serial_dev_inst *serial,
	int events,
	void **keyptr, gintptr *fdptr, unsigned int *pollevtptr)
{
	struct sp_event_set *event_set;
	gintptr poll_fd;
	unsigned int poll_events;
	enum sp_event mask;

	if ((events & (G_IO_IN | G_IO_ERR)) && (events & G_IO_OUT)) {
		sr_err("Cannot poll input/error and output simultaneously.");
		return SR_ERR_ARG;
	}
	if (!serial->sp_data) {
		sr_err("Invalid serial port.");
		return SR_ERR_ARG;
	}

	if (sp_new_event_set(&event_set) != SP_OK)
		return SR_ERR;

	mask = 0;
	if (events & G_IO_IN)
		mask |= SP_EVENT_RX_READY;
	if (events & G_IO_OUT)
		mask |= SP_EVENT_TX_READY;
	if (events & G_IO_ERR)
		mask |= SP_EVENT_ERROR;

	if (sp_add_port_events(event_set, serial->sp_data, mask) != SP_OK) {
		sp_free_event_set(event_set);
		return SR_ERR;
	}
	if (event_set->count != 1) {
		sr_err("Unexpected number (%u) of event handles to poll.",
			event_set->count);
		sp_free_event_set(event_set);
		return SR_ERR;
	}

	poll_fd = (gintptr) ((event_handle *)event_set->handles)[0];
	mask = event_set->masks[0];

	sp_free_event_set(event_set);

	poll_events = 0;
	if (mask & SP_EVENT_RX_READY)
		poll_events |= G_IO_IN;
	if (mask & SP_EVENT_TX_READY)
		poll_events |= G_IO_OUT;
	if (mask & SP_EVENT_ERROR)
		poll_events |= G_IO_ERR;

	/*
	 * Using serial->sp_data as the key for the event source is not quite
	 * proper, as it makes it impossible to create another event source
	 * for the same serial port. However, these fixed keys will soon be
	 * removed from the API anyway, so this is OK for now.
	 */
	*keyptr = serial->sp_data;
	*fdptr = poll_fd;
	*pollevtptr = poll_events;

	return SR_OK;
}

static int sr_ser_libsp_source_add(struct sr_session *session,
	struct sr_serial_dev_inst *serial, int events, int timeout,
	sr_receive_data_callback cb, void *cb_data)
{
	int ret;
	void *key;
	gintptr poll_fd;
	unsigned int poll_events;

	ret = sr_ser_libsp_source_add_int(serial, events,
		&key, &poll_fd, &poll_events);
	if (ret != SR_OK)
		return ret;

	return sr_session_fd_source_add(session,
		key, poll_fd, poll_events,
		timeout, cb, cb_data);
}

static int sr_ser_libsp_source_remove(struct sr_session *session,
	struct sr_serial_dev_inst *serial)
{
	void *key;

	key = serial->sp_data;
	return sr_session_source_remove_internal(session, key);
}

static GSList *sr_ser_libsp_list(GSList *list, sr_ser_list_append_t append)
{
	struct sp_port **ports;
	size_t i;
	const char *name;
	const char *desc;

	if (sp_list_ports(&ports) != SP_OK)
		return list;

	for (i = 0; ports[i]; i++) {
		name = sp_get_port_name(ports[i]);
		desc = sp_get_port_description(ports[i]);
		list = append(list, name, desc);
	}

	sp_free_port_list(ports);

	return list;
}

static GSList *sr_ser_libsp_find_usb(GSList *list, sr_ser_find_append_t append,
	uint16_t vendor_id, uint16_t product_id)
{
	struct sp_port **ports;
	int i, vid, pid;

	if (sp_list_ports(&ports) != SP_OK)
		return list;

	for (i = 0; ports[i]; i++) {
		if (sp_get_port_transport(ports[i]) != SP_TRANSPORT_USB)
			continue;
		if (sp_get_port_usb_vid_pid(ports[i], &vid, &pid) != SP_OK)
			continue;
		if (vendor_id && vid != vendor_id)
			continue;
		if (product_id && pid != product_id)
			continue;
		list = append(list, sp_get_port_name(ports[i]));
	}

	sp_free_port_list(ports);

	return list;
}

static int sr_ser_libsp_get_frame_format(struct sr_serial_dev_inst *serial,
	int *baud, int *bits)
{
	struct sp_port_config *config;
	int ret, tmp;
	enum sp_parity parity;

	if (sp_new_config(&config) < 0)
		return SR_ERR_MALLOC;
	*baud = *bits = 0;
	ret = SR_OK;
	do {
		if (sp_get_config(serial->sp_data, config) < 0) {
			ret = SR_ERR_NA;
			break;
		}

		if (sp_get_config_baudrate(config, &tmp) < 0) {
			ret = SR_ERR_NA;
			break;
		}
		*baud = tmp;

		*bits += 1;	/* Start bit. */
		if (sp_get_config_bits(config, &tmp) < 0) {
			ret = SR_ERR_NA;
			break;
		}
		*bits += tmp;
		if (sp_get_config_parity(config, &parity) < 0) {
			ret = SR_ERR_NA;
			break;
		}
		*bits += (parity != SP_PARITY_NONE) ? 1 : 0;
		if (sp_get_config_stopbits(config, &tmp) < 0) {
			ret = SR_ERR_NA;
			break;
		}
		*bits += tmp;
	} while (FALSE);
	sp_free_config(config);

	return ret;
}

static size_t sr_ser_libsp_get_rx_avail(struct sr_serial_dev_inst *serial)
{
	int rc;

	if (!serial)
		return 0;

	rc = sp_input_waiting(serial->sp_data);
	if (rc < 0)
		return 0;

	return rc;
}

static struct ser_lib_functions serlib_sp = {
	.open = sr_ser_libsp_open,
	.close = sr_ser_libsp_close,
	.flush = sr_ser_libsp_flush,
	.drain = sr_ser_libsp_drain,
	.write = sr_ser_libsp_write,
	.read = sr_ser_libsp_read,
	.set_params = sr_ser_libsp_set_params,
	.setup_source_add = sr_ser_libsp_source_add,
	.setup_source_remove = sr_ser_libsp_source_remove,
	.list = sr_ser_libsp_list,
	.find_usb = sr_ser_libsp_find_usb,
	.get_frame_format = sr_ser_libsp_get_frame_format,
	.get_rx_avail = sr_ser_libsp_get_rx_avail,
};
SR_PRIV struct ser_lib_functions *ser_lib_funcs_libsp = &serlib_sp;

#else

SR_PRIV struct ser_lib_functions *ser_lib_funcs_libsp = NULL;

#endif
