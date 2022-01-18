/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2022 François Revol <revol@free.fr>
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

SR_PRIV int francaise_instrumentation_ams515_receive_data(int fd, int revents, void *cb_data)
{
	const struct sr_dev_inst *sdi;
	struct dev_context *devc;

	(void)fd;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	if (revents == G_IO_IN) {
		/* TODO */
	}

	return TRUE;
}



/**
 * Send raw command
 *
 * @param[in] cmd Raw command string.
 * @param[out] answer Answer to the command (Buffer of ANSWER_MAX char).
 * @param[in] echoed Assume device is in echo mode, wait for sent chars to read back.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_ARG Invalid argument.
 * @retval SR_ERR Error.
 */
SR_PRIV int francaise_instrumentation_ams515_send_raw(const struct sr_serial_dev_inst *serial, const char *cmd, char *answer, gboolean echoed)
{
	int cmdlen = strlen(cmd);
	int i, ret = SR_ERR_IO;
	gboolean resync = FALSE;

	if (!serial)
		return SR_ERR_ARG;

	sr_spew("send_raw(): '%s'", cmd);
	memset(answer, '\0', ANSWER_MAX);

	/*
	 * The device seems to have an echo mode that can be disabled.
	 *
	 * When echo is on (default), spamming commands as full lines confuses it,
	 * probably because it misses characters while echoing the previous one back.
	 * Also we can't be sure if echo is on without trying to send a command,
	 * so we try to read back the character we just sent.
	 *
	 * When echo is off, we could send the line directly, but we must parse the answer
	 * manually since we don't get CR or LF anymore, just the > prompt character.
	 * But then we avoid trying to read back possible echo because we'll timeout,
	 * which slows down communication.
	 *
	 * So we first assume echo is on, but disable it in dev_open, and parse the answer
	 * discarding CR/LF on the way.
	 */
	for (i = 0; i < cmdlen; i++) {
		char c;
		if (serial_write_blocking(serial, &cmd[i], 1, SERIAL_WRITE_TIMEOUT_MS) < 1) {
			sr_err("Write error for cmd[%d].", i);
			return SR_ERR;
		}
		// if we didn't get an echo of the first char,
		// assume no echo, and don't eat the result.
		if (!echoed)
			continue;
		// we do not know if echo is on, so we try to read
		if (serial_read_blocking(serial, &c, 1, SERIAL_READ_TIMEOUT_MS) < 1) {
			sr_err("Unable to read echoed cmd, assuming no echo.");
			echoed = FALSE;
			continue;
		}
		echoed = TRUE;
		if (c != cmd[i]) {
			sr_err("Mismatched echoed cmd: %c != %c", c, cmd[i]);
			// actually keep going, if we ever want to resync properly
			//return SR_ERR;
		}
	}


	/*
	 * Read until we get the prompt.
	 */
	for (i = 0; i < ANSWER_MAX-1; i++) {
		if (serial_read_blocking(serial, &answer[i], 1, SERIAL_READ_TIMEOUT_MS) < 1) {
			sr_err("Unable to read cmd answer.");
			break;
		}
		sr_spew("send_raw(): answer: 0x%02x '%c'", answer[i], answer[i]);
		// skip CR/LF
		if (answer[i] == '\r' || answer[i] == '\n') {
			if (!echoed) {
				// We shouldn't get CR/LF in no-echo mode.
				// Likely the device was power-cycled, switch it back to no-echo mode
				sr_dbg("CR/LF found reply in no-echo mode!");
				resync = TRUE;
				break;
			}
			i--;
		}
		// The "I?" command returns a ">" in the answer, ain't it funny?
		if (answer[i] == '>' && (cmd[0] != 'I' || i > 0)) {
			// We got a prompt, so the command was handled.
			ret = SR_OK;
			break;
		}
	}
	answer[i] = '\0';
	sr_spew("send_raw(): answer: '%s'", answer);


	// Some error occured
	if (!strcmp("Error!", answer))
		return SR_ERR;

	// Argument is out of bounds
	if (!strcmp("Dep", answer))
		return SR_ERR_ARG;

	// Over-current
	if (!strcmp("Icc", answer))
		return SR_ERR;

	if (resync) {
		/*
		 * Something bad happened.
		 * Maybe the device was power-cycled, try to disable echo again.
		 */
		serial_flush(serial);
		francaise_instrumentation_ams515_send_raw(serial, "T\r", answer, TRUE);
		serial_flush(serial);
		// Assume this command failed
	}

	return ret;
}

