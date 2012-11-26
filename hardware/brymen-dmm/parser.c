/*
 * This file is part of the sigrok project.
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

#include "protocol.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/*
 * Flags passed from the DMM.
 */
struct brymen_flags {
	gboolean low_batt;
	gboolean decibel, duty_cycle, hertz, amp, beep, ohm, fahrenheit;
	gboolean celsius, capacitance, diode, volt, dc, ac;
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
 * driver is easily extensible to support more models, as the protocols are very
 * similar.
 */
enum {
	BM_CMD_REQUEST_READING = 0x00,
};


static int bm_send_command(uint8_t command, uint8_t arg1, uint8_t arg2,
			   struct sr_serial_dev_inst *serial)
{
	struct bm850_command cmdout = {
		.dle = 0x10, .stx = 0x02,
		.cmd = command,
		.arg = {arg1, arg2},
		.checksum = arg1^arg2, .dle2 = 0x10, .etx = 0x03};
		int written;
		
		/* TODO: How do we compute the checksum? Hardware seems to ignore it */
		
		/* Request reading */
		written = serial_write(serial, &cmdout, sizeof(cmdout));
		if(written != sizeof(cmdout))
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
	const int brymen_max_packet_len = 22;
	int packet_len;
	const size_t buflen = *len;
	
	hdr = (void*)buf;
	
	/* Did we receive a complete header yet? */
	if (buflen < sizeof(*hdr) )
		return PACKET_NEED_MORE_DATA;
	
	if (hdr->dle != 0x10 || hdr->stx != 0x02)
		return PACKET_INVALID_HEADER;
	
	/* Our packet includes the header, the payload, and the tail */
	packet_len = sizeof(*hdr) + hdr->len + sizeof(struct brymen_tail);
	
	/* In case we pick up an invalid header, limit our search */
	if (packet_len > brymen_max_packet_len) {
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
	const uint8_t *payload = buf + sizeof(struct brymen_header);
	
	hdr = (void*)buf;
	tail = (void*)(payload + hdr->len);
	
	for (i = 0; i< hdr->len; i++)
		chksum ^= payload[i];
	
	if (tail->checksum != chksum) {
		sr_dbg("Packet has invalid checksum 0x%.2x. Expected 0x%.2x",
		       chksum, tail->checksum);
		return FALSE;
	}
	
	return TRUE;
}

static int parse_value(const char *strbuf, const int len, float *floatval)
{
	int s, d;
	char str[32];

	if (strstr(strbuf, "OL")) {
		sr_dbg("Overlimit.");
		*floatval = INFINITY;
		return SR_OK;
	}

	memset(str, 0, sizeof(str));
	/* Spaces may interfere with strtod parsing the exponent. Strip them */
	for (s = 0, d = 0; s < len; s++)
		if (strbuf[s] != ' ')
			str[d++] = strbuf[s];
		/* YES, it's that simple !*/
		*floatval = strtod(str, NULL);

	return SR_OK;
}
static void parse_flags(const uint8_t *buf, struct brymen_flags *info)
{
	const uint8_t * bfunc = buf + sizeof(struct brymen_header);

	info->low_batt		= (bfunc[3] & (1 << 7)) != 0;

	info->decibel		= (bfunc[1] & (1 << 5)) != 0;
	info->duty_cycle	= (bfunc[1] & (1 << 3)) != 0;
	info->hertz		= (bfunc[1] & (1 << 2)) != 0;
	info->amp		= (bfunc[1] & (1 << 1)) != 0;
	info->beep		= (bfunc[1] & (1 << 0)) != 0;

	info->ohm		= (bfunc[0] & (1 << 7)) != 0;
	info->fahrenheit	= (bfunc[0] & (1 << 6)) != 0;
	info->celsius		= (bfunc[0] & (1 << 5)) != 0;
	info->diode		= (bfunc[0] & (1 << 4)) != 0;
	info->capacitance	= (bfunc[0] & (1 << 3)) != 0;
	info->volt		= (bfunc[0] & (1 << 2)) != 0;
	info->dc		= (bfunc[0] & (1 << 1)) != 0;
	info->ac		= (bfunc[0] & (1 << 0)) != 0;
}

SR_PRIV int sr_brymen_parse(const uint8_t *buf, float *floatval,
			    struct sr_datafeed_analog *analog, void *info)
{
	struct brymen_flags flags;
	struct brymen_header *hdr = (void*) buf;
	const uint8_t *bfunc = buf + sizeof(struct brymen_header);
	int asciilen;

	(void)info;
	analog->mqflags = 0;

	/* Give some debug info about the package */
	asciilen = hdr->len - 4;
	sr_dbg("DMM flags: %.2x %.2x %.2x %.2x",
	       bfunc[3], bfunc[2], bfunc[1], bfunc[0]);
	/* Value is an ASCII string */
	sr_dbg("DMM packet: \"%.*s\"", asciilen, bfunc + 4);

	parse_flags(buf, &flags);
	parse_value((const char*)(bfunc + 4), asciilen, floatval);

	if (flags.volt) {
		analog->mq = SR_MQ_VOLTAGE;
		analog->unit = SR_UNIT_VOLT;
	}
	if (flags.amp) {
		analog->mq = SR_MQ_CURRENT;
		analog->unit = SR_UNIT_AMPERE;
	}
	if (flags.ohm) {
		if (flags.beep)
			analog->mq = SR_MQ_CONTINUITY;
		else
			analog->mq = SR_MQ_RESISTANCE;
		analog->unit = SR_UNIT_OHM;
	}
	if (flags.hertz) {
		analog->mq = SR_MQ_FREQUENCY;
		analog->unit = SR_UNIT_HERTZ;
	}
	if (flags.duty_cycle) {
		analog->mq = SR_MQ_DUTY_CYCLE;
		analog->unit = SR_UNIT_PERCENTAGE;
	}
	if (flags.capacitance) {
		analog->mq = SR_MQ_CAPACITANCE;
		analog->unit = SR_UNIT_FARAD;
	}
	if (flags.fahrenheit) {
		analog->mq = SR_MQ_TEMPERATURE;
		analog->unit = SR_UNIT_FAHRENHEIT;
	}
	if (flags.celsius) {
		analog->mq = SR_MQ_TEMPERATURE;
		analog->unit = SR_UNIT_CELSIUS;
	}
	if (flags.capacitance) {
		analog->mq = SR_MQ_CAPACITANCE;
		analog->unit = SR_UNIT_FARAD;
	}
	/*
	 * The high-end brymen models have a configurable reference impedance.
	 * When the reference impedance is changed, the DMM sends one packet
	 * with the value of the new reference impedance. Both decibel and ohm
	 * flags are set in this case, so we must be careful to correctly
	 * identify the value as ohm, not dBmW
	 */
	if (flags.decibel && !flags.ohm) {
		analog->mq = SR_MQ_POWER;
		analog->unit = SR_UNIT_DECIBEL_MW;
		/*
		 * For some reason, dBm measurements are sent by the multimeter
		 * with a value three orders of magnitude smaller than the
		 * displayed value.
		 * */
		*floatval *= 1000;
	}

	if (flags.diode)
		analog->mqflags |= SR_MQFLAG_DIODE;
	/* We can have both AC+DC in a single measurement */
	if (flags.ac)
		analog->mqflags |= SR_MQFLAG_AC;
	if (flags.dc)
		analog->mqflags |= SR_MQFLAG_DC;

	if (flags.low_batt)
		sr_info("Low battery!");

	return SR_OK;
}