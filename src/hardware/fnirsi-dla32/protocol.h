/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Copyright (C) 2026 whatshu <shussm@qq.com>
 *
 * FNIRSI DLA-32 experimental driver. Protocol reference:
 * https://github.com/TechBirdCompany/PXView-DLA32-overlay (GPL-3.0).
 * Command framing and CRC checked against factory DLA-Logic 1.0.3.
 */

#ifndef LIBSIGROK_HARDWARE_FNIRSI_DLA32_PROTOCOL_H
#define LIBSIGROK_HARDWARE_FNIRSI_DLA32_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#if defined(__SSE2__) && !defined(DLA_DISABLE_SIMD)
#include <emmintrin.h>
#endif

#define DLA_COMMAND_SIZE 2048
#define DLA_CHANNELS 32

/* Factory DLA-Logic 1.0.3: reflected CRC32, initial register 0, final NOT.
 * Covers the command body only (excludes 0x0a/0x0b and USB padding).
 */
static inline uint32_t dla_crc(const uint8_t *data, size_t len)
{
	uint32_t crc = 0;
	unsigned int bit;

	while (len--) {
		crc ^= *data++;
		for (bit = 0; bit < 8; bit++)
			crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320U : 0);
	}
	return ~crc;
}

static inline void dla_put32(uint8_t *out, uint32_t value)
{
	out[0] = value;
	out[1] = value >> 8;
	out[2] = value >> 16;
	out[3] = value >> 24;
}

static inline void dla_put16(uint8_t *out, uint16_t value)
{
	out[0] = value;
	out[1] = value >> 8;
}

static inline void dla_put64(uint8_t *out, uint64_t value)
{
	dla_put32(out, value);
	dla_put32(out + 4, value >> 32);
}

static inline void dla_packet(uint8_t *out, const uint8_t *body, size_t len)
{
	memset(out, 0, DLA_COMMAND_SIZE);
	out[8] = 0x0a;
	memcpy(out + 9, body, len);
	out[9 + len] = 0x0b;
	dla_put32(out + 10 + len, dla_crc(body, len));
}

/* Factory setup encoder 0x140018a30. These fields reproduce the host protocol;
 * successful packet construction does not establish a tested hardware mode.
 */
struct dla_setup_config {
	uint32_t samplerate, trigger_position;
	uint64_t samples;
	uint16_t threshold, duration_ms, interval_ms;
	uint8_t samplerate_index, threshold_range, duration_index;
	uint8_t buffer, mode, rle;
};

static inline void dla_setup_configure(uint8_t *out,
	const struct dla_setup_config *cfg)
{
	uint8_t body[31] = {0x11, 0x1e, 0x00};

	body[3] = cfg->rle;
	body[4] = cfg->buffer;
	body[5] = cfg->mode;
	dla_put16(body + 6, cfg->interval_ms);
	dla_put32(body + 8, cfg->samplerate);
	body[12] = cfg->samplerate_index;
	dla_put16(body + 13, cfg->threshold);
	body[15] = cfg->threshold_range;
	dla_put16(body + 16, cfg->duration_ms);
	body[18] = cfg->duration_index;
	dla_put32(body + 19, cfg->trigger_position);
	dla_put64(body + 23, cfg->samples);
	dla_packet(out, body, sizeof(body));
}

/* A single comparator threshold, in signed hundredths of a volt. The pair
 * represents the same switching level, not configurable hysteresis. */
static inline int dla_threshold_from_volts(double low, double high, int16_t *value)
{
	double scaled, rounded;

	if (!value || !isfinite(low) || !isfinite(high) || low != high ||
			low < -5.0 || low > 5.0)
		return -1;
	scaled = low * 100.0;
	rounded = round(scaled);
	if (fabs(scaled - rounded) > 0.000001)
		return -1;
	*value = (int16_t)rounded;
	return 0;
}

static inline uint8_t dla_threshold_preset(int16_t value)
{
	switch (value) {
	case 250: return 1;
	case 160: return 2;
	case 150: return 3;
	case 120: return 4;
	case 90: return 5;
	case 70: return 6;
	case 60: return 7;
	default: return 0; /* Custom, including negative values. */
	}
}

