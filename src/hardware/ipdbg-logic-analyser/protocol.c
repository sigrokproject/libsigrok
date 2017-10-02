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

#include <config.h>
#ifdef _WIN32
#define _WIN32_WINNT 0x0501
#include <winsock2.h>
#include <ws2tcpip.h>
#endif
//#include <glib.h>
#include <string.h>
#include <unistd.h>
#ifndef _WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif
#include <errno.h>
#include "protocol.h"

#include <sys/ioctl.h>

#define BUFFER_SIZE 4


#define Start                      0xFE
#define reset                      0xEE
#define IPDBG_LA_ID                0xBB
#define Escape                     0x55


/* Command opcodes */
#define set_trigger                0x00

#define Trigger                    0xF0
#define LA                         0x0F

#define Masks                      0xF1
#define Mask                       0xF3

#define Value                      0xF7

#define Last_Masks                 0xF9
#define Mask_last                  0xFB

#define Value_last                 0xFF

#define Select_Edge_Mask           0xF5
#define Set_Edge_Mask              0xF6

#define delay                      0x1F
#define K_Mauslesen                0xAA

int hasData(struct ipdbg_org_la_tcp *tcp)
{
#ifdef __WIN32__
    ioctlsocket(tcp->socket,FIONREAD,&bytes_available);
#else
    //ioctl(fd,FIONREAD,&bytes_available);
    int status;

    //fd = open("/dev/ttyS0", O_RDONLY);
    if (ioctl(tcp->socket, FIONREAD, &status) < 0) //TIOCMGET
    {
           sr_err("FIONREAD failed: %s\n",
             strerror(errno));
            return 0;
    }
    else
    {
        if (status < 1)
        {
            return 0;
        }
        else
        {
            return 1;
        }
    }

#endif // __WIN32__
}


SR_PRIV int sendEscaping(struct ipdbg_org_la_tcp *tcp, char *dataToSend, int length);

SR_PRIV struct ipdbg_org_la_tcp *ipdbg_org_la_new_tcp(void)
{
    struct ipdbg_org_la_tcp *tcp;

    tcp = g_malloc0(sizeof(struct ipdbg_org_la_tcp));

    tcp->address = NULL;
    tcp->port = NULL;
    tcp->socket = -1;

    return tcp;
}

SR_PRIV int ipdbg_org_la_tcp_open(struct ipdbg_org_la_tcp *tcp)
{
    struct addrinfo hints;
    struct addrinfo *results, *res;
    int err;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    err = getaddrinfo(tcp->address, tcp->port, &hints, &results);

    if (err) {
        sr_err("Address lookup failed: %s:%s: %s", tcp->address, tcp->port,
            gai_strerror(err));
        return SR_ERR;
    }

    for (res = results; res; res = res->ai_next) {
        if ((tcp->socket = socket(res->ai_family, res->ai_socktype,
                        res->ai_protocol)) < 0)
            continue;
        if (connect(tcp->socket, res->ai_addr, res->ai_addrlen) != 0) {
            close(tcp->socket);
            tcp->socket = -1;
            continue;
        }
        break;
    }

    freeaddrinfo(results);

    if (tcp->socket < 0) {
        sr_err("Failed to connect to %s:%s: %s", tcp->address, tcp->port,
                g_strerror(errno));
        return SR_ERR;
    }

    return SR_OK;
}

SR_PRIV int ipdbg_org_la_tcp_send(struct ipdbg_org_la_tcp *tcp, const uint8_t *buf, size_t len)
{
    int out;
    out = send(tcp->socket, (char*)buf, len, 0);

    if (out < 0) {
        sr_err("Send error: %s", g_strerror(errno));
        return SR_ERR;
    }

    if ((unsigned int)out < len) {
        sr_dbg("Only sent %d/%d bytes of data.", out, (int)len);
    }

    return SR_OK;
}

