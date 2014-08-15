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

/** @file
  * @internal
  */

#ifndef LIBSIGROK_LIBSIGROK_INTERNAL_H
#define LIBSIGROK_LIBSIGROK_INTERNAL_H

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

/**
 * Read a 8 bits integer out of memory.
 * @param x a pointer to the input memory
 * @return the corresponding integer
 */
#define R8(x)     ((unsigned)((const uint8_t*)(x))[0])

/**
 * Read a 16 bits big endian integer out of memory.
 * @param x a pointer to the input memory
 * @return the corresponding integer
 */
#define RB16(x)  (((unsigned)((const uint8_t*)(x))[0] <<  8) |  \
                   (unsigned)((const uint8_t*)(x))[1])

/**
 * Read a 16 bits little endian integer out of memory.
 * @param x a pointer to the input memory
 * @return the corresponding integer
 */
#define RL16(x)  (((unsigned)((const uint8_t*)(x))[1] <<  8) | \
                   (unsigned)((const uint8_t*)(x))[0])

/**
 * Read a 32 bits big endian integer out of memory.
 * @param x a pointer to the input memory
 * @return the corresponding integer
 */
#define RB32(x)  (((unsigned)((const uint8_t*)(x))[0] << 24) | \
                  ((unsigned)((const uint8_t*)(x))[1] << 16) |  \
                  ((unsigned)((const uint8_t*)(x))[2] <<  8) |  \
                   (unsigned)((const uint8_t*)(x))[3])

/**
 * Read a 32 bits little endian integer out of memory.
 * @param x a pointer to the input memory
 * @return the corresponding integer
 */
#define RL32(x)  (((unsigned)((const uint8_t*)(x))[3] << 24) | \
                  ((unsigned)((const uint8_t*)(x))[2] << 16) |  \
                  ((unsigned)((const uint8_t*)(x))[1] <<  8) |  \
                   (unsigned)((const uint8_t*)(x))[0])

/**
 * Write a 8 bits integer to memory.
 * @param p a pointer to the output memory
 * @param x the input integer
 */
#define W8(p, x)    do { ((uint8_t*)(p))[0] = (uint8_t) (x);      } while(0)

/**
 * Write a 16 bits integer to memory stored as big endian.
 * @param p a pointer to the output memory
 * @param x the input integer
 */
#define WB16(p, x)  do { ((uint8_t*)(p))[1] = (uint8_t) (x);      \
                         ((uint8_t*)(p))[0] = (uint8_t)((x)>>8);  } while(0)

/**
 * Write a 16 bits integer to memory stored as little endian.
 * @param p a pointer to the output memory
 * @param x the input integer
 */
#define WL16(p, x)  do { ((uint8_t*)(p))[0] = (uint8_t) (x);      \
                         ((uint8_t*)(p))[1] = (uint8_t)((x)>>8);  } while(0)

/**
 * Write a 32 bits integer to memory stored as big endian.
 * @param p a pointer to the output memory
 * @param x the input integer
 */
#define WB32(p, x)  do { ((uint8_t*)(p))[3] = (uint8_t) (x);      \
                         ((uint8_t*)(p))[2] = (uint8_t)((x)>>8);  \
                         ((uint8_t*)(p))[1] = (uint8_t)((x)>>16); \
                         ((uint8_t*)(p))[0] = (uint8_t)((x)>>24); } while(0)

/**
 * Write a 32 bits integer to memory stored as little endian.
 * @param p a pointer to the output memory
 * @param x the input integer
 */
#define WL32(p, x)  do { ((uint8_t*)(p))[0] = (uint8_t) (x);      \
                         ((uint8_t*)(p))[1] = (uint8_t)((x)>>8);  \
                         ((uint8_t*)(p))[2] = (uint8_t)((x)>>16); \
                         ((uint8_t*)(p))[3] = (uint8_t)((x)>>24); } while(0)

/* Portability fixes for FreeBSD. */
#ifdef __FreeBSD__
#define LIBUSB_CLASS_APPLICATION 0xfe
#define libusb_handle_events_timeout_completed(ctx, tv, c) \
	libusb_handle_events_timeout(ctx, tv)
#endif

