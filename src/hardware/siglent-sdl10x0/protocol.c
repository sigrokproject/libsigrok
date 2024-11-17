/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2024 Timo Boettcher <timo@timoboettcher.name>
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
#include "scpi.h"
#include "protocol.h"

SR_PRIV const char *siglent_sdl10x0_mode_to_string(enum siglent_sdl10x0_modes mode)
{
	switch (mode) {
	case CC:
		return "CC";
	case CV:
		return "CV";
	case CP:
		return "CP";
	case CR:
		return "CR";
	default:
		return "Unknown";
	}
}

SR_PRIV const char *siglent_sdl10x0_mode_to_longstring(enum siglent_sdl10x0_modes mode)
{
	switch (mode) {
	case CC:
		return "CURRENT";
	case CV:
		return "VOLTAGE";
	case CP:
		return "POWER";
	case CR:
		return "RESISTANCE";
	default:
		return "Unknown";
	}
}


SR_PRIV int siglent_sdl10x0_string_to_mode(const char *modename, enum siglent_sdl10x0_modes *mode)
{
	size_t i;
	const char *s;

	for (i = 0; i < SDL10x0_MODES; i++) {
		s = siglent_sdl10x0_mode_to_string(i);
		if (strncmp(modename, s, strlen(s)) == 0) {
			*mode = i;
			return SR_OK;
		}
	}

	return SR_ERR;
}


SR_PRIV void siglent_sdl10x0_send_value(const struct sr_dev_inst *sdi, float value, enum sr_mq mq, enum sr_mqflag mqflags, enum sr_unit unit, int digits)
{
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;

	sr_analog_init(&analog, &encoding, &meaning, &spec, digits);
	analog.meaning->channels = sdi->channels;
	analog.num_samples = 1;
	analog.data = &value;
	analog.encoding->unitsize = sizeof(value);
	analog.encoding->is_float = TRUE;
	/* Are we on a little or big endian system? */
#ifdef WORDS_BIGENDIAN
		analog.encoding->is_bigendian = TRUE;
#else
		analog.encoding->is_bigendian = FALSE;
#endif
	analog.meaning->mq = mq;
	analog.meaning->unit = unit;
	analog.meaning->mqflags = mqflags;

	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	sr_session_send(sdi, &packet);
}

/* Gets invoked when RX data is available. */

SR_PRIV int siglent_sdl10x0_receive_data(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int ret;
	float ival;

	devc = sdi->priv;

	switch (devc->acq_state) {
	case ACQ_REQUESTED_VOLTAGE:
		/* Voltage was requested, read result */
		ret = sr_scpi_get_float(sdi->conn, NULL, &ival);
		if (ret != SR_OK)
			return SR_ERR;
		devc->voltage = ival;

		/* Now get next Value: Current */
		sr_scpi_send(sdi->conn, "MEAS:CURR?");
		devc->acq_state = ACQ_REQUESTED_CURRENT;
		break;

	case ACQ_REQUESTED_CURRENT:
		/* Current was requested, read result */
		ret = sr_scpi_get_float(sdi->conn, NULL, &ival);
		if (ret != SR_OK)
			return SR_ERR;
		devc->current = ival;

		/* All values received, now build a frame */
		std_session_send_df_frame_begin(sdi);
		siglent_sdl10x0_send_value(sdi, devc->voltage, SR_MQ_VOLTAGE, SR_MQFLAG_DC, SR_UNIT_VOLT, 7);
		siglent_sdl10x0_send_value(sdi, devc->current, SR_MQ_CURRENT, SR_MQFLAG_DC, SR_UNIT_AMPERE, 7);
		std_session_send_df_frame_end(sdi);
		sr_sw_limits_update_samples_read(&devc->limits, 1);

		/* Now get next Value: Voltage */
		sr_scpi_send(sdi->conn, "MEAS:VOLT?");
		devc->acq_state = ACQ_REQUESTED_VOLTAGE;
		break;

	default:
		return FALSE;
	}

	return TRUE;
}

SR_PRIV int siglent_sdl10x0_handle_events(int fd, int revents, void *cb_data)
{
	struct dev_context *devc;
	struct sr_dev_inst *sdi;

	(void)fd;
	(void)revents;

	sdi = cb_data;
	if (!sdi)
		return TRUE;

	devc = sdi->priv;
	if (!devc) {
		return TRUE;
	}

	if (revents & G_IO_IN) {
		siglent_sdl10x0_receive_data(sdi);
	} else {
		return FALSE;
	}

	if (sr_sw_limits_check(&devc->limits)) {
		sr_dev_acquisition_stop(sdi);
		return FALSE;
	}

	return TRUE;
}
