/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2020 LUMERIIX/danselmi <.>
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

#define HOLD_OFF        "H0\xD"
#define HOLD_ON         "H1\xD"
#define HOLD_TOGGLE     "H2\xD"

#define GATE_TIME_0     "G0\xD"
#define GATE_TIME_1     "G1\xD"
#define GATE_TIME_2     "G2\xD"
#define GATE_TIME_3     "G3\xD"

#define DATA_REQ        "D0\xD"

#define FUNCTION_A      "F0\xD"
#define FUNCTION_C      "F2\xD"
#define FUNCTION_PERIOD "F3\xD"
#define FUNCTION_TOTAL  "F4\xD"
#define FUNCTION_RPM    "F5\xD"

#define REMOTE_OFF      "R0\xD"
#define REMOTE_ON       "R1\xD"
#define LENGHT_OF_CMD  3

static void bk_1856d_send_input_sel(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	char *cmd;

	if(!sdi)
		return;

	devc = sdi->priv;
	serial = sdi->conn;
	if(!devc || !serial)
		return;

	if(devc->sel_input == InputA)
	{
		sr_spew("selecting input A");
		cmd = FUNCTION_A;
	}
	else
	{
		sr_spew("selecting input C");
		cmd = FUNCTION_C;
	}

	if (serial_write_blocking(serial, cmd, LENGHT_OF_CMD,
			serial_timeout(serial, LENGHT_OF_CMD)) < 1) {
		sr_err("unable to send function %c command",
				devc->sel_input == InputA ? 'A' : 'C' );
	}

	devc->curr_sel_input = devc->sel_input;
}

static void bk_1856d_chk_select_input(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	if(!sdi)
		return;

	devc = sdi->priv;
	if(!devc)
		return;

	if(devc->sel_input != devc->curr_sel_input)
		bk_1856d_send_input_sel(sdi);
}

static void bk_1856d_send_gate_time(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	char *cmd;
	gulong microseconds;

	if(!sdi)
		return;

	devc = sdi->priv;
	serial = sdi->conn;
	if(!devc || !serial)
		return;

	if (devc->gate_time < 0) devc->gate_time = 0;
	if (devc->gate_time > 3) devc->gate_time = 3;
	switch(devc->gate_time) {
		default:
		case 0:
			cmd = GATE_TIME_0;
			sr_info("sending gate time 0 (10ms)");
			microseconds = 40000;
			break;
		case 1:
			cmd = GATE_TIME_1;
			sr_info("sending gate time 1 (100ms)");
			microseconds = 80000;
			break;
		case 2:
			cmd = GATE_TIME_2;
			sr_info("sending gate time 2 (1s)");
			microseconds = 80000;
			break;
		case 3:
			cmd = GATE_TIME_3;
			sr_info("sending gate time 3 (10s)");
			microseconds = 800000;
			break;
	}

	if (serial_write_blocking(serial, cmd, LENGHT_OF_CMD,
			serial_timeout(serial, LENGHT_OF_CMD)) < 1) {
		sr_err("unable to send gate time command");
	}
	g_usleep(microseconds);
}

static void bk_1856d_request_data(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;

	if(!sdi)
		return;

	devc = sdi->priv;
	serial = sdi->conn;
	if(!devc || !serial)
		return;

	sr_spew("requesting data");

	if (serial_write_blocking(serial, DATA_REQ, LENGHT_OF_CMD,
			serial_timeout(serial, LENGHT_OF_CMD)) < 1) {
		sr_err("unable to send request data command");
	}
}

SR_PRIV void bk_1856d_init(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;

	if(!sdi)
		return;

	devc = sdi->priv;
	serial = sdi->conn;
	if(!devc || !serial)
		return;

	devc->buffer_level = 0;
	sr_sw_limits_acquisition_start(&(devc->sw_limits));
	serial_flush(serial);

	g_mutex_lock(&devc->rw_mutex);
	bk_1856d_send_input_sel(sdi);

	bk_1856d_send_gate_time(sdi);
	bk_1856d_request_data(sdi);
	g_mutex_unlock(&devc->rw_mutex);
}

static int bk_1856d_check_for_zero_message(struct dev_context *devc)
{
	int zero_found;
	int has_data;

	zero_found = 0;
	has_data = 0;

	for(int idx = 0; idx < BK1856D_MSG_SIZE-1 ; ++idx)
	{
		if(devc->buffer[idx] == ' ')
			continue;
		else if(devc->buffer[idx] == '0' && zero_found == 0)
		{
			zero_found = 1;
			continue;
		}
		else
		{
			has_data = 1;
			break;
		}
	}
	return !has_data;
}

