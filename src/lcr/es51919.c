/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 Janne Huttunen <jahuttun@gmail.com>
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

#include <stdint.h>
#include <string.h>
#include <math.h>
#include <glib.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"

#define LOG_PREFIX "es51919"

struct dev_buffer {
	/** Total size of the buffer. */
	size_t size;
	/** Amount of data currently in the buffer. */
	size_t len;
	/** Offset where the data starts in the buffer. */
	size_t offset;
	/** Space for the data. */
	uint8_t data[];
};

static struct dev_buffer *dev_buffer_new(size_t size)
{
	struct dev_buffer *dbuf;

	if (!(dbuf = g_try_malloc(sizeof(struct dev_buffer) + size))) {
		sr_err("Dev buffer malloc failed (size=%zu).", size);
		return NULL;
	}

	dbuf->size = size;
	dbuf->len = 0;
	dbuf->offset = 0;

	return dbuf;
}

static void dev_buffer_destroy(struct dev_buffer *dbuf)
{
	g_free(dbuf);
}

static int dev_buffer_fill_serial(struct dev_buffer *dbuf,
				  struct sr_dev_inst *sdi)
{
	struct sr_serial_dev_inst *serial;
	int len;

	serial = sdi->conn;

	/* If we already have data, move it to the beginning of the buffer. */
	if (dbuf->len > 0 && dbuf->offset > 0)
		memmove(dbuf->data, dbuf->data + dbuf->offset, dbuf->len);

	dbuf->offset = 0;

	len = dbuf->size - dbuf->len;
	len = serial_read_nonblocking(serial, dbuf->data + dbuf->len, len);
	if (len < 0) {
		sr_err("Serial port read error: %d.", len);
		return len;
	}

	dbuf->len += len;

	return SR_OK;
}

static uint8_t *dev_buffer_packet_find(struct dev_buffer *dbuf,
				gboolean (*packet_valid)(const uint8_t *),
				size_t packet_size)
{
	size_t offset;

	while (dbuf->len >= packet_size) {
		if (packet_valid(dbuf->data + dbuf->offset)) {
			offset = dbuf->offset;
			dbuf->offset += packet_size;
			dbuf->len -= packet_size;
			return dbuf->data + offset;
		}
		dbuf->offset++;
		dbuf->len--;
	}

	return NULL;
}

struct dev_sample_counter {
	/** The current number of already received samples. */
	uint64_t count;
	/** The current sampling limit (in number of samples). */
	uint64_t limit;
};

static void dev_sample_counter_start(struct dev_sample_counter *cnt)
{
	cnt->count = 0;
}

static void dev_sample_counter_inc(struct dev_sample_counter *cnt)
{
	cnt->count++;
}

static void dev_sample_limit_set(struct dev_sample_counter *cnt, uint64_t limit)
{
	cnt->limit = limit;
}

static gboolean dev_sample_limit_reached(struct dev_sample_counter *cnt)
{
	if (cnt->limit && cnt->count >= cnt->limit) {
		sr_info("Requested sample limit reached.");
		return TRUE;
	}

	return FALSE;
}

struct dev_time_counter {
	/** The starting time of current sampling run. */
	int64_t starttime;
	/** The time limit (in milliseconds). */
	uint64_t limit;
};

static void dev_time_counter_start(struct dev_time_counter *cnt)
{
	cnt->starttime = g_get_monotonic_time();
}

static void dev_time_limit_set(struct dev_time_counter *cnt, uint64_t limit)
{
	cnt->limit = limit;
}

static gboolean dev_time_limit_reached(struct dev_time_counter *cnt)
{
	int64_t time;

	if (cnt->limit) {
		time = (g_get_monotonic_time() - cnt->starttime) / 1000;
		if (time > (int64_t)cnt->limit) {
			sr_info("Requested time limit reached.");
			return TRUE;
		}
	}

	return FALSE;
}

