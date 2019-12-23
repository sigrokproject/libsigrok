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

#include <config.h>
#include <gio/gio.h>
#include <math.h>
#include <string.h>
#include "protocol.h"

/*
 * The Mooshimeter protocol is broken down into several layers in a
 * communication stack.
 *
 * The lowest layer is the BLE GATT stack, which provides two characteristics:
 * one to write packets to the meter and one to receive them from it. The
 * MTU for a packet in either direction is 20 bytes. This is implemented
 * in the GATT abstraction, so we can talk to it via simple write commands
 * and a read callback.
 *
 *
 * The next layer is the serial stream: each BLE packet in either direction
 * has a 1-byte header of a sequence number. Despite what the documentation
 * says, this is present in both directions (not just meter output) and is
 * NOT reset on the meter output on BLE connection. So the implementation
 * here needs to provide an output sequence number and incoming reassembly
 * for out of order packets (I haven't actually observed this, but
 * supposedly it happens, which is why the sequence number is present).
 * So the structure of packets received looks like:
 *
 * | 1 byte | 1-19 bytes  |
 * |--------|-------------|
 * | SeqNum | Serial Data |
 *
 *
 * On top of the serial layer is the "config tree" layer. This is how
 * the meter actually exposes data and configuration. The tree itself
 * is composed of nodes, each with a string name, data type, and a list
 * of children (zero or more). For value containing (non-informational)
 * nodes, they also contain a 7-bit unique identifier. Access to the
 * config tree is provided by packets on the serial stream, each packet
 * has a 1-byte header, where the uppermost bit (0x80) is set when writing
 * (i.e. never by the meter) and the remaining 7 bits are the node identifier.
 * The length of the packets varies based on the datatype of the tree node.
 * This means that any lost/dropped packets can make the stream unrecoverable
 * (i.e. there's no defined sync method other than reconnection). Packets
 * are emitted by the meter in response to a read or write command (write
 * commands simply back the value) and at unsolicited times by the meter
 * (e.g. continuous sampling and periodic battery voltage). A read packet
 * send to the meter looks like:
 *
 * | 1 bit | 7 bits |
 * |-------|--------|
 * |   0   | NodeID |
 *
 * In response to the read, the meter will send:
 *
 * | 1 bit | 7 bits | 1-N bytes |
 * |-------|--------|-----------|
 * |   0   | NodeID | NodeValue |
 *
 * A write packet sent to the meter:
 *
 * | 1 bit | 7 bits | 1-N bytes |
 * |-------|--------|-----------|
 * |   1   | NodeID | NodeValue |
 *
 * In response to the write, the meter will send a read response:
 *
 * | 1 bit | 7 bits | 1-N bytes |
 * |-------|--------|-----------|
 * |   0   | NodeID | NodeValue |
 *
 *
 * For the data in the tree, all values are little endian (least significant
 * bytes first). The supported type codes are:
 *
 * | Code | Description | Wire Format                            |
 * |------|-------------|----------------------------------------|
 * |  0   | Plain       |                                        |
 * |  1   | Link        |                                        |
 * |  2   | Chooser     | uint8_t                                |
 * |  3   | U8          | uint8_t                                |
 * |  4   | U16         | uint16_t                               |
 * |  5   | U32         | uint32_t                               |
 * |  6   | S8          | int8_t                                 |
 * |  7   | S16         | int16_t                                |
 * |  8   | S32         | int32_t                                |
 * |  9   | String      | uint16_t length; char value[length]    |
 * |  10  | Binary      | uint16_t length; uint8_t value[length] |
 * |  11  | Float       | float                                  |
 *
 * Plain and Link nodes are present to provide information and/or choices
 * but do not provide commands codes for direct access (see serialization
 * below). Chooser nodes are written with indices described by their Plain
 * type children (e.g. to select a choice identified by the second child
 * of a chooser, write 1 to the chooser node itself).
 *
 * On initial connection only three nodes at fixed identifiers are available:
 *
 * | Node             | ID | Type   |
 * |------------------|----|--------|
 * | ADMIN:CRC32      | 0  | U32    |
 * | ADMIN:TREE       | 1  | Binary |
 * | ADMIN:DIAGNOSTIC | 2  | String |
 *
 *
 * The handshake sequence is to read the contents of ADMIN:TREE, which contains
 * the zlib compressed tree serialization, then write the CRC of the compressed
 * data back to ADMIN:CRC32 (which the meter will echo back). Only after
 * that is done will the meter accept access to the rest of the tree.
 *
 * After zlib decompression the tree serialization is as follows:
 *
 * | Type         | Description                         |
 * |--------------|-------------------------------------|
 * | uint8_t      | The node data type code from above  |
 * | uint8_t      | Name length                         |
 * | char[length] | Node name (e.g. "ADMIN" or "CRC32") |
 * | uint8_t      | Number of children                  |
 * | Node[count]  | Child serialization (length varies) |
 *
 * Once the tree has been deserialized, each node needs its identifier
 * assigned. This is a depth first tree walk, assigning sequential identifiers
 * first the the current node (if it needs one), then repeating recursively
 * for each of its children. Plain and Link nodes are skipped in assignment
 * but not the walk (so the recursion still happens, but the identifier
 * is not incremented).
 *
 *
 * So, for example a write to the ADMIN:CRC32 as part of the handshake would
 * be a write by us (the host):
 *
 * | SerSeq | NodeID | U32 (CRC)  |
 * | 1 byte | 1 byte |   4 bytes  |
 * ---------|--------|------------|
 * |  0x01  |  0x80  | 0xDEADBEEF |
 *
 * The meter will respond with a packet like:
 *
 * | SerSeq | NodeID | U32 (CRC)  |
 * | 1 byte | 1 byte |   4 bytes  |
 * ---------|--------|------------|
 * |  0x42  |  0x00  | 0xDEADBEEF |
 *
 * A spontaneous error from the meter (e.g. in response to a bad packet)
 * can be emitted like:
 *
 * | SerSeq | NodeID | U16 (len)  |      String      |
 * | 1 byte | 1 byte |   2 bytes  |  len (=8) bytes  |
 * ---------|--------|------------|------------------|
 * |  0xAB  |  0x20  |   0x0008   |    BAD\x20DATA   |
 * 
 * 
 * The config tree at the time of writing looks like:
 *
 *  <ROOT> (PLAIN)
 *	ADMIN (PLAIN)
 *		CRC32 (U32) = 0
 *		TREE (BIN) = 1
 *		DIAGNOSTIC (STR) = 2
 *	PCB_VERSION (U8) = 3
 *	NAME (STR) = 4
 *	TIME_UTC (U32) = 5
 *	TIME_UTC_MS (U16) = 6
 *	BAT_V (FLT) = 7
 *	REBOOT (CHOOSER) = 8
 *		NORMAL (PLAIN)
 *		SHIPMODE (PLAIN)
 *	SAMPLING (PLAIN)
 *		RATE (CHOOSER) = 9
 *			125 (PLAIN)
 *			250 (PLAIN)
 *			500 (PLAIN)
 *			1000 (PLAIN)
 *			2000 (PLAIN)
 *			4000 (PLAIN)
 *			8000 (PLAIN)
 *		DEPTH (CHOOSER) = 10
 *			32 (PLAIN)
 *			64 (PLAIN)
 *			128 (PLAIN)
 *			256 (PLAIN)
 *		TRIGGER (CHOOSER) = 11
 *			OFF (PLAIN)
 *			SINGLE (PLAIN)
 *			CONTINUOUS (PLAIN)
 *	LOG (PLAIN)
 *		ON (U8) = 12
 *		INTERVAL (U16) = 13
 *		STATUS (U8) = 14
 *		POLLDIR (U8) = 15
 *		INFO (PLAIN)
 *			INDEX (U16) = 16
 *			END_TIME (U32) = 17
 *			N_BYTES (U32) = 18
 *		STREAM (PLAIN)
 *			INDEX (U16) = 19
 *			OFFSET (U32) = 20
 *			DATA (BIN) = 21
 *	CH1 (PLAIN)
 *		MAPPING (CHOOSER) = 22
 *			CURRENT (PLAIN)
 *				10 (PLAIN)
 *			TEMP (PLAIN)
 *				350 (PLAIN)
 *			SHARED (LINK)
 *		RANGE_I (U8) = 23
 *		ANALYSIS (CHOOSER) = 24
 *			MEAN (PLAIN)
 *			RMS (PLAIN)
 *			BUFFER (PLAIN)
 *		VALUE (FLT) = 25
 *		OFFSET (FLT) = 26
 *		BUF (BIN) = 27
 *		BUF_BPS (U8) = 28
 *		BUF_LSB2NATIVE (FLT) = 29
 *	CH2 (PLAIN)
 *		MAPPING (CHOOSER) = 30
 *			VOLTAGE (PLAIN)
 *				60 (PLAIN)
 *				600 (PLAIN)
 *			TEMP (PLAIN)
 *				350 (PLAIN)
 *			SHARED (LINK)
 *		RANGE_I (U8) = 31
 *		ANALYSIS (CHOOSER) = 32
 *			MEAN (PLAIN)
 *			RMS (PLAIN)
 *			BUFFER (PLAIN)
 *		VALUE (FLT) = 33
 *		OFFSET (FLT) = 34
 *		BUF (BIN) = 35
 *		BUF_BPS (U8) = 36
 *		BUF_LSB2NATIVE (FLT) = 37
 *	SHARED (CHOOSER) = 38
 *		AUX_V (PLAIN)
 *			0.1 (PLAIN)
 *			0.3 (PLAIN)
 *			1.2 (PLAIN)
 *		RESISTANCE (PLAIN)
 *			1000.0 (PLAIN)
 *			10000.0 (PLAIN)
 *			100000.0 (PLAIN)
 *			1000000.0 (PLAIN)
 *			10000000.0 (PLAIN)
 *		DIODE (PLAIN)
 *			1.2 (PLAIN)
 *	REAL_PWR (FLT) = 39
 */

