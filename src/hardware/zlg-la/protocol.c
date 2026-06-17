#include "protocol.h"

#include <config.h>
#include <fcntl.h>
#include <stdio.h>

#define BINARY_TO_STACK_HEX_STR(data_ptr, size, out_str)             \
    char out_str[((size) * 2) + 1];                                  \
    do {                                                             \
        const uint8_t *src_bytes = (const uint8_t *)(data_ptr);      \
        size_t bytes_len         = (size);                           \
        for (size_t i = 0; i < bytes_len; i++) {                     \
            /* Write 2 hex characters per byte, with zero-padding */ \
            sprintf(&out_str[i * 2], "%02X", src_bytes[i]);          \
        }                                                            \
        out_str[bytes_len * 2] = '\0';                               \
    } while (0)

static int zlg_la_receive_data_done(const struct sr_dev_inst *sdi);

static int zlg_la_cmd_write(const struct sr_dev_inst *sdi, uint8_t cmd_id, uint8_t *payload, size_t len) {
    struct sr_usb_dev_inst *usb = sdi->conn;
    uint8_t cmd[16]             = {0};
    int transferred, ret;

    cmd[0] = cmd_id;
    cmd[1] = (uint8_t)~cmd_id;

    if (payload && len > 0) {
        memcpy(&cmd[2], payload, (len > 14) ? 14 : len);
    }

    BINARY_TO_STACK_HEX_STR(cmd, 16, cmd_str);

    sr_dbg("%s:%d: cmd %s", __func__, __LINE__, cmd_str);

    ret = libusb_interrupt_transfer(usb->devhdl, 0x01, cmd, 16, &transferred, 100);
    if (ret < 0 || transferred != 16) {
        sr_err("Cmd 0x%02x write failed: %s", cmd_id, libusb_error_name(ret));
        return SR_ERR;
    }

    return SR_OK;
}

static int zlg_la_cmd_read(const struct sr_dev_inst *sdi, uint8_t *rsp, int *len) {
    struct sr_usb_dev_inst *usb = sdi->conn;
    int ret;

    ret = libusb_interrupt_transfer(usb->devhdl, 0x81, rsp, 16, len, 100);
    if (ret < 0 || *len == 0) {
        sr_err("Response read failed: %s, transfered:%d", libusb_error_name(ret), *len);
        return SR_ERR;
    }
    return SR_OK;
}

static int zlg_la_cmd(const struct sr_dev_inst *sdi, uint8_t cmd_id, uint8_t *payload, size_t len, uint8_t *rsp,
                      int *rsp_len) {
    int ret;
    ret = zlg_la_cmd_write(sdi, cmd_id, payload, len);
    if (ret != SR_OK) {
        return ret;
    }
    if (rsp && rsp_len) {
        ret = zlg_la_cmd_read(sdi, rsp, rsp_len);
        BINARY_TO_STACK_HEX_STR(rsp, *rsp_len, rsp_str);
        sr_dbg("%s:%d: cmd %02x rsp %s", __func__, __LINE__, cmd_id, rsp_str);
    }

    return ret;
}

/* Replicates sub_100031C0: 100MHz Divider Logic */
static int zlg_la_set_samplerate_ratio(const struct sr_dev_inst *sdi) {
    struct dev_context *devc = sdi->priv;
    uint8_t payload[14]      = {0};
    uint32_t divider         = 0;

    uint32_t ratio = devc->product->max_sample_depth * 1024 * devc->capture_ratio / 100;

    /* Replicating pcap: 05 fa 02 83 04 09 ... divider ratio */
    payload[0] = 0x02;
    payload[1] = 0x83;
    payload[2] = 0x04;
    payload[3] = 0x09;
    WL32(&(payload[10]), ratio);
    if (devc->cur_samplerate != SR_MHZ(devc->product->max_samplerate)) {
        /* Formula: Divider = (100MHz / Rate) - 1 */
        divider    = (uint32_t)(100000000 / devc->cur_samplerate) - 1;
        payload[5] = 0x1; // enable divider
        WL32(&(payload[6]), divider);
    }

    sr_info("Setting samplerate to %" PRIu64 " Hz (Divider: %u)", devc->cur_samplerate, divider);

    if (zlg_la_cmd(sdi, CMD_SET_EXEC, payload, 14, NULL, NULL) != SR_OK) {
        return SR_ERR;
    }

    return SR_OK;
}

