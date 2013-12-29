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

#define LOG_PREFIX "scpi"

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
static int parse_strict_bool(const char *str, gboolean *ret)
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
 * Open SCPI device.
 *
 * @param scpi Previously initialized SCPI device structure.
 *
 * @return SR_OK on success, SR_ERR on failure.
 */
SR_PRIV int sr_scpi_open(struct sr_scpi_dev_inst *scpi)
{
	return scpi->open(scpi->priv);
}

/**
 * Add an event source for an SCPI device.
 *
 * @param scpi Previously initialized SCPI device structure.
 * @param events Events to check for.
 * @param timeout Max time to wait before the callback is called, ignored if 0.
 * @param cb Callback function to add. Must not be NULL.
 * @param cb_data Data for the callback function. Can be NULL.
 *
 * @return SR_OK upon success, SR_ERR_ARG upon invalid arguments, or
 *         SR_ERR_MALLOC upon memory allocation errors.
 */
SR_PRIV int sr_scpi_source_add(struct sr_scpi_dev_inst *scpi, int events,
		int timeout, sr_receive_data_callback_t cb, void *cb_data)
{
	return scpi->source_add(scpi->priv, events, timeout, cb, cb_data);
}

/**
 * Remove event source for an SCPI device.
 *
 * @param scpi Previously initialized SCPI device structure.
 *
 * @return SR_OK upon success, SR_ERR_ARG upon invalid arguments, or
 *         SR_ERR_MALLOC upon memory allocation errors, SR_ERR_BUG upon
 *         internal errors.
 */
SR_PRIV int sr_scpi_source_remove(struct sr_scpi_dev_inst *scpi)
{
	return scpi->source_remove(scpi->priv);
}

/**
 * Send a SCPI command.
 *
 * @param scpi Previously initialized SCPI device structure.
 * @param format Format string, to be followed by any necessary arguments.
 *
 * @return SR_OK on success, SR_ERR on failure.
 */
SR_PRIV int sr_scpi_send(struct sr_scpi_dev_inst *scpi,
			 const char *format, ...)
{
	va_list args;
	int ret;

	va_start(args, format);
	ret = sr_scpi_send_variadic(scpi, format, args);
	va_end(args);

	return ret;
}

/**
 * Send a SCPI command with a variadic argument list.
 *
 * @param scpi Previously initialized SCPI device structure.
 * @param format Format string.
 * @param args Argument list.
 *
 * @return SR_OK on success, SR_ERR on failure.
 */
SR_PRIV int sr_scpi_send_variadic(struct sr_scpi_dev_inst *scpi,
			 const char *format, va_list args)
{
	va_list args_copy;
	char *buf;
	int len, ret;

	/* Get length of buffer required. */
	va_copy(args_copy, args);
	len = vsnprintf(NULL, 0, format, args_copy);
	va_end(args_copy);

	/* Allocate buffer and write out command. */
	buf = g_malloc(len + 1);
	vsprintf(buf, format, args);

	/* Send command. */
	ret = scpi->send(scpi->priv, buf);

	/* Free command buffer. */
	g_free(buf);

	return ret;
}

/**
 * Begin receiving an SCPI reply.
 *
 * @param scpi Previously initialised SCPI device structure.
 *
 * @return SR_OK on success, SR_ERR on failure.
 */
SR_PRIV int sr_scpi_read_begin(struct sr_scpi_dev_inst *scpi)
{
	return scpi->read_begin(scpi->priv);
}

/**
 * Read part of a response from SCPI device.
 *
 * @param scpi Previously initialised SCPI device structure.
 * @param buf Buffer to store result.
 * @param maxlen Maximum number of bytes to read.
 *
 * @return Number of bytes read, or SR_ERR upon failure.
 */
SR_PRIV int sr_scpi_read_data(struct sr_scpi_dev_inst *scpi,
			char *buf, int maxlen)
{
	return scpi->read_data(scpi->priv, buf, maxlen);
}

/**
 * Check whether a complete SCPI response has been received.
 *
 * @param scpi Previously initialised SCPI device structure.
 *
 * @return 1 if complete, 0 otherwise.
 */