static struct config_tree_node *lookup_tree_path(struct dev_context *devc,
	const char *path)
{
	struct config_tree_node *current = &devc->tree_root;
	struct config_tree_node *next;
	const char *end;
	size_t length;

	for (;;) {
		end = strchr(path, ':');
		if (!end)
			length = strlen(path);
		else
			length = end - path;

		next = NULL;
		for (size_t i = 0; i < current->count_children; i++) {
			if (!current->children[i].name)
				continue;
			if (strlen(current->children[i].name) != length)
				continue;
			if (g_ascii_strncasecmp(path,
				current->children[i].name,
				length)) {
				continue;
			}

			next = &current->children[i];
		}
		if (!next)
			return NULL;
		if (!end)
			return next;

		path = end + 1;
		current = next;
	}
}

static int lookup_chooser_index(struct dev_context *devc, const char *path)
{
	struct config_tree_node *node;

	node = lookup_tree_path(devc, path);
	if (!node)
		return -1;

	return (int)node->index_in_parent;
}

static gboolean update_tree_data(struct config_tree_node *node,
	GByteArray *contents)
{
	guint len;
	switch (node->type) {
	case TREE_NODE_DATATYPE_PLAIN:
	case TREE_NODE_DATATYPE_LINK:
		sr_err("Update for dataless node.");
		g_byte_array_remove_range(contents, 0, 2);
		return TRUE;
	case TREE_NODE_DATATYPE_CHOOSER:
	case TREE_NODE_DATATYPE_U8:
		node->value.i = R8(contents->data + 1);
		g_byte_array_remove_range(contents, 0, 2);
		break;
	case TREE_NODE_DATATYPE_U16:
		if (contents->len < 3)
			return FALSE;
		node->value.i = RL16(contents->data + 1);
		g_byte_array_remove_range(contents, 0, 3);
		break;
	case TREE_NODE_DATATYPE_U32:
		if (contents->len < 5)
			return FALSE;
		node->value.i = RL32(contents->data + 1);
		g_byte_array_remove_range(contents, 0, 5);
		break;
	case TREE_NODE_DATATYPE_S8:
		node->value.i = (int8_t)R8(contents->data + 1);
		g_byte_array_remove_range(contents, 0, 2);
		break;
	case TREE_NODE_DATATYPE_S16:
		if (contents->len < 3)
			return FALSE;
		node->value.i = RL16S(contents->data + 1);
		g_byte_array_remove_range(contents, 0, 3);
		break;
	case TREE_NODE_DATATYPE_S32:
		if (contents->len < 5)
			return FALSE;
		node->value.i = RL32S(contents->data + 1);
		g_byte_array_remove_range(contents, 0, 5);
		break;
	case TREE_NODE_DATATYPE_STRING:
	case TREE_NODE_DATATYPE_BINARY:
		if (contents->len < 3)
			return FALSE;
		len = RL16(contents->data + 1);
		if (contents->len < 3 + len)
			return FALSE;
		g_byte_array_set_size(node->value.b, len);
		memcpy(node->value.b->data, contents->data + 3, len);
		g_byte_array_remove_range(contents, 0, 3 + len);
		break;
	case TREE_NODE_DATATYPE_FLOAT:
		if (contents->len < 5)
			return FALSE;
		node->value.f = RLFL(contents->data + 1);
		g_byte_array_remove_range(contents, 0, 5);
		break;
	}

	node->update_number++;

	if (node->on_update)
		(*node->on_update)(node, node->on_update_param);

	return TRUE;
}

