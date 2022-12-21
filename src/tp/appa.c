/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2020 Martin Eitzenberger <x@cymaphore.net>
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

/**
 * @file
 * @version 1
 *
 * APPA Transport Protocol handler
 *
 * Most of the devices produced by APPA use the same transport protocol. These
 * packages are exchanged over EA232, EA485, Serial/USB, BLE and possibly other
 * types of connection.
 *
 * All traffic is initiated by the master, every (valid) packet is causing the
 * client device to respond with exactly one response packet. The command of
 * the response packet can be different than the request packet.
 *
 * Available commands and the layout of the payload entirely depend on the
 * device in question.
 *
 * APPA-Packet:
 *
 * [SS SS CC LL DD DD ... CS]
 *
 * SS: Start-byte (0x55)
 * CC: Command code, depends on the device
 * LL: Abount of data bytes contained in packet (max. 64)
 * DD: Data
 * CS: Checksum (sum of all bytes except for the checksum itself)
 *
 * Naming:
 *
 * Header: Start-byte (SS), Command (CC) and length (LL)
 * Payload: Header and Data (DD)
 * Packet: Payload and Checksum (CS)
 *
 * Examples:
 *
 * @code
 *
 * // Include the appa transport protocol to your project
 * #include "tp/appa.h"
 *
 * // create instance object
 * sr_tp_appa_inst tpai;
 *
 * // request packet
 * sr_tp_appa_packet request;
 *
 * // response packet
 * sr_tp_appa_packet response;
 *
 * // return codes are standard sigrok codes
 * int retr;
 *
 * // initialize APPA transport, provide serial port to use
 * if ((retr = sr_tp_appa_init(&tpai, serial)) < SR_OK)
 *	return retr;
 *
 * // Fill in request data to packet
 * request.command = 0x01;
 * request.length = 0;
 *
 * // Send a request and wait for response
 * if ((retr = sr_tp_appa_send_receive(&tpai, &request, &response)) < SR_OK)
 *	return retr;
 *
 * // check if a response was received
 * if (retr > FALSE)
 *	sr_err("Response command was received, command: %d, first byte: %d",
 *		response.command, response.data[0]);
 * else
 *	sr_err("No response received!");
 *
 * @endcode
 *
 */

#include <config.h>
#include "tp/appa.h"

#ifdef HAVE_SERIAL_COMM

#define LOG_PREFIX "tp-appa"

#define SR_TP_APPA_START_WORD 0x5555
#define SR_TP_APPA_START_BYTE 0x55
#define SR_TP_APPA_HEADER_SIZE 4
#define SR_TP_APPA_RECEIVE_TIMEOUT 500
#define SR_TP_APPA_PACKET_TIMING 50

#define SR_TP_APPA_MAX_PAYLOAD_SIZE \
	(SR_TP_APPA_MAX_DATA_SIZE + SR_TP_APPA_HEADER_SIZE)

static uint8_t sr_tp_appa_checksum(const uint8_t *arg_data, uint8_t arg_size,
	uint8_t *arg_out_checksum);
static int sr_tp_appa_buffer_reset(struct sr_tp_appa_inst *arg_tpai);

/**
 * Initialize APPA transport protocol
 *
 * @param arg_tpai Instance object
 * @param arg_serial Serial port for communication, must be ready
 * @retval SR_OK on success
 * @retval SR_ERR_... on error
 */
SR_PRIV int sr_tp_appa_init(struct sr_tp_appa_inst *arg_tpai,
	struct sr_serial_dev_inst *arg_serial)
{
	if (arg_tpai == NULL
		|| arg_serial == NULL)
		return SR_ERR_BUG;

	if (sr_tp_appa_buffer_reset(arg_tpai) < SR_OK)
		return SR_ERR_BUG;

	arg_tpai->serial = arg_serial;

	return SR_OK;
}

/**
 * Terminate APPA transport protocol
 *
 * @param arg_tpai Instance object
 * @retval SR_OK on success
 * @retval SR_ERR_... on error
 */
SR_PRIV int sr_tp_appa_term(struct sr_tp_appa_inst *arg_tpai)
{
	if (arg_tpai == NULL)
		return SR_ERR_BUG;

	return SR_OK;
}

