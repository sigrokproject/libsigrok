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

#include <string.h>
#include <stdlib.h>
#include <malloc.h>
#include "gologic_proj.h"
#include "zip.h"
#include "ini.h"

static int _gl_trace_open(gl_trace_t* pTrace, const uint8_t* pData, size_t dataSize, int glVersion)
{
	int ret = 0;

	pTrace->pData = NULL;
	do
	{
		if (glVersion == 0)
		{
			if (memcmp(pData, GL1_TRACE_MAGIC, sizeof(GL1_TRACE_MAGIC)) == 0)
				glVersion = 1;
			else if (memcmp(pData, GL2_TRACE_MAGIC, sizeof(GL2_TRACE_MAGIC)) == 0)
				glVersion = 2;
			else
			{
				ret = -1;
				break;
			}
		}

		memset(pTrace, 0, sizeof(*pTrace));

		// copy over trace data
		pTrace->pData = (uint8_t*)malloc(dataSize);
		if (pTrace->pData == NULL)
		{
			ret = -4;
			break;
		}
		pTrace->dataSize = dataSize;
		memcpy(pTrace->pData, pData, pTrace->dataSize);

		uint8_t* p_body = NULL;
		size_t body_size = 0;
		uint64_t val_size = 0;
		uint64_t clkval_size = 0;
		uint64_t time_size = 0;
		if (glVersion == 1)
		{
			pTrace->pHdr1 = (gl1_trace_hdr_t*)pTrace->pData;
			p_body = (uint8_t*)(pTrace->pHdr1 + 1);
			body_size = dataSize - sizeof(*pTrace->pHdr1);
			val_size = pTrace->pHdr1->valsSize;
			clkval_size = pTrace->pHdr1->clkvalsSize;
			time_size = pTrace->pHdr1->timesSize;
			pTrace->numSamples = pTrace->pHdr1->numSamples;
			if (time_size != 0 )
				pTrace->numSamples++;
			if (body_size != val_size + clkval_size + time_size)
			{
				//printf("bad size of parts\n");
				ret = -7;
				break;
			}
		}
		else if (glVersion == 2)
		{
			pTrace->pHdr2 = (gl2_trace_hdr_t*)pTrace->pData;
			p_body = (uint8_t*)(pTrace->pHdr2 + 1);
			body_size = dataSize - sizeof(*pTrace->pHdr2);
			val_size = pTrace->pHdr2->valsSize;
			clkval_size = pTrace->pHdr2->clkvalsSize;
			time_size = pTrace->pHdr2->timesSize;
			pTrace->numSamples = pTrace->pHdr2->numSamples;
			if (body_size != val_size + clkval_size + time_size)
			{
				//printf("bad size\n");
				ret = -7;
				break;
			}
		}
		else
		{
			ret = -2;
			break;
		}

		if(val_size >= (uint64_t)pTrace->numSamples * 8)
			pTrace->pVal64 = (gl_trace_val64_t*)(p_body);
		else if (val_size >= (uint64_t)pTrace->numSamples * 4)
			pTrace->pVal32 = (gl_trace_val32_t*)(p_body);
		else if (val_size >= (uint64_t)pTrace->numSamples * 2)
			pTrace->pVal16 = (gl_trace_val16_t*)(p_body);
		else if (val_size >= (uint64_t)pTrace->numSamples * 1)
			pTrace->pVal8 = (gl_trace_val8_t*)(p_body);
		// TODO: in gl2 it seems that clkval can sometimes be 16bit?!
		// need to work out how and why that is used, but i dont actally
		// have a gl2 so i cant do that.
		// It seems that 8bits of data is split into 4bits per 8bit of to
		// give a 16bit value. But why? Perhaps the examples I have are
		// missing something?
		if(clkval_size >= (uint64_t)pTrace->numSamples * 2)
			pTrace->pValClk16 = (gl_trace_val16_t*)pTrace->pValClk8;
		else if (clkval_size >= (uint64_t)pTrace->numSamples * 1)
			pTrace->pValClk8 = (gl_trace_val8_t*)(p_body + val_size);
		if (time_size == 0)
		{
			pTrace->pTime = NULL;
		}
		else
		{
			pTrace->pTime = (gl_trace_time_t*)(p_body + val_size + clkval_size);
		}

		// success
		ret = 0;
	} while (0);
	
	// check if error
	if (ret < 0)
	{
		if(pTrace->pData)
			free(pTrace->pData);
		pTrace->pData = NULL;
		pTrace->pVal64 = NULL;
		pTrace->pVal32 = NULL;
		pTrace->pValClk16 = NULL;
		pTrace->pValClk8 = NULL;
		pTrace->pTime = NULL;
	}
	return ret;
}

