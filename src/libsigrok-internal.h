/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Bert Vermeulen <bert@biot.com>
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

#ifndef LIBSIGROK_LIBSIGROK_INTERNAL_H
#define LIBSIGROK_LIBSIGROK_INTERNAL_H

#include "config.h"

#include <glib.h>
#ifdef HAVE_LIBHIDAPI
#include <hidapi.h>
#endif
#ifdef HAVE_LIBSERIALPORT
#include <libserialport.h>
#endif
#ifdef HAVE_LIBUSB_1_0
#include <libusb.h>
#endif
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

struct zip;
struct zip_stat;

/**
 * @file
 *
 * libsigrok private header file, only to be used internally.
 */

/*--- Macros ----------------------------------------------------------------*/

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

#ifndef ARRAY_AND_SIZE
#define ARRAY_AND_SIZE(a) (a), ARRAY_SIZE(a)
#endif

#ifndef G_SOURCE_FUNC
#define G_SOURCE_FUNC(f) ((GSourceFunc) (void (*)(void)) (f)) /* Since 2.58. */
#endif

#define SR_RECEIVE_DATA_CALLBACK(f) \
	((sr_receive_data_callback) (void (*)(void)) (f))

/**
 * Read a 8 bits unsigned integer out of memory.
 * @param x a pointer to the input memory
 * @return the corresponding unsigned integer
 */
static inline uint8_t read_u8(const uint8_t *p)
{
	return p[0];
}
#define R8(x)	read_u8((const uint8_t *)(x))

/**
 * Read an 8 bits signed integer out of memory.
 * @param x a pointer to the input memory
 * @return the corresponding signed integer
 */
static inline int8_t read_i8(const uint8_t *p)
{
	return (int8_t)p[0];
}

/**
 * Read a 16 bits big endian unsigned integer out of memory.
 * @param x a pointer to the input memory
 * @return the corresponding unsigned integer
 */
static inline uint16_t read_u16be(const uint8_t *p)
{
	uint16_t u;

	u = 0;
	u <<= 8; u |= p[0];
	u <<= 8; u |= p[1];

	return u;
}
#define RB16(x) read_u16be((const uint8_t *)(x))

/**
 * Read a 16 bits little endian unsigned integer out of memory.
 * @param x a pointer to the input memory
 * @return the corresponding unsigned integer
 */
static inline uint16_t read_u16le(const uint8_t *p)
{
	uint16_t u;

	u = 0;
	u <<= 8; u |= p[1];
	u <<= 8; u |= p[0];

	return u;
}
#define RL16(x) read_u16le((const uint8_t *)(x))

/**
 * Read a 16 bits big endian signed integer out of memory.
 * @param x a pointer to the input memory
 * @return the corresponding signed integer
 */
static inline int16_t read_i16be(const uint8_t *p)
{
	uint16_t u;
	int16_t i;

	u = read_u16be(p);
	i = (int16_t)u;

	return i;
}
#define RB16S(x) read_i16be((const uint8_t *)(x))

/**
 * Read a 16 bits little endian signed integer out of memory.
 * @param x a pointer to the input memory
 * @return the corresponding signed integer
 */
static inline int16_t read_i16le(const uint8_t *p)
{
	uint16_t u;
	int16_t i;

	u = read_u16le(p);
	i = (int16_t)u;

	return i;
}
#define RL16S(x) read_i16le((const uint8_t *)(x))

/**
 * Read a 24 bits little endian unsigned integer out of memory.
 * @param x a pointer to the input memory
 * @return the corresponding unsigned integer
 */
static inline uint32_t read_u24le(const uint8_t *p)
{
	uint32_t u;

	u = 0;
	u <<= 8; u |= p[2];
	u <<= 8; u |= p[1];
	u <<= 8; u |= p[0];

	return u;
}

/**
 * Read a 32 bits big endian unsigned integer out of memory.
 * @param x a pointer to the input memory
 * @return the corresponding unsigned integer
 */
static inline uint32_t read_u32be(const uint8_t *p)
{
	uint32_t u;

	u = 0;
	u <<= 8; u |= p[0];
	u <<= 8; u |= p[1];
	u <<= 8; u |= p[2];
	u <<= 8; u |= p[3];

	return u;
}
#define RB32(x) read_u32be((const uint8_t *)(x))

/**
 * Read a 32 bits little endian unsigned integer out of memory.
 * @param x a pointer to the input memory
 * @return the corresponding unsigned integer
 */
static inline uint32_t read_u32le(const uint8_t *p)
{
	uint32_t u;

	u = 0;
	u <<= 8; u |= p[3];
	u <<= 8; u |= p[2];
	u <<= 8; u |= p[1];
	u <<= 8; u |= p[0];

	return u;
}
#define RL32(x) read_u32le((const uint8_t *)(x))

/**
 * Read a 32 bits big endian signed integer out of memory.
 * @param x a pointer to the input memory
 * @return the corresponding signed integer
 */
static inline int32_t read_i32be(const uint8_t *p)
{
	uint32_t u;
	int32_t i;

	u = read_u32be(p);
	i = (int32_t)u;

	return i;
}
#define RB32S(x) read_i32be((const uint8_t *)(x))

/**
 * Read a 32 bits little endian signed integer out of memory.
 * @param x a pointer to the input memory
 * @return the corresponding signed integer
 */
static inline int32_t read_i32le(const uint8_t *p)
{
	uint32_t u;
	int32_t i;

	u = read_u32le(p);
	i = (int32_t)u;

	return i;
}
#define RL32S(x) read_i32le((const uint8_t *)(x))

/**
 * Read a 64 bits big endian unsigned integer out of memory.
 * @param x a pointer to the input memory
 * @return the corresponding unsigned integer
 */
static inline uint64_t read_u64be(const uint8_t *p)
{
	uint64_t u;

	u = 0;
	u <<= 8; u |= p[0];
	u <<= 8; u |= p[1];
	u <<= 8; u |= p[2];
	u <<= 8; u |= p[3];
	u <<= 8; u |= p[4];
	u <<= 8; u |= p[5];
	u <<= 8; u |= p[6];
	u <<= 8; u |= p[7];

	return u;
}
#define RB64(x) read_u64be((const uint8_t *)(x))

/**
 * Read a 64 bits little endian unsigned integer out of memory.
 * @param x a pointer to the input memory
 * @return the corresponding unsigned integer
 */
static inline uint64_t read_u64le(const uint8_t *p)
{
	uint64_t u;

	u = 0;
	u <<= 8; u |= p[7];
	u <<= 8; u |= p[6];
	u <<= 8; u |= p[5];
	u <<= 8; u |= p[4];
	u <<= 8; u |= p[3];
	u <<= 8; u |= p[2];
	u <<= 8; u |= p[1];
	u <<= 8; u |= p[0];

	return u;
}
#define RL64(x) read_u64le((const uint8_t *)(x))

/**
 * Read a 64 bits big endian signed integer out of memory.
 * @param x a pointer to the input memory
 * @return the corresponding unsigned integer
 */
static inline int64_t read_i64be(const uint8_t *p)
{
	uint64_t u;
	int64_t i;

	u = read_u64be(p);
	i = (int64_t)u;

	return i;
}
#define RB64S(x) read_i64be((const uint8_t *)(x))

/**
 * Read a 64 bits little endian signed integer out of memory.
 * @param x a pointer to the input memory
 * @return the corresponding unsigned integer
 */
static inline int64_t read_i64le(const uint8_t *p)
{
	uint64_t u;
	int64_t i;

	u = read_u64le(p);
	i = (int64_t)u;

	return i;
}
#define RL64S(x) read_i64le((const uint8_t *)(x))

/**
 * Read a 32 bits big endian float out of memory (single precision).
 * @param x a pointer to the input memory
 * @return the corresponding float
 */
static inline float read_fltbe(const uint8_t *p)
{
	/*
	 * Implementor's note: Strictly speaking the "union" trick
	 * is not portable. But this phrase was found to work on the
	 * project's supported platforms, and serve well until a more
	 * appropriate phrase is found.
	 */
	union { uint32_t u32; float flt; } u;
	float f;

	u.u32 = read_u32be(p);
	f = u.flt;

	return f;
}
#define RBFL(x) read_fltbe((const uint8_t *)(x))

/**
 * Read a 32 bits little endian float out of memory (single precision).
 * @param x a pointer to the input memory
 * @return the corresponding float
 */
static inline float read_fltle(const uint8_t *p)
{
	/*
	 * Implementor's note: Strictly speaking the "union" trick
	 * is not portable. But this phrase was found to work on the
	 * project's supported platforms, and serve well until a more
	 * appropriate phrase is found.
	 */
	union { uint32_t u32; float flt; } u;
	float f;

	u.u32 = read_u32le(p);
	f = u.flt;

	return f;
}
#define RLFL(x) read_fltle((const uint8_t *)(x))

/**
 * Read a 64 bits big endian float out of memory (double precision).
 * @param x a pointer to the input memory
 * @return the corresponding floating point value
 */
static inline double read_dblbe(const uint8_t *p)
{
	/*
	 * Implementor's note: Strictly speaking the "union" trick
	 * is not portable. But this phrase was found to work on the
	 * project's supported platforms, and serve well until a more
	 * appropriate phrase is found.
	 */
	union { uint64_t u64; double flt; } u;
	double f;

	u.u64 = read_u64be(p);
	f = u.flt;

	return f;
}

/**
 * Read a 64 bits little endian float out of memory (double precision).
 * @param x a pointer to the input memory
 * @return the corresponding floating point value
 */
static inline double read_dblle(const uint8_t *p)
{
	/*
	 * Implementor's note: Strictly speaking the "union" trick
	 * is not portable. But this phrase was found to work on the
	 * project's supported platforms, and serve well until a more
	 * appropriate phrase is found.
	 */
	union { uint64_t u64; double flt; } u;
	double f;

	u.u64 = read_u64le(p);
	f = u.flt;

	return f;
}
#define RLDB(x) read_dblle((const uint8_t *)(x))

/**
 * Write a 8 bits unsigned integer to memory.
 * @param p a pointer to the output memory
 * @param x the input unsigned integer
 */
static inline void write_u8(uint8_t *p, uint8_t x)
{
	p[0] = x;
}
#define W8(p, x) write_u8((uint8_t *)(p), (uint8_t)(x))

/**
 * Write a 16 bits unsigned integer to memory stored as big endian.
 * @param p a pointer to the output memory
 * @param x the input unsigned integer
 */
static inline void write_u16be(uint8_t *p, uint16_t x)
{
	p[1] = x & 0xff; x >>= 8;
	p[0] = x & 0xff; x >>= 8;
}
#define WB16(p, x) write_u16be((uint8_t *)(p), (uint16_t)(x))

/**
 * Write a 16 bits unsigned integer to memory stored as little endian.
 * @param p a pointer to the output memory
 * @param x the input unsigned integer
 */
static inline void write_u16le(uint8_t *p, uint16_t x)
{
	p[0] = x & 0xff; x >>= 8;
	p[1] = x & 0xff; x >>= 8;
}
#define WL16(p, x) write_u16le((uint8_t *)(p), (uint16_t)(x))

/**
 * Write a 32 bits unsigned integer to memory stored as big endian.
 * @param p a pointer to the output memory
 * @param x the input unsigned integer
 */
static inline void write_u32be(uint8_t *p, uint32_t x)
{
	p[3] = x & 0xff; x >>= 8;
	p[2] = x & 0xff; x >>= 8;
	p[1] = x & 0xff; x >>= 8;
	p[0] = x & 0xff; x >>= 8;
}
#define WB32(p, x) write_u32be((uint8_t *)(p), (uint32_t)(x))

/**
 * Write a 32 bits unsigned integer to memory stored as little endian.
 * @param p a pointer to the output memory
 * @param x the input unsigned integer
 */
static inline void write_u32le(uint8_t *p, uint32_t x)
{
	p[0] = x & 0xff; x >>= 8;
	p[1] = x & 0xff; x >>= 8;
	p[2] = x & 0xff; x >>= 8;
	p[3] = x & 0xff; x >>= 8;
}
#define WL32(p, x) write_u32le((uint8_t *)(p), (uint32_t)(x))

/**
 * Write a 64 bits unsigned integer to memory stored as big endian.
 * @param p a pointer to the output memory
 * @param x the input unsigned integer
 */
