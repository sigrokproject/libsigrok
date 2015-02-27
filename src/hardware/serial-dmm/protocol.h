/*
 * This file is part of the libsigrok project.
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

#ifndef LIBSIGROK_HARDWARE_SERIAL_DMM_PROTOCOL_H
#define LIBSIGROK_HARDWARE_SERIAL_DMM_PROTOCOL_H

#define LOG_PREFIX "serial-dmm"

enum {
	BBCGM_M2110,
	DIGITEK_DT4000ZC,
	TEKPOWER_TP4000ZC,
	METEX_ME31,
	PEAKTECH_3410,
	MASTECH_MAS345,
	MASTECH_MS8250B,
	VA_VA18B,
	VA_VA40B,
	METEX_M3640D,
	METEX_M4650CR,
	PEAKTECH_4370,
	PCE_PCE_DM32,
	RADIOSHACK_22_168,
	RADIOSHACK_22_805,
	RADIOSHACK_22_812,
	TECPEL_DMM_8061_SER,
	VOLTCRAFT_M3650CR,
	VOLTCRAFT_M3650D,
	VOLTCRAFT_M4650CR,
	VOLTCRAFT_ME42,
	VOLTCRAFT_VC820_SER,
	VOLTCRAFT_VC830_SER,
	VOLTCRAFT_VC840_SER,
	VOLTCRAFT_VC920_SER,
	VOLTCRAFT_VC940_SER,
	VOLTCRAFT_VC960_SER,
	UNI_T_UT60A_SER,
	UNI_T_UT60E_SER,
	UNI_T_UT60G_SER,
	UNI_T_UT61B_SER,
	UNI_T_UT61C_SER,
	UNI_T_UT61D_SER,
	UNI_T_UT61E_SER,
	UNI_T_UT71A_SER,
	UNI_T_UT71B_SER,
	UNI_T_UT71C_SER,
	UNI_T_UT71D_SER,
	UNI_T_UT71E_SER,
	ISO_TECH_IDM103N,
	TENMA_72_7745_SER,
	TENMA_72_7750_SER,
	BRYMEN_BM25X,
};

struct dmm_info {
	/** Manufacturer/brand. */
	char *vendor;
	/** Model. */
	char *device;
	/** serialconn string. */
	char *conn;
	/** Baud rate. */
	uint32_t baudrate;
	/** Packet size in bytes. */
	int packet_size;
	/** Request timeout [ms] before request is considered lost and a new
	 *  one is sent. Used only if device needs polling. */
	int64_t req_timeout_ms;
	/** Delay between reception of packet and next request. Some DMMs
	 *  need this. Used only if device needs polling. */
	int64_t req_delay_ms;
	/** Packet request function. */
	int (*packet_request)(struct sr_serial_dev_inst *);
	/** Packet validation function. */
	gboolean (*packet_valid)(const uint8_t *);
	/** Packet parsing function. */
	int (*packet_parse)(const uint8_t *, float *,
			    struct sr_datafeed_analog *, void *);
	/** */
	void (*dmm_details)(struct sr_datafeed_analog *, void *);
	/** libsigrok driver info struct. */
	struct sr_dev_driver *di;
	/** Data reception function. */
	int (*receive_data)(int, int, void *);
};

extern SR_PRIV struct dmm_info dmms[];

#define DMM_BUFSIZE 256

/** Private, per-device-instance driver context. */
struct dev_context {
	/** The current sampling limit (in number of samples). */
	uint64_t limit_samples;

	/** The time limit (in milliseconds). */
	uint64_t limit_msec;

	/** Opaque pointer passed in by the frontend. */
	void *cb_data;

	/** The current number of already received samples. */
	uint64_t num_samples;

	/** The starting time of current sampling run. */
	int64_t starttime;

	uint8_t buf[DMM_BUFSIZE];
	int bufoffset;
	int buflen;

	/** The timestamp [µs] to send the next request.
	 *  Used only if device needs polling. */
	int64_t req_next_at;
};

SR_PRIV int req_packet(struct sr_dev_inst *sdi, int dmm);

SR_PRIV int receive_data_BBCGM_M2110(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_DIGITEK_DT4000ZC(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_TEKPOWER_TP4000ZC(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_METEX_ME31(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_PEAKTECH_3410(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_MASTECH_MAS345(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_MASTECH_MS8250B(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_VA_VA18B(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_VA_VA40B(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_METEX_M3640D(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_METEX_M4650CR(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_PEAKTECH_4370(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_PCE_PCE_DM32(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_RADIOSHACK_22_168(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_RADIOSHACK_22_805(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_RADIOSHACK_22_812(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_TECPEL_DMM_8061_SER(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_VOLTCRAFT_M3650CR(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_VOLTCRAFT_M3650D(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_VOLTCRAFT_M4650CR(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_VOLTCRAFT_ME42(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_VOLTCRAFT_VC820_SER(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_VOLTCRAFT_VC830_SER(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_VOLTCRAFT_VC840_SER(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_VOLTCRAFT_VC920_SER(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_VOLTCRAFT_VC940_SER(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_VOLTCRAFT_VC960_SER(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_UNI_T_UT60A_SER(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_UNI_T_UT60E_SER(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_UNI_T_UT60G_SER(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_UNI_T_UT61B_SER(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_UNI_T_UT61C_SER(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_UNI_T_UT61D_SER(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_UNI_T_UT61E_SER(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_UNI_T_UT71A_SER(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_UNI_T_UT71B_SER(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_UNI_T_UT71C_SER(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_UNI_T_UT71D_SER(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_UNI_T_UT71E_SER(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_ISO_TECH_IDM103N(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_TENMA_72_7745_SER(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_TENMA_72_7750_SER(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_BRYMEN_BM25X(int fd, int revents, void *cb_data);

#endif
