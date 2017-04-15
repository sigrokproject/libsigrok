/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2010 Uwe Hermann <uwe@hermann-uwe.de>
 * Copyright (C) 2013 Bert Vermeulen <bert@biot.com>
 * Copyright (C) 2017-2020 Gerhard Sittig <gerhard.sittig@gmx.net>
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
 * TODO
 * - Check the mixed signal queue for completeness and correctness.
 * - Tune the analog "immediate write" code path for throughput.
 * - Remove excess diagnostics when the implementation is considered
 *   feature complete and reliable.
 */

#include <config.h>

#include <ctype.h>
#include <glib.h>
#include <stdlib.h>
#include <string.h>

#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "output/vcd"

static const int with_queue_stats = 0;
static const int with_pool_stats = 0;

struct vcd_channel_desc {
	size_t index;
	GString *name;
	enum sr_channeltype type;
	struct {
		uint8_t logic;
		double real;
	} last;
	uint64_t last_rcvd_snum;
};

/** Queued values for a given sample number. */
struct vcd_queue_item {
	uint64_t samplenum;	/**!< sample number, _not_ timestamp */
	GString *values;	/**!< text of value changes */
};

struct context {
	size_t enabled_count;
	size_t logic_count;
	size_t analog_count;
	gboolean header_done;
	uint64_t period;
	struct vcd_channel_desc *channels;
	uint64_t samplerate;
	GSList *free_list, *used_list;
	size_t alloced, freed, reused, pooled;
	GList *vcd_queue_list;
	GList *vcd_queue_last;
	gboolean immediate_write;
	uint8_t *last_logic;
};

/*
 * Construct VCD signal identifiers from a sigrok channel index. The
 * routine returns a GString which the caller is supposed to release.
 *
 * There are 94 printable ASCII characters. For larger channel index
 * numbers multiple letters get concatenated (sticking with letters).
 *
 * The current implementation covers these ranges:
 * - 94 single letter identifiers
 * - 26 ^ 2 = 676, 94 + 676 = 770 for two letter identifiers
 * - 26 ^ 3 = 17576, 770 + 17576 = 18346 for three letter identifiers
 *
 * This approach can get extended as needed when support for larger
 * channel counts is desired. Any such extension remains transparent
 * to call sites.
 *
 * TODO This implementation assumes that the software will run on a
 * machine which uses the ASCII character set. Platforms that use other
 * representations or non-contiguous character ranges for their alphabet
 * cannot use a simple addition, instead need to execute table lookups.
 */

#define VCD_IDENT_CHAR_MIN	'!'
#define VCD_IDENT_CHAR_MAX	'~'
#define VCD_IDENT_COUNT_1CHAR	(VCD_IDENT_CHAR_MAX + 1 - VCD_IDENT_CHAR_MIN)
#define VCD_IDENT_ALPHA_MIN	'a'
#define VCD_IDENT_ALPHA_MAX	'z'
#define VCD_IDENT_COUNT_ALPHA	(VCD_IDENT_ALPHA_MAX + 1 - VCD_IDENT_ALPHA_MIN)
#define VCD_IDENT_COUNT_2CHAR	(VCD_IDENT_COUNT_ALPHA * VCD_IDENT_COUNT_ALPHA)
#define VCD_IDENT_COUNT_3CHAR	(VCD_IDENT_COUNT_2CHAR * VCD_IDENT_COUNT_ALPHA)
#define VCD_IDENT_COUNT		(VCD_IDENT_COUNT_1CHAR + VCD_IDENT_COUNT_2CHAR + VCD_IDENT_COUNT_3CHAR)

