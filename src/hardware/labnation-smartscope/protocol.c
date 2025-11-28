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

#include <config.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include "protocol.h"


#define EP_CMD_IN               0x83
#define EP_CMD_OUT              0x2
#define EP_DATA                 0x81

#define HEADER_CMD_BYTE         0xC0 // C0 as in Command
#define HEADER_RESPONSE_BYTE    0xAD // AD as in Answer Dude

#define COMMAND_WRITE_EP_SIZE   32
#define COMMAND_READ_EP_SIZE    16

#define USB_TIMEOUT_DATA        500

#define DBG_INFO(fmt, ...) g_fprintf(stdout, fmt"\n", ##__VA_ARGS__)

int g_fprintf(FILE *fptr, const char *str, ...);
static int adc_reg_get(struct sr_usb_dev_inst *usb, enum ADC reg, uint8_t *value);
static int controller_register_get (struct sr_usb_dev_inst *usb, enum controller ctrl,
                                          uint32_t address, uint32_t length,
                                          uint8_t *data);

static void dump_regs(const struct sr_dev_inst *sdi)
{
#if 1
    uint8_t regs[39], i;
    uint8_t rom[8];
    uint8_t adc[10];

    if(sr_log_loglevel_get() < SR_LOG_SPEW){
        return;
    }

    if(controller_register_get(sdi->conn, CTRL_FPGA, FPGA_I2C_ADDRESS_ROM << 8, 8, rom)){
        return;
    }

    for(i = 0; i < 10; i ++){
        if(adc_reg_get(sdi->conn, i, &adc[i])){
            return;
        }
    }

    /**
     * Reading all regs at once fails reading control, however received data seams ok.
     *
     * It seams related with size 16. Length is limited to 12 that when adding 4 bytes of header
     * totals 16. For now split into multiple transfers to avoid errors.
     * */
    if(controller_register_get(sdi->conn, CTRL_FPGA, (FPGA_I2C_ADDRESS_REG << 8), 12, regs)) return;
    if(controller_register_get(sdi->conn, CTRL_FPGA, (FPGA_I2C_ADDRESS_REG << 8) + 12, 12, &regs[12])) return;
    if(controller_register_get(sdi->conn, CTRL_FPGA, (FPGA_I2C_ADDRESS_REG << 8) + 24, 12, &regs[24])) return;
    if(controller_register_get(sdi->conn, CTRL_FPGA, (FPGA_I2C_ADDRESS_REG << 8) + 36, 2, &regs[36])) return;

    DBG_INFO("------------------------------------------------------------");
    DBG_INFO("    ROM 0xD00                              ADC");
    DBG_INFO("FW_GIT0:  0x%02X                    POWER_MANAGMENT:   0x%02X", rom[0], adc[0]);
    DBG_INFO("FW_GIT1:  0x%02X                    OUTPUT_FORMAT:     0x%02X", rom[1], adc[1]);
    DBG_INFO("FW_GIT2:  0x%02X                    OUTPUT_PWR_MNGMNT: 0x%02X", rom[2], adc[2]);
    DBG_INFO("FW_GIT3:  0x%02X                    DATA_CLK_TIMING:   0x%02X", rom[3], adc[3]);
    DBG_INFO("SPI_RECEIVED_VALUE: 0x%02X          CHA_TERMINATION:   0x%02X", rom[4], adc[4]);
    DBG_INFO("STROBES0: 0x%02X                    CHB_TERMINATION:   0x%02X", rom[5], adc[5]);
    DBG_INFO("STROBES1: 0x%02X                    FORMAT_PATTERN:    0x%02X", rom[6], adc[6]);
    DBG_INFO("STROBES2: 0x%02X                    COMMON_MODE:       0x%02X", rom[7], adc[7]);
    DBG_INFO("                                  SOFT_RESET:        0x%02X", adc[9]);
    DBG_INFO("------------------------------------------------------------");
    DBG_INFO("    REG 0xC00                          Strobe");
    DBG_INFO("STROBE_UPDATE:           0x%02X     GLOBAL_RESET:        %d", regs[0], !!(rom[5] & (1 << 0)));
    DBG_INFO("SPI_ADDRESS:             0x%02X     INIT_SPI_TRANSFER:   %d", regs[1], !!(rom[5] & (1 << 1)));
    DBG_INFO("SPI_WRITE_VALUE:         0x%02X     GENERATOR_TO_AWG:    %d", regs[2], !!(rom[5] & (1 << 2)));
    DBG_INFO("DIVIDER_MULTIPLIER:      0x%02X     LA_ENABLE:           %d", regs[3], !!(rom[5] & (1 << 3)));
    DBG_INFO("CHA_YOFFSET_VOLTAGE:     0x%02X     SCOPE_ENABLE:        %d", regs[4], !!(rom[5] & (1 << 4)));
    DBG_INFO("CHB_YOFFSET_VOLTAGE:     0x%02X     SCOPE_UPDATE:        %d", regs[5], !!(rom[5] & (1 << 5)));
    DBG_INFO("TRIGGER_PWM:             0x%02X     FORCE_TRIGGER:       %d", regs[6], !!(rom[5] & (1 << 6)));
    DBG_INFO("TRIGGER_LEVEL:           0x%02X     VIEW_UPDATE:         %d", regs[7], !!(rom[5] & (1 << 7)));
    DBG_INFO("TRIGGER_MODE:            0x%02X     VIEW_SEND_OVERVIEW:  %d", regs[8], !!(rom[6] & (1 << 0)));
    DBG_INFO("TRIGGER_PW_MIN_B0:       0x%02X     VIEW_SEND_PARTIAL:   %d", regs[9], !!(rom[6] & (1 << 1)));
    DBG_INFO("TRIGGER_PW_MIN_B1:       0x%02X     ACQ_START:           %d", regs[10], !!(rom[6] & (1 << 2)));
    DBG_INFO("TRIGGER_PW_MIN_B2:       0x%02X     ACQ_STOP:            %d", regs[11], !!(rom[6] & (1 << 3)));
    DBG_INFO("TRIGGER_PW_MAX_B0:       0x%02X     CHA_DCCOUPLING:      %d", regs[12], !!(rom[6] & (1 << 4)));
    DBG_INFO("TRIGGER_PW_MAX_B1:       0x%02X     CHB_DCCOUPLING:      %d", regs[13], !!(rom[6] & (1 << 5)));
    DBG_INFO("TRIGGER_PW_MAX_B2:       0x%02X     ENABLE_ADC:          %d", regs[14], !!(rom[6] & (1 << 6)));
    DBG_INFO("INPUT_DECIMATION:        0x%02X     ENABLE_NEG:          %d", regs[15], !!(rom[6] & (1 << 7)));
    DBG_INFO("ACQUISITION_DEPTH:       0x%02X     ENABLE_RAM:          %d", regs[16], !!(rom[7] & (1 << 0)));
    DBG_INFO("TRIGGERHOLDOFF_B0:       0x%02X     DOUT_3V_5V:          %d", regs[17], !!(rom[7] & (1 << 1)));
    DBG_INFO("TRIGGERHOLDOFF_B1:       0x%02X     EN_OPAMP_B:          %d", regs[18], !!(rom[7] & (1 << 2)));
    DBG_INFO("TRIGGERHOLDOFF_B2:       0x%02X     GENERATOR_TO_DIGITAL:%d", regs[19], !!(rom[7] & (1 << 3)));
    DBG_INFO("TRIGGERHOLDOFF_B3:       0x%02X     ROLL:                %d", regs[20], !!(rom[7] & (1 << 4)));
    DBG_INFO("VIEW_DECIMATION:         0x%02X     LA_CHANNEL:          %d", regs[21], !!(rom[7] & (1 << 5)));
    DBG_INFO("VIEW_OFFSET_B0:          0x%02X", regs[22]);
    DBG_INFO("VIEW_OFFSET_B1:          0x%02X", regs[23]);
    DBG_INFO("VIEW_OFFSET_B2:          0x%02X", regs[24]);
    DBG_INFO("VIEW_ACQUISITIONS:       0x%02X", regs[25]);
    DBG_INFO("VIEW_BURSTS:             0x%02X", regs[26]);
    DBG_INFO("VIEW_EXCESS_B0:          0x%02X", regs[27]);
    DBG_INFO("VIEW_EXCESS_B1:          0x%02X", regs[28]);
    DBG_INFO("DIGITAL_TRIGGER_RISING:  0x%02X", regs[29]);
    DBG_INFO("DIGITAL_TRIGGER_FALLING: 0x%02X", regs[30]);
    DBG_INFO("DIGITAL_TRIGGER_HIGH:    0x%02X", regs[31]);
    DBG_INFO("DIGITAL_TRIGGER_LOW:     0x%02X", regs[32]);
    DBG_INFO("DIGITAL_OUT:             0x%02X", regs[33]);
    DBG_INFO("GENERATOR_DECIMATION_B0: 0x%02X", regs[34]);
    DBG_INFO("GENERATOR_DECIMATION_B1: 0x%02X", regs[35]);
    DBG_INFO("GENERATOR_DECIMATION_B2: 0x%02X", regs[36]);
    DBG_INFO("GENERATOR_SAMPLES_B0:    0x%02X", regs[37]);
    DBG_INFO("GENERATOR_SAMPLES_B1:    0x%02X", regs[38]);
    DBG_INFO("------------------------------------------------------------");
#endif
}

