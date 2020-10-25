/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2020 Gwenhael Goavec-Merou <gwenhael.goavec-merou@trabucayre.com>
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

#include <libm2k/m2k.hpp>
#include <libm2k/contextbuilder.hpp>
#include <libm2k/digital/m2kdigital.hpp>

#include <string.h>
#include <config.h>
#include <glib.h>
//#include <libsigrok/libsigrok.h>
//#include "libsigrok-internal.h"

#include "m2k_wrapper.h"

using namespace std;
using namespace libm2k;
using namespace libm2k::digital;
using namespace libm2k::context;

/**
 * Open device.
 * @param[in] uri: uri to the selected device. If null the first device is used
 * @return a libm2k wrapper or NULL
 */
m2k_wrapper_t *m2k_open(char *uri)
{
	m2k_wrapper_t *wrap;
	M2k *ctx;
	M2kDigital *dig;

	wrap = (m2k_wrapper_t *)g_malloc(sizeof(m2k_wrapper_t));

	if (uri == NULL)
		ctx = m2kOpen();
	else
		ctx = m2kOpen(uri);
	wrap->ctx = ctx;

	if (!wrap->ctx) {
		g_free(wrap);
		return NULL;
	}

	dig = ctx->getDigital();
	dig->setCyclic(false);

	wrap->dig = dig;
	wrap->trig = dig->getTrigger();

	return wrap;
}

/**
 * Close libm2k context and free structure
 * @param[in] m2k: struct to close
 * @return < 0 if device not initialized, 0 otherwise
 */
int m2k_close(m2k_wrapper_t *m2k)
{
	M2k *ctx;

	if (m2k == NULL)
		return -1;

	ctx = static_cast<M2k *>(m2k->ctx);
	if (ctx == NULL)
		return -2;

	contextClose(ctx);

	g_free(m2k);
	return 0;
}

/**
 * Fill m2k_infos with all required information for a specified device
 * @param[in] infos: GSList contained all information
 * @param[in] serial: serial number
 * @param[in] product: device name
 * @param[in] vendor: manufacturer name
 * @param[in] id_product: VID
 * @param[in] id_vendor: PID
 * @param[in] uri: uri to access
 */
void m2k_fill_infos(GSList **infos, string serial, string product,
		string vendor, string id_product, string id_vendor, string uri)
{
	struct m2k_infos *m2k_info;

	m2k_info = (struct m2k_infos *)g_malloc(sizeof(struct m2k_infos));
	m2k_info->uri = (char *)g_malloc(uri.size()+1);
	m2k_info->serial_number = (char *)g_malloc(serial.size()+1);
	m2k_info->name = (char *)g_malloc(product.size()+1);
	m2k_info->vendor = (char *)g_malloc(vendor.size()+1);
	m2k_info->id_product = (char *)g_malloc(id_product.size()+1);
	m2k_info->id_vendor = (char *)g_malloc(id_vendor.size()+1);

	strcpy(m2k_info->uri, uri.c_str());
	strcpy(m2k_info->serial_number, serial.c_str());
	strcpy(m2k_info->name, product.c_str());
	strcpy(m2k_info->vendor, vendor.c_str());
	strcpy(m2k_info->id_product, id_product.c_str());
	strcpy(m2k_info->id_vendor, id_vendor.c_str());

	*infos = g_slist_append(*infos, m2k_info);
}

/**
 * retrieve all required information for a device specified by uri
 * @param[in] uri: device uri
 * @param[in] infos: list of m2k_infos
 * @return 1 when context can't be opened, otherwise 0
 */
int m2k_get_specific_info(char *uri, GSList **infos)
{
	string sn, product, vendor, id_product, id_vendor;
	M2k *ctx;

	ctx = m2kOpen(uri);
	if (!ctx)
		return 1;

	sn = ctx->getContextAttributeValue("usb,serial");
	product = ctx->getContextAttributeValue("usb,product");
	vendor = ctx->getContextAttributeValue("usb,vendor");
	id_product = ctx->getContextAttributeValue("usb,idProduct");
	id_vendor = ctx->getContextAttributeValue("usb,idVendor");

	m2k_fill_infos(infos, sn, product, vendor, id_product, id_vendor, string(uri));

	contextClose(ctx);

	return 0;
}

/**
 * retrieve information for all devices connected
 * @param[in] infos: list of m2k_infos
 */
void m2k_list_all(GSList **infos)
{
	auto ctx_info = getContextsInfo();

	for (auto i = ctx_info.begin(); i != ctx_info.end(); i++) {
		struct libm2k::CONTEXT_INFO* c = (*i);
		m2k_fill_infos(infos, c->serial, c->product, c->manufacturer,
				c->id_product, c->id_vendor, c->uri);
	}
}

/**
 * set samplerate for the device
 * @param[in] m2k: libm2k wrapper
 * @param[in] rate: new samplerate
 * @return < 0 if device not configured, otherwise real samplerate
 */
