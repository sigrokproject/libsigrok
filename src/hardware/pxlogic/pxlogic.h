/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Bert Vermeulen <bert@biot.com>
 * Copyright (C) 2013 DreamSourceLab <support@dreamsourcelab.com>
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

#ifndef LIBSIGROK_HARDWARE_PXLOGIC_PROTOCOL_H
#define LIBSIGROK_HARDWARE_PXLOGIC_PROTOCOL_H

#include <glib.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <libusb.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <inttypes.h>
#include <sys/stat.h>

#include "usb_ctrl.h"

#define LOG_PREFIX "pxlogic"

#define PXVIEW_BL_EN 0
#define NUM_TRIGGER_STAGES 16
#define FIRMWARE_VERSION 0x56900028
#define FIRMWARE_BL_VERSION 0x56900000
#define PWM_CLK 125000000
#define PWM_MAX 1000000

#define TRIG_CHECKID 0x55555555

#define PXLOGIC_ATOMIC_BITS 6
#define PXLOGIC_ATOMIC_SAMPLES (1 << PXLOGIC_ATOMIC_BITS)
#define PXLOGIC_ATOMIC_SIZE (1 << (PXLOGIC_ATOMIC_BITS - 3))
#define PXLOGIC_ATOMIC_MASK (0xFFFFFFFF << PXLOGIC_ATOMIC_BITS)

/* --- Fork-compatible type definitions (not in upstream libsigrok 0.6.0) --- */

/* Fork time/frequency macros (upstream only has SR_HZ/SR_KHZ/SR_MHZ/SR_GHZ) */
#define SR_NS(n) (n)
#define SR_US(n) ((n) * (uint64_t)(1000ULL))
#define SR_Mn(n) ((n) * (uint64_t)(1000000ULL))
#define SR_Gn(n) ((n) * (uint64_t)(1000000000ULL))

/* Operation modes (fork enum OPERATION_MODE) */
enum pxlogic_operation_mode {
    PXLOGIC_MODE_LOGIC = 0,
    PXLOGIC_MODE_DSO = 1,
    PXLOGIC_MODE_ANALOG = 2,
};

/* Hardware operation modes (was fork enum DSLOGIC_OPERATION_MODE2).
 * Values match scilogic protocol.h: OP_BUFFER/OP_STREAM. */
enum pxlogic_op_mode {
    OP_BUFFER = 0,
    OP_STREAM = 1,
    OP_INTEST = 2,
    OP_EXTEST = 3,
    OP_LPTEST = 4,
};

/* Task 8.4: fork LOGIC/DSO/ANALOG #defines deleted — code now uses
 * PXLOGIC_MODE_LOGIC/DSO/ANALOG directly (defined in enum
 * pxlogic_operation_mode above). */
/* Task 8.1: fork SR_TH_3V3 / SR_TH_5V0 deleted — SR_CONF_VTH uses double. */
/* Task 8.2: fork SR_FILTER_NONE / SR_FILTER_1T deleted — SR_CONF_FILTER uses
 * string-based std_str_idx against filter_modes[] (driver-internal literals). */
/* Task 8.3: fork SR_TEST_NONE / SR_TEST_INTERNAL / SR_TEST_EXTERNAL deleted —
 * devc->test_mode stores 0 (none) / 1 (internal) directly. SR_CONF_TEST case
 * was already removed in Task 5.2. */

/* Fork constants */
#define PX_TRIG_MAX_PERCENT  90
#define SAMPLES_ALIGN  1023ULL

/* Task 10.7: struct sr_list_item deleted — config_list now returns standard
 * GVariant string arrays via g_variant_new_strv. */
/* Task 11.1: struct lang_text_map_item deleted — i18n moved to View layer
 * Qt tr() / LangResource system. */

/* Trigger position struct (fork struct ds_trigger_pos, ported as pxlogic_trigger_pos) */
struct pxlogic_trigger_pos {
    uint32_t check_id;
    uint32_t real_pos;
    uint32_t ram_saddr;
    uint32_t remain_cnt_l;
    uint32_t remain_cnt_h;
    uint32_t status;
};

/* Simplified status struct (fork struct sr_status, only fields pxlogic uses) */
struct pxlogic_status {
    uint8_t trig_hit;
    uint8_t captured_cnt3;
    uint8_t captured_cnt2;
    uint8_t captured_cnt1;
    uint8_t captured_cnt0;
    uint16_t pkt_id;
    uint32_t vlen;
};

