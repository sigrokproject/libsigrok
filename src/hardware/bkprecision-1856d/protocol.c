/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2021 LUMERIIX
 * Copyright (C) 2024 Daniel Anselmi <danselmi@gmx.ch>
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

/* https://bkpmedia.s3.us-west-1.amazonaws.com/downloads/manuals/en-us/1856D_manual.pdf */

#include <config.h>
#include "protocol.h"

#define GATE_TIME_0     "G0\xD"
#define GATE_TIME_1     "G1\xD"
#define GATE_TIME_2     "G2\xD"
#define GATE_TIME_3     "G3\xD"

#define DATA_REQ        "D0\xD"

#define FUNCTION_A      "F0\xD"
#define FUNCTION_C      "F2\xD"

#define LENGHT_OF_CMD  3

struct gate_time_config_command
{
    const char *cmd;
    const char *info;
    gulong sleep_time;
};

static struct gate_time_config_command gate_time_config_commands[] = {
    {
        .cmd = GATE_TIME_0,
        .info = "sending gate time 0 (10ms)",
        .sleep_time = 40000
    }, {
        .cmd = GATE_TIME_1,
        .info = "sending gate time 1 (100ms)",
        .sleep_time = 80000
    }, {
        .cmd = GATE_TIME_2,
        .info = "sending gate time 2 (1s)",
        .sleep_time = 80000
    }, {
        .cmd = GATE_TIME_3,
        .info = "sending gate time 3 (10s)",
        .sleep_time = 800000
    }
};

