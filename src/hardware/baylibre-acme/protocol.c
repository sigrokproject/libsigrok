/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2015 Bartosz Golaszewski <bgolaszewski@baylibre.com>
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

#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include "protocol.h"
#include "gpio.h"

struct channel_group_priv {
	int hwmon_num;
	int probe_type;
	int index;
};

struct channel_priv {
	int ch_type;
	struct channel_group_priv *probe;
};

static const uint8_t enrg_i2c_addrs[] = {
	0x40, 0x41, 0x44, 0x45, 0x42, 0x43, 0x46, 0x47,
};

static const uint8_t temp_i2c_addrs[] = {
	0x0, 0x0, 0x0, 0x0, 0x4c, 0x49, 0x4f, 0x4b,
};

static const uint32_t pws_gpios[] = {
	486, 498, 502, 482, 478, 506, 510, 474,
};

static const uint32_t pws_info_gpios[] = {
	487, 499, 503, 483, 479, 507, 511, 475,
};

#define MOHM_TO_UOHM(x) ((x) * 1000)
#define UOHM_TO_MOHM(x) ((x) / 1000)

SR_PRIV uint8_t bl_acme_get_enrg_addr(int index)
{
	return enrg_i2c_addrs[index];
}

SR_PRIV uint8_t bl_acme_get_temp_addr(int index)
{
	return temp_i2c_addrs[index];
}

SR_PRIV gboolean bl_acme_is_sane(void)
{
	gboolean status;

	/*
	 * We expect sysfs to be present and mounted at /sys, ina226 and
	 * tmp435 sensors detected by the system and their appropriate
	 * drivers loaded and functional.
	 */
	status = g_file_test("/sys", G_FILE_TEST_IS_DIR);
	if (!status) {
		sr_err("/sys/ directory not found - sysfs not mounted?");
		return FALSE;
	}

	return TRUE;
}

static void probe_name_path(unsigned int addr, GString *path)
{
	g_string_printf(path,
			"/sys/class/i2c-adapter/i2c-1/1-00%02x/name", addr);
}

/*
 * For given address fill buf with the path to appropriate hwmon entry.
 */
static void probe_hwmon_path(unsigned int addr, GString *path)
{
	g_string_printf(path,
			"/sys/class/i2c-adapter/i2c-1/1-00%02x/hwmon", addr);
}

SR_PRIV gboolean bl_acme_detect_probe(unsigned int addr,
				      int prb_num, const char *prb_name)
{
	gboolean ret = FALSE, status;
	char *buf = NULL;
	GString *path = g_string_sized_new(64);
	GError *err = NULL;
	gsize size;

	probe_name_path(addr, path);
	status = g_file_get_contents(path->str, &buf, &size, &err);
	if (!status) {
		sr_dbg("Name for probe %d can't be read: %s",
		       prb_num, err->message);
		g_string_free(path, TRUE);
		return ret;
	}

	if (!strncmp(buf, prb_name, strlen(prb_name))) {
		/*
		 * Correct driver registered on this address - but is
		 * there an actual probe connected?
		 */
		probe_hwmon_path(addr, path);
		status = g_file_test(path->str, G_FILE_TEST_IS_DIR);
		if (status) {
			/* We have found an ACME probe. */
			ret = TRUE;
		}
	}

	g_free(buf);
	g_string_free(path, TRUE);

	return ret;
}

static int get_hwmon_index(unsigned int addr)
{
	int status, hwmon;
	GString *path = g_string_sized_new(64);
	GError *err = NULL;
	GDir *dir;

	probe_hwmon_path(addr, path);
	dir = g_dir_open(path->str, 0, &err);
	if (!dir) {
		sr_err("Error opening %s: %s", path->str, err->message);
		g_string_free(path, TRUE);
		return -1;
	}

	g_string_free(path, TRUE);

	/*
	 * The directory should contain a single file named hwmonX where X
	 * is the hwmon index.
	 */
	status = sscanf(g_dir_read_name(dir), "hwmon%d", &hwmon);
	g_dir_close(dir);
	if (status != 1) {
		sr_err("Unable to determine the hwmon entry");
		return -1;
	}

	return hwmon;
}

