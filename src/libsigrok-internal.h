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
 * Read a 8 bits unsigned integer out of memory.
 * @param x a pointer to the input memory
 * @return the corresponding unsigned integer
 */
#define R8(x)     ((unsigned)((const uint8_t*)(x))[0])

/**
 * Read a 16 bits big endian unsigned integer out of memory.
 * @param x a pointer to the input memory
 * @return the corresponding unsigned integer
 */
#define RB16(x)  (((unsigned)((const uint8_t*)(x))[0] <<  8) |  \
                   (unsigned)((const uint8_t*)(x))[1])

/**
 * Read a 16 bits little endian unsigned integer out of memory.
 * @param x a pointer to the input memory
 * @return the corresponding unsigned integer
 */
#define RL16(x)  (((unsigned)((const uint8_t*)(x))[1] <<  8) | \
                   (unsigned)((const uint8_t*)(x))[0])

/**
 * Read a 16 bits little endian signed integer out of memory.
 * @param x a pointer to the input memory
 * @return the corresponding signed integer
 */
#define RL16S(x)  ((int16_t) \
                  (((unsigned)((const uint8_t*)(x))[1] <<  8) | \
                    (unsigned)((const uint8_t*)(x))[0]))

/**
 * Read a 32 bits big endian unsigned integer out of memory.
 * @param x a pointer to the input memory
 * @return the corresponding unsigned integer
 */
#define RB32(x)  (((unsigned)((const uint8_t*)(x))[0] << 24) | \
                  ((unsigned)((const uint8_t*)(x))[1] << 16) |  \
                  ((unsigned)((const uint8_t*)(x))[2] <<  8) |  \
                   (unsigned)((const uint8_t*)(x))[3])

/**
 * Read a 32 bits little endian unsigned integer out of memory.
 * @param x a pointer to the input memory
 * @return the corresponding unsigned integer
 */
#define RL32(x)  (((unsigned)((const uint8_t*)(x))[3] << 24) | \
                  ((unsigned)((const uint8_t*)(x))[2] << 16) |  \
                  ((unsigned)((const uint8_t*)(x))[1] <<  8) |  \
                   (unsigned)((const uint8_t*)(x))[0])

/**
 * Read a 32 bits little endian signed integer out of memory.
 * @param x a pointer to the input memory
 * @return the corresponding signed integer
 */
#define RL32S(x)  ((int32_t) \
                 (((unsigned)((const uint8_t*)(x))[3] << 24) | \
                  ((unsigned)((const uint8_t*)(x))[2] << 16) |  \
                  ((unsigned)((const uint8_t*)(x))[1] <<  8) |  \
                   (unsigned)((const uint8_t*)(x))[0]))

/**
 * Read a 32 bits big endian float out of memory.
 * @param x a pointer to the input memory
 * @return the corresponding float
 */
#define RBFL(x)  ((union { uint32_t u; float f; }) { .u = RB32(x) }.f)

/**
 * Read a 32 bits little endian float out of memory.
 * @param x a pointer to the input memory
 * @return the corresponding float
 */
#define RLFL(x)  ((union { uint32_t u; float f; }) { .u = RL32(x) }.f)

/**
 * Write a 8 bits unsigned integer to memory.
 * @param p a pointer to the output memory
 * @param x the input unsigned integer
 */
#define W8(p, x)    do { ((uint8_t*)(p))[0] = (uint8_t) (x);      } while (0)

/**
 * Write a 16 bits unsigned integer to memory stored as big endian.
 * @param p a pointer to the output memory
 * @param x the input unsigned integer
 */
#define WB16(p, x)  do { ((uint8_t*)(p))[1] = (uint8_t) (x);      \
                         ((uint8_t*)(p))[0] = (uint8_t)((x)>>8);  } while (0)

/**
 * Write a 16 bits unsigned integer to memory stored as little endian.
 * @param p a pointer to the output memory
 * @param x the input unsigned integer
 */