#if 0
static void dump_buf(void *buf, uint32_t len)
{
    g_fprintf(stdout, "[");

    if(buf == NULL)
        len = 0;

    for(uint32_t i = 0; i < len; i++){
        uint8_t byte = ((uint8_t*)buf)[i];
        g_fprintf(stdout, "%02X%s", byte, (i<len-1) ? ", ": "");
    }
    g_fprintf(stdout, "]\n");
    fflush(stdout);
}
#endif

static int get_i2c_reg(struct sr_usb_dev_inst *usb, uint8_t addr, uint32_t idx, uint8_t *value)
{
	int len, ret;

	uint8_t wbuf[] = { HEADER_CMD_BYTE, PICCMD_I2C_WRITE, 2, addr << 1, idx };
	uint8_t rbuf[] = { HEADER_CMD_BYTE, PICCMD_I2C_READ, addr, 1 };
	uint8_t inp[COMMAND_READ_EP_SIZE] = {0};

	if ((ret = libusb_bulk_transfer(usb->devhdl, EP_CMD_OUT, wbuf, sizeof(wbuf), &len, USB_TIMEOUT_DATA))) {
		sr_err("Failed to transfer wbuf: %s", libusb_error_name(ret));
		return SR_ERR_IO;
	}

	if ((ret = libusb_bulk_transfer(usb->devhdl, EP_CMD_OUT, rbuf, sizeof(rbuf), &len, USB_TIMEOUT_DATA))) {
		sr_err("Failed to transfer rbuf: %s", libusb_error_name(ret));
		return SR_ERR_IO;
	}
	if ((ret = libusb_bulk_transfer(usb->devhdl, EP_CMD_IN, inp, sizeof(inp), &len, USB_TIMEOUT_DATA))) {
		sr_err("Failed to read i2c response: %s", libusb_error_name(ret));
		return SR_ERR_IO;
	}

    //dump_buf(inp, 8);

    if(inp[0] != HEADER_RESPONSE_BYTE){
        sr_err("Response header mismatch");
    }

    *value = inp[4];

	return SR_OK;
}

/**
 * @brief Write to command endpoint
 */
static int write_control_bytes_bulk(struct sr_usb_dev_inst *usb, uint32_t length, uint8_t *msg)
{
    int ret;
    int n;

    ret = libusb_bulk_transfer(usb->devhdl, EP_CMD_OUT, msg, length, &n, USB_TIMEOUT_DATA);

    if (ret) {
        sr_err("%s(): Failed write control bytes: %s", __func__, libusb_error_name(ret));
        return SR_ERR_IO;
    }

    if(n != (int)length){
        sr_warn("%s(): Only wrote %d out of %d bytes", __func__, n, length);
        return SR_ERR_DATA;
    }

    return SR_OK;
}

/**
 * @brief Read from command endpoint
 */
static int read_control_bytes(struct sr_usb_dev_inst *usb, uint32_t length, uint8_t *msg)
{
    int ret;
    int n;

    ret = libusb_bulk_transfer(usb->devhdl, EP_CMD_IN, msg, length, &n, USB_TIMEOUT_DATA);

    if (ret) {
        sr_err("%s(): Failed read control bytes: %s", __func__, libusb_error_name(ret));
        return SR_ERR_IO;
    }

     if(n != (int)length){
        sr_warn("%s(): Only read %d out of %d bytes", __func__, n, length);
        return SR_ERR_DATA;
    }

    return SR_OK;
}

/**
 * @brief Read from data endpoint
 */
static int read_data_bytes(struct sr_usb_dev_inst *usb, uint32_t length, uint8_t *buffer)
{
    int count;

	if (!usb)
		return SR_ERR_ARG;

    int ret = libusb_bulk_transfer(usb->devhdl, EP_DATA, buffer, length, &count, USB_TIMEOUT_DATA);

    if(ret){
        sr_err("%s(): Failed read data: %s", __func__, libusb_error_name(ret));
        return SR_ERR_IO;
    }

    if(count != (int)length){
        sr_warn("%s(): Only read %d out of %d bytes", __func__, count, length);
    }

	return count;
}

