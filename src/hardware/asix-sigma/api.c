/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2010-2012 Håvard Espeland <gus@ping.uio.no>,
 * Copyright (C) 2010 Martin Stensgård <mastensg@ping.uio.no>
 * Copyright (C) 2010 Carl Henrik Lunde <chlunde@ping.uio.no>
 * Copyright (C) 2020 Gerhard Sittig <gerhard.sittig@gmx.net>
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
#include "protocol.h"

/*
 * Channels are labelled 1-16, see this vendor's image of the cable:
 * http://tools.asix.net/img/sigma_sigmacab_pins_720.jpg (TI/TO are
 * additional trigger in/out signals).
 */
static const char *channel_names[] = {
	"1", "2", "3", "4", "5", "6", "7", "8",
	"9", "10", "11", "12", "13", "14", "15", "16",
};

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t drvopts[] = {
	SR_CONF_LOGIC_ANALYZER,
};

static const uint32_t devopts[] = {
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_CONN | SR_CONF_GET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_EXTERNAL_CLOCK | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_EXTERNAL_CLOCK_SOURCE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_CLOCK_EDGE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
	SR_CONF_CAPTURE_RATIO | SR_CONF_GET | SR_CONF_SET,
	/* Consider SR_CONF_TRIGGER_PATTERN (SR_T_STRING, GET/SET) support. */
};

static const char *ext_clock_edges[] = {
	[SIGMA_CLOCK_EDGE_RISING] = "rising",
	[SIGMA_CLOCK_EDGE_FALLING] = "falling",
	[SIGMA_CLOCK_EDGE_EITHER] = "either",
};

static const int32_t trigger_matches[] = {
	SR_TRIGGER_ZERO,
	SR_TRIGGER_ONE,
	SR_TRIGGER_RISING,
	SR_TRIGGER_FALLING,
};

static void clear_helper(struct dev_context *devc)
{
	(void)sigma_force_close(devc);
}

static int dev_clear(const struct sr_dev_driver *di)
{
	return std_dev_clear_with_callback(di,
		(std_dev_clear_callback)clear_helper);
}

static gboolean bus_addr_in_devices(int bus, int addr, GSList *devs)
{
	struct sr_usb_dev_inst *usb;

	for (/* EMPTY */; devs; devs = devs->next) {
		usb = devs->data;
		if (usb->bus == bus && usb->address == addr)
			return TRUE;
	}

	return FALSE;
}

