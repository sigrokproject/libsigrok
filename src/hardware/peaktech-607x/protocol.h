// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2022 Enrico Scholz <enrico.scholz@ensc.de>
 */

#ifndef LIBSIGROK_HARDWARE_PEAKTECH_6070X_PROTOCOL_H
#define LIBSIGROK_HARDWARE_PEAKTECH_6070X_PROTOCOL_H

#include <config.h>

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#include <glib-2.0/glib.h>

#include "endian.h"

#define PEAKTECH_MAX_CHAN	(2u)

#define LOG_PREFIX		"peaktech-607x"
#define SERIALCOMM		"9600/8n1"

#define _pt_packed		__attribute__((__packed__))

struct pt_proto_hdr {
	/* Always 0xf7 */
	uint8_t				magic;
	/* PeakTech calls this field "address code" in their documentation.
	 * It seems to be:
	 * 0x01 ... for 6070
	 * 0x02 ... for 6075 */
	uint8_t				addr_code;
	/* 0x03 ... inquiry,
	 * 0x0a ... setup */
	uint8_t				func_code;
	/* 0x04 ... inquiry
	 * 0x09 ... ch1 voltage target
	 * 0x0a ... ch1 current limit
	 * 0x0b ... ch2 voltage target
	 * 0x0c ... ch2 current limit
	 * 0x1e ... output switch
	 * 0x1f ... parallel/series */
	uint8_t				addr;
	/* PeakTech calls this field "address length"; it seems to be
	 * 0x01 ... for setup requests
	 * 0x03 ... for inquiry requests + responses on 6070
	 * 0x09 ... for inquiry requests + responses on 6075 */
	uint8_t				addr_len;
} _pt_packed;

struct pt_proto_tail {
	/* CRC based on a 0xA001 polonym and 0xffff IV; it is encoded as
	 * little endian.  It goes over the complete header (inclusive magic)
	 * and data, but does not include the tail. */
	le16_t				crc;
	/* always 0xfd */
	uint8_t				magic;
} _pt_packed;

struct pt_proto_inquire_req {
	struct pt_proto_hdr		hdr;
	struct pt_proto_tail		tail;
} _pt_packed;

struct pt_6070_proto_inquire_reply {
	struct pt_proto_hdr		hdr;
	/* bit 0 ... cv
	 * bit 1 ... cc
	 * bit 2 ... ser
	 * bit 3 ... par
	 * bit 5 ... output
	 * bit 6 ... ??? (not in doc; seems to reflect bit 5) */
	uint8_t				ch1_status;
	uint8_t				_rsrvd;
	be16_t				ch1_volt;
	be16_t				ch1_curr;
	be16_t				ch1_volt_set;
	be16_t				ch1_curr_set;
	struct pt_proto_tail		tail;
} _pt_packed;

struct pt_6075_proto_inquire_reply {
	struct pt_proto_hdr		hdr;
	uint8_t				ch1_status;
	uint8_t				ch2_status;
	be16_t				ch1_volt;
	be16_t				ch1_curr;
	be16_t				ch2_volt;
	be16_t				ch2_curr;
	be16_t				ch1_volt_set;
	be16_t				ch1_curr_set;
	be16_t				ch2_volt_set;
	be16_t				ch2_curr_set;
	struct pt_proto_tail		tail;
} _pt_packed;

struct pt_proto_setup_req {
	struct pt_proto_hdr		hdr;
	be16_t				value;
	struct pt_proto_tail		tail;
} _pt_packed;

union pt_proto_inquire_reply {
	struct pt_6070_proto_inquire_reply	p6070;
	struct pt_6075_proto_inquire_reply	p6075;
};

union pt_proto_generic_req {
	struct pt_proto_inquire_req		inquiry;
	struct pt_proto_setup_req		setup;
	unsigned char				raw[1];
};

union pt_proto_generic_reply {
	struct pt_6070_proto_inquire_reply	unquiry_p6070;
	struct pt_6075_proto_inquire_reply	unquiry_p6075;
	struct pt_proto_setup_req		setup_confirm;
	unsigned char				raw[1];
};

enum peaktech_model {
	PEAKTECH_MODEL_6070,
	PEAKTECH_MODEL_6075,
};

/**
 *  Channel mode (only for 6075).
 *
 *  NOTE: it is used directly for addressing an CHANNEL_MODES[] array.  This
 *  works only efficently because the values in the protocol are having small
 *  values.
 */