static inline void write_u64be(uint8_t *p, uint64_t x)
{
	p[7] = x & 0xff; x >>= 8;
	p[6] = x & 0xff; x >>= 8;
	p[5] = x & 0xff; x >>= 8;
	p[4] = x & 0xff; x >>= 8;
	p[3] = x & 0xff; x >>= 8;
	p[2] = x & 0xff; x >>= 8;
	p[1] = x & 0xff; x >>= 8;
	p[0] = x & 0xff; x >>= 8;
}

/**
 * Write a 64 bits unsigned integer to memory stored as little endian.
 * @param p a pointer to the output memory
 * @param x the input unsigned integer
 */
static inline void write_u64le(uint8_t *p, uint64_t x)
{
	p[0] = x & 0xff; x >>= 8;
	p[1] = x & 0xff; x >>= 8;
	p[2] = x & 0xff; x >>= 8;
	p[3] = x & 0xff; x >>= 8;
	p[4] = x & 0xff; x >>= 8;
	p[5] = x & 0xff; x >>= 8;
	p[6] = x & 0xff; x >>= 8;
	p[7] = x & 0xff; x >>= 8;
}
#define WL64(p, x) write_u64le((uint8_t *)(p), (uint64_t)(x))

/**
 * Write a 32 bits float to memory stored as big endian.
 * @param p a pointer to the output memory
 * @param x the input float
 */
static inline void write_fltbe(uint8_t *p, float x)
{
	union { uint32_t u; float f; } u;
	u.f = x;
	write_u32be(p, u.u);
}
#define WBFL(p, x) write_fltbe((uint8_t *)(p), (x))

/**
 * Write a 32 bits float to memory stored as little endian.
 * @param p a pointer to the output memory
 * @param x the input float
 */
static inline void write_fltle(uint8_t *p, float x)
{
	union { uint32_t u; float f; } u;
	u.f = x;
	write_u32le(p, u.u);
}
#define WLFL(p, x) write_fltle((uint8_t *)(p), float (x))

/**
 * Write a 64 bits float to memory stored as little endian.
 * @param p a pointer to the output memory
 * @param x the input floating point value
 */
static inline void write_dblle(uint8_t *p, double x)
{
	union { uint64_t u; double f; } u;
	u.f = x;
	write_u64le(p, u.u);
}
#define WLDB(p, x) write_dblle((uint8_t *)(p), float (x))

/* Endianess conversion helpers with read/write position increment. */

/**
 * Read unsigned 8bit integer from raw memory, increment read position.
 * @param[in, out] p Pointer into byte stream.
 * @return Retrieved integer value, unsigned.
 */
static inline uint8_t read_u8_inc(const uint8_t **p)
{
	uint8_t v;

	if (!p || !*p)
		return 0;
	v = read_u8(*p);
	*p += sizeof(v);

	return v;
}

/**
 * Read signed 8bit integer from raw memory, increment read position.
 * @param[in, out] p Pointer into byte stream.
 * @return Retrieved integer value, signed.
 */
static inline int8_t read_i8_inc(const uint8_t **p)
{
	int8_t v;

	if (!p || !*p)
		return 0;
	v = read_i8(*p);
	*p += sizeof(v);

	return v;
}

/**
 * Read unsigned 16bit integer from raw memory (big endian format), increment read position.
 * @param[in, out] p Pointer into byte stream.
 * @return Retrieved integer value, unsigned.
 */
static inline uint16_t read_u16be_inc(const uint8_t **p)
{
	uint16_t v;

	if (!p || !*p)
		return 0;
	v = read_u16be(*p);
	*p += sizeof(v);

	return v;
}

/**
 * Read unsigned 16bit integer from raw memory (little endian format), increment read position.
 * @param[in, out] p Pointer into byte stream.
 * @return Retrieved integer value, unsigned.
 */
static inline uint16_t read_u16le_inc(const uint8_t **p)
{
	uint16_t v;

	if (!p || !*p)
		return 0;
	v = read_u16le(*p);
	*p += sizeof(v);

	return v;
}

/**
 * Read signed 16bit integer from raw memory (big endian format), increment read position.
 * @param[in, out] p Pointer into byte stream.
 * @return Retrieved integer value, signed.
 */
static inline int16_t read_i16be_inc(const uint8_t **p)
{
	int16_t v;

	if (!p || !*p)
		return 0;
	v = read_i16be(*p);
	*p += sizeof(v);

	return v;
}

/**
 * Read signed 16bit integer from raw memory (little endian format), increment read position.
 * @param[in, out] p Pointer into byte stream.
 * @return Retrieved integer value, signed.
 */
static inline int16_t read_i16le_inc(const uint8_t **p)
{
	int16_t v;

	if (!p || !*p)
		return 0;
	v = read_i16le(*p);
	*p += sizeof(v);

	return v;
}

/**
 * Read unsigned 24bit integer from raw memory (little endian format), increment read position.
 * @param[in, out] p Pointer into byte stream.
 * @return Retrieved integer value, unsigned.
 */
static inline uint32_t read_u24le_inc(const uint8_t **p)
{
	uint32_t v;

	if (!p || !*p)
		return 0;
	v = read_u24le(*p);
	*p += 3 * sizeof(uint8_t);

	return v;
}

/**
 * Read unsigned 32bit integer from raw memory (big endian format), increment read position.
 * @param[in, out] p Pointer into byte stream.
 * @return Retrieved integer value, unsigned.
 */
static inline uint32_t read_u32be_inc(const uint8_t **p)
{
	uint32_t v;

	if (!p || !*p)
		return 0;
	v = read_u32be(*p);
	*p += sizeof(v);

	return v;
}

/**
 * Read unsigned 32bit integer from raw memory (little endian format), increment read position.
 * @param[in, out] p Pointer into byte stream.
 * @return Retrieved integer value, unsigned.
 */
static inline uint32_t read_u32le_inc(const uint8_t **p)
{
	uint32_t v;

	if (!p || !*p)
		return 0;
	v = read_u32le(*p);
	*p += sizeof(v);

	return v;
}

/**
 * Read signed 32bit integer from raw memory (big endian format), increment read position.
 * @param[in, out] p Pointer into byte stream.
 * @return Retrieved integer value, signed.
 */
static inline int32_t read_i32be_inc(const uint8_t **p)
{
	int32_t v;

	if (!p || !*p)
		return 0;
	v = read_i32be(*p);
	*p += sizeof(v);

	return v;
}

/**
 * Read signed 32bit integer from raw memory (little endian format), increment read position.
 * @param[in, out] p Pointer into byte stream.
 * @return Retrieved integer value, signed.
 */
static inline int32_t read_i32le_inc(const uint8_t **p)
{
	int32_t v;

	if (!p || !*p)
		return 0;
	v = read_i32le(*p);
	*p += sizeof(v);

	return v;
}

/**
 * Read unsigned 64bit integer from raw memory (big endian format), increment read position.
 * @param[in, out] p Pointer into byte stream.
 * @return Retrieved integer value, unsigned.
 */
static inline uint64_t read_u64be_inc(const uint8_t **p)
{
	uint64_t v;

	if (!p || !*p)
		return 0;
	v = read_u64be(*p);
	*p += sizeof(v);

	return v;
}

/**
 * Read unsigned 64bit integer from raw memory (little endian format), increment read position.
 * @param[in, out] p Pointer into byte stream.
 * @return Retrieved integer value, unsigned.
 */
static inline uint64_t read_u64le_inc(const uint8_t **p)
{
	uint64_t v;

	if (!p || !*p)
		return 0;
	v = read_u64le(*p);
	*p += sizeof(v);

	return v;
}

/**
 * Read 32bit float from raw memory (big endian format), increment read position.
 * @param[in, out] p Pointer into byte stream.
 * @return Retrieved float value.
 */
static inline float read_fltbe_inc(const uint8_t **p)
{
	float v;

	if (!p || !*p)
		return 0;
	v = read_fltbe(*p);
	*p += sizeof(v);

	return v;
}

/**
 * Read 32bit float from raw memory (little endian format), increment read position.
 * @param[in, out] p Pointer into byte stream.
 * @return Retrieved float value.
 */
static inline float read_fltle_inc(const uint8_t **p)
{
	float v;

	if (!p || !*p)
		return 0;
	v = read_fltle(*p);
	*p += sizeof(v);

	return v;
}

/**
 * Read 64bit float from raw memory (big endian format), increment read position.
 * @param[in, out] p Pointer into byte stream.
 * @return Retrieved float value.
 */
static inline double read_dblbe_inc(const uint8_t **p)
{
	double v;

	if (!p || !*p)
		return 0;
	v = read_dblbe(*p);
	*p += sizeof(v);

	return v;
}

/**
 * Read 64bit float from raw memory (little endian format), increment read position.
 * @param[in, out] p Pointer into byte stream.
 * @return Retrieved float value.
 */
static inline double read_dblle_inc(const uint8_t **p)
{
	double v;

	if (!p || !*p)
		return 0;
	v = read_dblle(*p);
	*p += sizeof(v);

	return v;
}

/**
 * Write unsigned 8bit integer to raw memory, increment write position.
 * @param[in, out] p Pointer into byte stream.
 * @param[in] x Value to write.
 */
static inline void write_u8_inc(uint8_t **p, uint8_t x)
{
	if (!p || !*p)
		return;
	write_u8(*p, x);
	*p += sizeof(x);
}

/**
 * Write unsigned 16bit big endian integer to raw memory, increment write position.
 * @param[in, out] p Pointer into byte stream.
 * @param[in] x Value to write.
 */
static inline void write_u16be_inc(uint8_t **p, uint16_t x)
{
	if (!p || !*p)
		return;
	write_u16be(*p, x);
	*p += sizeof(x);
}

/**
 * Write unsigned 16bit little endian integer to raw memory, increment write position.
 * @param[in, out] p Pointer into byte stream.
 * @param[in] x Value to write.
 */
static inline void write_u16le_inc(uint8_t **p, uint16_t x)
{
	if (!p || !*p)
		return;
	write_u16le(*p, x);
	*p += sizeof(x);
}

/**
 * Write unsigned 32bit big endian integer to raw memory, increment write position.
 * @param[in, out] p Pointer into byte stream.
 * @param[in] x Value to write.
 */
static inline void write_u32be_inc(uint8_t **p, uint32_t x)
{
	if (!p || !*p)
		return;
	write_u32be(*p, x);
	*p += sizeof(x);
}

/**
 * Write unsigned 32bit little endian integer to raw memory, increment write position.
 * @param[in, out] p Pointer into byte stream.
 * @param[in] x Value to write.
 */
static inline void write_u32le_inc(uint8_t **p, uint32_t x)
{
	if (!p || !*p)
		return;
	write_u32le(*p, x);
	*p += sizeof(x);
}

/**
 * Write unsigned 64bit little endian integer to raw memory, increment write position.
 * @param[in, out] p Pointer into byte stream.
 * @param[in] x Value to write.
 */
static inline void write_u64le_inc(uint8_t **p, uint64_t x)
{
	if (!p || !*p)
		return;
	write_u64le(*p, x);
	*p += sizeof(x);
}

/**
 * Write single precision little endian float to raw memory, increment write position.
 * @param[in, out] p Pointer into byte stream.
 * @param[in] x Value to write.
 */
static inline void write_fltle_inc(uint8_t **p, float x)
{
	if (!p || !*p)
		return;
	write_fltle(*p, x);
	*p += sizeof(x);
}

/**
 * Write double precision little endian float to raw memory, increment write position.
 * @param[in, out] p Pointer into byte stream.
 * @param[in] x Value to write.
 */
static inline void write_dblle_inc(uint8_t **p, double x)
{
	if (!p || !*p)
		return;
	write_dblle(*p, x);
	*p += sizeof(x);
}

/* Portability fixes for FreeBSD. */
#ifdef __FreeBSD__
#define LIBUSB_CLASS_APPLICATION 0xfe
#define libusb_has_capability(x) 0
#define libusb_handle_events_timeout_completed(ctx, tv, c) \
	libusb_handle_events_timeout(ctx, tv)
#endif

/* Static definitions of structs ending with an all-zero entry are a
 * problem when compiling with -Wmissing-field-initializers: GCC
 * suppresses the warning only with { 0 }, clang wants { } */
