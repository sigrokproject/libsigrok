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

#include <glib.h>
#include <string.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"

#define LOG_PREFIX "scpi"

#define SCPI_READ_RETRIES 100
#define SCPI_READ_RETRY_TIMEOUT_US (10 * 1000)

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

SR_PRIV extern const struct sr_scpi_dev_inst scpi_serial_dev;
SR_PRIV extern const struct sr_scpi_dev_inst scpi_tcp_raw_dev;
SR_PRIV extern const struct sr_scpi_dev_inst scpi_tcp_rigol_dev;
SR_PRIV extern const struct sr_scpi_dev_inst scpi_usbtmc_libusb_dev;
SR_PRIV extern const struct sr_scpi_dev_inst scpi_vxi_dev;
SR_PRIV extern const struct sr_scpi_dev_inst scpi_visa_dev;
SR_PRIV extern const struct sr_scpi_dev_inst scpi_libgpib_dev;

static const struct sr_scpi_dev_inst *scpi_devs[] = {
	&scpi_tcp_raw_dev,
	&scpi_tcp_rigol_dev,
#ifdef HAVE_LIBUSB_1_0
	&scpi_usbtmc_libusb_dev,
#endif
#if HAVE_RPC
	&scpi_vxi_dev,
#endif
#ifdef HAVE_LIBREVISA
	&scpi_visa_dev,
#endif
#ifdef HAVE_LIBGPIB
	&scpi_libgpib_dev,
#endif
#ifdef HAVE_LIBSERIALPORT
	&scpi_serial_dev,  /* must be last as it matches any resource */
#endif
};

static struct sr_dev_inst *sr_scpi_scan_resource(struct drv_context *drvc,
		const char *resource, const char *serialcomm,
		struct sr_dev_inst *(*probe_device)(struct sr_scpi_dev_inst *scpi))
{
	struct sr_scpi_dev_inst *scpi;
	struct sr_dev_inst *sdi;

	if (!(scpi = scpi_dev_inst_new(drvc, resource, serialcomm)))
		return NULL;

	if (sr_scpi_open(scpi) != SR_OK) {
		sr_info("Couldn't open SCPI device.");
		sr_scpi_free(scpi);
		return NULL;
	};

	if ((sdi = probe_device(scpi)))
		return sdi;

	sr_scpi_close(scpi);
	sr_scpi_free(scpi);
	return NULL;
}

SR_PRIV GSList *sr_scpi_scan(struct drv_context *drvc, GSList *options,
		struct sr_dev_inst *(*probe_device)(struct sr_scpi_dev_inst *scpi))
{
	GSList *resources, *l, *devices;
	struct sr_dev_inst *sdi;
	const char *resource = NULL;
	const char *serialcomm = NULL;
	gchar **res;
	unsigned i;

	for (l = options; l; l = l->next) {
		struct sr_config *src = l->data;
		switch (src->key) {
		case SR_CONF_CONN:
			resource = g_variant_get_string(src->data, NULL);
			break;
		case SR_CONF_SERIALCOMM:
			serialcomm = g_variant_get_string(src->data, NULL);
			break;
		}
	}

	devices = NULL;
	for (i = 0; i < ARRAY_SIZE(scpi_devs); i++) {
		if ((resource && strcmp(resource, scpi_devs[i]->prefix))
		    || !scpi_devs[i]->scan)
			continue;
		resources = scpi_devs[i]->scan(drvc);
		for (l = resources; l; l = l->next) {
			res = g_strsplit(l->data, ":", 2);
			if (res[0] && (sdi = sr_scpi_scan_resource(drvc, res[0],
			               serialcomm ? serialcomm : res[1], probe_device))) {
				devices = g_slist_append(devices, sdi);
				sdi->connection_id = g_strdup(l->data);
			}
			g_strfreev(res);
		}
		g_slist_free_full(resources, g_free);
	}

	if (!devices && resource) {
		sdi = sr_scpi_scan_resource(drvc, resource, serialcomm, probe_device);
		if (sdi)
			devices = g_slist_append(NULL, sdi);
	}

	/* Tack a copy of the newly found devices onto the driver list. */
	if (devices)
		drvc->instances = g_slist_concat(drvc->instances, g_slist_copy(devices));

	return devices;
}

