/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2022 Filip Kosecek <kosecek1@uniba.sk>
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
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "input/isf"

#define CHUNK_SIZE	(4 * 1024 * 1024)

/* Number of bytes required to process header */
#define MIN_INITIAL_OFFSET 512

#define HEADER_ITEMS_PARAMETERS 10
#define MAX_CHANNEL_NAME_SIZE 32

enum byteorder{
    LSB = 0,
    MSB = 1,
};

enum format{
    RI = 0,
    FP = 1,
};

enum waveform_type{
    ANALOG = 0,
    RF_FD = 1,
};

union floating_point{
    float f;
    uint32_t i;
};

struct context{
    gboolean started;
    gboolean found_header;
    gboolean create_channel;
    float yoff;
    float yzero;
    float ymult;
    float xincr;
    int bytnr;
    int byte_order;
    int bn_fmt;
    int wfmtype;
    char channel_name[MAX_CHANNEL_NAME_SIZE];
};

enum header_items_enum{
    YOFF = 0,
    YZERO = 1,
    YMULT = 2,
    XINCR = 3,
    BYTNR = 4,
    BYTE_ORDER = 5,
    BN_FMT = 6,
    WFID = 7,
    WFMTYPE = 8,
    ENCODING = 9,
};

static const char *header_items[] = {
    [YOFF] = "YOFF ",
    [YZERO] = "YZERO ",
    [YMULT] = "YMULT ",
    [XINCR] = "XINCR ",
    [BYTNR] = "BYT_NR ",
    [BYTE_ORDER] = "BYT_OR ",
    [BN_FMT] = "BN_FMT ",
    [WFID] = "WFID ",
    [WFMTYPE] = "WFMTYPE ",
    [ENCODING] = "ENCDG ",
};


static char *find_item(GString *buf, const char *item)
{
    return g_strstr_len(buf->str, buf->len, item);
}

static gboolean has_header(GString *buf)
{
    return buf->len >= MIN_INITIAL_OFFSET;
}

static char *find_data_section(GString *buf)
{
    const char *curve = "CURVE #";

    char *data_ptr;
    size_t offset, data_length;

    data_ptr = g_strstr_len(buf->str, buf->len, curve);
    if(data_ptr == NULL)
        return NULL;

    data_ptr += strlen(curve);
    offset = data_ptr - buf->str;
    if(offset >= buf->len)
        return NULL;

    data_length = (size_t) *data_ptr - 48;
    data_ptr += 1 + data_length;
    offset = data_ptr - buf->str;

    if(offset >= buf->len)
        return NULL;

    return data_ptr;
}

static void extract_channel_name(struct context *inc, const char *beg)
{
    size_t i, channel_ix;

    channel_ix = 0;
    /* Isf wfid looks something like WFID "Ch1, ..."; thus we must skip character '"' */
    i = 1;
    while(beg[i] != ',' && beg[i] != '"' && channel_ix < MAX_CHANNEL_NAME_SIZE - 1)
        inc->channel_name[channel_ix++] = beg[i++];
}

static void find_string_value(const char *beg, char *value, size_t value_len)
{
    size_t i, value_idx;

    value_idx = 0;
    i = 0;
    while(beg[i] != ';' && value_idx < value_len)
        value[value_idx++] = beg[i++];
    value[value_idx] = 0;
}

static int find_encoding(const char *beg)
{
    char value[10];

    find_string_value(beg, value, 9);

    if(strcmp(value, "BINARY") != 0){
        sr_err("Only binary encoding supported.");
        return SR_ERR_NA;
    }

    return SR_OK;
}

static int find_waveform_type(struct context *inc, const char *beg)
{
    char value[10];

    find_string_value(beg, value, 9);

    if(strcmp(value, "ANALOG") == 0)
        inc->wfmtype = ANALOG;
    else if(strcmp(value, "RF_FD") == 0)
        inc->wfmtype = RF_FD;
    else
        return SR_ERR_DATA;

    return SR_OK;
}

