/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2011 Daniel Ribeiro <drwyrm@gmail.com>
 * Copyright (C) 2012 Renato Caldas <rmsc@fe.up.pt>
 * Copyright (C) 2013 Lior Elazary <lelazary@yahoo.com>
 * Copyright (C) 2022 Paul Kasemir <paul.kasemir@gmail.com>
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

#define LA_TRIGGER_MASK_IGNORE_ALL  0xff
#define TRIG_THRESH_START	    0x200 /* Equates to a trigger at 0.0 volts */

#define VBIT_CALIBRATION_DENOMINATOR (10000.0 * 1000.0)
#define OFFSET_VBIT_CALIBRATION_NUMERATOR 3.0

/* serial protocol */
#define mso_trans(a, v) \
	g_htons(((v) & 0x3f) | (((v) & 0xc0) << 6) | (((a) & 0xf) << 8) | \
	((~(v) & 0x20) << 1) | ((~(v) & 0x80) << 7))

static const char mso_head[] = { 0x40, 0x4c, 0x44, 0x53, 0x7e };
static const char mso_foot[] = { 0x7e };

static int mso_send_control_message(struct sr_serial_dev_inst *serial,
				    uint16_t payload[], int n)
{
	int written, ret, payload_size, total_size;
	char *copy_ptr, *buf;

	payload_size = n * sizeof(*payload);
	total_size = payload_size + sizeof(mso_head) + sizeof(mso_foot);
	ret = SR_ERR;

	buf = g_malloc(total_size);

	copy_ptr = buf;
	memcpy(copy_ptr, mso_head, sizeof(mso_head));
	copy_ptr += sizeof(mso_head);
	memcpy(copy_ptr, payload, payload_size);
	copy_ptr += payload_size;
	memcpy(copy_ptr, mso_foot, sizeof(mso_foot));

	written = 0;
	while (written < total_size) {
		ret = serial_write_blocking(serial,
				buf + written,
				total_size - written,
				10);
		if (ret < 0) {
			ret = SR_ERR;
			goto free;
		}
		written += ret;
	}
	ret = SR_OK;
free:
	g_free(buf);
	return ret;
}

static uint16_t mso_bank_select(const struct dev_context *devc, int bank)
{
	if (bank > 2) {
		sr_err("Unsupported bank %d", bank);
	}
	return mso_trans(REG_CTL2, devc->ctlbase2 | BITS_CTL2_BANK(bank));
}

static uint16_t mso_calc_trigger_threshold(struct dev_context *devc)
{
	int threshold;
	/* Trigger threshold is affected by the offset, so we need to add in
	 * the offset */
	threshold = devc->dso_trigger_adjusted + devc->dso_offset_adjusted;
	/* A calibrated raw threshold is always sent in 1x voltage, so need to
	 * scale by probe factor and then by calibration vbit */
	threshold = threshold / devc->dso_probe_factor / devc->vbit;

	return (uint16_t) (TRIG_THRESH_START - threshold);
}

SR_PRIV int mso_configure_trigger(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	uint16_t threshold_value;
	uint8_t reg_trig;

	devc = sdi->priv;
	threshold_value = mso_calc_trigger_threshold(devc);
	// REG_TRIG also holds the 2 MSB bits from the threshold value
	reg_trig = mso_calc_trigger_register(devc, threshold_value);

	uint16_t ops[] = {
		mso_trans(REG_TRIG_THRESH, threshold_value & 0xff),
		mso_trans(REG_TRIG, reg_trig),
		mso_trans(REG_TRIG_LA_VAL, devc->la_trigger),
		mso_trans(REG_TRIG_LA_MASK, devc->la_trigger_mask),
		mso_trans(REG_TRIG_POS_LSB, devc->ctltrig_pos & 0xff),
		mso_trans(REG_TRIG_POS_MSB, (devc->ctltrig_pos >> 8) & 0xff),

		mso_trans(REG_TRIG_WIDTH,
			  devc->dso_trigger_width /
			  SR_HZ_TO_NS(devc->cur_rate)),

		/* Select the SPI/I2C trigger config bank */
		mso_bank_select(devc, 2),
		/* Configure the SPI/I2C protocol trigger */
		mso_trans(REG_PT_WORD(0), devc->protocol_trigger.word[0]),
		mso_trans(REG_PT_WORD(1), devc->protocol_trigger.word[1]),
		mso_trans(REG_PT_WORD(2), devc->protocol_trigger.word[2]),
		mso_trans(REG_PT_WORD(3), devc->protocol_trigger.word[3]),
		mso_trans(REG_PT_MASK(0), devc->protocol_trigger.mask[0]),
		mso_trans(REG_PT_MASK(1), devc->protocol_trigger.mask[1]),
		mso_trans(REG_PT_MASK(2), devc->protocol_trigger.mask[2]),
		mso_trans(REG_PT_MASK(3), devc->protocol_trigger.mask[3]),
		mso_trans(REG_PT_SPIMODE, devc->protocol_trigger.spimode),
		/* Select the default config bank */
		mso_bank_select(devc, 0),
	};

	return mso_send_control_message(devc->serial, ARRAY_AND_SIZE(ops));
}