static void serial_conf_get(GSList *options, const char *def_serialcomm,
			    const char **conn, const char **serialcomm)
{
	struct sr_config *src;
	GSList *l;

	*conn = *serialcomm = NULL;
	for (l = options; l; l = l->next) {
		src = l->data;
		switch (src->key) {
		case SR_CONF_CONN:
			*conn = g_variant_get_string(src->data, NULL);
			break;
		case SR_CONF_SERIALCOMM:
			*serialcomm = g_variant_get_string(src->data, NULL);
			break;
		}
	}

	if (*serialcomm == NULL)
		*serialcomm = def_serialcomm;
}

static struct sr_serial_dev_inst *serial_dev_new(GSList *options,
						 const char *def_serialcomm)

{
	const char *conn, *serialcomm;

	serial_conf_get(options, def_serialcomm, &conn, &serialcomm);

	if (!conn)
		return NULL;

	return sr_serial_dev_inst_new(conn, serialcomm);
}

static int serial_stream_check_buf(struct sr_serial_dev_inst *serial,
				   uint8_t *buf, size_t buflen,
				   size_t packet_size,
				   packet_valid_callback is_valid,
				   uint64_t timeout_ms, int baudrate)
{
	size_t len, dropped;
	int ret;

	if ((ret = serial_open(serial, SERIAL_RDWR)) != SR_OK)
		return ret;

	serial_flush(serial);

	len = buflen;
	ret = serial_stream_detect(serial, buf, &len, packet_size,
				   is_valid, timeout_ms, baudrate);

	serial_close(serial);

	if (ret != SR_OK)
		return ret;

	/*
	 * If we dropped more than two packets worth of data, something is
	 * wrong. We shouldn't quit however, since the dropped bytes might be
	 * just zeroes at the beginning of the stream. Those can occur as a
	 * combination of the nonstandard cable that ships with some devices
	 * and the serial port or USB to serial adapter.
	 */
	dropped = len - packet_size;
	if (dropped > 2 * packet_size)
		sr_warn("Had to drop too much data.");

	return SR_OK;
}

static int serial_stream_check(struct sr_serial_dev_inst *serial,
			       size_t packet_size,
			       packet_valid_callback is_valid,
			       uint64_t timeout_ms, int baudrate)
{
	uint8_t buf[128];

	return serial_stream_check_buf(serial, buf, sizeof(buf), packet_size,
				       is_valid, timeout_ms, baudrate);
}

struct std_opt_desc {
	const uint32_t *scanopts;
	const int num_scanopts;
	const uint32_t *devopts;
	const int num_devopts;
};

static int std_config_list(uint32_t key, GVariant **data,
			   const struct std_opt_desc *d)
{
	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
			d->scanopts, d->num_scanopts, sizeof(uint32_t));
		break;
	case SR_CONF_DEVICE_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
			d->devopts, d->num_devopts, sizeof(uint32_t));
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int send_config_update(struct sr_dev_inst *sdi, struct sr_config *cfg)
{
	struct sr_datafeed_packet packet;
	struct sr_datafeed_meta meta;

	memset(&meta, 0, sizeof(meta));

	packet.type = SR_DF_META;
	packet.payload = &meta;

	meta.config = g_slist_append(meta.config, cfg);

	return sr_session_send(sdi, &packet);
}

