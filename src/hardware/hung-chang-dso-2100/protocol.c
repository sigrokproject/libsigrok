/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2015 Daniel Glöckner <daniel-gl@gmx.net>
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
#include <ieee1284.h>
#include "protocol.h"

/* The firmware can be in the following states:
 *  0x00	Temporary state during initialization
 *  		Automatically transitions to state 0x01
 *  0x01	Idle, this state updates calibration caps
 *		Send 0x02 to go to state 0x21
 *		Send 0x03 to go to state 0x03
 *		Send 0x04 to go to state 0x14
 *  0x21	Trigger is armed, caps are _not_ updated
 *  		Send 0x99 to check if trigger event occured
 *  			if triggered, goes to state 0x03
 *  			else stays in state 0x21
 *  		Send 0xFE to generate artificial trigger event
 *			returns to state 0x21
 *			but next 0x99 will succeed
 *		Send 0xFF to go to state 0x03 (abort capture)
 *  0x03	Extracts two 500 sample subsets from the 5000
 *  		sample capture buffer for readout
 *  		When reading samples, the FPGA starts at the
 *  		first of the 1000 samples and automatically
 *  		advances to the next.
 *  		Send 0x04 to go to state 0x0F
 *  0x14	Scroll acquisition mode, update calib caps
 *  		When reading samples, the FPGA provides the
 *  		current value of the ADCs
 *  		Send 0xFF to go to state 0x0F
 *  0x0F	Send channel number (1 or 2) to go to next state
 *  		There are actually two 0x0F states in series
 *  		which both expect the channel number.
 *  		If the values don't match, they are discarded.
 *  		The next state 0x05 is entered anyway
 *  0x05	Same as state 0x0F but expects sample rate index.
 *  		The next state is 0x08
 *  0x08	Same as state 0x0F but expects step size + 1 for
 *  		the second 500 sample subset
 *  		The next state is 0x09
 *  0x09	Same as state 0x0F but expects step size + 1 for
 *  		the first 500 sample subset
 *  		The next state is 0x06
 *  0x06	Same as state 0x0F but expects vdiv and coupling
 *  		configuration for the first channel and trigger
 *  		source selection.
 *  		(U46 in the schematics)
 *  		The next state is 0x07
 *  0x07	Same as state 0x0F but expects vdiv and coupling
 *  		configuration for the first channel and trigger
 *  		type (edge, TV hsync, TV vsync).
 *  		(U47 in the schematics)
 *  		The next state is 0x0A
 *  0x0A	Same as state 0x0F but expects a parameter X + 1
 *  		that determines the offset of the second 500 sample
 *  		subset
 *  		Offset = 5 * X * step size for first subset
 *  		The next state is 0x0B
 *  0x0B	Same as state 0x0F but expects the type of edge to
 *  		trigger on (rising or falling)
 *  		The next state is 0x0C
 *  0x0C	Same as state 0x0F but expects the calibration
 *  		value for the first channel's position
 *  		(POS1 in the schematics)
 *  		The next state is 0x0D
 *  0x0D	Same as state 0x0F but expects the calibration
 *  		value for the second channel's position
 *  		(POS2 in the schematics)
 *  		The next state is 0x0E
 *  0x0E	Same as state 0x0F but expects the trigger level
 *  		(TRIGLEVEL in the schematics)
 *  		Keep in mind that trigger sources are AC coupled
 *  		The next state is 0x10
 *  0x10	Same as state 0x0F but expects the calibration
 *  		value for the first channel's offset
 *  		(OFFSET1 in the schematics)
 *  		The next state is 0x11
 *  0x11	Same as state 0x0F but expects the calibration
 *  		value for the first channel's gain
 *  		(GAIN1 in the schematics)
 *  		The next state is 0x12
 *  0x12	Same as state 0x0F but expects the calibration
 *  		value for the second channel's offset
 *  		(OFFSET2 in the schematics)
 *  		The next state is 0x13
 *  0x13	Same as state 0x0F but expects the calibration
 *  		value for the second channel's gain
 *  		(GAIN2 in the schematics)
 *  		The next state is 0x01
 *
 * The Mailbox appears to be half duplex.
 * If one side writes a byte into the mailbox, it
 * reads 0 until the other side has written a byte.
 * So you can't transfer 0.
 *
 * As the status signals are unconnected, the device is not
 * IEEE1284 compliant and can't make use of EPP or ECP transfers.
 * It drives the data lines when control is set to:
 *                0                => Channel A data
 *          C1284_NAUTOFD          => Channel B data
 *         C1284_NSELECTIN         => Mailbox
 * C1284_NSELECTIN | C1284_NAUTOFD => 0x55
 *
 * It takes about 200ns for the data lines to become stable after
 * the control lines have been changed. This driver assumes that
 * parallel port access is slow enough to not require additional
 * delays.
 *
 * Channel values in state 0x14 and the mailbox can change their
 * value while they are selected, the latter of course only from
 * 0 to a valid state. Beware of intermediate values.
 *
 * SRAM N layout (N = 1 or 2):
 * 0x0000-0x13ff	samples captured from ADC N
 * 0x4000-0x41f3	bytes extracted from 0x6000 with step1
 *			(both ADCs but only channel N)
 * 0x41f4-0x43e7	bytes extracted from 0x6000+5*step1*shift
 *			with step2 (both ADCs but only channel N)
 * 0x43e8-0x43ea	{0x01, 0xfe, 0x80}
 * 0x43eb-0x444e	copy of bytes from 0x4320
 * 0x6000-0x7387	interleaved SRAM 1 and SRAM 2 bytes from
 * 			0x0001 to 0x09c5 after channel N was captured
 *
 * On a trigger event the FPGA directs the ADC samples to the region
 * at 0x0000. The microcontroller then copies 5000 samples from 0x0001
 * to 0x6000. Each time state 0x03 is entered, the bytes from 0x4000
 * to 0x444e are filled and the start address for readout is reset to
 * 0x4000. Readout will wrap around back to 0x4000 after reaching 0x7fff.
 *
 * As you can see from the layout, it was probably intended to capture
 * 5000 samples for both probes before they are read out. We don't do that
 * to be able to read the full 10k samples captured by the FPGA. It would
 * be useless anyway if you don't capture repetitive signals. We're also
 * not reading the two samples at 0x0000 to save a few milliseconds.
 */