static int usb_send_command(struct sr_usb_dev_inst *usb, enum pic_cmd cmd)
{
    uint8_t to_send[2] = {HEADER_CMD_BYTE, (uint8_t)cmd};
    return write_control_bytes_bulk(usb, 2, to_send);
}

/**
 * @brief Forms a lnss command header packet
 * containing a message to pic controller.
 */
static int usb_command_header_i2c (uint8_t I2cAddress, enum controller_op op,
                                   uint32_t address, uint32_t length,
                                   uint8_t *buffer)
{
   // Most common, will be overridden if necessary
   buffer[0] = HEADER_CMD_BYTE;

   switch (op)
   {
      case WRITE:
         buffer[1] = PICCMD_I2C_WRITE;
         buffer[2] = (uint8_t) (length + 2);
         buffer[3] = I2cAddress << 1;
         buffer[4] = address;
         return 5;
      case READ:
         buffer[1] = PICCMD_I2C_READ;
         buffer[2] = I2cAddress;
         buffer[3] = length;
         return 4;
      default:
         sr_warn ("%s(): Unsupported operation for I2C Header", __func__);
   }

   return -1;
}

/**
 * @brief Forms a lnss command header packet
 * with message for a controller
 */
static int usb_command_header (enum controller ctrl, enum controller_op op,
                               uint32_t address, uint32_t length,
                               uint8_t *buffer)
{
    // Most common, will be overridden if necessary
    buffer[0] = HEADER_CMD_BYTE;
    buffer[2] = (uint8_t) (address);
    buffer[3] = (uint8_t) (length);
    // Set operation
    switch (ctrl)
    {
    case CTRL_PIC:
        buffer[1] = op == WRITE ? PICCMD_PIC_WRITE : PICCMD_PIC_READ;
        return 4;
    case CTRL_ROM:
        buffer[1] = op == WRITE ? PICCMD_EEPROM_WRITE : PICCMD_EEPROM_READ;
        return 4;
    case CTRL_FLASH:
        buffer[1] =
            op == WRITE ? PICCMD_FLASH_ROM_WRITE : PICCMD_FLASH_ROM_READ;
        buffer[4] = (uint8_t) (address >> 8);
        return 5;
    case CTRL_FPGA:
        return usb_command_header_i2c ((uint8_t) ((address >> 8) & 0x7F), op,
                                    (uint8_t) (address & 0xFF), length,
                                    buffer);
    case CTRL_AWG:
        switch (op)
        {
        case WRITE:
            return usb_command_header_i2c (FPGA_I2C_ADDRESS_AWG, op, address,
                                            length, buffer);
        case WRITE_BEGIN:
            buffer[1] = PICCMD_I2C_WRITE_START;
            buffer[2] = length + 2;
            buffer[3] = FPGA_I2C_ADDRESS_AWG << 1;
            buffer[4] = address;
            return 5;
        case WRITE_BODY:
            buffer[1] = PICCMD_I2C_WRITE_BULK;
            buffer[2] = (uint8_t) length;
            return 3;
        case WRITE_END:
            buffer[1] = PICCMD_I2C_WRITE_STOP;
            buffer[2] = (uint8_t) length;
            return 3;
        case READ:
            sr_warn ("%s(): Can't read out AWG", __func__);
        }
   }

   return -1;
}

/**
 * @brief Writes to a controller regiter through libusb
 */
static int controller_register_set (struct sr_usb_dev_inst *usb,
                                          enum controller ctrl,
                                          uint32_t address, uint32_t length,
                                          uint8_t *data)
{
   uint8_t msg[32];
   int msgLen   = 0;
   int sr_error = SR_OK;

   // dump_buf(data, length);

   if (data != NULL && length > I2C_MAX_WRITE_LENGTH)
   {
      uint32_t offset = 0;
      int bytesLeft   = length;

      if (ctrl != CTRL_AWG)
      {
         // Chop up in smaller chunks
         while (bytesLeft > 0)
         {
            int length = bytesLeft > I2C_MAX_WRITE_LENGTH ?
                             I2C_MAX_WRITE_LENGTH :
                             bytesLeft;
            sr_error   = controller_register_set (usb, ctrl, address + offset,
                                                  length, &data[offset]);

            if (sr_error)
            {
               goto error;
            }

            offset += length;
            bytesLeft -= length;
         }
         return SR_OK;
      }

      msgLen   = usb_command_header (ctrl, WRITE_BEGIN, address, 0, msg);
      sr_error = write_control_bytes_bulk (usb, msgLen, msg);

      if (sr_error)
      {
         goto error;
      }

      while (bytesLeft > 0)
      {
         int length = bytesLeft > I2C_MAX_WRITE_LENGTH_BULK ?
                          I2C_MAX_WRITE_LENGTH_BULK :
                          bytesLeft;
         msgLen = usb_command_header (ctrl, WRITE_BODY, address, length, msg);

         memcpy (&msg[msgLen], &data[offset], length);
         sr_error = write_control_bytes_bulk (usb, 32, msg);
         if (sr_error)
         {
            goto error;
         }
         offset += length;
         bytesLeft -= length;
      }
      msgLen   = usb_command_header (ctrl, WRITE_END, address, 0, msg);
      sr_error = write_control_bytes_bulk (usb, msgLen, msg);
      if (sr_error)
      {
         goto error;
      }
   }
   else
   {
      msgLen = usb_command_header (ctrl, WRITE, address, length, msg);
      if (length > 0)
         memcpy (&msg[msgLen], data, length);
      sr_error = write_control_bytes_bulk (usb, msgLen + length, msg);
      if (sr_error)
      {
         goto error;
      }
   }

error:
   return sr_error;
}


/**
 * @brief Reads a controller regiter through libusb
 */
static int controller_register_get (struct sr_usb_dev_inst *usb,
                                          enum controller ctrl,
                                          uint32_t address, uint32_t length,
                                          uint8_t *data)
{
   uint8_t msg[PACKAGE_MAX];
   int sr_error;

   if (ctrl == CTRL_FPGA)
   {
      if ((sr_error = controller_register_set (usb, ctrl, address, 0, NULL)) !=
          SR_OK)
         goto error;
   }
   else if (ctrl == CTRL_FLASH &&
            (address + length) > (FLASH_USER_ADDRESS_MASK + 1))
   {
      sr_err ("%s(): Can't read flash rom beyond 0x%08X", __func__,
              FLASH_USER_ADDRESS_MASK);
      return SR_ERR_ARG;
   }

   int len = usb_command_header (ctrl, READ, address, length, msg);
   if ((sr_error = write_control_bytes_bulk (usb, len, msg)) != SR_OK)
      goto error;

   // EP3 always contains 16 bytes xxx should be linked to constant
   // FIXME: use endpoint length or so, or don't pass the argument to the
   // function
   if ((sr_error = read_control_bytes (usb, 16, msg)) != SR_OK)
      goto error;

   memcpy (data, &msg[ctrl == CTRL_FLASH ? 5 : 4], length);

error:
   return sr_error;
}