static gboolean incoming_frame(struct packet_rx *rx,
	const void *data, guint count)
{
	const guint8 *bytes = data;
	int seq, ahead;
	GByteArray *ba;
	GSList *target = NULL;

	if (!count)
		return FALSE;

	seq = bytes[0];
	if (rx->sequence_number < 0) {
		rx->sequence_number = (seq + 1) & 0xFF;
		g_byte_array_append(rx->contents, bytes + 1, count - 1);
		return TRUE;
	} else if (rx->sequence_number == seq) {
		rx->sequence_number = (seq + 1) & 0xFF;
		g_byte_array_append(rx->contents, data + 1, count - 1);

		while (rx->reorder_buffer && rx->reorder_buffer->data) {
			rx->sequence_number = (rx->sequence_number + 1) & 0xFF;

			ba = rx->reorder_buffer->data;
			g_byte_array_append(rx->contents, ba->data, ba->len);
			g_byte_array_free(ba, TRUE);
			target = rx->reorder_buffer;
			rx->reorder_buffer = rx->reorder_buffer->next;
			g_slist_free_1(target);
		}
		return TRUE;
	} else {
		ahead = seq - rx->sequence_number;
		if (ahead < 0)
			ahead += 256;
		if (!rx->reorder_buffer)
			rx->reorder_buffer = g_slist_alloc();
		target = rx->reorder_buffer;
		for (--ahead; ahead > 0; --ahead) {
			if (!target->next)
				target->next = g_slist_alloc();
			target = target->next;
		}
		if (target->data)
			g_byte_array_free(target->data, TRUE);
		target->data = g_byte_array_sized_new(count);
		g_byte_array_append(target->data, data + 1, count - 1);
		return TRUE;
	}
}

static void consume_packets(struct dev_context *devc)
{
	uint8_t id;
	struct config_tree_node *target;

	if (devc->rx.contents->len < 2)
		return;

	id = devc->rx.contents->data[0];
	id &= 0x7F;
	target = devc->tree_id_lookup[id];

	if (!target) {
		sr_err("Command %hhu code does not map to a known node.", id);
		g_byte_array_remove_index(devc->rx.contents, 0);
		return consume_packets(devc);
	}

	if (!update_tree_data(target, devc->rx.contents))
		return;

	return consume_packets(devc);
}

static int notify_cb(void *cb_data, uint8_t *data, size_t dlen)
{
	const struct sr_dev_inst *sdi = cb_data;
	struct dev_context *devc = sdi->priv;

	if (!incoming_frame(&devc->rx, data, (guint)dlen))
		return -1;

	consume_packets(devc);

	return 0;
}

static int write_frame(const struct sr_dev_inst *sdi,
	const void *frame, size_t length)
{
	struct sr_bt_desc *desc = sdi->conn;

	if (sr_bt_write(desc, frame, length) != (ssize_t)length)
		return SR_ERR;

	return SR_OK;
}

static int poll_tree_value(const struct sr_dev_inst *sdi,
	struct config_tree_node *node)
{
	struct dev_context *devc = sdi->priv;

	uint8_t frame[2];
	W8(&frame[0], devc->tx.sequence_number);
	W8(&frame[1], node->id);

	devc->tx.sequence_number = (devc->tx.sequence_number + 1) & 0xFF;

	return write_frame(sdi, frame, 2);
}

static void set_tree_integer(const struct sr_dev_inst *sdi,
	struct config_tree_node *node, int32_t value)
{
	struct dev_context *devc = sdi->priv;
	uint8_t frame[20];
	size_t length;

	W8(&frame[0], devc->tx.sequence_number);
	W8(&frame[1], 0x80 | node->id);

	length = 2;

