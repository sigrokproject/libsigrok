/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2010 Bert Vermeulen <bert@biot.com>
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

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <termios.h>
#endif
#include <string.h>
#include <sys/time.h>
#include <inttypes.h>
#ifdef _WIN32
/* TODO */
#else
#include <arpa/inet.h>
#endif
#include <glib.h>
#include <sigrok.h>
#include <sigrok-internal.h>
#include "ols.h"

#ifdef _WIN32
#define O_NONBLOCK FIONBIO
#endif


static int capabilities[] = {
	SR_HWCAP_LOGIC_ANALYZER,
	SR_HWCAP_SAMPLERATE,
	SR_HWCAP_CAPTURE_RATIO,
	SR_HWCAP_LIMIT_SAMPLES,
	0,
};

/* default supported samplerates, can be overridden by device metadata */
static struct sr_samplerates samplerates = {
	SR_HZ(10),
	SR_MHZ(200),
	SR_HZ(1),
	NULL,
};

/* List of struct sr_serial_device_instance */
static GSList *device_instances = NULL;



static int send_shortcommand(int fd, uint8_t command)
{
	char buf[1];

	g_debug("ols: sending cmd 0x%.2x", command);
	buf[0] = command;
	if (serial_write(fd, buf, 1) != 1)
		return SR_ERR;

	return SR_OK;
}

static int send_longcommand(int fd, uint8_t command, uint32_t data)
{
	char buf[5];

	g_debug("ols: sending cmd 0x%.2x data 0x%.8x", command, data);
	buf[0] = command;
	buf[1] = (data & 0xff000000) >> 24;
	buf[2] = (data & 0xff0000) >> 16;
	buf[3] = (data & 0xff00) >> 8;
	buf[4] = data & 0xff;
	if (serial_write(fd, buf, 5) != 5)
		return SR_ERR;

	return SR_OK;
}

static int configure_probes(struct ols_device *ols, GSList *probes)
{
	struct sr_probe *probe;
	GSList *l;
	int probe_bit, stage, i;
	char *tc;

	ols->probe_mask = 0;
	for (i = 0; i < NUM_TRIGGER_STAGES; i++) {
		ols->trigger_mask[i] = 0;
		ols->trigger_value[i] = 0;
	}

	ols->num_stages = 0;
	for (l = probes; l; l = l->next) {
		probe = (struct sr_probe *)l->data;
		if (!probe->enabled)
			continue;

		/*
		 * Set up the probe mask for later configuration into the
		 * flag register.
		 */
		probe_bit = 1 << (probe->index - 1);
		ols->probe_mask |= probe_bit;

		if (!probe->trigger)
			continue;

		/* Configure trigger mask and value. */
		stage = 0;
		for (tc = probe->trigger; tc && *tc; tc++) {
			ols->trigger_mask[stage] |= probe_bit;
			if (*tc == '1')
				ols->trigger_value[stage] |= probe_bit;
			stage++;
			if (stage > 3)
				/*
				 * TODO: Only supporting parallel mode, with
				 * up to 4 stages.
				 */
				return SR_ERR;
		}
		if (stage > ols->num_stages)
			ols->num_stages = stage;
	}

	return SR_OK;
}

static uint32_t reverse16(uint32_t in)
{
	uint32_t out;

	out = (in & 0xff) << 8;
	out |= (in & 0xff00) >> 8;
	out |= (in & 0xff0000) << 8;
	out |= (in & 0xff000000) >> 8;

	return out;
}

static uint32_t reverse32(uint32_t in)
{
	uint32_t out;

	out = (in & 0xff) << 24;
	out |= (in & 0xff00) << 8;
	out |= (in & 0xff0000) >> 8;
	out |= (in & 0xff000000) >> 24;

	return out;
}

static struct ols_device *ols_device_new(void)
{
	struct ols_device *ols;

	ols = g_malloc0(sizeof(struct ols_device));
	ols->trigger_at = -1;
	ols->probe_mask = 0xffffffff;
	ols->cur_samplerate = SR_KHZ(200);

	return ols;
}

