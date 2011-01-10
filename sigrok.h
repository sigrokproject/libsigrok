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

#ifndef SIGROK_SIGROK_H
#define SIGROK_SIGROK_H

#include <stdio.h>
#include <sys/time.h>
#include <stdint.h>
#include <inttypes.h>
#include <glib.h>
#include <libusb.h>

/*
 * Status/error codes returned by libsigrok functions.
 *
 * All possible return codes of libsigrok functions must be listed here.
 * Functions should never return hardcoded numbers as status, but rather
 * use these #defines instead. All error codes are negative numbers.
 *
 * The error codes are globally unique in libsigrok, i.e. if one of the
 * libsigrok functions returns a "malloc error" it must be exactly the same
 * return value as used by all other functions to indicate "malloc error".
 * There must be no functions which indicate two different errors via the
 * same return code.
 *
 * Also, for compatibility reasons, no defined return codes are ever removed
 * or reused for different #defines later. You can only add new #defines and
 * return codes, but never remove or redefine existing ones.
 */
#define SIGROK_OK			 0 /* No error */
#define SIGROK_ERR			-1 /* Generic/unspecified error */
#define SIGROK_ERR_MALLOC		-2 /* Malloc/calloc/realloc error */
#define SIGROK_ERR_SAMPLERATE		-3 /* Incorrect samplerate */

/* limited by uint64_t */
#define MAX_NUM_PROBES 64
#define MAX_PROBENAME_LEN 32


/* Handy little macros */
#define KHZ(n) ((n) * 1000)
#define MHZ(n) ((n) * 1000000)
#define GHZ(n) ((n) * 1000000000)

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

#ifndef ARRAY_AND_SIZE
#define ARRAY_AND_SIZE(a) (a), ARRAY_SIZE(a)
#endif

/* Data types, used by hardware plugins for set_configuration() */
enum {
	T_UINT64,
	T_CHAR,
};

enum {
	PROTO_RAW,
};

/* (Unused) protocol decoder stack entry */
struct protocol {
	char *name;
	int id;
	int stackindex;
};

/*
 * Datafeed
 */

/* datafeed_packet.type values */
enum {
	DF_HEADER,
	DF_END,
	DF_TRIGGER,
	DF_LOGIC,
	DF_ANALOG,
	DF_PD,
};

struct datafeed_packet {
	uint16_t type;
	uint64_t length;
	uint16_t unitsize;
	void *payload;
};

struct datafeed_header {
	int feed_version;
	struct timeval starttime;
	uint64_t samplerate;
	int protocol_id;
	int num_probes;
};

/*
 * Input
 */
struct input {
	struct input_format *format;
	void *param;
	void *internal;
};

struct input_format {
	char *extension;
	char *description;
	int (*format_match) (const char *filename);
	int (*in_loadfile) (const char *filename);
};

struct input_format **input_list(void);


/*
 * Output
 */
struct output {
	struct output_format *format;
	struct device *device;
	char *param;
	void *internal;
};

struct output_format {
	char *extension;
	char *description;
	int df_type;
	int (*init) (struct output *o);
	int (*data) (struct output *o, char *data_in, uint64_t length_in,
		     char **data_out, uint64_t *length_out);
	int (*event) (struct output *o, int event_type, char **data_out,
		      uint64_t *length_out);
};

struct output_format **output_list(void);


int filter_probes(int in_unitsize, int out_unitsize, int *probelist,
		  char *data_in, uint64_t length_in, char **data_out,
		  uint64_t *length_out);

char *sigrok_samplerate_string(uint64_t samplerate);
char *sigrok_period_string(uint64_t frequency);

/*--- analyzer.c ------------------------------------------------------------*/

struct analyzer {
	char *name;
	char *filename;
	/*
	 * TODO: Parameters? If so, configured plugins need another struct.
	 * TODO: Input and output format?
	 */
};

/*--- backend.c -------------------------------------------------------------*/

int sigrok_init(void);
void sigrok_cleanup(void);

/*--- datastore.c -----------------------------------------------------------*/

/* Size of a chunk in units */
#define DATASTORE_CHUNKSIZE 512000