/*
 * Cyrustek ES51919 LCR chipset host protocol.
 *
 * Public official documentation does not contain the protocol
 * description, so this is all based on reverse engineering.
 *
 * Packet structure (17 bytes):
 *
 * 0x00: header1 ?? (0x00)
 * 0x01: header2 ?? (0x0d)
 *
 * 0x02: flags
 *         bit 0 = hold enabled
 *         bit 1 = reference shown (in delta mode)
 *         bit 2 = delta mode
 *         bit 3 = calibration mode
 *         bit 4 = sorting mode
 *         bit 5 = LCR mode
 *         bit 6 = auto mode
 *         bit 7 = parallel measurement (vs. serial)
 *
 * 0x03: config
 *         bit 0-4 = ??? (0x10)
 *         bit 5-7 = test frequency
 *                     0 = 100 Hz
 *                     1 = 120 Hz
 *                     2 = 1 kHz
 *                     3 = 10 kHz
 *                     4 = 100 kHz
 *                     5 = 0 Hz (DC)
 *
 * 0x04: tolerance (sorting mode)
 *         0 = not set
 *         3 = +-0.25%
 *         4 = +-0.5%
 *         5 = +-1%
 *         6 = +-2%
 *         7 = +-5%
 *         8 = +-10%
 *         9 = +-20%
 *        10 = -20+80%
 *
 * 0x05-0x09: primary measurement
 *   0x05: measured quantity
 *           1 = inductance
 *           2 = capacitance
 *           3 = resistance
 *           4 = DC resistance
 *   0x06: measurement MSB  (0x4e20 = 20000 = outside limits)
 *   0x07: measurement LSB
 *   0x08: measurement info
 *           bit 0-2 = decimal point multiplier (10^-val)
 *           bit 3-7 = unit
 *                       0 = no unit
 *                       1 = Ohm
 *                       2 = kOhm
 *                       3 = MOhm
 *                       5 = uH
 *                       6 = mH
 *                       7 = H
 *                       8 = kH
 *                       9 = pF
 *                       10 = nF
 *                       11 = uF
 *                       12 = mF
 *                       13 = %
 *                       14 = degree
 *   0x09: measurement status
 *           bit 0-3 = status
 *                       0 = normal (measurement shown)
 *                       1 = blank (nothing shown)
 *                       2 = lines ("----")
 *                       3 = ouside limits ("OL")
 *                       7 = pass ("PASS")
 *                       8 = fail ("FAIL")
 *                       9 = open ("OPEn")
 *                      10 = shorted ("Srt")
 *           bit 4-6 = ??? (maybe part of same field with 0-3)
 *           bit 7   = ??? (some independent flag)
 *
 * 0x0a-0x0e: secondary measurement
 *   0x0a: measured quantity
 *           0 = none
 *           1 = dissipation factor
 *           2 = quality factor
 *           3 = parallel AC resistance / ESR
 *           4 = phase angle
 *   0x0b-0x0e: like primary measurement
 *
 * 0x0f: footer1 (0x0d) ?
 * 0x10: footer2 (0x0a) ?
 */

#define PACKET_SIZE 17

static const uint64_t frequencies[] = {
	100, 120, 1000, 10000, 100000, 0,
};

/** Private, per-device-instance driver context. */
struct dev_context {
	/** Opaque pointer passed in by the frontend. */
	void *cb_data;

	/** The number of samples. */
	struct dev_sample_counter sample_count;

	/** The time limit counter. */
	struct dev_time_counter time_count;

	/** Data buffer. */
	struct dev_buffer *buf;

	/** The frequency of the test signal (index to frequencies[]). */
	unsigned int freq;
};

static int parse_mq(const uint8_t *buf, int is_secondary, int is_parallel)
{
	switch (is_secondary << 8 | buf[0]) {
	case 0x001:
		return is_parallel ?
			SR_MQ_PARALLEL_INDUCTANCE : SR_MQ_SERIAL_INDUCTANCE;
	case 0x002:
		return is_parallel ?
			SR_MQ_PARALLEL_CAPACITANCE : SR_MQ_SERIAL_CAPACITANCE;
	case 0x003:
	case 0x103:
		return is_parallel ?
			SR_MQ_PARALLEL_RESISTANCE : SR_MQ_SERIAL_RESISTANCE;
	case 0x004:
		return SR_MQ_RESISTANCE;
	case 0x100:
		return SR_MQ_DIFFERENCE;
	case 0x101:
		return SR_MQ_DISSIPATION_FACTOR;
	case 0x102:
		return SR_MQ_QUALITY_FACTOR;
	case 0x104:
		return SR_MQ_PHASE_ANGLE;
	}

	sr_err("Unknown quantity 0x%03x.", is_secondary << 8 | buf[0]);

	return -1;
}