static int zlg_la_send_trigger_blocks(const struct sr_dev_inst *sdi, uint8_t *data, int len) {
    struct sr_usb_dev_inst *usb = sdi->conn;
    uint8_t payload[14]         = {0};
    int ret, transferred;

    sr_dbg("Configuring trigger...");

    /* 1. Tell device to expect 32 bytes of trigger data */
    payload[0] = 0x02;
    payload[1] = 0x00;
    payload[2] = 0x06;
    payload[3] = len;

    if (zlg_la_cmd(sdi, CMD_SET_TRIG, payload, 14, NULL, NULL) != SR_OK) {
        return SR_ERR;
    }

    BINARY_TO_STACK_HEX_STR(data, len, trigger_str);
    sr_dbg("trigger: %s", trigger_str);
    ret = libusb_bulk_transfer(usb->devhdl, 0x02, data, len, &transferred, 100);
    if (ret < 0 || transferred != len) {
        sr_err("send trigger failed, ret = %s, transferred = %d/%d", libusb_error_name(ret), transferred, len);
        return SR_ERR;
    }
    return SR_OK;
}
static int zlg_la_set_trigger(const struct sr_dev_inst *sdi) {
    struct sr_trigger *trigger;
    struct sr_trigger_stage *stage;
    struct sr_trigger_match *match;
    GSList *m;
    uint8_t buf[96] = {0};

    uint32_t mask_lvl = 0, value_lvl = 0;
    uint32_t mask_edge = 0, value_edge_start = 0, value_edge_end = 0;
    gboolean has_edge = FALSE;

    if (!(trigger = sr_session_trigger_get(sdi->session))) {
        /* --- CASE 1: IMMEDIATE TRIGGER (Baseline Dump) --- */
        /* Block 1 */
        buf[0]  = 0x01;
        buf[1]  = 0x01;
        buf[2]  = 0x10;
        buf[3]  = 0x10;
        buf[12] = 0x00;
        buf[17] = 0x18;
        /* Block 2 */
        buf[32]      = 0x01;
        buf[32 + 1]  = 0x01;
        buf[32 + 2]  = 0x10;
        buf[32 + 3]  = 0x10;
        buf[32 + 12] = 0x00;
        buf[32 + 17] = 0x10;

        return zlg_la_send_trigger_blocks(sdi, buf, 64);
    }

    /* We only support the first sigrok trigger stage */
    stage = trigger->stages->data;

    for (m = stage->matches; m; m = m->next) {
        match = m->data;
        if (!match->channel->enabled) {
            continue;
        }

        uint32_t bit = (1 << match->channel->index);

        if (match->match == SR_TRIGGER_ONE) {
            mask_lvl |= bit;
            value_lvl |= bit;
        } else if (match->match == SR_TRIGGER_ZERO) {
            mask_lvl |= bit;
        } else if (match->match == SR_TRIGGER_RISING) {
            has_edge = TRUE;
            mask_edge |= bit;
            value_edge_end |= bit; /* Ends High */
        } else if (match->match == SR_TRIGGER_FALLING) {
            has_edge = TRUE;
            mask_edge |= bit;
            value_edge_start |= bit; /* Starts High */
        }
    }

    if (has_edge) {
        /* --- CASE 2: EDGE TRIGGER (3 Stages) --- */
        /* STAGE 1: Look for the "Before" condition */
        buf[0] = 0x01;
        buf[1] = 0x00;
        buf[2] = 0x00;
        buf[3] = 0x00; // Edge Prep mode
        WL32(&buf[4], mask_lvl | mask_edge);
        WL32(&buf[8], value_lvl | value_edge_start);
        buf[12] = 0x03;
        buf[17] = 0x18;

        /* STAGE 2: Transition Condition */
        buf[32]     = 0x02;
        buf[32 + 1] = 0x01;
        buf[32 + 2] = 0x10;
        buf[32 + 3] = 0x00; // Edge Transition mode
        WL32(&buf[32 + 4], mask_lvl | mask_edge);
        WL32(&buf[32 + 8], value_lvl | value_edge_end);
        buf[32 + 12] = 0x03;
        buf[32 + 17] = 0x18;

        /* STAGE 3: Finalizer (Level Match) */
        buf[64]      = 0x02;
        buf[64 + 1]  = 0x02;
        buf[64 + 2]  = 0x10;
        buf[64 + 3]  = 0x10; // Finalize mode
        buf[64 + 12] = 0x03;
        buf[64 + 17] = 0x18;

        return zlg_la_send_trigger_blocks(sdi, buf, 96);

    } else {
        /* --- CASE 3: LEVEL/BUS TRIGGER (2 Stages) --- */
        /* STAGE 1: Compare against pattern */
        buf[0] = 0x01;
        buf[1] = 0x00;
        buf[2] = 0x10;
        buf[3] = 0x00; // Level Match mode
        WL32(&buf[4], mask_lvl);
        WL32(&buf[8], value_lvl);
        buf[12] = 0x03;
        buf[17] = 0x18;

        /* STAGE 2: Terminal State */
        buf[32]      = 0x01;
        buf[32 + 1]  = 0x01;
        buf[32 + 2]  = 0x10;
        buf[32 + 3]  = 0x10; // Sustain mode
        buf[32 + 12] = 0x00;
        buf[32 + 17] = 0x18;

        return zlg_la_send_trigger_blocks(sdi, buf, 64);
    }
}