#define WL16(p, x)  do { ((uint8_t*)(p))[0] = (uint8_t) (x);      \
                         ((uint8_t*)(p))[1] = (uint8_t)((x)>>8);  } while (0)

/**
 * Write a 32 bits unsigned integer to memory stored as big endian.
 * @param p a pointer to the output memory
 * @param x the input unsigned integer
 */
#define WB32(p, x)  do { ((uint8_t*)(p))[3] = (uint8_t) (x);      \
                         ((uint8_t*)(p))[2] = (uint8_t)((x)>>8);  \
                         ((uint8_t*)(p))[1] = (uint8_t)((x)>>16); \
                         ((uint8_t*)(p))[0] = (uint8_t)((x)>>24); } while (0)

/**
 * Write a 32 bits unsigned integer to memory stored as little endian.
 * @param p a pointer to the output memory
 * @param x the input unsigned integer
 */
#define WL32(p, x)  do { ((uint8_t*)(p))[0] = (uint8_t) (x);      \
                         ((uint8_t*)(p))[1] = (uint8_t)((x)>>8);  \
                         ((uint8_t*)(p))[2] = (uint8_t)((x)>>16); \
                         ((uint8_t*)(p))[3] = (uint8_t)((x)>>24); } while (0)

/**
 * Write a 32 bits float to memory stored as big endian.
 * @param p a pointer to the output memory
 * @param x the input float
 */
#define WBFL(p, x)  WB32(p, (union { uint32_t u; float f; }) { .f = x }.u)

/**
 * Write a 32 bits float to memory stored as little endian.
 * @param p a pointer to the output memory
 * @param x the input float
 */
#define WLFL(p, x)  WL32(p, (union { uint32_t u; float f; }) { .f = x }.u)

/* Portability fixes for FreeBSD. */
#ifdef __FreeBSD__
#define LIBUSB_CLASS_APPLICATION 0xfe
#define libusb_has_capability(x) 0
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
	struct sr_dev_driver **driver_list;
#ifdef HAVE_LIBUSB_1_0
	libusb_context *libusb_ctx;
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
	gboolean sdi_ready;
	void *priv;
};

/** Input (file) module driver. */
struct sr_input_module {
	/**
	 * A unique ID for this input module, suitable for use in command-line
	 * clients, [a-z0-9-]. Must not be NULL.
	 */
	const char *id;

	/**
	 * A unique name for this input module, suitable for use in GUI
	 * clients, can contain UTF-8. Must not be NULL.
	 */
	const char *name;

	/**
	 * A short description of the input module. Must not be NULL.
	 *
	 * This can be displayed by frontends, e.g. when selecting the input
	 * module for saving a file.
	 */
	const char *desc;

	/**
	 * A NULL terminated array of strings containing a list of file name
	 * extensions typical for the input file format, or NULL if there is
	 * no typical extension for this file format.
	 */
	const char *const *exts;

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
	 * @retval SR_OK This module knows the format.
	 * @retval SR_ERR_NA There wasn't enough data for this module to
	 *   positively identify the format.
	 * @retval SR_ERR_DATA This module knows the format, but cannot handle it.
	 *   This means the stream is either corrupt, or indicates a feature
	 *   that the module does not support.
	 * @retval SR_ERR This module does not know the format.
	 */
	int (*format_match) (GHashTable *metadata);

	/**
	 * Initialize the input module.
	 *
	 * @retval SR_OK Success
	 * @retval other Negative error code.
	 */
	int (*init) (struct sr_input *in, GHashTable *options);

	/**
	 * Send data to the specified input instance.
	 *
	 * When an input module instance is created with sr_input_new(), this
	 * function is used to feed data to the instance.
	 *
	 * As enough data gets fed into this function to completely populate
	 * the device instance associated with this input instance, this is
	 * guaranteed to return the moment it's ready. This gives the caller
	 * the chance to examine the device instance, attach session callbacks
	 * and so on.
	 *
	 * @retval SR_OK Success
	 * @retval other Negative error code.
	 */
	int (*receive) (struct sr_input *in, GString *buf);

