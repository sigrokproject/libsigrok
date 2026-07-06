/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2010 Uwe Hermann <uwe@hermann-uwe.de>
 * Copyright (C) 2011 Olivier Fauchon <olivier@aixmarseille.com>
 * Copyright (C) 2012 Alexandru Gagniuc <mr.nuke.me@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include "pxlogic.h"
#include <assert.h>
#include <errno.h>
#include <math.h>

#define IGNORE_RESULT(x) do { if (x) {} } while(0)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <inttypes.h>
#include <unistd.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>

#define pipe(fds) _pipe(fds, 4096, _O_BINARY)
#endif

uint32_t BUFSIZE = 1024 * 1024 * 1;
#define DSO_BUFSIZE 10 * 1024

/* Task 10.1: opmode string array for config_list (g_variant_new_strv).
 * Exposes only Buffer/Stream to the UI (Internal Test is hidden, set via
 * config_set only when op_mode is OP_INTEST). config_set uses the larger
 * operation_modes[] below for std_str_idx validation. */
static const char *opmode_strs[] = {
    "Buffer Mode",
    "Stream Mode",
};

/* Operation mode string array for std_str_idx input validation.
 * Indexed by enum pxlogic_op_mode values (OP_BUFFER=0/OP_STREAM=1/OP_INTEST=2).
 * Includes the hidden OP_INTEST internal-test mode so std_str_idx can validate
 * it when set programmatically. */
static const char *operation_modes[] = {
    [OP_BUFFER]  = "Buffer Mode",
    [OP_STREAM]  = "Stream Mode",
    [OP_INTEST]  = "Internal Test",
};

/* Task 10.2: filter mode string array for both config_list (g_variant_new_strv)
 * and config_set (std_str_idx). Indexed 0..1 — entry 0 corresponds to filter
 * disabled, entry 1 to 1-sample-clock filter. devc->filter stores the same
 * index value and is consumed by hardware path usb_wr_reg(.., devc->filter << 3). */
static const char *filter_modes[] = {
    [0] = "None",
    [1] = "1 Sample Clock",
};

enum pxlogic_extern_edge_modes {
    PX_TRIGGER_CLOSE,
    PX_TRIGGER_RISING,
    PX_TRIGGER_ONE,
    PX_TRIGGER_FALLING,
    PX_TRIGGER_ZERO,
    PX_TRIGGER_EDGE,
};

/* Task 10.3: extern trigger match string array for config_list
 * (g_variant_new_strv). Replaces the fork-style struct sr_list_item array.
 * Indexed by enum pxlogic_extern_edge_modes. */
static const char *extern_trigger_match_strs[] = {
    [PX_TRIGGER_CLOSE]   = "close",
    [PX_TRIGGER_RISING]   = "Rising",
    [PX_TRIGGER_ONE]      = "One",
    [PX_TRIGGER_FALLING]  = "Falling",
    [PX_TRIGGER_ZERO]     = "Zero",
    [PX_TRIGGER_EDGE]     = "Edge",
};

/* Standard SR_TRIGGER_* match list for SR_CONF_TRIGGER_MATCH.
 * Mirrors fx2lafw/api.c trigger_matches[] — exposes the 5 upstream
 * trigger match types so the application can build trigger UI. */
static const int32_t trigger_matches[] = {
    SR_TRIGGER_ZERO,
    SR_TRIGGER_ONE,
    SR_TRIGGER_RISING,
    SR_TRIGGER_FALLING,
    SR_TRIGGER_EDGE,
};

static const struct PX_channels channel_modes[] = {
    { BUFFER_LOGIC250x32, PXLOGIC_MODE_LOGIC, SR_CHANNEL_LOGIC, 0, 32, 1, SR_MHZ(250), SR_MHZ(250),
        SR_KHZ(2), SR_MHZ(250), "Use 32 Channels (Max 250MHz)" },

    { BUFFER_LOGIC250x16, PXLOGIC_MODE_LOGIC, SR_CHANNEL_LOGIC, 0, 16, 1, SR_MHZ(250), SR_MHZ(250),
        SR_KHZ(2), SR_MHZ(250), "Use 16 Channels (Max 250MHz)" },

    { BUFFER_LOGIC500x16, PXLOGIC_MODE_LOGIC, SR_CHANNEL_LOGIC, 0, 16, 1, SR_MHZ(500), SR_MHZ(500),
        SR_KHZ(2), SR_MHZ(500), "Use 16 Channels (Max 500MHz)" },

    { BUFFER_LOGIC1000x8, PXLOGIC_MODE_LOGIC, SR_CHANNEL_LOGIC, 0, 8, 1, SR_GHZ(1), SR_GHZ(1),
        SR_KHZ(2), SR_GHZ(1), "Use 8 Channels (Max 1000MHz)" },

    { STREAM_LOGIC50x32, PXLOGIC_MODE_LOGIC, SR_CHANNEL_LOGIC, 1, 32, 1, SR_MHZ(50), SR_MHZ(50),
        SR_KHZ(2), SR_MHZ(50), "Use 32 Channels (Max50MHz)" },

    { STREAM_LOGIC125x16, PXLOGIC_MODE_LOGIC, SR_CHANNEL_LOGIC, 1, 16, 1, SR_MHZ(125), SR_MHZ(125),
        SR_KHZ(2), SR_MHZ(125), "Use 16 Channels (Max 125MHz)" },

    { STREAM_LOGIC250x8, PXLOGIC_MODE_LOGIC, SR_CHANNEL_LOGIC, 1, 8, 1, SR_MHZ(250), SR_MHZ(250),
        SR_KHZ(2), SR_MHZ(250), "Use 8 Channels (Max 250MHz)" },

    { STREAM_LOGIC500x4, PXLOGIC_MODE_LOGIC, SR_CHANNEL_LOGIC, 1, 4, 1, SR_MHZ(500), SR_MHZ(500),
        SR_KHZ(2), SR_MHZ(500), "Use 4 Channels (Max 500MHz)" },

    { STREAM_LOGIC1000x2, PXLOGIC_MODE_LOGIC, SR_CHANNEL_LOGIC, 1, 2, 1, SR_MHZ(1000), SR_MHZ(1000),
        SR_KHZ(2), SR_MHZ(1000), "Use 2 Channels (Max 1000MHz)" },

    { STREAM_LOGIC200x1, PXLOGIC_MODE_LOGIC, SR_CHANNEL_LOGIC, 1, 1, 1, SR_MHZ(200), SR_MHZ(200),
        SR_KHZ(2), SR_MHZ(200), "Use 1 Channels (Max200MHz)" },

    { STREAM_LOGIC100x2, PXLOGIC_MODE_LOGIC, SR_CHANNEL_LOGIC, 1, 2, 1, SR_MHZ(100), SR_MHZ(100),
        SR_KHZ(2), SR_MHZ(100), "Use 2 Channels (Max100MHz)" },

    { STREAM_LOGIC50x4, PXLOGIC_MODE_LOGIC, SR_CHANNEL_LOGIC, 1, 4, 1, SR_MHZ(50), SR_MHZ(50),
        SR_KHZ(2), SR_MHZ(50), "Use 4 Channels (Max50MHz)" },

    { STREAM_LOGIC25x8, PXLOGIC_MODE_LOGIC, SR_CHANNEL_LOGIC, 1, 8, 1, SR_MHZ(25), SR_MHZ(25),
        SR_KHZ(2), SR_MHZ(25), "Use 8 Channels (Max25MHz)" },

    { STREAM_LOGIC10x16, PXLOGIC_MODE_LOGIC, SR_CHANNEL_LOGIC, 1, 16, 1, SR_MHZ(10), SR_MHZ(10),
        SR_KHZ(2), SR_MHZ(10), "Use 16 Channels (Max10MHz)" },

    { STREAM_LOGIC5x32, PXLOGIC_MODE_LOGIC, SR_CHANNEL_LOGIC, 1, 32, 1, SR_MHZ(5), SR_MHZ(5),
        SR_KHZ(2), SR_MHZ(5), "Use 32 Channels (Max5MHz)" }
};

/* Channel mode string array for std_str_idx input validation.
 * Indexed by enum PX_CHANNEL_ID values (0..14). Strings mirror
 * channel_modes[].descr so std_str_idx returns the PX_CHANNEL_ID.
 * Task 10.4: also used as the source for config_list CHANNEL_MODE filtering. */
static const char *channel_mode_str[] = {
    [BUFFER_LOGIC250x32]  = "Use 32 Channels (Max 250MHz)",
    [BUFFER_LOGIC250x16]  = "Use 16 Channels (Max 250MHz)",
    [BUFFER_LOGIC500x16]  = "Use 16 Channels (Max 500MHz)",
    [BUFFER_LOGIC1000x8]  = "Use 8 Channels (Max 1000MHz)",
    [STREAM_LOGIC50x32]   = "Use 32 Channels (Max50MHz)",
    [STREAM_LOGIC125x16]  = "Use 16 Channels (Max 125MHz)",
    [STREAM_LOGIC250x8]   = "Use 8 Channels (Max 250MHz)",
    [STREAM_LOGIC500x4]   = "Use 4 Channels (Max 500MHz)",
    [STREAM_LOGIC1000x2]  = "Use 2 Channels (Max 1000MHz)",
    [STREAM_LOGIC200x1]   = "Use 1 Channels (Max200MHz)",
    [STREAM_LOGIC100x2]   = "Use 2 Channels (Max100MHz)",
    [STREAM_LOGIC50x4]    = "Use 4 Channels (Max50MHz)",
    [STREAM_LOGIC25x8]    = "Use 8 Channels (Max25MHz)",
    [STREAM_LOGIC10x16]   = "Use 16 Channels (Max10MHz)",
    [STREAM_LOGIC5x32]    = "Use 32 Channels (Max5MHz)",
};

/* Task 11.2: lang_text_map[] deleted — i18n moved to View layer Qt tr() /
 * LangResource system. Was dead code (compiler warning: defined but not used).
 */
/* Task 11.3: channel_mode_cn_map[] deleted — i18n moved to View layer Qt
 * tr() / LangResource system. Was dead code (compiler warning: defined but
 * not used). */

/* Driver info forward declaration */
static struct sr_dev_driver pxlogic_driver_info;
static struct sr_dev_driver *di = &pxlogic_driver_info;

static int hw_dev_acquisition_stop(struct sr_dev_inst *sdi);
static void finish_acquisition(struct sr_dev_inst *sdi);
static void receive_transfer(struct libusb_transfer *transfer);

