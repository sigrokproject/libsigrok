/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2021 Kartikay Sharma <sharma.kartik2107@gmail.com>
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
#include "protocol.h"

SR_PRIV int pslab_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct sr_serial_dev_inst *serial;
	struct dev_context *devc;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	struct sr_channel *ch;
	int i, samples_collected;

	(void)fd;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	if (!(revents == G_IO_IN || revents == 0))
		return TRUE;

	serial = sdi->conn;
	ch = devc->channel_entry->data;

	if (pslab_fetch_data(sdi) != SR_OK)
		return TRUE;

	devc->short_int_buffer = g_malloc(2);
	devc->data = g_malloc0((int)devc->limits.limit_samples * sizeof(float));
	samples_collected = 0;

	for (i = 0; i < (int)devc->limits.limit_samples; i++) {
		if (serial_read_blocking(serial, devc->short_int_buffer, 2,
					 serial_timeout(serial, 2)) < 2) {
			sr_dbg("Failed to read buffer properly, samples read = %d", samples_collected);
			break;
		}
		devc->data[i] = pslab_scale(ch, *devc->short_int_buffer);
		samples_collected = i;
	}
	pslab_get_ack(sdi);

	sr_analog_init(&analog, &encoding, &meaning, &spec, 6);
	analog.meaning->channels = g_slist_append(NULL, ch);
	analog.num_samples = samples_collected + 1;
	analog.data = devc->data;
	analog.meaning->mq = SR_MQ_VOLTAGE;
	analog.meaning->unit = SR_UNIT_VOLT;
	analog.meaning->mqflags = 0;
	analog.encoding->unitsize = sizeof(float);
	analog.encoding->is_float = TRUE;
	analog.encoding->is_signed = TRUE;

	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	sr_session_send(sdi, &packet);
	g_slist_free(analog.meaning->channels);
	g_free(devc->short_int_buffer);
	g_free(devc->data);

	if (devc->channel_entry->next) {
		/* We got the samples for this channel, now get the next channel. */
		devc->channel_entry = devc->channel_entry->next;
	} else {
		/* Samples collected from all channels. */
		devc->channel_entry = NULL;
		devc->enabled_channels = NULL;
		g_slist_free(devc->channel_entry);
		g_slist_free(devc->enabled_channels);
		std_session_send_df_frame_end(sdi);
		sr_dev_acquisition_stop(sdi);
	}

	return TRUE;
}

SR_PRIV void pslab_write_u8(struct sr_serial_dev_inst* serial, uint8_t cmd[], int count)
{
	int i, bytes_written;
	for (i = 0; i < count; i++) {
		bytes_written = serial_write_blocking(serial, &cmd[i], 1,
							  serial_timeout(serial, 1));

		if (bytes_written < 1)
			sr_dbg("Failed to write command %d to device.", cmd[i]);
	}

}

SR_PRIV void pslab_write_u16(struct sr_serial_dev_inst* serial, uint16_t val[], int count)
{
	int i, bytes_written;
	for (i = 0; i < count; i++) {
		bytes_written = serial_write_blocking(serial, &val[i], 2,
							  serial_timeout(serial, 2));
		if (bytes_written < 2)
			sr_dbg("Failed to write command %d to device.", val[i]);
	}
}

SR_PRIV char* pslab_get_version(struct sr_serial_dev_inst* serial)
{
	sr_info("Sending version commands to device");

	uint8_t cmd[] = {COMMON, VERSION_COMMAND};
	char *buffer = g_malloc0(16);
	int len = 15;
	pslab_write_u8(serial, cmd, 2);
	serial_readline(serial, &buffer, &len, serial_timeout(serial, sizeof(buffer)));
	return buffer;
}