SR_PRIV int ipdbg_org_la_tcp_receive_blocking(struct ipdbg_org_la_tcp *tcp, uint8_t *buf, int bufsize)
{
    int received = 0;
    while (received < bufsize)
    {
        int valid = ipdbg_org_la_tcp_receive(tcp, buf);
        if(valid >0)
        {
            ++buf;
            ++received;
        }
    }
    return received;
}
SR_PRIV int ipdbg_org_la_tcp_receive(struct ipdbg_org_la_tcp *tcp, uint8_t *buf)
{
    int received = 0;

    if (hasData(tcp) == 1)
    {
        while(received < 1)
        {
            int len = recv(tcp->socket, buf, 1, 0);

            if (len < 0)
            {
                sr_err("Receive error: %s", g_strerror(errno));
                return SR_ERR;
            }
            else
            {
                received += len;
            }
        }
        return received;

    }
    else
    {
        return -1;
    }

}
SR_PRIV int ipdbg_org_la_tcp_close(struct ipdbg_org_la_tcp *tcp)
{
    int ret = SR_ERR;
    if (close(tcp->socket) >= 0)
        ret = SR_OK;

    tcp->socket = -1;

    return ret;
}

SR_PRIV void ipdbg_org_la_tcp_free(struct ipdbg_org_la_tcp *tcp)
{
    g_free(tcp->address);
    g_free(tcp->port);
}

SR_PRIV int ipdbg_org_la_convert_trigger(const struct sr_dev_inst *sdi)
{
    struct ipdbg_org_la_dev_context *devc;
    struct sr_trigger *trigger;
    struct sr_trigger_stage *stage;
    struct sr_trigger_match *match;
    const GSList *l, *m;

    devc = sdi->priv;

    devc->num_stages = 0;
    devc->num_transfers = 0;
    devc->raw_sample_buf = NULL; /// name convert_trigger to init acquisition...
    for (unsigned int i = 0; i < devc->DATA_WIDTH_BYTES; i++)
    {
        devc->trigger_mask[i] = 0;
        devc->trigger_value[i] = 0;
        devc->trigger_mask_last[i] = 0;
        devc->trigger_value_last[i] = 0;
        devc->trigger_edge_mask[i] = 0;
    }

    if (!(trigger = sr_session_trigger_get(sdi->session)))
    {
        return SR_OK;
    }

    for (l = trigger->stages; l; l = l->next)
    {
            stage = l->data;
        for (m = stage->matches; m; m = m->next)
        {
            match = m->data;
            unsigned int byteIndex = (match->channel->index) /8;
            unsigned char matchPattern = 1 << (match->channel->index - 8* byteIndex);

            if (!match->channel->enabled)
                /* Ignore disabled channels with a trigger. */
                continue;
            if (match->match == SR_TRIGGER_ONE )
            {
                devc->trigger_value[byteIndex] |= matchPattern;
                devc->trigger_mask[byteIndex] |= matchPattern;
                devc->trigger_mask_last[byteIndex] &= ~matchPattern;
                devc->trigger_edge_mask[byteIndex] &= ~matchPattern;
                //sr_err("\n========ONE MASK===========");

            }
            else if (match->match == SR_TRIGGER_ZERO)
            {
                devc->trigger_value[byteIndex] &= ~matchPattern;
                devc->trigger_mask[byteIndex] |= matchPattern;
                devc->trigger_mask_last[byteIndex] &= ~matchPattern;
                devc->trigger_edge_mask[byteIndex] &= ~matchPattern;
                //sr_err("\n========ZERO MASK===========");
            }
            else if ( match->match == SR_TRIGGER_RISING)
            {
                devc->trigger_value[byteIndex] |= matchPattern;
                devc->trigger_value_last[byteIndex] &= ~matchPattern;
                devc->trigger_mask[byteIndex] |= matchPattern;
                devc->trigger_mask_last[byteIndex] |= matchPattern;
                devc->trigger_edge_mask[byteIndex] &= ~matchPattern;
                //sr_err("\n==========RISING===========");

            }
            else if (match->match == SR_TRIGGER_FALLING )
            {
                devc->trigger_value[byteIndex] &= ~matchPattern;
                devc->trigger_value_last[byteIndex] |= matchPattern;
                devc->trigger_mask[byteIndex] |= matchPattern;
                devc->trigger_mask_last[byteIndex] |= matchPattern;
                devc->trigger_edge_mask[byteIndex] &= ~matchPattern;
                //sr_err("\n========FALlING===========");
            }
            else if (match->match == SR_TRIGGER_EDGE)
            {
                devc->trigger_mask[byteIndex] &= ~matchPattern;
                devc->trigger_mask_last[byteIndex] &= ~matchPattern;
                devc->trigger_edge_mask[byteIndex] |= matchPattern;
            }

        }

    }

    return SR_OK;
}