#ifdef __clang__
#define ALL_ZERO { }
#else
#define ALL_ZERO { 0 }
#endif

#ifdef __APPLE__
#define SR_DRIVER_LIST_SECTION "__DATA,__sr_driver_list"
#else
#define SR_DRIVER_LIST_SECTION "__sr_driver_list"
#endif

#if !defined SR_DRIVER_LIST_NOREORDER && defined __has_attribute
#if __has_attribute(no_reorder)
#define SR_DRIVER_LIST_NOREORDER __attribute__((no_reorder))
#endif
#endif
#if !defined SR_DRIVER_LIST_NOREORDER
#define SR_DRIVER_LIST_NOREORDER /* EMPTY */
#endif

/**
 * Register a list of hardware drivers.
 *
 * This macro can be used to register multiple hardware drivers to the library.
 * This is useful when a driver supports multiple similar but slightly
 * different devices that require different sr_dev_driver struct definitions.
 *
 * For registering only a single driver see SR_REGISTER_DEV_DRIVER().
 *
 * Example:
 * @code{c}
 * #define MY_DRIVER(_name) \
 *     &(struct sr_dev_driver){ \
 *         .name = _name, \
 *         ...
 *     };
 *
 * SR_REGISTER_DEV_DRIVER_LIST(my_driver_infos,
 *     MY_DRIVER("driver 1"),
 *     MY_DRIVER("driver 2"),
 *     ...
 * );
 * @endcode
 *
 * @param name Name to use for the driver list identifier.
 * @param ... Comma separated list of pointers to sr_dev_driver structs.
 */
#define SR_REGISTER_DEV_DRIVER_LIST(name, ...) \
	static const struct sr_dev_driver *name[] \
		SR_DRIVER_LIST_NOREORDER \
		__attribute__((section (SR_DRIVER_LIST_SECTION), used, \
			aligned(sizeof(struct sr_dev_driver *)))) \
		= { \
			__VA_ARGS__ \
		};

/**
 * Register a hardware driver.
 *
 * This macro is used to register a hardware driver with the library. It has
 * to be used in order to make the driver accessible to applications using the
 * library.
 *
 * The macro invocation should be placed directly under the struct
 * sr_dev_driver definition.
 *
 * Example:
 * @code{c}
 * static struct sr_dev_driver driver_info = {
 *     .name = "driver",
 *     ....
 * };
 * SR_REGISTER_DEV_DRIVER(driver_info);
 * @endcode
 *
 * @param name Identifier name of sr_dev_driver struct to register.
 */