/**
 * @brief Write to a FPGA register
 */
static int reg_set(struct sr_usb_dev_inst *usb, enum REG reg, uint8_t value)
{
    return controller_register_set(usb, CTRL_FPGA, (FPGA_I2C_ADDRESS_REG << 8) + reg, 1, &value);
}
#if 0
/**
 * @brief Read FPGA register
 * There are 39 registers, see enum REG for register names
 */
static int reg_get(struct sr_usb_dev_inst *usb, enum REG reg, uint8_t *value)
{
    return controller_register_get(usb, CTRL_FPGA, (FPGA_I2C_ADDRESS_REG << 8) + reg, 1, value);
}
#endif
/**
 * @brief Write FPGA strobe bit.
 * Strobe bit is written to FPGA STROBE_UPDATE register
 *
 *
 *     STROBE_REGISTER
 * 7              1       0
 * | Strobe Index | State |
 *
 * Index: 0-31
 * State: 0|1
 */
static int strobe_set(struct sr_usb_dev_inst *usb, enum STR strobe, uint8_t state)
{
    uint8_t value;

    value = (strobe << 1) | (state & 1);
    return controller_register_set(usb, CTRL_FPGA, (FPGA_I2C_ADDRESS_REG << 8) + STROBE_UPDATE, 1, &value);
}
#if 0
/**
 * @brief Read FPGA strobe bit.
 * Strobe bit is read from FPGA ROM starting from STROBE0 register
 *
 * There are 21 strobe bits starting from STROBE0[0] to STROBE2[1]
 */
static int strobe_get(struct sr_usb_dev_inst *usb, enum STR strobe, uint8_t *state)
{
    uint8_t addr, value;
    int res;

    addr = (uint8_t)STROBES0 + (uint8_t)strobe / 8;

    res = controller_register_get(usb, CTRL_FPGA, (FPGA_I2C_ADDRESS_ROM << 8) + addr, 1, &value);

    if(res != SR_OK){
        return res;
    }

    *state = (value >> (addr % 8)) & 1;

    return SR_OK;
}
#endif
/**
 * @brief Read ADC register
 */
static int adc_reg_get(struct sr_usb_dev_inst *usb, enum ADC reg, uint8_t *value)
{
    int res;
    uint8_t reg_val = (uint8_t)reg | 0x80;

    res = controller_register_set(usb, CTRL_FPGA, ((FPGA_I2C_ADDRESS_REG << 8) + SPI_ADDRESS), 1, &reg_val);

    if(res != SR_OK){
        return res;
    }

    strobe_set(usb, INIT_SPI_TRANSFER, 0);
    strobe_set(usb, INIT_SPI_TRANSFER, 1);

    res = controller_register_get(usb, CTRL_FPGA, (FPGA_I2C_ADDRESS_ROM << 8) + SPI_RECEIVED_VALUE, 1, value);

    return (res == SR_OK) ? SR_OK : res;
}

/**
 * @brief Write to ADC register
 */
static int adc_reg_set(struct sr_usb_dev_inst *usb, enum ADC reg, uint8_t value)
{
    int res;

    res = controller_register_set(usb, CTRL_FPGA, (FPGA_I2C_ADDRESS_REG << 8) + SPI_ADDRESS, 1, (uint8_t*)&reg);

    if(res != SR_OK){
        return res;
    }

    res = controller_register_set(usb, CTRL_FPGA, (FPGA_I2C_ADDRESS_REG << 8) + SPI_WRITE_VALUE, 1, &value);

    if(res != SR_OK){
        return res;
    }

    strobe_set(usb, INIT_SPI_TRANSFER, 0);
    strobe_set(usb, INIT_SPI_TRANSFER, 1);

    return SR_OK;
}

/**
 * @brief Retrieves usb package
 *        usb packages have a maximum size of 64 + (2 * 2048) bytes
 *
 *  64 bytes for header
 *  2048 bytes per channel
 */
static int acquisition_get(struct sr_usb_dev_inst *usb,
                           uint8_t *buffer, uint32_t length)
{
    struct header *hdr;
    int tries;

    tries = 0;

    while(true) {
        if(read_data_bytes(usb, SZ_HDR, buffer) <= 0){
            return 0;
        }

        hdr = (struct header*)buffer;

        if(hdr->magic[0] == 'L' && hdr->magic[1] == 'N')
            break;

        if(tries++ > PACKAGE_MAX){
            sr_err("%s(): Invalid header magic 0x%02X%02X at fetch %d", __func__, hdr->magic[0], hdr->magic[1], tries);
            return 0;
        }
    }

    if(tries > 0)
        sr_warn("%s(): Had to try %d times before a good header came through", __func__, tries + 1);

    sr_dbg("%s(): Pkt hdr: 0x%02x %s%s%s%s%s%s%s%s", __func__, hdr->flags,
                hdr->flags & IsFullAcqusition ? "|Full Acquisition" : "",
                hdr->flags & Armded ? "|Armed" : "",
                hdr->flags & AwaitingTrigger ? "|AwaitingTrigger" : "",
                hdr->flags & TimedOut ? "|TimedOut" : "",
                hdr->flags & Rolling ? "|Rolling" : "",
                hdr->flags & IsLastAcquisition ? "|LastAcquisition" : "",
                hdr->flags & IsOverview ? "|Overview" : "",
                hdr->flags & Acquiring ? "|Acquiring" : "");

    if (hdr->flags & TimedOut){
        //sr_dbg("%s(): TimedOut, Header only (0x%02X)", __func__, hdr->flags);
        return SZ_HDR;
    }

    if (hdr->flags & IsOverview) {
        //sr_dbg("%s(): Overview packet", __func__);
        read_data_bytes(usb, SZ_OVERVIEW, buffer + SZ_HDR);
        return SZ_HDR + SZ_OVERVIEW;
    }

    if (hdr->n_bursts == 0){
        sr_err("%s(): number of bursts in this USB packet is 0, cannot fetch", __func__);
        return 0;
    }

    uint32_t size = hdr->n_bursts * hdr->bytes_per_burst;

    if (size + SZ_HDR > length) {
        sr_err("%s(): Length of packet (%u) is bigger than buffer (%u)"
               "   (N_burst: %d, bytes per burst: %d) expect failure",
                    __func__,
                    size + SZ_HDR, length,
                    hdr->n_bursts, hdr->bytes_per_burst);
        void* dummy = malloc(size);
        read_data_bytes(usb, size, (uint8_t*)dummy);
        free(dummy);
        return 0;
    }

    read_data_bytes(usb, size, buffer + SZ_HDR);
    //sr_dbg("%s(): Data packet", __func__);

    return SZ_HDR + size;
}