/* --- Device capability/profile structs --- */

struct PX_caps {
  uint64_t mode_caps;
  uint64_t feature_caps;
  uint64_t channels;
  uint64_t hw_depth;
  uint8_t intest_channel;
  uint16_t default_channelmode;
  uint64_t default_timebase;
};

struct PX_profile {
  uint16_t vid;
  uint16_t pid;
  enum libusb_speed usb_speed;
  uint32_t logic_mode;

  const char *vendor;
  const char *model;
  const char *model_version;

  const char *firmware;
  uint32_t firmware_version;
  const char *firmware_bl;
  uint32_t firmware_bl_version;
  const char *fpga_bit;
  const char *fpga_rst_bit;

  struct PX_caps dev_caps;
};

enum PX_CHANNEL_ID {
  BUFFER_LOGIC250x32 = 0,
  BUFFER_LOGIC250x16,
  BUFFER_LOGIC500x16,
  BUFFER_LOGIC1000x8,

  STREAM_LOGIC50x32,
  STREAM_LOGIC125x16,
  STREAM_LOGIC250x8,
  STREAM_LOGIC500x4,
  STREAM_LOGIC1000x2,

  STREAM_LOGIC200x1,
  STREAM_LOGIC100x2,
  STREAM_LOGIC50x4,
  STREAM_LOGIC25x8,
  STREAM_LOGIC10x16,
  STREAM_LOGIC5x32
};

struct PX_channels {
  enum PX_CHANNEL_ID id;
  int mode;          /* enum pxlogic_operation_mode */
  int type;          /* SR_CHANNEL_LOGIC etc. */
  gboolean stream;
  uint16_t num;
  uint8_t unit_bits;
  uint64_t default_samplerate;
  uint64_t default_samplelimit;
  uint64_t min_samplerate;
  uint64_t max_samplerate;

  const char *descr;
};

struct PX_context {
  const struct PX_profile *profile;

  int pipe_fds[2];
  GIOChannel *channel;
  uint64_t cur_samplerate;
  uint64_t limit_samples;
  uint64_t limit_samples2Byte;
  uint64_t limit_samples_show;
  uint64_t limit_msec;
  uint8_t sample_generator;
  uint64_t samples_counter;
  uint64_t samples_counter_div2;
  volatile int ch_num;
  void *cb_data;
  int64_t starttime;
  int stop;
  uint64_t timebase;
  enum PX_CHANNEL_ID ch_mode;
  uint16_t samplerates_min_index;
  uint16_t samplerates_max_index;
  gboolean instant;
  uint8_t max_height;
  uint64_t samples_not_sent;

  uint8_t *buf;
  uint64_t pre_index;
  struct pxlogic_status mstatus;

  unsigned int num_transfers;
  unsigned int submitted_transfers;
  unsigned int rece_transfers;
  struct libusb_transfer **transfers;
  int *usbfd;
  enum libusb_speed usb_speed;
  int send_total;

  int trigger_stage;
  uint16_t trigger_mask;
  uint16_t trigger_value;
  uint16_t trigger_edge;
  uint8_t trigger_slope;
  uint8_t trigger_source;
  uint16_t op_mode;
  gboolean stream;
  gboolean rle_mode;
  gboolean rle_support;
  uint8_t test_mode;
  uint32_t block_size;
  gboolean acq_aborted;
  double vth;
  gboolean clock_edge;
  uint16_t ext_trig_mode;
  gboolean trig_out_en;
  uint16_t filter;
  uint32_t ch_en;
  uint32_t trig_zero;
  uint32_t trig_one;
  uint32_t trig_rise;
  uint32_t trig_fall;
  uint16_t trig_mask0[NUM_TRIGGER_STAGES];
  uint16_t trig_mask1[NUM_TRIGGER_STAGES];
  uint16_t trig_value0[NUM_TRIGGER_STAGES];
  uint16_t trig_value1[NUM_TRIGGER_STAGES];
  uint16_t trig_edge0[NUM_TRIGGER_STAGES];
  uint16_t trig_edge1[NUM_TRIGGER_STAGES];
  uint16_t trig_logic0[NUM_TRIGGER_STAGES];
  uint16_t trig_logic1[NUM_TRIGGER_STAGES];
  uint32_t trig_count[NUM_TRIGGER_STAGES];
  /* Capture ratio (trigger position as 0..100 percent). Replaces the
   * fork-era pxlogic_trigger_cfg.trigger_pos stub that nobody populated.
   * Set via SR_CONF_CAPTURE_RATIO by the application layer before capture. */
  uint64_t capture_ratio;
  double stream_buff_size;
  double stream_mem_buff_size;
  gboolean disk_cache_enable;
  char *disk_cache_path;

