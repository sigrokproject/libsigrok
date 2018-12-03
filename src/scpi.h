/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2015 Bert Vermeulen <bert@biot.com>
 * Copyright (C) 2018 Guido Trentalancia <guido@trentalancia.com>
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

/** @file
  * @internal
  */

#ifndef LIBSIGROK_SCPI_H
#define LIBSIGROK_SCPI_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define SCPI_CMD_IDN "*IDN?"
#define SCPI_CMD_OPC "*OPC?"
#define SCPI_CMD_ESR "*ESR?"

enum {
	SCPI_CMD_GET_TIMEBASE = 1,
	SCPI_CMD_SET_TIMEBASE,
	SCPI_CMD_GET_HORIZONTAL_DIV,
	SCPI_CMD_GET_VERTICAL_SCALE,
	SCPI_CMD_SET_VERTICAL_SCALE,
	SCPI_CMD_GET_TRIGGER_SOURCE,
	SCPI_CMD_SET_TRIGGER_SOURCE,
	SCPI_CMD_GET_TRIGGER_SLOPE,
	SCPI_CMD_SET_TRIGGER_SLOPE,
	SCPI_CMD_GET_TRIGGER_PATTERN,
	SCPI_CMD_SET_TRIGGER_PATTERN,
	SCPI_CMD_GET_HIGH_RESOLUTION,
	SCPI_CMD_SET_HIGH_RESOLUTION,
	SCPI_CMD_GET_PEAK_DETECTION,
	SCPI_CMD_SET_PEAK_DETECTION,
	SCPI_CMD_GET_COUPLING,
	SCPI_CMD_SET_COUPLING,
	SCPI_CMD_GET_HORIZ_TRIGGERPOS,
	SCPI_CMD_SET_HORIZ_TRIGGERPOS,
	SCPI_CMD_GET_ANALOG_CHAN_STATE,
	SCPI_CMD_SET_ANALOG_CHAN_STATE,
	SCPI_CMD_GET_DIG_CHAN_STATE,
	SCPI_CMD_SET_DIG_CHAN_STATE,
	SCPI_CMD_GET_VERTICAL_OFFSET,
	SCPI_CMD_GET_DIG_POD_STATE,
	SCPI_CMD_SET_DIG_POD_STATE,
	SCPI_CMD_GET_ANALOG_DATA,
	SCPI_CMD_GET_DIG_DATA,
	SCPI_CMD_GET_SAMPLE_RATE,
	SCPI_CMD_SET_SAMPLE_RATE,
	SCPI_CMD_GET_WAVEFORM_SAMPLE_RATE,
	SCPI_CMD_SET_WAVEFORM_SAMPLE_RATE,
	SCPI_CMD_GET_AUTO_RECORD_LENGTH,
	SCPI_CMD_SET_AUTO_RECORD_LENGTH,
	SCPI_CMD_GET_RANDOM_SAMPLING,
	SCPI_CMD_SET_RANDOM_SAMPLING,
	SCPI_CMD_GET_ACQUISITION_MODE,
	SCPI_CMD_SET_ACQUISITION_MODE,
	SCPI_CMD_GET_ARITHMETICS_TYPE,
	SCPI_CMD_SET_ARITHMETICS_TYPE,
	SCPI_CMD_GET_INTERPOLATION_MODE,
	SCPI_CMD_SET_INTERPOLATION_MODE,
	SCPI_CMD_GET_PROBE_UNIT,
	SCPI_CMD_GET_ANALOG_THRESHOLD,
	SCPI_CMD_SET_ANALOG_THRESHOLD,
	SCPI_CMD_GET_DIG_POD_THRESHOLD,
	SCPI_CMD_SET_DIG_POD_THRESHOLD,
	SCPI_CMD_GET_DIG_POD_USER_THRESHOLD,
	SCPI_CMD_SET_DIG_POD_USER_THRESHOLD,
	SCPI_CMD_GET_BANDWIDTH_LIMIT,
	SCPI_CMD_SET_BANDWIDTH_LIMIT,
	SCPI_CMD_GET_MATH_EXPRESSION,
	SCPI_CMD_SET_MATH_EXPRESSION,
	SCPI_CMD_GET_FFT_SAMPLE_RATE,
	SCPI_CMD_SET_FFT_SAMPLE_RATE,
	SCPI_CMD_GET_FFT_WINDOW_TYPE,
	SCPI_CMD_SET_FFT_WINDOW_TYPE,
	SCPI_CMD_GET_FFT_FREQUENCY_START,
	SCPI_CMD_SET_FFT_FREQUENCY_START,
	SCPI_CMD_GET_FFT_FREQUENCY_STOP,
	SCPI_CMD_SET_FFT_FREQUENCY_STOP,
	SCPI_CMD_GET_FFT_FREQUENCY_SPAN,
	SCPI_CMD_SET_FFT_FREQUENCY_SPAN,
	SCPI_CMD_GET_FFT_FREQUENCY_CENTER,
	SCPI_CMD_SET_FFT_FREQUENCY_CENTER,
	SCPI_CMD_GET_FFT_RESOLUTION_BW,
	SCPI_CMD_SET_FFT_RESOLUTION_BW,
	SCPI_CMD_GET_FFT_SPAN_RBW_COUPLING,
	SCPI_CMD_SET_FFT_SPAN_RBW_COUPLING,
	SCPI_CMD_GET_FFT_SPAN_RBW_RATIO,
	SCPI_CMD_SET_FFT_SPAN_RBW_RATIO,
	SCPI_CMD_GET_FFT_DATA,
	SCPI_CMD_GET_SYS_BEEP_ON_TRIGGER,
	SCPI_CMD_SET_SYS_BEEP_ON_TRIGGER,
	SCPI_CMD_GET_SYS_BEEP_ON_ERROR,
	SCPI_CMD_SET_SYS_BEEP_ON_ERROR,
};