static const struct {
	uint16_t num;
	uint8_t step1;
	uint8_t shift;
	uint8_t interleave;
} readout_steps[] = {
	{ 1000, 1, 100, 0 },
	{ 500, 100, 2, 0 },
	{ 500, 100, 3, 0 },
	{ 500, 100, 4, 0 },
	{ 500, 100, 5, 0 },
	{ 500, 100, 6, 0 },
	{ 500, 100, 7, 0 },
	{ 500, 100, 8, 0 },
	{ 500, 100, 9, 0 },
	{ 499, 212, 41, 1 },
	{ 500, 157, 56, 1 },
	{ 500, 247, 36, 1 },
	{ 500, 232, 180, 1 },
	{ 500, 230, 182, 1 },
	{ 120, 212, 43, 1 }
};

SR_PRIV void hung_chang_dso_2100_reset_port(struct parport *port)
{
	ieee1284_write_control(port,
			C1284_NSTROBE | C1284_NAUTOFD | C1284_NSELECTIN);
	ieee1284_data_dir(port, 0);
}

SR_PRIV gboolean hung_chang_dso_2100_check_id(struct parport *port)
{
	gboolean ret = FALSE;

	if (ieee1284_data_dir(port, 1) != E1284_OK)
		goto fail;

	ieee1284_write_control(port, C1284_NSTROBE | C1284_NAUTOFD | C1284_NSELECTIN);
	ieee1284_write_control(port, C1284_NAUTOFD | C1284_NSELECTIN);

	if (ieee1284_read_data(port) != 0x55)
		goto fail;

	ret = TRUE;
fail:
	hung_chang_dso_2100_reset_port(port);

	return ret;
}

