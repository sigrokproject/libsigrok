/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2010 Sven Peter <sven@fail0verflow.com>
 * Copyright (C) 2010 Haxx Enterprises <bushing@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 *  THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <assert.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"
#include "analyzer.h"
#include "gl_usb.h"
#include "protocol.h"

enum {
	HARD_DATA_CHECK_SUM		= 0x00,
	PASS_WORD,

	DEV_ID0				= 0x10,
	DEV_ID1,

	START_STATUS			= 0x20,
	DEV_STATUS			= 0x21,
	FREQUENCY_REG0			= 0x30,
	FREQUENCY_REG1,
	FREQUENCY_REG2,
	FREQUENCY_REG3,
	FREQUENCY_REG4,
	MEMORY_LENGTH,
	CLOCK_SOURCE,

	TRIGGER_STATUS0			= 0x40,
	TRIGGER_STATUS1,
	TRIGGER_STATUS2,
	TRIGGER_STATUS3,
	TRIGGER_STATUS4,
	TRIGGER_STATUS5,
	TRIGGER_STATUS6,
	TRIGGER_STATUS7,
	TRIGGER_STATUS8,

	TRIGGER_COUNT0			= 0x50,
	TRIGGER_COUNT1,

	TRIGGER_LEVEL0			= 0x55,
	TRIGGER_LEVEL1,
	TRIGGER_LEVEL2,
	TRIGGER_LEVEL3,

	RAMSIZE_TRIGGERBAR_ADDRESS0	= 0x60,
	RAMSIZE_TRIGGERBAR_ADDRESS1,
	RAMSIZE_TRIGGERBAR_ADDRESS2,
	TRIGGERBAR_ADDRESS0,
	TRIGGERBAR_ADDRESS1,
	TRIGGERBAR_ADDRESS2,
	DONT_CARE_TRIGGERBAR,

	FILTER_ENABLE			= 0x70,
	FILTER_STATUS,

	ENABLE_DELAY_TIME0		= 0x7a,
	ENABLE_DELAY_TIME1,

	ENABLE_INSERT_DATA0		= 0x80,
	ENABLE_INSERT_DATA1,
	ENABLE_INSERT_DATA2,
	ENABLE_INSERT_DATA3,
	COMPRESSION_TYPE0,
	COMPRESSION_TYPE1,

	TRIGGER_ADDRESS0		= 0x90,
	TRIGGER_ADDRESS1,
	TRIGGER_ADDRESS2,

	NOW_ADDRESS0			= 0x96,
	NOW_ADDRESS1,
	NOW_ADDRESS2,

	STOP_ADDRESS0			= 0x9b,
	STOP_ADDRESS1,
	STOP_ADDRESS2,

	READ_RAM_STATUS			= 0xa0,
};

static int g_trigger_status[9] = { 0 };
static int g_trigger_count = 1;
static int g_filter_status[8] = { 0 };
static int g_filter_enable = 0;

static int g_freq_value = 1;
static int g_freq_scale = FREQ_SCALE_MHZ;
static int g_memory_size = MEMORY_SIZE_8K;
static int g_ramsize_triggerbar_addr = 2 * 1024;
static int g_triggerbar_addr = 0;
static int g_compression = COMPRESSION_NONE;
static int g_thresh = 0x31; /* 1.5V */

/* Maybe unk specifies an "endpoint" or "register" of sorts. */
static int analyzer_write_status(libusb_device_handle *devh, unsigned char unk,
				 unsigned char flags)
{
	assert(unk <= 3);
	return gl_reg_write(devh, START_STATUS, unk << 6 | flags);
}