SR_PRIV int ipdbg_org_la_receive_data(int fd, int revents, void *cb_data)
{


    const struct sr_dev_inst *sdi;
    struct ipdbg_org_la_dev_context *devc;



    (void)fd;
    (void)revents;

    sdi = (const struct sr_dev_inst *)cb_data;
    if (!sdi)
    {
        return FALSE;
    }

    if (!(devc = sdi->priv))
    {
        return FALSE;

    }


    struct ipdbg_org_la_tcp *tcp = sdi->conn;
    struct sr_datafeed_packet packet;
    struct sr_datafeed_logic logic;


    if (!devc->raw_sample_buf)
    {
        devc->raw_sample_buf = g_try_malloc(devc->limit_samples*devc->DATA_WIDTH_BYTES);
        if (!devc->raw_sample_buf) {
            sr_warn("Sample buffer malloc failed.");
            return FALSE;
        }

    }


    if (devc->num_transfers < devc->limit_samples_max*devc->DATA_WIDTH_BYTES)
    {
        unsigned char byte;

        if (ipdbg_org_la_tcp_receive(tcp, &byte) == 1)
        {
            if(devc->num_transfers < devc->limit_samples*devc->DATA_WIDTH_BYTES)
                devc->raw_sample_buf[devc->num_transfers] = byte;

            devc->num_transfers++;
        }

    }
    else
    {

        if (devc->delay_value > 0) {
            /* There are pre-trigger samples, send those first. */
            packet.type = SR_DF_LOGIC;
            packet.payload = &logic;
            //logic.length = devc->delay_value-1;
            logic.length = devc->delay_value*devc->DATA_WIDTH_BYTES;
            logic.unitsize = devc->DATA_WIDTH_BYTES;
            logic.data = devc->raw_sample_buf;
            sr_session_send(cb_data, &packet);
        }

        /* Send the trigger. */
        packet.type = SR_DF_TRIGGER;
        sr_session_send(cb_data, &packet);

        /* Send post-trigger samples. */
        packet.type = SR_DF_LOGIC;
        packet.payload = &logic;
        //logic.length = devc->limit_samples - devc->delay_value+1;
        logic.length = (devc->limit_samples - devc->delay_value)*devc->DATA_WIDTH_BYTES;
        logic.unitsize = devc->DATA_WIDTH_BYTES;
        logic.data = devc->raw_sample_buf + devc->delay_value*devc->DATA_WIDTH_BYTES;
        //logic.data = devc->raw_sample_buf + devc->delay_value-1;
        sr_session_send(cb_data, &packet);

        g_free(devc->raw_sample_buf);
        devc->raw_sample_buf = NULL;

        //serial_flush(serial);
        ipdbg_org_la_abort_acquisition(sdi);
    }

    return TRUE;
}

SR_PRIV int ipdbg_org_la_sendDelay(struct ipdbg_org_la_dev_context *devc, struct ipdbg_org_la_tcp *tcp)
{

    int maxSample;

    maxSample = devc->limit_samples;

    devc->delay_value = (maxSample/100.0) * devc->capture_ratio;
    uint8_t Befehl[1];
    Befehl[0] = LA;
    ipdbg_org_la_tcp_send(tcp, Befehl, 1);
    Befehl[0] = delay;
    ipdbg_org_la_tcp_send(tcp, Befehl, 1);

    //sr_warn("delay 2");


    char buf[4] = { devc->delay_value        & 0x000000ff,
                   (devc->delay_value >>  8) & 0x000000ff,
                   (devc->delay_value >> 16) & 0x000000ff,
                   (devc->delay_value >> 24) & 0x000000ff};

    for(size_t i = 0 ; i < devc->ADDR_WIDTH_BYTES ; ++i)
        sendEscaping(tcp, &(buf[devc->ADDR_WIDTH_BYTES-1-i]), 1);

    return SR_OK;
}

