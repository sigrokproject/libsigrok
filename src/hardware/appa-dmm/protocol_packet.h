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
 * APPA DMM Packet conversion functions
 *
 */

#ifndef LIBSIGROK_HARDWARE_APPA_DMM_PROTOCOL_PACKET_H
#define LIBSIGROK_HARDWARE_APPA_DMM_PROTOCOL_PACKET_H

#include <config.h>
#include "protocol.h"

#include <math.h>

/* ********************************* */
/* ****** Encoding / decoding ****** */
/* ********************************* */

/**
 * Get frame size of request command
 *
 * @param arg_command Command
 * @return Size in bytes of frame
 */
static int appadmm_get_request_size(enum appadmm_command_e arg_command)
{
	switch (arg_command) {
	case APPADMM_COMMAND_READ_INFORMATION:
		return APPADMM_FRAME_DATA_SIZE_REQUEST_READ_INFORMATION;
	case APPADMM_COMMAND_READ_DISPLAY:
		return APPADMM_FRAME_DATA_SIZE_REQUEST_READ_DISPLAY;
	case APPADMM_COMMAND_READ_PROTOCOL_VERSION:
		return APPADMM_FRAME_DATA_SIZE_REQUEST_READ_PROTOCOL_VERSION;
	case APPADMM_COMMAND_READ_BATTERY_LIFE:
		return APPADMM_FRAME_DATA_SIZE_REQUEST_READ_BATTERY_LIFE;
	case APPADMM_COMMAND_WRITE_UART_CONFIGURATION:
		return APPADMM_FRAME_DATA_SIZE_REQUEST_WRITE_UART_CONFIGURATION;
	case APPADMM_COMMAND_CAL_READING:
		return APPADMM_FRAME_DATA_SIZE_REQUEST_CAL_READING;
	case APPADMM_COMMAND_READ_MEMORY:
		return APPADMM_FRAME_DATA_SIZE_REQUEST_READ_MEMORY;
	case APPADMM_COMMAND_READ_HARMONICS_DATA:
		return APPADMM_FRAME_DATA_SIZE_REQUEST_READ_HARMONICS_DATA;
	case APPADMM_COMMAND_CAL_ENTER:
		return APPADMM_FRAME_DATA_SIZE_REQUEST_CAL_ENTER;
	case APPADMM_COMMAND_CAL_WRITE_FUNCTION_CODE:
		return APPADMM_FRAME_DATA_SIZE_REQUEST_CAL_WRITE_FUNCTION_CODE;
	case APPADMM_COMMAND_CAL_WRITE_RANGE_CODE:
		return APPADMM_FRAME_DATA_SIZE_REQUEST_CAL_WRITE_RANGE_CODE;
	case APPADMM_COMMAND_CAL_WRITE_MEMORY:
		return APPADMM_FRAME_DATA_SIZE_REQUEST_CAL_WRITE_MEMORY;
	case APPADMM_COMMAND_CAL_EXIT:
		return APPADMM_FRAME_DATA_SIZE_REQUEST_CAL_EXIT;
	case APPADMM_COMMAND_OTA_ENTER:
		return APPADMM_FRAME_DATA_SIZE_REQUEST_OTA_ENTER;
	case APPADMM_COMMAND_OTA_SEND_INFORMATION:
		return APPADMM_FRAME_DATA_SIZE_REQUEST_OTA_SEND_INFORMATION;
	case APPADMM_COMMAND_OTA_SEND_FIRMWARE_PACKAGE:
		return APPADMM_FRAME_DATA_SIZE_REQUEST_OTA_SEND_FIRMWARE_PACKAGE;
	case APPADMM_COMMAND_OTA_START_UPGRADE_PROCEDURE:
		return APPADMM_FRAME_DATA_SIZE_REQUEST_OTA_START_UPGRADE_PROCEDURE;

	case APPADMM_COMMAND_FAILURE:
	case APPADMM_COMMAND_SUCCESS:
		/* these are responses only */

	default:
		/* safe default */
		return SR_ERR_DATA;
	}
	return SR_ERR_BUG;
}

/**
 * Get frame size of response command
 *
 * @param arg_command Command
 * @return Size in bytes of frame
 */
static int appadmm_get_response_size(enum appadmm_command_e arg_command)
{
	switch (arg_command) {
	case APPADMM_COMMAND_READ_INFORMATION:
		return APPADMM_FRAME_DATA_SIZE_RESPONSE_READ_INFORMATION;
	case APPADMM_COMMAND_READ_DISPLAY:
		return APPADMM_FRAME_DATA_SIZE_RESPONSE_READ_DISPLAY;
	case APPADMM_COMMAND_READ_PROTOCOL_VERSION:
		return APPADMM_FRAME_DATA_SIZE_RESPONSE_READ_PROTOCOL_VERSION;
	case APPADMM_COMMAND_READ_BATTERY_LIFE:
		return APPADMM_FRAME_DATA_SIZE_RESPONSE_READ_BATTERY_LIFE;
	case APPADMM_COMMAND_CAL_READING:
		return APPADMM_FRAME_DATA_SIZE_RESPONSE_CAL_READING;
	case APPADMM_COMMAND_READ_MEMORY:
		return APPADMM_FRAME_DATA_SIZE_RESPONSE_READ_MEMORY;
	case APPADMM_COMMAND_READ_HARMONICS_DATA:
		return APPADMM_FRAME_DATA_SIZE_RESPONSE_READ_HARMONICS_DATA;
	case APPADMM_COMMAND_FAILURE:
		return APPADMM_FRAME_DATA_SIZE_RESPONSE_FAILURE;
	case APPADMM_COMMAND_SUCCESS:
		return APPADMM_FRAME_DATA_SIZE_RESPONSE_SUCCESS;

	case APPADMM_COMMAND_WRITE_UART_CONFIGURATION:
	case APPADMM_COMMAND_CAL_ENTER:
	case APPADMM_COMMAND_CAL_WRITE_FUNCTION_CODE:
	case APPADMM_COMMAND_CAL_WRITE_RANGE_CODE:
	case APPADMM_COMMAND_CAL_WRITE_MEMORY:
	case APPADMM_COMMAND_CAL_EXIT:
	case APPADMM_COMMAND_OTA_ENTER:
	case APPADMM_COMMAND_OTA_SEND_INFORMATION:
	case APPADMM_COMMAND_OTA_SEND_FIRMWARE_PACKAGE:
	case APPADMM_COMMAND_OTA_START_UPGRADE_PROCEDURE:
		/* these respond with APPADMM_FRAME_DATA_SIZE_RESPONSE_SUCCESS
		 * or APPADMM_FRAME_DATA_SIZE_RESPONSE_FAILURE */

	default:
		/* safe default */
		return SR_ERR_DATA;
	}
	return SR_ERR_BUG;
}

/**
 * Map functioncodes from Legacy 500 to current
 * to allow usage of the same value parser for all devices
 *
 * @param arg_functioncode APPA 500 Legacy function code
 * @param arg_lpf LPF enabled or not
 * @return Modern APPA functioncode
 */