	switch (node->type) {
	case TREE_NODE_DATATYPE_PLAIN:
	case TREE_NODE_DATATYPE_LINK:
		sr_err("Set attempted for dataless node.");
		return;
	case TREE_NODE_DATATYPE_CHOOSER:
	case TREE_NODE_DATATYPE_U8:
		node->value.i = value;
		W8(&frame[length], value);
		length += 1;
		break;
	case TREE_NODE_DATATYPE_U16:
		node->value.i = value;
		WL16(&frame[length], value);
		length += 2;
		break;
	case TREE_NODE_DATATYPE_U32:
		node->value.i = value;
		WL32(&frame[length], value);
		length += 4;
		break;
	case TREE_NODE_DATATYPE_S8:
		node->value.i = value;
		W8(&frame[length], value);
		length += 1;
		break;
	case TREE_NODE_DATATYPE_S16:
		node->value.i = value;
		WL16(&frame[length], value);
		length += 2;
		break;
	case TREE_NODE_DATATYPE_S32:
		node->value.i = value;
		WL32(&frame[length], value);
		length += 4;
		break;
	case TREE_NODE_DATATYPE_STRING:
	case TREE_NODE_DATATYPE_BINARY:
	case TREE_NODE_DATATYPE_FLOAT:
		return;
	}

	devc->tx.sequence_number = (devc->tx.sequence_number + 1) & 0xFF;
	write_frame(sdi, frame, length);
}

static int32_t get_tree_integer(struct config_tree_node *node)
{
	switch (node->type) {
	case TREE_NODE_DATATYPE_PLAIN:
	case TREE_NODE_DATATYPE_LINK:
		sr_err("Read attempted for dataless node.");
		return 0;
	case TREE_NODE_DATATYPE_CHOOSER:
	case TREE_NODE_DATATYPE_U8:
	case TREE_NODE_DATATYPE_U16:
	case TREE_NODE_DATATYPE_U32:
	case TREE_NODE_DATATYPE_S8:
	case TREE_NODE_DATATYPE_S16:
	case TREE_NODE_DATATYPE_S32:
		return node->value.i;
	case TREE_NODE_DATATYPE_FLOAT:
		return (int)node->value.f;
	default:
		break;
	}

	return 0;
}

static void tree_diagnostic_updated(struct config_tree_node *node, void *param)
{
	(void)param;

	if (!node->value.b->len) {
		sr_warn("Mooshimeter error with no information.");
		return;
	}

	if (node->value.b->data[node->value.b->len]) {
		g_byte_array_set_size(node->value.b, node->value.b->len + 1);
		node->value.b->data[node->value.b->len - 1] = 0;
	}

	sr_warn("Mooshimeter error: %s.", node->value.b->data);
}

static void chX_value_update(struct config_tree_node *node,
	struct sr_dev_inst *sdi, int channel)
{
	struct dev_context *devc = sdi->priv;
	float value;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;

	if (!devc->enable_value_stream)
		return;

	if (!((struct sr_channel *)devc->channel_meaning[channel].
		channels->data)->enabled) {
		return;
	}

	if (node->type != TREE_NODE_DATATYPE_FLOAT)
		return;
	value = node->value.f;

	sr_spew("Received value for channel %d = %g.", channel, value);

	/*
	 * Could do significant digit calculations based on the
	 * effective number of effective bits (sample rate, buffer size, etc),
	 * but does it matter?
	 * (see https://github.com/mooshim/Mooshimeter-AndroidApp/blob/94a20a2d42f6af9975ad48591caa6a17130ca53b/app/src/main/java/com/mooshim/mooshimeter/devices/MooshimeterDevice.java#L691 )
	 */
	sr_analog_init(&analog, &encoding, &meaning, &spec, 2);

	memcpy(analog.meaning, &devc->channel_meaning[channel],
		sizeof(struct sr_analog_meaning));
	analog.num_samples = 1;
	analog.data = &value;
	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	sr_session_send(sdi, &packet);

	if (devc->channel_autorange[channel])
		(*devc->channel_autorange[channel])(sdi, value);

	sr_sw_limits_update_samples_read(&devc->limits, 1);
	if (sr_sw_limits_check(&devc->limits))
		sr_dev_acquisition_stop(sdi);
}

static void chX_buffer_update(struct config_tree_node *node,
	struct sr_dev_inst *sdi, int channel)
{
	struct dev_context *devc = sdi->priv;
	uint32_t bits_per_sample = devc->buffer_bps[channel];
	float output_scalar = devc->buffer_lsb2native[channel];
	uint32_t bytes_per_sample;
	const uint8_t *raw;
	size_t size;
	size_t number_of_samples;
	int32_t unscaled = 0;
	int32_t sign_bit;
	int32_t sign_mask;
	float converted_value = 0;
	float maximum_value = 0;
	float *values;
	float *output_value;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;

	if (!devc->enable_value_stream)
		return;

	if (!((struct sr_channel *)devc->channel_meaning[channel].
		channels->data)->enabled) {
		return;
	}

	if (!bits_per_sample)
		return;
	if (node->type != TREE_NODE_DATATYPE_BINARY)
		return;
	raw = node->value.b->data;
	size = node->value.b->len;
	if (!size)
		return;

	bytes_per_sample = bits_per_sample / 8;
	if (bits_per_sample % 8 != 0)
		bytes_per_sample++;
	if (bytes_per_sample > 4)
		return;
	number_of_samples = size / bytes_per_sample;
	if (!number_of_samples)
		return;

	sr_analog_init(&analog, &encoding, &meaning, &spec, 0);

	values = g_new0(float, number_of_samples);
	output_value = values;

	memcpy(analog.meaning, &devc->channel_meaning[channel],
		sizeof(struct sr_analog_meaning));
	analog.num_samples = number_of_samples;
	analog.data = output_value;
	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;

	sr_spew("Received buffer for channel %d with %u bytes (%u samples).",
		channel, (unsigned int)size, (unsigned int)number_of_samples);

	sign_bit = 1 << (bits_per_sample - 1);
	sign_mask = sign_bit - 1;
	for (; size >= bytes_per_sample; size -= bytes_per_sample,
		raw += bytes_per_sample, output_value++) {
		switch (bytes_per_sample) {
		case 1:
			unscaled = R8(raw);
			break;
		case 2:
			unscaled = RL16(raw);
			break;
		case 3:
			unscaled = ((uint32_t)raw[0]) |
				(((uint32_t)raw[1]) << 8) |
				(((uint32_t)raw[2]) << 16);
			break;
		case 4:
			unscaled = RL32(raw);
			break;
		default:
			break;
		}

		unscaled = (unscaled & sign_mask) - (unscaled & sign_bit);
		converted_value = (float)unscaled * output_scalar;
		*output_value = converted_value;
		if (fabsf(converted_value) > maximum_value)
			maximum_value = fabsf(maximum_value);
	}