static void append_channel(struct sr_dev_inst *sdi, struct sr_channel_group *cg,
			   int index, int type)
{
	struct channel_priv *cp;
	struct dev_context *devc;
	struct sr_channel *ch;
	char *name;

	devc = sdi->priv;

	switch (type) {
	case ENRG_PWR:
		name = g_strdup_printf("P%d_ENRG_PWR", index);
		break;
	case ENRG_CURR:
		name = g_strdup_printf("P%d_ENRG_CURR", index);
		break;
	case ENRG_VOL:
		name = g_strdup_printf("P%d_ENRG_VOL", index);
		break;
	case TEMP_IN:
		name = g_strdup_printf("P%d_TEMP_IN", index);
		break;
	case TEMP_OUT:
		name = g_strdup_printf("P%d_TEMP_OUT", index);
		break;
	default:
		sr_err("Invalid channel type: %d.", type);
		return;
	}

	cp = g_malloc0(sizeof(struct channel_priv));
	cp->ch_type = type;
	cp->probe = cg->priv;

	ch = sr_channel_new(devc->num_channels++,
			    SR_CHANNEL_ANALOG, TRUE, name);
	g_free(name);

	ch->priv = cp;
	cg->channels = g_slist_append(cg->channels, ch);
	sdi->channels = g_slist_append(sdi->channels, ch);
}

SR_PRIV gboolean bl_acme_register_probe(struct sr_dev_inst *sdi, int type,
					unsigned int addr, int prb_num)
{
	struct sr_channel_group *cg;
	struct channel_group_priv *cgp;
	int hwmon;

	/* Obtain the hwmon index. */
	hwmon = get_hwmon_index(addr);
	if (hwmon < 0)
		return FALSE;

	cg = g_malloc0(sizeof(struct sr_channel_group));
	cgp = g_malloc0(sizeof(struct channel_group_priv));
	cgp->hwmon_num = hwmon;
	cgp->probe_type = type;
	cgp->index = prb_num - 1;
	cg->name = g_strdup_printf("Probe_%d", prb_num);
	cg->priv = cgp;

	if (type == PROBE_ENRG) {
		append_channel(sdi, cg, prb_num, ENRG_PWR);
		append_channel(sdi, cg, prb_num, ENRG_CURR);
		append_channel(sdi, cg, prb_num, ENRG_VOL);
	} else if (type == PROBE_TEMP) {
		append_channel(sdi, cg, prb_num, TEMP_IN);
		append_channel(sdi, cg, prb_num, TEMP_OUT);
	} else {
		sr_err("Invalid probe type: %d.", type);
	}

	sdi->channel_groups = g_slist_append(sdi->channel_groups, cg);

	return TRUE;
}

/*
 * Sets path to the hwmon attribute if this channel group
 * supports shunt resistance setting. The caller has to supply
 * a valid GString.
 */
static int get_shunt_path(const struct sr_channel_group *cg, GString *path)
{
	struct channel_group_priv *cgp;
	int ret = SR_OK, status;

	cgp = cg->priv;

	if (cgp->probe_type != PROBE_ENRG) {
		sr_err("Probe doesn't support shunt resistance setting");
		return SR_ERR_ARG;
	}

	g_string_append_printf(path,
			       "/sys/class/hwmon/hwmon%d/shunt_resistor",
			       cgp->hwmon_num);

	/*
	 * The shunt_resistor sysfs attribute is available
	 * in the Linux kernel since version 3.20. We have
	 * to notify the user if this attribute is not present.
	 */
	status = g_file_test(path->str, G_FILE_TEST_EXISTS);
	if (!status) {
		sr_err("shunt_resistance attribute not present, please update "
		       "your kernel to version >=3.20");
		return SR_ERR_NA;
	}

	return ret;
}