static struct sr_device_instance *get_metadata(int fd)
{
	struct sr_device_instance *sdi;
	struct ols_device *ols;
	uint32_t tmp_int;
	uint8_t key, type, token;
	GString *tmp_str, *devicename, *version;
	gchar tmp_c;

	sdi = sr_device_instance_new(0, SR_ST_INACTIVE, NULL, NULL, NULL);
	ols = ols_device_new();
	sdi->priv = ols;

	devicename = g_string_new("");
	version = g_string_new("");

	key = 0xff;
	while (key) {
		if (serial_read(fd, &key, 1) != 1 || key == 0x00)
			break;
		type = key >> 5;
		token = key & 0x1f;
		switch (type) {
		case 0:
			/* NULL-terminated string */
			tmp_str = g_string_new("");
			while (serial_read(fd, &tmp_c, 1) == 1 && tmp_c != '\0')
				g_string_append_c(tmp_str, tmp_c);
			g_debug("ols: got metadata key 0x%.2x value '%s'", key, tmp_str->str);
			switch (token) {
			case 0x01:
				/* Device name */
				devicename = g_string_append(devicename, tmp_str->str);
				break;
			case 0x02:
				/* FPGA firmware version */
				if (version->len)
					g_string_append(version, ", ");
				g_string_append(version, "FPGA version ");
				g_string_append(version, tmp_str->str);
				break;
			case 0x03:
				/* Ancillary version */
				if (version->len)
					g_string_append(version, ", ");
				g_string_append(version, "Ancillary version ");
				g_string_append(version, tmp_str->str);
				break;
			default:
				g_message("ols: unknown token 0x%.2x: '%s'", token, tmp_str->str);
				break;
			}
			g_string_free(tmp_str, TRUE);
			break;
		case 1:
			/* 32-bit unsigned integer */
			if (serial_read(fd, &tmp_int, 4) != 4)
				break;
			tmp_int = reverse32(tmp_int);
			g_debug("ols: got metadata key 0x%.2x value 0x%.8x", key, tmp_int);
			switch (token) {
			case 0x00:
				/* Number of usable probes */
				ols->num_probes = tmp_int;
				break;
			case 0x01:
				/* Amount of sample memory available (bytes) */
				ols->max_samples = tmp_int;
				break;
			case 0x02:
				/* Amount of dynamic memory available (bytes) */
				/* what is this for? */
				break;
			case 0x03:
				/* Maximum sample rate (hz) */
				ols->max_samplerate = tmp_int;
				break;
			case 0x04:
				/* protocol version */
				ols->protocol_version = tmp_int;
				break;
			default:
				g_message("ols: unknown token 0x%.2x: 0x%.8x", token, tmp_int);
				break;
			}
			break;
		case 2:
			/* 8-bit unsigned integer */
			if (serial_read(fd, &tmp_c, 1) != 1)
				break;
			g_debug("ols: got metadata key 0x%.2x value 0x%.2x", key, tmp_c);
			switch (token) {
			case 0x00:
				/* Number of usable probes */
				ols->num_probes = tmp_c;
				break;
			case 0x01:
				/* protocol version */
				ols->protocol_version = tmp_c;
				break;
			default:
				g_message("ols: unknown token 0x%.2x: 0x%.2x", token, tmp_c);
				break;
			}
			break;
		default:
			/* unknown type */
			break;
		}
	}

	sdi->model = devicename->str;
	sdi->version = version->str;
	g_string_free(devicename, FALSE);
	g_string_free(version, FALSE);

	return sdi;
}


