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
	struct sr_serial_dev_inst *serial;
	struct dev_context *devc;
	char answer[ANSWER_MAX];
	int res;

	(void)fd;

	sr_dbg("receive_data() %d %d\n", fd, revents);

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	if (!(serial = sdi->conn))
		return TRUE;

	// We shouldn't be getting actual events here, just timeouts
	if (revents == 0/*G_IO_IN*/) {
		if (devc->resync) {
			/*
			 * Something bad happened.
			 * Maybe the device was power-cycled, try to disable echo again.
			 */
			devc->resync = FALSE;
			sr_dbg("Resyncing serial.");
			serial_flush(serial);
			francaise_instrumentation_ams515_send_raw(sdi, "T\r", answer, TRUE);
			serial_flush(serial);
			// Assume this command failed
		}
		/*
		 * First make sure we aren't over-current,
		 * else other commands won't work anyway.
		 */
		res = francaise_instrumentation_ams515_query_str(sdi, 'I', answer);
		sr_dbg("I? -> '%s'", answer);
		if (res == SR_OK) {
			if (!strcmp(answer, "Ok") && devc->overcurrent) {
				sr_dbg("End of overcurrent.");
				devc->overcurrent = FALSE;
				sr_session_send_meta(sdi,
					SR_CONF_OVER_CURRENT_PROTECTION_ACTIVE,
					g_variant_new_boolean(FALSE));
			} else if (answer[0] == '>') {
				// No need to check which channel at this point.
				sr_dbg("Notifying overcurrent.");
				// XXX: how do we tell on which channel group it happens anyway?
				sr_session_send_meta(sdi,
					SR_CONF_OVER_CURRENT_PROTECTION_ACTIVE,
					g_variant_new_boolean(TRUE));
				devc->overcurrent = TRUE;
			}
		}

		/* Check the front panel status */
		if (!devc->overcurrent) {
			res = francaise_instrumentation_ams515_query_str(sdi, 'S', answer);
			if (res == SR_OK && answer[0] >= 'A' && answer[0] <= 'C')
				devc->selected_channel = answer[0] - 'A';
			sr_dbg("Selected channel %d.", devc->selected_channel);
			// TODO: check actual targets
		}
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
SR_PRIV int francaise_instrumentation_ams515_send_raw(const struct sr_dev_inst *sdi, const char *cmd, char *answer, gboolean echoed)
{
	struct sr_serial_dev_inst *serial;
	struct dev_context *devc;
	int cmdlen = strlen(cmd);
	int i, ret = SR_ERR_IO;

	if (!sdi)
		return SR_ERR_ARG;
	serial = sdi->conn;
	devc = sdi->priv;
	if (!serial || !devc)
		return SR_ERR_ARG;
	/* do not even try */
	if (devc->resync)
		return SR_ERR_IO;

	sr_spew("send_raw(): '%s'", cmd);
	memset(answer, '\0', ANSWER_MAX);

	g_mutex_lock(&devc->mutex);

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
			break;
		}
		// if we didn't get an echo of the first char,
		// assume no echo, and don't eat the result.
		if (!echoed)
			continue;
		// we do not know if echo is on, so we try to read
		if (serial_read_blocking(serial, &c, 1, SERIAL_READ_TIMEOUT_MS) < 1) {
			sr_dbg("Unable to read echoed cmd, assuming no echo.");
			echoed = FALSE;
			continue;
		}
		echoed = TRUE;
		if (c != cmd[i]) {
			sr_err("Mismatched echoed cmd: %c != %c", c, cmd[i]);
			// actually keep going, if we ever want to resync properly
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
		//sr_spew("send_raw(): answer: 0x%02x '%c'", answer[i], answer[i]);
		// skip CR/LF
		if (answer[i] == '\r' || answer[i] == '\n') {
			if (!echoed) {
				// We shouldn't get CR/LF in no-echo mode.
				// Likely the device was power-cycled, switch it back to no-echo mode
				sr_dbg("CR/LF found reply in no-echo mode!");
				devc->resync = TRUE;
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

	g_mutex_unlock(&devc->mutex);

	answer[i] = '\0';
	sr_spew("send_raw(): answer: '%s'", answer);

	// Some error occured
	if (!strcmp("Error!", answer))
		return SR_ERR;

	// Argument is out of bounds
	if (!strcmp("Dep", answer))
		return SR_ERR_ARG;

	// Over-current
	if (!strcmp("Icc", answer)) {
		devc->overcurrent = TRUE;
		return SR_ERR;
	}

	return ret;
}

/**
 * Set a state on the device to enabled or disabled
 *
 * @param[in] cmd Raw command char.
 * @param[in] param Echo mode to set.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_ARG Invalid argument.
 * @retval SR_ERR Error.
 */
SR_PRIV int francaise_instrumentation_ams515_set_state(const struct sr_dev_inst *sdi, char cmd, gboolean param)
{
	char command[4];
	char answer[ANSWER_MAX];
	int ret;

	if (!sdi)
		return SR_ERR_ARG;

	snprintf(command, 4, "%c?\r", cmd);
	// Query current state
	ret = francaise_instrumentation_ams515_send_raw(sdi, command, answer, TRUE);
	if (ret < SR_OK)
		return ret;
	if ((param && !strcmp(answer, "00")) || (!param && !strcmp(answer, "FF"))) {
		snprintf(command, 4, "%c\r", cmd);
		// Toggle state to the one we want
		ret = francaise_instrumentation_ams515_send_raw(sdi, command, answer, TRUE);
	}

	return ret;
}

/**
 * Set echo mode on the device
 *
 * @param[in] param Echo mode to set.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_ARG Invalid argument.
 * @retval SR_ERR Error.
 */
SR_PRIV int francaise_instrumentation_ams515_set_echo(const struct sr_dev_inst *sdi, gboolean param)
{
	struct sr_serial_dev_inst *serial;
	int ret;

	if (!sdi)
		return SR_ERR_ARG;
	serial = sdi->conn;
	if (!serial)
		return SR_ERR_ARG;

	serial_flush(serial);

	// State is actually reversed
	ret = francaise_instrumentation_ams515_set_state(sdi, 'T', !param);

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
	char command[4];
	char answer[ANSWER_MAX];
	int ret;
	long res;

	if (!sdi || !result)
		return SR_ERR_ARG;

	snprintf(command, 4, "%c?\r", cmd);
	sr_dbg("query_int(): sending %s", command);

	ret = francaise_instrumentation_ams515_send_raw(sdi, command, answer, FALSE);
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
	char command[4];
	int ret;

	if (!sdi || !result)
		return SR_ERR_ARG;

	snprintf(command, 4, "%c?\r", cmd);
	sr_dbg("query_str(): sending %s", command);

	ret = francaise_instrumentation_ams515_send_raw(sdi, command, result, FALSE);
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
	char command[13];
	char answer[ANSWER_MAX];
	int ret;

	if (!sdi)
		return SR_ERR_ARG;

	snprintf(command, 12, "%c%c%.02X\r", cmd, param < 0 ? '-' : '+', abs(param));
	sr_dbg("send_int(): sending %s", command);

	ret = francaise_instrumentation_ams515_send_raw(sdi, command, answer, FALSE);
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
	char command[10];
	char answer[ANSWER_MAX];
	int ret;

	if (!sdi)
		return SR_ERR_ARG;

	snprintf(command, 9, "%c%c\r", cmd, param);
	sr_dbg("send_char(): sending %s", command);

	ret = francaise_instrumentation_ams515_send_raw(sdi, command, answer, FALSE);
	return ret;
}
