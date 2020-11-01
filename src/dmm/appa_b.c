/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2020 Martin Eitzenberger <x@cymaphore.net>
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

/**
 * @file
 *
 * see also "appa_b.h"
 * 
 * Interface to APPA B (150/208/506) Series based Multimeters and Clamps
 * 
 * @TODO Calibration
 * @TODO BTLE comm code
 * @TODO Log download
 * @TODO Finetuning and details
 * @TODO Display reading strings
 * 
 */

#include "appa_b.h"

#include "config.h"
#include <ctype.h>
#include <glib.h>
#include <math.h>
#include <string.h>
#include <strings.h>
#include "libsigrok/libsigrok.h"
#include "libsigrok-internal.h"

#define LOG_PREFIX "appa_b"

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////

/**
 * @TODO implement BLTE-Communication (same interface as serial)
 * @TODO Implement after_open function to read device information
 * @TODO integrate further brand information to provide support for more devices
 */

#ifdef HAVE_SERIAL_COMM
/**
 * Request frame from device
 * @param serial
 * @return @sr_error_code Status
 */
SR_PRIV int sr_appa_b_packet_request(struct sr_serial_dev_inst *serial)
{
  appaBFrame_t appaFrame;
  bzero(&appaFrame, sizeof(appaFrame));
  
  appaFrame.start = APPA_B_START;
  appaFrame.command = APPA_B_COMMAND_READ_DISPLAY;
  appaFrame.dataLength = APPA_B_DATA_LENGTH_REQUEST_READ_DISPLAY;
  
  appaInt_t frameLen = APPA_B_GET_FRAMELEN(REQUEST_READ_DISPLAY);
  appaByte_t checksum = appa_b_checksum((appaByte_t*)&appaFrame, frameLen);
  
  if(serial_flush(serial) != SR_OK)
    return(SR_ERR_IO);
  
  if(serial_write_nonblocking(serial, &appaFrame, frameLen) != frameLen)
    return(SR_ERR_IO);
  
  if(serial_write_nonblocking(serial, &checksum, sizeof(checksum)) != sizeof(checksum))
    return(SR_ERR_IO);
  
  return(SR_OK);
}
#endif/*HAVE_SERIAL_COMM*/

/**
 * Validate APPA-Frame
 * @TODO validate other aspects / start code / etc.
 * 
 * @param buf
 * @return TRUE if checksum is fine
 */
SR_PRIV gboolean sr_appa_b_packet_valid(const uint8_t *buf)
{
  appaInt_t frameLen = APPA_B_GET_FRAMELEN(RESPONSE_READ_DISPLAY);
  appaByte_t checksum = appa_b_checksum((const appaByte_t*)buf, frameLen);
  return(checksum == buf[frameLen]);
}

/**
 * Parse APPA-Frame and assign values to virtual channels
 * 
 * @TODO include display reading
 * 
 * @param buf Buffer from Serial or BTLE
 * @param floatval Return display reading
 * @param analog Metadata of the reading
 * @param info Channel information and other things
 * @return @sr_error_code Status
 */
SR_PRIV int sr_appa_b_parse(const uint8_t *buf, float *floatval,
		struct sr_datafeed_analog *analog, void *info)
{
  struct appa_b_info* info_local = (struct appa_b_info*)info;
  
  gboolean isSub = info_local->ch_idx == 1;
  
  const appaBFrame_t* appaFrame = (const appaBFrame_t*)buf;
  const appaBFrame_ReadDisplayResponse_t* readDisplayResponse = (const appaBFrame_ReadDisplayResponse_t*)&appaFrame->readDisplay.response;
  const appaBFrame_ReadDisplayResponse_DisplayData_t* displayData;
  
  if(!isSub)
    displayData = (const appaBFrame_ReadDisplayResponse_DisplayData_t*)&readDisplayResponse->mainDisplayData;
  else
    displayData = (const appaBFrame_ReadDisplayResponse_DisplayData_t*)&readDisplayResponse->subDisplayData;
  
  appaDouble_t unitFactor = 1;
  appaInt_t displayReadingRaw = APPA_B_DECODE_READING((*displayData));
  appaDouble_t displayReading = (appaDouble_t)displayReadingRaw;
  