static GString *vcd_identifier(size_t idx)
{
	GString *symbol;
	char c1, c2, c3;

	symbol = g_string_sized_new(4);

	/* First 94 channels, one printable character. */
	if (idx < VCD_IDENT_COUNT_1CHAR) {
		c1 = VCD_IDENT_CHAR_MIN + idx;
		g_string_printf(symbol, "%c", c1);
		return symbol;
	}
	idx -= VCD_IDENT_COUNT_1CHAR;

	/* Next 676 channels, two lower case characters. */
	if (idx < VCD_IDENT_COUNT_2CHAR) {
		c2 = VCD_IDENT_ALPHA_MIN + (idx % VCD_IDENT_COUNT_ALPHA);
		idx /= VCD_IDENT_COUNT_ALPHA;
		c1 = VCD_IDENT_ALPHA_MIN + (idx % VCD_IDENT_COUNT_ALPHA);
		idx /= VCD_IDENT_COUNT_ALPHA;
		if (idx)
			sr_dbg("VCD identifier creation BUG (two char).");
		g_string_printf(symbol, "%c%c", c1, c2);
		return symbol;
	}
	idx -= VCD_IDENT_COUNT_2CHAR;

	/* Next 17576 channels, three lower case characters. */
	if (idx < VCD_IDENT_COUNT_3CHAR) {
		c3 = VCD_IDENT_ALPHA_MIN + (idx % VCD_IDENT_COUNT_ALPHA);
		idx /= VCD_IDENT_COUNT_ALPHA;
		c2 = VCD_IDENT_ALPHA_MIN + (idx % VCD_IDENT_COUNT_ALPHA);
		idx /= VCD_IDENT_COUNT_ALPHA;
		c1 = VCD_IDENT_ALPHA_MIN + (idx % VCD_IDENT_COUNT_ALPHA);
		idx /= VCD_IDENT_COUNT_ALPHA;
		if (idx)
			sr_dbg("VCD identifier creation BUG (three char).");
		g_string_printf(symbol, "%c%c%c", c1, c2, c3);
		return symbol;
	}
	idx -= VCD_IDENT_COUNT_3CHAR;

	/*
	 * TODO
	 * Add combinations with more positions or larger character sets
	 * when support for more channels is required.
	 */
	sr_dbg("VCD identifier creation ENOTSUPP (need %zu more).", idx);
	g_string_free(symbol, TRUE);

	return NULL;
}

/*
 * Notes on the VCD text output formatting routines:
 * - Always start new text lines when timestamps get emitted.
 * - Optionally terminate timestamp lines when the caller asked us to.
 * - Prepend all values with whitespace, assume they follow a timestamp
 *   or a previously printed value. This works fine from the data point
 *   of view for the start of new lines, as well.
 * - Put the mandatory whitespace between real (or vector) values and
 *   the following identifier. No whitespace for single bit values.
 * - For real values callers need not specify "precision" nor the number
 *   of significant digits. The Verilog VCD spec specifically picked the
 *   "%.16g" format such that all bits of the internal presentation of
 *   the IEEE754 floating point value get communicated between the
 *   writer and the reader.
 */

static void append_vcd_timestamp(GString *s, double ts, gboolean lf)
{

	g_string_append_c(s, '\n');
	g_string_append_c(s, '#');
	g_string_append_printf(s, "%.0f", ts);
	g_string_append_c(s, lf ? '\n' : ' ');
}

static void format_vcd_value_bit(GString *s, uint8_t bit_value, GString *id)
{

	g_string_append_c(s, bit_value ? '1' : '0');
	g_string_append(s, id->str);
}

static void format_vcd_value_real(GString *s, double real_value, GString *id)
{

	g_string_append_c(s, 'r');
	g_string_append_printf(s, "%.16g", real_value);
	g_string_append_c(s, ' ');
	g_string_append(s, id->str);
}

static int init(struct sr_output *o, GHashTable *options)
{
	struct context *ctx;
	size_t alloc_size;
	struct sr_channel *ch;
	GSList *l;
	size_t num_enabled, num_logic, num_analog, desc_idx;
	struct vcd_channel_desc *desc;

	(void)options;

	/* Determine the number of involved channels. */
	num_enabled = 0;
	num_logic = 0;
	num_analog = 0;
	for (l = o->sdi->channels; l; l = l->next) {
		ch = l->data;
		if (!ch->enabled)
			continue;
		if (ch->type == SR_CHANNEL_LOGIC) {
			num_logic++;
		} else if (ch->type == SR_CHANNEL_ANALOG) {
			num_analog++;
		} else {
			continue;
		}
		num_enabled++;
	}
	if (num_enabled > VCD_IDENT_COUNT) {
		sr_err("Only up to %d VCD signals supported.", VCD_IDENT_COUNT);
		return SR_ERR;
	}

	/* Allocate space for channel descriptions. */
	ctx = g_malloc0(sizeof(*ctx));
	o->priv = ctx;
	ctx->enabled_count = num_enabled;
	ctx->logic_count = num_logic;
	ctx->analog_count = num_analog;
	alloc_size = sizeof(ctx->channels[0]) * ctx->enabled_count;
	ctx->channels = g_malloc0(alloc_size);

	/*
	 * Reiterate input descriptions, to fill in output descriptions.
	 * Map channel indices, and assign symbols to VCD channels.
	 */
	desc_idx = 0;
	for (l = o->sdi->channels; l; l = l->next) {
		ch = l->data;
		if (!ch->enabled)
			continue;
		desc = &ctx->channels[desc_idx];
		desc->index = ch->index;
		desc->name = vcd_identifier(desc_idx);
		desc->type = ch->type;
		/*
		 * Make sure to _not_ match next time, to have initial
		 * values dumped when the first sample gets received.
		 */
		if (desc->type == SR_CHANNEL_LOGIC && num_logic) {
			num_logic--;
			desc->last.logic = ~0;
		} else if (desc->type == SR_CHANNEL_ANALOG && num_analog) {
			num_analog--;
			/* "Construct" NaN, avoid a compile time error. */
			desc->last.real = 0.0;
			desc->last.real = 0.0 / desc->last.real;
		} else {
			g_string_free(desc->name, TRUE);
			memset(desc, 0, sizeof(*desc));
			continue;
		}
		desc_idx++;
	}

	/*
	 * Keep channel counts at hand, and a flag which allows to tune
	 * for special cases' speedup in .receive().
	 */
	ctx->immediate_write = FALSE;
	if (ctx->analog_count == 0)
		ctx->immediate_write = TRUE;
	if (ctx->logic_count == 0 && ctx->analog_count == 1)
		ctx->immediate_write = TRUE;

	/*
	 * Keep a copy of the last logic data bitmap around. To avoid
	 * iterating over individual bits when nothing in the set has
	 * changed. The overhead of two byte array compares should
	 * outweight the tenfold bit count compared to byte counts.
	 */
	alloc_size = (ctx->logic_count + 7) / 8;
	ctx->last_logic = g_malloc0(alloc_size);
	if (ctx->logic_count && !ctx->last_logic)
		return SR_ERR_MALLOC;

	return SR_OK;
}

