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

#include "libsigrok.h"
#include "libsigrok-internal.h"
#include "gpio.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#define LOG_PREFIX "gpio"

static int open_and_write(const gchar *path, const gchar *buf)
{
	FILE *fd;
	ssize_t wr;

	fd = g_fopen(path, "w");
	if (!fd) {
		sr_err("error opening %s: %s", path, strerror(errno));
		return -1;
	}

	wr = g_fprintf(fd, "%s", buf);
	fclose(fd);
	if (wr < 0) {
		sr_err("error writing to %s: %s", path, strerror(errno));
		return -1;
	}

	return 0;
}

SR_PRIV int sr_gpio_export(unsigned gpio)
{
	GString *path, *buf;
	gboolean exported;
	int status;

	path = g_string_sized_new(128);
	g_string_printf(path, "/sys/class/gpio/gpio%d", gpio);
	exported = g_file_test(path->str, G_FILE_TEST_IS_DIR);
	g_string_free(path, TRUE);
	if (exported)
		return 0; /* Already exported. */

	buf = g_string_sized_new(16);
	g_string_printf(buf, "%u\n", gpio);
	status = open_and_write("/sys/class/gpio/export", buf->str);
	g_string_free(buf, TRUE);

	return status;
}

SR_PRIV int sr_gpio_set_direction(unsigned gpio, unsigned direction)
{
	GString *path, *buf;
	int status;

	path = g_string_sized_new(128);
	buf = g_string_sized_new(16);
	g_string_printf(path, "/sys/class/gpio/gpio%d/direction", gpio);
	g_string_printf(buf, "%s\n", direction == GPIO_DIR_IN ? "in" : "out");

	status = open_and_write(path->str, buf->str);

	g_string_free(path, TRUE);
	g_string_free(buf, TRUE);

	return status;
}

SR_PRIV int sr_gpio_set_value(unsigned gpio, unsigned value)
{
	GString *path, *buf;
	int status;

	path = g_string_sized_new(128);
	buf = g_string_sized_new(16);
	g_string_printf(path, "/sys/class/gpio/gpio%d/value", gpio);
	g_string_printf(buf, "%d\n", value);

	status = open_and_write(path->str, buf->str);

	g_string_free(path, TRUE);
	g_string_free(buf, TRUE);

	return status;
}

SR_PRIV int sr_gpio_get_value(int gpio)
{
	FILE *fd;
	GString *path;
	int ret, status;

	path = g_string_sized_new(128);
	g_string_printf(path, "/sys/class/gpio/gpio%d/value", gpio);
	fd = g_fopen(path->str, "r");
	if (!fd) {
		sr_err("error opening %s: %s", path->str, strerror(errno));
		g_string_free(path, TRUE);
		return -1;
	}

	status = fscanf(fd, "%d", &ret);
	fclose(fd);
	if (status != 1) {
		sr_err("error reading from %s: %s", path, strerror(errno));
		g_string_free(path, TRUE);
		return -1;
	}

	g_string_free(path, TRUE);
	return ret;
}

SR_PRIV int sr_gpio_setval_export(int gpio, int value)
{
	int status;

	status = sr_gpio_export(gpio);
	if (status < 0)
		return status;

	status = sr_gpio_set_direction(gpio, GPIO_DIR_OUT);
	if (status < 0)
		return status;

	status = sr_gpio_set_value(gpio, value);
	if (status < 0)
		return status;

	return 0;
}

SR_PRIV int sr_gpio_getval_export(int gpio)
{
	int status;

	status = sr_gpio_export(gpio);
	if (status < 0)
		return status;

	status = sr_gpio_set_direction(gpio, GPIO_DIR_IN);
	if (status < 0)
		return status;

	return sr_gpio_get_value(gpio);
}
