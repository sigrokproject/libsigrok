/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2017-2018 Frank Stettner <frank-stettner@gmx.net>
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
#include <math.h>
#include <stdlib.h>
#include "scpi.h"
#include "protocol.h"

static int set_mq_volt(struct sr_scpi_dev_inst *scpi, enum sr_mqflag flags);
static int set_mq_amp(struct sr_scpi_dev_inst *scpi, enum sr_mqflag flags);
static int set_mq_ohm(struct sr_scpi_dev_inst *scpi, enum sr_mqflag flags);

static const struct {
	enum sr_mq mq;
	int (*set_mode)(struct sr_scpi_dev_inst *scpi, enum sr_mqflag flags);
} sr_mq_to_cmd_map[] = {
	{ SR_MQ_VOLTAGE, set_mq_volt },
	{ SR_MQ_CURRENT, set_mq_amp },
	{ SR_MQ_RESISTANCE, set_mq_ohm },
};

static int set_mq_volt(struct sr_scpi_dev_inst *scpi, enum sr_mqflag flags)
{
	const char *cmd;

	if ((flags & SR_MQFLAG_AC) != SR_MQFLAG_AC &&
		(flags & SR_MQFLAG_DC) != SR_MQFLAG_DC)
		return SR_ERR_NA;

	if ((flags & SR_MQFLAG_AC) == SR_MQFLAG_AC)
		cmd = "F2";
	else
		cmd = "F1";

	return sr_scpi_send(scpi, "%s", cmd);
}

static int set_mq_amp(struct sr_scpi_dev_inst *scpi, enum sr_mqflag flags)
{
	const char *cmd;

	if ((flags & SR_MQFLAG_AC) != SR_MQFLAG_AC &&
		(flags & SR_MQFLAG_DC) != SR_MQFLAG_DC)
		return SR_ERR_NA;

	if (flags & SR_MQFLAG_AC)
		cmd = "F6";
	else
		cmd = "F5";

	return sr_scpi_send(scpi, "%s", cmd);
}

static int set_mq_ohm(struct sr_scpi_dev_inst *scpi, enum sr_mqflag flags)
{
	const char *cmd;

	if (flags & SR_MQFLAG_FOUR_WIRE)
		cmd = "F4";
	else
		cmd = "F3";

	return sr_scpi_send(scpi, "%s", cmd);
}

SR_PRIV int hp_3478a_set_mq(const struct sr_dev_inst *sdi, enum sr_mq mq,
				enum sr_mqflag mq_flags)
{
	int ret;
	size_t i;
	struct sr_scpi_dev_inst *scpi = sdi->conn;
	struct dev_context *devc = sdi->priv;

	/* No need to send command if we're not changing measurement type. */
	if (devc->measurement_mq == mq &&
		((devc->measurement_mq_flags & mq_flags) == mq_flags))
		return SR_OK;

	for (i = 0; i < ARRAY_SIZE(sr_mq_to_cmd_map); i++) {
		if (sr_mq_to_cmd_map[i].mq != mq)
			continue;

		ret = sr_mq_to_cmd_map[i].set_mode(scpi, mq_flags);
		if (ret != SR_OK)
			return ret;

		ret = hp_3478a_get_status_bytes(sdi);
		return ret;
	}

	return SR_ERR_NA;
}

static int parse_range_vdc(struct dev_context *devc, uint8_t range_byte)
{
	if ((range_byte & SB1_RANGE_BLOCK) == RANGE_VDC_30MV) {
		devc->enc_digits = devc->spec_digits - 2;
	} else if ((range_byte & SB1_RANGE_BLOCK) == RANGE_VDC_300MV) {
		devc->enc_digits = devc->spec_digits - 3;
	} else if ((range_byte & SB1_RANGE_BLOCK) == RANGE_VDC_3V) {
		devc->enc_digits = devc->spec_digits - 1;
	} else if ((range_byte & SB1_RANGE_BLOCK) == RANGE_VDC_30V) {
		devc->enc_digits = devc->spec_digits - 2;
	} else if ((range_byte & SB1_RANGE_BLOCK) == RANGE_VDC_300V) {
		devc->enc_digits = devc->spec_digits - 3;
	} else {
		return SR_ERR_DATA;
	}

	return SR_OK;
}