static enum appadmm_functioncode_e appadmm_500_map_functioncode(const uint16_t arg_functioncode,
	gboolean arg_lpf)
{
	switch (arg_functioncode) {
	case APPADMM_500_FUNCTIONCODE_DEGC:
		return APPADMM_FUNCTIONCODE_DEGC;
	case APPADMM_500_FUNCTIONCODE_DEGF:
		return APPADMM_FUNCTIONCODE_DEGF;
	case APPADMM_500_FUNCTIONCODE_AC_V:
		if (arg_lpf)
			return APPADMM_FUNCTIONCODE_LPF_V;
		else
			return APPADMM_FUNCTIONCODE_AC_V;
	case APPADMM_500_FUNCTIONCODE_DC_V:
		return APPADMM_FUNCTIONCODE_DC_V;
	case APPADMM_500_FUNCTIONCODE_AC_DC_V:
		return APPADMM_FUNCTIONCODE_AC_DC_V;
	case APPADMM_500_FUNCTIONCODE_AC_MV:
		if (arg_lpf)
			return APPADMM_FUNCTIONCODE_LPF_MV;
		else
			return APPADMM_FUNCTIONCODE_AC_MV;
	case APPADMM_500_FUNCTIONCODE_DC_MV:
		return APPADMM_FUNCTIONCODE_DC_MV;
	case APPADMM_500_FUNCTIONCODE_AC_DC_MV:
		return APPADMM_FUNCTIONCODE_AC_DC_MV;
	case APPADMM_500_FUNCTIONCODE_OHM:
		return APPADMM_FUNCTIONCODE_OHM;
	case APPADMM_500_FUNCTIONCODE_CONTINUITY:
		return APPADMM_FUNCTIONCODE_CONTINUITY;
	case APPADMM_500_FUNCTIONCODE_CAP:
		return APPADMM_FUNCTIONCODE_CAP;
	case APPADMM_500_FUNCTIONCODE_DIODE:
		return APPADMM_FUNCTIONCODE_DIODE;
	case APPADMM_500_FUNCTIONCODE_AC_MA:
		if (arg_lpf)
			return APPADMM_FUNCTIONCODE_LPF_MA;
		else
			return APPADMM_FUNCTIONCODE_AC_MA;
	case APPADMM_500_FUNCTIONCODE_DC_MA:
		return APPADMM_FUNCTIONCODE_DC_MA;
	case APPADMM_500_FUNCTIONCODE_AC_DC_MA:
		return APPADMM_FUNCTIONCODE_AC_DC_MA;
	case APPADMM_500_FUNCTIONCODE_AC_A:
		if (arg_lpf)
			return APPADMM_FUNCTIONCODE_LPF_A;
		else
			return APPADMM_FUNCTIONCODE_AC_A;
	case APPADMM_500_FUNCTIONCODE_DC_A:
		return APPADMM_FUNCTIONCODE_DC_A;
	case APPADMM_500_FUNCTIONCODE_AC_DC_A:
		return APPADMM_FUNCTIONCODE_AC_DC_A;
	case APPADMM_500_FUNCTIONCODE_FREQUENCY:
		return APPADMM_FUNCTIONCODE_FREQUENCY;
	case APPADMM_500_FUNCTIONCODE_DUTY:
		return APPADMM_FUNCTIONCODE_DUTY;
	default:
		break;
	}
	return APPADMM_FUNCTIONCODE_NONE;
}

/**
 * Map log rates from Legacy 500 to current
 * to allow usage of the same value parser for all devices
 *
 * @param arg_ratecode APPA 500 Legacy rate code
 * @return log rate in ms
 */
static int64_t appadmm_500_map_log_rate(uint8_t arg_ratecode)
{
	switch (arg_ratecode) {
	case 0x00:
		return 500;
	case 0x01:
		return 1000;
	case 0x02:
		return 10000;
	case 0x03:
		return 30000;
	case 0x04:
		return 60000;
	case 0x05:
		return 120000;
	case 0x06:
		return 180000;
	case 0x07:
		return 240000;
	case 0x08:
		return 300000;
	case 0x09:
		return 360000;
	case 0x0a:
		return 480000;
	case 0x0b:
		return 600000;
	default:
		return 0;
	}
	return 0;
}

/**
 * Check, if response size is valid
 *
 * @param arg_command Command
 * @return SR_OK if size is valid, otherwise SR_ERR_...
 */
static int appadmm_is_response_size_valid(enum appadmm_command_e arg_command,
	int arg_size)
{
	int size;

	size = appadmm_get_response_size(arg_command);

	if (size < SR_OK)
		return size;

	if (arg_command == APPADMM_COMMAND_READ_MEMORY
		&& arg_size <= size)
		return SR_OK;

	if (size == arg_size)
		return SR_OK;

	return SR_ERR_DATA;
}

/**
 * Encode raw data of COMMAND_READ_INFORMATION
 *
 * @param arg_read_information Read Information structure
 * @param arg_packet APPA Packet
 * @retval SR_OK on success
 * @retval SR_ERR_... on error
 */
static int appadmm_enc_read_information(const struct appadmm_request_data_read_information_s *arg_read_information,
	struct sr_tp_appa_packet *arg_packet)
{
	if (arg_packet == NULL
		|| arg_read_information == NULL)
		return SR_ERR_ARG;

	arg_packet->command = APPADMM_COMMAND_READ_INFORMATION;
	arg_packet->length = appadmm_get_request_size(APPADMM_COMMAND_READ_INFORMATION);

	return SR_OK;
}

/**
 * Decode raw data of COMMAND_READ_INFORMATION
 *
 * @param arg_packet APPA Packet
 * @param arg_read_information Device information structure
 * @retval SR_OK on success
 * @retval SR_ERR_... on error
 */
static int appadmm_dec_read_information(const struct sr_tp_appa_packet *arg_packet,
	struct appadmm_response_data_read_information_s *arg_read_information)
{
	int xloop;
	char *ltr;
	const uint8_t *rdptr;


	if (arg_packet == NULL
		|| arg_read_information == NULL)
		return SR_ERR_ARG;

	if (sizeof(arg_read_information->model_name) == 0
		|| sizeof(arg_read_information->serial_number) == 0)
		return SR_ERR_BUG;

	if (arg_packet->command != APPADMM_COMMAND_READ_INFORMATION)
		return SR_ERR_DATA;

	if (appadmm_is_response_size_valid(APPADMM_COMMAND_READ_INFORMATION, arg_packet->length))
		return SR_ERR_DATA;

	rdptr = &arg_packet->data[0];

	arg_read_information->model_name[0] = 0;
	arg_read_information->serial_number[0] = 0;
	arg_read_information->firmware_version = 0;
	arg_read_information->model_id = 0;

