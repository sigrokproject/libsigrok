/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2024 libsigrok contributors
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

/**
 * @file protocol.c
 *
 * Low-level TCP communication and binary frame decoding for the
 * Hantek DSO2xxx quick-fetch driver.
 *
 * Protocol overview
 * -----------------
 * The patched scope firmware (LD_PRELOAD'd quick-fetch.so) listens on a
 * TCP port.  When the user presses SAVE TO USB the scope transmits a single
 * binary blob per trigger event:
 *
 *   1. struct hantek_frame_header  (32 bytes, all fields little-endian)
 *   2. CH1 sample data             (num_samples × int8_t, if ch1_enabled)
 *   3. CH2 sample data             (num_samples × int8_t, if ch2_enabled)
 *
 * The connection stays open between frames; we simply block-read the next
 * header to detect the next waveform.
 */

#include "protocol.h"

#include <config.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <locale.h>
#include <netinet/tcp.h>
#include <glib.h>
#include <stdlib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"


locale_t hantek_dso2xxx_c_locale;


/**
 * Perform a full blocking read of exactly @p len bytes into @p buf.
 *
 * Returns SR_OK on success, SR_ERR_IO on any error or EOF.
 */
static int tcp_read_all(int fd, void *buf, size_t len)
{
	uint8_t *ptr = (uint8_t *)buf;
	size_t   remaining = len;
	ssize_t  n;

	while (remaining > 0) {
		n = read(fd, ptr, remaining);
		if (n <= 0) {
			if (n < 0 && (errno == EINTR || errno == EAGAIN))
				continue;
			sr_err("TCP read error: %s (wanted %zu, got %zd)",
			       n < 0 ? strerror(errno) : "EOF",
			       len, (ssize_t)(len - remaining));
			return SR_ERR_IO;
		}
		ptr       += n;
		remaining -= (size_t)n;
	}
	return SR_OK;
}

SR_PRIV void send_session_update(const struct sr_dev_inst *sdi,
		int what, GVariant *value)
{
    struct sr_datafeed_packet packet;
    struct sr_datafeed_meta   meta;
    struct sr_config         *cfg;

    cfg = sr_config_new(what, value);

    meta.config = g_slist_append(NULL, cfg);

    packet.type    = SR_DF_META;
    packet.payload = &meta;

    sr_session_send(sdi, &packet);

    g_slist_free(meta.config);
    sr_config_free(cfg);
}

/**
 * Open a TCP connection to the scope.
 *
 * Populates devc->sockfd on success.
 */
SR_PRIV int hantek_dso2xxx_tcp_connect(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	struct addrinfo     hints, *res, *rp;
	int                 fd, rc, one = 1;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family   = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	rc = getaddrinfo(devc->tcp_address, devc->tcp_port, &hints, &res);
	if (rc != 0) {
		sr_err("getaddrinfo(%s:%s): %s",
		       devc->tcp_address, devc->tcp_port, gai_strerror(rc));
		return SR_ERR;
	}

	fd = -1;
	for (rp = res; rp; rp = rp->ai_next) {
		fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (fd < 0)
			continue;
		if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0)
			break;
		close(fd);
		fd = -1;
	}
	freeaddrinfo(res);

	if (fd < 0) {
		sr_err("Could not connect to %s:%s",
		       devc->tcp_address, devc->tcp_port);
		return SR_ERR;
	}

	/* Disable Nagle for lower latency on the control path. */
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

	devc->sockfd = fd;
	sr_info("Connected to Hantek DSO2xxx at %s:%s",
	        devc->tcp_address, devc->tcp_port);
	return SR_OK;
}

SR_PRIV int hantek_dso2xxx_timeout(struct dev_context *devc)
{
	fd_set fds = { 0 };
	struct timeval timeout = { .tv_sec = 1, .tv_usec = 0 }; /* TODO: configurable? */

	FD_SET(devc->sockfd, &fds);
	if (select(devc->sockfd+1, &fds, NULL, NULL, &timeout) == 1)
		return SR_OK;
	return SR_ERR;
}