SR_PRIV int sr_scpi_read_complete(struct sr_scpi_dev_inst *scpi)
{
	return scpi->read_complete(scpi->priv);
}

/**
 * Close SCPI device.
 *
 * @param scpi Previously initialized SCPI device structure.
 *
 * @return SR_OK on success, SR_ERR on failure.
 */
SR_PRIV int sr_scpi_close(struct sr_scpi_dev_inst *scpi)
{
	return scpi->close(scpi->priv);
}

/**
 * Free SCPI device.
 *
 * @param scpi Previously initialized SCPI device structure.
 *
 * @return SR_OK on success, SR_ERR on failure.
 */
SR_PRIV void sr_scpi_free(struct sr_scpi_dev_inst *scpi)
{
	scpi->free(scpi->priv);
	g_free(scpi);
}

/**
 * Send a SCPI command, receive the reply and store the reply in scpi_response.
 *
 * @param scpi Previously initialised SCPI device structure.
 * @param command The SCPI command to send to the device (can be NULL).
 * @param scpi_response Pointer where to store the SCPI response.
 *
 * @return SR_OK on success, SR_ERR on failure.
 */
SR_PRIV int sr_scpi_get_string(struct sr_scpi_dev_inst *scpi,
			       const char *command, char **scpi_response)
{
	char buf[256];
	int len;
	GString *response;

	if (command)
		if (sr_scpi_send(scpi, command) != SR_OK)
			return SR_ERR;

	if (sr_scpi_read_begin(scpi) != SR_OK)
		return SR_ERR;

	response = g_string_new("");

	*scpi_response = NULL;

	while (!sr_scpi_read_complete(scpi)) {
		len = sr_scpi_read_data(scpi, buf, sizeof(buf));
		if (len < 0) {
			g_string_free(response, TRUE);
			return SR_ERR;
		}
		g_string_append_len(response, buf, len);
	}

	*scpi_response = response->str;
	g_string_free(response, FALSE);

	return SR_OK;
}

/**
 * Send a SCPI command, read the reply, parse it as a bool value and store the
 * result in scpi_response.
 *
 * @param scpi Previously initialised SCPI device structure.
 * @param command The SCPI command to send to the device (can be NULL).
 * @param scpi_response Pointer where to store the parsed result.
 *
 * @return SR_OK on success, SR_ERR on failure.
 */