static float parse_value(const uint8_t *buf)
{
	static const float decimals[] = {
		1, 1e-1, 1e-2, 1e-3, 1e-4, 1e-5, 1e-6, 1e-7
	};
	int16_t val;

	val = (buf[1] << 8) | buf[2];
	return (float)val * decimals[buf[3] & 7];
}

static void parse_measurement(const uint8_t *pkt, float *floatval,
			      struct sr_datafeed_analog *analog,
			      int is_secondary)
{
	static const struct {
		int unit;
		float mult;
	} units[] = {
		{ SR_UNIT_UNITLESS, 1 },	/* no unit */
		{ SR_UNIT_OHM, 1 },		/* Ohm     */
		{ SR_UNIT_OHM, 1e3 },		/* kOhm    */
		{ SR_UNIT_OHM, 1e6 },		/* MOhm    */
		{ -1, 0 },			/* ???     */
		{ SR_UNIT_HENRY, 1e-6 },	/* uH      */
		{ SR_UNIT_HENRY, 1e-3 },	/* mH      */
		{ SR_UNIT_HENRY, 1 },		/* H       */
		{ SR_UNIT_HENRY, 1e3 },		/* kH      */
		{ SR_UNIT_FARAD, 1e-12 },	/* pF      */
		{ SR_UNIT_FARAD, 1e-9 },	/* nF      */
		{ SR_UNIT_FARAD, 1e-6 },	/* uF      */
		{ SR_UNIT_FARAD, 1e-3 },	/* mF      */
		{ SR_UNIT_PERCENTAGE, 1 },	/* %       */
		{ SR_UNIT_DEGREE, 1 }		/* degree  */
	};
	const uint8_t *buf;
	int state;

	buf = pkt + (is_secondary ? 10 : 5);

	analog->mq = -1;
	analog->mqflags = 0;

	state = buf[4] & 0xf;

	if (state != 0 && state != 3)
		return;

	if (pkt[2] & 0x18) {
		/* Calibration and Sorting modes not supported. */
		return;
	}

	if (!is_secondary) {
		if (pkt[2] & 0x01)
			analog->mqflags |= SR_MQFLAG_HOLD;
		if (pkt[2] & 0x02)
			analog->mqflags |= SR_MQFLAG_REFERENCE;
		if (pkt[2] & 0x20)
			analog->mqflags |= SR_MQFLAG_AUTOMQ;
		if (pkt[2] & 0x40)
			analog->mqflags |= SR_MQFLAG_AUTOMODEL;
	} else {
		if (pkt[2] & 0x04)
			analog->mqflags |= SR_MQFLAG_RELATIVE;
	}

	if ((analog->mq = parse_mq(buf, is_secondary, pkt[2] & 0x80)) < 0)
		return;

	if ((buf[3] >> 3) >= ARRAY_SIZE(units)) {
		sr_err("Unknown unit %u.", buf[3] >> 3);
		analog->mq = -1;
		return;
	}

	analog->unit = units[buf[3] >> 3].unit;

	*floatval = parse_value(buf);
	*floatval *= (state == 0) ? units[buf[3] >> 3].mult : INFINITY;
}

static unsigned int parse_frequency(const uint8_t *pkt)
{
	unsigned int freq;

	freq = pkt[3] >> 5;

	if (freq >= ARRAY_SIZE(frequencies)) {
		sr_err("Unknown frequency %u.", freq);
		freq = ARRAY_SIZE(frequencies) - 1;
	}

	return freq;
}

static gboolean packet_valid(const uint8_t *pkt)
{
	/*
	 * If the first two bytes of the packet are indeed a constant
	 * header, they should be checked too. Since we don't know it
	 * for sure, we'll just check the last two for now since they
	 * seem to be constant just like in the other Cyrustek chipset
	 * protocols.
	 */
	if (pkt[15] == 0xd && pkt[16] == 0xa)
		return TRUE;

	return FALSE;
}