static int _gl_trace_close(gl_trace_t* pTrace)
{
	if (pTrace->pData)
	{
		free(pTrace->pData);
	}
	memset(pTrace, 0, sizeof(*pTrace));
	return 0;
}


static int _gl_ini_callback(void* user, const char* section, const char* name,
	const char* value)
{
	gl_ini_t* p_ini = (gl_ini_t*)user;
	gl_ini_entry_t* p_ini_entry = (gl_ini_entry_t*)malloc(sizeof(gl_ini_entry_t));
	if (p_ini_entry == NULL)
		return 0;
	if (p_ini->pHead == NULL)
	{
		p_ini->pHead = p_ini->pTail = p_ini_entry;
	}
	else
	{
		p_ini->pTail->next = p_ini_entry;
		p_ini->pTail = p_ini_entry;
	}

	p_ini_entry->section = _strdup(section);
	p_ini_entry->name = _strdup(name);
	p_ini_entry->value = _strdup(value);
	p_ini_entry->next = NULL;
	return 1;
}

static int _gl_ini_open(gl_ini_t* pIni, const char* pIniStr)
{
	int ret = ini_parse_string(pIniStr, _gl_ini_callback, pIni);
	return ret;
}

static void _gl_ini_close(gl_ini_t* pIni)
{
	gl_ini_entry_t* p_ini_entry = pIni->pHead;
	while (p_ini_entry)
	{
		gl_ini_entry_t* p_entry_to_free = p_ini_entry;
		p_ini_entry = p_ini_entry->next;
		if (p_entry_to_free->section)
			free(p_entry_to_free->section);
		if (p_entry_to_free->name)
			free(p_entry_to_free->name);
		if (p_entry_to_free->value)
			free(p_entry_to_free->value);
		if (p_entry_to_free)
			free(p_entry_to_free);
	}
	pIni->pHead = pIni->pTail = NULL;
}

static const char* _gl_ini_get_value(gl_ini_t* pIni, const char* section, const char* name)
{
	gl_ini_entry_t* p_ini_entry = pIni->pHead;
	while (p_ini_entry)
	{
		if (strcmp(p_ini_entry->section, section) == 0 &&
			strcmp(p_ini_entry->name, name) == 0)
		{
			// found it
			return p_ini_entry->value;
		}
		p_ini_entry = p_ini_entry->next;
	}
	// not found
	return NULL;
}

// gets the next "token" from a string
// returns a pointer to the string past the next separator
static char* _gl_ini_get_token(const char* pStr, char sep, char* pDest)
{
	char* p_sep = strchr(pStr, sep);
	if (p_sep == NULL)
	{
		// no separator - last item in string
		strcpy(pDest, pStr);
		return (char*)pStr + strlen(pStr);
	}
	size_t len = p_sep - pStr;
	memcpy(pDest, pStr, len);
	pDest[len] = '\0';
	return p_sep + 1;
}

