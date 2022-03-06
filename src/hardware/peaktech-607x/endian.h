// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2022 Enrico Scholz <enrico.scholz@ensc.de>
 */

#ifndef LIBSIGROK_HARDWARE_PEAKTECH_6070X_ENDIAN_H
#define LIBSIGROK_HARDWARE_PEAKTECH_6070X_ENDIAN_H

#include <config.h>

typedef struct {
	uint16_t		v;
} be16_t;

typedef struct {
	uint16_t		v;
} le16_t;

inline static uint16_t	swap_u16(uint16_t v) {
	return ((((v >> 8) & 0xffu) << 0) |
		(((v >> 0) & 0xffu) << 8));
}

inline static be16_t	cpu_to_be16(uint16_t v) {
#ifdef WORDS_BIGENDIAN
	return (be16_t){ v };
#else
	return (be16_t){ swap_u16(v) };
#endif
}

inline static le16_t	cpu_to_le16(uint16_t v) {
#ifdef WORDS_BIGENDIAN
	return (le16_t){ swap_u16(v) };
#else
	return (le16_t){ v };
#endif
}

inline static uint16_t	be16_to_cpu(be16_t v) {
#ifdef WORDS_BIGENDIAN
	return v.v;
#else
	return swap_u16(v.v);
#endif
}

inline static uint16_t	le16_to_cpu(le16_t v) {
#ifdef WORDS_BIGENDIAN
	return swap_u16(v.v);
#else
	return v.v;
#endif
}

#ifdef WORDS_BIGENDIAN
#  define IS_BIGENDIAN		(TRUE)
#else
#  define IS_BIGENDIAN		(FALSE)
#endif

#endif	/* LIBSIGROK_HARDWARE_PEAKTECH_6070X_ENDIAN_H */
