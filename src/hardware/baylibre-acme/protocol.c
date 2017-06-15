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

#include <config.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <glib/gstdio.h>
#include "protocol.h"
#include "gpio.h"

#define ACME_REV_A		1
#define ACME_REV_B		2

enum channel_type {
	ENRG_PWR = 1,
	ENRG_CURR,
	ENRG_VOL,
	TEMP_IN,
	TEMP_OUT,
};

struct channel_group_priv {
	uint8_t rev;
	int hwmon_num;
	int probe_type;
	int index;
	int has_pws;
	uint32_t pws_gpio;
};

struct channel_priv {
	int ch_type;
	int fd;
	int digits;
	float val;
	struct channel_group_priv *probe;
};

#define EEPROM_SERIAL_SIZE		16
#define EEPROM_TAG_SIZE			32

#define EEPROM_PROBE_TYPE_USB		1
#define EEPROM_PROBE_TYPE_JACK		2
#define EEPROM_PROBE_TYPE_HE10		3

struct probe_eeprom {
	uint32_t type;
	uint32_t rev;
	uint32_t shunt;
	uint8_t pwr_sw;
	uint8_t serial[EEPROM_SERIAL_SIZE];
	int8_t tag[EEPROM_TAG_SIZE];
};

#define EEPROM_SIZE (3 * sizeof(uint32_t) + 1 + EEPROM_SERIAL_SIZE + EEPROM_TAG_SIZE)

#define EEPROM_OFF_TYPE		0
#define EEPROM_OFF_REV		sizeof(uint32_t)
#define EEPROM_OFF_SHUNT	(2 * sizeof(uint32_t))
#define EEPROM_OFF_PWR_SW	(3 * sizeof(uint32_t))
#define EEPROM_OFF_SERIAL	(3 * sizeof(uint32_t) + 1)
#define EEPROM_OFF_TAG		(EEPROM_OFF_SERIAL + EEPROM_SERIAL_SIZE)

static const uint8_t enrg_i2c_addrs[] = {
	0x40, 0x41, 0x44, 0x45, 0x42, 0x43, 0x46, 0x47,
};

static const uint8_t temp_i2c_addrs[] = {
	0x0, 0x0, 0x0, 0x0, 0x4c, 0x49, 0x4f, 0x4b,
};

static const uint32_t revA_pws_gpios[] = {
	486, 498, 502, 482, 478, 506, 510, 474,
};

static const uint32_t revA_pws_info_gpios[] = {
	487, 499, 503, 483, 479, 507, 511, 475,
};