#if 0
static int __analyzer_set_freq(libusb_device_handle *devh, int freq, int scale)
{
	int reg0 = 0, divisor = 0, reg2 = 0;

	switch (scale) {
	case FREQ_SCALE_MHZ: /* MHz */
		if (freq >= 100 && freq <= 200) {
			reg0 = freq * 0.1;
			divisor = 1;
			reg2 = 0;
			break;
		}
		if (freq >= 50 && freq < 100) {
			reg0 = freq * 0.2;
			divisor = 2;
			reg2 = 0;
			break;
		}
		if (freq >= 10 && freq < 50) {
			if (freq == 25) {
				reg0 = 25;
				divisor = 5;
				reg2 = 1;
				break;
			} else {
				reg0 = freq * 0.5;
				divisor = 5;
				reg2 = 1;
				break;
			}
		}
		if (freq >= 2 && freq < 10) {
			divisor = 5;
			reg0 = freq * 2;
			reg2 = 2;
			break;
		}
		if (freq == 1) {
			divisor = 5;
			reg2 = 16;
			reg0 = 5;
			break;
		}
		divisor = 5;
		reg0 = 5;
		reg2 = 64;
		break;
	case FREQ_SCALE_HZ: /* Hz */
		if (freq >= 500 && freq < 1000) {
			reg0 = freq * 0.01;
			divisor = 10;
			reg2 = 64;
			break;
		}
		if (freq >= 300 && freq < 500) {
			reg0 = freq * 0.005 * 8;
			divisor = 5;
			reg2 = 67;
			break;
		}
		if (freq >= 100 && freq < 300) {
			reg0 = freq * 0.005 * 16;
			divisor = 5;
			reg2 = 68;
			break;
		}
		divisor = 5;
		reg0 = 5;
		reg2 = 64;
		break;
	case FREQ_SCALE_KHZ: /* kHz */
		if (freq >= 500 && freq < 1000) {
			reg0 = freq * 0.01;
			divisor = 5;
			reg2 = 17;
			break;
		}
		if (freq >= 100 && freq < 500) {
			reg0 = freq * 0.05;
			divisor = 5;
			reg2 = 32;
			break;
		}
		if (freq >= 50 && freq < 100) {
			reg0 = freq * 0.1;
			divisor = 5;
			reg2 = 33;
			break;
		}
		if (freq >= 10 && freq < 50) {
			if (freq == 25) {
				reg0 = 25;
				divisor = 5;
				reg2 = 49;
				break;
			}
			reg0 = freq * 0.5;
			divisor = 5;
			reg2 = 48;
			break;
		}
		if (freq >= 2 && freq < 10) {
			divisor = 5;
			reg0 = freq * 2;
			reg2 = 50;
			break;
		}
		divisor = 5;
		reg0 = 5;
		reg2 = 64;
		break;
	default:
		divisor = 5;
		reg0 = 5;
		reg2 = 64;
		break;
	}

	sr_dbg("Setting samplerate regs (freq=%d, scale=%d): "
	       "reg0: %d, reg1: %d, reg2: %d, reg3: %d.",
	       freq, scale, divisor, reg0, 0x02, reg2);

	if (gl_reg_write(devh, FREQUENCY_REG0, divisor) < 0)
		return -1; /* Divisor maybe? */

	if (gl_reg_write(devh, FREQUENCY_REG1, reg0) < 0)
		return -1; /* 10 / 0.2 */

	if (gl_reg_write(devh, FREQUENCY_REG2, 0x02) < 0)
		return -1; /* Always 2 */

	if (gl_reg_write(devh, FREQUENCY_REG4, reg2) < 0)
		return -1;

	return 0;
}
#endif

/*
 * It seems that ...
 *	FREQUENCT_REG0 - division factor (?)
 *	FREQUENCT_REG1 - multiplication factor (?)
 *	FREQUENCT_REG4 - clock selection (?)
 *
 *	clock selection
 *	0  10MHz  16   1MHz  32 100kHz  48  10kHz  64   1kHz
 *	1   5MHz  17 500kHz  33  50kHz  49   5kHz  65  500Hz
 *	2 2.5MHz   .          .         50 2.5kHz  66  250Hz
 *	.          .          .          .         67  125Hz
 *	.          .          .          .         68 62.5Hz
 */
