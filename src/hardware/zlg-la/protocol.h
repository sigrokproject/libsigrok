#ifndef LIBSIGROK_HARDWARE_ZLG_LA_PROTOCOL_H
#define LIBSIGROK_HARDWARE_ZLG_LA_PROTOCOL_H

#include <config.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include <libusb.h>
#include <stdint.h>

#include "libsigrok-internal.h"
#include "protocol.h"

#define LOG_PREFIX "zlg-la"

#define CMD_FW_UPLOAD 0x01       /* FE01: Firmware/FPGA bitstream start */
#define CMD_GET_DEVICE_INFO 0x02 /* FD02: Get device info */
#define CMD_COMMIT 0x03          /* FC03: Settings commit */
#define CMD_SET_EXEC 0x05        /* FA05: Sample rate / divider, and Strat / Stop capture */
#define CMD_GET_STATE 0x06       /* F906: Activity check (Trigger Armed) BIT1:BUSY 2:MODE(Trigger1) 3:DONE*/
#define CMD_SET_TRIG 0x07        /* F807: Trigger pattern/mask upload */
#define CMD_GET_BULKIN 0x0c      /* F30C: Activity check (Device Bulk Data, not used) */
#define CMD_GET_SIZE 0x0d        /* F20D: Get captured data size */

#define STATE_IDLE 0x00
#define STATE_CAPTURE 0x01
#define STATE_WAITING 0x02
#define STATE_DESTROY 0x03

#define ZLG_DEFAULT_CAPTURE_RATIO 10

struct zlg_product {
    uint16_t vid;
    uint16_t pid;
    const char *product_name;
    const char *fw_name;
    uint32_t channels;
    uint32_t max_sample_depth; /* In Ksamples/channel */
    uint32_t max_samplerate;   /* In MHz */
};

static const struct zlg_product zlg_products[] = {
    {0x04cc, 0x120e, "la-1016", "Configure1016.dll", 16, 32, 100},
    ALL_ZERO,
};

struct dev_context {
    const struct zlg_product *product;
    uint64_t limit_samples;  // default max samples
    uint64_t capture_ratio;  // default 10%
    uint64_t cur_samplerate; // default max sample rate
};

SR_PRIV int zlg_la_fw_upload(const struct sr_dev_inst *sdi, const char *name);
SR_PRIV int zlg_la_acquisition_start(const struct sr_dev_inst *sdi);
SR_PRIV int zlg_la_receive_data(int fd, int revents, void *cb_data);
SR_PRIV int zlg_la_hardware_stop(const struct sr_dev_inst *sdi);

#endif