static const uint32_t revB_pws_gpios[] = {
	489, 491, 493, 495, 497, 499, 501, 503,
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

static void probe_eeprom_path(unsigned int addr, GString *path)
{
	g_string_printf(path,
			"/sys/class/i2c-dev/i2c-1/device/1-00%02x/eeprom",
			addr + 0x10);
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
		/* Don't log "No such file or directory" messages. */
		if (err->code != G_FILE_ERROR_NOENT)
			sr_dbg("Name for probe %d can't be read (%d): %s",
			       prb_num, err->code, err->message);
		g_string_free(path, TRUE);
		g_error_free(err);
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
		g_error_free(err);
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

	ch = sr_channel_new(sdi, devc->num_channels++,
			    SR_CHANNEL_ANALOG, TRUE, name);
	g_free(name);

	ch->priv = cp;
	cg->channels = g_slist_append(cg->channels, ch);
}

static int read_probe_eeprom(unsigned int addr, struct probe_eeprom *eeprom)
{
	GString *path = g_string_sized_new(64);
	char eeprom_buf[EEPROM_SIZE];
	ssize_t rd;
	int fd;

	probe_eeprom_path(addr, path);
	fd = g_open(path->str, O_RDONLY);
	g_string_free(path, TRUE);
	if (fd < 0)
		return -1;

	rd = read(fd, eeprom_buf, EEPROM_SIZE);
	close(fd);
	if (rd != EEPROM_SIZE)
		return -1;

	eeprom->type = RB32(eeprom_buf + EEPROM_OFF_TYPE);
	eeprom->rev = RB32(eeprom_buf + EEPROM_OFF_REV);
	eeprom->shunt = RB32(eeprom_buf + EEPROM_OFF_SHUNT);
	eeprom->pwr_sw = R8(eeprom_buf + EEPROM_OFF_PWR_SW);
	/* Don't care about the serial number and tag for now. */

	/* Check if we have some sensible values. */
	if (eeprom->rev != 'B')
		/* 'B' is the only supported revision with EEPROM for now. */
		return -1;

	if (eeprom->type != EEPROM_PROBE_TYPE_USB &&
	    eeprom->type != EEPROM_PROBE_TYPE_JACK &&
	    eeprom->type != EEPROM_PROBE_TYPE_HE10)
		return -1;

	return 0;
}

/* Some i2c slave addresses on revision B probes differ from revision A. */
static int revB_addr_to_num(unsigned int addr)
{
	switch (addr) {
	case 0x44: return 5;
	case 0x45: return 6;
	case 0x42: return 3;
	case 0x43: return 4;
	default:   return addr - 0x3f;
	}
}

SR_PRIV gboolean bl_acme_register_probe(struct sr_dev_inst *sdi, int type,
					unsigned int addr, int prb_num)
{
	struct sr_channel_group *cg;
	struct channel_group_priv *cgp;
	struct probe_eeprom eeprom;
	int hwmon, status;
	uint32_t gpio;

	/* Obtain the hwmon index. */
	hwmon = get_hwmon_index(addr);
	if (hwmon < 0)
		return FALSE;

	cg = g_malloc0(sizeof(struct sr_channel_group));
	cgp = g_malloc0(sizeof(struct channel_group_priv));
	cg->priv = cgp;

	/*
	 * See if we can read the EEPROM contents. If not, assume it's
	 * a revision A probe.
	 */
	memset(&eeprom, 0, sizeof(struct probe_eeprom));
	status = read_probe_eeprom(addr, &eeprom);
	cgp->rev = status < 0 ? ACME_REV_A : ACME_REV_B;

	prb_num = cgp->rev == ACME_REV_A ? prb_num : revB_addr_to_num(addr);

	cgp->hwmon_num = hwmon;
	cgp->probe_type = type;
	cgp->index = prb_num - 1;
	cg->name = g_strdup_printf("Probe_%d", prb_num);

	if (cgp->rev == ACME_REV_A) {
		gpio = revA_pws_info_gpios[cgp->index];
		cgp->has_pws = sr_gpio_getval_export(gpio);
		cgp->pws_gpio = revA_pws_gpios[cgp->index];
	} else {
		cgp->has_pws = eeprom.pwr_sw;
		cgp->pws_gpio = revB_pws_gpios[cgp->index];

		/*
		 * For revision B we can already try to set the shunt
		 * resistance according to the EEPROM contents.
		 *
		 * Keep the default value if shunt in EEPROM == 0.
		 */
		if (eeprom.shunt > 0)
			bl_acme_set_shunt(cg, UOHM_TO_MOHM(eeprom.shunt));
	}

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

SR_PRIV int bl_acme_get_probe_type(const struct sr_channel_group *cg)
{
	struct channel_group_priv *cgp = cg->priv;

	return cgp->probe_type;
}

SR_PRIV int bl_acme_probe_has_pws(const struct sr_channel_group *cg)
{
	struct channel_group_priv *cgp = cg->priv;

	return cgp->has_pws;
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

/*
 * Try setting the update_interval sysfs attribute for each probe according
 * to samplerate.
 */
SR_PRIV void bl_acme_maybe_set_update_interval(const struct sr_dev_inst *sdi,
					       uint64_t samplerate)
{
	struct sr_channel_group *cg;
	struct channel_group_priv *cgp;
	GString *hwmon;
	GSList *l;
	FILE *fd;

	for (l = sdi->channel_groups; l != NULL; l = l->next) {
		cg = l->data;
		cgp = cg->priv;

		hwmon = g_string_sized_new(64);
		g_string_append_printf(hwmon,
				"/sys/class/hwmon/hwmon%d/update_interval",
				cgp->hwmon_num);

		if (g_file_test(hwmon->str, G_FILE_TEST_EXISTS)) {
			fd = g_fopen(hwmon->str, "w");
			if (!fd) {
				g_string_free(hwmon, TRUE);
				continue;
			}

			g_fprintf(fd, "%" PRIu64 "\n", 1000 / samplerate);
			fclose(fd);
		}

		g_string_free(hwmon, TRUE);
	}
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
		g_error_free(err);
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
		sr_err("Error opening %s: %s", path->str, g_strerror(errno));
		ret = SR_ERR_IO;
		goto out;
	}

	g_fprintf(fd, "%" PRIu64 "\n", MOHM_TO_UOHM(shunt));
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

	if (!bl_acme_probe_has_pws(cg)) {
		sr_err("Probe has no power-switch");
		return SR_ERR_ARG;
	}

	val = sr_gpio_getval_export(cgp->pws_gpio);
	*off = val ? FALSE : TRUE;

	return SR_OK;
}

SR_PRIV int bl_acme_set_power_off(const struct sr_channel_group *cg,
				  gboolean off)
{
	struct channel_group_priv *cgp;
	int val;

	cgp = cg->priv;

	if (!bl_acme_probe_has_pws(cg)) {
		sr_err("Probe has no power-switch");
		return SR_ERR_ARG;
	}

	val = sr_gpio_setval_export(cgp->pws_gpio, off ? 0 : 1);
	if (val < 0) {
		sr_err("Error setting power-off state: gpio: %d",
		       cgp->pws_gpio);
		return SR_ERR_IO;
	}

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
static int type_digits(int type)
{
	switch (type) {
	case ENRG_PWR:
		return 6;
	case ENRG_CURR: /* Fallthrough */
	case ENRG_VOL: /* Fallthrough */
	case TEMP_IN: /* Fallthrough */
	case TEMP_OUT:
		return 3;
	default:
		return 0;
	}
}

static float read_sample(struct sr_channel *ch)
{
	struct channel_priv *chp;
	char buf[16];
	ssize_t len;
	int fd;

	chp = ch->priv;
	fd = chp->fd;

	lseek(fd, 0, SEEK_SET);

	len = read(fd, buf, sizeof(buf));
	if (len < 0) {
		sr_err("Error reading from channel %s (hwmon: %d): %s",
			ch->name, chp->probe->hwmon_num, g_strerror(errno));
		ch->enabled = FALSE;
		return -1.0;
	}

	chp->digits = type_digits(chp->ch_type);
	return strtol(buf, NULL, 10) * powf(10, -chp->digits);
}

SR_PRIV int bl_acme_open_channel(struct sr_channel *ch)
{
	struct channel_priv *chp;
	char path[64];
	const char *file;
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
		return SR_ERR;
	}

	snprintf(path, sizeof(path), "/sys/class/hwmon/hwmon%d/%s",
		 chp->probe->hwmon_num, file);

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		sr_err("Error opening %s: %s", path, g_strerror(errno));
		ch->enabled = FALSE;
		return SR_ERR;
	}

	chp->fd = fd;

	return 0;
}

SR_PRIV void bl_acme_close_channel(struct sr_channel *ch)
{
	struct channel_priv *chp;

	chp = ch->priv;
	close(chp->fd);
	chp->fd = -1;
}

SR_PRIV int bl_acme_receive_data(int fd, int revents, void *cb_data)
{
	uint64_t nrexpiration;
	struct sr_datafeed_packet packet, framep;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	struct sr_dev_inst *sdi;
	struct sr_channel *ch;
	struct channel_priv *chp;
	struct dev_context *devc;
	GSList *chl, chonly;
	unsigned i;

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
	sr_analog_init(&analog, &encoding, &meaning, &spec, 0);

	if (read(devc->timer_fd, &nrexpiration, sizeof(nrexpiration)) < 0) {
		sr_warn("Failed to read timer information");
		return TRUE;
	}

	/*
	 * We were not able to process the previous timer expiration, we are
	 * overloaded.
	 */
	if (nrexpiration > 1)
		devc->samples_missed += nrexpiration - 1;

	/*
	 * XXX This is a nasty workaround...
	 *
	 * At high sampling rates and maximum channels we are not able to
	 * acquire samples fast enough, even though frontends still think
	 * that samples arrive on time. This causes shifts in frontend
	 * plots.
	 *
	 * To compensate for the delay we check if any clock events were
	 * missed and - if so - don't really read the next value, but send
	 * the same sample as fast as possible. We do it until we are back
	 * on schedule.
	 *
	 * At high sampling rate this doesn't seem to visibly reduce the
	 * accuracy.
	 */
	for (i = 0; i < nrexpiration; i++) {
		framep.type = SR_DF_FRAME_BEGIN;
		sr_session_send(sdi, &framep);

		/*
		 * Due to different units used in each channel we're sending
		 * samples one-by-one.
		 */
		for (chl = sdi->channels; chl; chl = chl->next) {
			ch = chl->data;
			chp = ch->priv;

			if (!ch->enabled)
				continue;
			chonly.next = NULL;
			chonly.data = ch;
			analog.num_samples = 1;
			analog.meaning->channels = &chonly;
			analog.meaning->mq = channel_to_mq(chl->data);
			analog.meaning->unit = channel_to_unit(ch);

			if (i < 1)
				chp->val = read_sample(ch);

			analog.encoding->digits  = chp->digits;
			analog.spec->spec_digits = chp->digits;
			analog.data = &chp->val;
			sr_session_send(sdi, &packet);
		}

		framep.type = SR_DF_FRAME_END;
		sr_session_send(sdi, &framep);
	}

	sr_sw_limits_update_samples_read(&devc->limits, 1);

	if (sr_sw_limits_check(&devc->limits)) {
		sr_dev_acquisition_stop(sdi);
		return TRUE;
	}

	return TRUE;
}
