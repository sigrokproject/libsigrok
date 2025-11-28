/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2017 Karl Palsson <karlp@tweak.net.au>
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

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <glib.h>
#include <libusb.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "LNSS"

#define USB_INTERFACE			    0

#define LNSS_VID                    0x4d8
#define LNSS_PID                    0xf4b5
#define LNSS_NUM_CHANNELS           8
#define LNSS_NUM_AN_CHANNELS        2
#define LNSS_MAX_SAMPLERATE         100000000UL
#define LNSS_MIN_SAMPLERATE         195312
#define LNSS_MAX_ACQUISITION        (4 * 1024 * 1024) // Size of memory chip on device
#define LNSS_MIN_ACQUISITION        2048
#define LNSS_INPUT_DECIMATION_MAX   9

#define SZ_HDR                      64
#define SZ_OVERVIEW                 (LNSS_MIN_ACQUISITION * LNSS_NUM_AN_CHANNELS)
#define PACKAGE_MAX                 64

#define HDR_N_REGS                  30
#define HDR_N_STROBES               5

#define I2C_MAX_WRITE_LENGTH        27
#define I2C_MAX_WRITE_LENGTH_BULK   29

#define FPGA_I2C_ADDRESS_REG        0x0C
#define FPGA_I2C_ADDRESS_ROM        0x0D
#define FPGA_I2C_ADDRESS_AWG        0x0E

#define FLASH_USER_ADDRESS_MASK     0x0FFF

/**
 * TRIGGER_MODE Register Bits
 * | ACQ Mode[1:0] | Edge[1:0] | Source | Channel | Mode [1:0] |
 */
#define TRG_ACQ_AUTO        (0 << 6)    // Auto acquisition
#define TRG_ACQ_NORMAL      (1 << 6)    // Normal acquisition
#define TRG_ACQ_SINGLE      (2 << 6)    // Single acquisition
#define TRG_EDGE_RISING     (0 << 4)    // Trigger on rising edge
#define TRG_EDGE_FALLING    (1 << 4)    // Trigger on falling
#define TRG_EDGE_ANY        (2 << 4)    // Trigger on any edge
#define TRG_SOURCE_CHANNEL  (0 << 3)    // A channel as trigger source
#define TRG_SOURCE_EXT      (1 << 3)    // External trigger source
#define TRG_CHANNEL_A       (0 << 2)    // Trigger on analog channel A
#define TRG_CHANNEL_B       (1 << 2)    // Trigger on analog channel B
#define TRG_MODE_EDGE       (0 << 0)    // Trigger on edge
#define TRG_MODE_TIMEOUT    (1 << 0)
#define TRG_MODE_PULSE      (2 << 0)
#define TRG_MODE_DIGITAL    (3 << 0)    // Trigger on digital logic

#define DEFAULT_SAMPLERATE      6500000 //MHz, should match a value in samplerates array
#define DEFAULT_CAPTURE_RACIO   10
#define DEFAULT_NUM_SAMPLES     LNSS_MIN_ACQUISITION

enum pic_cmd {
	PICCMD_PIC_VERSION = 1,
	PICCMD_PIC_WRITE = 2,
	PICCMD_PIC_READ = 3,
	PICCMD_PIC_RESET = 4,
	PICCMD_PIC_BOOTLOADER = 5,
	PICCMD_EEPROM_READ = 6,
	PICCMD_EEPROM_WRITE = 7,
	PICCMD_FLASH_ROM_READ = 8,
	PICCMD_FLASH_ROM_WRITE = 9,
	PICCMD_I2C_WRITE = 10,
	PICCMD_I2C_READ = 11,
	PICCMD_PROGRAM_FPGA_START = 12,
	PICCMD_PROGRAM_FPGA_END = 13,
	PICCMD_I2C_WRITE_START = 14,
	PICCMD_I2C_WRITE_BULK = 15,
	PICCMD_I2C_WRITE_STOP = 16
};

enum controller_op {
    READ = 0,
    WRITE,
    WRITE_BEGIN,
    WRITE_BODY,
    WRITE_END
};

enum controller {
    CTRL_PIC = 0,
    CTRL_ROM = 1,
    CTRL_FLASH = 2,
    CTRL_FPGA = 3,
    CTRL_AWG = 4
};

enum ROM {
    FW_GIT0,
    FW_GIT1,
    FW_GIT2,
    FW_GIT3,
    SPI_RECEIVED_VALUE,
    STROBES0,
    STROBES1,
    STROBES2,
};

enum ADC {
    POWER_MANAGMENT = 0,
    OUTPUT_FORMAT,
    OUTPUT_PWR_MNGMNT,
    DATA_CLK_TIMING,
    CHA_TERMINATION,
    CHB_TERMINATION,
    FORMAT_PATTERN,
    COMMON_MODE,
    ADC_RESERVED,
    SOFT_RESET
};