	ltr = &arg_read_information->model_name[0];
	for (xloop = 0; xloop < 32; xloop++) {
		*ltr = read_u8_inc(&rdptr);
		ltr++;
	}
	arg_read_information->model_name[sizeof(arg_read_information->model_name) - 1] = 0;

	ltr = &arg_read_information->serial_number[0];
	for (xloop = 0; xloop < 16; xloop++) {
		*ltr = read_u8_inc(&rdptr);
		ltr++;
	}
	arg_read_information->serial_number[sizeof(arg_read_information->serial_number) - 1] = 0;

	arg_read_information->model_id = read_u16le_inc(&rdptr);
	arg_read_information->firmware_version = read_u16le_inc(&rdptr);

	g_strstrip(arg_read_information->model_name);
	g_strstrip(arg_read_information->serial_number);

	return SR_OK;
}

/**
 * Request Device information and return response if available
 *
 * Fill with safe defaults on error (allows easy device detection)
 *
 * @param arg_tpai APPA instance
 * @param arg_request Request structure
 * @param arg_response Response structure
 * @retval SR_OK on success
 * @retval SR_ERR_... on error
 */
static int appadmm_rere_read_information(struct sr_tp_appa_inst *arg_tpai,
	const struct appadmm_request_data_read_information_s *arg_request,
	struct appadmm_response_data_read_information_s *arg_response)
{
	struct sr_tp_appa_packet packet_request;
	struct sr_tp_appa_packet packet_response;

	int retr;

	if (arg_tpai == NULL
		|| arg_request == NULL
		|| arg_response == NULL)
		return SR_ERR_ARG;

	if ((retr = appadmm_enc_read_information(arg_request, &packet_request))
		< SR_OK)
		return retr;
	if ((retr = sr_tp_appa_send_receive(arg_tpai, &packet_request,
		&packet_response)) < TRUE)
		return retr;
	if ((retr = appadmm_dec_read_information(&packet_response, arg_response))
		< SR_OK)
		return retr;

	return TRUE;
}

/**
 * Encode raw data of COMMAND_READ_DISPLAY
 *
 * @param arg_read_display Read display request structure
 * @param arg_packet APPA packet
 * @retval SR_OK on success
 * @retval SR_ERR_... on error
 */
static int appadmm_enc_read_display(const struct appadmm_request_data_read_display_s *arg_read_display,
	struct sr_tp_appa_packet *arg_packet)
{
	if (arg_packet == NULL
		|| arg_read_display == NULL)
		return SR_ERR_ARG;

	arg_packet->command = APPADMM_COMMAND_READ_DISPLAY;
	arg_packet->length = appadmm_get_request_size(APPADMM_COMMAND_READ_DISPLAY);

	return SR_OK;
}

/**
 * Decode raw data of COMMAND_READ_DISPLAY
 *
 * @param arg_rdptr Pointer to read from
 * @param arg_data Data structure to decode into
 * @return SR_OK if successfull, otherweise SR_ERR_...
 */
static int appadmm_dec_read_display(const struct sr_tp_appa_packet *arg_packet,
	struct appadmm_response_data_read_display_s *arg_read_display)
{
	const uint8_t *rdptr;
	uint8_t u8;

	if (arg_packet == NULL
		|| arg_read_display == NULL)
		return SR_ERR_ARG;

	if (arg_packet->command != APPADMM_COMMAND_READ_DISPLAY)
		return SR_ERR_DATA;

	if (appadmm_is_response_size_valid(APPADMM_COMMAND_READ_DISPLAY, arg_packet->length))
		return SR_ERR_DATA;

	rdptr = &arg_packet->data[0];

	u8 = read_u8_inc(&rdptr);
	arg_read_display->function_code = u8 & 0x7f;
	arg_read_display->auto_test = u8 >> 7;

	u8 = read_u8_inc(&rdptr);
	arg_read_display->range_code = u8 & 0x7f;
	arg_read_display->auto_range = u8 >> 7;

	arg_read_display->primary_display_data.reading = read_i24le_inc(&rdptr);

	u8 = read_u8_inc(&rdptr);
	arg_read_display->primary_display_data.dot = u8 & 0x7;
	arg_read_display->primary_display_data.unit = u8 >> 3;

	u8 = read_u8_inc(&rdptr);
	arg_read_display->primary_display_data.data_content = u8 & 0x7f;
	arg_read_display->primary_display_data.overload = u8 >> 7;

	arg_read_display->secondary_display_data.reading = read_i24le_inc(&rdptr);

	u8 = read_u8_inc(&rdptr);
	arg_read_display->secondary_display_data.dot = u8 & 0x7;
	arg_read_display->secondary_display_data.unit = u8 >> 3;

	u8 = read_u8_inc(&rdptr);
	arg_read_display->secondary_display_data.data_content = u8 & 0x7f;
	arg_read_display->secondary_display_data.overload = u8 >> 7;

	return SR_OK;
}

/**
 * Send out COMMAND_READ_DISPLAY to APPA device to request live
 * display readings
 *
 * @param arg_tpai APPA instance
 * @param arg_request Request structure
 * @retval TRUE on success
 * @retval SR_ERR_... on error
 */
static int appadmm_request_read_display(struct sr_tp_appa_inst *arg_tpai,
	const struct appadmm_request_data_read_display_s *arg_request)
{
	struct sr_tp_appa_packet packet_request;

	int retr;

	if (arg_tpai == NULL
		|| arg_request == NULL)
		return SR_ERR_ARG;

	if ((retr = appadmm_enc_read_display(arg_request, &packet_request))
		< SR_OK)
		return retr;
	if ((retr = sr_tp_appa_send(arg_tpai, &packet_request, FALSE)) < SR_OK)
		return retr;

	return retr;
}

/**
 * Try to receive COMMAND_READ_DISPLAY resonse
 *
 * @param arg_tpai APPA device instance
 * @param arg_response Response structure
 * @retval TRUE if packet was received and arg_response is valid
 * @retval FALSE if no data was available
 * @ratval SR_ERR on error
 */
static int appadmm_response_read_display(struct sr_tp_appa_inst *arg_tpai,
	struct appadmm_response_data_read_display_s *arg_response)
{
	struct sr_tp_appa_packet packet_response;

	int retr;

	if (arg_tpai == NULL
		|| arg_response == NULL)
		return SR_ERR_ARG;

	if ((retr = sr_tp_appa_receive(arg_tpai, &packet_response, FALSE))
		< TRUE)
		return retr;

	if ((retr = appadmm_dec_read_display(&packet_response, arg_response))
		< SR_OK)
		return retr;

	return TRUE;
}

/**
 * Encode raw data of COMMAND_READ_MEMORY
 *
 * @param arg_wrptr Pointer to write to
 * @param arg_data Data structure to encode from
 * @retval SR_OK on success
 * @retval SR_ERR_... on error
 */
static int appadmm_enc_read_memory(const struct appadmm_request_data_read_memory_s *arg_read_memory,
	struct sr_tp_appa_packet *arg_packet)
{
	uint8_t *wrptr;

