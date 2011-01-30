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
void sr_cleanup(void);

/*--- datastore.c -----------------------------------------------------------*/

int datastore_new(int unitsize, struct datastore **ds);
int datastore_destroy(struct datastore *ds);
void datastore_put(struct datastore *ds, void *data, unsigned int length,
		   int in_unitsize, int *probelist);

/*--- device.c --------------------------------------------------------------*/

void device_scan(void);
int device_plugin_init(struct sr_device_plugin *plugin);
void device_close_all(void);
GSList *device_list(void);
struct sr_device *device_new(struct sr_device_plugin *plugin, int plugin_index,
			     int num_probes);
void device_clear(struct sr_device *device);
void device_destroy(struct sr_device *dev);

void device_probe_clear(struct sr_device *device, int probenum);
void device_probe_add(struct sr_device *device, char *name);
struct probe *probe_find(struct sr_device *device, int probenum);
void device_probe_name(struct sr_device *device, int probenum, char *name);

void device_trigger_clear(struct sr_device *device);
void device_trigger_set(struct sr_device *device, int probenum, char *trigger);

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
typedef void (*datafeed_callback) (struct sr_device *device,
				 struct sr_datafeed_packet *packet);

/* Session setup */
struct session *session_load(const char *filename);
struct session *session_new(void);
void session_destroy(void);
void session_device_clear(void);
int session_device_add(struct sr_device *device);

/* Protocol analyzers setup */
void session_pa_clear(void);
void session_pa_add(struct analyzer *pa);

/* Datafeed setup */
void session_datafeed_callback_clear(void);
void session_datafeed_callback_add(datafeed_callback callback);

/* Session control */
int session_start(void);
void session_run(void);
void session_halt(void);
void session_stop(void);
void session_bus(struct sr_device *device, struct sr_datafeed_packet *packet);
void make_metadata(char *filename);
int session_save(char *filename);
void session_source_add(int fd, int events, int timeout,
	        receive_data_callback callback, void *user_data);
void session_source_remove(int fd);

/*--- input/input.c ---------------------------------------------------------*/

struct sr_input_format **sr_input_list(void);

/*--- output/output.c -------------------------------------------------------*/

struct sr_output_format **sr_output_list(void);

/*--- output/common.c -------------------------------------------------------*/

char *sr_samplerate_string(uint64_t samplerate);
char *sr_period_string(uint64_t frequency);

#endif