static int parse_range_vac(struct dev_context *devc, uint8_t range_byte)
{
	if ((range_byte & SB1_RANGE_BLOCK) == RANGE_VAC_300MV) {
		devc->enc_digits = devc->spec_digits - 3;
	} else if ((range_byte & SB1_RANGE_BLOCK) == RANGE_VAC_3V) {
		devc->enc_digits = devc->spec_digits - 1;
	} else if ((range_byte & SB1_RANGE_BLOCK) == RANGE_VAC_30V) {
		devc->enc_digits = devc->spec_digits - 2;
	} else if ((range_byte & SB1_RANGE_BLOCK) == RANGE_VAC_300V) {
		devc->enc_digits = devc->spec_digits - 3;
	} else {
		return SR_ERR_DATA;
	}

	return SR_OK;
}

static int parse_range_a(struct dev_context *devc, uint8_t range_byte)
{
	if ((range_byte & SB1_RANGE_BLOCK) == RANGE_A_300MA) {
		devc->enc_digits = devc->spec_digits - 3;
	} else if ((range_byte & SB1_RANGE_BLOCK) == RANGE_A_3A) {
		devc->enc_digits = devc->spec_digits - 1;
	} else {
		return SR_ERR_DATA;
	}

	return SR_OK;
}

static int parse_range_ohm(struct dev_context *devc, uint8_t range_byte)
{
	if ((range_byte & SB1_RANGE_BLOCK) == RANGE_OHM_30R) {
		devc->enc_digits = devc->spec_digits - 2;
	} else if ((range_byte & SB1_RANGE_BLOCK) == RANGE_OHM_300R) {
		devc->enc_digits = devc->spec_digits - 3;
	} else if ((range_byte & SB1_RANGE_BLOCK) == RANGE_OHM_3KR) {
		devc->enc_digits = devc->spec_digits - 1;
	} else if ((range_byte & SB1_RANGE_BLOCK) == RANGE_OHM_30KR) {
		devc->enc_digits = devc->spec_digits - 2;
	} else if ((range_byte & SB1_RANGE_BLOCK) == RANGE_OHM_300KR) {
		devc->enc_digits = devc->spec_digits - 3;
	} else if ((range_byte & SB1_RANGE_BLOCK) == RANGE_OHM_3MR) {
		devc->enc_digits = devc->spec_digits - 1;
	} else if ((range_byte & SB1_RANGE_BLOCK) == RANGE_OHM_30MR) {
		devc->enc_digits = devc->spec_digits - 2;
	} else {
		return SR_ERR_DATA;
	}

	return SR_OK;
}

static int parse_function_byte(struct dev_context *devc, uint8_t function_byte)
{
	devc->measurement_mq_flags = 0;

	/* Function + Range */
	if ((function_byte & SB1_FUNCTION_BLOCK) == FUNCTION_VDC) {
		devc->measurement_mq = SR_MQ_VOLTAGE;
		devc->measurement_mq_flags |= SR_MQFLAG_DC;
		devc->measurement_unit = SR_UNIT_VOLT;
		parse_range_vdc(devc, function_byte);
	} else if ((function_byte & SB1_FUNCTION_BLOCK) == FUNCTION_VAC) {
		devc->measurement_mq = SR_MQ_VOLTAGE;
		devc->measurement_mq_flags |= SR_MQFLAG_AC | SR_MQFLAG_RMS;
		devc->measurement_unit = SR_UNIT_VOLT;
		parse_range_vac(devc, function_byte);
	} else if ((function_byte & SB1_FUNCTION_BLOCK) == FUNCTION_2WR) {
		devc->measurement_mq = SR_MQ_RESISTANCE;
		devc->measurement_unit = SR_UNIT_OHM;
		parse_range_ohm(devc, function_byte);
	} else if ((function_byte & SB1_FUNCTION_BLOCK) == FUNCTION_4WR) {
		devc->measurement_mq = SR_MQ_RESISTANCE;
		devc->measurement_mq_flags |= SR_MQFLAG_FOUR_WIRE;
		devc->measurement_unit = SR_UNIT_OHM;
		parse_range_ohm(devc, function_byte);
	} else if ((function_byte & SB1_FUNCTION_BLOCK) == FUNCTION_ADC) {
		devc->measurement_mq = SR_MQ_CURRENT;
		devc->measurement_mq_flags |= SR_MQFLAG_DC;
		devc->measurement_unit = SR_UNIT_AMPERE;
		parse_range_a(devc, function_byte);
	} else if ((function_byte & SB1_FUNCTION_BLOCK) == FUNCTION_AAC) {
		devc->measurement_mq = SR_MQ_CURRENT;
		devc->measurement_mq_flags |= SR_MQFLAG_AC | SR_MQFLAG_RMS;
		devc->measurement_unit = SR_UNIT_AMPERE;
		parse_range_a(devc, function_byte);
	} else if ((function_byte & SB1_FUNCTION_BLOCK) == FUNCTION_EXR) {
		devc->measurement_mq = SR_MQ_RESISTANCE;
		devc->measurement_unit = SR_UNIT_OHM;
		parse_range_ohm(devc, function_byte);
	}

	/* Digits / Resolution */
	if ((function_byte & SB1_DIGITS_BLOCK) == DIGITS_5_5) {
		devc->spec_digits = 5;
	} else if ((function_byte & SB1_DIGITS_BLOCK) == DIGITS_4_5) {
		devc->spec_digits = 4;
	} else if ((function_byte & SB1_DIGITS_BLOCK) == DIGITS_3_5) {
		devc->spec_digits = 3;
	}

	return SR_OK;
}