	if (arg_packet == NULL
		|| arg_read_memory == NULL)
		return SR_ERR_ARG;

	arg_packet->command = APPADMM_COMMAND_READ_MEMORY;
	arg_packet->length = appadmm_get_request_size(APPADMM_COMMAND_READ_MEMORY);

	wrptr = &arg_packet->data[0];

	write_u8_inc(&wrptr, arg_read_memory->device_number);
	write_u16le_inc(&wrptr, arg_read_memory->memory_address);
	write_u8_inc(&wrptr, arg_read_memory->data_length);

	return SR_OK;
}

/**
 * Decode raw data of COMMAND_READ_MEMORY
 *
 * @param arg_rdptr Pointer to read from
 * @param arg_data Data structure to decode into
 * @retval SR_OK on success
 * @retval SR_ERR_... on error
 */
static int appadmm_dec_read_memory(const struct sr_tp_appa_packet *arg_packet,
	struct appadmm_response_data_read_memory_s *arg_read_memory)
{
	const uint8_t *rdptr;
	int xloop;

	if (arg_packet == NULL
		|| arg_read_memory == NULL)
		return SR_ERR_ARG;

	if (arg_packet->command != APPADMM_COMMAND_READ_MEMORY)
		return SR_ERR_DATA;

	if (appadmm_is_response_size_valid(APPADMM_COMMAND_READ_MEMORY, arg_packet->length))
		return SR_ERR_DATA;

	rdptr = &arg_packet->data[0];

	if (arg_packet->length > sizeof(arg_read_memory->data))
		return SR_ERR_DATA;

	/* redundent, for future compatibility with older models */
	arg_read_memory->data_length = arg_packet->length;

	for (xloop = 0; xloop < arg_packet->length; xloop++)
		arg_read_memory->data[xloop] = read_u8_inc(&rdptr);

	return SR_OK;
}

/**
 * Request memory block of device and return result immediatly.
 *
 * Can read any accessible EEPROM address of the device.
 *
 * @param arg_tpai APPA instance
 * @param arg_request Request structure
 * @param arg_response Response structure
 * @retval TRUE if successfull, arg_response is holding the data
 * @retval FALSE if unsuccessfull
 * @retval SR_ERR_... on error
 */
static int appadmm_rere_read_memory(struct sr_tp_appa_inst *arg_tpai,
	const struct appadmm_request_data_read_memory_s *arg_request,
	struct appadmm_response_data_read_memory_s *arg_response)
{
	struct sr_tp_appa_packet packet_request;
	struct sr_tp_appa_packet packet_response;

	int retr;

	if (arg_tpai == NULL
		|| arg_request == NULL
		|| arg_response == NULL)
		return SR_ERR_ARG;

	if ((retr = appadmm_enc_read_memory(arg_request, &packet_request))
		< SR_OK)
		return retr;

	if ((retr = sr_tp_appa_send_receive(arg_tpai, &packet_request,
		&packet_response)) < TRUE)
		return retr;

	if ((retr = appadmm_dec_read_memory(&packet_response, arg_response))
		< SR_OK)
		return retr;

	return TRUE;
}

/**
 * Request memory data from device
 *
 * Used for MEM/LOG data acquisition, will not block.
 *
 * @param arg_tpai APPA instance
 * @param arg_request Request structure
 * @retval TRUE on success
 * @retval FALSE if not successfull
 * @retval SR_ERR on error
 */
static int appadmm_request_read_memory(struct sr_tp_appa_inst *arg_tpai,
	const struct appadmm_request_data_read_memory_s *arg_request)
{
	struct sr_tp_appa_packet packet_request;

	int retr;

	if (arg_tpai == NULL
		|| arg_request == NULL)
		return SR_ERR_ARG;

	if ((retr = appadmm_enc_read_memory(arg_request, &packet_request))
		< SR_OK)
		return retr;
	if ((retr = sr_tp_appa_send(arg_tpai, &packet_request, FALSE)) < SR_OK)
		return retr;

	return retr;
}

/**
 * Try to receive memory response from device
 *
 * Used for MEM/LOG acquisition
 *
 * @param arg_tpai APPA instance
 * @param arg_response Response structure
 * @retval TRUE if packet was received and arg_response is valid
 * @retval FALSE if no data was available
 * @ratval SR_ERR on error
 */
static int appadmm_response_read_memory(struct sr_tp_appa_inst *arg_tpai,
	struct appadmm_response_data_read_memory_s *arg_response)
{
	struct sr_tp_appa_packet packet_response;

	int retr;

	if (arg_tpai == NULL
		|| arg_response == NULL)
		return SR_ERR_ARG;

	if ((retr = sr_tp_appa_receive(arg_tpai, &packet_response, FALSE))
		< TRUE)
		return retr;

	if ((retr = appadmm_dec_read_memory(&packet_response, arg_response))
		< SR_OK)
		return retr;

	return TRUE;
}

/**
 * Decode storage information data from EEPROM
 *
 * Based on the model decode metadata from device EEPROM to get the amount of
 * samples and sample rate in MEM and LOG memory.
 *
 * @param arg_read_memory Read memory response to decode
 * @param arg_devc Context where the storage_info structure is located
 * @retval SR_OK on success
 * @retval SR_ERR_... on error
 */