SR_PRIV int zlg_la_acquisition_start(const struct sr_dev_inst *sdi) {
    int ret             = SR_OK;
    uint8_t payload[16] = {0};
    uint8_t rsp[16]     = {0};
    int rsp_len         = 0;

    // check device state
    ret = zlg_la_cmd(sdi, CMD_GET_DEVICE_INFO, NULL, 0, rsp, &rsp_len);
    if (ret != SR_OK) {
        return ret;
    }
    if (rsp[4] == 0xff || rsp[5] == 0xff) {
        sr_err("Device fw not ready");
        return SR_ERR;
    }

    // check busy
    payload[0] = 0x02;
    payload[1] = 0x80;
    payload[2] = 0x04;
    payload[3] = 0x02;
    ret        = zlg_la_cmd(sdi, CMD_GET_STATE, payload, 4, rsp, &rsp_len);
    if (ret != SR_OK || (rsp[0] & 0x02)) {
        sr_err("Device not idle");
        return SR_ERR;
    }

    // set trigger
    ret = zlg_la_set_trigger(sdi);
    if (ret != SR_OK) {
        return ret;
    }

    // check device state
    ret = zlg_la_cmd(sdi, CMD_GET_DEVICE_INFO, NULL, 0, rsp, &rsp_len);
    if (ret != SR_OK) {
        return ret;
    }
    if (rsp[4] == 0xff || rsp[5] == 0xff) {
        sr_err("Device fw not ready");
        return SR_ERR;
    }

    // set freq
    ret = zlg_la_set_samplerate_ratio(sdi);
    if (ret != SR_OK) {
        return ret;
    }

    // commit
    payload[0] = 0xff;
    payload[1] = 0xff;
    payload[2] = 0xff;
    payload[3] = 0xff;
    ret        = zlg_la_cmd(sdi, CMD_COMMIT, payload, 4, rsp, &rsp_len);
    if (ret != SR_OK) {
        return ret;
    }
    if (rsp[0] != 0x03 || rsp[1] != 0xfc) {
        sr_err("Send commit command failed");
        return SR_ERR;
    }

    // start capture
    // 028000010001, 028000010000, 028000010002
    payload[0] = 0x02;
    payload[1] = 0x80;
    payload[2] = 0x00;
    payload[3] = 0x01;
    payload[4] = 0x00;
    for (int i = 0; i < 3; i++) {
        switch (i) {
            case 0:
                payload[5] = 0x01;
                break;
            case 1:
                payload[5] = 0x00;
                break;
            case 2:
                payload[5] = 0x02;
                break;
        }
        ret = zlg_la_cmd(sdi, CMD_SET_EXEC, payload, 6, NULL, NULL);
        if (ret != SR_OK) {
            return ret;
        }
    }

    return SR_OK;
}