enum peaktech_chan_mode {
	PEAKTECH_CHAN_MODE_INDEPEDENT =	0x00,
	PEAKTECH_CHAN_MODE_SERIES =	0x01,
	PEAKTECH_CHAN_MODE_PARALLEL =	0x02,
};

/**
 *  Calculates a crc.
 */
uint16_t peaktech_607x_proto_crc_get(void const *data, size_t cnt) G_GNUC_CONST;

/**
 *  Checks whether the crc is valid.
 *
 *  data must contain a struct pt_proto_hdr and struct pt_proto_tail.
 */
bool peaktech_607x_proto_crc_check(void const *, size_t cnt) G_GNUC_CONST;

#define pt_proto_crc_set(_a) do {					\
		/* for type safety... */				\
		struct pt_proto_hdr const	*_h = &(_a)->hdr;	\
		struct pt_proto_tail const	*_t = &(_a)->tail;	\
		uint16_t			_crc;			\
									\
		(void)_h;						\
									\
		_crc = peaktech_607x_proto_crc_get((_a), sizeof *(_a) - sizeof *_t); \
									\
		(_a)->tail.crc = cpu_to_le16(_crc);			\
	} while (0)

#define PT_PROTO_HDR(_model, _f_c, _a, _a_l)				\
	((struct pt_proto_hdr) {					\
		.magic		= 0xf7,					\
		.addr_code	= (_model) == PEAKTECH_MODEL_6070 ? 0x01 : 0x02, \
		.func_code	= (_f_c),				\
		.addr		= (_a),					\
		.addr_len	= (_a_l),				\
	})								\

#define PT_PROTO_TAIL					\
	((struct pt_proto_tail) {			\
		.magic		= 0xfd,			\
	})

inline static struct pt_proto_inquire_req
pt_proto_inquire_req(enum peaktech_model model)
{
	struct pt_proto_inquire_req	req = {
		.hdr	= PT_PROTO_HDR(model, 0x03, 0x04,
				       (model) == PEAKTECH_MODEL_6070 ? 0x05 : 0x09),
		.tail	= PT_PROTO_TAIL,
	};

	pt_proto_crc_set(&req);

	g_assert(model == PEAKTECH_MODEL_6070 ||
		 model == PEAKTECH_MODEL_6075);

	return req;
}

inline static struct pt_proto_setup_req
_pt_proto_setup_req(enum peaktech_model model, uint8_t addr, uint16_t v)
{
	struct pt_proto_setup_req	req = {
		.hdr	= PT_PROTO_HDR(model, 0x0a, addr, 0x01),
		.value	= cpu_to_be16(v),
		.tail	= PT_PROTO_TAIL,
	};

	pt_proto_crc_set(&req);

	g_assert(model == PEAKTECH_MODEL_6070 ||
		 model == PEAKTECH_MODEL_6075);

	return req;
}

inline static struct pt_proto_setup_req
pt_proto_volt_set_req(enum peaktech_model model, unsigned int ch,
		      unsigned int volt)
{
	g_assert(ch == 0 ||
		 (model == PEAKTECH_MODEL_6075 && ch == 1));

	return _pt_proto_setup_req(model, (ch) == 0 ? 0x09 : 0x0b,
				   volt);
}

inline static struct pt_proto_setup_req
pt_proto_curr_set_req(enum peaktech_model model, unsigned int ch,
		      unsigned int curr)
{
	g_assert(ch == 0 ||
		 (model == PEAKTECH_MODEL_6075 && ch == 1));

	return _pt_proto_setup_req(model, (ch) == 0 ? 0x0a : 0x0c, curr);
}

inline static struct pt_proto_setup_req
pt_proto_output_en_req(enum peaktech_model model, bool ena)
{
	return _pt_proto_setup_req(model, 0x1e, ena ? 1 : 0);
}

inline static struct pt_proto_setup_req
pt_proto_chan_mode_req(enum peaktech_model model,
		       enum peaktech_chan_mode mode)
{
	/* only supported on 6075 */
	g_assert(model == PEAKTECH_MODEL_6075);
	g_assert(mode == PEAKTECH_CHAN_MODE_INDEPEDENT ||
		 mode == PEAKTECH_CHAN_MODE_SERIES ||
		 mode == PEAKTECH_CHAN_MODE_PARALLEL);

	return _pt_proto_setup_req(model, 0x1f, mode);
}

#endif	/* LIBSIGROK_HARDWARE_PEAKTECH_6070X_PROTOCOL_H */
