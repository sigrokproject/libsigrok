/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Matthias Heidbrink <m-sigrok@heidbrink.biz>
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

SR_PRIV const struct nmadmm_req nmadmm_requests[] = {
	{ NMADMM_REQ_IDN, "IDN?" },
	{ NMADMM_REQ_IDN, "STATUS?" },
	{ 0, NULL },
};

static int nma_send_req(const struct sr_dev_inst *sdi, int req, char *params)
{
	struct sr_serial_dev_inst *serial;
	struct dev_context *devc;
	char buf[NMADMM_BUFSIZE];
	int len;

	if (!sdi || !(serial = sdi->conn) || !(devc = sdi->priv))
		return SR_ERR_BUG;

	len = snprintf(buf, sizeof(buf), "%s%s\r\n",
		nmadmm_requests[req].req_str, params ? params : "");

	sr_spew("Sending request: '%s'.", buf);

	devc->last_req = req;
	devc->last_req_pending = TRUE;

	if (serial_write(serial, buf, len) == -1) {
		sr_err("Unable to send request: %d %s.",
			errno, strerror(errno));
		devc->last_req_pending = FALSE;
		return SR_ERR;
	}

	return SR_OK;
}

/**
 * Convert hexadecimal digit to int.
 *
 * @param[in] xgit Hexadecimal digit to convert.
 * @return Int value of xgit (0 on invalid xgit).
 */
SR_PRIV int xgittoint(char xgit)
{
	if ((xgit >= '0') && (xgit <= '9'))
		return xgit - '0';
	xgit = tolower(xgit);
	if ((xgit >= 'a') && (xgit <= 'f'))
		return xgit - 'a';
	return 0;
}

/**
 * Process received line. It consists of 20 hex digits + \\r\\n,
 * e.g. '08100400018100400000'.
 */