SR_PRIV int bl_acme_get_shunt(const struct sr_channel_group *cg,
			      uint64_t *shunt)
{
	GString *path = g_string_sized_new(64);
	gchar *contents;
	int status, ret = SR_OK;
	GError *err = NULL;

	status = get_shunt_path(cg, path);
	if (status != SR_OK) {
		ret = status;
		goto out;
	}

	status = g_file_get_contents(path->str, &contents, NULL, &err);
	if (!status) {
		sr_err("Error reading shunt resistance: %s", err->message);
		ret = SR_ERR_IO;
		goto out;
	}

	*shunt = UOHM_TO_MOHM(strtol(contents, NULL, 10));

out:
	g_string_free(path, TRUE);
	return ret;
}

SR_PRIV int bl_acme_set_shunt(const struct sr_channel_group *cg, uint64_t shunt)
{
	GString *path = g_string_sized_new(64);;
	int status, ret = SR_OK;
	FILE *fd;

	status = get_shunt_path(cg, path);
	if (status != SR_OK) {
		ret = status;
		goto out;
	}

	/*
	 * Can't use g_file_set_contents() here, as it calls open() with
	 * O_EXEC flag in a sysfs directory thus failing with EACCES.
	 */
	fd = g_fopen(path->str, "w");
	if (!fd) {
		sr_err("Error opening %s: %s", path->str, strerror(errno));
		g_string_free(path, TRUE);
		return SR_ERR_IO;
	}

	g_string_free(path, TRUE);
	g_fprintf(fd, "%llu\n", MOHM_TO_UOHM(shunt));
	/*
	 * XXX There's no g_fclose() in GLib. This seems to work,
	 * but is it safe?
	 */
	fclose(fd);

out:
	g_string_free(path, TRUE);
	return ret;
}

SR_PRIV int bl_acme_read_power_state(const struct sr_channel_group *cg,
				     gboolean *off)
{
	struct channel_group_priv *cgp;
	int val;

	cgp = cg->priv;

	val = sr_gpio_getval_export(pws_info_gpios[cgp->index]);
	if (val != 1) {
		sr_err("Probe has no power-switch");
		return SR_ERR_ARG;
	}

	val = sr_gpio_getval_export(pws_gpios[cgp->index]);
	*off = val ? FALSE : TRUE;

	return SR_OK;
}

SR_PRIV int bl_acme_set_power_off(const struct sr_channel_group *cg,
				  gboolean off)
{
	struct channel_group_priv *cgp;
	int val;

	cgp = cg->priv;

	val = sr_gpio_getval_export(pws_info_gpios[cgp->index]);
	if (val != 1) {
		sr_err("Probe has no power-switch");
		return SR_ERR_ARG;
	}

	val = sr_gpio_setval_export(pws_gpios[cgp->index], off ? 0 : 1);

	return SR_OK;
}

static int channel_to_mq(struct sr_channel *ch)
{
	struct channel_priv *chp;

	chp = ch->priv;

	switch (chp->ch_type) {
	case ENRG_PWR:
		return SR_MQ_POWER;
	case ENRG_CURR:
		return SR_MQ_CURRENT;
	case ENRG_VOL:
		return SR_MQ_VOLTAGE;
	case TEMP_IN: /* Fallthrough */
	case TEMP_OUT:
		return SR_MQ_TEMPERATURE;
	default:
		return -1;
	}
}

static int channel_to_unit(struct sr_channel *ch)
{
	struct channel_priv *chp;

	chp = ch->priv;

	switch (chp->ch_type) {
	case ENRG_PWR:
		return SR_UNIT_WATT;
	case ENRG_CURR:
		return SR_UNIT_AMPERE;
	case ENRG_VOL:
		return SR_UNIT_VOLT;
	case TEMP_IN: /* Fallthrough */
	case TEMP_OUT:
		return SR_UNIT_CELSIUS;
	default:
		return -1;
	}
}

/* We need to scale measurements down from the units used by the drivers. */
static float adjust_data(int val, int type)
{
	switch (type) {
	case ENRG_PWR:
		return ((float)val) / 1000000.0;
	case ENRG_CURR: /* Fallthrough */
	case ENRG_VOL: /* Fallthrough */
	case TEMP_IN: /* Fallthrough */
	case TEMP_OUT:
		return ((float)val) / 1000.0;
	default:
		return 0.0;
	}
}

