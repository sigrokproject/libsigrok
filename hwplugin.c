/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2010-2012 Bert Vermeulen <bert@biot.com>
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
#include "sigrok.h"
#include "sigrok-internal.h"

/* The list of loaded plugins lives here. */
static GSList *plugins;

/*
 * This enumerates which plugin capabilities correspond to user-settable
 * options.
 */
/* TODO: This shouldn't be a global. */
SR_API struct sr_hwcap_option sr_hwcap_options[] = {
	{SR_HWCAP_SAMPLERATE, SR_T_UINT64, "Sample rate", "samplerate"},
	{SR_HWCAP_CAPTURE_RATIO, SR_T_UINT64, "Pre-trigger capture ratio", "captureratio"},
	{SR_HWCAP_PATTERN_MODE, SR_T_CHAR, "Pattern generator mode", "patternmode"},
	{SR_HWCAP_RLE, SR_T_BOOL, "Run Length Encoding", "rle"},
	{0, 0, NULL, NULL},
};

#ifdef HAVE_LA_DEMO
extern struct sr_dev_plugin demo_plugin_info;
#endif
#ifdef HAVE_LA_SALEAE_LOGIC
extern struct sr_dev_plugin saleae_logic_plugin_info;
#endif
#ifdef HAVE_LA_OLS
extern struct sr_dev_plugin ols_plugin_info;
#endif
#ifdef HAVE_LA_ZEROPLUS_LOGIC_CUBE
extern struct sr_dev_plugin zeroplus_logic_cube_plugin_info;
#endif
#ifdef HAVE_LA_ASIX_SIGMA
extern struct sr_dev_plugin asix_sigma_plugin_info;
#endif
#ifdef HAVE_LA_CHRONOVU_LA8
extern SR_PRIV struct dev_plugin chronovu_la8_plugin_info;
#endif
#ifdef HAVE_LA_LINK_MSO19
extern struct sr_dev_plugin link_mso19_plugin_info;
#endif
#ifdef HAVE_LA_ALSA
extern struct sr_dev_plugin alsa_plugin_info;
#endif

/* TODO: No linked list needed, this can be a simple array. */
SR_PRIV int sr_hw_load_all(void)
{
#ifdef HAVE_LA_DEMO
	plugins = g_slist_append(plugins, (gpointer *)&demo_plugin_info);
#endif
#ifdef HAVE_LA_SALEAE_LOGIC
	plugins =
	    g_slist_append(plugins, (gpointer *)&saleae_logic_plugin_info);
#endif
#ifdef HAVE_LA_OLS
	plugins = g_slist_append(plugins, (gpointer *)&ols_plugin_info);
#endif
#ifdef HAVE_LA_ZEROPLUS_LOGIC_CUBE
	plugins = g_slist_append(plugins,
			   (gpointer *)&zeroplus_logic_cube_plugin_info);
#endif
#ifdef HAVE_LA_ASIX_SIGMA
	plugins = g_slist_append(plugins, (gpointer *)&asix_sigma_plugin_info);
#endif
#ifdef HAVE_LA_CHRONOVU_LA8
	plugins = g_slist_append(plugins, (gpointer *)&chronovu_la8_plugin_info);
#endif
#ifdef HAVE_LA_LINK_MSO19
	plugins = g_slist_append(plugins, (gpointer *)&link_mso19_plugin_info);
#endif
#ifdef HAVE_LA_ALSA
	plugins = g_slist_append(plugins, (gpointer *)&alsa_plugin_info);
#endif

	return SR_OK;
}

/**
 * Return the list of loaded hardware plugins.
 *
 * The list of plugins is initialized from sr_init(), and can only be reset
 * by calling sr_exit().
 *
 * @return A GSList of pointers to loaded plugins.
 */
SR_API GSList *sr_hw_list(void)
{
	return plugins;
}

/**
 * Initialize a hardware plugin.
 *
 * The specified plugin is initialized, and all devices discovered by the
 * plugin are instantiated.
 *
 * @param plugin The plugin to initialize.
 *
 * @return The number of devices found and instantiated by the plugin.
 */
SR_API int sr_hw_init(struct sr_dev_plugin *plugin)
{
	int num_devs, num_probes, i, j;
	int num_initialized_devs = 0;
	struct sr_dev *dev;
	char **probe_names;

	sr_dbg("initializing %s plugin", plugin->name);
	num_devs = plugin->init(NULL);
	for (i = 0; i < num_devs; i++) {
		num_probes = GPOINTER_TO_INT(
				plugin->get_dev_info(i, SR_DI_NUM_PROBES));
		probe_names = (char **)plugin->get_dev_info(i,
							SR_DI_PROBE_NAMES);

		if (!probe_names) {
			sr_warn("hwplugin: %s: plugin %s does not return a "
				"list of probe names", __func__, plugin->name);
			continue;
		}

		dev = sr_dev_new(plugin, i);
		for (j = 0; j < num_probes; j++)
			sr_dev_probe_add(dev, probe_names[j]);
		num_initialized_devs++;
	}

	return num_initialized_devs;
}