static int _channel_to_bit_idx(const char* pChannelStr)
{
	const char* p_letters = pChannelStr;
	const char* p_nums = p_letters+1;
	if (*p_nums == 'A' || *p_nums == 'C')
		p_nums++;
	int chan_num = strtol(p_nums, 0, 10);
	if (chan_num < 0 || chan_num > 17)
		return -1;
	if (p_letters[0] == 'C' && p_letters[1] == 'A')
	{
		if(chan_num <= 3)
			return 0x40 + chan_num;
		return -1;
	}
	else if (p_letters[0] == 'C' && p_letters[1] == 'C')
	{
		if (chan_num <= 3)
			return 0x44 + chan_num;
		return -1;
	}
	else if (p_letters[0] == 'A')
	{
		if(chan_num>=16 )
			return 0x40 + (chan_num-16);
		else
			return  0x00 + chan_num;
	}
	else if (p_letters[0] == 'B')
	{
		if (chan_num >= 16)
			return 0x42 + (chan_num - 16);
		else
			return 0x10 + chan_num;
	}
	else if (p_letters[0] == 'C')
	{
		if (chan_num >= 16)
			return 0x44 + (chan_num - 16);
		else
			return 0x20 + chan_num;
	}
	else if (p_letters[0] == 'D')
	{
		if (chan_num >= 16)
			return 0x46 + (chan_num - 16);
		else
			return 0x30 + chan_num;
	}
	return -1;
}
static int _parse_group_channels(const char* pChannels, uint8_t* pNumBits, uint8_t* bitIdx)
{
	int num_bits = 0;
	const char* ptr = pChannels;
	while (*ptr)
	{
		char token[16];
		ptr = _gl_ini_get_token(ptr, ',', token);
		char* p_dash = strchr(token, '-');
		if (p_dash)
		{
			// dash means a continuous run of channels
			int start = _channel_to_bit_idx(token);
			if (start < 0)
				return start;
			int end = _channel_to_bit_idx(p_dash + 1);
			if (end < 0)
				return start;
			for (int i = start; i <= end; i++)
			{
				bitIdx[num_bits] = i;
				num_bits++;
			}
		}
		else
		{
			// no dash means single channel
			int val = _channel_to_bit_idx(token);
			if (val < 0)
				return val;
			bitIdx[num_bits] = val;
			num_bits++;
		}
	}
	
	// success
	*pNumBits = num_bits;
	return 0;
}


static int _gl_add_group(gl_project_t* pProj, gl_group_info_t* pGroup)
{
	memcpy(&pProj->groups[pProj->numGroups], pGroup, sizeof(*pGroup));

	// set as single channel if only 1 channel bit
	if (pProj->groups[pProj->numGroups].numBits == 1)
	{
		pProj->channelToGroupIdx[pProj->numChannels] = pProj->numGroups;
		pProj->numChannels++;
	}
	// set as multi channel is more than 1 channel bit
	else if (pProj->groups[pProj->numGroups].numBits > 1)
	{
		pProj->multiChannelToGroupIdx[pProj->numMultiChannels] = pProj->numGroups;
		pProj->numMultiChannels++;
	}
	
	pProj->numGroups++;
	
	return 0;
}