SR_PRIV int ipdbg_org_la_sendTrigger(struct ipdbg_org_la_dev_context *devc, struct ipdbg_org_la_tcp *tcp)
{
    /////////////////////////////////////////////Mask////////////////////////////////////////////////////////////
    uint8_t buf[1];
    buf[0] = Trigger;
    ipdbg_org_la_tcp_send(tcp, buf, 1);
    buf[0] = Masks;
    ipdbg_org_la_tcp_send(tcp, buf, 1);
    buf[0] = Mask;
    ipdbg_org_la_tcp_send(tcp, buf, 1);

    for(size_t k = 0 ; k < devc->DATA_WIDTH_BYTES ; ++k)
        sendEscaping(tcp, devc->trigger_mask + devc->DATA_WIDTH_BYTES -1-k, 1);


     /////////////////////////////////////////////Value////////////////////////////////////////////////////////////
    buf[0]= Trigger;
    ipdbg_org_la_tcp_send(tcp, buf, 1);
    buf[0] = Masks;
    ipdbg_org_la_tcp_send(tcp, buf, 1);
    buf[0] = Value;
    ipdbg_org_la_tcp_send(tcp, buf, 1);


    for(size_t k = 0 ; k < devc->DATA_WIDTH_BYTES ; ++k)
        sendEscaping(tcp, devc->trigger_value + devc->DATA_WIDTH_BYTES -1-k, 1);


    /////////////////////////////////////////////Mask_last////////////////////////////////////////////////////////////
    buf[0] = Trigger;
    ipdbg_org_la_tcp_send(tcp, buf, 1);
    buf[0] = Last_Masks;
    ipdbg_org_la_tcp_send(tcp, buf, 1);
    buf[0] = Mask_last;
    ipdbg_org_la_tcp_send(tcp, buf, 1);


    for(size_t k = 0 ; k < devc->DATA_WIDTH_BYTES ; ++k)
        sendEscaping(tcp, devc->trigger_mask_last + devc->DATA_WIDTH_BYTES -1-k, 1);


    /////////////////////////////////////////////Value_last////////////////////////////////////////////////////////////
    buf[0] = Trigger;
    ipdbg_org_la_tcp_send(tcp, buf, 1);
    buf[0]= Last_Masks;
    ipdbg_org_la_tcp_send(tcp, buf, 1);
    buf[0]= Value_last;
    ipdbg_org_la_tcp_send(tcp, buf, 1);


    for(size_t k = 0 ; k < devc->DATA_WIDTH_BYTES ; ++k)
        sendEscaping(tcp, devc->trigger_value_last + devc->DATA_WIDTH_BYTES -1-k, 1);

    /////////////////////////////////////////////edge_mask////////////////////////////////////////////////////////////
    buf[0] = Trigger;
    ipdbg_org_la_tcp_send(tcp, buf, 1);
    buf[0] = Select_Edge_Mask;
    ipdbg_org_la_tcp_send(tcp, buf, 1);
    buf[0] = Set_Edge_Mask;
    ipdbg_org_la_tcp_send(tcp, buf, 1);

    for(size_t k = 0 ; k < devc->DATA_WIDTH_BYTES ; ++k)
        sendEscaping(tcp, devc->trigger_edge_mask + devc->DATA_WIDTH_BYTES -1-k, 1);



    return SR_OK;
}

SR_PRIV int sendEscaping(struct ipdbg_org_la_tcp *tcp, char *dataToSend, int length)
{
    while(length--)
    {
        uint8_t payload = *dataToSend++;

        if ( payload == (uint8_t)reset )
        {
            uint8_t escapeSymbol = Escape;
            sr_warn("Escape");

            if(ipdbg_org_la_tcp_send(tcp, &escapeSymbol, 1) != SR_OK)
                sr_warn("can't send escape");


        }

        if ( payload == (char)Escape )
        {
            uint8_t escapeSymbol = Escape;
            sr_warn("Escape");

            if(ipdbg_org_la_tcp_send(tcp, &escapeSymbol, 1) != SR_OK)
                sr_warn("can't send escape");
        }

        if (ipdbg_org_la_tcp_send(tcp, &payload, 1) != SR_OK)
        {
            sr_warn("Can't send data");
        }

    }
    return SR_OK;
}