/**
 *
 */
enum REG {
    STROBE_UPDATE = 0,           // strobe = (STROBE_UPDATE << 1) | state
    SPI_ADDRESS = 1,             // ADC Register address
    SPI_WRITE_VALUE = 2,         // ADC Register value
    DIVIDER_MULTIPLIER = 3,      // Selects input range. CHA bits 3:0, CHB bits 7:4
    CHA_YOFFSET_VOLTAGE = 4,     //
    CHB_YOFFSET_VOLTAGE = 5,     //
    TRIGGER_PWM = 6,             //
    TRIGGER_LEVEL = 7,           // 0x80 -> 0V
    TRIGGER_MODE = 8,            // See trigger mode bits
    TRIGGER_PW_MIN_B0 = 9,       //
    TRIGGER_PW_MIN_B1 = 10,      //
    TRIGGER_PW_MIN_B2 = 11,      //
    TRIGGER_PW_MAX_B0 = 12,      //
    TRIGGER_PW_MAX_B1 = 13,      //
    TRIGGER_PW_MAX_B2 = 14,      //
    INPUT_DECIMATION = 15,       // sample rate = 100MHz / 2^INPUT_DECIMATION
    ACQUISITION_DEPTH = 16,      // number of samples = 2048 * 2^ACQUISITION_DEPTH
    TRIGGERHOLDOFF_B0 = 17,      // Trigger holdoff, number of samples before trigger. Note a delay must be applied depending sample rate
    TRIGGERHOLDOFF_B1 = 18,      //
    TRIGGERHOLDOFF_B2 = 19,      //
    TRIGGERHOLDOFF_B3 = 20,      //
    VIEW_DECIMATION = 21,
    VIEW_OFFSET_B0 = 22,
    VIEW_OFFSET_B1 = 23,
    VIEW_OFFSET_B2 = 24,
    VIEW_ACQUISITIONS = 25,
    VIEW_BURSTS = 26,             // Number of data bursts = 2^VIEW_BURSTS
    VIEW_EXCESS_B0 = 27,
    VIEW_EXCESS_B1 = 28,
    DIGITAL_TRIGGER_RISING = 29,  // Trigger bit masks
    DIGITAL_TRIGGER_FALLING = 30, //
    DIGITAL_TRIGGER_HIGH = 31,    //
    DIGITAL_TRIGGER_LOW = 32,     //
    DIGITAL_OUT = 33,
    GENERATOR_DECIMATION_B0 = 34,
    GENERATOR_DECIMATION_B1 = 35,
    GENERATOR_DECIMATION_B2 = 36,
    GENERATOR_SAMPLES_B0 = 37,
    GENERATOR_SAMPLES_B1 = 38,
};

enum STR {
    GLOBAL_RESET = 0,
    INIT_SPI_TRANSFER = 1,      // 0: Start spi transfer, 1: Stop
    GENERATOR_TO_AWG = 2,
    LA_ENABLE = 3,              // Enables Logic analyzer
    SCOPE_ENABLE = 4,           // Must be enable to perform acquisitions
    SCOPE_UPDATE = 5,           // Must be set to apply register values
    FORCE_TRIGGER = 6,
    VIEW_UPDATE = 7,            // Sends updated view packet (after change view registers set this bit to get an updated view packet)
    VIEW_SEND_OVERVIEW = 8,     // Enables Overview data packets
    VIEW_SEND_PARTIAL = 9,
    ACQ_START = 10,             // Start acquisition
    ACQ_STOP = 11,              // Stop acquisition
    CHA_DCCOUPLING = 12,
    CHB_DCCOUPLING = 13,
    ENABLE_ADC = 14,            // Must be enable to perform acquisitions
    ENABLE_NEG = 15,
    ENABLE_RAM = 16,
    DOUT_3V_5V = 17,
    EN_OPAMP_B = 18,
    GENERATOR_TO_DIGITAL = 19,
    ROLL = 20,
    LA_CHANNEL = 21,            // Selects which channel will hold LA samples, 0: CHA 1: CHB
};

/**
 * Data packet header registers
 */
