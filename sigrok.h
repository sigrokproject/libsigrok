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
#ifdef HAVE_LIBUSB_1_0
#include <libusb.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

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
#define SR_OK                 0 /* No error */
#define SR_ERR               -1 /* Generic/unspecified error */
#define SR_ERR_MALLOC        -2 /* Malloc/calloc/realloc error */
#define SR_ERR_SAMPLERATE    -3 /* Incorrect samplerate */

/* limited by uint64_t */
#define MAX_NUM_PROBES 64
#define MAX_PROBENAME_LEN 32

/* Handy little macros */
#define KHZ(n) ((n) * 1000)
#define MHZ(n) ((n) * 1000000)
#define GHZ(n) ((n) * 1000000000)

#define HZ_TO_NS(n) (1000000000 / (n))

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

#ifndef ARRAY_AND_SIZE
#define ARRAY_AND_SIZE(a) (a), ARRAY_SIZE(a)
#endif

typedef int (*sr_receive_data_callback) (int fd, int revents, void *user_data);

/* Data types used by hardware plugins for set_configuration() */
enum {
	SR_T_UINT64,
	SR_T_CHAR,
	SR_T_NULL,
};

enum {
	SR_PROTO_RAW,
};

/* (Unused) protocol decoder stack entry */
struct protocol {
	char *name;
	int id;
	int stackindex;
};

/* sr_datafeed_packet.type values */
enum {
	SR_DF_HEADER,
	SR_DF_END,
	SR_DF_TRIGGER,
	SR_DF_LOGIC,
	SR_DF_ANALOG,
	SR_DF_PD,
};

struct sr_datafeed_packet {
	uint16_t type;
	uint64_t length;
	uint16_t unitsize;
	void *payload;
};

struct sr_datafeed_header {
	int feed_version;
	struct timeval starttime;
	uint64_t samplerate;
	int protocol_id;
	int num_analog_probes;
	int num_logic_probes;
};

struct analog_probe {
	uint8_t att;
	uint8_t res;	/* Needs to be a power of 2, FIXME */
	uint16_t val;	/* Max hardware ADC width is 16bits */
};

struct analog_sample {
	uint8_t num_probes; /* Max hardware probes is 256 */
	struct analog_probe probes[];
};

struct sr_input {
	struct sr_input_format *format;
	char *param;
	struct sr_device *vdevice;
};

struct sr_input_format {
	char *extension;
	char *description;
	int (*format_match) (const char *filename);
	int (*init) (struct sr_input *in);
	int (*loadfile) (struct sr_input *in, const char *filename);
};

struct sr_output {
	struct sr_output_format *format;
	struct sr_device *device;
	char *param;
	void *internal;
};

struct sr_output_format {
	char *extension;
	char *description;
	int df_type;
	int (*init) (struct sr_output *o);
	int (*data) (struct sr_output *o, char *data_in, uint64_t length_in,
		     char **data_out, uint64_t *length_out);
	int (*event) (struct sr_output *o, int event_type, char **data_out,
		      uint64_t *length_out);
};

struct analyzer {
	char *name;
	char *filename;
	/*
	 * TODO: Parameters? If so, configured plugins need another struct.
	 * TODO: Input and output format?
	 */
};

/* Size of a chunk in units */
#define DATASTORE_CHUNKSIZE 512000

struct sr_datastore {
	/* Size in bytes of the number of units stored in this datastore */
	int ds_unitsize;
	unsigned int num_units; /* TODO: uint64_t */
	GSList *chunklist;
};

/*
 * This represents a generic device connected to the system.
 * For device-specific information, ask the plugin. The plugin_index refers
 * to the device index within that plugin; it may be handling more than one
 * device. All relevant plugin calls take a device_index parameter for this.
 */
struct sr_device {
	/* Which plugin handles this device */
	struct sr_device_plugin *plugin;
	/* A plugin may handle multiple devices of the same type */
	int plugin_index;
	/* List of struct sr_probe* */
	GSList *probes;
	/* Data acquired by this device, if any */
	struct sr_datastore *datastore;
};