int gl_project_open_buffer(gl_project_t* pProj, const uint8_t* pData, size_t dataSize)
{
	zip_source_t* src = NULL;
	zip_t* za = NULL;
	zip_error_t error;
	int ret = 0;

	memset(pProj, 0, sizeof(*pProj));

	const uint8_t* p_file_data = pData;
	pProj->version = 1;
	if (memcmp(p_file_data, GL2_PROJ_MAGIC, sizeof(GL2_PROJ_MAGIC)) == 0)
	{
		p_file_data += 0x10;
		dataSize -= 0x10;
		pProj->version = 2;
	}
	if (memcmp(p_file_data, "PK", 2) != 0)
	{
		return -1;
	}

	// create source from buffer
	zip_error_init(&error);
	if ((src = zip_source_buffer_create(p_file_data, dataSize, 0, &error)) == NULL)
	{
		//		printf("can't create source: %s\n", zip_error_strerror(&error));
		return -2;
	}

	// open zip archive from source
	if ((za = zip_open_from_source(src, 0, &error)) == NULL) {
		//		printf("can't open zip from source: %s\n", zip_error_strerror(&error));
		zip_source_free(src);
		return -3;
	}

	// get files in zip
	struct zip_stat stat;
	zip_stat_init(&stat);
	zip_int64_t num_entries = zip_get_num_entries(za, 0);
	for (zip_int64_t entry_idx = 0; entry_idx < num_entries; entry_idx++)
	{
		if (zip_stat_index(za, entry_idx, 0, &stat) == 0)
		{
//			printf("  %s\n", stat.name);
			if (pProj->version == 1)
			{
				if (_stricmp(stat.name, "project.ini") == 0 ||
					_stricmp(stat.name, "setup.txt") == 0 ||
					_stricmp(stat.name, "trace.dat") == 0)
				{
					zip_file_t* fd_inside = zip_fopen(za, stat.name, 0);
					if (fd_inside == NULL)
						continue;
					uint8_t* p_buff = (uint8_t*)malloc((size_t)stat.size + 1);
					if (p_buff == NULL)
						continue;
					p_buff[stat.size] = '\0';
					zip_fread(fd_inside, p_buff, stat.size);

					if (_stricmp(stat.name, "project.ini") == 0)
					{
						ret = _gl_ini_open(&pProj->project_ini, (const char*)p_buff);
					}
					else if (_stricmp(stat.name, "setup.txt") == 0)
					{
						ret = _gl_ini_open(&pProj->setup_txt, (const char*)p_buff);
					}
					else if (_stricmp(stat.name, "trace.dat") == 0)
					{
						ret = _gl_trace_open(&pProj->trace_dat, p_buff, (size_t)stat.size, pProj->version);
					}
					free(p_buff);
					if (ret < 0)
						break;
				}
			}
			else if (pProj->version == 2)
			{
				if (_stricmp(stat.name, "serial_display.txt") == 0 ||
					_stricmp(stat.name, "version.txt") == 0 ||
					_stricmp(stat.name, "setup.txt") == 0 ||
					_stricmp(stat.name, "trace.dat") == 0)
				{
					zip_file_t* fd_inside = zip_fopen(za, stat.name, 0);
					if (fd_inside == NULL)
						continue;
					uint8_t* p_buff = (uint8_t*)malloc((size_t)stat.size + 1);
					if (p_buff == NULL)
						continue;
					p_buff[stat.size] = '\0';
					zip_fread(fd_inside, p_buff, stat.size);

					if (_stricmp(stat.name, "serial_display.txt") == 0)
					{
						ret = _gl_ini_open(&pProj->project_ini, (const char*)p_buff);
					}
					else if (_stricmp(stat.name, "version.txt") == 0)
					{
						ret = _gl_ini_open(&pProj->version_txt, (const char*)p_buff);
					}
					else if (_stricmp(stat.name, "setup.txt") == 0)
					{
						ret = _gl_ini_open(&pProj->setup_txt, (const char*)p_buff);
					}
					else if (_stricmp(stat.name, "trace.dat") == 0)
					{
						ret = _gl_trace_open(&pProj->trace_dat, p_buff, (size_t)stat.size, pProj->version);
					}
					free(p_buff);
					if (ret < 0)
						break;
				}
			}
		}
	}

	// can free zip related memory now that we have extracted the files we need
	if (za)
	{
		zip_close(za);
		za = NULL;
	}

	// get group/channel names from project.txt
	if (ret == 0 && pProj->setup_txt.pHead)
	{
		// iterate throught the input groups from the setup.txt file
		for (uint32_t group_idx_in = 0; group_idx_in < GL_MAX_GROUPS; group_idx_in++)
		{
			char token[100] = "";
			gl_group_info_t group = { 0 };
			
			// get info from "WaveForm Line Setup" entry
			char group_idx_str[8];
			sprintf(group_idx_str, "%02d", group_idx_in);
			const char* p_value = _gl_ini_get_value(&pProj->setup_txt, "WaveForm Line Setup", group_idx_str);
			if (p_value == NULL)
				continue;
			if (pProj->version == 1)
			{
				// version 1
				p_value = _gl_ini_get_token(p_value, ';', token);	// "TRACE"
				p_value = _gl_ini_get_token(p_value, ';', token);	// group idx
				sprintf(group_idx_str, "%02d", (int)strtol(token, 0, 0));
				p_value = _gl_ini_get_token(p_value, ';', token);	// "HEX"
				p_value = _gl_ini_get_token(p_value, ';', token);	// "0"
				p_value = _gl_ini_get_token(p_value, ';', token);	// color
				group.color = strtol(token, 0, 0);
				if (group.color > GL_COLOR_MAX)
				{
					const char* p_col_vals = _gl_ini_get_value(&pProj->setup_txt, "Project colors", token);
					group.color = 0xFF000000;
					p_col_vals = _gl_ini_get_token(p_col_vals, ';', token);	// r
					group.color |= strtol(token, 0, 0) << 16;
					p_col_vals = _gl_ini_get_token(p_col_vals, ';', token);	// g
					group.color |= strtol(token, 0, 0) << 8;
					p_col_vals = _gl_ini_get_token(p_col_vals, ';', token);	// b
					group.color |= strtol(token, 0, 0) << 0;
				}

				// get info from "Groups" entry
				p_value = _gl_ini_get_value(&pProj->setup_txt, "Groups", group_idx_str);
				if (p_value == NULL)
					continue;
				p_value = _gl_ini_get_token(p_value, ';', token);	// group_name
				strcpy(group.name, token);
				p_value = _gl_ini_get_token(p_value, ';', token);	// "+"
				p_value = _gl_ini_get_token(p_value, ';', token);	// "N"
				p_value = _gl_ini_get_token(p_value, ';', token);	// "N"
				p_value = _gl_ini_get_token(p_value, ';', token);	// "N"
				p_value = _gl_ini_get_token(p_value, ';', token);	// channels
				_parse_group_channels(token, &group.numBits, group.bitIdx);
				_gl_add_group(pProj, &group);
			}
			else
			{
				// version 2
				int is_serial = 0;
				p_value = _gl_ini_get_token(p_value, ';', token);	// 1 or 3
				p_value = _gl_ini_get_token(p_value, ';', token);	// "trc"
				p_value = _gl_ini_get_token(p_value, ';', token);	// 0
				p_value = _gl_ini_get_token(p_value, ';', token);	// 0
				p_value = _gl_ini_get_token(p_value, ';', token);	// group idx (or 'S' for serial?)
				is_serial = strcmp(token, "S") == 0;
				if( !is_serial )
					sprintf(group_idx_str, "%02d", (int)strtol(token, 0, 0));
				p_value = _gl_ini_get_token(p_value, ';', token);	// 0
				p_value = _gl_ini_get_token(p_value, ';', token);	// 0
				p_value = _gl_ini_get_token(p_value, ';', token);	// color
				group.color = strtol(token, 0, 0);
				p_value = _gl_ini_get_token(p_value, ';', token);	// "none"
				p_value = _gl_ini_get_token(p_value, ';', token);	// 0.0000
				p_value = _gl_ini_get_token(p_value, ',', token);	// serial index
				if (is_serial)
					sprintf(group_idx_str, "%d", (int)strtol(token, 0, 0));
				if (group.color > GL_COLOR_MAX)
				{
					const char* p_col_vals = _gl_ini_get_value(&pProj->setup_txt, "Project colors", token);
					group.color = 0xFF000000;
					p_col_vals = _gl_ini_get_token(p_col_vals, ';', token);	// r
					group.color |= strtol(token, 0, 0) << 16;
					p_col_vals = _gl_ini_get_token(p_col_vals, ';', token);	// g
					group.color |= strtol(token, 0, 0) << 8;
					p_col_vals = _gl_ini_get_token(p_col_vals, ';', token);	// b
					group.color |= strtol(token, 0, 0) << 0;
				}

				if (!is_serial)
				{
					// get info from "Groups" entry
					p_value = _gl_ini_get_value(&pProj->setup_txt, "Groups", group_idx_str);
					if (p_value == NULL)
						continue;
					p_value = _gl_ini_get_token(p_value, ';', token);	// group_name
					strcpy(group.name, token);
					p_value = _gl_ini_get_token(p_value, ';', token);	// color
					p_value = _gl_ini_get_token(p_value, ';', token);	// "+"
					p_value = _gl_ini_get_token(p_value, ';', token);	// "N"
					//p_value = _gl_ini_get_token(p_value, ';', token);	// "N"
					//p_value = _gl_ini_get_token(p_value, ';', token);	// "N"
					p_value = _gl_ini_get_token(p_value, ';', token);	// channels
					_parse_group_channels(token, &group.numBits, group.bitIdx);
					_gl_add_group(pProj, &group);
				}
				else
				{
					// get info from "Serial Bus Setup" entry
					p_value = _gl_ini_get_value(&pProj->setup_txt, "Serial Bus Setup", group_idx_str);
					if (p_value == NULL)
						continue;
					p_value = _gl_ini_get_token(p_value, ';', token);	// 0
					p_value = _gl_ini_get_token(p_value, ';', token);	// 1
					p_value = _gl_ini_get_token(p_value, ':', token);	// group_name
					char chan_major_name[GL_MAX_NAME];
					strcpy(chan_major_name, token);
					// p_value now points to a comma separated list of A=B.
					// eg "A=B,C=D,E=F"
					while (p_value && strlen(p_value) > 0)
					{
						// get A=B as token
						// then check if B is a valid channel
						p_value = _gl_ini_get_token(p_value, ',', token);
						char chan_minor_name[GL_MAX_NAME];
						const char* p_chans = _gl_ini_get_token(token, '=', chan_minor_name);
						if (_parse_group_channels(p_chans, &group.numBits, group.bitIdx) >= 0)
						{
							sprintf(group.name, "%s_%s", chan_major_name, chan_minor_name);
							_gl_add_group(pProj, &group);
						}
					}
				}
			}
		}
	}

	// get sample rate from "setup.txt"
	char token[100] = "";
	const char* p_value = NULL;
	const char* p_sample_rate = NULL;
	const char* p_sample_units = NULL;
	p_value = _gl_ini_get_value(&pProj->setup_txt, "Setup", "set_sampling");
	if ((p_value = _gl_ini_get_value(&pProj->setup_txt, "Setup", "set_sampling")) != NULL)
	{
		// gl1
		p_sample_rate = _gl_ini_get_token(p_value, ';', token);
		p_sample_units = _gl_ini_get_token(p_sample_rate, ' ', token);
	}
	else if ((p_value = _gl_ini_get_value(&pProj->setup_txt, "setup", "sampling")) != NULL)
	{
		// gl2
		p_sample_rate = _gl_ini_get_token(p_value, ';', token);
		p_sample_rate = _gl_ini_get_token(p_sample_rate, ';', token);
		p_sample_units = p_sample_rate;
		while (*p_sample_units >= '0' && *p_sample_units <= '9')
			p_sample_units++;
	}
	else
	{
		return -1;
	}
	pProj->sampleRate = _strtoi64(p_sample_rate, 0, 10);
	/*const char* p_unk20 =*/ _gl_ini_get_token(p_sample_units, ';', token);
	if (strcmp(token, "Hz") == 0)
		pProj->sampleRate *= 1ULL;
	else if (strcmp(token, "KHz") == 0)
		pProj->sampleRate *= 1000ULL;
	else if (strcmp(token, "MHz") == 0)
		pProj->sampleRate *= 1000000ULL;
	else if (strcmp(token, "GHz") == 0)
		pProj->sampleRate *= 1000000000ULL;
	// calc sample unit length in ps
	pProj->samplePeriod = (1000ULL*1000ULL *1000ULL*1000ULL) / pProj->sampleRate;

	// success
	return ret;
}