static int __analyzer_set_freq(libusb_device_handle *devh, int freq, int scale)
{
	struct freq_factor {
		int freq;
		int scale;
		int sel;
		int div;
		int mul;
	};

	static const struct freq_factor f[] = {
		{ 200, FREQ_SCALE_MHZ,  0,  1, 20 },
		{ 150, FREQ_SCALE_MHZ,  0,  1, 15 },
		{ 100, FREQ_SCALE_MHZ,  0,  1, 10 },
		{  80, FREQ_SCALE_MHZ,  0,  2, 16 },
		{  50, FREQ_SCALE_MHZ,  0,  2, 10 },
		{  25, FREQ_SCALE_MHZ,  1,  5, 25 },
		{  10, FREQ_SCALE_MHZ,  1,  5, 10 },
		{   1, FREQ_SCALE_MHZ, 16,  5,  5 },
		{ 800, FREQ_SCALE_KHZ, 17,  5,  8 },
		{ 400, FREQ_SCALE_KHZ, 32,  5, 20 },
		{ 200, FREQ_SCALE_KHZ, 32,  5, 10 },
		{ 100, FREQ_SCALE_KHZ, 32,  5,  5 },
		{  50, FREQ_SCALE_KHZ, 33,  5,  5 },
		{  25, FREQ_SCALE_KHZ, 49,  5, 25 },
		{   5, FREQ_SCALE_KHZ, 50,  5, 10 },
		{   1, FREQ_SCALE_KHZ, 64,  5,  5 },
		{ 500, FREQ_SCALE_HZ,  64, 10,  5 },
		{ 100, FREQ_SCALE_HZ,  68,  5,  8 },
		{   0, 0,              0,   0,  0 }
	};

	int i;

	for (i = 0; f[i].freq; i++) {
		if (scale == f[i].scale && freq == f[i].freq)
			break;
	}
	if (!f[i].freq)
		return -1;

	sr_dbg("Setting samplerate regs (freq=%d, scale=%d): "
	       "reg0: %d, reg1: %d, reg2: %d, reg3: %d.",
	       freq, scale, f[i].div, f[i].mul, 0x02, f[i].sel);

	if (gl_reg_write(devh, FREQUENCY_REG0, f[i].div) < 0)
		return -1;

	if (gl_reg_write(devh, FREQUENCY_REG1, f[i].mul) < 0)
		return -1;

	if (gl_reg_write(devh, FREQUENCY_REG2, 0x02) < 0)
		return -1;

	if (gl_reg_write(devh, FREQUENCY_REG4, f[i].sel) < 0)
		return -1;

	return 0;
}

static void __analyzer_set_ramsize_trigger_address(libusb_device_handle *devh,
						   unsigned int address)
{
	gl_reg_write(devh, RAMSIZE_TRIGGERBAR_ADDRESS0, (address >> 0) & 0xFF);
	gl_reg_write(devh, RAMSIZE_TRIGGERBAR_ADDRESS1, (address >> 8) & 0xFF);
	gl_reg_write(devh, RAMSIZE_TRIGGERBAR_ADDRESS2, (address >> 16) & 0xFF);
}

static void __analyzer_set_triggerbar_address(libusb_device_handle *devh,
					      unsigned int address)
{
	gl_reg_write(devh, TRIGGERBAR_ADDRESS0, (address >> 0) & 0xFF);
	gl_reg_write(devh, TRIGGERBAR_ADDRESS1, (address >> 8) & 0xFF);
	gl_reg_write(devh, TRIGGERBAR_ADDRESS2, (address >> 16) & 0xFF);
}

static void __analyzer_set_compression(libusb_device_handle *devh,
				       unsigned int type)
{
	gl_reg_write(devh, COMPRESSION_TYPE0, (type >> 0) & 0xFF);
	gl_reg_write(devh, COMPRESSION_TYPE1, (type >> 8) & 0xFF);
}

static void __analyzer_set_trigger_count(libusb_device_handle *devh,
					 unsigned int count)
{
	gl_reg_write(devh, TRIGGER_COUNT0, (count >> 0) & 0xFF);
	gl_reg_write(devh, TRIGGER_COUNT1, (count >> 8) & 0xFF);
}

static void analyzer_write_enable_insert_data(libusb_device_handle *devh)
{
	gl_reg_write(devh, ENABLE_INSERT_DATA0, 0x12);
	gl_reg_write(devh, ENABLE_INSERT_DATA1, 0x34);
	gl_reg_write(devh, ENABLE_INSERT_DATA2, 0x56);
	gl_reg_write(devh, ENABLE_INSERT_DATA3, 0x78);
}

static void analyzer_set_filter(libusb_device_handle *devh)
{
	int i;
	gl_reg_write(devh, FILTER_ENABLE, g_filter_enable);
	for (i = 0; i < 8; i++)
		gl_reg_write(devh, FILTER_STATUS + i, g_filter_status[i]);
}

SR_PRIV void analyzer_reset(libusb_device_handle *devh)
{
	analyzer_write_status(devh, 3, STATUS_FLAG_NONE);	// reset device
	analyzer_write_status(devh, 3, STATUS_FLAG_RESET);	// reset device
}

SR_PRIV void analyzer_initialize(libusb_device_handle *devh)
{
	analyzer_write_status(devh, 1, STATUS_FLAG_NONE);
	analyzer_write_status(devh, 1, STATUS_FLAG_INIT);
	analyzer_write_status(devh, 1, STATUS_FLAG_NONE);
}