	sr_session_send(sdi, &packet);

	g_free(values);

	if (devc->channel_autorange[channel])
		(*devc->channel_autorange[channel])(sdi, maximum_value);

	sr_sw_limits_update_samples_read(&devc->limits, number_of_samples);
	if (sr_sw_limits_check(&devc->limits))
		sr_dev_acquisition_stop(sdi);
}

static void ch1_value_update(struct config_tree_node *node, void *param)
{
	chX_value_update(node, param, 0);
}

static void ch2_value_update(struct config_tree_node *node, void *param)
{
	chX_value_update(node, param, 1);
}

static void power_value_update(struct config_tree_node *node, void *param)
{
	chX_value_update(node, param, 2);
}

static void ch1_buffer_update(struct config_tree_node *node, void *param)
{
	chX_buffer_update(node, param, 0);
}

static void ch2_buffer_update(struct config_tree_node *node, void *param)
{
	chX_buffer_update(node, param, 1);
}

static void ch1_buffer_bps_update(struct config_tree_node *node, void *param)
{
	const struct sr_dev_inst *sdi = param;
	struct dev_context *devc = sdi->priv;
	devc->buffer_bps[0] = (uint32_t)get_tree_integer(node);
}

static void ch2_buffer_bps_update(struct config_tree_node *node, void *param)
{
	const struct sr_dev_inst *sdi = param;
	struct dev_context *devc = sdi->priv;
	devc->buffer_bps[1] = (uint32_t)get_tree_integer(node);
}

static void ch1_buffer_lsb2native_update(struct config_tree_node *node,
	void *param)
{
	const struct sr_dev_inst *sdi = param;
	struct dev_context *devc = sdi->priv;
	if (node->type != TREE_NODE_DATATYPE_BINARY)
		return;
	devc->buffer_lsb2native[0] = node->value.f;
}

static void ch2_buffer_lsb2native_update(struct config_tree_node *node,
	void *param)
{
	const struct sr_dev_inst *sdi = param;
	struct dev_context *devc = sdi->priv;
	if (node->type != TREE_NODE_DATATYPE_BINARY)
		return;
	devc->buffer_lsb2native[1] = node->value.f;
}

static void release_tree_node(struct config_tree_node *node)
{
	g_free(node->name);

	switch (node->type) {
	case TREE_NODE_DATATYPE_STRING:
	case TREE_NODE_DATATYPE_BINARY:
		g_byte_array_free(node->value.b, TRUE);
		break;
	default:
		break;
	}

	for (size_t i = 0; i < node->count_children; i++)
		release_tree_node(node->children + i);
	g_free(node->children);
}

static void allocate_startup_tree(struct dev_context *devc)
{
	struct config_tree_node *node;

	node = &devc->tree_root;
	node->name = g_strdup("ADMIN");
	node->type = TREE_NODE_DATATYPE_PLAIN;
	node->count_children = 3;
	node->children = g_new0(struct config_tree_node, node->count_children);

	node = &devc->tree_root.children[0];
	node->name = g_strdup("CRC");
	node->type = TREE_NODE_DATATYPE_U32;
	node->id = 0;
	devc->tree_id_lookup[node->id] = node;

	node = &devc->tree_root.children[1];
	node->name = g_strdup("TREE");
	node->type = TREE_NODE_DATATYPE_BINARY;
	node->value.b = g_byte_array_new();
	node->id = 1;
	devc->tree_id_lookup[node->id] = node;

	node = &devc->tree_root.children[2];
	node->name = g_strdup("DIAGNOSTIC");
	node->type = TREE_NODE_DATATYPE_STRING;
	node->value.b = g_byte_array_new();
	node->id = 2;
	devc->tree_id_lookup[node->id] = node;
}

static gboolean tree_node_has_id(struct config_tree_node *node)
{
	switch (node->type) {
	case TREE_NODE_DATATYPE_PLAIN:
	case TREE_NODE_DATATYPE_LINK:
		return FALSE;
	default:
		break;
	}

	return TRUE;
}

static int deserialize_tree(struct dev_context *devc,
	struct config_tree_node *node,
	int *id, const uint8_t **data, size_t *size)
{
	size_t n;
	int res;

	if (*size < 2)
		return SR_ERR_DATA;

	n = R8(*data);
	*data += 1;
	*size -= 1;
	if (n > TREE_NODE_DATATYPE_FLOAT)
		return SR_ERR_DATA;
	node->type = n;

	switch (node->type) {
	case TREE_NODE_DATATYPE_STRING:
	case TREE_NODE_DATATYPE_BINARY:
		node->value.b = g_byte_array_new();
		break;
	default:
		break;
	}

	n = R8(*data);
	*data += 1;
	*size -= 1;
	if (n > *size)
		return SR_ERR_DATA;
	node->name = g_strndup((const char *)(*data), n);
	*data += n;
	*size -= n;

	if (!(*size))
		return SR_ERR_DATA;

	if (tree_node_has_id(node)) {
		node->id = *id;
		(*id)++;
		devc->tree_id_lookup[node->id] = node;
	}

	n = R8(*data);
	*data += 1;
	*size -= 1;

	if (n) {
		node->count_children = n;
		node->children = g_new0(struct config_tree_node, n);

		for (size_t i = 0; i < n; i++) {
			if ((res = deserialize_tree(devc,
				node->children + i, id,
				data, size)) != SR_OK) {
				return res;
			}
			node->children[i].index_in_parent = i;
		}
	}

	return SR_OK;
}