static void nma_process_line(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int pos, flags;
	int vt, range;	/* Measurement value type, range in device format */
	int mmode, devstat;	/* Measuring mode, device status */
	float value;	/* Measured value */
	float scale;	/* Scaling factor depending on range and function */
	struct sr_datafeed_analog analog;
	struct sr_datafeed_packet packet;

	devc = sdi->priv;

	devc->buf[20] = '\0';

	sr_spew("Received line '%s'.", devc->buf);

	/* Check line. */
	if (strlen((const char *)devc->buf) != 20) {
		sr_err("line: Invalid status '%s', must be 20 hex digits.",
		       devc->buf);
		devc->buflen = 0;
		return;
	}

	for (pos = 0; pos < 20; pos++) {
		if (!isxdigit(devc->buf[pos])) {
			sr_err("line: Expected hex digit in '%s' at pos %d!",
				devc->buf, pos);
			devc->buflen = 0;
			return;
		}
	}

	/* Start decoding. */
	value = 0.0;
	scale = 1.0;
	memset(&analog, 0, sizeof(analog));

	/*
	 * The numbers are hex digits, starting from 0.
	 * 0: Keyboard status, currently not interesting.
	 * 1: Central switch status, currently not interesting.
	 * 2: Type of measured value.
	 */
	vt = xgittoint(devc->buf[2]);
	switch (vt) {
	case 0:
		analog.mq = SR_MQ_VOLTAGE;
		break;
	case 1:
		analog.mq = SR_MQ_CURRENT;	/* 2A */
		break;
	case 2:
		analog.mq = SR_MQ_RESISTANCE;
		break;
	case 3:
		analog.mq = SR_MQ_CAPACITANCE;
		break;
	case 4:
		analog.mq = SR_MQ_TEMPERATURE;
		break;
	case 5:
		analog.mq = SR_MQ_FREQUENCY;
		break;
	case 6:
		analog.mq = SR_MQ_CURRENT;	/* 10A */
		break;
	case 7:
		analog.mq = SR_MQ_GAIN;		/* TODO: Scale factor */
		break;
	case 8:
		analog.mq = SR_MQ_GAIN;		/* Percentage */
		scale /= 100.0;
		break;
	case 9:
		analog.mq = SR_MQ_GAIN;		/* dB */
		scale /= 100.0;
		break;
	default:
		sr_err("Unknown value type: 0x%02x.", vt);
		break;
	}

	/* 3: Measurement range for measured value */
	range = xgittoint(devc->buf[3]);
	switch (vt) {
	case 0: /* V */
		scale *= pow(10.0, range - 5);
		break;
	case 1: /* A */
		scale *= pow(10.0, range - 7);
		break;
	case 2: /* Ω */
		scale *= pow(10.0, range - 2);
		break;
	case 3: /* F */
		scale *= pow(10.0, range - 12);
		break;
	case 4: /* °C */
		scale *= pow(10.0, range - 1);
		break;
	case 5: /* Hz */
		scale *= pow(10.0, range - 2);
		break;
	// No default, other value types have fixed display format.
	}

	/* 5: Sign and 1st digit */
	flags = xgittoint(devc->buf[5]);
	value = (flags & 0x03);
	if (flags & 0x04)
		scale *= -1;

	/* 6-9: 2nd-4th digit */
	for (pos = 6; pos < 10; pos++)
		value = value * 10 + xgittoint(devc->buf[pos]);
	value *= scale;

	/* 10: Display counter */
	mmode = xgittoint(devc->buf[10]);
	switch (mmode) {
	case 0: /* Frequency */
		analog.unit = SR_UNIT_HERTZ;
		break;
	case 1: /* V TRMS, only type 5 */
		analog.unit = SR_UNIT_VOLT;
		analog.mqflags |= (SR_MQFLAG_AC | SR_MQFLAG_DC | SR_MQFLAG_RMS);
		break;
	case 2: /* V AC */
		analog.unit = SR_UNIT_VOLT;
		analog.mqflags |= SR_MQFLAG_AC;
		if (devc->type >= 3)
			analog.mqflags |= SR_MQFLAG_RMS;
		break;
	case 3: /* V DC */
		analog.unit = SR_UNIT_VOLT;
		analog.mqflags |= SR_MQFLAG_DC;
		break;
	case 4: /* Ohm */
		analog.unit = SR_UNIT_OHM;
		break;
	case 5: /* Continuity */
		analog.unit = SR_UNIT_BOOLEAN;
		analog.mq = SR_MQ_CONTINUITY;
		/* TODO: Continuity handling is a bit odd in libsigrok. */
		break;
	case 6: /* Degree Celsius */
		analog.unit = SR_UNIT_CELSIUS;
		break;
	case 7: /* Capacity */
		analog.unit = SR_UNIT_FARAD;
		break;
	case 8: /* Current DC */
		analog.unit = SR_UNIT_AMPERE;
		analog.mqflags |= SR_MQFLAG_DC;
		break;
	case 9: /* Current AC */
		analog.unit = SR_UNIT_AMPERE;
		analog.mqflags |= SR_MQFLAG_AC;
		if (devc->type >= 3)
			analog.mqflags |= SR_MQFLAG_RMS;
		break;
	case 0xa: /* Current TRMS, only type 5 */
		analog.unit = SR_UNIT_AMPERE;
		analog.mqflags |= (SR_MQFLAG_AC | SR_MQFLAG_DC | SR_MQFLAG_RMS);
		break;
	case 0xb: /* Diode */
		analog.unit = SR_UNIT_VOLT;
		analog.mqflags |= (SR_MQFLAG_DIODE | SR_MQFLAG_DC);
		break;
	default:
		sr_err("Unknown mmode: 0x%02x.", mmode);
		break;
	}

	/* 11: Device status */
	devstat = xgittoint(devc->buf[11]);

	switch (devstat) {
	case 1: /* Normal measurement */
		break;
	case 2: /* Input loop (limit, reference values) */
		break;
	case 3: /* TRANS/SENS */
		break;
	case 4: /* Error */
		sr_err("Device error. Fuse?"); /* TODO: Really abort? */
		devc->buflen = 0;
		return;	/* Cannot continue. */
	default:
		sr_err("Unknown device status: 0x%02x", devstat);
		break;
	}

	/* 12-19: Flags and display symbols */
	/* 12, 13 */
	flags = (xgittoint(devc->buf[12]) << 8) | xgittoint(devc->buf[13]);
	/* 0x80: PRINT TODO: Stop polling when discovered? */
	/* 0x40: EXTR */
	if (analog.mq == SR_MQ_CONTINUITY) {
		if (flags & 0x20)
			value = 1.0; /* Beep */
		else
			value = 0.0;
	}
	/* 0x10: AVG */
	/* 0x08: Diode */
	if (flags & 0x04) /* REL */
		analog.mqflags |= SR_MQFLAG_RELATIVE;
	/* 0x02: SHIFT	*/
	if (flags & 0x01) /* % */
		analog.unit = SR_UNIT_PERCENTAGE;

	/* 14, 15 */
	flags = (xgittoint(devc->buf[14]) << 8) | xgittoint(devc->buf[15]);
	if (!(flags & 0x80))	/* MAN: Manual range */
		analog.mqflags |= SR_MQFLAG_AUTORANGE;
	if (flags & 0x40) /* LOBATT1: Low battery, measurement still within specs */
		devc->lowbatt = 1;
	/* 0x20: PEAK */
	/* 0x10: COUNT */
	if (flags & 0x08)	/* HOLD */
		analog.mqflags |= SR_MQFLAG_HOLD;
	/* 0x04: LIMIT	*/
	if (flags & 0x02) 	/* MAX */
		analog.mqflags |= SR_MQFLAG_MAX;
	if (flags & 0x01) 	/* MIN */
		analog.mqflags |= SR_MQFLAG_MIN;

	/* 16, 17 */
	flags = (xgittoint(devc->buf[16]) << 8) | xgittoint(devc->buf[17]);
	/* 0xe0: undefined */
	if (flags & 0x10) { /* LOBATT2: Low battery, measurement inaccurate */
		devc->lowbatt = 2;
		sr_warn("Low battery, measurement quality degraded!");
	}
	/* 0x08: SCALED */
	/* 0x04: RATE (=lower resolution, allows higher rata rate up to 10/s. */
	/* 0x02: Current clamp */
	if (flags & 0x01) { /* dB */
		/*
		 * TODO: The Norma has an adjustable dB reference value. If
		 * changed from default, this is not correct.
		 */
		if (analog.unit == SR_UNIT_VOLT)
			analog.unit = SR_UNIT_DECIBEL_VOLT;
		else
			analog.unit = SR_UNIT_UNITLESS;
	}

	/* 18, 19 */
	/* flags = (xgittoint(devc->buf[18]) << 8) | xgittoint(devc->buf[19]); */
	/* 0x80: Undefined. */
	/* 0x40: Remote mode, keyboard locked */
	/* 0x38: Undefined. */
	/* 0x04: MIN > MAX */
	/* 0x02: Measured value < Min */
	/* 0x01: Measured value > Max */

	/* 4: Flags. Evaluating this after setting value! */
	flags = xgittoint(devc->buf[4]);
	if (flags & 0x04) /* Invalid value */
	    value = NAN;
	else if (flags & 0x01) /* Overload */
	    value =  INFINITY;
	if (flags & 0x02) { /* Duplicate value, has been sent before. */
	    sr_spew("Duplicate value, dismissing!");
	    devc->buflen = 0;
	    return;
	}

	sr_spew("range=%d/scale=%f/value=%f", range,
		(double)scale, (double)value);

	/* Finish and send packet. */
	analog.channels = sdi->channels;
	analog.num_samples = 1;
	analog.data = &value;

	memset(&packet, 0, sizeof(packet));
	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	sr_session_send(devc->cb_data, &packet);

	/* Finish processing. */
	devc->num_samples++;
	devc->buflen = 0;
}