static int process_header_item(const char *beg, struct context *inc, enum header_items_enum item)
{
    int ret;

    switch (item) {
        case YOFF:
            inc->yoff = (float) g_ascii_strtod(beg, NULL);
            break;
        case YZERO:
            inc->yzero = (float) g_ascii_strtod(beg, NULL);
            break;
        case YMULT:
            inc->ymult = (float) g_ascii_strtod(beg, NULL);
            break;
        case XINCR:
            inc->xincr = (float) g_ascii_strtod(beg, NULL);
            break;
        case BYTNR:
            inc->bytnr = (int) g_ascii_strtoll(beg, NULL, 10);
            break;
        case BYTE_ORDER:
            if(strncmp(beg, "LSB", 3) == 0)
                inc->byte_order = LSB;
            else if(strncmp(beg, "MSB", 3) == 0)
                inc->byte_order = MSB;
            else
                return SR_ERR_DATA;
            
            break;
        case BN_FMT:
            if(strncmp(beg, "RI", 2) == 0)
                inc->bn_fmt = RI;
            else if(strncmp(beg, "FP", 2) == 0)
                inc->bn_fmt = FP;
            else
                return SR_ERR_DATA;
            break;
        case WFID:
            extract_channel_name(inc, beg);
            break;
        case WFMTYPE:
            ret = find_waveform_type(inc, beg);
            if(ret != SR_OK)
                return ret;
            break;
        case ENCODING:
            ret = find_encoding(beg);
            if(ret != SR_OK)
                return ret;
            break;
        default:
            return SR_ERR_ARG;
    }
    
    return SR_OK;
}
static int parse_isf_header(GString *buf, struct context *inc)
{
    char *pattern, *data_section;
    int ret, i;

    if(inc == NULL)
        return SR_ERR_ARG;
    
    for(i = 0; i < HEADER_ITEMS_PARAMETERS; ++i){
        pattern = find_item(buf, header_items[i]);
        if(pattern == NULL)
            return SR_ERR_DATA;
        
        ret = process_header_item(pattern + strlen(header_items[i]), inc, i);
        if(ret != SR_OK)
            return ret;
    }

    data_section = find_data_section(buf);
    if(data_section == NULL)
        return SR_ERR_DATA;

    return SR_OK;
}

static int format_match(GHashTable *metadata, unsigned int *confidence)
{
    const char *default_extension = ".isf";
    const char *nr_pt = "NR_PT";

    GString *buf;
    char *fn;
    size_t fn_len;

    fn = g_hash_table_lookup(metadata, GINT_TO_POINTER(SR_INPUT_META_FILENAME));
    if(fn != NULL && (fn_len = strlen(fn)) >= strlen(default_extension)){
        if(strcmp(fn + fn_len - strlen(default_extension), default_extension) == 0){
            *confidence = 10;
            return SR_OK;
        }
    }

    buf = g_hash_table_lookup(metadata, GINT_TO_POINTER(SR_INPUT_META_HEADER));
    if(buf == NULL || g_strstr_len(buf->str, buf->len, nr_pt) == NULL)
        return SR_ERR;

    *confidence = 10;
    return SR_OK;
}

static int init(struct sr_input *in, GHashTable *options)
{
    struct context *inc;

    (void) options;

    in->sdi = g_malloc0(sizeof(struct sr_dev_inst));
    in->priv = g_malloc0(sizeof(struct context));

    inc = in->priv;
    inc->found_header = FALSE;
    inc->create_channel = TRUE;
    memset(inc->channel_name, 0, MAX_CHANNEL_NAME_SIZE);

    return SR_OK;
}

static int64_t read_int_value(struct sr_input *in, size_t offset)
{
    struct context *inc;
    int64_t value;
    int bytnr, i;
    uint8_t data[8];
    
    inc = in->priv;
    bytnr = inc->bytnr;
    memcpy(data, in->buf->str + offset, inc->bytnr);
    value = 0;
    if(inc->byte_order == MSB){
        for(i = 0; i < bytnr; ++i){
            value = value << 8;
            value |= data[i];
        }
    }else{
        for(i = bytnr - 1; i >= 0; --i){
            value = value << 8;
            value |= data[i];
        }
    }
    
    if(value & (1 << (8*inc->bytnr-1))){
        i = G_MAXINT64 - (1 << inc->bytnr*8) + 1;
        value |= i;
    }

    return value;
}

static float read_float_value(struct sr_input *in, size_t offset)
{
    struct context *inc;
    union floating_point fp;
    int bytnr, i;
    uint8_t data[4];

    inc = in->priv;
    bytnr = inc->bytnr;
    g_assert(bytnr == 4);
    fp.i = 0;
    memcpy(data, in->buf->str + offset, 4);

    if(inc->byte_order == MSB){
        for(i = 0; i < bytnr; ++i){
            fp.i = fp.i << 8;
            fp.i |= data[i];
        }
    }else{
        for(i = bytnr - 1; i >= 0; --i){
            fp.i = fp.i << 8;
            fp.i |= data[i];
        }
    }

    return fp.f;
}

