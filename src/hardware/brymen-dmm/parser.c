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

static int parse_value(const char *strbuf, int len, float *floatval)
{
	int s, d;
	char str[32];

	if (strstr(strbuf, "OL")) {
		sr_dbg("Overlimit.");
		*floatval = INFINITY;
		return SR_OK;
	}

	memset(str, 0, sizeof(str));
	/* Spaces may interfere with parsing the exponent. Strip them. */
	for (s = 0, d = 0; s < len; s++) {
		if (strbuf[s] != ' ')
			str[d++] = strbuf[s];
	}
	if (sr_atof_ascii(str, floatval) != SR_OK)
		return SR_ERR;

	return SR_OK;
}

static void parse_flags(const uint8_t *buf, struct brymen_flags *info)
{
	info->is_low_batt	= (buf[4 + 3] & (1 << 7)) != 0;

	info->is_decibel	= (buf[4 + 1] & (1 << 5)) != 0;
	info->is_duty_cycle	= (buf[4 + 1] & (1 << 3)) != 0;
	info->is_hertz		= (buf[4 + 1] & (1 << 2)) != 0;
	info->is_amp		= (buf[4 + 1] & (1 << 1)) != 0;
	info->is_beep		= (buf[4 + 1] & (1 << 0)) != 0;

	info->is_ohm		= (buf[4 + 0] & (1 << 7)) != 0;
	info->is_fahrenheit	= (buf[4 + 0] & (1 << 6)) != 0;
	info->is_celsius	= (buf[4 + 0] & (1 << 5)) != 0;
	info->is_diode		= (buf[4 + 0] & (1 << 4)) != 0;
	info->is_capacitance	= (buf[4 + 0] & (1 << 3)) != 0;
	info->is_volt		= (buf[4 + 0] & (1 << 2)) != 0;
	info->is_dc		= (buf[4 + 0] & (1 << 1)) != 0;
	info->is_ac		= (buf[4 + 0] & (1 << 0)) != 0;
}

SR_PRIV int brymen_parse(const uint8_t *buf, float *floatval,
		struct sr_datafeed_analog *analog, void *info)
{
	struct brymen_flags flags;
	struct brymen_header *hdr;
	uint8_t *bfunc;
	int asciilen;

	(void)info;

	hdr = (void *)buf;
	bfunc = (uint8_t *)(buf + sizeof(struct brymen_header));

	analog->meaning->mqflags = 0;

	/* Give some debug info about the package. */
	asciilen = hdr->len - 4;
	sr_dbg("DMM flags: %.2x %.2x %.2x %.2x",
	       bfunc[3], bfunc[2], bfunc[1], bfunc[0]);
	/* Value is an ASCII string. */
	sr_dbg("DMM packet: \"%.*s\"", asciilen, bfunc + 4);

	parse_flags(buf, &flags);
	if (parse_value((const char *)(bfunc + 4), asciilen, floatval) != SR_OK)
		return SR_ERR;

	if (flags.is_volt) {
		analog->meaning->mq = SR_MQ_VOLTAGE;
		analog->meaning->unit = SR_UNIT_VOLT;
	}
	if (flags.is_amp) {
		analog->meaning->mq = SR_MQ_CURRENT;
		analog->meaning->unit = SR_UNIT_AMPERE;
	}
	if (flags.is_ohm) {
		if (flags.is_beep)
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
	 * The high-end Brymen models have a configurable reference impedance.
	 * When the reference impedance is changed, the DMM sends one packet
	 * with the value of the new reference impedance. Both decibel and ohm
	 * flags are set in this case, so we must be careful to correctly
	 * identify the value as ohm, not dBmW.
	 */
	if (flags.is_decibel && !flags.is_ohm) {
		analog->meaning->mq = SR_MQ_POWER;
		analog->meaning->unit = SR_UNIT_DECIBEL_MW;
		/*
		 * For some reason, dBm measurements are sent by the multimeter
		 * with a value three orders of magnitude smaller than the
		 * displayed value.
		 */
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
		sr_info("Low battery!");

	return SR_OK;
}