/* Static definitions of structs ending with an all-zero entry are a
 * problem when compiling with -Wmissing-field-initializers: GCC
 * suppresses the warning only with { 0 }, clang wants { } */
#ifdef __clang__
#define ALL_ZERO { }
#else
#define ALL_ZERO { 0 }
#endif

struct sr_context {
#ifdef HAVE_LIBUSB_1_0
	libusb_context *libusb_ctx;
	gboolean usb_source_present;
#ifdef _WIN32
	GThread *usb_thread;
	gboolean usb_thread_running;
	GMutex usb_mutex;
	HANDLE usb_event;
	GPollFD usb_pollfd;
	sr_receive_data_callback usb_cb;
	void *usb_cb_data;
#endif
#endif
};

/** Input module metadata keys. */
enum sr_input_meta_keys {
	/** The input filename, if there is one. */
	SR_INPUT_META_FILENAME = 0x01,
	/** The input file's size in bytes. */
	SR_INPUT_META_FILESIZE = 0x02,
	/** The first 128 bytes of the file, provided as a GString. */
	SR_INPUT_META_HEADER = 0x04,
	/** The file's MIME type. */
	SR_INPUT_META_MIMETYPE = 0x08,

	/** The module cannot identify a file without this metadata. */
	SR_INPUT_META_REQUIRED = 0x80,
};

/** Input (file) module struct. */
struct sr_input {
	/**
	 * A pointer to this input module's 'struct sr_input_module'.
	 */
	const struct sr_input_module *module;
	GString *buf;
	struct sr_dev_inst *sdi;
	void *priv;
};

/** Input (file) module driver. */
struct sr_input_module {
	/**
	 * A unique ID for this output module, suitable for use in command-line
	 * clients, [a-z0-9-]. Must not be NULL.
	 */
	const char *id;

	/**
	 * A unique name for this output module, suitable for use in GUI
	 * clients, can contain UTF-8. Must not be NULL.
	 */
	const char *name;

	/**
	 * A short description of the output module. Must not be NULL.
	 *
	 * This can be displayed by frontends, e.g. when selecting the output
	 * module for saving a file.
	 */
	const char *desc;

	/**
	 * Zero-terminated list of metadata items the module needs to be able
	 * to identify an input stream. Can be all-zero, if the module cannot
	 * identify streams at all, i.e. has to be forced into use.
	 *
	 * Each item is one of:
	 *   SR_INPUT_META_FILENAME
	 *   SR_INPUT_META_FILESIZE
	 *   SR_INPUT_META_HEADER
	 *   SR_INPUT_META_MIMETYPE
	 *
	 * If the high bit (SR_INPUT META_REQUIRED) is set, the module cannot
	 * identify a stream without the given metadata.
	 */
	const uint8_t metadata[8];

	/**
	 * Returns a NULL-terminated list of options this module can take.
	 * Can be NULL, if the module has no options.
	 */
	struct sr_option *(*options) (void);

	/**
	 * Check if this input module can load and parse the specified stream.
	 *
	 * @param[in] metadata Metadata the module can use to identify the stream.
	 *
	 * @retval TRUE This module knows the format.
	 * @retval FALSE This module does not know the format.
	 */
	int (*format_match) (GHashTable *metadata);

	/**
	 * Initialize the input module.
	 *
	 * @param in A pointer to a valid 'struct sr_input' that the caller
	 *           has to allocate and provide to this function. It is also
	 *           the responsibility of the caller to free it later.
	 * @param[in] filename The name (and path) of the file to use.
	 *
	 * @retval SR_OK Success
	 * @retval other Negative error code.
	 */
	int (*init) (struct sr_input *in, GHashTable *options);

	/**
	 * Load a file, parsing the input according to the file's format.
	 *
	 * This function will send datafeed packets to the session bus, so
	 * the calling frontend must have registered its session callbacks
	 * beforehand.
	 *
	 * The packet types sent across the session bus by this function must
	 * include at least SR_DF_HEADER, SR_DF_END, and an appropriate data
	 * type such as SR_DF_LOGIC. It may also send a SR_DF_TRIGGER packet
	 * if appropriate.
	 *
	 * @param in A pointer to a valid 'struct sr_input' that the caller
	 *           has to allocate and provide to this function. It is also
	 *           the responsibility of the caller to free it later.
	 * @param f The name (and path) of the file to use.
	 *
	 * @retval SR_OK Success
	 * @retval other Negative error code.
	 */
	int (*receive) (const struct sr_input *in, GString *buf);

