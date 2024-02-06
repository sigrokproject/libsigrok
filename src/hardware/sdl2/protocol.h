/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2022 Tomas Mudrunka <harviecz@gmail.com>
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

#ifndef LIBSIGROK_HARDWARE_SDL2_PROTOCOL_H
#define LIBSIGROK_HARDWARE_SDL2_PROTOCOL_H

#define SDL_SAMPLES_TO_BYTES(bytes,spec) ((bytes)*((SDL_AUDIO_BITSIZE((spec).format)/8)*((spec).channels)))
#define SDL_BYTES_TO_SAMPLES(bytes,spec) ((bytes)/SDL_SAMPLES_TO_BYTES(1,(spec)))
#define SDL_FORMAT_MAX_VAL(f) (1ull<<(SDL_AUDIO_BITSIZE(f)-SDL_AUDIO_ISSIGNED(f)))

#include <stdint.h>
#include <string.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include <SDL2/SDL.h>

#define LOG_PREFIX "sdl2-audio-interface"

struct dev_context {
	const char* 	  sdl_device_name;
	SDL_AudioDeviceID sdl_device_index;
	SDL_AudioSpec	  sdl_device_spec;
	SDL_AudioDeviceID sdl_device_handle;

	uint64_t limit_samples;
	uint64_t limit_samples_remaining;
};

#endif