static int appadmm_dec_storage_info(const struct appadmm_response_data_read_memory_s
	*arg_read_memory, struct appadmm_context *arg_devc)
{
	const uint8_t *rdptr;
	int xloop;

	if (arg_read_memory == NULL
		|| arg_devc == NULL)
		return SR_ERR_ARG;

	if (arg_read_memory->data_length != 6)
		return SR_ERR_DATA;

	rdptr = &arg_read_memory->data[0];

	switch (arg_devc->model_id) {
	default:
	case APPADMM_MODEL_ID_OVERFLOW:
	case APPADMM_MODEL_ID_INVALID:
	case APPADMM_MODEL_ID_S0:
	case APPADMM_MODEL_ID_SFLEX_10A:
	case APPADMM_MODEL_ID_SFLEX_18A:
	case APPADMM_MODEL_ID_A17N:
		sr_err("Your Device doesn't support MEM/LOG or invalid information!");
		break;
	case APPADMM_MODEL_ID_150:
	case APPADMM_MODEL_ID_150B:
		arg_devc->storage_info[APPADMM_STORAGE_MEM].amount = read_u16be_inc(&rdptr);
		arg_devc->storage_info[APPADMM_STORAGE_LOG].amount = read_u16be_inc(&rdptr);
		arg_devc->storage_info[APPADMM_STORAGE_LOG].rate = ((int64_t)read_u16be_inc(&rdptr)) * 1000;

		arg_devc->storage_info[APPADMM_STORAGE_MEM].entry_size = APPADMM_STORAGE_150_ENTRY_SIZE;
		arg_devc->storage_info[APPADMM_STORAGE_MEM].entry_count = APPADMM_STORAGE_150_MEM_ENTRY_COUNT;
		arg_devc->storage_info[APPADMM_STORAGE_MEM].mem_offset = APPADMM_STORAGE_150_MEM_ADDRESS;
		arg_devc->storage_info[APPADMM_STORAGE_MEM].mem_count = APPADMM_STORAGE_150_MEM_MEM_COUNT;

		arg_devc->storage_info[APPADMM_STORAGE_LOG].entry_size = APPADMM_STORAGE_150_ENTRY_SIZE;
		arg_devc->storage_info[APPADMM_STORAGE_LOG].entry_count = APPADMM_STORAGE_150_LOG_ENTRY_COUNT;
		arg_devc->storage_info[APPADMM_STORAGE_LOG].mem_offset = APPADMM_STORAGE_150_LOG_ADDRESS;
		arg_devc->storage_info[APPADMM_STORAGE_LOG].mem_count = APPADMM_STORAGE_150_LOG_MEM_COUNT;
		break;
	case APPADMM_MODEL_ID_208:
	case APPADMM_MODEL_ID_208B:
	case APPADMM_MODEL_ID_501:
	case APPADMM_MODEL_ID_502:
	case APPADMM_MODEL_ID_503:
	case APPADMM_MODEL_ID_505:
	case APPADMM_MODEL_ID_506:
	case APPADMM_MODEL_ID_506B:
	case APPADMM_MODEL_ID_506B_2:
		arg_devc->storage_info[APPADMM_STORAGE_LOG].rate = ((int64_t)read_u16be_inc(&rdptr)) * 1000;
		arg_devc->storage_info[APPADMM_STORAGE_LOG].amount = read_u16be_inc(&rdptr);
		arg_devc->storage_info[APPADMM_STORAGE_MEM].amount = read_u16be_inc(&rdptr);

		arg_devc->storage_info[APPADMM_STORAGE_MEM].entry_size = APPADMM_STORAGE_200_500_ENTRY_SIZE;
		arg_devc->storage_info[APPADMM_STORAGE_MEM].entry_count = APPADMM_STORAGE_200_500_MEM_ENTRY_COUNT;
		arg_devc->storage_info[APPADMM_STORAGE_MEM].mem_offset = APPADMM_STORAGE_200_500_MEM_ADDRESS;
		arg_devc->storage_info[APPADMM_STORAGE_MEM].mem_count = APPADMM_STORAGE_200_500_MEM_MEM_COUNT;

		arg_devc->storage_info[APPADMM_STORAGE_LOG].entry_size = APPADMM_STORAGE_200_500_ENTRY_SIZE;
		arg_devc->storage_info[APPADMM_STORAGE_LOG].entry_count = APPADMM_STORAGE_200_500_LOG_ENTRY_COUNT;
		arg_devc->storage_info[APPADMM_STORAGE_LOG].mem_offset = APPADMM_STORAGE_200_500_LOG_ADDRESS;
		arg_devc->storage_info[APPADMM_STORAGE_LOG].mem_count = APPADMM_STORAGE_200_500_LOG_MEM_COUNT;
		break;
	case APPADMM_MODEL_ID_S1:
	case APPADMM_MODEL_ID_S2:
	case APPADMM_MODEL_ID_S3:
	case APPADMM_MODEL_ID_172:
	case APPADMM_MODEL_ID_173:
	case APPADMM_MODEL_ID_175:
	case APPADMM_MODEL_ID_177:
	case APPADMM_MODEL_ID_179:
		for (xloop = 0; xloop < 4; xloop++) {
			arg_devc->storage_info[APPADMM_STORAGE_LOG].rate = ((int64_t)read_u16be_inc(&rdptr)) * 1000;
			arg_devc->storage_info[APPADMM_STORAGE_LOG].amount = read_u16be_inc(&rdptr);
			/* rotating metadata to assumably avoid EEPROM write cycles */
			if (arg_devc->storage_info[APPADMM_STORAGE_LOG].rate != (0xff * 1000)
				&& arg_devc->storage_info[APPADMM_STORAGE_LOG].amount != 0xff) {
				arg_devc->storage_info[APPADMM_STORAGE_LOG].entry_size = APPADMM_STORAGE_170_S_ENTRY_SIZE;
				arg_devc->storage_info[APPADMM_STORAGE_LOG].entry_count = APPADMM_STORAGE_170_S_LOG_ENTRY_COUNT;
				arg_devc->storage_info[APPADMM_STORAGE_LOG].mem_offset = APPADMM_STORAGE_170_S_LOG_ADDRESS;
				arg_devc->storage_info[APPADMM_STORAGE_LOG].mem_count = APPADMM_STORAGE_170_S_LOG_MEM_COUNT;
				arg_devc->storage_info[APPADMM_STORAGE_LOG].mem_start = APPADMM_STORAGE_170_S_LOG_MEM_START;
				break;
			}
		}
		break;
	}

	return SR_OK;
}

/**
 * Encode request for MEM/LOG Data from Device memory
 *
 * Used for data acquisition, this function uses device specific storage_info
 * previously obtained from the device to request up to 12 Samples. Always
 * read full 64 Bytes of data for all but the last entry to avoid memory
 * corruption on some of the devices with problematic BLE chipset.
 *
 * Function calculates the correct memory device id an address based on
 * configuration.
 *
 * @param arg_read_memory Request
 * @param arg_storage_info Storage information
 * @param arg_start_entry First entry to fetch
 * @param arg_entry_count Number of entries to fetch
 * @retval SR_OK on success
 * @retval SR_ERR_... on error
 */
static int appadmm_enc_read_storage(struct appadmm_request_data_read_memory_s *arg_read_memory,
	struct appadmm_storage_info_s *arg_storage_info, int arg_start_entry,
	int arg_entry_count)
{
	int address_position;

	if (arg_read_memory == NULL
		|| arg_storage_info == NULL)
		return SR_ERR_ARG;

	if (arg_start_entry >
		arg_storage_info->mem_count * arg_storage_info->entry_count)
		return SR_ERR_ARG;

	address_position = (arg_start_entry % arg_storage_info->entry_count);

	if (arg_entry_count > (SR_TP_APPA_MAX_DATA_SIZE / arg_storage_info->entry_size))
		arg_entry_count = (SR_TP_APPA_MAX_DATA_SIZE / arg_storage_info->entry_size);

	if (address_position + arg_entry_count > arg_storage_info->entry_count) {
		arg_entry_count = arg_storage_info->entry_count - address_position;
		arg_read_memory->data_length = arg_entry_count * arg_storage_info->entry_size;
	} else {
		/* I don't want to know why I need to do this
		 * to avoid data to become garbage, but I can guess... */
		arg_read_memory->data_length = SR_TP_APPA_MAX_DATA_SIZE;
	}

	arg_read_memory->device_number = arg_start_entry /
		(arg_storage_info->entry_count) + arg_storage_info->mem_start;
	arg_read_memory->memory_address = arg_storage_info->mem_offset
		+ address_position * arg_storage_info->entry_size;

	if (arg_storage_info->endian == APPADMM_MEMENDIAN_BE) {
		arg_read_memory->memory_address = (uint16_t)
			(arg_read_memory->memory_address << 8)
			+ (arg_read_memory->memory_address >> 8);
	}

	if (arg_read_memory->data_length > SR_TP_APPA_MAX_DATA_SIZE)
		arg_read_memory->data_length = SR_TP_APPA_MAX_DATA_SIZE;

	if (arg_read_memory->device_number > arg_storage_info->mem_count + arg_storage_info->mem_start)
		return SR_ERR_BUG;

	return SR_OK;
}

