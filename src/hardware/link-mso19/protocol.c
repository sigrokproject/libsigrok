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

/* serial protocol */
#define mso_trans(a, v) \
	(((v) & 0x3f) | (((v) & 0xc0) << 6) | (((a) & 0xf) << 8) | \
	((~(v) & 0x20) << 1) | ((~(v) & 0x80) << 7))

static const char mso_head[] = { 0x40, 0x4c, 0x44, 0x53, 0x7e };
static const char mso_foot[] = { 0x7e };

static int mso_send_control_message(struct sr_serial_dev_inst *serial,
				    uint16_t payload[], int n)
{
	int i, w, ret, s = n * 2 + sizeof(mso_head) + sizeof(mso_foot);
	char *p, *buf;

	ret = SR_ERR;

	buf = g_malloc(s);

	p = buf;
	memcpy(p, mso_head, sizeof(mso_head));
	p += sizeof(mso_head);

	for (i = 0; i < n; i++) {
		WB16(p, payload[i]);
		p += sizeof(uint16_t);
	}
	memcpy(p, mso_foot, sizeof(mso_foot));

	w = 0;
	while (w < s) {
		ret = serial_write_blocking(serial, buf + w, s - w, 10);
		if (ret < 0) {
			ret = SR_ERR;
			goto free;
		}
		w += ret;
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

SR_PRIV int mso_configure_trigger(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	uint16_t threshold_value = mso_calc_raw_from_mv(devc);

	// Use 0x200 temporary value till we can properly calculate it.
	threshold_value = 0x200;
	uint8_t trigger_config = 0;

	if (devc->trigger_slope)
		trigger_config |= 0x04;	//Trigger on falling edge

	switch (devc->trigger_outsrc) {
	case 1:
		trigger_config |= 0x00;	//Trigger pulse output
		break;
	case 2:
		trigger_config |= 0x08;	//PWM DAC from the pattern generator buffer
		break;
	case 3:
		trigger_config |= 0x18;	//White noise
		break;
	}

	switch (devc->trigger_chan) {
	case 0:
		trigger_config |= 0x00;	//DSO level trigger //b00000000
		break;
	case 1:
		trigger_config |= 0x20;	//DSO level trigger & width < trigger_width
		break;
	case 2:
		trigger_config |= 0x40;	//DSO level trigger & width >= trigger_width
		break;
	case 3:
		trigger_config |= 0x60;	//LA combination trigger
		break;
	}

	//Last bit of trigger config reg 4 needs to be 1 for trigger enable,
	//otherwise the trigger is not enabled
	if (devc->use_trigger)
		trigger_config |= 0x80;

	uint16_t ops[18];
	ops[0] = mso_trans(3, threshold_value & 0xff);
	//The trigger_config also holds the 2 MSB bits from the threshold value
	ops[1] = mso_trans(4, trigger_config | ((threshold_value >> 8) & 0x03));
	ops[2] = mso_trans(5, devc->la_trigger);
	ops[3] = mso_trans(6, devc->la_trigger_mask);
	ops[4] = mso_trans(7, devc->trigger_holdoff[0]);
	ops[5] = mso_trans(8, devc->trigger_holdoff[1]);

	ops[6] = mso_trans(11,
			   devc->dso_trigger_width /
			   SR_HZ_TO_NS(devc->cur_rate));

	/* Select the SPI/I2C trigger config bank */
	ops[7] = mso_bank_select(devc, 2);
	/* Configure the SPI/I2C protocol trigger */
	ops[8] = mso_trans(REG_PT_WORD(0), devc->protocol_trigger.word[0]);
	ops[9] = mso_trans(REG_PT_WORD(1), devc->protocol_trigger.word[1]);
	ops[10] = mso_trans(REG_PT_WORD(2), devc->protocol_trigger.word[2]);
	ops[11] = mso_trans(REG_PT_WORD(3), devc->protocol_trigger.word[3]);
	ops[12] = mso_trans(REG_PT_MASK(0), devc->protocol_trigger.mask[0]);
	ops[13] = mso_trans(REG_PT_MASK(1), devc->protocol_trigger.mask[1]);
	ops[14] = mso_trans(REG_PT_MASK(2), devc->protocol_trigger.mask[2]);
	ops[15] = mso_trans(REG_PT_MASK(3), devc->protocol_trigger.mask[3]);
	ops[16] = mso_trans(REG_PT_SPIMODE, devc->protocol_trigger.spimode);
	/* Select the default config bank */
	ops[17] = mso_bank_select(devc, 0);

	return mso_send_control_message(devc->serial, ARRAY_AND_SIZE(ops));
}

SR_PRIV int mso_configure_threshold_level(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	return mso_dac_out(sdi, la_threshold_map[devc->la_threshold]);
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
		mso_trans(REG_DAC1, (val >> 8) & 0xff),
		mso_trans(REG_DAC2, val & 0xff),
		mso_trans(REG_CTL1, devc->ctlbase1 | BIT_CTL1_LOAD_DAC),
	};

	sr_dbg("Setting dac word to 0x%x.", val);
	return mso_send_control_message(devc->serial, ARRAY_AND_SIZE(ops));
}

SR_PRIV uint16_t mso_calc_raw_from_mv(struct dev_context *devc)
{
	return (uint16_t) (0x200 -
			   ((devc->dso_trigger_voltage / devc->dso_probe_attn) /
			    devc->vbit));
}

SR_PRIV int mso_parse_serial(const char *iSerial, const char *iProduct,
			     struct dev_context *devc)
{
	unsigned int u1, u2, u3, u4, u5, u6;

	(void)iProduct;

	/* FIXME: This code is in the original app, but I think its
	 * used only for the GUI */
	/*    if (strstr(iProduct, "REV_02") || strstr(iProduct, "REV_03"))
	   devc->num_sample_rates = 0x16;
	   else
	   devc->num_sample_rates = 0x10; */

	/* parse iSerial */
	if (iSerial[0] != '4' || sscanf(iSerial, "%5u%3u%3u%1u%1u%6u",
					&u1, &u2, &u3, &u4, &u5, &u6) != 6)
		return SR_ERR;
	if (u1 == 0)
		u1 = 42874;
	if (u2 == 0)
		u2 = 343;
	if (u3 == 0)
		u3 = 500;
	devc->vbit = ((double)u1) / 10000.0 / 1000.0;
	devc->dac_offset = u2;
	devc->offset_range = u3;
	devc->hwmodel = u4;
	devc->hwrev = u5;

	/*
	 * FIXME: There is more code on the original software to handle
	 * bigger iSerial strings, but as I can't test on my device
	 * I will not implement it yet
	 */

	return SR_OK;
}

SR_PRIV int mso_reset_adc(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	uint16_t ops[3];

	ops[0] = mso_bank_select(devc, 0);
	ops[1] = mso_trans(REG_CTL1, BIT_CTL1_RESETADC);
	ops[2] = mso_trans(REG_CTL1, 0);

	sr_dbg("Requesting ADC reset.");
	return mso_send_control_message(devc->serial, ARRAY_AND_SIZE(ops));
}

SR_PRIV int mso_reset_fsm(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	uint16_t ops[1];

	ops[0] = mso_trans(REG_CTL1, devc->ctlbase1 | BIT_CTL1_RESETFSM);

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
	struct dev_context *devc = sdi->priv;
	unsigned int i;
	int ret = SR_ERR;

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
	uint8_t action = BITS_STATUS_ACTION(val);
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
	uint8_t buf = 0;
	int ret;

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
			* devc->dso_probe_attn;
		logic_out[i] = ((devc->buffer[i * 3 + 1] & 0x30) >> 4) |
		    ((devc->buffer[i * 3 + 2] & 0x3f) << 2);
	}

	packet.type = SR_DF_LOGIC;
	packet.payload = &logic;
	logic.length = MSO_NUM_SAMPLES;
	logic.unitsize = 1;
	logic.data = logic_out;
	sr_session_send(sdi, &packet);

	sr_analog_init(&analog, &encoding, &meaning, &spec, 3);
	analog.meaning->channels = g_slist_append(NULL, g_slist_nth_data(sdi->channels, 0));
	analog.num_samples = MSO_NUM_SAMPLES;
	analog.data = analog_out;
	analog.meaning->mq = SR_MQ_VOLTAGE;
	analog.meaning->unit = SR_UNIT_VOLT;
	analog.meaning->mqflags = SR_MQFLAG_DC;

	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	sr_session_send(sdi, &packet);
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
	struct sr_channel *ch;
	GSList *l;
	char *tc;

	devc = sdi->priv;

	devc->la_trigger_mask = 0xFF;	//the mask for the LA_TRIGGER (bits set to 0 matter, those set to 1 are ignored).
	devc->la_trigger = 0x00;	//The value of the LA byte that generates a trigger event (in that mode).
	devc->dso_trigger_voltage = 3;
	devc->dso_probe_attn = 1;
	devc->trigger_outsrc = 0;
	//devc->trigger_chan = 3;	//LA combination trigger
	devc->trigger_chan = 0;	// DSO trigger
	devc->use_trigger = FALSE;

	/*
	for (l = sdi->channels; l; l = l->next) {
		ch = (struct sr_channel *)l->data;
		if (ch->enabled == FALSE)
			continue;

		int channel_bit = 1 << (ch->index);
		if (!(ch->trigger))
			continue;

		devc->use_trigger = TRUE;
		//Configure trigger mask and value.
		for (tc = ch->trigger; *tc; tc++) {
			devc->la_trigger_mask &= ~channel_bit;
			if (*tc == '1')
				devc->la_trigger |= channel_bit;
		}
	}
	*/

	return SR_OK;
}