/* Deinterleave LA_CROSS_DATA (channel-block) to sample-interleaved format.
 * LA_CROSS_DATA: [8 bytes per channel, 64 samples per 8-byte block]
 *   Layout: [ch0: 64 samples in 8 bytes][ch1: 64 samples in 8 bytes]...
 * Sample-interleaved: [unitsize bytes per sample, all channels]
 *   unitsize = ch_num / 8
 */
static void deinterleave_cross_to_interleaved(const uint8_t *cross_buf,
        uint8_t *interleaved_buf, uint64_t byte_length, int ch_num)
{
    int unitsize = ch_num / 8;
    uint64_t total_samples = byte_length * 8 / ch_num;
    uint64_t s;

    for (s = 0; s < total_samples; s++) {
        uint64_t block = s / 64;
        uint64_t bit_in_block = s % 64;
        const uint8_t *block_start = cross_buf + block * (uint64_t)ch_num * 8;
        uint8_t *out = interleaved_buf + s * unitsize;
        int ch;

        memset(out, 0, unitsize);
        for (ch = 0; ch < ch_num; ch++) {
            const uint8_t *ch_block = block_start + ch * 8;
            uint8_t bit = (ch_block[bit_in_block / 8] >> (bit_in_block % 8)) & 1;
            if (bit)
                out[ch / 8] |= (1 << (ch % 8));
        }
    }
}

static int hw_init(struct sr_dev_driver *driver, struct sr_context *sr_ctx)
{
    return std_init(driver, sr_ctx);
}

static void adjust_samplerate(struct PX_context *devc)
{
    devc->samplerates_max_index = ARRAY_SIZE(samplerates) - 1;
    while (samplerates[devc->samplerates_max_index] > channel_modes[devc->ch_mode].max_samplerate)
        devc->samplerates_max_index--;

    devc->samplerates_min_index = 0;
    while (samplerates[devc->samplerates_min_index] < channel_modes[devc->ch_mode].min_samplerate)
        devc->samplerates_min_index++;

    assert(devc->samplerates_max_index >= devc->samplerates_min_index);

    if (devc->cur_samplerate > samplerates[devc->samplerates_max_index])
        devc->cur_samplerate = samplerates[devc->samplerates_max_index];

    if (devc->cur_samplerate < samplerates[devc->samplerates_min_index])
        devc->cur_samplerate = samplerates[devc->samplerates_min_index];
}

static void probe_init(struct sr_dev_inst *sdi)
{
    /* Fork had extensive probe field setup (bits/vdiv/vfactor/coupling/etc.)
     * Upstream sr_channel only has index/type/enabled/name/priv.
     * Nothing to initialize here for upstream. */
    (void)sdi;
}

static int setup_probes(struct sr_dev_inst *sdi, int num_probes)
{
    uint16_t j;
    struct sr_channel *probe;
    struct sr_channel_group *cg;
    struct PX_context *devc = sdi->priv;
    GSList *l;

    /* Task 12.1-12.3: create a "Logic" channel group and append all logic
     * channels to it (mirrors scilogic/protocol.c:547-555). Channels are
     * also added to sdi->channels for backward compat with code that
     * iterates sdi->channels directly. */

    /* Free any existing channel_groups (e.g. on DEVICE_MODE switch).
     * The caller (config_set DEVICE_MODE) already frees sdi->channels
     * via sr_channel_free, so cg->channels pointers are dangling — only
     * free the GSList containers, not the channel data. */
    if (sdi->channel_groups) {
        for (l = sdi->channel_groups; l; l = l->next) {
            struct sr_channel_group *old_cg = l->data;
            g_free(old_cg->name);
            g_slist_free(old_cg->channels);
            g_free(old_cg);
        }
        g_slist_free(sdi->channel_groups);
        sdi->channel_groups = NULL;
    }

    cg = g_malloc0(sizeof(struct sr_channel_group));
    cg->name = g_strdup("Logic");

    for (j = 0; j < num_probes; j++) {
        if (!(probe = sr_channel_new(sdi, j, channel_modes[devc->ch_mode].type,
                  TRUE, probe_names[j])))
            return SR_ERR;
        sdi->channels = g_slist_append(sdi->channels, probe);
        cg->channels = g_slist_append(cg->channels, probe);
    }
    sdi->channel_groups = g_slist_append(sdi->channel_groups, cg);

    probe_init(sdi);
    return SR_OK;
}

static struct PX_context *pxlogic_dev_new(const struct PX_profile *prof)
{
    struct PX_context *devc;
    unsigned int i;

    if (!(devc = g_try_malloc(sizeof(struct PX_context)))) {
        sr_err("Device context malloc failed.");
        return NULL;
    }
    memset(devc, 0, sizeof(struct PX_context));

    for (i = 0; i < ARRAY_SIZE(channel_modes); i++) {
        if (channel_modes[i].id != i)
            assert(0);
    }

    sr_info("devc->profile = prof");
    devc->channel = NULL;
    devc->profile = prof;
    devc->ch_mode = devc->profile->dev_caps.default_channelmode;
    devc->cur_samplerate = channel_modes[devc->ch_mode].default_samplerate;
    devc->limit_samples = channel_modes[devc->ch_mode].default_samplelimit;
    devc->limit_samples_show = devc->limit_samples;
    devc->limit_msec = 0;
    devc->timebase = devc->profile->dev_caps.default_timebase;
    devc->max_height = 0;
    devc->op_mode = OP_BUFFER;
    devc->stream = (devc->op_mode != OP_BUFFER);
    devc->test_mode = 0;  /* PX_TEST_NONE (fork SR_TEST_NONE removed) */
    devc->rle_mode = FALSE;
    devc->vth = 2.0;
    devc->ch_num = 16;
    devc->instant = FALSE;
    devc->clock_edge = 0;
    devc->ext_trig_mode = 0;
    devc->trig_out_en = 0;
    devc->filter = 0;
    devc->mode = PXLOGIC_MODE_LOGIC;

    devc->pwm0_en = 0;
    devc->pwm0_freq = 1000;
    devc->pwm0_duty = 50;
    devc->pwm0_freq_set = (uint32_t)((double)PWM_CLK / devc->pwm0_freq);
    devc->pwm0_duty_set = (uint32_t)((double)devc->pwm0_freq_set * devc->pwm0_duty / 100);
    devc->pwm1_en = 0;
    devc->pwm1_freq = 1000;
    devc->pwm1_duty = 50;
    devc->pwm1_freq_set = (uint32_t)((double)PWM_CLK / devc->pwm1_freq);
    devc->pwm1_duty_set = (uint32_t)((double)devc->pwm1_freq_set * devc->pwm1_duty / 100);
    devc->is_loop = 0;
    devc->capture_ratio = 0;

    devc->stream_buff_size = 16;
    devc->stream_mem_buff_size = 16;
    devc->disk_cache_enable = FALSE;
    devc->disk_cache_path = NULL;

    devc->deinterleave_buf = NULL;
    devc->deinterleave_buf_size = 0;

    adjust_samplerate(devc);
    sr_info("adjust_samplerate");

    return devc;
}

SR_PRIV gboolean logic_check_conf_profile(libusb_device *dev, uint32_t *logic_mode)
{
    struct libusb_device_descriptor des;
    struct libusb_device_handle *hdl;
    int ret;
    gboolean bSucess;
    unsigned char strdesc[64];

    hdl = NULL;
    bSucess = FALSE;
    ret = 0;

    while (!bSucess) {
        if ((ret = libusb_get_device_descriptor(dev, &des)) < 0) {
            sr_err("%s:%d, Failed to get device descriptor: %s",
                __func__, __LINE__, libusb_error_name(ret));
            break;
        }

        if ((ret = libusb_open(dev, &hdl)) < 0) {
            sr_err("%s:%d, Failed to open device: %s",
                __func__, __LINE__, libusb_error_name(ret));
            return FALSE;
        }

        ret = libusb_claim_interface(hdl, USB_INTERFACE_C);
        ret = libusb_claim_interface(hdl, USB_INTERFACE_D);

        if ((ret = libusb_get_string_descriptor_ascii(hdl,
                 des.iManufacturer, strdesc, sizeof(strdesc)))
            < 0) {
            sr_err("%s:%d, Failed to get device descriptor ascii: %s",
                __func__, __LINE__, libusb_error_name(ret));
            break;
        }

        if (strncmp((const char *)strdesc, "PX", 2))
            break;

        uint32_t reg_addr;
        uint32_t reg_data;
        reg_addr = 8192 + 22 * 4;
        ret = usb_rd_reg(hdl, reg_addr, &reg_data);

        if (ret == 0) {
            bSucess = TRUE;
            *logic_mode = reg_data;
        } else {
            bSucess = FALSE;
            break;
        }
    }

    if (hdl) {
        ret = libusb_release_interface(hdl, USB_INTERFACE_C);
        ret = libusb_release_interface(hdl, USB_INTERFACE_D);
        libusb_close(hdl);
    }

    return bSucess;
}