static int send_frequency_update(struct sr_dev_inst *sdi, unsigned int freq)
{
	struct sr_config *cfg;
	struct dev_context *devc;
	int ret;

	devc = sdi->priv;

	cfg = sr_config_new(SR_CONF_OUTPUT_FREQUENCY,
			    g_variant_new_uint64(frequencies[freq]));

	if (!cfg)
		return SR_ERR;

	ret = send_config_update(devc->cb_data, cfg);
	sr_config_free(cfg);

	return ret;
}

static void handle_packet(struct sr_dev_inst *sdi, const uint8_t *pkt)
{
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct dev_context *devc;
	unsigned int freq;
	float floatval;
	int count;

	devc = sdi->priv;

	freq = parse_frequency(pkt);
	if (freq != devc->freq) {
		if (send_frequency_update(sdi, freq) == SR_OK)
			devc->freq = freq;
		else
			return;
	}

	count = 0;

	memset(&analog, 0, sizeof(analog));

	analog.num_samples = 1;
	analog.data = &floatval;

	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;

	analog.channels = g_slist_append(NULL, sdi->channels->data);

	parse_measurement(pkt, &floatval, &analog, 0);
	if (analog.mq >= 0) {
		if (sr_session_send(devc->cb_data, &packet) == SR_OK)
			count++;
	}

	analog.channels = g_slist_append(NULL, sdi->channels->next->data);

	parse_measurement(pkt, &floatval, &analog, 1);
	if (analog.mq >= 0) {
		if (sr_session_send(devc->cb_data, &packet) == SR_OK)
			count++;
	}

	if (count > 0)
		dev_sample_counter_inc(&devc->sample_count);
}

static int handle_new_data(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	uint8_t *pkt;
	int ret;

	devc = sdi->priv;

	ret = dev_buffer_fill_serial(devc->buf, sdi);
	if (ret < 0)
		return ret;

	while ((pkt = dev_buffer_packet_find(devc->buf, packet_valid,
					     PACKET_SIZE)))
		handle_packet(sdi, pkt);

	return SR_OK;
}

static int receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;

	(void)fd;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	if (revents == G_IO_IN) {
		/* Serial data arrived. */
		handle_new_data(sdi);
	}

	if (dev_sample_limit_reached(&devc->sample_count) ||
	    dev_time_limit_reached(&devc->time_count))
		sdi->driver->dev_acquisition_stop(sdi, cb_data);

	return TRUE;
}

static int add_channel(struct sr_dev_inst *sdi, const char *name)
{
	struct sr_channel *ch;

	if (!(ch = sr_channel_new(0, SR_CHANNEL_ANALOG, TRUE, name)))
		return SR_ERR;

	sdi->channels = g_slist_append(sdi->channels, ch);

	return SR_OK;
}

static const char *const channel_names[] = { "P1", "P2" };

static int setup_channels(struct sr_dev_inst *sdi)
{
	unsigned int i;
	int ret;

	ret = SR_ERR_BUG;

	for (i = 0; i < ARRAY_SIZE(channel_names); i++) {
		ret = add_channel(sdi, channel_names[i]);
		if (ret != SR_OK)
			break;
	}

	return ret;
}

SR_PRIV void es51919_serial_clean(void *priv)
{
	struct dev_context *devc;

	if (!(devc = priv))
		return;

	dev_buffer_destroy(devc->buf);
	g_free(devc);
}

SR_PRIV struct sr_dev_inst *es51919_serial_scan(GSList *options,
						const char *vendor,
						const char *model)
{
	struct sr_serial_dev_inst *serial;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	int ret;

	serial = NULL;
	sdi = NULL;
	devc = NULL;

	if (!(serial = serial_dev_new(options, "9600/8n1/rts=1/dtr=1")))
		goto scan_cleanup;

	ret = serial_stream_check(serial, PACKET_SIZE, packet_valid,
				  3000, 9600);
	if (ret != SR_OK)
		goto scan_cleanup;

	sr_info("Found device on port %s.", serial->port);

	if (!(sdi = sr_dev_inst_new(SR_ST_INACTIVE, vendor, model, NULL)))
		goto scan_cleanup;

	if (!(devc = g_try_malloc0(sizeof(struct dev_context)))) {
		sr_err("Device context malloc failed.");
		goto scan_cleanup;
	}

	if (!(devc->buf = dev_buffer_new(PACKET_SIZE * 8)))
		goto scan_cleanup;

	devc->freq = -1;

	sdi->inst_type = SR_INST_SERIAL;
	sdi->conn = serial;

	sdi->priv = devc;

	if (setup_channels(sdi) != SR_OK)
		goto scan_cleanup;

	return sdi;

scan_cleanup:
	es51919_serial_clean(devc);
	if (sdi)
		sr_dev_inst_free(sdi);
	if (serial)
		sr_serial_dev_inst_free(serial);

	return NULL;
}