static void send_chunk(struct sr_input *in, size_t initial_offset, size_t num_samples)
{
    struct sr_datafeed_packet packet;
    struct sr_datafeed_analog analog;
    struct sr_analog_encoding encoding;
    struct sr_analog_meaning meaning;
    struct sr_analog_spec spec;
    struct context *inc;
    float *fdata;
    size_t offset, i;

    inc = in->priv;
    offset = initial_offset;
    fdata = g_malloc0(sizeof(float) * num_samples);
    for(i = 0; i < num_samples; ++i){
        if(inc->bn_fmt == RI){
            fdata[i] = ((float) read_int_value(in, offset) - inc->yoff) * inc->ymult + inc->yzero;
        }else if(inc->bn_fmt == FP)
            fdata[i] = (read_float_value(in, offset) - inc->yoff) * inc->ymult + inc->yzero;
        offset += inc->bytnr;

        /* Convert W to dBm */
        if(inc->wfmtype == RF_FD)
            fdata[i] = 10* log10f(1000* fdata[i]);
    }

    sr_analog_init(&analog, &encoding, &meaning, &spec, 2);
    packet.type = SR_DF_ANALOG;
    packet.payload = &analog;
    analog.num_samples = num_samples;
    analog.data = fdata;
    analog.meaning->channels = in->sdi->channels;
    analog.meaning->mq = 0;
    analog.meaning->mqflags = 0;
    analog.meaning->unit = 0;

    sr_session_send(in->sdi, &packet);
    g_free(fdata);
}

static int process_buffer(struct sr_input *in)
{
    struct context *inc;
    char *data;
    size_t offset, chunk_samples, total_samples, processed, max_chunk_samples, num_samples;

    inc = in->priv;
    if(!inc->started){
        std_session_send_df_header(in->sdi);
        (void) sr_session_send_meta(in->sdi, SR_CONF_SAMPLERATE, g_variant_new_uint64((uint64_t) (1 / inc->xincr)));
        inc->started = TRUE;
    }

    if(!inc->found_header){
        data = find_data_section(in->buf);
        if(data == NULL) {
            sr_err("Couldn't find data section.");
            return SR_ERR;
        }
        offset = data - in->buf->str;
        inc->found_header = TRUE;
    }else
        offset = 0;

    processed = 0;
    chunk_samples = (in->buf->len - offset)/inc->bytnr;
    max_chunk_samples = CHUNK_SIZE/inc->bytnr;
    total_samples = chunk_samples;
    
    while(processed < total_samples){
        if(chunk_samples > max_chunk_samples)
            num_samples = max_chunk_samples;
        else
            num_samples = chunk_samples;

        send_chunk(in, offset, num_samples);
        offset += num_samples * inc->bytnr;
        chunk_samples -= num_samples;
        processed += num_samples;
    }

    if(offset < in->buf->len)
        g_string_erase(in->buf, 0, offset);
    else
        g_string_truncate(in->buf, 0);

    return SR_OK;
}

static int receive(struct sr_input *in, GString *buf)
{
    struct context *inc;
    int ret;

    inc = in->priv;
    g_string_append_len(in->buf, buf->str, buf->len);

    if(!in->sdi_ready) {
        if(!has_header(in->buf))
            return SR_OK;

        ret = parse_isf_header(in->buf, inc);
        if (ret != SR_OK)
            return ret;

        if(inc->bytnr > 8){
            sr_err("Byte number > 8 is not supported.");
            return SR_ERR_NA;
        }

        if(inc->create_channel)
            sr_channel_new(in->sdi, 0, SR_CHANNEL_ANALOG, TRUE, inc->channel_name);

        in->sdi_ready = TRUE;
        return SR_OK;
    }

    return process_buffer(in);
}

static int end(struct sr_input *in)
{
    struct context *inc;
    int ret;

    if (in->sdi_ready)
        ret = process_buffer(in);
    else
        ret = SR_OK;

    inc = in->priv;
    if (inc->started)
        std_session_send_df_end(in->sdi);

    return ret;
}

static int reset(struct sr_input *in){
    memset(in->priv, 0, sizeof(struct context));

    g_string_truncate(in->buf, 0);
    return SR_OK;
}

SR_PRIV struct sr_input_module input_isf = {
        .id = "isf",
        .name = "ISF",
        .desc = "Tektronix isf format",
        .exts = (const char *[]){"isf", NULL},
        .metadata = {SR_INPUT_META_FILENAME, SR_INPUT_META_HEADER | SR_INPUT_META_REQUIRED},
        .format_match = format_match,
        .init = init,
        .receive = receive,
        .end = end,
        .reset = reset
};
