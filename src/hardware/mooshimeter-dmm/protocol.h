/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2019 Derek Hageman <hageman@inthat.cloud>
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

#ifndef LIBSIGROK_HARDWARE_MOOSHIMETER_DMM_PROTOCOL_H
#define LIBSIGROK_HARDWARE_MOOSHIMETER_DMM_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "mooshimeter-dmm"

struct packet_rx {
	int sequence_number;
	GSList *reorder_buffer;
	GByteArray *contents;
};

struct packet_tx {
	int sequence_number;
};

enum tree_node_datatype {
	TREE_NODE_DATATYPE_PLAIN = 0,
	TREE_NODE_DATATYPE_LINK,
	TREE_NODE_DATATYPE_CHOOSER,
	TREE_NODE_DATATYPE_U8,
	TREE_NODE_DATATYPE_U16,
	TREE_NODE_DATATYPE_U32,
	TREE_NODE_DATATYPE_S8,
	TREE_NODE_DATATYPE_S16,
	TREE_NODE_DATATYPE_S32,
	TREE_NODE_DATATYPE_STRING,
	TREE_NODE_DATATYPE_BINARY,
	TREE_NODE_DATATYPE_FLOAT,
};

union tree_value {
	int32_t i;
	float f;
	GByteArray *b;
};

struct config_tree_node {
	char *name;
	int id;
	size_t index_in_parent;

	enum tree_node_datatype type;
	union tree_value value;

	size_t count_children;
	struct config_tree_node *children;

	uint32_t update_number;
	void (*on_update)(struct config_tree_node *node, void *param);
	void *on_update_param;
};

struct dev_context {
	struct packet_rx rx;
	struct packet_tx tx;
	struct config_tree_node tree_root;
	struct config_tree_node *tree_id_lookup[0x7F];
	uint32_t buffer_bps[2];
	float buffer_lsb2native[2];

	void (*channel_autorange[3])(const struct sr_dev_inst *sdi, float value);

	struct sr_sw_limits limits;
	struct sr_analog_meaning channel_meaning[3];

	gboolean enable_value_stream;
};

SR_PRIV int mooshimeter_dmm_open(const struct sr_dev_inst *sdi);
SR_PRIV int mooshimeter_dmm_close(const struct sr_dev_inst *sdi);
SR_PRIV int mooshimeter_dmm_set_chooser(const struct sr_dev_inst *sdi, const char *path, const char *choice);
SR_PRIV int mooshimeter_dmm_set_integer(const struct sr_dev_inst *sdi, const char *path, int value);
SR_PRIV int mooshimeter_dmm_set_larger_number(const struct sr_dev_inst *sdi, const char *path, const char *parent, float number);
SR_PRIV gboolean mooshimeter_dmm_set_autorange(const struct sr_dev_inst *sdi, const char *path, const char *parent, float latest);
SR_PRIV int mooshimeter_dmm_get_chosen_number(const struct sr_dev_inst *sdi, const char *path, const char *parent, float *number);
SR_PRIV int mooshimeter_dmm_get_available_number_choices(const struct sr_dev_inst *sdi, const char *path, float **numbers, size_t *count);
SR_PRIV int mooshimeter_dmm_poll(int fd, int revents, void *cb_data);
SR_PRIV int mooshimeter_dmm_heartbeat(int fd, int revents, void *cb_data);

#endif