int gl_project_open_file(gl_project_t* pProj, const char* filename)
{
	memset(pProj, 0, sizeof(*pProj));

	// buffer file in memory
	FILE* fd = fopen(filename, "rb");
	if (fd == NULL)
	{
//		printf("Error opening %s\n", filename);
		return -1;
	}
	fseek(fd, 0, SEEK_END);
	size_t file_size = ftell(fd);
	if (file_size < 0x20)
	{
		fclose(fd);
		return -2;
	}
	fseek(fd, 0, SEEK_SET);
	uint8_t* p_file_buff = (uint8_t*)malloc(file_size);
	if (p_file_buff == NULL)
	{
		fclose(fd);
		return -3;
	}
	fread(p_file_buff, 1, file_size, fd);
	fclose(fd);
	fd = NULL;

	// process file from memory, then free the mem buffer
	int ret = gl_project_open_buffer(pProj, p_file_buff, file_size);
	free(p_file_buff);
	return ret;
}

void gl_project_close(gl_project_t* pProj)
{
	_gl_ini_close(&pProj->project_ini);
	_gl_ini_close(&pProj->setup_txt);
	_gl_ini_close(&pProj->serial_display_txt);
	_gl_ini_close(&pProj->version_txt);
	_gl_trace_close(&pProj->trace_dat);
}



