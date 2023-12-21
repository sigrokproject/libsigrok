/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2019 Katherine J. Temkin <k@ktemkin.com>
 * Copyright (C) 2019 Mikaela Szekely <qyriad@gmail.com>
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

#include "config.h"

#include <stdio.h>
#include <string.h>

#include "protocol.h"

/*
 * Communicate to GreatFET firmware, especially its Logic Analyzer mode.
 *
 * Firmware communication is done by two means: Control transfers to
 * EP0 for command execution. Bulk transfer from EP1 for sample data.
 * The sample data endpoint number is also provided by firmware in
 * responses to LA configuration requests.
 *
 * Control transfers have a fixed layout: 2x u32 class and verb numbers,
 * and u8[] payload data up to 512 bytes length. Payload layout depends
 * on commands and the verb's parameters. Binary data is represented in
 * LE format (firmware executes on Cortex-M). Strings are limited to a
 * maximum of 128 bytes.
 *
 * The set of commands used by this sigrok driver is minimal:
 * - Get the GreatFET's firmware version and serial number.
 *   - String queries, a core verb, individual verb codes for the
 *     version and for the serial number.
 * - Configure Logic Analyzer mode, start and stop captures.
 *   - Configure takes a u32 samplerate and u8 channel count. Yields
 *     u32 samplerate, u32 buffer size, u8 endpoint number.
 *   - Start takes a u32 samplerate (does it? depending on firmware
 *     version?). Empty/no response.
 *   - Stop has empty/no request and response payloads.
 *
 * Firmware implementation details, observed during sigrok driver
 * creation.
 * - Serial number "strings" in responses may carry binary data and
 *   not a text presentation of the serial number. It's uncertain
 *   whether that is by design or an oversight. This sigrok driver
 *   copes when it happens. (Remainder from another request which
 *   provided the part number as well?)
 * - The GreatFET firmware is designed for exploration by host apps.
 *   The embedded classes, their methods, their in/out parameters,
 *   including builtin help texts, can get enumerated. This driver
 *   does not use this discovery approach, assumes a given protocol.
 * - The NXP LPC4330 chip has 16 SGPIO pins. It's assumed that the
 *   GreatFET firmware currently does not support more than 8 logic
 *   channels due to constraints on bitbang machinery synchronization
 *   which is under construction (IIUC, it's about pin banks that
 *   run independently). When firmware versions get identified which
 *   transparently (from the host's perspective) support more than
 *   8 channels, this host driver may need a little adjustment.
 * - The device can sample and stream 8 channels to the host at a
 *   continuous rate of 40.8MHz. Higher rates are possible assuming
 *   that fewer pins get sampled. The firmware then provides sample
 *   memory where data taken at several sample points reside in the
 *   same byte of sample memory. It helps that power-of-two bitness
 *   is applied, IOW that there are either 1, 2, 4, or 8 bits per
 *   sample point. Even when say 3 or 5 channels are enabled. The
 *   device firmware may assume that a "dense" list of channels gets
 *   enabled, the sigrok driver supports when some disabled channels
 *   preceed other enabled channels. The device is then asked to get
 *   as many channels as are needed to cover all enabled channels,
 *   including potentially disabled channels before them.
 * - The LA configure request returns a samplerate that is supported
 *   by the hardware/firmware combination and will be used during
 *   acquisition. This returned rate is at least as high as the
 *   requested samplerate. But might exceed the USB bandwidth which
 *   the firmware is capable to sustain. Users may not expect that
 *   since numbers add up differently from their perspective. In the
 *   example of 3 enabled channels and a requested 72MHz samplerate,
 *   the firmware will derive that it needs to sample 4 channels at
 *   a 102MHz rate. Which exceeds its capabilities while users may
 *   not be aware of these constraints. This sigrok driver attempts
 *   to detect the condition, and not start an acquisition. And also
 *   emits diagnostics (at info level which is silent by default).
 *   It's assumed that users increase verbosity when diagnosing
 *   issues they may experience.
 */

/*
 * Assign a symbolic name to endpoint 0 which is used for USB control
 * transfers. Although those "or 0" phrases don't take effect from the
 * compiler's perspective, they hopefully increase readability of the
 * USB related incantations.
 *
 * Endpoint 1 for sample data reception is not declared here. Its value
 * is taken from logic analyzer configure response. Which remains more
 * portable across firmware versions and supported device models.
 */
#define CONTROL_ENDPOINT	0

/* Header fields for USB control requests. */
#define LIBGREAT_REQUEST_NUMBER	0x65
#define LIBGREAT_VALUE_EXECUTE	0
#define LIBGREAT_FLAG_SKIP_RSP	(1UL << 0)

/* Classes and their verbs for core and logic analyzer. */
#define GREATFET_CLASS_CORE	0x000
#define CORE_VERB_READ_VERSION	0x1
#define CORE_VERB_READ_SERIAL	0x3

#define GREATFET_CLASS_LA	0x10d
#define LA_VERB_CONFIGURE	0x0
#define LA_VERB_FIRST_PIN	0x1
#define LA_VERB_ALT_PIN_MAP	0x2
#define LA_VERB_START_CAPTURE	0x3
#define LA_VERB_STOP_CAPTURE	0x4

/* Maximum text string and binary payload sizes for control requests. */
#define CORE_MAX_STRING_LENGTH	128
#define LOGIC_MAX_PAYLOAD_DATA	512

/* USB communication parameters, pool dimensions. */
#define LOGIC_DEFAULT_TIMEOUT	1000
#define TRANSFER_POOL_SIZE	16
#define TRANSFER_BUFFER_SIZE	(256 * 1024)

static int greatfet_process_receive_data(const struct sr_dev_inst *sdi,
	const uint8_t *data, size_t dlen);
static int greatfet_cancel_transfers(const struct sr_dev_inst *sdi);

