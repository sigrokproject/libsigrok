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

/* Message logging helpers with driver-specific prefix string. */
#define DRIVER_LOG_DOMAIN "serial-dmm: "
#define sr_log(l, s, args...) sr_log(l, DRIVER_LOG_DOMAIN s, ## args)
#define sr_spew(s, args...) sr_spew(DRIVER_LOG_DOMAIN s, ## args)
#define sr_dbg(s, args...) sr_dbg(DRIVER_LOG_DOMAIN s, ## args)
#define sr_info(s, args...) sr_info(DRIVER_LOG_DOMAIN s, ## args)
#define sr_warn(s, args...) sr_warn(DRIVER_LOG_DOMAIN s, ## args)
#define sr_err(s, args...) sr_err(DRIVER_LOG_DOMAIN s, ## args)

/* Note: When adding entries here, don't forget to update DMM_COUNT. */
enum {
	DIGITEK_DT4000ZC,
	TEKPOWER_TP4000ZC,
	METEX_ME31,
	PEAKTECH_3410,
	MASTECH_MAS345,
	VA_VA18B,
	METEX_M3640D,
	PEAKTECH_4370,
	PCE_PCE_DM32,
	RADIOSHACK_22_168,
	RADIOSHACK_22_805,
	RADIOSHACK_22_812,
	TECPEL_DMM_8061_SER,
	VOLTCRAFT_VC820_SER,
	VOLTCRAFT_VC840_SER,
	UNI_T_UT61D_SER,
	UNI_T_UT61E_SER,
};

#define DMM_COUNT 17

struct dmm_info {
	char *vendor;
	char *device;
	char *conn;
	uint32_t baudrate;
	int packet_size;
	int (*packet_request)(struct sr_serial_dev_inst *);
	gboolean (*packet_valid)(const uint8_t *);
	int (*packet_parse)(const uint8_t *, float *,
			    struct sr_datafeed_analog *, void *);
	void (*dmm_details)(struct sr_datafeed_analog *, void *);
	struct sr_dev_driver *di;
	int (*receive_data)(int, int, void *);
};

extern SR_PRIV struct dmm_info dmms[DMM_COUNT];

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

	int64_t starttime;

	uint8_t buf[DMM_BUFSIZE];
	int bufoffset;
	int buflen;
};

SR_PRIV int receive_data_DIGITEK_DT4000ZC(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_TEKPOWER_TP4000ZC(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_METEX_ME31(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_PEAKTECH_3410(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_MASTECH_MAS345(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_VA_VA18B(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_METEX_M3640D(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_PEAKTECH_4370(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_PCE_PCE_DM32(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_RADIOSHACK_22_168(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_RADIOSHACK_22_805(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_RADIOSHACK_22_812(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_TECPEL_DMM_8061_SER(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_VOLTCRAFT_VC820_SER(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_VOLTCRAFT_VC840_SER(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_UNI_T_UT61D_SER(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_UNI_T_UT61E_SER(int fd, int revents, void *cb_data);

SR_PRIV void dmm_details_tp4000zc(struct sr_datafeed_analog *analog, void *info);
SR_PRIV void dmm_details_dt4000zc(struct sr_datafeed_analog *analog, void *info);
SR_PRIV void dmm_details_va18b(struct sr_datafeed_analog *analog, void *info);
SR_PRIV void dmm_details_pce_dm32(struct sr_datafeed_analog *analog, void *info);

#endif
