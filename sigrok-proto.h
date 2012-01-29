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
int sr_exit(void);

/*--- log.c -----------------------------------------------------------------*/

int sr_set_loglevel(int loglevel);
int sr_get_loglevel(void);

/*--- datastore.c -----------------------------------------------------------*/

int sr_datastore_new(int unitsize, struct sr_datastore **ds);
int sr_datastore_destroy(struct sr_datastore *ds);
int sr_datastore_put(struct sr_datastore *ds, void *data, unsigned int length,
		     int in_unitsize, int *probelist);

/*--- device.c --------------------------------------------------------------*/

int sr_device_scan(void);
GSList *sr_device_list(void);
struct sr_device *sr_device_new(const struct sr_device_plugin *plugin,
				int plugin_index);
int sr_device_clear(struct sr_device *device);
int sr_device_probe_clear(struct sr_device *device, int probenum);
int sr_device_probe_add(struct sr_device *device, const char *name);
struct sr_probe *sr_device_probe_find(const struct sr_device *device,
				      int probenum);
int sr_device_probe_name(struct sr_device *device, int probenum,
			 const char *name);
int sr_device_trigger_clear(struct sr_device *device);
int sr_device_trigger_set(struct sr_device *device, int probenum,
			  const char *trigger);
gboolean sr_device_has_hwcap(const struct sr_device *device, int hwcap);
int sr_device_get_info(const struct sr_device *device, int id,
					   const void **data);

/*--- filter.c --------------------------------------------------------------*/

int sr_filter_probes(int in_unitsize, int out_unitsize, const int *probelist,
		     const unsigned char *data_in, uint64_t length_in,
		     char **data_out, uint64_t *length_out);

/*--- hwplugin.c ------------------------------------------------------------*/

GSList *sr_list_hwplugins(void);
int sr_init_hwplugins(struct sr_device_plugin *plugin);
void sr_cleanup_hwplugins(void);

/* Generic device instances */
struct sr_device_instance *sr_device_instance_new(int index,
       int status, const char *vendor, const char *model, const char *version);
struct sr_device_instance *sr_get_device_instance(GSList *device_instances,
						  int device_index);
void sr_device_instance_free(struct sr_device_instance *sdi);

int sr_find_hwcap(int *capabilities, int hwcap);
struct sr_hwcap_option *sr_find_hwcap_option(int hwcap);
void sr_source_remove(int fd);
void sr_source_add(int fd, int events, int timeout,
		   sr_receive_data_callback rcv_cb, void *user_data);

/*--- session.c -------------------------------------------------------------*/

typedef void (*sr_datafeed_callback) (struct sr_device *device,
				      struct sr_datafeed_packet *packet);

/* Session setup */
int sr_session_load(const char *filename);
struct sr_session *sr_session_new(void);
int sr_session_destroy(void);
int sr_session_device_clear(void);
int sr_session_device_add(struct sr_device *device);

/* Datafeed setup */
int sr_session_datafeed_callback_clear(void);
int sr_session_datafeed_callback_add(sr_datafeed_callback callback);

/* Session control */
int sr_session_start(void);
int sr_session_run(void);
int sr_session_halt(void);
int sr_session_stop(void);
int sr_session_bus(struct sr_device *device, struct sr_datafeed_packet *packet);
int sr_session_save(const char *filename);
int sr_session_source_add(int fd, int events, int timeout,
	        sr_receive_data_callback callback, void *user_data);
int sr_session_source_remove(int fd);

/*--- input/input.c ---------------------------------------------------------*/

struct sr_input_format **sr_input_list(void);

/*--- output/output.c -------------------------------------------------------*/

struct sr_output_format **sr_output_list(void);

/*--- output/common.c -------------------------------------------------------*/

char *sr_samplerate_string(uint64_t samplerate);
char *sr_period_string(uint64_t frequency);
char **sr_parse_triggerstring(struct sr_device *device,
			      const char *triggerstring);
int sr_parse_sizestring(const char *sizestring, uint64_t *size);
uint64_t sr_parse_timestring(const char *timestring);
gboolean sr_parse_boolstring(const char *boolstring);

#endif