static GSList *scan(struct sr_dev_driver *driver, GSList *options)
{
    struct sr_dev_inst *sdi;
    struct drv_context *drvc;
    struct PX_context *devc;
    struct sr_usb_dev_inst *usb;
    struct sr_config *src;
    const struct PX_profile *prof;
    GSList *l, *devices, *conn_devices;
    struct libusb_device_descriptor des;
    libusb_device **devlist;
    libusb_device *device_handle = NULL;
    int ret, i, j;
    const char *conn;
    enum libusb_speed usb_speed;
    struct sr_usb_dev_inst *usb_dev_info;
    uint8_t bus;
    uint8_t address;
    (void)options;
    drvc = driver->context;
    devices = NULL;

    if (options != NULL)
        sr_info("%s", "Scan ZZY device with options.");
    else
        sr_info("%s", "Scan ZZY device.");

    conn = NULL;
    for (l = options; l; l = l->next) {
        src = l->data;
        switch (src->key) {
        case SR_CONF_CONN:
            conn = g_variant_get_string(src->data, NULL);
            break;
        }
    }
    if (conn) {
        sr_info("%s", "Find usb device with connect config.");
        conn_devices = sr_usb_find(drvc->sr_ctx->libusb_ctx, conn);
    } else
        conn_devices = NULL;

    devices = NULL;
    devlist = NULL;

    libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &devlist);
    if (devlist == NULL) {
        sr_info("%s: Failed to call libusb_get_device_list(), it returns a null list.", __func__);
        return NULL;
    }
    for (i = 0; devlist[i]; i++) {
        device_handle = devlist[i];

        if (conn) {
            usb = NULL;
            for (l = conn_devices; l; l = l->next) {
                usb = l->data;
                if (usb->bus == libusb_get_bus_number(device_handle)
                    && usb->address == libusb_get_device_address(device_handle))
                    break;
            }
            if (!l)
                continue;
        }

        if ((ret = libusb_get_device_descriptor(device_handle, &des)) != 0) {
            sr_warn("Failed to get device descriptor: %s.",
                libusb_error_name(ret));
            continue;
        }

        if (des.idVendor != supported_PX[0].vid && des.idVendor != supported_PX[2].vid)
            continue;

        sr_info("enter libusb_get_device_speed");
        usb_speed = libusb_get_device_speed(device_handle);
        if ((usb_speed != LIBUSB_SPEED_HIGH) && (usb_speed != LIBUSB_SPEED_SUPER)) {
            sr_info("usb_speed errr");
            continue;
        } else {
            sr_info("enter libusb_get_device_speed = %d", usb_speed);
            sr_info("usb_speed ok");
        }

        prof = NULL;
        for (j = 0; supported_PX[j].vid; j++) {
            if (des.idVendor == supported_PX[j].vid && des.idProduct == supported_PX[j].pid) {
                if (usb_speed == supported_PX[j].usb_speed) {
                    prof = &supported_PX[j];
                    sr_info("Found a PX usb: vid:0x%4x,address:0x%4x", supported_PX[j].vid, supported_PX[j].pid);
                    break;
                }
            }
        }

        if (prof == NULL) {
            sr_info("Skip if the device was not found");
            continue;
        }

        bus = libusb_get_bus_number(device_handle);
        address = libusb_get_device_address(device_handle);
        sr_info("Found a new device,handle:%p,bus:%d,address:%d", device_handle, bus, address);
        uint32_t logic_mode = 0;
        if (logic_check_conf_profile(device_handle, &logic_mode)) {
            for (j = 0; supported_PX[j].vid; j++) {
                if (des.idVendor == supported_PX[j].vid && des.idProduct == supported_PX[j].pid) {
                    if (usb_speed == supported_PX[j].usb_speed && logic_mode == supported_PX[j].logic_mode) {
                        prof = &supported_PX[j];
                        sr_info("Found a PX usb: vid:0x%4x,address:0x%4x", supported_PX[j].vid, supported_PX[j].pid);
                        break;
                    }
                }
            }

            devc = pxlogic_dev_new(prof);
            devc->usb_speed = usb_speed;
            sr_info("pxlogic_dev_new");
            if (!devc)
                break;

            sdi = g_malloc0(sizeof(struct sr_dev_inst));
            if (sdi == NULL) {
                g_free(devc);
                sr_info("sr_dev_inst_new error");
                break;
            }
            sdi->status = SR_ST_INITIALIZING;
            sdi->vendor = g_strdup(prof->vendor);
            sdi->model = g_strdup(prof->model);
            sdi->version = g_strdup(prof->model_version);
            sdi->inst_type = SR_INST_USB;
            sdi->driver = di;
            sdi->priv = devc;
            devc->usb_dev = device_handle;

            if (setup_probes(sdi, channel_modes[devc->ch_mode].num) != SR_OK) {
                sr_err("%s", "eng_setup_probes() error");
                sr_dev_inst_free(sdi);
                g_free(devc);
                break;
            }

            sr_info("Found a device,name:\"%s\",handle:%p", prof->model, device_handle);
            usb_dev_info = sr_usb_dev_inst_new(bus, address, NULL);
            sdi->conn = usb_dev_info;
            sdi->status = SR_ST_INACTIVE;

            devices = g_slist_append(devices, sdi);
            sr_info("enter eng_check_conf_profile");
        }
    }

    libusb_free_device_list(devlist, 0);

    if (conn_devices) {
        g_slist_free_full(conn_devices, (GDestroyNotify)sr_usb_dev_inst_free);
    }

    return std_scan_complete(di, devices);
}

SR_PRIV int firmware_config(struct sr_context *sr_ctx, struct libusb_device_handle *usbdevh, const char *name, unsigned int mode)
{
    struct sr_resource fw_res;
    int chunksize, ret = SR_OK;
    unsigned char *buf;
    int transferred;
    (void)chunksize;
    (void)transferred;
    uint64_t filesize;

    unsigned int base_addr;
    int length;

    sr_info("Configure FPGA using \"%s\"", name);
    if (sr_resource_open(sr_ctx, &fw_res, SR_RESOURCE_FIRMWARE, name) != SR_OK) {
        sr_err("Firmware not found.");
        return SR_ERR;
    }

    filesize = fw_res.size;

    if (mode == 0) {
        if ((buf = malloc(48 * 4 * 1024)) == NULL) {
            sr_err("wch569 app configure buf malloc failed.");
            sr_resource_close(sr_ctx, &fw_res);
            return SR_ERR;
        }

    } else if (mode == 2) {
        if ((buf = malloc(48 * 4 * 1024)) == NULL) {
            sr_err("wch569 bl configure buf malloc failed.");
            sr_resource_close(sr_ctx, &fw_res);
            return SR_ERR;
        }

    } else {
        if ((buf = malloc(filesize * 2)) == NULL) {
            sr_err("FPGA configure buf malloc failed.");
            sr_resource_close(sr_ctx, &fw_res);
            return SR_ERR;
        }
    }

    if (mode == 0) {
        base_addr = 48 * 1024;
        length = 48 * 1024;
        IGNORE_RESULT(sr_resource_read(sr_ctx, &fw_res, buf, filesize));
        memset(buf + filesize, 0xff, length - filesize);
        memcpy(buf + length, buf, length);
        memcpy(buf + length * 2, buf, length);

        length = length * 3;
        libusb_clear_halt(usbdevh, 0x03);
        ret = usb_wr_data_update(usbdevh, base_addr, length, 0, buf, 0);

    } else if (mode == 2) {
        base_addr = 0;
        length = 32 * 1024;
        IGNORE_RESULT(sr_resource_read(sr_ctx, &fw_res, buf, filesize));
        memset(buf + filesize, 0xff, length - filesize);
        memcpy(buf + length, buf, length);

        length = length;
        libusb_clear_halt(usbdevh, 0x03);
        ret = usb_wr_data_update(usbdevh, base_addr, length, 0, buf, 0);

    } else if (mode == 1) {
        base_addr = 0;
        length = filesize;
        IGNORE_RESULT(sr_resource_read(sr_ctx, &fw_res, buf, filesize));
        libusb_clear_halt(usbdevh, 0x03);
        ret = usb_wr_data_update(usbdevh, base_addr, length, 4, buf, 0);
        if (ret != 0) {
            sr_err("FPGA configure usb_wr_data_update error");
        }
    }

    sr_resource_close(sr_ctx, &fw_res);
    free(buf);

    if (ret != SR_OK) {
        return SR_ERR;
    }

    sr_info("FPGA configure done: %d bytes.", filesize);
    return SR_OK;
}

static int hw_usb_open(struct sr_dev_driver *drv, struct sr_dev_inst *sdi, gboolean *fpga_done)
{
    libusb_device *dev_handel = NULL;
    struct sr_usb_dev_inst *usb;
    struct PX_context *devc;
    struct drv_context *drvc;
    int ret;

    drvc = drv->context;
    devc = sdi->priv;
    usb = sdi->conn;

    if (devc->usb_dev == NULL) {
        sr_err("%s", "hw_dev_open(), usb_dev is null.");
        return SR_ERR;
    }

    if (sdi->status == SR_ST_ACTIVE) {
        sr_spew("The usb device is opened");
        return SR_OK;
    }

    if (sdi->status == SR_ST_INITIALIZING) {
        sr_info("%s", "The device instance is still boosting.");
    }
    dev_handel = devc->usb_dev;

    sr_info("Open usb device instance, handle: %p", dev_handel);

    if ((ret = libusb_open(dev_handel, &usb->devhdl)) != 0) {
        sr_err("Failed to open device: %s, handle:%p",
            libusb_error_name(ret), dev_handel);
        return SR_ERR;
    }

    ret = libusb_claim_interface(usb->devhdl, USB_INTERFACE_C);
    ret = libusb_claim_interface(usb->devhdl, USB_INTERFACE_D);

    if (usb->address == 0xff) {
        usb->address = libusb_get_device_address(dev_handel);
    }

    {
        uint32_t reg_addr;
        uint32_t reg_data;
        reg_addr = 8192 + 13 * 4;
        ret = usb_rd_reg(usb->devhdl, reg_addr, &reg_data);
        if (ret == 0) {
            sr_info("current   firmware_version = %x   new firmware_version = %x", reg_data, devc->profile->firmware_version);
            if (reg_data == devc->profile->firmware_bl_version && PXVIEW_BL_EN == 1) {
                sr_info(" open bl bin file %s ", devc->profile->firmware_bl);
                ret = firmware_config(drvc->sr_ctx, usb->devhdl, devc->profile->firmware_bl, 2);
                sr_info("firmware  end");
            }

            if (reg_data != devc->profile->firmware_version) {
                sr_info(" open app bin file %s ", devc->profile->firmware);
                ret = firmware_config(drvc->sr_ctx, usb->devhdl, devc->profile->firmware, 0);
                sr_info("firmware  end");
                sr_info("rst usb ");
                reg_addr = 8192 + 12 * 4;
                reg_data = 0;
                ret = usb_wr_reg(usb->devhdl, reg_addr, reg_data);
                sdi->status = SR_ST_INITIALIZING;

                return SR_ERR_DEV_CLOSED;
            }
            sdi->status = SR_ST_ACTIVE;
        }
    }

    if (sdi->status == SR_ST_ACTIVE) {
        if (!(*fpga_done)) {

            sr_info("fpag_bit start");

            sr_info(" open FPGA bit file %s ", devc->profile->fpga_rst_bit);
            ret = firmware_config(drvc->sr_ctx, usb->devhdl, devc->profile->fpga_rst_bit, 1);

            sr_info(" open FPGA bit file %s ", devc->profile->fpga_bit);
            ret = firmware_config(drvc->sr_ctx, usb->devhdl, devc->profile->fpga_bit, 1);
            *fpga_done = 1;
            sr_info("fpag_bit end");
        }
    }

    return SR_OK;
}

static int hw_dev_open(struct sr_dev_inst *sdi)
{
    struct PX_context *const devc = sdi->priv;
    (void)devc;
    gboolean fpga_done = 0;

    if (sdi->status != SR_ST_ACTIVE) {
        fpga_done = 0;
    }

    hw_usb_open(di, sdi, &fpga_done);
    sr_info("hw_dev_open");

    return SR_OK;
}

