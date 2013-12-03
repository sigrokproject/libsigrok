/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Bert Vermeulen <bert@biot.com>
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

#ifndef LIBSIGROK_SIGROK_INTERNAL_H
#define LIBSIGROK_SIGROK_INTERNAL_H

#include <stdarg.h>
#include <glib.h>
#include "config.h" /* Needed for HAVE_LIBUSB_1_0 and others. */
#ifdef HAVE_LIBUSB_1_0
#include <libusb.h>
#endif
#ifdef HAVE_LIBSERIALPORT
#include <libserialport.h>
#endif

/**
 * @file
 *
 * libsigrok private header file, only to be used internally.
 */

/*--- Macros ----------------------------------------------------------------*/

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

#ifndef ARRAY_AND_SIZE
#define ARRAY_AND_SIZE(a) (a), ARRAY_SIZE(a)
#endif

/* Portability fixes for FreeBSD. */
#ifdef __FreeBSD__
#define LIBUSB_CLASS_APPLICATION 0xfe
#define libusb_handle_events_timeout_completed(ctx, tv, c) \
	libusb_handle_events_timeout(ctx, tv)
#endif

struct sr_context {
#ifdef HAVE_LIBUSB_1_0
	libusb_context *libusb_ctx;
#endif
};

#ifdef HAVE_LIBUSB_1_0
struct sr_usb_dev_inst {
	uint8_t bus;
	uint8_t address;
	struct libusb_device_handle *devhdl;
};
#endif

#ifdef HAVE_LIBSERIALPORT
#define SERIAL_PARITY_NONE SP_PARITY_NONE
#define SERIAL_PARITY_EVEN SP_PARITY_EVEN
#define SERIAL_PARITY_ODD  SP_PARITY_ODD
struct sr_serial_dev_inst {
	char *port;
	char *serialcomm;
	int fd;
	int nonblocking;
	struct sp_port *data;
};
#endif

struct sr_usbtmc_dev_inst {
	char *device;
	int fd;
};

/* Private driver context. */
struct drv_context {
	struct sr_context *sr_ctx;
	GSList *instances;
};

/*--- log.c -----------------------------------------------------------------*/

SR_PRIV int sr_log(int loglevel, const char *format, ...);
SR_PRIV int sr_spew(const char *format, ...);
SR_PRIV int sr_dbg(const char *format, ...);
SR_PRIV int sr_info(const char *format, ...);
SR_PRIV int sr_warn(const char *format, ...);
SR_PRIV int sr_err(const char *format, ...);

/*--- device.c --------------------------------------------------------------*/

SR_PRIV struct sr_probe *sr_probe_new(int index, int type,
		gboolean enabled, const char *name);

/* Generic device instances */
SR_PRIV struct sr_dev_inst *sr_dev_inst_new(int index, int status,
		const char *vendor, const char *model, const char *version);
SR_PRIV void sr_dev_inst_free(struct sr_dev_inst *sdi);

#ifdef HAVE_LIBUSB_1_0
/* USB-specific instances */
SR_PRIV struct sr_usb_dev_inst *sr_usb_dev_inst_new(uint8_t bus,
		uint8_t address, struct libusb_device_handle *hdl);
SR_PRIV GSList *sr_usb_find_usbtmc(libusb_context *usb_ctx);
SR_PRIV void sr_usb_dev_inst_free(struct sr_usb_dev_inst *usb);
#endif

#ifdef HAVE_LIBSERIALPORT
/* Serial-specific instances */
SR_PRIV struct sr_serial_dev_inst *sr_serial_dev_inst_new(const char *port,
		const char *serialcomm);
SR_PRIV void sr_serial_dev_inst_free(struct sr_serial_dev_inst *serial);
#endif

/* USBTMC-specific instances */
SR_PRIV struct sr_usbtmc_dev_inst *sr_usbtmc_dev_inst_new(const char *device);
SR_PRIV void sr_usbtmc_dev_inst_free(struct sr_usbtmc_dev_inst *usbtmc);

/*--- hwdriver.c ------------------------------------------------------------*/

SR_PRIV void sr_hw_cleanup_all(void);
SR_PRIV struct sr_config *sr_config_new(int key, GVariant *data);
SR_PRIV void sr_config_free(struct sr_config *src);
SR_PRIV int sr_source_remove(int fd);
SR_PRIV int sr_source_add(int fd, int events, int timeout,
		sr_receive_data_callback_t cb, void *cb_data);

