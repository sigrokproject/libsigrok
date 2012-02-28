/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2010-2012 Bert Vermeulen <bert@biot.com>
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

#ifndef LIBSIGROK_SIGROK_H
#define LIBSIGROK_SIGROK_H

#include <stdio.h>
#include <sys/time.h>
#include <stdint.h>
#include <inttypes.h>
#include <glib.h>

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
#define SR_ERR_ARG           -3 /* Function argument error */
#define SR_ERR_BUG           -4 /* Errors hinting at internal bugs */
#define SR_ERR_SAMPLERATE    -5 /* Incorrect samplerate */

#define SR_MAX_NUM_PROBES    64 /* Limited by uint64_t. */
#define SR_MAX_PROBENAME_LEN 32

/* Handy little macros */
#define SR_HZ(n)  (n)
#define SR_KHZ(n) ((n) * 1000)
#define SR_MHZ(n) ((n) * 1000000)
#define SR_GHZ(n) ((n) * 1000000000)

#define SR_HZ_TO_NS(n) (1000000000 / (n))

/* libsigrok loglevels. */
#define SR_LOG_NONE           0 /**< Output no messages at all. */
#define SR_LOG_ERR            1 /**< Output error messages. */
#define SR_LOG_WARN           2 /**< Output warnings. */
#define SR_LOG_INFO           3 /**< Output informational messages. */
#define SR_LOG_DBG            4 /**< Output debug messages. */
#define SR_LOG_SPEW           5 /**< Output very noisy debug messages. */

/*
 * Use SR_API to mark public API symbols, and SR_PRIV for private symbols.
 *
 * Variables and functions marked 'static' are private already and don't
 * need SR_PRIV. However, functions which are not static (because they need
 * to be used in other libsigrok-internal files) but are also not meant to
 * be part of the public libsigrok API, must use SR_PRIV.
 *
 * This uses the 'visibility' feature of gcc (requires gcc >= 4.0).
 *
 * Details: http://gcc.gnu.org/wiki/Visibility
 */

/* Marks public libsigrok API symbols. */
#define SR_API __attribute__((visibility("default")))

/* Marks private, non-public libsigrok symbols (not part of the API). */
#define SR_PRIV __attribute__((visibility("hidden")))

typedef int (*sr_receive_data_callback) (int fd, int revents, void *user_data);

/* Data types used by hardware drivers for dev_config_set() */
enum {
	SR_T_UINT64,
	SR_T_CHAR,
	SR_T_BOOL,
};

/* sr_datafeed_packet.type values */
enum {
	SR_DF_HEADER,
	SR_DF_END,
	SR_DF_TRIGGER,
	SR_DF_LOGIC,
	SR_DF_PD,
};

struct sr_datafeed_packet {
	uint16_t type;
	void *payload;
};

struct sr_datafeed_header {
	int feed_version;
	struct timeval starttime;
	uint64_t samplerate;
	int num_logic_probes;
};

struct sr_datafeed_logic {
	uint64_t length;
	uint16_t unitsize;
	void *data;
};

struct sr_input {
	struct sr_input_format *format;
	char *param;
	struct sr_dev *vdev;
};

struct sr_input_format {
	char *id;
	char *description;
	int (*format_match) (const char *filename);
	int (*init) (struct sr_input *in);
	int (*loadfile) (struct sr_input *in, const char *filename);
};

struct sr_output {
	struct sr_output_format *format;
	struct sr_dev *dev;
	char *param;
	void *internal;
};

struct sr_output_format {
	char *id;
	char *description;
	int df_type;
	int (*init) (struct sr_output *o);
	int (*data) (struct sr_output *o, const char *data_in,
		     uint64_t length_in, char **data_out, uint64_t *length_out);
	int (*event) (struct sr_output *o, int event_type, char **data_out,
		      uint64_t *length_out);
};

struct sr_datastore {
	/* Size in bytes of the number of units stored in this datastore */
	int ds_unitsize;
	unsigned int num_units; /* TODO: uint64_t */
	GSList *chunklist;
};

/*
 * This represents a generic device connected to the system.
 * For device-specific information, ask the driver. The driver_index refers
 * to the device index within that driver; it may be handling more than one
 * device. All relevant driver calls take a dev_index parameter for this.
 */
