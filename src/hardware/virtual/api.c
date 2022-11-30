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

#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <config.h>
#include "protocol.h"

#define FIFO_PATH "../../../fifo"

static const uint32_t scanopts[] = {
};

// NOTE: see similar scope/logic analyzers - link-mso19, hameg-hmo, siglent-sds
static const uint32_t drvopts[] = {
	SR_CONF_OSCILLOSCOPE,
	SR_CONF_LOGIC_ANALYZER,
};

static const uint32_t devopts[] = {
};

static struct sr_dev_driver virtual_driver_info;

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct drv_context *drvc;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	GSList *devices;

	// TODO: allow user to configure scope/LA options here
	(void)options;

	devices = NULL;
	drvc = di->context;
	drvc->instances = NULL;

	// TODO: PV only allows for USB, serial or TCP connections - need to support a new type of connection
	// - how does the demo device interact with PV??? maybe recompile it in and see

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->status = SR_ST_INACTIVE;
	sdi->model = g_strdup("Virtual hardware interface");

	devc = g_malloc0(sizeof(struct dev_context));
	devc->fd = -1;

	return devices;
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	devc->fd = open(FIFO_PATH, O_RDONLY);
	if (devc->fd == -1)
		return SR_ERR_IO;
	
	sdi->status = SR_ST_ACTIVE;

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	if (close(devc->fd) == -1)
		return SR_ERR_IO;

	sdi->status = SR_ST_INACTIVE;

	return SR_OK;
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int ret;

	(void)sdi;
	(void)data;
	(void)cg;

	ret = SR_OK;
	switch (key) {
	/* TODO */
	default:
		return SR_ERR_NA;
	}

	return ret;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int ret;

	(void)sdi;
	(void)data;
	(void)cg;

	ret = SR_OK;
	switch (key) {
	/* TODO */
	default:
		ret = SR_ERR_NA;
	}

	return ret;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int ret;

	(void)sdi;
	(void)data;
	(void)cg;

	ret = SR_OK;
	switch (key) {
	/* TODO */
	default:
		return SR_ERR_NA;
	}

	return ret;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	int ret;
	
	ret = std_session_send_df_header(sdi);
	if (ret != SR_OK)
		return ret;

	// TODO: write samples at 10Hz on C# side, read at 10Hz on libsigrok side
	// TODO: speed up C# writing and slow down reading here in the future with buffered FIFO

	// TODO: does this set sample rate??? or just timeout...
	ret = sr_session_source_add(sdi->session, devc->fd, (G_IO_IN | G_IO_ERR), 
			100, virtual_receive_data, (void *)sdi);
	if (ret != SR_OK)
		return ret;

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	int ret;

	/* Remove session source and send EOT packet */
	ret = sr_session_source_remove(sdi->session, devc->fd);
	if (ret != SR_OK)
		return ret;

	ret = std_session_send_df_end(sdi);
	if (ret != SR_OK)
		return ret;

	return SR_OK;
}

static struct sr_dev_driver virtual_driver_info = {
	.name = "virtual",
	.longname = "Virtual hardware interface",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = std_dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(virtual_driver_info);