#define SR_REGISTER_DEV_DRIVER(name) \
	SR_REGISTER_DEV_DRIVER_LIST(name##_list, &name);

SR_API void sr_drivers_init(struct sr_context *context);

struct sr_context {
	struct sr_dev_driver **driver_list;
#ifdef HAVE_LIBUSB_1_0
	libusb_context *libusb_ctx;
#endif
	sr_resource_open_callback resource_open_cb;
	sr_resource_close_callback resource_close_cb;
	sr_resource_read_callback resource_read_cb;
	void *resource_cb_data;
};

/** Input module metadata keys. */
enum sr_input_meta_keys {
	/** The input filename, if there is one. */
	SR_INPUT_META_FILENAME = 0x01,
	/** The input file's size in bytes. */
	SR_INPUT_META_FILESIZE = 0x02,
	/** The first 128 bytes of the file, provided as a GString. */
	SR_INPUT_META_HEADER = 0x04,

	/** The module cannot identify a file without this metadata. */
	SR_INPUT_META_REQUIRED = 0x80,
};

/** Input (file) module struct. */
struct sr_input {
	/**
	 * A pointer to this input module's 'struct sr_input_module'.
	 */
	const struct sr_input_module *module;
	GString *buf;
	struct sr_dev_inst *sdi;
	gboolean sdi_ready;
	void *priv;
};

/** Input (file) module driver. */
struct sr_input_module {
	/**
	 * A unique ID for this input module, suitable for use in command-line
	 * clients, [a-z0-9-]. Must not be NULL.
	 */
	const char *id;

	/**
	 * A unique name for this input module, suitable for use in GUI
	 * clients, can contain UTF-8. Must not be NULL.
	 */
	const char *name;

	/**
	 * A short description of the input module. Must not be NULL.
	 *
	 * This can be displayed by frontends, e.g. when selecting the input
	 * module for saving a file.
	 */
	const char *desc;

	/**
	 * A NULL terminated array of strings containing a list of file name
	 * extensions typical for the input file format, or NULL if there is
	 * no typical extension for this file format.
	 */
	const char *const *exts;

	/**
	 * Zero-terminated list of metadata items the module needs to be able
	 * to identify an input stream. Can be all-zero, if the module cannot
	 * identify streams at all, i.e. has to be forced into use.
	 *
	 * Each item is one of:
	 *   SR_INPUT_META_FILENAME
	 *   SR_INPUT_META_FILESIZE
	 *   SR_INPUT_META_HEADER
	 *
	 * If the high bit (SR_INPUT META_REQUIRED) is set, the module cannot
	 * identify a stream without the given metadata.
	 */
	const uint8_t metadata[8];

	/**
	 * Returns a NULL-terminated list of options this module can take.
	 * Can be NULL, if the module has no options.
	 */
	const struct sr_option *(*options) (void);

	/**
	 * Check if this input module can load and parse the specified stream.
	 *
	 * @param[in] metadata Metadata the module can use to identify the stream.
	 * @param[out] confidence "Strength" of the detection.
	 *   Specialized handlers can take precedence over generic/basic support.
	 *
	 * @retval SR_OK This module knows the format.
	 * @retval SR_ERR_NA There wasn't enough data for this module to
	 *   positively identify the format.
	 * @retval SR_ERR_DATA This module knows the format, but cannot handle
	 *   it. This means the stream is either corrupt, or indicates a
	 *   feature that the module does not support.
	 * @retval SR_ERR This module does not know the format.
	 *
	 * Lower numeric values of 'confidence' mean that the input module
	 * stronger believes in its capability to handle this specific format.
	 * This way, multiple input modules can claim support for a format,
	 * and the application can pick the best match, or try fallbacks
	 * in case of errors. This approach also copes with formats that
	 * are unreliable to detect in the absence of magic signatures.
	 */
	int (*format_match) (GHashTable *metadata, unsigned int *confidence);

	/**
	 * Initialize the input module.
	 *
	 * @retval SR_OK Success
	 * @retval other Negative error code.
	 */
	int (*init) (struct sr_input *in, GHashTable *options);

	/**
	 * Send data to the specified input instance.
	 *
	 * When an input module instance is created with sr_input_new(), this
	 * function is used to feed data to the instance.
	 *
	 * As enough data gets fed into this function to completely populate
	 * the device instance associated with this input instance, this is
	 * guaranteed to return the moment it's ready. This gives the caller
	 * the chance to examine the device instance, attach session callbacks
	 * and so on.
	 *
	 * @retval SR_OK Success
	 * @retval other Negative error code.
	 */
	int (*receive) (struct sr_input *in, GString *buf);

	/**
	 * Signal the input module no more data will come.
	 *
	 * This will cause the module to process any data it may have buffered.
	 * The SR_DF_END packet will also typically be sent at this time.
	 */
	int (*end) (struct sr_input *in);

	/**
	 * Reset the input module's input handling structures.
	 *
	 * Causes the input module to reset its internal state so that we can
	 * re-send the input data from the beginning without having to
	 * re-create the entire input module.
	 *
	 * @retval SR_OK Success.
	 * @retval other Negative error code.
	 */
	int (*reset) (struct sr_input *in);

	/**
	 * This function is called after the caller is finished using
	 * the input module, and can be used to free any internal
	 * resources the module may keep.
	 *
	 * This function is optional.
	 *
	 * @retval SR_OK Success
	 * @retval other Negative error code.
	 */
	void (*cleanup) (struct sr_input *in);
};

/** Output module instance. */
struct sr_output {
	/** A pointer to this output's module. */
	const struct sr_output_module *module;

	/**
	 * The device for which this output module is creating output. This
	 * can be used by the module to find out channel names and numbers.
	 */
	const struct sr_dev_inst *sdi;

	/**
	 * The name of the file that the data should be written to.
	 */
	const char *filename;

	/**
	 * A generic pointer which can be used by the module to keep internal
	 * state between calls into its callback functions.
	 *
	 * For example, the module might store a pointer to a chunk of output
	 * there, and only flush it when it reaches a certain size.
	 */
	void *priv;
};

/** Output module driver. */
struct sr_output_module {
	/**
	 * A unique ID for this output module, suitable for use in command-line
	 * clients, [a-z0-9-]. Must not be NULL.
	 */
	const char *id;

	/**
	 * A unique name for this output module, suitable for use in GUI
	 * clients, can contain UTF-8. Must not be NULL.
	 */
	const char *name;

	/**
	 * A short description of the output module. Must not be NULL.
	 *
	 * This can be displayed by frontends, e.g. when selecting the output
	 * module for saving a file.
	 */
	const char *desc;

	/**
	 * A NULL terminated array of strings containing a list of file name
	 * extensions typical for the input file format, or NULL if there is
	 * no typical extension for this file format.
	 */
	const char *const *exts;

	/**
	 * Bitfield containing flags that describe certain properties
	 * this output module may or may not have.
	 * @see sr_output_flags
	 */
	const uint64_t flags;

	/**
	 * Returns a NULL-terminated list of options this module can take.
	 * Can be NULL, if the module has no options.
	 */
	const struct sr_option *(*options) (void);

	/**
	 * This function is called once, at the beginning of an output stream.
	 *
	 * The device struct will be available in the output struct passed in,
	 * as well as the param field -- which may be NULL or an empty string,
	 * if no parameter was passed.
	 *
	 * The module can use this to initialize itself, create a struct for
	 * keeping state and storing it in the <code>internal</code> field.
	 *
	 * @param o Pointer to the respective 'struct sr_output'.
	 *
	 * @retval SR_OK Success
	 * @retval other Negative error code.
	 */
	int (*init) (struct sr_output *o, GHashTable *options);

	/**
	 * This function is passed a copy of every packet in the data feed.
	 * Any output generated by the output module in response to the
	 * packet should be returned in a newly allocated GString
	 * <code>out</code>, which will be freed by the caller.
	 *
	 * Packets not of interest to the output module can just be ignored,
	 * and the <code>out</code> parameter set to NULL.
	 *
	 * @param o Pointer to the respective 'struct sr_output'.
	 * @param sdi The device instance that generated the packet.
	 * @param packet The complete packet.
	 * @param out A pointer where a GString * should be stored if
	 * the module generates output, or NULL if not.
	 *
	 * @retval SR_OK Success
	 * @retval other Negative error code.
	 */
	int (*receive) (const struct sr_output *o,
			const struct sr_datafeed_packet *packet, GString **out);

	/**
	 * This function is called after the caller is finished using
	 * the output module, and can be used to free any internal
	 * resources the module may keep.
	 *
	 * @retval SR_OK Success
	 * @retval other Negative error code.
	 */
	int (*cleanup) (struct sr_output *o);
};

/** Transform module instance. */
struct sr_transform {
	/** A pointer to this transform's module. */
	const struct sr_transform_module *module;

	/**
	 * The device for which this transform module is used. This
	 * can be used by the module to find out channel names and numbers.
	 */
	const struct sr_dev_inst *sdi;

	/**
	 * A generic pointer which can be used by the module to keep internal
	 * state between calls into its callback functions.
	 */
	void *priv;
};

struct sr_transform_module {
	/**
	 * A unique ID for this transform module, suitable for use in
	 * command-line clients, [a-z0-9-]. Must not be NULL.
	 */
	const char *id;

	/**
	 * A unique name for this transform module, suitable for use in GUI
	 * clients, can contain UTF-8. Must not be NULL.
	 */
	const char *name;

	/**
	 * A short description of the transform module. Must not be NULL.
	 *
	 * This can be displayed by frontends, e.g. when selecting
	 * which transform module(s) to add.
	 */
	const char *desc;

	/**
	 * Returns a NULL-terminated list of options this transform module
	 * can take. Can be NULL, if the transform module has no options.
	 */
	const struct sr_option *(*options) (void);

	/**
	 * This function is called once, at the beginning of a stream.
	 *
	 * @param t Pointer to the respective 'struct sr_transform'.
	 * @param options Hash table of options for this transform module.
	 *                Can be NULL if no options are to be used.
	 *
	 * @retval SR_OK Success
	 * @retval other Negative error code.
	 */
	int (*init) (struct sr_transform *t, GHashTable *options);

	/**
	 * This function is passed a pointer to every packet in the data feed.
	 *
	 * It can either return (in packet_out) a pointer to another packet
	 * (possibly the exact same packet it got as input), or NULL.
	 *
	 * @param t Pointer to the respective 'struct sr_transform'.
	 * @param packet_in Pointer to a datafeed packet.
	 * @param packet_out Pointer to the resulting datafeed packet after
	 *                   this function was run. If NULL, the transform
	 *                   module intentionally didn't output a new packet.
	 *
	 * @retval SR_OK Success
	 * @retval other Negative error code.
	 */
	int (*receive) (const struct sr_transform *t,
			struct sr_datafeed_packet *packet_in,
			struct sr_datafeed_packet **packet_out);

	/**
	 * This function is called after the caller is finished using
	 * the transform module, and can be used to free any internal
	 * resources the module may keep.
	 *
	 * @retval SR_OK Success
	 * @retval other Negative error code.
	 */
	int (*cleanup) (struct sr_transform *t);
};

#ifdef HAVE_LIBUSB_1_0
/** USB device instance */
struct sr_usb_dev_inst {
	/** USB bus */
	uint8_t bus;
	/** Device address on USB bus */
	uint8_t address;
	/** libusb device handle */
	struct libusb_device_handle *devhdl;
};
#endif

struct sr_serial_dev_inst;
#ifdef HAVE_SERIAL_COMM
struct ser_lib_functions;
struct ser_hid_chip_functions;
struct sr_bt_desc;
typedef void (*serial_rx_chunk_callback)(struct sr_serial_dev_inst *serial,
	void *cb_data, const void *buf, size_t count);
struct sr_serial_dev_inst {
	/** Port name, e.g. '/dev/tty42'. */
	char *port;
	/** Comm params for serial_set_paramstr(). */
	char *serialcomm;
	struct ser_lib_functions *lib_funcs;
	struct {
		int bit_rate;
		int data_bits;
		int parity_bits;
		int stop_bits;
	} comm_params;
	GString *rcv_buffer;
	serial_rx_chunk_callback rx_chunk_cb_func;
	void *rx_chunk_cb_data;
#ifdef HAVE_LIBSERIALPORT
	/** libserialport port handle */
	struct sp_port *sp_data;
#endif
#ifdef HAVE_LIBHIDAPI
	enum ser_hid_chip_t {
		SER_HID_CHIP_UNKNOWN,		/**!< place holder */
		SER_HID_CHIP_BTC_BU86X,		/**!< Brymen BU86x */
		SER_HID_CHIP_SIL_CP2110,	/**!< SiLabs CP2110 */
		SER_HID_CHIP_VICTOR_DMM,	/**!< Victor 70/86 DMM cable */
		SER_HID_CHIP_WCH_CH9325,	/**!< WCH CH9325 */
		SER_HID_CHIP_LAST,		/**!< sentinel */
	} hid_chip;
	struct ser_hid_chip_functions *hid_chip_funcs;
	char *usb_path;
	char *usb_serno;
	const char *hid_path;
	hid_device *hid_dev;
	GSList *hid_source_args;
#endif
#ifdef HAVE_BLUETOOTH
	enum ser_bt_conn_t {
		SER_BT_CONN_UNKNOWN,	/**!< place holder */
		SER_BT_CONN_RFCOMM,	/**!< BT classic, RFCOMM channel */
		SER_BT_CONN_BLE122,	/**!< BLE, BLE122 module, indications */
		SER_BT_CONN_NRF51,	/**!< BLE, Nordic nRF51, notifications */
		SER_BT_CONN_CC254x,	/**!< BLE, TI CC254x, notifications */
		SER_BT_CONN_MAX,	/**!< sentinel */
	} bt_conn_type;
	char *bt_addr_local;
	char *bt_addr_remote;
	size_t bt_rfcomm_channel;
	uint16_t bt_notify_handle_read;
	uint16_t bt_notify_handle_write;
	uint16_t bt_notify_handle_cccd;
	uint16_t bt_notify_value_cccd;
	struct sr_bt_desc *bt_desc;
	GSList *bt_source_args;
#endif
};
#endif

struct sr_usbtmc_dev_inst {
	char *device;
	int fd;
};

/* Private driver context. */
struct drv_context {
	/** sigrok context */
	struct sr_context *sr_ctx;
	GSList *instances;
};

/*--- log.c -----------------------------------------------------------------*/

#if defined(_WIN32) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 4))
/*
 * On MinGW, we need to specify the gnu_printf format flavor or GCC
 * will assume non-standard Microsoft printf syntax.
 */
SR_PRIV int sr_log(int loglevel, const char *format, ...)
		__attribute__((__format__ (__gnu_printf__, 2, 3)));
#else
SR_PRIV int sr_log(int loglevel, const char *format, ...) G_GNUC_PRINTF(2, 3);
#endif

/* Message logging helpers with subsystem-specific prefix string. */
#define sr_spew(...)	sr_log(SR_LOG_SPEW, LOG_PREFIX ": " __VA_ARGS__)
#define sr_dbg(...)	sr_log(SR_LOG_DBG,  LOG_PREFIX ": " __VA_ARGS__)
#define sr_info(...)	sr_log(SR_LOG_INFO, LOG_PREFIX ": " __VA_ARGS__)
#define sr_warn(...)	sr_log(SR_LOG_WARN, LOG_PREFIX ": " __VA_ARGS__)
#define sr_err(...)	sr_log(SR_LOG_ERR,  LOG_PREFIX ": " __VA_ARGS__)

/*--- device.c --------------------------------------------------------------*/

/** Scan options supported by a driver. */
#define SR_CONF_SCAN_OPTIONS 0x7FFF0000

/** Device options for a particular device. */
#define SR_CONF_DEVICE_OPTIONS 0x7FFF0001

/** Mask for separating config keys from capabilities. */
#define SR_CONF_MASK 0x1fffffff

/** Values for the changes argument of sr_dev_driver.config_channel_set. */
enum {
	/** The enabled state of the channel has been changed. */
	SR_CHANNEL_SET_ENABLED = 1 << 0,
};

SR_PRIV struct sr_channel *sr_channel_new(struct sr_dev_inst *sdi,
		int index, int type, gboolean enabled, const char *name);
SR_PRIV void sr_channel_free(struct sr_channel *ch);
SR_PRIV void sr_channel_free_cb(void *p);
SR_PRIV struct sr_channel *sr_next_enabled_channel(const struct sr_dev_inst *sdi,
		struct sr_channel *cur_channel);
SR_PRIV gboolean sr_channels_differ(struct sr_channel *ch1, struct sr_channel *ch2);
SR_PRIV gboolean sr_channel_lists_differ(GSList *l1, GSList *l2);

/** Device instance data */
struct sr_dev_inst {
	/** Device driver. */
	struct sr_dev_driver *driver;
	/** Device instance status. SR_ST_NOT_FOUND, etc. */
	int status;
	/** Device instance type. SR_INST_USB, etc. */
	int inst_type;
	/** Device vendor. */
	char *vendor;
	/** Device model. */
	char *model;
	/** Device version. */
	char *version;
	/** Serial number. */
	char *serial_num;
	/** Connection string to uniquely identify devices. */
	char *connection_id;
	/** List of channels. */
	GSList *channels;
	/** List of sr_channel_group structs */
	GSList *channel_groups;
	/** Device instance connection data (used?) */
	void *conn;
	/** Device instance private data (used?) */
	void *priv;
	/** Session to which this device is currently assigned. */
	struct sr_session *session;
};

/* Generic device instances */
SR_PRIV void sr_dev_inst_free(struct sr_dev_inst *sdi);

#ifdef HAVE_LIBUSB_1_0
/* USB-specific instances */
SR_PRIV struct sr_usb_dev_inst *sr_usb_dev_inst_new(uint8_t bus,
		uint8_t address, struct libusb_device_handle *hdl);
SR_PRIV void sr_usb_dev_inst_free(struct sr_usb_dev_inst *usb);
#endif

#ifdef HAVE_SERIAL_COMM
#ifndef HAVE_LIBSERIALPORT
/*
 * Some identifiers which initially got provided by libserialport are
 * used internally within the libsigrok serial layer's implementation,
 * while libserialport no longer is the exclusive provider of serial
 * communication support. Declare the identifiers here so they remain
 * available across all build configurations.
 */
enum libsp_parity {
	SP_PARITY_NONE = 0,
	SP_PARITY_ODD = 1,
	SP_PARITY_EVEN = 2,
	SP_PARITY_MARK = 3,
	SP_PARITY_SPACE = 4,
};

enum libsp_flowcontrol {
	SP_FLOWCONTROL_NONE = 0,
	SP_FLOWCONTROL_XONXOFF = 1,
	SP_FLOWCONTROL_RTSCTS = 2,
	SP_FLOWCONTROL_DTRDSR = 3,
};
#endif

/* Serial-specific instances */
SR_PRIV struct sr_serial_dev_inst *sr_serial_dev_inst_new(const char *port,
		const char *serialcomm);
SR_PRIV void sr_serial_dev_inst_free(struct sr_serial_dev_inst *serial);
#endif

/* USBTMC-specific instances */
SR_PRIV struct sr_usbtmc_dev_inst *sr_usbtmc_dev_inst_new(const char *device);
SR_PRIV void sr_usbtmc_dev_inst_free(struct sr_usbtmc_dev_inst *usbtmc);

/*--- hwdriver.c ------------------------------------------------------------*/

SR_PRIV const GVariantType *sr_variant_type_get(int datatype);
SR_PRIV int sr_variant_type_check(uint32_t key, GVariant *data);
SR_PRIV void sr_hw_cleanup_all(const struct sr_context *ctx);
SR_PRIV struct sr_config *sr_config_new(uint32_t key, GVariant *data);
SR_PRIV void sr_config_free(struct sr_config *src);
SR_PRIV int sr_dev_acquisition_start(struct sr_dev_inst *sdi);
SR_PRIV int sr_dev_acquisition_stop(struct sr_dev_inst *sdi);

/*--- session.c -------------------------------------------------------------*/

struct sr_session {
	/** Context this session exists in. */
	struct sr_context *ctx;
	/** List of struct sr_dev_inst pointers. */
	GSList *devs;
	/** List of struct sr_dev_inst pointers owned by this session. */
	GSList *owned_devs;
	/** List of struct datafeed_callback pointers. */
	GSList *datafeed_callbacks;
	GSList *transforms;
	struct sr_trigger *trigger;

	/** Callback to invoke on session stop. */
	sr_session_stopped_callback stopped_callback;
	/** User data to be passed to the session stop callback. */
	void *stopped_cb_data;

	/** Mutex protecting the main context pointer. */
	GMutex main_mutex;
	/** Context of the session main loop. */
	GMainContext *main_context;

	/** Registered event sources for this session. */
	GHashTable *event_sources;
	/** Session main loop. */
	GMainLoop *main_loop;
	/** ID of idle source for dispatching the session stop notification. */
	unsigned int stop_check_id;
	/** Whether the session has been started. */
	gboolean running;
};

SR_PRIV int sr_session_source_add_internal(struct sr_session *session,
		void *key, GSource *source);
SR_PRIV int sr_session_source_remove_internal(struct sr_session *session,
		void *key);
SR_PRIV int sr_session_source_destroyed(struct sr_session *session,
		void *key, GSource *source);
SR_PRIV int sr_session_fd_source_add(struct sr_session *session,
		void *key, gintptr fd, int events, int timeout,
		sr_receive_data_callback cb, void *cb_data);

SR_PRIV int sr_session_source_add(struct sr_session *session, int fd,
		int events, int timeout, sr_receive_data_callback cb, void *cb_data);
SR_PRIV int sr_session_source_add_pollfd(struct sr_session *session,
		GPollFD *pollfd, int timeout, sr_receive_data_callback cb,
		void *cb_data);
SR_PRIV int sr_session_source_add_channel(struct sr_session *session,
		GIOChannel *channel, int events, int timeout,
		sr_receive_data_callback cb, void *cb_data);
SR_PRIV int sr_session_source_remove(struct sr_session *session, int fd);
SR_PRIV int sr_session_source_remove_pollfd(struct sr_session *session,
		GPollFD *pollfd);
SR_PRIV int sr_session_source_remove_channel(struct sr_session *session,
		GIOChannel *channel);

SR_PRIV int sr_session_send_meta(const struct sr_dev_inst *sdi,
		uint32_t key, GVariant *var);
SR_PRIV int sr_session_send(const struct sr_dev_inst *sdi,
		const struct sr_datafeed_packet *packet);
SR_PRIV int sr_sessionfile_check(const char *filename);
SR_PRIV struct sr_dev_inst *sr_session_prepare_sdi(const char *filename,
		struct sr_session **session);

/*--- session_file.c --------------------------------------------------------*/

#if !HAVE_ZIP_DISCARD
/* Replace zip_discard() if not available. */
#define zip_discard(zip) sr_zip_discard(zip)
SR_PRIV void sr_zip_discard(struct zip *archive);
#endif

SR_PRIV GKeyFile *sr_sessionfile_read_metadata(struct zip *archive,
			const struct zip_stat *entry);

/*--- analog.c --------------------------------------------------------------*/

SR_PRIV int sr_analog_init(struct sr_datafeed_analog *analog,
                           struct sr_analog_encoding *encoding,
                           struct sr_analog_meaning *meaning,
                           struct sr_analog_spec *spec,
                           int digits);

/*--- std.c -----------------------------------------------------------------*/

typedef int (*dev_close_callback)(struct sr_dev_inst *sdi);
typedef void (*std_dev_clear_callback)(void *priv);

SR_PRIV int std_init(struct sr_dev_driver *di, struct sr_context *sr_ctx);
SR_PRIV int std_cleanup(const struct sr_dev_driver *di);
SR_PRIV int std_dummy_dev_open(struct sr_dev_inst *sdi);
SR_PRIV int std_dummy_dev_close(struct sr_dev_inst *sdi);
SR_PRIV int std_dummy_dev_acquisition_start(const struct sr_dev_inst *sdi);
SR_PRIV int std_dummy_dev_acquisition_stop(struct sr_dev_inst *sdi);
#ifdef HAVE_SERIAL_COMM
SR_PRIV int std_serial_dev_open(struct sr_dev_inst *sdi);
SR_PRIV int std_serial_dev_acquisition_stop(struct sr_dev_inst *sdi);
#endif
SR_PRIV int std_session_send_df_header(const struct sr_dev_inst *sdi);
SR_PRIV int std_session_send_df_end(const struct sr_dev_inst *sdi);
SR_PRIV int std_session_send_df_trigger(const struct sr_dev_inst *sdi);
SR_PRIV int std_session_send_df_frame_begin(const struct sr_dev_inst *sdi);
SR_PRIV int std_session_send_df_frame_end(const struct sr_dev_inst *sdi);
SR_PRIV int std_dev_clear_with_callback(const struct sr_dev_driver *driver,
		std_dev_clear_callback clear_private);
SR_PRIV int std_dev_clear(const struct sr_dev_driver *driver);
SR_PRIV GSList *std_dev_list(const struct sr_dev_driver *di);
SR_PRIV int std_serial_dev_close(struct sr_dev_inst *sdi);
SR_PRIV GSList *std_scan_complete(struct sr_dev_driver *di, GSList *devices);

SR_PRIV int std_opts_config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg,
	const uint32_t scanopts[], size_t scansize, const uint32_t drvopts[],
	size_t drvsize, const uint32_t devopts[], size_t devsize);