	/**
	 * Signal the input module no more data will come.
	 *
	 * This will cause the module to process any data it may have buffered.
	 * The SR_DF_END packet will also typically be sent at this time.
	 */
	int (*end) (struct sr_input *in);

	/**
	 * This function is called after the caller is finished using
	 * the input module, and can be used to free any internal
	 * resources the module may keep.
	 *
	 * This function is optional.
	 *
	 * @retval SR_OK Success
	 * @retval other Negative error code.
	 */
	void (*cleanup) (struct sr_input *in);
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
	 * The name of the file that the data should be written to.
	 */
	const char *filename;

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
	 * A NULL terminated array of strings containing a list of file name
	 * extensions typical for the input file format, or NULL if there is
	 * no typical extension for this file format.
	 */
	const char *const *exts;

	/**
	 * Bitfield containing flags that describe certain properties
	 * this output module may or may not have.
	 * @see sr_output_flags
	 */
	const uint64_t flags;

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
	 * This function is passed a copy of every packet in the data feed.
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

/** Transform module instance. */
struct sr_transform {
	/** A pointer to this transform's module.  */
	const struct sr_transform_module *module;

	/**
	 * The device for which this transform module is used. This
	 * can be used by the module to find out channel names and numbers.
	 */
	const struct sr_dev_inst *sdi;

	/**
	 * A generic pointer which can be used by the module to keep internal
	 * state between calls into its callback functions.
	 */
	void *priv;
};

struct sr_transform_module {
	/**
	 * A unique ID for this transform module, suitable for use in
	 * command-line clients, [a-z0-9-]. Must not be NULL.
	 */
	char *id;

	/**
	 * A unique name for this transform module, suitable for use in GUI
	 * clients, can contain UTF-8. Must not be NULL.
	 */
	const char *name;

	/**
	 * A short description of the transform module. Must not be NULL.
	 *
	 * This can be displayed by frontends, e.g. when selecting
	 * which transform module(s) to add.
	 */
	char *desc;

	/**
	 * Returns a NULL-terminated list of options this transform module
	 * can take. Can be NULL, if the transform module has no options.
	 */
	const struct sr_option *(*options) (void);

	/**
	 * This function is called once, at the beginning of a stream.
	 *
	 * @param t Pointer to the respective 'struct sr_transform'.
	 * @param options Hash table of options for this transform module.
	 *                Can be NULL if no options are to be used.
	 *
	 * @retval SR_OK Success
	 * @retval other Negative error code.
	 */
	int (*init) (struct sr_transform *t, GHashTable *options);

	/**
	 * This function is passed a pointer to every packet in the data feed.
	 *
	 * It can either return (in packet_out) a pointer to another packet
	 * (possibly the exact same packet it got as input), or NULL.
	 *
	 * @param t Pointer to the respective 'struct sr_transform'.
	 * @param packet_in Pointer to a datafeed packet.
	 * @param packet_out Pointer to the resulting datafeed packet after
	 *                   this function was run. If NULL, the transform
	 *                   module intentionally didn't output a new packet.
	 *
	 * @retval SR_OK Success
	 * @retval other Negative error code.
	 */
	int (*receive) (const struct sr_transform *t,
			struct sr_datafeed_packet *packet_in,
			struct sr_datafeed_packet **packet_out);