  gboolean pwm0_en;
  double pwm0_freq;
  double pwm0_duty;

  uint32_t pwm0_freq_set;
  uint32_t pwm0_duty_set;

  gboolean pwm1_en;
  double pwm1_freq;
  double pwm1_duty;

  uint32_t pwm1_freq_set;
  uint32_t pwm1_duty_set;

  int is_loop;
  uint8_t usb_data_align_en;
  struct pxlogic_trigger_pos *trigger_pos;
  uint32_t trigger_pos_set;
  struct ctl_data cmd_data;

  /* Port-added fields (replace fork sr_dev_inst fields not in upstream) */
  int mode;                        /* replaces fork sdi->mode */
  struct libusb_device *usb_dev;   /* replaces fork usb->usb_dev */
  uint8_t *deinterleave_buf;       /* deinterleave buffer for LA_CROSS_DATA conversion */
  uint64_t deinterleave_buf_size;  /* allocated size of deinterleave_buf */
};

static const uint64_t samplerates[] = {
    SR_HZ(10),
    SR_HZ(20),
    SR_HZ(50),
    SR_HZ(100),
    SR_HZ(200),
    SR_HZ(500),
    SR_KHZ(1),
    SR_KHZ(2),
    SR_KHZ(5),
    SR_KHZ(10),
    SR_KHZ(20),
    SR_KHZ(40),
    SR_KHZ(50),
    SR_KHZ(100),
    SR_KHZ(200),
    SR_KHZ(400),
    SR_KHZ(500),
    SR_MHZ(1),
    SR_MHZ(2),
    SR_MHZ(4),
    SR_MHZ(5),
    SR_MHZ(10),
    SR_MHZ(20),
    SR_MHZ(25),
    SR_MHZ(50),
    SR_MHZ(100),
    SR_MHZ(125),
    SR_MHZ(200),
    SR_MHZ(250),
    SR_MHZ(400),
    SR_MHZ(500),
    SR_MHZ(800),
    SR_GHZ(1),
};

/* hardware Capabilities */
#define CAPS_MODE_LOGIC (1 << 0)
#define CAPS_MODE_ANALOG (1 << 1)
#define CAPS_MODE_DSO (1 << 2)

#define CAPS_FEATURE_NONE 0
#define CAPS_FEATURE_VTH (1 << 0)
#define CAPS_FEATURE_BUF (1 << 1)
#define CAPS_FEATURE_PREOFF (1 << 2)
#define CAPS_FEATURE_SEEP (1 << 3)
#define CAPS_FEATURE_ZERO (1 << 4)
#define CAPS_FEATURE_HMCAD1511 (1 << 5)
#define CAPS_FEATURE_USB30 (1 << 6)
#define CAPS_FEATURE_POGOPIN (1 << 7)
#define CAPS_FEATURE_ADF4360 (1 << 8)
#define CAPS_FEATURE_20M (1 << 9)
#define CAPS_FEATURE_FLASH (1 << 10)
#define CAPS_FEATURE_LA_CH32 (1 << 11)
#define CAPS_FEATURE_AUTO_VGAIN (1 << 12)

#define USB_INTERFACE_C 0
#define USB_INTERFACE_D 1

static const char *maxHeights[] = {
    "1X", "2X", "3X", "4X", "5X",
};

static const char *probe_names[] = {
    "0",  "1",  "2",  "3",  "4",  "5",  "6",  "7",  "8",  "9",  "10",
    "11", "12", "13", "14", "15", "16", "17", "18", "19", "20", "21",
    "22", "23", "24", "25", "26", "27", "28", "29", "30", "31", NULL,
};

static const char *probeMapUnits[] = {
    "V", "A", "°C", "°F", "g", "m", "m/s",
};