static uint8_t sink_buffer[0xff];
SR_PRIV int zlg_la_receive_data(int fd, int revents, void *cb_data) {
    (void)fd;
    (void)revents;
    struct sr_dev_inst *sdi     = cb_data;
    struct sr_usb_dev_inst *usb = sdi->conn;
    int ret                     = SR_OK;
    uint8_t payload[16]         = {0};
    uint8_t rsp[16]             = {0};
    uint8_t status;
    int rsp_len = 0;

    // check state
    payload[0] = 0x02;
    payload[1] = 0x80;
    payload[2] = 0x04;
    payload[3] = 0x02;
    ret        = zlg_la_cmd(sdi, CMD_GET_STATE, payload, 4, rsp, &rsp_len);
    if (ret != SR_OK) {
        return TRUE;
    }
    status = rsp[0];

    // peek the data
    ret = zlg_la_cmd(sdi, CMD_GET_BULKIN, NULL, 0, rsp, &rsp_len);
    if (ret != SR_OK) {
        return TRUE;
    }
    if (rsp[2] != 0) {
        // drain the data
        ret = libusb_bulk_transfer(usb->devhdl, 0x82, sink_buffer, 0xff, &rsp_len, 100);
        sr_spew("cmd 0cf3 peek channels: %02x %02x", sink_buffer[0], sink_buffer[1]);
        if (ret != SR_OK) {
            return TRUE;
        }
    }

    if (!(status & 0x02) && (status & 0x08)) {
        // trigger armed
        ret = zlg_la_receive_data_done(sdi);
        std_session_send_df_end(sdi);
        return FALSE;
    }

    return TRUE;
}