/* Communicate a GreatFET request to EP0, and get its response. */
static int greatfet_ctrl_out_in(const struct sr_dev_inst *sdi,
	const uint8_t *tx_data, size_t tx_size,
	uint8_t *rx_data, size_t rx_size, unsigned int timeout_ms)
{
	struct sr_usb_dev_inst *usb;
	uint16_t flags;
	int ret;
	size_t sent, rcvd;

	usb = sdi->conn;
	if (!usb)
		return SR_ERR_ARG;

	/* Caller can request to skip transmission of a response. */
	flags = 0;
	if (!rx_size)
		flags |= LIBGREAT_FLAG_SKIP_RSP;

	/* Send USB Control OUT request. */
	if (sr_log_loglevel_get() >= SR_LOG_SPEW) {
		GString *dump = sr_hexdump_new(tx_data, tx_size);
		sr_spew("USB out data: %s", dump->str);
		sr_hexdump_free(dump);
	}
	ret = libusb_control_transfer(usb->devhdl,
		LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_ENDPOINT |
		LIBUSB_ENDPOINT_OUT | CONTROL_ENDPOINT,
		LIBGREAT_REQUEST_NUMBER, LIBGREAT_VALUE_EXECUTE,
		flags, (void *)tx_data, tx_size, timeout_ms);
	if (sr_log_loglevel_get() >= SR_LOG_SPEW) {
		const char *msg;
		msg = ret < 0 ? libusb_error_name(ret) : "-";
		sr_spew("USB out, rc %d, %s", ret, msg);
	}
	if (ret < 0) {
		/* Rate limit error messages. Skip "please retry" kinds. */
		if (ret != LIBUSB_ERROR_BUSY) {
			sr_err("USB out transfer failed: %s (%d)",
				libusb_error_name(ret), ret);
		}
		return SR_ERR_IO;
	}
	sent = (size_t)ret;
	if (sent != tx_size) {
		sr_err("Short USB write: want %zu, got %zu: %s.",
			tx_size, sent, libusb_error_name(ret));
		return SR_ERR_IO;
	}

	/* Get the USB Control IN response. */
	if (!rx_size)
		return SR_OK;
	ret = libusb_control_transfer(usb->devhdl,
		LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_ENDPOINT |
		LIBUSB_ENDPOINT_IN | CONTROL_ENDPOINT,
		LIBGREAT_REQUEST_NUMBER, LIBGREAT_VALUE_EXECUTE,
		0, rx_data, rx_size, timeout_ms);
	if (sr_log_loglevel_get() >= SR_LOG_SPEW) {
		const char *msg;
		msg = ret < 0 ? libusb_error_name(ret) : "-";
		sr_spew("USB in, rc %d, %s", ret, msg);
	}
	if (ret < 0) {
		sr_err("USB in transfer failed: %s (%d)",
			libusb_error_name(ret), ret);
		return SR_ERR_IO;
	}
	rcvd = (size_t)ret;
	if (sr_log_loglevel_get() >= SR_LOG_SPEW) {
		GString *dump = sr_hexdump_new(rx_data, rcvd);
		sr_spew("USB in data: %s", dump->str);
		sr_hexdump_free(dump);
	}
	/* Short read, including zero length, is not fatal. */

	return rcvd;
}

/*
 * Use a string buffer in devc for USB transfers. This simplifies
 * resource management in error paths.
 */
static int greatfet_prep_usb_buffer(const struct sr_dev_inst *sdi,
	uint8_t **tx_buff, size_t *tx_size, uint8_t **rx_buff, size_t *rx_size)
{
	struct dev_context *devc;
	size_t want_len;
	GString *s;

	if (tx_buff)
		*tx_buff = NULL;
	if (tx_size)
		*tx_size = 0;
	if (rx_buff)
		*rx_buff = NULL;
	if (rx_size)
		*rx_size = 0;

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;
	if (!devc)
		return SR_ERR_ARG;

	/*
	 * Allocate the string buffer unless previously done.
	 * Ensure sufficient allocated space for request/response use.
	 * Assume that glib GString is suitable to hold uint8_t[] data.
	 */
	if (!devc->usb_comm_buffer) {
		want_len = 2 * sizeof(uint32_t) + LOGIC_MAX_PAYLOAD_DATA;
		devc->usb_comm_buffer = g_string_sized_new(want_len);
		if (!devc->usb_comm_buffer)
			return SR_ERR_MALLOC;
	}

	/* Pass buffer start and size to the caller if requested. */
	s = devc->usb_comm_buffer;
	if (tx_buff)
		*tx_buff = (uint8_t *)s->str;
	if (tx_size)
		*tx_size = s->allocated_len;
	if (rx_buff)
		*rx_buff = (uint8_t *)s->str;
	if (rx_size)
		*rx_size = s->allocated_len;

	return SR_OK;
}

/* Retrieve a string by executing a core service. */
static int greatfet_get_string(const struct sr_dev_inst *sdi,
	uint32_t verb, char **value)
{
	uint8_t *req, *rsp;
	size_t rsp_size;
	uint8_t *wrptr;
	size_t wrlen, rcvd;
	const char *text;
	int ret;

	if (value)
		*value = NULL;
	if (!sdi)
		return SR_ERR_ARG;
	ret = greatfet_prep_usb_buffer(sdi, &req, NULL, &rsp, &rsp_size);
	if (ret != SR_OK)
		return ret;

	wrptr = req;
	write_u32le_inc(&wrptr, GREATFET_CLASS_CORE);
	write_u32le_inc(&wrptr, verb);
	wrlen = wrptr - req;
	ret = greatfet_ctrl_out_in(sdi, req, wrlen,
		rsp, rsp_size, LOGIC_DEFAULT_TIMEOUT);
	if (ret < 0) {
		sr_err("Cannot get core string.");
		return ret;
	}
	rcvd = (size_t)ret;

	rsp[rcvd] = '\0';
	text = (const char *)rsp;
	sr_dbg("got string, verb %u, text (%zu) %s", verb, rcvd, text);
	if (value && *text) {
		*value = g_strndup(text, rcvd);
	} else if (value) {
		/*
		 * g_strndup(3) does _not_ copy 'n' bytes. Instead it
		 * truncates the result at the first NUL character seen.
		 * That's why we need extra logic to pass binary data
		 * to callers, to not violate API layers and confuse
		 * USB readers with firmware implementation details
		 * (that may be version dependent).
		 * The very condition to determine whether text or some
		 * binary data was received is a simple check for NUL
		 * in the first position, implemented above. This is
		 * GoodEnough(TM) to handle the firmware version case.
		 */
		*value = g_malloc0(rcvd + 1);
		memcpy(*value, text, rcvd);
	}

	return rcvd;
}