/*
 * VCD can only handle 1/10/100 factors in the s to fs range. Find a
 * suitable timescale which satisfies this resolution constraint, yet
 * won't result in excessive overhead.
 */
static uint64_t get_timescale_freq(uint64_t samplerate)
{
	uint64_t timescale;
	size_t max_up_scale;

	/* Go to the next full decade. */
	timescale = 1;
	while (timescale < samplerate) {
		timescale *= 10;
	}

	/*
	 * Avoid loss of precision, go up a few more decades when needed.
	 * For example switch to 10GHz timescale when samplerate is 400MHz.
	 * Stop after at most factor 100 to not loop endlessly for odd
	 * samplerates, yet provide good enough accuracy.
	 */
	max_up_scale = 2;
	while (max_up_scale--) {
		if (timescale / samplerate * samplerate == timescale)
			break;
		timescale *= 10;
	}

	return timescale;
}

/* Emit a VCD file header. */
static GString *gen_header(const struct sr_output *o)
{
	struct context *ctx;
	struct sr_channel *ch;
	GVariant *gvar;
	GString *header;
	GSList *l;
	time_t t;
	size_t num_channels, i;
	char *samplerate_s, *frequency_s, *timestamp;
	struct vcd_channel_desc *desc;
	char *type_text, *size_text;
	int ret;

	ctx = o->priv;

	/* Get channel count, and samplerate if not done yet. */
	num_channels = g_slist_length(o->sdi->channels);
	if (!ctx->samplerate) {
		ret = sr_config_get(o->sdi->driver, o->sdi, NULL,
			SR_CONF_SAMPLERATE, &gvar);
		if (ret == SR_OK) {
			ctx->samplerate = g_variant_get_uint64(gvar);
			g_variant_unref(gvar);
		}
	}
	ctx->period = get_timescale_freq(ctx->samplerate);
	t = time(NULL);
	timestamp = g_strdup(ctime(&t));
	timestamp[strlen(timestamp) - 1] = '\0';
	samplerate_s = NULL;
	if (ctx->samplerate)
		samplerate_s = sr_samplerate_string(ctx->samplerate);
	frequency_s = sr_period_string(1, ctx->period);

	/* Construct the VCD output file header. */
	header = g_string_sized_new(512);
	g_string_printf(header, "$date %s $end\n", timestamp);
	g_string_append_printf(header, "$version %s %s $end\n",
		PACKAGE_NAME, sr_package_version_string_get());
	g_string_append_printf(header, "$comment\n");
	g_string_append_printf(header,
		"  Acquisition with %zu/%zu channels%s%s\n",
		ctx->enabled_count, num_channels,
		samplerate_s ? " at " : "", samplerate_s ? : "");
	g_string_append_printf(header, "$end\n");
	g_string_append_printf(header, "$timescale %s $end\n", frequency_s);

	/* List generated VCD signals within a scope. */
	g_string_append_printf(header, "$scope module %s $end\n", PACKAGE_NAME);
	i = 0;
	for (l = o->sdi->channels; l; l = l->next) {
		ch = l->data;
		if (!ch->enabled)
			continue;
		desc = &ctx->channels[i++];
		if (desc->type == SR_CHANNEL_LOGIC) {
			type_text = "wire";
			size_text = "1";
		} else if (desc->type == SR_CHANNEL_ANALOG) {
			type_text = "real";
			size_text = "64";
		} else {
			i--;
			continue;
		}
		g_string_append_printf(header, "$var %s %s %s %s $end\n",
			type_text, size_text, desc->name->str, ch->name);
	}
	g_string_append(header, "$upscope $end\n");

	g_string_append(header, "$enddefinitions $end\n");

	g_free(timestamp);
	g_free(samplerate_s);
	g_free(frequency_s);

	return header;
}