  switch(displayData->dot)
  {
    default: case APPA_B_DOT_NONE:
      analog->spec->spec_digits = 0;
      analog->encoding->digits = 0;
      unitFactor /= 1;
      break;
    case APPA_B_DOT_9999_9:
      analog->spec->spec_digits = 1;
      analog->encoding->digits = 1;
      unitFactor /= 10;
      break;
    case APPA_B_DOT_999_99:
      analog->spec->spec_digits = 2;
      analog->encoding->digits = 2;
      unitFactor /= 100;
      break;
    case APPA_B_DOT_99_999:
      analog->spec->spec_digits = 3;
      analog->encoding->digits = 3;
      unitFactor /= 1000;
      break;
    case APPA_B_DOT_9_9999:
      analog->spec->spec_digits = 4;
      analog->encoding->digits = 4;
      unitFactor /= 10000;
      break;
  }
  
  switch(displayData->dataContent)
  {
    default: case APPA_B_DATA_CONTENT_MEASURING_DATA:
      break;
    case APPA_B_DATA_CONTENT_FREQUENCY:
      break;
    case APPA_B_DATA_CONTENT_CYCLE:
      break;
    case APPA_B_DATA_CONTENT_DUTY:
      break;
    case APPA_B_DATA_CONTENT_MEMORY_STAMP:
      break;
    case APPA_B_DATA_CONTENT_MEMORY_SAVE:
      break;
    case APPA_B_DATA_CONTENT_MEMORY_LOAD:
      break;
    case APPA_B_DATA_CONTENT_LOG_SAVE:
      break;
    case APPA_B_DATA_CONTENT_LOG_LOAD:
      break;
    case APPA_B_DATA_CONTENT_LOAG_RATE:
      break;
    case APPA_B_DATA_CONTENT_REL_DELTA:
      break;
    case APPA_B_DATA_CONTENT_REL_PERCENT:
      break;
    case APPA_B_DATA_CONTENT_REL_REFERENCE:
      break;
    case APPA_B_DATA_CONTENT_MAXIMUM:
      analog->meaning->mqflags |= SR_MQFLAG_MAX;
      break;
    case APPA_B_DATA_CONTENT_MINIMUM:
      analog->meaning->mqflags |= SR_MQFLAG_MIN;
      break;
    case APPA_B_DATA_CONTENT_AVERAGE:
      analog->meaning->mqflags |= SR_MQFLAG_AVG;
      break;
    case APPA_B_DATA_CONTENT_PEAK_HOLD_MAX:
      analog->meaning->mqflags |= SR_MQFLAG_MAX;
      if(isSub)
      {
        analog->meaning->mqflags |= SR_MQFLAG_HOLD;
      }
      break;
    case APPA_B_DATA_CONTENT_PEAK_HOLD_MIN:
      analog->meaning->mqflags |= SR_MQFLAG_MIN;
      if(isSub)
      {
        analog->meaning->mqflags |= SR_MQFLAG_HOLD;
      }
      break;
    case APPA_B_DATA_CONTENT_DBM:
      break;
    case APPA_B_DATA_CONTENT_DB:
      break;
    case APPA_B_DATA_CONTENT_AUTO_HOLD:
      if(isSub)
      {
        analog->meaning->mqflags |= SR_MQFLAG_HOLD;
      }
      break;
    case APPA_B_DATA_CONTENT_SETUP:
      break;
    case APPA_B_DATA_CONTENT_LOG_STAMP:
      break;
    case APPA_B_DATA_CONTENT_LOG_MAX:
      break;
    case APPA_B_DATA_CONTENT_LOG_MIN:
      break;
    case APPA_B_DATA_CONTENT_LOG_TP:
      break;
    case APPA_B_DATA_CONTENT_HOLD:
      if(isSub)
      {
        analog->meaning->mqflags |= SR_MQFLAG_HOLD;
      }
      break;
    case APPA_B_DATA_CONTENT_CURRENT_OUTPUT:
      break;
    case APPA_B_DATA_CONTENT_CUR_OUT_0_20MA_PERCENT:
      break;
    case APPA_B_DATA_CONTENT_CUR_OUT_4_20MA_PERCENT:
      break;
  }
  
  if(readDisplayResponse->autoRange)
  {
    analog->meaning->mqflags |= SR_MQFLAG_AUTORANGE;
  }
  