extern SR_PRIV const uint32_t NO_OPTS[1];

#define STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts) \
	std_opts_config_list(key, data, sdi, cg, ARRAY_AND_SIZE(scanopts), \
		ARRAY_AND_SIZE(drvopts), ARRAY_AND_SIZE(devopts))

SR_PRIV GVariant *std_gvar_tuple_array(const uint64_t a[][2], unsigned int n);
SR_PRIV GVariant *std_gvar_tuple_rational(const struct sr_rational *r, unsigned int n);
SR_PRIV GVariant *std_gvar_samplerates(const uint64_t samplerates[], unsigned int n);
SR_PRIV GVariant *std_gvar_samplerates_steps(const uint64_t samplerates[], unsigned int n);
SR_PRIV GVariant *std_gvar_min_max_step(double min, double max, double step);
SR_PRIV GVariant *std_gvar_min_max_step_array(const double a[3]);
SR_PRIV GVariant *std_gvar_min_max_step_thresholds(const double dmin, const double dmax, const double dstep);

SR_PRIV GVariant *std_gvar_tuple_u64(uint64_t low, uint64_t high);
SR_PRIV GVariant *std_gvar_tuple_double(double low, double high);

SR_PRIV GVariant *std_gvar_array_i32(const int32_t a[], unsigned int n);
SR_PRIV GVariant *std_gvar_array_u32(const uint32_t a[], unsigned int n);
SR_PRIV GVariant *std_gvar_array_u64(const uint64_t a[], unsigned int n);
SR_PRIV GVariant *std_gvar_array_str(const char *a[], unsigned int n);

SR_PRIV GVariant *std_gvar_thresholds(const double a[][2], unsigned int n);

SR_PRIV int std_str_idx(GVariant *data, const char *a[], unsigned int n);
SR_PRIV int std_u64_idx(GVariant *data, const uint64_t a[], unsigned int n);
SR_PRIV int std_u8_idx(GVariant *data, const uint8_t a[], unsigned int n);

SR_PRIV int std_str_idx_s(const char *s, const char *a[], unsigned int n);
SR_PRIV int std_u8_idx_s(uint8_t b, const uint8_t a[], unsigned int n);

SR_PRIV int std_u64_tuple_idx(GVariant *data, const uint64_t a[][2], unsigned int n);
SR_PRIV int std_double_tuple_idx(GVariant *data, const double a[][2], unsigned int n);
SR_PRIV int std_double_tuple_idx_d0(const double d, const double a[][2], unsigned int n);

SR_PRIV int std_cg_idx(const struct sr_channel_group *cg, struct sr_channel_group *a[], unsigned int n);

SR_PRIV int std_dummy_set_params(struct sr_serial_dev_inst *serial,
	int baudrate, int bits, int parity, int stopbits,
	int flowcontrol, int rts, int dtr);
SR_PRIV int std_dummy_set_handshake(struct sr_serial_dev_inst *serial,
	int rts, int dtr);

/*--- resource.c ------------------------------------------------------------*/

SR_PRIV int64_t sr_file_get_size(FILE *file);

SR_PRIV int sr_resource_open(struct sr_context *ctx,
		struct sr_resource *res, int type, const char *name)
		G_GNUC_WARN_UNUSED_RESULT;
SR_PRIV int sr_resource_close(struct sr_context *ctx,
		struct sr_resource *res);
SR_PRIV gssize sr_resource_read(struct sr_context *ctx,
		const struct sr_resource *res, void *buf, size_t count)
		G_GNUC_WARN_UNUSED_RESULT;
SR_PRIV void *sr_resource_load(struct sr_context *ctx, int type,
		const char *name, size_t *size, size_t max_size)
		G_GNUC_MALLOC G_GNUC_WARN_UNUSED_RESULT;

/*--- strutil.c -------------------------------------------------------------*/

SR_PRIV int sr_atol(const char *str, long *ret);
SR_PRIV int sr_atol_base(const char *str, long *ret, char **end, int base);
SR_PRIV int sr_atoul_base(const char *str, unsigned long *ret, char **end, int base);
SR_PRIV int sr_atoi(const char *str, int *ret);
SR_PRIV int sr_atod(const char *str, double *ret);
SR_PRIV int sr_atof(const char *str, float *ret);
SR_PRIV int sr_atod_ascii(const char *str, double *ret);
SR_PRIV int sr_atod_ascii_digits(const char *str, double *ret, int *digits);
SR_PRIV int sr_atof_ascii(const char *str, float *ret);

SR_PRIV GString *sr_hexdump_new(const uint8_t *data, const size_t len);
SR_PRIV void sr_hexdump_free(GString *s);

/*--- soft-trigger.c --------------------------------------------------------*/

struct soft_trigger_logic {
	const struct sr_dev_inst *sdi;
	const struct sr_trigger *trigger;
	int count;
	int unitsize;
	int cur_stage;
	uint8_t *prev_sample;
	uint8_t *pre_trigger_buffer;
	uint8_t *pre_trigger_head;
	int pre_trigger_size;
	int pre_trigger_fill;
};

SR_PRIV int logic_channel_unitsize(GSList *channels);
SR_PRIV struct soft_trigger_logic *soft_trigger_logic_new(
		const struct sr_dev_inst *sdi, struct sr_trigger *trigger,
		int pre_trigger_samples);
SR_PRIV void soft_trigger_logic_free(struct soft_trigger_logic *st);
SR_PRIV int soft_trigger_logic_check(struct soft_trigger_logic *st, uint8_t *buf,
		int len, int *pre_trigger_samples);

/*--- serial.c --------------------------------------------------------------*/

#ifdef HAVE_SERIAL_COMM
enum {
	SERIAL_RDWR = 1,
	SERIAL_RDONLY = 2,
};

typedef gboolean (*packet_valid_callback)(const uint8_t *buf);
typedef int (*packet_valid_len_callback)(void *st,
	const uint8_t *p, size_t l, size_t *pl);

typedef GSList *(*sr_ser_list_append_t)(GSList *devs, const char *name,
		const char *desc);
typedef GSList *(*sr_ser_find_append_t)(GSList *devs, const char *name);

SR_PRIV int serial_open(struct sr_serial_dev_inst *serial, int flags);
SR_PRIV int serial_close(struct sr_serial_dev_inst *serial);
SR_PRIV int serial_flush(struct sr_serial_dev_inst *serial);
SR_PRIV int serial_drain(struct sr_serial_dev_inst *serial);
SR_PRIV size_t serial_has_receive_data(struct sr_serial_dev_inst *serial);
SR_PRIV int serial_write_blocking(struct sr_serial_dev_inst *serial,
		const void *buf, size_t count, unsigned int timeout_ms);
SR_PRIV int serial_write_nonblocking(struct sr_serial_dev_inst *serial,
		const void *buf, size_t count);
SR_PRIV int serial_read_blocking(struct sr_serial_dev_inst *serial, void *buf,
		size_t count, unsigned int timeout_ms);
SR_PRIV int serial_read_nonblocking(struct sr_serial_dev_inst *serial, void *buf,
		size_t count);
SR_PRIV int serial_set_read_chunk_cb(struct sr_serial_dev_inst *serial,
		serial_rx_chunk_callback cb, void *cb_data);
SR_PRIV int serial_set_params(struct sr_serial_dev_inst *serial, int baudrate,
		int bits, int parity, int stopbits, int flowcontrol, int rts, int dtr);
SR_PRIV int serial_set_handshake(struct sr_serial_dev_inst *serial,
		int rts, int dtr);
SR_PRIV int serial_set_paramstr(struct sr_serial_dev_inst *serial,
		const char *paramstr);
SR_PRIV int serial_readline(struct sr_serial_dev_inst *serial, char **buf,
		int *buflen, gint64 timeout_ms);
SR_PRIV int serial_stream_detect(struct sr_serial_dev_inst *serial,
		uint8_t *buf, size_t *buflen,
		size_t packet_size, packet_valid_callback is_valid,
		packet_valid_len_callback is_valid_len, size_t *return_size,
		uint64_t timeout_ms);
SR_PRIV int sr_serial_extract_options(GSList *options, const char **serial_device,
				      const char **serial_options);
SR_PRIV int serial_source_add(struct sr_session *session,
		struct sr_serial_dev_inst *serial, int events, int timeout,
		sr_receive_data_callback cb, void *cb_data);
SR_PRIV int serial_source_remove(struct sr_session *session,
		struct sr_serial_dev_inst *serial);
SR_PRIV GSList *sr_serial_find_usb(uint16_t vendor_id, uint16_t product_id);
SR_PRIV int serial_timeout(struct sr_serial_dev_inst *port, int num_bytes);

