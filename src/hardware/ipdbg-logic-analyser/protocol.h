/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2016 danselmi <da@da>
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

#ifndef LIBSIGROK_HARDWARE_IPDBG_ORG_LA_PROTOCOL_H
#define LIBSIGROK_HARDWARE_IPDBG_ORG_LA_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "ipdbg-org-la"


struct ipdbg_org_la_tcp {
    char *address;
    char *port;
    int socket;
};

/** Private, per-device-instance driver context. */
struct ipdbg_org_la_dev_context
{
    unsigned int DATA_WIDTH;
    unsigned int DATA_WIDTH_BYTES;
    unsigned int ADDR_WIDTH;
    unsigned int ADDR_WIDTH_BYTES ;

    unsigned int limit_samples;
    unsigned int limit_samples_max;
    char capture_ratio;
    char *trigger_mask;
    char *trigger_value;
    char *trigger_mask_last;
    char *trigger_value_last;
    char *trigger_edge_mask;
    unsigned int delay_value;
    int num_stages; //always 0
    unsigned int num_transfers;
    unsigned char *raw_sample_buf;
};

SR_PRIV struct ipdbg_org_la_tcp *ipdbg_org_la_new_tcp(void);
SR_PRIV int ipdbg_org_la_tcp_open(struct ipdbg_org_la_tcp *tcp);
SR_PRIV int ipdbg_org_la_tcp_close(struct ipdbg_org_la_tcp *tcp);
SR_PRIV void ipdbg_org_la_tcp_free(struct ipdbg_org_la_tcp *tcp);
SR_PRIV int ipdbg_org_la_tcp_send(struct ipdbg_org_la_tcp *tcp, const uint8_t *buf, size_t len);
SR_PRIV int ipdbg_org_la_tcp_receive(struct ipdbg_org_la_tcp *tcp, uint8_t *buf, int bufsize);

SR_PRIV struct ipdbg_org_la_dev_context *ipdbg_org_la_dev_new(void);
SR_PRIV void ipdbg_org_la_get_addrwidth_and_datawidth(struct ipdbg_org_la_tcp *tcp, struct ipdbg_org_la_dev_context *devc);
SR_PRIV int ipdbg_org_la_sendReset(struct ipdbg_org_la_tcp *tcp);
SR_PRIV int ipdbg_org_la_requestID(struct ipdbg_org_la_tcp *tcp);
SR_PRIV int ipdbg_org_la_sendStart(struct ipdbg_org_la_tcp *tcp);
SR_PRIV int ipdbg_org_la_sendTrigger(struct ipdbg_org_la_dev_context *devc, struct ipdbg_org_la_tcp *tcp);
SR_PRIV int ipdbg_org_la_sendDelay(struct ipdbg_org_la_dev_context *devc, struct ipdbg_org_la_tcp *tcp);
SR_PRIV int ipdbg_org_la_convert_trigger(const struct sr_dev_inst *sdi);
SR_PRIV int ipdbg_org_la_receive_data(int fd, int revents, void *cb_data);
SR_PRIV void ipdbg_org_la_abort_acquisition(const struct sr_dev_inst *sdi);

#endif
