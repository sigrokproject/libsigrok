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

#include <stdio.h>
#include <glib.h>
#include <sigrok.h>

extern struct sr_global *global;

GSList *devices = NULL;

void sr_device_scan(void)
{
	GSList *plugins, *l;
	struct sr_device_plugin *plugin;

	plugins = list_hwplugins();

	/*
	 * Initialize all plugins first. Since the init() call may involve
	 * a firmware upload and associated delay, we may as well get all
	 * of these out of the way first.
	 */
	for (l = plugins; l; l = l->next) {
		plugin = l->data;
		sr_device_plugin_init(plugin);
	}

}

int sr_device_plugin_init(struct sr_device_plugin *plugin)
{
	int num_devices, num_probes, i;

	g_message("initializing %s plugin", plugin->name);
	num_devices = plugin->init(NULL);
	for (i = 0; i < num_devices; i++) {
		num_probes = (int)plugin->get_device_info(i, SR_DI_NUM_PROBES);
		sr_device_new(plugin, i, num_probes);
	}

	return num_devices;
}

void sr_device_close_all(void)
{
	struct sr_device *device;

	while (devices) {
		device = devices->data;
		if (device->plugin && device->plugin->close)
			device->plugin->close(device->plugin_index);
		sr_device_destroy(device);
	}
}

GSList *sr_device_list(void)
{

	if (!devices)
		sr_device_scan();

	return devices;
}

struct sr_device *sr_device_new(struct sr_device_plugin *plugin, int plugin_index,
			     int num_probes)
{
	struct sr_device *device;
	int i;

	device = g_malloc0(sizeof(struct sr_device));
	device->plugin = plugin;
	device->plugin_index = plugin_index;
	devices = g_slist_append(devices, device);

	for (i = 0; i < num_probes; i++)
		sr_device_probe_add(device, NULL);

	return device;
}

void sr_device_clear(struct sr_device *device)
{
	unsigned int pnum;

	/* TODO: Plugin-specific clear call? */

	if (!device->probes)
		return;

	for (pnum = 1; pnum <= g_slist_length(device->probes); pnum++)
		sr_device_probe_clear(device, pnum);
}

void sr_device_destroy(struct sr_device *device)
{
	unsigned int pnum;

	/*
	 * TODO: Plugin-specific destroy call, need to decrease refcount
	 * in plugin.
	 */

	devices = g_slist_remove(devices, device);
	if (device->probes) {
		for (pnum = 1; pnum <= g_slist_length(device->probes); pnum++)
			sr_device_probe_clear(device, pnum);
		g_slist_free(device->probes);
	}
	g_free(device);
}

void sr_device_probe_clear(struct sr_device *device, int probenum)
{
	struct sr_probe *p;

	p = sr_device_probe_find(device, probenum);
	if (!p)
		return;

	if (p->name) {
		g_free(p->name);
		p->name = NULL;
	}

	if (p->trigger) {
		g_free(p->trigger);
		p->trigger = NULL;
	}
}

void sr_device_probe_add(struct sr_device *device, char *name)
{
	struct sr_probe *p;
	char probename[16];
	int probenum;

	probenum = g_slist_length(device->probes) + 1;
	p = g_malloc0(sizeof(struct sr_probe));
	p->index = probenum;
	p->enabled = TRUE;
	if (name) {
		p->name = g_strdup(name);
	} else {
		snprintf(probename, 16, "%d", probenum);
		p->name = g_strdup(probename);
	}
	p->trigger = NULL;
	device->probes = g_slist_append(device->probes, p);
}

struct sr_probe *sr_device_probe_find(struct sr_device *device, int probenum)
{
	GSList *l;
	struct sr_probe *p, *found_probe;

	found_probe = NULL;
	for (l = device->probes; l; l = l->next) {
		p = l->data;
		if (p->index == probenum) {
			found_probe = p;
			break;
		}
	}

	return found_probe;
}

/* TODO: return SIGROK_ERR if probenum not found */
void sr_device_probe_name(struct sr_device *device, int probenum, char *name)
{
	struct sr_probe *p;

	p = sr_device_probe_find(device, probenum);
	if (!p)
		return;

	if (p->name)
		g_free(p->name);
	p->name = g_strdup(name);
}

/* TODO: return SIGROK_ERR if probenum not found */
void sr_device_trigger_clear(struct sr_device *device)
{
	struct sr_probe *p;
	unsigned int pnum;

	if (!device->probes)
		return;

	for (pnum = 1; pnum <= g_slist_length(device->probes); pnum++) {
		p = sr_device_probe_find(device, pnum);
		if (p && p->trigger) {
			g_free(p->trigger);
			p->trigger = NULL;
		}
	}
}

/* TODO: return SIGROK_ERR if probenum not found */
void sr_device_trigger_set(struct sr_device *device, int probenum, char *trigger)
{
	struct sr_probe *p;

	p = sr_device_probe_find(device, probenum);
	if (!p)
		return;

	if (p->trigger)
		g_free(p->trigger);

	p->trigger = g_strdup(trigger);

}

gboolean sr_device_has_hwcap(struct sr_device *device, int hwcap)
{
	int *capabilities, i;

	if (!device || !device->plugin)
		return;

	if ((capabilities = device->plugin->get_capabilities()))
		for (i = 0; capabilities[i]; i++)
			if (capabilities[i] == hwcap)
				return TRUE;

	return FALSE;
}