SR_PRIV void sr_ser_discard_queued_data(struct sr_serial_dev_inst *serial);
SR_PRIV size_t sr_ser_has_queued_data(struct sr_serial_dev_inst *serial);
SR_PRIV void sr_ser_queue_rx_data(struct sr_serial_dev_inst *serial,
		const uint8_t *data, size_t len);
SR_PRIV size_t sr_ser_unqueue_rx_data(struct sr_serial_dev_inst *serial,
		uint8_t *data, size_t len);

struct ser_lib_functions {
	int (*open)(struct sr_serial_dev_inst *serial, int flags);
	int (*close)(struct sr_serial_dev_inst *serial);
	int (*flush)(struct sr_serial_dev_inst *serial);
	int (*drain)(struct sr_serial_dev_inst *serial);
	int (*write)(struct sr_serial_dev_inst *serial,
			const void *buf, size_t count,
			int nonblocking, unsigned int timeout_ms);
	int (*read)(struct sr_serial_dev_inst *serial,
			void *buf, size_t count,
			int nonblocking, unsigned int timeout_ms);
	int (*set_params)(struct sr_serial_dev_inst *serial,
			int baudrate, int bits, int parity, int stopbits,
			int flowcontrol, int rts, int dtr);
	int (*set_handshake)(struct sr_serial_dev_inst *serial,
			int rts, int dtr);
	int (*setup_source_add)(struct sr_session *session,
			struct sr_serial_dev_inst *serial,
			int events, int timeout,
			sr_receive_data_callback cb, void *cb_data);
	int (*setup_source_remove)(struct sr_session *session,
			struct sr_serial_dev_inst *serial);
	GSList *(*list)(GSList *list, sr_ser_list_append_t append);
	GSList *(*find_usb)(GSList *list, sr_ser_find_append_t append,
			uint16_t vendor_id, uint16_t product_id);
	int (*get_frame_format)(struct sr_serial_dev_inst *serial,
			int *baud, int *bits);
	size_t (*get_rx_avail)(struct sr_serial_dev_inst *serial);
};
extern SR_PRIV struct ser_lib_functions *ser_lib_funcs_libsp;
SR_PRIV int ser_name_is_hid(struct sr_serial_dev_inst *serial);
extern SR_PRIV struct ser_lib_functions *ser_lib_funcs_hid;
SR_PRIV int ser_name_is_bt(struct sr_serial_dev_inst *serial);
extern SR_PRIV struct ser_lib_functions *ser_lib_funcs_bt;

#ifdef HAVE_LIBHIDAPI
struct vid_pid_item {
	uint16_t vid, pid;
};

struct ser_hid_chip_functions {
	const char *chipname;
	const char *chipdesc;
	const struct vid_pid_item *vid_pid_items;
	const int max_bytes_per_request;
	int (*set_params)(struct sr_serial_dev_inst *serial,
			int baudrate, int bits, int parity, int stopbits,
			int flowcontrol, int rts, int dtr);
	int (*read_bytes)(struct sr_serial_dev_inst *serial,
			uint8_t *data, int space, unsigned int timeout);
	int (*write_bytes)(struct sr_serial_dev_inst *serial,
			const uint8_t *data, int space);
	int (*flush)(struct sr_serial_dev_inst *serial);
	int (*drain)(struct sr_serial_dev_inst *serial);
};
extern SR_PRIV struct ser_hid_chip_functions *ser_hid_chip_funcs_bu86x;
extern SR_PRIV struct ser_hid_chip_functions *ser_hid_chip_funcs_ch9325;
extern SR_PRIV struct ser_hid_chip_functions *ser_hid_chip_funcs_cp2110;
extern SR_PRIV struct ser_hid_chip_functions *ser_hid_chip_funcs_victor;
SR_PRIV const char *ser_hid_chip_find_name_vid_pid(uint16_t vid, uint16_t pid);
#endif
#endif

/*--- bt/ API ---------------------------------------------------------------*/

#ifdef HAVE_BLUETOOTH
SR_PRIV const char *sr_bt_adapter_get_address(size_t idx);

struct sr_bt_desc;
typedef void (*sr_bt_scan_cb)(void *cb_data, const char *addr, const char *name);
typedef int (*sr_bt_data_cb)(void *cb_data, uint8_t *data, size_t dlen);

SR_PRIV struct sr_bt_desc *sr_bt_desc_new(void);
SR_PRIV void sr_bt_desc_free(struct sr_bt_desc *desc);

SR_PRIV int sr_bt_config_cb_scan(struct sr_bt_desc *desc,
	sr_bt_scan_cb cb, void *cb_data);
SR_PRIV int sr_bt_config_cb_data(struct sr_bt_desc *desc,
	sr_bt_data_cb cb, void *cb_data);
SR_PRIV int sr_bt_config_addr_local(struct sr_bt_desc *desc, const char *addr);
SR_PRIV int sr_bt_config_addr_remote(struct sr_bt_desc *desc, const char *addr);
SR_PRIV int sr_bt_config_rfcomm(struct sr_bt_desc *desc, size_t channel);
SR_PRIV int sr_bt_config_notify(struct sr_bt_desc *desc,
	uint16_t read_handle, uint16_t write_handle,
	uint16_t cccd_handle, uint16_t cccd_value);

SR_PRIV int sr_bt_scan_le(struct sr_bt_desc *desc, int duration);
SR_PRIV int sr_bt_scan_bt(struct sr_bt_desc *desc, int duration);

SR_PRIV int sr_bt_connect_ble(struct sr_bt_desc *desc);
SR_PRIV int sr_bt_connect_rfcomm(struct sr_bt_desc *desc);
SR_PRIV void sr_bt_disconnect(struct sr_bt_desc *desc);

SR_PRIV ssize_t sr_bt_read(struct sr_bt_desc *desc,
	void *data, size_t len);
SR_PRIV ssize_t sr_bt_write(struct sr_bt_desc *desc,
	const void *data, size_t len);

SR_PRIV int sr_bt_start_notify(struct sr_bt_desc *desc);
SR_PRIV int sr_bt_check_notify(struct sr_bt_desc *desc);
#endif

/*--- ezusb.c ---------------------------------------------------------------*/

#ifdef HAVE_LIBUSB_1_0
SR_PRIV int ezusb_reset(struct libusb_device_handle *hdl, int set_clear);
SR_PRIV int ezusb_install_firmware(struct sr_context *ctx, libusb_device_handle *hdl,
				   const char *name);
SR_PRIV int ezusb_upload_firmware(struct sr_context *ctx, libusb_device *dev,
				  int configuration, const char *name);
#endif

/*--- usb.c -----------------------------------------------------------------*/

#ifdef HAVE_LIBUSB_1_0
SR_PRIV GSList *sr_usb_find(libusb_context *usb_ctx, const char *conn);
SR_PRIV int sr_usb_open(libusb_context *usb_ctx, struct sr_usb_dev_inst *usb);
SR_PRIV void sr_usb_close(struct sr_usb_dev_inst *usb);
SR_PRIV int usb_source_add(struct sr_session *session, struct sr_context *ctx,
		int timeout, sr_receive_data_callback cb, void *cb_data);
SR_PRIV int usb_source_remove(struct sr_session *session, struct sr_context *ctx);
SR_PRIV int usb_get_port_path(libusb_device *dev, char *path, int path_len);
SR_PRIV gboolean usb_match_manuf_prod(libusb_device *dev,
		const char *manufacturer, const char *product);
#endif

/*--- binary_helpers.c ------------------------------------------------------*/

/** Binary value type */
enum binary_value_type {
	BVT_UINT8 = 0,
	BVT_BE_UINT8 = BVT_UINT8,
	BVT_LE_UINT8 = BVT_UINT8,

	BVT_BE_UINT16,
	BVT_BE_UINT32,
	BVT_BE_UINT64,
	BVT_BE_FLOAT,

	BVT_LE_UINT16,
	BVT_LE_UINT32,
	BVT_LE_UINT64,
	BVT_LE_FLOAT,
};

/** Binary value specification */
struct binary_value_spec {
	/** Offset into binary blob */
	size_t offset;
	/** Data type to decode */
	enum binary_value_type type;
	/** Scale factor to get native units */
	float scale;
};

/** Binary channel definition */
struct binary_analog_channel {
	/** Channel name */
	const char *name;
	/** Binary value in data stream */
	struct binary_value_spec spec;
	/** Significant digits */
	int digits;
	/** Measured quantity */
	enum sr_mq mq;
	/** Measured unit */
	enum sr_unit unit;
};

/**
 * Read extract a value from a binary blob.
 *
 * @param out Pointer to output buffer.
 * @param spec Binary value specification
 * @param data Pointer to binary blob
 * @param length Size of binary blob
 * @return SR_OK on success, SR_ERR_* error code on failure.
 */
SR_PRIV int bv_get_value(float *out, const struct binary_value_spec *spec, const void *data, size_t length);

/**
 * Send an analog channel packet based on a binary analog channel
 * specification.
 *
 * @param sdi Device instance
 * @param ch Sigrok channel
 * @param spec Channel specification
 * @param data Pointer to binary blob
 * @param length Size of binary blob
 * @return SR_OK on success, SR_ERR_* error code on failure.
 */
SR_PRIV int bv_send_analog_channel(const struct sr_dev_inst *sdi, struct sr_channel *ch,
				   const struct binary_analog_channel *spec, const void *data, size_t length);

/*--- crc.c -----------------------------------------------------------------*/

#define SR_CRC16_DEFAULT_INIT 0xffffU

/**
 * Calculate a CRC16 checksum using the 0x8005 polynomial.
 *
 * This CRC16 flavor is also known as CRC16-ANSI or CRC16-MODBUS.
 *
 * @param crc Initial value (typically 0xffff)
 * @param buffer Input buffer
 * @param len Buffer length
 * @return Checksum
 */
SR_PRIV uint16_t sr_crc16(uint16_t crc, const uint8_t *buffer, int len);

/*--- modbus/modbus.c -------------------------------------------------------*/

struct sr_modbus_dev_inst {
	const char *name;
	const char *prefix;
	int priv_size;
	GSList *(*scan)(int modbusaddr);
	int (*dev_inst_new)(void *priv, const char *resource,
		char **params, const char *serialcomm, int modbusaddr);
	int (*open)(void *priv);
	int (*source_add)(struct sr_session *session, void *priv, int events,
		int timeout, sr_receive_data_callback cb, void *cb_data);
	int (*source_remove)(struct sr_session *session, void *priv);
	int (*send)(void *priv, const uint8_t *buffer, int buffer_size);
	int (*read_begin)(void *priv, uint8_t *function_code);
	int (*read_data)(void *priv, uint8_t *buf, int maxlen);
	int (*read_end)(void *priv);
	int (*close)(void *priv);
	void (*free)(void *priv);
	unsigned int read_timeout_ms;
	void *priv;
};

SR_PRIV GSList *sr_modbus_scan(struct drv_context *drvc, GSList *options,
		struct sr_dev_inst *(*probe_device)(struct sr_modbus_dev_inst *modbus));
SR_PRIV struct sr_modbus_dev_inst *modbus_dev_inst_new(const char *resource,
		const char *serialcomm, int modbusaddr);
SR_PRIV int sr_modbus_open(struct sr_modbus_dev_inst *modbus);
SR_PRIV int sr_modbus_source_add(struct sr_session *session,
		struct sr_modbus_dev_inst *modbus, int events, int timeout,
		sr_receive_data_callback cb, void *cb_data);
SR_PRIV int sr_modbus_source_remove(struct sr_session *session,
		struct sr_modbus_dev_inst *modbus);
SR_PRIV int sr_modbus_request(struct sr_modbus_dev_inst *modbus,
                              uint8_t *request, int request_size);
SR_PRIV int sr_modbus_reply(struct sr_modbus_dev_inst *modbus,
                            uint8_t *reply, int reply_size);
SR_PRIV int sr_modbus_request_reply(struct sr_modbus_dev_inst *modbus,
                                    uint8_t *request, int request_size,
                                    uint8_t *reply, int reply_size);
SR_PRIV int sr_modbus_read_coils(struct sr_modbus_dev_inst *modbus,
                                 int address, int nb_coils, uint8_t *coils);
SR_PRIV int sr_modbus_read_holding_registers(struct sr_modbus_dev_inst *modbus,
                                             int address, int nb_registers,
                                             uint16_t *registers);