static void bk_1856d_send_packet(
	const struct sr_dev_inst *sdi, double freq_value, int digits)
{
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;

	sr_analog_init(&analog, &encoding, &meaning, &spec, digits);
	analog.meaning->mq = SR_MQ_FREQUENCY;
	analog.meaning->unit = SR_UNIT_HERTZ;
	analog.meaning->channels = sdi->channels;
	analog.num_samples = 1;
	analog.data = &freq_value;
	analog.encoding->unitsize = sizeof(freq_value);
	analog.encoding->is_float = TRUE;
	analog.encoding->digits = digits;

	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	sr_session_send(sdi, &packet);
}

static void bk_1856d_parse_message(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;

	double freq_value;
	int digits;
	char *endPtr, *dotPtr;

	if (!(devc = sdi->priv) || !(serial = sdi->conn))
		return;

	/* check for cr at end of message */
	if(devc->buffer[BK1856D_MSG_SIZE-1] != '\xD')
	{
		sr_err("expected cr at end of message.");
		devc->buffer_level = 0;
		serial_flush(serial);
		g_mutex_lock(&devc->rw_mutex);
		bk_1856d_send_input_sel(sdi);
		bk_1856d_send_gate_time(sdi);
		bk_1856d_request_data(sdi);
		g_mutex_unlock(&devc->rw_mutex);
		return;
	}

	devc->buffer[BK1856D_MSG_SIZE-1] = 0; /* set trailing zero */

	if(bk_1856d_check_for_zero_message(devc))
	{
		sr_spew("received 0 package");
		devc->buffer_level = 0;
		bk_1856d_request_data(sdi);
		return;
	}

	sr_dbg("received msg: '%s'", devc->buffer);
	freq_value = strtod(devc->buffer, &endPtr);
	sr_dbg("parsed value: %f", freq_value);

	if(strcmp(devc->buffer + BK1856D_MSG_NUMBER_SIZE+1, "Hz ") != 0)
	{
		sr_err("not a frequency returned");
		devc->buffer_level = 0;
		g_mutex_lock(&devc->rw_mutex);
		bk_1856d_send_input_sel(sdi);
		bk_1856d_send_gate_time(sdi);
		bk_1856d_request_data(sdi);
		g_mutex_unlock(&devc->rw_mutex);
		return;
	}

	dotPtr = strchr(devc->buffer, '.');
	if(dotPtr)
		digits = endPtr - (dotPtr+1);
	else
		digits = endPtr - devc->buffer;

	if(devc->buffer[BK1856D_MSG_NUMBER_SIZE] == 'M') {
		freq_value *= 1e6;
		digits -= 6;
	}
	if(devc->buffer[BK1856D_MSG_NUMBER_SIZE] == 'k') {
		freq_value *= 1e3;
		digits -= 3;
	}

	bk_1856d_send_packet(sdi, freq_value, digits);

	sr_sw_limits_update_samples_read(&(devc->sw_limits), 1);

	if(!sr_sw_limits_check(&(devc->sw_limits)))
	{
		devc->buffer_level = 0;
		g_mutex_lock(&devc->rw_mutex);
		bk_1856d_chk_select_input(sdi);
		bk_1856d_send_gate_time(sdi);
		bk_1856d_request_data(sdi);
		g_mutex_unlock(&devc->rw_mutex);
	}
	else
		sr_dev_acquisition_stop((struct sr_dev_inst *)sdi);
}

SR_PRIV int bk_1856d_receive_data(int fd, int revents, void *cb_data)
{
	const struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	int len;

	(void)fd;

	if (!(sdi = cb_data) || !(devc = sdi->priv))
		return TRUE;

	if (revents != G_IO_IN)
	{
		/* Timeout
		   in rare cases th bk1856 doesn't respond anymore
		   (probably timing on rs232)
		   for now, just restart the measurement. */
		sr_dbg("received timeout");
		g_mutex_lock(&devc->rw_mutex);
		bk_1856d_send_gate_time(sdi);
		bk_1856d_request_data(sdi);
		g_mutex_unlock(&devc->rw_mutex);

		return TRUE;
	}

	if (!(serial = sdi->conn))
		return TRUE;

	len = serial_read_nonblocking(serial, devc->buffer + devc->buffer_level,
			BK1856D_MSG_SIZE - devc->buffer_level );

	if (len < 1)
		return TRUE;
	devc->buffer_level += len;
	if(devc->buffer_level == BK1856D_MSG_SIZE)
		bk_1856d_parse_message(sdi);

	return TRUE;
}

SR_PRIV void bk_1856d_set_gate_time(struct dev_context *devc, int time)
{
	g_mutex_lock(&devc->rw_mutex);
	devc->gate_time = time;
	g_mutex_unlock(&devc->rw_mutex);
}

SR_PRIV void bk_1856d_select_input(struct dev_context *devc, int intput)
{
	g_mutex_lock(&devc->rw_mutex);
	devc->sel_input = intput;
	g_mutex_unlock(&devc->rw_mutex);
}