/*
 * Gets called when a session feed packet was received. Either creates
 * a VCD file header (once in the output module's lifetime), or an empty
 * GString. Callers will append the text representation of sample data
 * to that string as needed.
 */
static GString *chk_header(const struct sr_output *o)
{
	struct context *ctx;
	GString *s;

	ctx = o->priv;

	if (!ctx->header_done) {
		ctx->header_done = TRUE;
		s = gen_header(o);
	} else {
		s = g_string_sized_new(512);
	}

	return s;
}

/*
 * Helpers to "merge sort" sample data that we have received in chunks
 * at different times in different code paths. Queue the data until we
 * have seen samples from all involved channels for a given samplenumber.
 * Data for a given sample number can only get emitted when we are sure
 * no other channel's data can arrive any more.
 */

static struct vcd_queue_item *queue_alloc_item(struct context *ctx, uint64_t snum)
{
	GSList *node;
	struct vcd_queue_item *item;

	/* Get an item from the free list if available. */
	node = ctx->free_list;
	if (node) {
		ctx->reused++;

		/* Unlink GSList node from the free list. */
		ctx->free_list = node->next;
		node->next = NULL;
		item = node->data;
		node->data = NULL;

		/* Setup content of the item. */
		item->samplenum = snum;
		if (!item->values)
			item->values = g_string_sized_new(32);
		else
			g_string_truncate(item->values, 0);

		/* Keep GSList node in the used list (avoid free/alloc). */
		node->next = ctx->used_list;
		ctx->used_list = node;

		return item;
	}

	/* Dynamic allocation of an item. */
	ctx->alloced++;
	item = g_malloc0(sizeof(*item));
	if (!item)
		return NULL;
	item->samplenum = snum;
	item->values = g_string_sized_new(32);

	/* Create a used list item, to later move to the free list. */
	ctx->used_list = g_slist_prepend(ctx->used_list, item);

	return item;
}

static void queue_free_item(struct context *ctx, struct vcd_queue_item *item)
{
	GSList *node;

	/*
	 * Put item back into the free list. We can assume to find a
	 * used list node, it got allocated when the item was acquired.
	 */
	node = ctx->used_list;
	if (node) {
		ctx->pooled++;

		ctx->used_list = node->next;
		node->next = NULL;
		node->data = item;

		item->samplenum = 0;
		g_string_truncate(item->values, 0);

		node->next = ctx->free_list;
		ctx->free_list = node;

		return;
	}

	/*
	 * Release dynamically allocated resources. Could also be used
	 * to release free list items when the use list is empty.
	 */
	ctx->freed++;
	if (item->values)
		g_string_free(item->values, TRUE);
	g_free(item);
}

static void queue_drain_pool_cb(gpointer data, gpointer cb_data)
{
	struct context *ctx;
	struct vcd_queue_item *item;

	item = data;
	ctx = cb_data;
	queue_free_item(ctx, item);
}

static void queue_drain_pool(struct context *ctx)
{
	GSList *list;

	/*
	 * Grab the list and "empty" the context member. Then
	 * iterate over the items, have dymamic memory released.
	 * Then free the GSList nodes (but not their data parts).
	 * Do this for the used and the free lists.
	 */
	list = ctx->used_list;
	ctx->used_list = NULL;
	g_slist_foreach(list, queue_drain_pool_cb, ctx);
	g_slist_free(list);

	list = ctx->free_list;
	ctx->free_list = NULL;
	g_slist_foreach(list, queue_drain_pool_cb, ctx);
	g_slist_free(list);
}

static int cmp_snum(gconstpointer l, gconstpointer d)
{
	const struct vcd_queue_item *list_item;
	const uint64_t *snum_ptr;

	list_item = l;
	snum_ptr = d;
	if (list_item->samplenum > *snum_ptr)
		return +1;
	if (list_item->samplenum < *snum_ptr)
		return -1;
	return 0;
}

static int cmp_items(gconstpointer a, gconstpointer b)
{
	const struct vcd_queue_item *item_a, *item_b;

	item_a = a;
	item_b = b;
	if (item_a->samplenum > item_b->samplenum)
		return +1;
	if (item_a->samplenum < item_b->samplenum)
		return -1;
	return 0;
}