SR_PRIV int mso_configure_threshold_level(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	return mso_dac_out(sdi, DAC_SELECT_LA | devc->logic_threshold_value);
}

SR_PRIV int mso_read_buffer(struct sr_dev_inst *sdi)
{
	uint16_t ops[] = { mso_trans(REG_BUFFER, 0) };
	struct dev_context *devc = sdi->priv;

	sr_dbg("Requesting buffer dump.");
	return mso_send_control_message(devc->serial, ARRAY_AND_SIZE(ops));
}

SR_PRIV int mso_arm(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	uint16_t ops[] = {
		mso_trans(REG_CTL1, devc->ctlbase1 | BIT_CTL1_RESETFSM),
		mso_trans(REG_CTL1, devc->ctlbase1 | BIT_CTL1_ARM),
		mso_trans(REG_CTL1, devc->ctlbase1),
	};

	sr_dbg("Requesting trigger arm.");
	return mso_send_control_message(devc->serial, ARRAY_AND_SIZE(ops));
}

SR_PRIV int mso_force_capture(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	uint16_t ops[] = {
		mso_trans(REG_CTL1, devc->ctlbase1 | 8),
		mso_trans(REG_CTL1, devc->ctlbase1),
	};

	sr_dbg("Requesting forced capture.");
	return mso_send_control_message(devc->serial, ARRAY_AND_SIZE(ops));
}

SR_PRIV int mso_dac_out(const struct sr_dev_inst *sdi, uint16_t val)
{
	struct dev_context *devc = sdi->priv;
	uint16_t ops[] = {
		mso_trans(REG_DAC_MSB, (val >> 8) & 0xff),
		mso_trans(REG_DAC_LSB, val & 0xff),
		mso_trans(REG_CTL1, devc->ctlbase1 | BIT_CTL1_LOAD_DAC),
	};

	sr_dbg("Setting dac word to 0x%x.", val);
	return mso_send_control_message(devc->serial, ARRAY_AND_SIZE(ops));
}

SR_PRIV int mso_parse_serial(const char *serial_num, const char *product,
			     struct dev_context *devc)
{
	unsigned int u1, u2, u3, u4, u5, u6;

	(void)product;

	/* FIXME: This code is in the original app, but I think its
	 * used only for the GUI */
	/*    if (strstr(product, "REV_02") || strstr(product, "REV_03"))
	   devc->num_sample_rates = 0x16;
	   else
	   devc->num_sample_rates = 0x10; */

	/* Parse serial_num. This code/calulation is based off of another
	 * project https://github.com/tkrmnz/mso19fcgi */
	if (serial_num[0] != '4' || sscanf(serial_num, "%5u%3u%3u%1u%1u%6u",
					   &u1, &u2, &u3, &u4, &u5, &u6) != 6)
		return SR_ERR;

	/* Provide sane defaults if the serial number doesn't look right */
	if (u1 == 0)
		u1 = 42874;
	if (u2 == 0)
		u2 = 343;
	if (u3 == 0)
		u3 = 500;

	/* vbit is a calibration value used to alter the analog signal to
	 * correct for voltage reference inaccuracy */
	devc->vbit = ((double)u1) / VBIT_CALIBRATION_DENOMINATOR;
	/* dac_offset is used to move the voltage range of the detectable
	 * signal up or down by use of a DAC. This way you can detect signals
	 * in a given range, for example [-2v, 2v], [0v, 4v], [-4v, 0v] */
	devc->dac_offset = u2;
	/* offset_vbit is similar to vbit, but used with the dac_offset
	 * calculations. It is a calibration value to correct any inaccuracy in
	 * the voltage output of the DAC */
	devc->offset_vbit = OFFSET_VBIT_CALIBRATION_NUMERATOR / u3;
	devc->hwmodel = u4;
	devc->hwrev = u5;