  switch(readDisplayResponse->functionCode)
  {
    default: case APPA_B_FUNCTIONCODE_NONE:
      break;
    case APPA_B_FUNCTIONCODE_AC_V:
      analog->meaning->mqflags |= SR_MQFLAG_AC;
      analog->meaning->mqflags |= SR_MQFLAG_RMS;
      break;
    case APPA_B_FUNCTIONCODE_DC_V:
      analog->meaning->mqflags |= SR_MQFLAG_DC;
      break;
    case APPA_B_FUNCTIONCODE_AC_MV:
      analog->meaning->mqflags |= SR_MQFLAG_AC;
      analog->meaning->mqflags |= SR_MQFLAG_RMS;
      break;
    case APPA_B_FUNCTIONCODE_DC_MV:
      analog->meaning->mqflags |= SR_MQFLAG_DC;
      break;
    case APPA_B_FUNCTIONCODE_OHM:
      break;
    case APPA_B_FUNCTIONCODE_CONTINUITY:
      analog->meaning->mq = SR_MQ_CONTINUITY;
      break;
    case APPA_B_FUNCTIONCODE_DIODE:
      analog->meaning->mqflags |= SR_MQFLAG_DIODE;
      analog->meaning->mqflags |= SR_MQFLAG_DC;
      break;
    case APPA_B_FUNCTIONCODE_CAP:
      break;
    case APPA_B_FUNCTIONCODE_AC_A:
      analog->meaning->mqflags |= SR_MQFLAG_AC;
      analog->meaning->mqflags |= SR_MQFLAG_RMS;
      break;
    case APPA_B_FUNCTIONCODE_DC_A:
      analog->meaning->mqflags |= SR_MQFLAG_DC;
      break;
    case APPA_B_FUNCTIONCODE_AC_MA:
      analog->meaning->mqflags |= SR_MQFLAG_AC;
      analog->meaning->mqflags |= SR_MQFLAG_RMS;
      break;
    case APPA_B_FUNCTIONCODE_DC_MA:
      analog->meaning->mqflags |= SR_MQFLAG_DC;
      break;
    case APPA_B_FUNCTIONCODE_DEGC:
      break;
    case APPA_B_FUNCTIONCODE_DEGF:
      break;
    case APPA_B_FUNCTIONCODE_FREQUENCY:
      analog->meaning->mqflags |= SR_MQFLAG_RMS;
      break;
    case APPA_B_FUNCTIONCODE_DUTY:
      analog->meaning->mqflags |= SR_MQFLAG_RMS;
      break;
    case APPA_B_FUNCTIONCODE_HZ_V:
      analog->meaning->mqflags |= SR_MQFLAG_RMS;
      break;
    case APPA_B_FUNCTIONCODE_HZ_MV:
      analog->meaning->mqflags |= SR_MQFLAG_RMS;
      break;
    case APPA_B_FUNCTIONCODE_HZ_A:
      analog->meaning->mqflags |= SR_MQFLAG_RMS;
      break;
    case APPA_B_FUNCTIONCODE_HZ_MA:
      analog->meaning->mqflags |= SR_MQFLAG_RMS;
      break;
    case APPA_B_FUNCTIONCODE_AC_DC_V:
      analog->meaning->mqflags |= SR_MQFLAG_DC;
      analog->meaning->mqflags |= SR_MQFLAG_RMS;
      break;
    case APPA_B_FUNCTIONCODE_AC_DC_MV:
      analog->meaning->mqflags |= SR_MQFLAG_DC;
      analog->meaning->mqflags |= SR_MQFLAG_RMS;
      break;
    case APPA_B_FUNCTIONCODE_AC_DC_A:
      analog->meaning->mqflags |= SR_MQFLAG_DC;
      analog->meaning->mqflags |= SR_MQFLAG_RMS;
      break;
    case APPA_B_FUNCTIONCODE_AC_DC_MA:
      analog->meaning->mqflags |= SR_MQFLAG_DC;
      analog->meaning->mqflags |= SR_MQFLAG_RMS;
      break;
    case APPA_B_FUNCTIONCODE_LPF_V:
      analog->meaning->mqflags |= SR_MQFLAG_RMS;
      break;
    case APPA_B_FUNCTIONCODE_LPF_MV:
      analog->meaning->mqflags |= SR_MQFLAG_RMS;
      break;
    case APPA_B_FUNCTIONCODE_LPF_A:
      analog->meaning->mqflags |= SR_MQFLAG_RMS;
      break;
    case APPA_B_FUNCTIONCODE_LPF_MA:
      analog->meaning->mqflags |= SR_MQFLAG_RMS;
      break;
    case APPA_B_FUNCTIONCODE_AC_UA:
      analog->meaning->mqflags |= SR_MQFLAG_RMS;
      analog->meaning->mqflags |= SR_MQFLAG_AC;
      break;
    case APPA_B_FUNCTIONCODE_DC_UA:
      analog->meaning->mqflags |= SR_MQFLAG_DC;
      break;
    case APPA_B_FUNCTIONCODE_DC_A_OUT:
      analog->meaning->mqflags |= SR_MQFLAG_DC;
      break;
    case APPA_B_FUNCTIONCODE_DC_A_OUT_SLOW_LINEAR:
      analog->meaning->mqflags |= SR_MQFLAG_DC;
      break;
    case APPA_B_FUNCTIONCODE_DC_A_OUT_FAST_LINEAR:
      analog->meaning->mqflags |= SR_MQFLAG_DC;
      break;
    case APPA_B_FUNCTIONCODE_DC_A_OUT_SLOW_STEP:
      analog->meaning->mqflags |= SR_MQFLAG_DC;
      break;
    case APPA_B_FUNCTIONCODE_DC_A_OUT_FAST_STEP:
      analog->meaning->mqflags |= SR_MQFLAG_DC;
      break;
    case APPA_B_FUNCTIONCODE_LOOP_POWER:
      analog->meaning->mqflags |= SR_MQFLAG_DC;
      break;
    case APPA_B_FUNCTIONCODE_250OHM_HART:
      break;
    case APPA_B_FUNCTIONCODE_VOLT_SENSE:
      analog->meaning->mqflags |= SR_MQFLAG_AC;
      analog->meaning->mqflags |= SR_MQFLAG_RMS;
      break;
    case APPA_B_FUNCTIONCODE_PEAK_HOLD_V:
      break;
    case APPA_B_FUNCTIONCODE_PEAK_HOLD_MV:
      break;
    case APPA_B_FUNCTIONCODE_PEAK_HOLD_A:
      break;
    case APPA_B_FUNCTIONCODE_PEAK_HOLD_MA:
      break;
    case APPA_B_FUNCTIONCODE_LOZ_AC_V:
      analog->meaning->mqflags |= SR_MQFLAG_AC;
      analog->meaning->mqflags |= SR_MQFLAG_RMS;
      break;
    case APPA_B_FUNCTIONCODE_LOZ_DC_V:
      analog->meaning->mqflags |= SR_MQFLAG_DC;
      break;
    case APPA_B_FUNCTIONCODE_LOZ_AC_DC_V:
      analog->meaning->mqflags |= SR_MQFLAG_DC;
      analog->meaning->mqflags |= SR_MQFLAG_RMS;
      break;
    case APPA_B_FUNCTIONCODE_LOZ_LPF_V:
      break;
    case APPA_B_FUNCTIONCODE_LOZ_HZ_V:
      analog->meaning->mqflags |= SR_MQFLAG_AC;
      analog->meaning->mqflags |= SR_MQFLAG_RMS;
      break;
    case APPA_B_FUNCTIONCODE_LOZ_PEAK_HOLD_V:
      break;
    case APPA_B_FUNCTIONCODE_BATTERY:
      break;
    case APPA_B_FUNCTIONCODE_AC_W:
      analog->meaning->mqflags |= SR_MQFLAG_AC;
      analog->meaning->mqflags |= SR_MQFLAG_RMS;
      break;
    case APPA_B_FUNCTIONCODE_DC_W:
      analog->meaning->mqflags |= SR_MQFLAG_DC;
      break;
    case APPA_B_FUNCTIONCODE_PF:
      analog->meaning->mqflags |= SR_MQFLAG_AC;
      analog->meaning->mqflags |= SR_MQFLAG_RMS;
      break;
    case APPA_B_FUNCTIONCODE_FLEX_AC_A:
      analog->meaning->mqflags |= SR_MQFLAG_AC;
      analog->meaning->mqflags |= SR_MQFLAG_RMS;
      break;
    case APPA_B_FUNCTIONCODE_FLEX_LPF_A:
      analog->meaning->mqflags |= SR_MQFLAG_AC;
      analog->meaning->mqflags |= SR_MQFLAG_RMS;
      break;
    case APPA_B_FUNCTIONCODE_FLEX_PEAK_HOLD_A:
      analog->meaning->mqflags |= SR_MQFLAG_AC;
      analog->meaning->mqflags |= SR_MQFLAG_RMS;
      break;
    case APPA_B_FUNCTIONCODE_FLEX_HZ_A:
      analog->meaning->mqflags |= SR_MQFLAG_AC;
      analog->meaning->mqflags |= SR_MQFLAG_RMS;
      break;
    case APPA_B_FUNCTIONCODE_V_HARM:
      analog->meaning->mqflags |= SR_MQFLAG_AC;
      analog->meaning->mqflags |= SR_MQFLAG_RMS;
      break;
    case APPA_B_FUNCTIONCODE_INRUSH:
      analog->meaning->mqflags |= SR_MQFLAG_AC;
      analog->meaning->mqflags |= SR_MQFLAG_RMS;
      break;
    case APPA_B_FUNCTIONCODE_A_HARM:
      analog->meaning->mqflags |= SR_MQFLAG_AC;
      analog->meaning->mqflags |= SR_MQFLAG_RMS;
      break;
    case APPA_B_FUNCTIONCODE_FLEX_INRUSH:
      analog->meaning->mqflags |= SR_MQFLAG_AC;
      analog->meaning->mqflags |= SR_MQFLAG_RMS;
      break;
    case APPA_B_FUNCTIONCODE_FLEX_A_HARM:
      analog->meaning->mqflags |= SR_MQFLAG_AC;
      analog->meaning->mqflags |= SR_MQFLAG_RMS;
      break;
    case APPA_B_FUNCTIONCODE_PEAK_HOLD_UA:
      analog->meaning->mqflags |= SR_MQFLAG_AC;
      analog->meaning->mqflags |= SR_MQFLAG_RMS;
      break;
  }
  