/*
 * Meaning of the 8 bits that are used by the
 * Event Status Register (ESR). Some of them
 * correspond to the different possible SCPI
 * errors (Bit 2-5).
 */
enum scpi_esr {
	operation_complete	= (1u << 0), /* Bit 0 */
	not_used		= (1u << 1), /* Bit 1 */
	query_error		= (1u << 2), /* Bit 2, SCPI err: -400 to -499 */
	device_dependent_error	= (1u << 3), /* Bit 3, SCPI err: -300 to -399 */
	execution_error		= (1u << 4), /* Bit 4, SCPI err: -200 to -299 */
	command_error		= (1u << 5), /* Bit 5, SCPI err: -100 to -199 */
	user_request		= (1u << 6), /* Bit 6 */
	power_on		= (1u << 7), /* Bit 7 */
};

struct scpi_command {
	int command;
	const char *string;
};

struct sr_scpi_hw_info {
	char *manufacturer;
	char *model;
	char *serial_number;
	char *firmware_version;
};

struct sr_scpi_dev_inst {
	const char *name;
	const char *prefix;
	int priv_size;
	GSList *(*scan)(struct drv_context *drvc);
	int (*dev_inst_new)(void *priv, struct drv_context *drvc,
		const char *resource, char **params, const char *serialcomm);
	int (*open)(struct sr_scpi_dev_inst *scpi);
	int (*source_add)(struct sr_session *session, void *priv, int events,
		int timeout, sr_receive_data_callback cb, void *cb_data);
	int (*source_remove)(struct sr_session *session, void *priv);
	int (*send)(void *priv, const char *command);
	int (*read_begin)(void *priv);
	int (*read_data)(void *priv, char *buf, int maxlen);
	int (*write_data)(void *priv, char *buf, int len);
	int (*read_complete)(void *priv);
	int (*close)(struct sr_scpi_dev_inst *scpi);
	void (*free)(void *priv);
	unsigned int read_timeout_us;
	void *priv;
	/* Only used for quirk workarounds, notably the Rigol DS1000 series. */
	uint64_t firmware_version;
	GMutex scpi_mutex;
	char *actual_channel_name;
};

SR_PRIV GSList *sr_scpi_scan(struct drv_context *drvc, GSList *options,
		struct sr_dev_inst *(*probe_device)(struct sr_scpi_dev_inst *scpi));
