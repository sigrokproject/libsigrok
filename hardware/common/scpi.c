/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 poljar (Damir Jelić) <poljarinho@gmail.com>
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

#include "libsigrok.h"
#include "libsigrok-internal.h"

#include <glib.h>
#include <string.h>

/* Message logging helpers with subsystem-specific prefix string. */
#define LOG_PREFIX "scpi: "
#define sr_log(l, s, args...) sr_log(l, LOG_PREFIX s, ## args)
#define sr_spew(s, args...) sr_spew(LOG_PREFIX s, ## args)
#define sr_dbg(s, args...) sr_dbg(LOG_PREFIX s, ## args)
#define sr_info(s, args...) sr_info(LOG_PREFIX s, ## args)
#define sr_warn(s, args...) sr_warn(LOG_PREFIX s, ## args)

#define SCPI_READ_RETRIES 100
#define SCPI_READ_RETRY_TIMEOUT 10000

/**
 * Parse a string representation of a boolean-like value into a gboolean.
 * Similar to sr_parse_boolstring but rejects strings which do not represent
 * a boolean-like value.
 *
 * @param str String to convert.
 * @param ret Pointer to a gboolean where the result of the conversion will be
 * stored.
 *
 * @return SR_OK on success, SR_ERR on failure.
 */
static int sr_parse_strict_bool(const char *str, gboolean *ret)
{
	if (!str)
		return SR_ERR_ARG;

	if (!g_strcmp0(str, "1") ||
	    !g_ascii_strncasecmp(str, "y", 1) ||
	    !g_ascii_strncasecmp(str, "t", 1) ||
	    !g_ascii_strncasecmp(str, "yes", 3) ||
	    !g_ascii_strncasecmp(str, "true", 4) ||
	    !g_ascii_strncasecmp(str, "on", 2)) {

		*ret = TRUE;
		return SR_OK;

	} else if (!g_strcmp0(str, "0") ||
		   !g_ascii_strncasecmp(str, "n", 1) ||
		   !g_ascii_strncasecmp(str, "f", 1) ||
		   !g_ascii_strncasecmp(str, "no", 2) ||
		   !g_ascii_strncasecmp(str, "false", 5) ||
		   !g_ascii_strncasecmp(str, "off", 3)) {

		*ret = FALSE;
		return SR_OK;
	}

	return SR_ERR;
}

/**
 * Send a SCPI command.
 *
 * @param serial Previously initialized serial port structure.
 * @param command The SCPI command to send to the device.
 *
 * @return SR_OK on success, SR_ERR on failure.
 */
SR_PRIV int sr_scpi_send(struct sr_serial_dev_inst *serial,
			 const char *command)
{
	int len;
	int out;
	gchar *terminated_command;

	terminated_command = g_strconcat(command, "\n", NULL);
	len = strlen(terminated_command);

	out = serial_write(serial, terminated_command,
			   strlen(terminated_command));

	g_free(terminated_command);

	if (out != len) {
		sr_dbg("Only sent %d/%d bytes of SCPI command: '%s'.", out,
		       len, command);
		return SR_ERR;
	}

	sr_spew("Successfully sent SCPI command: '%s'.", command);

	return SR_OK;
}

/**
 * Send a SCPI command, receive the reply and store the reply in scpi_response.
 *
 * @param serial Previously initialized serial port structure.
 * @param command The SCPI command to send to the device (can be NULL).
 * @param scpi_response Pointer where to store the scpi response.
 *
 * @return SR_OK upon fetching a full SCPI response, SR_ERR upon fetching a
 * incomplete or no response. The allocated response must be freed by the caller
 * in the case of a full response as well in the case of an incomplete.
 */
SR_PRIV int sr_scpi_get_string(struct sr_serial_dev_inst *serial,
				 const char *command, char **scpi_response)
{
	int len;
	int ret;
	char buf[256];
	unsigned int i;
	GString *response;

	if (command)
		if (sr_scpi_send(serial, command) != SR_OK)
			return SR_ERR;

	response = g_string_sized_new(1024);

	for (i = 0; i <= SCPI_READ_RETRIES; i++) {
		while ((len = serial_read(serial, buf, sizeof(buf))) > 0)
			response = g_string_append_len(response, buf, len);

		if (response->len > 0 &&
		    response->str[response->len-1] == '\n') {
			sr_spew("Fetched full SCPI response");
			break;
		}

		g_usleep(SCPI_READ_RETRY_TIMEOUT);
	}

	if (response->len == 0) {
		sr_dbg("No SCPI response received");
		g_string_free(response, TRUE);
		*scpi_response = NULL;
		return SR_ERR;

	} else if (response->str[response->len-1] == '\n') {
		/*
		 * The SCPI response contains a LF ('\n') at the end and we
		 * don't need this so replace it with a '\0' and decrement
		 * the length.
		 */
		response->str[--response->len] = '\0';
		ret = SR_OK;

	} else {
		sr_warn("Incomplete SCPI response received!");
		ret = SR_ERR;
	}

	/* Minor optimization: steal the string instead of copying. */
	*scpi_response = response->str;

	/* A SCPI response can be quite large, print at most 50 characters */
	sr_dbg("SCPI response for command %s received (length %d): '%.50s'",
	       command, response->len, response->str);

	g_string_free(response, FALSE);

	return ret;
}