/*--- session.c -------------------------------------------------------------*/

struct sr_session {
	/** List of struct sr_dev pointers. */
	GSList *devs;
	/** List of struct datafeed_callback pointers. */
	GSList *datafeed_callbacks;
	GTimeVal starttime;
	gboolean running;

	unsigned int num_sources;

	/*
	 * Both "sources" and "pollfds" are of the same size and contain pairs
	 * of descriptor and callback function. We can not embed the GPollFD
	 * into the source struct since we want to be able to pass the array
	 * of all poll descriptors to g_poll().
	 */
	struct source *sources;
	GPollFD *pollfds;
	int source_timeout;

	/*
	 * These are our synchronization primitives for stopping the session in
	 * an async fashion. We need to make sure the session is stopped from
	 * within the session thread itself.
	 */
	GMutex stop_mutex;
	gboolean abort_session;
};

SR_PRIV int sr_session_send(const struct sr_dev_inst *sdi,
		const struct sr_datafeed_packet *packet);
SR_PRIV int sr_session_stop_sync(void);
SR_PRIV int sr_sessionfile_check(const char *filename);

/*--- std.c -----------------------------------------------------------------*/

typedef int (*dev_close_t)(struct sr_dev_inst *sdi);
typedef void (*std_dev_clear_t)(void *priv);

SR_PRIV int std_init(struct sr_context *sr_ctx, struct sr_dev_driver *di,
		const char *prefix);
#ifdef HAVE_LIBSERIALPORT
SR_PRIV int std_dev_acquisition_stop_serial(struct sr_dev_inst *sdi,
		void *cb_data, dev_close_t dev_close_fn,
		struct sr_serial_dev_inst *serial, const char *prefix);
#endif
SR_PRIV int std_session_send_df_header(const struct sr_dev_inst *sdi,
		const char *prefix);
SR_PRIV int std_dev_clear(const struct sr_dev_driver *driver,
		std_dev_clear_t clear_private);

/*--- strutil.c -------------------------------------------------------------*/

SR_PRIV int sr_atol(const char *str, long *ret);
SR_PRIV int sr_atoi(const char *str, int *ret);
SR_PRIV int sr_atod(const char *str, double *ret);
SR_PRIV int sr_atof(const char *str, float *ret);

/*--- hardware/common/serial.c ----------------------------------------------*/

#ifdef HAVE_LIBSERIALPORT
enum {
	SERIAL_RDWR = 1,
	SERIAL_RDONLY = 2,
	SERIAL_NONBLOCK = 4,
};

typedef gboolean (*packet_valid_t)(const uint8_t *buf);

SR_PRIV int serial_open(struct sr_serial_dev_inst *serial, int flags);
SR_PRIV int serial_close(struct sr_serial_dev_inst *serial);
SR_PRIV int serial_flush(struct sr_serial_dev_inst *serial);
SR_PRIV int serial_write(struct sr_serial_dev_inst *serial,
		const void *buf, size_t count);
SR_PRIV int serial_read(struct sr_serial_dev_inst *serial, void *buf,
		size_t count);
SR_PRIV int serial_set_params(struct sr_serial_dev_inst *serial, int baudrate,
		int bits, int parity, int stopbits, int flowcontrol, int rts, int dtr);
SR_PRIV int serial_set_paramstr(struct sr_serial_dev_inst *serial,
		const char *paramstr);
SR_PRIV int serial_readline(struct sr_serial_dev_inst *serial, char **buf,
		int *buflen, gint64 timeout_ms);
SR_PRIV int serial_stream_detect(struct sr_serial_dev_inst *serial,
				 uint8_t *buf, size_t *buflen,
				 size_t packet_size, packet_valid_t is_valid,
				 uint64_t timeout_ms, int baudrate);
SR_PRIV int sr_serial_extract_options(GSList *options, const char **serial_device,
				      const char **serial_options);
#endif

/*--- hardware/common/ezusb.c -----------------------------------------------*/

#ifdef HAVE_LIBUSB_1_0
SR_PRIV int ezusb_reset(struct libusb_device_handle *hdl, int set_clear);
SR_PRIV int ezusb_install_firmware(libusb_device_handle *hdl,
				   const char *filename);
