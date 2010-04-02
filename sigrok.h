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

/* Returned status/error codes */
#define SIGROK_STATUS_DISABLED		0
#define SIGROK_OK			1
#define SIGROK_NOK			2
#define SIGROK_ERR_BADVALUE		20

/* Handy little macros */
#define KHZ(n) (n * 1000)
#define MHZ(n) (n * 1000000)
#define GHZ(n) (n * 1000000000)

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
 * datafeed
 */

/* datafeed_packet.type values */
enum {
	DF_HEADER,
	DF_END,
	DF_TRIGGER,
	DF_LOGIC8,
	DF_LOGIC16,
	DF_LOGIC24,
	DF_LOGIC32,
	DF_LOGIC48,
	DF_LOGIC64,
};

struct datafeed_packet {
	uint16_t type;
	uint16_t length;
	void *payload;
};

struct datafeed_header {
	int feed_version;
	struct timeval starttime;
	uint64_t rate;
	int protocol_id;
	int num_probes;
};

/*
 * output
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
	void (*init) (struct output *o);
	int (*data) (struct output *o, char *data_in, uint64_t length_in,
		     char **data_out, uint64_t *length_out);
	int (*event) (struct output *o, int event_type, char **data_out,
		      uint64_t *length_out);
};

struct output_format **output_list(void);
int filter_probes(int in_unitsize, int out_unitsize, int *probelist,
		  char *data_in, uint64_t length_in, char **data_out,
		  uint64_t *length_out);

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
	unsigned int num_units;
	GSList *chunklist;
};

struct datastore *datastore_new(int unitsize);
void datastore_destroy(struct datastore *ds);
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
struct device *device_new(struct device_plugin *plugin, int plugin_index);
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
	HWCAP_DUMMY,		// used to terminate lists
	HWCAP_LOGIC_ANALYZER,
	HWCAP_SAMPLERATE,       // change sample rate
	HWCAP_PROBECONFIG,      // configure probe mask
	HWCAP_CAPTURE_RATIO,    // set pre-trigger / post-trigger ratio
	HWCAP_LIMIT_MSEC,	// set a time limit for sample acquisition
	HWCAP_LIMIT_SAMPLES,	// set a limit on number of samples
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
	/* Sample rates supported by this device, (struct samplerates) */
	DI_SAMPLERATES,
	/* Types of trigger supported, out of "01crf" (char *) */
	DI_TRIGGER_TYPES,
	/* The currently set sample rate in Hz (uint64_t) */
	DI_CUR_SAMPLE_RATE,
};

/* a device supports either a range of samplerates with steps of a given
 * granularity, or is limited to a set of defined samplerates. use either
 * step or list, but not both.
 */
struct samplerates {
	uint64_t low;
	uint64_t high;
	uint64_t step;
	uint64_t *list;
};

struct device_plugin {
	/* plugin-specific */
	char *name;
	int api_version;
	int (*init) (char *deviceinfo);
	void (*cleanup) (void);

	/* device-specific */
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
	int status, char *vendor, char *model, char *version);
struct sigrok_device_instance *get_sigrok_device_instance(GSList *device_instances, int device_index);
void sigrok_device_instance_free(struct sigrok_device_instance *sdi);

/* USB-specific instances */
struct usb_device_instance *usb_device_instance_new(uint8_t bus,
		uint8_t address, struct libusb_device_handle *hdl);
void usb_device_instance_free(struct usb_device_instance *usb);

/* Serial-specific instances */
struct serial_device_instance *serial_device_instance_new(char *port, int fd);
void serial_device_instance_free(struct serial_device_instance *serial);

int find_hwcap(int *capabilities, int hwcap);
struct hwcap_option *find_hwcap_option(int hwcap);
void source_remove(int fd);
void source_add(int fd, int events, int timeout, receive_data_callback rcv_cb, void *user_data);

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
	/* datafeed callbacks */
	GSList *datafeed_callbacks;
	GTimeVal starttime;
};

/* Session setup */
struct session *session_load(char *filename);
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

GSList *list_serial_ports(void);

#endif