/**
 * @brief
 *
 */
static int adc_ramp_verify(uint8_t *data, uint32_t len)
{
    uint32_t failing_indexes = 0;

    if (len < 4){
        return 0;
    }

    // Check samples from both channels
    for(uint32_t i = 2; i < len; i++){
        if((uint8_t)(data[i - 2] + 1) != data[i]){
            failing_indexes++;
        }
    }

    return failing_indexes == 0;
}

static int adc_ramp_test(struct sr_usb_dev_inst *usb)
{
    int sr_error = SR_ERR_TIMEOUT;
    int tries, size;
    struct header *hdr;
    uint8_t *packet = g_try_malloc0(SZ_HDR + SZ_OVERVIEW);

    if(!packet){
        return SR_ERR_MALLOC;
    }

    hdr = (struct header*)packet;
    tries = 10;

    do{
        if((size = acquisition_get(usb, packet, SZ_HDR + SZ_OVERVIEW)) == SZ_HDR){
            strobe_set(usb, ACQ_START, 1);
            strobe_set(usb, FORCE_TRIGGER, 1);
            continue;
        }

        if(!(hdr->flags & IsFullAcqusition)){
            continue;
        }

        size -= SZ_HDR;

        sr_dbg("%s(): Checking %d samples", __func__, size);

        if(adc_ramp_verify(packet + SZ_HDR, size)){
            sr_error = SR_OK;
            goto adc_ramp_end;
        }

    }while(--tries);

    sr_err("%s(): Failed to get ADC calibration data", __func__);

adc_ramp_end:

    if(packet)
        free(packet);

    return sr_error;
}

/**
 * @brief Configures MAX19506 ADC registers
 */
static void adc_configure(struct sr_usb_dev_inst *usb)
{
    adc_reg_set(usb, SOFT_RESET, 90);
    adc_reg_set(usb, POWER_MANAGMENT, 4);    // CHA/B Standby
    adc_reg_set(usb, OUTPUT_PWR_MNGMNT, 0);  // Clock active
    adc_reg_set(usb, FORMAT_PATTERN, 16);    // Offset binary
    adc_reg_set(usb, DATA_CLK_TIMING, 24);   //
    adc_reg_set(usb, CHA_TERMINATION, 0);    // 50 Ohm
    adc_reg_set(usb, POWER_MANAGMENT, 3);    // CHA/B Active
    adc_reg_set(usb, OUTPUT_FORMAT, 2);      // Multiplexed data
}

/**
 * @brief Performs ADC calibration
 */
static int adc_calibrate(struct sr_usb_dev_inst *usb)
{
    adc_configure(usb);

    adc_reg_set(usb, FORMAT_PATTERN, 80); // Enable test data

    /* AcquisitionDepth */
    reg_set(usb, ACQUISITION_DEPTH, 1);

    /* SetViewPort */
    reg_set(usb, VIEW_DECIMATION, 1);
    reg_set(usb, VIEW_BURSTS, 6);

    /* AcquisitionMode */
    reg_set(usb, TRIGGER_MODE, TRG_ACQ_SINGLE);

    /* SetTriggerByte */
    reg_set(usb, TRIGGER_LEVEL, 127);

    /* SendOverViewBuffer */
    strobe_set(usb, VIEW_SEND_OVERVIEW, 0);

    /* PreferPartial */
    strobe_set(usb, VIEW_SEND_PARTIAL, 0);

    /* Disable LA */
    strobe_set(usb, LA_CHANNEL, 0);
    strobe_set(usb, LA_ENABLE, 0);

    // Apply register values
    strobe_set(usb, SCOPE_UPDATE, 1);

    /* Start acquiring */
    strobe_set(usb, ACQ_START, 1);

    sr_info("Calibrating ADC timing...");

    if(adc_ramp_test(usb) == SR_OK){
        sr_info("ADC calibration ok.");
        strobe_set(usb, FORCE_TRIGGER, 1);
        lnss_flush_data_pipe(usb);

        /* Reconfigure ADC with new timing */
        adc_configure(usb);

        strobe_set(usb, ACQ_STOP, 1);
        return SR_OK;
    }

    sr_warn("ADC calibration failed!");

    return SR_ERR_TIMEOUT;
}

/**
 * @brief Send acquired data to session
 *
 */
static void send_logic_data(struct sr_dev_inst *sdi, void *samples, uint32_t len)
{
    struct dev_context *devc;
    struct sr_datafeed_packet packet;
    struct sr_datafeed_logic logic;
    int trigger_offset;

    devc = sdi->priv;

    logic.unitsize = sizeof(uint8_t);
    packet.type = SR_DF_LOGIC;
    packet.payload = &logic;

    sr_dbg("%s(): Sending %d samples of %d, remaining: %d", __func__,
                    (int)len,
                    (int)devc->acquisition_depth,
                    (int)(devc->acquisition_depth - devc->sent_samples - len));

    if(devc->trigger_enabled && !devc->trigger_sent){
        /**
         * trigger_offset:
         *
         *  > len, trigger is not on this packet
         *  < 0, trigger is on this packet
         *  = 0, is on first sample of next packet
         */
        trigger_offset = (int)devc->pre_trigger_samples - (int)(devc->sent_samples + len);

        if(trigger_offset != 0){
            if(trigger_offset < 0){
                devc->trigger_sent = true;
                trigger_offset = len - abs(trigger_offset);

                if(trigger_offset == 0){
                    // trigger on first sample
                    std_session_send_df_trigger(sdi);
                }else{
                    trigger_offset -= devc->trigger_delay;
                    // send pre-trigger samples
                    logic.length = trigger_offset;
                    logic.data = samples;
                    sr_session_send(sdi, &packet);
                    sr_dbg("%s(): Send trigger set to offset %lu, with delay %u",
                                __func__,
                                devc->sent_samples + trigger_offset,
                                devc->trigger_delay);
                    // send trigger marker and pos-trigger samples
                    std_session_send_df_trigger(sdi);
                    logic.length = len - trigger_offset;
                    logic.data = samples + trigger_offset;
                    sr_session_send(sdi, &packet);
                    return;
                }
            }
        }
    }else if(!devc->trigger_sent){
        // no triggers configured, send trigger mark on first packet
        std_session_send_df_trigger(sdi);
        devc->trigger_sent = true;
    }

    logic.length = len;
    logic.data = samples;
    sr_session_send(sdi, &packet);
}

#if 0
/**
 * @brief Send analog data (oscilloscope data)
 *
 */