/*
 * Position the current pointer of the VCD value queue to a specific
 * sample number. Create a new queue item when needed. The logic assumes
 * a specific use pattern: Reception of striped sample data for channels
 * and processing in strict order of sample numbers within a channel.
 * Lower sample numbers near the start of the queue when channels change
 * between session feed packets, before another linear sequence follows.
 *
 * Naive use of convenience glib routines would severely lose performance.
 * That's why custom code is used, which is as complex as it needs to be,
 * yet shall execute faster than a simpler implementation. For trivial
 * cases (logic only, one analog channel only) this queue is bypassed.
 */
static int queue_samplenum(struct context *ctx, uint64_t snum)
{
	struct vcd_queue_item *item, *add_item;
	GList *walk_list, *after_snum, *before_snum, *add_list;
	GList *last;
	gboolean add_after_last, do_search;

	/* Already at that position? */
	item = ctx->vcd_queue_last ? ctx->vcd_queue_last->data : NULL;
	if (item && item->samplenum == snum)
		return SR_OK;

	/*
	 * Search after the current position in the remaining queue. The
	 * custom code uses the queue's being sorted by sample number.
	 * Narrow down a later insert position as much as possible. This
	 * avoids linear search in huge spaces later on.
	 */
	last = NULL;
	add_after_last = FALSE;
	after_snum = NULL;
	before_snum = NULL;
	walk_list = ctx->vcd_queue_last;
	while (walk_list) {
		item = walk_list->data;
		if (!item)
			break;
		if (item->samplenum == snum) {
			ctx->vcd_queue_last = walk_list;
			return SR_OK;
		}
		last = walk_list;
		if (item->samplenum < snum)
			before_snum = walk_list;
		if (item->samplenum > snum) {
			after_snum = walk_list;
			break;
		}
		if (!walk_list->next)
			add_after_last = TRUE;
		walk_list = walk_list->next;
	}

	/*
	 * No exact match at or beyond the current position. Run another
	 * search from the start of the queue, again restrict the space
	 * which is searched, and narrow down the insert position when
	 * no match is found.
	 *
	 * If the searched sample number is larger than any we have seen
	 * before, or was in the above covered range but was not found,
	 * then we know that another queue item needs to get added, and
	 * where to put it. In that case we need not iterate the earlier
	 * list items.
	 */
	walk_list = ctx->vcd_queue_list;
	do_search = TRUE;
	if (add_after_last)
		do_search = FALSE;
	if (before_snum)
		do_search = FALSE;
	while (do_search && walk_list && walk_list != ctx->vcd_queue_last) {
		item = walk_list->data;
		if (!item)
			break;
		if (item->samplenum == snum) {
			ctx->vcd_queue_last = walk_list;
			return SR_OK;
		}
		if (item->samplenum < snum)
			before_snum = walk_list;
		if (item->samplenum > snum) {
			after_snum = walk_list;
			break;
		}
		walk_list = walk_list->next;
	}

	/*
	 * The complete existing queue was exhausted, no exact match was
	 * found. A new queue item must get inserted. Identify a good
	 * position where to start searching for the exact position to
	 * link the new item to the list. Assume that the combination of
	 * the glib routine's list traversal and the sample number check
	 * in the callback is expensive, reduce the amount of work done.
	 *
	 * If we have seen an item with a larger sample number than the
	 * wanted, check its immediate predecessor. If this has a smaller
	 * sample number, then we found a perfect location to insert the
	 * new item. If we know that the new item must be inserted after
	 * the last traversed queue item, start there.
	 */
	if (!before_snum) do {
		if (add_after_last)
			break;
		if (!after_snum)
			break;
		walk_list = after_snum->prev;
		if (!walk_list)
			break;
		item = walk_list->data;
		if (!item)
			break;
		if (item->samplenum == snum) {
			ctx->vcd_queue_last = walk_list;
			return SR_OK;
		}
		if (item->samplenum < snum)
			before_snum = walk_list;
	} while (0);
	add_list = add_after_last ? last : before_snum;
	if (!add_list) {
		walk_list = ctx->vcd_queue_list;
		while (walk_list) {
			item = walk_list->data;
			if (!item)
				break;
			if (item->samplenum == snum) {
				ctx->vcd_queue_last = walk_list;
				return SR_OK;
			}
			if (item->samplenum > snum) {
				after_snum = walk_list;
				break;
			}
			add_list = walk_list;
			walk_list = walk_list->next;
		}
	}
	if (add_list && (item = add_list->data) && item->samplenum == snum) {
		ctx->vcd_queue_last = add_list;
		return SR_OK;
	}

	/*
	 * Create a new queue item for the so far untracked sample
	 * number. Immediately search for the inserted position (is
	 * unfortunately not returned from the insert call), and
	 * cache that position for subsequent lookups.
	 */
	if (with_queue_stats)
		sr_dbg("%s(), queue nr %" PRIu64, __func__, snum);
	add_item = queue_alloc_item(ctx, snum);
	if (!add_item)
		return SR_ERR_MALLOC;
	if (!add_list)
		add_list = ctx->vcd_queue_list;
	if (add_list && add_list->prev)
		add_list = add_list->prev;
	walk_list = g_list_insert_sorted(add_list, add_item, cmp_items);
	if (!walk_list->prev)
		ctx->vcd_queue_list = walk_list;
	walk_list = g_list_find_custom(walk_list, &snum, cmp_snum);
	item = walk_list ? walk_list->data : NULL;
	if (item && item->samplenum == snum) {
		ctx->vcd_queue_last = walk_list;
	}
	return SR_OK;
}