enum HDR_REGS {
    HDR_TRIGGER_LEVEL = 0,
    HDR_TRIGGER_MODE,
    HDR_TRIGGERHOLDOFF_B0,
    HDR_TRIGGERHOLDOFF_B1,
    HDR_TRIGGERHOLDOFF_B2,
    HDR_TRIGGERHOLDOFF_B3,
    HDR_CHA_YOFFSET_VOLTAGE,
    HDR_CHB_YOFFSET_VOLTAGE,
    HDR_DIVIDER_MULTIPLIER,
    HDR_INPUT_DECIMATION,
    HDR_TRIGGER_PW_MIN_B0,
    HDR_TRIGGER_PW_MIN_B1,
    HDR_TRIGGER_PW_MIN_B2,
    HDR_TRIGGER_PW_MAX_B0,
    HDR_TRIGGER_PW_MAX_B1,
    HDR_TRIGGER_PW_MAX_B2,
    HDR_TRIGGER_PWM,
    HDR_DIGITAL_TRIGGER_RISING,
    HDR_DIGITAL_TRIGGER_FALLING,
    HDR_DIGITAL_TRIGGER_HIGH,
    HDR_DIGITAL_TRIGGER_LOW,
    HDR_ACQUISITION_DEPTH,
    /* Not valid for full acquisition packets?? */
    HDR_VIEW_DECIMATION,
    HDR_VIEW_OFFSET_B0,
    HDR_VIEW_OFFSET_B1,
    HDR_VIEW_OFFSET_B2,
    HDR_VIEW_ACQUISITIONS,
    HDR_VIEW_BURSTS,
    HDR_VIEW_EXCESS_B0,
    HDR_VIEW_EXCESS_B1,
};

enum HDR_STROBES {
    HDR_LA_ENABLE = 0,
    HDR_CHA_DCCOUPLING,
    HDR_CHB_DCCOUPLING,
    HDR_ROLL,
    HDR_LA_CHANNEL,
};

enum header_flags{
    Acquiring         = 0x01,
    IsOverview        = 0x02, // Data is overview
    IsLastAcquisition = 0x04, // Last data packet is being acquired
    Rolling           = 0x08,
    TimedOut          = 0x10, // No data available
    AwaitingTrigger   = 0x20, // Trigger has not been tripped
    Armded            = 0x40, // Trigger is configured
    IsFullAcqusition  = 0x80  // Packet belongs to a full acquisition
};

struct  __attribute__ ((__packed__)) header {
    uint8_t magic[2];           //0-1       "LN"
    uint8_t header_offset;      //2
    uint8_t bytes_per_burst;    //3
    uint16_t n_bursts;          //4-5
    uint16_t offset;            //6-7
    uint8_t unused[2];          //8-9
    uint8_t flags;              //10
    uint8_t acquisition_id;     //11        Incremented at start of acquisition
    uint8_t unused2[3];         //12-13-14
    uint8_t regs[HDR_N_REGS];   //15-44
    uint8_t strobes[(HDR_N_STROBES + 7) / 8];//45-
};

/** Private, per-device-instance driver context. */
struct dev_context {
	/* Model-specific information */
	char hw_rev[4];
	/* Acquisition settings */
    uint64_t samplerate;        // Current selected sample rate
    uint64_t limit_samples;     // samples requested by UI
    uint64_t sent_samples;      // number of samples sent to UI
    uint32_t acquisition_depth; // Real number of samples acquired, > limit_samples
    uint32_t pre_trigger_samples;
    uint8_t capture_ratio;      // ratio between samples before and after trigger
    uint8_t acquisition_id;     // Number of acquisition, same for all packets of that acquisition
    uint8_t new_acquisition;    // A new acquisition was started by UI
    uint8_t trigger_enabled;
    uint8_t trigger_sent;
    uint8_t trigger_delay;      // Compensation for trigger, sample rate dependent
    uint8_t *rx_buffer;
};

SR_PRIV int lnss_init(const struct sr_dev_inst *sdi);
SR_PRIV void lnss_cleanup(const struct sr_dev_inst *sdi);
SR_PRIV bool lnss_load_fpga(const struct sr_dev_inst *sdi);
SR_PRIV int lnss_aquisition_start(const struct sr_dev_inst *sdi);
SR_PRIV int lnss_subsamplerate_set(const struct sr_dev_inst *sdi, uint64_t rate);
SR_PRIV uint32_t lnss_acquisition_depth_set(const struct sr_dev_inst *sdi, uint32_t length);
SR_PRIV bool lnss_version_fpga(struct sr_usb_dev_inst *usb, char *dest);
SR_PRIV bool lnss_get_pic_firmware_version(struct sr_usb_dev_inst *usb, uint8_t *version);
SR_PRIV bool lnss_flash_fpga(struct sr_usb_dev_inst *usb, const uint8_t *firmware, size_t length);
SR_PRIV void lnss_flush_data_pipe(struct sr_usb_dev_inst *usb);
SR_PRIV void lnss_reset(struct sr_usb_dev_inst *usb);
SR_PRIV int lnss_data_receive(int fd, int revents, void *cb_data);
SR_PRIV int lnss_triggers_set(const struct sr_dev_inst *sdi, uint8_t falling, uint8_t rising, uint8_t low, uint8_t high);