static int wait_for_update(const struct sr_dev_inst *sdi,
	struct config_tree_node *node,
	uint32_t original_update_number)
{
	struct sr_bt_desc *desc = sdi->conn;
	int ret;
	gint64 start_time;

	start_time = g_get_monotonic_time();
	for (;;) {
		ret = sr_bt_check_notify(desc);
		if (ret < 0)
			return SR_ERR;

		if (node->update_number != original_update_number)
			return SR_OK;

		if (g_get_monotonic_time() - start_time > 5 * 1000 * 1000)
			break;

		if (ret > 0)
			continue;

		/* Nothing pollable, so just sleep a bit and try again. */
		g_usleep(50 * 1000);
	}

	return SR_ERR_TIMEOUT;
}

static void install_update_handlers(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	struct config_tree_node *target;

	target = lookup_tree_path(devc, "CH1:VALUE");
	if (target) {
		target->on_update = ch1_value_update;
		target->on_update_param = sdi;
	} else {
		sr_warn("No tree path for channel 1 values.");
	}

	target = lookup_tree_path(devc, "CH1:BUF");
	if (target) {
		target->on_update = ch1_buffer_update;
		target->on_update_param = sdi;
	} else {
		sr_warn("No tree path for channel 1 buffer.");
	}

	target = lookup_tree_path(devc, "CH1:BUF_BPS");
	if (target) {
		target->on_update = ch1_buffer_bps_update;
		target->on_update_param = sdi;
	} else {
		sr_warn("No tree path for channel 1 buffer BPS.");
	}

	target = lookup_tree_path(devc, "CH1:BUF_LSB2NATIVE");
	if (target) {
		target->on_update = ch1_buffer_lsb2native_update;
		target->on_update_param = sdi;
	} else {
		sr_warn("No tree path for channel 1 buffer conversion factor.");
	}

	target = lookup_tree_path(devc, "CH2:VALUE");
	if (target) {
		target->on_update = ch2_value_update;
		target->on_update_param = sdi;
	} else {
		sr_warn("No tree path for channel 2 values.");
	}

	target = lookup_tree_path(devc, "CH2:BUF");
	if (target) {
		target->on_update = ch2_buffer_update;
		target->on_update_param = sdi;
	} else {
		sr_warn("No tree path for channel 2 buffer.");
	}

	target = lookup_tree_path(devc, "CH2:BUF_BPS");
	if (target) {
		target->on_update = ch2_buffer_bps_update;
		target->on_update_param = sdi;
	} else {
		sr_warn("No tree path for channel 2 buffer BPS.");
	}

	target = lookup_tree_path(devc, "CH2:BUF_LSB2NATIVE");
	if (target) {
		target->on_update = ch2_buffer_lsb2native_update;
		target->on_update_param = sdi;
	} else {
		sr_warn("No tree path for channel 2 buffer conversion factor.");
	}

	target = lookup_tree_path(devc, "REAL_PWR");
	if (target) {
		target->on_update = power_value_update;
		target->on_update_param = sdi;
	} else {
		sr_warn("No tree path for real power.");
	}
}

struct startup_context {
	struct sr_dev_inst *sdi;
	uint32_t crc;
	int result;
	gboolean running;
};

static void startup_failed(struct startup_context *ctx, int err)
{
	sr_dbg("Startup handshake failed: %s.", sr_strerror(err));

	ctx->result = err;
	ctx->running = FALSE;
}

static void startup_complete(struct startup_context *ctx)
{
	sr_dbg("Startup handshake completed.");

	install_update_handlers(ctx->sdi);

	ctx->running = FALSE;
}

static int startup_run(struct startup_context *ctx)
{
	struct sr_bt_desc *desc = ctx->sdi->conn;
	int ret;
	gint64 start_time;

	ctx->result = SR_OK;
	ctx->running = TRUE;

	start_time = g_get_monotonic_time();
	for (;;) {
		ret = sr_bt_check_notify(desc);
		if (ret < 0)
			return SR_ERR;

		if (!ctx->running)
			return ctx->result;

		if (g_get_monotonic_time() - start_time > 30 * 1000 * 1000)
			break;

		if (ret > 0)
			continue;

		/* Nothing pollable, so just sleep a bit and try again. */
		g_usleep(50 * 1000);
	}

	return SR_ERR_TIMEOUT;
}

static void startup_tree_crc_updated(struct config_tree_node *node, void *param)
{
	struct startup_context *ctx = param;
	uint32_t result;

	node->on_update = NULL;

	result = (uint32_t)get_tree_integer(node);
	if (result != ctx->crc) {
		sr_err("Tree CRC mismatch, expected %08X but received %08X.",
			ctx->crc, result);
		startup_failed(ctx, SR_ERR_DATA);
		return;
	}

	startup_complete(ctx);
}

static void startup_send_tree_crc(struct startup_context *ctx)
{
	struct dev_context *devc = ctx->sdi->priv;
	struct config_tree_node *target;

	if (!(target = lookup_tree_path(devc, "ADMIN:CRC32"))) {
		sr_err("ADMIN:CRC32 node not found in received startup tree.");
		startup_failed(ctx, SR_ERR_DATA);
		return;
	}

	target->on_update = startup_tree_crc_updated;
	target->on_update_param = ctx;

	set_tree_integer(ctx->sdi, target, ctx->crc);
}

static uint32_t crc32(const uint8_t *ptr, size_t size)
{
	uint32_t result = 0xFFFFFFFF;
	uint32_t t;
	for (; size; size--, ptr++) {
		result ^= *ptr;
		for (int i = 0; i < 8; i++) {
			t = result & 1;
			result >>= 1;
			if (t)
				result ^= 0xEDB88320;
		}
	}

	return ~result;
}