SR_PRIV int norma_dmm_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	int len;
	gboolean terminating;
	gdouble elapsed_s;

	(void)fd;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	serial = sdi->conn;
	if (revents == G_IO_IN) {
		/* Serial data arrived. */
		while (NMADMM_BUFSIZE - devc->buflen - 1 > 0) {
			len = serial_read(serial, devc->buf + devc->buflen, 1);
			if (len < 1)
				break;
			devc->buflen += len;
			*(devc->buf + devc->buflen) = '\0';
			if (*(devc->buf + devc->buflen - 1) == '\n') {
				/*
				 * TODO: According to specs, should be \r, but
				 * then we'd have to get rid of the \n.
				 */
				devc->last_req_pending = FALSE;
				nma_process_line(sdi);
				break;
			}
		}
	}

	/* If number of samples or time limit reached, stop acquisition. */
	terminating = FALSE;
	if (devc->limit_samples && (devc->num_samples >= devc->limit_samples)) {
		sdi->driver->dev_acquisition_stop(sdi, cb_data);
		terminating = TRUE;
	}

	if (devc->limit_msec) {
		elapsed_s = g_timer_elapsed(devc->elapsed_msec, NULL);
		if ((elapsed_s * 1000) >= devc->limit_msec) {
			sdi->driver->dev_acquisition_stop(sdi, cb_data);
			terminating = TRUE;
		}
	}

	/* Request next package. */
	if (!terminating && !devc->last_req_pending) {
		if (nma_send_req(sdi, NMADMM_REQ_STATUS, NULL) != SR_OK)
			return FALSE;
	}

	return TRUE;
}