enum {
	SR_PROBE_TYPE_LOGIC,
	SR_PROBE_TYPE_ANALOG,
};

struct sr_probe {
	int index;
	int type;
	gboolean enabled;
	char *name;
	char *trigger;
};

extern GSList *devices;

/* Hardware plugin capabilities */
enum {
	SR_HWCAP_DUMMY,             /* Used to terminate lists */
	/* device classes */
	SR_HWCAP_LOGIC_ANALYZER,

	/* device options */
	SR_HWCAP_SAMPLERATE,        /* Change samplerate */
	SR_HWCAP_PROBECONFIG,       /* Configure probe mask */
	SR_HWCAP_CAPTURE_RATIO,     /* Set pre/post-trigger capture ratio */
	SR_HWCAP_PATTERN_MODE,      /* Pattern generator mode */

	/* special stuff */
	SR_HWCAP_CAPTUREFILE,       /* capturefile to inject */
	SR_HWCAP_CAPTURE_UNITSIZE,  /* unitsize of capturefile data */
	SR_HWCAP_CAPTURE_NUM_PROBES,/* set number of probes */

	/* acquisition modes */
	SR_HWCAP_LIMIT_MSEC,        /* Set a time limit for sample acquisition */
	SR_HWCAP_LIMIT_SAMPLES,     /* Set a limit on number of samples */
	SR_HWCAP_CONTINUOUS,
};

struct sr_hwcap_option {
	int capability;
	int type;
	char *description;
	char *shortname;
};

struct sr_device_instance {
	int index;
	int status;
	int instance_type;
	char *vendor;
	char *model;
	char *version;
	void *priv;
	union {
		struct sr_usb_device_instance *usb;
		struct sr_serial_device_instance *serial;
	};
};

/* sr_device_instance types */
enum {
	SR_USB_INSTANCE,
	SR_SERIAL_INSTANCE,
};

#ifdef HAVE_LIBUSB_1_0
struct sr_usb_device_instance {
	uint8_t bus;
	uint8_t address;
	struct libusb_device_handle *devhdl;
};
#endif

struct sr_serial_device_instance {
	char *port;
	int fd;
};

/* Device instance status */
enum {
	SR_ST_NOT_FOUND,
	/* Found, but still booting */
	SR_ST_INITIALIZING,
	/* Live, but not in use */
	SR_ST_INACTIVE,
	/* Actively in use in a session */
	SR_ST_ACTIVE,
};

/*
 * TODO: This sucks, you just kinda have to "know" the returned type.
 * TODO: Need a DI to return the number of trigger stages supported.
 */

/* Device info IDs */
enum {
	/* struct sr_device_instance for this specific device */
	SR_DI_INSTANCE,
	/* The number of probes connected to this device */
	SR_DI_NUM_PROBES,
	/* Samplerates supported by this device, (struct sr_samplerates) */
	SR_DI_SAMPLERATES,
	/* Types of trigger supported, out of "01crf" (char *) */
	SR_DI_TRIGGER_TYPES,
	/* The currently set samplerate in Hz (uint64_t) */
	SR_DI_CUR_SAMPLERATE,
	/* Supported pattern generator modes */
	SR_DI_PATTERNMODES,
};

/*
 * A device supports either a range of samplerates with steps of a given
 * granularity, or is limited to a set of defined samplerates. Use either
 * step or list, but not both.
 */
struct sr_samplerates {
	uint64_t low;
	uint64_t high;
	uint64_t step;
	uint64_t *list;
};

struct sr_device_plugin {
	/* Plugin-specific */
	char *name;
	char *longname;
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

struct sr_session {
	/* List of struct sr_device* */
	GSList *devices;
	/* List of struct analyzer* */
	GSList *analyzers;
	/* list of sr_receive_data_callback */
	GSList *datafeed_callbacks;
	GTimeVal starttime;
	gboolean running;
};

#include "sigrok-proto.h"

#ifdef __cplusplus
}
#endif

#endif
