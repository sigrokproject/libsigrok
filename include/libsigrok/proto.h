/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Bert Vermeulen <bert@biot.com>
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

#ifndef LIBSIGROK_PROTO_H
#define LIBSIGROK_PROTO_H

/**
 * @file
 *
 * Header file containing API function prototypes.
 */

/*--- analog.c --------------------------------------------------------------*/

SR_API int sr_analog_to_float(const struct sr_datafeed_analog2 *analog,
		float *buf);
SR_API int sr_analog_float_to_string(float value, int digits, char **result);
SR_API int sr_analog_unit_to_string(const struct sr_datafeed_analog2 *analog,
		char **result);
SR_API inline void sr_rational_set(struct sr_rational *r, uint64_t p, uint64_t q);

/*--- backend.c -------------------------------------------------------------*/

SR_API int sr_init(struct sr_context **ctx);
SR_API int sr_exit(struct sr_context *ctx);

/*--- log.c -----------------------------------------------------------------*/

typedef int (*sr_log_callback)(void *cb_data, int loglevel,
				const char *format, va_list args);

SR_API int sr_log_loglevel_set(int loglevel);
SR_API int sr_log_loglevel_get(void);
SR_API int sr_log_callback_set(sr_log_callback cb, void *cb_data);
SR_API int sr_log_callback_set_default(void);
SR_API int sr_log_logdomain_set(const char *logdomain);
SR_API char *sr_log_logdomain_get(void);

/*--- device.c --------------------------------------------------------------*/

SR_API int sr_dev_channel_name_set(const struct sr_dev_inst *sdi,
		int channelnum, const char *name);
SR_API int sr_dev_channel_enable(const struct sr_dev_inst *sdi, int channelnum,
		gboolean state);
SR_API gboolean sr_dev_has_option(const struct sr_dev_inst *sdi, int key);
SR_API GSList *sr_dev_list(const struct sr_dev_driver *driver);
SR_API int sr_dev_clear(const struct sr_dev_driver *driver);
SR_API int sr_dev_open(struct sr_dev_inst *sdi);
SR_API int sr_dev_close(struct sr_dev_inst *sdi);

SR_API struct sr_dev_driver *sr_dev_inst_driver_get(const struct sr_dev_inst *sdi);
SR_API const char *sr_dev_inst_vendor_get(const struct sr_dev_inst *sdi);
SR_API const char *sr_dev_inst_model_get(const struct sr_dev_inst *sdi);
SR_API const char *sr_dev_inst_version_get(const struct sr_dev_inst *sdi);
SR_API const char *sr_dev_inst_sernum_get(const struct sr_dev_inst *sdi);
SR_API const char *sr_dev_inst_connid_get(const struct sr_dev_inst *sdi);
SR_API GSList *sr_dev_inst_channels_get(const struct sr_dev_inst *sdi);
SR_API GSList *sr_dev_inst_channel_groups_get(const struct sr_dev_inst *sdi);

SR_API struct sr_dev_inst *sr_dev_inst_user_new(const char *vendor,
		const char *model, const char *version);
SR_API int sr_dev_inst_channel_add(struct sr_dev_inst *sdi, int index, int type, const char *name);

/*--- hwdriver.c ------------------------------------------------------------*/

SR_API struct sr_dev_driver **sr_driver_list(void);
SR_API int sr_driver_init(struct sr_context *ctx,
		struct sr_dev_driver *driver);
SR_API GSList *sr_driver_scan(struct sr_dev_driver *driver, GSList *options);
SR_API int sr_config_get(const struct sr_dev_driver *driver,
		const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg,
		uint32_t key, GVariant **data);
SR_API int sr_config_set(const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg,
		uint32_t key, GVariant *data);
SR_API int sr_config_commit(const struct sr_dev_inst *sdi);
SR_API int sr_config_list(const struct sr_dev_driver *driver,
		const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg,
		uint32_t key, GVariant **data);
SR_API const struct sr_config_info *sr_config_info_get(uint32_t key);
SR_API const struct sr_config_info *sr_config_info_name_get(const char *optname);

/*--- session.c -------------------------------------------------------------*/

typedef void (*sr_datafeed_callback)(const struct sr_dev_inst *sdi,
		const struct sr_datafeed_packet *packet, void *cb_data);
SR_API struct sr_trigger *sr_session_trigger_get(struct sr_session *session);

/* Session setup */
SR_API int sr_session_load(const char *filename, struct sr_session **session);
SR_API int sr_session_new(struct sr_session **session);
SR_API int sr_session_destroy(struct sr_session *session);
SR_API int sr_session_dev_remove_all(struct sr_session *session);
SR_API int sr_session_dev_add(struct sr_session *session,
		struct sr_dev_inst *sdi);
SR_API int sr_session_dev_list(struct sr_session *session, GSList **devlist);
SR_API int sr_session_trigger_set(struct sr_session *session, struct sr_trigger *trig);

