/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2010 Bert Vermeulen <bert@biot.com>
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

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>
#include <string.h>
#include <glib.h>
#include <gmodule.h>
#include "sigrok.h"

source_callback_add source_cb_add = NULL;
source_callback_remove source_cb_remove = NULL;

/* the list of loaded plugins lives here */
GSList *plugins;

/* this enumerates which plugin capabilities correspond to user-settable options */
struct hwcap_option hwcap_options[] = {
	{ HWCAP_SAMPLERATE, T_UINT64, "Sample rate", "samplerate" },
	{ 0, 0, NULL, NULL }
};

extern struct device_plugin saleae_logic_plugin_info;
extern struct device_plugin ols_plugin_info;
extern struct device_plugin zeroplus_logic_cube_plugin_info;

int load_hwplugins(void)
{
	plugins = g_slist_append(plugins, (gpointer *)&saleae_logic_plugin_info);
	plugins = g_slist_append(plugins, (gpointer *)&ols_plugin_info);
	plugins = g_slist_append(plugins, (gpointer *)&zeroplus_logic_cube_plugin_info);

	return SIGROK_OK;
}


GSList *list_hwplugins(void)
{

	return plugins;
}


struct sigrok_device_instance *sigrok_device_instance_new(int index, int status,
		char *vendor, char *model, char *version)
{
	struct sigrok_device_instance *sdi;

	sdi = malloc(sizeof(struct sigrok_device_instance));
	if(!sdi)
		return NULL;

	sdi->index = index;
	sdi->status = status;
	sdi->instance_type = -1;
	sdi->vendor = strdup(vendor);
	sdi->model = strdup(model);
	sdi->version = strdup(version);
	sdi->usb = NULL;

	return sdi;
}


struct sigrok_device_instance *get_sigrok_device_instance(GSList *device_instances, int device_index)
{
	struct sigrok_device_instance *sdi;
	GSList *l;

	sdi = NULL;
	for(l = device_instances; l; l = l->next) {
		sdi = (struct sigrok_device_instance *) (l->data);
		if(sdi->index == device_index)
			return sdi;
	}
	g_warning("could not find device index %d instance", device_index);

	return NULL;
}


void sigrok_device_instance_free(struct sigrok_device_instance *sdi)
{

	switch(sdi->instance_type) {
	case USB_INSTANCE:
		usb_device_instance_free(sdi->usb);
		break;
	case SERIAL_INSTANCE:
		serial_device_instance_free(sdi->serial);
		break;
		/* no specific type, nothing extra to free */
	}

	free(sdi->vendor);
	free(sdi->model);
	free(sdi->version);
	free(sdi);

}


struct usb_device_instance *usb_device_instance_new(uint8_t bus, uint8_t address,
		struct libusb_device_handle *hdl)
{
	struct usb_device_instance *udi;

	udi = malloc(sizeof(struct usb_device_instance));
	if(!udi)
		return NULL;

	udi->bus = bus;
	udi->address = address;
	udi->devhdl = hdl;

	return udi;
}


void usb_device_instance_free(struct usb_device_instance *usb)
{
	/* QUICK HACK */
	usb = usb;

	/* nothing to do for this device instance type */

}


struct serial_device_instance *serial_device_instance_new(char *port, int fd)
{
	struct serial_device_instance *serial;

	serial = malloc(sizeof(struct serial_device_instance));
	if(!serial)
		return NULL;

	serial->port = strdup(port);
	serial->fd = fd;

	return serial;
}


void serial_device_instance_free(struct serial_device_instance *serial)
{

	free(serial->port);

}


int find_hwcap(int *capabilities, int hwcap)
{
	int i;

	for(i = 0; capabilities[i]; i++)
		if(capabilities[i] == hwcap)
			return TRUE;

	return FALSE;
}


struct hwcap_option *find_hwcap_option(int hwcap)
{
	struct hwcap_option *hwo;
	int i;

	hwo = NULL;
	for(i = 0; hwcap_options[i].capability; i++)
	{
		if(hwcap_options[i].capability == hwcap)
		{
			hwo = &hwcap_options[i];
			break;
		}
	}

	return hwo;
}


void source_remove(int fd)
{

	if(source_cb_remove)
		source_cb_remove(fd);

}


void source_add(int fd, int events, int timeout, receive_data_callback rcv_cb, void *user_data)
{

	if(source_cb_add)
		source_cb_add(fd, events, timeout, rcv_cb, user_data);

}




