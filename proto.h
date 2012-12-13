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

/**
 * @file
 *
 * Header file containing API function prototypes.
 */

/*--- backend.c -------------------------------------------------------------*/

SR_API int sr_init(struct sr_context **ctx);
SR_API int sr_exit(struct sr_context *ctx);

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

SR_API int sr_dev_probe_name_set(const struct sr_dev_inst *sdi,
		int probenum, const char *name);
SR_API int sr_dev_probe_enable(const struct sr_dev_inst *sdi, int probenum,
		gboolean state);
SR_API int sr_dev_trigger_set(const struct sr_dev_inst *sdi, int probenum,
		const char *trigger);
SR_API gboolean sr_dev_has_hwcap(const struct sr_dev_inst *sdi, int hwcap);
SR_API int sr_dev_config_set(const struct sr_dev_inst *sdi, int hwcap,
		const void *value);
SR_API GSList *sr_dev_inst_list(const struct sr_dev_driver *driver);
SR_API int sr_dev_inst_clear(const struct sr_dev_driver *driver);

/*--- filter.c --------------------------------------------------------------*/

SR_API int sr_filter_probes(int in_unitsize, int out_unitsize,
			    const int *probelist, const uint8_t *data_in,
			    uint64_t length_in, uint8_t **data_out,
			    uint64_t *length_out);

/*--- hwdriver.c ------------------------------------------------------------*/

SR_API struct sr_dev_driver **sr_driver_list(void);
SR_API int sr_driver_init(struct sr_context *ctx,
		struct sr_dev_driver *driver);
SR_API GSList *sr_driver_scan(struct sr_dev_driver *driver, GSList *options);
SR_API int sr_info_get(struct sr_dev_driver *driver, int id,
		const void **data, const struct sr_dev_inst *sdi);
SR_API gboolean sr_driver_hwcap_exists(struct sr_dev_driver *driver, int hwcap);
SR_API const struct sr_hwcap_option *sr_drvopt_get(int opt);
SR_API const struct sr_hwcap_option *sr_drvopt_name_get(const char *optname);
SR_API const struct sr_hwcap_option *sr_devopt_get(int opt);
SR_API const struct sr_hwcap_option *sr_devopt_name_get(const char *optname);

/*--- session.c -------------------------------------------------------------*/

typedef void (*sr_datafeed_callback_t)(const struct sr_dev_inst *sdi,
		const struct sr_datafeed_packet *packet);

/* Session setup */
SR_API int sr_session_load(const char *filename);
SR_API struct sr_session *sr_session_new(void);
SR_API int sr_session_destroy(void);
SR_API int sr_session_dev_remove_all(void);
SR_API int sr_session_dev_add(const struct sr_dev_inst *sdi);

/* Datafeed setup */
SR_API int sr_session_datafeed_callback_remove_all(void);
SR_API int sr_session_datafeed_callback_add(sr_datafeed_callback_t cb);

/* Session control */
SR_API int sr_session_start(void);
SR_API int sr_session_run(void);
SR_API int sr_session_halt(void);
SR_API int sr_session_stop(void);
SR_API int sr_session_save(const char *filename,
		const struct sr_dev_inst *sdi, struct sr_datastore *ds);
SR_API int sr_session_source_add(int fd, int events, int timeout,
		sr_receive_data_callback_t cb, void *cb_data);
SR_API int sr_session_source_add_pollfd(GPollFD *pollfd, int timeout,
		sr_receive_data_callback_t cb, void *cb_data);
SR_API int sr_session_source_add_channel(GIOChannel *channel, int events,
		int timeout, sr_receive_data_callback_t cb, void *cb_data);
SR_API int sr_session_source_remove(int fd);
SR_API int sr_session_source_remove_pollfd(GPollFD *pollfd);
SR_API int sr_session_source_remove_channel(GIOChannel *channel);

/*--- input/input.c ---------------------------------------------------------*/

SR_API struct sr_input_format **sr_input_list(void);

/*--- output/output.c -------------------------------------------------------*/

SR_API struct sr_output_format **sr_output_list(void);

/*--- strutil.c -------------------------------------------------------------*/

SR_API char *sr_si_string_u64(uint64_t x, const char *unit);
SR_API char *sr_samplerate_string(uint64_t samplerate);
SR_API char *sr_period_string(uint64_t frequency);
SR_API char *sr_voltage_string(struct sr_rational *voltage);
SR_API char **sr_parse_triggerstring(const struct sr_dev_inst *sdi,
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

/*--- error.c ---------------------------------------------------------------*/

SR_API const char *sr_strerror(int error_code);
SR_API const char *sr_strerror_name(int error_code);

#endif