SR_PRIV int es51919_serial_config_get(uint32_t key, GVariant **data,
				      const struct sr_dev_inst *sdi,
				      const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	(void)cg;

	if (!(devc = sdi->priv))
		return SR_ERR_BUG;

	switch (key) {
	case SR_CONF_OUTPUT_FREQUENCY:
		*data = g_variant_new_uint64(frequencies[devc->freq]);
		break;
	default:
		sr_spew("%s: Unsupported key %u", __func__, key);
		return SR_ERR_NA;
	}

	return SR_OK;
}

SR_PRIV int es51919_serial_config_set(uint32_t key, GVariant *data,
				      const struct sr_dev_inst *sdi,
				      const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	uint64_t val;

	(void)cg;

	if (!(devc = sdi->priv))
		return SR_ERR_BUG;

	switch (key) {
	case SR_CONF_LIMIT_MSEC:
		val = g_variant_get_uint64(data);
		dev_time_limit_set(&devc->time_count, val);
		sr_dbg("Setting time limit to %" PRIu64 ".", val);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		val = g_variant_get_uint64(data);
		dev_sample_limit_set(&devc->sample_count, val);
		sr_dbg("Setting sample limit to %" PRIu64 ".", val);
		break;
	default:
		sr_spew("%s: Unsupported key %u", __func__, key);
		return SR_ERR_NA;
	}

	return SR_OK;
}

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const uint32_t devopts[] = {
	SR_CONF_LCR_METER,
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_SET,
	SR_CONF_OUTPUT_FREQUENCY | SR_CONF_GET | SR_CONF_LIST,
};

static const struct std_opt_desc opts = {
	scanopts, ARRAY_SIZE(scanopts),
	devopts, ARRAY_SIZE(devopts),
};

SR_PRIV int es51919_serial_config_list(uint32_t key, GVariant **data,
				       const struct sr_dev_inst *sdi,
				       const struct sr_channel_group *cg)
{
	(void)sdi;
	(void)cg;

	if (std_config_list(key, data, &opts) == SR_OK)
		return SR_OK;

	switch (key) {
	case SR_CONF_OUTPUT_FREQUENCY:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT64,
			frequencies, ARRAY_SIZE(frequencies), sizeof(uint64_t));
		break;
	default:
		sr_spew("%s: Unsupported key %u", __func__, key);
		return SR_ERR_NA;
	}

	return SR_OK;
}

SR_PRIV int es51919_serial_acquisition_start(const struct sr_dev_inst *sdi,
					     void *cb_data)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	if (!(devc = sdi->priv))
		return SR_ERR_BUG;

	devc->cb_data = cb_data;

	dev_sample_counter_start(&devc->sample_count);
	dev_time_counter_start(&devc->time_count);

	/* Send header packet to the session bus. */
	std_session_send_df_header(cb_data, LOG_PREFIX);

	/* Poll every 50ms, or whenever some data comes in. */
	serial = sdi->conn;
	serial_source_add(sdi->session, serial, G_IO_IN, 50,
			  receive_data, (void *)sdi);

	return SR_OK;
}

SR_PRIV int es51919_serial_acquisition_stop(struct sr_dev_inst *sdi,
					    void *cb_data)
{
	return std_serial_dev_acquisition_stop(sdi, cb_data,
			std_serial_dev_close, sdi->conn, LOG_PREFIX);
}
