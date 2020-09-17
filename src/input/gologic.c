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

/*
 * This is to support importing NCI GoLogic and GoLogicXL project files.
 * 
 * The vendor's website is at: https://www.nci-usa.com/mainsite/
 * 
 * GoLogic is the older Logic Analyser and software.
 * GoLogicXL is the newer Logic Analyser and software.
 * These two use different but similar project files,
 * so this module will support importing from either of them.
 * 
 * The GoLogic project file has the extension ".prj".
 * These prj files are just zip files with a different extension.
 * 
 * The GoLogicXL project file has the extension ".xlp".
 * These xlp files are just zip files with a 16 byte header and a different extension.
 *
 * Both project files are zip files.
 * This will wait till the full zip file has been passed in via receive()
 * before it does any processing. This is because zip files are hard
 * to process until you have the full file.
 * 
 * LIMITATIONS
 * - Sigrok code only supports channels but gologic uses "groups" which can
 *   have 0, 1, or multiple channels. When importing, any group with 0 or multiple
 *   channels will be ignored. So only groups with exactly 1 channel will get imported.
 * - Gologic supports up to 72 channels but sigrok only supports 64 max. So if all
     channels are used in a project, sigrok will not be able to handle more than 64 of them.
 */

#include <config.h>
#include <ctype.h>
#include <glib.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zip.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "gologic_proj.h"

#define LOG_PREFIX	"input/gologic"

// Context for an instance of input file importing.
struct context {
	int projIsOpen;		// 1 if project is currently open, 0 otherwise.
	uint32_t unitsize;	// number of bytes per "sample". 1, 2, 4 or 8 depending on the number of channels.
	GSList *prev_sr_channels;	// saved copy of channels to get around the "reset() bug" #1215
	gl_project_t proj;	// info for the currently open project file.
};


// Returns a NULL-terminated list of options this module can take.
// Can be NULL, if the module has no options.
static struct sr_option options[] = {
	ALL_ZERO,
};
static const struct sr_option *get_options(void)
{
	return options;
}

#define GL1_PROJ_MAGIC	"PK"
#define GL2_PROJ_MAGIC	"GoLogicXL XLP"

// Check if this input module can load and parse the specified stream.
static int format_match(GHashTable *metadata, unsigned int *confidence)
{
	static const char *gl1_ext = ".prj";
	static const char *gl2_ext = ".xlp";

	gboolean matched;
	const char *fn;
	size_t fn_len, ext_len;
	const char *ext_pos;
	GString *buf;

	matched = FALSE;

	// get file size
	uint64_t filesize = GPOINTER_TO_SIZE(g_hash_table_lookup(metadata,
			GINT_TO_POINTER(SR_INPUT_META_FILESIZE)));
	sr_info( "format_match() filesize is 0x%llX", filesize);
	
	/* Weak match on the filename (when available). */
	fn = g_hash_table_lookup(metadata, GINT_TO_POINTER(SR_INPUT_META_FILENAME));
	if (fn && *fn) {
		fn_len = strlen(fn);
		ext_len = strlen(gl1_ext);
		ext_pos = &fn[fn_len - ext_len];
		if (fn_len >= ext_len && g_ascii_strcasecmp(ext_pos, gl1_ext) == 0) {
			*confidence = 50;
			matched = TRUE;
		}
		ext_len = strlen(gl2_ext);
		ext_pos = &fn[fn_len - ext_len];
		if (fn_len >= ext_len && g_ascii_strcasecmp(ext_pos, gl2_ext) == 0) {
			*confidence = 50;
			matched = TRUE;
		}
	}

	/* Stronger match when magic literals are found in file content. */
	buf = g_hash_table_lookup(metadata, GINT_TO_POINTER(SR_INPUT_META_HEADER));
	if (!buf || !buf->len || !buf->str)
		return SR_ERR_ARG;
	if (buf->len >=strlen(GL1_PROJ_MAGIC) &&
		memcmp(buf->str, GL1_PROJ_MAGIC, strlen(GL1_PROJ_MAGIC)) == 0)
	{
		*confidence = 1;
		matched = TRUE;
	}
	if (buf->len >=strlen(GL2_PROJ_MAGIC) &&
		memcmp(buf->str, GL2_PROJ_MAGIC, strlen(GL2_PROJ_MAGIC)) == 0)
	{
		*confidence = 1;
		matched = TRUE;
	}
	
	return matched ? SR_OK : SR_ERR_DATA;
}

// Initialize the input module.
static int init(struct sr_input *in, GHashTable *options)
{
	struct context *inc;
	
	(void)options;
	
	/* Allocate resources. */
	in->sdi = g_malloc0(sizeof(*in->sdi));
	inc = g_malloc0(sizeof(*inc));
	in->priv = inc;
	
	// 64 bits per "sample unit" (allows for up to 64 channels)
	// this is the max supported by sigrok at this time (even tho gologic supports 72 channels)
	inc->unitsize = 64 / 8;
	
	return SR_OK;
}

