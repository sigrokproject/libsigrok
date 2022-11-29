/*
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
#include <stdlib.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "protocol.h"

static const uint32_t scanopts[] = {
};

static const uint32_t drvopts[] = {
};

static const uint32_t devopts[] = {
};

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	return NULL;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	(void)cg;

	devc = sdi->priv;

	return sr_sw_limits_config_set(&devc->sw_limits, key, data);
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	return 0;
}

static struct sr_dev_driver virtual_driver_info = {
	.name = "virtual",
	.longname = "Virtual HW interface",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = std_dev_clear,
	.config_get = NULL,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = std_serial_dev_open,
	.dev_close = std_serial_dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = std_serial_dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(virtual_driver_info);