SR_PRIV int greatfet_get_serial_number(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	char *text;
	int ret;
	const uint8_t *rdptr;
	size_t rdlen;
	GString *snr;
	uint32_t chunk;

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;
	if (!devc)
		return SR_ERR_ARG;

	ret = greatfet_get_string(sdi, CORE_VERB_READ_SERIAL, &text);
	if (ret < 0)
		return ret;
	if (!text)
		return SR_ERR_DATA;

	/*
	 * The simple case, we got a text string. The 2019 K.Temkin
	 * implementation took the received string as is. So there
	 * are firmware versions which provide this presentation.
	 */
	if (*text) {
		devc->serial_number = text;
		return SR_OK;
	}

	/*
	 * The complex case. The received "string" looks binary. Local
	 * setups with v2018.12.1 and v2021.2.1 firmware versions yield
	 * response data that does not look like a text string. Instead
	 * it looks like four u32 fields which carry a binary value and
	 * leading padding. Try that interpreation as well. Construct a
	 * twenty character text presentation from that binary content.
	 *
	 * Implementation detail: Is the "leader" the part number which
	 * a different firmware request may yield? Are there other verbs
	 * which reliably yield the serial number in text format?
	 */
	rdptr = (const uint8_t *)text;
	rdlen = (size_t)ret;
	sr_dbg("trying to read serial nr \"text\" as binary");
	if (rdlen != 4 * sizeof(uint32_t)) {
		g_free(text);
		return SR_ERR_DATA;
	}
	snr = g_string_sized_new(20 + 1);
	chunk = read_u32le_inc(&rdptr);
	if (chunk) {
		g_free(text);
		return SR_ERR_DATA;
	}
	chunk = read_u32le_inc(&rdptr);
	if (chunk) {
		g_free(text);
		return SR_ERR_DATA;
	}
	g_string_append_printf(snr, "%04" PRIx32, chunk);
	chunk = read_u32le_inc(&rdptr);
	g_string_append_printf(snr, "%08" PRIx32, chunk);
	chunk = read_u32le_inc(&rdptr);
	g_string_append_printf(snr, "%08" PRIx32, chunk);
	sr_dbg("got serial number text %s", snr->str);
	g_free(text);
	text = g_string_free(snr, FALSE);
	devc->serial_number = text;
	return SR_OK;
}

SR_PRIV int greatfet_get_version_number(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	char *text;
	int ret;

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;
	if (!devc)
		return SR_ERR_ARG;

	ret = greatfet_get_string(sdi, CORE_VERB_READ_VERSION, &text);
	if (ret < SR_OK)
		return ret;

	devc->firmware_version = text;
	return SR_OK;
}

/*
 * Transmit a parameter-less request that wants no response. Or a
 * request with just a few bytes worth of parameter values, still
 * not expecting a response.
 */
static int greatfet_trivial_request(const struct sr_dev_inst *sdi,
	uint32_t cls, uint32_t verb, const uint8_t *tx_data, size_t tx_dlen)
{
	struct dev_context *devc;
	uint8_t *req;
	uint8_t *wrptr;
	size_t wrlen;
	int ret;

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;
	if (!devc)
		return SR_ERR_ARG;

	ret = greatfet_prep_usb_buffer(sdi, &req, NULL, NULL, NULL);
	if (ret != SR_OK)
		return ret;

	wrptr = req;
	write_u32le_inc(&wrptr, cls);
	write_u32le_inc(&wrptr, verb);
	while (tx_dlen--)
		write_u8_inc(&wrptr, *tx_data++);
	wrlen = wrptr - req;
	return greatfet_ctrl_out_in(sdi, req, wrlen,
		NULL, 0, LOGIC_DEFAULT_TIMEOUT);
}

/*
 * Transmit a "configure logic analyzer" request. Gets the resulting
 * samplerate (which can differ from requested values) and endpoint
 * (which is very useful for compatibility across devices/versions).
 * Also gets the device firmware's buffer size, which is only used
 * for information, while the host assumes a fixed larger buffer size
 * for its own purposes.
 */