SR_PRIV void pslab_caputure_oscilloscope(const struct sr_dev_inst *sdi)
{
	sr_info("Sending oscilloscope capture commands to device");

	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	struct sr_channel *ch;
	struct channel_priv *cp;
	int i, chosa;
	struct channel_priv *cp_map;
	char *ch234[3] = {"CH2", "CH3", "MIC"};

	devc = sdi->priv;
	serial = sdi->conn;
	cp_map = devc->channel_one_map->priv;
	pslab_set_resolution(devc->channel_one_map, 10);
	chosa = cp_map->chosa;
	cp_map->buffer_idx = 0;

	uint8_t command[] = {ADC};
	pslab_write_u8(serial, command, 1);

	if (g_slist_length(devc->enabled_channels) == 1) {
		if (devc->trigger_enabled) {
			uint8_t cmd[] = {CAPTURE_ONE, (chosa | 0x80)};
			pslab_write_u8(serial, cmd, 2);
		}

		if (devc->samplerate <= 1000000) {
			pslab_set_resolution(devc->channel_one_map, 12);
			uint8_t cmd[] = {CAPTURE_DMASPEED, (chosa | 0x80)};
			pslab_write_u8(serial, cmd, 2);
		} else {
			uint8_t cmd[] = {CAPTURE_DMASPEED, chosa};
			pslab_write_u8(serial, cmd, 2);
		}
	} else if (g_slist_length(devc->enabled_channels) == 2) {
		ch = g_malloc0(sizeof(struct sr_channel));
		if (assign_channel(ch234[0], ch, devc->enabled_channels) == SR_OK) {
			cp = ch->priv;
			pslab_set_resolution(ch, 10);
			cp->buffer_idx = (int) devc->limits.limit_samples;
		}
		uint8_t cmd[] = {CAPTURE_TWO, chosa | (0x80 * devc->trigger_enabled)};
		pslab_write_u8(serial, cmd, 2);
	} else {
		for (i = 0; i < 3; i++) {
			ch = g_malloc0(sizeof (struct sr_channel));
			if (assign_channel(ch234[i], ch, devc->enabled_channels) != SR_OK)
				break;
			cp = ch->priv;
			pslab_set_resolution(ch, 10);
			cp->buffer_idx = (i + 1) * (int)devc->limits.limit_samples;
		}
		uint8_t cmd[] = {CAPTURE_FOUR, (chosa | (0 << 4) | (0x80 * devc->trigger_enabled))};
		pslab_write_u8(serial, cmd, 2);
	}

	uint16_t val[] = {devc->limits.limit_samples, (int)(8000000/devc->samplerate)};
	pslab_write_u16(serial, val, 2);

	if (pslab_get_ack(sdi) != SR_OK)
		sr_dbg("Failed to capture samples");

	g_usleep(8000000 * devc->limits.limit_samples / devc->samplerate);

	while (!pslab_progress(sdi))
		continue;

}

SR_PRIV int pslab_fetch_data(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_channel *ch;
	struct sr_serial_dev_inst *serial;

	if (!(devc = sdi->priv))
		return SR_ERR;

	ch = devc->channel_entry->data;
	serial = sdi->conn;

	sr_info("Reading buffer of channel %s", ch->name);

	uint8_t cmd[] = {COMMON, RETRIEVE_BUFFER};
	pslab_write_u8(serial, cmd, 2);

	uint16_t val[] = {((struct channel_priv *)(ch->priv))->buffer_idx,
		devc->limits.limit_samples};
	pslab_write_u16(serial, val, 2);

	return SR_OK;
}

SR_PRIV void pslab_set_resolution(const struct sr_channel *ch, int resolution)
{
	sr_info("Setting %s resolution to %d", ch->name, resolution);
	struct channel_priv *cp;

	cp = ch->priv;
	cp->resolution = pow(2,resolution) - 1;
}

SR_PRIV gboolean pslab_progress(const struct sr_dev_inst *sdi)
{
	sr_info("Checking if all the samples have been captured in buffer");
	struct sr_serial_dev_inst *serial;

	serial = sdi->conn;
	uint8_t cmd[] = {ADC, GET_CAPTURE_STATUS};
	pslab_write_u8(serial, cmd, 2);

	uint8_t *buf = g_malloc0(1);
	serial_read_blocking(serial,buf,1, serial_timeout(serial,1));
	uint8_t capturing_complete = *buf;
	uint16_t *buf2 = g_malloc0(2);
	serial_read_blocking(serial,buf2,2, serial_timeout(serial,2));
	uint16_t samples_read = *buf2;

	sr_dbg("Samples Read = %d", samples_read);
	if (pslab_get_ack(sdi) != SR_OK)
		sr_dbg("Failed in knowing capturing status");

	g_free(buf);
	g_free(buf2);
	return capturing_complete;
}