/**
 * Decode response with LOG/MEM samples
 *
 * Response packet is parsed and all entries are decoded into proper display
 * data. Fill-bytes of certain models are dumped.
 *
 * @param arg_read_memory Response structure
 * @param arg_storage_info Storage information structure
 * @param arg_display_data Resulting display data, up to 12 entries
 * @retval SR_OK on success
 * @retval SR_ERR_... on error
 */
static int appadmm_dec_read_storage(const struct appadmm_response_data_read_memory_s
	*arg_read_memory, struct appadmm_storage_info_s *arg_storage_info,
	struct appadmm_display_data_s *arg_display_data)
{
	const uint8_t *rdptr;
	uint8_t u8;
	int xloop;
	int yloop;

	if (arg_read_memory == NULL
		|| arg_storage_info == NULL)
		return SR_ERR_ARG;

	rdptr = &arg_read_memory->data[0];

	for (xloop = 0; xloop < arg_read_memory->data_length /
		arg_storage_info->entry_size; xloop++) {

		arg_display_data[xloop].reading = read_i24le_inc(&rdptr);

		u8 = read_u8_inc(&rdptr);
		arg_display_data[xloop].dot = u8 & 0x7;
		arg_display_data[xloop].unit = u8 >> 3;

		u8 = read_u8_inc(&rdptr);
		arg_display_data[xloop].data_content = u8 & 0x7f;
		arg_display_data[xloop].overload = u8 >> 7;

		/* Ignore fill bytes on devices that provide them */
		for (yloop = 0; yloop < arg_storage_info->entry_size - 5;
			yloop++)
			read_u8_inc(&rdptr);
	}

	return SR_OK;
}

/* **************************************** */
/* ****** Series 500 Legacy Protocol ****** */
/* **************************************** */

/**
 * Encode raw data of Information / APPADMM_500_COMMAND_READ_ALL_DATA
 *
 * @param arg_read_information Read Information structure
 * @param arg_packet APPA Packet
 * @retval SR_OK on success
 * @retval SR_ERR_... on error
 */
static int appadmm_500_enc_read_information(const struct
	appadmm_request_data_read_information_s *arg_read_information,
	struct sr_tp_appa_packet *arg_packet)
{
	if (arg_packet == NULL
		|| arg_read_information == NULL)
		return SR_ERR_ARG;

	arg_packet->command = APPADMM_500_COMMAND_READ_ALL_DATA;
	arg_packet->length = APPADMM_500_FRAME_DATA_SIZE_REQUEST_READ_ALL_DATA;

	return SR_OK;
}

/**
 * Decode raw data of Information / APPADMM_500_COMMAND_READ_ALL_DATA
 *
 * @param arg_packet APPA Packet
 * @param arg_read_information Device information structure
 * @retval SR_OK on success
 * @retval SR_ERR_... on error
 */
static int appadmm_500_dec_read_information(const struct
	sr_tp_appa_packet *arg_packet,
	struct appadmm_response_data_read_information_s *arg_read_information)
{
	int xloop;
	char *ltr;
	const uint8_t *rdptr;

	if (arg_packet == NULL
		|| arg_read_information == NULL)
		return SR_ERR_ARG;

	if (sizeof(arg_read_information->model_name) == 0
		|| sizeof(arg_read_information->serial_number) == 0)
		return SR_ERR_BUG;

	if (arg_packet->command != APPADMM_500_COMMAND_READ_ALL_DATA)
		return SR_ERR_DATA;

	if (arg_packet->length !=
		APPADMM_500_FRAME_DATA_SIZE_RESPONSE_READ_ALL_DATA)
		return SR_ERR_DATA;

	rdptr = &arg_packet->data[0];

	arg_read_information->model_name[0] = 0;
	arg_read_information->serial_number[0] = 0;
	arg_read_information->firmware_version = 0;
	arg_read_information->model_id = 0;

	ltr = &arg_read_information->model_name[0];
	for (xloop = 0; xloop < 32; xloop++) {
		if (xloop < 10)
			*ltr = read_u8_inc(&rdptr);
		else
			*ltr = 0;
		ltr++;
	}
	arg_read_information->model_name[sizeof(arg_read_information->model_name) - 1] = 0;

	ltr = &arg_read_information->serial_number[0];
	for (xloop = 0; xloop < 16; xloop++) {
		if (xloop < 8)
			*ltr = read_u8_inc(&rdptr);
		else
			*ltr = 0;
		ltr++;
	}
	arg_read_information->serial_number[sizeof(arg_read_information->serial_number) - 1] = 0;
	arg_read_information->model_id = 0x5050;
	arg_read_information->firmware_version = read_u8_inc(&rdptr) * 100;
	arg_read_information->firmware_version += read_u8_inc(&rdptr) + 1;

	g_strstrip(arg_read_information->model_name);
	g_strstrip(arg_read_information->serial_number);

	return SR_OK;
}

/**
 * Request Device information and return response if available
 *
 * Fill with safe defaults on error (allows easy device detection)
 *
 * @param arg_tpai APPA instance
 * @param arg_request Request structure
 * @param arg_response Response structure
 * @retval SR_OK on success
 * @retval SR_ERR_... on error
 */
static int appadmm_500_rere_read_information(struct sr_tp_appa_inst *arg_tpai,
	const struct appadmm_request_data_read_information_s *arg_request,
	struct appadmm_response_data_read_information_s *arg_response)
{
	struct sr_tp_appa_packet packet_request;
	struct sr_tp_appa_packet packet_response;

	int retr;

	if (arg_tpai == NULL
		|| arg_request == NULL
		|| arg_response == NULL)
		return SR_ERR_ARG;

	if ((retr
		= appadmm_500_enc_read_information(arg_request, &packet_request))
		< SR_OK)
		return retr;
	if ((retr = sr_tp_appa_send_receive(arg_tpai, &packet_request,
		&packet_response)) < TRUE)
		return retr;
	if ((retr
		= appadmm_500_dec_read_information(&packet_response, arg_response))
		< SR_OK)
		return retr;

	return TRUE;
}

/**
 * Encode raw data of Display / APPADMM_500_COMMAND_READ_ALL_DATA
 *
 * @param arg_read_display Read display request structure
 * @param arg_packet APPA packet
 * @retval SR_OK on success
 * @retval SR_ERR_... on error
 */
