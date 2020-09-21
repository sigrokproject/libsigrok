/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2012 Alexandru Gagniuc <mr.nuke.me@gmail.com>
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

#define MAX_PACKET_LEN 22

/* Flags passed from the DMM. */
struct brymen_flags {
	gboolean is_low_batt, is_decibel, is_duty_cycle, is_hertz, is_amp;
	gboolean is_beep, is_ohm, is_fahrenheit, is_celsius, is_capacitance;
	gboolean is_diode, is_volt, is_dc, is_ac;
};

struct bm850_command {
	uint8_t dle;
	uint8_t stx;
	uint8_t cmd;
	uint8_t arg[2];
	uint8_t checksum;
	uint8_t dle2;
	uint8_t etx;
};

struct brymen_header {
	uint8_t dle;
	uint8_t stx;
	uint8_t cmd;
	uint8_t len;
};

struct brymen_tail {
	uint8_t checksum;
	uint8_t dle;
	uint8_t etx;
};

/*
 * We only have one command because we only support the BM-857. However, the
 * driver is easily extensible to support more models, as the protocols are
 * very similar.
 */
enum {
	BM_CMD_REQUEST_READING = 0x00,
};

static int bm_send_command(uint8_t command, uint8_t arg1, uint8_t arg2,
			   struct sr_serial_dev_inst *serial)
{
	struct bm850_command cmdout;
	int written;

	cmdout.dle = 0x10;
	cmdout.stx = 0x02;
	cmdout.cmd = command;
	cmdout.arg[0] = arg1;
	cmdout.arg[1] = arg2;
	cmdout.checksum = arg1 ^ arg2;
	cmdout.dle2 = 0x10;
	cmdout.etx = 0x03;

	/* TODO: How to compute the checksum? Hardware seems to ignore it. */

	/* Request reading. */
	written = serial_write_blocking(serial, &cmdout, sizeof(cmdout),
			serial_timeout(serial, sizeof(cmdout)));
	if (written != sizeof(cmdout))
		return SR_ERR;

	return SR_OK;
}

SR_PRIV int brymen_packet_request(struct sr_serial_dev_inst *serial)
{
	return bm_send_command(BM_CMD_REQUEST_READING, 0, 0, serial);
}

SR_PRIV int brymen_packet_length(const uint8_t *buf, int *len)
{
	struct brymen_header *hdr;
	int packet_len;
	size_t buflen;

	buflen = *len;
	hdr = (void *)buf;

	/* Did we receive a complete header yet? */
	if (buflen < sizeof(*hdr))
		return PACKET_NEED_MORE_DATA;

	if (hdr->dle != 0x10 || hdr->stx != 0x02)
		return PACKET_INVALID_HEADER;

	/* Our packet includes the header, the payload, and the tail. */
	packet_len = sizeof(*hdr) + hdr->len + sizeof(struct brymen_tail);

	/* In case we pick up an invalid header, limit our search. */
	if (packet_len > MAX_PACKET_LEN) {
		sr_spew("Header specifies an invalid payload length: %i.",
			hdr->len);
		return PACKET_INVALID_HEADER;
	}

	*len = packet_len;
	sr_spew("Expecting a %d-byte packet.", *len);
	return PACKET_HEADER_OK;
}

SR_PRIV gboolean brymen_packet_is_valid(const uint8_t *buf)
{
	struct brymen_header *hdr;
	struct brymen_tail *tail;
	int i;
	uint8_t chksum = 0;
	uint8_t *payload;

	payload = (uint8_t *)(buf + sizeof(struct brymen_header));

	hdr = (void *)buf;
	tail = (void *)(payload + hdr->len);

	for (i = 0; i< hdr->len; i++)
		chksum ^= payload[i];

	if (tail->checksum != chksum) {
		sr_dbg("Packet has invalid checksum 0x%.2x. Expected 0x%.2x.",
		       chksum, tail->checksum);
		return FALSE;
	}

	return TRUE;
}