static int hw_init(const char *deviceinfo)
{
	struct sr_device_instance *sdi;
	struct ols_device *ols;
	GSList *ports, *l;
	GPollFD *fds, probefd;
	int devcnt, final_devcnt, num_ports, fd, ret, i;
	char buf[8], **device_names, **serial_params;

	if (deviceinfo)
		ports = g_slist_append(NULL, strdup(deviceinfo));
	else
		/* No specific device given, so scan all serial ports. */
		ports = list_serial_ports();

	num_ports = g_slist_length(ports);
	fds = calloc(1, num_ports * sizeof(GPollFD));
	device_names = malloc(num_ports * sizeof(char *));
	serial_params = malloc(num_ports * sizeof(char *));
	devcnt = 0;
	for (l = ports; l; l = l->next) {
		/* The discovery procedure is like this: first send the Reset
		 * command (0x00) 5 times, since the device could be anywhere
		 * in a 5-byte command. Then send the ID command (0x02).
		 * If the device responds with 4 bytes ("OLS1" or "SLA1"), we
		 * have a match.
		 *
		 * Since it may take the device a while to respond at 115Kb/s,
		 * we do all the sending first, then wait for all of them to
		 * respond with g_poll().
		 */
		g_message("ols: probing %s...", (char *)l->data);
		fd = serial_open(l->data, O_RDWR | O_NONBLOCK);
		if (fd != -1) {
			serial_params[devcnt] = serial_backup_params(fd);
			serial_set_params(fd, 115200, 8, 0, 1, 2);
			ret = SR_OK;
			for (i = 0; i < 5; i++) {
				if ((ret = send_shortcommand(fd,
					CMD_RESET)) != SR_OK) {
					/* Serial port is not writable. */
					break;
				}
			}
			if (ret != SR_OK) {
				serial_restore_params(fd,
					serial_params[devcnt]);
				serial_close(fd);
				continue;
			}
			send_shortcommand(fd, CMD_ID);
			fds[devcnt].fd = fd;
			fds[devcnt].events = G_IO_IN;
			device_names[devcnt] = strdup(l->data);
			devcnt++;
		}
		free(l->data);
	}

	/* 2ms isn't enough for reliable transfer with pl2303, let's try 10 */
	usleep(10000);

	final_devcnt = 0;
	g_poll(fds, devcnt, 1);

	for (i = 0; i < devcnt; i++) {
		if (fds[i].revents != G_IO_IN)
			continue;
		if (serial_read(fds[i].fd, buf, 4) != 4)
			continue;
		if (strncmp(buf, "1SLO", 4) && strncmp(buf, "1ALS", 4))
			continue;

		/* definitely using the OLS protocol, check if it supports
		 * the metadata command
		 */
		send_shortcommand(fds[i].fd, CMD_METADATA);
		probefd.fd = fds[i].fd;
		probefd.events = G_IO_IN;
		if (g_poll(&probefd, 1, 10) > 0) {
			/* got metadata */
			sdi = get_metadata(fds[i].fd);
			sdi->index = final_devcnt;
		} else {
			/* not an OLS -- some other board that uses the sump protocol */
			sdi = sr_device_instance_new(final_devcnt, SR_ST_INACTIVE,
					"Sump", "Logic Analyzer", "v1.0");
			ols = ols_device_new();
			ols->num_probes = 32;
			sdi->priv = ols;
		}
		sdi->serial = sr_serial_device_instance_new(device_names[i], -1);
		device_instances = g_slist_append(device_instances, sdi);
		final_devcnt++;
		serial_close(fds[i].fd);
		fds[i].fd = 0;

	}

	/* clean up after all the probing */
	for (i = 0; i < devcnt; i++) {
		if (fds[i].fd != 0) {
			serial_restore_params(fds[i].fd, serial_params[i]);
			serial_close(fds[i].fd);
		}
		free(serial_params[i]);
		free(device_names[i]);
	}

	free(fds);
	free(device_names);
	free(serial_params);
	g_slist_free(ports);

	return final_devcnt;
}

static int hw_opendev(int device_index)
{
	struct sr_device_instance *sdi;

	if (!(sdi = sr_get_device_instance(device_instances, device_index)))
		return SR_ERR;

	sdi->serial->fd = serial_open(sdi->serial->port, O_RDWR);
	if (sdi->serial->fd == -1)
		return SR_ERR;

	sdi->status = SR_ST_ACTIVE;

	return SR_OK;
}

static void hw_closedev(int device_index)
{
	struct sr_device_instance *sdi;

	if (!(sdi = sr_get_device_instance(device_instances, device_index)))
		return;

	if (sdi->serial->fd != -1) {
		serial_close(sdi->serial->fd);
		sdi->serial->fd = -1;
		sdi->status = SR_ST_INACTIVE;
	}
}