SR_PRIV void analyzer_wait(libusb_device_handle *devh, int set, int unset)
{
	int status;

	while (1) {
		status = gl_reg_read(devh, DEV_STATUS);
		if ((!set || (status & set)) && ((status & unset) == 0))
			return;
	}
}

SR_PRIV void analyzer_read_start(libusb_device_handle *devh)
{
	analyzer_write_status(devh, 3, STATUS_FLAG_20 | STATUS_FLAG_READ);

	/* Prep for bulk reads */
	gl_reg_read_buf(devh, READ_RAM_STATUS, NULL, 0);
}

SR_PRIV int analyzer_read_data(libusb_device_handle *devh, void *buffer,
		       unsigned int size)
{
	return gl_read_bulk(devh, buffer, size);
}

SR_PRIV void analyzer_read_stop(libusb_device_handle *devh)
{
	analyzer_write_status(devh, 3, STATUS_FLAG_20);
	analyzer_write_status(devh, 3, STATUS_FLAG_NONE);
}

SR_PRIV void analyzer_start(libusb_device_handle *devh)
{
	analyzer_write_status(devh, 1, STATUS_FLAG_NONE);
	analyzer_write_status(devh, 1, STATUS_FLAG_INIT);
	analyzer_write_status(devh, 1, STATUS_FLAG_NONE);
	analyzer_write_status(devh, 1, STATUS_FLAG_GO);
}

SR_PRIV void analyzer_configure(libusb_device_handle *devh)
{
	int i;

	/* Write_Start_Status */
	analyzer_write_status(devh, 1, STATUS_FLAG_RESET);
	analyzer_write_status(devh, 1, STATUS_FLAG_NONE);

	/* Start_Config_Outside_Device ? */
	analyzer_write_status(devh, 1, STATUS_FLAG_INIT);
	analyzer_write_status(devh, 1, STATUS_FLAG_NONE);

	/* SetData_To_Frequence_Reg */
	__analyzer_set_freq(devh, g_freq_value, g_freq_scale);

	/* SetMemory_Length */
	gl_reg_write(devh, MEMORY_LENGTH, g_memory_size);

	/* Sele_Inside_Outside_Clock */
	gl_reg_write(devh, CLOCK_SOURCE, 0x03);

	/* Set_Trigger_Status */
	for (i = 0; i < 9; i++)
		gl_reg_write(devh, TRIGGER_STATUS0 + i, g_trigger_status[i]);

	__analyzer_set_trigger_count(devh, g_trigger_count);

	/* Set_Trigger_Level */
	gl_reg_write(devh, TRIGGER_LEVEL0, g_thresh);
	gl_reg_write(devh, TRIGGER_LEVEL1, g_thresh);
	gl_reg_write(devh, TRIGGER_LEVEL2, g_thresh);
	gl_reg_write(devh, TRIGGER_LEVEL3, g_thresh);

	/* Size of actual memory >> 2 */
	__analyzer_set_ramsize_trigger_address(devh, g_ramsize_triggerbar_addr);
	__analyzer_set_triggerbar_address(devh, g_triggerbar_addr);

	/* Set_Dont_Care_TriggerBar */
	gl_reg_write(devh, DONT_CARE_TRIGGERBAR, 0x01);

	/* Enable_Status */
	analyzer_set_filter(devh);

	/* Set_Enable_Delay_Time */
	gl_reg_write(devh, 0x7a, 0x00);
	gl_reg_write(devh, 0x7b, 0x00);
	analyzer_write_enable_insert_data(devh);
	__analyzer_set_compression(devh, g_compression);
}

SR_PRIV int analyzer_add_triggers(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_trigger *trigger;
	struct sr_trigger_stage *stage;
	struct sr_trigger_match *match;
	GSList *l, *m;
	int channel;

	devc = sdi->priv;

	if (!(trigger = sr_session_trigger_get()))
		return SR_OK;

	for (l = trigger->stages; l; l = l->next) {
		stage = l->data;
		for (m = stage->matches; m; m = m->next) {
			match = m->data;
			devc->trigger = 1;
			if (!match->channel->enabled)
				/* Ignore disabled channels with a trigger. */
				continue;
			channel = match->channel->index;
			switch (match->match) {
			case SR_TRIGGER_ZERO:
				g_trigger_status[channel / 4] |= 2 << (channel % 4 * 2);
			case SR_TRIGGER_ONE:
				g_trigger_status[channel / 4] |= 1 << (channel % 4 * 2);
				break;
			default:
				sr_err("Unsupported match %d", match->match);
				return SR_ERR;
			}
		}
	}

	return SR_OK;
}

