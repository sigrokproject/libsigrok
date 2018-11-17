/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2018 Gerhard Sittig <gerhard.sittig@gmx.net>
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
#include <string.h>
#include "protocol.h"

#define WITH_CMD_DELAY 0	/* TODO See which devices need delays. */

SR_PRIV void scpi_dmm_cmd_delay(struct sr_scpi_dev_inst *scpi)
{
	if (WITH_CMD_DELAY)
		g_usleep(WITH_CMD_DELAY * 1000);
	sr_scpi_get_opc(scpi);
}

SR_PRIV const struct mqopt_item *scpi_dmm_lookup_mq_number(
	const struct sr_dev_inst *sdi, enum sr_mq mq, enum sr_mqflag flag)
{
	struct dev_context *devc;
	size_t i;
	const struct mqopt_item *item;

	devc = sdi->priv;
	for (i = 0; i < devc->model->mqopt_size; i++) {
		item = &devc->model->mqopts[i];
		if (item->mq != mq || item->mqflag != flag)
			continue;
		return item;
	}

	return NULL;
}

SR_PRIV const struct mqopt_item *scpi_dmm_lookup_mq_text(
	const struct sr_dev_inst *sdi, const char *text)
{
	struct dev_context *devc;
	size_t i;
	const struct mqopt_item *item;

	devc = sdi->priv;
	for (i = 0; i < devc->model->mqopt_size; i++) {
		item = &devc->model->mqopts[i];
		if (!item->scpi_func_query || !item->scpi_func_query[0])
			continue;
		if (!g_str_has_prefix(text, item->scpi_func_query))
			continue;
		return item;
	}

	return NULL;
}

SR_PRIV int scpi_dmm_get_mq(const struct sr_dev_inst *sdi,
	enum sr_mq *mq, enum sr_mqflag *flag, char **rsp,
	const struct mqopt_item **mqitem)
{
	struct dev_context *devc;
	const char *command;
	char *response;
	const char *have;
	int ret;
	const struct mqopt_item *item;

	devc = sdi->priv;
	if (mq)
		*mq = 0;
	if (flag)
		*flag = 0;
	if (rsp)
		*rsp = NULL;
	if (mqitem)
		*mqitem = NULL;

	scpi_dmm_cmd_delay(sdi->conn);
	command = sr_scpi_cmd_get(devc->cmdset, DMM_CMD_QUERY_FUNC);
	if (!command || !*command)
		return SR_ERR_NA;
	response = NULL;
	ret = sr_scpi_get_string(sdi->conn, command, &response);
	if (ret != SR_OK)
		return ret;
	if (!response || !*response)
		return SR_ERR_NA;
	have = response;
	if (*have == '"')
		have++;

	ret = SR_ERR_NA;
	item = scpi_dmm_lookup_mq_text(sdi, have);
	if (item) {
		if (mq)
			*mq = item->mq;
		if (flag)
			*flag = item->mqflag;
		if (mqitem)
			*mqitem = item;
		ret = SR_OK;
	}

	if (rsp) {
		*rsp = response;
		response = NULL;
	}
	g_free(response);

	return ret;
}

SR_PRIV int scpi_dmm_set_mq(const struct sr_dev_inst *sdi,
	enum sr_mq mq, enum sr_mqflag flag)
{
	struct dev_context *devc;
	const struct mqopt_item *item;
	const char *mode, *command;
	int ret;

	devc = sdi->priv;
	item = scpi_dmm_lookup_mq_number(sdi, mq, flag);
	if (!item)
		return SR_ERR_NA;

	mode = item->scpi_func_setup;
	command = sr_scpi_cmd_get(devc->cmdset, DMM_CMD_SETUP_FUNC);
	scpi_dmm_cmd_delay(sdi->conn);
	ret = sr_scpi_send(sdi->conn, command, mode);
	if (ret != SR_OK)
		return ret;

	return SR_OK;
}