static int parse_value(const char *txt, size_t len, float *floatval)
{
	const char *txt_end;
	char c, buf[32], *dst;
	int ret;

	/*
	 * The input text is not NUL terminated, the checksum follows
	 * the value text field. Spaces may interfere with the text to
	 * number conversion, especially with exponent parsing. Copy the
	 * input data to a terminated text buffer and strip spaces in the
	 * process, before running ASCIIZ string operations.
	 */
	if (len >= sizeof(buf)) {
		sr_err("Insufficient text conversion buffer size.");
		return SR_ERR_BUG;
	}
	txt_end = txt + len;
	dst = &buf[0];
	while (txt < txt_end && *txt) {
		c = *txt++;
		if (c == ' ')
			continue;
		*dst++ = c;
	}
	*dst = '\0';

	/* Check for overflow, or get the number value. */
	if (strstr(buf, "+OL")) {
		*floatval = +INFINITY;
		return SR_OK;
	}
	if (strstr(buf, "-OL")) {
		*floatval = -INFINITY;
		return SR_OK;
	}
	if (strstr(buf, "OL")) {
		*floatval = INFINITY;
		return SR_OK;
	}
	ret = sr_atof_ascii(buf, floatval);
	if (ret != SR_OK)
		return ret;

	return SR_OK;
}

static void parse_flags(const uint8_t *bfunc, struct brymen_flags *info)
{
	info->is_low_batt	= (bfunc[3] & (1 << 7)) != 0;

	info->is_decibel	= (bfunc[1] & (1 << 5)) != 0;
	info->is_duty_cycle	= (bfunc[1] & (1 << 3)) != 0;
	info->is_hertz		= (bfunc[1] & (1 << 2)) != 0;
	info->is_amp		= (bfunc[1] & (1 << 1)) != 0;
	info->is_beep		= (bfunc[1] & (1 << 0)) != 0;

	info->is_ohm		= (bfunc[0] & (1 << 7)) != 0;
	info->is_fahrenheit	= (bfunc[0] & (1 << 6)) != 0;
	info->is_celsius	= (bfunc[0] & (1 << 5)) != 0;
	info->is_diode		= (bfunc[0] & (1 << 4)) != 0;
	info->is_capacitance	= (bfunc[0] & (1 << 3)) != 0;
	info->is_volt		= (bfunc[0] & (1 << 2)) != 0;
	info->is_dc		= (bfunc[0] & (1 << 1)) != 0;
	info->is_ac		= (bfunc[0] & (1 << 0)) != 0;
}

SR_PRIV int brymen_parse(const uint8_t *buf, float *floatval,
		struct sr_datafeed_analog *analog, void *info)
{
	struct brymen_flags flags;
	struct brymen_header *hdr;
	uint8_t *bfunc;
	const char *txt;
	int txtlen;
	char *p;
	char *unit;
	int ret;

	(void)info;

	hdr = (void *)buf;
	bfunc = (uint8_t *)(buf + sizeof(struct brymen_header));
	txt = (const char *)&bfunc[4];
	txtlen = hdr->len - 4;
	sr_dbg("DMM bfunc: %02x %02x %02x %02x, text '%.*s'",
		bfunc[3], bfunc[2], bfunc[1], bfunc[0], txtlen, txt);

	memset(&flags, 0, sizeof(flags));
	parse_flags(bfunc, &flags);
	if (flags.is_decibel && flags.is_ohm) {
		/*
		 * The reference impedance for the dBm function is in an
		 * unexpected format. Naive conversion of non-space chars
		 * gives incorrect results. Isolate the 4..1200 Ohms value
		 * instead, ignore the "0." and exponent parts of the
		 * response text.
		 */
		if (strncmp(txt, " 0.", strlen(" 0.")) == 0 && strstr(txt, " E")) {
			txt = &txt[strlen(" 0.")];
			txtlen -= strlen(" 0.");
			p = strchr(txt, 'E');
			if (p)
				*p = '\0';
		}
	}
	if (flags.is_fahrenheit || flags.is_celsius) {
		/*
		 * The value text in temperature mode includes the C/F
		 * suffix between the mantissa and the exponent, which
		 * breaks the text to number conversion. Example data:
		 * " 0.0217CE+3". Remove the C/F unit identifier.
		 */
		unit = strchr(txt, flags.is_fahrenheit ? 'F' : 'C');
		if (!unit)
			return SR_ERR;
		*unit = ' ';
	}
	ret = parse_value(txt, txtlen, floatval);
	sr_dbg("floatval: %f, ret %d", *floatval, ret);
	if (ret != SR_OK)
		return SR_ERR;

