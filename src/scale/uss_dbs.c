/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2015 Uwe Hermann <uwe@hermann-uwe.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * U.S. Solid scale protocol parser.
 */

#include <config.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "uss-dbs"

SR_PRIV gboolean sr_uss_dbs_packet_valid(const uint8_t *buf)
{
	return (buf[0] == '+' || buf[0] == '-')
		&& buf[1] == ' '
		&& buf[15] == '\r'
		&& buf[16] == '\n';
}

struct unit {
	const char name[3];
	int exponent;
	enum sr_unit value;
	enum sr_mqflag flags;
	int ratios[4];
};

static const struct unit units[] = {
	{"g  ", 0, SR_UNIT_GRAM, 0, {}},
	{"Kg ", 3, SR_UNIT_GRAM, 0, {}},
	{"ct ", 0, SR_UNIT_CARAT, 0, {}},
	{"T  ", 0, SR_UNIT_TOLA, 0, {}},
	{"TAR", 0, SR_UNIT_TOLA, 0, {1, 16, 6, 10}}, /* tola, aana, ratti, ratti/10 */
	{"dr ", 0, SR_UNIT_DRAM, 0, {}},
	/* Ratios from experimentation. No idea what the base unit is, so it's not supported yet. */
	/* 10. 0.0.0PKT == 121.50g == 10. 5.0.0TMR*/
	{"PKT", 0, 0, 0, {1, 12, 8, 10}},
	{"GN ", 0, SR_UNIT_GRAIN, 0, {}},
	{"TMR", 0, SR_UNIT_TOLA, 0, {1, 12, 8, 10}}, /* tolā, māshā, rattī, rattī/10 */
	/* My particular model seems to assume 10 cm^2 samples, so it's just centigrams. */
	{"gsm", 0, SR_UNIT_GRAMMAGE, 0, {}},
	{"tIJ", 0, SR_UNIT_TAEL, SR_MQFLAG_TAEL_JAPAN, {}},
	{"mo ", 0, SR_UNIT_MOMME, 0, {}},
	{"dwt", 0, SR_UNIT_PENNYWEIGHT, 0, {}},
	{"oz ", 0, SR_UNIT_OUNCE, 0, {}},
	{"lb ", 0, SR_UNIT_POUND, 0, {}},
	{"tIT", 0, SR_UNIT_TAEL, SR_MQFLAG_TAEL_TAIWAN, {}},
	{"ozt", 0, SR_UNIT_TROY_OUNCE, 0, {}},
	{"tIH", 0, SR_UNIT_TAEL, SR_MQFLAG_TAEL_HONGKONG_TROY, {}},
	{"%  ", 0, SR_UNIT_PERCENTAGE, 0, {}},
	{"pcs", 0, SR_UNIT_PIECE, 0, {}},
};

static const struct unit *parse_unit(const uint8_t *buf)
{
	const struct unit *u = units;
	while (u < units + ARRAY_SIZE(units)) {
		if (!memcmp(u->name, buf, sizeof(units[0].name)))
			return u;
		u++;
	}
	return NULL;
}

static enum sr_error_code parse_decimal(const uint8_t *buf, double *result, int *digits)
{
	uint8_t withSign[12];
	const uint8_t *end = buf+11; /* The last byte that's part of the value. */
	const uint8_t *s = end;

	while (isdigit(*s) || *s == '.')
		s--;

	withSign[0] = *buf;
	memcpy(withSign + 1, s + 1, end - s);
	withSign[end - s + 1] = '\0';

	return sr_atod_ascii_digits((const char *)withSign, result, digits);
}

static enum sr_error_code parse_component(const uint8_t **buf, double *result)
{
	char toParse[4];
	const uint8_t *orig = *buf;

	while (isdigit(**buf) || **buf == ' ')
		(*buf)--;

	memcpy(toParse, *buf + 1, orig - *buf);
	toParse[orig - *buf] = '\0';

	return sr_atod_ascii((const char *)toParse, result);
}

static enum sr_error_code parse_rational(const uint8_t *buf, const struct unit *u, double *result, int *digits)
{
	int err, f = 1;
	double n, ret = 0;
	size_t i = ARRAY_SIZE(u->ratios);
	const uint8_t *s = buf+11; /* The last byte that's part of the value. */

	/* We parse the four '.'-delimited numbers separately from least to
	 * most significant, adding them up in units of the least significant
	 * number. The final sum gets devided by the product of all the
	 * divisions ratios to yield a result in the expected unit. */
	while (i > 0 && s >= buf) {
		if ((err = parse_component(&s, &n))) {
			return err;
		}
		ret += n * f;
		f *= u->ratios[i-1];
		if (i > 1 && *s != '.')
			return SR_ERR_DATA;
		s--;
		i--;
	}

	if (buf[0] == '-')
		ret = -ret;
	*result = ret / f;
	*digits = floor(log(f));

	return SR_OK;
}

static enum sr_error_code parse_value(const uint8_t *buf, const struct unit *u, double *result, int *digits)
{
	if (u->ratios[0] == 0) {
		return parse_decimal(buf, result, digits);
	}

	return parse_rational(buf, u, result, digits);
}

SR_PRIV enum sr_error_code sr_uss_dbs_parse(const uint8_t *buf, struct sr_datafeed_analog *analog, double *result)
{
	int ret, i, digits = 0;
	char c;
	const struct unit *u;

	analog->meaning->mq = SR_MQ_MASS;
	u = parse_unit(buf+12);
	if (!u || !u->value)
		return SR_ERR_DATA;
	analog->meaning->unit = u->value;
	analog->meaning->mqflags |= u->flags;

	c = buf[0];
	if (c == '~' || c == '_') { /* over- and under-flow, respectively */
		for (i = 1; i < 10; i++) {
			if (buf[i] != c)
				return SR_ERR_DATA;
		}
		*result = c == '~' ? INFINITY : -INFINITY;
		return SR_OK;
	}

	ret = parse_value(buf, u, result, &digits);
	if (ret)
		return ret;

	*result *= powf(10, u->exponent);
	digits -= u->exponent;

	analog->encoding->digits = digits;
	analog->spec->spec_digits = digits;

	return SR_OK;
}