/**
 * Send packet, non-blocking
 *
 * Write out package over serial connection and return immediatly
 *
 * Important: Write should be performed in a single operation, to avoid troubles
 * with some APPA BLE implementations (any additional write puts additional
 * delay to the communication).
 *
 * @param arg_tpai Instance object
 * @param arg_s_packet
 * @retval TRUE on successfull handover to serial send
 * @retval SR_ERR_... on error
 */
SR_PRIV int sr_tp_appa_send(struct sr_tp_appa_inst *arg_tpai,
	const struct sr_tp_appa_packet *arg_s_packet, gboolean arg_is_blocking)
{
	int retr;
	int dcount;
	uint8_t buf[SR_TP_APPA_MAX_PACKET_SIZE];
	uint8_t *wrptr;
	uint8_t checksum;

	if (arg_tpai == NULL
		|| arg_s_packet == NULL)
		return SR_ERR_BUG;

	if (arg_s_packet->length > SR_TP_APPA_MAX_PAYLOAD_SIZE)
		return SR_ERR_DATA;

	wrptr = &buf[0];

	write_u16le_inc(&wrptr, SR_TP_APPA_START_WORD);
	write_u8_inc(&wrptr, arg_s_packet->command);
	write_u8_inc(&wrptr, arg_s_packet->length);

	for (dcount = 0; dcount < arg_s_packet->length; dcount++)
		write_u8_inc(&wrptr, arg_s_packet->data[dcount]);

	if ((retr = sr_tp_appa_checksum(buf, arg_s_packet->length
		+ SR_TP_APPA_HEADER_SIZE, &checksum)) < SR_OK)
		return retr;
	write_u8_inc(&wrptr, checksum);

	dcount = arg_s_packet->length + SR_TP_APPA_HEADER_SIZE + 1;
	if (arg_is_blocking) {
		if ((retr = serial_write_blocking(arg_tpai->serial, buf,
			dcount, 50)) != dcount)
			return(SR_ERR_IO);
	} else {
		if ((retr = serial_write_nonblocking(arg_tpai->serial, buf,
			dcount)) != dcount)
			return(SR_ERR_IO);
	}

	return TRUE;
}

/**
 * Receive package, non-blocking
 *
 * Read the serial line and try to receive a package. If no (full) package is
 * available, return FALSE, otherwise TRUE.
 *
 * Partial packet data will be retained for the next call, data received after
 * a package will be dumped.
 *
 * @param arg_tpai Instance object
 * @param arg_r_packet Received package
 * @param arg_is_blocking Blocking request
 * @retval TRUE Packages was received and written to arg_r_packet
 * @retval FALSE No (complete) packet was available
 * @retval SR_ERR_... on error
 */
SR_PRIV int sr_tp_appa_receive(struct sr_tp_appa_inst *arg_tpai,
	struct sr_tp_appa_packet *arg_r_packet, gboolean arg_is_blocking)
{
	int len;
	int xloop;
	int retr;
	uint8_t buf[SR_TP_APPA_MAX_PACKET_SIZE * 3];
	uint8_t checksum;

	if (arg_tpai == NULL
		|| arg_r_packet == NULL)
		return SR_ERR_BUG;

	retr = FALSE;

	if (arg_is_blocking)
		len = serial_read_blocking(arg_tpai->serial,
		buf, sizeof(buf), 50);
	else
		len = serial_read_nonblocking(arg_tpai->serial,
		buf, sizeof(buf));

	if (len < SR_OK)
		return len;