static void hw_cleanup(void)
{
	GSList *l;
	struct sr_device_instance *sdi;

	/* Properly close all devices. */
	for (l = device_instances; l; l = l->next) {
		sdi = l->data;
		if (sdi->serial->fd != -1)
			serial_close(sdi->serial->fd);
		sr_device_instance_free(sdi);
	}
	g_slist_free(device_instances);
	device_instances = NULL;
}

static void *hw_get_device_info(int device_index, int device_info_id)
{
	struct sr_device_instance *sdi;
	struct ols_device *ols;
	void *info;

	if (!(sdi = sr_get_device_instance(device_instances, device_index)))
		return NULL;
	ols = sdi->priv;

	info = NULL;
	switch (device_info_id) {
	case SR_DI_INSTANCE:
		info = sdi;
		break;
	case SR_DI_NUM_PROBES:
		info = GINT_TO_POINTER(NUM_PROBES);
		break;
	case SR_DI_SAMPLERATES:
		info = &samplerates;
		break;
	case SR_DI_TRIGGER_TYPES:
		info = (char *)TRIGGER_TYPES;
		break;
	case SR_DI_CUR_SAMPLERATE:
		info = &ols->cur_samplerate;
		break;
	}

	return info;
}

static int hw_get_status(int device_index)
{
	struct sr_device_instance *sdi;

	if (!(sdi = sr_get_device_instance(device_instances, device_index)))
		return SR_ST_NOT_FOUND;

	return sdi->status;
}

static int *hw_get_capabilities(void)
{
	return capabilities;
}

static int set_configuration_samplerate(struct sr_device_instance *sdi,
					uint64_t samplerate)
{
	struct ols_device *ols;

	ols = sdi->priv;
	if (ols->max_samplerate) {
		if (samplerate > ols->max_samplerate)
			return SR_ERR_SAMPLERATE;
	} else if (samplerate < samplerates.low || samplerate > samplerates.high)
		return SR_ERR_SAMPLERATE;

	ols->cur_samplerate = samplerate;
	if (samplerate > CLOCK_RATE) {
		ols->flag_reg |= FLAG_DEMUX;
		ols->cur_samplerate_divider = (CLOCK_RATE * 2 / samplerate) - 1;
	} else {
		ols->flag_reg &= ~FLAG_DEMUX;
		ols->cur_samplerate_divider = (CLOCK_RATE / samplerate) - 1;
	}

	return SR_OK;
}

static int hw_set_configuration(int device_index, int capability, void *value)
{
	struct sr_device_instance *sdi;
	struct ols_device *ols;
	int ret;
	uint64_t *tmp_u64;

	if (!(sdi = sr_get_device_instance(device_instances, device_index)))
		return SR_ERR;
	ols = sdi->priv;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR;

	switch (capability) {
	case SR_HWCAP_SAMPLERATE:
		tmp_u64 = value;
		ret = set_configuration_samplerate(sdi, *tmp_u64);
		break;
	case SR_HWCAP_PROBECONFIG:
		ret = configure_probes(ols, (GSList *) value);
		break;
	case SR_HWCAP_LIMIT_SAMPLES:
		tmp_u64 = value;
		if (*tmp_u64 < MIN_NUM_SAMPLES)
			return SR_ERR;
		ols->limit_samples = *tmp_u64;
		g_message("ols: sample limit %" PRIu64, ols->limit_samples);
		ret = SR_OK;
		break;
	case SR_HWCAP_CAPTURE_RATIO:
		tmp_u64 = value;
		ols->capture_ratio = *tmp_u64;
		if (ols->capture_ratio < 0 || ols->capture_ratio > 100) {
			ols->capture_ratio = 0;
			ret = SR_ERR;
		} else
			ret = SR_OK;
		break;
	default:
		ret = SR_ERR;
	}

	return ret;
}