static int send_analog_data(struct sr_dev_inst *sdi,
                    uint64_t samples_todo, uint64_t *samples_done,
                    void *samples)
{
    (void)sdi;
    (void)samples_todo;
    (void)samples_done;
    (void)samples;
    /*
    struct sr_datafeed_analog analog;
    struct sr_analog_encoding encoding;
    struct sr_analog_meaning meaning;
    struct sr_analog_spec spec;

    sr_analog_init(&analog, &encoding, &meaning, &spec, 2);
    analog.meaning->channels = devc->enabled_analog_channels;
    analog.meaning->mq = SR_MQ_VOLTAGE;
	analog.meaning->unit = SR_UNIT_VOLT;
	analog.meaning->mqflags = SR_MQFLAG_DC;
	analog.num_samples = samples_total;
	analog.data = analog_buffer;

    packet.type = SR_DF_ANALOG;
    packet.payload = &analog;

    //sr_session_send(sdi, &packet);

    sr_dev_acquisition_stop(sdi);
*/
    return 0;
}
#endif

/**
 * SmartScope class public functions
 */
SR_PRIV int lnss_init(const struct sr_dev_inst *sdi)
{
    struct dev_context *devc = sdi->priv;
    struct sr_usb_dev_inst *usb;

    sr_dbg("%s()", __func__);

    usb = sdi->conn;

    /* Enable essentials */
    strobe_set(usb, GLOBAL_RESET, 1);

    /* DigitalOutput */
    reg_set(usb, DIGITAL_OUT, 0);

    /* Channels offset */
    reg_set(usb, DIVIDER_MULTIPLIER, 153); // Magic??
    reg_set(usb, CHA_YOFFSET_VOLTAGE, 114);
    reg_set(usb, CHB_YOFFSET_VOLTAGE, 114);

    /*Trigger */
    reg_set(usb, TRIGGER_PWM, 0);
    reg_set(usb, TRIGGER_LEVEL, 125);
    reg_set(usb, TRIGGER_MODE, 0);
    reg_set(usb, TRIGGER_PW_MIN_B0, 0);
    reg_set(usb, TRIGGER_PW_MIN_B1, 0);
    reg_set(usb, TRIGGER_PW_MIN_B2, 0);
    reg_set(usb, TRIGGER_PW_MAX_B0, 255);
    reg_set(usb, TRIGGER_PW_MAX_B1, 255);
    reg_set(usb, TRIGGER_PW_MAX_B2, 255);
    reg_set(usb, TRIGGERHOLDOFF_B0, 0);
    reg_set(usb, TRIGGERHOLDOFF_B1, 0);
    reg_set(usb, TRIGGERHOLDOFF_B2, 0);
    reg_set(usb, TRIGGERHOLDOFF_B3, 0);
    reg_set(usb, DIGITAL_TRIGGER_RISING, 0);
    reg_set(usb, DIGITAL_TRIGGER_FALLING, 0);
    reg_set(usb, DIGITAL_TRIGGER_LOW, 0);
    reg_set(usb, DIGITAL_TRIGGER_HIGH, 0);

    /* Misc */
    reg_set(usb, INPUT_DECIMATION, 0);
    reg_set(usb, ACQUISITION_DEPTH, 0);

    /* GeneratorStretching */
    reg_set(usb, GENERATOR_DECIMATION_B0, 0);
    reg_set(usb, GENERATOR_DECIMATION_B1, 0);
    reg_set(usb, GENERATOR_DECIMATION_B2, 0);

    /* SetViewPort */
    reg_set(usb, VIEW_DECIMATION, 0);
    reg_set(usb, VIEW_BURSTS, 6);
    reg_set(usb, VIEW_OFFSET_B0, 0);
    reg_set(usb, VIEW_OFFSET_B1, 0);
    reg_set(usb, VIEW_OFFSET_B2, 0);
    reg_set(usb, VIEW_ACQUISITIONS, 0);
    reg_set(usb, VIEW_EXCESS_B0, 0);
    reg_set(usb, VIEW_EXCESS_B1, 0);

    /* GeneratorNumberOfSamples */
    reg_set(usb, GENERATOR_SAMPLES_B0, 0xFF); // 0x800 - 1
    reg_set(usb, GENERATOR_SAMPLES_B1, 0x07);

    /* Strobes */
    strobe_set(usb, SCOPE_UPDATE, 1);    // apply register values
    strobe_set(usb, GENERATOR_TO_AWG, 0);
    strobe_set(usb, LA_ENABLE, 0);
    strobe_set(usb, SCOPE_ENABLE, 1);
    strobe_set(usb, FORCE_TRIGGER, 0);
    strobe_set(usb, VIEW_UPDATE, 0);
    strobe_set(usb, VIEW_SEND_OVERVIEW, 0);
    strobe_set(usb, VIEW_SEND_PARTIAL, 0);
    strobe_set(usb, ACQ_START, 0);
    strobe_set(usb, ACQ_STOP, 0);
    strobe_set(usb, CHA_DCCOUPLING, 1);
    strobe_set(usb, CHB_DCCOUPLING, 1);
    strobe_set(usb, ENABLE_ADC, 1);
    strobe_set(usb, ENABLE_NEG, 1);
    strobe_set(usb, ENABLE_RAM, 1);
    strobe_set(usb, DOUT_3V_5V, 0);
    strobe_set(usb, EN_OPAMP_B, 0);
    strobe_set(usb, GENERATOR_TO_DIGITAL, 0);
    strobe_set(usb, ROLL, 0);
    strobe_set(usb, LA_CHANNEL, 0);

    lnss_flush_data_pipe(usb);

    adc_calibrate(usb);

    /* SendOverViewBuffer */
    strobe_set(usb, VIEW_SEND_OVERVIEW, 0);
    reg_set(usb, VIEW_DECIMATION, 0);

    /* Enable LA*/
    strobe_set(usb, LA_ENABLE, 1);

    /* Configure trigger for full acquisitions */
    reg_set(usb, TRIGGER_MODE,
                TRG_ACQ_SINGLE |
                TRG_EDGE_ANY |
                TRG_SOURCE_CHANNEL |
                TRG_CHANNEL_A |
                TRG_MODE_DIGITAL
    );

    reg_set(usb, DIVIDER_MULTIPLIER, 0);
    reg_set(usb, TRIGGER_LEVEL, 0x7E);      // got this value after calibration from SmartScope App

    strobe_set(usb, SCOPE_UPDATE, 1);       // Update registers

    dump_regs(sdi);

    // Packets obtained from device have a fixed size
    devc->rx_buffer = g_try_malloc0(SZ_HDR + (LNSS_MIN_ACQUISITION * LNSS_NUM_AN_CHANNELS));

    return (devc->rx_buffer) ? SR_OK : SR_ERR_MALLOC;
}

SR_PRIV void lnss_cleanup(const struct sr_dev_inst *sdi)
{
    struct dev_context *devc = sdi->priv;

    sr_dbg("%s()", __func__);

    g_free(devc->rx_buffer);
}