double m2k_set_rate(m2k_wrapper_t *m2k, double rate)
{
	M2kDigital *dig;

	if (m2k == NULL)
		return -1;

	dig = static_cast<M2kDigital *>(m2k->dig);
	if (dig == NULL)
		return -2;

	return dig->setSampleRateIn(rate);
}

/**
 * get current samplerate for the device
 * @param[in] m2k: libm2k wrapper
 * @return < 0 if device not configured, otherwise samplerate
 */
double m2k_get_rate(m2k_wrapper_t *m2k)
{
	M2kDigital *dig;

	if (m2k == NULL)
		return -1;

	dig = static_cast<M2kDigital *>(m2k->dig);
	if (dig == NULL)
		return -2;

	return dig->getSampleRateIn();
}

/**
 * fetch samples for device
 * @param[in] m2k: libm2k wrapper
 * @param[in] samples: array to store samples
 * @param[in] nb_sample: number of samples to fetch
 * @return < 0 if device not configured, number of samples fetch otherwise
 */
int m2k_get_sample(m2k_wrapper_t *m2k, unsigned short *samples, int nb_sample)
{
	vector<unsigned short> buff_in;
	M2kDigital *dig;

	if (m2k == NULL)
		return -1;

	dig = static_cast<M2kDigital *>(m2k->dig);
	if (dig == NULL)
		return -2;

	buff_in = dig->getSamples(nb_sample);
	memcpy(samples, buff_in.data(), buff_in.size() * sizeof(unsigned short));

	return buff_in.size();
}

/**
 * configure specified channels as input
 * @param[in] m2k: libm2k wrapper
 * @param[in] channels: list of channels to configure
 * @return < 0 if device not configured, 0 otherwise
 */
int m2k_enable_channel(m2k_wrapper_t *m2k, unsigned short channels)
{
	M2kDigital *dig;

	if (m2k == NULL)
		return -1;

	dig = static_cast<M2kDigital *>(m2k->dig);
	if (dig == NULL)
		return -2;

	/* mask to set all direction enabled as input */
	for (int i = 0; i < 16; i++)
		if (((channels >> i) & 0x01) != 0)
			dig->setDirection(i, DIO_INPUT);

	return 0;
}

/**
 * configure trigger for specified channel
 * @param[in] m2k: libm2k wrapper
 * @param[in] channel: specified chanel
 * @param[in] cond: trigger mode
 * @return < 0 if device not configured, 0 otherwise
 */
int m2k_configure_trigg(m2k_wrapper_t *m2k, uint16_t channel, uint8_t cond)
{
	M2kHardwareTrigger *trig;

	if (m2k == NULL)
		return -1;

	trig = static_cast<M2kHardwareTrigger*>(m2k->trig);
	if (trig == NULL)
		return -2;

	trig->setDigitalCondition(channel, static_cast<M2K_TRIGGER_CONDITION_DIGITAL>(cond));

	return 0;
}

/**
 * disable trigger for all channels
 * @param[in] m2k: libm2k wrapper
 * @return < 0 if device not configured, 0 otherwise
 */
int m2k_disable_trigg(m2k_wrapper_t *m2k)
{
	M2kHardwareTrigger *trig;

	if (m2k == NULL)
		return -1;

	trig = static_cast<M2kHardwareTrigger*>(m2k->trig);
	if (trig == NULL)
		return -2;

	for (int channel = 0; channel < 16; channel++)
		trig->setDigitalCondition(channel, NO_TRIGGER_DIGITAL);

	return 0;
}

/**
 * configure pre trigger
 * @param[in] m2k: libm2k wrapper
 * @param[in] delay: number of samples before trig (max -8192)
 * @return < 0 if device not configured, -3 if delay not applied, 0 otherwise
 */
int m2k_pre_trigger_delay(m2k_wrapper_t *m2k, int delay)
{
	M2kHardwareTrigger *trig;

	if (m2k == NULL)
		return -1;

	trig = static_cast<M2kHardwareTrigger*>(m2k->trig);
	if (trig == NULL)
		return -2;

	trig->setDigitalDelay(delay);
	if (delay != trig->getDigitalDelay())
		return -3;

	return 0;
}

/**
 * disable acquisition
 * @param[in] m2k: libm2k wrapper
 * @return < 0 if device not configured, 0 otherwise
 */
int m2k_stop_acquisition(m2k_wrapper_t *m2k)
{
	M2kDigital *dig;

	if (m2k == NULL)
		return -1;

	dig = static_cast<M2kDigital *>(m2k->dig);
	if (dig == NULL)
		return -2;

	dig->stopAcquisition();

	return 0;
}

/**
 * start acquisition
 * @param[in] m2k: libm2k wrapper
 * @return < 0 if device not configured, 0 otherwise
 */
int m2k_start_acquisition(m2k_wrapper_t *m2k, int nb_sample)
{
	M2kDigital *dig;

	if (m2k == NULL)
		return -1;

	dig = static_cast<M2kDigital *>(m2k->dig);
	if (dig == NULL)
		return -2;

	dig->startAcquisition(nb_sample);

	return 0;
}