SR_PRIV int scpi_dmm_get_meas_agilent(const struct sr_dev_inst *sdi, size_t ch)
{
	struct sr_scpi_dev_inst *scpi;
	struct dev_context *devc;
	struct scpi_dmm_acq_info *info;
	struct sr_datafeed_analog *analog;
	int ret;
	enum sr_mq mq;
	enum sr_mqflag mqflag;
	char *mode_response;
	const char *p;
	char **fields;
	size_t count;
	char prec_text[20];
	const struct mqopt_item *item;
	int prec_exp;
	const char *command;
	char *response;
	gboolean use_double;
	int sig_digits, val_exp;
	int digits;
	enum sr_unit unit;

	scpi = sdi->conn;
	devc = sdi->priv;
	info = &devc->run_acq_info;
	analog = &info->analog[ch];

	/*
	 * Get the meter's current mode, keep the response around.
	 * Skip the measurement if the mode is uncertain.
	 */
	ret = scpi_dmm_get_mq(sdi, &mq, &mqflag, &mode_response, &item);
	if (ret != SR_OK) {
		g_free(mode_response);
		return ret;
	}
	if (!mode_response)
		return SR_ERR;
	if (!mq) {
		g_free(mode_response);
		return +1;
	}

	/*
	 * Get the last comma separated field of the function query
	 * response, or fallback to the model's default precision for
	 * the current function. This copes with either of these cases:
	 *   VOLT +1.00000E-01,+1.00000E-06
	 *   DIOD
	 *   TEMP THER,5000,+1.00000E+00,+1.00000E-01
	 */
	p = sr_scpi_unquote_string(mode_response);
	fields = g_strsplit(p, ",", 0);
	count = g_strv_length(fields);
	if (count >= 2) {
		snprintf(prec_text, sizeof(prec_text),
			"%s", fields[count - 1]);
		p = prec_text;
	} else if (!item) {
		p = NULL;
	} else if (item->default_precision == NO_DFLT_PREC) {
		p = NULL;
	} else {
		snprintf(prec_text, sizeof(prec_text),
			"1e%d", item->default_precision);
		p = prec_text;
	}
	g_strfreev(fields);

	/*
	 * Need to extract the exponent value ourselves, since a strtod()
	 * call will "eat" the exponent, too. Strip space, strip sign,
	 * strip float number (without! exponent), check for exponent
	 * and get exponent value. Accept absence of Esnn suffixes.
	 */
	while (p && *p && g_ascii_isspace(*p))
		p++;
	if (p && *p && (*p == '+' || *p == '-'))
		p++;
	while (p && *p && g_ascii_isdigit(*p))
		p++;
	if (p && *p && *p == '.')
		p++;
	while (p && *p && g_ascii_isdigit(*p))
		p++;
	ret = SR_OK;
	if (!p || !*p)
		prec_exp = 0;
	else if (*p != 'e' && *p != 'E')
		ret = SR_ERR_DATA;
	else
		ret = sr_atoi(++p, &prec_exp);
	g_free(mode_response);
	if (ret != SR_OK)
		return ret;

	/*
	 * Get the measurement value. Make sure to strip trailing space
	 * or else number conversion may fail in fatal ways. Detect OL
	 * conditions. Determine the measurement's precision: Count the
	 * number of significant digits before the period, and get the
	 * exponent's value.
	 *
	 * The text presentation of values is like this:
	 *   +1.09450000E-01
	 * Skip space/sign, count digits before the period, skip to the
	 * exponent, get exponent value.
	 *
	 * TODO Can sr_parse_rational() return the exponent for us? In
	 * addition to providing a precise rational value instead of a
	 * float that's an approximation of the received value? Can the
	 * 'analog' struct that we fill in carry rationals?
	 *
	 * Use double precision FP here during conversion. Optionally
	 * downgrade to single precision later to reduce the amount of
	 * logged information.
	 */
	command = sr_scpi_cmd_get(devc->cmdset, DMM_CMD_QUERY_VALUE);
	if (!command || !*command)
		return SR_ERR_NA;
	scpi_dmm_cmd_delay(scpi);
	ret = sr_scpi_get_string(scpi, command, &response);
	if (ret != SR_OK)
		return ret;
	g_strstrip(response);
	use_double = devc->model->digits > 6;
	ret = sr_atod_ascii(response, &info->d_value);
	if (ret != SR_OK) {
		g_free(response);
		return ret;
	}
	if (!response)
		return SR_ERR;
	if (info->d_value > +9e37) {
		info->d_value = +INFINITY;
	} else if (info->d_value < -9e37) {
		info->d_value = -INFINITY;
	} else {
		p = response;
		while (p && *p && g_ascii_isspace(*p))
			p++;
		if (p && *p && (*p == '-' || *p == '+'))
			p++;
		sig_digits = 0;
		while (p && *p && g_ascii_isdigit(*p)) {
			sig_digits++;
			p++;
		}
		if (p && *p && *p == '.')
			p++;
		while (p && *p && g_ascii_isdigit(*p))
			p++;
		ret = SR_OK;
		if (!p || !*p)
			val_exp = 0;
		else if (*p != 'e' && *p != 'E')
			ret = SR_ERR_DATA;
		else
			ret = sr_atoi(++p, &val_exp);
	}
	g_free(response);
	if (ret != SR_OK)
		return ret;
	/*
	 * TODO Come up with the most appropriate 'digits' calculation.
	 * This implementation assumes that either the device provides
	 * the resolution with the query for the meter's function, or
	 * the driver uses a fallback text pretending the device had
	 * provided it. This works with supported Agilent devices.
	 *
	 * An alternative may be to assume a given digits count which
	 * depends on the device, and adjust that count based on the
	 * value's significant digits and exponent. But this approach
	 * fails if devices change their digits count depending on
	 * modes or user requests, and also fails when e.g. devices
	 * with "100000 counts" can provide values between 100000 and
	 * 120000 in either 4 or 5 digits modes, depending on the most
	 * recent trend of the values. This less robust approach should
	 * only be taken if the mode inquiry won't yield the resolution
	 * (as e.g. DIOD does on 34405A, though we happen to know the
	 * fixed resolution for this very mode on this very model).
	 *
	 * For now, let's keep the prepared code path for the second
	 * approach in place, should some Agilent devices need it yet
	 * benefit from re-using most of the remaining acquisition
	 * routine.
	 */
#if 1
	digits = -prec_exp;
#else
	digits = devc->model->digits;
	digits -= sig_digits;
	digits -= val_exp;
#endif

	/*
	 * Fill in the 'analog' description: value, encoding, meaning.
	 * Callers will fill in the sample count, and channel name,
	 * and will send out the packet.
	 */
	if (use_double) {
		analog->data = &info->d_value;
		analog->encoding->unitsize = sizeof(info->d_value);
	} else {
		info->f_value = info->d_value;
		analog->data = &info->f_value;
		analog->encoding->unitsize = sizeof(info->f_value);
	}
	analog->encoding->is_float = TRUE;
#ifdef WORDS_BIGENDIAN
	analog->encoding->is_bigendian = TRUE;
#else
	analog->encoding->is_bigendian = FALSE;
#endif
	analog->encoding->digits = digits;
	analog->meaning->mq = mq;
	analog->meaning->mqflags = mqflag;
	switch (mq) {
	case SR_MQ_VOLTAGE:
		unit = SR_UNIT_VOLT;
		break;
	case SR_MQ_CURRENT:
		unit = SR_UNIT_AMPERE;
		break;
	case SR_MQ_RESISTANCE:
	case SR_MQ_CONTINUITY:
		unit = SR_UNIT_OHM;
		break;
	case SR_MQ_CAPACITANCE:
		unit = SR_UNIT_FARAD;
		break;
	case SR_MQ_TEMPERATURE:
		unit = SR_UNIT_CELSIUS;
		break;
	case SR_MQ_FREQUENCY:
		unit = SR_UNIT_HERTZ;
		break;
	default:
		return SR_ERR_NA;
	}
	analog->meaning->unit = unit;
	analog->spec->spec_digits = digits;

	return SR_OK;
}