	/**
	 * This function is called after the caller is finished using
	 * the transform module, and can be used to free any internal
	 * resources the module may keep.
	 *
	 * @retval SR_OK Success
	 * @retval other Negative error code.
	 */
	int (*cleanup) (struct sr_transform *t);
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
	/** libserialport port handle */
	struct sp_port *data;
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

#if defined(G_OS_WIN32) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 4))
/*
 * On MinGW, we need to specify the gnu_printf format flavor or GCC
 * will assume non-standard Microsoft printf syntax.
 */
SR_PRIV int sr_log(int loglevel, const char *format, ...)
		__attribute__((__format__ (__gnu_printf__, 2, 3)));
#else
SR_PRIV int sr_log(int loglevel, const char *format, ...) G_GNUC_PRINTF(2, 3);
#endif

/* Message logging helpers with subsystem-specific prefix string. */
#define sr_spew(...)	sr_log(SR_LOG_SPEW, LOG_PREFIX ": " __VA_ARGS__)
#define sr_dbg(...)	sr_log(SR_LOG_DBG,  LOG_PREFIX ": " __VA_ARGS__)
#define sr_info(...)	sr_log(SR_LOG_INFO, LOG_PREFIX ": " __VA_ARGS__)
#define sr_warn(...)	sr_log(SR_LOG_WARN, LOG_PREFIX ": " __VA_ARGS__)
#define sr_err(...)	sr_log(SR_LOG_ERR,  LOG_PREFIX ": " __VA_ARGS__)

/*--- device.c --------------------------------------------------------------*/

/** Values for the changes argument of sr_dev_driver.config_channel_set. */
enum {
	/** The enabled state of the channel has been changed. */
	SR_CHANNEL_SET_ENABLED = 1 << 0,
};

SR_PRIV struct sr_channel *sr_channel_new(struct sr_dev_inst *sdi,
		int index, int type, gboolean enabled, const char *name);
SR_PRIV struct sr_channel *sr_next_enabled_channel(const struct sr_dev_inst *sdi,
		struct sr_channel *cur_channel);

/** Device instance data */
struct sr_dev_inst {
	/** Device driver. */
	struct sr_dev_driver *driver;
	/** Device instance status. SR_ST_NOT_FOUND, etc. */
	int status;
	/** Device instance type. SR_INST_USB, etc. */
	int inst_type;
	/** Device vendor. */
	char *vendor;
	/** Device model. */
	char *model;
	/** Device version. */
	char *version;
	/** Serial number. */
	char *serial_num;
	/** Connection string to uniquely identify devices. */
	char *connection_id;
	/** List of channels. */
	GSList *channels;
	/** List of sr_channel_group structs */
	GSList *channel_groups;
	/** Device instance connection data (used?) */
	void *conn;
	/** Device instance private data (used?) */
	void *priv;
	/** Session to which this device is currently assigned. */
	struct sr_session *session;
};

/* Generic device instances */
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

extern SR_PRIV struct sr_dev_driver **drivers_lists[];

SR_PRIV const GVariantType *sr_variant_type_get(int datatype);
SR_PRIV int sr_variant_type_check(uint32_t key, GVariant *data);
SR_PRIV void sr_hw_cleanup_all(const struct sr_context *ctx);
SR_PRIV struct sr_config *sr_config_new(uint32_t key, GVariant *data);
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
	/** Context this session exists in. */
	struct sr_context *ctx;
	/** List of struct sr_dev_inst pointers. */
	GSList *devs;
	/** List of struct sr_dev_inst pointers owned by this session. */
	GSList *owned_devs;
	/** List of struct datafeed_callback pointers. */
	GSList *datafeed_callbacks;
	GSList *transforms;
	struct sr_trigger *trigger;

	/** Mutex protecting the main context pointer and ownership flag. */
	GMutex main_mutex;
	/** Context of the session main loop. */
	GMainContext *main_context;
	/** Whether we are using the thread's default context. */
	gboolean main_context_is_default;

	/** Whether the session has been started. */
	gboolean running;

	/** Registered event sources for this session. */
	GHashTable *event_sources;
	/** Session main loop. */
	GMainLoop *main_loop;
};

SR_PRIV int sr_session_source_add_internal(struct sr_session *session,
		void *key, GSource *source);
SR_PRIV int sr_session_source_remove_internal(struct sr_session *session,
		void *key);
SR_PRIV int sr_session_source_destroyed(struct sr_session *session,
		void *key, GSource *source);
SR_PRIV int sr_session_fd_source_add(struct sr_session *session,
		void *key, gintptr fd, int events, int timeout,
		sr_receive_data_callback cb, void *cb_data);
SR_PRIV int sr_session_send(const struct sr_dev_inst *sdi,
		const struct sr_datafeed_packet *packet);
SR_PRIV int sr_sessionfile_check(const char *filename);
SR_PRIV int sr_packet_copy(const struct sr_datafeed_packet *packet,
		struct sr_datafeed_packet **copy);
SR_PRIV void sr_packet_free(struct sr_datafeed_packet *packet);

/*--- analog.c --------------------------------------------------------------*/

SR_PRIV int sr_analog_init(struct sr_datafeed_analog2 *analog,
                           struct sr_analog_encoding *encoding,
                           struct sr_analog_meaning *meaning,
                           struct sr_analog_spec *spec,
                           int digits);

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
	uint8_t *pre_trigger_buffer;
	uint8_t *pre_trigger_head;
	int pre_trigger_size;
	int pre_trigger_fill;
};