// Get sample rate in Hz
int  gl_project_sample_rate(gl_project_t* pProj, uint64_t* pSampleRate)
{
	*pSampleRate = pProj->sampleRate;
	return 0;
}

// get sample period in picoseconds
int  gl_project_sample_period(gl_project_t* pProj, uint64_t* pSamplePeriod)
{
	*pSamplePeriod = pProj->samplePeriod;
	return 0;
}

// Get the number of groups.
// Groups can be one or more channels.
int  gl_group_cnt(gl_project_t* pProj)
{
	return pProj->numGroups;

}
int  gl_group_info(gl_project_t* pProj, uint32_t idx, gl_group_info_t* pInfo)
{
	if (idx >= pProj->numGroups)
		return -1;
	*pInfo = pProj->groups[idx];
//	strcpy(pInfo->name, pProj->groups[idx].name);
	return 0;
}

// Gets the number of single channels.
// This ignores groups with more than one channel.
int  gl_channel_cnt(gl_project_t* pProj)
{
	return pProj->numChannels;
}
int  gl_channel_info(gl_project_t* pProj, uint32_t idx, gl_channel_info_t* pInfo)
{
	if (idx >= pProj->numChannels)
		return -1;
	gl_group_info_t gi;
	int ret = gl_group_info(pProj, pProj->channelToGroupIdx[idx], &gi);
	if (ret < 0)
		return ret;
	if (gi.numBits != 1)
		return -2;
	pInfo->color = gi.color;
	pInfo->bitIdx = gi.bitIdx[0];
	strcpy(pInfo->name, gi.name);
	return ret;
}