SR_PRIV int hw_usb_close(struct sr_dev_inst *sdi)
{
    struct sr_usb_dev_inst *usb;

    usb = sdi->conn;
    if (usb->devhdl == NULL) {
        sr_spew("%s", "eng_dev_close(),libusb_device_handle is null.");
        return SR_ERR;
    }

    sr_info("%s: Closing device on %d.%d interface %d.",
        sdi->driver->name, usb->bus, usb->address, USB_INTERFACE_C);

    libusb_release_interface(usb->devhdl, USB_INTERFACE_C);
    libusb_release_interface(usb->devhdl, USB_INTERFACE_D);
    libusb_close(usb->devhdl);
    usb->devhdl = NULL;

    return SR_OK;
}

static int hw_dev_close(struct sr_dev_inst *sdi)
{
    struct PX_context *devc = sdi->priv;
    (void)devc;
    sr_info("hw_dev_close");
    hw_usb_close(sdi);
    sdi->status = SR_ST_INACTIVE;

    return SR_OK;
}

static int hw_cleanup(const struct sr_dev_driver *drv)
{
    return std_cleanup(drv);
}

static unsigned int en_ch_num_mask(const struct sr_dev_inst *sdi)
{
    GSList *l;
    unsigned int channel_en_mask = 0;
    unsigned int i = 0;

    for (l = sdi->channels; l; l = l->next) {
        struct sr_channel *probe = (struct sr_channel *)l->data;
        channel_en_mask = channel_en_mask | (probe->enabled << i);
        i++;
    }

    return channel_en_mask;
}

static unsigned int en_ch_num(const struct sr_dev_inst *sdi)
{
    GSList *l;
    unsigned int channel_en_cnt = 0;

    for (l = sdi->channels; l; l = l->next) {
        struct sr_channel *probe = (struct sr_channel *)l->data;
        channel_en_cnt += probe->enabled;
    }

    return channel_en_cnt;
}

static int config_get(uint32_t key, GVariant **data, const struct sr_dev_inst *sdi,
    const struct sr_channel_group *cg)
{
    (void)cg;

    struct PX_context *devc;

    assert(sdi);
    assert(sdi->priv);

    devc = sdi->priv;
    devc->ch_num = en_ch_num(sdi);
    switch (key) {
    case SR_CONF_OPERATION_MODE:
        /* Task 10/Phase 3: return string for consistency with config_set
         * (std_str_idx) and config_list (g_variant_new_strv). View layer
         * reads via get_config_string and compares against the radio button
         * string list. DeviceAgent::get_operation_mode_int() converts back
         * to int (LO_OP_*) for callers that need the int value. */
        *data = g_variant_new_string(operation_modes[devc->op_mode]);
        break;

    case SR_CONF_EX_TRIGGER_MATCH:
        /* Task 10/Phase 3: return string matching config_list extern_trigger_match_strs[]. */
        *data = g_variant_new_string(extern_trigger_match_strs[devc->ext_trig_mode]);
        break;

    case SR_CONF_CHANNEL_MODE:
        /* Task 10/Phase 3: return string (channel_mode_str[ch_mode]) for
         * consistency with config_set (std_str_idx) and config_list
         * (g_variant_new_strv filtered list). View reads via get_config_string
         * and highlights the matching radio button by string comparison. */
        *data = g_variant_new_string(channel_mode_str[devc->ch_mode]);
        break;

    case SR_CONF_SAMPLERATE:
        *data = g_variant_new_uint64(devc->cur_samplerate);
        break;
    case SR_CONF_LIMIT_SAMPLES:
        *data = g_variant_new_uint64(devc->limit_samples_show);
        break;
    case SR_CONF_LIMIT_MSEC:
        *data = g_variant_new_uint64(devc->limit_msec);
        break;
    case SR_CONF_DEVICE_MODE:
        *data = g_variant_new_int16(devc->mode);
        break;
    case SR_CONF_CAPTURE_RATIO:
        /* Trigger position as 0..100 percent. Mirrors scilogic api.c. */
        *data = g_variant_new_uint64(devc->capture_ratio);
        break;

    case SR_CONF_VTH:
        *data = g_variant_new_double(devc->vth);
        break;

    case SR_CONF_CLOCK_EDGE:
        *data = g_variant_new_boolean(devc->clock_edge);
        break;
    case SR_CONF_TRIGGER_OUT:
        *data = g_variant_new_boolean(devc->trig_out_en);
        break;
    case SR_CONF_FILTER:
        *data = g_variant_new_int16(devc->filter);
        break;

    case SR_CONF_PWM0_EN:
        *data = g_variant_new_boolean(devc->pwm0_en);
        break;
    case SR_CONF_PWM0_FREQ:
        *data = g_variant_new_double(devc->pwm0_freq);
        break;
    case SR_CONF_PWM0_DUTY:
        *data = g_variant_new_double(devc->pwm0_duty);
        break;

    case SR_CONF_PWM1_EN:
        *data = g_variant_new_boolean(devc->pwm1_en);
        break;
    case SR_CONF_PWM1_FREQ:
        *data = g_variant_new_double(devc->pwm1_freq);
        break;
    case SR_CONF_PWM1_DUTY:
        *data = g_variant_new_double(devc->pwm1_duty);
        break;

    default:
        return SR_ERR_NA;
    }

    return SR_OK;
}

SR_PRIV int pxlogic_adjust_probes(struct sr_dev_inst *sdi, int num_probes)
{
    uint16_t j;
    struct sr_channel *probe;
    struct sr_channel_group *cg;
    struct PX_context *devc = sdi->priv;
    GSList *l;

    assert(num_probes > 0);

    j = g_slist_length(sdi->channels);
    while (j < num_probes) {
        if (!(probe = sr_channel_new(sdi, j, channel_modes[devc->ch_mode].type,
                  TRUE, probe_names[j])))
            return SR_ERR;
        sdi->channels = g_slist_append(sdi->channels, probe);
        j++;
    }

    while (j > num_probes) {
        sdi->channels = g_slist_delete_link(sdi->channels, g_slist_last(sdi->channels));
        j--;
    }

    for (l = sdi->channels; l; l = l->next) {
        probe = (struct sr_channel *)l->data;
        probe->enabled = TRUE;
        probe->type = channel_modes[devc->ch_mode].type;
    }

    /* Task 12: keep the "Logic" channel group's channel list in sync with
     * sdi->channels after add/remove operations. Rebuild cg->channels from
     * sdi->channels so the group always reflects the current channel set. */
    if (sdi->channel_groups) {
        cg = sdi->channel_groups->data;
        if (cg) {
            g_slist_free(cg->channels);
            cg->channels = NULL;
            for (l = sdi->channels; l; l = l->next) {
                cg->channels = g_slist_append(cg->channels, l->data);
            }
        }
    }
    return SR_OK;
}