static int receive_data(int fd, int revents, void *user_data)
{
	struct sr_datafeed_packet packet;
	struct sr_device_instance *sdi;
	struct ols_device *ols;
	GSList *l;
	int count, buflen, num_channels, offset, i, j;
	unsigned char byte, *buffer;

	/* find this device's ols_device struct by its fd */
	ols = NULL;
	for (l = device_instances; l; l = l->next) {
		sdi = l->data;
		if (sdi->serial->fd == fd) {
			ols = sdi->priv;
			break;
		}
	}
	if (!ols)
		/* shouldn't happen */
		return TRUE;

	if (ols->num_transfers++ == 0) {
		/*
		 * First time round, means the device started sending data,
		 * and will not stop until done. If it stops sending for
		 * longer than it takes to send a byte, that means it's
		 * finished. We'll double that to 30ms to be sure...
		 */
		sr_source_remove(fd);
		sr_source_add(fd, G_IO_IN, 30, receive_data, user_data);
		ols->raw_sample_buf = malloc(ols->limit_samples * 4);
		/* fill with 1010... for debugging */
		memset(ols->raw_sample_buf, 0x82, ols->limit_samples * 4);
	}

	num_channels = 0;
	for (i = 0x20; i > 0x02; i /= 2) {
		if ((ols->flag_reg & i) == 0)
			num_channels++;
	}

	if (revents == G_IO_IN
	    && ols->num_transfers / num_channels <= ols->limit_samples) {
		if (serial_read(fd, &byte, 1) != 1)
			return FALSE;

		ols->sample[ols->num_bytes++] = byte;
		g_debug("ols: received byte 0x%.2x", byte);
		if (ols->num_bytes == num_channels) {
			/* Got a full sample. */
			g_debug("ols: received sample 0x%.*x", ols->num_bytes * 2, (int) *ols->sample);
			if (ols->flag_reg & FLAG_RLE) {
				/*
				 * In RLE mode -1 should never come in as a
				 * sample, because bit 31 is the "count" flag.
				 * TODO: Endianness may be wrong here, could be
				 * sample[3].
				 */
				if (ols->sample[0] & 0x80
				    && !(ols->last_sample[0] & 0x80)) {
					count = (int)(*ols->sample) & 0x7fffffff;
					buffer = g_malloc(count);
					buflen = 0;
					for (i = 0; i < count; i++) {
						memcpy(buffer + buflen, ols->last_sample, 4);
						buflen += 4;
					}
				} else {
					/*
					 * Just a single sample, next sample
					 * will probably be a count referring
					 * to this -- but this one is still a
					 * part of the stream.
					 */
					buffer = ols->sample;
					buflen = 4;
				}
			} else {
				/* No compression. */
				buffer = ols->sample;
				buflen = 4;
			}

			if (num_channels < 4) {
				/*
				 * Some channel groups may have been turned
				 * off, to speed up transfer between the
				 * hardware and the PC. Expand that here before
				 * submitting it over the session bus --
				 * whatever is listening on the bus will be
				 * expecting a full 32-bit sample, based on
				 * the number of probes.
				 */
				j = 0;
				memset(ols->tmp_sample, 0, 4);
				for (i = 0; i < 4; i++) {
					if (((ols->flag_reg >> 2) & (1 << i)) == 0) {
						/*
						 * This channel group was
						 * enabled, copy from received
						 * sample.
						 */
						ols->tmp_sample[i] = ols->sample[j++];
					}
				}
				memcpy(ols->sample, ols->tmp_sample, 4);
				g_debug("ols: full sample 0x%.8x", (int) *ols->sample);
			}

			/* the OLS sends its sample buffer backwards.
			 * store it in reverse order here, so we can dump
			 * this on the session bus later.
			 */
			offset = (ols->limit_samples - ols->num_transfers / num_channels) * 4;
			memcpy(ols->raw_sample_buf + offset, ols->sample, 4);

			if (buffer == ols->sample)
				memcpy(ols->last_sample, buffer, num_channels);
			else
				g_free(buffer);

			memset(ols->sample, 0, 4);
			ols->num_bytes = 0;
		}
	} else {
		/*
		 * This is the main loop telling us a timeout was reached, or
		 * we've acquired all the samples we asked for -- we're done.
		 * Send the (properly-ordered) buffer to the frontend.
		 */
		if (ols->trigger_at != -1) {
			/* a trigger was set up, so we need to tell the frontend
			 * about it.
			 */
			if (ols->trigger_at > 0) {
				/* there are pre-trigger samples, send those first */
				packet.type = SR_DF_LOGIC;
				packet.length = ols->trigger_at * 4;
				packet.unitsize = 4;
				packet.payload = ols->raw_sample_buf;
				sr_session_bus(user_data, &packet);
			}

			packet.type = SR_DF_TRIGGER;
			packet.length = 0;
			sr_session_bus(user_data, &packet);

			packet.type = SR_DF_LOGIC;
			packet.length = (ols->limit_samples * 4) - (ols->trigger_at * 4);
			packet.unitsize = 4;
			packet.payload = ols->raw_sample_buf + ols->trigger_at * 4;
			sr_session_bus(user_data, &packet);
		} else {
			packet.type = SR_DF_LOGIC;
			packet.length = ols->limit_samples * 4;
			packet.unitsize = 4;
			packet.payload = ols->raw_sample_buf;
			sr_session_bus(user_data, &packet);
		}
		free(ols->raw_sample_buf);

		serial_flush(fd);
		serial_close(fd);
		packet.type = SR_DF_END;
		packet.length = 0;
		sr_session_bus(user_data, &packet);
	}

	return TRUE;
}