SR_PRIV int sr_modbus_write_coil(struct sr_modbus_dev_inst *modbus,
                                 int address, int value);
SR_PRIV int sr_modbus_write_multiple_registers(struct sr_modbus_dev_inst*modbus,
                                               int address, int nb_registers,
                                               uint16_t *registers);
SR_PRIV int sr_modbus_close(struct sr_modbus_dev_inst *modbus);
SR_PRIV void sr_modbus_free(struct sr_modbus_dev_inst *modbus);

/*--- dmm/es519xx.c ---------------------------------------------------------*/

/**
 * All 11-byte es519xx chips repeat each block twice for each conversion cycle
 * so always read 2 blocks at a time.
 */
#define ES519XX_11B_PACKET_SIZE (11 * 2)
#define ES519XX_14B_PACKET_SIZE 14

struct es519xx_info {
	gboolean is_judge, is_voltage, is_auto, is_micro, is_current;
	gboolean is_milli, is_resistance, is_continuity, is_diode;
	gboolean is_frequency, is_rpm, is_capacitance, is_duty_cycle;
	gboolean is_temperature, is_celsius, is_fahrenheit;
	gboolean is_adp0, is_adp1, is_adp2, is_adp3;
	gboolean is_sign, is_batt, is_ol, is_pmax, is_pmin, is_apo;
	gboolean is_dc, is_ac, is_vahz, is_min, is_max, is_rel, is_hold;
	gboolean is_digit4, is_ul, is_vasel, is_vbar, is_lpf1, is_lpf0, is_rmr;
	uint32_t baudrate;
	int packet_size;
	gboolean alt_functions, fivedigits, clampmeter, selectable_lpf;
	int digits;
};

SR_PRIV gboolean sr_es519xx_2400_11b_packet_valid(const uint8_t *buf);
SR_PRIV int sr_es519xx_2400_11b_parse(const uint8_t *buf, float *floatval,
		struct sr_datafeed_analog *analog, void *info);
SR_PRIV gboolean sr_es519xx_2400_11b_altfn_packet_valid(const uint8_t *buf);
SR_PRIV int sr_es519xx_2400_11b_altfn_parse(const uint8_t *buf,
		float *floatval, struct sr_datafeed_analog *analog, void *info);
SR_PRIV gboolean sr_es519xx_19200_11b_5digits_packet_valid(const uint8_t *buf);
SR_PRIV int sr_es519xx_19200_11b_5digits_parse(const uint8_t *buf,
		float *floatval, struct sr_datafeed_analog *analog, void *info);
SR_PRIV gboolean sr_es519xx_19200_11b_clamp_packet_valid(const uint8_t *buf);
SR_PRIV int sr_es519xx_19200_11b_clamp_parse(const uint8_t *buf,
		float *floatval, struct sr_datafeed_analog *analog, void *info);
SR_PRIV gboolean sr_es519xx_19200_11b_packet_valid(const uint8_t *buf);
SR_PRIV int sr_es519xx_19200_11b_parse(const uint8_t *buf, float *floatval,
		struct sr_datafeed_analog *analog, void *info);
SR_PRIV gboolean sr_es519xx_19200_14b_packet_valid(const uint8_t *buf);
SR_PRIV int sr_es519xx_19200_14b_parse(const uint8_t *buf, float *floatval,
		struct sr_datafeed_analog *analog, void *info);
SR_PRIV gboolean sr_es519xx_19200_14b_sel_lpf_packet_valid(const uint8_t *buf);
SR_PRIV int sr_es519xx_19200_14b_sel_lpf_parse(const uint8_t *buf,
		float *floatval, struct sr_datafeed_analog *analog, void *info);

/*--- dmm/fs9922.c ----------------------------------------------------------*/

#define FS9922_PACKET_SIZE 14

struct fs9922_info {
	gboolean is_auto, is_dc, is_ac, is_rel, is_hold, is_bpn, is_z1, is_z2;
	gboolean is_max, is_min, is_apo, is_bat, is_nano, is_z3, is_micro;
	gboolean is_milli, is_kilo, is_mega, is_beep, is_diode, is_percent;
	gboolean is_z4, is_volt, is_ampere, is_ohm, is_hfe, is_hertz, is_farad;
	gboolean is_celsius, is_fahrenheit;
	int bargraph_sign, bargraph_value;
};

SR_PRIV gboolean sr_fs9922_packet_valid(const uint8_t *buf);
SR_PRIV int sr_fs9922_parse(const uint8_t *buf, float *floatval,
			    struct sr_datafeed_analog *analog, void *info);
SR_PRIV void sr_fs9922_z1_diode(struct sr_datafeed_analog *analog, void *info);

/*--- dmm/fs9721.c ----------------------------------------------------------*/

#define FS9721_PACKET_SIZE 14

struct fs9721_info {
	gboolean is_ac, is_dc, is_auto, is_rs232, is_micro, is_nano, is_kilo;
	gboolean is_diode, is_milli, is_percent, is_mega, is_beep, is_farad;
	gboolean is_ohm, is_rel, is_hold, is_ampere, is_volt, is_hz, is_bat;
	gboolean is_c2c1_11, is_c2c1_10, is_c2c1_01, is_c2c1_00, is_sign;
};

SR_PRIV gboolean sr_fs9721_packet_valid(const uint8_t *buf);
SR_PRIV int sr_fs9721_parse(const uint8_t *buf, float *floatval,
			    struct sr_datafeed_analog *analog, void *info);
SR_PRIV void sr_fs9721_00_temp_c(struct sr_datafeed_analog *analog, void *info);
SR_PRIV void sr_fs9721_01_temp_c(struct sr_datafeed_analog *analog, void *info);
SR_PRIV void sr_fs9721_10_temp_c(struct sr_datafeed_analog *analog, void *info);
SR_PRIV void sr_fs9721_01_10_temp_f_c(struct sr_datafeed_analog *analog, void *info);
SR_PRIV void sr_fs9721_max_c_min(struct sr_datafeed_analog *analog, void *info);

/*--- dmm/ms2115b.c ---------------------------------------------------------*/

#define MS2115B_PACKET_SIZE 9

enum ms2115b_display {
	MS2115B_DISPLAY_MAIN,
	MS2115B_DISPLAY_SUB,
	MS2115B_DISPLAY_COUNT,
};

struct ms2115b_info {
	/* Selected channel. */
	size_t ch_idx;
	gboolean is_ac, is_dc, is_auto;
	gboolean is_diode, is_beep, is_farad;
	gboolean is_ohm, is_ampere, is_volt, is_hz;
	gboolean is_duty_cycle, is_percent;
};

extern SR_PRIV const char *ms2115b_channel_formats[];
SR_PRIV gboolean sr_ms2115b_packet_valid(const uint8_t *buf);
SR_PRIV int sr_ms2115b_parse(const uint8_t *buf, float *floatval,
	struct sr_datafeed_analog *analog, void *info);

/*--- dmm/ms8250d.c ---------------------------------------------------------*/

#define MS8250D_PACKET_SIZE 18

struct ms8250d_info {
	gboolean is_ac, is_dc, is_auto, is_rs232, is_micro, is_nano, is_kilo;
	gboolean is_diode, is_milli, is_percent, is_mega, is_beep, is_farad;
	gboolean is_ohm, is_rel, is_hold, is_ampere, is_volt, is_hz, is_bat;
	gboolean is_ncv, is_min, is_max, is_sign, is_autotimer;
};

SR_PRIV gboolean sr_ms8250d_packet_valid(const uint8_t *buf);
SR_PRIV int sr_ms8250d_parse(const uint8_t *buf, float *floatval,
			     struct sr_datafeed_analog *analog, void *info);

/*--- dmm/dtm0660.c ---------------------------------------------------------*/

#define DTM0660_PACKET_SIZE 15

struct dtm0660_info {
	gboolean is_ac, is_dc, is_auto, is_rs232, is_micro, is_nano, is_kilo;
	gboolean is_diode, is_milli, is_percent, is_mega, is_beep, is_farad;
	gboolean is_ohm, is_rel, is_hold, is_ampere, is_volt, is_hz, is_bat;
	gboolean is_degf, is_degc, is_c2c1_01, is_c2c1_00, is_apo, is_min;
	gboolean is_minmax, is_max, is_sign;
};

SR_PRIV gboolean sr_dtm0660_packet_valid(const uint8_t *buf);
SR_PRIV int sr_dtm0660_parse(const uint8_t *buf, float *floatval,
			struct sr_datafeed_analog *analog, void *info);

/*--- dmm/m2110.c -----------------------------------------------------------*/

#define BBCGM_M2110_PACKET_SIZE 9

/* Dummy info struct. The parser does not use it. */
struct m2110_info { int dummy; };

SR_PRIV gboolean sr_m2110_packet_valid(const uint8_t *buf);
SR_PRIV int sr_m2110_parse(const uint8_t *buf, float *floatval,
			     struct sr_datafeed_analog *analog, void *info);

/*--- dmm/metex14.c ---------------------------------------------------------*/

#define METEX14_PACKET_SIZE 14

struct metex14_info {
	size_t ch_idx;
	gboolean is_ac, is_dc, is_resistance, is_capacity, is_temperature;
	gboolean is_diode, is_frequency, is_ampere, is_volt, is_farad;
	gboolean is_hertz, is_ohm, is_celsius, is_fahrenheit, is_watt;
	gboolean is_pico, is_nano, is_micro, is_milli, is_kilo, is_mega;
	gboolean is_gain, is_decibel, is_power, is_decibel_mw, is_power_factor;
	gboolean is_hfe, is_unitless, is_logic, is_min, is_max, is_avg;
};

#ifdef HAVE_SERIAL_COMM
SR_PRIV int sr_metex14_packet_request(struct sr_serial_dev_inst *serial);
#endif
SR_PRIV gboolean sr_metex14_packet_valid(const uint8_t *buf);
SR_PRIV int sr_metex14_parse(const uint8_t *buf, float *floatval,
			     struct sr_datafeed_analog *analog, void *info);
SR_PRIV gboolean sr_metex14_4packets_valid(const uint8_t *buf);
SR_PRIV int sr_metex14_4packets_parse(const uint8_t *buf, float *floatval,
			     struct sr_datafeed_analog *analog, void *info);

/*--- dmm/rs9lcd.c ----------------------------------------------------------*/

#define RS9LCD_PACKET_SIZE 9

/* Dummy info struct. The parser does not use it. */
struct rs9lcd_info { int dummy; };

SR_PRIV gboolean sr_rs9lcd_packet_valid(const uint8_t *buf);
SR_PRIV int sr_rs9lcd_parse(const uint8_t *buf, float *floatval,
			    struct sr_datafeed_analog *analog, void *info);

/*--- dmm/bm25x.c -----------------------------------------------------------*/

#define BRYMEN_BM25X_PACKET_SIZE 15

/* Dummy info struct. The parser does not use it. */
struct bm25x_info { int dummy; };

SR_PRIV gboolean sr_brymen_bm25x_packet_valid(const uint8_t *buf);
SR_PRIV int sr_brymen_bm25x_parse(const uint8_t *buf, float *floatval,
			     struct sr_datafeed_analog *analog, void *info);

/*--- dmm/bm52x.c -----------------------------------------------------------*/

#define BRYMEN_BM52X_PACKET_SIZE 24
#define BRYMEN_BM52X_DISPLAY_COUNT 2

struct brymen_bm52x_info { size_t ch_idx; };

#ifdef HAVE_SERIAL_COMM
SR_PRIV int sr_brymen_bm52x_packet_request(struct sr_serial_dev_inst *serial);
SR_PRIV int sr_brymen_bm82x_packet_request(struct sr_serial_dev_inst *serial);
#endif
SR_PRIV gboolean sr_brymen_bm52x_packet_valid(const uint8_t *buf);
SR_PRIV gboolean sr_brymen_bm82x_packet_valid(const uint8_t *buf);
/* BM520s and BM820s protocols are similar, the parse routine is shared. */
SR_PRIV int sr_brymen_bm52x_parse(const uint8_t *buf, float *floatval,
		struct sr_datafeed_analog *analog, void *info);

struct brymen_bm52x_state;