/* Strictly speaking this is a timer controlled poll routine. */
SR_PRIV int scpi_dmm_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct sr_scpi_dev_inst *scpi;
	struct dev_context *devc;
	struct scpi_dmm_acq_info *info;
	gboolean sent_sample;
	size_t ch;
	struct sr_channel *channel;
	int ret;

	(void)fd;
	(void)revents;

	sdi = cb_data;
	if (!sdi)
		return TRUE;
	scpi = sdi->conn;
	devc = sdi->priv;
	if (!scpi || !devc)
		return TRUE;
	info = &devc->run_acq_info;

	sent_sample = FALSE;
	ret = SR_OK;
	for (ch = 0; ch < devc->num_channels; ch++) {
		/* Check the channel's enabled status. */
		channel = g_slist_nth_data(sdi->channels, ch);
		if (!channel->enabled)
			continue;

		/*
		 * Prepare an analog measurement value. Note that digits
		 * will get updated later.
		 */
		info->packet.type = SR_DF_ANALOG;
		info->packet.payload = &info->analog[ch];
		sr_analog_init(&info->analog[ch], &info->encoding[ch],
			&info->meaning[ch], &info->spec[ch], 0);

		/* Just check OPC before sending another request. */
		scpi_dmm_cmd_delay(sdi->conn);

		/*
		 * Have the model take and interpret a measurement. Lack
		 * of support is pointless, failed retrieval/conversion
		 * is considered fatal. The routine will fill in the
		 * 'analog' details, except for channel name and sample
		 * count (assume one value per channel).
		 *
		 * Note that non-zero non-negative return codes signal
		 * that the channel's data shell get skipped in this
		 * iteration over the channels. This copes with devices
		 * or modes where channels may provide data at different
		 * rates.
		 */
		if (!devc->model->get_measurement) {
			ret = SR_ERR_NA;
			break;
		}
		ret = devc->model->get_measurement(sdi, ch);
		if (ret > 0)
			continue;
		if (ret != SR_OK)
			break;

		/* Send the packet that was filled in by the model's routine. */
		info->analog[ch].num_samples = 1;
		info->analog[ch].meaning->channels = g_slist_append(NULL, channel);
		sr_session_send(sdi, &info->packet);
		g_slist_free(info->analog[ch].meaning->channels);
		sent_sample = TRUE;
	}
	if (sent_sample)
		sr_sw_limits_update_samples_read(&devc->limits, 1);
	if (ret != SR_OK) {
		/* Stop acquisition upon communication or data errors. */
		sr_dev_acquisition_stop(sdi);
		return TRUE;
	}
	if (sr_sw_limits_check(&devc->limits))
		sr_dev_acquisition_stop(sdi);

	return TRUE;
}