/**
 * Set echo mode on the device
 *
 * @param[in] cmd Raw command char.
 * @param[in] param Echo mode to set.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_ARG Invalid argument.
 * @retval SR_ERR Error.
 */
SR_PRIV int francaise_instrumentation_ams515_set_echo(const struct sr_serial_dev_inst *serial, gboolean param)
{
	const char *cmd;
	char answer[ANSWER_MAX];
	int ret;

	if (!serial)
		return SR_ERR_ARG;

	serial_flush(serial);

	cmd = "T?\r";
	// Query current echo mode
	ret = francaise_instrumentation_ams515_send_raw(serial, cmd, answer, TRUE);
	if (ret < SR_OK)
		return ret;
	if (!param && !strcmp(answer, "00") || param && !strcmp(answer, "FF")) {
		cmd = "T\r";
		// Toggle echo mode to the one we want
		ret = francaise_instrumentation_ams515_send_raw(serial, cmd, answer, TRUE);
	}

	serial_flush(serial);

	return ret;
}

/**
 * Send query command with integer result
 *
 * @param[in] cmd Raw command char.
 * @param[out] result Pointer to result
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_ARG Invalid argument.
 * @retval SR_ERR Error.
 */
SR_PRIV int francaise_instrumentation_ams515_query_int(const struct sr_dev_inst *sdi, const char cmd, int *result)
{
	struct sr_serial_dev_inst *serial;
	char command[4];
	char answer[ANSWER_MAX];
	int ret;
	long res;

	if (!sdi)
		return SR_ERR_ARG;
	serial = sdi->conn;
	if (!serial || !result)
		return SR_ERR_ARG;

	snprintf(command, 4, "%c?\r", cmd);
	ret = francaise_instrumentation_ams515_send_raw(serial, command, answer, FALSE);
	if (ret < SR_OK)
		return ret;
	if (answer[0] != '+' && answer[0] != '-')
		return SR_ERR;
	res = strtoul(&answer[1], NULL, 16);
	*result = (int)(answer[0] == '-' ? -res : res);
	return SR_OK;
}

/**
 * Send query command with string result
 *
 * @param[in] cmd Raw command char.
 * @param[out] result Pointer to result char[ANSWER_MAX]
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_ARG Invalid argument.
 * @retval SR_ERR Error.
 */
SR_PRIV int francaise_instrumentation_ams515_query_str(const struct sr_dev_inst *sdi, const char cmd, char *result)
{
	struct sr_serial_dev_inst *serial;
	char command[4];
	int ret;
	long res;

	if (!sdi)
		return SR_ERR_ARG;
	serial = sdi->conn;
	if (!serial || !result)
		return SR_ERR_ARG;

	snprintf(command, 4, "%c?\r", cmd);
	ret = francaise_instrumentation_ams515_send_raw(serial, command, result, FALSE);
	if (ret < SR_OK)
		return ret;
	return SR_OK;
}

/**
 * Send command with integer parameter
 *
 * @param[in] cmd Raw command char.
 * @param[out] param Integer parameter.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_ARG Invalid argument.
 * @retval SR_ERR Error.
 */
SR_PRIV int francaise_instrumentation_ams515_send_int(const struct sr_dev_inst *sdi, const char cmd, int param)
{
	struct sr_serial_dev_inst *serial;
	char command[10];
	char answer[ANSWER_MAX];
	int ret;
	long res;

	if (!sdi)
		return SR_ERR_ARG;
	serial = sdi->conn;
	if (!serial)
		return SR_ERR_ARG;

	snprintf(command, 9, "%c%c%02.02X\r", cmd, param < 0 ? '-' : '+', abs(param));
	sr_spew("send_int(): sending %s", command);

	ret = francaise_instrumentation_ams515_send_raw(serial, command, answer, FALSE);
	return ret;
}

/**
 * Send command with char parameter
 *
 * @param[in] cmd Raw command char.
 * @param[out] param Character parameter.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_ARG Invalid argument.
 * @retval SR_ERR Error.
 */
SR_PRIV int francaise_instrumentation_ams515_send_char(const struct sr_dev_inst *sdi, const char cmd, char param)
{
	struct sr_serial_dev_inst *serial;
	char command[10];
	char answer[ANSWER_MAX];
	int ret;
	long res;

	if (!sdi)
		return SR_ERR_ARG;
	serial = sdi->conn;
	if (!serial)
		return SR_ERR_ARG;

	snprintf(command, 9, "%c%c\r", cmd, param);
	sr_spew("send_char(): sending %s", command);

	ret = francaise_instrumentation_ams515_send_raw(serial, command, answer, FALSE);
	return ret;
}