/*
 * Prepare to append another text fragment for a value change to the
 * queue item which corresponds to the current sample number. Return
 * the GString which the caller then will append to.
 */
static GString *queue_value_text_prep(struct context *ctx)
{
	struct vcd_queue_item *item;
	GString *buff;

	/* Cope with not-yet-positioned write pointers. */
	item = ctx->vcd_queue_last ? ctx->vcd_queue_last->data : NULL;
	if (!item)
		return NULL;

	/* Create a GString if not done already. */
	buff = item->values;
	if (!buff) {
		buff = g_string_sized_new(20);
		item->values = buff;
	}

	/* Separate items with spaces (if previous content is present). */
	if (buff->len)
		g_string_append_c(buff, ' ');

	return buff;
}

static double snum_to_ts(struct context *ctx, uint64_t snum)
{
	double ts;

	ts = (double)snum;
	ts /= ctx->samplerate;
	ts *= ctx->period;

	return ts;
}

/*
 * Unqueue one item of the VCD values queue which corresponds to one
 * sample number. Append all of the text to the passed in GString.
 */
static int unqueue_item(struct context *ctx,
	struct vcd_queue_item *item, GString *s)
{
	double ts;
	GString *buff;
	gboolean is_empty;

	/*
	 * Start the sample number's string with the timestamp. Append
	 * all value changes. Terminate lines for items which have a
	 * timestamp but no value changes, assuming this is the last
	 * entry which corresponds to SR_DF_END.
	 */
	ts = snum_to_ts(ctx, item->samplenum);
	buff = item->values;
	is_empty = !buff || !buff->len || !buff->str || !*buff->str;
	append_vcd_timestamp(s, ts, is_empty);
	if (!is_empty)
		g_string_append(s, buff->str);

	return SR_OK;
}

/*
 * Get the last sample number which logic data was received for. This
 * implementation assumes that all logic channels get received within
 * exactly one packet of corresponding unitsize.
 */
static uint64_t get_last_snum_logic(struct context *ctx)
{
	size_t i;
	struct vcd_channel_desc *desc;

	for (i = 0; i < ctx->enabled_count; i++) {
		desc = &ctx->channels[i];
		if (desc->type != SR_CHANNEL_LOGIC)
			continue;
		return desc->last_rcvd_snum;
	}

	return 0;
}

/*
 * Update the last sample number which logic data was received for.
 */
static void upd_last_snum_logic(struct context *ctx, uint64_t inc)
{
	size_t i;
	struct vcd_channel_desc *desc;

	for (i = 0; i < ctx->enabled_count; i++) {
		desc = &ctx->channels[i];
		if (desc->type != SR_CHANNEL_LOGIC)
			continue;
		desc->last_rcvd_snum += inc;
	}
}

/*
 * Get and update the last sample number which analog data was received
 * for on a specific channel (which the caller already has identified).
 */

static uint64_t get_last_snum_analog(struct vcd_channel_desc *desc)
{

	return desc->last_rcvd_snum;
}

static void upd_last_snum_analog(struct vcd_channel_desc *desc, uint64_t inc)
{

	if (!desc)
		return;
	desc->last_rcvd_snum += inc;
}

/*
 * Determine the maximum sample number which data from all involved
 * channels was received for.
 */
static uint64_t get_max_snum_export(struct context *ctx)
{
	uint64_t snum;
	size_t i;
	struct vcd_channel_desc *desc;

	snum = ~UINT64_C(0);
	for (i = 0; i < ctx->enabled_count; i++) {
		desc = &ctx->channels[i];
		if (snum > desc->last_rcvd_snum)
			snum = desc->last_rcvd_snum;
	}

	return snum;
}