static int greatfet_logic_config(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct dev_acquisition_t *acq;
	struct sr_usb_dev_inst *usb;
	uint8_t *req, *rsp;
	size_t rsp_size;
	uint8_t *wrptr;
	size_t wrlen, rcvd, want_len;
	const uint8_t *rdptr;
	uint64_t rate, bw;
	size_t bufsize;
	uint8_t ep;
	char *print_bw;
	int ret;

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;
	usb = sdi->conn;
	if (!devc || !usb)
		return SR_ERR_ARG;
	acq = &devc->acquisition;

	ret = greatfet_prep_usb_buffer(sdi, &req, NULL, &rsp, &rsp_size);
	if (ret != SR_OK)
		return ret;

	/*
	 * Optionally request to capture the upper pin bank. The device
	 * can sample from pins starting at number 8. We use the feature
	 * transparently when the first 8 channels are disabled.
	 *
	 * Values different from 0 or 8 are not used here. The details
	 * of the SGPIO hardware implementation degrade performance in
	 * this case. Its use is not desirable for users.
	 */
	sr_dbg("about to config first pin, upper %d", acq->use_upper_pins);
	wrptr = req;
	write_u32le_inc(&wrptr, GREATFET_CLASS_LA);
	write_u32le_inc(&wrptr, LA_VERB_FIRST_PIN);
	write_u8_inc(&wrptr, acq->use_upper_pins ? 8 : 0);
	wrlen = wrptr - req;
	ret = greatfet_ctrl_out_in(sdi, req, wrlen,
		NULL, 0, LOGIC_DEFAULT_TIMEOUT);
	if (ret < 0) {
		sr_err("Cannot configure first capture pin.");
		return ret;
	}

	/* Disable alt pin mapping, just for good measure. */
	sr_dbg("about to config alt pin mapping");
	wrptr = req;
	write_u32le_inc(&wrptr, GREATFET_CLASS_LA);
	write_u32le_inc(&wrptr, LA_VERB_ALT_PIN_MAP);
	write_u8_inc(&wrptr, 0);
	wrlen = wrptr - req;
	ret = greatfet_ctrl_out_in(sdi, req, wrlen,
		NULL, 0, LOGIC_DEFAULT_TIMEOUT);
	if (ret < 0) {
		sr_err("Cannot configure alt pin mapping.");
		return ret;
	}

	/*
	 * Prepare to get a specific amount of receive data. The logic
	 * analyzer configure response is strictly binary, in contrast
	 * to variable length string responses elsewhere.
	 */
	want_len = 2 * sizeof(uint32_t) + sizeof(uint8_t);
	if (rsp_size < want_len)
		return SR_ERR_BUG;
	rsp_size = want_len;

	sr_dbg("about to config LA, rate %" PRIu64 ", chans %zu",
		devc->samplerate, acq->capture_channels);
	wrptr = req;
	write_u32le_inc(&wrptr, GREATFET_CLASS_LA);
	write_u32le_inc(&wrptr, LA_VERB_CONFIGURE);
	write_u32le_inc(&wrptr, devc->samplerate);
	write_u8_inc(&wrptr, acq->capture_channels);
	wrlen = wrptr - req;
	ret = greatfet_ctrl_out_in(sdi, req, wrlen,
		rsp, rsp_size, LOGIC_DEFAULT_TIMEOUT);
	if (ret < 0) {
		sr_err("Cannot configure logic analyzer mode.");
		return ret;
	}
	rcvd = (size_t)ret;
	if (rcvd != want_len) {
		sr_warn("Unexpected LA configuration response length.");
		return SR_ERR_DATA;
	}

	rdptr = rsp;
	rate = read_u32le_inc(&rdptr);
	bufsize = read_u32le_inc(&rdptr);
	ep = read_u8_inc(&rdptr);
	sr_dbg("LA configured, rate %" PRIu64 ", buf %zu, ep %" PRIu8,
		rate, bufsize, ep);
	if (rate != devc->samplerate) {
		sr_info("Configuration feedback, want rate %" PRIu64 ", got rate %." PRIu64,
			devc->samplerate, rate);
		devc->samplerate = rate;
	}
	acq->capture_samplerate = rate;
	acq->firmware_bufsize = bufsize;
	acq->samples_endpoint = ep;

	/*
	 * The firmware does not reject requests that would exceed
	 * its capabilities. Yet the device becomes unaccessible when
	 * START is sent in that situation. (Observed with v2021.2.1
	 * firmware.)
	 *
	 * Assume a maximum USB bandwidth that we don't want to exceed.
	 * It's protecting the GreatFET's firmware. It's not a statement
	 * on the host's capability of keeping up with the GreatFET's
	 * firmware capabilities. :)
	 */
	print_bw = sr_samplerate_string(acq->capture_samplerate);
	sr_info("Capture configuration: %zu channels, samplerate %s.",
		acq->capture_channels, print_bw);
	g_free(print_bw);
	bw = acq->capture_samplerate * 8 / acq->points_per_byte;
	if (!acq->use_upper_pins)
		bw *= acq->wire_unit_size;
	print_bw = sr_si_string_u64(bw, "bps");
	sr_info("Resulting USB bandwidth: %s.", print_bw);
	g_free(print_bw);
	if (acq->bandwidth_threshold && bw > acq->bandwidth_threshold) {
		sr_err("Configuration exceeds bandwidth limit. Aborting.");
		return SR_ERR_SAMPLERATE;
	}

	return SR_OK;
}

/* Transmit "start logic capture" request. */
static int greatfet_logic_start(const struct sr_dev_inst *sdi)
{
	int ret;

	ret = greatfet_trivial_request(sdi,
		GREATFET_CLASS_LA, LA_VERB_START_CAPTURE, NULL, 0);
	sr_dbg("LA start, USB out, rc %d", ret);
	if (ret != SR_OK)
		sr_err("Cannot start logic analyzer capture.");

	return ret;
}

/* Transmit "stop logic capture" request. */
static int greatfet_logic_stop(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct dev_acquisition_t *acq;
	int ret;

	devc = sdi->priv;
	if (!devc)
		return SR_ERR_ARG;
	acq = &devc->acquisition;

	/* Only send STOP when START was sent before. */
	if (!acq->start_req_sent)
		return SR_OK;

	ret = greatfet_trivial_request(sdi,
		GREATFET_CLASS_LA, LA_VERB_STOP_CAPTURE, NULL, 0);
	sr_dbg("LA stop, USB out, rc %d", ret);
	if (ret == SR_OK)
		acq->start_req_sent = FALSE;
	else
		sr_warn("Cannot stop logic analyzer capture in the device.");

	return ret;
}

/*
 * Determine how many channels the device firmware needs to sample.
 * So that resulting capture data will cover all those logic channels
 * which currently are enabled on the sigrok side. We (have to) accept
 * when the sequence of enabled channels "has gaps" in them. Disabling
 * channels in the middle of the pin groups is a user's choice that we
 * need to obey. The count of enabled channels is not good enough for
 * the purpose of acquisition, it must be "a maximum index" or a total
 * to-get-sampled count.
 */