SR_PRIV void sr_hw_cleanup_all(void)
{
	struct sr_dev_plugin *plugin;
	GSList *l;

	for (l = plugins; l; l = l->next) {
		plugin = l->data;
		if (plugin->cleanup)
			plugin->cleanup();
	}
}

SR_PRIV struct sr_dev_inst *sr_dev_inst_new(int index, int status,
		const char *vendor, const char *model, const char *version)
{
	struct sr_dev_inst *sdi;

	if (!(sdi = g_try_malloc(sizeof(struct sr_dev_inst)))) {
		sr_err("hwplugin: %s: sdi malloc failed", __func__);
		return NULL;
	}

	sdi->index = index;
	sdi->status = status;
	sdi->instance_type = -1;
	sdi->vendor = vendor ? g_strdup(vendor) : NULL;
	sdi->model = model ? g_strdup(model) : NULL;
	sdi->version = version ? g_strdup(version) : NULL;
	sdi->priv = NULL;

	return sdi;
}

SR_PRIV struct sr_dev_inst *sr_dev_inst_get(GSList *dev_insts, int dev_index)
{
	struct sr_dev_inst *sdi;
	GSList *l;

	for (l = dev_insts; l; l = l->next) {
		sdi = (struct sr_dev_inst *)(l->data);
		if (sdi->index == dev_index)
			return sdi;
	}
	sr_warn("could not find device index %d instance", dev_index);

	return NULL;
}

SR_PRIV void sr_dev_inst_free(struct sr_dev_inst *sdi)
{
	g_free(sdi->priv);
	g_free(sdi->vendor);
	g_free(sdi->model);
	g_free(sdi->version);
	g_free(sdi);
}

#ifdef HAVE_LIBUSB_1_0

SR_PRIV struct sr_usb_dev_inst *sr_usb_dev_inst_new(uint8_t bus,
			uint8_t address, struct libusb_device_handle *hdl)
{
	struct sr_usb_dev_inst *udi;

	if (!(udi = g_try_malloc(sizeof(struct sr_usb_dev_inst)))) {
		sr_err("hwplugin: %s: udi malloc failed", __func__);
		return NULL;
	}

	udi->bus = bus;
	udi->address = address;
	udi->devhdl = hdl; /* TODO: Check if this is NULL? */

	return udi;
}

SR_PRIV void sr_usb_dev_inst_free(struct sr_usb_dev_inst *usb)
{
	/* Avoid compiler warnings. */
	(void)usb;

	/* Nothing to do for this device instance type. */
}

#endif

SR_PRIV struct sr_serial_dev_inst *sr_serial_dev_inst_new(const char *port,
							  int fd)
{
	struct sr_serial_dev_inst *serial;

	if (!(serial = g_try_malloc(sizeof(struct sr_serial_dev_inst)))) {
		sr_err("hwplugin: %s: serial malloc failed", __func__);
		return NULL;
	}

	serial->port = g_strdup(port);
	serial->fd = fd;

	return serial;
}

SR_PRIV void sr_serial_dev_inst_free(struct sr_serial_dev_inst *serial)
{
	g_free(serial->port);
}

/**
 * Find out if a hardware plugin has a specific capability.
 *
 * @param plugin The hardware plugin in which to search for the capability.
 * @param hwcap The capability to find in the list.
 *
 * @return TRUE if found, FALSE otherwise.
 */
SR_API gboolean sr_hw_has_hwcap(struct sr_dev_plugin *plugin, int hwcap)
{
	int *capabilities, i;

	capabilities = plugin->get_capabilities();
	for (i = 0; capabilities[i]; i++) {
		if (capabilities[i] == hwcap)
			return TRUE;
	}

	return FALSE;
}

/**
 * Get a hardware plugin capability option.
 *
 * @param hwcap The capability to get.
 *
 * @return A pointer to a struct with information about the parameter, or NULL
 *         if the capability was not found.
 */
SR_API struct sr_hwcap_option *sr_hw_hwcap_get(int hwcap)
{
	int i;

	for (i = 0; sr_hwcap_options[i].capability; i++) {
		if (sr_hwcap_options[i].capability == hwcap)
			return &sr_hwcap_options[i];
	}

	return NULL;
}

/* unnecessary level of indirection follows. */

SR_PRIV void sr_source_remove(int fd)
{
	sr_session_source_remove(fd);
}

SR_PRIV void sr_source_add(int fd, int events, int timeout,
		   sr_receive_data_callback rcv_cb, void *user_data)
{
	sr_session_source_add(fd, events, timeout, rcv_cb, user_data);
}