// C-class keys (DISK_CACHE_ENABLE/PATH, STREAM_BUFF, STREAM_MEM_BUFF) are
// NOT declared here — they are app-layer concepts served by DeviceAgent's
// app-layer config state (see deviceagent.cpp is_app_layer_key). Declaring
// them in hwoptions would cause the UI to query the driver, but the driver
// no longer implements config_get/set for these keys.
static const int hwoptions[] = {
    SR_CONF_OPERATION_MODE,
    SR_CONF_VTH,
    SR_CONF_EX_TRIGGER_MATCH,
    SR_CONF_FILTER,
    SR_CONF_CLOCK_EDGE,
    SR_CONF_TRIGGER_OUT,
    SR_CONF_PWM0_EN,
    SR_CONF_PWM0_FREQ,
    SR_CONF_PWM0_DUTY,
};

static const int32_t sessions[] = {
    SR_CONF_SAMPLERATE,
    SR_CONF_LIMIT_SAMPLES,
    SR_CONF_OPERATION_MODE,
    SR_CONF_CHANNEL_MODE,
    SR_CONF_VTH,
    SR_CONF_EX_TRIGGER_MATCH,
    SR_CONF_FILTER,
    SR_CONF_CLOCK_EDGE,
    SR_CONF_TRIGGER_OUT,
};

/* STD_CONFIG_LIST arrays (Task 7.1-7.3). Carrying SR_CONF_GET|SET|LIST
 * capability bits per upstream libsigrok 0.6.0 convention.
 * NOTE: SR_CONF_DEVICE_OPTIONS still returns hwoptions[] (legacy bare-key
 * array) for application-layer compatibility — PXView's deviceoptions.cpp
 * reads options as plain int32_t without masking capability bits, so
 * switching DEVICE_OPTIONS to STD_CONFIG_LIST would break UI binding.
 * SR_CONF_SCAN_OPTIONS is the only key routed through STD_CONFIG_LIST. */
static const uint32_t scanopts[] = {
    SR_CONF_CONN,
};

static const uint32_t drvopts[] = {
    SR_CONF_LOGIC_ANALYZER,
};

static const uint32_t devopts[] = {
    SR_CONF_OPERATION_MODE   | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
    SR_CONF_CHANNEL_MODE    | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
    SR_CONF_SAMPLERATE      | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
    SR_CONF_LIMIT_SAMPLES   | SR_CONF_GET | SR_CONF_SET,
    SR_CONF_LIMIT_MSEC      | SR_CONF_GET | SR_CONF_SET,
    SR_CONF_CAPTURE_RATIO   | SR_CONF_GET | SR_CONF_SET,
    SR_CONF_DEVICE_MODE     | SR_CONF_GET | SR_CONF_SET,
    SR_CONF_VTH             | SR_CONF_GET | SR_CONF_SET,
    SR_CONF_EX_TRIGGER_MATCH| SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
    SR_CONF_FILTER          | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
    SR_CONF_CLOCK_EDGE      | SR_CONF_GET | SR_CONF_SET,
    SR_CONF_TRIGGER_OUT     | SR_CONF_GET | SR_CONF_SET,
    SR_CONF_TRIGGER_MATCH   | SR_CONF_LIST,
};