	/**
	 * This function is called after the caller is finished using
	 * the input module, and can be used to free any internal
	 * resources the module may keep.
	 *
	 * @retval SR_OK Success
	 * @retval other Negative error code.
	 */
	int (*cleanup) (struct sr_input *in);
};

/** Output module instance. */
struct sr_output {
	/** A pointer to this output's module.  */
	const struct sr_output_module *module;

	/**
	 * The device for which this output module is creating output. This
	 * can be used by the module to find out channel names and numbers.
	 */
	const struct sr_dev_inst *sdi;

	/**
	 * A generic pointer which can be used by the module to keep internal
	 * state between calls into its callback functions.
	 *
	 * For example, the module might store a pointer to a chunk of output
	 * there, and only flush it when it reaches a certain size.
	 */
	void *priv;
};

/** Output module driver. */
struct sr_output_module {
	/**
	 * A unique ID for this output module, suitable for use in command-line
	 * clients, [a-z0-9-]. Must not be NULL.
	 */
	char *id;

	/**
	 * A unique name for this output module, suitable for use in GUI
	 * clients, can contain UTF-8. Must not be NULL.
	 */
	const char *name;

	/**
	 * A short description of the output module. Must not be NULL.
	 *
	 * This can be displayed by frontends, e.g. when selecting the output
	 * module for saving a file.
	 */
	char *desc;

	/**
	 * Returns a NULL-terminated list of options this module can take.
	 * Can be NULL, if the module has no options.
	 */
	const struct sr_option *(*options) (void);

	/**
	 * This function is called once, at the beginning of an output stream.
	 *
	 * The device struct will be available in the output struct passed in,
	 * as well as the param field -- which may be NULL or an empty string,
	 * if no parameter was passed.
	 *
	 * The module can use this to initialize itself, create a struct for
	 * keeping state and storing it in the <code>internal</code> field.
	 *
	 * @param o Pointer to the respective 'struct sr_output'.
	 *
	 * @retval SR_OK Success
	 * @retval other Negative error code.
	 */
	int (*init) (struct sr_output *o, GHashTable *options);

	/**
	 * This function is passed a copy of every packed in the data feed.
	 * Any output generated by the output module in response to the
	 * packet should be returned in a newly allocated GString
	 * <code>out</code>, which will be freed by the caller.
	 *
	 * Packets not of interest to the output module can just be ignored,
	 * and the <code>out</code> parameter set to NULL.
	 *
	 * @param o Pointer to the respective 'struct sr_output'.
	 * @param sdi The device instance that generated the packet.
	 * @param packet The complete packet.
	 * @param out A pointer where a GString * should be stored if
	 * the module generates output, or NULL if not.
	 *
	 * @retval SR_OK Success
	 * @retval other Negative error code.
	 */
	int (*receive) (const struct sr_output *o,
			const struct sr_datafeed_packet *packet, GString **out);

	/**
	 * This function is called after the caller is finished using
	 * the output module, and can be used to free any internal
	 * resources the module may keep.
	 *
	 * @retval SR_OK Success
	 * @retval other Negative error code.
	 */
	int (*cleanup) (struct sr_output *o);
};

#ifdef HAVE_LIBUSB_1_0
/** USB device instance */
struct sr_usb_dev_inst {
	/** USB bus */
	uint8_t bus;
	/** Device address on USB bus */
	uint8_t address;
	/** libusb device handle */
	struct libusb_device_handle *devhdl;
};
#endif

#ifdef HAVE_LIBSERIALPORT
#define SERIAL_PARITY_NONE SP_PARITY_NONE
#define SERIAL_PARITY_EVEN SP_PARITY_EVEN
#define SERIAL_PARITY_ODD  SP_PARITY_ODD
struct sr_serial_dev_inst {
	/** Port name, e.g. '/dev/tty42'. */
	char *port;
	/** Comm params for serial_set_paramstr(). */
	char *serialcomm;
	/** Port is non-blocking. */
	int nonblocking;
	/** libserialport port handle */
	struct sp_port *data;
	/** libserialport event set */
	struct sp_event_set *event_set;
	/** GPollFDs for event polling */
	GPollFD *pollfds;
};
#endif