static int appadmm_500_enc_read_display(const struct
	appadmm_request_data_read_display_s *arg_read_display,
	struct sr_tp_appa_packet *arg_packet)
{
	if (arg_packet == NULL
		|| arg_read_display == NULL)
		return SR_ERR_ARG;

	arg_packet->command = APPADMM_500_COMMAND_READ_ALL_DATA;
	arg_packet->length = APPADMM_500_FRAME_DATA_SIZE_REQUEST_READ_ALL_DATA;

	return SR_OK;
}

/**
 * Decode raw data of Display / APPADMM_500_COMMAND_READ_ALL_DATA
 *
 * @param arg_rdptr Pointer to read from
 * @param arg_data Data structure to decode into
 * @return SR_OK if successfull, otherweise SR_ERR_...
 */
static int appadmm_500_dec_read_display(const struct sr_tp_appa_packet *arg_packet,
	struct appadmm_response_data_read_display_s *arg_read_display)
{
	if (arg_packet == NULL
		|| arg_read_display == NULL)
		return SR_ERR_ARG;

	if (arg_packet->command != APPADMM_500_COMMAND_READ_ALL_DATA)
		return SR_ERR_DATA;

	if (arg_packet->length != APPADMM_500_FRAME_DATA_SIZE_RESPONSE_READ_ALL_DATA)
		return SR_ERR_DATA;

	arg_read_display->range_code = arg_packet->data[23];
	arg_read_display->auto_range = ((arg_packet->data[24]) & 1) == 0;
	arg_read_display->auto_test = 0;
	arg_read_display->auto_test = ((arg_packet->data[24] >> 1) & 1)
		| ((arg_packet->data[24] >> 5) & 1);
	arg_read_display->function_code =
		appadmm_500_map_functioncode(read_u16be(&arg_packet->data[20]),
		((arg_packet->data[24] >> 4) & 1));

	arg_read_display->primary_display_data.reading =
		read_i24be(&arg_packet->data[37]);
	arg_read_display->primary_display_data.dot =
		arg_packet->data[40] & 0x7;
	arg_read_display->primary_display_data.unit =
		arg_packet->data[40] >> 3;
	arg_read_display->primary_display_data.data_content =
		arg_packet->data[41] & 0x1f;
	if (arg_read_display->primary_display_data.data_content < 2)
		arg_read_display->primary_display_data.data_content = 0;
	else
		arg_read_display->primary_display_data.data_content--;
	arg_read_display->primary_display_data.overload =
		(arg_packet->data[41] >> 5) & 1;
	if (((arg_packet->data[41] >> 7) & 1) == 1)
		arg_read_display->primary_display_data.reading =
		APPADMM_WORDCODE_SPACE;
	else if ((arg_packet->data[41] >> 6) & 1) {
		arg_read_display->primary_display_data.reading +=
			APPADMM_WORDCODE_SPACE;
		if (arg_read_display->primary_display_data.reading
			== APPADMM_WORDCODE_SPACE)
			arg_read_display->primary_display_data.reading =
			APPADMM_WORDCODE_ER;
	}

	arg_read_display->secondary_display_data.reading =
		read_i24be(&arg_packet->data[42]);
	arg_read_display->secondary_display_data.dot =
		arg_packet->data[45] & 0x7;
	arg_read_display->secondary_display_data.unit =
		arg_packet->data[45] >> 3;
	arg_read_display->secondary_display_data.data_content =
		arg_packet->data[46] & 0x1f;
	if (arg_read_display->secondary_display_data.data_content < 2)
		arg_read_display->secondary_display_data.data_content = 0;
	else
		arg_read_display->secondary_display_data.data_content--;
	arg_read_display->secondary_display_data.overload =
		(arg_packet->data[46] >> 5) & 1;
	if (((arg_packet->data[46] >> 7) & 1) == 1)
		arg_read_display->secondary_display_data.reading =
		APPADMM_WORDCODE_SPACE;
	else if ((arg_packet->data[46] >> 6) & 1) {
		arg_read_display->secondary_display_data.reading +=
			APPADMM_WORDCODE_SPACE;
		if (arg_read_display->secondary_display_data.reading
			== APPADMM_WORDCODE_SPACE)
			arg_read_display->secondary_display_data.reading =
			APPADMM_WORDCODE_ER;
	}

	return SR_OK;
}

/**
 * Send out COMMAND_READ_DISPLAY to APPA device to request live
 * display readings
 *
 * @param arg_tpai APPA instance
 * @param arg_request Request structure
 * @retval TRUE on success
 * @retval SR_ERR_... on error
 */
static int appadmm_500_request_read_display(struct sr_tp_appa_inst *arg_tpai,
	const struct appadmm_request_data_read_display_s *arg_request)
{
	struct sr_tp_appa_packet packet_request;

	int retr;

	if (arg_tpai == NULL
		|| arg_request == NULL)
		return SR_ERR_ARG;

	if ((retr = appadmm_500_enc_read_display(arg_request, &packet_request))
		< SR_OK)
		return retr;
	if ((retr = sr_tp_appa_send(arg_tpai, &packet_request, FALSE)) < SR_OK)
		return retr;

	return retr;
}

/**
 * Try to receive COMMAND_READ_DISPLAY resonse
 *
 * @param arg_tpai APPA device instance
 * @param arg_response Response structure
 * @retval TRUE if packet was received and arg_response is valid
 * @retval FALSE if no data was available
 * @ratval SR_ERR on error
 */
static int appadmm_500_response_read_display(struct sr_tp_appa_inst *arg_tpai,
	struct appadmm_response_data_read_display_s *arg_response)
{
	struct sr_tp_appa_packet packet_response;

	int retr;

	if (arg_tpai == NULL
		|| arg_response == NULL)
		return SR_ERR_ARG;

	if ((retr = sr_tp_appa_receive(arg_tpai, &packet_response, FALSE))
		< TRUE)
		return retr;

	if ((retr = appadmm_500_dec_read_display(&packet_response, arg_response))
		< SR_OK)
		return retr;

	return TRUE;
}

/**
 * Encode raw data of COMMAND_READ_MEMORY
 *
 * @param arg_wrptr Pointer to write to
 * @param arg_data Data structure to encode from
 * @retval SR_OK on success
 * @retval SR_ERR_... on error
 */
static int appadmm_500_enc_read_amount(const struct appadmm_500_request_data_read_amount_s *arg_read_amount,
	struct sr_tp_appa_packet *arg_packet,
	enum appadmm_500_command_e arg_amount_command)
{
	if (arg_packet == NULL
		|| arg_read_amount == NULL)
		return SR_ERR_ARG;

	arg_packet->command = arg_amount_command;
	arg_packet->length = APPADMM_500_FRAME_DATA_SIZE_REQUEST_READ_DATALOG_INFO;

	return SR_OK;
}

/**
 * Decode raw data of COMMAND_READ_MEMORY
 *
 * @param arg_rdptr Pointer to read from
 * @param arg_data Data structure to decode into
 * @retval SR_OK on success
 * @retval SR_ERR_... on error
 */