static int greatfet_calc_capture_chans(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct dev_acquisition_t *acq;
	GSList *l;
	struct sr_channel *ch;
	int last_used_idx;
	uint16_t pin_map;
	size_t logic_ch_count, en_ch_count, fw_ch_count;
	gboolean have_upper, have_lower, use_upper_pins;
	int ret;

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;
	if (!devc)
		return SR_ERR_ARG;
	acq = &devc->acquisition;

	last_used_idx = -1;
	logic_ch_count = 0;
	pin_map = 0;
	for (l = sdi->channels; l; l = l->next) {
		ch = l->data;
		if (ch->type != SR_CHANNEL_LOGIC)
			continue;
		logic_ch_count++;
		if (!ch->enabled)
			continue;
		if (last_used_idx < ch->index)
			last_used_idx = ch->index;
		pin_map |= 1UL << ch->index;
	}
	en_ch_count = last_used_idx + 1;
	sr_dbg("channel count, logic %zu, highest enabled idx %d -> count %zu",
		logic_ch_count, last_used_idx, en_ch_count);
	if (!en_ch_count)
		return SR_ERR_ARG;
	have_upper = pin_map & 0xff00;
	have_lower = pin_map & 0x00ff;
	use_upper_pins = have_upper && !have_lower;
	if (use_upper_pins) {
		sr_dbg("ch mask 0x%04x -> using upper pins", pin_map);
		last_used_idx -= 8;
		en_ch_count -= 8;
	}
	if (have_upper && !use_upper_pins)
		sr_warn("Multi-bank capture, check firmware support!");

	acq->capture_channels = en_ch_count;
	acq->use_upper_pins = use_upper_pins;
	ret = sr_next_power_of_two(last_used_idx, NULL, &fw_ch_count);
	if (ret != SR_OK)
		return ret;
	if (!fw_ch_count)
		return SR_ERR_ARG;
	if (fw_ch_count > 8) {
		acq->wire_unit_size = sizeof(uint16_t);
		acq->points_per_byte = 1;
	} else {
		acq->wire_unit_size = sizeof(uint8_t);
		acq->points_per_byte = 8 / fw_ch_count;
	}
	acq->channel_shift = fw_ch_count % 8;
	sr_dbg("unit %zu, dense %d -> shift %zu, points %zu",
		acq->wire_unit_size, !!acq->channel_shift,
		acq->channel_shift, acq->points_per_byte);

	return SR_OK;
}

/*
 * This is an opportunity to adapt the host's USB transfer size to
 * the value which the device firmware has provided in the LA config
 * response.
 *
 * We let the opportunity pass. Always use a fixed value for the host
 * configuration. BULK transfers will adopt, which reduces the number
 * of transfer completion events for the host.
 *
 * Notice that transfer size adjustment is _not_ a means to get user
 * feedback earlier at low samplerates. This may be done in other
 * drivers but does not take effect here. Because a buffer is used to
 * submit sample values to the session. When in doubt, the feed queue
 * needs flushing.
 *
 * TODO Consider whether sample data needs flushing when sample rates
 * are low and buffers are deep. Ideally use common feed queue support
 * if that becomes available in the future. Translate low samplerates
 * (and channel counts) to the amount of samples after which the queue
 * should get flushed.
 *
 * This implementation assumes that samplerates start at 1MHz, and
 * flushing is not necessary.
 */
static int greatfet_calc_submit_size(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct dev_transfers_t *dxfer;

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;
	if (!devc)
		return SR_ERR_ARG;
	dxfer = &devc->transfers;

	dxfer->capture_bufsize = dxfer->transfer_bufsize;
	return SR_OK;
}

/*
 * This routine is local to protocol.c and does mere data manipulation
 * and a single attempt at sending "logic analyzer stop" to the device.
 * This routine gets invoked from USB transfer completion callbacks as
 * well as periodic timer or data availability callbacks. It is essential
 * to not spend extended periods of time here.
 */
static void greatfet_abort_acquisition_quick(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct dev_acquisition_t *acq;

	if (!sdi)
		return;
	devc = sdi->priv;
	if (!devc)
		return;
	acq = &devc->acquisition;

	if (acq->acquisition_state == ACQ_RECEIVE)
		acq->acquisition_state = ACQ_SHUTDOWN;

	(void)greatfet_logic_stop(sdi);
	greatfet_cancel_transfers(sdi);

	if (acq->feed_queue)
		feed_queue_logic_flush(acq->feed_queue);
}

/* Allocate USB transfers and associated receive buffers. */
static int greatfet_allocate_transfers(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct dev_transfers_t *dxfer;
	size_t alloc_size, idx;
	struct libusb_transfer *xfer;

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;
	if (!devc)
		return SR_ERR_ARG;
	dxfer = &devc->transfers;

	dxfer->transfer_bufsize = TRANSFER_BUFFER_SIZE;
	dxfer->transfers_count = TRANSFER_POOL_SIZE;

	alloc_size = dxfer->transfers_count * dxfer->transfer_bufsize;
	dxfer->transfer_buffer = g_malloc0(alloc_size);
	if (!dxfer->transfer_buffer)
		return SR_ERR_MALLOC;

	alloc_size = dxfer->transfers_count;
	alloc_size *= sizeof(dxfer->transfers[0]);
	dxfer->transfers = g_malloc0(alloc_size);
	if (!dxfer->transfers)
		return SR_ERR_MALLOC;

	for (idx = 0; idx < dxfer->transfers_count; idx++) {
		xfer = libusb_alloc_transfer(0);
		if (!xfer)
			return SR_ERR_MALLOC;
		dxfer->transfers[idx] = xfer;
	}

	return SR_OK;
}

/* Submit USB transfers for reception, registers the data callback. */
static int greatfet_prepare_transfers(const struct sr_dev_inst *sdi,
	libusb_transfer_cb_fn callback)
{
	struct dev_context *devc;
	struct dev_acquisition_t *acq;
	struct dev_transfers_t *dxfer;
	struct sr_usb_dev_inst *conn;
	uint8_t ep;
	size_t submit_length;
	size_t off, idx;
	struct libusb_transfer *xfer;
	int ret;

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;
	conn = sdi->conn;
	if (!devc || !conn)
		return SR_ERR_ARG;
	acq = &devc->acquisition;
	dxfer = &devc->transfers;

	ep = acq->samples_endpoint;
	ret = greatfet_calc_submit_size(sdi);
	if (ret != SR_OK)
		return ret;
	submit_length = dxfer->capture_bufsize;
	if (submit_length > dxfer->transfer_bufsize)
		submit_length = dxfer->transfer_bufsize;
	sr_dbg("prep xfer, ep %u (%u), len %zu",
		ep, ep & ~LIBUSB_ENDPOINT_IN, submit_length);

	dxfer->active_transfers = 0;
	off = 0;
	for (idx = 0; idx < dxfer->transfers_count; idx++) {
		xfer = dxfer->transfers[idx];
		libusb_fill_bulk_transfer(xfer, conn->devhdl, ep,
			&dxfer->transfer_buffer[off], submit_length,
			callback, (void *)sdi, 0);
		if (!xfer->buffer)
			return SR_ERR_MALLOC;
		ret = libusb_submit_transfer(xfer);
		if (ret != 0) {
			sr_spew("submit bulk xfer failed, idx %zu, %d: %s",
				idx, ret, libusb_error_name(ret));
			return SR_ERR_IO;
		}
		dxfer->active_transfers++;
		off += submit_length;
	}

	return SR_OK;
}