static float read_sample(struct sr_channel *ch)
{
	struct channel_priv *chp;
	char path[64], *file, buf[16];
	ssize_t len;
	int fd;

	chp = ch->priv;

	switch (chp->ch_type) {
	case ENRG_PWR:	file = "power1_input";	break;
	case ENRG_CURR:	file = "curr1_input";	break;
	case ENRG_VOL:	file = "in1_input";	break;
	case TEMP_IN:	file = "temp1_input";	break;
	case TEMP_OUT:	file = "temp2_input";	break;
	default:
		sr_err("Invalid channel type: %d.", chp->ch_type);
		return -1.0;
	}

	snprintf(path, sizeof(path), "/sys/class/hwmon/hwmon%d/%s",
		 chp->probe->hwmon_num, file);
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		sr_err("Error opening %s: %s", path, strerror(errno));
		ch->enabled = FALSE;
		return -1.0;
	}

	len = read(fd, buf, sizeof(buf));
	close(fd);
	if (len < 0) {
		sr_err("Error reading from %s: %s", path, strerror(errno));
		ch->enabled = FALSE;
		return -1.0;
	}

	return adjust_data(strtol(buf, NULL, 10), chp->ch_type);
}

SR_PRIV int bl_acme_receive_data(int fd, int revents, void *cb_data)
{
	uint32_t cur_time, elapsed_time, diff_time;
	int64_t time_to_sleep;
	struct sr_datafeed_packet packet, framep;
	struct sr_datafeed_analog analog;
	struct sr_dev_inst *sdi;
	struct sr_channel *ch;
	struct dev_context *devc;
	GSList *chl, chonly;
	float valf;

	(void)fd;
	(void)revents;

	sdi = cb_data;
	if (!sdi)
		return TRUE;

	devc = sdi->priv;
	if (!devc)
		return TRUE;

	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	memset(&analog, 0, sizeof(analog));
	analog.data = &valf;

	/*
	 * Reading from sysfs takes some time - try to keep up with samplerate.
	 */
	if (devc->samples_read) {
		cur_time = g_get_monotonic_time();
		diff_time = cur_time - devc->last_sample_fin;
		time_to_sleep = G_USEC_PER_SEC / devc->samplerate - diff_time;
		if (time_to_sleep > 0)
			g_usleep(time_to_sleep);
	}

	framep.type = SR_DF_FRAME_BEGIN;
	sr_session_send(cb_data, &framep);

	/*
	 * Due to different units used in each channel we're sending
	 * samples one-by-one.
	 */
	for (chl = sdi->channels; chl; chl = chl->next) {
		ch = chl->data;
		if (!ch->enabled)
			continue;
		chonly.next = NULL;
		chonly.data = ch;
		analog.channels = &chonly;
		analog.num_samples = 1;
		analog.mq = channel_to_mq(chl->data);
		analog.unit = channel_to_unit(ch);

		valf = read_sample(ch);

		sr_session_send(cb_data, &packet);
	}

	framep.type = SR_DF_FRAME_END;
	sr_session_send(cb_data, &framep);

	devc->samples_read++;
	if (devc->limit_samples > 0 &&
	    devc->samples_read >= devc->limit_samples) {
		sr_info("Requested number of samples reached.");
		sdi->driver->dev_acquisition_stop(sdi, cb_data);
		devc->last_sample_fin = g_get_monotonic_time();
		return TRUE;
	} else if (devc->limit_msec > 0) {
		cur_time = g_get_monotonic_time();
		elapsed_time = cur_time - devc->start_time;

		if (elapsed_time >= devc->limit_msec) {
			sr_info("Sampling time limit reached.");
			sdi->driver->dev_acquisition_stop(sdi, cb_data);
			devc->last_sample_fin = g_get_monotonic_time();
			return TRUE;
		}
	}

	devc->last_sample_fin = g_get_monotonic_time();
	return TRUE;
}