/*
 * Determine the maximum sample number of any channel we may have
 * received data for. Then pretend we had seen that number of samples
 * on all channels. Such that the next export can flush all previously
 * queued data up to and including the final number, which serves as
 * some kind of termination of the VCD output data.
 */
static uint64_t get_max_snum_flush(struct context *ctx)
{
	uint64_t snum;
	size_t i;
	struct vcd_channel_desc *desc;

	/* Determine the maximum sample number. */
	snum = 0;
	for (i = 0; i < ctx->enabled_count; i++) {
		desc = &ctx->channels[i];
		if (snum < desc->last_rcvd_snum)
			snum = desc->last_rcvd_snum;
	}

	/* Record that number as "seen" with all channels. */
	for (i = 0; i < ctx->enabled_count; i++) {
		desc = &ctx->channels[i];
		desc->last_rcvd_snum = snum + 1;
	}

	return snum;
}

/*
 * Pass all queued value changes when we are certain we have received
 * data from all channels.
 */
static int write_completed_changes(struct context *ctx, GString *out)
{
	uint64_t upto_snum;
	GList **listref, *node;
	struct vcd_queue_item *item;
	int rc;
	size_t dumped;

	/* Determine the number which all data was received for so far. */
	upto_snum = get_max_snum_export(ctx);
	if (with_queue_stats)
		sr_spew("%s(), check up to %" PRIu64, __func__, upto_snum);

	/*
	 * Forward and consume those items from the head of the list
	 * which we completely have accumulated and are certain about.
	 */
	dumped = 0;
	listref = &ctx->vcd_queue_list;
	while (*listref) {
		/* Find items before the targetted sample number. */
		node = *listref;
		item = node->data;
		if (!item)
			break;
		if (item->samplenum >= upto_snum)
			break;

		/*
		 * Unlink the item from the list. Void cached positions.
		 * Append its timestamp and values to the caller's text.
		 */
		dumped++;
		if (with_queue_stats)
			sr_dbg("%s(), dump nr %" PRIu64,
				__func__, item->samplenum);
		if (ctx->vcd_queue_last == node)
			ctx->vcd_queue_last = NULL;
		*listref = g_list_remove_link(*listref, node);
		rc = unqueue_item(ctx, item, out);
		queue_free_item(ctx, item);
		if (rc != SR_OK)
			return rc;
	}

	return SR_OK;
}