/*
 * Initiate the termination of an acquisition. Cancel all USB transfers.
 * Their completion will drive further progress including resource
 * release.
 */
static int greatfet_cancel_transfers(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct dev_transfers_t *dxfer;
	size_t idx;
	struct libusb_transfer *xfer;

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;
	if (!devc)
		return SR_ERR_ARG;
	dxfer = &devc->transfers;
	if (!dxfer->transfers)
		return SR_OK;

	for (idx = 0; idx < dxfer->transfers_count; idx++) {
		xfer = dxfer->transfers[idx];
		if (!xfer)
			continue;
		(void)libusb_cancel_transfer(xfer);
		/*
		 * Cancelled transfers will cause acquisitions to abort
		 * in their callback. Keep the "active" count as is.
		 */
	}

	return SR_OK;
}

/*
 * Free an individual transfer during its callback's execution.
 * Releasing the last USB transfer also happens to drive more of
 * the shutdown path.
 */
static void greatfet_free_transfer(const struct sr_dev_inst *sdi,
	struct libusb_transfer *xfer)
{
	struct drv_context *drvc;
	struct sr_usb_dev_inst *usb;
	struct dev_context *devc;
	struct dev_acquisition_t *acq;
	struct dev_transfers_t *dxfer;
	size_t idx;

	if (!sdi || !sdi->driver)
		return;
	drvc = sdi->driver->context;
	usb = sdi->conn;
	devc = sdi->priv;
	if (!drvc || !usb || !devc)
		return;
	acq = &devc->acquisition;
	dxfer = &devc->transfers;

	/* Void the transfer in the driver's list of transfers. */
	for (idx = 0; idx < dxfer->transfers_count; idx++) {
		if (xfer != dxfer->transfers[idx])
			continue;
		dxfer->transfers[idx] = NULL;
		dxfer->active_transfers--;
		break;
	}

	/* Release the transfer from libusb use. */
	libusb_free_transfer(xfer);

	/* Done here when more transfers are still pending. */
	if (!dxfer->active_transfers)
		return;

	/*
	 * The last USB transfer has been freed after completion.
	 * Post process the previous acquisition's execution.
	 */
	(void)greatfet_stop_acquisition(sdi);
	if (acq->frame_begin_sent) {
		std_session_send_df_end(sdi);
		acq->frame_begin_sent = FALSE;
	}
	usb_source_remove(sdi->session, drvc->sr_ctx);
	if (acq->samples_interface_claimed) {
		libusb_release_interface(usb->devhdl, acq->samples_interface);
		acq->samples_interface_claimed = FALSE;
	}
	feed_queue_logic_free(acq->feed_queue);
	acq->feed_queue = NULL;
	acq->acquisition_state = ACQ_IDLE;
}

/*
 * Callback for the completion of previously submitted USB transfers.
 * Processes received sample memory content. Initiates termination of
 * the current acquisition in case of failed processing or failed
 * communication to the acquisition device. Also initiates termination
 * when previously configured acquisition limits were reached.
 */
static void LIBUSB_CALL xfer_complete_cb(struct libusb_transfer *xfer)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct dev_acquisition_t *acq;
	const uint8_t *data;
	size_t dlen;
	gboolean was_completed, was_cancelled;
	gboolean has_timedout, device_gone, is_stalled;
	int level;
	gboolean shall_abort;
	int ret;

	sdi = xfer ? xfer->user_data : NULL;
	devc = sdi ? sdi->priv : NULL;
	if (!sdi || !devc) {
		/* ShouldNotHappen(TM) */
		sr_warn("Completion of unregistered USB transfer.");
		libusb_free_transfer(xfer);
		return;
	}
	acq = &devc->acquisition;

	/*
	 * Outside of an acquisition? Or in its shutdown path?
	 * Just release the USB transfer, don't process its data.
	 */
	if (acq->acquisition_state != ACQ_RECEIVE) {
		greatfet_free_transfer(sdi, xfer);
		return;
	}

	/*
	 * Avoid the unfortunate libusb identifiers and data types.
	 * Simplify USB transfer status checks for later code paths.
	 * Optionally log the USB transfers' completion.
	 */
	data = xfer->buffer;
	dlen = xfer->actual_length;
	was_completed = xfer->status == LIBUSB_TRANSFER_COMPLETED;
	has_timedout = xfer->status == LIBUSB_TRANSFER_TIMED_OUT;
	was_cancelled = xfer->status == LIBUSB_TRANSFER_CANCELLED;
	device_gone = xfer->status == LIBUSB_TRANSFER_NO_DEVICE;
	is_stalled = xfer->status == LIBUSB_TRANSFER_STALL;
	level = sr_log_loglevel_get();
	if (level >= SR_LOG_SPEW) {
		sr_spew("USB transfer, status %s, byte count %zu.",
			libusb_error_name(xfer->status), dlen);
	} else if (level >= SR_LOG_DBG && !was_completed) {
		sr_dbg("USB transfer, status %s, byte count %zu.",
			libusb_error_name(xfer->status), dlen);
	}

	/*
	 * Timed out transfers may contain a little data. Warn but accept.
	 * Typical case will be completed transfers. Cancelled transfers
	 * are seen in shutdown paths, their data need not get processed.
	 * Terminate acquisition in case of communication or processing
	 * failure, or when limits were reached.
	 */
	shall_abort = FALSE;
	if (has_timedout)
		sr_warn("USB transfer timed out. Using available data.");
	if (was_completed || has_timedout) {
		ret = greatfet_process_receive_data(sdi, data, dlen);
		if (ret != SR_OK) {
			sr_err("Error processing sample data. Aborting.");
			shall_abort = TRUE;
		}
		if (acq->acquisition_state != ACQ_RECEIVE) {
			sr_dbg("Sample data processing ends acquisition.");
			feed_queue_logic_flush(acq->feed_queue);
			shall_abort = TRUE;
		}
	} else if (device_gone) {
		sr_err("Device gone during USB transfer. Aborting.");
		shall_abort = TRUE;
	} else if (was_cancelled) {
		sr_dbg("Cancelled USB transfer. Terminating acquisition.");
		shall_abort = TRUE;
	} else if (is_stalled) {
		sr_err("Device firmware is stalled on USB transfer. Aborting.");
		shall_abort = TRUE;
	} else {
		sr_err("USB transfer failed (%s). Aborting.",
			libusb_error_name(xfer->status));
		shall_abort = TRUE;
	}

	/*
	 * Resubmit the USB transfer for continued reception of sample
	 * data. Or release the transfer when acquisition terminates
	 * after errors were seen, or limits were reached, or the end
	 * was requested in other regular ways.
	 *
	 * In the case of error or other terminating conditions cancel
	 * the currently executing acquisition, end all USB transfers.
	 */
	if (!shall_abort) {
		ret = libusb_submit_transfer(xfer);
		if (ret < 0) {
			sr_err("Cannot resubmit USB transfer. Aborting.");
			shall_abort = TRUE;
		}
	}
	if (shall_abort) {
		greatfet_free_transfer(sdi, xfer);
		greatfet_abort_acquisition_quick(sdi);
	}
}