SR_PRIV void lnss_flush_data_pipe(struct sr_usb_dev_inst *usb)
{
    uint8_t buffer[PACKAGE_MAX];
    int n;
    int count = 0;

    do{
        count++;
        libusb_bulk_transfer(usb->devhdl, EP_DATA, buffer, sizeof(buffer), &n, 100);
    }while(n > 0);

    sr_dbg("%s(): flushed %d bytes", __func__, count * (int)sizeof(buffer));
}

SR_PRIV void lnss_reset(struct sr_usb_dev_inst *usb)
{
    usb_send_command(usb, PICCMD_PIC_RESET);
}

SR_PRIV bool lnss_get_pic_firmware_version(struct sr_usb_dev_inst *usb, uint8_t *version)
{
  uint8_t response[16];

  if(usb_send_command(usb, PICCMD_PIC_VERSION) != SR_OK){
    return false;
  }

  if(read_control_bytes(usb, 16, response) != SR_OK){
    return false;
  }

  sprintf((char*)version, "%u.%u.%u", response[6], response[5], response[4]);

  return true;
}

SR_PRIV bool lnss_version_fpga(struct sr_usb_dev_inst *usb, char *dest) {
	bool rv = false;
    uint8_t x = 0;

	if (!dest) {
		sr_warn("no destination?!");
		return false;
	}

	for (int i = 0; i < 4; i++) {
		// to match vendor sw, it's in reverse order
		get_i2c_reg(usb, FPGA_I2C_ADDRESS_ROM, 3 - i, &x);
		//sr_dbg("fpga byte %d = %02X", i, x);
		/* if the git rev is _plausible_ it's true */
		if (x != 255) {
			rv = true;
		}
		sprintf(dest + i*2, "%02X", x);
	}

	dest[8] = 0;

	return rv;
}

SR_PRIV bool lnss_load_fpga(const struct sr_dev_inst *sdi) {
	struct drv_context *drvc = sdi->driver->context;
	struct dev_context *devc = sdi->priv;

	char name[200];
	uint8_t *firmware;
	size_t length;
    bool res;

	snprintf(name, sizeof(name) - 1, "SmartScope_%s.bin", devc->hw_rev);

	/* All existing blogs are < 300k, don't really expect much change */
	firmware = sr_resource_load(drvc->sr_ctx, SR_RESOURCE_FIRMWARE,
		name, &length, 400 * 1024);
	if (!firmware) {
		sr_err("Failed to load firmware :(");
		return false;
	}

    sr_info("Uploading firmware '%s'.", name);

    res = lnss_flash_fpga(sdi->conn, firmware, length);

    g_free(firmware);

    return res;
}

SR_PRIV bool lnss_flash_fpga(struct sr_usb_dev_inst *usb, const uint8_t *firmware, size_t length)
{
    unsigned i, err;
    /* Straight from labnation code. don't ask me what the real plan is */
	int PACKSIZE = 32;
	unsigned PADDING = 2048/8;
	int cmd = length / PACKSIZE + PADDING;

	uint8_t cmd_start[] = {HEADER_CMD_BYTE, PICCMD_PROGRAM_FPGA_START, cmd >> 8, cmd & 0xff};
	uint8_t cmd_end[] = {HEADER_CMD_BYTE, PICCMD_PROGRAM_FPGA_END};

	err = libusb_bulk_transfer(usb->devhdl, EP_CMD_OUT, cmd_start, sizeof(cmd_start), NULL, 200);
	if (err != 0) {
		sr_err("Failed to start fpga programming: %s", libusb_error_name(err));
		return false;
	}

	err = libusb_clear_halt(usb->devhdl, EP_DATA);
	if (err != 0) {
		sr_err("Failed to clear halt stage 1: %s", libusb_error_name(err));
		return false;
	}

	int chunkcnt = 0;
	int actual;
	int actual_sum = 0;
	for (i = 0; i < length; i += 2048) {
		int desired = MIN(2048, length - i);
		err = libusb_bulk_transfer(usb->devhdl, EP_CMD_OUT, (uint8_t*)(firmware + i), desired, &actual, 200);
		if (err != 0) {
			sr_err("Failed to write chunk %d (%d bytes): %s", chunkcnt, desired, libusb_error_name(err));
			return false;
		}
		chunkcnt++;
		sr_dbg("wrote chunk %d for %d bytes, libusb: %d", chunkcnt, actual, err);
		if (actual != desired) {
			sr_warn("Failed to write a chunk %d : %d < 2048", chunkcnt, actual);
		}
		actual_sum += actual;
	}

	sr_info("After %d chunks, have written %u, length=%lu", chunkcnt, actual_sum, length);

	/* this seems rather insane, but, hey, it's what the vendor code does... */
	uint8_t data[32];
	memset(data, 0xff, sizeof(data));
	for (i = 0; i < PADDING; i++) {
		err = libusb_bulk_transfer(usb->devhdl, EP_CMD_OUT, data, 32, NULL, 200);
		if (err != 0) {
			sr_err("Failed to write 0xff trailer iteration: %d : %s", i, libusb_error_name(err));
			return false;
		}
	}
	err = libusb_bulk_transfer(usb->devhdl, EP_CMD_OUT, cmd_end, sizeof(cmd_end), NULL, 200);
	if (err != 0) {
		sr_err("Failed to exit fpga programming : %s", libusb_error_name(err));
		return false;
	}
	err = libusb_clear_halt(usb->devhdl, EP_DATA);
	if (err != 0) {
		sr_err("Failed to clear halt stage 2: %s", libusb_error_name(err));
		return false;
	}
	return true;
}

/**
 * samplerate = 1/(10e-9 * 2^decimation)
 */
SR_PRIV int lnss_subsamplerate_set(const struct sr_dev_inst *sdi, uint64_t samplerate)
{
    uint8_t power;

    if(!sdi)
        return SR_ERR_ARG;

    if(samplerate > LNSS_MAX_SAMPLERATE)
        samplerate =  LNSS_MAX_SAMPLERATE;
    else if (samplerate < LNSS_MIN_SAMPLERATE)
        samplerate = LNSS_MIN_SAMPLERATE;

    power = 0;

    while(((uint32_t)(LNSS_MAX_SAMPLERATE >> power)) > samplerate){
        power++;
    }

    sr_dbg("%s(): Input decimation %u => %luHz", __func__, power, LNSS_MAX_SAMPLERATE / (1 << power));

    return reg_set(sdi->conn, INPUT_DECIMATION, power);
}

/**
 * @brief Configures the number of samples per acquisition.
 *        Despite the 100MHz sample rate, this device is
 *        quite slow when transfering capture samples through usb.
 *        A 4M sample capture can take a while....
 *
 * length = 2048 * 2^Acquisition
 */
