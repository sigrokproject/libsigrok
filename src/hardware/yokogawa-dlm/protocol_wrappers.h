/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 abraxa (Soeren Apel) <soeren@apelpie.net>
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

#ifndef LIBSIGROK_HARDWARE_YOKOGAWA_DLM_PROTOCOL_WRAPPERS_H
#define LIBSIGROK_HARDWARE_YOKOGAWA_DLM_PROTOCOL_WRAPPERS_H

#include <glib.h>
#include <stdint.h>
#include <string.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"
#include "protocol.h"

extern int dlm_timebase_get(struct sr_scpi_dev_inst *scpi,
		gchar **response);
extern int dlm_timebase_set(struct sr_scpi_dev_inst *scpi,
		const gchar *value);
extern int dlm_horiz_trigger_pos_get(struct sr_scpi_dev_inst *scpi,
		float *response);
extern int dlm_horiz_trigger_pos_set(struct sr_scpi_dev_inst *scpi,
		const gchar *value);
extern int dlm_trigger_source_get(struct sr_scpi_dev_inst *scpi,
		gchar **response);
extern int dlm_trigger_source_set(struct sr_scpi_dev_inst *scpi,
		const gchar *value);
extern int dlm_trigger_slope_get(struct sr_scpi_dev_inst *scpi,
		int *value);
extern int dlm_trigger_slope_set(struct sr_scpi_dev_inst *scpi,
		const int value);

extern int dlm_analog_chan_state_get(struct sr_scpi_dev_inst *scpi, int channel,
		gboolean *response);
extern int dlm_analog_chan_state_set(struct sr_scpi_dev_inst *scpi, int channel,
		const gboolean value);
extern int dlm_analog_chan_vdiv_get(struct sr_scpi_dev_inst *scpi, int channel,
		gchar **response);
extern int dlm_analog_chan_vdiv_set(struct sr_scpi_dev_inst *scpi, int channel,
		const gchar *value);
extern int dlm_analog_chan_voffs_get(struct sr_scpi_dev_inst *scpi, int channel,
		float *response);
extern int dlm_analog_chan_srate_get(struct sr_scpi_dev_inst *scpi, int channel,
		float *response);
extern int dlm_analog_chan_coupl_get(struct sr_scpi_dev_inst *scpi, int channel,
		gchar **response);
extern int dlm_analog_chan_coupl_set(struct sr_scpi_dev_inst *scpi, int channel,
		const gchar *value);
extern int dlm_analog_chan_wrange_get(struct sr_scpi_dev_inst *scpi, int channel,
		float *response);
extern int dlm_analog_chan_woffs_get(struct sr_scpi_dev_inst *scpi, int channel,
		float *response);

extern int dlm_digital_chan_state_get(struct sr_scpi_dev_inst *scpi, int channel,
		gboolean *response);
extern int dlm_digital_chan_state_set(struct sr_scpi_dev_inst *scpi, int channel,
		const gboolean value);
extern int dlm_digital_pod_state_get(struct sr_scpi_dev_inst *scpi, int pod,
		gboolean *response);
extern int dlm_digital_pod_state_set(struct sr_scpi_dev_inst *scpi, int pod,
		const gboolean value);

extern int dlm_response_headers_set(struct sr_scpi_dev_inst *scpi,
		const gboolean value);
extern int dlm_acquisition_stop(struct sr_scpi_dev_inst *scpi);

extern int dlm_acq_length_get(struct sr_scpi_dev_inst *scpi,
		uint32_t *response);
extern int dlm_chunks_per_acq_get(struct sr_scpi_dev_inst *scpi,
		int *response);
extern int dlm_start_frame_set(struct sr_scpi_dev_inst *scpi, int value);
extern int dlm_data_get(struct sr_scpi_dev_inst *scpi, int acquisition_num);
extern int dlm_analog_data_get(struct sr_scpi_dev_inst *scpi, int channel);
extern int dlm_digital_data_get(struct sr_scpi_dev_inst *scpi);

#endif