struct sr_usbtmc_dev_inst {
	char *device;
	int fd;
};

/* Private driver context. */
struct drv_context {
	/** sigrok context */
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

/* Message logging helpers with subsystem-specific prefix string. */
#ifndef NO_LOG_WRAPPERS
#define sr_log(l, s, args...) sr_log(l, "%s: " s, LOG_PREFIX, ## args)
#define sr_spew(s, args...) sr_spew("%s: " s, LOG_PREFIX, ## args)
#define sr_dbg(s, args...) sr_dbg("%s: " s, LOG_PREFIX, ## args)
#define sr_info(s, args...) sr_info("%s: " s, LOG_PREFIX, ## args)
#define sr_warn(s, args...) sr_warn("%s: " s, LOG_PREFIX, ## args)
#define sr_err(s, args...) sr_err("%s: " s, LOG_PREFIX, ## args)
#endif

/*--- device.c --------------------------------------------------------------*/

/** Values for the changes argument of sr_dev_driver.config_channel_set. */
enum {
	/** The enabled state of the channel has been changed. */
	SR_CHANNEL_SET_ENABLED = 1 << 0,
};

SR_PRIV struct sr_channel *sr_channel_new(int index, int type,
		gboolean enabled, const char *name);

/* Generic device instances */
SR_PRIV struct sr_dev_inst *sr_dev_inst_new(int index, int status,
		const char *vendor, const char *model, const char *version);
SR_PRIV void sr_dev_inst_free(struct sr_dev_inst *sdi);

#ifdef HAVE_LIBUSB_1_0
/* USB-specific instances */
SR_PRIV struct sr_usb_dev_inst *sr_usb_dev_inst_new(uint8_t bus,
		uint8_t address, struct libusb_device_handle *hdl);
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
SR_PRIV int sr_source_remove_pollfd(GPollFD *pollfd);
SR_PRIV int sr_source_remove_channel(GIOChannel *channel);
SR_PRIV int sr_source_add(int fd, int events, int timeout,
		sr_receive_data_callback cb, void *cb_data);
SR_PRIV int sr_source_add_pollfd(GPollFD *pollfd, int timeout,
		sr_receive_data_callback cb, void *cb_data);
SR_PRIV int sr_source_add_channel(GIOChannel *channel, int events, int timeout,
		sr_receive_data_callback cb, void *cb_data);

/*--- session.c -------------------------------------------------------------*/

struct sr_session {
	/** List of struct sr_dev pointers. */
	GSList *devs;
	/** List of struct datafeed_callback pointers. */
	GSList *datafeed_callbacks;
	struct sr_trigger *trigger;
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
	/** Mutex protecting access to abort_session. */
	GMutex stop_mutex;
	/** Abort current session. See sr_session_stop(). */
	gboolean abort_session;
};

SR_PRIV int sr_session_send(const struct sr_dev_inst *sdi,
		const struct sr_datafeed_packet *packet);
SR_PRIV int sr_session_stop_sync(struct sr_session *session);
SR_PRIV int sr_sessionfile_check(const char *filename);

/*--- std.c -----------------------------------------------------------------*/

typedef int (*dev_close_callback)(struct sr_dev_inst *sdi);
typedef void (*std_dev_clear_callback)(void *priv);

SR_PRIV int std_init(struct sr_context *sr_ctx, struct sr_dev_driver *di,
		const char *prefix);
#ifdef HAVE_LIBSERIALPORT
SR_PRIV int std_serial_dev_open(struct sr_dev_inst *sdi);
SR_PRIV int std_serial_dev_acquisition_stop(struct sr_dev_inst *sdi,
		void *cb_data, dev_close_callback dev_close_fn,
		struct sr_serial_dev_inst *serial, const char *prefix);
#endif
SR_PRIV int std_session_send_df_header(const struct sr_dev_inst *sdi,
		const char *prefix);
SR_PRIV int std_dev_clear(const struct sr_dev_driver *driver,
		std_dev_clear_callback clear_private);
SR_PRIV int std_serial_dev_close(struct sr_dev_inst *sdi);

/*--- strutil.c -------------------------------------------------------------*/

SR_PRIV int sr_atol(const char *str, long *ret);
SR_PRIV int sr_atoi(const char *str, int *ret);
SR_PRIV int sr_atod(const char *str, double *ret);
SR_PRIV int sr_atof(const char *str, float *ret);
SR_PRIV int sr_atof_ascii(const char *str, float *ret);

/*--- soft-trigger.c --------------------------------------------------------*/

struct soft_trigger_logic {
	const struct sr_dev_inst *sdi;
	const struct sr_trigger *trigger;
	int count;
	int unitsize;
	int cur_stage;
	uint8_t *prev_sample;
};

SR_PRIV struct soft_trigger_logic *soft_trigger_logic_new(
		const struct sr_dev_inst *sdi, struct sr_trigger *trigger);
SR_PRIV void soft_trigger_logic_free(struct soft_trigger_logic *st);
SR_PRIV int soft_trigger_logic_check(struct soft_trigger_logic *st, uint8_t *buf,
		int len);

/*--- hardware/common/serial.c ----------------------------------------------*/

#ifdef HAVE_LIBSERIALPORT
enum {
	SERIAL_RDWR = 1,
	SERIAL_RDONLY = 2,
	SERIAL_NONBLOCK = 4,
};

typedef gboolean (*packet_valid_callback)(const uint8_t *buf);

SR_PRIV int serial_open(struct sr_serial_dev_inst *serial, int flags);
SR_PRIV int serial_close(struct sr_serial_dev_inst *serial);
SR_PRIV int serial_flush(struct sr_serial_dev_inst *serial);
SR_PRIV int serial_write(struct sr_serial_dev_inst *serial,
		const void *buf, size_t count);
SR_PRIV int serial_write_blocking(struct sr_serial_dev_inst *serial,
		const void *buf, size_t count);
SR_PRIV int serial_write_nonblocking(struct sr_serial_dev_inst *serial,
		const void *buf, size_t count);
SR_PRIV int serial_read(struct sr_serial_dev_inst *serial, void *buf,
		size_t count);
SR_PRIV int serial_read_blocking(struct sr_serial_dev_inst *serial, void *buf,
		size_t count);
SR_PRIV int serial_read_nonblocking(struct sr_serial_dev_inst *serial, void *buf,
		size_t count);
SR_PRIV int serial_set_params(struct sr_serial_dev_inst *serial, int baudrate,
		int bits, int parity, int stopbits, int flowcontrol, int rts, int dtr);
SR_PRIV int serial_set_paramstr(struct sr_serial_dev_inst *serial,
		const char *paramstr);
SR_PRIV int serial_readline(struct sr_serial_dev_inst *serial, char **buf,
		int *buflen, gint64 timeout_ms);
SR_PRIV int serial_stream_detect(struct sr_serial_dev_inst *serial,
				 uint8_t *buf, size_t *buflen,
				 size_t packet_size,
				 packet_valid_callback is_valid,
				 uint64_t timeout_ms, int baudrate);
SR_PRIV int sr_serial_extract_options(GSList *options, const char **serial_device,
				      const char **serial_options);
SR_PRIV int serial_source_add(struct sr_session *session,
		struct sr_serial_dev_inst *serial, int events, int timeout,
		sr_receive_data_callback cb, void *cb_data);
SR_PRIV int serial_source_remove(struct sr_session *session,
		struct sr_serial_dev_inst *serial);
SR_PRIV GSList *sr_serial_find_usb(uint16_t vendor_id, uint16_t product_id);
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
SR_PRIV int usb_source_add(struct sr_session *session, struct sr_context *ctx,
		int timeout, sr_receive_data_callback cb, void *cb_data);
SR_PRIV int usb_source_remove(struct sr_session *session, struct sr_context *ctx);
#endif

/*--- hardware/common/scpi.c ------------------------------------------------*/

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
	SCPI_CMD_GET_SAMPLE_RATE,
	SCPI_CMD_GET_SAMPLE_RATE_LIVE,
};