SR_PRIV uint32_t lnss_acquisition_depth_set(const struct sr_dev_inst *sdi, uint32_t length)
{
    uint8_t power;

    if(!sdi)
        return 0;

    if(length > LNSS_MAX_ACQUISITION)
        length =  LNSS_MAX_ACQUISITION;
    else if (length < LNSS_MIN_ACQUISITION)
        length = LNSS_MIN_ACQUISITION;

    power = 0;

    while(((uint32_t)(LNSS_MIN_ACQUISITION << power)) < length){
        power++;
    }

    length = LNSS_MIN_ACQUISITION * (1 << power);

    reg_set(sdi->conn, ACQUISITION_DEPTH, power);

    sr_dbg("%s(): Acquisition depth: %u => %u samples", __func__, power, length);

    return length;
}

SR_PRIV int lnss_triggers_set(const struct sr_dev_inst *sdi, uint8_t falling, uint8_t rising, uint8_t low, uint8_t high)
{
    struct dev_context *devc;
    uint8_t buf[4];

    devc = sdi->priv;

    buf[0] = rising;
    buf[1] = falling;
    buf[2] = high;
    buf[3] = low;

    devc->trigger_enabled = falling | rising | low | high;

    sr_dbg("%s(): Falling 0x%02X Rising 0x%02X Low 0x%02X High 0x%02X ",
                __func__,
                falling, rising,
                low, high);

    return controller_register_set(sdi->conn, CTRL_FPGA, (FPGA_I2C_ADDRESS_REG << 8) + DIGITAL_TRIGGER_RISING, 4, buf);
}

SR_PRIV int lnss_aquisition_start(const struct sr_dev_inst *sdi)
{
    struct dev_context *devc;
    int sr_error;

    devc = sdi->priv;

    //lnss_flush_data_pipe(sdi->conn);

    devc->sent_samples = 0;
    devc->trigger_sent = false;
    devc->new_acquisition = true;

    devc->pre_trigger_samples = (devc->trigger_enabled) ?
        devc->capture_ratio * ((double)devc->acquisition_depth / 100):
        0;

    reg_set(sdi->conn, TRIGGERHOLDOFF_B0, devc->pre_trigger_samples);
    reg_set(sdi->conn, TRIGGERHOLDOFF_B1, devc->pre_trigger_samples >> 8);
    reg_set(sdi->conn, TRIGGERHOLDOFF_B2, devc->pre_trigger_samples >> 16);
    reg_set(sdi->conn, TRIGGERHOLDOFF_B3, devc->pre_trigger_samples >> 24);

    strobe_set(sdi->conn, SCOPE_UPDATE, 1);
    dump_regs(sdi);
    sr_error = strobe_set(sdi->conn, ACQ_START, 1);

    sr_info("Requested %lu samples, acquiring %u ...", devc->limit_samples, devc->acquisition_depth);

    return sr_error;
}

/**
 * @brief Acquire data from logic analyzer
 *
 */
SR_PRIV int lnss_data_receive(int fd, int revents, void *cb_data)
{
    struct sr_dev_inst *sdi;
    struct dev_context *devc;
    uint64_t samples_todo;
    uint32_t samples_received;
    const uint8_t trigger_delay [] = {4,3,2,1,0,0,0,0,0,0};

    (void)fd;
	(void)revents;

    sdi = cb_data;
    devc = sdi->priv;

    uint32_t rx_buffer_len = acquisition_get(sdi->conn, devc->rx_buffer,
                SZ_HDR + (LNSS_MIN_ACQUISITION * LNSS_NUM_AN_CHANNELS));

    if(rx_buffer_len == 0){
        // Something fail, stop all
        sr_dev_acquisition_stop(sdi);
        strobe_set(sdi->conn, ACQ_STOP, 1);
        return false;
    }

    struct header *hdr = (struct header*)devc->rx_buffer;

    if(hdr->flags & (TimedOut | IsOverview)){
        return G_SOURCE_CONTINUE;
    }

    if(hdr->flags){
        if(!(hdr->flags & IsFullAcqusition)){
            return G_SOURCE_CONTINUE;
        }
    }

    // Received samples include two channels (CHA, CHB). For current setup
    // LA samples are on CHA (set by LA_CHANNEL Strobe)
    int n_channels = 2;
    samples_received = (rx_buffer_len - SZ_HDR) / n_channels;

    sr_spew("%s(): packet flags:             0x%02X", __func__, hdr->flags);
    sr_spew("%s(): packet acquisition_id:    %u", __func__, hdr->acquisition_id);
    sr_spew("%s(): packet bursts:            %u", __func__, hdr->n_bursts);
    sr_spew("%s(): packet bursts size:       %u", __func__, hdr->bytes_per_burst);
    sr_spew("%s(): packet in decimation:     %u", __func__, hdr->regs[HDR_INPUT_DECIMATION]);
    sr_spew("%s(): packet view decimation:   %u", __func__, hdr->regs[HDR_VIEW_DECIMATION]);
    sr_spew("%s(): packet acquisition depth: %u", __func__, hdr->regs[HDR_ACQUISITION_DEPTH]);
    sr_spew("%s(): packet trigger holdoff:   %u", __func__, *((uint32_t*)&hdr->regs[HDR_TRIGGERHOLDOFF_B0]));
    sr_spew("%s(): packet view offset:       %u", __func__, *((uint32_t*)&hdr->regs[HDR_VIEW_OFFSET_B0]) & 0x00FFFFFF);

    if(devc->new_acquisition){
        if(hdr->acquisition_id == devc->acquisition_id){
            sr_dbg("%s(): Ignoring packet from previous acquisition", __func__);
            return G_SOURCE_CONTINUE;
        }
        devc->new_acquisition = false;
        devc->acquisition_id = hdr->acquisition_id;
    }

    sr_dbg("%s(): Received %d samples", __func__, samples_received);

    samples_todo = (devc->sent_samples < devc->acquisition_depth) ?
            devc->acquisition_depth - devc->sent_samples : 0;

    int samples_to_send = (samples_received < samples_todo) ? samples_received : samples_todo;

    devc->trigger_delay = trigger_delay[hdr->regs[HDR_INPUT_DECIMATION]];

    //Extract LA samples from CHA to buffer start smashing header (not needed at this point)
    for(int i = 0; i < samples_to_send; i++){
        devc->rx_buffer[i] = devc->rx_buffer[SZ_HDR + (i * n_channels)];
    }

    send_logic_data(sdi, devc->rx_buffer, samples_to_send);

	devc->sent_samples += samples_to_send;

    if ((devc->acquisition_depth > 0) && (devc->sent_samples >= devc->acquisition_depth)) {
		sr_dbg("%s(): Requested number of samples reached!", __func__);
        sr_dev_acquisition_stop(sdi);
        return false;
	}

    return G_SOURCE_CONTINUE;
}
