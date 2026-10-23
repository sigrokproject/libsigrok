/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef LIBSIGROK_FNIRSI_DLA32_DISCOVERY_H
#define LIBSIGROK_FNIRSI_DLA32_DISCOVERY_H

#include <string.h>
#include <libusb.h>

/* The VID/PID belongs to WCH and is not sufficient to identify an analyzer.
 * Do not change configuration, detach a kernel driver, or start acquisition
 * while probing. An interface already owned by another application is skipped.
 */
static int dla_libusb_probe(libusb_device *device)
{
	struct libusb_config_descriptor *config;
	const struct libusb_interface_descriptor *interface;
	const struct libusb_endpoint_descriptor *endpoint;
	libusb_device_handle *handle;
	unsigned char request[512] = {0x0a, 0x10}, reply[512];
	unsigned int i, j, k, endpoints = 0;
	int ret, actual;

	ret = libusb_get_active_config_descriptor(device, &config);
	if (ret != LIBUSB_SUCCESS)
		return ret;
	for (i = 0; i < config->bNumInterfaces; i++) {
		for (j = 0; j < (unsigned int)config->interface[i].num_altsetting; j++) {
			interface = &config->interface[i].altsetting[j];
			if (interface->bInterfaceNumber || interface->bAlternateSetting)
				continue;
			for (k = 0; k < interface->bNumEndpoints; k++) {
				endpoint = &interface->endpoint[k];
				if ((endpoint->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) !=
						LIBUSB_TRANSFER_TYPE_BULK)
					continue;
				if (endpoint->bEndpointAddress == 0x02)
					endpoints |= 1;
				if (endpoint->bEndpointAddress == 0x82)
					endpoints |= 2;
				if (endpoint->bEndpointAddress == 0x81)
					endpoints |= 4;
			}
			break;
		}
	}
	libusb_free_config_descriptor(config);
	if (endpoints != 7)
		return LIBUSB_ERROR_NOT_FOUND;
	ret = libusb_open(device, &handle);
	if (ret != LIBUSB_SUCCESS)
		return ret;
	ret = libusb_claim_interface(handle, 0);
	if (ret != LIBUSB_SUCCESS)
		goto close;
	actual = 0;
	ret = libusb_bulk_transfer(handle, 0x02, request, sizeof(request),
		&actual, 1000);
	if (ret == LIBUSB_SUCCESS && actual != (int)sizeof(request))
		ret = LIBUSB_ERROR_IO;
	if (ret != LIBUSB_SUCCESS)
		goto release;
	actual = 0;
	ret = libusb_bulk_transfer(handle, 0x82, reply, sizeof(reply),
		&actual, 1000);
	if (ret == LIBUSB_SUCCESS && (actual < 19 || reply[0] != 0x0a ||
			reply[1] != 0x02 || reply[2] != 0x0d ||
			memcmp(reply + 13, "DL32", 4) || reply[18] != 0x0b))
		ret = LIBUSB_ERROR_NOT_FOUND;
release:
	libusb_release_interface(handle, 0);
close:
	libusb_close(handle);
	return ret;
}

#endif