struct sr_scpi_hw_info {
	char *manufacturer;
	char *model;
	char *serial_number;
	char *firmware_version;
};

struct sr_scpi_dev_inst {
	const char *name;
	const char *prefix;
	int priv_size;
	GSList *(*scan)(struct drv_context *drvc);
	int (*dev_inst_new)(void *priv, struct drv_context *drvc,
		const char *resource, char **params, const char *serialcomm);
	int (*open)(void *priv);
	int (*source_add)(struct sr_session *session, void *priv, int events,
		int timeout, sr_receive_data_callback cb, void *cb_data);
	int (*source_remove)(struct sr_session *session, void *priv);
	int (*send)(void *priv, const char *command);
	int (*read_begin)(void *priv);
	int (*read_data)(void *priv, char *buf, int maxlen);
	int (*read_complete)(void *priv);
	int (*close)(void *priv);
	void (*free)(void *priv);
	void *priv;
};

SR_PRIV GSList *sr_scpi_scan(struct drv_context *drvc, GSList *options,
		struct sr_dev_inst *(*probe_device)(struct sr_scpi_dev_inst *scpi));
SR_PRIV struct sr_scpi_dev_inst *scpi_dev_inst_new(struct drv_context *drvc,
		const char *resource, const char *serialcomm);
SR_PRIV int sr_scpi_open(struct sr_scpi_dev_inst *scpi);
SR_PRIV int sr_scpi_source_add(struct sr_session *session,
		struct sr_scpi_dev_inst *scpi, int events, int timeout,
		sr_receive_data_callback cb, void *cb_data);