/* Process the raw 98312 bytes logic data */
static int zlg_la_receive_data_done(const struct sr_dev_inst *sdi) {
    struct dev_context *devc    = sdi->priv;
    struct sr_usb_dev_inst *usb = sdi->conn;
    struct sr_datafeed_packet packet;
    struct sr_datafeed_logic logic;
    int ret             = SR_OK;
    int transferred     = 0;
    uint8_t payload[16] = {0};
    uint8_t rsp[16]     = {0};
    int rsp_len         = 0;
    uint8_t *in_buffer  = NULL; /* Initialize to NULL to avoid freeing wild pointers */
    uint8_t *out_buffer = NULL; /* Initialize to NULL */
    // uint32_t samples_before_trigger;
    // uint32_t start_point;
    uint32_t trigger_point;
    gboolean is_trigger = FALSE;

    // stop capture 05fa 02800001
    payload[0] = 0x02;
    payload[1] = 0x80;
    payload[2] = 0x00;
    payload[3] = 0x01;
    ret        = zlg_la_cmd(sdi, CMD_SET_EXEC, payload, 4, NULL, NULL);
    if (ret != SR_OK) {
        return ret;
    }

    // get data size 0df2 0609010101
    payload[0] = 0x06;
    payload[1] = 0x09;
    payload[2] = 0x01;
    payload[3] = 0x01;
    payload[4] = 0x01;
    ret        = zlg_la_cmd(sdi, CMD_GET_SIZE, payload, 5, rsp, &rsp_len);
    if (ret != SR_OK) {
        return ret;
    }

    uint32_t data_len = RL32(&rsp[2]);
    if (data_len == 0) {
        return SR_ERR_BUG;
    }

    sr_dbg("Receiving %u bytes of data...", data_len);

    in_buffer = g_malloc(data_len);

    /* Blocking read for now (TODO: Change to async later) */
    uint32_t total_transferred = 0;
    while (total_transferred < data_len) {
        ret = libusb_bulk_transfer(usb->devhdl, 0x82, in_buffer, data_len, &transferred, 2000);
        if (ret < 0) {
            sr_err("Receive failed: %s, transferred: %d", libusb_error_name(ret), transferred);
            /* Go to exit sequence to safely clean up the allocated in_buffer */
            ret = SR_ERR;
            goto cleanup;
        }
        total_transferred += transferred;
    }

    // get mode
    payload[0] = 0x02;
    payload[1] = 0x80;
    payload[2] = 0x00;
    payload[3] = 0x01;
    ret        = zlg_la_cmd(sdi, CMD_GET_STATE, payload, 4, rsp, &rsp_len);
    if (ret != SR_OK || rsp_len != 1) {
        ret = SR_ERR;
        goto cleanup;
    }
    if (rsp[0] & 0x04) {
        is_trigger = TRUE;
    }

    // get position
    payload[0] = 0x02;
    payload[1] = 0x88;
    payload[2] = 0x04;
    payload[3] = 0x0c;
    ret        = zlg_la_cmd(sdi, CMD_GET_STATE, payload, 4, rsp, &rsp_len);
    if (ret != SR_OK || rsp_len != 12) {
        ret = SR_ERR;
        goto cleanup;
    }

    // samples_before_trigger = RL32(rsp);
    trigger_point = RL32(rsp + 4);
    // start_point   = RL32(rsp + 8);

    // clean up device state
    ret = zlg_la_cmd(sdi, CMD_GET_DEVICE_INFO, NULL, 0, rsp, &rsp_len);
    if (ret != SR_OK) {
        ret = SR_ERR;
        goto cleanup;
    }

    sr_dbg("Received %u bytes of data", transferred);

    uint32_t max_samples           = devc->product->max_sample_depth * 1024;
    uint32_t scaled_before_samples = (devc->capture_ratio * devc->limit_samples) / 100;
    uint32_t scaled_after_samples  = ((100 - devc->capture_ratio) * devc->limit_samples) / 100;
    if (devc->limit_samples == max_samples) {
        // workaround, hw write more data than reported, remove from head and tail
        scaled_before_samples -= 8;
        scaled_after_samples -= 8;
    }
    uint32_t scaled_start_point = trigger_point - scaled_before_samples;

    if (scaled_before_samples > trigger_point) {
        scaled_start_point = max_samples - (scaled_before_samples - trigger_point);
    }
    if (!is_trigger) {
        // remove data before trigger point or non-trigger
        scaled_start_point = trigger_point;
    }

    uint32_t scaled_end_point = trigger_point + scaled_after_samples;
    if (scaled_end_point >= max_samples) {
        scaled_end_point -= max_samples;
    }

    sr_dbg("trigger_point=0x%04x, samples=0x%04lx/0x%04x, start=0x%04x, end=0x%04x", trigger_point, devc->limit_samples,
           max_samples, scaled_start_point, scaled_end_point);

    /* Allocate output logic buffer securely */
    size_t out_buffer_size = devc->limit_samples * 2; /* 16 channels = 2 bytes per sample */
    out_buffer             = g_malloc(out_buffer_size);

    uint32_t index              = 0;
    uint32_t out_buffer_idx     = 0;
    uint32_t pre_trigger_length = 0;
    for (uint32_t i = 0; i < devc->limit_samples; i++) {
        index = scaled_start_point + i;
        if (index >= max_samples) {
            index -= max_samples;
        }

        // Done
        if (index == scaled_end_point) {
            break;
        }

        /* Bounds protection: guarantee we do not read past USB transferred payload size */
        if ((index * 3) + 1 >= (uint32_t)transferred) {
            sr_err("Hardware ring buffer index parsing overflowed actual transferred USB length!");
            ret = SR_ERR_DATA;
            goto cleanup;
        }

        if (is_trigger && index == trigger_point && out_buffer_idx > 0) {
            /* Send samples tracked before trigger point */
            packet.type    = SR_DF_LOGIC;
            packet.payload = &logic;
            logic.length   = out_buffer_idx;
            logic.unitsize = 2;
            logic.data     = out_buffer;
            sr_session_send(sdi, &packet);

            /* Send trigger marker packet */
            packet.type    = SR_DF_TRIGGER;
            packet.payload = NULL;
            sr_session_send(sdi, &packet);

            pre_trigger_length = out_buffer_idx;
        }

        /* Bounds check protection for out_buffer writes */
        if (out_buffer_idx >= out_buffer_size) {
            sr_err("Output buffer write out of bounds structural error!");
            ret = SR_ERR_BUG;
            goto cleanup;
        }

        out_buffer[out_buffer_idx]     = in_buffer[index * 3];
        out_buffer[out_buffer_idx + 1] = in_buffer[index * 3 + 1];
        out_buffer_idx += 2;
    }

    /* Send remaining post-trigger samples payload */
    uint32_t post_trigger_length = out_buffer_idx - pre_trigger_length;
    if (post_trigger_length > 0) {
        packet.type    = SR_DF_LOGIC;
        packet.payload = &logic;
        logic.length   = post_trigger_length;
        logic.unitsize = 2;
        logic.data     = out_buffer + pre_trigger_length;
        sr_session_send(sdi, &packet);
    }

    ret = SR_OK;

cleanup:
    g_free(in_buffer);
    g_free(out_buffer);
    return ret;
}

