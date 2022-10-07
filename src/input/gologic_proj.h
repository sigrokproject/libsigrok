/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2020 xorloser me@xorloser.com
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

// 
// Support for getting sample data from gologic project files
// This should be C code (not C++) so it can also be used with sigrok.
// 
// This supports both GoLogic and GologicXL projects.
// In the code the following abbreviations will be used:
// GL  = GoLogic	(common signifier for both versions)
// GL1 = GoLogic	(older version)
// GL2 = GoLogic XL	(newer version)
// 
// xorloser August 2020
// 

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// This is located at the start of the GoLogic/GoLogicXL project files.
#define GL1_PROJ_MAGIC	"PK"
#define GL2_PROJ_MAGIC	"GoLogicXL XLP"

#define GL1_TRACE_MAGIC	"Version 5.00 TRACE"
#define GL2_TRACE_MAGIC	"v6.00.0010"

#define GL_MAX_CHANNELS			72		// 32+4+32+4=72 channels max
#define GL_MAX_GROUPS			100		// groups can be single channels or collections of channels
#define GL_MAX_NAME				100		// max number of chars in name (includes null terminator in this count)
#define GL_MAX_CHANNELS_PER_GROUP	72	// number of channels in a group

#pragma pack(push, 1)

// There is one of these at the start of the file.
// 0x5F2 bytes
typedef struct {
	/*000*/char magic[0x14];		// "Version 5.00 TRACE\0\0"
	/*014*/uint32_t hdrSize;		// 0x5F2 bytes (total size of header)
	/*018*/uint32_t unkn018;		// 0x48 (number of channels+clocks ?)
	/*01C*/uint32_t unkn01C;		// 0x5 or 0x6
	/*020*/uint32_t unkn020;		// 0x2
	/*024*/uint16_t channels1[64];	// 0x0000 - 0x003F for A0-A15, B0-B15, C0-C16, D0-D15
	/*0A4*/uint16_t clocks1[8];		// 0x1000 - 0x1007 for CA0-CA3, CC0-CC3
	/*0B4*/uint8_t  zeros0B4[0x1F0];// all 0x00
	/*2A4*/uint8_t  ChannelsCnta;	// 0x40
	/*2A5*/uint8_t  ChannelsCntb;	// 0x40
	/*2A6*/uint16_t channels[64];	// 0x0000 - 0x003F for A0-A15, B0-B15, C0-C16, D0-D15
	/*326*/uint64_t valsSize;		// (numSamples+1) * 8 == sizeof(gologic_val_t) section
	/*32E*/uint8_t  clocksCnta;		// 0x08
	/*32F*/uint8_t  clocksCntb;		// 0x08
	/*330*/uint16_t clocks[8];		// 0x1000 - 0x1007 for CA0-CA3, CC0-CC3
	/*340*/uint8_t  zeros340[0x70];	// all 0x00
	/*3B0*/uint64_t clkvalsSize;	// (numSamples+1) * 1 == sizeof(gologic_changes_t) section
	/*3B8*/uint8_t  zeros3B4[0x19E];// all 0x00
	/*556*/uint32_t numSamples;		// number of samples (each sample may contain multiple bits)	(perhaps this is 64bits not 32bits?)
	/*55A*/uint8_t  zeros55A[0x10];	// all 0x00
	/*56A*/uint32_t unkn56A;		// 0x419DCD65
	/*56E*/uint8_t  zeros56E[0x6];	// all 0x00
	/*574*/uint8_t  unkn574[0x2];	// 0x00, 0x40
	/*576*/char timingName[0x20];	// "transitional timing" or "normal timing" padded with 0x00
	/*596*/uint8_t  zeros596[0x44];	// all 0x00
	/*5DA*/uint32_t unkn5DA;		// 1
	/*5DE*/uint64_t timesSize;		// (numSamples+1) * 8 == sizeof(gologic_time_t) section
	/*5E6*/uint8_t  zeros5E6[0xC];	// all 0x00
} gl1_trace_hdr_t;

// 64 single bit values of 0 or 1.
// in the order from lsb to msb: A0-A15, B0-B15, C0-C15, D0-D15.
typedef uint64_t gl_trace_val64_t;