	for (xloop = 0; xloop < len; xloop++) {
		/* validate header */
		if (arg_tpai->buffer_size < 1) {
			if (buf[xloop] != SR_TP_APPA_START_BYTE) {
				continue;
			}
		} else if (arg_tpai->buffer_size < 2) {
			if (buf[xloop] != SR_TP_APPA_START_BYTE) {
				sr_tp_appa_buffer_reset(arg_tpai);
				continue;
			}
		} else if (arg_tpai->buffer_size < 4) {
			if (buf[xloop] > SR_TP_APPA_MAX_DATA_SIZE) {
				sr_tp_appa_buffer_reset(arg_tpai);
				continue;
			}
		}

		arg_tpai->buffer[arg_tpai->buffer_size++] = buf[xloop];

		if (arg_tpai->buffer_size > 4) {

			if (arg_tpai->buffer[3]
				+ SR_TP_APPA_HEADER_SIZE
				+ 1
				== arg_tpai->buffer_size) {

				if ((retr = sr_tp_appa_checksum(arg_tpai->buffer,
					arg_tpai->buffer[3]
					+ SR_TP_APPA_HEADER_SIZE, &checksum))
					< SR_OK) {
				} else if (checksum == arg_tpai->buffer[arg_tpai->buffer_size - 1]) {
					arg_r_packet->command = arg_tpai->buffer[2];
					arg_r_packet->length = arg_tpai->buffer[3];

					/* copy payload to packet struct */
					if (memcpy(arg_r_packet->data,
						&arg_tpai->buffer[4],
						arg_tpai->buffer[3])
						!= arg_r_packet->data)
						retr = SR_ERR_BUG;
					else
						retr = TRUE;
				} else {
					retr = SR_ERR_IO;
				}
				break;
			}
		}

		/* catch impossible situations, abort */
		if (arg_tpai->buffer_size > SR_TP_APPA_MAX_PACKET_SIZE) {
			sr_tp_appa_buffer_reset(arg_tpai);
			return SR_ERR_BUG;
		}
	}

	/* dump the rest of the data, if a valid packet was completed */
	if (retr == TRUE)
		sr_tp_appa_buffer_reset(arg_tpai);

	return retr;
}

/**
 * Combined send/receive function, blocking
 *
 * Send out package and wait for response, block until a response is recieved
 * or timeout occurs.
 *
 * @param arg_tpai Instance object
 * @param arg_s_packet Package to transmit
 * @param arg_r_packet Received package
 * @retval TRUE if a package was received
 * @retval FALSE if no response has been received
 * @retval SR_ERR_... on error
 */
SR_PRIV int sr_tp_appa_send_receive(struct sr_tp_appa_inst *arg_tpai,
	const struct sr_tp_appa_packet *arg_s_packet,
	struct sr_tp_appa_packet *arg_r_packet)
{
	int trycount;
	int retr;

	if (arg_tpai == NULL
		|| arg_s_packet == NULL
		|| arg_r_packet == NULL)
		return SR_ERR_BUG;

	if ((retr = sr_tp_appa_send(arg_tpai, arg_s_packet, TRUE)) < TRUE)
		return retr;

	retr = FALSE;

	trycount = SR_TP_APPA_RECEIVE_TIMEOUT / SR_TP_APPA_PACKET_TIMING;
	while (trycount-- > 0) {
		retr = sr_tp_appa_receive(arg_tpai, arg_r_packet, TRUE);

		if (retr < SR_OK
			|| retr == TRUE)
			break;
	};

	return retr;
}

/**
 * Calculate APPA-style checksum
 *
 * Summarize all the data provided and return result.
 *
 * @param arg_data Data to calculate APPA-checksum for
 * @param arg_size Size of data
 * @param arg_out_checksum Checksum result
 * @retval SR_OK if checksum was calculated and stored in arg_out_checksum
 * @retval SR_ERR_... on error
 */
static uint8_t sr_tp_appa_checksum(const uint8_t *arg_data, uint8_t arg_size,
	uint8_t *arg_out_checksum)
{
	if (arg_data == NULL
		|| arg_out_checksum == NULL)
		return SR_ERR_BUG;

	*arg_out_checksum = 0;
	while (arg_size-- > 0)
		*arg_out_checksum += arg_data[arg_size];

	return SR_OK;
}

/**
 * Reset data buffer
 *
 * Clears first bytes of buffer and resets buffer length in instance object.
 *
 * @param arg_tpai Instance object
 * @retval SR_OK on success
 * @retval SR_ERR_... on error
 */
static int sr_tp_appa_buffer_reset(struct sr_tp_appa_inst *arg_tpai)
{
	if (arg_tpai == NULL)
		return SR_ERR_BUG;

	arg_tpai->buffer_size = 0;
	arg_tpai->buffer[0] = 0;
	arg_tpai->buffer[1] = 0;
	arg_tpai->buffer[2] = 0;
	arg_tpai->buffer[3] = 0;

	return SR_OK;
}

#endif/*HAVE_SERIAL_COMM*/
