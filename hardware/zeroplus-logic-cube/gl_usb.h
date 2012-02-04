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

#ifndef LIBSIGROK_HARDWARE_ZEROPLUS_LOGIC_CUBE_GL_USB_H
#define LIBSIGROK_HARDWARE_ZEROPLUS_LOGIC_CUBE_GL_USB_H

#include <libusb.h>
#include "sigrok.h"

SR_PRIV int gl_read_bulk(libusb_device_handle *devh, void *buffer,
			 unsigned int size);
SR_PRIV int gl_reg_write(libusb_device_handle *devh, unsigned int reg,
			 unsigned int val);
SR_PRIV int gl_reg_read(libusb_device_handle *devh, unsigned int reg);

#endif
