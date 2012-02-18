/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2010-2012 Bert Vermeulen <bert@biot.com>
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
#include "sigrok.h"
#include "sigrok-internal.h"
#include "ols.h"

#ifdef _WIN32
#define O_NONBLOCK FIONBIO
#endif

static int hwcaps[] = {
	SR_HWCAP_LOGIC_ANALYZER,
	SR_HWCAP_SAMPLERATE,
	SR_HWCAP_CAPTURE_RATIO,
	SR_HWCAP_LIMIT_SAMPLES,
	SR_HWCAP_RLE,
	0,
};

static const char *probe_names[NUM_PROBES + 1] = {
	"0",
	"1",
	"2",
	"3",
	"4",
	"5",
	"6",
	"7",
	"8",
	"9",
	"10",
	"11",
	"12",
	"13",
	"14",
	"15",
	"16",
	"17",
	"18",
	"19",
	"20",
	"21",
	"22",
	"23",
	"24",
	"25",
	"26",
	"27",
	"28",
	"29",
	"30",
	"31",
	NULL,
};

/* default supported samplerates, can be overridden by device metadata */
static struct sr_samplerates samplerates = {
	SR_HZ(10),
	SR_MHZ(200),
	SR_HZ(1),
	NULL,
};

/* List of struct sr_serial_dev_inst */
static GSList *dev_insts = NULL;

static int send_shortcommand(int fd, uint8_t command)
{
	char buf[1];

	sr_dbg("ols: sending cmd 0x%.2x", command);
	buf[0] = command;
	if (serial_write(fd, buf, 1) != 1)
		return SR_ERR;

	return SR_OK;
}

static int send_longcommand(int fd, uint8_t command, uint32_t data)
{
	char buf[5];

	sr_dbg("ols: sending cmd 0x%.2x data 0x%.8x", command, data);
	buf[0] = command;
	buf[1] = (data & 0xff000000) >> 24;
	buf[2] = (data & 0xff0000) >> 16;
	buf[3] = (data & 0xff00) >> 8;
	buf[4] = data & 0xff;
	if (serial_write(fd, buf, 5) != 5)
		return SR_ERR;

	return SR_OK;
}

static int configure_probes(struct ols_dev *ols, GSList *probes)
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

static struct ols_dev *ols_dev_new(void)
{
	struct ols_dev *ols;

	/* TODO: Is 'ols' ever g_free()'d? */
	if (!(ols = g_try_malloc0(sizeof(struct ols_dev)))) {
		sr_err("ols: %s: ols malloc failed", __func__);
		return NULL;
	}

	ols->trigger_at = -1;
	ols->probe_mask = 0xffffffff;
	ols->cur_samplerate = SR_KHZ(200);
	ols->serial = NULL;

	return ols;
}

static struct sr_dev_inst *get_metadata(int fd)
{
	struct sr_dev_inst *sdi;
	struct ols_dev *ols;
	uint32_t tmp_int;
	uint8_t key, type, token;
	GString *tmp_str, *devname, *version;
	gchar tmp_c;

	sdi = sr_dev_inst_new(0, SR_ST_INACTIVE, NULL, NULL, NULL);
	ols = ols_dev_new();
	sdi->priv = ols;

	devname = g_string_new("");
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
			sr_dbg("ols: got metadata key 0x%.2x value '%s'",
			       key, tmp_str->str);
			switch (token) {
			case 0x01:
				/* Device name */
				devname = g_string_append(devname, tmp_str->str);
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
				sr_info("ols: unknown token 0x%.2x: '%s'",
					token, tmp_str->str);
				break;
			}
			g_string_free(tmp_str, TRUE);
			break;
		case 1:
			/* 32-bit unsigned integer */
			if (serial_read(fd, &tmp_int, 4) != 4)
				break;
			tmp_int = reverse32(tmp_int);
			sr_dbg("ols: got metadata key 0x%.2x value 0x%.8x",
			       key, tmp_int);
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
				sr_info("ols: unknown token 0x%.2x: 0x%.8x",
					token, tmp_int);
				break;
			}
			break;
		case 2:
			/* 8-bit unsigned integer */
			if (serial_read(fd, &tmp_c, 1) != 1)
				break;
			sr_dbg("ols: got metadata key 0x%.2x value 0x%.2x",
			       key, tmp_c);
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
				sr_info("ols: unknown token 0x%.2x: 0x%.2x",
					token, tmp_c);
				break;
			}
			break;
		default:
			/* unknown type */
			break;
		}
	}

	sdi->model = devname->str;
	sdi->version = version->str;
	g_string_free(devname, FALSE);
	g_string_free(version, FALSE);

	return sdi;
}