	analog->meaning->mqflags = 0;
	if (flags.is_volt) {
		analog->meaning->mq = SR_MQ_VOLTAGE;
		analog->meaning->unit = SR_UNIT_VOLT;
	}
	if (flags.is_amp) {
		analog->meaning->mq = SR_MQ_CURRENT;
		analog->meaning->unit = SR_UNIT_AMPERE;
	}
	if (flags.is_ohm) {
		if (flags.is_decibel)
			analog->meaning->mq = SR_MQ_RESISTANCE;
		else if (flags.is_beep)
			analog->meaning->mq = SR_MQ_CONTINUITY;
		else
			analog->meaning->mq = SR_MQ_RESISTANCE;
		analog->meaning->unit = SR_UNIT_OHM;
	}
	if (flags.is_hertz) {
		analog->meaning->mq = SR_MQ_FREQUENCY;
		analog->meaning->unit = SR_UNIT_HERTZ;
	}
	if (flags.is_duty_cycle) {
		analog->meaning->mq = SR_MQ_DUTY_CYCLE;
		analog->meaning->unit = SR_UNIT_PERCENTAGE;
	}
	if (flags.is_capacitance) {
		analog->meaning->mq = SR_MQ_CAPACITANCE;
		analog->meaning->unit = SR_UNIT_FARAD;
	}
	if (flags.is_fahrenheit) {
		analog->meaning->mq = SR_MQ_TEMPERATURE;
		analog->meaning->unit = SR_UNIT_FAHRENHEIT;
	}
	if (flags.is_celsius) {
		analog->meaning->mq = SR_MQ_TEMPERATURE;
		analog->meaning->unit = SR_UNIT_CELSIUS;
	}
	if (flags.is_capacitance) {
		analog->meaning->mq = SR_MQ_CAPACITANCE;
		analog->meaning->unit = SR_UNIT_FARAD;
	}

	/*
	 * The high-end Brymen models have a configurable reference
	 * impedance for dBm measurements. When the meter's function
	 * is entered, or when the reference impedance is changed, the
	 * meter sends one packet with the value of the new reference.
	 * Both decibel and ohm flags are set in this case, so we must
	 * be careful to not clobber the resistance value from above,
	 * and only provide dBm when the measurement is shown and not
	 * its reference.
	 *
	 * The meter's response values also use an unexpected scale
	 * (always off by factor 1000, as if it was Watts not mW).
	 *
	 * Example responses:
	 * bfunc: 00 00 20 80, text ' 0. 800 E+1' (reference)
	 * bfunc: 00 00 20 00, text '-0.3702 E-1' (measurement)
	 */
	if (flags.is_decibel && !flags.is_ohm) {
		analog->meaning->mq = SR_MQ_POWER;
		analog->meaning->unit = SR_UNIT_DECIBEL_MW;
		*floatval *= 1000;
	}

	if (flags.is_diode)
		analog->meaning->mqflags |= SR_MQFLAG_DIODE | SR_MQFLAG_DC;
	/* We can have both AC+DC in a single measurement. */
	if (flags.is_ac)
		analog->meaning->mqflags |= SR_MQFLAG_AC;
	if (flags.is_dc)
		analog->meaning->mqflags |= SR_MQFLAG_DC;

	if (flags.is_low_batt)
		sr_warn("Low battery!");

	return SR_OK;
}