static int config_set(uint32_t key, GVariant *data, const struct sr_dev_inst *sdi,
    const struct sr_channel_group *cg)
{
    (void)cg;

    uint16_t i, nv;
    int ret, num_probes;
    const char *stropt;
    struct PX_context *devc;
    struct sr_usb_dev_inst *usb;

    assert(sdi);
    assert(sdi->priv);

    devc = (struct PX_context *)sdi->priv;
    usb = sdi->conn;

    if (sdi->status != SR_ST_ACTIVE)
        return SR_ERR_DEV_CLOSED;
    ret = SR_OK;
    if (key == SR_CONF_SAMPLERATE) {
        int idx = std_u64_idx(data, ARRAY_AND_SIZE(samplerates));
        if (idx < 0)
            return SR_ERR_ARG;
        devc->cur_samplerate = samplerates[idx];
        devc->samples_counter = 0;
        devc->pre_index = 0;
        sr_dbg("%s: setting samplerate to %llu", __func__,
            devc->cur_samplerate);
        ret = SR_OK;
    } else if (key == SR_CONF_LIMIT_SAMPLES) {
        devc->limit_msec = 0;
        devc->limit_samples = g_variant_get_uint64(data);
        devc->limit_samples = (devc->limit_samples + 63) & ~63;
        devc->limit_samples_show = devc->limit_samples;
        if (devc->mode == PXLOGIC_MODE_DSO && en_ch_num(sdi) == 1) {
            devc->limit_samples /= 2;
        }
        sr_dbg("%s: setting limit_samples to %llu", __func__,
            devc->limit_samples);
        ret = SR_OK;
    } else if (key == SR_CONF_LIMIT_MSEC) {
        devc->limit_msec = g_variant_get_uint64(data);
        devc->limit_samples = 0;
        devc->limit_samples_show = devc->limit_samples;
        sr_dbg("%s: setting limit_msec to %llu", __func__,
            devc->limit_msec);
        ret = SR_OK;
    } else if (key == SR_CONF_DEVICE_MODE) {
        devc->mode = g_variant_get_int16(data);

        /* Only LOGIC mode is supported; DSO/ANALOG hardware was dropped
         * (DSL hardware removal). Reject non-LOGIC requests up front. */
        if (devc->mode != PXLOGIC_MODE_LOGIC) {
            sr_err("%s: unsupported device mode %d (only LOGIC)", __func__, devc->mode);
            ret = SR_ERR;
        } else {
            for (i = 0; i < ARRAY_SIZE(channel_modes); i++) {
                if (channel_modes[i].mode == devc->mode && devc->profile->dev_caps.channels & (1 << i)) {
                    devc->ch_mode = channel_modes[i].id;
                    break;
                }
            }
            num_probes = channel_modes[devc->ch_mode].num;
            devc->cur_samplerate = channel_modes[devc->ch_mode].default_samplerate;
            devc->limit_samples = channel_modes[devc->ch_mode].default_samplelimit;
            devc->limit_samples_show = devc->limit_samples;
            devc->timebase = devc->profile->dev_caps.default_timebase;
            {
                struct sr_dev_inst *mut_sdi = (struct sr_dev_inst *)sdi;
                g_slist_free_full(mut_sdi->channels, (GDestroyNotify)sr_channel_free);
                mut_sdi->channels = NULL;
                setup_probes(mut_sdi, num_probes);
            }
            adjust_samplerate(devc);
            sr_info("%s: setting mode to %d", __func__, devc->mode);
            ret = SR_OK;
        }
    }
    else if (key == SR_CONF_CAPTURE_RATIO) {
        /* Trigger position as 0..100 percent. Stored in devc->capture_ratio
         * and applied by set_trigger() at acquisition start. Replaces the
         * fork-era pxlogic_trigger_cfg.trigger_pos stub. Mirrors scilogic. */
        devc->capture_ratio = g_variant_get_uint64(data);
        sr_dbg("%s: setting capture_ratio to %llu", __func__,
            (unsigned long long)devc->capture_ratio);
        ret = SR_OK;
    } else if (key == SR_CONF_OPERATION_MODE) {
        /* Task 10/Phase 3: View layer now sends a string (selected from the
         * g_variant_new_strv list exposed by config_list). Validate via
         * std_str_idx against operation_modes[] (3 entries, includes hidden
         * OP_INTEST). Returns OP_BUFFER=0 / OP_STREAM=1 / OP_INTEST=2. */
        int idx = std_str_idx(data, ARRAY_AND_SIZE(operation_modes));
        if (idx < 0)
            return SR_ERR_ARG;
        ret = SR_OK;
        nv = idx;

        if (devc->mode == PXLOGIC_MODE_LOGIC && devc->op_mode != nv)
        {
            if (nv == OP_BUFFER) {
                devc->op_mode = OP_BUFFER;
                devc->test_mode = 0;  /* PX_TEST_NONE (fork SR_TEST_NONE removed) */
                devc->stream = FALSE;

                for (i = 0; i < ARRAY_SIZE(channel_modes); i++) {
                    if (channel_modes[i].mode == PXLOGIC_MODE_LOGIC && channel_modes[i].stream == devc->stream && devc->profile->dev_caps.channels & (1 << i)) {
                        devc->ch_mode = channel_modes[i].id;
                        break;
                    }
                }
            } else if (nv == OP_STREAM) {
                devc->op_mode = OP_STREAM;
                devc->test_mode = 0;  /* PX_TEST_NONE (fork SR_TEST_NONE removed) */
                devc->stream = TRUE;

                for (i = 0; i < ARRAY_SIZE(channel_modes); i++) {
                    if (channel_modes[i].mode == PXLOGIC_MODE_LOGIC && channel_modes[i].stream == devc->stream && devc->profile->dev_caps.channels & (1 << i)) {
                        devc->ch_mode = channel_modes[i].id;
                        break;
                    }
                }
            } else if (nv == OP_INTEST) {
                devc->op_mode = OP_INTEST;
                devc->test_mode = 1;  /* PX_TEST_INTERNAL (fork SR_TEST_INTERNAL removed) */
                devc->ch_mode = devc->profile->dev_caps.intest_channel;
                devc->stream = !(devc->profile->dev_caps.feature_caps & CAPS_FEATURE_BUF);
            } else {
                ret = SR_ERR;
            }

            pxlogic_adjust_probes((struct sr_dev_inst *)sdi, channel_modes[devc->ch_mode].num);
            adjust_samplerate(devc);
        }
        sr_dbg("%s: setting pattern to %d",
            __func__, devc->op_mode);
    } else if (key == SR_CONF_EX_TRIGGER_MATCH) {
        /* Task 10/Phase 3: View layer now sends a string (selected from the
         * g_variant_new_strv list exposed by config_list). Validate via
         * std_str_idx against extern_trigger_match_strs[]. */
        int idx = std_str_idx(data, ARRAY_AND_SIZE(extern_trigger_match_strs));
        if (idx < 0)
            return SR_ERR_ARG;
        ret = SR_OK;
        devc->ext_trig_mode = idx;
    } else if (key == SR_CONF_CHANNEL_MODE) {
        /* Task 10/Phase 3: View layer now sends a string (selected from the
         * g_variant_new_strv list exposed by config_list). Validate via
         * std_str_idx against channel_mode_str[] (indexed by PX_CHANNEL_ID,
         * so the returned idx IS the channel mode id). */
        int idx = std_str_idx(data, ARRAY_AND_SIZE(channel_mode_str));
        if (idx < 0)
            return SR_ERR_ARG;
        ret = SR_OK;
        nv = idx;
        if (devc->mode == PXLOGIC_MODE_LOGIC) {
            for (i = 0; i < ARRAY_SIZE(channel_modes); i++) {
                if (devc->profile->dev_caps.channels & (1 << i)) {
                    if (channel_modes[i].id == nv) {
                        devc->ch_mode = nv;
                        break;
                    }
                }
            }

            pxlogic_adjust_probes((struct sr_dev_inst *)sdi, channel_modes[devc->ch_mode].num);
            adjust_samplerate(devc);
        }
        sr_dbg("%s: setting channel mode to %d",
            __func__, devc->ch_mode);
    }
    else if (key == SR_CONF_VTH) {
        ret = SR_OK;
        devc->vth = g_variant_get_double(data);
    } else if (key == SR_CONF_CLOCK_EDGE) {
        devc->clock_edge = g_variant_get_boolean(data);
    } else if (key == SR_CONF_TRIGGER_OUT) {
        devc->trig_out_en = g_variant_get_boolean(data);
    } else if (key == SR_CONF_FILTER) {
        /* Task 10/Phase 3: View layer now sends a string (selected from the
         * g_variant_new_strv list exposed by config_list). Validate via
         * std_str_idx against filter_modes[]. devc->filter stores the index
         * (0=None, 1=1 Sample Clock); hardware path
         * usb_wr_reg(.., devc->filter << 3) consumes it unchanged. */
        int fidx = std_str_idx(data, ARRAY_AND_SIZE(filter_modes));
        if (fidx < 0)
            return SR_ERR_ARG;
        devc->filter = fidx;
        sr_dbg("%s: setting filter to %d",
            __func__, devc->filter);
    } else if (key == SR_CONF_PWM0_EN) {
        devc->pwm0_en = g_variant_get_boolean(data);
        usb_wr_reg(usb->devhdl, 16 << 2, (uint32_t)devc->pwm0_en);
    } else if (key == SR_CONF_PWM0_FREQ) {
        ret = SR_OK;
        devc->pwm0_freq = g_variant_get_double(data);
        devc->pwm0_freq_set = (uint32_t)((double)PWM_CLK / devc->pwm0_freq);
        sr_dbg("pwm0_freq_set =  %d", devc->pwm0_freq_set);
        devc->pwm0_freq = (double)PWM_CLK / (double)devc->pwm0_freq_set;

        devc->pwm0_duty_set = (uint32_t)((double)devc->pwm0_freq_set * devc->pwm0_duty / 100);
        devc->pwm0_duty = (double)devc->pwm0_duty_set * 100 / (double)devc->pwm0_freq_set;

        usb_wr_reg(usb->devhdl, 16 << 2, 0);
        usb_wr_reg(usb->devhdl, 17 << 2, devc->pwm0_freq_set - 1);
        usb_wr_reg(usb->devhdl, 18 << 2, devc->pwm0_duty_set - 1);
        usb_wr_reg(usb->devhdl, 16 << 2, (uint32_t)devc->pwm0_en);
    } else if (key == SR_CONF_PWM0_DUTY) {
        ret = SR_OK;
        devc->pwm0_duty = g_variant_get_double(data);
        devc->pwm0_duty_set = (uint32_t)((double)devc->pwm0_freq_set * devc->pwm0_duty / 100);
        sr_dbg("pwm0_duty_set =  %d", devc->pwm0_duty_set);
        devc->pwm0_duty = (double)devc->pwm0_duty_set * 100 / (double)devc->pwm0_freq_set;

        usb_wr_reg(usb->devhdl, 16 << 2, 0);
        usb_wr_reg(usb->devhdl, 17 << 2, devc->pwm0_freq_set - 1);
        usb_wr_reg(usb->devhdl, 18 << 2, devc->pwm0_duty_set - 1);
        usb_wr_reg(usb->devhdl, 16 << 2, (uint32_t)devc->pwm0_en);

    } else if (key == SR_CONF_PWM1_EN) {
        devc->pwm1_en = g_variant_get_boolean(data);
        usb_wr_reg(usb->devhdl, 19 << 2, (uint32_t)devc->pwm1_en);
    } else if (key == SR_CONF_PWM1_FREQ) {
        ret = SR_OK;
        devc->pwm1_freq = g_variant_get_double(data);
        devc->pwm1_freq_set = (uint32_t)((double)PWM_CLK / devc->pwm1_freq);
        sr_dbg("pwm1_freq_set =  %d", devc->pwm1_freq_set);
        devc->pwm1_freq = (double)PWM_CLK / (double)devc->pwm1_freq_set;

        devc->pwm1_duty_set = (uint32_t)((double)devc->pwm1_freq_set * devc->pwm1_duty / 100);
        devc->pwm1_duty = (double)devc->pwm1_duty_set * 100 / (double)devc->pwm1_freq_set;

        usb_wr_reg(usb->devhdl, 19 << 2, 0);
        usb_wr_reg(usb->devhdl, 20 << 2, devc->pwm1_freq_set - 1);
        usb_wr_reg(usb->devhdl, 21 << 2, devc->pwm1_duty_set - 1);
        usb_wr_reg(usb->devhdl, 19 << 2, (uint32_t)devc->pwm1_en);
    } else if (key == SR_CONF_PWM1_DUTY) {
        ret = SR_OK;
        devc->pwm1_duty = g_variant_get_double(data);
        devc->pwm1_duty_set = (uint32_t)(devc->pwm1_freq_set * (uint32_t)devc->pwm1_duty / 100);
        sr_dbg("pwm1_duty_set =  %d", devc->pwm1_duty_set);
        devc->pwm1_duty = (double)devc->pwm1_duty_set * 100 / (double)devc->pwm1_freq_set;

        usb_wr_reg(usb->devhdl, 19 << 2, 0);
        usb_wr_reg(usb->devhdl, 20 << 2, devc->pwm1_freq_set - 1);
        usb_wr_reg(usb->devhdl, 21 << 2, devc->pwm1_duty_set - 1);
        usb_wr_reg(usb->devhdl, 19 << 2, (uint32_t)devc->pwm1_en);
    } else {
        ret = SR_ERR_NA;
    }

    return ret;
}