/* The public protocol.c API to start/stop acquisitions. */

SR_PRIV int greatfet_setup_acquisition(const struct sr_dev_inst *sdi)
{
	int ret;

	ret = greatfet_allocate_transfers(sdi);
	if (ret != SR_OK)
		return ret;

	ret = greatfet_calc_capture_chans(sdi);
	if (ret != SR_OK)
		return ret;

	return SR_OK;
}

SR_PRIV int greatfet_start_acquisition(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct dev_acquisition_t *acq;
	struct sr_usb_dev_inst *usb;
	int ret;

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;
	usb = sdi->conn;
	if (!devc || !usb)
		return SR_ERR_ARG;
	acq = &devc->acquisition;

	/*
	 * Configure the logic analyzer. Claim the USB interface. This
	 * part of the sequence is not time critical.
	 */
	ret = greatfet_logic_config(sdi);
	if (ret != SR_OK)
		return ret;

	ret = libusb_claim_interface(usb->devhdl, acq->samples_interface);
	acq->samples_interface_claimed = ret == 0;

	/*
	 * Ideally we could submit USB transfers before sending the
	 * logic analyzer start request. Experience suggests that this
	 * results in libusb IO errors. That's why we need to accept the
	 * window of blindness between sending the LA start request and
	 * initiating USB data reception.
	 */
	ret = greatfet_logic_start(sdi);
	if (ret != SR_OK)
		return ret;

	ret = greatfet_prepare_transfers(sdi, xfer_complete_cb);
	if (ret != SR_OK)
		return ret;

	return SR_OK;
}

/*
 * The public acquisition abort routine, invoked by api.c logic. Could
 * optionally spend more time than the _quick() routine.
 */
SR_PRIV void greatfet_abort_acquisition(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	if (!sdi)
		return;
	devc = sdi->priv;
	if (!devc)
		return;

	(void)greatfet_logic_stop(sdi);
	greatfet_abort_acquisition_quick(sdi);
}

SR_PRIV int greatfet_stop_acquisition(const struct sr_dev_inst *sdi)
{
	struct sr_usb_dev_inst *usb;
	int ret;

	if (!sdi)
		return SR_ERR_ARG;
	usb = sdi->conn;
	if (!usb)
		return SR_ERR_ARG;

	ret = greatfet_logic_stop(sdi);
	if (ret != SR_OK)
		return ret;

	return SR_OK;
}

SR_PRIV void greatfet_release_resources(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct dev_transfers_t *dxfer;

	if (!sdi)
		return;
	devc = sdi->priv;
	if (!devc)
		return;
	dxfer = &devc->transfers;

	/*
	 * Is there something that needs to be done here? Transfers'
	 * cancellation gets initiated and then happens as they keep
	 * completing. The completion handler releases their libusb
	 * resources. The last release also unregisters the periodic
	 * glib main loop callback.
	 *
	 * Can something be done here? The receive buffer still is
	 * allocated. As is the feed queue. Can we synchronize to the
	 * last release of the USB resources? Need we keep invoking
	 * the receive callback until the USB transfers pool has been
	 * released? Need we wait for the active transfers counter to
	 * drop to zero, is more checking involved?
	 */
	if (dxfer->active_transfers)
		sr_warn("Got active USB transfers in release code path.");
}

/*
 * Process received sample date. There are two essential modes:
 * - The straight forward case. The device provides 16 bits per sample
 *   point. Forward raw received data as is to the sigrok session. The
 *   device's endianess matches the session's LE expectation. And the
 *   data matches the device's announced total channel count.
 * - The compact presentation where a smaller number of channels is
 *   active, and their data spans only part of a byte per sample point.
 *   Multiple samples' data is sharing bytes, and bytes will carry data
 *   that was taken at different times. This requires some untangling
 *   before forwarding sample data to the sigrok session which is of
 *   the expected width (unit size) and carries one sample per item.
 * - The cases where one sample point's data occupies full bytes, but
 *   the firmware only communicates one byte per sample point, are seen
 *   as a special case of the above bit packing. The "complex case"
 *   logic covers the "bytes extension" as well.
 *
 * Implementation details:
 * - Samples taken first are found in the least significant bits of a
 *   byte. Samples taken next are found in upper bits of the byte. For
 *   example a byte containing 4x 2bit sample data is seen as 33221100.
 * - Depending on the number of enabled channels there could be up to
 *   eight samples in one byte of sample memory. This implementation
 *   tries to accumulate one input byte's content, but not more. To
 *   simplify the implementation. Performance can get tuned later as
 *   the need gets identified. Sampling at 204MHz results in some 3%
 *   CPU load with Pulseview on the local workstation.
 * - Samples for 16 channels transparently are handled by the simple
 *   8 channel case above. All logic data of an individual samplepoint
 *   occupies full bytes, endianess of sample data as provided by the
 *   device firmware and the sigrok session are the same. No conversion
 *   is required.
 */