// Sometimes used instead of 64bit.
// (TODO: when and why is this used?)
typedef uint32_t gl_trace_val32_t;

//  8 single bit values of 0 or 1.
// gl1 : in the order from lsb to msb: CA0-CA3, CC0-CC3.
// gl2 : in the order from lsb to msb: A16-A17,B16-B17, C16-C17,D16-D17
//typedef uint8_t  gl_trace_valclk_t;
typedef uint16_t gl_trace_val16_t;
typedef uint8_t gl_trace_val8_t;

// 64bit time value in "time units" based on current settings.
// gl1: eg for 125MHz, one time unit represents 8ns.
// gl2: double value in nanoseconds
// output: 64bit picosecond count
typedef uint64_t gl_trace_time_t;


// There is one of these at the start of the file.
// Lots of fields are unknown, but really don't need to know most of them.
// 0x780 bytes
typedef struct {
	/*000*/char magic[0x14];		// "v6.00.0010\0" (with padding of 0xFE bytes)
	/*014*/uint32_t hdrSize;		// 0x780 bytes (total size of header)
	/*018*/uint32_t unkn018;		// 0x24 or 0x48 (number of channels+clocks per port?)
	/*01C*/uint32_t unkn01C;		// 0x24 or 0x48 (number of channels+clocks per port?)
	/*020*/uint32_t unkn020;		// 0x01 or 0x24 (number of channels+clocks per port?)
	/*024*/uint32_t unkn024;		// 2
	/*028*/uint16_t channels1[32];	// 0x0000 - 0x001F for A0-A15, B0-B15 ??
	/*068*/uint16_t clocks1[4];		// 0x1000 - 0x1003 for A16-A17,B16-B17, C16-C17,D16-D17 ??
	/*070*/uint8_t  zeros0B4[0x238];// all 0x00
	/*2A8*/uint8_t  ChannelsCnta;	// 0x20
	/*2A9*/uint8_t  ChannelsCntb;	// 0x20
	/*2AA*/uint16_t channels2[32];	// 0x0404 - 0x0404 for ???
	/*2EA*/uint16_t channels3[32];	// 0x0000 - 0x001F for ???
	/*32A*/uint8_t  zeros32A[0x40];	// all 0x00
	/*36A*/uint64_t valsSize;		// (numSamples+1) * 8 == sizeof(gologic_val_t) section
	/*372*/uint8_t  clocksCnta;		// 0x08 or 0x10
	/*373*/uint8_t  clocksCntb;		// 0x04
	/*374*/uint16_t channels4[32];	// 0x0404 - 0x0404 for ???
	/*3B4*/uint16_t clocks[4];		// 0x1000 - 0x1003 for A16-A17,B16-B17, C16-C17,D16-D17 ??
	/*3BC*/uint8_t  zeros340[0x78];	// all 0x00
	/*434*/uint64_t clkvalsSize;	// (numSamples+1) * 1 == sizeof(gologic_changes_t) section
	/*43C*/uint8_t  zeros43C[0x2];	// all 0x00
	/*43E*/uint16_t channels5[32];	// 0x0404 - 0x0404 for ???
	/*47E*/uint8_t  zeros47E[0x82];	// all 0x00
	/*500*/uint64_t unkn500;		// 0x10
	/*508*/uint16_t channels6[32];	// 0x0404 - 0x0404 for ???
	/*548*/uint8_t  zeros548[0x82];	// all 0x00
	/*5CA*/uint64_t unkn5CA;		// 0x10
	/*5D2*/uint16_t channels7[32];	// 0x0404 - 0x0404 for ???
	/*612*/uint8_t  zeros612[0x82];	// all 0x00
	/*694*/uint64_t unkn694;		// 0x10
	/*69C*/uint8_t  zeros69C[0x2];	// all 0x00
	/*69E*/uint32_t numSamples;		// number of samples
	/*6A2*/uint8_t  unkn6A2[0x20];
	/*6C2*/uint32_t unkn6C2;		// 0x41BDCD65 (close to value from GoLogic)
	/*6C6*/uint64_t unkn6C6;		// 0x40540000_00000000
	/*6CE*/char info[0x32];			// "10/18/12;8:0:0" (padded with 0xFE)
	/*700*/char timingName[0x64];	// "transitional timing" or "rs232 timing" (padded with 0xFD)
	/*764*/uint32_t unkn764;		// 1
	/*768*/uint64_t timesSize;		// 0x1410
	/*770*/uint32_t unkn770;		// 0x281
	/*774*/uint8_t  zeros774[0xC];	// all 0x00
} gl2_trace_hdr_t;