SR_PRIV int ezusb_upload_firmware(libusb_device *dev, int configuration,
				  const char *filename);
#endif

/*--- hardware/common/usb.c -------------------------------------------------*/

#ifdef HAVE_LIBUSB_1_0
SR_PRIV GSList *sr_usb_find(libusb_context *usb_ctx, const char *conn);
SR_PRIV int sr_usb_open(libusb_context *usb_ctx, struct sr_usb_dev_inst *usb);
#endif

/*--- hardware/common/scpi.c ------------------------------------------------*/

#ifdef HAVE_LIBSERIALPORT

#define SCPI_CMD_IDN "*IDN?"
#define SCPI_CMD_OPC "*OPC?"

enum {
	SCPI_CMD_SET_TRIGGER_SOURCE,
	SCPI_CMD_SET_TIMEBASE,
	SCPI_CMD_SET_VERTICAL_DIV,
	SCPI_CMD_SET_TRIGGER_SLOPE,
	SCPI_CMD_SET_COUPLING,
	SCPI_CMD_SET_HORIZ_TRIGGERPOS,
	SCPI_CMD_GET_ANALOG_CHAN_STATE,
	SCPI_CMD_GET_DIG_CHAN_STATE,
	SCPI_CMD_GET_TIMEBASE,
	SCPI_CMD_GET_VERTICAL_DIV,
	SCPI_CMD_GET_VERTICAL_OFFSET,
	SCPI_CMD_GET_TRIGGER_SOURCE,
	SCPI_CMD_GET_HORIZ_TRIGGERPOS,
	SCPI_CMD_GET_TRIGGER_SLOPE,
	SCPI_CMD_GET_COUPLING,
	SCPI_CMD_SET_ANALOG_CHAN_STATE,
	SCPI_CMD_SET_DIG_CHAN_STATE,
	SCPI_CMD_GET_DIG_POD_STATE,
	SCPI_CMD_SET_DIG_POD_STATE,
	SCPI_CMD_GET_ANALOG_DATA,
	SCPI_CMD_GET_DIG_DATA,
};

struct sr_scpi_hw_info {
	char *manufacturer;
	char *model;
	char *serial_number;
	char *firmware_version;
};

struct sr_scpi_dev_inst {
	int (*open)(void *priv);
	int (*source_add)(void *priv, int events,
		int timeout, sr_receive_data_callback_t cb, void *cb_data);
	int (*source_remove)(void *priv);
	int (*send)(void *priv, const char *command);
	int (*receive)(void *priv, char **scpi_response);
	int (*close)(void *priv);
	void (*free)(void *priv);
	void *priv;
};

SR_PRIV int sr_scpi_open(struct sr_scpi_dev_inst *scpi);
SR_PRIV int sr_scpi_source_add(struct sr_scpi_dev_inst *scpi, int events,
		int timeout, sr_receive_data_callback_t cb, void *cb_data);
SR_PRIV int sr_scpi_source_remove(struct sr_scpi_dev_inst *scpi);
SR_PRIV int sr_scpi_send(struct sr_scpi_dev_inst *scpi,
			const char *command);
SR_PRIV int sr_scpi_receive(struct sr_scpi_dev_inst *scpi,
			char **scpi_response);
SR_PRIV int sr_scpi_close(struct sr_scpi_dev_inst *scpi);
SR_PRIV void sr_scpi_free(struct sr_scpi_dev_inst *scpi);

SR_PRIV int sr_scpi_get_string(struct sr_scpi_dev_inst *scpi,
			const char *command, char **scpi_response);
SR_PRIV int sr_scpi_get_bool(struct sr_scpi_dev_inst *scpi,
			const char *command, gboolean *scpi_response);
SR_PRIV int sr_scpi_get_int(struct sr_scpi_dev_inst *scpi,
			const char *command, int *scpi_response);
SR_PRIV int sr_scpi_get_float(struct sr_scpi_dev_inst *scpi,
			const char *command, float *scpi_response);
SR_PRIV int sr_scpi_get_double(struct sr_scpi_dev_inst *scpi,
			const char *command, double *scpi_response);