/**
 * Send a SCPI command, read the reply, parse it as a bool value and store the
 * result in scpi_response.
 *
 * @param serial Previously initialized serial port structure.
 * @param command The SCPI command to send to the device (can be NULL).
 * @param scpi_response Pointer where to store the parsed result.
 *
 * @return SR_OK on success, SR_ERR on failure.
 */
SR_PRIV int sr_scpi_get_bool(struct sr_serial_dev_inst *serial,
			     const char *command, gboolean *scpi_response)
{
	int ret;
	char *response;

	response = NULL;

	if (sr_scpi_get_string(serial, command, &response) != SR_OK)
		if (!response)
			return SR_ERR;

	if (sr_parse_strict_bool(response, scpi_response) == SR_OK)
		ret = SR_OK;
	else
		ret = SR_ERR;

	g_free(response);

	return ret;
}

/**
 * Send a SCPI command, read the reply, parse it as an integer and store the
 * result in scpi_response.
 *
 * @param serial Previously initialized serial port structure.
 * @param command The SCPI command to send to the device (can be NULL).
 * @param scpi_response Pointer where to store the parsed result.
 *
 * @return SR_OK on success, SR_ERR on failure.
 */
SR_PRIV int sr_scpi_get_int(struct sr_serial_dev_inst *serial,
				  const char *command, int *scpi_response)
{
	int ret;
	char *response;

	response = NULL;

	if (sr_scpi_get_string(serial, command, &response) != SR_OK)
		if (!response)
			return SR_ERR;

	if (sr_atoi(response, scpi_response) == SR_OK)
		ret = SR_OK;
	else
		ret = SR_ERR;

	g_free(response);

	return ret;
}

/**
 * Send a SCPI command, read the reply, parse it as a float and store the
 * result in scpi_response.
 *
 * @param serial Previously initialized serial port structure.
 * @param command The SCPI command to send to the device (can be NULL).
 * @param scpi_response Pointer where to store the parsed result.
 *
 * @return SR_OK on success, SR_ERR on failure.
 */
SR_PRIV int sr_scpi_get_float(struct sr_serial_dev_inst *serial,
			      const char *command, float *scpi_response)
{
	int ret;
	char *response;

	response = NULL;

	if (sr_scpi_get_string(serial, command, &response) != SR_OK)
		if (!response)
			return SR_ERR;

	if (sr_atof(response, scpi_response) == SR_OK)
		ret = SR_OK;
	else
		ret = SR_ERR;

	g_free(response);

	return ret;
}

/**
 * Send a SCPI command, read the reply, parse it as a double and store the
 * result in scpi_response.
 *
 * @param serial Previously initialized serial port structure.
 * @param command The SCPI command to send to the device (can be NULL).
 * @param scpi_response Pointer where to store the parsed result.
 *
 * @return SR_OK on success, SR_ERR on failure.
 */
SR_PRIV int sr_scpi_get_double(struct sr_serial_dev_inst *serial,
			      const char *command, double *scpi_response)
{
	int ret;
	char *response;

	response = NULL;

	if (sr_scpi_get_string(serial, command, &response) != SR_OK)
		if (!response)
			return SR_ERR;

	if (sr_atod(response, scpi_response) == SR_OK)
		ret = SR_OK;
	else
		ret = SR_ERR;

	g_free(response);

	return ret;
}

/**
 * Send a SCPI *OPC? command, read the reply and return the result of the
 * command.
 *
 * @param serial Previously initialized serial port structure.
 *
 * @return SR_OK on success, SR_ERR on failure.
 */
SR_PRIV int sr_scpi_get_opc(struct sr_serial_dev_inst *serial)
{
	unsigned int i;
	gboolean opc;

	for (i = 0; i < SCPI_READ_RETRIES; ++i) {
		sr_scpi_get_bool(serial, SCPI_CMD_OPC, &opc);

		if (opc)
			return SR_OK;

		g_usleep(SCPI_READ_RETRY_TIMEOUT);
	}

	return SR_ERR;
}

/**
 * Send a SCPI command, read the reply, parse it as comma separated list of
 * floats and store the as an result in scpi_response.
 *
 * @param serial Previously initialized serial port structure.
 * @param command The SCPI command to send to the device (can be NULL).
 * @param scpi_response Pointer where to store the parsed result.
 *
 * @return SR_OK upon successfully parsing all values, SR_ERR upon a parsing
 * error or upon no response. The allocated response must be freed by the caller
 * in the case of an SR_OK as well as in the case of parsing error.
 */