SR_PRIV int sr_scpi_get_bool(struct sr_scpi_dev_inst *scpi,
			     const char *command, gboolean *scpi_response)
{
	int ret;
	char *response;

	response = NULL;

	if (sr_scpi_get_string(scpi, command, &response) != SR_OK)
		if (!response)
			return SR_ERR;

	if (parse_strict_bool(response, scpi_response) == SR_OK)
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
 * @param scpi Previously initialised SCPI device structure.
 * @param command The SCPI command to send to the device (can be NULL).
 * @param scpi_response Pointer where to store the parsed result.
 *
 * @return SR_OK on success, SR_ERR on failure.
 */
SR_PRIV int sr_scpi_get_int(struct sr_scpi_dev_inst *scpi,
			    const char *command, int *scpi_response)
{
	int ret;
	char *response;

	response = NULL;

	if (sr_scpi_get_string(scpi, command, &response) != SR_OK)
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
 * @param scpi Previously initialised SCPI device structure.
 * @param command The SCPI command to send to the device (can be NULL).
 * @param scpi_response Pointer where to store the parsed result.
 *
 * @return SR_OK on success, SR_ERR on failure.
 */
SR_PRIV int sr_scpi_get_float(struct sr_scpi_dev_inst *scpi,
			      const char *command, float *scpi_response)
{
	int ret;
	char *response;

	response = NULL;

	if (sr_scpi_get_string(scpi, command, &response) != SR_OK)
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
 * @param scpi Previously initialised SCPI device structure.
 * @param command The SCPI command to send to the device (can be NULL).
 * @param scpi_response Pointer where to store the parsed result.
 *
 * @return SR_OK on success, SR_ERR on failure.
 */
SR_PRIV int sr_scpi_get_double(struct sr_scpi_dev_inst *scpi,
			       const char *command, double *scpi_response)
{
	int ret;
	char *response;

	response = NULL;

	if (sr_scpi_get_string(scpi, command, &response) != SR_OK)
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
 * @param scpi Previously initialised SCPI device structure.
 *
 * @return SR_OK on success, SR_ERR on failure.
 */
SR_PRIV int sr_scpi_get_opc(struct sr_scpi_dev_inst *scpi)
{
	unsigned int i;
	gboolean opc;

	for (i = 0; i < SCPI_READ_RETRIES; ++i) {
		sr_scpi_get_bool(scpi, SCPI_CMD_OPC, &opc);
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
 * @param scpi Previously initialised SCPI device structure.
 * @param command The SCPI command to send to the device (can be NULL).
 * @param scpi_response Pointer where to store the parsed result.
 *
 * @return SR_OK upon successfully parsing all values, SR_ERR upon a parsing
 *         error or upon no response. The allocated response must be freed by
 *         the caller in the case of an SR_OK as well as in the case of
 *         parsing error.
 */
SR_PRIV int sr_scpi_get_floatv(struct sr_scpi_dev_inst *scpi,
			       const char *command, GArray **scpi_response)
{
	int ret;
	float tmp;
	char *response;
	gchar **ptr, **tokens;
	GArray *response_array;

	ret = SR_OK;
	response = NULL;
	tokens = NULL;

	if (sr_scpi_get_string(scpi, command, &response) != SR_OK)
		if (!response)
			return SR_ERR;

	tokens = g_strsplit(response, ",", 0);
	ptr = tokens;

	response_array = g_array_sized_new(TRUE, FALSE, sizeof(float), 256);

	while (*ptr) {
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
 * @param scpi Previously initialised SCPI device structure.
 * @param command The SCPI command to send to the device (can be NULL).
 * @param scpi_response Pointer where to store the parsed result.
 *
 * @return SR_OK upon successfully parsing all values, SR_ERR upon a parsing
 *         error or upon no response. The allocated response must be freed by
 *         the caller in the case of an SR_OK as well as in the case of
 *         parsing error.
 */
SR_PRIV int sr_scpi_get_uint8v(struct sr_scpi_dev_inst *scpi,
			       const char *command, GArray **scpi_response)
{
	int tmp, ret;
	char *response;
	gchar **ptr, **tokens;
	GArray *response_array;

	ret = SR_OK;
	response = NULL;
	tokens = NULL;

	if (sr_scpi_get_string(scpi, command, &response) != SR_OK)
		if (!response)
			return SR_ERR;

	tokens = g_strsplit(response, ",", 0);
	ptr = tokens;

	response_array = g_array_sized_new(TRUE, FALSE, sizeof(uint8_t), 256);

	while (*ptr) {
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
 * The hw_info structure must be freed by the caller via sr_scpi_hw_info_free().
 *
 * @param scpi Previously initialised SCPI device structure.
 * @param scpi_response Pointer where to store the hw_info structure.
 *
 * @return SR_OK upon success, SR_ERR on failure.
 */
SR_PRIV int sr_scpi_get_hw_id(struct sr_scpi_dev_inst *scpi,
			      struct sr_scpi_hw_info **scpi_response)
{
	int num_tokens;
	char *response;
	char *newline;
	gchar **tokens;
	struct sr_scpi_hw_info *hw_info;

	response = NULL;
	tokens = NULL;

	if (sr_scpi_get_string(scpi, SCPI_CMD_IDN, &response) != SR_OK)
		if (!response)
			return SR_ERR;

	sr_info("Got IDN string: '%s'", response);

	/* Remove trailing newline if present. */
	if ((newline = g_strrstr(response, "\n")))
		newline[0] = '\0';

	/*
	 * The response to a '*IDN?' is specified by the SCPI spec. It contains
	 * a comma-separated list containing the manufacturer name, instrument
	 * model, serial number of the instrument and the firmware version.
	 */
	tokens = g_strsplit(response, ",", 0);

	for (num_tokens = 0; tokens[num_tokens] != NULL; num_tokens++);

	if (num_tokens != 4) {
		sr_dbg("IDN response not according to spec: %80.s.", response);
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
