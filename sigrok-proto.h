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

#ifndef LIBSIGROK_SIGROK_PROTO_H
#define LIBSIGROK_SIGROK_PROTO_H

/*--- backend.c -------------------------------------------------------------*/

SR_API int sr_init(void);
SR_API int sr_exit(void);

/*--- log.c -----------------------------------------------------------------*/

typedef int (*sr_log_callback_t)(void *cb_data, int loglevel,
				 const char *format, va_list args);

SR_API int sr_log_loglevel_set(int loglevel);
SR_API int sr_log_loglevel_get(void);
SR_API int sr_log_callback_set(sr_log_callback_t cb, void *cb_data);
SR_API int sr_log_callback_set_default(void);
SR_API int sr_log_logdomain_set(const char *logdomain);
SR_API char *sr_log_logdomain_get(void);

/*--- datastore.c -----------------------------------------------------------*/

SR_API int sr_datastore_new(int unitsize, struct sr_datastore **ds);
SR_API int sr_datastore_destroy(struct sr_datastore *ds);
SR_API int sr_datastore_put(struct sr_datastore *ds, void *data,
			    unsigned int length, int in_unitsize,
			    const int *probelist);

/*--- device.c --------------------------------------------------------------*/

SR_API int sr_dev_scan(void);
SR_API GSList *sr_dev_list(void);
SR_API struct sr_dev *sr_dev_new(const struct sr_dev_driver *driver,
				 int driver_index);
SR_API int sr_dev_probe_add(struct sr_dev *dev, const char *name);
SR_API struct sr_probe *sr_dev_probe_find(const struct sr_dev *dev,
					  int probenum);
SR_API int sr_dev_probe_name_set(struct sr_dev *dev, int probenum,
				 const char *name);
SR_API int sr_dev_trigger_remove_all(struct sr_dev *dev);
SR_API int sr_dev_trigger_set(struct sr_dev *dev, int probenum,
			      const char *trigger);
SR_API gboolean sr_dev_has_hwcap(const struct sr_dev *dev, int hwcap);
SR_API int sr_dev_info_get(const struct sr_dev *dev, int id, const void **data);

/*--- filter.c --------------------------------------------------------------*/

SR_API int sr_filter_probes(int in_unitsize, int out_unitsize,
			    const int *probelist, const uint8_t *data_in,
			    uint64_t length_in, uint8_t **data_out,
			    uint64_t *length_out);

/*--- hwdriver.c ------------------------------------------------------------*/

SR_API struct sr_dev_driver **sr_driver_list(void);
SR_API int sr_driver_init(struct sr_dev_driver *driver);
SR_API gboolean sr_driver_hwcap_exists(struct sr_dev_driver *driver, int hwcap);
SR_API struct sr_hwcap_option *sr_hw_hwcap_get(int hwcap);

/*--- session.c -------------------------------------------------------------*/

typedef void (*sr_datafeed_callback_t)(struct sr_dev *dev,
				       struct sr_datafeed_packet *packet);

/* Session setup */
SR_API int sr_session_load(const char *filename);
SR_API struct sr_session *sr_session_new(void);
SR_API int sr_session_destroy(void);
SR_API int sr_session_dev_remove_all(void);
SR_API int sr_session_dev_add(struct sr_dev *dev);

/* Datafeed setup */
SR_API int sr_session_datafeed_callback_remove_all(void);
SR_API int sr_session_datafeed_callback_add(sr_datafeed_callback_t cb);

/* Session control */
SR_API int sr_session_start(void);
SR_API int sr_session_run(void);
SR_API int sr_session_halt(void);
SR_API int sr_session_stop(void);
SR_API int sr_session_save(const char *filename);
SR_API int sr_session_source_add(int fd, int events, int timeout,
		sr_receive_data_callback_t cb, void *cb_data);
SR_API int sr_session_source_remove(int fd);

/*--- input/input.c ---------------------------------------------------------*/

SR_API struct sr_input_format **sr_input_list(void);

/*--- output/output.c -------------------------------------------------------*/

SR_API struct sr_output_format **sr_output_list(void);

/*--- strutil.c -------------------------------------------------------------*/

SR_API char *sr_samplerate_string(uint64_t samplerate);
SR_API char *sr_period_string(uint64_t frequency);
SR_API char *sr_voltage_string(struct sr_rational *voltage);
SR_API char **sr_parse_triggerstring(struct sr_dev *dev,
				     const char *triggerstring);
SR_API int sr_parse_sizestring(const char *sizestring, uint64_t *size);
SR_API uint64_t sr_parse_timestring(const char *timestring);
SR_API gboolean sr_parse_boolstring(const char *boolstring);
SR_API int sr_parse_period(const char *periodstr, struct sr_rational *r);
SR_API int sr_parse_voltage(const char *voltstr, struct sr_rational *r);

/*--- version.c -------------------------------------------------------------*/

SR_API int sr_package_version_major_get(void);
SR_API int sr_package_version_minor_get(void);
SR_API int sr_package_version_micro_get(void);
SR_API const char *sr_package_version_string_get(void);

SR_API int sr_lib_version_current_get(void);
SR_API int sr_lib_version_revision_get(void);
SR_API int sr_lib_version_age_get(void);
SR_API const char *sr_lib_version_string_get(void);

#endif