// Send data to the specified input instance.
// 
// Our project files are zip files and so need the entire file to be
// read in before it can be opened.
// So if the project filesize is bigger than the size of the chunks
// sent in to this then it will fail to open.
// This requires the fix submitted to inputfile.cpp to support
// project files > 4MB.
static int receive(struct sr_input *in, GString *buf)
{
	struct context *inc;
	int rc, gl_ret;
	struct sr_channel *ch;

	inc = in->priv;
	
	// Save file data into buffer until all data has been received,
	// then process it all at once.
	g_string_append_len(in->buf, buf->str, buf->len);
	sr_info( "receive() got 0x%llX bytes.  Total bytes is 0x%llX\n", buf->len, in->buf->len);
	
	// try to open file.
	// if successful then the full file has been received.
	// receive() will not be called again, end() will be called after this.
	// 
	// it is only once we get this far that we can unpack the project file
	// in order to get channel and sample info.
	gl_ret = gl_project_open_buffer(&inc->proj, (uint8_t*)in->buf->str, in->buf->len);
	if(gl_ret < 0) {
		sr_info( "receive() : error opening project %d\n", gl_ret);
		return SR_OK;
	}
	g_string_erase(in->buf, 0, in->buf->len);
	inc->projIsOpen = 1;
	sr_info( "opened project ok");
	
	// add channels
	uint32_t num_channels = gl_channel_cnt(&inc->proj);
	sr_info( "project has %d channels", num_channels);
	for(uint32_t idx=0; idx<num_channels; idx++)
	{
		sr_info( "processing channel %d", idx);
		gl_channel_info_t ci;
		if( gl_channel_info(&inc->proj, idx, &ci) >= 0 )
		{
			const gboolean channel_enabled = TRUE;
			sr_info("receive() : adding channel %s", ci.name);
			ch = sr_channel_new(in->sdi, idx, SR_CHANNEL_LOGIC, channel_enabled, ci.name);
			if (!ch)
				return SR_ERR_MALLOC;
			sr_info("SUCCESS");
		}
	}
	sr_info("ADDED ALL CHANNELS");
	
	// choose unitsize based on the current number of channels in use
	if(num_channels <= 8)
		inc->unitsize =  8 / 8;
	else if(num_channels <= 16)
		inc->unitsize = 16 / 8;
	else if(num_channels <= 32)
		inc->unitsize = 32 / 8;
	else if(num_channels <= 64)
		inc->unitsize = 64 / 8;
	else
		inc->unitsize = 64 / 8;
	
	if (inc->prev_sr_channels) {
		if (sr_channel_lists_differ(inc->prev_sr_channels, in->sdi->channels)) {
			sr_err("Channel list change not supported for file re-read.");
			return SR_ERR;
		}

		g_slist_free_full(in->sdi->channels, sr_channel_free_cb);
		in->sdi->channels = inc->prev_sr_channels;
		inc->prev_sr_channels = NULL;
	}
	
	// done initial setup
	in->sdi_ready = TRUE;
	
	// success
	rc = SR_OK;
	return rc;
}