SR_PRIV struct soft_trigger_logic *soft_trigger_logic_new(
		const struct sr_dev_inst *sdi, struct sr_trigger *trigger,
		int pre_trigger_samples);
SR_PRIV void soft_trigger_logic_free(struct soft_trigger_logic *st);
SR_PRIV int soft_trigger_logic_check(struct soft_trigger_logic *st, uint8_t *buf,
		int len, int *pre_trigger_samples);

/*--- hardware/serial.c -----------------------------------------------------*/

#ifdef HAVE_LIBSERIALPORT
enum {
	SERIAL_RDWR = 1,
	SERIAL_RDONLY = 2,
};

typedef gboolean (*packet_valid_callback)(const uint8_t *buf);

SR_PRIV int serial_open(struct sr_serial_dev_inst *serial, int flags);
SR_PRIV int serial_close(struct sr_serial_dev_inst *serial);
SR_PRIV int serial_flush(struct sr_serial_dev_inst *serial);
SR_PRIV int serial_drain(struct sr_serial_dev_inst *serial);
SR_PRIV int serial_write_blocking(struct sr_serial_dev_inst *serial,
		const void *buf, size_t count, unsigned int timeout_ms);
SR_PRIV int serial_write_nonblocking(struct sr_serial_dev_inst *serial,
		const void *buf, size_t count);
SR_PRIV int serial_read_blocking(struct sr_serial_dev_inst *serial, void *buf,
		size_t count, unsigned int timeout_ms);
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
SR_PRIV int serial_timeout(struct sr_serial_dev_inst *port, int num_bytes);
#endif

/*--- hardware/ezusb.c ------------------------------------------------------*/

#ifdef HAVE_LIBUSB_1_0
SR_PRIV int ezusb_reset(struct libusb_device_handle *hdl, int set_clear);
SR_PRIV int ezusb_install_firmware(libusb_device_handle *hdl,
				   const char *filename);
SR_PRIV int ezusb_upload_firmware(libusb_device *dev, int configuration,
				  const char *filename);
#endif

/*--- hardware/usb.c --------------------------------------------------------*/

#ifdef HAVE_LIBUSB_1_0
SR_PRIV GSList *sr_usb_find(libusb_context *usb_ctx, const char *conn);
SR_PRIV int sr_usb_open(libusb_context *usb_ctx, struct sr_usb_dev_inst *usb);
SR_PRIV void sr_usb_close(struct sr_usb_dev_inst *usb);
SR_PRIV int usb_source_add(struct sr_session *session, struct sr_context *ctx,
		int timeout, sr_receive_data_callback cb, void *cb_data);
SR_PRIV int usb_source_remove(struct sr_session *session, struct sr_context *ctx);
SR_PRIV int usb_get_port_path(libusb_device *dev, char *path, int path_len);
#endif


/*--- modbus/modbus.c -------------------------------------------------------*/

