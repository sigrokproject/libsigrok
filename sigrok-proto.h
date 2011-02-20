/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2011 Bert Vermeulen <bert@biot.com>
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

#ifndef SIGROK_SIGROK_PROTO_H
#define SIGROK_SIGROK_PROTO_H

/*--- backend.c -------------------------------------------------------------*/

int sr_init(void);
void sr_exit(void);

/*--- datastore.c -----------------------------------------------------------*/

int sr_datastore_new(int unitsize, struct sr_datastore **ds);
int sr_datastore_destroy(struct sr_datastore *ds);
void sr_datastore_put(struct sr_datastore *ds, void *data, unsigned int length,
		      int in_unitsize, int *probelist);

/*--- device.c --------------------------------------------------------------*/

void sr_device_scan(void);
int sr_device_plugin_init(struct sr_device_plugin *plugin);
void sr_device_close_all(void);
GSList *sr_device_list(void);
struct sr_device *sr_device_new(struct sr_device_plugin *plugin,
				int plugin_index, int num_probes);
void sr_device_clear(struct sr_device *device);
void sr_device_destroy(struct sr_device *dev);

void sr_device_probe_clear(struct sr_device *device, int probenum);
void sr_device_probe_add(struct sr_device *device, char *name);
struct sr_probe *sr_device_probe_find(struct sr_device *device, int probenum);
void sr_device_probe_name(struct sr_device *device, int probenum, char *name);

void sr_device_trigger_clear(struct sr_device *device);
void sr_device_trigger_set(struct sr_device *device, int probenum,
			   char *trigger);
gboolean sr_device_has_hwcap(struct sr_device *device, int hwcap);

/*--- filter.c --------------------------------------------------------------*/

int filter_probes(int in_unitsize, int out_unitsize, int *probelist,
		  char *data_in, uint64_t length_in, char **data_out,
		  uint64_t *length_out);

/*--- hwplugin.c ------------------------------------------------------------*/

int load_hwplugins(void);
GSList *list_hwplugins(void);

/* Generic device instances */
struct sr_device_instance *sr_device_instance_new(int index,
       int status, const char *vendor, const char *model, const char *version);
struct sr_device_instance *sr_get_device_instance(GSList *device_instances,
						  int device_index);
void sr_device_instance_free(struct sr_device_instance *sdi);

/* USB-specific instances */
#ifdef HAVE_LIBUSB_1_0
struct sr_usb_device_instance *sr_usb_device_instance_new(uint8_t bus,
		uint8_t address, struct libusb_device_handle *hdl);
void sr_usb_device_instance_free(struct sr_usb_device_instance *usb);
#endif

/* Serial-specific instances */
struct sr_serial_device_instance *sr_serial_device_instance_new(
					const char *port, int fd);
void sr_serial_device_instance_free(struct sr_serial_device_instance *serial);

int sr_find_hwcap(int *capabilities, int hwcap);
struct sr_hwcap_option *sr_find_hwcap_option(int hwcap);
void sr_source_remove(int fd);
void sr_source_add(int fd, int events, int timeout,
		   receive_data_callback rcv_cb, void *user_data);

/*--- session.c -------------------------------------------------------------*/

typedef void (*source_callback_remove) (int fd);
typedef void (*source_callback_add) (int fd, int events, int timeout,
		receive_data_callback callback, void *user_data);
typedef void (*datafeed_callback) (struct sr_device *device,
				 struct sr_datafeed_packet *packet);

/* Session setup */
int sr_session_load(const char *filename);
struct sr_session *sr_session_new(void);
void sr_session_destroy(void);
void sr_session_device_clear(void);
int sr_session_device_add(struct sr_device *device);

/* Protocol analyzers setup */
void sr_session_pa_clear(void);
void sr_session_pa_add(struct analyzer *pa);

/* Datafeed setup */
void sr_session_datafeed_callback_clear(void);
void sr_session_datafeed_callback_add(datafeed_callback callback);

/* Session control */
int sr_session_start(void);
void sr_session_run(void);
void sr_session_halt(void);
void sr_session_stop(void);
void sr_session_bus(struct sr_device *device,
		    struct sr_datafeed_packet *packet);
int sr_session_save(char *filename);
void sr_session_source_add(int fd, int events, int timeout,
	        receive_data_callback callback, void *user_data);
void sr_session_source_remove(int fd);

/*--- input/input.c ---------------------------------------------------------*/

struct sr_input_format **sr_input_list(void);

/*--- output/output.c -------------------------------------------------------*/

struct sr_output_format **sr_output_list(void);

/*--- output/common.c -------------------------------------------------------*/

char *sr_samplerate_string(uint64_t samplerate);
char *sr_period_string(uint64_t frequency);
char **sr_parse_triggerstring(struct sr_device *device,
			      const char *triggerstring);
uint64_t sr_parse_sizestring(const char *sizestring);
uint64_t sr_parse_timestring(const char *timestring);

#endif
