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

/*
 * Linux-specific GPIO interface helpers. These functions could be moved out
 * of this directory if any other driver would happen to want to use them.
 */

#ifndef LIBSIGROK_GPIO_H
#define LIBSIGROK_GPIO_H

enum {
	GPIO_DIR_IN,
	GPIO_DIR_OUT,
};

SR_PRIV int sr_gpio_export(unsigned gpio);
SR_PRIV int sr_gpio_set_direction(unsigned gpio, unsigned direction);
SR_PRIV int sr_gpio_set_value(unsigned gpio, unsigned value);
SR_PRIV int sr_gpio_get_value(int gpio);
/* These functions export given gpio if it's not already exported. */
SR_PRIV int sr_gpio_setval_export(int gpio, int value);
SR_PRIV int sr_gpio_getval_export(int gpio);

#endif /* LIBSIGROK_GPIO_H */