static int appadmm_500_dec_read_amount(const struct sr_tp_appa_packet *arg_packet,
	struct appadmm_500_response_data_read_amount_s *arg_read_amount,
	enum appadmm_500_command_e arg_amount_command)
{
	if (arg_packet == NULL
		|| arg_read_amount == NULL)
		return SR_ERR_ARG;

	if (arg_packet->command != arg_amount_command)
		return SR_ERR_DATA;

	arg_read_amount->amount = read_u16be(&arg_packet->data[0]);

	return SR_OK;
}

/**
 * Request memory block of device and return result immediatly.
 *
 * Can read any accessible EEPROM address of the device.
 *
 * @param arg_tpai APPA instance
 * @param arg_request Request structure
 * @param arg_response Response structure
 * @retval TRUE if successfull, arg_response is holding the data
 * @retval FALSE if unsuccessfull
 * @retval SR_ERR_... on error
 */
static int appadmm_500_rere_read_amount(struct sr_tp_appa_inst *arg_tpai,
	const struct appadmm_500_request_data_read_amount_s *arg_request,
	struct appadmm_500_response_data_read_amount_s *arg_response,
	enum appadmm_500_command_e arg_amount_command)
{
	struct sr_tp_appa_packet packet_request;
	struct sr_tp_appa_packet packet_response;

	int retr;

	if (arg_tpai == NULL
		|| arg_request == NULL
		|| arg_response == NULL)
		return SR_ERR_ARG;

	if ((retr = appadmm_500_enc_read_amount(arg_request, &packet_request,
		arg_amount_command))
		< SR_OK)
		return retr;

	if ((retr = sr_tp_appa_send_receive(arg_tpai, &packet_request,
		&packet_response)) < TRUE)
		return retr;

	if ((retr = appadmm_500_dec_read_amount(&packet_response, arg_response,
		arg_amount_command))
		< SR_OK)
		return retr;

	return TRUE;
}

/**
 * Decode storage information data from log/stor
 *
 * Based on the model decode metadata from device EEPROM to get the amount of
 * samples and sample rate in MEM and LOG memory.
 *
 * @param arg_read_memory Read memory response to decode
 * @param arg_devc Context where the storage_info structure is located
 * @retval SR_OK on success
 * @retval SR_ERR_... on error
 */
static int appadmm_500_dec_storage_info(uint16_t arg_amount_log,
	uint16_t arg_amount_mem,
	int64_t arg_rate,
	struct appadmm_context *arg_devc)
{
	if (arg_devc == NULL)
		return SR_ERR_ARG;

	switch (arg_devc->model_id) {
	default:
		sr_err("Your Device doesn't support MEM/LOG or invalid information!");
		break;
	case APPADMM_MODEL_ID_LEGACY_505:
		arg_devc->storage_info[APPADMM_STORAGE_LOG].rate = arg_rate;
		arg_devc->storage_info[APPADMM_STORAGE_LOG].amount = arg_amount_log;
		arg_devc->storage_info[APPADMM_STORAGE_LOG].endian = APPADMM_MEMENDIAN_BE;
		arg_devc->storage_info[APPADMM_STORAGE_MEM].amount = arg_amount_mem;
		arg_devc->storage_info[APPADMM_STORAGE_MEM].endian = APPADMM_MEMENDIAN_BE;

		arg_devc->storage_info[APPADMM_STORAGE_MEM].entry_size = APPADMM_STORAGE_500_LEGACY_ENTRY_SIZE;
		arg_devc->storage_info[APPADMM_STORAGE_MEM].entry_count = APPADMM_STORAGE_500_LEGACY_MEM_ENTRY_COUNT;
		arg_devc->storage_info[APPADMM_STORAGE_MEM].mem_offset = APPADMM_STORAGE_500_LEGACY_MEM_ADDRESS;
		arg_devc->storage_info[APPADMM_STORAGE_MEM].mem_count = APPADMM_STORAGE_500_LEGACY_MEM_MEM_COUNT;

		arg_devc->storage_info[APPADMM_STORAGE_LOG].entry_size = APPADMM_STORAGE_500_LEGACY_ENTRY_SIZE;
		arg_devc->storage_info[APPADMM_STORAGE_LOG].entry_count = APPADMM_STORAGE_500_LEGACY_LOG_ENTRY_COUNT;
		arg_devc->storage_info[APPADMM_STORAGE_LOG].mem_offset = APPADMM_STORAGE_500_LEGACY_LOG_ADDRESS;
		arg_devc->storage_info[APPADMM_STORAGE_LOG].mem_count = APPADMM_STORAGE_500_LEGACY_LOG_MEM_COUNT;
		break;
	}

	return SR_OK;
}

/**
 * Decode response with LOG/MEM samples
 *
 * Response packet is parsed and all entries are decoded into proper display
 * data. Fill-bytes of certain models are dumped.
 *
 * @param arg_read_memory Response structure
 * @param arg_storage_info Storage information structure
 * @param arg_display_data Resulting display data, up to 12 entries
 * @retval SR_OK on success
 * @retval SR_ERR_... on error
 */
static int appadmm_500_dec_read_storage(const struct appadmm_response_data_read_memory_s
	*arg_read_memory, struct appadmm_storage_info_s *arg_storage_info,
	struct appadmm_display_data_s *arg_display_data)
{
	const uint8_t *rdptr;
	uint8_t u8;
	int xloop;
	int yloop;

	if (arg_read_memory == NULL
		|| arg_storage_info == NULL)
		return SR_ERR_ARG;

	rdptr = &arg_read_memory->data[0];

	for (xloop = 0; xloop < arg_read_memory->data_length /
		arg_storage_info->entry_size; xloop++) {

		arg_display_data[xloop].reading = read_i24be_inc(&rdptr);

		u8 = read_u8_inc(&rdptr);
		arg_display_data[xloop].dot = u8 & 0x7;
		arg_display_data[xloop].unit = u8 >> 3;

		u8 = read_u8_inc(&rdptr);
		arg_display_data[xloop].log_function_code = u8 & 0x31;
		arg_display_data[xloop].overload = (u8 >> 5);

		if (((u8 >> 7) & 1) == 1)
			arg_display_data[xloop].reading =
			APPADMM_WORDCODE_SPACE;
		else if ((u8 >> 6) & 1) {
			arg_display_data[xloop].reading +=
				APPADMM_WORDCODE_SPACE;
			if (arg_display_data[xloop].reading
				== APPADMM_WORDCODE_SPACE)
				arg_display_data[xloop].reading =
				APPADMM_WORDCODE_ER;
		}

		/* Ignore fill bytes on devices that provide them */
		for (yloop = 0; yloop < arg_storage_info->entry_size - 5;
			yloop++)
			read_u8_inc(&rdptr);
	}

	return SR_OK;
}

#endif/*LIBSIGROK_HARDWARE_APPA_DMM_PROTOCOL_PACKET_H*/