struct datastore {
	/* Size in bytes of the number of units stored in this datastore */
	int ds_unitsize;
	unsigned int num_units; /* TODO: uint64_t */
	GSList *chunklist;
};

int datastore_new(int unitsize, struct datastore **ds);
int datastore_destroy(struct datastore *ds);
void datastore_put(struct datastore *ds, void *data, unsigned int length,
		   int in_unitsize, int *probelist);

/*--- debug.c ---------------------------------------------------------------*/

void hexdump(unsigned char *address, int length);

/*--- device.c --------------------------------------------------------------*/

/*
 * This represents a generic device connected to the system.
 * For device-specific information, ask the plugin. The plugin_index refers
 * to the device index within that plugin; it may be handling more than one
 * device. All relevant plugin calls take a device_index parameter for this.
 */
struct device {
	/* Which plugin handles this device */
	struct device_plugin *plugin;
	/* A plugin may handle multiple devices of the same type */
	int plugin_index;
	/* List of struct probe* */
	GSList *probes;
	/* Data acquired by this device, if any */
	struct datastore *datastore;
};

struct probe {
	int index;
	gboolean enabled;
	char *name;
	char *trigger;
};

extern GSList *devices;

void device_scan(void);
void device_close_all(void);
GSList *device_list(void);
struct device *device_new(struct device_plugin *plugin, int plugin_index, int num_probes);
void device_clear(struct device *device);
void device_destroy(struct device *dev);

void device_probe_clear(struct device *device, int probenum);
void device_probe_add(struct device *device, char *name);
struct probe *probe_find(struct device *device, int probenum);
void device_probe_name(struct device *device, int probenum, char *name);

void device_trigger_clear(struct device *device);
void device_trigger_set(struct device *device, int probenum, char *trigger);

/*--- hwplugin.c ------------------------------------------------------------*/

/* Hardware plugin capabilities */
enum {
	HWCAP_DUMMY,		/* Used to terminate lists */
	HWCAP_LOGIC_ANALYZER,
	HWCAP_SAMPLERATE,	/* Change samplerate */
	HWCAP_PROBECONFIG,	/* Configure probe mask */
	HWCAP_CAPTURE_RATIO,	/* Set pre/post-trigger capture ratio */
	HWCAP_LIMIT_MSEC,	/* Set a time limit for sample acquisition */
	HWCAP_LIMIT_SAMPLES,	/* Set a limit on number of samples */
};

struct hwcap_option {
	int capability;
	int type;
	char *description;
	char *shortname;
};

struct sigrok_device_instance {
	int index;
	int status;
	int instance_type;
	char *vendor;
	char *model;
	char *version;
	void *priv;
	union {
		struct usb_device_instance *usb;
		struct serial_device_instance *serial;
	};
};

/* sigrok_device_instance types */
enum {
	USB_INSTANCE,
	SERIAL_INSTANCE,
};

struct usb_device_instance {
	uint8_t bus;
	uint8_t address;
	struct libusb_device_handle *devhdl;
};

struct serial_device_instance {
	char *port;
	int fd;
};

/* Device instance status */
enum {
	ST_NOT_FOUND,
	/* Found, but still booting */
	ST_INITIALIZING,
	/* Live, but not in use */
	ST_INACTIVE,
	/* Actively in use in a session */
	ST_ACTIVE,
};

/*
 * TODO: This sucks, you just kinda have to "know" the returned type.
 * TODO: Need a DI to return the number of trigger stages supported.
 */

/* Device info IDs */
enum {
	/* struct sigrok_device_instance for this specific device */
	DI_INSTANCE,
	/* The number of probes connected to this device */
	DI_NUM_PROBES,
	/* Samplerates supported by this device, (struct samplerates) */
	DI_SAMPLERATES,
	/* Types of trigger supported, out of "01crf" (char *) */
	DI_TRIGGER_TYPES,
	/* The currently set samplerate in Hz (uint64_t) */
	DI_CUR_SAMPLERATE,
};

/*
 * A device supports either a range of samplerates with steps of a given
 * granularity, or is limited to a set of defined samplerates. Use either
 * step or list, but not both.
 */
struct samplerates {
	uint64_t low;
	uint64_t high;
	uint64_t step;
	uint64_t *list;
};