static int hw_init(const char *devinfo)
{
	struct sr_dev_inst *sdi;
	struct ols_dev *ols;
	GSList *ports, *l;
	GPollFD *fds, probefd;
	int devcnt, final_devcnt, num_ports, fd, ret, i;
	char buf[8], **dev_names, **serial_params;

	final_devcnt = 0;

	if (devinfo)
		ports = g_slist_append(NULL, g_strdup(devinfo));
	else
		/* No specific device given, so scan all serial ports. */
		ports = list_serial_ports();

	num_ports = g_slist_length(ports);

	if (!(fds = g_try_malloc0(num_ports * sizeof(GPollFD)))) {
		sr_err("ols: %s: fds malloc failed", __func__);
		goto hw_init_free_ports; /* TODO: SR_ERR_MALLOC. */
	}

	if (!(dev_names = g_try_malloc(num_ports * sizeof(char *)))) {
		sr_err("ols: %s: dev_names malloc failed", __func__);
		goto hw_init_free_fds; /* TODO: SR_ERR_MALLOC. */
	}

	if (!(serial_params = g_try_malloc(num_ports * sizeof(char *)))) {
		sr_err("ols: %s: serial_params malloc failed", __func__);
		goto hw_init_free_dev_names; /* TODO: SR_ERR_MALLOC. */
	}

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
		sr_info("ols: probing %s...", (char *)l->data);
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
			dev_names[devcnt] = g_strdup(l->data);
			devcnt++;
		}
		g_free(l->data);
	}

	/* 2ms isn't enough for reliable transfer with pl2303, let's try 10 */
	usleep(10000);

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
			sdi = sr_dev_inst_new(final_devcnt, SR_ST_INACTIVE,
					"Sump", "Logic Analyzer", "v1.0");
			ols = ols_dev_new();
			ols->num_probes = 32;
			sdi->priv = ols;
		}
		ols->serial = sr_serial_dev_inst_new(dev_names[i], -1);
		dev_insts = g_slist_append(dev_insts, sdi);
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
		g_free(serial_params[i]);
		g_free(dev_names[i]);
	}

	g_free(serial_params);
hw_init_free_dev_names:
	g_free(dev_names);
hw_init_free_fds:
	g_free(fds);
hw_init_free_ports:
	g_slist_free(ports);

	return final_devcnt;
}

static int hw_dev_open(int dev_index)
{
	struct sr_dev_inst *sdi;
	struct ols_dev *ols;

	if (!(sdi = sr_dev_inst_get(dev_insts, dev_index)))
		return SR_ERR;

	ols = sdi->priv;

	ols->serial->fd = serial_open(ols->serial->port, O_RDWR);
	if (ols->serial->fd == -1)
		return SR_ERR;

	sdi->status = SR_ST_ACTIVE;

	return SR_OK;
}

static int hw_dev_close(int dev_index)
{
	struct sr_dev_inst *sdi;
	struct ols_dev *ols;

	if (!(sdi = sr_dev_inst_get(dev_insts, dev_index))) {
		sr_err("ols: %s: sdi was NULL", __func__);
		return SR_ERR; /* TODO: SR_ERR_ARG? */
	}

	ols = sdi->priv;

	/* TODO */
	if (ols->serial->fd != -1) {
		serial_close(ols->serial->fd);
		ols->serial->fd = -1;
		sdi->status = SR_ST_INACTIVE;
	}

	return SR_OK;
}