static void bkprecision_1856d_send_input_sel(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	char *cmd;

	if (!sdi)
		return;

	devc = sdi->priv;
	serial = sdi->conn;
	if (!devc || !serial)
		return;

	if (devc->sel_input == InputA) {
		sr_spew("selecting input A");
		cmd = FUNCTION_A;
	} else {
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

static void bkprecision_1856d_chk_select_input(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	if (!sdi)
		return;

	if (!(devc = sdi->priv))
		return;

	if (devc->sel_input != devc->curr_sel_input)
		bkprecision_1856d_send_input_sel(sdi);
}

static void bkprecision_1856d_send_gate_time(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	struct gate_time_config_command *cfg;

	if (!sdi)
		return;

	devc = sdi->priv;
	serial = sdi->conn;
	if (!devc || !serial)
		return;

	cfg = &(gate_time_config_commands[devc->gate_time]);

	sr_info("%s", cfg->info);

	if (serial_write_blocking(serial, cfg->cmd, LENGHT_OF_CMD,
			serial_timeout(serial, LENGHT_OF_CMD)) < 1) {
		sr_err("unable to send gate time command");
	}
	g_usleep(cfg->sleep_time);
}

static void bkprecision_1856d_request_data(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;

	if (!sdi)
		return;

	devc = sdi->priv;
	serial = sdi->conn;
	if (!devc || !serial)
		return;

	sr_spew("requesting data");

	if (serial_write_blocking(serial, DATA_REQ, LENGHT_OF_CMD,
			serial_timeout(serial, LENGHT_OF_CMD)) < 1) {
		sr_err("unable to send request data command");
	}
}

SR_PRIV void bkprecision_1856d_init(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;

	if (!sdi)
		return;

	devc = sdi->priv;
	serial = sdi->conn;
	if (!devc || !serial)
		return;

	devc->buffer_level = 0;
	sr_sw_limits_acquisition_start(&(devc->sw_limits));
	serial_flush(serial);

	bkprecision_1856d_send_input_sel(sdi);

	bkprecision_1856d_send_gate_time(sdi);
	bkprecision_1856d_request_data(sdi);
}

static int bkprecision_1856d_check_for_zero_message(struct dev_context *devc)
{
	int zero_found;
	int has_data;
	int idx;

	zero_found = 0;
	has_data = 0;

	for (idx = 0; idx < BKPRECISION1856D_MSG_SIZE - 1; ++idx) {
		if (devc->buffer[idx] == ' ') {
			continue;
		} else if (devc->buffer[idx] == '0' && zero_found == 0) {
			zero_found = 1;
			continue;
		} else {
			has_data = 1;
			break;
		}
	}
	return !has_data;
}

static void bkprecision_1856d_send_packet(
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

static void bkprecision_1856d_parse_message(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;

	double freq_value;
	int digits;
	char *endPtr, *dotPtr;

	if (!sdi->priv || !sdi->conn)
		return;

	devc = sdi->priv;
	serial = sdi->conn;

	/* check for cr at end of message */
	if (devc->buffer[BKPRECISION1856D_MSG_SIZE - 1] != '\xD') {
		sr_err("expected cr at end of message.");
		devc->buffer_level = 0;
		serial_flush(serial);
		bkprecision_1856d_send_input_sel(sdi);
		bkprecision_1856d_send_gate_time(sdi);
		bkprecision_1856d_request_data(sdi);
		return;
	}

	devc->buffer[BKPRECISION1856D_MSG_SIZE - 1] = 0; /* set trailing zero */

	if (bkprecision_1856d_check_for_zero_message(devc)) {
		sr_spew("received an empty packet");
		devc->buffer_level = 0;
		bkprecision_1856d_request_data(sdi);
		return;
	}

	freq_value = strtod(devc->buffer, &endPtr);

	if (strcmp(devc->buffer + BKPRECISION1856D_MSG_NUMBER_SIZE + 1, "Hz ")) {
		sr_err("not a frequency returned");
		devc->buffer_level = 0;
		bkprecision_1856d_send_input_sel(sdi);
		bkprecision_1856d_send_gate_time(sdi);
		bkprecision_1856d_request_data(sdi);
		return;
	}

	dotPtr = strchr(devc->buffer, '.');
	if (dotPtr)
		digits = endPtr - (dotPtr+1);
	else
		digits = endPtr - devc->buffer;

	if (devc->buffer[BKPRECISION1856D_MSG_NUMBER_SIZE] == 'M') {
		freq_value *= 1e6;
		digits -= 6;
	}
	if (devc->buffer[BKPRECISION1856D_MSG_NUMBER_SIZE] == 'k') {
		freq_value *= 1e3;
		digits -= 3;
	}

	bkprecision_1856d_send_packet(sdi, freq_value, digits);

	sr_sw_limits_update_samples_read(&(devc->sw_limits), 1);

	if (!sr_sw_limits_check(&(devc->sw_limits))) {
		devc->buffer_level = 0;
		bkprecision_1856d_chk_select_input(sdi);
		bkprecision_1856d_send_gate_time(sdi);
		bkprecision_1856d_request_data(sdi);
	}
	else
		sr_dev_acquisition_stop(sdi);
}

SR_PRIV int bkprecision_1856d_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	int len;

	(void)fd;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	if (revents != G_IO_IN) {
		/* Timeout, in rare cases the bk1856 doesn't respond anymore
		   (probably timing on rs232).
		   For now, just restart the measurement.*/
		bkprecision_1856d_send_gate_time(sdi);
		bkprecision_1856d_request_data(sdi);

		return TRUE;
	}

	if (!sdi->conn)
		return TRUE;
	serial = sdi->conn;

	len = serial_read_nonblocking(serial, devc->buffer + devc->buffer_level,
			BKPRECISION1856D_MSG_SIZE - devc->buffer_level );

	if (len < 1)
		return TRUE;
	devc->buffer_level += len;
	if (devc->buffer_level == BKPRECISION1856D_MSG_SIZE)
		bkprecision_1856d_parse_message(sdi);

	return TRUE;
}

SR_PRIV void bkprecision_1856d_set_gate_time(struct dev_context *devc, int time)
{
	devc->gate_time = time;
}

SR_PRIV void bkprecision_1856d_select_input(struct dev_context *devc,
											int intput)
{
	devc->sel_input = intput;
}
