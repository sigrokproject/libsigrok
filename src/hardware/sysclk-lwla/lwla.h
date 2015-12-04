/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 Daniel Elstner <daniel.kitta@gmail.com>
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

#ifndef LIBSIGROK_HARDWARE_SYSCLK_LWLA_LWLA_H
#define LIBSIGROK_HARDWARE_SYSCLK_LWLA_LWLA_H

#include <stdint.h>
#include <libusb.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>

struct sr_usb_dev_inst;

/* Rotate argument n bits to the left.
 * This construct is an idiom recognized by GCC as bit rotation.
 */
#define LROTATE(a, n) (((a) << (n)) | ((a) >> (CHAR_BIT * sizeof(a) - (n))))

/* Convert 16-bit little endian LWLA protocol word to machine word order. */
#define LWLA_TO_UINT16(val) GUINT16_FROM_LE(val)

/* Convert 32-bit mixed endian LWLA protocol word to machine word order. */
#define LWLA_TO_UINT32(val) LROTATE(GUINT32_FROM_LE(val), 16)

/* Convert 16-bit argument to LWLA protocol word. */
#define LWLA_WORD(val) GUINT16_TO_LE(val)

/* Extract 16-bit units in mixed endian order from 32/64-bit value. */
#define LWLA_WORD_0(val) GUINT16_TO_LE(((val) >> 16) & 0xFFFF)
#define LWLA_WORD_1(val) GUINT16_TO_LE((val) & 0xFFFF)
#define LWLA_WORD_2(val) GUINT16_TO_LE(((val) >> 48) & 0xFFFF)
#define LWLA_WORD_3(val) GUINT16_TO_LE(((val) >> 32) & 0xFFFF)

/* Maximum number of 16-bit words sent at a time during acquisition.
 * Used for allocating the libusb transfer buffer. Keep this even so that
 * subsequent members are always 32-bit aligned.
 */
#define MAX_ACQ_SEND_LEN16	64 /* 43 for capture setup plus stuffing */

/* Maximum number of 32-bit words received at a time during acquisition.
 * This is a multiple of the endpoint buffer size to avoid transfer overflow
 * conditions.
 */
#define MAX_ACQ_RECV_LEN32	(2 * 512 / 4)

/* Maximum length of a register read/write sequence.
 */
#define MAX_REG_SEQ_LEN		16

/* Logic datafeed packet size in bytes.
 * This is a multiple of both 4 and 5 to match any model's unit size
 * and memory granularity.
 */
#define PACKET_SIZE		(5000 * 4 * 5)

/** LWLA protocol command ID codes.
 */
enum command_id {
	CMD_READ_REG	= 1,
	CMD_WRITE_REG	= 2,
	CMD_READ_MEM32	= 3,
	CMD_READ_MEM36	= 6,
	CMD_WRITE_LREGS	= 7,
	CMD_READ_LREGS	= 8,
};

/** LWLA capture state flags.
 * The bit positions are the same as in the LWLA1016 control register.
 */
enum status_flag {
	STATUS_CAPTURING = 1 << 2,
	STATUS_TRIGGERED = 1 << 5,
	STATUS_MEM_AVAIL = 1 << 6,
};

/** LWLA1034 run-length encoding states.
 */
enum rle_state {
	RLE_STATE_DATA,
	RLE_STATE_LEN
};

/** Register address/value pair.
 */
struct regval {
	unsigned int reg;
	uint32_t val;
};

/** LWLA sample acquisition and decompression state.
 */
struct acquisition_state {
	uint64_t samples_max;	/* maximum number of samples to process */
	uint64_t samples_done;	/* number of samples sent to the session bus */
	uint64_t duration_max;	/* maximum capture duration in milliseconds */
	uint64_t duration_now;	/* running capture duration since trigger */

	uint64_t sample;	/* last sample read from capture memory */
	uint64_t run_len;	/* remaining run length of current sample */

	struct libusb_transfer *xfer_in;	/* USB in transfer record */
	struct libusb_transfer *xfer_out;	/* USB out transfer record */

	unsigned int mem_addr_fill;	/* capture memory fill level */
	unsigned int mem_addr_done;	/* next address to be processed */
	unsigned int mem_addr_next;	/* start address for next async read */
	unsigned int mem_addr_stop;	/* end of memory range to be read */
	unsigned int in_index;		/* position in read transfer buffer */
	unsigned int out_index;		/* position in logic packet buffer */
	enum rle_state rle;		/* RLE decoding state */

	gboolean rle_enabled;	/* capturing in timing-state mode */
	gboolean clock_boost;	/* switch to faster clock during capture */
	unsigned int status;	/* last received device status */

	unsigned int reg_seq_pos;	/* index of next register/value pair */
	unsigned int reg_seq_len;	/* length of register/value sequence */

	struct regval reg_sequence[MAX_REG_SEQ_LEN];	/* register buffer */
	uint32_t xfer_buf_in[MAX_ACQ_RECV_LEN32];	/* USB in buffer */
	uint16_t xfer_buf_out[MAX_ACQ_SEND_LEN16];	/* USB out buffer */
	uint8_t out_packet[PACKET_SIZE];		/* logic payload */
};

static inline void lwla_queue_regval(struct acquisition_state *acq,
				     unsigned int reg, uint32_t value)
{
	acq->reg_sequence[acq->reg_seq_len].reg = reg;
	acq->reg_sequence[acq->reg_seq_len].val = value;
	acq->reg_seq_len++;
}

SR_PRIV int lwla_send_bitstream(struct sr_context *ctx,
				const struct sr_usb_dev_inst *usb,
				const char *name);

SR_PRIV int lwla_send_command(const struct sr_usb_dev_inst *usb,
			      const uint16_t *command, int cmd_len);

SR_PRIV int lwla_receive_reply(const struct sr_usb_dev_inst *usb,
			       uint32_t *reply, int reply_len, int expect_len);

SR_PRIV int lwla_read_reg(const struct sr_usb_dev_inst *usb,
			  uint16_t reg, uint32_t *value);

SR_PRIV int lwla_write_reg(const struct sr_usb_dev_inst *usb,
			   uint16_t reg, uint32_t value);

SR_PRIV int lwla_write_regs(const struct sr_usb_dev_inst *usb,
			    const struct regval *regvals, int count);

#endif