#pragma pack(pop)




typedef struct gl_ini_entry_t {
	char* section;
	char* name;
	char* value;
	struct gl_ini_entry_t* next;
} gl_ini_entry_t;

typedef struct gl_ini_t {
	gl_ini_entry_t* pHead;	// pointer to first
	gl_ini_entry_t* pTail;	// pointer to last
} gl_ini_t;

// GoLogic Color codes (00RRGGBB)
#define GL_COLOR_GREY	0	// 00DCDCDC
#define GL_COLOR_BROWN	1	// 00DCA060
#define GL_COLOR_RED	2	// 00FF0000
#define GL_COLOR_ORANGE	3	// 00FF8000
#define GL_COLOR_YELLOW	4	// 00FFFF00
#define GL_COLOR_GREEN	5	// 0000FF00
#define GL_COLOR_AQUA	6	// 0000FFFF
#define GL_COLOR_PINK	7	// 00FF00FF
#define GL_COLOR_WHITE	8	// 00FFFFFF
// any number higher is a custom color defgined in setup.txt:Project colors:9/10/11/etc
#define GL_COLOR_MAX	8

// info about a group of channels
// 0xAD bytes (maybe more do to alignment)
typedef struct {
	uint32_t color;					// GL_COLOR_?? value or color in 0xFFRRGGBB (RGB format with 0xFF000000 set)
	uint8_t numBits;				// number of bits in bitIdx array
	uint8_t bitIdx[GL_MAX_CHANNELS];// bit index: 0-63 in val, 64-72 in clkval
	char name[GL_MAX_NAME];			// name to display for group
} gl_group_info_t;

/*
// These are not actually used anymore in this code.
// They are kept here in case their use is reinstated.

// trace types numbers
#define GL_TRACE_TYPE_UNKNOWN	0
#define GL_TRACE_TYPE_NORMAL	1
#define GL_TRACE_TYPE_TRANS		2
#define GL_TRACE_TYPE_SIMPLE	3
#define GL_TRACE_TYPE_SIMPLETIME 4
#define GL_TRACE_TYPE_COMPLEX	5
#define GL_TRACE_TYPE_4TRANS	6
#define GL_TRACE_TYPE_I2CTIME	7
#define GL_TRACE_TYPE_I2CSTATE	8
#define GL_TRACE_TYPE_SPITIME	9
#define GL_TRACE_TYPE_UARTTIME	10
#define GL_TRACE_TYPE_CANTIME	11
#define GL_TRACE_TYPE_LINTIME	12
#define GL_TRACE_TYPE_BITTIME	13
#define GL_TRACE_TYPE_I2STIME	14

// trace types strings
#define GL_TRACE_TYPESTR_UNKNOWN	"unknown"
#define GL_TRACE_TYPESTR_NORMAL		"normal timing"
#define GL_TRACE_TYPESTR_TRANS		"transitional timing"
#define GL_TRACE_TYPESTR_SIMPLE		"simple state"
#define GL_TRACE_TYPESTR_SIMPLETIME	"simple state with time stamp"
#define GL_TRACE_TYPESTR_COMPLEX	"complex state"
#define GL_TRACE_TYPESTR_4TRANS		"four channel trans"
#define GL_TRACE_TYPESTR_I2CTIME	"i2c timing"
#define GL_TRACE_TYPESTR_I2CSTATE	"i2c state"
#define GL_TRACE_TYPESTR_SPITIME	"spi timing"
#define GL_TRACE_TYPESTR_UARTTIME	"UART timing"
#define GL_TRACE_TYPESTR_CANTIME	"can timing"
#define GL_TRACE_TYPESTR_LINTIME	"lin timing"
#define GL_TRACE_TYPESTR_BITTIME	"bitstream timing"
#define GL_TRACE_TYPESTR_I2STIME	"i2s timing"
// from gl2
#define GL_TRACE_TYPESTR_RS232TIME	"rs232 timing"
*/