SR_PRIV struct sr_scpi_dev_inst *scpi_dev_inst_new(struct drv_context *drvc,
		const char *resource, const char *serialcomm);
SR_PRIV int sr_scpi_open(struct sr_scpi_dev_inst *scpi);
SR_PRIV int sr_scpi_source_add(struct sr_session *session,
		struct sr_scpi_dev_inst *scpi, int events, int timeout,
		sr_receive_data_callback cb, void *cb_data);
SR_PRIV int sr_scpi_source_remove(struct sr_session *session,
		struct sr_scpi_dev_inst *scpi);
SR_PRIV int sr_scpi_send(struct sr_scpi_dev_inst *scpi,
		const char *format, ...);
SR_PRIV int sr_scpi_send_variadic(struct sr_scpi_dev_inst *scpi,
		const char *format, va_list args);
SR_PRIV int sr_scpi_read_begin(struct sr_scpi_dev_inst *scpi);
SR_PRIV int sr_scpi_read_data(struct sr_scpi_dev_inst *scpi, char *buf, int maxlen);
SR_PRIV int sr_scpi_write_data(struct sr_scpi_dev_inst *scpi, char *buf, int len);
SR_PRIV int sr_scpi_read_complete(struct sr_scpi_dev_inst *scpi);
SR_PRIV int sr_scpi_close(struct sr_scpi_dev_inst *scpi);
SR_PRIV void sr_scpi_free(struct sr_scpi_dev_inst *scpi);

SR_PRIV int sr_scpi_read_response(struct sr_scpi_dev_inst *scpi,
			GString *response, gint64 abs_timeout_us);
SR_PRIV int sr_scpi_get_string(struct sr_scpi_dev_inst *scpi,
			const char *command, char **scpi_response);
SR_PRIV int sr_scpi_get_bool(struct sr_scpi_dev_inst *scpi,
			const char *command, gboolean *scpi_response);
SR_PRIV int sr_scpi_get_int(struct sr_scpi_dev_inst *scpi,
			const char *command, int *scpi_response);
SR_PRIV int sr_scpi_get_float(struct sr_scpi_dev_inst *scpi,
			const char *command, float *scpi_response);
SR_PRIV int sr_scpi_get_double(struct sr_scpi_dev_inst *scpi,
			const char *command, double *scpi_response);
SR_PRIV int sr_scpi_get_opc(struct sr_scpi_dev_inst *scpi);
SR_PRIV int sr_scpi_get_esr(struct sr_scpi_dev_inst *scpi, int *scpi_response);
SR_PRIV int sr_scpi_get_floatv(struct sr_scpi_dev_inst *scpi,
			const char *command, GArray **scpi_response);
SR_PRIV int sr_scpi_get_uint8v(struct sr_scpi_dev_inst *scpi,
			const char *command, GArray **scpi_response);
SR_PRIV int sr_scpi_get_data(struct sr_scpi_dev_inst *scpi,
			const char *command, GString **scpi_response);
SR_PRIV int sr_scpi_get_block(struct sr_scpi_dev_inst *scpi,
			const char *command, GByteArray **scpi_response);
SR_PRIV int sr_scpi_get_hw_id(struct sr_scpi_dev_inst *scpi,
			struct sr_scpi_hw_info **scpi_response);
SR_PRIV void sr_scpi_hw_info_free(struct sr_scpi_hw_info *hw_info);

SR_PRIV const char *sr_scpi_unquote_string(char *s);

SR_PRIV const char *sr_vendor_alias(const char *raw_vendor);
SR_PRIV const char *sr_scpi_cmd_get(const struct scpi_command *cmdtable,
		int command);
SR_PRIV int sr_scpi_cmd(const struct sr_dev_inst *sdi,
		const struct scpi_command *cmdtable,
		int channel_command, const char *channel_name,
		int command, ...);
SR_PRIV int sr_scpi_cmd_resp(const struct sr_dev_inst *sdi,
		const struct scpi_command *cmdtable,
		int channel_command, const char *channel_name,
		GVariant **gvar, const GVariantType *gvtype, int command, ...);

#endif