static int parse_status_byte(struct dev_context *devc, uint8_t status_byte)
{
	devc->trigger = TRIGGER_UNDEFINED;

	/* External Trigger */
	if ((status_byte & STATUS_EXT_TRIGGER) == STATUS_EXT_TRIGGER)
		devc->trigger = TRIGGER_EXTERNAL;

	/* Cal RAM */
	if ((status_byte & STATUS_CAL_RAM) == STATUS_CAL_RAM)
		devc->calibration = TRUE;
	else
		devc->calibration = FALSE;

	/* Front/Rear terminals */
	if ((status_byte & STATUS_FRONT_TERMINAL) == STATUS_FRONT_TERMINAL)
		devc->terminal = TERMINAL_FRONT;
	else
		devc->terminal = TERMINAL_REAR;

	/* 50Hz / 60Hz */
	if ((status_byte & STATUS_50HZ) == STATUS_50HZ)
		devc->line = LINE_50HZ;
	else
		devc->line = LINE_60HZ;

	/* Auto-Zero */
	if ((status_byte & STATUS_AUTO_ZERO) == STATUS_AUTO_ZERO)
		devc->auto_zero = TRUE;
	else
		devc->auto_zero = FALSE;

	/* Auto-Range */
	if ((status_byte & STATUS_AUTO_RANGE) == STATUS_AUTO_RANGE)
		devc->measurement_mq_flags |= SR_MQFLAG_AUTORANGE;
	else
		devc->measurement_mq_flags &= ~SR_MQFLAG_AUTORANGE;

	/* Internal trigger */
	if ((status_byte & STATUS_INT_TRIGGER) == STATUS_INT_TRIGGER)
		devc->trigger = TRIGGER_INTERNAL;

	return SR_OK;
}

static int parse_srq_byte(uint8_t sqr_byte)
{
	(void)sqr_byte;

#if 0
	/* The ServiceReQuest register isn't used at the moment. */

	/* PON SRQ */
	if ((sqr_byte & SRQ_POWER_ON) == SRQ_POWER_ON)
		sr_spew("Power On SRQ or clear msg received");

	/* Cal failed SRQ */
	if ((sqr_byte & SRQ_CAL_FAILED) == SRQ_CAL_FAILED)
		sr_spew("CAL failed SRQ");

	/* Keyboard SRQ */
	if ((sqr_byte & SRQ_KEYBORD) == SRQ_KEYBORD)
		sr_spew("Keyboard SRQ");

	/* Hardware error SRQ */
	if ((sqr_byte & SRQ_HARDWARE_ERR) == SRQ_HARDWARE_ERR)
		sr_spew("Hardware error SRQ");

	/* Syntax error SRQ */
	if ((sqr_byte & SRQ_SYNTAX_ERR) == SRQ_SYNTAX_ERR)
		sr_spew("Syntax error SRQ");

	/* Every reading is available to the bus SRQ */
	if ((sqr_byte & SRQ_BUS_AVAIL) == SRQ_BUS_AVAIL)
		sr_spew("Every reading is available to the bus SRQ");
#endif

	return SR_OK;
}