struct sr_dev {
	/* Which driver handles this device */
	struct sr_dev_driver *driver;
	/* A driver may handle multiple devices of the same type */
	int driver_index;
	/* List of struct sr_probe* */
	GSList *probes;
	/* Data acquired by this device, if any */
	struct sr_datastore *datastore;
};

enum {
	SR_PROBE_TYPE_LOGIC,
};

struct sr_probe {
	int index;
	int type;
	gboolean enabled;
	char *name;
	char *trigger;
};

/* Hardware driver capabilities */
enum {
	SR_HWCAP_DUMMY = 0, /* Used to terminate lists. Must be 0! */

	/*--- Device classes ------------------------------------------------*/

	/** The device can act as logic analyzer. */
	SR_HWCAP_LOGIC_ANALYZER,

	/* TODO: SR_HWCAP_SCOPE, SW_HWCAP_PATTERN_GENERATOR, etc.? */

	/*--- Device types --------------------------------------------------*/

	/** The device is demo device. */
	SR_HWCAP_DEMO_DEV,

	/*--- Device options ------------------------------------------------*/

	/** The device supports setting/changing its samplerate. */
	SR_HWCAP_SAMPLERATE,

	/* TODO: Better description? Rename to PROBE_AND_TRIGGER_CONFIG? */
	/** The device supports setting a probe mask. */
	SR_HWCAP_PROBECONFIG,

	/** The device supports setting a pre/post-trigger capture ratio. */
	SR_HWCAP_CAPTURE_RATIO,

	/* TODO? */
	/** The device supports setting a pattern (pattern generator mode). */
	SR_HWCAP_PATTERN_MODE,

	/** The device supports Run Length Encoding. */
	SR_HWCAP_RLE,

	/*--- Special stuff -------------------------------------------------*/

	/* TODO: Better description. */
	/** The device supports specifying a capturefile to inject. */
	SR_HWCAP_CAPTUREFILE,

	/* TODO: Better description. */
	/** The device supports specifying the capturefile unit size. */
	SR_HWCAP_CAPTURE_UNITSIZE,

	/* TODO: Better description. */
	/** The device supports setting the number of probes. */
	SR_HWCAP_CAPTURE_NUM_PROBES,

	/*--- Acquisition modes ---------------------------------------------*/

	/**
	 * The device supports setting a sample time limit, i.e. how long the
	 * sample acquisition should run (in ms).
	 */
	SR_HWCAP_LIMIT_MSEC,

	/**
	 * The device supports setting a sample number limit, i.e. how many
	 * samples should be acquired.
	 */
	SR_HWCAP_LIMIT_SAMPLES,

	/**
	 * The device supports continuous sampling, i.e. neither a time limit
	 * nor a sample number limit has to be supplied, it will just acquire
	 * samples continuously, until explicitly stopped by a certain command.
	 */
	SR_HWCAP_CONTINUOUS,

	/* TODO: SR_HWCAP_JUST_SAMPLE or similar. */
};

struct sr_hwcap_option {
	int hwcap;
	int type;
	char *description;
	char *shortname;
};

struct sr_dev_inst {
	int index;
	int status;
	int inst_type;
	char *vendor;
	char *model;
	char *version;
	void *priv;
};

/* sr_dev_inst types */
enum {
	SR_USB_INST,
	SR_SERIAL_INST,
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
	/* struct sr_dev_inst for this specific device */
	SR_DI_INST,
	/* The number of probes connected to this device */
	SR_DI_NUM_PROBES,
	/* The probe names on this device */
	SR_DI_PROBE_NAMES,
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

struct sr_dev_driver {
	/* Driver-specific */
	char *name;
	char *longname;
	int api_version;
	int (*init) (const char *devinfo);
	int (*cleanup) (void);

	/* Device-specific */
	int (*dev_open) (int dev_index);
	int (*dev_close) (int dev_index);
	void *(*dev_info_get) (int dev_index, int dev_info_id);
	int (*dev_status_get) (int dev_index);
	int *(*hwcap_get_all) (void);
	int (*dev_config_set) (int dev_index, int hwcap, void *value);
	int (*dev_acquisition_start) (int dev_index, gpointer session_dev_id);
	int (*dev_acquisition_stop) (int dev_index, gpointer session_dev_id);
};

struct sr_session {
	/* List of struct sr_dev* */
	GSList *devs;
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