SR_PRIV void *brymen_bm52x_state_init(void);
SR_PRIV void brymen_bm52x_state_free(void *state);
SR_PRIV int brymen_bm52x_config_get(void *state, uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg);
SR_PRIV int brymen_bm52x_config_set(void *state, uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg);
SR_PRIV int brymen_bm52x_config_list(void *state, uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg);
SR_PRIV int brymen_bm52x_acquire_start(void *state,
	const struct sr_dev_inst *sdi,
	sr_receive_data_callback *cb, void **cb_data);

/*--- dmm/bm85x.c -----------------------------------------------------------*/

#define BRYMEN_BM85x_PACKET_SIZE_MIN 4

struct brymen_bm85x_info { int dummy; };

#ifdef HAVE_SERIAL_COMM
SR_PRIV int brymen_bm85x_after_open(struct sr_serial_dev_inst *serial);
SR_PRIV int brymen_bm85x_packet_request(struct sr_serial_dev_inst *serial);
#endif
SR_PRIV gboolean brymen_bm85x_packet_valid(void *state,
	const uint8_t *buf, size_t len, size_t *pkt_len);
SR_PRIV int brymen_bm85x_parse(void *state, const uint8_t *buf, size_t len,
	double *floatval, struct sr_datafeed_analog *analog, void *info);

/*--- dmm/bm86x.c -----------------------------------------------------------*/

#define BRYMEN_BM86X_PACKET_SIZE 24
#define BRYMEN_BM86X_DISPLAY_COUNT 2

struct brymen_bm86x_info { size_t ch_idx; };

#ifdef HAVE_SERIAL_COMM
SR_PRIV int sr_brymen_bm86x_packet_request(struct sr_serial_dev_inst *serial);
#endif
SR_PRIV gboolean sr_brymen_bm86x_packet_valid(const uint8_t *buf);
SR_PRIV int sr_brymen_bm86x_parse(const uint8_t *buf, float *floatval,
		struct sr_datafeed_analog *analog, void *info);

/*--- dmm/ut71x.c -----------------------------------------------------------*/

#define UT71X_PACKET_SIZE 11

struct ut71x_info {
	gboolean is_voltage, is_resistance, is_capacitance, is_temperature;
	gboolean is_celsius, is_fahrenheit, is_current, is_continuity;
	gboolean is_diode, is_frequency, is_duty_cycle, is_dc, is_ac;
	gboolean is_auto, is_manual, is_sign, is_power, is_loop_current;
};

SR_PRIV gboolean sr_ut71x_packet_valid(const uint8_t *buf);
SR_PRIV int sr_ut71x_parse(const uint8_t *buf, float *floatval,
		struct sr_datafeed_analog *analog, void *info);

/*--- dmm/vc870.c -----------------------------------------------------------*/

#define VC870_PACKET_SIZE 23

struct vc870_info {
	gboolean is_voltage, is_dc, is_ac, is_temperature, is_resistance;
	gboolean is_continuity, is_capacitance, is_diode, is_loop_current;
	gboolean is_current, is_micro, is_milli, is_power;
	gboolean is_power_factor_freq, is_power_apparent_power, is_v_a_rms_value;
	gboolean is_sign2, is_sign1, is_batt, is_ol1, is_max, is_min;
	gboolean is_maxmin, is_rel, is_ol2, is_open, is_manu, is_hold;
	gboolean is_light, is_usb, is_warning, is_auto_power, is_misplug_warn;
	gboolean is_lo, is_hi, is_open2;

	gboolean is_frequency, is_dual_display, is_auto;
};

SR_PRIV gboolean sr_vc870_packet_valid(const uint8_t *buf);
SR_PRIV int sr_vc870_parse(const uint8_t *buf, float *floatval,
		struct sr_datafeed_analog *analog, void *info);

/*--- dmm/vc96.c ------------------------------------------------------------*/

#define VC96_PACKET_SIZE 13

struct vc96_info {
	size_t ch_idx;
	gboolean is_ac, is_dc, is_resistance, is_diode, is_ampere, is_volt;
	gboolean is_ohm, is_micro, is_milli, is_kilo, is_mega, is_hfe;
	gboolean is_unitless;
};

SR_PRIV gboolean sr_vc96_packet_valid(const uint8_t *buf);
SR_PRIV int sr_vc96_parse(const uint8_t *buf, float *floatval,
		struct sr_datafeed_analog *analog, void *info);

/*--- lcr/es51919.c ---------------------------------------------------------*/

/* Acquisition details which apply to all supported serial-lcr devices. */
struct lcr_parse_info {
	size_t ch_idx;
	uint64_t output_freq;
	const char *circuit_model;
};

#define ES51919_PACKET_SIZE	17
#define ES51919_CHANNEL_COUNT	2
#define ES51919_COMM_PARAM	"9600/8n1/rts=1/dtr=1"

SR_PRIV int es51919_config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg);
SR_PRIV int es51919_config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg);
SR_PRIV int es51919_config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg);
SR_PRIV gboolean es51919_packet_valid(const uint8_t *pkt);
SR_PRIV int es51919_packet_parse(const uint8_t *pkt, float *floatval,
	struct sr_datafeed_analog *analog, void *info);

/*--- lcr/vc4080.c ----------------------------------------------------------*/

/* Note: Also uses 'struct lcr_parse_info' from es51919 above. */

#define VC4080_PACKET_SIZE	39
#define VC4080_COMM_PARAM	"1200/8n1"
#define VC4080_WITH_DQ_CHANS	0 /* Enable separate D/Q channels? */

enum vc4080_display {
	VC4080_DISPLAY_PRIMARY,
	VC4080_DISPLAY_SECONDARY,
#if VC4080_WITH_DQ_CHANS
	VC4080_DISPLAY_D_VALUE,
	VC4080_DISPLAY_Q_VALUE,
#endif
	VC4080_CHANNEL_COUNT,
};

extern SR_PRIV const char *vc4080_channel_formats[VC4080_CHANNEL_COUNT];

SR_PRIV int vc4080_config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg);
SR_PRIV int vc4080_packet_request(struct sr_serial_dev_inst *serial);
SR_PRIV gboolean vc4080_packet_valid(const uint8_t *pkt);
SR_PRIV int vc4080_packet_parse(const uint8_t *pkt, float *floatval,
	struct sr_datafeed_analog *analog, void *info);

/*--- dmm/ut372.c -----------------------------------------------------------*/

#define UT372_PACKET_SIZE 27

struct ut372_info {
	int dummy;
};

SR_PRIV gboolean sr_ut372_packet_valid(const uint8_t *buf);
SR_PRIV int sr_ut372_parse(const uint8_t *buf, float *floatval,
		struct sr_datafeed_analog *analog, void *info);

/*--- dmm/asycii.c ----------------------------------------------------------*/

#define ASYCII_PACKET_SIZE 16

struct asycii_info {
	gboolean is_ac, is_dc, is_ac_and_dc;
	gboolean is_resistance, is_capacitance, is_diode, is_gain;
	gboolean is_frequency, is_duty_cycle, is_duty_pos, is_duty_neg;
	gboolean is_pulse_width, is_period_pos, is_period_neg;
	gboolean is_pulse_count, is_count_pos, is_count_neg;
	gboolean is_ampere, is_volt, is_volt_ampere, is_farad, is_ohm;
	gboolean is_hertz, is_percent, is_seconds, is_decibel;
	gboolean is_pico, is_nano, is_micro, is_milli, is_kilo, is_mega;
	gboolean is_unitless;
	gboolean is_peak_min, is_peak_max;
	gboolean is_invalid;
};

#ifdef HAVE_SERIAL_COMM
SR_PRIV int sr_asycii_packet_request(struct sr_serial_dev_inst *serial);
#endif
SR_PRIV gboolean sr_asycii_packet_valid(const uint8_t *buf);
SR_PRIV int sr_asycii_parse(const uint8_t *buf, float *floatval,
			    struct sr_datafeed_analog *analog, void *info);

/*--- dmm/eev121gw.c --------------------------------------------------------*/

#define EEV121GW_PACKET_SIZE 19

enum eev121gw_display {
	EEV121GW_DISPLAY_MAIN,
	EEV121GW_DISPLAY_SUB,
	EEV121GW_DISPLAY_BAR,
	EEV121GW_DISPLAY_COUNT,
};

struct eev121gw_info {
	/* Selected channel. */
	size_t ch_idx;
	/*
	 * Measured value, number and sign/overflow flags, scale factor
	 * and significant digits.
	 */
	uint32_t uint_value;
	gboolean is_ofl, is_neg;
	int factor, digits;
	/* Currently active mode (meter's function). */
	gboolean is_ac, is_dc, is_voltage, is_current, is_power, is_gain;
	gboolean is_resistance, is_capacitance, is_diode, is_temperature;
	gboolean is_continuity, is_frequency, is_period, is_duty_cycle;
	/* Quantities associated with mode/function. */
	gboolean is_ampere, is_volt, is_volt_ampere, is_dbm;
	gboolean is_ohm, is_farad, is_celsius, is_fahrenheit;
	gboolean is_hertz, is_seconds, is_percent, is_loop_current;
	gboolean is_unitless, is_logic;
	/* Other indicators. */
	gboolean is_min, is_max, is_avg, is_1ms_peak, is_rel, is_hold;
	gboolean is_low_pass, is_mem, is_bt, is_auto_range, is_test;
	gboolean is_auto_poweroff, is_low_batt;
};

extern SR_PRIV const char *eev121gw_channel_formats[];
SR_PRIV gboolean sr_eev121gw_packet_valid(const uint8_t *buf);
SR_PRIV int sr_eev121gw_3displays_parse(const uint8_t *buf, float *floatval,
		struct sr_datafeed_analog *analog, void *info);

/*--- scale/kern.c ----------------------------------------------------------*/

struct kern_info {
	gboolean is_gram, is_carat, is_ounce, is_pound, is_troy_ounce;
	gboolean is_pennyweight, is_grain, is_tael, is_momme, is_tola;
	gboolean is_percentage, is_piece, is_unstable, is_stable, is_error;
	int buflen;
};

SR_PRIV gboolean sr_kern_packet_valid(const uint8_t *buf);
SR_PRIV int sr_kern_parse(const uint8_t *buf, float *floatval,
		struct sr_datafeed_analog *analog, void *info);

/*--- sw_limits.c -----------------------------------------------------------*/

struct sr_sw_limits {
	uint64_t limit_samples;
	uint64_t limit_frames;
	uint64_t limit_msec;
	uint64_t samples_read;
	uint64_t frames_read;
	uint64_t start_time;
};

SR_PRIV int sr_sw_limits_config_get(const struct sr_sw_limits *limits, uint32_t key,
	GVariant **data);
SR_PRIV int sr_sw_limits_config_set(struct sr_sw_limits *limits, uint32_t key,
	GVariant *data);
SR_PRIV void sr_sw_limits_acquisition_start(struct sr_sw_limits *limits);
SR_PRIV gboolean sr_sw_limits_check(struct sr_sw_limits *limits);
SR_PRIV void sr_sw_limits_update_samples_read(struct sr_sw_limits *limits,
	uint64_t samples_read);
SR_PRIV void sr_sw_limits_update_frames_read(struct sr_sw_limits *limits,
	uint64_t frames_read);
SR_PRIV void sr_sw_limits_init(struct sr_sw_limits *limits);

/*--- feed_queue.h ----------------------------------------------------------*/

struct feed_queue_logic;
struct feed_queue_analog;

SR_API struct feed_queue_logic *feed_queue_logic_alloc(
	struct sr_dev_inst *sdi,
	size_t sample_count, size_t unit_size);
SR_API int feed_queue_logic_submit(struct feed_queue_logic *q,
	const uint8_t *data, size_t count);
SR_API int feed_queue_logic_flush(struct feed_queue_logic *q);
SR_API void feed_queue_logic_free(struct feed_queue_logic *q);

SR_API struct feed_queue_analog *feed_queue_analog_alloc(
	struct sr_dev_inst *sdi,
	size_t sample_count, int digits, struct sr_channel *ch);
SR_API int feed_queue_analog_submit(struct feed_queue_analog *q,
	float data, size_t count);
SR_API int feed_queue_analog_flush(struct feed_queue_analog *q);
SR_API void feed_queue_analog_free(struct feed_queue_analog *q);

#endif