SR_PRIV void ipdbg_org_la_get_addrwidth_and_datawidth(struct ipdbg_org_la_tcp *tcp, struct ipdbg_org_la_dev_context *devc)
{
    uint8_t buf[8];
    uint8_t auslesen[1];
    auslesen[0]= K_Mauslesen;

    if(ipdbg_org_la_tcp_send(tcp, auslesen, 1) != SR_OK)
        sr_warn("Can't send K_Mauslesen");

    if (ipdbg_org_la_tcp_receive_blocking(tcp, buf,8)!=8)
        sr_warn("getAddrAndDataWidth failed");


    devc->DATA_WIDTH  =  buf[0]        & 0x000000FF;
    devc->DATA_WIDTH |= (buf[1] <<  8) & 0x0000FF00;
    devc->DATA_WIDTH |= (buf[2] << 16) & 0x00FF0000;
    devc->DATA_WIDTH |= (buf[3] << 24) & 0xFF000000;

    devc->ADDR_WIDTH  =  buf[4]        & 0x000000FF;
    devc->ADDR_WIDTH |= (buf[5] <<  8) & 0x0000FF00;
    devc->ADDR_WIDTH |= (buf[6] << 16) & 0x00FF0000;
    devc->ADDR_WIDTH |= (buf[7] << 24) & 0xFF000000;


    int HOST_WORD_SIZE = 8; // bits/ word

    devc->DATA_WIDTH_BYTES = (devc->DATA_WIDTH+HOST_WORD_SIZE -1)/HOST_WORD_SIZE;
    devc->ADDR_WIDTH_BYTES = (devc->ADDR_WIDTH+HOST_WORD_SIZE -1)/HOST_WORD_SIZE;
    devc->limit_samples_max = (0x01 << devc->ADDR_WIDTH);
    devc->limit_samples = devc->limit_samples_max;



    devc->trigger_mask       = g_malloc0(devc->DATA_WIDTH_BYTES);
    devc->trigger_value      = g_malloc0(devc->DATA_WIDTH_BYTES);
    devc->trigger_mask_last  = g_malloc0(devc->DATA_WIDTH_BYTES);
    devc->trigger_value_last = g_malloc0(devc->DATA_WIDTH_BYTES);
    devc->trigger_edge_mask  = g_malloc0(devc->DATA_WIDTH_BYTES);


}

SR_PRIV struct ipdbg_org_la_dev_context *ipdbg_org_la_dev_new(void)
{
    struct ipdbg_org_la_dev_context *devc;

    devc = g_malloc0(sizeof(struct ipdbg_org_la_dev_context));


    devc->capture_ratio = 50;
    ///devc->num_bytes = 0;

    return devc;
}

SR_PRIV int ipdbg_org_la_sendReset(struct ipdbg_org_la_tcp *tcp)
{
    uint8_t buf[1];
    buf[0]= reset;
    if(ipdbg_org_la_tcp_send(tcp, buf, 1) != SR_OK)
        sr_warn("Reset can't send");
    return SR_OK;
}

SR_PRIV int ipdbg_org_la_requestID(struct ipdbg_org_la_tcp *tcp)
{
    uint8_t buf[1];
    buf[0]= IPDBG_LA_ID;
    if(ipdbg_org_la_tcp_send(tcp, buf, 1) != SR_OK)
        sr_warn("IDBG can't send");

    char ID[4];
    if(ipdbg_org_la_tcp_receive_blocking(tcp, (uint8_t*)ID, 4) != 4)
    {
        sr_warn("IDBG can't read");
    }


    if (strncmp(ID, "IDBG", 4)) {
        sr_err("Invalid reply (expected 'IDBG' '%c%c%c%c').", ID[0], ID[1], ID[2], ID[3]);
        return SR_ERR;
    }

    return SR_OK;
}

SR_PRIV void ipdbg_org_la_abort_acquisition(const struct sr_dev_inst *sdi)
{

    struct ipdbg_org_la_tcp *tcp = sdi->conn;

    sr_session_source_remove(sdi->session, tcp->socket);

    std_session_send_df_end(sdi);
}

SR_PRIV int ipdbg_org_la_sendStart(struct ipdbg_org_la_tcp *tcp)
{
    uint8_t buf[1];
    buf[0] = Start;

   if(ipdbg_org_la_tcp_send(tcp, buf, 1) != SR_OK)
        sr_warn("Reset can't send");
    return SR_OK;
}