static int config_list(uint32_t key, GVariant **data, const struct sr_dev_inst *sdi,
    const struct sr_channel_group *cg)
{
    struct PX_context *devc;
    GVariant *gvar;
    GVariantBuilder gvb;
    int i;
    int num;

    (void)cg;
    devc = sdi->priv;

    switch (key) {
    case SR_CONF_SCAN_OPTIONS:
        /* Task 7.4: route SCAN_OPTIONS through STD_CONFIG_LIST so scanopts[]
         * is returned as a standard uint32 fixed array (libsigrok 0.6.0 ABI).
         * SR_CONF_DEVICE_OPTIONS / DEVICE_SESSIONS intentionally NOT routed
         * through STD_CONFIG_LIST — PXView's deviceoptions.cpp reads them as
         * plain int32 without masking capability bits; STD_CONFIG_LIST would
         * emit uint32 with cap bits and break the UI binding. */
        return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
    case SR_CONF_DEVICE_OPTIONS:
        *data = g_variant_new_from_data(G_VARIANT_TYPE("ai"),
            hwoptions, ARRAY_SIZE(hwoptions) * sizeof(int32_t), TRUE, NULL, NULL);
        break;
    case SR_CONF_DEVICE_SESSIONS:
        *data = g_variant_new_from_data(G_VARIANT_TYPE("ai"),
            sessions, ARRAY_SIZE(sessions) * sizeof(int32_t), TRUE, NULL, NULL);
        break;
    case SR_CONF_SAMPLERATE:
        g_variant_builder_init(&gvb, G_VARIANT_TYPE("a{sv}"));
        gvar = g_variant_new_from_data(G_VARIANT_TYPE("at"),
            samplerates + devc->samplerates_min_index,
            (devc->samplerates_max_index - devc->samplerates_min_index + 1) * sizeof(uint64_t), TRUE, NULL, NULL);
        g_variant_builder_add(&gvb, "{sv}", "samplerates", gvar);
        *data = g_variant_builder_end(&gvb);
        break;

    case SR_CONF_TRIGGER_MATCH:
        *data = std_gvar_array_i32(ARRAY_AND_SIZE(trigger_matches));
        break;

    /* Task 10.1-10.4: config_list now returns standard GVariant string
     * arrays via g_variant_new_strv (replaces fork-style
     * g_variant_new_uint64((uintptr_t)&array) bare-pointer cast).
     * View layer reads via g_variant_get_strv. */
    case SR_CONF_OPERATION_MODE:
        *data = g_variant_new_strv(ARRAY_AND_SIZE(opmode_strs));
        break;
    case SR_CONF_EX_TRIGGER_MATCH:
        *data = g_variant_new_strv(ARRAY_AND_SIZE(extern_trigger_match_strs));
        break;
    case SR_CONF_CHANNEL_MODE: {
        /* Build a filtered string list of channel modes matching the current
         * stream/buffer mode and hardware profile. Strings come from
         * channel_modes[].descr (mirrored in channel_mode_str[] for
         * std_str_idx validation in config_set). */
        const char *mode_strs[ARRAY_SIZE(channel_modes)];
        num = 0;
        for (i = 0; i < (int)ARRAY_SIZE(channel_modes); i++) {
            if (channel_modes[i].stream == devc->stream &&
                devc->profile->dev_caps.channels & (1 << i)) {
                mode_strs[num++] = channel_modes[i].descr;
            }
        }
        *data = g_variant_new_strv(mode_strs, num);
        break;
    }
    case SR_CONF_FILTER:
        *data = g_variant_new_strv(ARRAY_AND_SIZE(filter_modes));
        break;
    default:
        return SR_ERR_NA;
    }

    return SR_OK;
}

static void free_transfer(struct libusb_transfer *transfer)
{
    struct PX_context *devc = transfer->user_data;
    struct sr_dev_inst *sdi = devc->cb_data;
    unsigned int i;

    g_free(transfer->buffer);
    transfer->buffer = NULL;
    libusb_free_transfer(transfer);
    sr_info("free_transfer: devc->num_transfers = %d", devc->num_transfers);
    for (i = 0; i < devc->num_transfers; i++) {
        if (devc->transfers[i] == transfer) {
            devc->transfers[i] = NULL;
            devc->submitted_transfers--;
            break;
        }
    }

    if (devc->submitted_transfers == 0) {
        sr_info("submitted_transfers == 0");
        finish_acquisition(sdi);
    }
}

static void resubmit_transfer(struct libusb_transfer *transfer)
{
    int ret;

    if ((ret = libusb_submit_transfer(transfer)) == LIBUSB_SUCCESS) {
        return;
    } else {
        free_transfer(transfer);
        sr_info("resubmit_transfer error ");
    }

    sr_err("%s: %s", __func__, libusb_error_name(ret));
}

SR_PRIV void pxlogic_abort_acquisition(struct PX_context *devc)
{
    int i;

    devc->acq_aborted = TRUE;

    for (i = devc->num_transfers - 1; i >= 0; i--) {
        if (devc->transfers[i])
            libusb_cancel_transfer(devc->transfers[i]);
    }
}

uint64_t align_2m_64(uint64_t pix)
{
    uint64_t align_pix;
    if (pix % BUFSIZE) {
        align_pix = (pix / BUFSIZE + 1) * BUFSIZE;
    } else {
        align_pix = pix;
    }
    return align_pix;
}

uint64_t align_4k(uint64_t pix)
{
    uint64_t align_pix;
    uint64_t align = 4096;
    if (pix % align) {
        align_pix = (pix / align + 1) * align;
    } else {
        align_pix = pix;
    }
    return align_pix;
}

SR_PRIV uint64_t px_channel_depth(const struct sr_dev_inst *sdi)
{
    struct PX_context *devc = sdi->priv;
    int ch_num = en_ch_num(sdi);
    return (devc->profile->dev_caps.hw_depth / (ch_num ? ch_num : 1)) & ~SAMPLES_ALIGN;
}

static void set_trigger(const struct sr_dev_inst *sdi)
{
    /* Upstream trigger API migration.
     *
     * Fork era read trigger config from the file-static pxlogic_trigger_cfg
     * stub that nobody populated, so trigger was effectively always disabled.
     * Now mirrors scilogic protocol.c: read sr_trigger set on the session by
     * the application layer (SigSession::sync_trigger_to_libsigrok), iterate
     * stages -> matches, and translate SR_TRIGGER_* enums into the per-channel
     * bitmasks the PXLogic FPGA expects:
     *   SR_TRIGGER_ONE     -> trig_one   (was '1')
     *   SR_TRIGGER_ZERO    -> trig_zero (was '0')
     *   SR_TRIGGER_FALLING -> trig_fall (was 'F')
     *   SR_TRIGGER_RISING  -> trig_rise (was 'R')
     *   SR_TRIGGER_EDGE    -> trig_fall | trig_rise (was 'C')
     * Trigger position (capture ratio) now comes from devc->capture_ratio,
     * set via SR_CONF_CAPTURE_RATIO by the application layer. */
    struct PX_context *devc;
    struct sr_trigger *trigger;
    struct sr_trigger_stage *stage;
    struct sr_trigger_match *match;
    const GSList *l, *m;
    int channelbit;

    devc = sdi->priv;

    devc->ch_en = en_ch_num_mask(sdi);

    devc->trig_zero = 0;
    devc->trig_one = 0;
    devc->trig_rise = 0;
    devc->trig_fall = 0;

    if (!(trigger = sr_session_trigger_get(sdi->session))) {
        sr_dbg("No session trigger set; PXLogic trigger disabled.");
    } else {
        for (l = trigger->stages; l; l = l->next) {
            stage = l->data;
            for (m = stage->matches; m; m = m->next) {
                match = m->data;
                if (!match->channel->enabled)
                    continue;
                channelbit = 1 << (match->channel->index);
                if (match->match == SR_TRIGGER_ONE) {
                    devc->trig_one |= channelbit;
                } else if (match->match == SR_TRIGGER_ZERO) {
                    devc->trig_zero |= channelbit;
                } else if (match->match == SR_TRIGGER_FALLING) {
                    devc->trig_fall |= channelbit;
                } else if (match->match == SR_TRIGGER_RISING) {
                    devc->trig_rise |= channelbit;
                } else if (match->match == SR_TRIGGER_EDGE) {
                    devc->trig_fall |= channelbit;
                    devc->trig_rise |= channelbit;
                }
            }
        }
    }

    sr_info(" devc->trig_one =  %8x", devc->trig_one);
    sr_info(" devc->trig_zero =  %8x", devc->trig_zero);
    sr_info(" devc->trig_fall =  %8x", devc->trig_fall);
    sr_info(" devc->trig_rise =  %8x", devc->trig_rise);

    uint32_t tmp_u32;
    tmp_u32 = MAX((uint32_t)(devc->capture_ratio / 100.0 * devc->limit_samples), PXLOGIC_ATOMIC_SAMPLES);

    if (devc->stream)
        tmp_u32 = MIN(tmp_u32, px_channel_depth(sdi) * 10 / 100);
    else
        tmp_u32 = MIN(tmp_u32, px_channel_depth(sdi) * PX_TRIG_MAX_PERCENT / 100);

    devc->trigger_pos_set = tmp_u32;
}

