/*
 * This file is part of the sigrok project.
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

#ifndef SIGROK_ZEROPLUS_ANALYZER_H
#define SIGROK_ZEROPLUS_ANALYZER_H

#include <libusb.h>

#define STATUS_FLAG_NONE	0x00
#define STATUS_FLAG_RESET	0x01
#define STATUS_FLAG_INIT	0x02
#define STATUS_FLAG_GO		0x04
#define STATUS_FLAG_PAUSE	0x08
#define STATUS_FLAG_READ	0x10
#define STATUS_FLAG_20		0x20

#define MEMORY_SIZE_8K		0x00
#define MEMORY_SIZE_64K		0x01
#define MEMORY_SIZE_128K	0x02
#define MEMORY_SIZE_512K	0x04

#define STATUS_BUSY		0x01	/* WTF / ??? */
#define STATUS_READY		0x02
#define STATUS_BUTTON_PRESSED	0x04

#define CHANNEL_A		0x1000
#define CHANNEL_B		0x2000
#define CHANNEL_C		0x3000
#define CHANNEL_D		0x4000

#define FREQ_SCALE_HZ		0
#define FREQ_SCALE_KHZ		1
#define FREQ_SCALE_MHZ		2

#define FILTER_HIGH		0
#define FILTER_LOW		1

#define COMPRESSION_NONE	0x0001
#define COMPRESSION_ENABLE	0x8001
#define COMPRESSION_DOUBLE	0x8002

enum {
	TRIGGER_HIGH = 0,
	TRIGGER_LOW,
	TRIGGER_POSEDGE,
	TRIGGER_NEGEDGE,
	TRIGGER_ANYEDGE,
};

void analyzer_set_freq(int freq, int scale);
void analyzer_set_ramsize_trigger_address(unsigned int address);
void analyzer_set_triggerbar_address(unsigned int address);
void analyzer_set_compression(unsigned int type);
void analyzer_set_memory_size(unsigned int size);
void analyzer_add_trigger(int channel, int type);
void analyzer_set_trigger_count(int count);
void analyzer_add_filter(int channel, int type);

unsigned int analyzer_read_id(libusb_device_handle *devh);
unsigned int analyzer_get_stop_address(libusb_device_handle *devh);
unsigned int analyzer_get_now_address(libusb_device_handle *devh);
unsigned int analyzer_get_trigger_address(libusb_device_handle *devh);
int analyzer_decompress(void *input, unsigned int input_len, void *output,
			unsigned int output_len);

void analyzer_reset(libusb_device_handle *devh);
void analyzer_initialize(libusb_device_handle *devh);
void analyzer_wait(libusb_device_handle *devh, int set, int unset);
void analyzer_read_start(libusb_device_handle *devh);
int analyzer_read_data(libusb_device_handle *devh, void *buffer,
		       unsigned int size);
void analyzer_read_stop(libusb_device_handle *devh);
void analyzer_start(libusb_device_handle *devh);
void analyzer_configure(libusb_device_handle *devh);

void analyzer_wait_button(libusb_device_handle *devh);
void analyzer_wait_data(libusb_device_handle *devh);

#endif