static int hw_start_acquisition(int device_index, gpointer session_device_id)
{
	struct sr_datafeed_packet *packet;
	struct sr_datafeed_header *header;
	struct sr_device_instance *sdi;
	struct ols_device *ols;
	uint32_t trigger_config[4];
	uint32_t data;
	uint16_t readcount, delaycount;
	uint8_t changrp_mask;
	int i;

	if (!(sdi = sr_get_device_instance(device_instances, device_index)))
		return SR_ERR;
	ols = sdi->priv;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR;

	readcount = ols->limit_samples / 4;

	memset(trigger_config, 0, 16);
	trigger_config[ols->num_stages - 1] |= 0x08;
	if (ols->trigger_mask[0]) {
		delaycount = readcount * (1 - ols->capture_ratio / 100.0);
		ols->trigger_at = (readcount - delaycount) * 4 - ols->num_stages;

		if (send_longcommand(sdi->serial->fd, CMD_SET_TRIGGER_MASK_0,
			reverse32(ols->trigger_mask[0])) != SR_OK)
			return SR_ERR;
		if (send_longcommand(sdi->serial->fd, CMD_SET_TRIGGER_VALUE_0,
			reverse32(ols->trigger_value[0])) != SR_OK)
			return SR_ERR;
		if (send_longcommand(sdi->serial->fd, CMD_SET_TRIGGER_CONFIG_0,
			trigger_config[0]) != SR_OK)
			return SR_ERR;

		if (send_longcommand(sdi->serial->fd, CMD_SET_TRIGGER_MASK_1,
			reverse32(ols->trigger_mask[1])) != SR_OK)
			return SR_ERR;
		if (send_longcommand(sdi->serial->fd, CMD_SET_TRIGGER_VALUE_1,
			reverse32(ols->trigger_value[1])) != SR_OK)
			return SR_ERR;
		if (send_longcommand(sdi->serial->fd, CMD_SET_TRIGGER_CONFIG_1,
			trigger_config[1]) != SR_OK)
			return SR_ERR;

		if (send_longcommand(sdi->serial->fd, CMD_SET_TRIGGER_MASK_2,
			reverse32(ols->trigger_mask[2])) != SR_OK)
			return SR_ERR;
		if (send_longcommand(sdi->serial->fd, CMD_SET_TRIGGER_VALUE_2,
			reverse32(ols->trigger_value[2])) != SR_OK)
			return SR_ERR;
		if (send_longcommand(sdi->serial->fd, CMD_SET_TRIGGER_CONFIG_2,
			trigger_config[2]) != SR_OK)
			return SR_ERR;

		if (send_longcommand(sdi->serial->fd, CMD_SET_TRIGGER_MASK_3,
			reverse32(ols->trigger_mask[3])) != SR_OK)
			return SR_ERR;
		if (send_longcommand(sdi->serial->fd, CMD_SET_TRIGGER_VALUE_3,
			reverse32(ols->trigger_value[3])) != SR_OK)
			return SR_ERR;
		if (send_longcommand(sdi->serial->fd, CMD_SET_TRIGGER_CONFIG_3,
			trigger_config[3]) != SR_OK)
			return SR_ERR;
	} else {
		if (send_longcommand(sdi->serial->fd, CMD_SET_TRIGGER_MASK_0,
				ols->trigger_mask[0]) != SR_OK)
			return SR_ERR;
		if (send_longcommand(sdi->serial->fd, CMD_SET_TRIGGER_VALUE_0,
				ols->trigger_value[0]) != SR_OK)
			return SR_ERR;
		if (send_longcommand(sdi->serial->fd, CMD_SET_TRIGGER_CONFIG_0,
		     0x00000008) != SR_OK)
			return SR_ERR;
		delaycount = readcount;
	}

	g_message("ols: setting samplerate to %" PRIu64 " Hz (divider %u, demux %s)",
			ols->cur_samplerate, ols->cur_samplerate_divider,
			ols->flag_reg & FLAG_DEMUX ? "on" : "off");
	if (send_longcommand(sdi->serial->fd, CMD_SET_DIVIDER,
			reverse32(ols->cur_samplerate_divider)) != SR_OK)
		return SR_ERR;

	/* Send sample limit and pre/post-trigger capture ratio. */
	data = ((readcount - 1) & 0xffff) << 16;
	data |= (delaycount - 1) & 0xffff;
	if (send_longcommand(sdi->serial->fd, CMD_CAPTURE_SIZE, reverse16(data)) != SR_OK)
		return SR_ERR;

	/*
	 * Enable/disable channel groups in the flag register according to the
	 * probe mask.
	 */
	changrp_mask = 0;
	for (i = 0; i < 4; i++) {
		if (ols->probe_mask & (0xff << (i * 8)))
			changrp_mask |= (1 << i);
	}

	/* The flag register wants them here, and 1 means "disable channel". */
	ols->flag_reg |= ~(changrp_mask << 2) & 0x3c;
	ols->flag_reg |= FLAG_FILTER;
	data = ols->flag_reg << 24;
	if (send_longcommand(sdi->serial->fd, CMD_SET_FLAGS, data) != SR_OK)
		return SR_ERR;

	/* Start acquisition on the device. */
	if (send_shortcommand(sdi->serial->fd, CMD_RUN) != SR_OK)
		return SR_ERR;

	sr_source_add(sdi->serial->fd, G_IO_IN, -1, receive_data,
		      session_device_id);

	/* Send header packet to the session bus. */
	packet = g_malloc(sizeof(struct sr_datafeed_packet));
	header = g_malloc(sizeof(struct sr_datafeed_header));
	if (!packet || !header)
		return SR_ERR;
	packet->type = SR_DF_HEADER;
	packet->length = sizeof(struct sr_datafeed_header);
	packet->payload = (unsigned char *)header;
	header->feed_version = 1;
	gettimeofday(&header->starttime, NULL);
	header->samplerate = ols->cur_samplerate;
	header->protocol_id = SR_PROTO_RAW;
	header->num_logic_probes = NUM_PROBES;
	header->num_analog_probes = 0;
	sr_session_bus(session_device_id, packet);
	g_free(header);
	g_free(packet);

	return SR_OK;
}

static void hw_stop_acquisition(int device_index, gpointer session_device_id)
{
	struct sr_datafeed_packet packet;

	/* Avoid compiler warnings. */
	device_index = device_index;

	packet.type = SR_DF_END;
	packet.length = 0;
	sr_session_bus(session_device_id, &packet);
}

struct sr_device_plugin ols_plugin_info = {
	"ols",
	"Openbench Logic Sniffer",
	1,
	hw_init,
	hw_cleanup,
	hw_opendev,
	hw_closedev,
	hw_get_device_info,
	hw_get_status,
	hw_get_capabilities,
	hw_set_configuration,
	hw_start_acquisition,
	hw_stop_acquisition,
};