SR_PRIV struct sr_scpi_dev_inst *scpi_dev_inst_new(struct drv_context *drvc,
		const char *resource, const char *serialcomm)
{
	struct sr_scpi_dev_inst *scpi = NULL;
	const struct sr_scpi_dev_inst *scpi_dev;
	gchar **params;
	unsigned i;

	for (i = 0; i < ARRAY_SIZE(scpi_devs); i++) {
		scpi_dev = scpi_devs[i];
		if (!strncmp(resource, scpi_dev->prefix, strlen(scpi_dev->prefix))) {
			sr_dbg("Opening %s device %s.", scpi_dev->name, resource);
			scpi = g_malloc(sizeof(*scpi));
			*scpi = *scpi_dev;
			scpi->priv = g_malloc0(scpi->priv_size);
			scpi->read_timeout_ms = 1000;
			params = g_strsplit(resource, "/", 0);
			if (scpi->dev_inst_new(scpi->priv, drvc, resource,
			                       params, serialcomm) != SR_OK) {
				sr_scpi_free(scpi);
				scpi = NULL;
			}
			g_strfreev(params);
			break;
		}
	}

	return scpi;
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
 * @param session The session to add the event source to.
 * @param scpi Previously initialized SCPI device structure.
 * @param events Events to check for.
 * @param timeout Max time to wait before the callback is called, ignored if 0.
 * @param cb Callback function to add. Must not be NULL.
 * @param cb_data Data for the callback function. Can be NULL.
 *
 * @return SR_OK upon success, SR_ERR_ARG upon invalid arguments, or
 *         SR_ERR_MALLOC upon memory allocation errors.
 */
SR_PRIV int sr_scpi_source_add(struct sr_session *session,
		struct sr_scpi_dev_inst *scpi, int events, int timeout,
		sr_receive_data_callback cb, void *cb_data)
{
	return scpi->source_add(session, scpi->priv, events, timeout, cb, cb_data);
}

/**
 * Remove event source for an SCPI device.
 *
 * @param session The session to remove the event source from.
 * @param scpi Previously initialized SCPI device structure.
 *
 * @return SR_OK upon success, SR_ERR_ARG upon invalid arguments, or
 *         SR_ERR_MALLOC upon memory allocation errors, SR_ERR_BUG upon
 *         internal errors.
 */
SR_PRIV int sr_scpi_source_remove(struct sr_session *session,
		struct sr_scpi_dev_inst *scpi)
{
	return scpi->source_remove(session, scpi->priv);
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
	g_free(scpi->priv);
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
	gint64 laststart;
	unsigned int elapsed_ms;

	if (command)
		if (sr_scpi_send(scpi, command) != SR_OK)
			return SR_ERR;

	if (sr_scpi_read_begin(scpi) != SR_OK)
		return SR_ERR;

	laststart = g_get_monotonic_time();

	response = g_string_new("");

	*scpi_response = NULL;

	while (!sr_scpi_read_complete(scpi)) {
		len = sr_scpi_read_data(scpi, buf, sizeof(buf));
		if (len < 0) {
			sr_err("Incompletely read SCPI response.");
			g_string_free(response, TRUE);
			return SR_ERR;
		} else if (len > 0) {
		        laststart = g_get_monotonic_time();
		}
		g_string_append_len(response, buf, len);
		elapsed_ms = (g_get_monotonic_time() - laststart) / 1000;
		if (elapsed_ms >= scpi->read_timeout_ms) {
			sr_err("Timed out waiting for SCPI response.");
			g_string_free(response, TRUE);
			return SR_ERR;
		}
	}

	/* Get rid of trailing linefeed if present */
	if (response->len >= 1 && response->str[response->len - 1] == '\n')
		g_string_truncate(response, response->len - 1);

	/* Get rid of trailing carriage return if present */
	if (response->len >= 1 && response->str[response->len - 1] == '\r')
		g_string_truncate(response, response->len - 1);

	*scpi_response = response->str;
	g_string_free(response, FALSE);

	sr_spew("Got response: '%.70s', length %d.", *scpi_response, strlen(*scpi_response));

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

	if (sr_atof_ascii(response, scpi_response) == SR_OK)
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
		g_usleep(SCPI_READ_RETRY_TIMEOUT_US);
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
		if (sr_atof_ascii(*ptr, &tmp) == SR_OK)
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
	gchar **tokens;
	struct sr_scpi_hw_info *hw_info;

	response = NULL;
	tokens = NULL;

	if (sr_scpi_get_string(scpi, SCPI_CMD_IDN, &response) != SR_OK)
		if (!response)
			return SR_ERR;

	sr_info("Got IDN string: '%s'", response);

	/*
	 * The response to a '*IDN?' is specified by the SCPI spec. It contains
	 * a comma-separated list containing the manufacturer name, instrument
	 * model, serial number of the instrument and the firmware version.
	 */
	tokens = g_strsplit(response, ",", 0);

	for (num_tokens = 0; tokens[num_tokens] != NULL; num_tokens++);

	if (num_tokens < 4) {
		sr_dbg("IDN response not according to spec: %80.s.", response);
		g_strfreev(tokens);
		g_free(response);
		return SR_ERR;
	}
	g_free(response);

	hw_info = g_malloc0(sizeof(struct sr_scpi_hw_info));
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