SR_PRIV int sr_scpi_get_opc(struct sr_scpi_dev_inst *scpi);
SR_PRIV int sr_scpi_get_floatv(struct sr_scpi_dev_inst *scpi,
			const char *command, GArray **scpi_response);
SR_PRIV int sr_scpi_get_uint8v(struct sr_scpi_dev_inst *scpi,
			const char *command, GArray **scpi_response);
SR_PRIV int sr_scpi_get_hw_id(struct sr_scpi_dev_inst *scpi,
			struct sr_scpi_hw_info **scpi_response);
SR_PRIV void sr_scpi_hw_info_free(struct sr_scpi_hw_info *hw_info);

#endif

/*--- hardware/common/scpi_serial.c -----------------------------------------*/

SR_PRIV struct sr_scpi_dev_inst *scpi_serial_dev_inst_new(const char *port,
		        const char *serialcomm);

/*--- hardware/common/scpi_usbtmc.c -----------------------------------------*/

SR_PRIV struct sr_scpi_dev_inst *scpi_usbtmc_dev_inst_new(const char *device);

/*--- hardware/common/dmm/es51922.c -----------------------------------------*/

#define ES51922_PACKET_SIZE 14

struct es51922_info {
	gboolean is_judge, is_vbar, is_voltage, is_auto, is_micro, is_current;
	gboolean is_milli, is_resistance, is_continuity, is_diode, is_lpf;
	gboolean is_frequency, is_duty_cycle, is_capacitance, is_temperature;
	gboolean is_celsius, is_fahrenheit, is_adp, is_sign, is_batt, is_ol;
	gboolean is_max, is_min, is_rel, is_rmr, is_ul, is_pmax, is_pmin;
	gboolean is_dc, is_ac, is_vahz, is_hold, is_nano, is_kilo, is_mega;
};

SR_PRIV gboolean sr_es51922_packet_valid(const uint8_t *buf);
SR_PRIV int sr_es51922_parse(const uint8_t *buf, float *floatval,
			     struct sr_datafeed_analog *analog, void *info);

/*--- hardware/common/dmm/es519xx.c -----------------------------------------*/

/**
 * All 11-byte es519xx chips repeat each block twice for each conversion cycle
 * so always read 2 blocks at a time.
 */
#define ES519XX_11B_PACKET_SIZE (11 * 2)
#define ES519XX_14B_PACKET_SIZE 14

struct es519xx_info {
	gboolean is_judge, is_voltage, is_auto, is_micro, is_current;
	gboolean is_milli, is_resistance, is_continuity, is_diode;
	gboolean is_frequency, is_rpm, is_capacitance, is_duty_cycle;
	gboolean is_temperature, is_celsius, is_fahrenheit;
	gboolean is_adp0, is_adp1, is_adp2, is_adp3;
	gboolean is_sign, is_batt, is_ol, is_pmax, is_pmin, is_apo;
	gboolean is_dc, is_ac, is_vahz, is_min, is_max, is_rel, is_hold;
	gboolean is_digit4, is_ul, is_vasel, is_vbar, is_lpf1, is_lpf0, is_rmr;
	uint32_t baudrate;
	int packet_size;
	gboolean alt_functions, fivedigits, clampmeter, selectable_lpf;
};

SR_PRIV gboolean sr_es519xx_2400_11b_packet_valid(const uint8_t *buf);
SR_PRIV int sr_es519xx_2400_11b_parse(const uint8_t *buf, float *floatval,
		struct sr_datafeed_analog *analog, void *info);
SR_PRIV gboolean sr_es519xx_19200_11b_5digits_packet_valid(const uint8_t *buf);
SR_PRIV int sr_es519xx_19200_11b_5digits_parse(const uint8_t *buf,
		float *floatval, struct sr_datafeed_analog *analog, void *info);
SR_PRIV gboolean sr_es519xx_19200_11b_clamp_packet_valid(const uint8_t *buf);
SR_PRIV int sr_es519xx_19200_11b_clamp_parse(const uint8_t *buf,
		float *floatval, struct sr_datafeed_analog *analog, void *info);
SR_PRIV gboolean sr_es519xx_19200_11b_packet_valid(const uint8_t *buf);
SR_PRIV int sr_es519xx_19200_11b_parse(const uint8_t *buf, float *floatval,
		struct sr_datafeed_analog *analog, void *info);
