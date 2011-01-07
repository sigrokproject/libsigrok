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

extern struct sigrok_global *global;

GSList *devices = NULL;

void device_scan(void)
{
	GSList *plugins, *l;
	struct device_plugin *plugin;
	int num_devices, num_probes, i;

	plugins = list_hwplugins();

	/*
	 * Initialize all plugins first. Since the init() call may involve
	 * a firmware upload and associated delay, we may as well get all
	 * of these out of the way first.
	 */
	for (l = plugins; l; l = l->next) {
		plugin = l->data;
		g_message("initializing %s plugin", plugin->name);
		num_devices = plugin->init(NULL);
		for (i = 0; i < num_devices; i++) {
			num_probes = (int)plugin->get_device_info(i,
							DI_NUM_PROBES);
			device_new(plugin, i, num_probes);
		}
	}
}

void device_close_all(void)
{
	struct device *device;

	while (devices) {
		device = devices->data;
		if (device->plugin)
			device->plugin->close(device->plugin_index);
		device_destroy(device);
	}
}

GSList *device_list(void)
{
	return devices;
}

struct device *device_new(struct device_plugin *plugin, int plugin_index,
			  int num_probes)
{
	struct device *device;
	int i;
	char probename[16];

	device = g_malloc0(sizeof(struct device));
	device->plugin = plugin;
	device->plugin_index = plugin_index;
	devices = g_slist_append(devices, device);

	for (i = 0; i < num_probes; i++) {
		snprintf(probename, 16, "%d", i + 1);
		device_probe_add(device, probename);
	}

	return device;
}

void device_clear(struct device *device)
{
	unsigned int pnum;

	/* TODO: Plugin-specific clear call? */

	if (!device->probes)
		return;

	for (pnum = 1; pnum <= g_slist_length(device->probes); pnum++)
		device_probe_clear(device, pnum);
}

void device_destroy(struct device *device)
{
	unsigned int pnum;

	/*
	 * TODO: Plugin-specific destroy call, need to decrease refcount
	 * in plugin.
	 */

	devices = g_slist_remove(devices, device);
	if (device->probes) {
		for (pnum = 1; pnum <= g_slist_length(device->probes); pnum++)
			device_probe_clear(device, pnum);
		g_slist_free(device->probes);
	}
	g_free(device);
}

void device_probe_clear(struct device *device, int probenum)
{
	struct probe *p;

	p = probe_find(device, probenum);
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

void device_probe_add(struct device *device, char *name)
{
	struct probe *p;

	p = g_malloc0(sizeof(struct probe));
	p->index = g_slist_length(device->probes) + 1;
	p->enabled = TRUE;
	p->name = g_strdup(name);
	p->trigger = NULL;
	device->probes = g_slist_append(device->probes, p);
}

struct probe *probe_find(struct device *device, int probenum)
{
	GSList *l;
	struct probe *p, *found_probe;

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

void device_probe_name(struct device *device, int probenum, char *name)
{
	struct probe *p;

	p = probe_find(device, probenum);
	if (!p)
		return;

	if (p->name)
		g_free(p->name);
	p->name = g_strdup(name);
}

void device_trigger_clear(struct device *device)
{
	struct probe *p;
	unsigned int pnum;

	if (!device->probes)
		return;

	for (pnum = 1; pnum <= g_slist_length(device->probes); pnum++) {
		p = probe_find(device, pnum);
		if (p && p->trigger) {
			g_free(p->trigger);
			p->trigger = NULL;
		}
	}
}

void device_trigger_set(struct device *device, int probenum, char *trigger)
{
	struct probe *p;

	p = probe_find(device, probenum);
	if (!p)
		return;

	if (p->trigger)
		g_free(p->trigger);

	p->trigger = g_strdup(trigger);
}