SR_PRIV int sr_scpi_source_remove(struct sr_session *session,
		struct sr_scpi_dev_inst *scpi);
SR_PRIV int sr_scpi_send(struct sr_scpi_dev_inst *scpi,
		const char *format, ...);
SR_PRIV int sr_scpi_send_variadic(struct sr_scpi_dev_inst *scpi,
		const char *format, va_list args);
SR_PRIV int sr_scpi_read_begin(struct sr_scpi_dev_inst *scpi);
SR_PRIV int sr_scpi_read_data(struct sr_scpi_dev_inst *scpi, char *buf, int maxlen);
SR_PRIV int sr_scpi_read_complete(struct sr_scpi_dev_inst *scpi);
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
SR_PRIV gboolean sr_es519xx_2400_11b_altfn_packet_valid(const uint8_t *buf);
SR_PRIV int sr_es519xx_2400_11b_altfn_parse(const uint8_t *buf,
		float *floatval, struct sr_datafeed_analog *analog, void *info);
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
SR_PRIV void sr_fs9721_max_c_min(struct sr_datafeed_analog *analog, void *info);

/*--- hardware/common/dmm/m2110.c -----------------------------------------*/

#define BBCGM_M2110_PACKET_SIZE 9

SR_PRIV gboolean sr_m2110_packet_valid(const uint8_t *buf);
SR_PRIV int sr_m2110_parse(const uint8_t *buf, float *floatval,
			     struct sr_datafeed_analog *analog, void *info);

/*--- hardware/common/dmm/metex14.c -----------------------------------------*/

#define METEX14_PACKET_SIZE 14

struct metex14_info {
	gboolean is_ac, is_dc, is_resistance, is_capacity, is_temperature;
	gboolean is_diode, is_frequency, is_ampere, is_volt, is_farad;
	gboolean is_hertz, is_ohm, is_celsius, is_pico, is_nano, is_micro;
	gboolean is_milli, is_kilo, is_mega, is_gain, is_decibel, is_hfe;
	gboolean is_unitless, is_logic;
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

/*--- hardware/common/dmm/bm25x.c -----------------------------------------*/

#define BRYMEN_BM25X_PACKET_SIZE 15

/* Dummy info struct. The parser does not use it. */
struct bm25x_info { int dummy; };

SR_PRIV gboolean sr_brymen_bm25x_packet_valid(const uint8_t *buf);
SR_PRIV int sr_brymen_bm25x_parse(const uint8_t *buf, float *floatval,
			     struct sr_datafeed_analog *analog, void *info);

#endif