static int parse_error_byte(uint8_t error_byte)
{
	int ret;

	ret = SR_OK;

	/* A/D link */
	if ((error_byte & ERROR_AD_LINK) == ERROR_AD_LINK) {
		sr_err("Failure in the A/D link");
		ret = SR_ERR;
	}

	/* A/D Self Test */
	if ((error_byte & ERROR_AD_SELF_TEST) == ERROR_AD_SELF_TEST) {
		sr_err("A/D has failed its internal Self Test");
		ret = SR_ERR;
	}

	/* A/D slope error */
	if ((error_byte & ERROR_AD_SLOPE) == ERROR_AD_SLOPE) {
		sr_err("There has been an A/D slope error");
		ret = SR_ERR;
	}

	/* ROM Selt Test */
	if ((error_byte & ERROR_ROM_SELF_TEST) == ERROR_ROM_SELF_TEST) {
		sr_err("The ROM Self Test has failed");
		ret = SR_ERR;
	}

	/* RAM Selt Test */
	if ((error_byte & ERROR_RAM_SELF_TEST) == ERROR_RAM_SELF_TEST) {
		sr_err("The RAM Self Test has failed");
		ret = SR_ERR;
	}

	/* Selt Test */
	if ((error_byte & ERROR_SELF_TEST) == ERROR_SELF_TEST) {
		sr_err("Self Test: Any of the CAL RAM locations have bad "
		       "checksums, or a range with a bad checksum is selected");
		ret = SR_ERR;
	}

	return ret;
}

SR_PRIV int hp_3478a_get_status_bytes(const struct sr_dev_inst *sdi)
{
	int ret;
	char *response;
	uint8_t function_byte, status_byte, srq_byte, error_byte;
	struct sr_scpi_dev_inst *scpi = sdi->conn;
	struct dev_context *devc = sdi->priv;

	ret = sr_scpi_get_string(scpi, "B", &response);
	if (ret != SR_OK)
		return ret;

	if (!response)
		return SR_ERR;

	function_byte = (uint8_t)response[0];
	status_byte = (uint8_t)response[1];
	srq_byte = (uint8_t)response[2];
	error_byte = (uint8_t)response[3];

	g_free(response);

	parse_function_byte(devc, function_byte);
	parse_status_byte(devc, status_byte);
	parse_srq_byte(srq_byte);
	ret = parse_error_byte(error_byte);

	return ret;
}

static void acq_send_measurement(struct sr_dev_inst *sdi)
{
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	struct dev_context *devc;
	float f;

	devc = sdi->priv;

	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;

	sr_analog_init(&analog, &encoding, &meaning, &spec, devc->enc_digits);

	/* TODO: Implement NAN, depending on counts, range and value. */
	f = devc->measurement;
	analog.num_samples = 1;
	analog.data = &f;

	encoding.unitsize = sizeof(float);
	encoding.is_float = TRUE;
	encoding.digits = devc->enc_digits;

	meaning.mq = devc->measurement_mq;
	meaning.mqflags = devc->measurement_mq_flags;
	meaning.unit = devc->measurement_unit;
	meaning.channels = sdi->channels;

	spec.spec_digits = devc->spec_digits;

	sr_session_send(sdi, &packet);
}

SR_PRIV int hp_3478a_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_scpi_dev_inst *scpi;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;

	(void)fd;
	(void)revents;

	if (!(sdi = cb_data) || !(devc = sdi->priv))
		return TRUE;

	scpi = sdi->conn;

	/*
	 * This is necessary to get the actual range for the encoding digits.
	 * When SPoll is implemmented, this can be done via SPoll.
	 */
	if (hp_3478a_get_status_bytes(sdi) != SR_OK)
		return FALSE;

	/*
	 * TODO: Implement GPIB-SPoll, to get notified by a SRQ when a new
	 *       measurement is available. This is necessary, because when
	 *       switching ranges, there could be a timeout.
	 */
	if (sr_scpi_get_double(scpi, NULL, &devc->measurement) != SR_OK)
		return FALSE;

	acq_send_measurement(sdi);
	sr_sw_limits_update_samples_read(&devc->limits, 1);

	if (sr_sw_limits_check(&devc->limits))
		sr_dev_acquisition_stop(sdi);

	return TRUE;
}