struct sr_modbus_dev_inst {
	const char *name;
	const char *prefix;
	int priv_size;
	GSList *(*scan)(int modbusaddr);
	int (*dev_inst_new)(void *priv, const char *resource,
		char **params, const char *serialcomm, int modbusaddr);
	int (*open)(void *priv);
	int (*source_add)(struct sr_session *session, void *priv, int events,
		int timeout, sr_receive_data_callback cb, void *cb_data);
	int (*source_remove)(struct sr_session *session, void *priv);
	int (*send)(void *priv, const uint8_t *buffer, int buffer_size);
	int (*read_begin)(void *priv, uint8_t *function_code);
	int (*read_data)(void *priv, uint8_t *buf, int maxlen);
	int (*read_end)(void *priv);
	int (*close)(void *priv);
	void (*free)(void *priv);
	unsigned int read_timeout_ms;
	void *priv;
};

SR_PRIV GSList *sr_modbus_scan(struct drv_context *drvc, GSList *options,
		struct sr_dev_inst *(*probe_device)(struct sr_modbus_dev_inst *modbus));
SR_PRIV struct sr_modbus_dev_inst *modbus_dev_inst_new(const char *resource,
		const char *serialcomm, int modbusaddr);
SR_PRIV int sr_modbus_open(struct sr_modbus_dev_inst *modbus);
SR_PRIV int sr_modbus_source_add(struct sr_session *session,
		struct sr_modbus_dev_inst *modbus, int events, int timeout,
		sr_receive_data_callback cb, void *cb_data);
SR_PRIV int sr_modbus_source_remove(struct sr_session *session,
		struct sr_modbus_dev_inst *modbus);
SR_PRIV int sr_modbus_request(struct sr_modbus_dev_inst *modbus,
                              uint8_t *request, int request_size);
SR_PRIV int sr_modbus_reply(struct sr_modbus_dev_inst *modbus,
                            uint8_t *reply, int reply_size);
SR_PRIV int sr_modbus_request_reply(struct sr_modbus_dev_inst *modbus,
                                    uint8_t *request, int request_size,
                                    uint8_t *reply, int reply_size);
SR_PRIV int sr_modbus_read_coils(struct sr_modbus_dev_inst *modbus,
                                 int address, int nb_coils, uint8_t *coils);
SR_PRIV int sr_modbus_read_holding_registers(struct sr_modbus_dev_inst *modbus,
                                             int address, int nb_registers,
                                             uint16_t *registers);
SR_PRIV int sr_modbus_write_coil(struct sr_modbus_dev_inst *modbus,
                                 int address, int value);
SR_PRIV int sr_modbus_write_multiple_registers(struct sr_modbus_dev_inst*modbus,
                                               int address, int nb_registers,
                                               uint16_t *registers);
SR_PRIV int sr_modbus_close(struct sr_modbus_dev_inst *modbus);
SR_PRIV void sr_modbus_free(struct sr_modbus_dev_inst *modbus);

/*--- hardware/dmm/es519xx.c ------------------------------------------------*/

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

/*--- hardware/dmm/fs9922.c -------------------------------------------------*/

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

/*--- hardware/dmm/fs9721.c -------------------------------------------------*/

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

/*--- hardware/dmm/m2110.c --------------------------------------------------*/

#define BBCGM_M2110_PACKET_SIZE 9

SR_PRIV gboolean sr_m2110_packet_valid(const uint8_t *buf);
SR_PRIV int sr_m2110_parse(const uint8_t *buf, float *floatval,
			     struct sr_datafeed_analog *analog, void *info);

/*--- hardware/dmm/metex14.c ------------------------------------------------*/

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

/*--- hardware/dmm/rs9lcd.c -------------------------------------------------*/

#define RS9LCD_PACKET_SIZE 9

/* Dummy info struct. The parser does not use it. */
struct rs9lcd_info { int dummy; };

SR_PRIV gboolean sr_rs9lcd_packet_valid(const uint8_t *buf);
SR_PRIV int sr_rs9lcd_parse(const uint8_t *buf, float *floatval,
			    struct sr_datafeed_analog *analog, void *info);

/*--- hardware/dmm/bm25x.c --------------------------------------------------*/

#define BRYMEN_BM25X_PACKET_SIZE 15

/* Dummy info struct. The parser does not use it. */
struct bm25x_info { int dummy; };