typedef struct {
	uint8_t* pData;
	size_t dataSize;
	gl1_trace_hdr_t* pHdr1;
	gl2_trace_hdr_t* pHdr2;
//	uint32_t type;					// 1="normal timing", 2="transitional timing"
	uint32_t numSamples;			// number of entries in pVal32/pVal64, pValClk, pTime.
	gl_trace_val64_t* pVal64;
	gl_trace_val32_t* pVal32;
	gl_trace_val16_t* pVal16;
	gl_trace_val8_t* pVal8;
	gl_trace_val16_t* pValClk16;
	gl_trace_val8_t* pValClk8;
	gl_trace_time_t* pTime;
} gl_trace_t;

typedef struct {
	uint32_t version;		// 1=GoLogic, 2=GoLogicXL
	uint64_t sampleRate;		// 0=not set. otherwise this is samplerate in Hz.
	uint64_t samplePeriod;	// length of one sample in picoseconds
	// groups contains both single channels and groups of channels.
	uint32_t numGroups;
	gl_group_info_t groups[GL_MAX_GROUPS];
	// channels is the subset of groups that contains only single channels.
	// since this is a subset of groups, this contains indexes into groups info.
	uint32_t numChannels;
	uint32_t channelToGroupIdx[GL_MAX_GROUPS];
	// multi-channels is the subset of groups that contains more than one channel.
	// since this is a subset of groups, this contains indexes into groups info.
	uint32_t numMultiChannels;
	uint32_t multiChannelToGroupIdx[GL_MAX_GROUPS];
	// TODO: markers
	// TODO: current location in waveform
	// TODO: current zoom of waveform
	// ini file entries
	gl_ini_t project_ini;
	gl_ini_t setup_txt;
	gl_ini_t serial_display_txt;
	gl_ini_t version_txt;
	// trace file values
	gl_trace_t trace_dat;
} gl_project_t;

// info about a channel
typedef struct {
	uint32_t color;				// color in 0x00RRGGBB (RGB format)
	uint8_t bitIdx;				// bit index: 0-63 in val, 64-72 in clkval
	char name[GL_MAX_NAME];		// name to display for channel
} gl_channel_info_t;

// info for a sample of all channels at a point in time
typedef struct {
	uint64_t val;				// sample bit values (from lsb to msb) for A0-A15, B0-B15, C0-C15, D0-D15
	uint8_t  clkval;			// sample bit values (from lsb to msb) for:
								// gl1: CA0-CA3, CC0-CC3
								// gl2: A16-A17,B16-B17, C16-C17,D16-D17
	uint64_t time;				// picosecond offset of this sample from the start of the entire trace.
} gl_sample_info_t;


// Opens and closes project file.
int  gl_project_open_buffer(gl_project_t* pProj, const uint8_t* pData, size_t dataSize);
int  gl_project_open_file(gl_project_t* pProj, const char* filename);
void gl_project_close(gl_project_t* pProj);

// Get sample rate in Hz
int  gl_project_sample_rate(gl_project_t* pProj, uint64_t* pSampleRate);

// Get sample period in picoseconds
int  gl_project_sample_period(gl_project_t* pProj, uint64_t* pSamplePeriod);

// Get the number of groups.
// Groups can be one or more channels.
int  gl_group_cnt(gl_project_t* pProj);
int  gl_group_info(gl_project_t* pProj, uint32_t idx, gl_group_info_t* pInfo);

// Gets the number of single channels.
// This is the subset of groups that have only 1 channel.
int  gl_channel_cnt(gl_project_t* pProj);
int  gl_channel_info(gl_project_t* pProj, uint32_t idx, gl_channel_info_t* pInfo);

// Gets the number of samples.
// Each sample can be for multiple groups/channels.
int  gl_sample_cnt(gl_project_t* pProj, uint64_t* pSampleCnt);
int  gl_sample_info(gl_project_t* pProj, uint64_t idx, uint64_t cnt, gl_sample_info_t* pInfo);

#ifdef __cplusplus
}
#endif