SR_PRIV void analyzer_add_filter(int channel, int type)
{
	int i;

	if (type != FILTER_HIGH && type != FILTER_LOW)
		return;
	if ((channel & 0xf) >= 8)
		return;

	if (channel & CHANNEL_A)
		i = 0;
	else if (channel & CHANNEL_B)
		i = 2;
	else if (channel & CHANNEL_C)
		i = 4;
	else if (channel & CHANNEL_D)
		i = 6;
	else
		return;

	if ((channel & 0xf) >= 4) {
		i++;
		channel -= 4;
	}

	g_filter_status[i] |=
	    1 << ((2 * channel) + (type == FILTER_LOW ? 1 : 0));

	g_filter_enable = 1;
}

SR_PRIV void analyzer_set_trigger_count(int count)
{
	g_trigger_count = count;
}

SR_PRIV void analyzer_set_freq(int freq, int scale)
{
	g_freq_value = freq;
	g_freq_scale = scale;
}

SR_PRIV void analyzer_set_memory_size(unsigned int size)
{
	g_memory_size = size;
}

SR_PRIV void analyzer_set_ramsize_trigger_address(unsigned int address)
{
	g_ramsize_triggerbar_addr = address;
}

SR_PRIV unsigned int analyzer_get_ramsize_trigger_address(void)
{
	return g_ramsize_triggerbar_addr;
}

SR_PRIV void analyzer_set_triggerbar_address(unsigned int address)
{
	g_triggerbar_addr = address;
}

SR_PRIV unsigned int analyzer_get_triggerbar_address(void)
{
	return g_triggerbar_addr;
}

SR_PRIV unsigned int analyzer_read_status(libusb_device_handle *devh)
{
	return gl_reg_read(devh, DEV_STATUS);
}

SR_PRIV unsigned int analyzer_read_id(libusb_device_handle *devh)
{
	return gl_reg_read(devh, DEV_ID1) << 8 | gl_reg_read(devh, DEV_ID0);
}

SR_PRIV unsigned int analyzer_get_stop_address(libusb_device_handle *devh)
{
	return gl_reg_read(devh, STOP_ADDRESS2) << 16 | gl_reg_read(devh,
			STOP_ADDRESS1) << 8 | gl_reg_read(devh, STOP_ADDRESS0);
}

SR_PRIV unsigned int analyzer_get_now_address(libusb_device_handle *devh)
{
	return gl_reg_read(devh, NOW_ADDRESS2) << 16 | gl_reg_read(devh,
			NOW_ADDRESS1) << 8 | gl_reg_read(devh, NOW_ADDRESS0);
}

SR_PRIV unsigned int analyzer_get_trigger_address(libusb_device_handle *devh)
{
	return gl_reg_read(devh, TRIGGER_ADDRESS2) << 16 | gl_reg_read(devh,
		TRIGGER_ADDRESS1) << 8 | gl_reg_read(devh, TRIGGER_ADDRESS0);
}

SR_PRIV void analyzer_set_compression(unsigned int type)
{
	g_compression = type;
}

SR_PRIV void analyzer_set_voltage_threshold(int thresh)
{
	g_thresh = thresh;
}

SR_PRIV void analyzer_wait_button(libusb_device_handle *devh)
{
	analyzer_wait(devh, STATUS_BUTTON_PRESSED, 0);
}

SR_PRIV void analyzer_wait_data(libusb_device_handle *devh)
{
	analyzer_wait(devh, 0, STATUS_BUSY);
}

SR_PRIV int analyzer_decompress(void *input, unsigned int input_len,
				void *output, unsigned int output_len)
{
	unsigned char *in = input;
	unsigned char *out = output;
	unsigned int A, B, C, count;
	unsigned int written = 0;

	while (input_len > 0) {
		A = *in++;
		B = *in++;
		C = *in++;
		count = (*in++) + 1;

		if (count > output_len)
			count = output_len;
		output_len -= count;
		written += count;

		while (count--) {
			*out++ = A;
			*out++ = B;
			*out++ = C;
			*out++ = 0; /* Channel D */
		}

		input_len -= 4;
		if (output_len == 0)
			break;
	}

	return written;
}