SR_PRIV int hantek_dso2xxx_acq_now(struct dev_context *devc)
{
	const char now[] = "now\r\n";
	const int len = sizeof(now) -1;

	if (write(devc->sockfd, now, len) != len)
		return SR_ERR;
	return SR_OK;
}

/* Do a time sync */
SR_PRIV int hantek_dso2xxx_timesync(struct dev_context *devc)
{
	const char thx[] = "thx\r\n";
	char buffer[32];
	ssize_t len;

	len = sprintf(buffer, "time %ld\n", time(NULL));
	if (write(devc->sockfd, buffer, len) != len)
		return SR_ERR;
	
	if (hantek_dso2xxx_timeout(devc) != SR_OK)
		return SR_ERR;

	len = read(devc->sockfd, buffer, sizeof(buffer)-1);
	/* Don't check ==, there may be an additional NUL byte */
	if (len < (long)sizeof(thx) -1)
		return SR_ERR;

	return (strncmp(buffer, thx, len) == 0) ? SR_OK : SR_ERR_DATA;
}

/**
 * Close the TCP connection.
 */
SR_PRIV void hantek_dso2xxx_tcp_close(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	if (devc->sockfd >= 0) {
		close(devc->sockfd);
		devc->sockfd = -1;
		sr_dbg("TCP connection closed.");
	}
}


SR_PRIV double atof_for_C_locale(const char *input)
{
	return strtod_l(input, NULL, hantek_dso2xxx_c_locale);
}

/* Writes outside the character vector, but not outside the buffer */
#define terminate_and_conv(field, fn) ( field[ sizeof(field) ] = 0, fn(field) )
#define terminate_and_atoi(field) (terminate_and_conv(field, atoi))

/**
 * Read one complete waveform frame from the scope and dispatch
 * SR_DF_ANALOG packets for each enabled channel.
 *
 * This function blocks until the full frame has been received.
 *
 * @return SR_OK on success, SR_ERR* on failure.
 */
