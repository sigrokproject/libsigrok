/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2011 Bert Vermeulen <bert@biot.com>
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

#ifndef TEXT_H_
#define TEXT_H_

#define DEFAULT_BPL_BITS 64
#define DEFAULT_BPL_HEX  192
#define DEFAULT_BPL_ASCII 74

enum outputmode {
	MODE_BITS = 1,
	MODE_HEX,
	MODE_ASCII,
};

struct context {
	unsigned int num_enabled_probes;
	int samples_per_line;
	unsigned int unitsize;
	int line_offset;
	int linebuf_len;
	char *probelist[65];
	char *linebuf;
	int spl_cnt;
	uint8_t *linevalues;
	char *header;
	int mark_trigger;
	uint64_t prevsample;
	enum outputmode mode;
};

void flush_linebufs(struct context *ctx, char *outbuf);
int init(struct sr_output *o, int default_spl, enum outputmode mode);
int event(struct sr_output *o, int event_type, char **data_out,
		 uint64_t *length_out);


int init_bits(struct sr_output *o);
int data_bits(struct sr_output *o, const char *data_in, uint64_t length_in,
		     char **data_out, uint64_t *length_out);

int init_hex(struct sr_output *o);
int data_hex(struct sr_output *o, const char *data_in, uint64_t length_in,
		     char **data_out, uint64_t *length_out);

int init_ascii(struct sr_output *o);
int data_ascii(struct sr_output *o, const char *data_in, uint64_t length_in,
		     char **data_out, uint64_t *length_out);

#endif