	/*
	 * FIXME: There is more code on the original software to handle
	 * bigger serial_num strings, but as I can't test on my device
	 * I will not implement it yet
	 */

	return SR_OK;
}

SR_PRIV int mso_reset_adc(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	uint16_t ops[] = {
		mso_bank_select(devc, 0),
		mso_trans(REG_CTL1, BIT_CTL1_RESETADC),
		mso_trans(REG_CTL1, 0),
	};

	sr_dbg("Requesting ADC reset.");
	return mso_send_control_message(devc->serial, ARRAY_AND_SIZE(ops));
}

SR_PRIV int mso_reset_fsm(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	uint16_t ops[] = {
		mso_trans(REG_CTL1, devc->ctlbase1 | BIT_CTL1_RESETFSM),
	};

	sr_dbg("Requesting FSM reset.");
	return mso_send_control_message(devc->serial, ARRAY_AND_SIZE(ops));
}

SR_PRIV void stop_acquisition(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = sdi->priv;
	serial_source_remove(sdi->session, devc->serial);

	std_session_send_df_end(sdi);
}

SR_PRIV int mso_clkrate_out(struct sr_serial_dev_inst *serial, uint16_t val)
{
	uint16_t ops[] = {
		mso_trans(REG_CLKRATE1, (val >> 8) & 0xff),
		mso_trans(REG_CLKRATE2, val & 0xff),
	};

	sr_dbg("Setting clkrate word to 0x%x.", val);
	return mso_send_control_message(serial, ARRAY_AND_SIZE(ops));
}

SR_PRIV int mso_configure_rate(const struct sr_dev_inst *sdi, uint32_t rate)
{
	struct dev_context *devc;
	unsigned int i;
	int ret;

	devc = sdi->priv;
	ret = SR_ERR;

	for (i = 0; i < ARRAY_SIZE(rate_map); i++) {
		if (rate_map[i].rate == rate) {
			devc->ctlbase2 = rate_map[i].slowmode;
			ret = mso_clkrate_out(devc->serial, rate_map[i].val);
			if (ret == SR_OK)
				devc->cur_rate = rate;
			return ret;
		}
	}

	if (ret != SR_OK)
		sr_err("Unsupported rate.");

	return ret;
}

static int mso_validate_status(uint8_t val) {
	uint8_t action;

	action = BITS_STATUS_ACTION(val);
	if (action == 0 || action > STATUS_DATA_READY
			|| val & 0xC0) {
		sr_err("Invalid status byte %.2x", val);
		return SR_ERR;
	}

	return SR_OK;
}

SR_PRIV int mso_read_status(struct sr_serial_dev_inst *serial, uint8_t *status)
{
	uint16_t ops[] = { mso_trans(REG_STATUS, 0) };
	uint8_t buf;
	int ret;

	buf = 0;

	sr_dbg("Requesting status.");
	ret = mso_send_control_message(serial, ARRAY_AND_SIZE(ops));
	if (!status || ret != SR_OK) {
		return ret;
	}

	if (serial_read_blocking(serial, &buf, 1, 10) != 1) {
		sr_err("Reading status failed");
		return SR_ERR;
	}
	ret = mso_validate_status(buf);
	if (ret == SR_OK) {
		*status = buf;
		sr_dbg("Status is: 0x%x.", *status);
	}

	return ret;
}