SR_PRIV int sr_scpi_get_floatv(struct sr_serial_dev_inst *serial,
			      const char *command, GArray **scpi_response)
{
	int ret;
	float tmp;
	char *response;

	gchar **ptr;
	gchar **tokens;
	GArray *response_array;

	ret = SR_OK;
	response = NULL;
	tokens = NULL;

	if (sr_scpi_get_string(serial, command, &response) != SR_OK)
		if (!response)
			return SR_ERR;

	tokens = g_strsplit(response, ",", 0);
	ptr = tokens;

	response_array = g_array_sized_new(TRUE, FALSE, sizeof(float), 256);

	while(*ptr) {
		if (sr_atof(*ptr, &tmp) == SR_OK)
			response_array = g_array_append_val(response_array,
							    tmp);
		else
			ret = SR_ERR;

		ptr++;
	}
	g_strfreev(tokens);
	g_free(response);

	if (ret == SR_ERR && response_array->len == 0) {
		g_array_free(response_array, TRUE);
		*scpi_response = NULL;
		return SR_ERR;
	}

	*scpi_response = response_array;

	return ret;
}

/**
 * Send a SCPI command, read the reply, parse it as comma separated list of
 * unsigned 8 bit integers and store the as an result in scpi_response.
 *
 * @param serial Previously initialized serial port structure.
 * @param command The SCPI command to send to the device (can be NULL).
 * @param scpi_response Pointer where to store the parsed result.
 *
 * @return SR_OK upon successfully parsing all values, SR_ERR upon a parsing
 * error or upon no response. The allocated response must be freed by the caller
 * in the case of an SR_OK as well as in the case of parsing error.
 */
SR_PRIV int sr_scpi_get_uint8v(struct sr_serial_dev_inst *serial,
			      const char *command, GArray **scpi_response)
{
	int tmp;
	int ret;
	char *response;

	gchar **ptr;
	gchar **tokens;
	GArray *response_array;

	ret = SR_OK;
	response = NULL;
	tokens = NULL;

	if (sr_scpi_get_string(serial, command, &response) != SR_OK)
		if (!response)
			return SR_ERR;

	tokens = g_strsplit(response, ",", 0);
	ptr = tokens;

	response_array = g_array_sized_new(TRUE, FALSE, sizeof(uint8_t), 256);

	while(*ptr) {
		if (sr_atoi(*ptr, &tmp) == SR_OK)
			response_array = g_array_append_val(response_array,
							    tmp);
		else
			ret = SR_ERR;

		ptr++;
	}
	g_strfreev(tokens);
	g_free(response);

	if (response_array->len == 0) {
		g_array_free(response_array, TRUE);
		*scpi_response = NULL;
		return SR_ERR;
	}

	*scpi_response = response_array;

	return ret;
}

/**
 * Send the *IDN? SCPI command, receive the reply, parse it and store the
 * reply as a sr_scpi_hw_info structure in the supplied scpi_response pointer.
 *
 * @param serial Previously initialized serial port structure.
 * @param scpi_response Pointer where to store the hw_info structure.
 *
 * @return SR_OK upon success, SR_ERR on failure.
 * The hw_info structure must be freed by the caller with sr_scpi_hw_info_free().
 */
SR_PRIV int sr_scpi_get_hw_id(struct sr_serial_dev_inst *serial,
			      struct sr_scpi_hw_info **scpi_response)
{
	int num_tokens;
	char *response;
	gchar **tokens;

	struct sr_scpi_hw_info *hw_info;

	response = NULL;
	tokens = NULL;

	if (sr_scpi_get_string(serial, SCPI_CMD_IDN, &response) != SR_OK)
		if (!response)
			return SR_ERR;

	/*
	 * The response to a '*IDN?' is specified by the SCPI spec. It contains
	 * a comma-separated list containing the manufacturer name, instrument
	 * model, serial number of the instrument and the firmware version.
	 */
	tokens = g_strsplit(response, ",", 0);

	for (num_tokens = 0; tokens[num_tokens] != NULL; num_tokens++);

	if (num_tokens != 4) {
		sr_dbg("IDN response not according to spec: %80.s", response);
		g_strfreev(tokens);
		g_free(response);
		return SR_ERR;
	}
	g_free(response);

	hw_info = g_try_malloc(sizeof(struct sr_scpi_hw_info));
	if (!hw_info) {
		g_strfreev(tokens);
		return SR_ERR_MALLOC;
	}

	hw_info->manufacturer = g_strdup(tokens[0]);
	hw_info->model = g_strdup(tokens[1]);
	hw_info->serial_number = g_strdup(tokens[2]);
	hw_info->firmware_version = g_strdup(tokens[3]);

	g_strfreev(tokens);

	*scpi_response = hw_info;

	return SR_OK;
}

/**
 * Free a sr_scpi_hw_info struct.
 *
 * @param hw_info Pointer to the struct to free.
 *
 * This function is safe to call with a NULL pointer.
 */
SR_PRIV void sr_scpi_hw_info_free(struct sr_scpi_hw_info *hw_info)
{
	if (hw_info) {
		g_free(hw_info->manufacturer);
		g_free(hw_info->model);
		g_free(hw_info->serial_number);
		g_free(hw_info->firmware_version);
		g_free(hw_info);
	}
}
