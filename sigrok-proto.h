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

#ifndef LIBSIGROK_SIGROK_PROTO_H
#define LIBSIGROK_SIGROK_PROTO_H

/*--- backend.c -------------------------------------------------------------*/

SR_API int sr_init(void);
SR_API int sr_exit(void);

/*--- log.c -----------------------------------------------------------------*/

typedef int (*sr_log_handler_t)(void *data, int loglevel, const char *format,
				 va_list args);

SR_API int sr_log_loglevel_set(int loglevel);
SR_API int sr_log_loglevel_get(void);
SR_API int sr_log_handler_set(sr_log_handler_t handler, void *data);
SR_API int sr_log_handler_set_default(void);
SR_API int sr_log_logdomain_set(const char *logdomain);
SR_API char *sr_log_logdomain_get(void);

/*--- datastore.c -----------------------------------------------------------*/

SR_API int sr_datastore_new(int unitsize, struct sr_datastore **ds);
SR_API int sr_datastore_destroy(struct sr_datastore *ds);
SR_API int sr_datastore_put(struct sr_datastore *ds, void *data,
			    unsigned int length, int in_unitsize,
			    int *probelist);

/*--- device.c --------------------------------------------------------------*/

SR_API int sr_dev_scan(void);
SR_API GSList *sr_dev_list(void);
SR_API struct sr_device *sr_dev_new(const struct sr_device_plugin *plugin,
				       int plugin_index);
SR_API int sr_dev_probe_add(struct sr_device *device, const char *name);
SR_API struct sr_probe *sr_dev_probe_find(const struct sr_device *device,
					     int probenum);
SR_API int sr_dev_probe_name(struct sr_device *device, int probenum,
				const char *name);
SR_API int sr_dev_trigger_clear(struct sr_device *device);
SR_API int sr_dev_trigger_set(struct sr_device *device, int probenum,
				 const char *trigger);
SR_API gboolean sr_dev_has_hwcap(const struct sr_device *device, int hwcap);
SR_API int sr_dev_get_info(const struct sr_device *device, int id,
			      const void **data);

/*--- filter.c --------------------------------------------------------------*/

SR_API int sr_filter_probes(int in_unitsize, int out_unitsize,
			    const int *probelist, const unsigned char *data_in,
			    uint64_t length_in, char **data_out,
			    uint64_t *length_out);

/*--- hwplugin.c ------------------------------------------------------------*/

SR_API GSList *sr_list_hwplugins(void);
SR_API int sr_init_hwplugin(struct sr_device_plugin *plugin);
SR_API gboolean sr_has_hwcap(int *capabilities, int hwcap);
SR_API struct sr_hwcap_option *sr_find_hwcap_option(int hwcap);

/*--- session.c -------------------------------------------------------------*/

typedef void (*sr_datafeed_callback) (struct sr_device *device,
				      struct sr_datafeed_packet *packet);

/* Session setup */
SR_API int sr_session_load(const char *filename);
SR_API struct sr_session *sr_session_new(void);
SR_API int sr_session_destroy(void);
SR_API int sr_session_device_clear(void);
SR_API int sr_session_device_add(struct sr_device *device);

/* Datafeed setup */
SR_API int sr_session_datafeed_callback_clear(void);
SR_API int sr_session_datafeed_callback_add(sr_datafeed_callback callback);

/* Session control */
SR_API int sr_session_start(void);
SR_API int sr_session_run(void);
SR_API int sr_session_halt(void);
SR_API int sr_session_stop(void);
SR_API int sr_session_save(const char *filename);
SR_API int sr_session_source_add(int fd, int events, int timeout,
		sr_receive_data_callback callback, void *user_data);
SR_API int sr_session_source_remove(int fd);

/*--- input/input.c ---------------------------------------------------------*/

SR_API struct sr_input_format **sr_input_list(void);

/*--- output/output.c -------------------------------------------------------*/

SR_API struct sr_output_format **sr_output_list(void);

/*--- strutil.c -------------------------------------------------------*/

SR_API char *sr_samplerate_string(uint64_t samplerate);
SR_API char *sr_period_string(uint64_t frequency);
SR_API char **sr_parse_triggerstring(struct sr_device *device,
				     const char *triggerstring);
SR_API int sr_parse_sizestring(const char *sizestring, uint64_t *size);
SR_API uint64_t sr_parse_timestring(const char *timestring);
SR_API gboolean sr_parse_boolstring(const char *boolstring);

#endif