SR_PRIV int pslab_set_gain(const struct sr_dev_inst *sdi,
	const struct sr_channel *ch, uint16_t gain)
{
	sr_info("Set gain of channel %s to %d", ch->name, gain);
	struct sr_serial_dev_inst *serial;
	struct channel_priv *cp;
	int gain_idx;

	if (g_strcmp0(ch->name,"CH1") && g_strcmp0(ch->name,"CH2")) {
		sr_info("Analog gain is not available on %s", ch->name);
		return SR_ERR_ARG;
	}

	serial = sdi->conn;
	cp = ch->priv;
	gain_idx = std_u64_idx(g_variant_new_uint64(gain), GAIN_VALUES, 8);

	if (gain_idx < 0) {
		sr_dbg("Invalid gain value %d", gain);
		return SR_ERR_ARG;
	}

	uint8_t cmd[] = {ADC, SET_PGA_GAIN, cp->programmable_gain_amplifier, (uint8_t)gain_idx};
	pslab_write_u8(serial, cmd, 4);

	if (pslab_get_ack(sdi) != SR_OK) {
		sr_dbg("Could not set set gain %d on channel %s", gain, ch->name);
		return SR_ERR_IO;
	}

	return SR_OK;
}

SR_PRIV void pslab_configure_trigger(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	int channel;

	devc = sdi->priv;
	serial = sdi->conn;

	sr_info("Configuring trigger on channel %s at %f Volts",
		devc->trigger_channel.name, devc->trigger_voltage);

	if (devc->trigger_channel.name == devc->channel_one_map->name)
		channel = 0;
	else
		channel = devc->trigger_channel.index;

	uint8_t cmd[] = {ADC, CONFIGURE_TRIGGER, (0 << 4) | (1 << channel)};
	pslab_write_u8(serial, cmd, 3);
	uint16_t level = pslab_unscale(&devc->trigger_channel, devc->trigger_voltage);
	serial_write_blocking(serial,&level, 2, serial_timeout(serial, 2));

	if (pslab_get_ack(sdi) != SR_OK)
		sr_dbg("Could not configure trigger on channel %s, voltage = %f raw value = %d",
			devc->trigger_channel.name, devc->trigger_voltage, level);

}

SR_PRIV float pslab_scale(const struct sr_channel *ch, uint16_t raw_value)
{
	sr_spew("Scaling raw value %d to voltage", raw_value);
	struct channel_priv *cp;
	float slope, intercept;

	cp = ch->priv;
	slope = (float)((cp->max_input - cp->min_input) / cp->resolution / cp->gain);
	intercept = (float)(cp->min_input/cp->gain);
	return slope * (float)raw_value + intercept;
}

SR_PRIV int pslab_unscale(const struct sr_channel *ch, double voltage)
{
	sr_spew("Scaling voltage %f to raw value", voltage);
	struct channel_priv *cp;
	double slope, intercept;
	int level;

	cp = ch->priv;
	slope = (cp->max_input/cp->gain - cp->min_input/cp->gain) / cp->resolution;
	intercept = cp->min_input/cp->gain;
	level = (int)((voltage - intercept) / slope);
	if (level < 0)
		level = 0;
	else if (level > cp->resolution)
		level = (int)cp->resolution;

	sr_dbg("Unscaled Voltage = %d", level);
	return level;
}

SR_PRIV int pslab_get_ack(const struct sr_dev_inst *sdi)
{
	struct sr_serial_dev_inst *serial;
	int *buf;

	serial = sdi->conn;
	buf = g_malloc0(1);
	serial_read_blocking(serial,buf,1, serial_timeout(serial,1));

	if (!(*buf & 0x01) || !(*buf)) {
		sr_dbg("Did not receive ACK or Received non ACK byte while waiting for ACK.");
		g_free(buf);
		return SR_ERR_IO;
	}

	g_free(buf);
	return SR_OK;
}

SR_PRIV int assign_channel(const char* channel_name,
			   struct sr_channel *target, GSList* list)
{
	sr_info("Assign channel %s from list to target", channel_name);
	GSList *l;
	struct sr_channel *ch;

	for (l = list; l; l = l->next) {
		ch = l->data;
		if (!g_strcmp0(ch->name, channel_name)) {
			*target = *ch;
			return SR_OK;
		}
	}
	return SR_ERR_ARG;
}