// Signal the input module no more data will come.
static int end(struct sr_input *in)
{
	struct context *inc;
	int rc, gl_ret;
	
	inc = in->priv;
	
	sr_info( "end() for filesize 0x%llX\n", in->buf->len);
	
	// send datafeed header
	rc = std_session_send_df_header(in->sdi);
	if (rc)
		return rc;
	
	// set sample rate
	uint64_t sample_rate = 0;
	if( gl_project_sample_rate(&inc->proj, &sample_rate) < 0 )
	{
		rc = SR_ERR;
		return rc;
	}
	sr_info("end() : setting sample rate %lld", sample_rate);
	rc = sr_session_send_meta(in->sdi, SR_CONF_SAMPLERATE, g_variant_new_uint64(sample_rate));
	
	// get all channel bit indexes here one time, to save getting them for every single sample
	uint32_t gl_num_channels = gl_channel_cnt(&inc->proj);
	uint8_t channel_bit_idx[GL_MAX_CHANNELS] = {0};
	for(uint32_t ch_idx=0; ch_idx<gl_num_channels; ch_idx++)
	{
		gl_channel_info_t gl_ch;
		gl_channel_info(&inc->proj, ch_idx, &gl_ch);
		channel_bit_idx[ch_idx] = gl_ch.bitIdx;
	}
	// send all samples in one packet
	struct sr_datafeed_logic logic;
	struct sr_datafeed_packet packet;
	packet.type = SR_DF_LOGIC;
	packet.payload = &logic;
	const uint32_t SAMPLE_BUFF_CNT = 1024*1024;
	uint8_t* p_sample_buff8 = malloc(SAMPLE_BUFF_CNT * inc->unitsize);
	uint16_t* p_sample_buff16 = (uint16_t*)p_sample_buff8;
	uint32_t* p_sample_buff32 = (uint32_t*)p_sample_buff8;
	uint64_t* p_sample_buff64 = (uint64_t*)p_sample_buff8;
	logic.unitsize = inc->unitsize;
	logic.data = p_sample_buff8;
	uint64_t gl_num_samples = 0;
	gl_sample_cnt(&inc->proj, &gl_num_samples);
	uint64_t gl_sample_period = 0;
	gl_project_sample_period(&inc->proj, &gl_sample_period);
	uint32_t sample_buff_idx = 0;
	uint64_t total_num_samples = 0;
	for(uint64_t gl_idx=0; gl_idx<gl_num_samples-1; gl_idx++)
	{
		// get 2 samples for start and end (in transitional timing mode)
		gl_sample_info_t si[2] = { 0 };
		if(gl_idx < gl_num_samples-1)
		{
			// every pair of samples except for the last sample
			gl_ret = gl_sample_info(&inc->proj, gl_idx, 2, si);
			if(gl_ret < 0)
				return SR_ERR;
		}
		
		// handle multiples of sample period
		uint64_t start_time = si[0].time;
		uint64_t end_time = si[1].time;
		
		//if(gl_idx == gl_num_samples-1)
		//	sr_info("end() : last 2 times values are: %lld  %lld", start_time, end_time);
		
		for (uint64_t t = start_time; t < end_time; t += gl_sample_period)
		{
			// remap bits orginally indexed by group to be indexed by channel position in PV
			uint64_t val = 0;
			for(uint32_t ch_idx=0; ch_idx<gl_num_channels; ch_idx++)
			{
				if( channel_bit_idx[ch_idx] >= 0x40 )
					val |= ((si[0].clkval >> (channel_bit_idx[ch_idx]-0x40)) & 1) << ch_idx;
				else
					val |= ((si[0].val >> channel_bit_idx[ch_idx]) & 1) << ch_idx;
			}
			if(inc->unitsize == 8/8)
				p_sample_buff8[sample_buff_idx]  = (uint8_t)val;
			else if(inc->unitsize == 16/8)
				p_sample_buff16[sample_buff_idx] = (uint16_t)val;
			else if(inc->unitsize == 32/8)
				p_sample_buff32[sample_buff_idx] = (uint32_t)val;
			else if(inc->unitsize == 64/8)
				p_sample_buff64[sample_buff_idx] = (uint64_t)val;
			else
				p_sample_buff64[sample_buff_idx] = (uint64_t)val;
			sample_buff_idx++;
			total_num_samples++;
			if(sample_buff_idx>=SAMPLE_BUFF_CNT)
			{
				logic.length = inc->unitsize * sample_buff_idx;
				rc = sr_session_send(in->sdi, &packet);
				if(rc < 0) {
					free(p_sample_buff8);
					return rc;
				}
				sample_buff_idx = 0;
			}
		}
	}
	// handle any left over samples in sample buffer that didnt fill the full sample buffer
	if(sample_buff_idx>0)
	{
		logic.length = inc->unitsize * sample_buff_idx;
		rc = sr_session_send(in->sdi, &packet);
		if(rc < 0) {
			free(p_sample_buff8);
			return rc;
		}
		sample_buff_idx = 0;
	}
	free(p_sample_buff8);
	
	// send datafeed footer
	rc = std_session_send_df_end(in->sdi);
	
	sr_info( "end() added %lld samples\n", total_num_samples);
	
	// success
	rc = SR_OK;
	return rc;
}

// This function is called after the caller is finished using
// the input module, and can be used to free any internal
// resources the module may keep.
static void cleanup(struct sr_input *in)
{
	struct context *inc;

	sr_info( "cleanup()");
	
	if (!in)
		return;

	inc = in->priv;
	if (!inc)
		return;

	if( inc->projIsOpen )
	{
		gl_project_close(&inc->proj);
		inc->projIsOpen = 0;
	}
	
	if(inc->prev_sr_channels) {
		g_slist_free_full(inc->prev_sr_channels, sr_channel_free_cb);
		inc->prev_sr_channels = NULL;
	}
	
	// Release potentially allocated resources. Void all references
	// and scalars, so that re-runs start out fresh again.
	memset(inc, 0, sizeof(*inc));
}

// Reset the input module's input handling structures.
static int reset(struct sr_input *in)
{
	struct context *inc;
	GSList *channels;

	sr_info( "reset()");
	inc = in->priv;
	g_string_truncate(in->buf, 0);
	
	/*
	 * The input module's .reset() routine clears the 'inc' context,
	 * but 'in' is kept which contains channel groups which reference
	 * channels. Since we cannot re-create the channels (applications
	 * don't expect us to, see bug #1215), make sure to keep the
	 * channels across the reset operation.
	 */
	 // TODO: should this reset inc->proj ?
	 // Or should we leave the proj open across calls to reset()?
	channels = in->sdi->channels;
	in->sdi->channels = NULL;
	cleanup(in);
	inc->prev_sr_channels = channels;

	return SR_OK;
}


SR_PRIV struct sr_input_module input_gologic = {
	.id = "gologic",
	.name = "GoLogic File",
	.desc = "NCI GoLogic project",
	.exts = (const char *[]){ "prj", "xlp", NULL },
	.metadata = { SR_INPUT_META_FILENAME | SR_INPUT_META_HEADER | SR_INPUT_META_FILESIZE | SR_INPUT_META_REQUIRED },
	.options = get_options,
	.format_match = format_match,
	.init = init,
	.receive = receive,
	.end = end,
	.cleanup = cleanup,
	.reset = reset,
};
