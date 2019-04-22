/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2019 Vitaliy Vorobyov
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

#ifndef LIBSIGROK_HARDWARE_SYSCLK_SLA5032_SLA5032_H
#define LIBSIGROK_HARDWARE_SYSCLK_SLA5032_SLA5032_H

#include <stdint.h>
#include <libusb.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>

/** SLA5032 protocol command ID codes. */
enum command_id {
	CMD_INIT_FW_UPLOAD = 1,
	CMD_UPLOAD_FW_CHUNK = 2,
	CMD_READ_REG = 3,
	CMD_WRITE_REG = 4,
	CMD_READ_MEM = 5,
	CMD_READ_DATA = 7,
};

struct sr_usb_dev_inst;

SR_PRIV int sla5032_apply_fpga_config(const struct sr_dev_inst* sdi);
SR_PRIV int sla5032_start_sample(const struct sr_usb_dev_inst *usb);
SR_PRIV int sla5032_get_status(const struct sr_usb_dev_inst *usb, uint32_t status[3]);
SR_PRIV int sla5032_set_read_back(const struct sr_usb_dev_inst *usb);
SR_PRIV int sla5032_read_data_chunk(const struct sr_usb_dev_inst *usb, void *buf, unsigned int len, int *xfer_len);
SR_PRIV int sla5032_set_depth(const struct sr_usb_dev_inst *usb, uint32_t pre, uint32_t post);
SR_PRIV int sla5032_set_triggers(const struct sr_usb_dev_inst *usb,  uint32_t trg_value, uint32_t trg_edge_mask, uint32_t trg_mask);
SR_PRIV int sla5032_set_samplerate(const struct sr_usb_dev_inst *usb, unsigned int sr);
SR_PRIV int sla5032_set_pwm1(const struct sr_usb_dev_inst *usb, uint32_t hi, uint32_t lo);
SR_PRIV int sla5032_set_pwm2(const struct sr_usb_dev_inst* usb, uint32_t hi, uint32_t lo);
SR_PRIV int sla5032_write_reg14_zero(const struct sr_usb_dev_inst* usb);

#endif