static inline uint64_t gl2_time_as_u64_ps(uint64_t t)
{
	double dt = *(double*)((char*)&t);
	return (uint64_t)dt * 1000ULL;
}

// Gets the number of samples.
// Each sample can be for multiple groups/channels.
int  gl_sample_cnt(gl_project_t* pProj, uint64_t* pSampleCnt)
{
	*pSampleCnt = pProj->trace_dat.numSamples;
	return 0;
}

#define DO_GOLOGIC_SW_BUG	1

static int  _gl_sample_info_trans(gl_project_t* pProj, uint64_t idx, uint64_t cnt, gl_sample_info_t* pInfo)
{
	// watch out for overflow
	if (idx + cnt < idx)
		return -1;
	if (idx >= pProj->trace_dat.numSamples)
		return -1;
	if (idx + cnt > pProj->trace_dat.numSamples)
		return -1;
	uint64_t start_time = 0;
	if (pProj->version == 1 && pProj->trace_dat.pTime[0] > 0)
	{
		start_time = pProj->trace_dat.pTime[0] - 1;
	}
	if (pProj->version == 2 && gl2_time_as_u64_ps(pProj->trace_dat.pTime[0]) > 0)
	{
		start_time = gl2_time_as_u64_ps(pProj->trace_dat.pTime[0]) - pProj->samplePeriod;
	}
	while (cnt--)
	{
		// Note: gologic software appears to have a bug where it always inserts
		// a sample of 1 time unit at the start of each trace.
		// (At least I assume it is a bug).
		// This code replicates the bug so that traces will
		// match between GoLogic and PulseView.
#ifdef DO_GOLOGIC_SW_BUG
		// Code that does replicate the gologic bug.
		if (pProj->trace_dat.pVal64)
			pInfo->val = pProj->trace_dat.pVal64[idx];
		else if (pProj->trace_dat.pVal32)
			pInfo->val = pProj->trace_dat.pVal32[idx];
		else if (pProj->trace_dat.pVal16)
			pInfo->val = pProj->trace_dat.pVal16[idx];
		else if (pProj->trace_dat.pVal8)
			pInfo->val = pProj->trace_dat.pVal8[idx];
		else
			pInfo->val = 0;

		if (pProj->trace_dat.pValClk16)
			pInfo->clkval = ((pProj->trace_dat.pValClk16[idx] & 0x000F) << 0) | ((pProj->trace_dat.pValClk16[idx] & 0x0F00) >> 4);
		else if (pProj->trace_dat.pValClk8)
			pInfo->clkval = pProj->trace_dat.pValClk8[idx];
		else
			pInfo->clkval = 0;
#else
		// Code that does not replicate the gologic bug.
		if (pProj->trace_dat.pVal32)
			pInfo->val = pProj->trace_dat.pVal32[idx + 1];
		else
			pInfo->val = pProj->trace_dat.pVal64[idx + 1];
		if (pProj->trace_dat.pValClk8)
			pInfo->clkval = pProj->trace_dat.pValClk8[idx + 1];
		else
			pInfo->clkval = ((pProj->trace_dat.pValClk16[idx +1 ] & 0x000F) << 4) | ((pProj->trace_dat.pValClk16[idx + 1] & 0x0F00) >> 8);
		pInfo->time = (pProj->trace_dat.pTime[idx] - pProj->trace_dat.pTime[0]) * pProj->samplePeriod;
#endif // DO_GOLOGIC_SW_BUG		

		if (pProj->version == 1)
		{
			if (idx == 0)
				pInfo->time = 0 * pProj->samplePeriod;
			else
				pInfo->time = (pProj->trace_dat.pTime[idx - 1] - start_time) * pProj->samplePeriod;
		}
		else if (pProj->version == 2)
		{
			if (idx == 0)
				pInfo->time = 0;
			else
			{
//				double dtime = (*(double*)&pProj->trace_dat.pTime[idx - 1]) * (double)1000;
//				uint64_t curr_time = (gl2_time_as_u64(pProj->trace_dat.pTime[idx - 1]) * 1000) - pProj->samplePeriod;
				
//				pInfo->time = gl2_time_as_u64_ps(pProj->trace_dat.pTime[idx - 1]) - start_time;

				pInfo->time = gl2_time_as_u64_ps(pProj->trace_dat.pTime[idx - 1]);// -start_time;
			}
		}
		else
		{
			pInfo->time = 0;
		}
		idx++;
		pInfo++;
	}
	return 0;
}