/* Get packets from the session feed, generate output text. */
static int receive(const struct sr_output *o,
	const struct sr_datafeed_packet *packet, GString **out)
{
	struct context *ctx;
	const struct sr_datafeed_meta *meta;
	const struct sr_datafeed_logic *logic;
	const struct sr_datafeed_analog *analog;
	const struct sr_config *src;
	GSList *l;
	struct vcd_channel_desc *desc;
	uint64_t snum_curr;
	size_t count, index, p, unit_size;
	gboolean changed;
	GString *s_val;
	uint8_t *sample, *last_logic, prevbit, curbit;
	GSList *channels;
	struct sr_channel *channel;
	int rc;
	float *floats, value;
	double ts;

	*out = NULL;
	if (!o || !o->priv)
		return SR_ERR_BUG;
	ctx = o->priv;

	switch (packet->type) {
	case SR_DF_META:
		meta = packet->payload;
		for (l = meta->config; l; l = l->next) {
			src = l->data;
			if (src->key != SR_CONF_SAMPLERATE)
				continue;
			ctx->samplerate = g_variant_get_uint64(src->data);
		}
		break;
	case SR_DF_LOGIC:
		*out = chk_header(o);

		logic = packet->payload;
		sample = logic->data;
		unit_size = logic->unitsize;
		count = logic->length / unit_size;
		snum_curr = get_last_snum_logic(ctx);
		upd_last_snum_logic(ctx, count);

		last_logic = ctx->last_logic;
		while (count--) {
			/* Check whether any logic value has changed. */
			changed = memcmp(last_logic, sample, unit_size) != 0;
			changed |= snum_curr == 0;
			if (changed)
				memcpy(last_logic, sample, unit_size);

			/*
			 * Start or continue tracking that sample number.
			 * Avoid string copies for logic-only setups.
			 */
			if (changed) {
				if (ctx->immediate_write) {
					ts = snum_to_ts(ctx, snum_curr);
					append_vcd_timestamp(*out, ts, FALSE);
				} else {
					queue_samplenum(ctx, snum_curr);
				}
			}

			/* Iterate over individual logic channels. */
			for (p = 0; changed && p < ctx->enabled_count; p++) {
				/*
				 * TODO Check whether the mapping from
				 * data image positions to channel numbers
				 * is required. Experiments suggest that
				 * the data image "is dense", and packs
				 * bits of enabled channels, and leaves no
				 * room for positions of disabled channels.
				 */
				desc = &ctx->channels[p];
				if (desc->type != SR_CHANNEL_LOGIC)
					continue;
				index = desc->index;
				prevbit = desc->last.logic;

				/* Skip over unchanged values. */
				curbit = sample[index / 8];
				curbit = (curbit & (1 << (index % 8))) ? 1 : 0;
				if (snum_curr != 0 && prevbit == curbit)
					continue;
				desc->last.logic = curbit;

				/*
				 * Queue, or immediately emit the text for
				 * the observed value change.
				 */
				if (ctx->immediate_write) {
					g_string_append_c(*out, ' ');
					s_val = *out;
				} else {
					s_val = queue_value_text_prep(ctx);
					if (!s_val)
						break;
				}
				format_vcd_value_bit(s_val, curbit, desc->name);
			}

			/* Advance to next set of logic samples. */
			snum_curr++;
			sample += unit_size;
		}
		write_completed_changes(ctx, *out);
		break;
	case SR_DF_ANALOG:
		*out = chk_header(o);

		/*
		 * This implementation expects one analog packet per
		 * individual channel, with a number of samples each.
		 * Lookup the VCD output channel description.
		 */
		analog = packet->payload;
		count = analog->num_samples;
		channels = analog->meaning->channels;
		if (g_slist_length(channels) != 1) {
			sr_err("Analog packets must be single-channel.");
			return SR_ERR_ARG;
		}
		channel = g_slist_nth_data(channels, 0);
		desc = NULL;
		for (index = 0; index < ctx->enabled_count; index++) {
			desc = &ctx->channels[index];
			if ((int)desc->index == channel->index)
				break;
		}
		if (!desc)
			return SR_OK;
		if (desc->type != SR_CHANNEL_ANALOG)
			return SR_ERR;
		snum_curr = get_last_snum_analog(desc);
		upd_last_snum_analog(desc, count);

		/*
		 * Convert incoming data to an array of single precision
		 * floating point values.
		 */
		floats = g_try_malloc(sizeof(*floats) * analog->num_samples);
		if (!floats)
			return SR_ERR_MALLOC;
		rc = sr_analog_to_float(analog, floats);
		if (rc != SR_OK) {
			g_free(floats);
			return rc;
		}

		/*
		 * Check for changes in the channel's values. Have the
		 * sample number's timestamp and new value printed when
		 * the value has changed.
		 */
		for (index = 0; index < count; index++) {
			/* Check for changes in the channel's values. */
			value = floats[index];
			changed = value != desc->last.real;
			changed |= snum_curr + index == 0;
			if (!changed)
				continue;
			desc->last.real = value;

			/* Queue, or emit the timestamp and the new value. */
			if (ctx->immediate_write) {
				ts = snum_to_ts(ctx, snum_curr + index);
				append_vcd_timestamp(*out, ts, FALSE);
				s_val = *out;
			} else {
				queue_samplenum(ctx, snum_curr + index);
				s_val = queue_value_text_prep(ctx);
			}
			format_vcd_value_real(s_val, value, desc->name);
		}

		g_free(floats);
		write_completed_changes(ctx, *out);
		break;
	case SR_DF_END:
		*out = chk_header(o);
		/* Push the final timestamp as length indicator. */
		snum_curr = get_max_snum_flush(ctx);
		queue_samplenum(ctx, snum_curr);
		/* Flush previously queued value changes. */
		write_completed_changes(ctx, *out);
		break;
	}

	return SR_OK;
}

static int cleanup(struct sr_output *o)
{
	struct context *ctx;
	struct vcd_channel_desc *desc;

	if (!o || !o->priv)
		return SR_ERR_ARG;

	ctx = o->priv;

	if (with_pool_stats)
		sr_info("STATS: alloc/reuse %zu/%zu, pool/free %zu/%zu",
			ctx->alloced, ctx->reused, ctx->pooled, ctx->freed);
	queue_drain_pool(ctx);
	if (with_pool_stats)
		sr_info("STATS: alloc/reuse %zu/%zu, pool/free %zu/%zu",
			ctx->alloced, ctx->reused, ctx->pooled, ctx->freed);

	while (ctx->enabled_count--) {
		desc = &ctx->channels[ctx->enabled_count];
		g_string_free(desc->name, TRUE);
	}
	g_free(ctx->channels);
	g_free(ctx);

	return SR_OK;
}

struct sr_output_module output_vcd = {
	.id = "vcd",
	.name = "VCD",
	.desc = "Value Change Dump data",
	.exts = (const char*[]){"vcd", NULL},
	.flags = 0,
	.options = NULL,
	.init = init,
	.receive = receive,
	.cleanup = cleanup,
};