static int hw_cleanup(void)
{
	GSList *l;
	struct sr_dev_inst *sdi;
	struct ols_dev *ols;
	int ret = SR_OK;

	/* Properly close and free all devices. */
	for (l = dev_insts; l; l = l->next) {
		if (!(sdi = l->data)) {
			/* Log error, but continue cleaning up the rest. */
			sr_err("ols: %s: sdi was NULL, continuing", __func__);
			ret = SR_ERR_BUG;
			continue;
		}
		if (!(ols = sdi->priv)) {
			/* Log error, but continue cleaning up the rest. */
			sr_err("ols: %s: sdi->priv was NULL, continuing",
			       __func__);
			ret = SR_ERR_BUG;
			continue;
		}
		/* TODO: Check for serial != NULL. */
		if (ols->serial->fd != -1)
			serial_close(ols->serial->fd);
		sr_serial_dev_inst_free(ols->serial);
		sr_dev_inst_free(sdi);
	}
	g_slist_free(dev_insts);
	dev_insts = NULL;

	return ret;
}

static void *hw_dev_info_get(int dev_index, int dev_info_id)
{
	struct sr_dev_inst *sdi;
	struct ols_dev *ols;
	void *info;

	if (!(sdi = sr_dev_inst_get(dev_insts, dev_index)))
		return NULL;
	ols = sdi->priv;

	info = NULL;
	switch (dev_info_id) {
	case SR_DI_INST:
		info = sdi;
		break;
	case SR_DI_NUM_PROBES:
		info = GINT_TO_POINTER(NUM_PROBES);
		break;
	case SR_DI_PROBE_NAMES:
		info = probe_names;
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

static int hw_dev_status_get(int dev_index)
{
	struct sr_dev_inst *sdi;

	if (!(sdi = sr_dev_inst_get(dev_insts, dev_index)))
		return SR_ST_NOT_FOUND;

	return sdi->status;
}

static int *hw_hwcap_get_all(void)
{
	return hwcaps;
}

static int config_set_samplerate(struct sr_dev_inst *sdi, uint64_t samplerate)
{
	struct ols_dev *ols;

	ols = sdi->priv;
	if (ols->max_samplerate) {
		if (samplerate > ols->max_samplerate)
			return SR_ERR_SAMPLERATE;
	} else if (samplerate < samplerates.low || samplerate > samplerates.high)
		return SR_ERR_SAMPLERATE;

	if (samplerate > CLOCK_RATE) {
		ols->flag_reg |= FLAG_DEMUX;
		ols->cur_samplerate_divider = (CLOCK_RATE * 2 / samplerate) - 1;
	} else {
		ols->flag_reg &= ~FLAG_DEMUX;
		ols->cur_samplerate_divider = (CLOCK_RATE / samplerate) - 1;
	}

	/* Calculate actual samplerate used and complain if it is different
	 * from the requested.
	 */
	ols->cur_samplerate = CLOCK_RATE / (ols->cur_samplerate_divider + 1);
	if (ols->flag_reg & FLAG_DEMUX)
		ols->cur_samplerate *= 2;
	if (ols->cur_samplerate != samplerate)
		sr_err("ols: can't match samplerate %" PRIu64 ", using %"
		       PRIu64, samplerate, ols->cur_samplerate);

	return SR_OK;
}

static int hw_config_set(int dev_index, int hwcap, void *value)
{
	struct sr_dev_inst *sdi;
	struct ols_dev *ols;
	int ret;
	uint64_t *tmp_u64;

	if (!(sdi = sr_dev_inst_get(dev_insts, dev_index)))
		return SR_ERR;
	ols = sdi->priv;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR;

	switch (hwcap) {
	case SR_HWCAP_SAMPLERATE:
		tmp_u64 = value;
		ret = config_set_samplerate(sdi, *tmp_u64);
		break;
	case SR_HWCAP_PROBECONFIG:
		ret = configure_probes(ols, (GSList *) value);
		break;
	case SR_HWCAP_LIMIT_SAMPLES:
		tmp_u64 = value;
		if (*tmp_u64 < MIN_NUM_SAMPLES)
			return SR_ERR;
		if (*tmp_u64 > ols->max_samples)
			sr_err("ols: sample limit exceeds hw max");
		ols->limit_samples = *tmp_u64;
		sr_info("ols: sample limit %" PRIu64, ols->limit_samples);
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
	case SR_HWCAP_RLE:
		if (GPOINTER_TO_INT(value)) {
			sr_info("ols: enabling RLE");
			ols->flag_reg |= FLAG_RLE;
		}
		ret = SR_OK;
		break;
	default:
		ret = SR_ERR;
	}

	return ret;
}

static int receive_data(int fd, int revents, void *session_data)
{
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	struct sr_dev_inst *sdi;
	struct ols_dev *ols;
	GSList *l;
	int num_channels, offset, i, j;
	unsigned char byte;

	/* find this device's ols_dev struct by its fd */
	ols = NULL;
	for (l = dev_insts; l; l = l->next) {
		sdi = l->data;
		if (ols->serial->fd == fd) {
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
		sr_source_add(fd, G_IO_IN, 30, receive_data, session_data);
		ols->raw_sample_buf = g_try_malloc(ols->limit_samples * 4);
		if (!ols->raw_sample_buf) {
			sr_err("ols: %s: ols->raw_sample_buf malloc failed",
			       __func__);
			return FALSE;
		}
		/* fill with 1010... for debugging */
		memset(ols->raw_sample_buf, 0x82, ols->limit_samples * 4);
	}

	num_channels = 0;
	for (i = 0x20; i > 0x02; i /= 2) {
		if ((ols->flag_reg & i) == 0)
			num_channels++;
	}

	if (revents == G_IO_IN) {
		if (serial_read(fd, &byte, 1) != 1)
			return FALSE;

		/* Ignore it if we've read enough. */
		if (ols->num_samples >= ols->limit_samples)
			return TRUE;

		ols->sample[ols->num_bytes++] = byte;
		sr_dbg("ols: received byte 0x%.2x", byte);
		if (ols->num_bytes == num_channels) {
			/* Got a full sample. */
			sr_dbg("ols: received sample 0x%.*x",
			       ols->num_bytes * 2, *(int *)ols->sample);
			if (ols->flag_reg & FLAG_RLE) {
				/*
				 * In RLE mode -1 should never come in as a
				 * sample, because bit 31 is the "count" flag.
				 */
				if (ols->sample[ols->num_bytes - 1] & 0x80) {
					ols->sample[ols->num_bytes - 1] &= 0x7f;
					/*
					 * FIXME: This will only work on
					 * little-endian systems.
					 */
					ols->rle_count = *(int *)(ols->sample);
					sr_dbg("ols: RLE count = %d", ols->rle_count);
					ols->num_bytes = 0;
					return TRUE;
				}
			}
			ols->num_samples += ols->rle_count + 1;
			if (ols->num_samples > ols->limit_samples) {
				/* Save us from overrunning the buffer. */
				ols->rle_count -= ols->num_samples - ols->limit_samples;
				ols->num_samples = ols->limit_samples;
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
				sr_dbg("ols: full sample 0x%.8x", *(int *)ols->sample);
			}

			/* the OLS sends its sample buffer backwards.
			 * store it in reverse order here, so we can dump
			 * this on the session bus later.
			 */
			offset = (ols->limit_samples - ols->num_samples) * 4;
			for (i = 0; i <= ols->rle_count; i++) {
				memcpy(ols->raw_sample_buf + offset + (i * 4),
				       ols->sample, 4);
			}
			memset(ols->sample, 0, 4);
			ols->num_bytes = 0;
			ols->rle_count = 0;
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
				packet.payload = &logic;
				logic.length = ols->trigger_at * 4;
				logic.unitsize = 4;
				logic.data = ols->raw_sample_buf +
					(ols->limit_samples - ols->num_samples) * 4;
				sr_session_bus(session_data, &packet);
			}

			/* send the trigger */
			packet.type = SR_DF_TRIGGER;
			sr_session_bus(session_data, &packet);

			/* send post-trigger samples */
			packet.type = SR_DF_LOGIC;
			packet.payload = &logic;
			logic.length = (ols->num_samples * 4) - (ols->trigger_at * 4);
			logic.unitsize = 4;
			logic.data = ols->raw_sample_buf + ols->trigger_at * 4 +
				(ols->limit_samples - ols->num_samples) * 4;
			sr_session_bus(session_data, &packet);
		} else {
			/* no trigger was used */
			packet.type = SR_DF_LOGIC;
			packet.payload = &logic;
			logic.length = ols->num_samples * 4;
			logic.unitsize = 4;
			logic.data = ols->raw_sample_buf +
				(ols->limit_samples - ols->num_samples) * 4;
			sr_session_bus(session_data, &packet);
		}
		g_free(ols->raw_sample_buf);

		serial_flush(fd);
		serial_close(fd);
		packet.type = SR_DF_END;
		sr_session_bus(session_data, &packet);
	}

	return TRUE;
}

static int hw_dev_acquisition_start(int dev_index, gpointer session_data)
{
	struct sr_datafeed_packet *packet;
	struct sr_datafeed_header *header;
	struct sr_dev_inst *sdi;
	struct ols_dev *ols;
	uint32_t trigger_config[4];
	uint32_t data;
	uint16_t readcount, delaycount;
	uint8_t changrp_mask;
	int num_channels;
	int i;

	if (!(sdi = sr_dev_inst_get(dev_insts, dev_index)))
		return SR_ERR;

	ols = sdi->priv;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR;

	/*
	 * Enable/disable channel groups in the flag register according to the
	 * probe mask. Calculate this here, because num_channels is needed
	 * to limit readcount.
	 */
	changrp_mask = 0;
	num_channels = 0;
	for (i = 0; i < 4; i++) {
		if (ols->probe_mask & (0xff << (i * 8))) {
			changrp_mask |= (1 << i);
			num_channels++;
		}
	}

	/*
	 * Limit readcount to prevent reading past the end of the hardware
	 * buffer.
	 */
	readcount = MIN(ols->max_samples / num_channels, ols->limit_samples) / 4;

	memset(trigger_config, 0, 16);
	trigger_config[ols->num_stages - 1] |= 0x08;
	if (ols->trigger_mask[0]) {
		delaycount = readcount * (1 - ols->capture_ratio / 100.0);
		ols->trigger_at = (readcount - delaycount) * 4 - ols->num_stages;

		if (send_longcommand(ols->serial->fd, CMD_SET_TRIGGER_MASK_0,
			reverse32(ols->trigger_mask[0])) != SR_OK)
			return SR_ERR;
		if (send_longcommand(ols->serial->fd, CMD_SET_TRIGGER_VALUE_0,
			reverse32(ols->trigger_value[0])) != SR_OK)
			return SR_ERR;
		if (send_longcommand(ols->serial->fd, CMD_SET_TRIGGER_CONFIG_0,
			trigger_config[0]) != SR_OK)
			return SR_ERR;

		if (send_longcommand(ols->serial->fd, CMD_SET_TRIGGER_MASK_1,
			reverse32(ols->trigger_mask[1])) != SR_OK)
			return SR_ERR;
		if (send_longcommand(ols->serial->fd, CMD_SET_TRIGGER_VALUE_1,
			reverse32(ols->trigger_value[1])) != SR_OK)
			return SR_ERR;
		if (send_longcommand(ols->serial->fd, CMD_SET_TRIGGER_CONFIG_1,
			trigger_config[1]) != SR_OK)
			return SR_ERR;

		if (send_longcommand(ols->serial->fd, CMD_SET_TRIGGER_MASK_2,
			reverse32(ols->trigger_mask[2])) != SR_OK)
			return SR_ERR;
		if (send_longcommand(ols->serial->fd, CMD_SET_TRIGGER_VALUE_2,
			reverse32(ols->trigger_value[2])) != SR_OK)
			return SR_ERR;
		if (send_longcommand(ols->serial->fd, CMD_SET_TRIGGER_CONFIG_2,
			trigger_config[2]) != SR_OK)
			return SR_ERR;

		if (send_longcommand(ols->serial->fd, CMD_SET_TRIGGER_MASK_3,
			reverse32(ols->trigger_mask[3])) != SR_OK)
			return SR_ERR;
		if (send_longcommand(ols->serial->fd, CMD_SET_TRIGGER_VALUE_3,
			reverse32(ols->trigger_value[3])) != SR_OK)
			return SR_ERR;
		if (send_longcommand(ols->serial->fd, CMD_SET_TRIGGER_CONFIG_3,
			trigger_config[3]) != SR_OK)
			return SR_ERR;
	} else {
		if (send_longcommand(ols->serial->fd, CMD_SET_TRIGGER_MASK_0,
				ols->trigger_mask[0]) != SR_OK)
			return SR_ERR;
		if (send_longcommand(ols->serial->fd, CMD_SET_TRIGGER_VALUE_0,
				ols->trigger_value[0]) != SR_OK)
			return SR_ERR;
		if (send_longcommand(ols->serial->fd, CMD_SET_TRIGGER_CONFIG_0,
		     0x00000008) != SR_OK)
			return SR_ERR;
		delaycount = readcount;
	}

	sr_info("ols: setting samplerate to %" PRIu64 " Hz (divider %u, "
		"demux %s)", ols->cur_samplerate, ols->cur_samplerate_divider,
		ols->flag_reg & FLAG_DEMUX ? "on" : "off");
	if (send_longcommand(ols->serial->fd, CMD_SET_DIVIDER,
			reverse32(ols->cur_samplerate_divider)) != SR_OK)
		return SR_ERR;

	/* Send sample limit and pre/post-trigger capture ratio. */
	data = ((readcount - 1) & 0xffff) << 16;
	data |= (delaycount - 1) & 0xffff;
	if (send_longcommand(ols->serial->fd, CMD_CAPTURE_SIZE, reverse16(data)) != SR_OK)
		return SR_ERR;

	/* The flag register wants them here, and 1 means "disable channel". */
	ols->flag_reg |= ~(changrp_mask << 2) & 0x3c;
	ols->flag_reg |= FLAG_FILTER;
	ols->rle_count = 0;
	data = (ols->flag_reg << 24) | ((ols->flag_reg << 8) & 0xff0000);
	if (send_longcommand(ols->serial->fd, CMD_SET_FLAGS, data) != SR_OK)
		return SR_ERR;

	/* Start acquisition on the device. */
	if (send_shortcommand(ols->serial->fd, CMD_RUN) != SR_OK)
		return SR_ERR;

	sr_source_add(ols->serial->fd, G_IO_IN, -1, receive_data,
		      session_data);

	if (!(packet = g_try_malloc(sizeof(struct sr_datafeed_packet)))) {
		sr_err("ols: %s: packet malloc failed", __func__);
		return SR_ERR_MALLOC;
	}

	if (!(header = g_try_malloc(sizeof(struct sr_datafeed_header)))) {
		sr_err("ols: %s: header malloc failed", __func__);
		g_free(packet);
		return SR_ERR_MALLOC;
	}

	/* Send header packet to the session bus. */
	packet->type = SR_DF_HEADER;
	packet->payload = (unsigned char *)header;
	header->feed_version = 1;
	gettimeofday(&header->starttime, NULL);
	header->samplerate = ols->cur_samplerate;
	header->num_logic_probes = NUM_PROBES;
	sr_session_bus(session_data, packet);

	g_free(header);
	g_free(packet);

	return SR_OK;
}

static int hw_dev_acquisition_stop(int dev_index, gpointer session_dev_id)
{
	struct sr_datafeed_packet packet;

	/* Avoid compiler warnings. */
	(void)dev_index;

	packet.type = SR_DF_END;
	sr_session_bus(session_dev_id, &packet);

	return SR_OK;
}

SR_PRIV struct sr_dev_plugin ols_plugin_info = {
	.name = "ols",
	.longname = "Openbench Logic Sniffer",
	.api_version = 1,
	.init = hw_init,
	.cleanup = hw_cleanup,
	.dev_open = hw_dev_open,
	.dev_close = hw_dev_close,
	.dev_info_get = hw_dev_info_get,
	.dev_status_get = hw_dev_status_get,
	.hwcap_get_all = hw_hwcap_get_all,
	.config_set = hw_config_set,
	.dev_acquisition_start = hw_dev_acquisition_start,
	.dev_acquisition_stop = hw_dev_acquisition_stop,
};