SR_PRIV gboolean sr_brymen_bm25x_packet_valid(const uint8_t *buf);
SR_PRIV int sr_brymen_bm25x_parse(const uint8_t *buf, float *floatval,
			     struct sr_datafeed_analog *analog, void *info);

/*--- hardware/dmm/ut71x.c --------------------------------------------------*/

#define UT71X_PACKET_SIZE 11

struct ut71x_info {
	gboolean is_voltage, is_resistance, is_capacitance, is_temperature;
	gboolean is_celsius, is_fahrenheit, is_current, is_continuity;
	gboolean is_diode, is_frequency, is_duty_cycle, is_dc, is_ac;
	gboolean is_auto, is_manual, is_sign, is_power, is_loop_current;
};

SR_PRIV gboolean sr_ut71x_packet_valid(const uint8_t *buf);
SR_PRIV int sr_ut71x_parse(const uint8_t *buf, float *floatval,
		struct sr_datafeed_analog *analog, void *info);

/*--- hardware/dmm/vc870.c --------------------------------------------------*/

#define VC870_PACKET_SIZE 23

struct vc870_info {
	gboolean is_voltage, is_dc, is_ac, is_temperature, is_resistance;
	gboolean is_continuity, is_capacitance, is_diode, is_loop_current;
	gboolean is_current, is_micro, is_milli, is_power;
	gboolean is_power_factor_freq, is_power_apparent_power, is_v_a_eff_value;
	gboolean is_sign2, is_sign1, is_batt, is_ol1, is_max, is_min;
	gboolean is_maxmin, is_rel, is_ol2, is_open, is_manu, is_hold;
	gboolean is_light, is_usb, is_warning, is_auto_power, is_misplug_warn;
	gboolean is_lo, is_hi, is_open2;

	gboolean is_frequency, is_dual_display, is_auto, is_rms;
};

SR_PRIV gboolean sr_vc870_packet_valid(const uint8_t *buf);
SR_PRIV int sr_vc870_parse(const uint8_t *buf, float *floatval,
		struct sr_datafeed_analog *analog, void *info);

/*--- hardware/lcr/es51919.c ------------------------------------------------*/

SR_PRIV void es51919_serial_clean(void *priv);
SR_PRIV struct sr_dev_inst *es51919_serial_scan(GSList *options,
						const char *vendor,
						const char *model);
SR_PRIV int es51919_serial_config_get(uint32_t key, GVariant **data,
				      const struct sr_dev_inst *sdi,
				      const struct sr_channel_group *cg);
SR_PRIV int es51919_serial_config_set(uint32_t key, GVariant *data,
				      const struct sr_dev_inst *sdi,
				      const struct sr_channel_group *cg);
SR_PRIV int es51919_serial_config_list(uint32_t key, GVariant **data,
				       const struct sr_dev_inst *sdi,
				       const struct sr_channel_group *cg);
SR_PRIV int es51919_serial_acquisition_start(const struct sr_dev_inst *sdi,
					     void *cb_data);
SR_PRIV int es51919_serial_acquisition_stop(struct sr_dev_inst *sdi,
					    void *cb_data);

/*--- hardware/dmm/ut372.c --------------------------------------------------*/

#define UT372_PACKET_SIZE 27

struct ut372_info {
	int dummy;
};

SR_PRIV gboolean sr_ut372_packet_valid(const uint8_t *buf);
SR_PRIV int sr_ut372_parse(const uint8_t *buf, float *floatval,
		struct sr_datafeed_analog *analog, void *info);

/*--- hardware/scale/kern.c -------------------------------------------------*/

struct kern_info {
	gboolean is_gram, is_carat, is_ounce, is_pound, is_troy_ounce;
	gboolean is_pennyweight, is_grain, is_tael, is_momme, is_tola;
	gboolean is_percentage, is_piece, is_unstable, is_stable, is_error;
	int buflen;
};

SR_PRIV gboolean sr_kern_packet_valid(const uint8_t *buf);
SR_PRIV int sr_kern_parse(const uint8_t *buf, float *floatval,
		struct sr_datafeed_analog *analog, void *info);

#endif