SR_PRIV void hung_chang_dso_2100_write_mbox(struct parport *port, uint8_t val)
{
	sr_dbg("mbox <= %X", val);
	ieee1284_write_control(port,
			C1284_NSTROBE | C1284_NINIT | C1284_NSELECTIN);
	ieee1284_data_dir(port, 0);
	ieee1284_write_data(port, val);
	ieee1284_write_control(port, C1284_NINIT | C1284_NSELECTIN);
	ieee1284_write_control(port,
			C1284_NSTROBE | C1284_NINIT | C1284_NSELECTIN);
	ieee1284_data_dir(port, 1);
	ieee1284_write_control(port,
		C1284_NSTROBE | C1284_NAUTOFD | C1284_NINIT | C1284_NSELECTIN);
}

SR_PRIV uint8_t hung_chang_dso_2100_read_mbox(struct parport *port, float timeout)
{
	GTimer *timer = NULL;
	uint8_t val;

	ieee1284_write_control(port, C1284_NSTROBE | C1284_NSELECTIN);
	ieee1284_write_control(port, C1284_NSELECTIN);

	for (;;) {
		if (ieee1284_read_data(port)) {
			/* Always read the value a second time.
			 * The first one may be unstable. */
			val = ieee1284_read_data(port);
			break;
		}
		if (!timer) {
			timer = g_timer_new();
		} else if (g_timer_elapsed(timer, NULL) > timeout) {
			val = 0;
			break;
		}
	}

	ieee1284_write_control(port, C1284_NSTROBE | C1284_NSELECTIN);
	ieee1284_write_control(port,
		C1284_NSTROBE | C1284_NAUTOFD | C1284_NINIT | C1284_NSELECTIN);

	if (timer)
		g_timer_destroy(timer);
	sr_dbg("mbox == %X", val);
	return val;
}

SR_PRIV int hung_chang_dso_2100_move_to(const struct sr_dev_inst *sdi, uint8_t target)
{
	struct dev_context *devc = sdi->priv;
	int timeout = 40;
	uint8_t c;

	while (timeout--) {
		c = hung_chang_dso_2100_read_mbox(sdi->conn, 0.1);
		if (c == target)
			return SR_OK;

		switch (c) {
		case 0x00:
			/* Can happen if someone wrote something into
			 * the mbox that was not expected by the uC.
			 * Alternating between 0xff and 4 helps in
			 * all states. */
			c = (timeout & 1) ? 0xFF : 0x04;
			break;
		case 0x01:
			switch (target) {
			case 0x21: c = 2; break;
			case 0x03: c = 3; break;
			default: c = 4;
			}
			break;
		case 0x03: c = 4; break;
		case 0x05: c = devc->rate + 1; break;
		case 0x06: c = devc->cctl[0]; break;
		case 0x07: c = devc->cctl[1]; break;
		case 0x08: c = 1 /* step 2 */ + 1 ; break;
		case 0x09: c = readout_steps[devc->step].step1 + 1; break;
		case 0x0A: c = readout_steps[devc->step].shift + 1; break;
		case 0x0B: c = devc->edge + 1; break;
		case 0x0C: c = devc->pos[0]; break;
		case 0x0D: c = devc->pos[1]; break;
		case 0x0E: c = devc->tlevel; break;
		case 0x0F:
			if (!devc->channel)
				c = 1;
			else if (readout_steps[devc->step].interleave)
				c = devc->adc2 ? 2 : 1;
			else
				c = devc->channel;
			break;
		case 0x10: c = devc->offset[0]; break;
		case 0x11: c = devc->gain[0]; break;
		case 0x12: c = devc->offset[1]; break;
		case 0x13: c = devc->gain[1]; break;
		case 0x14:
		case 0x21: c = 0xFF; break;
		default:
			return SR_ERR_DATA;
		}
		hung_chang_dso_2100_write_mbox(sdi->conn, c);
	}
	return SR_ERR_TIMEOUT;
}

static void skip_samples(struct parport *port, uint8_t ctrl, size_t num)
{
	while (num--) {
		ieee1284_write_control(port, ctrl & ~C1284_NSTROBE);
		ieee1284_write_control(port, ctrl);
	}
}

static void read_samples(struct parport *port, uint8_t ctrl, uint8_t *buf, size_t num, size_t stride)
{
	while (num--) {
		ieee1284_write_control(port, ctrl & ~C1284_NSTROBE);
		*buf = ieee1284_read_data(port);
		buf += stride;
		ieee1284_write_control(port, ctrl);
	}
}

