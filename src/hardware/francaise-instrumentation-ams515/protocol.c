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
SR_PRIV int francaise_instrumentation_ams515_send_raw(struct sr_serial_dev_inst *serial, const char *cmd, char *answer, gboolean echoed)
{
	int cmdlen = strlen(cmd);
	int i;

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
			return SR_ERR;
		}
	}


	/*
	 * Read until we get the prompt.
	 */
	for (i = 0; i < ANSWER_MAX-1 && answer[i] != '>'; i++) {
		if (serial_read_blocking(serial, &answer[i], 1, SERIAL_READ_TIMEOUT_MS) < 1) {
			sr_err("Unable to read cmd answer.");
			break;
		}
		sr_spew("send_raw(): answer: 0x%02x '%c'", answer[i], answer[i]);
		// skip CR/LF
		if (answer[i] == '\r' || answer[i] == '\n')
			i--;
		if (answer[i] == '>')
			break;
	}
	answer[i] = '\0';
	sr_spew("send_raw(): answer: '%s'", answer);

	if (!strcmp("Error!", answer))
		return SR_ERR;

	return SR_OK;
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
 * Send query command with integer parameter
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
	sr_spew("send_send_int(): sending %s", command);

	ret = francaise_instrumentation_ams515_send_raw(serial, command, answer, FALSE);
	return ret;
}

/**
 * Send query command with char parameter
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
	sr_spew("send_send_char(): sending %s", command);

	ret = francaise_instrumentation_ams515_send_raw(serial, command, answer, FALSE);
	return ret;
}