SR_PRIV int start_transfers(const struct sr_dev_inst *sdi)
{
    struct PX_context *devc = sdi->priv;
    struct sr_usb_dev_inst *usb;
    struct libusb_transfer *transfer;
    unsigned int i, num_transfers = 0;
    int ret, rc;
    unsigned char *buf = NULL;
    size_t size;
    uint64_t samples_to_send = 0, sending_total = 0;
    uint64_t sending_last = 0;
    (void)sending_last;
    usb = sdi->conn;
    unsigned int ch_num;
    unsigned int ch_en = 0;
    unsigned int gpio_mode = 0;
    unsigned int gpio_div = 0;
    uint16_t op_mode;
    uint32_t stream_mask = 0;
    uint64_t dma_size = 4096;
    (void)dma_size;
    uint64_t dma_size_min = 4096;
    (void)dma_size_min;

    uint64_t samples_ch_1s = 0;
    uint64_t samples_ch_1s_align_4k = 0;

    uint64_t usb_samples_1s = 0;

    int time_out = 0;
    uint64_t usb_buff_max = 8 * 1024 * 1024;

    devc->acq_aborted = FALSE;

    devc->usb_data_align_en = 0;

    devc->cmd_data.sync_cur_sample = 0;
    devc->cmd_data.trig_out_validset = 0;
    devc->cmd_data.real_pos = 0;

    usb_wr_reg(usb->devhdl, 16 << 2, 0);
    usb_wr_reg(usb->devhdl, 17 << 2, devc->pwm0_freq_set - 1);
    usb_wr_reg(usb->devhdl, 18 << 2, devc->pwm0_duty_set - 1);
    usb_wr_reg(usb->devhdl, 16 << 2, (uint32_t)devc->pwm0_en);
    usb_wr_reg(usb->devhdl, 19 << 2, 0);

    op_mode = devc->op_mode;
    ch_num = en_ch_num(sdi);
    ch_en = en_ch_num_mask(sdi);
    set_trigger(sdi);
    if (op_mode == OP_STREAM) {
        stream_mask = 1 << 1;
    } else {
        stream_mask = 0 << 1;
    }

    dma_size = 4096;

    if (devc->usb_speed == LIBUSB_SPEED_SUPER) {
        usb_samples_1s = 5LL * 1000 * 1000 * 1000; // 5G USB3.0
    } else {
        usb_samples_1s = 480 * 1000 * 1000; // 480M USB2.0
    }

    sr_info(" usb_samples_1s =  %d", usb_samples_1s);

    devc->ch_num = ch_num;
    sr_info(" ch_num =  %d", ch_num);
    sr_info(" devc-> ch_num =  %d", devc->ch_num);

    sr_info(" devc->limit_samples =  %d", devc->limit_samples);

    samples_ch_1s = devc->cur_samplerate / 100 / 8;
    sr_info(" samples_ch_1s =  %d", samples_ch_1s);
    samples_ch_1s_align_4k = align_4k(samples_ch_1s);
    sr_info(" samples_ch_1s_align_4k =  %d", samples_ch_1s_align_4k);

    if (devc->usb_speed == LIBUSB_SPEED_SUPER) {
        usb_buff_max = 4 * 1024 * 1024; // 4M
    } else {
        usb_buff_max = usb_samples_1s / 100 / 8; // 10ms
    }

    usb_buff_max = align_4k(usb_buff_max);
    sr_info(" usb_buff_max =  %d", usb_buff_max);

    if (samples_ch_1s_align_4k * ch_num > usb_buff_max) {
        devc->block_size = (usb_buff_max / ch_num / 4096) * 4096 * ch_num;
    } else {
        devc->block_size = samples_ch_1s_align_4k * ch_num;
    }

    sr_info(" devc->block_size =  %d", devc->block_size);
    if (devc->cur_samplerate >= 500000) {
        time_out = 100;
    } else {
        time_out = 0;
    }
    time_out = 0;
    BUFSIZE = devc->block_size;

    devc->limit_samples2Byte = devc->limit_samples * ch_num / 8;
    devc->limit_samples2Byte = devc->limit_samples2Byte + BUFSIZE;
    sr_err("BUFSIZE = %d", BUFSIZE);

    while (sending_total < devc->limit_samples2Byte && devc->limit_samples) {
        samples_to_send = MIN(devc->limit_samples2Byte - sending_total, BUFSIZE);
        sending_last = samples_to_send;
        sending_total = sending_total + samples_to_send;
        num_transfers++;
    }

    num_transfers = 4;
    sr_err("num_transfers = %d", num_transfers);

    devc->transfers = g_try_malloc0(sizeof(*devc->transfers) * (num_transfers));
    if (!devc->transfers) {
        sr_err("%s: USB transfer malloc failed.", __func__);
        return SR_ERR_MALLOC;
    }

    /* Pre-allocate deinterleave buffer (same size as USB transfer buffer) */
    if (devc->deinterleave_buf_size < BUFSIZE) {
        g_free(devc->deinterleave_buf);
        devc->deinterleave_buf = g_try_malloc(BUFSIZE);
        if (!devc->deinterleave_buf) {
            sr_err("%s: deinterleave buffer malloc failed.", __func__);
            return SR_ERR_MALLOC;
        }
        devc->deinterleave_buf_size = BUFSIZE;
    }

    rc = usb_wr_reg(usb->devhdl, 8192 + (11 << 2), 0); // set_block_start
    libusb_clear_halt(usb->devhdl, 0x82);
    libusb_clear_halt(usb->devhdl, 0x04);
    libusb_clear_halt(usb->devhdl, 0x84);
    uint32_t pwm_freq = 10000;
    uint32_t pwm_max = 120000000 / pwm_freq;

    rc = usb_wr_reg(usb->devhdl, 2 << 1, pwm_max);
    rc = usb_wr_reg(usb->devhdl, 2 << 2, (uint32_t)(devc->vth * (100.0 / 200.0) / 3.334 * pwm_max));
    sr_info(" devc->vth =  %f", devc->vth);
    sr_info(" pwm_max =  %d", pwm_max);
    sr_info(" pwm =  %d", (uint32_t)(devc->vth * (100.0 / 200.0) / 3.334 * pwm_max));
    rc = usb_wr_reg(usb->devhdl, 4 << 2, 0);
    rc = usb_wr_reg(usb->devhdl, 0 << 2, 5 | stream_mask);
    rc = usb_wr_reg(usb->devhdl, 0 << 2, 5 | stream_mask | (1 << 4));
    rc = usb_wr_reg(usb->devhdl, 0 << 2, 5 | stream_mask);

    rc = usb_wr_reg(usb->devhdl, 8 << 2, 0xffffffff);

    rc = usb_wr_reg(usb->devhdl, 7 << 2, BUFSIZE);
    rc = usb_wr_reg(usb->devhdl, 8192 + (2 << 2), BUFSIZE);

    rc = usb_wr_reg(usb->devhdl, 8192 + (9 << 2), devc->limit_samples2Byte);
    rc = usb_wr_reg(usb->devhdl, 8192 + (10 << 2), devc->limit_samples2Byte >> 32);
    sr_info(" devc->limit_samples2Byte =  %d", devc->limit_samples2Byte);

    sr_info(" devc->cur_samplerate =  %d", devc->cur_samplerate);

    if (devc->cur_samplerate == 1000000000)
        gpio_mode = 0;
    else if (devc->cur_samplerate == 500000000)
        gpio_mode = 1;
    else if (devc->cur_samplerate == 250000000)
        gpio_mode = 2;
    else if (devc->cur_samplerate == 125000000)
        gpio_mode = 3;
    else if (devc->cur_samplerate == 800000000)
        gpio_mode = 0 + 4;
    else if (devc->cur_samplerate == 400000000)
        gpio_mode = 1 + 4;
    else if (devc->cur_samplerate == 200000000)
        gpio_mode = 2 + 4;
    else if (devc->cur_samplerate == 100000000)
        gpio_mode = 3 + 4;
    else {
        gpio_mode = 3 + 4;
        if (devc->cur_samplerate == 50000000)
            gpio_div = 1;
        else if (devc->cur_samplerate == 25000000)
            gpio_div = 3;
        else if (devc->cur_samplerate == 20000000)
            gpio_div = 4;
        else if (devc->cur_samplerate == 10000000)
            gpio_div = 9;
        else if (devc->cur_samplerate == 5000000)
            gpio_div = 19;
        else if (devc->cur_samplerate == 4000000)
            gpio_div = 24;
        else if (devc->cur_samplerate == 2000000)
            gpio_div = 49;
        else if (devc->cur_samplerate == 1000000)
            gpio_div = 99;
        else if (devc->cur_samplerate == 500000)
            gpio_div = 199;
        else if (devc->cur_samplerate == 400000)
            gpio_div = 249;
        else if (devc->cur_samplerate == 200000)
            gpio_div = 499;
        else if (devc->cur_samplerate == 100000)
            gpio_div = 999;
        else if (devc->cur_samplerate == 50000)
            gpio_div = 1999;
        else if (devc->cur_samplerate == 40000)
            gpio_div = 2499;
        else if (devc->cur_samplerate == 20000)
            gpio_div = 4999;
        else if (devc->cur_samplerate == 10000)
            gpio_div = 9999;
        else if (devc->cur_samplerate == 5000)
            gpio_div = 19999;
        else if (devc->cur_samplerate == 2000)
            gpio_div = 49999;
        else
            gpio_div = 0;
    }

    rc = usb_wr_reg(usb->devhdl, 15 << 2, devc->ext_trig_mode);
    rc = usb_wr_reg(usb->devhdl, 22 << 2, devc->trig_out_en);

    sr_info(" gpio_mode =  %x", gpio_mode);
    rc = usb_wr_reg(usb->devhdl, 5 << 2, gpio_mode | (devc->clock_edge << 3));
    if (rc != 0)
        sr_info("usb_wr_reg gpio_mode error : rc =  %d", rc);
    else {
        sr_info("usb_wr_reg gpio_mode success : rc =  %d", rc);
    }
    sr_info(" gpio_div =  %d", gpio_div);
    rc = usb_wr_reg(usb->devhdl, 6 << 2, gpio_div);
    if (rc != 0)
        sr_info("usb_wr_reg gpio_div error : rc =  %d", rc);
    else {
        sr_info("usb_wr_reg gpio_div success : rc =  %d", rc);
    }

    usb_wr_reg(usb->devhdl, 8192 + (19 << 2), ch_num);
    usb_wr_reg(usb->devhdl, 8192 + (20 << 2), devc->trigger_pos_set);

    rc = usb_wr_reg(usb->devhdl, 8192 + (11 << 2), 0); // set_block_start
    rc = usb_rd_reg(usb->devhdl, 6 << 2, &gpio_div);
    if (rc != 0) {
        sr_info("usb_rd_reg gpio_div error : rc =  %d", rc);
    } else {
        sr_info("gpio_div  =  %d", gpio_div);
        sr_info("usb_rd_reg gpio_div success : rc =  %d", rc);
    }

    sr_info(" ch_en =  %x", ch_en);
    rc = usb_wr_reg(usb->devhdl, 4 << 2, ch_en);
    if (rc != 0)
        sr_info("usb_wr_reg ch_en error : rc =  %d", rc);
    else {
        sr_info("usb_wr_reg ch_en success : rc =  %d", rc);
    }

    rc = usb_wr_reg(usb->devhdl, 0 << 2, 0 | stream_mask | (devc->filter << 3));
    rc = usb_wr_reg(usb->devhdl, 9 << 2, devc->trig_zero);
    rc = usb_wr_reg(usb->devhdl, 10 << 2, devc->trig_one);
    rc = usb_wr_reg(usb->devhdl, 11 << 2, devc->trig_rise);
    rc = usb_wr_reg(usb->devhdl, 12 << 2, devc->trig_fall);

    rc = usb_wr_reg(usb->devhdl, 8 << 2, 0x0);

    devc->num_transfers = 0;
    devc->submitted_transfers = 0;
    devc->rece_transfers = 0;
    devc->send_total = num_transfers * BUFSIZE;

    for (i = 0; i < num_transfers; i++) {

        size = BUFSIZE;

        if (!(buf = g_try_malloc(BUFSIZE))) {
            sr_err("%s: USB transfer buffer malloc failed.", __func__);
            return SR_ERR_MALLOC;
        }

        transfer = libusb_alloc_transfer(0);
        transfer->actual_length = 0;
        libusb_fill_bulk_transfer(transfer, usb->devhdl, 0x82, buf, size, (libusb_transfer_cb_fn)receive_transfer, devc, time_out);
        if ((ret = libusb_submit_transfer(transfer)) != 0) {
            sr_err("%s: Failed to submit transfer: %s.",
                __func__, libusb_error_name(ret));
            libusb_free_transfer(transfer);
            g_free(buf);
            return SR_ERR;
        } else {
            sr_info("success   submit transfer");
            devc->transfers[i] = transfer;
            devc->num_transfers++;
            devc->submitted_transfers++;
        }
    }

    return SR_OK;
}