SR_PRIV int hantek_dso2xxx_receive_frame(const struct sr_dev_inst *sdi)
{
	struct dev_context      *devc     = sdi->priv;
	struct hantek_frame_header hdr;
	int8_t byte_buffer[HANTEK_BLOCK_SIZE];
	float                   *float_buf[HANTEK_CHANNELS] = {};
	int                      ret      = SR_ERR;
	size_t                   ch_bytes;
	uint32_t channels, sample_nr, ch;
	int i, pos;
	struct sr_channel *channel;
	struct sr_datafeed_packet  pkt;
	struct sr_datafeed_analog  analog;
	struct sr_analog_encoding  enc;
	struct sr_analog_meaning   meaning;
	struct sr_analog_spec      spec;
	GSList *chl = NULL;

	/* ------------------------------------------------------------------ *
	 * 1.  Read and validate the frame header.                             *
	 * ------------------------------------------------------------------ */
	if (tcp_read_all(devc->sockfd, &hdr, sizeof(hdr)) != SR_OK) {
		sr_err(LOG_PREFIX "Failed to read frame header.");
		return SR_ERR_IO;
	}

	if (hdr.magic[0] != '#' ||
			hdr.magic[1] != '9') {
		sr_err(LOG_PREFIX "Bad frame magic: 0x%02X 0x%02X",
		       hdr.magic[0], hdr.magic[1]);
		return SR_ERR_DATA;
	}

	channels = 0;

	/* Needs to be in last-to-first order as atoi needs NUL-terminated input!
	 * (strtol does, too, and sscanf with field lengths tries locales and
	 * other stuff we don't want.) */

	devc->sample_rate = terminate_and_conv(hdr.sample_rate, atof_for_C_locale);
	devc->sample_period = 1.0/devc->sample_rate;

	send_session_update(sdi,
			SR_CONF_SAMPLERATE,
			g_variant_new_uint64(devc->sample_rate));

	for(i = HANTEK_CHANNELS-1; i>= 0; i--) {
		devc->ch_enabled[i] = hdr.ch_enable[i] == '1';
		if (devc->ch_enabled[i])
			channels++;
	}

	for(i = HANTEK_CHANNELS-1; i>= 0; i--) {
		devc->ch_scale[i] = terminate_and_conv( hdr.ch_voltage[i], atof_for_C_locale);
	}
	for(i = HANTEK_CHANNELS-1; i>= 0; i--) {
		devc->ch_offset[i] = hdr.ch_offset[i][0] | ( hdr.ch_offset[i][1] << 8);
	}

	devc->num_samples = terminate_and_atoi(hdr.total_length) / channels;

	sr_dbg(LOG_PREFIX "Frame: %u samples, rate=%.3e s, CH1=%s/%.1f%+d CH2=%s/%.1f%+d",
	       devc->num_samples,
	       (float)devc->sample_rate,
	       devc->ch_enabled[0] ? "on" : "off",
	       devc->ch_scale[0],
	       devc->ch_offset[0],
	       devc->ch_enabled[1] ? "on" : "off",
	       devc->ch_scale[1],
	       devc->ch_offset[1]);

	for(i = 0; i < HANTEK_CHANNELS; i++) {
		if (devc->ch_enabled[i]) {
			float_buf[i] = g_malloc( devc->num_samples * sizeof(float));
			if (!float_buf[i]) {
				sr_err("Out of memory allocating sample buffer");
				return SR_ERR_MALLOC;
			}
		}
	}

	ch_bytes = HANTEK_BLOCK_SIZE / channels;

	for(sample_nr = 0;
			sample_nr < devc->num_samples;
			sample_nr += ch_bytes) {

		if (hantek_dso2xxx_timeout(devc) != SR_OK)
			return SR_ERR;

		if (tcp_read_all(devc->sockfd, byte_buffer, HANTEK_BLOCK_SIZE) != SR_OK) {
			sr_err(LOG_PREFIX "Failed to read samples.");
			return SR_ERR_IO;
		}

		pos = 0;
		for(ch = 0; ch < HANTEK_CHANNELS; ch++) {
			if (devc->ch_enabled[ch]) {
				for(i = 0; i < ch_bytes; i++) {
					float_buf[ch][sample_nr + i] =
						(byte_buffer[pos] - devc->ch_offset[ch])
						* devc->ch_scale[ch] 
						/ HANTEK_COUNTS_PER_DIV;
					pos++;
				}
			}
		}
	}

	/* ------------------------------------------------------------------ *
	 * 3.  Send SR_DF_FRAME_BEGIN                                          *
	 * ------------------------------------------------------------------ */
	std_session_send_df_frame_begin(sdi);

	/* ------------------------------------------------------------------ *
	 * 4.  Process each enabled channel.                                   *
	 * ------------------------------------------------------------------ */

	for(ch = 0; ch < HANTEK_CHANNELS; ch++) {
		if (devc->ch_enabled[ch]) {
			/* Locate the sr_channel object for this channel index. */
			channel = g_slist_nth_data(sdi->channels, ch);
			if (!channel || !channel->enabled) {
				sr_dbg(LOG_PREFIX " not enabled in session, skipping.");
				continue;
			}

			chl = g_slist_append(NULL, channel);

			sr_analog_init(&analog, &enc, &meaning, &spec, 6);
			meaning.mq      = SR_MQ_VOLTAGE;
			meaning.unit    = SR_UNIT_VOLT;
			meaning.mqflags = SR_MQFLAG_DC;
			meaning.channels = chl;
			analog.data      = float_buf[ch];
			analog.num_samples = devc->num_samples;

			pkt.type    = SR_DF_ANALOG;
			pkt.payload = &analog;
			sr_session_send(sdi, &pkt);
			g_slist_free(chl);
		}
	}

	/* ------------------------------------------------------------------ *
	 * 5.  Send SR_DF_FRAME_END                                            *
	 * ------------------------------------------------------------------ */
	std_session_send_df_frame_end(sdi);

	devc->frames_received++;
	ret = SR_OK;

cleanup:
//	g_free(float_buf);
	return ret;
}