static int greatfet_process_receive_data(const struct sr_dev_inst *sdi,
	const uint8_t *data, size_t dlen)
{
	static int diag_shown;

	struct dev_context *devc;
	struct dev_acquisition_t *acq;
	struct feed_queue_logic *q;
	uint64_t samples_remain;
	gboolean exceeded;
	size_t samples_rcvd;
	uint8_t raw_mask, raw_data;
	size_t points_per_byte, points_count;
	uint16_t wr_data;
	uint8_t accum[8 * sizeof(wr_data)];
	const uint8_t *rdptr;
	uint8_t *wrptr;
	int ret;

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;
	if (!devc)
		return SR_ERR_ARG;
	acq = &devc->acquisition;
	q = acq->feed_queue;

	/*
	 * Check whether acquisition limits apply, and whether they
	 * were reached or exceeded before. Constrain the submission
	 * of more sample values to what's still within the limits of
	 * the current acquisition.
	 */
	ret = sr_sw_limits_get_remain(&devc->sw_limits,
		&samples_remain, NULL, NULL, &exceeded);
	if (ret != SR_OK)
		return ret;
	if (exceeded)
		return SR_OK;

	/*
	 * Check for the simple case first. Where the firmware provides
	 * sample data for all logic channels supported by the device.
	 * Pass sample memory as received from the device in verbatim
	 * form to the session feed.
	 *
	 * This happens to work because sample data received from the
	 * device and logic data in sigrok sessions both are in little
	 * endian format.
	 */
	if (acq->wire_unit_size == devc->feed_unit_size) {
		samples_rcvd = dlen / acq->wire_unit_size;
		if (samples_remain && samples_rcvd > samples_remain)
			samples_rcvd = samples_remain;
		ret = feed_queue_logic_submit_many(q, data, samples_rcvd);
		if (ret != SR_OK)
			return ret;
		sr_sw_limits_update_samples_read(&devc->sw_limits, samples_rcvd);
		return SR_OK;
	}
	if (sizeof(wr_data) != devc->feed_unit_size) {
		sr_err("Unhandled unit size mismatch. Flawed implementation?");
		return SR_ERR_BUG;
	}

	/*
	 * Handle the complex cases where one byte carries values that
	 * were taken at multiple sample points, or where the firmware
	 * does not communicate all pin banks to the host (upper pins
	 * or lower pins only on the wire).
	 *
	 * This involves manipulation between reception and forwarding.
	 * It helps that the firmware provides sample data in units of
	 * power-of-two bit counts per sample point. This eliminates
	 * fragments which could span several transfers.
	 *
	 * Notice that "upper pins" and "multiple samples per byte" can
	 * happen in combination. The implementation transparently deals
	 * with upper pin use where bytes carry exactly one value.
	 */
	if (acq->channel_shift) {
		raw_mask = (1UL << acq->channel_shift) - 1;
		points_per_byte = 8 / acq->channel_shift;
	} else {
		raw_mask = (1UL << 8) - 1;
		points_per_byte = 1;
	}
	if (!diag_shown++) {
		sr_dbg("sample mem: ch count %zu, ch shift %zu, mask 0x%x, points %zu, upper %d",
			acq->capture_channels, acq->channel_shift,
			raw_mask, points_per_byte, acq->use_upper_pins);
	}
	samples_rcvd = dlen * points_per_byte;
	if (samples_remain && samples_rcvd > samples_remain) {
		samples_rcvd = samples_remain;
		dlen = samples_rcvd;
		dlen += points_per_byte - 1;
		dlen /= points_per_byte;
	}
	rdptr = data;
	while (dlen--) {
		raw_data = read_u8_inc(&rdptr);
		wrptr = accum;
		points_count = points_per_byte;
		while (points_count--) {
			wr_data = raw_data & raw_mask;
			if (acq->use_upper_pins)
				wr_data <<= 8;
			write_u16le_inc(&wrptr, wr_data);
			raw_data >>= acq->channel_shift;
		}
		points_count = points_per_byte;
		ret = feed_queue_logic_submit_many(q, accum, points_count);
		if (ret != SR_OK)
			return ret;
		sr_sw_limits_update_samples_read(&devc->sw_limits, points_count);
	}
	return SR_OK;
}

/* Receive callback, invoked when data is available, or periodically. */
SR_PRIV int greatfet_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct drv_context *drvc;
	libusb_context *ctx;
	struct timeval tv;

	(void)fd;
	(void)revents;

	sdi = cb_data;
	if (!sdi || !sdi->priv || !sdi->driver)
		return TRUE;
	devc = sdi->priv;
	if (!devc)
		return TRUE;
	drvc = sdi->driver->context;
	if (!drvc || !drvc->sr_ctx)
		return TRUE;
	ctx = drvc->sr_ctx->libusb_ctx;

	/*
	 * Handle those USB transfers which have completed so far
	 * in a regular fashion. These carry desired sample values.
	 */
	tv.tv_sec = tv.tv_usec = 0;
	libusb_handle_events_timeout(ctx, &tv);

	/*
	 * End the current acquisition when limites were reached.
	 * Process USB transfers again here before returning, because
	 * acquisition termination will unregister the receive callback,
	 * and cancel previously submitted transfers. Reap those here.
	 */
	if (sr_sw_limits_check(&devc->sw_limits)) {
		greatfet_abort_acquisition_quick(sdi);
		tv.tv_sec = tv.tv_usec = 0;
		libusb_handle_events_timeout(ctx, &tv);
	}

	return TRUE;
}