static const struct PX_profile supported_PX[] = {
    /*
     * 32 ch old pid vid
     */
    {0x1A86,
     0x5237,
     LIBUSB_SPEED_SUPER,
     0,
     "PX_Tool",
     "PX-Logic U3 channel 32",
     NULL,
     "SCI_LOGIC.bin",
     FIRMWARE_VERSION,
     "SCI_LOGIC_BL.bin",
     FIRMWARE_BL_VERSION,
     "hspi_ddr.bin",
     "hspi_ddr_RST.bin",
     {CAPS_MODE_LOGIC,
      CAPS_FEATURE_USB30 | CAPS_FEATURE_BUF,
      (1 << BUFFER_LOGIC250x32) | (1 << BUFFER_LOGIC500x16) |
          (1 << BUFFER_LOGIC1000x8) | (1 << STREAM_LOGIC50x32) |
          (1 << STREAM_LOGIC125x16) | (1 << STREAM_LOGIC250x8) |
          (1 << STREAM_LOGIC500x4) | (1 << STREAM_LOGIC1000x2),
      SR_Gn(4), 0, BUFFER_LOGIC250x32, SR_NS(500)}},

    {0x1A86,
     0x5237,
     LIBUSB_SPEED_HIGH,
     0,
     "PX_Tool",
     "PX-Logic U2 channel 32",
     NULL,
     "SCI_LOGIC.bin",
     FIRMWARE_VERSION,
     "SCI_LOGIC_BL.bin",
     FIRMWARE_BL_VERSION,
     "hspi_ddr.bin",
     "hspi_ddr_RST.bin",
     {CAPS_MODE_LOGIC,
      CAPS_FEATURE_USB30 | CAPS_FEATURE_BUF,
      (1 << BUFFER_LOGIC250x32) | (1 << BUFFER_LOGIC500x16) |
          (1 << BUFFER_LOGIC1000x8) | (1 << STREAM_LOGIC200x1) |
          (1 << STREAM_LOGIC100x2) | (1 << STREAM_LOGIC50x4) |
          (1 << STREAM_LOGIC25x8) | (1 << STREAM_LOGIC10x16) |
          (1 << STREAM_LOGIC5x32),
      SR_Gn(4), 0, BUFFER_LOGIC500x16, SR_NS(500)}},

    // 32 ch new pid vid
    {0x16C0,
     0x05DC,
     LIBUSB_SPEED_SUPER,
     0,
     "PX_Tool",
     "PX-Logic U3 channel 32",
     NULL,
     "SCI_LOGIC.bin",
     FIRMWARE_VERSION,
     "SCI_LOGIC_BL.bin",
     FIRMWARE_BL_VERSION,
     "hspi_ddr.bin",
     "hspi_ddr_RST.bin",
     {CAPS_MODE_LOGIC,
      CAPS_FEATURE_USB30 | CAPS_FEATURE_BUF,
      (1 << BUFFER_LOGIC250x32) | (1 << BUFFER_LOGIC500x16) |
          (1 << BUFFER_LOGIC1000x8) | (1 << STREAM_LOGIC50x32) |
          (1 << STREAM_LOGIC125x16) | (1 << STREAM_LOGIC250x8) |
          (1 << STREAM_LOGIC500x4) | (1 << STREAM_LOGIC1000x2),
      SR_Gn(4), 0, BUFFER_LOGIC250x32, SR_NS(500)}},

    {0x16C0,
     0x05DC,
     LIBUSB_SPEED_HIGH,
     0,
     "PX_Tool",
     "PX-Logic U2 channel 32",
     NULL,
     "SCI_LOGIC.bin",
     FIRMWARE_VERSION,
     "SCI_LOGIC_BL.bin",
     FIRMWARE_BL_VERSION,
     "hspi_ddr.bin",
     "hspi_ddr_RST.bin",
     {CAPS_MODE_LOGIC,
      CAPS_FEATURE_USB30 | CAPS_FEATURE_BUF,
      (1 << BUFFER_LOGIC250x32) | (1 << BUFFER_LOGIC500x16) |
          (1 << BUFFER_LOGIC1000x8) | (1 << STREAM_LOGIC200x1) |
          (1 << STREAM_LOGIC100x2) | (1 << STREAM_LOGIC50x4) |
          (1 << STREAM_LOGIC25x8) | (1 << STREAM_LOGIC10x16) |
          (1 << STREAM_LOGIC5x32),
      SR_Gn(4), 0, BUFFER_LOGIC500x16, SR_NS(500)}},

    // 16 ch 1G new pid vid
    {0x16C0,
     0x05DC,
     LIBUSB_SPEED_SUPER,
     1,
     "PX_Tool",
     "PX-Logic U3 channel 16 Pro",
     NULL,
     "SCI_LOGIC.bin",
     FIRMWARE_VERSION,
     "SCI_LOGIC_BL.bin",
     FIRMWARE_BL_VERSION,
     "hspi_ddr.bin",
     "hspi_ddr_RST.bin",
     {CAPS_MODE_LOGIC,
      CAPS_FEATURE_USB30 | CAPS_FEATURE_BUF,
      (1 << BUFFER_LOGIC500x16) | (1 << BUFFER_LOGIC1000x8) |
          (1 << STREAM_LOGIC125x16) | (1 << STREAM_LOGIC250x8) |
          (1 << STREAM_LOGIC500x4) | (1 << STREAM_LOGIC1000x2),
      SR_Gn(4), 0, BUFFER_LOGIC500x16, SR_NS(500)}},

    {0x16C0,
     0x05DC,
     LIBUSB_SPEED_HIGH,
     1,
     "PX_Tool",
     "PX-Logic U2 channel 16 Pro",
     NULL,
     "SCI_LOGIC.bin",
     FIRMWARE_VERSION,
     "SCI_LOGIC_BL.bin",
     FIRMWARE_BL_VERSION,
     "hspi_ddr.bin",
     "hspi_ddr_RST.bin",
     {CAPS_MODE_LOGIC,
      CAPS_FEATURE_USB30 | CAPS_FEATURE_BUF,
      (1 << BUFFER_LOGIC500x16) | (1 << BUFFER_LOGIC1000x8) |
          (1 << STREAM_LOGIC200x1) | (1 << STREAM_LOGIC100x2) |
          (1 << STREAM_LOGIC50x4) | (1 << STREAM_LOGIC25x8) |
          (1 << STREAM_LOGIC10x16),
      SR_Gn(4), 0, BUFFER_LOGIC500x16, SR_NS(500)}},

    // 16 ch 500M new pid vid
    {0x16C0,
     0x05DC,
     LIBUSB_SPEED_SUPER,
     2,
     "PX_Tool",
     "PX-Logic U3 channel 16 Plus",
     NULL,
     "SCI_LOGIC.bin",
     FIRMWARE_VERSION,
     "SCI_LOGIC_BL.bin",
     FIRMWARE_BL_VERSION,
     "hspi_ddr.bin",
     "hspi_ddr_RST.bin",
     {CAPS_MODE_LOGIC,
      CAPS_FEATURE_USB30 | CAPS_FEATURE_BUF,
      (1 << BUFFER_LOGIC500x16) | (1 << STREAM_LOGIC125x16) |
          (1 << STREAM_LOGIC250x8) | (1 << STREAM_LOGIC500x4),
      SR_Gn(2), 0, BUFFER_LOGIC500x16, SR_NS(500)}},

    {0x16C0,
     0x05DC,
     LIBUSB_SPEED_HIGH,
     2,
     "PX_Tool",
     "PX-Logic U2 channel 16 Plus",
     NULL,
     "SCI_LOGIC.bin",
     FIRMWARE_VERSION,
     "SCI_LOGIC_BL.bin",
     FIRMWARE_BL_VERSION,
     "hspi_ddr.bin",
     "hspi_ddr_RST.bin",
     {CAPS_MODE_LOGIC,
      CAPS_FEATURE_USB30 | CAPS_FEATURE_BUF,
      (1 << BUFFER_LOGIC500x16) | (1 << STREAM_LOGIC200x1) |
          (1 << STREAM_LOGIC100x2) | (1 << STREAM_LOGIC50x4) |
          (1 << STREAM_LOGIC25x8) | (1 << STREAM_LOGIC10x16),
      SR_Gn(2), 0, BUFFER_LOGIC500x16, SR_NS(500)}},

    // 16 ch 250M new pid vid
    {0x16C0,
     0x05DC,
     LIBUSB_SPEED_SUPER,
     3,
     "PX_Tool",
     "PX-Logic U3 channel 16 Base",
     NULL,
     "SCI_LOGIC.bin",
     FIRMWARE_VERSION,
     "SCI_LOGIC_BL.bin",
     FIRMWARE_BL_VERSION,
     "hspi_ddr.bin",
     "hspi_ddr_RST.bin",
     {CAPS_MODE_LOGIC,
      CAPS_FEATURE_USB30 | CAPS_FEATURE_BUF,
      (1 << BUFFER_LOGIC250x16) | (1 << STREAM_LOGIC125x16) |
          (1 << STREAM_LOGIC250x8),
      SR_Gn(1), 0, BUFFER_LOGIC250x16, SR_NS(500)}},

    {0x16C0,
     0x05DC,
     LIBUSB_SPEED_HIGH,
     3,
     "PX_Tool",
     "PX-Logic U2 channel 16 Base",
     NULL,
     "SCI_LOGIC.bin",
     FIRMWARE_VERSION,
     "SCI_LOGIC_BL.bin",
     FIRMWARE_BL_VERSION,
     "hspi_ddr.bin",
     "hspi_ddr_RST.bin",
     {CAPS_MODE_LOGIC,
      CAPS_FEATURE_USB30 | CAPS_FEATURE_BUF,
      (1 << BUFFER_LOGIC250x16) | (1 << STREAM_LOGIC200x1) |
          (1 << STREAM_LOGIC100x2) | (1 << STREAM_LOGIC50x4) |
          (1 << STREAM_LOGIC25x8) | (1 << STREAM_LOGIC10x16),
      SR_Gn(1), 0, BUFFER_LOGIC250x16, SR_NS(500)}},

    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, {0, 0, 0, 0, 0, 0, 0}}};

#endif
