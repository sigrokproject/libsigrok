/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2023 Gerhard Sittig <gerhard.sittig@gmx.net>
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

#ifndef LIBSIGROK_HARDWARE_DEVANTECH_ETH008_PROTOCOL_H
#define LIBSIGROK_HARDWARE_DEVANTECH_ETH008_PROTOCOL_H

#include <glib.h>
#include <libsigrok/libsigrok.h>
#include <stdint.h>

#include "libsigrok-internal.h"

#define LOG_PREFIX "devantech-eth008"

/*
 * See developer notes in the protocol.c source file for details on the
 * feature set and communication protocol variants across the series.
 * This 'model' description tells their discriminating properties apart.
 */
struct devantech_eth008_model {
	uint8_t code;		/**!< model ID */
	const char *name;	/**!< model name */
	size_t ch_count_do;	/**!< digital output channel count */
	size_t ch_count_di;	/**!< digital input channel count */
	size_t ch_count_ai;	/**!< analog input channel count */
	uint8_t min_serno_fw;	/**!< min FW version to get serial nr */
	size_t width_do;	/**!< digital output image width */
	size_t width_di;	/**!< digital input image width */
	uint32_t mask_do_missing; /**!< missing digital output channels */
};

struct channel_group_context {
	size_t index;
	size_t number;
	enum devantech_eth008_channel_type {
		DV_CH_DIGITAL_OUTPUT,
		DV_CH_DIGITAL_INPUT,
		DV_CH_ANALOG_INPUT,
		DV_CH_SUPPLY_VOLTAGE,
	} ch_type;
};

struct dev_context {
	uint8_t model_code, hardware_version, firmware_version;
	const struct devantech_eth008_model *model;
	uint32_t mask_do;
	uint32_t curr_do;
	uint32_t curr_di;
};

SR_PRIV int devantech_eth008_get_model(struct sr_serial_dev_inst *serial,
	uint8_t *model_code, uint8_t *hw_version, uint8_t *fw_version);
SR_PRIV int devantech_eth008_get_serno(struct sr_serial_dev_inst *serial,
	char *text_buffer, size_t text_length);
SR_PRIV int devantech_eth008_cache_state(const struct sr_dev_inst *sdi);
SR_PRIV int devantech_eth008_query_do(const struct sr_dev_inst *sdi,
	const struct sr_channel_group *cg, gboolean *on);
SR_PRIV int devantech_eth008_setup_do(const struct sr_dev_inst *sdi,
	const struct sr_channel_group *cg, gboolean on);
SR_PRIV int devantech_eth008_query_di(const struct sr_dev_inst *sdi,
	const struct sr_channel_group *cg, gboolean *on);
SR_PRIV int devantech_eth008_query_ai(const struct sr_dev_inst *sdi,
	const struct sr_channel_group *cg, uint16_t *adc_value);
SR_PRIV int devantech_eth008_query_supply(const struct sr_dev_inst *sdi,
	const struct sr_channel_group *cg, uint16_t *millivolts);

#endif