/* Datafeed setup */
SR_API int sr_session_datafeed_callback_remove_all(struct sr_session *session);
SR_API int sr_session_datafeed_callback_add(struct sr_session *session,
		sr_datafeed_callback cb, void *cb_data);

/* Session control */
SR_API int sr_session_start(struct sr_session *session);
SR_API int sr_session_run(struct sr_session *session);
SR_API int sr_session_stop(struct sr_session *session);
SR_API int sr_session_save(struct sr_session *session, const char *filename,
		const struct sr_dev_inst *sdi, unsigned char *buf, int unitsize,
		int units);
SR_API int sr_session_save_init(struct sr_session *session,
		const char *filename, uint64_t samplerate, char **channels);
SR_API int sr_session_append(struct sr_session *session,
		const char *filename, unsigned char *buf, int unitsize, int units);
SR_API int sr_session_source_add(struct sr_session *session, int fd,
		int events, int timeout, sr_receive_data_callback cb, void *cb_data);
SR_API int sr_session_source_add_pollfd(struct sr_session *session,
		GPollFD *pollfd, int timeout, sr_receive_data_callback cb,
		void *cb_data);
SR_API int sr_session_source_add_channel(struct sr_session *session,
		GIOChannel *channel, int events, int timeout,
		sr_receive_data_callback cb, void *cb_data);
SR_API int sr_session_source_remove(struct sr_session *session, int fd);
SR_API int sr_session_source_remove_pollfd(struct sr_session *session,
		GPollFD *pollfd);
SR_API int sr_session_source_remove_channel(struct sr_session *session,
		GIOChannel *channel);

/*--- input/input.c ---------------------------------------------------------*/

SR_API const struct sr_input_module **sr_input_list(void);
SR_API const char *sr_input_id_get(const struct sr_input_module *in);
SR_API const char *sr_input_name_get(const struct sr_input_module *in);
SR_API const char *sr_input_description_get(const struct sr_input_module *in);
SR_API const struct sr_input_module *sr_input_find(char *id);
SR_API const struct sr_option **sr_input_options_get(const struct sr_input_module *in);
SR_API void sr_input_options_free(const struct sr_option **options);
SR_API struct sr_input *sr_input_new(const struct sr_input_module *imod,
		GHashTable *options);
SR_API int sr_input_scan_buffer(GString *buf, const struct sr_input **in);
SR_API int sr_input_scan_file(const char *filename, const struct sr_input **in);
SR_API struct sr_dev_inst *sr_input_dev_inst_get(const struct sr_input *in);
SR_API int sr_input_send(const struct sr_input *in, GString *buf);
SR_API int sr_input_end(const struct sr_input *in);
SR_API void sr_input_free(const struct sr_input *in);

/*--- output/output.c -------------------------------------------------------*/

SR_API const struct sr_output_module **sr_output_list(void);
SR_API const char *sr_output_id_get(const struct sr_output_module *o);
SR_API const char *sr_output_name_get(const struct sr_output_module *o);
SR_API const char *sr_output_description_get(const struct sr_output_module *o);
SR_API const struct sr_output_module *sr_output_find(char *id);
SR_API const struct sr_option **sr_output_options_get(const struct sr_output_module *o);
SR_API void sr_output_options_free(const struct sr_option **opts);
SR_API const struct sr_output *sr_output_new(const struct sr_output_module *o,
		GHashTable *params, const struct sr_dev_inst *sdi);
SR_API int sr_output_send(const struct sr_output *o,
		const struct sr_datafeed_packet *packet, GString **out);
SR_API int sr_output_free(const struct sr_output *o);

/*--- trigger.c -------------------------------------------------------------*/

SR_API struct sr_trigger *sr_trigger_new(const char *name);
SR_API void sr_trigger_free(struct sr_trigger *trig);
SR_API struct sr_trigger_stage *sr_trigger_stage_add(struct sr_trigger *trig);
SR_API int sr_trigger_match_add(struct sr_trigger_stage *stage,
		struct sr_channel *ch, int trigger_match, float value);

/*--- strutil.c -------------------------------------------------------------*/

SR_API char *sr_si_string_u64(uint64_t x, const char *unit);
SR_API char *sr_samplerate_string(uint64_t samplerate);
SR_API char *sr_period_string(uint64_t frequency);
SR_API char *sr_voltage_string(uint64_t v_p, uint64_t v_q);
SR_API int sr_parse_sizestring(const char *sizestring, uint64_t *size);
SR_API uint64_t sr_parse_timestring(const char *timestring);
SR_API gboolean sr_parse_boolstring(const char *boolstring);
SR_API int sr_parse_period(const char *periodstr, uint64_t *p, uint64_t *q);
SR_API int sr_parse_voltage(const char *voltstr, uint64_t *p, uint64_t *q);

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