SR_PRIV gboolean sr_es519xx_19200_14b_packet_valid(const uint8_t *buf);
SR_PRIV int sr_es519xx_19200_14b_parse(const uint8_t *buf, float *floatval,
		struct sr_datafeed_analog *analog, void *info);
SR_PRIV gboolean sr_es519xx_19200_14b_sel_lpf_packet_valid(const uint8_t *buf);
SR_PRIV int sr_es519xx_19200_14b_sel_lpf_parse(const uint8_t *buf,
		float *floatval, struct sr_datafeed_analog *analog, void *info);

/*--- hardware/common/dmm/fs9922.c ------------------------------------------*/

#define FS9922_PACKET_SIZE 14

struct fs9922_info {
	gboolean is_auto, is_dc, is_ac, is_rel, is_hold, is_bpn, is_z1, is_z2;
	gboolean is_max, is_min, is_apo, is_bat, is_nano, is_z3, is_micro;
	gboolean is_milli, is_kilo, is_mega, is_beep, is_diode, is_percent;
	gboolean is_z4, is_volt, is_ampere, is_ohm, is_hfe, is_hertz, is_farad;
	gboolean is_celsius, is_fahrenheit;
	int bargraph_sign, bargraph_value;
};

SR_PRIV gboolean sr_fs9922_packet_valid(const uint8_t *buf);
SR_PRIV int sr_fs9922_parse(const uint8_t *buf, float *floatval,
			    struct sr_datafeed_analog *analog, void *info);
SR_PRIV void sr_fs9922_z1_diode(struct sr_datafeed_analog *analog, void *info);

/*--- hardware/common/dmm/fs9721.c ------------------------------------------*/

#define FS9721_PACKET_SIZE 14

struct fs9721_info {
	gboolean is_ac, is_dc, is_auto, is_rs232, is_micro, is_nano, is_kilo;
	gboolean is_diode, is_milli, is_percent, is_mega, is_beep, is_farad;
	gboolean is_ohm, is_rel, is_hold, is_ampere, is_volt, is_hz, is_bat;
	gboolean is_c2c1_11, is_c2c1_10, is_c2c1_01, is_c2c1_00, is_sign;
};

SR_PRIV gboolean sr_fs9721_packet_valid(const uint8_t *buf);
SR_PRIV int sr_fs9721_parse(const uint8_t *buf, float *floatval,
			    struct sr_datafeed_analog *analog, void *info);
SR_PRIV void sr_fs9721_00_temp_c(struct sr_datafeed_analog *analog, void *info);
SR_PRIV void sr_fs9721_01_temp_c(struct sr_datafeed_analog *analog, void *info);
SR_PRIV void sr_fs9721_10_temp_c(struct sr_datafeed_analog *analog, void *info);
SR_PRIV void sr_fs9721_01_10_temp_f_c(struct sr_datafeed_analog *analog, void *info);

/*--- hardware/common/dmm/metex14.c -----------------------------------------*/

#define METEX14_PACKET_SIZE 14

struct metex14_info {
	gboolean is_ac, is_dc, is_resistance, is_capacity, is_temperature;
	gboolean is_diode, is_frequency, is_ampere, is_volt, is_farad;
	gboolean is_hertz, is_ohm, is_celsius, is_pico, is_nano, is_micro;
	gboolean is_milli, is_kilo, is_mega, is_gain, is_decibel, is_hfe;
	gboolean is_unitless;
};

#ifdef HAVE_LIBSERIALPORT
SR_PRIV int sr_metex14_packet_request(struct sr_serial_dev_inst *serial);
#endif
SR_PRIV gboolean sr_metex14_packet_valid(const uint8_t *buf);
SR_PRIV int sr_metex14_parse(const uint8_t *buf, float *floatval,
			     struct sr_datafeed_analog *analog, void *info);

/*--- hardware/common/dmm/rs9lcd.c ------------------------------------------*/

#define RS9LCD_PACKET_SIZE 9

/* Dummy info struct. The parser does not use it. */
struct rs9lcd_info { int dummy; };

SR_PRIV gboolean sr_rs9lcd_packet_valid(const uint8_t *buf);
SR_PRIV int sr_rs9lcd_parse(const uint8_t *buf, float *floatval,
			    struct sr_datafeed_analog *analog, void *info);

#endif