static void startup_tree_updated(struct config_tree_node *node, void *param)
{
	struct startup_context *ctx = param;
	struct dev_context *devc = ctx->sdi->priv;

	GConverter *decompressor;
	GConverterResult decompress_result;
	GByteArray *tree_data;
	gsize input_read;
	gsize output_size;
	GError *err = NULL;
	int res;
	int id;
	const uint8_t *data;
	size_t size;
	struct config_tree_node *target;

	ctx->crc = crc32(node->value.b->data, node->value.b->len);

	tree_data = g_byte_array_new();
	g_byte_array_set_size(tree_data, 4096);
	decompressor = (GConverter *)g_zlib_decompressor_new(
		G_ZLIB_COMPRESSOR_FORMAT_ZLIB);
	for (;;) {
		g_converter_reset(decompressor);
		decompress_result = g_converter_convert(decompressor,
			node->value.b->data,
			node->value.b->len,
			tree_data->data,
			tree_data->len,
			G_CONVERTER_INPUT_AT_END,
			&input_read,
			&output_size,
			&err);
		if (decompress_result == G_CONVERTER_FINISHED)
			break;
		if (decompress_result == G_CONVERTER_ERROR) {
			if (err->code == G_IO_ERROR_NO_SPACE &&
				tree_data->len < 1024 * 1024) {
				g_byte_array_set_size(tree_data,
					tree_data->len * 2);
				continue;
			}
			sr_err("Tree decompression failed: %s.", err->message);
		} else {
			sr_err("Tree decompression error %d.",
				(int)decompress_result);
		}
		startup_failed(ctx, SR_ERR_DATA);
		return;
	}
	g_object_unref(decompressor);

	sr_dbg("Config tree received (%d -> %d bytes) with CRC %08X.",
		node->value.b->len, (int)output_size,
		ctx->crc);

	release_tree_node(&devc->tree_root);
	memset(&devc->tree_root, 0, sizeof(struct config_tree_node));
	memset(devc->tree_id_lookup, 0, sizeof(devc->tree_id_lookup));

	id = 0;
	data = tree_data->data;
	size = output_size;
	res = deserialize_tree(devc, &devc->tree_root, &id, &data, &size);
	g_byte_array_free(tree_data, TRUE);

	if (res != SR_OK) {
		sr_err("Tree deserialization failed.");
		startup_failed(ctx, res);
		return;
	}

	if ((target = lookup_tree_path(devc, "ADMIN:DIAGNOSTIC"))) {
		target->on_update = tree_diagnostic_updated;
		target->on_update_param = ctx->sdi;
	}

	startup_send_tree_crc(ctx);
}

static void release_rx_buffer(void *data)
{
	GByteArray *ba = data;
	if (!ba)
		return;
	g_byte_array_free(ba, TRUE);
}

SR_PRIV int mooshimeter_dmm_open(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	struct sr_bt_desc *desc = sdi->conn;
	struct startup_context ctx;
	int ret;

	release_tree_node(&devc->tree_root);
	memset(&devc->tree_root, 0, sizeof(struct config_tree_node));
	memset(devc->tree_id_lookup, 0, sizeof(devc->tree_id_lookup));

	g_slist_free_full(devc->rx.reorder_buffer, release_rx_buffer);
	devc->rx.reorder_buffer = NULL;
	if (devc->rx.contents)
		devc->rx.contents->len = 0;
	else
		devc->rx.contents = g_byte_array_new();
	devc->rx.sequence_number = -1;
	devc->tx.sequence_number = 0;

	ret = sr_bt_config_cb_data(desc, notify_cb, (void *)sdi);
	if (ret < 0)
		return SR_ERR;

	ret = sr_bt_connect_ble(desc);
	if (ret < 0)
		return SR_ERR;

	ret = sr_bt_start_notify(desc);
	if (ret < 0)
		return SR_ERR;

	memset(&ctx, 0, sizeof(ctx));
	ctx.sdi = (struct sr_dev_inst *)sdi;

	allocate_startup_tree(devc);
	devc->tree_id_lookup[1]->on_update = startup_tree_updated;
	devc->tree_id_lookup[1]->on_update_param = &ctx;
	devc->tree_id_lookup[2]->on_update = tree_diagnostic_updated;
	devc->tree_id_lookup[2]->on_update_param = (struct sr_dev_inst *)sdi;

	sr_spew("Initiating startup handshake.");

	ret = poll_tree_value(sdi, devc->tree_id_lookup[1]);
	if (ret != SR_OK)
		return ret;

	return startup_run(&ctx);
}

SR_PRIV int mooshimeter_dmm_close(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	struct sr_bt_desc *desc = sdi->conn;

	sr_bt_disconnect(desc);

	release_tree_node(&devc->tree_root);
	memset(&devc->tree_root, 0, sizeof(struct config_tree_node));
	memset(devc->tree_id_lookup, 0, sizeof(devc->tree_id_lookup));

	g_slist_free_full(devc->rx.reorder_buffer, release_rx_buffer);
	devc->rx.reorder_buffer = NULL;
	if (devc->rx.contents)
		g_byte_array_free(devc->rx.contents, TRUE);
	devc->rx.contents = NULL;

	return SR_OK;
}

SR_PRIV int mooshimeter_dmm_set_chooser(const struct sr_dev_inst *sdi,
	const char *path, const char *choice)
{
	struct dev_context *devc = sdi->priv;
	struct config_tree_node *target;
	int value;
	uint32_t original_update_number;

	value = lookup_chooser_index(devc, choice);
	if (value == -1) {
		sr_err("Value %s not found for chooser %s.", choice, path);
		return SR_ERR_DATA;
	}

	target = lookup_tree_path(devc, path);
	if (!target) {
		sr_err("Tree path %s not found.", path);
		return SR_ERR_DATA;
	}

	sr_spew("Setting chooser %s to %s (%d).", path, choice, value);

	original_update_number = target->update_number;
	set_tree_integer(sdi, target, value);
	return wait_for_update(sdi, target, original_update_number);
}

