/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2021 Daniel Anselmi <danselmi@gmx.ch>
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
#ifdef _WIN32
#define _WIN32_WINNT 0x0501
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/ioctl.h>
#endif
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include "protocol.h"

#define CMD_RESET  0xEE
#define CMD_ESCAPE 0x55

#define START_COMMAND               0xF0
#define STOP_COMMAND                0xF1
#define RETURN_SIZES_COMMAND        0xF2
#define WRITE_SAMPLES_COMMAND       0xF3
#define SET_NUMBEROFSAMPLES_COMMAND 0xF4
#define RETURN_ISRUNNING_COMMAND    0xF5

#define STR_WAVEFORM_DC        "DC"
#define STR_WAVEFORM_SINE      "Sine"
#define STR_WAVEFORM_RECTANGLE "Rectangle"
#define STR_WAVEFORM_TRIANGLE  "Triangle"
#define STR_WAVEFORM_NOISE     "Noise"
#define STR_WAVEFORM_ARB       "Arb"

static gboolean data_available(struct ipdbg_awg_tcp *tcp)
{
#ifdef _WIN32
	u_long bytes_available;
	if (ioctlsocket(tcp->socket, FIONREAD, &bytes_available) != 0) {
#else
	int bytes_available;
	if (ioctl(tcp->socket, FIONREAD, &bytes_available) < 0) { /* TIOCMGET */
#endif
		sr_err("FIONREAD failed: %s\n", g_strerror(errno));
		return FALSE;
	}
	return (bytes_available > 0);
}

SR_PRIV struct ipdbg_awg_tcp *ipdbg_awg_tcp_new(void)
{
	struct ipdbg_awg_tcp *tcp;

	tcp = g_malloc0(sizeof(struct ipdbg_awg_tcp));
	tcp->address = NULL;
	tcp->port = NULL;
	tcp->socket = -1;

	return tcp;
}

SR_PRIV void ipdbg_awg_tcp_free(struct ipdbg_awg_tcp *tcp)
{
	g_free(tcp->address);
	g_free(tcp->port);
}

SR_PRIV int ipdbg_awg_tcp_open(struct ipdbg_awg_tcp *tcp)
{
	struct addrinfo hints;
	struct addrinfo *results, *res;
	int err;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	err = getaddrinfo(tcp->address, tcp->port, &hints, &results);

	if (err) {
		sr_err("Address lookup failed: %s:%s: %s", tcp->address,
			tcp->port, gai_strerror(err));
		return SR_ERR;
	}

	for (res = results; res; res = res->ai_next) {
		if ((tcp->socket = socket(res->ai_family, res->ai_socktype,
			res->ai_protocol)) < 0)
			continue;
		if (connect(tcp->socket, res->ai_addr, res->ai_addrlen) != 0) {
			close(tcp->socket);
			tcp->socket = -1;
			continue;
		}
		break;
	}

	freeaddrinfo(results);

	if (tcp->socket < 0) {
		sr_err("Failed to connect to %s:%s: %s", tcp->address, tcp->port,
			g_strerror(errno));
		return SR_ERR;
	}

	return SR_OK;
}

SR_PRIV int ipdbg_awg_tcp_close(struct ipdbg_awg_tcp *tcp)
{
	int ret;
	ret = SR_OK;

#ifdef _WIN32
	if (shutdown(tcp->socket, SD_SEND) != SOCKET_ERROR) {
		char recvbuf[16];
		int recvbuflen = 16;
		/* Receive until the peer closes the connection. */
		while (recv(tcp->socket, recvbuf, recvbuflen, 0) > 0);
	}
#endif
	if (close(tcp->socket) < 0)
		ret = SR_ERR;

	tcp->socket = -1;

	return ret;
}

static int ipdbg_awg_tcp_send(
	struct ipdbg_awg_tcp *tcp, const uint8_t *buf, size_t len)
{
	int out;
	out = send(tcp->socket, (const char *)buf, len, 0);

	if (out < 0) {
		sr_err("Send error: %s", g_strerror(errno));
		return SR_ERR;
	}

	if (out < (int)len)
		sr_dbg("Only sent %d/%d bytes of data.", out, (int)len);

	return SR_OK;
}

static int ipdbg_awg_tcp_receive_blocking(struct ipdbg_awg_tcp *tcp,
	uint8_t *buf, int bufsize)
{
	int received;
	int error_count;

	received = 0;
	error_count = 0;

	/* Timeout after 2s of not receiving data. */
	/* Increase timeout in case lab is not just beside the office. */
	while ((received < bufsize) && (error_count < 2000)) {
		int recd = ipdbg_awg_tcp_receive(tcp, buf, bufsize - received);
		if (recd > 0) {
			buf += recd;
			received += recd;
		} else {
			error_count++;
			g_usleep(1000);
		}
	}

	return received;
}

SR_PRIV int ipdbg_awg_tcp_receive(struct ipdbg_awg_tcp *tcp,
	uint8_t *buf, size_t bufsize)
{
	int received;
	received = 0;

	if (data_available(tcp))
		received = recv(tcp->socket, (char *)buf, bufsize, 0);
	if (received < 0) {
		sr_err("Receive error: %s", g_strerror(errno));
		return -1;
	} else
		return received;
}

static int ipdbg_awg_send_escaping(
	struct ipdbg_awg_tcp *tcp, uint8_t *data_to_send, uint32_t length)
{
	uint8_t escape;
	escape = CMD_ESCAPE;

	while (length--) {
		uint8_t payload = *data_to_send++;

		if (payload == CMD_RESET)
			if (ipdbg_awg_tcp_send(tcp, &escape, 1) != SR_OK)
				sr_warn("Couldn't send escape");

		if (payload == CMD_ESCAPE)
			if (ipdbg_awg_tcp_send(tcp, &escape, 1) != SR_OK)
				sr_warn("Couldn't send escape");

		if (ipdbg_awg_tcp_send(tcp, &payload, 1) != SR_OK)
			sr_warn("Couldn't send data");
	}

	return SR_OK;
}

SR_PRIV int ipdbg_awg_send_reset(struct ipdbg_awg_tcp *tcp)
{
	uint8_t buf;
	buf = CMD_RESET;
	if (ipdbg_awg_tcp_send(tcp, &buf, 1) != SR_OK)
		sr_warn("Couldn't send reset");

	return SR_OK;
}

SR_PRIV void ipdbg_awg_abort_acquisition(const struct sr_dev_inst *sdi)
{
	struct ipdbg_awg_tcp *tcp;

	tcp = sdi->conn;

	sr_session_source_remove(sdi->session, tcp->socket);

	std_session_send_df_end(sdi);
}

SR_PRIV void ipdbg_awg_get_addrwidth_and_datawidth(
	struct ipdbg_awg_tcp *tcp, struct dev_context *devc)
{
	uint8_t buf[8];
	uint8_t read_cmd;

	read_cmd = RETURN_SIZES_COMMAND;

	if (ipdbg_awg_tcp_send(tcp, &read_cmd, 1) != SR_OK)
		sr_warn("Can't send read command");

	if (ipdbg_awg_tcp_receive_blocking(tcp, buf, 8) != 8)
		sr_warn("Can't get address and data width from device");

	devc->data_width = buf[0] & 0x000000FF;
	devc->data_width |= (buf[1] << 8) & 0x0000FF00;
	devc->data_width |= (buf[2] << 16) & 0x00FF0000;
	devc->data_width |= (buf[3] << 24) & 0xFF000000;

	devc->addr_width = buf[4] & 0x000000FF;
	devc->addr_width |= (buf[5] << 8) & 0x0000FF00;
	devc->addr_width |= (buf[6] << 16) & 0x00FF0000;
	devc->addr_width |= (buf[7] << 24) & 0xFF000000;

	devc->limit_samples_max = (0x01 << devc->addr_width);

	const uint8_t host_word_size = 8;
	devc->data_width_bytes =
		(devc->data_width + host_word_size - 1) / host_word_size;
	devc->addr_width_bytes =
		(devc->addr_width + host_word_size - 1) / host_word_size;
}

SR_PRIV void ipdbg_awg_get_isrunning(
	struct ipdbg_awg_tcp *tcp, struct dev_context *devc)
{
	uint8_t buf[1];
	uint8_t read_cmd;
	read_cmd = RETURN_ISRUNNING_COMMAND;
	if (ipdbg_awg_tcp_send(tcp, &read_cmd, 1) != SR_OK)
		sr_warn("Can't send read running");

	if (ipdbg_awg_tcp_receive_blocking(tcp, buf, 1) != 1)
		sr_warn("Can't get state from device");

	devc->is_running = buf[0] == 1;
}

static void ipdbg_awg_calculate_dc(struct dev_context *devc)
{
	int64_t *out;
	const int64_t full_scale = (1ll << (devc->data_width-1)) - 1;
	out = devc->wave_buffer;
	// awg is only able to repeat at least 2 samples
	int64_t val = (int64_t)(devc->offset * full_scale);
	*out++ = val;
	*out++ = val;
}

static void ipdbg_awg_limit_waveform(struct dev_context *devc)
{
	int64_t *out;
	const int64_t full_scale = (1ll << (devc->data_width-1)) - 1;
	out = devc->wave_buffer;

	for (size_t i = 0 ; i < devc->limit_samples ; ++i) {
		if (*out > full_scale)
			*out = full_scale;
		else if (*out < -full_scale)
			*out = -full_scale;
		++out;
	}
}

static void ipdbg_awg_calculate_sine(struct dev_context *devc)
{
	int64_t *out;
	const int64_t full_scale = (1ll << (devc->data_width-1)) - 1;
	const double pi = acos(-1.0);
	const double A = devc->amplitude * full_scale;
	const double O = 2.0*pi/(double)(devc->limit_samples);
	const double offset = devc->offset * full_scale;
	out = devc->wave_buffer;
	if (devc->center_freq) {
		const double fs = (double)devc->sample_rate;
		const double f_min = fs/(double)(devc->limit_samples_max);
		const double fc = (double)devc->center_freq;
		const double f = devc->frequency - fc;
		if (f > -f_min/2.0 && f < f_min/2.0) {
			*out++ = (int64_t)round(A*cos(devc->phase));
			*out++ = (int64_t)round(A*sin(devc->phase));
		}
		else {
			for (size_t i = 0 ; i < devc->limit_samples ; i+=2) {
				*out++ = (int64_t)round(A*cos(i*O*devc->periods + devc->phase));
				*out++ = (int64_t)round(A*sin(i*O*devc->periods + devc->phase));
			}
		}
	}
	else {
		for (size_t i = 0 ; i < devc->limit_samples ; ++i)
			*out++ = (int64_t)round(
					A*sin(i*O*devc->periods + devc->phase) + offset);
	}

	ipdbg_awg_limit_waveform(devc);
}

static double ipdbg_awg_triangle(double x, double d)
{
	x = fmod(x, 1.0);
	if (d > 0.0 && 2.0*x < d)
		return 2.0*x/d;
	else if (d < 1.0 && 2.0*(1.0-x) > d)
		return -2.0*(x-0.5)/(1.0-d);
	else if (d > 0.0)
		return 2.0*(x-1.0)/d;

	return 0.0;
}

static void ipdbg_awg_calculate_triangle(struct dev_context *devc)
{
	int64_t *out;
	const double pi = acos(-1.0);
	const double full_scale = (1ll << (devc->data_width-1)) - 1;
	const double A = devc->amplitude * full_scale;
	const double N = 1.0/(double)(devc->limit_samples);
	const double offset = devc->offset * full_scale;
	const size_t phi = devc->phase/pi/2.0;
	out = devc->wave_buffer;

	for (size_t i = 0 ; i < devc->limit_samples; ++i)
		*out++ = (int64_t)round(A*ipdbg_awg_triangle(i*N*devc->periods + phi,
				devc->dutycycle) + offset);

	ipdbg_awg_limit_waveform(devc);
}

static double ipdbg_awg_rectangle(double x, double d)
{
	x = fmod(x, 1.0);
	if (x < d)
		return 1.0;
	else
		return -1.0;
}

static void ipdbg_awg_calculate_rectangle(struct dev_context *devc)
{
	int64_t *out;
	const double pi = acos(-1.0);
	const double full_scale = (1ll << (devc->data_width-1)) - 1;
	const double A = devc->amplitude * full_scale;
	const double N = 1.0/(double)(devc->limit_samples);
	const double offset = devc->offset * full_scale;
	const size_t phi = devc->phase/pi/2.0;
	out = devc->wave_buffer;

	for (size_t i = 0 ; i < devc->limit_samples; ++i)
		*out++ = (int64_t)round(A*ipdbg_awg_rectangle(i*N*devc->periods + phi,
				devc->dutycycle) + offset);

	ipdbg_awg_limit_waveform(devc);
}

static void ipdbg_awg_calculate_waveform(struct dev_context *devc)
{
	int64_t *out;
	if (devc->wave_buffer)
		g_free(devc->wave_buffer);

	devc->wave_buffer = g_malloc(devc->limit_samples * sizeof(int64_t));

	if (!(devc->wave_buffer))
		return;

	switch (devc->waveform) {
	case IPDBG_AWG_WAVEFORM_DC:
		ipdbg_awg_calculate_dc(devc);
		break;
	case IPDBG_AWG_WAVEFORM_SINE:
		ipdbg_awg_calculate_sine(devc);
		break;
	case IPDBG_AWG_WAVEFORM_RECTANGLE:
		ipdbg_awg_calculate_rectangle(devc);
		break;
	case IPDBG_AWG_WAVEFORM_TRIANGLE:
		ipdbg_awg_calculate_triangle(devc);
		break;
	//case IPDBG_AWG_WAVEFORM_NOISE:
	case IPDBG_AWG_WAVEFORM_ARB:
	default:
		out = devc->wave_buffer;
		for (size_t i = 0 ; i < devc->limit_samples ; ++i)
			*out++ = 0ll;
	}
}

static void ipdbg_awg_update_limit_samples(struct dev_context *devc)
{
	size_t N = devc->limit_samples_max;
	const double fs = (double)devc->sample_rate;
	const double f_min = fs/(double)N;
	const double fc = (double)devc->center_freq;

	if (devc->waveform == IPDBG_AWG_WAVEFORM_DC) {
		// ipdbg-awg is only able to repeat at least 2 samples
		devc->limit_samples = 2;
	}
	else if (devc->center_freq) {
		N /= 2;
		size_t p;
		size_t m;
		double f = devc->frequency - fc;
		if (f < -fs/2.0) f = -fs/2.0;
		if (f > fs/2.0) f = fs/2.0;

		if (f > -f_min/2.0 && f < f_min/2.0) {
			f = 0.0;
			m = 2.0;
			p = 1.0;
			//devc->frequency_act = fc;
		}
		else {
			f = fabs(f);
			p = round(f/fs*N);
			m = round(fs/f*p);
			//devc->frequency_act = fs*(double)p/(double)m + fc;
		}
		devc->periods = p;
		devc->limit_samples = m;
	}
	else {
		double f = devc->frequency;
		if (f < f_min)
			f = f_min;
		if (f > fs/2.0)
			f = fs/2.0;

		size_t p = round(f/fs*N);
		size_t m = round(fs/f*p);

		//devc->frequency_act = fs*(double)p/(double)m;

		devc->periods = p;
		devc->limit_samples = m;
	}
}

SR_PRIV void ipdbg_awg_init_waveform(struct dev_context *devc)
{
	ipdbg_awg_update_limit_samples(devc);
	ipdbg_awg_calculate_waveform(devc);
}

SR_PRIV int ipdbg_awg_update_waveform(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct ipdbg_awg_tcp *tcp;
	int ret;
	uint8_t buffer[4];
	int64_t *data_to_send;
	gboolean was_running;

	sr_spew("ipdbg_awg_update_waveform");

	if (!(devc = sdi->priv) || !(tcp = sdi->conn))
		return SR_ERR_BUG;

	was_running = devc->is_running;
	if (devc->is_running) {
		ret = ipdbg_awg_stop(sdi);
		if (ret != SR_OK) {
			sr_err("stopping the generator failed");
			return ret;
		}
	}

	buffer[0] = SET_NUMBEROFSAMPLES_COMMAND;
	ret = ipdbg_awg_tcp_send(tcp, buffer, 1);
	if (ret != SR_OK)
	{
		sr_warn("Can't send num_samples command");
		return ret;
	}

	ret = SR_OK;
	for(size_t i = 0 ; i < devc->addr_width_bytes && ret == SR_OK ; ++i) {
		buffer[0] = (devc->limit_samples-1) >> ((devc->addr_width_bytes-1-i)*8);
		ret = ipdbg_awg_send_escaping(tcp, buffer, 1);
	}
	if (ret != SR_OK)
	{
		sr_warn("Can't send num_samples");
		return ret;
	}

	data_to_send = devc->wave_buffer;

	buffer[0] = WRITE_SAMPLES_COMMAND;
	ret = ipdbg_awg_tcp_send(tcp, buffer, 1);
	if (ret != SR_OK)
	{
		sr_err("Can't send write samples command");
		return ret;
	}

	ret = SR_OK;
	for (unsigned int i = 0; i < devc->limit_samples ; i++)
	{
		int64_t val = data_to_send[i];
		for (size_t k = 0 ; k < devc->data_width_bytes && ret == SR_OK ; ++k) {
			buffer[0] = val >> ((devc->data_width_bytes-1-k)*8);
			ret = ipdbg_awg_send_escaping(tcp, buffer, 1);
		}
	}
	if (ret != SR_OK)
	{
		sr_err("Can't send samples");
		return ret;
	}

	if (was_running) {
		ret = ipdbg_awg_start(sdi);
		if (ret != SR_OK)
		{
			sr_err("starting the generator failed");
			return ret;
		}
	}

	return SR_OK;
}

SR_PRIV int ipdbg_awg_set_frequency(const struct sr_dev_inst *sdi,
	double f_value)
{
	struct dev_context *devc;
	if (!(devc = sdi->priv))
		return SR_ERR_BUG;

	if (devc->frequency == f_value)
		return SR_OK;

	devc->frequency = f_value;

	ipdbg_awg_update_limit_samples(devc);
	ipdbg_awg_calculate_waveform(devc);

	return ipdbg_awg_update_waveform(sdi);
}

SR_PRIV double ipdbg_awg_get_frequency(struct dev_context *devc)
{
	return devc->frequency;
}

SR_PRIV int ipdbg_awg_set_amplitude(const struct sr_dev_inst *sdi,
	double a_value)
{
	struct dev_context *devc;
	if (!(devc = sdi->priv))
		return SR_ERR_BUG;

	if (devc->amplitude == a_value)
		return SR_OK;

	if (a_value > 1.0 || a_value < 0.0)
		return SR_ERR;

	if (devc->amplitude == a_value)
		return SR_OK;

	devc->amplitude = a_value;

	ipdbg_awg_calculate_waveform(devc);

	return ipdbg_awg_update_waveform(sdi);
}

SR_PRIV double ipdbg_awg_get_amplitude(struct dev_context *devc)
{
	return devc->amplitude;
}

SR_PRIV int ipdbg_awg_set_phase(const struct sr_dev_inst *sdi,
	double p_value)
{
	struct dev_context *devc;
	double phase;
	if (!(devc = sdi->priv))
		return SR_ERR_BUG;

	if (devc->phase == p_value)
		return SR_OK;

	if (p_value > 360.0 || p_value < 0.0)
		return SR_ERR;

	phase = p_value/180.0*acos(-1.0);
	if (devc->phase == phase)
		return SR_OK;
	devc->phase = phase;

	ipdbg_awg_calculate_waveform(devc);

	return ipdbg_awg_update_waveform(sdi);
}

SR_PRIV double ipdbg_awg_get_phase(struct dev_context *devc)
{
	return devc->phase*180.0/acos(-1.0);
}

SR_PRIV int ipdbg_awg_set_offset(const struct sr_dev_inst *sdi,
	double o_value)
{
	struct dev_context *devc;
	if (!(devc = sdi->priv))
		return SR_ERR_BUG;

	if (devc->offset == o_value)
		return SR_OK;

	if (o_value > 1.0 || o_value < -1.0)
		return SR_ERR;

	if (devc->offset == o_value)
		return SR_OK;

	devc->offset = o_value;

	ipdbg_awg_calculate_waveform(devc);

	return ipdbg_awg_update_waveform(sdi);
}

SR_PRIV double ipdbg_awg_get_offset(struct dev_context *devc)
{
	return devc->offset;
}

SR_PRIV int ipdbg_awg_set_dutycycle(const struct sr_dev_inst *sdi,
	double d_value)
{
	struct dev_context *devc;
	double dutycycle = d_value/100.0;

	if (!(devc = sdi->priv))
		return SR_ERR_BUG;

	if (devc->dutycycle == dutycycle)
		return SR_OK;

	devc->dutycycle = dutycycle;

	ipdbg_awg_calculate_waveform(devc);

	return ipdbg_awg_update_waveform(sdi);
}

SR_PRIV double ipdbg_awg_get_dutycycle(struct dev_context *devc)
{
	return devc->dutycycle*100.0;
}

SR_PRIV int ipdbg_awg_set_waveform(const struct sr_dev_inst *sdi,
	int wf_value)
{
	struct dev_context *devc;
	if (!(devc = sdi->priv))
		return SR_ERR_BUG;

	if (devc->waveform == wf_value)
		return SR_OK;

	if (devc->center_freq > 0)
		if ((wf_value != IPDBG_AWG_WAVEFORM_SINE) &&
			(wf_value != IPDBG_AWG_WAVEFORM_ARB))
			return SR_ERR_NA;

	devc->waveform = wf_value;

	ipdbg_awg_update_limit_samples(devc);
	ipdbg_awg_calculate_waveform(devc);

	return ipdbg_awg_update_waveform(sdi);
}

SR_PRIV const char *ipdbg_awg_waveform_to_string(int waveform)
{
	switch (waveform) {
		case IPDBG_AWG_WAVEFORM_DC:
			return STR_WAVEFORM_DC;
		case IPDBG_AWG_WAVEFORM_SINE:
			return STR_WAVEFORM_SINE;
		case IPDBG_AWG_WAVEFORM_RECTANGLE:
			return STR_WAVEFORM_RECTANGLE;
		case IPDBG_AWG_WAVEFORM_TRIANGLE:
			return STR_WAVEFORM_TRIANGLE;
		//case IPDBG_AWG_WAVEFORM_NOISE:
			return STR_WAVEFORM_NOISE;
		case IPDBG_AWG_WAVEFORM_ARB:
			return STR_WAVEFORM_ARB;
	}
	return "Unknown";
}

SR_PRIV uint64_t ipdbg_awg_get_sample_rate(struct dev_context *devc)
{
	return devc->sample_rate;
}

SR_PRIV int ipdbg_awg_set_sample_rate(const struct sr_dev_inst *sdi,
	uint64_t rate)
{
	struct dev_context *devc;

	if (!(devc = sdi->priv))
		return SR_ERR_BUG;

	if (devc->sample_rate == rate)
		return SR_OK;

	devc->sample_rate = rate;

	ipdbg_awg_update_limit_samples(devc);
	ipdbg_awg_calculate_waveform(devc);

	return ipdbg_awg_update_waveform(sdi);
}

SR_PRIV int ipdbg_awg_set_center_freq(const struct sr_dev_inst *sdi,
	uint64_t center_freq)
{
	struct dev_context *devc;

	if (!(devc = sdi->priv))
		return SR_ERR_BUG;

	if (devc->center_freq == center_freq)
		return SR_OK;

	if (center_freq > 0)
		if ((devc->waveform != IPDBG_AWG_WAVEFORM_SINE) &&
			(devc->waveform != IPDBG_AWG_WAVEFORM_ARB))
			return SR_ERR_NA;

	devc->center_freq = center_freq;

	ipdbg_awg_update_limit_samples(devc);
	ipdbg_awg_calculate_waveform(devc);

	return ipdbg_awg_update_waveform(sdi);
}

SR_PRIV uint64_t ipdbg_awg_get_center_freq(struct dev_context *devc)
{
	return devc->center_freq;
}

SR_PRIV int ipdbg_awg_set_enable(const struct sr_dev_inst *sdi, int en)
{
	struct dev_context *devc;

	if (!(devc = sdi->priv))
		return SR_ERR_BUG;

	if (devc->is_running != en) {
		devc->is_running = en;
		return en ? ipdbg_awg_start(sdi):
					ipdbg_awg_stop(sdi);
	}
	return SR_OK;
}

SR_PRIV int ipdbg_awg_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct ipdbg_awg_tcp *tcp;
	int ret;

	if (!(devc = sdi->priv) || !(tcp = sdi->conn))
		return SR_ERR_BUG;

	uint8_t buf = START_COMMAND;

	ret = ipdbg_awg_tcp_send(tcp, &buf, 1);
	if (ret != SR_OK)
		sr_warn("Can't send start command");

	devc->is_running = TRUE;

	return ret;
}

SR_PRIV int ipdbg_awg_stop(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct ipdbg_awg_tcp *tcp;
	int ret;

	if (!(devc = sdi->priv) || !(tcp = sdi->conn))
		return SR_ERR_BUG;

	uint8_t buf = STOP_COMMAND;

	ret = ipdbg_awg_tcp_send(tcp, &buf, 1);
	if (ret != SR_OK)
		sr_warn("Can't send stop command");

	devc->is_running = FALSE;

	return ret;
}