/* Callback handling data */
static void receive_transfer(struct libusb_transfer *transfer)
{
    struct PX_context *devc = transfer->user_data;
    struct sr_dev_inst *sdi = devc->cb_data;
    struct sr_datafeed_packet packet;
    struct sr_datafeed_logic logic;
    uint64_t samples_to_send = 0, sending_now;
    (void)samples_to_send;
    uint64_t offset = 0;

    devc->buf = transfer->buffer;
    sr_info("%llu: receive_transfer(): status %d; timeout %d; received %d bytes.",
        g_get_monotonic_time(), transfer->status, transfer->timeout, transfer->actual_length);

    if (devc->acq_aborted) {
        free_transfer(transfer);
        return;
    }

    switch (transfer->status) {
    case LIBUSB_TRANSFER_STALL:
    case LIBUSB_TRANSFER_NO_DEVICE:
        pxlogic_abort_acquisition(devc);
        free_transfer(transfer);
        return;
    case LIBUSB_TRANSFER_CANCELLED:
    case LIBUSB_TRANSFER_COMPLETED:
    case LIBUSB_TRANSFER_TIMED_OUT:
        break;
    default:
        break;
    }

    if (transfer->actual_length != 0 && transfer->status == LIBUSB_TRANSFER_COMPLETED) {
        devc->rece_transfers++;
        if (devc->limit_samples) {
            if (transfer->actual_length == devc->block_size) {
                samples_to_send = transfer->actual_length;
            } else {
                samples_to_send = transfer->actual_length;
            }
        } else {
            samples_to_send = transfer->actual_length;
        }

        if (samples_to_send > 0 && !devc->stop) {
            sending_now = samples_to_send;
            if (devc->mode == PXLOGIC_MODE_LOGIC) {
                if (devc->op_mode == OP_BUFFER || (devc->op_mode == OP_STREAM && devc->is_loop == 0)
                ) {
                    if (devc->samples_counter + (sending_now * 8) / devc->ch_num >= devc->limit_samples) {
                        sending_now = (devc->limit_samples - devc->samples_counter) * devc->ch_num / 8;
                        devc->samples_counter = devc->limit_samples;

                    } else {
                        devc->samples_counter = devc->samples_counter + (sending_now * 8) / devc->ch_num;
                    }
                }
            }
            if (devc->usb_data_align_en) {
                offset = (devc->ch_num - (64 % devc->ch_num)) * 8;
                sr_info("usb_data_align_en");
            }
            devc->usb_data_align_en = 0;
            offset = 0;
            {
                if (devc->mode == PXLOGIC_MODE_LOGIC) {
                    uint64_t data_len = sending_now - offset;
                    int ch_num = devc->ch_num;
                    int unitsize = ch_num / 8;

                    packet.type = SR_DF_LOGIC;
                    packet.payload = &logic;

                    /* Forward raw channel-block (LA_CROSS_DATA) directly — no
                     * deinterleave in driver. Conversion to per-channel chunk
                     * tree is done by LogicSnapshot::append_cross_payload on
                     * the PXView side. This avoids the ~100ms/4MB deinterleave
                     * bottleneck on the USB receive path (v1.49 architecture). */
                    logic.length = data_len;
                    logic.unitsize = unitsize;
                    logic.format = LA_CROSS_DATA;
                    logic.data = transfer->buffer + offset;

                    sr_session_send(sdi, &packet);
                    devc->samples_counter_div2 = devc->samples_counter / 2;
                    devc->mstatus.trig_hit = 1;
                    devc->mstatus.vlen = devc->block_size;
                    devc->mstatus.captured_cnt0 = devc->samples_counter;
                    devc->mstatus.captured_cnt1 = devc->samples_counter >> 8;
                    devc->mstatus.captured_cnt2 = devc->samples_counter >> 16;
                    devc->mstatus.captured_cnt3 = devc->samples_counter >> 24;
                }
            }
        }
    }

    if ((devc->mode == PXLOGIC_MODE_LOGIC || devc->instant) && devc->limit_samples && devc->samples_counter >= devc->limit_samples
    ) {
        sr_dbg("last  transfer");
        devc->stop = TRUE;

        pxlogic_abort_acquisition(devc);
        free_transfer(transfer);
    } else if (devc->stop != TRUE) {
        resubmit_transfer(transfer);
    }

    if (transfer->status == LIBUSB_TRANSFER_COMPLETED) {
        if (devc->block_size != (uint32_t)transfer->actual_length && devc->usb_speed != LIBUSB_SPEED_SUPER) {
            devc->usb_data_align_en = 1;
        } else {
            devc->usb_data_align_en = 0;
        }
    }
}

static int receive_data2(int fd, int revents, void *cb_data)
{
    const struct sr_dev_inst *sdi = cb_data;
    struct PX_context *devc = sdi->priv;
    struct drv_context *drvc;
    struct timeval tv;
    int ret = 0;
    uint32_t cur_sample;
    int completed = 0;
    struct sr_usb_dev_inst *usb;

    (void)fd;
    (void)revents;
    usb = sdi->conn;
    tv.tv_sec = tv.tv_usec = 0;
    drvc = di->context;
    libusb_handle_events_timeout_completed(drvc->sr_ctx->libusb_ctx, &tv, &completed);

    if ((devc->mode == PXLOGIC_MODE_LOGIC || devc->instant) && devc->limit_samples && devc->samples_counter >= devc->limit_samples) {
        return TRUE;
    }

    if ((devc->mode == PXLOGIC_MODE_LOGIC || devc->instant) && devc->limit_samples && devc->samples_counter == 0) {
        if (devc->cmd_data.trig_out_validset == 0) {
            ret = command_ctl_rddata(usb->devhdl, &(devc->cmd_data));
            if (ret == SR_OK) {

                if (devc->cmd_data.sync_cur_sample > devc->trigger_pos_set) {
                    cur_sample = devc->trigger_pos_set;

                } else {
                    cur_sample = devc->cmd_data.sync_cur_sample;
                }

                devc->mstatus.trig_hit = devc->cmd_data.trig_out_validset;
                devc->mstatus.vlen = devc->block_size;
                devc->mstatus.captured_cnt0 = cur_sample;
                devc->mstatus.captured_cnt1 = cur_sample >> 8;
                devc->mstatus.captured_cnt2 = cur_sample >> 16;
                devc->mstatus.captured_cnt3 = cur_sample >> 24;
                if (devc->op_mode == OP_STREAM && devc->is_loop == 1) {

                } else {
                    if (devc->cmd_data.trig_out_validset) {
                        devc->trigger_pos_set = devc->cmd_data.real_pos;
                        if (devc->trig_one | devc->trig_zero | devc->trig_fall | devc->trig_rise) {
                            /* Emit SR_DF_TRIGGER via upstream helper (was
                             * fork-era set_trigger_pos with custom payload). */
                            std_session_send_df_trigger(sdi);
                        }
                    }
                }

            }
        }
        return TRUE;
    }

    return TRUE;
}

static int hw_dev_acquisition_start(const struct sr_dev_inst *sdi)
{
    struct PX_context *devc = sdi->priv;
    struct sr_usb_dev_inst *usb;
    struct drv_context *drvc;
    int rc;
    (void)usb;
    drvc = di->context;
    if (sdi->status != SR_ST_ACTIVE)
        return SR_ERR_DEV_CLOSED;

    devc->samples_counter = 0;
    devc->pre_index = 0;
    devc->mstatus.captured_cnt0 = 0;
    devc->mstatus.captured_cnt1 = 0;
    devc->mstatus.captured_cnt2 = 0;
    devc->mstatus.captured_cnt3 = 0;
    devc->stop = FALSE;
    devc->samples_not_sent = 0;

    devc->trigger_stage = 0;
    usb = sdi->conn;
    devc->cb_data = (void *)sdi;

    sr_dbg("start    acquisition.");

    /* Setup USB event source using upstream usb_source_add */
    usb_source_add(sdi->session, drvc->sr_ctx, 5, receive_data2, (void *)sdi);

    std_session_send_df_header(sdi);

    start_transfers(devc->cb_data);
    sr_dbg("start_transfers");

    return SR_OK;
}

static void finish_acquisition(struct sr_dev_inst *sdi)
{
    struct PX_context *const devc = sdi->priv;
    struct sr_datafeed_packet packet;
    struct drv_context *drvc;

    (void)devc;
    devc->stop = TRUE;

    /* Send last packet. */
    packet.type = SR_DF_END;
    packet.payload = NULL;
    sr_session_send(sdi, &packet);

    drvc = di->context;
    usb_source_remove(sdi->session, drvc->sr_ctx);

    devc->num_transfers = 0;
    g_free(devc->transfers);

    sr_dbg("finish_acquisition");
}

static int hw_dev_acquisition_stop(struct sr_dev_inst *sdi)
{
    struct PX_context *const devc = sdi->priv;
    pxlogic_abort_acquisition(devc);
    sr_dbg("Stopping acquisition.");

    return SR_OK;
}

static struct sr_dev_driver pxlogic_driver_info = {
    .name = "pxlogic",
    .longname = "PXLogic",
    .api_version = 1,
    .init = hw_init,
    .cleanup = hw_cleanup,
    .scan = scan,
    .dev_list = std_dev_list,
    .dev_clear = std_dev_clear,
    .config_get = config_get,
    .config_set = config_set,
    .config_list = config_list,
    .dev_open = hw_dev_open,
    .dev_close = hw_dev_close,
    .dev_acquisition_start = hw_dev_acquisition_start,
    .dev_acquisition_stop = hw_dev_acquisition_stop,
    .context = NULL,
};
SR_REGISTER_DEV_DRIVER(pxlogic_driver_info);