struct device_plugin {
	/* Plugin-specific */
	char *name;
	int api_version;
	int (*init) (char *deviceinfo);
	void (*cleanup) (void);

	/* Device-specific */
	int (*open) (int device_index);
	void (*close) (int device_index);
	void *(*get_device_info) (int device_index, int device_info_id);
	int (*get_status) (int device_index);
	int *(*get_capabilities) (void);
	int (*set_configuration) (int device_index, int capability, void *value);
	int (*start_acquisition) (int device_index, gpointer session_device_id);
	void (*stop_acquisition) (int device_index, gpointer session_device_id);
};

struct gsource_fd {
	GSource source;
	GPollFD gpfd;
	/* Not really using this */
	GSource *timeout_source;
};

typedef int (*receive_data_callback) (int fd, int revents, void *user_data);

int load_hwplugins(void);
GSList *list_hwplugins(void);

/* Generic device instances */
struct sigrok_device_instance *sigrok_device_instance_new(int index,
       int status, const char *vendor, const char *model, const char *version);
struct sigrok_device_instance *get_sigrok_device_instance(
			GSList *device_instances, int device_index);
void sigrok_device_instance_free(struct sigrok_device_instance *sdi);

/* USB-specific instances */
struct usb_device_instance *usb_device_instance_new(uint8_t bus,
		uint8_t address, struct libusb_device_handle *hdl);
void usb_device_instance_free(struct usb_device_instance *usb);

/* Serial-specific instances */
struct serial_device_instance *serial_device_instance_new(
					const char *port, int fd);
void serial_device_instance_free(struct serial_device_instance *serial);

int find_hwcap(int *capabilities, int hwcap);
struct hwcap_option *find_hwcap_option(int hwcap);
void source_remove(int fd);
void source_add(int fd, int events, int timeout, receive_data_callback rcv_cb,
		void *user_data);

/*--- session.c -------------------------------------------------------------*/

typedef void (*source_callback_remove) (int fd);
typedef void (*source_callback_add) (int fd, int events, int timeout,
		receive_data_callback callback, void *user_data);
typedef void (*datafeed_callback) (struct device *device,
				 struct datafeed_packet *packet);

struct session {
	/* List of struct device* */
	GSList *devices;
	/* List of struct analyzer* */
	GSList *analyzers;
	/* Datafeed callbacks */
	GSList *datafeed_callbacks;
	GTimeVal starttime;
};

/* Session setup */
struct session *session_load(const char *filename);
struct session *session_new(void);
void session_destroy(void);
void session_device_clear(void);
int session_device_add(struct device *device);

/* Protocol analyzers setup */
void session_pa_clear(void);
void session_pa_add(struct analyzer *pa);

/* Datafeed setup */
void session_datafeed_callback_clear(void);
void session_datafeed_callback_add(datafeed_callback callback);

/* Session control */
int session_start(void);
void session_stop(void);
void session_bus(struct device *device, struct datafeed_packet *packet);
void make_metadata(char *filename);
int session_save(char *filename);

/*--- hwcommon.c ------------------------------------------------------------*/

int ezusb_reset(struct libusb_device_handle *hdl, int set_clear);
int ezusb_install_firmware(libusb_device_handle *hdl, char *filename);
int ezusb_upload_firmware(libusb_device *dev, int configuration,
                          const char *filename);

GSList *list_serial_ports(void);
int serial_open(const char *pathname, int flags);
int serial_close(int fd);
int serial_flush(int fd);
void *serial_backup_params(int fd);
void serial_restore_params(int fd, void *backup);
int serial_set_params(int fd, int speed, int bits, int parity, int stopbits,
		      int flowcontrol);

/* libsigrok/hardware/common/misc.c */
/* TODO: Should not be public. */
int opendev2(int device_index, struct sigrok_device_instance **sdi,
	     libusb_device *dev, struct libusb_device_descriptor *des,
	     int *skip, uint16_t vid, uint16_t pid, int interface);
int opendev3(struct sigrok_device_instance **sdi, libusb_device *dev,
	     struct libusb_device_descriptor *des,
	     uint16_t vid, uint16_t pid, int interface);

#endif