/* DLA-32 physical PWM0/PWM1/PWM2/PWM3. 0/100% command constant LOW/HIGH.
 * Intermediate pulse widths have device timing quantization.
 * Disabled electrical state (driven low or high impedance) needs measurement.
 */
static inline int dla_pwm(uint8_t *out, unsigned int channel,
	uint32_t frequency, unsigned int duty, int enabled)
{
	uint8_t body[12] = {0x17, 0x0b, 0x00};

	if (channel >= 4 || (enabled &&
		(!frequency || frequency > 20000000 || duty > 100)))
		return -1;
	body[3] = 0x10 + channel;
	if (enabled) {
		dla_put32(body + 4, frequency);
		dla_put32(body + 8, duty);
		dla_packet(out, body, sizeof(body));
	} else {
		body[1] = 3;
		dla_packet(out, body, 4);
	}
	return 0;
}

/* Factory channel encoder 0x140014e70: 32-bit physical enable mask, followed
 * by 32 trigger conditions: none/rising/high/falling/low/either = 0..5.
 */
static inline int dla_channels_config(uint8_t *out, uint32_t mask,
	const uint8_t triggers[DLA_CHANNELS], int instantly)
{
	uint8_t body[40] = {0x12, 0x27, 0x00};
	unsigned int ch;

	if (!mask || (instantly != 0 && instantly != 1))
		return -1;
	body[3] = instantly;
	dla_put32(body + 4, mask);
	if (triggers) {
		for (ch = 0; ch < DLA_CHANNELS; ch++) {
			if (triggers[ch] > 5)
				return -1;
			body[8 + ch] = triggers[ch];
		}
	}
	dla_packet(out, body, sizeof(body));
	return 0;
}

/* 50 MHz / 1, 2, 5 or 10 ms / buffer mode, captured by the DLA-32 reference.
 * Threshold is the comparator voltage in centivolts, not the logic rail.
 */
static inline void dla_setup(uint8_t *out, uint16_t threshold, uint8_t range,
	uint32_t requested_samples)
{
	uint8_t body[] = {
		0x11, 0x1e, 0x00, 0x00, 0x01, 0x00, 0x03, 0x00,
		0x80, 0xf0, 0xfa, 0x02, 0x08, 0xa0, 0x00, 0x02,
		0x01, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00,
		0x50, 0xc3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	};
	const uint32_t limits[] = {50000, 100000, 250000, 500000};
	const uint8_t duration_ms[] = {1, 2, 5, 10};
	unsigned int profile = 0;

	while (profile < 3 && requested_samples > limits[profile])
		profile++;
	body[13] = threshold;
	body[14] = threshold >> 8;
	body[15] = range;
	body[16] = duration_ms[profile];
	body[18] = profile;
	dla_put32(body + 23, limits[profile]);
	dla_packet(out, body, sizeof(body));
}

static inline void dla_channels(uint8_t *out)
{
	uint8_t body[40] = { 0x12, 0x27, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff };

	dla_packet(out, body, sizeof(body));
}

static inline void dla_stop(uint8_t *out)
{
	const uint8_t body[] = { 0x15, 0x02, 0x00 };

	dla_packet(out, body, sizeof(body));
}

/* The factory rounds active lanes to 1/2/4/8/16/32 in Stream, or 1/8/16/32 in
 * Buffer. Each active lane is one packed byte (eight LSB-first samples).
 * Lookup expansion avoids an inner per-sample loop at 1 GS/s.
 */
struct dla_decoder {
	unsigned int wire_channels, channels, unitsize;
	uint64_t expand[DLA_CHANNELS][256][4];
};