SR_PRIV int zlg_la_hardware_stop(const struct sr_dev_inst *sdi) {
    return zlg_la_receive_data_done(sdi);
}

SR_PRIV int zlg_la_fw_upload(const struct sr_dev_inst *sdi, const char *name) {
    struct drv_context *drvc    = sdi->driver->context;
    struct sr_usb_dev_inst *usb = sdi->conn;
    struct sr_resource bitstream;
    uint8_t payload[4];
    uint8_t rsp[16] = {0};
    int rsp_len;
    uint8_t buffer[512];
    int transferred, ret;
    size_t size, offset, read_bytes;

    // check device state
    ret = zlg_la_cmd(sdi, CMD_GET_DEVICE_INFO, NULL, 0, rsp, &rsp_len);
    if (ret != SR_OK) {
        return ret;
    }
    if (rsp[4] != 0xff && rsp[5] != 0xff) {
        sr_err("FW exists, skip uploading");
        return SR_OK;
    }

    sr_info("Uploading firmware '%s'...", name);

    /* 1. Open the firmware file using sigrok's resource loader */
    ret = sr_resource_open(drvc->sr_ctx, &bitstream, SR_RESOURCE_FIRMWARE, name);
    if (ret != SR_OK) {
        sr_err("Could not find firmware %s. Place it in ~/.local/share/sigrok-firmware/", name);
        return SR_ERR;
    }

    size = bitstream.size;

    /* 2. Send CMD_FW_UPLOAD (0x01 / FE01) with the size as payload */
    /* Payload is 4 bytes, Little Endian */
    WL32(payload, size);
    if (zlg_la_cmd(sdi, CMD_FW_UPLOAD, payload, 4, rsp, &rsp_len) != SR_OK) {
        sr_resource_close(drvc->sr_ctx, &bitstream);
        return SR_ERR;
    }
    if (RL16(rsp) != 0xFE01) {
        sr_err("Hardware rejected firmware upload command, 0x%x 0x%x", rsp[0], rsp[1]);
        sr_resource_close(drvc->sr_ctx, &bitstream);
        return SR_ERR;
    }

    /* 3. Stream the data in 512-byte blocks to Endpoint 0x02 */
    offset = 0;
    while (offset < size) {
        size_t chunk_size = MIN(size - offset, 512);
        read_bytes        = sr_resource_read(drvc->sr_ctx, &bitstream, buffer, chunk_size);
        if (read_bytes != chunk_size) {
            sr_err("sr_resource_read failed %d", ret);
            break;
        }

        ret = libusb_bulk_transfer(usb->devhdl, 0x02, buffer, chunk_size, &transferred, 1000);
        if (ret < 0) {
            sr_err("Firmware transfer failed at offset %zu: %s", offset, libusb_error_name(ret));
            break;
        }
        offset += transferred;
        sr_spew("Uploaded %zu/%zu bytes", offset, size);
    }

    sr_resource_close(drvc->sr_ctx, &bitstream);

    if (offset < size) {
        return SR_ERR;
    }

    sr_info("Firmware upload complete.");
    return SR_OK;
}