  switch(displayData->unit)
  {
    default: case APPA_B_UNIT_NONE:
      analog->meaning->unit = SR_UNIT_UNITLESS;
      break;
    case APPA_B_UNIT_MV:
      analog->meaning->unit = SR_UNIT_VOLT;
      analog->meaning->mq = SR_MQ_VOLTAGE;
      unitFactor /= 1000;
      analog->encoding->digits += 3;
      break;
    case APPA_B_UNIT_V:
      analog->meaning->unit = SR_UNIT_VOLT;
      analog->meaning->mq = SR_MQ_VOLTAGE;
      break;
    case APPA_B_UNIT_UA:
      analog->meaning->unit = SR_UNIT_AMPERE;
      analog->meaning->mq = SR_MQ_CURRENT;
      unitFactor /= 1000000;
      analog->encoding->digits += 6;
      break;
    case APPA_B_UNIT_MA:
      analog->meaning->unit = SR_UNIT_AMPERE;
      analog->meaning->mq = SR_MQ_CURRENT;
      unitFactor /= 1000;
      analog->encoding->digits += 3;
      break;
    case APPA_B_UNIT_A:
      analog->meaning->unit = SR_UNIT_AMPERE;
      analog->meaning->mq = SR_MQ_CURRENT;
      break;
    case APPA_B_UNIT_DB: 
      analog->meaning->unit = SR_UNIT_DECIBEL_VOLT;
      analog->meaning->mq = SR_MQ_POWER;
      break;
    case APPA_B_UNIT_DBM: 
      analog->meaning->unit = SR_UNIT_DECIBEL_MW;
      analog->meaning->mq = SR_MQ_POWER;
      break;
    case APPA_B_UNIT_NF: 
      analog->meaning->unit = SR_UNIT_FARAD;
      analog->meaning->mq = SR_MQ_CAPACITANCE;
      unitFactor /= 1000000000;
      analog->encoding->digits += 9;
      break;
    case APPA_B_UNIT_UF: 
      analog->meaning->unit = SR_UNIT_FARAD;
      analog->meaning->mq = SR_MQ_CAPACITANCE;
      unitFactor /= 1000000;
      analog->encoding->digits += 6;
      break;
    case APPA_B_UNIT_MF: 
      analog->meaning->unit = SR_UNIT_FARAD;
      analog->meaning->mq = SR_MQ_CAPACITANCE;
      unitFactor /= 1000;
      analog->encoding->digits += 3;
      break;
    case APPA_B_UNIT_GOHM: 
      analog->meaning->unit = SR_UNIT_OHM;
      analog->meaning->mq = SR_MQ_RESISTANCE;
      unitFactor *= 1000000000;
      break;
    case APPA_B_UNIT_MOHM: 
      analog->meaning->unit = SR_UNIT_OHM;
      analog->meaning->mq = SR_MQ_RESISTANCE;
      unitFactor *= 1000000;
      break;
    case APPA_B_UNIT_KOHM: 
      analog->meaning->unit = SR_UNIT_OHM;
      analog->meaning->mq = SR_MQ_RESISTANCE;
      unitFactor *= 1000;
      break;
    case APPA_B_UNIT_OHM: 
      analog->meaning->unit = SR_UNIT_OHM;
      analog->meaning->mq = SR_MQ_RESISTANCE;
      break;
    case APPA_B_UNIT_PERCENT: 
      analog->meaning->unit = SR_UNIT_PERCENTAGE;
      analog->meaning->mq = SR_MQ_DIFFERENCE;
      break;
    case APPA_B_UNIT_MHZ: 
      analog->meaning->unit = SR_UNIT_HERTZ;
      analog->meaning->mq = SR_MQ_FREQUENCY;
      unitFactor *= 1000000;
      break;
    case APPA_B_UNIT_KHZ: 
      analog->meaning->unit = SR_UNIT_HERTZ;
      analog->meaning->mq = SR_MQ_FREQUENCY;
      unitFactor *= 1000;
      break;
    case APPA_B_UNIT_HZ: 
      analog->meaning->unit = SR_UNIT_HERTZ;
      analog->meaning->mq = SR_MQ_FREQUENCY;
      break;
    case APPA_B_UNIT_DEGC: 
      analog->meaning->unit = SR_UNIT_CELSIUS;
      analog->meaning->mq = SR_MQ_TEMPERATURE;
      break;
    case APPA_B_UNIT_DEGF: 
      analog->meaning->unit = SR_UNIT_FAHRENHEIT;
      analog->meaning->mq = SR_MQ_TEMPERATURE;
      break;
    case APPA_B_UNIT_NS: 
      analog->meaning->unit = SR_UNIT_SECOND;
      analog->meaning->mq = SR_MQ_TIME;
      unitFactor /= 1000000000;
      analog->encoding->digits += 9;
      break;
    case APPA_B_UNIT_US: 
      analog->meaning->unit = SR_UNIT_SECOND;
      analog->meaning->mq = SR_MQ_TIME;
      unitFactor /= 1000000;
      analog->encoding->digits += 6;
      break;
    case APPA_B_UNIT_MS: 
      analog->meaning->unit = SR_UNIT_SECOND;
      analog->meaning->mq = SR_MQ_TIME;
      unitFactor /= 1000;
      analog->encoding->digits += 3;
      break;
    case APPA_B_UNIT_SEC: 
      analog->meaning->unit = SR_UNIT_SECOND;
      analog->meaning->mq = SR_MQ_TIME;
      break;
    case APPA_B_UNIT_MIN: 
      analog->meaning->unit = SR_UNIT_SECOND;
      analog->meaning->mq = SR_MQ_TIME;
      unitFactor *= 60;
      break;
    case APPA_B_UNIT_KW: 
      analog->meaning->unit = SR_UNIT_WATT;
      analog->meaning->mq = SR_MQ_POWER;
      unitFactor *= 1000;
      break;
    case APPA_B_UNIT_PF: 
      analog->meaning->unit = SR_UNIT_UNITLESS;
      analog->meaning->mq = SR_MQ_POWER_FACTOR;
      break;
  }
  
  displayReading *= unitFactor;  
  
  if(displayReadingRaw == APPA_B_WORDCODE_BATT)
  {
    sr_err("Battery low");
  }
    
  if(displayData->overload
      || APPA_B_IS_WORDCODE(displayReadingRaw))
  {
    *floatval = INFINITY;
  }
  else
  {
    *floatval = displayReading;
  }
  
  info_local->ch_idx++;
  return(SR_OK);
}

SR_PRIV const char *sr_appa_b_channel_formats[APPA_B_DISPLAY_COUNT] = {
	"main",
  "sub",
};