static void push_samples(const struct sr_dev_inst *sdi, uint8_t *buf, size_t num)
{
	struct dev_context *devc = sdi->priv;
	float *data = devc->samples;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	struct sr_datafeed_packet packet = {
		.type = SR_DF_ANALOG,
		.payload = &analog,
	};
	float factor = devc->factor;

	while (num--)
		data[num] = (buf[num] - 0x80) * factor;

	float vdivlog = log10f(factor);
	int digits = -(int)vdivlog + (vdivlog < 0.0);

	sr_analog_init(&analog, &encoding, &meaning, &spec, digits);
	analog.meaning->channels = devc->enabled_channel;
	analog.meaning->mq = SR_MQ_VOLTAGE;
	analog.meaning->unit = SR_UNIT_VOLT;
	analog.meaning->mqflags = 0;
	analog.num_samples = num;
	analog.data = data;

	sr_session_send(sdi, &packet);
}

static int read_subframe(const struct sr_dev_inst *sdi, uint8_t *buf)
{
	struct dev_context *devc = sdi->priv;
	uint8_t sig[3], ctrl;
	unsigned int num;
	gboolean interleave;

	interleave = readout_steps[devc->step].interleave;
	ctrl = C1284_NSTROBE;
	if ((interleave && devc->adc2) || (!interleave && devc->channel == 2))
		ctrl |= C1284_NAUTOFD;

	ieee1284_write_control(sdi->conn, ctrl);
	num = readout_steps[devc->step].num;
	if (num < 1000)
		skip_samples(sdi->conn, ctrl, 1000 - num);
	read_samples(sdi->conn, ctrl, buf + (devc->adc2 ? 1 : 0), num,
		     interleave ? 2 : 1);
	read_samples(sdi->conn, ctrl, sig, 3, 1);
	if (sig[0] != 0x01 || sig[1] != 0xfe || sig[2] != 0x80) {
		if (--devc->retries) {
			sr_dbg("Missing signature at end of buffer, %i tries remaining",
			       devc->retries);
			return TRUE;
		} else {
			sr_err("Failed to read frame without transfer errors");
			devc->step = 0;
		}
	} else {
		if (interleave && !devc->adc2) {
			devc->adc2 = TRUE;
			devc->retries = MAX_RETRIES;
			return TRUE;
		} else {
			if (interleave)
				num *= 2;
			if (!devc->step) {
				struct sr_datafeed_packet packet = {
					.type = SR_DF_TRIGGER
				};

				push_samples(sdi, buf, 6);
				sr_session_send(sdi, &packet);
				buf += 6;
				num -= 6;
			}
			push_samples(sdi, buf, num);
			if (++devc->step > devc->last_step)
				devc->step = 0;
		}
	}

	devc->adc2 = FALSE;
	devc->retries = MAX_RETRIES;

	return devc->step > 0;
}

SR_PRIV int hung_chang_dso_2100_poll(int fd, int revents, void *cb_data)
{
	struct sr_datafeed_packet packet = { .type = SR_DF_FRAME_BEGIN };
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	uint8_t state, buf[1000];

	(void)fd;
	(void)revents;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	if (devc->state_known)
		hung_chang_dso_2100_write_mbox(sdi->conn, 0x99);

	state = hung_chang_dso_2100_read_mbox(sdi->conn, 0.00025);
	devc->state_known = (state != 0x00);

	if (!devc->state_known || state == 0x21)
		return TRUE;

	if (state != 0x03) {
		sr_err("Unexpected state 0x%X while checking for trigger", state);
		return FALSE;
	}

	sr_session_send(sdi, &packet);

	if (devc->channel) {
		while (read_subframe(sdi, buf)) {
			if (hung_chang_dso_2100_move_to(sdi, 1) != SR_OK)
				break;
			hung_chang_dso_2100_write_mbox(sdi->conn, 3);
			g_usleep(1700);
			if (hung_chang_dso_2100_read_mbox(sdi->conn, 0.02) != 0x03)
				break;
		}
	}

	packet.type = SR_DF_FRAME_END;
	sr_session_send(sdi, &packet);

	if (++devc->frame >= devc->frame_limit)
		sr_dev_acquisition_stop(sdi);
	else
		hung_chang_dso_2100_move_to(sdi, 0x21);

	return TRUE;
}