static gboolean known_vid_pid(const struct libusb_device_descriptor *des)
{
	gboolean is_sigma, is_omega;

	if (des->idVendor != USB_VENDOR_ASIX)
		return FALSE;
	is_sigma = des->idProduct == USB_PRODUCT_SIGMA;
	is_omega = des->idProduct == USB_PRODUCT_OMEGA;
	if (!is_sigma && !is_omega)
		return FALSE;
	return TRUE;
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct drv_context *drvc;
	libusb_context *usbctx;
	const char *conn;
	GSList *l, *conn_devices;
	struct sr_config *src;
	GSList *devices;
	libusb_device **devlist, *devitem;
	int bus, addr;
	struct libusb_device_descriptor des;
	struct libusb_device_handle *hdl;
	int ret;
	char conn_id[20];
	char serno_txt[16];
	char *end;
	unsigned long serno_num, serno_pre;
	enum asix_device_type dev_type;
	const char *dev_text;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	size_t devidx, chidx;

	drvc = di->context;
	usbctx = drvc->sr_ctx->libusb_ctx;

	/* Find all devices which match an (optional) conn= spec. */
	conn = NULL;
	for (l = options; l; l = l->next) {
		src = l->data;
		switch (src->key) {
		case SR_CONF_CONN:
			conn = g_variant_get_string(src->data, NULL);
			break;
		}
	}
	conn_devices = NULL;
	if (conn)
		conn_devices = sr_usb_find(usbctx, conn);
	if (conn && !conn_devices)
		return NULL;

	/* Find all ASIX logic analyzers (which match the connection spec). */
	devices = NULL;
	libusb_get_device_list(usbctx, &devlist);
	for (devidx = 0; devlist[devidx]; devidx++) {
		devitem = devlist[devidx];

		/* Check for connection match if a user spec was given. */
		bus = libusb_get_bus_number(devitem);
		addr = libusb_get_device_address(devitem);
		if (conn && !bus_addr_in_devices(bus, addr, conn_devices))
			continue;
		snprintf(conn_id, sizeof(conn_id), "%d.%d", bus, addr);

		/*
		 * Check for known VID:PID pairs. Get the serial number,
		 * to then derive the device type from it.
		 */
		libusb_get_device_descriptor(devitem, &des);
		if (!known_vid_pid(&des))
			continue;
		if (!des.iSerialNumber) {
			sr_warn("Cannot get serial number (index 0).");
			continue;
		}
		ret = libusb_open(devitem, &hdl);
		if (ret < 0) {
			sr_warn("Cannot open USB device %04x.%04x: %s.",
				des.idVendor, des.idProduct,
				libusb_error_name(ret));
			continue;
		}
		ret = libusb_get_string_descriptor_ascii(hdl,
			des.iSerialNumber,
			(unsigned char *)serno_txt, sizeof(serno_txt));
		if (ret < 0) {
			sr_warn("Cannot get serial number (%s).",
				libusb_error_name(ret));
			libusb_close(hdl);
			continue;
		}
		libusb_close(hdl);

		/*
		 * All ASIX logic analyzers have a serial number, which
		 * reads as a hex number, and tells the device type.
		 */
		ret = sr_atoul_base(serno_txt, &serno_num, &end, 16);
		if (ret != SR_OK || !end || *end) {
			sr_warn("Cannot interpret serial number %s.", serno_txt);
			continue;
		}
		dev_type = ASIX_TYPE_NONE;
		dev_text = NULL;
		serno_pre = serno_num >> 16;
		switch (serno_pre) {
		case 0xa601:
			dev_type = ASIX_TYPE_SIGMA;
			dev_text = "SIGMA";
			sr_info("Found SIGMA, serno %s.", serno_txt);
			break;
		case 0xa602:
			dev_type = ASIX_TYPE_SIGMA;
			dev_text = "SIGMA2";
			sr_info("Found SIGMA2, serno %s.", serno_txt);
			break;
		case 0xa603:
			dev_type = ASIX_TYPE_OMEGA;
			dev_text = "OMEGA";
			sr_info("Found OMEGA, serno %s.", serno_txt);
			if (!ASIX_WITH_OMEGA) {
				sr_warn("OMEGA support is not implemented yet.");
				continue;
			}
			break;
		default:
			sr_warn("Unknown serno %s, skipping.", serno_txt);
			continue;
		}

		/* Create a device instance, add it to the result set. */

		sdi = g_malloc0(sizeof(*sdi));
		devices = g_slist_append(devices, sdi);
		sdi->status = SR_ST_INITIALIZING;
		sdi->vendor = g_strdup("ASIX");
		sdi->model = g_strdup(dev_text);
		sdi->serial_num = g_strdup(serno_txt);
		sdi->connection_id = g_strdup(conn_id);
		for (chidx = 0; chidx < ARRAY_SIZE(channel_names); chidx++)
			sr_channel_new(sdi, chidx, SR_CHANNEL_LOGIC,
				TRUE, channel_names[chidx]);

		devc = g_malloc0(sizeof(*devc));
		sdi->priv = devc;
		devc->id.vid = des.idVendor;
		devc->id.pid = des.idProduct;
		devc->id.serno = serno_num;
		devc->id.prefix = serno_pre;
		devc->id.type = dev_type;
		sr_sw_limits_init(&devc->limit.config);
		devc->capture_ratio = 50;
		devc->use_triggers = FALSE;

		/* Get current hardware configuration (or use defaults). */
		(void)sigma_fetch_hw_config(sdi);
	}
	libusb_free_device_list(devlist, 1);
	g_slist_free_full(conn_devices, (GDestroyNotify)sr_usb_dev_inst_free);

	return std_scan_complete(di, devices);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = sdi->priv;

	if (devc->id.type == ASIX_TYPE_OMEGA && !ASIX_WITH_OMEGA) {
		sr_err("OMEGA support is not implemented yet.");
		return SR_ERR_NA;
	}

	return sigma_force_open(sdi);
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = sdi->priv;

	return sigma_force_close(devc);
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	const char *clock_text;

	(void)cg;

	if (!sdi)
		return SR_ERR;
	devc = sdi->priv;

	switch (key) {
	case SR_CONF_CONN:
		*data = g_variant_new_string(sdi->connection_id);
		break;
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(devc->clock.samplerate);
		break;
	case SR_CONF_EXTERNAL_CLOCK:
		*data = g_variant_new_boolean(devc->clock.use_ext_clock);
		break;
	case SR_CONF_EXTERNAL_CLOCK_SOURCE:
		clock_text = channel_names[devc->clock.clock_pin];
		*data = g_variant_new_string(clock_text);
		break;
	case SR_CONF_CLOCK_EDGE:
		clock_text = ext_clock_edges[devc->clock.clock_edge];
		*data = g_variant_new_string(clock_text);
		break;
	case SR_CONF_LIMIT_MSEC:
	case SR_CONF_LIMIT_SAMPLES:
		return sr_sw_limits_config_get(&devc->limit.config, key, data);
	case SR_CONF_CAPTURE_RATIO:
		*data = g_variant_new_uint64(devc->capture_ratio);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	int ret;
	uint64_t want_rate, have_rate;
	int idx;

	(void)cg;

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_SAMPLERATE:
		want_rate = g_variant_get_uint64(data);
		ret = sigma_normalize_samplerate(want_rate, &have_rate);
		if (ret != SR_OK)
			return ret;
		if (have_rate != want_rate) {
			char *text_want, *text_have;
			text_want = sr_samplerate_string(want_rate);
			text_have = sr_samplerate_string(have_rate);
			sr_info("Adjusted samplerate %s to %s.",
				text_want, text_have);
			g_free(text_want);
			g_free(text_have);
		}
		devc->clock.samplerate = have_rate;
		break;
	case SR_CONF_EXTERNAL_CLOCK:
		devc->clock.use_ext_clock = g_variant_get_boolean(data);
		break;
	case SR_CONF_EXTERNAL_CLOCK_SOURCE:
		idx = std_str_idx(data, ARRAY_AND_SIZE(channel_names));
		if (idx < 0)
			return SR_ERR_ARG;
		devc->clock.clock_pin = idx;
		break;
	case SR_CONF_CLOCK_EDGE:
		idx = std_str_idx(data, ARRAY_AND_SIZE(ext_clock_edges));
		if (idx < 0)
			return SR_ERR_ARG;
		devc->clock.clock_edge = idx;
		break;
	case SR_CONF_LIMIT_MSEC:
	case SR_CONF_LIMIT_SAMPLES:
		return sr_sw_limits_config_set(&devc->limit.config, key, data);
	case SR_CONF_CAPTURE_RATIO:
		devc->capture_ratio = g_variant_get_uint64(data);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
	case SR_CONF_DEVICE_OPTIONS:
		if (cg)
			return SR_ERR_NA;
		return STD_CONFIG_LIST(key, data, sdi, cg,
			scanopts, drvopts, devopts);
	case SR_CONF_SAMPLERATE:
		*data = sigma_get_samplerates_list();
		break;
	case SR_CONF_EXTERNAL_CLOCK_SOURCE:
		*data = g_variant_new_strv(ARRAY_AND_SIZE(channel_names));
		break;
	case SR_CONF_CLOCK_EDGE:
		*data = g_variant_new_strv(ARRAY_AND_SIZE(ext_clock_edges));
		break;
	case SR_CONF_TRIGGER_MATCH:
		*data = std_gvar_array_i32(ARRAY_AND_SIZE(trigger_matches));
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	uint16_t pindis_mask;
	uint8_t async, div;
	int ret;
	size_t triggerpin;
	uint8_t trigsel2;
	struct triggerinout triggerinout_conf;
	struct triggerlut lut;
	uint8_t regval, cmd_bytes[4], *wrptr;

	devc = sdi->priv;

	/* Convert caller's trigger spec to driver's internal format. */
	ret = sigma_convert_trigger(sdi);
	if (ret != SR_OK) {
		sr_err("Could not configure triggers.");
		return ret;
	}

	/*
	 * Setup the device's samplerate from the value which up to now
	 * just got checked and stored. As a byproduct this can pick and
	 * send firmware to the device, reduce the number of available
	 * logic channels, etc.
	 *
	 * Determine an acquisition timeout from optionally configured
	 * sample count or time limits. Which depends on the samplerate.
	 * Force 50MHz samplerate when external clock is in use.
	 */
	if (devc->clock.use_ext_clock) {
		if (devc->clock.samplerate != SR_MHZ(50))
			sr_info("External clock, forcing 50MHz samplerate.");
		devc->clock.samplerate = SR_MHZ(50);
	}
	ret = sigma_set_samplerate(sdi);
	if (ret != SR_OK)
		return ret;
	ret = sigma_set_acquire_timeout(devc);
	if (ret != SR_OK)
		return ret;

	/* Enter trigger programming mode. */
	trigsel2 = TRGSEL2_RESET;
	ret = sigma_set_register(devc, WRITE_TRIGGER_SELECT2, trigsel2);
	if (ret != SR_OK)
		return ret;

	trigsel2 = 0;
	if (devc->clock.samplerate >= SR_MHZ(100)) {
		/* 100 and 200 MHz mode. */
		/* TODO Decipher the 0x81 magic number's purpose. */
		ret = sigma_set_register(devc, WRITE_TRIGGER_SELECT2, 0x81);
		if (ret != SR_OK)
			return ret;

		/* Find which pin to trigger on from mask. */
		for (triggerpin = 0; triggerpin < 8; triggerpin++) {
			if (devc->trigger.risingmask & BIT(triggerpin))
				break;
			if (devc->trigger.fallingmask & BIT(triggerpin))
				break;
		}

		/* Set trigger pin and light LED on trigger. */
		trigsel2 = triggerpin & TRGSEL2_PINS_MASK;
		trigsel2 |= TRGSEL2_LEDSEL1;

		/* Default rising edge. */
		/* TODO Documentation disagrees, bit set means _rising_ edge. */
		if (devc->trigger.fallingmask)
			trigsel2 |= TRGSEL2_PINPOL_RISE;

	} else if (devc->clock.samplerate <= SR_MHZ(50)) {
		/* 50MHz firmware modes. */

		/* Translate application specs to hardware perspective. */
		ret = sigma_build_basic_trigger(devc, &lut);
		if (ret != SR_OK)
			return ret;

		/* Communicate resulting register values to the device. */
		ret = sigma_write_trigger_lut(devc, &lut);
		if (ret != SR_OK)
			return ret;

		trigsel2 = TRGSEL2_LEDSEL1 | TRGSEL2_LEDSEL0;
	}

	/* Setup trigger in and out pins to default values. */
	memset(&triggerinout_conf, 0, sizeof(triggerinout_conf));
	triggerinout_conf.trgout_bytrigger = TRUE;
	triggerinout_conf.trgout_enable = TRUE;
	/* TODO
	 * Verify the correctness of this implementation. The previous
	 * version used to assign to a C language struct with bit fields
	 * which is highly non-portable and hard to guess the resulting
	 * raw memory layout or wire transfer content. The C struct's
	 * field names did not match the vendor documentation's names.
	 * Which means that I could not verify "on paper" either. Let's
	 * re-visit this code later during research for trigger support.
	 */
	wrptr = cmd_bytes;
	regval = 0;
	if (triggerinout_conf.trgout_bytrigger)
		regval |= TRGOPT_TRGOOUTEN;
	write_u8_inc(&wrptr, regval);
	regval &= ~TRGOPT_CLEAR_MASK;
	if (triggerinout_conf.trgout_enable)
		regval |= TRGOPT_TRGOEN;
	write_u8_inc(&wrptr, regval);
	ret = sigma_write_register(devc, WRITE_TRIGGER_OPTION,
		cmd_bytes, wrptr - cmd_bytes);
	if (ret != SR_OK)
		return ret;

	/* Leave trigger programming mode. */
	ret = sigma_set_register(devc, WRITE_TRIGGER_SELECT2, trigsel2);
	if (ret != SR_OK)
		return ret;

	/*
	 * Samplerate dependent clock and channels configuration. Some
	 * channels by design are not available at higher clock rates.
	 * Register layout differs between firmware variants (depth 1
	 * with LSB channel mask above 50MHz, depth 4 with more details
	 * up to 50MHz).
	 *
	 * Derive a mask where bits are set for unavailable channels.
	 * Either send the single byte, or the full byte sequence.
	 */
	pindis_mask = ~BITS_MASK(devc->interp.num_channels);
	if (devc->clock.samplerate > SR_MHZ(50)) {
		ret = sigma_set_register(devc, WRITE_CLOCK_SELECT,
			pindis_mask & 0xff);
	} else {
		wrptr = cmd_bytes;
		/* Select 50MHz base clock, and divider. */
		async = 0;
		div = SR_MHZ(50) / devc->clock.samplerate - 1;
		if (devc->clock.use_ext_clock) {
			async = CLKSEL_CLKSEL8;
			div = devc->clock.clock_pin + 1;
			switch (devc->clock.clock_edge) {
			case SIGMA_CLOCK_EDGE_RISING:
				div |= CLKSEL_RISING;
				break;
			case SIGMA_CLOCK_EDGE_FALLING:
				div |= CLKSEL_FALLING;
				break;
			case SIGMA_CLOCK_EDGE_EITHER:
				div |= CLKSEL_RISING;
				div |= CLKSEL_FALLING;
				break;
			}
		}
		write_u8_inc(&wrptr, async);
		write_u8_inc(&wrptr, div);
		write_u16be_inc(&wrptr, pindis_mask);
		ret = sigma_write_register(devc, WRITE_CLOCK_SELECT,
			cmd_bytes, wrptr - cmd_bytes);
	}
	if (ret != SR_OK)
		return ret;

	/* Setup maximum post trigger time. */
	ret = sigma_set_register(devc, WRITE_POST_TRIGGER,
		(devc->capture_ratio * 255) / 100);
	if (ret != SR_OK)
		return ret;

	/* Start acqusition. */
	regval = WMR_TRGRES | WMR_SDRAMWRITEEN;
	if (devc->use_triggers)
		regval |= WMR_TRGEN;
	ret = sigma_set_register(devc, WRITE_MODE, regval);
	if (ret != SR_OK)
		return ret;

	ret = std_session_send_df_header(sdi);
	if (ret != SR_OK)
		return ret;

	/* Add capture source. */
	ret = sr_session_source_add(sdi->session, -1, 0, 10,
		sigma_receive_data, (void *)sdi);
	if (ret != SR_OK)
		return ret;

	devc->state = SIGMA_CAPTURE;

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = sdi->priv;

	/*
	 * When acquisition is currently running, keep the receive
	 * routine registered and have it stop the acquisition upon the
	 * next invocation. Else unregister the receive routine here
	 * already. The detour is required to have sample data retrieved
	 * for forced acquisition stops.
	 */
	if (devc->state == SIGMA_CAPTURE) {
		devc->state = SIGMA_STOPPING;
	} else {
		devc->state = SIGMA_IDLE;
		(void)sr_session_source_remove(sdi->session, -1);
	}

	return SR_OK;
}

static struct sr_dev_driver asix_sigma_driver_info = {
	.name = "asix-sigma",
	.longname = "ASIX SIGMA/SIGMA2",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(asix_sigma_driver_info);