static inline int dla_decoder_init(struct dla_decoder *decoder,
	uint32_t mask, int stream)
{
	unsigned int ch, lane = 0, value, t, bit, highest = 0;

	if (!mask)
		return -1;
	memset(decoder, 0, sizeof(*decoder));
	for (ch = 0; ch < DLA_CHANNELS; ch++) {
		if (mask & (1U << ch)) {
			decoder->channels++;
			highest = ch;
		}
	}
	decoder->unitsize = highest < 8 ? 1 : highest < 16 ? 2 : 4;
	decoder->wire_channels = 1;
	while (decoder->wire_channels < decoder->channels)
		decoder->wire_channels *= 2;
	if (!stream && decoder->wire_channels > 1 && decoder->wire_channels < 8)
		decoder->wire_channels = 8;
	for (ch = 0; ch < DLA_CHANNELS; ch++) {
		if (!(mask & (1U << ch)))
			continue;
		for (value = 0; value < 256; value++) {
			for (t = 0; t < 8; t++) {
				bit = t * decoder->unitsize * 8 + ch;
				if (value & (1U << t))
					decoder->expand[lane][value][bit / 64] |=
						UINT64_C(1) << (bit % 64);
			}
		}
		lane++;
	}
	return 0;
}

static inline void dla_decode_frames(const struct dla_decoder *decoder,
	uint8_t *out, const uint8_t *in, size_t frames)
{
	size_t f;
	unsigned int lane, word;
	uint64_t words[4];

#if defined(__SSE2__) && !defined(DLA_DISABLE_SIMD)
	/* Transpose all 32 lanes into eight samples. SSE2 is baseline on x86-64;
	 * sparse masks and other architectures keep the table decoder below. */
	if (decoder->channels == 32 && decoder->wire_channels == 32 && decoder->unitsize == 4) {
		unsigned int t;
		__m128i low, high;
		for (f = 0; f < frames; f++) {
			low = _mm_loadu_si128((const __m128i *)in);
			high = _mm_loadu_si128((const __m128i *)(in + 16));
			for (t = 8; t-- > 0;) {
				dla_put32(out + t * 4, (uint32_t)_mm_movemask_epi8(low) |
					((uint32_t)_mm_movemask_epi8(high) << 16));
				low = _mm_slli_epi16(low, 1);
				high = _mm_slli_epi16(high, 1);
			}
			in += 32;
			out += 32;
		}
		return;
	}
#endif

	for (f = 0; f < frames; f++) {
		memset(words, 0, sizeof(words));
		for (lane = 0; lane < decoder->channels; lane++)
			for (word = 0; word < decoder->unitsize; word++)
				words[word] |= decoder->expand[lane][in[lane]][word];
		for (word = 0; word < decoder->unitsize; word++)
			dla_put64(out + word * 8, words[word]);
		in += decoder->wire_channels;
		out += decoder->unitsize * 8;
	}
}

/* Preserve incomplete bit-sliced frames across arbitrary USB short reads.
 * Check destination capacity before changing decoder state. */
static inline int dla_decode_feed(const struct dla_decoder *decoder,
	uint8_t tail[DLA_CHANNELS], size_t *pending, uint8_t *out, size_t capacity,
	const uint8_t *in, size_t length, size_t *produced)
{
	size_t frames, needed, bytes;

	*produced = 0;
	if (!decoder->wire_channels || *pending >= decoder->wire_channels ||
		length > SIZE_MAX - *pending)
		return -1;
	frames = (length + *pending) / decoder->wire_channels;
	if (frames > SIZE_MAX / (8 * decoder->unitsize))
		return -1;
	bytes = frames * 8 * decoder->unitsize;
	if (bytes > capacity)
		return -1;
	if (*pending) {
		needed = decoder->wire_channels - *pending;
		if (length < needed) {
			memcpy(tail + *pending, in, length);
			*pending += length;
			return 0;
		}
		memcpy(tail + *pending, in, needed);
		dla_decode_frames(decoder, out, tail, 1);
		out += 8 * decoder->unitsize;
		in += needed;
		length -= needed;
		*pending = 0;
	}
	frames = length / decoder->wire_channels;
	if (frames) {
		dla_decode_frames(decoder, out, in, frames);
		in += frames * decoder->wire_channels;
		length -= frames * decoder->wire_channels;
	}
	if (length) {
		memcpy(tail, in, length);
		*pending = length;
	}
	*produced = bytes;
	return 0;
}
#endif