SR_PRIV int mooshimeter_dmm_set_integer(const struct sr_dev_inst *sdi,
	const char *path, int value)
{
	struct dev_context *devc = sdi->priv;
	struct config_tree_node *target;
	uint32_t original_update_number;

	target = lookup_tree_path(devc, path);
	if (!target) {
		sr_err("Tree path %s not found.", path);
		return SR_ERR_DATA;
	}

	sr_spew("Setting integer %s to %d.", path, value);

	original_update_number = target->update_number;
	set_tree_integer(sdi, target, value);
	return wait_for_update(sdi, target, original_update_number);
}

static struct config_tree_node *select_next_largest_in_tree(
	struct dev_context *devc,
	const char *parent, float number)
{
	float node_value;
	float distance;
	float best_distance = 0;
	struct config_tree_node *choice_parent;
	struct config_tree_node *selected_choice = NULL;

	choice_parent = lookup_tree_path(devc, parent);
	if (!choice_parent) {
		sr_err("Tree path %s not found.", parent);
		return NULL;
	}
	if (!choice_parent->count_children) {
		sr_err("Tree path %s has no children.", parent);
		return NULL;
	}

	for (size_t i = 0; i < choice_parent->count_children; i++) {
		node_value = strtof(choice_parent->children[i].name, NULL);
		if (node_value <= 0)
			continue;
		distance = node_value - number;
		if (!selected_choice) {
			selected_choice = &choice_parent->children[i];
			best_distance = distance;
			continue;
		}
		/* Select the one that's the least below it, if all
		 * are below the target */
		if (distance < 0) {
			if (best_distance > 0)
				continue;
			if (distance > best_distance) {
				selected_choice = &choice_parent->children[i];
				best_distance = distance;
			}
			continue;
		}
		if (best_distance < 0 || distance < best_distance) {
			selected_choice = &choice_parent->children[i];
			best_distance = distance;
		}
	}

	return selected_choice;
}

SR_PRIV int mooshimeter_dmm_set_larger_number(const struct sr_dev_inst *sdi,
	const char *path, const char *parent, float number)
{
	struct dev_context *devc = sdi->priv;
	struct config_tree_node *selected_choice;
	struct config_tree_node *target;
	uint32_t original_update_number;

	selected_choice = select_next_largest_in_tree(devc, parent, number);
	if (!selected_choice) {
		sr_err("No choice available for %f at %s.", number, parent);
		return SR_ERR_NA;
	}

	target = lookup_tree_path(devc, path);
	if (!target) {
		sr_err("Tree path %s not found.", path);
		return SR_ERR_DATA;
	}

	sr_spew("Setting number choice %s to index %d for requested %g.", path,
		(int)selected_choice->index_in_parent, number);

	original_update_number = target->update_number;
	set_tree_integer(sdi, target, selected_choice->index_in_parent);
	return wait_for_update(sdi, target, original_update_number);
}

SR_PRIV gboolean mooshimeter_dmm_set_autorange(const struct sr_dev_inst *sdi,
	const char *path, const char *parent, float latest)
{
	struct dev_context *devc = sdi->priv;
	struct config_tree_node *selected_choice;
	struct config_tree_node *target;

	selected_choice = select_next_largest_in_tree(devc, parent,
		fabsf(latest));
	if (!selected_choice) {
		sr_err("No choice available for %f at %s.", latest, parent);
		return FALSE;
	}

	target = lookup_tree_path(devc, path);
	if (!target) {
		sr_err("Tree path %s not found.", path);
		return FALSE;
	}

	if (get_tree_integer(target) == (int)selected_choice->index_in_parent)
		return FALSE;

	sr_spew("Changing autorange %s to index %d for %g.", path,
		(int)selected_choice->index_in_parent, latest);

	set_tree_integer(sdi, target, selected_choice->index_in_parent);

	return TRUE;
}

SR_PRIV int mooshimeter_dmm_get_chosen_number(const struct sr_dev_inst *sdi,
	const char *path, const char *parent, float *number)
{
	struct dev_context *devc = sdi->priv;
	struct config_tree_node *value_node;
	struct config_tree_node *available;
	int32_t selected;

	value_node = lookup_tree_path(devc, path);
	if (!value_node) {
		sr_err("Tree path %s not found.", path);
		return SR_ERR_DATA;
	}

	available = lookup_tree_path(devc, parent);
	if (!available) {
		sr_err("Tree path %s not found.", path);
		return SR_ERR_DATA;
	}

	selected = get_tree_integer(value_node);
	if (selected < 0 || selected >= (int32_t)available->count_children)
		return SR_ERR_DATA;

	*number = g_ascii_strtod(available->children[selected].name, NULL);

	return SR_OK;
}

SR_PRIV int mooshimeter_dmm_get_available_number_choices(
	const struct sr_dev_inst *sdi, const char *path,
	float **numbers, size_t *count)
{
	struct dev_context *devc = sdi->priv;
	struct config_tree_node *available;

	available = lookup_tree_path(devc, path);
	if (!available) {
		sr_err("Tree path %s not found.", path);
		return SR_ERR_NA;
	}

	*numbers = g_malloc(sizeof(float) * available->count_children);
	*count = available->count_children;

	for (size_t i = 0; i < available->count_children; i++) {
		(*numbers)[i] = g_ascii_strtod(available->children[i].name,
			NULL);
	}

	return SR_OK;
}

SR_PRIV int mooshimeter_dmm_poll(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct sr_bt_desc *desc;

	(void)fd;
	(void)revents;

	if (!(sdi = cb_data))
		return TRUE;

	desc = sdi->conn;

	while (sr_bt_check_notify(desc) > 0);

	return TRUE;
}

/*
 * The meter will disconnect if it doesn't receive a host command for 30 (?)
 * seconds, so periodically poll a trivial value to keep it alive.
 */
SR_PRIV int mooshimeter_dmm_heartbeat(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct config_tree_node *target;

	(void)fd;
	(void)revents;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	target = lookup_tree_path(devc, "PCB_VERSION");
	if (!target) {
		sr_err("Tree for PCB_VERSION not found.");
		return FALSE;
	}

	sr_spew("Sending heartbeat request.");
	poll_tree_value(sdi, target);

	return TRUE;
}