static int  _gl_sample_info_normal(gl_project_t* pProj, uint64_t idx, uint64_t cnt, gl_sample_info_t* pInfo)
{
	// watch out for overflow
	if (idx + cnt < idx)
		return -1;
	if (idx >= pProj->trace_dat.numSamples)
		return -1;
	if (idx + cnt > pProj->trace_dat.numSamples)
		return -1;
	while (cnt--)
	{
		if (pProj->trace_dat.pVal64)
			pInfo->val = pProj->trace_dat.pVal64[idx];
		else if (pProj->trace_dat.pVal32)
			pInfo->val = pProj->trace_dat.pVal32[idx];
		else if (pProj->trace_dat.pVal16)
			pInfo->val = pProj->trace_dat.pVal16[idx];
		else if (pProj->trace_dat.pVal8)
			pInfo->val = pProj->trace_dat.pVal8[idx];
		else
			pInfo->val = 0;

		if (pProj->trace_dat.pValClk16)
			pInfo->clkval = ((pProj->trace_dat.pValClk16[idx] & 0x000F) << 0) | ((pProj->trace_dat.pValClk16[idx] & 0x0F00) >> 4);
		else if (pProj->trace_dat.pValClk8)
			pInfo->clkval = pProj->trace_dat.pValClk8[idx];
		else
			pInfo->clkval = 0;
		
		pInfo->time = idx * pProj->samplePeriod;

		idx++;
		pInfo++;
	}
	return 0;
}

int  gl_sample_info(gl_project_t* pProj, uint64_t idx, uint64_t cnt, gl_sample_info_t* pInfo)
{
	if (pProj->trace_dat.pTime == NULL)
		return _gl_sample_info_normal(pProj, idx, cnt, pInfo);
	else
		return _gl_sample_info_trans(pProj, idx, cnt, pInfo);
}