SR_PRIV int mso_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;

	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	int i;
	int trigger_sample;
	int pre_samples, post_samples;

	uint8_t in[MSO_NUM_SAMPLES];
	size_t s;

	(void)fd;
	(void)revents;

	sdi = cb_data;
	if (!sdi)
		return TRUE;

	devc = sdi->priv;
	if (!devc)
		return TRUE;

	s = serial_read_blocking(devc->serial, in, sizeof(in), 10);
	if (s <= 0)
		return FALSE;

	/* Check if we triggered, then send a command that we are ready
	 * to read the data */
	if (BITS_STATUS_ACTION(devc->status) != STATUS_DATA_READY) {
		if (mso_validate_status(in[0]) != SR_OK) {
			return FALSE;
		}
		devc->status = in[0];
		if (BITS_STATUS_ACTION(devc->status) == STATUS_DATA_READY) {
			mso_read_buffer(sdi);
			devc->buffer_n = 0;
		} else {
			mso_read_status(devc->serial, NULL);
		}
		return TRUE;
	}

	/* the hardware always dumps 1024 samples, 24bits each */
	if (devc->buffer_n < (MSO_NUM_SAMPLES * 3)) {
		memcpy(devc->buffer + devc->buffer_n, in, s);
		devc->buffer_n += s;
	}
	if (devc->buffer_n < (MSO_NUM_SAMPLES * 3))
		return TRUE;

	/* do the conversion */
	uint8_t logic_out[MSO_NUM_SAMPLES];
	float analog_out[MSO_NUM_SAMPLES];
	for (i = 0; i < MSO_NUM_SAMPLES; i++) {
		analog_out[i] = (devc->buffer[i * 3] & 0x3f) |
		    ((devc->buffer[i * 3 + 1] & 0xf) << 6);
		analog_out[i] = (512.0 - analog_out[i]) * devc->vbit
			* devc->dso_probe_factor
			- devc->dso_offset_adjusted;
		logic_out[i] = ((devc->buffer[i * 3 + 1] & 0x30) >> 4) |
		    ((devc->buffer[i * 3 + 2] & 0x3f) << 2);
	}

	if (devc->ctltrig_pos & TRIG_POS_IS_NEGATIVE) {
		trigger_sample = -1;
		pre_samples = MSO_NUM_SAMPLES;
	} else {
		trigger_sample = devc->ctltrig_pos & TRIG_POS_VALUE_MASK;
		pre_samples = MIN(trigger_sample, MSO_NUM_SAMPLES);
	}

	logic.unitsize = sizeof(*logic_out);

	sr_analog_init(&analog, &encoding, &meaning, &spec, 3);
	analog.meaning->channels = g_slist_append(
			NULL, g_slist_nth_data(sdi->channels, 0));
	analog.meaning->mq = SR_MQ_VOLTAGE;
	analog.meaning->unit = SR_UNIT_VOLT;
	analog.meaning->mqflags = SR_MQFLAG_DC;

	if (pre_samples > 0) {
		packet.type = SR_DF_LOGIC;
		packet.payload = &logic;
		logic.length = pre_samples * sizeof(*logic_out);
		logic.data = logic_out;
		sr_session_send(sdi, &packet);

		packet.type = SR_DF_ANALOG;
		packet.payload = &analog;
		analog.num_samples = pre_samples;
		analog.data = analog_out;
		sr_session_send(sdi, &packet);
	}

	if (pre_samples == trigger_sample) {
		std_session_send_df_trigger(sdi);
	}

	post_samples = MSO_NUM_SAMPLES - pre_samples;

	if (post_samples > 0) {
		packet.type = SR_DF_LOGIC;
		packet.payload = &logic;
		logic.length = post_samples * sizeof(*logic_out);
		logic.data = logic_out + pre_samples;
		sr_session_send(sdi, &packet);

		packet.type = SR_DF_ANALOG;
		packet.payload = &analog;
		analog.num_samples = post_samples;
		analog.data = analog_out + pre_samples;
		sr_session_send(sdi, &packet);
	}
	g_slist_free(analog.meaning->channels);

	devc->num_samples += MSO_NUM_SAMPLES;

	if (devc->limit_samples && devc->num_samples >= devc->limit_samples) {
		sr_info("Requested number of samples reached.");
		sr_dev_acquisition_stop(sdi);
	}

	return TRUE;
}

SR_PRIV int mso_configure_channels(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_trigger *trigger;
	struct sr_trigger_stage *stage;
	struct sr_trigger_match *match;
	uint8_t channel_bit;
	GSList *l, *m;

	devc = sdi->priv;

	/* The mask for the LA_TRIGGER
	 * (bits set to 0 matter, those set to 1 are ignored). */
	devc->la_trigger_mask = LA_TRIGGER_MASK_IGNORE_ALL;
	/* The LA byte that generates a trigger event (in that mode).
	 * Set to 0x00 and then bitwise-or in the SR_TRIGGER_ONE bits */
	devc->la_trigger = 0x00;
	trigger = sr_session_trigger_get(sdi->session);
	if (!trigger)
		return SR_OK;
	for (l = trigger->stages; l; l = l->next) {
		stage = l->data;
		for (m = stage->matches; m; m = m->next) {
			match = m->data;
			if (!match->channel->enabled)
				/* Ignore disabled channels with a trigger. */
				continue;
			channel_bit = 1 << match->channel->index;
			devc->la_trigger_mask &= ~channel_bit;
			if (match->match == SR_TRIGGER_ONE)
				devc->la_trigger |= channel_bit;
		}
	}

	return SR_OK;
}
