/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2019-2023 Gerhard Sittig <gerhard.sittig@gmx.net>
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

/*
 * This input module reads data values from an input stream, and sends
 * the corresponding samples to the sigrok session feed which form the
 * respective waveform, pretending that a logic analyzer had captured
 * wire traffic. This allows to feed data to protocol decoders which
 * were recorded by different means (COM port redirection, pcap(3)
 * recordings, 3rd party bus analyzers). It can also simplify the
 * initial creation of protocol decoders by generating synthetic
 * input data, before real world traffic captures become available.
 *
 * This input module "assumes ideal traffic" and absence of protocol
 * errors. Does _not_ inject error conditions, instead generates valid
 * bit patterns by naively filling blanks to decorate the payload data
 * which the input file provides. To yield a stream of samples which
 * successfully decodes at the recipient's, and upper layer decoders
 * will see valid data which corresponds to the file's content. Edge
 * positions and minute timnig details are not adjustable either in
 * this module (no support for setup or hold times or slew rates etc).
 * The goal is not to emulate a protocol with all its possibilities to
 * the fullest detail. The module's purpose is to simplify the import
 * of values while no capture of the wire traffic was available.
 *
 * There are several approaches to using the input module:
 * - Input data can be a mere bytes sequence. While attributes can get
 *   specified by means of input module options. This is the fastest
 *   approach to accessing raw data that's externally made available.
 * - An optional leading magic literal supports automatic file type
 *   detection, and obsoletes the -I input module selection. Unwanted
 *   automatic detection is possible but very unlikely. The magic text
 *   was chosen such that its occurance at the very start of payload
 *   data is extremely unlikely, and is easy to work around should the
 *   situation happen. Of course specifying input module options does
 *   necessitate the selection of the input module.
 * - When the file type magic is present, an optional header section
 *   can follow, and can carry parameters which obsolete the necessity
 *   to specify input module options. The choice of header section
 *   boundaries again reduces the likelyhood of false detection. When
 *   input module options were specified, they take precedence over
 *   input stream content.
 * - The payload of the input stream (the protocol values) can take
 *   the form of a mere bytes sequence where every byte is a value
 *   (this is the default). Or values can be represented in textual
 *   format when either an input module option or the header section
 *   specify that the input is text. Individual protocol handlers can
 *   also prefer one format over another, while file content and
 *   module options take precedence as usual. Some protocols may not
 *   usefully be described by values only, or may involve values and
 *   numbers larger than a byte, which essentially makes text format
 *   a non-option for these situations.
 * - The text format supports coments which silently get discarded.
 *   As well as pseudo comments which can affect the interpretation
 *   of the input text, and/or can control properties of protocols
 *   that exceed the mere submission of values. Think chip-select or
 *   ACK/NAK slots or similar.
 * - It's understood that the text format is more expensive to process,
 *   but is also more versatile. It's assumed that the 'protocoldata'
 *   input format is used for small or mid size capture lengths. The
 *   input module enables quick access to data that became available
 *   by other means. For higher fidelity of real world traffic and for
 *   long captures the native format should be preferred. For error
 *   injection the VCD format might be a better match.
 * - It should be obvious that raw bytes or input data in text form,
 *   as well as header fields can either be the content of a file on
 *   disk, or can be part of a pipe input. Either the earlier process
 *   in the pipe which provides the values, or an intermediate filter
 *   in the pipe, can provide the decoration.
 *     $ ./gen-values.sh | sigrok-cli -i - ...
 *     $ ./gen-values.sh | cat header - | sigrok-cli -i - ...
 * - Since the input format supports automatic detection as well as
 *   parameter specs by means of input module options as well as in
 *   file content, the format lends itself equally well to pipelined
 *   or scripted as well as interactive use in different applications.
 *   For pipelines, the header as well as the values (as well as any
 *   mix of these pieces) can be kept in separate locations. Generators
 *   need not provide all of the input stream in a single invocation.
 * - As a matter of convenience, especially when targetting upper layer
 *   protocol decoders, users need not construct "correctly configured"
 *   from the lower protocol's perspective) waveforms on the wire.
 *   Instead "naive" waveforms which match the decoders' default options
 *   can be used, which eliminates the need to configure non-default
 *   options in decoders (and redundantly do the same thing in the
 *   input module, just to have them match again).
 *     $ ./gen-values.sh | sigrok-cli \
 *       -i - -I protocoldata:protocol=uart:bitrate=57600:frameformat=8e2 \
 *       -P uart:parity=even:baudrate=57600
 *     $ ./gen-values.sh | sigrok-cli \
 *       -i - -I protocoldata:protocol=uart -P uart,midi
 *
 * Example invocations:
 *
 *   $ sigrok-cli -I protocoldata --show
 *
 *   $ echo "Hello sigrok protocol values!" | \
 *     sigrok-cli \
 *       -I protocoldata:protocol=uart -i - \
 *       -P uart:format=ascii -A uart=rx-data
 *
 *   $ sigrok-cli -i file.bin -P uart -A uart=rx-data
 *   $ sigrok-cli -i file.txt -P uart:rx=rxtx -A uart
 *   $ sigrok-cli -i file.txt --show
 *   $ sigrok-cli -i file.txt -O ascii:width=4000 | $PAGER
 *
 *   $ echo "# -- sigrok protocol data values file --" > header.txt
 *   $ echo "# -- sigrok protocol data header start --" >> header.txt
 *   $ echo "protocol=uart" >> header.txt
 *   $ echo "bitrate=100000" >> header.txt
 *   $ echo "frameformat=8e2" >> header.txt
 *   $ echo "textinput=yes" >> header.txt
 *   $ echo "# -- sigrok protocol data header end --" >> header.txt
 *   $ echo "# textinput: radix=16" > values.txt
 *   $ echo "0f  40 a6 28 fa 78 05 19 ee c2 92 70 58 62 09 a9 f1 ca 44 90 d1 07 19  02  00" >> values.txt
 *   $ head header.txt values.txt
 *   $ cat values.txt | cat header.txt - | \
 *     sigrok-cli -i - -P uart:baudrate=100000:parity=even,sbus_futaba -A sbus_futaba
 *
 *   $ pulseview -i file-spi-text.txt &
 *
 * Known issues:
 * - Only few protocols are implemented so far. Existing handlers have
 *   suggested which infrastructure is required for future extension.
 *   But future handlers may reveal more omissions or assumptions that
 *   need addressing.
 * - Terminology may be inconsistent, because this input module supports
 *   several protocols which often differ in how they use terms. What is
 *   available:
 *   - The input module constructs waveforms that span multiple traces.
 *     Resulting waveforms are said to have a samplerate. Data that is
 *     kept in that waveform can have a bitrate. Which is essential for
 *     asynchronous communication, but could be unimportant for clocked
 *     protocols. Protocol handlers may adjust their output to enforce
 *     a bitrate, but need not. The timing is an approximation anyway,
 *     does not reflect pauses or jitter or turnarounds which real world
 *     traffic would reveal.
 *   - Protocol handlers can generate an arbitrary number of samples for
 *     a protocol data value. A maximum number of samples per value is
 *     assumed. Variable length samples sequences per data value or per
 *     invocation is supported (and can be considered the typical case).
 *   - Protocol handlers can configure differing widths for the samples
 *     that they derived from input data. These quanta get configured
 *     when the frame format gets interpreted, and are assumed to remain
 *     as they are across data value processing.
 *   - Data values can be considered "a frame" (as seen with UART). But
 *     data values could also be "bytes" or "words" in a protocol, while
 *     "frames" or "transfers" are implemented by different means (as
 *     seen with SPI or I2C). The typical approach would be to control a
 *     "select" signal by means of pseudo comments which are interleaved
 *     with data values.
 *   - Data values need not get forwarded to decoders. They might also
 *     control the processing of the following data values as well as
 *     the waveform construction. This is at the discretion of protocol
 *     handlers, think of slave addresses, preceeding field or value
 *     counts before their data values follow, etc.
 * - Users may need to specify more options than expected when the file
 *   content is "incomplete". The sequence of scanning builtin defaults,
 *   then file content provided specs, then user specified specs, is
 *   yet to get done. Until then it helps being explicit and thorough.
 *
 * TODO (arbitrary order, could partially be outdated)
 * - Implement the most appropriate order of option scanning. Use
 *   builtin defaults first, file content then, then user specified
 *   options (when available). This shall be most robust and correct.
 * - Switch to "submit one sample" in feed queue API when available.
 *   The current implementation of this input module uses ugly ifdefs
 *   to adjust to either feed queue API approach.
 * - (obsoleted by the introduction of support for text format input?)
 *   Introduce TLV support for the binary input format? u32be type,
 *   u64be length, u8[] payload. The complexity of the implementation
 *   in the input module, combined with the complexity of generating
 *   the input stream which uses TLV sections, are currently considered
 *   undesirable for this input module. Do we expect huge files where
 *   the computational cost of text conversion causes pain?
 * - Extend the UART protocol handler. Implement separate RX and TX
 *   traces. Support tx-only, rx-only, and tx-then-rx input orders.
 * - Add a 'parallel' protocol handler, which grabs a bit pattern and
 *   derives the waveform in straight forward ways? This would be similar
 *   to the raw binary input module, but the text format could improve
 *   readability, and the input module could generate a clock signal
 *   which isn't part of the input stream. That 'parallel' protocol
 *   could be used as a vehicle to bitbang any other protocol that is
 *   unknown to the input module. The approach is only limited by the
 *   input stream generator's imagination.
 * - Add other protocol variants. The binary input format was very
 *   limiting, the text format could cover a lot of more cases:
 *   - CAN: Pseudo comments can communicate the frame's flags (and
 *     address type etc). The first data value can be the address. The
 *     second data value or a pseudo comment can hold the CAN frame's
 *     data length (bytes count). Other data values are the 0..8 data
 *     bytes. CAN-FD might be possible with minimal adjustment?
 *   - W1: Pseudo comments can start a frame (initiate RESET). First
 *     value can carry frame length. Data bytes follow. Scans can get
 *     represented as raw bytes (bit count results in full 8bit size).
 * - Are more than 8 traces desirable? The initial implementation was
 *   motivated by serial communication (UART). More channels were not
 *   needed so far. Even QuadSPI and Hitachi displays fit onto 8 lines.
 *
 * See the sigrok.org file format wiki page for details about the syntax
 * that is supported by this input module. Or see the top of the source
 * file and its preprocessor symbols to quickly get an idea of known
 * keywords in input files.
 */

#include "config.h"

#include <ctype.h>
#include <libsigrok/libsigrok.h>
#include <string.h>
#include <strings.h>

#include "libsigrok-internal.h"

#define LOG_PREFIX	"input/protocoldata"

#define CHUNK_SIZE	(4 * 1024 * 1024)

/*
 * Support optional automatic file type detection. Support optionally
 * embedded options in a header section after the file detection magic
 * and before the payload data (bytes or text).
 */
#define MAGIC_FILE_TYPE		"# -- sigrok protocol data values file --"
#define TEXT_HEAD_START		"# -- sigrok protocol data header start --"
#define TEXT_HEAD_END		"# -- sigrok protocol data header end --"
#define TEXT_COMM_LEADER	"#"

#define LABEL_SAMPLERATE	"samplerate="
#define LABEL_BITRATE		"bitrate="
#define LABEL_PROTOCOL		"protocol="
#define LABEL_FRAMEFORMAT	"frameformat="
#define LABEL_TEXTINPUT		"textinput="

/*
 * Options which are embedded in pseudo comments and are related to
 * how the input module reads the input text stream. Universally
 * applicable to all text inputs regardless of protocol choice.
 */
#define TEXT_INPUT_PREFIX	"textinput:"
#define TEXT_INPUT_RADIX	"radix="

/*
 * Protocol dependent frame formats, the default and absolute limits.
 * Protocol dependent keywords in pseudo-comments.
 *
 * UART assumes 9x2 as the longest useful frameformat. Additional STOP
 * bits let users insert idle phases between frames, until more general
 * support for inter-frame gaps is in place. By default the protocol
 * handler generously adds a few more idle bit times after a UART frame.
 *
 * SPI assumes exactly 8 bits per "word". And leaves bit slots around
 * the byte transmission, to have space where CS asserts or releases.
 * Including time where SCK changes to its idle level. And requires two
 * samples per bit time (pos and neg clock phase). The "decoration" also
 * helps users' interactive exploration of generated waveforms.
 *
 * I2C generously assumes six quanta per bit slot, to gracefully allow
 * for reliable SCL and SDA transitions regardless of samples that result
 * from prior communication. The longest waveform is a byte (with eight
 * data bits and an ACK slot). Special symbols like START, and STOP will
 * fit into that memory while it is not used to communicate a byte.
 */
#define UART_HANDLER_NAME	"uart"
#define UART_DFLT_SAMPLERATE	SR_MHZ(1)
#define UART_DFLT_BITRATE	115200
#define UART_DFLT_FRAMEFMT	"8n1"
#define UART_MIN_DATABITS	5
#define UART_MAX_DATABITS	9
#define UART_MAX_STOPBITS	20
#define UART_ADD_IDLEBITS	2
#define UART_MAX_WAVELEN	(1 + UART_MAX_DATABITS + 1 + UART_MAX_STOPBITS \
				+ UART_ADD_IDLEBITS)
#define UART_FORMAT_INVERT	"inverted"
/* In addition the usual '8n1' et al are supported. */
#define UART_PSEUDO_BREAK	"break"
#define UART_PSEUDO_IDLE	"idle"

#define SPI_HANDLER_NAME	"spi"
#define SPI_DFLT_SAMPLERATE	SR_MHZ(10)
#define SPI_DFLT_BITRATE	SR_MHZ(1)
#define SPI_DFLT_FRAMEFMT	"cs-low,bits=8,mode=0,msb-first"
#define SPI_MIN_DATABITS	8
#define SPI_MAX_DATABITS	8
#define SPI_MAX_WAVELEN		(2 + 2 * SPI_MAX_DATABITS + 3)
#define SPI_FORMAT_CS_LOW	"cs-low"
#define SPI_FORMAT_CS_HIGH	"cs-high"
#define SPI_FORMAT_DATA_BITS	"bits="
#define SPI_FORMAT_SPI_MODE	"mode="
#define SPI_FORMAT_MODE_CPOL	"cpol="
#define SPI_FORMAT_MODE_CPHA	"cpha="
#define SPI_FORMAT_MSB_FIRST	"msb-first"
#define SPI_FORMAT_LSB_FIRST	"lsb-first"
#define SPI_PSEUDO_MOSI_ONLY	"mosi-only"
#define SPI_PSEUDO_MOSI_FIXED	"mosi-fixed="
#define SPI_PSEUDO_MISO_ONLY	"miso-only"
#define SPI_PSEUDO_MISO_FIXED	"miso-fixed="
#define SPI_PSEUDO_MOSI_MISO	"mosi-then-miso"
#define SPI_PSEUDO_MISO_MOSI	"miso-then-mosi"
#define SPI_PSEUDO_CS_ASSERT	"cs-assert"
#define SPI_PSEUDO_CS_RELEASE	"cs-release"
#define SPI_PSEUDO_CS_NEXT	"cs-auto-next="
#define SPI_PSEUDO_IDLE		"idle"

#define I2C_HANDLER_NAME	"i2c"
#define I2C_DFLT_SAMPLERATE	SR_MHZ(10)
#define I2C_DFLT_BITRATE	SR_KHZ(400)
#define I2C_DFLT_FRAMEFMT	"addr-7bit"
#define I2C_BITTIME_SLOTS	(1 + 8 + 1 + 1)
#define I2C_BITTIME_QUANTA	6
#define I2C_ADD_IDLESLOTS	2
#define I2C_MAX_WAVELEN		(I2C_BITTIME_QUANTA * I2C_BITTIME_SLOTS + I2C_ADD_IDLESLOTS)
#define I2C_FORMAT_ADDR_7BIT	"addr-7bit"
#define I2C_FORMAT_ADDR_10BIT	"addr-10bit"
#define I2C_PSEUDO_START	"start"
#define I2C_PSEUDO_REP_START	"repeat-start"
#define I2C_PSEUDO_STOP		"stop"
#define I2C_PSEUDO_ADDR_WRITE	"addr-write="
#define I2C_PSEUDO_ADDR_READ	"addr-read="
#define I2C_PSEUDO_ACK_NEXT	"ack-next="
#define I2C_PSEUDO_ACK_ONCE	"ack-next"

enum textinput_t {
	INPUT_UNSPEC,
	INPUT_BYTES,
	INPUT_TEXT,
};

static const char *input_format_texts[] = {
	[INPUT_UNSPEC] = "from-file",
	[INPUT_BYTES] = "raw-bytes",
	[INPUT_TEXT] = "text-format",
};

struct spi_proto_context_t {
	gboolean needs_mosi, has_mosi;
	gboolean needs_miso, has_miso;
	gboolean mosi_first;
	gboolean cs_active;
	size_t auto_cs_remain;
	uint8_t mosi_byte, miso_byte;
	uint8_t mosi_fixed_value;
	gboolean mosi_is_fixed;
	uint8_t miso_fixed_value;
	gboolean miso_is_fixed;
};

struct i2c_proto_context_t {
	size_t ack_remain;
};

struct context;

struct proto_handler_t {
	const char *name;
	struct {
		uint64_t samplerate;
		uint64_t bitrate;
		const char *frame_format;
		enum textinput_t textinput;
	} dflt;
	struct {
		size_t count;
		const char **names;
	} chans;
	size_t priv_size;
	int (*check_opts)(struct context *inc);
	int (*config_frame)(struct context *inc);
	int (*proc_pseudo)(struct sr_input *in, char *text);
	int (*proc_value)(struct context *inc, uint32_t value);
	int (*get_idle_capture)(struct context *inc,
		size_t *bits, uint8_t *lvls);
	int (*get_idle_interframe)(struct context *inc,
		size_t *samples, uint8_t *lvls);
};

struct context {
	/* User provided options. */
	struct user_opts_t {
		uint64_t samplerate;
		uint64_t bitrate;
		const char *proto_name;
		const char *fmt_text;
		enum textinput_t textinput;
	} user_opts;
	/* Derived at runtime. */
	struct {
		uint64_t samplerate;
		uint64_t bitrate;
		uint64_t samples_per_bit;
		char *proto_name;
		char *fmt_text;
		enum textinput_t textinput;
		enum proto_type_t {
			PROTO_TYPE_NONE,
			PROTO_TYPE_UART,
			PROTO_TYPE_SPI,
			PROTO_TYPE_I2C,
			PROTO_TYPE_COUNT,
		} protocol_type;
		const struct proto_handler_t *prot_hdl;
		void *prot_priv;
		union {
			struct uart_frame_fmt_opts {
				size_t databit_count;
				enum {
					UART_PARITY_NONE,
					UART_PARITY_ODD,
					UART_PARITY_EVEN,
				} parity_type;
				size_t stopbit_count;
				gboolean half_stopbit;
				gboolean inverted;
			} uart;
			struct spi_frame_fmt_opts {
				uint8_t cs_polarity;
				size_t databit_count;
				gboolean msb_first;
				gboolean spi_mode_cpol;
				gboolean spi_mode_cpha;
			} spi;
			struct i2c_frame_fmt_opts {
				gboolean addr_10bit;
			} i2c;
		} frame_format;
	} curr_opts;
	/* Module stage. Logic output channels. Session feed. */
	gboolean scanned_magic;
	gboolean has_magic;
	gboolean has_header;
	gboolean got_header;
	gboolean started;
	gboolean meta_sent;
	size_t channel_count;
	const char **channel_names;
	struct feed_queue_logic *feed_logic;
	/*
	 * Internal state: Allocated space for a theoretical maximum
	 * bit count. Filled in bit pattern for the current data value.
	 * (Stuffing can result in varying bit counts across frames.)
	 *
	 * Keep the bits' width in sample numbers, as well as the bits'
	 * boundaries relative to the start of the protocol frame's
	 * start. Support a number of logic bits per bit time.
	 *
	 * Implementor's note: Due to development history terminology
	 * might slip here. Strictly speaking it's "waveform sections"
	 * that hold samples for a given number of cycles. "A bit" in
	 * the protocol can occupy multiple of these slots to e.g. have
	 * a synchronous clock, or to present setup and hold phases,
	 * etc. Sample data spans several logic signal traces. You get
	 * the idea ...
	 */
	size_t max_frame_bits;	/* Reserved. */
	size_t top_frame_bits;	/* Currently filled. */
	struct {
		size_t mul;
		size_t div;
	} *bit_scale;		/* Quanta scaling. */
	size_t *sample_edges;
	size_t *sample_widths;
	uint8_t *sample_levels;	/* Sample data, logic traces. */
	/* Common support for samples updating by manipulation. */
	struct {
		uint8_t idle_levels;
		uint8_t curr_levels;
	} samples;
	/* Internal state of the input text reader. */
	struct {
		int base;
	} read_text;
	/* Manage state across .reset() calls. Robustness. */
	struct proto_prev {
		GSList *sr_channels;
		GSList *sr_groups;
	} prev;
};

/* {{{ frame bits manipulation, waveform construction */

/*
 * Primitives to construct waveforms for a protocol frame, by sequencing
 * samples after data values were seen in the input stream. Individual
 * protocol handlers will use these common routines.
 *
 * The general idea is: The protocol handler's options parser determines
 * the frame format, and derives the maximum number of time slots needed
 * to represent the waveform. Slots can scale differintly, proportions
 * get configured once during initialization. All remaining operation
 * receives arbitrarily interleaved data values and pseudo comments, uses
 * the pre-allocated and pre-scaled time slots to construct waveforms,
 * which then get sent to the session bus as if an acquisition device
 * had captured wire traffic. For clocked signals the "coarse" timing
 * should never be an issue. Protocol handlers are free to use as many
 * time slots per bit time as they please or feel necessary.
 */

static int alloc_frame_storage(struct context *inc)
{
	size_t bits, alloc;

	if (!inc)
		return SR_ERR_ARG;

	if (!inc->max_frame_bits)
		return SR_ERR_DATA;

	inc->top_frame_bits = 0;
	bits = inc->max_frame_bits;

	alloc = bits * sizeof(inc->sample_edges[0]);
	inc->sample_edges = g_malloc0(alloc);
	alloc = bits * sizeof(inc->sample_widths[0]);
	inc->sample_widths = g_malloc0(alloc);
	alloc = bits * sizeof(inc->sample_levels[0]);
	inc->sample_levels = g_malloc0(alloc);
	if (!inc->sample_edges || !inc->sample_widths || !inc->sample_levels)
		return SR_ERR_MALLOC;

	alloc = bits * sizeof(inc->bit_scale[0]);
	inc->bit_scale = g_malloc0(alloc);
	if (!inc->bit_scale)
		return SR_ERR_MALLOC;

	return SR_OK;
}

/*
 * Assign an equal bit width to all bits in the frame. Derive the width
 * from the bitrate and the sampelrate. Protocol handlers optionally can
 * arrange for "odd bit widths" (either fractions, or multiples, or when
 * desired any rational at all). Think half-bits, or think quanta within
 * a bit time, depends on the protocol handler really.
 *
 * Implementation note: The input module assumes that the position of
 * odd length bits will never vary during frame construction. The total
 * length may vary, 'top' can be smaller than 'max' in every iteration.
 * It is assumed that frames with odd-length bits have constant layout,
 * and that stuffing protocols have same-width bits. Odd lengths also
 * can support bit time quanta, while it's assumed that these always use
 * the same layout for all generated frames. This constraint is kept in
 * the implementation, until one of the supported protocols genuinely
 * requires higher flexibility and the involved complexity and runtime
 * cost of per-samplepoint adjustment.
 */
static int assign_bit_widths(struct context *inc)
{
	const struct proto_handler_t *handler;
	int ret;
	double bit_edge, bit_time, this_bit_time;
	uint64_t bit_time_int, bit_time_prev, bit_times_total;
	size_t idx;

	if (!inc)
		return SR_ERR_ARG;

	/*
	 * Run the protocol handler's optional configure routine.
	 * It derives the maximum number of "bit slots" that are needed
	 * to represent a protocol frame's waveform.
	 */
	handler = inc->curr_opts.prot_hdl;
	if (handler && handler->config_frame) {
		ret = handler->config_frame(inc);
		if (ret != SR_OK)
			return ret;
	}

	/* Assign bit widths to the protocol frame's bit positions. */
	bit_time = inc->curr_opts.samplerate;
	bit_time /= inc->curr_opts.bitrate;
	inc->curr_opts.samples_per_bit = bit_time + 0.5;
	sr_dbg("Samplerate %" PRIu64 ", bitrate %" PRIu64 ".",
		inc->curr_opts.samplerate, inc->curr_opts.bitrate);
	sr_dbg("Resulting bit width %.2f samples, int %" PRIu64 ".",
		bit_time, inc->curr_opts.samples_per_bit);
	bit_edge = 0.0;
	bit_time_prev = 0;
	bit_times_total = 0;
	for (idx = 0; idx < inc->max_frame_bits; idx++) {
		this_bit_time = bit_time;
		if (inc->bit_scale[idx].mul)
			this_bit_time *= inc->bit_scale[idx].mul;
		if (inc->bit_scale[idx].div)
			this_bit_time /= inc->bit_scale[idx].div;
		bit_edge += this_bit_time;
		bit_time_int = (uint64_t)(bit_edge + 0.5);
		inc->sample_edges[idx] = bit_time_int;
		bit_time_int -= bit_time_prev;
		inc->sample_widths[idx] = bit_time_int;
		bit_time_prev = inc->sample_edges[idx];
		bit_times_total += bit_time_int;
		sr_spew("Bit %zu, width %" PRIu64 ".", idx, bit_time_int);
	}
	sr_dbg("Maximum waveform width: %zu slots, %.2f / %" PRIu64 " samples.",
		inc->max_frame_bits, bit_edge, bit_times_total);

	return SR_OK;
}

/* Start accumulating the samples for a new part of the waveform. */
static int wave_clear_sequence(struct context *inc)
{

	if (!inc)
		return SR_ERR_ARG;

	inc->top_frame_bits = 0;

	return SR_OK;
}

/* Append channels' levels to the waveform for another period of samples. */
static int wave_append_pattern(struct context *inc, uint8_t sample)
{

	if (!inc)
		return SR_ERR_ARG;

	if (inc->top_frame_bits >= inc->max_frame_bits)
		return SR_ERR_DATA;

	inc->sample_levels[inc->top_frame_bits++] = sample;

	return SR_OK;
}

/* Initially assign idle levels, start the buffer from idle state. */
static void sample_buffer_preset(struct context *inc, uint8_t idle_sample)
{
	inc->samples.idle_levels = idle_sample;
	inc->samples.curr_levels = idle_sample;
}

/* Modify the samples buffer by assigning a given traces state. */
static void sample_buffer_assign(struct context *inc, uint8_t sample)
{
	inc->samples.curr_levels = sample;
}

/* Modify the samples buffer by changing individual traces. */
static void sample_buffer_modify(struct context *inc,
	uint8_t set_mask, uint8_t clr_mask)
{
	inc->samples.curr_levels |= set_mask;
	inc->samples.curr_levels &= ~clr_mask;
}

static void sample_buffer_raise(struct context *inc, uint8_t bits)
{
	return sample_buffer_modify(inc, bits, 0);
}

static void sample_buffer_clear(struct context *inc, uint8_t bits)
{
	return sample_buffer_modify(inc, 0, bits);
}

static void sample_buffer_setclr(struct context *inc,
	gboolean level, uint8_t mask)
{
	if (level)
		sample_buffer_raise(inc, mask);
	else
		sample_buffer_clear(inc, mask);
}

static void sample_buffer_toggle(struct context *inc, uint8_t mask)
{
	inc->samples.curr_levels ^= mask;
}

/* Reset current sample buffer to idle state. */
static void sample_buffer_toidle(struct context *inc)
{
	inc->samples.curr_levels = inc->samples.idle_levels;
}

/* Append the buffered samples to the waveform memory. */
static int wave_append_buffer(struct context *inc)
{
	return wave_append_pattern(inc, inc->samples.curr_levels);
}

/* Send idle level before the first generated frame and at end of capture. */
static int send_idle_capture(struct context *inc)
{
	const struct proto_handler_t *handler;
	size_t count;
	uint8_t data;
	int ret;

	handler = inc->curr_opts.prot_hdl;
	if (!handler->get_idle_capture)
		return SR_OK;

	ret = handler->get_idle_capture(inc, &count, &data);
	if (ret != SR_OK)
		return ret;
	count *= inc->curr_opts.samples_per_bit;
	ret = feed_queue_logic_submit_one(inc->feed_logic, &data, count);
	if (ret != SR_OK)
		return ret;

	return SR_OK;
}

/* Optionally send idle level between protocol frames. */
static int send_idle_interframe(struct context *inc)
{
	const struct proto_handler_t *handler;
	size_t count;
	uint8_t data;
	int ret;

	handler = inc->curr_opts.prot_hdl;
	if (!handler->get_idle_interframe)
		return SR_OK;

	ret = handler->get_idle_interframe(inc, &count, &data);
	if (ret != SR_OK)
		return ret;
	ret = feed_queue_logic_submit_one(inc->feed_logic, &data, count);
	if (ret != SR_OK)
		return ret;

	return SR_OK;
}

/* Forward the previously accumulated samples of the waveform. */
static int send_frame(struct sr_input *in)
{
	struct context *inc;
	size_t count, index;
	uint8_t data;
	int ret;

	inc = in->priv;

	for (index = 0; index < inc->top_frame_bits; index++) {
		data = inc->sample_levels[index];
		count = inc->sample_widths[index];
		ret = feed_queue_logic_submit_one(inc->feed_logic,
			&data, count);
		if (ret != SR_OK)
			return ret;
	}

	return SR_OK;
}

/* }}} frame bits manipulation */
/* {{{ UART protocol handler */

enum uart_pin_t {
	UART_PIN_RXTX,
};

#define UART_PINMASK_RXTX	(1UL << UART_PIN_RXTX)

/* UART specific options and frame format check. */
static int uart_check_opts(struct context *inc)
{
	struct uart_frame_fmt_opts *fmt_opts;
	const char *fmt_text;
	char **opts, *opt;
	size_t opt_count, opt_idx;
	int ret;
	unsigned long v;
	char par_text;
	char *endp;
	size_t total_bits;

	if (!inc)
		return SR_ERR_ARG;
	fmt_opts = &inc->curr_opts.frame_format.uart;

	/* Apply defaults before reading external spec. */
	memset(fmt_opts, 0, sizeof(*fmt_opts));
	fmt_opts->databit_count = 8;
	fmt_opts->parity_type = UART_PARITY_NONE;
	fmt_opts->stopbit_count = 1;
	fmt_opts->half_stopbit = FALSE;
	fmt_opts->inverted = FALSE;

	/* Provide a default UART frame format. */
	fmt_text = inc->curr_opts.fmt_text;
	if (!fmt_text || !*fmt_text)
		fmt_text = UART_DFLT_FRAMEFMT;
	sr_dbg("UART frame format: %s.", fmt_text);

	/* Parse the comma separated list of user provided options. */
	opts = g_strsplit_set(fmt_text, ", ", 0);
	opt_count = g_strv_length(opts);
	for (opt_idx = 0; opt_idx < opt_count; opt_idx++) {
		opt = opts[opt_idx];
		if (!opt || !*opt)
			continue;
		sr_spew("UART format option: %s", opt);
		/*
		 * Check for specific keywords. Before falling back to
		 * attempting the "8n1" et al interpretation.
		 */
		if (strcmp(opt, UART_FORMAT_INVERT) == 0) {
			fmt_opts->inverted = TRUE;
			continue;
		}
		/* Parse an "8n1", "8e2", "7o1", or similar input spec. */
		/* Get the data bits count. */
		endp = NULL;
		ret = sr_atoul_base(opt, &v, &endp, 10);
		if (ret != SR_OK || !endp)
			return SR_ERR_DATA;
		opt = endp;
		if (v < UART_MIN_DATABITS || v > UART_MAX_DATABITS)
			return SR_ERR_DATA;
		fmt_opts->databit_count = v;
		/* Get the parity type. */
		par_text = tolower((int)*opt++);
		switch (par_text) {
		case 'n':
			fmt_opts->parity_type = UART_PARITY_NONE;
			break;
		case 'o':
			fmt_opts->parity_type = UART_PARITY_ODD;
			break;
		case 'e':
			fmt_opts->parity_type = UART_PARITY_EVEN;
			break;
		default:
			return SR_ERR_DATA;
		}
		/* Get the stop bits count. Supports half bits too. */
		endp = NULL;
		ret = sr_atoul_base(opt, &v, &endp, 10);
		if (ret != SR_OK || !endp)
			return SR_ERR_DATA;
		opt = endp;
		if (v > UART_MAX_STOPBITS)
			return SR_ERR_DATA;
		fmt_opts->stopbit_count = v;
		if (g_ascii_strcasecmp(opt, ".5") == 0) {
			opt += strlen(".5");
			fmt_opts->half_stopbit = TRUE;
		}
		/* Incomplete consumption of input text is fatal. */
		if (*opt) {
			sr_err("Unprocessed frame format remainder: %s.", opt);
			return SR_ERR_DATA;
		}
		continue;
	}
	g_strfreev(opts);

	/*
	 * Calculate the total number of bit times in the UART frame.
	 * Add a few more bit times to the reserved space. They usually
	 * are not occupied during data transmission, but are useful to
	 * have for special symbols (BREAK, IDLE).
	 */
	total_bits = 1; /* START bit, unconditional. */
	total_bits += fmt_opts->databit_count;
	total_bits += (fmt_opts->parity_type != UART_PARITY_NONE) ? 1 : 0;
	total_bits += fmt_opts->stopbit_count;
	total_bits += fmt_opts->half_stopbit ? 1 : 0;
	total_bits += UART_ADD_IDLEBITS;
	sr_dbg("UART frame: total bits %zu.", total_bits);
	if (total_bits > UART_MAX_WAVELEN)
		return SR_ERR_DATA;
	inc->max_frame_bits = total_bits;

	return SR_OK;
}

/*
 * Configure the frame's bit widths when not identical across the
 * complete frame. Think half STOP bits.
 * Preset the sample data for an idle bus.
 */
static int uart_config_frame(struct context *inc)
{
	struct uart_frame_fmt_opts *fmt_opts;
	size_t bit_idx;
	uint8_t sample;

	if (!inc)
		return SR_ERR_ARG;
	fmt_opts = &inc->curr_opts.frame_format.uart;

	/*
	 * Position after the START bit. Advance over DATA, PARITY and
	 * (full) STOP bits. Then set the trailing STOP bit to half if
	 * needed. Make the trailing IDLE period after a UART frame
	 * wider than regular bit times. Add an even wider IDLE period
	 * which is used for special symbols.
	 */
	bit_idx = 1;
	bit_idx += fmt_opts->databit_count;
	bit_idx += (fmt_opts->parity_type == UART_PARITY_NONE) ? 0 : 1;
	bit_idx += fmt_opts->stopbit_count;
	if (fmt_opts->half_stopbit) {
		sr_dbg("Setting bit index %zu to half width.", bit_idx);
		inc->bit_scale[bit_idx].div = 2;
		bit_idx++;
	}
	inc->bit_scale[bit_idx++].mul = 2;
	inc->bit_scale[bit_idx++].mul = 4;

	/* Start from idle signal levels (high when not inverted). */
	sample = 0;
	if (!fmt_opts->inverted)
		sample |= UART_PINMASK_RXTX;
	sample_buffer_preset(inc, sample);

	return SR_OK;
}

/* Create samples for a special UART frame (IDLE, BREAK). */
static int uart_write_special(struct context *inc, uint8_t level)
{
	struct uart_frame_fmt_opts *fmt_opts;
	int ret;
	size_t bits;

	if (!inc)
		return SR_ERR_ARG;
	fmt_opts = &inc->curr_opts.frame_format.uart;

	ret = wave_clear_sequence(inc);
	if (ret != SR_OK)
		return ret;

	/*
	 * Set the same level for all bit slots, covering all of
	 * START and DATA (and PARITY) and STOP. This allows the
	 * simulation of BREAK and IDLE phases.
	 */
	if (fmt_opts->inverted)
		level = !level;
	sample_buffer_setclr(inc, level, UART_PINMASK_RXTX);
	bits = 1; /* START */
	bits += fmt_opts->databit_count;
	bits += (fmt_opts->parity_type != UART_PARITY_NONE) ? 1 : 0;
	bits += fmt_opts->stopbit_count;
	bits += fmt_opts->half_stopbit ? 1 : 0;
	while (bits--) {
		ret = wave_append_buffer(inc);
		if (ret != SR_OK)
			return ret;
	}

	/*
	 * Force a few more idle bit times. This does not affect a
	 * caller requested IDLE symbol. But helps separate (i.e.
	 * robustly detect) several caller requested BREAK symbols.
	 * Also separates those specials from subsequent data bytes.
	 */
	sample_buffer_toidle(inc);
	bits = UART_ADD_IDLEBITS;
	while (bits--) {
		ret = wave_append_buffer(inc);
		if (ret != SR_OK)
			return ret;
	}

	return SR_OK;
}

/* Process UART protocol specific pseudo comments. */
static int uart_proc_pseudo(struct sr_input *in, char *line)
{
	struct context *inc;
	char *word;
	int ret;

	inc = in->priv;

	while (line) {
		word = sr_text_next_word(line, &line);
		if (!word)
			break;
		if (!*word)
			continue;
		if (strcmp(word, UART_PSEUDO_BREAK) == 0) {
			ret = uart_write_special(inc, 0);
			if (ret != SR_OK)
				return ret;
			ret = send_frame(in);
			if (ret != SR_OK)
				return ret;
			continue;
		}
		if (strcmp(word, UART_PSEUDO_IDLE) == 0) {
			ret = uart_write_special(inc, 1);
			if (ret != SR_OK)
				return ret;
			ret = send_frame(in);
			if (ret != SR_OK)
				return ret;
			continue;
		}
		return SR_ERR_DATA;
	}

	return SR_OK;
}

/*
 * Create the UART frame's waveform for the given data value.
 *
 * In theory the protocol handler could setup START and STOP once during
 * initialization. But the overhead compares to DATA and PARITY is small.
 * And unconditional START/STOP would break the creation of BREAK and
 * IDLE frames, or complicate their construction and recovery afterwards.
 * A future implementation might as well support UART traffic on multiple
 * traces, including interleaved bidirectional communication. So let's
 * keep the implementation simple. Execution time is not a priority.
 */
static int uart_proc_value(struct context *inc, uint32_t value)
{
	struct uart_frame_fmt_opts *fmt_opts;
	int ret;
	size_t bits;
	int par_bit, data_bit;

	if (!inc)
		return SR_ERR_ARG;
	fmt_opts = &inc->curr_opts.frame_format.uart;

	ret = wave_clear_sequence(inc);
	if (ret != SR_OK)
		return ret;

	/* START bit, unconditional, always 0. */
	sample_buffer_clear(inc, UART_PINMASK_RXTX);
	if (fmt_opts->inverted)
		sample_buffer_toggle(inc, UART_PINMASK_RXTX);
	ret = wave_append_buffer(inc);

	/* DATA bits. Track parity here (unconditionally). */
	par_bit = 0;
	bits = fmt_opts->databit_count;
	while (bits--) {
		data_bit = value & 0x01;
		value >>= 1;
		par_bit ^= data_bit;
		if (fmt_opts->inverted)
			data_bit = !data_bit;
		sample_buffer_setclr(inc, data_bit, UART_PINMASK_RXTX);
		ret = wave_append_buffer(inc);
		if (ret != SR_OK)
			return ret;
	}

	/* PARITY bit. Emission is optional. */
	switch (fmt_opts->parity_type) {
	case UART_PARITY_ODD:
		data_bit = par_bit ? 0 : 1;
		bits = 1;
		break;
	case UART_PARITY_EVEN:
		data_bit = par_bit ? 1 : 0;
		bits = 1;
		break;
	default:
		data_bit = 0;
		bits = 0;
		break;
	}
	if (bits) {
		if (fmt_opts->inverted)
			data_bit = !data_bit;
		sample_buffer_setclr(inc, data_bit, UART_PINMASK_RXTX);
		ret = wave_append_buffer(inc);
		if (ret != SR_OK)
			return ret;
	}

	/* STOP bits. Optional. */
	sample_buffer_raise(inc, UART_PINMASK_RXTX);
	if (fmt_opts->inverted)
		sample_buffer_toggle(inc, UART_PINMASK_RXTX);
	bits = fmt_opts->stopbit_count;
	bits += fmt_opts->half_stopbit ? 1 : 0;
	while (bits--) {
		ret = wave_append_buffer(inc);
		if (ret != SR_OK)
			return ret;
	}

	/*
	 * Force some idle time after the UART frame.
	 * A little shorter than for special symbols.
	 */
	sample_buffer_toidle(inc);
	bits = UART_ADD_IDLEBITS - 1;
	while (bits--) {
		ret = wave_append_buffer(inc);
		if (ret != SR_OK)
			return ret;
	}

	return SR_OK;
}

/* Start/end the logic trace with a few bit times of idle level. */
static int uart_get_idle_capture(struct context *inc,
	size_t *bitcount, uint8_t *sample)
{

	/* Describe a UART frame's length of idle level. */
	if (bitcount)
		*bitcount = inc->max_frame_bits;
	if (sample)
		*sample = inc->samples.idle_levels;
	return SR_OK;
}

/* Arrange for a few samples of idle level between UART frames. */
static int uart_get_idle_interframe(struct context *inc,
	size_t *samplecount, uint8_t *sample)
{

	(void)inc;

	/*
	 * Regular waveform creation for UART frames already includes
	 * padding between UART frames. That is why we don't need to
	 * add extra inter-frame samples. Yet prepare the implementation
	 * for when we need or want to add a few more idle samples.
	 */
	if (samplecount) {
		*samplecount = inc->curr_opts.samples_per_bit;
		*samplecount *= 0;
	}
	if (sample)
		*sample = inc->samples.idle_levels;
	return SR_OK;
}

/* }}} UART protocol handler */
/* {{{ SPI protocol handler */

enum spi_pin_t {
	SPI_PIN_SCK,
	SPI_PIN_MISO,
	SPI_PIN_MOSI,
	SPI_PIN_CS,
	SPI_PIN_COUNT,
};

#define SPI_PINMASK_SCK		(1UL << SPI_PIN_SCK)
#define SPI_PINMASK_MISO	(1UL << SPI_PIN_MISO)
#define SPI_PINMASK_MOSI	(1UL << SPI_PIN_MOSI)
#define SPI_PINMASK_CS		(1UL << SPI_PIN_CS)

/* "Forget" data which was seen before. */
static void spi_value_discard_prev_data(struct context *inc)
{
	struct spi_proto_context_t *incs;

	incs = inc->curr_opts.prot_priv;
	incs->has_mosi = !incs->needs_mosi;
	incs->has_miso = !incs->needs_miso;
	incs->mosi_byte = 0;
	incs->miso_byte = 0;
}

/* Check whether all required values for the byte time were seen. */
static gboolean spi_value_is_bytes_complete(struct context *inc)
{
	struct spi_proto_context_t *incs;

	incs = inc->curr_opts.prot_priv;

	return incs->has_mosi && incs->has_miso;
}

/* Arrange for data reception before waveform emission. */
static void spi_pseudo_data_order(struct context *inc,
	gboolean needs_mosi, gboolean needs_miso, gboolean mosi_first)
{
	struct spi_proto_context_t *incs;

	incs = inc->curr_opts.prot_priv;

	incs->needs_mosi = needs_mosi;
	incs->needs_miso = needs_miso;
	incs->mosi_first = mosi_first;
	if (needs_mosi)
		incs->mosi_is_fixed = FALSE;
	if (needs_miso)
		incs->miso_is_fixed = FALSE;
	spi_value_discard_prev_data(inc);
}

static void spi_pseudo_mosi_fixed(struct context *inc, uint8_t v)
{
	struct spi_proto_context_t *incs;

	incs = inc->curr_opts.prot_priv;

	incs->mosi_fixed_value = v;
	incs->mosi_is_fixed = TRUE;
}

static void spi_pseudo_miso_fixed(struct context *inc, uint8_t v)
{
	struct spi_proto_context_t *incs;

	incs = inc->curr_opts.prot_priv;

	incs->miso_fixed_value = v;
	incs->miso_is_fixed = TRUE;
}

/* Explicit CS control. Arrange for next CS level, track state to keep it. */
static void spi_pseudo_select_control(struct context *inc, gboolean cs_active)
{
	struct spi_frame_fmt_opts *fmt_opts;
	struct spi_proto_context_t *incs;
	uint8_t cs_level, sck_level;

	fmt_opts = &inc->curr_opts.frame_format.spi;
	incs = inc->curr_opts.prot_priv;

	/* Track current "CS active" state. */
	incs->cs_active = cs_active;
	incs->auto_cs_remain = 0;

	/* Derive current "CS pin level". Update sample data buffer. */
	cs_level = 1 - fmt_opts->cs_polarity;
	if (incs->cs_active)
		cs_level = fmt_opts->cs_polarity;
	sample_buffer_setclr(inc, cs_level, SPI_PINMASK_CS);

	/* Derive the idle "SCK level" from the SPI mode's CPOL. */
	sck_level = fmt_opts->spi_mode_cpol ? 1 : 0;
	sample_buffer_setclr(inc, sck_level, SPI_PINMASK_SCK);
}

/* Arrange for automatic CS release after transfer length. Starts the phase. */
static void spi_pseudo_auto_select(struct context *inc, size_t length)
{
	struct spi_frame_fmt_opts *fmt_opts;
	struct spi_proto_context_t *incs;
	uint8_t cs_level;

	fmt_opts = &inc->curr_opts.frame_format.spi;
	incs = inc->curr_opts.prot_priv;

	/* Track current "CS active" state. */
	incs->cs_active = TRUE;
	incs->auto_cs_remain = length;

	/* Derive current "CS pin level". Update sample data buffer. */
	cs_level = 1 - fmt_opts->cs_polarity;
	if (incs->cs_active)
		cs_level = fmt_opts->cs_polarity;
	sample_buffer_setclr(inc, cs_level, SPI_PINMASK_CS);
}

/* Check for automatic CS release. Decrements, yields result. No action here. */
static gboolean spi_auto_select_ends(struct context *inc)
{
	struct spi_proto_context_t *incs;

	incs = inc->curr_opts.prot_priv;
	if (!incs->auto_cs_remain)
		return FALSE;

	incs->auto_cs_remain--;
	if (incs->auto_cs_remain)
		return FALSE;

	/*
	 * DON'T release CS yet. The last data is yet to get sent.
	 * Keep the current "CS pin level", but tell the caller that
	 * CS will be released after transmission of that last data.
	 */
	return TRUE;
}

/* Update for automatic CS release after last data was sent. */
static void spi_auto_select_update(struct context *inc)
{
	struct spi_frame_fmt_opts *fmt_opts;
	struct spi_proto_context_t *incs;
	uint8_t cs_level;

	fmt_opts = &inc->curr_opts.frame_format.spi;
	incs = inc->curr_opts.prot_priv;

	/* Track current "CS active" state. */
	incs->cs_active = FALSE;
	incs->auto_cs_remain = 0;

	/* Derive current "CS pin level". Map to bits pattern. */
	cs_level = 1 - fmt_opts->cs_polarity;
	sample_buffer_setclr(inc, cs_level, SPI_PINMASK_CS);
}

/*
 * Create the waveforms for one SPI byte. Also cover idle periods:
 * Dummy/padding bytes within a frame with clock. Idle lines outside
 * of frames without clock edges. Optional automatic CS release with
 * resulting inter-frame gap.
 */
static int spi_write_frame_patterns(struct context *inc,
	gboolean idle, gboolean cs_release)
{
	struct spi_proto_context_t *incs;
	struct spi_frame_fmt_opts *fmt_opts;
	int ret;
	uint8_t mosi_bit, miso_bit;
	size_t bits;

	if (!inc)
		return SR_ERR_ARG;
	incs = inc->curr_opts.prot_priv;
	fmt_opts = &inc->curr_opts.frame_format.spi;

	/* Apply fixed values before drawing the waveform. */
	if (incs->mosi_is_fixed)
		incs->mosi_byte = incs->mosi_fixed_value;
	if (incs->miso_is_fixed)
		incs->miso_byte = incs->miso_fixed_value;

	ret = wave_clear_sequence(inc);
	if (ret != SR_OK)
		return ret;

	/* Provide two samples with idle SCK and current CS. */
	ret = wave_append_buffer(inc);
	if (ret != SR_OK)
		return ret;
	ret = wave_append_buffer(inc);
	if (ret != SR_OK)
		return ret;

	/*
	 * Provide two samples per DATABIT time slot. Keep CS as is.
	 * Toggle SCK according to CPHA specs. Shift out MOSI and MISO
	 * in the configured order.
	 *
	 * Force dummy MOSI/MISO bits for idle bytes within a frame.
	 * Skip SCK toggling for idle "frames" outside of active CS.
	 */
	bits = fmt_opts->databit_count;
	while (bits--) {
		/*
		 * First half-period. Provide next DATABIT values.
		 * Toggle SCK here when CPHA is set.
		 */
		if (fmt_opts->msb_first) {
			mosi_bit = incs->mosi_byte & 0x80;
			miso_bit = incs->miso_byte & 0x80;
			incs->mosi_byte <<= 1;
			incs->miso_byte <<= 1;
		} else {
			mosi_bit = incs->mosi_byte & 0x01;
			miso_bit = incs->miso_byte & 0x01;
			incs->mosi_byte >>= 1;
			incs->miso_byte >>= 1;
		}
		if (incs->cs_active && !idle) {
			sample_buffer_setclr(inc, mosi_bit, SPI_PINMASK_MOSI);
			sample_buffer_setclr(inc, miso_bit, SPI_PINMASK_MISO);
		}
		if (fmt_opts->spi_mode_cpha && incs->cs_active)
			sample_buffer_toggle(inc, SPI_PINMASK_SCK);
		ret = wave_append_buffer(inc);
		if (ret != SR_OK)
			return ret;
		/* Second half-period. Keep DATABIT, toggle SCK. */
		if (incs->cs_active)
			sample_buffer_toggle(inc, SPI_PINMASK_SCK);
		ret = wave_append_buffer(inc);
		if (ret != SR_OK)
			return ret;
		/* Toggle SCK again unless done above due to CPHA. */
		if (!fmt_opts->spi_mode_cpha && incs->cs_active)
			sample_buffer_toggle(inc, SPI_PINMASK_SCK);
	}

	/*
	 * Hold the waveform for another sample period. Happens to
	 * also communicate the most recent SCK pin level.
	 *
	 * Optionally auto-release the CS signal after sending the
	 * last data byte. Update the CS trace's level. Add another
	 * (long) bit slot to present an inter-frame gap.
	 */
	ret = wave_append_buffer(inc);
	if (ret != SR_OK)
		return ret;
	if (cs_release)
		spi_auto_select_update(inc);
	ret = wave_append_buffer(inc);
	if (ret != SR_OK)
		return ret;
	if (cs_release) {
		ret = wave_append_buffer(inc);
		if (ret != SR_OK)
			return ret;
	}

	return SR_OK;
}

/* SPI specific options and frame format check. */
static int spi_check_opts(struct context *inc)
{
	struct spi_frame_fmt_opts *fmt_opts;
	const char *fmt_text;
	char **opts, *opt;
	size_t opt_count, opt_idx;
	int ret;
	unsigned long v;
	char *endp;
	size_t total_bits;

	if (!inc)
		return SR_ERR_ARG;
	fmt_opts = &inc->curr_opts.frame_format.spi;

	/* Setup defaults before reading external specs. */
	fmt_opts->cs_polarity = 0;
	fmt_opts->databit_count = SPI_MIN_DATABITS;
	fmt_opts->msb_first = TRUE;
	fmt_opts->spi_mode_cpol = FALSE;
	fmt_opts->spi_mode_cpha = FALSE;

	/* Provide a default SPI frame format. */
	fmt_text = inc->curr_opts.fmt_text;
	if (!fmt_text || !*fmt_text)
		fmt_text = SPI_DFLT_FRAMEFMT;
	sr_dbg("SPI frame format: %s.", fmt_text);

	/* Accept comma separated key=value pairs of specs. */
	opts = g_strsplit_set(fmt_text, ", ", 0);
	opt_count = g_strv_length(opts);
	for (opt_idx = 0; opt_idx < opt_count; opt_idx++) {
		opt = opts[opt_idx];
		if (!opt || !*opt)
			continue;
		sr_spew("SPI format option: %s.", opt);
		if (strcmp(opt, SPI_FORMAT_CS_LOW) == 0) {
			sr_spew("SPI chip select: low.");
			fmt_opts->cs_polarity = 0;
			continue;
		}
		if (strcmp(opt, SPI_FORMAT_CS_HIGH) == 0) {
			sr_spew("SPI chip select: high.");
			fmt_opts->cs_polarity = 1;
			continue;
		}
		if (g_str_has_prefix(opt, SPI_FORMAT_DATA_BITS)) {
			opt += strlen(SPI_FORMAT_DATA_BITS);
			endp = NULL;
			ret = sr_atoul_base(opt, &v, &endp, 10);
			if (ret != SR_OK)
				return ret;
			if (!endp || *endp)
				return SR_ERR_ARG;
			sr_spew("SPI word size: %lu.", v);
			if (v < SPI_MIN_DATABITS || v > SPI_MAX_DATABITS)
				return SR_ERR_ARG;
			fmt_opts->databit_count = v;
			continue;
		}
		if (g_str_has_prefix(opt, SPI_FORMAT_SPI_MODE)) {
			opt += strlen(SPI_FORMAT_SPI_MODE);
			endp = NULL;
			ret = sr_atoul_base(opt, &v, &endp, 10);
			if (ret != SR_OK)
				return ret;
			if (!endp || *endp)
				return SR_ERR_ARG;
			sr_spew("SPI mode: %lu.", v);
			if (v > 3)
				return SR_ERR_ARG;
			fmt_opts->spi_mode_cpol = v & (1UL << 1);
			fmt_opts->spi_mode_cpha = v & (1UL << 0);
			continue;
		}
		if (g_str_has_prefix(opt, SPI_FORMAT_MODE_CPOL)) {
			opt += strlen(SPI_FORMAT_MODE_CPOL);
			endp = NULL;
			ret = sr_atoul_base(opt, &v, &endp, 10);
			if (ret != SR_OK)
				return ret;
			if (!endp || *endp)
				return SR_ERR_ARG;
			sr_spew("SPI cpol: %lu.", v);
			if (v > 1)
				return SR_ERR_ARG;
			fmt_opts->spi_mode_cpol = !!v;
			continue;
		}
		if (g_str_has_prefix(opt, SPI_FORMAT_MODE_CPHA)) {
			opt += strlen(SPI_FORMAT_MODE_CPHA);
			endp = NULL;
			ret = sr_atoul_base(opt, &v, &endp, 10);
			if (ret != SR_OK)
				return ret;
			if (!endp || *endp)
				return SR_ERR_ARG;
			sr_spew("SPI cpha: %lu.", v);
			if (v > 1)
				return SR_ERR_ARG;
			fmt_opts->spi_mode_cpha = !!v;
			continue;
		}
		if (strcmp(opt, SPI_FORMAT_MSB_FIRST) == 0) {
			sr_spew("SPI endianess: MSB first.");
			fmt_opts->msb_first = 1;
			continue;
		}
		if (strcmp(opt, SPI_FORMAT_LSB_FIRST) == 0) {
			sr_spew("SPI endianess: LSB first.");
			fmt_opts->msb_first = 0;
			continue;
		}
		return SR_ERR_ARG;
	}
	g_strfreev(opts);

	/*
	 * Get the total bit count. Add slack for CS control, and to
	 * visually separate bytes in frames. Multiply data bit count
	 * for the creation of two clock half-periods.
	 */
	total_bits = 2;
	total_bits += 2 * fmt_opts->databit_count;
	total_bits += 3;

	sr_dbg("SPI frame: total bits %zu.", total_bits);
	if (total_bits > SPI_MAX_WAVELEN)
		return SR_ERR_DATA;
	inc->max_frame_bits = total_bits;

	return SR_OK;
}

/*
 * Setup half-width slots for the two halves of a DATABIT time. Keep
 * the "decoration" (CS control) at full width. Setup a rather long
 * last slot for potential inter-frame gaps.
 *
 * Preset CS and SCK from their idle levels according to the frame format
 * configuration. So that idle times outside of SPI transfers are covered
 * with simple logic despite the protocol's flexibility.
 */
static int spi_config_frame(struct context *inc)
{
	struct spi_frame_fmt_opts *fmt_opts;
	size_t bit_idx, bit_count;

	if (!inc)
		return SR_ERR_ARG;
	fmt_opts = &inc->curr_opts.frame_format.spi;

	/* Configure DATABIT positions for half width (for clock period). */
	bit_idx = 2;
	bit_count = fmt_opts->databit_count;
	while (bit_count--) {
		inc->bit_scale[bit_idx + 0].div = 2;
		inc->bit_scale[bit_idx + 1].div = 2;
		bit_idx += 2;
	}
	bit_idx += 2;
	inc->bit_scale[bit_idx].mul = fmt_opts->databit_count;

	/*
	 * Seed the protocol handler's internal state before seeing
	 * first data values. To properly cover idle periods, and to
	 * operate correctly in the absence of pseudo comments.
	 *
	 * Use internal helpers for sample data initialization. Then
	 * grab the resulting pin levels as the idle state.
	 */
	spi_value_discard_prev_data(inc);
	spi_pseudo_data_order(inc, TRUE, TRUE, TRUE);
	spi_pseudo_select_control(inc, FALSE);
	sample_buffer_preset(inc, inc->samples.curr_levels);

	return SR_OK;
}

/*
 * Process protocol dependent pseudo comments. Can affect future frame
 * construction and submission, or can immediately emit "inter frame"
 * bit patterns like chip select control.
 */
static int spi_proc_pseudo(struct sr_input *in, char *line)
{
	struct context *inc;
	char *word, *endp;
	int ret;
	unsigned long v;

	inc = in->priv;

	while (line) {
		word = sr_text_next_word(line, &line);
		if (!word)
			break;
		if (!*word)
			continue;
		if (strcmp(word, SPI_PSEUDO_MOSI_ONLY) == 0) {
			sr_spew("SPI pseudo: MOSI only");
			spi_pseudo_data_order(inc, TRUE, FALSE, TRUE);
			continue;
		}
		if (g_str_has_prefix(word, SPI_PSEUDO_MOSI_FIXED)) {
			word += strlen(SPI_PSEUDO_MOSI_FIXED);
			endp = NULL;
			ret = sr_atoul_base(word, &v, &endp, inc->read_text.base);
			if (ret != SR_OK)
				return ret;
			if (!endp || *endp)
				return SR_ERR_ARG;
			sr_spew("SPI pseudo: MOSI fixed %lu", v);
			spi_pseudo_mosi_fixed(inc, v);
			continue;
		}
		if (strcmp(word, SPI_PSEUDO_MISO_ONLY) == 0) {
			sr_spew("SPI pseudo: MISO only");
			spi_pseudo_data_order(inc, FALSE, TRUE, FALSE);
			continue;
		}
		if (g_str_has_prefix(word, SPI_PSEUDO_MISO_FIXED)) {
			word += strlen(SPI_PSEUDO_MISO_FIXED);
			endp = NULL;
			ret = sr_atoul_base(word, &v, &endp, inc->read_text.base);
			if (ret != SR_OK)
				return ret;
			if (!endp || *endp)
				return SR_ERR_ARG;
			sr_spew("SPI pseudo: MISO fixed %lu", v);
			spi_pseudo_miso_fixed(inc, v);
			continue;
		}
		if (strcmp(word, SPI_PSEUDO_MOSI_MISO) == 0) {
			sr_spew("SPI pseudo: MOSI then MISO");
			spi_pseudo_data_order(inc, TRUE, TRUE, TRUE);
			continue;
		}
		if (strcmp(word, SPI_PSEUDO_MISO_MOSI) == 0) {
			sr_spew("SPI pseudo: MISO then MOSI");
			spi_pseudo_data_order(inc, TRUE, TRUE, FALSE);
			continue;
		}
		if (strcmp(word, SPI_PSEUDO_CS_ASSERT) == 0) {
			sr_spew("SPI pseudo: CS assert");
			spi_pseudo_select_control(inc, TRUE);
			continue;
		}
		if (strcmp(word, SPI_PSEUDO_CS_RELEASE) == 0) {
			sr_spew("SPI pseudo: CS release");
			/* Release CS. Force IDLE to display the pin change. */
			spi_pseudo_select_control(inc, FALSE);
			ret = spi_write_frame_patterns(inc, TRUE, FALSE);
			if (ret != SR_OK)
				return ret;
			ret = send_frame(in);
			if (ret != SR_OK)
				return ret;
			continue;
		}
		if (g_str_has_prefix(word, SPI_PSEUDO_CS_NEXT)) {
			word += strlen(SPI_PSEUDO_CS_NEXT);
			endp = NULL;
			ret = sr_atoul_base(word, &v, &endp, 0);
			if (ret != SR_OK)
				return ret;
			if (!endp || *endp)
				return SR_ERR_ARG;
			sr_spew("SPI pseudo: CS auto next %lu", v);
			spi_pseudo_auto_select(inc, v);
			continue;
		}
		if (strcmp(word, SPI_PSEUDO_IDLE) == 0) {
			sr_spew("SPI pseudo: idle");
			ret = spi_write_frame_patterns(inc, TRUE, FALSE);
			if (ret != SR_OK)
				return ret;
			ret = send_frame(in);
			if (ret != SR_OK)
				return ret;
			continue;
		}
		return SR_ERR_DATA;
	}

	return SR_OK;
}

/*
 * Create the frame's waveform for the given data value. For bidirectional
 * communication multiple routine invocations accumulate data bits, while
 * the last invocation completes the frame preparation.
 */
static int spi_proc_value(struct context *inc, uint32_t value)
{
	struct spi_proto_context_t *incs;
	gboolean taken;
	int ret;
	gboolean auto_cs_end;

	if (!inc)
		return SR_ERR_ARG;
	incs = inc->curr_opts.prot_priv;

	/*
	 * Discard previous data when we get here after having completed
	 * a previous frame. This roundtrip from filling in to clearing
	 * is required to have the caller emit the waveform that we have
	 * constructed after receiving data values.
	 */
	if (spi_value_is_bytes_complete(inc)) {
		sr_spew("SPI value: discarding previous data");
		spi_value_discard_prev_data(inc);
	}

	/*
	 * Consume the caller provided value. Apply data in the order
	 * that was configured before.
	 */
	taken = FALSE;
	if (!taken && incs->mosi_first && !incs->has_mosi) {
		sr_spew("SPI value: grabbing MOSI value");
		incs->mosi_byte = value & 0xff;
		incs->has_mosi = TRUE;
		taken = TRUE;
	}
	if (!taken && !incs->has_miso) {
		sr_spew("SPI value: grabbing MISO value");
		incs->miso_byte = value & 0xff;
		incs->has_miso = TRUE;
	}
	if (!taken && !incs->mosi_first && !incs->has_mosi) {
		sr_spew("SPI value: grabbing MOSI value");
		incs->mosi_byte = value & 0xff;
		incs->has_mosi = TRUE;
		taken = TRUE;
	}

	/*
	 * Generate the waveform when all data values in a byte time
	 * were seen (all MOSI and MISO including their being optional
	 * or fixed values).
	 *
	 * Optionally automatically release CS after a given number of
	 * data bytes, when requested by the input stream.
	 */
	if (!spi_value_is_bytes_complete(inc)) {
		sr_spew("SPI value: need more values");
		return +1;
	}
	auto_cs_end = spi_auto_select_ends(inc);
	sr_spew("SPI value: frame complete, drawing, auto CS %d", auto_cs_end);
	ret = spi_write_frame_patterns(inc, FALSE, auto_cs_end);
	if (ret != SR_OK)
		return ret;
	return 0;
}

/* Start/end the logic trace with a few bit times of idle level. */
static int spi_get_idle_capture(struct context *inc,
	size_t *bitcount, uint8_t *sample)
{

	/* Describe one byte time of idle level. */
	if (bitcount)
		*bitcount = inc->max_frame_bits;
	if (sample)
		*sample = inc->samples.idle_levels;
	return SR_OK;
}

/* Arrange for a few samples of idle level between UART frames. */
static int spi_get_idle_interframe(struct context *inc,
	size_t *samplecount, uint8_t *sample)
{

	/* Describe four bit times, re-use most recent pin levels. */
	if (samplecount) {
		*samplecount = inc->curr_opts.samples_per_bit;
		*samplecount *= 4;
	}
	if (sample)
		*sample = inc->samples.curr_levels;
	return SR_OK;
}

/* }}} SPI protocol handler */
/* {{{ I2C protocol handler */

enum i2c_pin_t {
	I2C_PIN_SCL,
	I2C_PIN_SDA,
	I2C_PIN_COUNT,
};

#define I2C_PINMASK_SCL		(1UL << I2C_PIN_SCL)
#define I2C_PINMASK_SDA		(1UL << I2C_PIN_SDA)

/* Arrange for automatic ACK for a given number of data bytes. */
static void i2c_auto_ack_start(struct context *inc, size_t count)
{
	struct i2c_proto_context_t *incs;

	incs = inc->curr_opts.prot_priv;
	incs->ack_remain = count;
}

/* Check whether automatic ACK is still applicable. Decrements. */
static gboolean i2c_auto_ack_avail(struct context *inc)
{
	struct i2c_proto_context_t *incs;

	incs = inc->curr_opts.prot_priv;
	if (!incs->ack_remain)
		return FALSE;

	if (incs->ack_remain--)
		return TRUE;
	return FALSE;
}

/* Occupy the slots where START/STOP would be. Keep current levels. */
static int i2c_write_nothing(struct context *inc)
{
	size_t reps;
	int ret;

	reps = I2C_BITTIME_QUANTA;
	while (reps--) {
		ret = wave_append_buffer(inc);
		if (ret != SR_OK)
			return ret;
	}

	return SR_OK;
}

/*
 * Construct a START symbol. Occupy a full bit time in the waveform.
 * Can also be used as REPEAT START due to its conservative signalling.
 *
 * Definition of START: Falling SDA while SCL is high.
 * Repeated START: A START without a preceeding STOP.
 */
static int i2c_write_start(struct context *inc)
{
	int ret;

	/*
	 * Important! Assumes that either SDA and SCL already are
	 * high (true when we come here from an idle bus). Or that
	 * SCL already is low before SDA potentially changes (this
	 * is true for preceeding START or REPEAT START or DATA BIT
	 * symbols).
	 *
	 * Implementation detail: This START implementation can be
	 * used for REPEAT START as well. The signalling sequence is
	 * conservatively done.
	 */

	/* Enforce SDA high. */
	sample_buffer_raise(inc, I2C_PINMASK_SDA);
	ret = wave_append_buffer(inc);
	if (ret != SR_OK)
		return ret;

	/* Enforce SCL high. */
	sample_buffer_raise(inc, I2C_PINMASK_SCL);
	ret = wave_append_buffer(inc);
	if (ret != SR_OK)
		return ret;

	/* Keep high SCL and high SDA for another period. */
	ret = wave_append_buffer(inc);
	if (ret != SR_OK)
		return ret;

	/* Falling SDA while SCL is high. */
	sample_buffer_clear(inc, I2C_PINMASK_SDA);
	ret = wave_append_buffer(inc);
	if (ret != SR_OK)
		return ret;

	/* Keep high SCL and low SDA for one more period. */
	ret = wave_append_buffer(inc);
	if (ret != SR_OK)
		return ret;

	/*
	 * Lower SCL here already. Which kind of prepares DATA BIT
	 * times (fits a data bit's start condition, does not harm).
	 * Improves back to back START and (repeated) START as well
	 * as STOP without preceeding DATA BIT.
	 */
	sample_buffer_clear(inc, I2C_PINMASK_SCL);
	ret = wave_append_buffer(inc);
	if (ret != SR_OK)
		return ret;

	return SR_OK;
}

/*
 * Construct a STOP symbol. Occupy a full bit time in the waveform.
 *
 * Definition of STOP: Rising SDA while SCL is high.
 */
static int i2c_write_stop(struct context *inc)
{
	int ret;

	/* Enforce SCL low before SDA changes. */
	sample_buffer_clear(inc, I2C_PINMASK_SCL);
	ret = wave_append_buffer(inc);
	if (ret != SR_OK)
		return ret;

	/* Enforce SDA low (can change while SCL is low). */
	sample_buffer_clear(inc, I2C_PINMASK_SDA);
	ret = wave_append_buffer(inc);
	if (ret != SR_OK)
		return ret;

	/* Rise SCL high while SDA is low. */
	sample_buffer_raise(inc, I2C_PINMASK_SCL);
	ret = wave_append_buffer(inc);
	if (ret != SR_OK)
		return ret;

	/* Keep high SCL and low SDA for another period. */
	ret = wave_append_buffer(inc);
	if (ret != SR_OK)
		return ret;

	/* Rising SDA. */
	sample_buffer_raise(inc, I2C_PINMASK_SDA);
	ret = wave_append_buffer(inc);
	if (ret != SR_OK)
		return ret;

	/* Keep high SCL and high SDA for one more periods. */
	ret = wave_append_buffer(inc);
	if (ret != SR_OK)
		return ret;

	return SR_OK;
}

/*
 * Construct a DATA BIT symbol. Occupy a full bit time in the waveform.
 *
 * SDA can change while SCL is low. SDA must be kept while SCL is high.
 */
static int i2c_write_bit(struct context *inc, uint8_t value)
{
	int ret;

	/* Enforce SCL low before SDA changes. */
	sample_buffer_clear(inc, I2C_PINMASK_SCL);
	ret = wave_append_buffer(inc);
	if (ret != SR_OK)
		return ret;

	/* Setup SDA pin level while SCL is low. */
	sample_buffer_setclr(inc, value, I2C_PINMASK_SDA);
	ret = wave_append_buffer(inc);
	if (ret != SR_OK)
		return ret;

	/* Rising SCL, starting SDA validity. */
	sample_buffer_raise(inc, I2C_PINMASK_SCL);
	ret = wave_append_buffer(inc);
	if (ret != SR_OK)
		return ret;

	/* Keep SDA level with high SCL for two more periods. */
	ret = wave_append_buffer(inc);
	if (ret != SR_OK)
		return ret;
	ret = wave_append_buffer(inc);
	if (ret != SR_OK)
		return ret;

	/* Falling SCL, terminates SDA validity. */
	sample_buffer_clear(inc, I2C_PINMASK_SCL);
	ret = wave_append_buffer(inc);
	if (ret != SR_OK)
		return ret;

	return SR_OK;
}

/* Create a waveform for the eight data bits and the ACK/NAK slot. */
static int i2c_write_byte(struct context *inc, uint8_t value, uint8_t ack)
{
	size_t bit_mask, bit_value;
	int ret;

	/* Keep an empty bit time before the data byte. */
	ret = i2c_write_nothing(inc);
	if (ret != SR_OK)
		return ret;

	/* Send 8 data bits, MSB first. */
	bit_mask = 0x80;
	while (bit_mask) {
		bit_value = value & bit_mask;
		bit_mask >>= 1;
		ret = i2c_write_bit(inc, bit_value);
		if (ret != SR_OK)
			return ret;
	}

	/* Send ACK, which is low active. NAK is recessive, high. */
	bit_value = !ack;
	ret = i2c_write_bit(inc, bit_value);
	if (ret != SR_OK)
		return ret;

	/* Keep an empty bit time after the data byte. */
	ret = i2c_write_nothing(inc);
	if (ret != SR_OK)
		return ret;

	return SR_OK;
}

/* Send slave address (7bit or 10bit, 1 or 2 bytes). Consumes one ACK. */
static int i2c_send_address(struct sr_input *in, uint16_t addr, gboolean read)
{
	struct context *inc;
	struct i2c_frame_fmt_opts *fmt_opts;
	gboolean with_ack;
	uint8_t addr_byte, rw_bit;
	int ret;

	inc = in->priv;
	fmt_opts = &inc->curr_opts.frame_format.i2c;

	addr &= 0x3ff;
	rw_bit = read ? 1 : 0;
	with_ack = i2c_auto_ack_avail(inc);

	if (!fmt_opts->addr_10bit) {
		/* 7 bit address, the simple case. */
		addr_byte = addr & 0x7f;
		addr_byte <<= 1;
		addr_byte |= rw_bit;
		sr_spew("I2C 7bit address, byte 0x%" PRIx8, addr_byte);
		ret = wave_clear_sequence(inc);
		if (ret != SR_OK)
			return ret;
		ret = i2c_write_byte(inc, addr_byte, with_ack);
		if (ret != SR_OK)
			return ret;
		ret = send_frame(in);
		if (ret != SR_OK)
			return ret;
	} else {
		/*
		 * 10 bit address, need to write two bytes: First byte
		 * with prefix 0xf0, upper most 2 address bits, and R/W.
		 * Second byte with lower 8 address bits.
		 */
		addr_byte = addr >> 8;
		addr_byte <<= 1;
		addr_byte |= 0xf0;
		addr_byte |= rw_bit;
		sr_spew("I2C 10bit address, byte 0x%" PRIx8, addr_byte);
		ret = wave_clear_sequence(inc);
		if (ret != SR_OK)
			return ret;
		ret = i2c_write_byte(inc, addr_byte, with_ack);
		if (ret != SR_OK)
			return ret;
		ret = send_frame(in);
		if (ret != SR_OK)
			return ret;

		addr_byte = addr & 0xff;
		sr_spew("I2C 10bit address, byte 0x%" PRIx8, addr_byte);
		ret = wave_clear_sequence(inc);
		if (ret != SR_OK)
			return ret;
		ret = i2c_write_byte(inc, addr_byte, with_ack);
		if (ret != SR_OK)
			return ret;
		ret = send_frame(in);
		if (ret != SR_OK)
			return ret;
	}

	return SR_OK;
}

/* I2C specific options and frame format check. */
static int i2c_check_opts(struct context *inc)
{
	struct i2c_frame_fmt_opts *fmt_opts;
	const char *fmt_text;
	char **opts, *opt;
	size_t opt_count, opt_idx;
	size_t total_bits;

	if (!inc)
		return SR_ERR_ARG;
	fmt_opts = &inc->curr_opts.frame_format.i2c;

	/* Apply defaults before reading external specs. */
	memset(fmt_opts, 0, sizeof(*fmt_opts));
	fmt_opts->addr_10bit = FALSE;

	/* Provide a default I2C frame format. */
	fmt_text = inc->curr_opts.fmt_text;
	if (!fmt_text || !*fmt_text)
		fmt_text = I2C_DFLT_FRAMEFMT;
	sr_dbg("I2C frame format: %s.", fmt_text);

	/* Accept comma separated key=value pairs of specs. */
	opts = g_strsplit_set(fmt_text, ", ", 0);
	opt_count = g_strv_length(opts);
	for (opt_idx = 0; opt_idx < opt_count; opt_idx++) {
		opt = opts[opt_idx];
		if (!opt || !*opt)
			continue;
		sr_spew("I2C format option: %s.", opt);
		if (strcmp(opt, I2C_FORMAT_ADDR_7BIT) == 0) {
			sr_spew("I2C address: 7 bit");
			fmt_opts->addr_10bit = FALSE;
			continue;
		}
		if (strcmp(opt, I2C_FORMAT_ADDR_10BIT) == 0) {
			sr_spew("I2C address: 10 bit");
			fmt_opts->addr_10bit = TRUE;
			continue;
		}
		return SR_ERR_ARG;
	}
	g_strfreev(opts);

	/* Get the total slot count. Leave plenty room for convenience. */
	total_bits = 0;
	total_bits += I2C_BITTIME_SLOTS;
	total_bits *= I2C_BITTIME_QUANTA;
	total_bits += I2C_ADD_IDLESLOTS;

	sr_dbg("I2C frame: total bits %zu.", total_bits);
	if (total_bits > I2C_MAX_WAVELEN)
		return SR_ERR_DATA;
	inc->max_frame_bits = total_bits;

	return SR_OK;
}

/*
 * Don't bother with wide and narrow slots, just assume equal size for
 * them all. Edges will occupy exactly one sample, then levels are kept.
 * This protocol handler's oversampling should be sufficient for decoders
 * to extract the content from generated waveforms.
 *
 * Start with high levels on SCL and SDA for an idle bus condition.
 */
static int i2c_config_frame(struct context *inc)
{
	struct i2c_proto_context_t *incs;
	size_t bit_idx;
	uint8_t sample;

	if (!inc)
		return SR_ERR_ARG;
	incs = inc->curr_opts.prot_priv;

	memset(incs, 0, sizeof(*incs));
	incs->ack_remain = 0;

	/*
	 * Adjust all time slots since they represent a smaller quanta
	 * of an I2C bit time.
	 */
	for (bit_idx = 0; bit_idx < inc->max_frame_bits; bit_idx++) {
		inc->bit_scale[bit_idx].div = I2C_BITTIME_QUANTA;
	}

	sample = 0;
	sample |= I2C_PINMASK_SCL;
	sample |= I2C_PINMASK_SDA;
	sample_buffer_preset(inc, sample);

	return SR_OK;
}

/*
 * Process protocol dependent pseudo comments. Can affect future frame
 * construction and submission, or can immediately emit "inter frame"
 * bit patterns like START/STOP control. Use wide waveforms for these
 * transfer controls, put the special symbol nicely centered. Supports
 * users during interactive exploration of generated waveforms.
 */
static int i2c_proc_pseudo(struct sr_input *in, char *line)
{
	struct context *inc;
	char *word, *endp;
	int ret;
	unsigned long v;
	size_t bits;

	inc = in->priv;

	while (line) {
		word = sr_text_next_word(line, &line);
		if (!word)
			break;
		if (!*word)
			continue;
		sr_spew("I2C pseudo: word %s", word);
		if (strcmp(word, I2C_PSEUDO_START) == 0) {
			sr_spew("I2C pseudo: send START");
			ret = wave_clear_sequence(inc);
			if (ret != SR_OK)
				return ret;
			bits = I2C_BITTIME_SLOTS / 2;
			while (bits--) {
				ret = i2c_write_nothing(inc);
				if (ret != SR_OK)
					return ret;
			}
			ret = i2c_write_start(inc);
			if (ret != SR_OK)
				return ret;
			bits = I2C_BITTIME_SLOTS / 2;
			while (bits--) {
				ret = i2c_write_nothing(inc);
				if (ret != SR_OK)
					return ret;
			}
			ret = send_frame(in);
			if (ret != SR_OK)
				return ret;
			continue;
		}
		if (strcmp(word, I2C_PSEUDO_REP_START) == 0) {
			sr_spew("I2C pseudo: send REPEAT START");
			ret = wave_clear_sequence(inc);
			if (ret != SR_OK)
				return ret;
			bits = I2C_BITTIME_SLOTS / 2;
			while (bits--) {
				ret = i2c_write_nothing(inc);
				if (ret != SR_OK)
					return ret;
			}
			ret = i2c_write_start(inc);
			if (ret != SR_OK)
				return ret;
			bits = I2C_BITTIME_SLOTS / 2;
			while (bits--) {
				ret = i2c_write_nothing(inc);
				if (ret != SR_OK)
					return ret;
			}
			ret = send_frame(in);
			if (ret != SR_OK)
				return ret;
			continue;
		}
		if (strcmp(word, I2C_PSEUDO_STOP) == 0) {
			sr_spew("I2C pseudo: send STOP");
			ret = wave_clear_sequence(inc);
			if (ret != SR_OK)
				return ret;
			bits = I2C_BITTIME_SLOTS / 2;
			while (bits--) {
				ret = i2c_write_nothing(inc);
				if (ret != SR_OK)
					return ret;
			}
			ret = i2c_write_stop(inc);
			if (ret != SR_OK)
				return ret;
			bits = I2C_BITTIME_SLOTS / 2;
			while (bits--) {
				ret = i2c_write_nothing(inc);
				if (ret != SR_OK)
					return ret;
			}
			ret = send_frame(in);
			if (ret != SR_OK)
				return ret;
			continue;
		}
		if (g_str_has_prefix(word, I2C_PSEUDO_ADDR_WRITE)) {
			word += strlen(I2C_PSEUDO_ADDR_WRITE);
			endp = NULL;
			ret = sr_atoul_base(word, &v, &endp, 0);
			if (ret != SR_OK)
				return ret;
			if (!endp || *endp)
				return SR_ERR_ARG;
			sr_spew("I2C pseudo: addr write %lu", v);
			ret = i2c_send_address(in, v, FALSE);
			if (ret != SR_OK)
				return ret;
			continue;
		}
		if (g_str_has_prefix(word, I2C_PSEUDO_ADDR_READ)) {
			word += strlen(I2C_PSEUDO_ADDR_READ);
			endp = NULL;
			ret = sr_atoul_base(word, &v, &endp, 0);
			if (ret != SR_OK)
				return ret;
			if (!endp || *endp)
				return SR_ERR_ARG;
			sr_spew("I2C pseudo: addr read %lu", v);
			ret = i2c_send_address(in, v, TRUE);
			if (ret != SR_OK)
				return ret;
			continue;
		}
		if (g_str_has_prefix(word, I2C_PSEUDO_ACK_NEXT)) {
			word += strlen(I2C_PSEUDO_ACK_NEXT);
			endp = NULL;
			ret = sr_atoul_base(word, &v, &endp, 0);
			if (ret != SR_OK)
				return ret;
			if (!endp || *endp)
				return SR_ERR_ARG;
			sr_spew("i2c pseudo: ack next %lu", v);
			i2c_auto_ack_start(inc, v);
			continue;
		}
		if (strcmp(word, I2C_PSEUDO_ACK_ONCE) == 0) {
			sr_spew("i2c pseudo: ack once");
			i2c_auto_ack_start(inc, 1);
			continue;
		}
		return SR_ERR_DATA;
	}

	return SR_OK;
}

/*
 * Create the frame's waveform for the given data value. Automatically
 * track ACK bits, Fallback to NAK when externally specified ACK counts
 * have expired. The caller sends the waveform that we created.
 */
static int i2c_proc_value(struct context *inc, uint32_t value)
{
	gboolean with_ack;
	int ret;

	if (!inc)
		return SR_ERR_ARG;

	with_ack = i2c_auto_ack_avail(inc);

	ret = wave_clear_sequence(inc);
	if (ret != SR_OK)
		return ret;
	ret = i2c_write_byte(inc, value, with_ack);
	if (ret != SR_OK)
		return ret;

	return 0;
}

/* Start/end the logic trace with a few bit times of idle level. */
static int i2c_get_idle_capture(struct context *inc,
	size_t *bitcount, uint8_t *sample)
{

	/* Describe a byte's time of idle level. */
	if (bitcount)
		*bitcount = I2C_BITTIME_SLOTS;
	if (sample)
		*sample = inc->samples.idle_levels;
	return SR_OK;
}

/* Arrange for a few samples of idle level between UART frames. */
static int i2c_get_idle_interframe(struct context *inc,
	size_t *samplecount, uint8_t *sample)
{

	/*
	 * The space around regular bytes already is sufficient. We
	 * don't need to generate an inter-frame gap, but the code is
	 * prepared to in case we want to in the future.
	 */
	if (samplecount) {
		*samplecount = inc->curr_opts.samples_per_bit;
		*samplecount *= 0;
	}
	if (sample)
		*sample = inc->samples.curr_levels;
	return SR_OK;
}

/* }}} I2C protocol handler */
/* {{{ protocol dispatching */

/*
 * The list of supported protocols and their handlers, including
 * protocol specific defaults. The first item after the NONE slot
 * is the default protocol, and takes effect in the absence of any
 * user provided or file content provided spec.
 */
static const struct proto_handler_t protocols[PROTO_TYPE_COUNT] = {
	[PROTO_TYPE_UART] = {
		UART_HANDLER_NAME,
		{
			UART_DFLT_SAMPLERATE,
			UART_DFLT_BITRATE, UART_DFLT_FRAMEFMT,
			INPUT_BYTES,
		},
		{
			1, (const char *[]){
				[UART_PIN_RXTX] = "rxtx",
			},
		},
		0,
		uart_check_opts,
		uart_config_frame,
		uart_proc_pseudo,
		uart_proc_value,
		uart_get_idle_capture,
		uart_get_idle_interframe,
	},
	[PROTO_TYPE_SPI] = {
		SPI_HANDLER_NAME,
		{
			SPI_DFLT_SAMPLERATE,
			SPI_DFLT_BITRATE, SPI_DFLT_FRAMEFMT,
			INPUT_TEXT,
		},
		{
			4, (const char *[]){
				[SPI_PIN_SCK] = "sck",
				[SPI_PIN_MISO] = "miso",
				[SPI_PIN_MOSI] = "mosi",
				[SPI_PIN_CS] = "cs",
			},
		},
		sizeof(struct spi_proto_context_t),
		spi_check_opts,
		spi_config_frame,
		spi_proc_pseudo,
		spi_proc_value,
		spi_get_idle_capture,
		spi_get_idle_interframe,
	},
	[PROTO_TYPE_I2C] = {
		I2C_HANDLER_NAME,
		{
			I2C_DFLT_SAMPLERATE,
			I2C_DFLT_BITRATE, I2C_DFLT_FRAMEFMT,
			INPUT_TEXT,
		},
		{
			2, (const char *[]){
				[I2C_PIN_SCL] = "scl",
				[I2C_PIN_SDA] = "sda",
			},
		},
		sizeof(struct i2c_proto_context_t),
		i2c_check_opts,
		i2c_config_frame,
		i2c_proc_pseudo,
		i2c_proc_value,
		i2c_get_idle_capture,
		i2c_get_idle_interframe,
	},
};

static int lookup_protocol_name(struct context *inc)
{
	const char *name;
	const struct proto_handler_t *handler;
	size_t idx;
	void *priv;

	/*
	 * Silence compiler warnings. Protocol handlers are free to use
	 * several alternative sets of primitives for their operation.
	 * Not using part of the API is nothing worth warning about.
	 */
	(void)sample_buffer_assign;

	if (!inc)
		return SR_ERR_ARG;
	inc->curr_opts.protocol_type = PROTO_TYPE_NONE;
	inc->curr_opts.prot_hdl = NULL;

	name = inc->curr_opts.proto_name;
	if (!name || !*name) {
		/* Fallback to first item after NONE slot. */
		handler = &protocols[PROTO_TYPE_NONE + 1];
		name = handler->name;
	}

	for (idx = 0; idx < ARRAY_SIZE(protocols); idx++) {
		if (idx == PROTO_TYPE_NONE)
			continue;
		handler = &protocols[idx];
		if (!handler->name || !*handler->name)
			continue;
		if (strcmp(name, handler->name) != 0)
			continue;
		inc->curr_opts.protocol_type = idx;
		inc->curr_opts.prot_hdl = handler;
		if (handler->priv_size) {
			priv = g_malloc0(handler->priv_size);
			if (!priv)
				return SR_ERR_MALLOC;
			inc->curr_opts.prot_priv = priv;
		}
		return SR_OK;
	}

	return SR_ERR_DATA;
}

/* }}} protocol dispatching */
/* {{{ text/binary input file reader */

/**
 * Checks for UTF BOM, removes it when found at the start of the buffer.
 *
 * @param[in] buf The accumulated input buffer.
 */
static void check_remove_bom(GString *buf)
{
	static const char *bom_text = "\xef\xbb\xbf";

	if (buf->len < strlen(bom_text))
		return;
	if (strncmp(buf->str, bom_text, strlen(bom_text)) != 0)
		return;
	g_string_erase(buf, 0, strlen(bom_text));
}

/**
 * Checks for presence of a caption, yields the position after its text line.
 *
 * @param[in] buf The accumulated input buffer.
 * @param[in] caption The text to search for (NUL terminated ASCII literal).
 * @param[in] max_pos The maximum length to search for.
 *
 * @returns The position after the text line which contains the caption.
 *   Or #NULL when either the caption or the end-of-line was not found.
 */
static char *have_text_line(GString *buf, const char *caption, size_t max_pos)
{
	size_t cap_len, rem_len;
	char *p_read, *p_found;

	cap_len = strlen(caption);
	rem_len = buf->len;
	p_read = buf->str;

	/* Search for the occurance of the caption itself. */
	if (!max_pos) {
		/* Caption must be at the start of the buffer. */
		if (rem_len < cap_len)
			return NULL;
		if (strncmp(p_read, caption, cap_len) != 0)
			return NULL;
	} else {
		/* Caption can be anywhere up to a max position. */
		p_found = g_strstr_len(p_read, rem_len, caption);
		if (!p_found)
			return NULL;
		/* Pretend that caption had been rather long. */
		cap_len += p_found - p_read;
	}

	/*
	 * Advance over the caption. Advance over end-of-line. Supports
	 * several end-of-line conditions, but rejects unexpected trailer
	 * after the caption and before the end-of-line. Always wants LF.
	 */
	p_read += cap_len;
	rem_len -= cap_len;
	while (rem_len && *p_read != '\n' && g_ascii_isspace(*p_read)) {
		p_read++;
		rem_len--;
	}
	if (rem_len && *p_read != '\n' && *p_read == '\r') {
		p_read++;
		rem_len--;
	}
	if (rem_len && *p_read == '\n') {
		p_read++;
		rem_len--;
		return p_read;
	}

	return NULL;
}

/**
 * Checks for the presence of the magic string at the start of the file.
 *
 * @param[in] buf The accumulated input buffer.
 * @param[out] next_pos The text after the magic text line.
 *
 * @returns Boolean whether the magic was found.
 *
 * This implementation assumes that the magic file type marker never gets
 * split across receive chunks.
 */
static gboolean have_magic(GString *buf, char **next_pos)
{
	char *next_line;

	if (next_pos)
		*next_pos = NULL;

	next_line = have_text_line(buf, MAGIC_FILE_TYPE, 0);
	if (!next_line)
		return FALSE;

	if (next_pos)
		*next_pos = next_line;

	return TRUE;
}

/**
 * Checks for the presence of the header section at the start of the file.
 *
 * @param[in] buf The accumulated input buffer.
 * @param[out] next_pos The text after the header section.
 *
 * @returns A negative value when the answer is yet unknown (insufficient
 *   input data). Or boolean 0/1 when the header was found absent/present.
 *
 * The caller is supposed to have checked for and removed the magic text
 * for the file type. This routine expects to find the header section
 * boundaries right at the start of the input buffer.
 *
 * This implementation assumes that the header start marker never gets
 * split across receive chunks.
 */
static int have_header(GString *buf, char **next_pos)
{
	char *after_start, *after_end;

	if (next_pos)
		*next_pos = NULL;

	after_start = have_text_line(buf, TEXT_HEAD_START, 0);
	if (!after_start)
		return 0;

	after_end = have_text_line(buf, TEXT_HEAD_END, buf->len);
	if (!after_end)
		return -1;

	if (next_pos)
		*next_pos = after_end;
	return 1;
}

/*
 * Implementation detail: Most parse routines merely accept an input
 * string or at most convert text to numbers. Actual processing of the
 * values or constraints checks are done later when the header section
 * ended and all data was seen, regardless of order of appearance.
 */

static int parse_samplerate(struct context *inc, const char *text)
{
	uint64_t rate;
	int ret;

	ret = sr_parse_sizestring(text, &rate);
	if (ret != SR_OK)
		return SR_ERR_DATA;

	inc->curr_opts.samplerate = rate;

	return SR_OK;
}

static int parse_bitrate(struct context *inc, const char *text)
{
	uint64_t rate;
	int ret;

	ret = sr_parse_sizestring(text, &rate);
	if (ret != SR_OK)
		return SR_ERR_DATA;

	inc->curr_opts.bitrate = rate;

	return SR_OK;
}

static int parse_protocol(struct context *inc, const char *line)
{

	if (!line || !*line)
		return SR_ERR_DATA;

	if (inc->curr_opts.proto_name) {
		free(inc->curr_opts.proto_name);
		inc->curr_opts.proto_name = NULL;
	}
	inc->curr_opts.proto_name = g_strdup(line);
	if (!inc->curr_opts.proto_name)
		return SR_ERR_MALLOC;
	line = inc->curr_opts.proto_name;

	return SR_OK;
}

static int parse_frameformat(struct context *inc, const char *line)
{

	if (!line || !*line)
		return SR_ERR_DATA;

	if (inc->curr_opts.fmt_text) {
		free(inc->curr_opts.fmt_text);
		inc->curr_opts.fmt_text = NULL;
	}
	inc->curr_opts.fmt_text = g_strdup(line);
	if (!inc->curr_opts.fmt_text)
		return SR_ERR_MALLOC;
	line = inc->curr_opts.fmt_text;

	return SR_OK;
}

static int parse_textinput(struct context *inc, const char *text)
{
	gboolean is_text;

	if (!text || !*text)
		return SR_ERR_ARG;

	is_text = sr_parse_boolstring(text);
	inc->curr_opts.textinput = is_text ? INPUT_TEXT : INPUT_BYTES;
	return SR_OK;
}

static int parse_header_line(struct context *inc, const char *line)
{

	/* Silently ignore comment lines. Also covers start/end markers. */
	if (strncmp(line, TEXT_COMM_LEADER, strlen(TEXT_COMM_LEADER)) == 0)
		return SR_OK;

	if (strncmp(line, LABEL_SAMPLERATE, strlen(LABEL_SAMPLERATE)) == 0) {
		line += strlen(LABEL_SAMPLERATE);
		return parse_samplerate(inc, line);
	}
	if (strncmp(line, LABEL_BITRATE, strlen(LABEL_BITRATE)) == 0) {
		line += strlen(LABEL_BITRATE);
		return parse_bitrate(inc, line);
	}
	if (strncmp(line, LABEL_PROTOCOL, strlen(LABEL_PROTOCOL)) == 0) {
		line += strlen(LABEL_PROTOCOL);
		return parse_protocol(inc, line);
	}
	if (strncmp(line, LABEL_FRAMEFORMAT, strlen(LABEL_FRAMEFORMAT)) == 0) {
		line += strlen(LABEL_FRAMEFORMAT);
		return parse_frameformat(inc, line);
	}
	if (strncmp(line, LABEL_TEXTINPUT, strlen(LABEL_TEXTINPUT)) == 0) {
		line += strlen(LABEL_TEXTINPUT);
		return parse_textinput(inc, line);
	}

	/* Unsupported directive. */
	sr_err("Unsupported header directive: %s.", line);

	return SR_ERR_DATA;
}

static int parse_header(struct context *inc, GString *buf, size_t hdr_len)
{
	size_t remain;
	char *curr, *next, *line;
	int ret;

	ret = SR_OK;

	/* The caller determined where the header ends. Read up to there. */
	remain = hdr_len;
	curr = buf->str;
	while (curr && remain) {
		/* Get another text line. Skip empty lines. */
		line = sr_text_next_line(curr, remain, &next, NULL);
		if (!line)
			break;
		if (next)
			remain -= next - curr;
		else
			remain = 0;
		curr = next;
		if (!*line)
			continue;
		/* Process the non-empty file header text line. */
		sr_dbg("Header line: %s", line);
		ret = parse_header_line(inc, line);
		if (ret != SR_OK)
			break;
	}

	return ret;
}

/* Process input text reader specific pseudo comment. */
static int process_pseudo_textinput(struct sr_input *in, char *line)
{
	struct context *inc;
	char *word;
	unsigned long v;
	char *endp;
	int ret;

	inc = in->priv;
	while (line) {
		word = sr_text_next_word(line, &line);
		if (!word)
			break;
		if (!*word)
			continue;
		if (g_str_has_prefix(word, TEXT_INPUT_RADIX)) {
			word += strlen(TEXT_INPUT_RADIX);
			endp = NULL;
			ret = sr_atoul_base(word, &v, &endp, 10);
			if (ret != SR_OK)
				return ret;
			inc->read_text.base = v;
			continue;
		}
		return SR_ERR_DATA;
	}

	return SR_OK;
}

/* Process a line of input text. */
static int process_textline(struct sr_input *in, char *line)
{
	struct context *inc;
	const struct proto_handler_t *handler;
	gboolean is_comm, is_pseudo;
	char *p, *word, *endp;
	unsigned long value;
	int ret;

	inc = in->priv;
	handler = inc->curr_opts.prot_hdl;

	/*
	 * Check for comments, including pseudo-comments with protocol
	 * specific or text reader specific instructions. It's essential
	 * to check for "# ${PROTO}:" last, because the implementation
	 * of the check advances the read position, cannot rewind when
	 * detection fails. But we know that it is a comment and was not
	 * a pseudo-comment. So any non-matching data just gets discarded.
	 * Matching data gets processed (when handlers exist).
	 */
	is_comm = g_str_has_prefix(line, TEXT_COMM_LEADER);
	if (is_comm) {
		line += strlen(TEXT_COMM_LEADER);
		while (isspace(*line))
			line++;
		is_pseudo = g_str_has_prefix(line, TEXT_INPUT_PREFIX);
		if (is_pseudo) {
			line += strlen(TEXT_INPUT_PREFIX);
			while (isspace(*line))
				line++;
			sr_dbg("pseudo comment, textinput: %s", line);
			line = sr_text_trim_spaces(line);
			return process_pseudo_textinput(in, line);
		}
		is_pseudo = g_str_has_prefix(line, handler->name);
		if (is_pseudo) {
			line += strlen(handler->name);
			is_pseudo = *line == ':';
			if (is_pseudo)
				line++;
		}
		if (is_pseudo) {
			while (isspace(*line))
				line++;
			sr_dbg("pseudo comment, protocol: %s", line);
			if (!handler->proc_pseudo)
				return SR_OK;
			return handler->proc_pseudo(in, line);
		}
		sr_spew("comment, skipping: %s", line);
		return SR_OK;
	}

	/*
	 * Non-empty non-comment lines carry protocol values.
	 * (Empty lines are handled transparently when they get here.)
	 * Accept comma and semicolon separators for user convenience.
	 * Convert text according to previously received instructions.
	 * Pass the values to the protocol handler. Flush waveforms
	 * when handlers state that their construction has completed.
	 */
	sr_spew("got values line: %s", line);
	for (p = line; *p; p++) {
		if (*p == ',' || *p == ';')
			*p = ' ';
	}
	while (line) {
		word = sr_text_next_word(line, &line);
		if (!word)
			break;
		if (!*word)
			continue;
		/* Get another numeric value. */
		endp = NULL;
		ret = sr_atoul_base(word, &value, &endp, inc->read_text.base);
		if (ret != SR_OK)
			return ret;
		if (!endp || *endp)
			return SR_ERR_DATA;
		sr_spew("got a value, text [%s] -> number [%lu]", word, value);
		/* Forward the value to the protocol handler. */
		ret = 0;
		if (handler->proc_value)
			ret = handler->proc_value(inc, value);
		if (ret < 0)
			return ret;
		/* Flush the waveform when handler signals completion. */
		if (ret > 0)
			continue;
		ret = send_frame(in);
		if (ret != SR_OK)
			return ret;
		ret = send_idle_interframe(inc);
		if (ret != SR_OK)
			return ret;
	}

	return SR_OK;
}

/* }}} text/binary input file reader */

/*
 * Consistency check of all previously received information. Combines
 * the data file's optional header section, as well as user provided
 * options that were specified during input module creation. User specs
 * take precedence over file content.
 */
static int check_header_user_options(struct context *inc)
{
	int ret;
	const struct proto_handler_t *handler;
	uint64_t rate;
	const char *text;
	enum textinput_t is_text;

	if (!inc)
		return SR_ERR_ARG;

	/* Prefer user specs over file content. */
	rate = inc->user_opts.samplerate;
	if (rate) {
		sr_dbg("Using user samplerate %" PRIu64 ".", rate);
		inc->curr_opts.samplerate = rate;
	}
	rate = inc->user_opts.bitrate;
	if (rate) {
		sr_dbg("Using user bitrate %" PRIu64 ".", rate);
		inc->curr_opts.bitrate = rate;
	}
	text = inc->user_opts.proto_name;
	if (text && *text) {
		sr_dbg("Using user protocol %s.", text);
		ret = parse_protocol(inc, text);
		if (ret != SR_OK)
			return SR_ERR_DATA;
	}
	text = inc->user_opts.fmt_text;
	if (text && *text) {
		sr_dbg("Using user frame format %s.", text);
		ret = parse_frameformat(inc, text);
		if (ret != SR_OK)
			return SR_ERR_DATA;
	}
	is_text = inc->user_opts.textinput;
	if (is_text) {
		sr_dbg("Using user textinput %d.", is_text);
		inc->curr_opts.textinput = is_text;
	}

	/* Lookup the protocol (with fallback). Use protocol's defaults. */
	text = inc->curr_opts.proto_name;
	ret = lookup_protocol_name(inc);
	handler = inc->curr_opts.prot_hdl;
	if (ret != SR_OK || !handler) {
		sr_err("Unsupported protocol: %s.", text);
		return SR_ERR_DATA;
	}
	text = handler->name;
	if (!inc->curr_opts.proto_name && text) {
		sr_dbg("Using protocol handler name %s.", text);
		ret = parse_protocol(inc, text);
		if (ret != SR_OK)
			return SR_ERR_DATA;
	}
	rate = handler->dflt.samplerate;
	if (!inc->curr_opts.samplerate && rate) {
		sr_dbg("Using protocol handler samplerate %" PRIu64 ".", rate);
		inc->curr_opts.samplerate = rate;
	}
	rate = handler->dflt.bitrate;
	if (!inc->curr_opts.bitrate && rate) {
		sr_dbg("Using protocol handler bitrate %" PRIu64 ".", rate);
		inc->curr_opts.bitrate = rate;
	}
	text = handler->dflt.frame_format;
	if (!inc->curr_opts.fmt_text && text && *text) {
		sr_dbg("Using protocol handler frame format %s.", text);
		ret = parse_frameformat(inc, text);
		if (ret != SR_OK)
			return SR_ERR_DATA;
	}
	is_text = handler->dflt.textinput;
	if (!inc->curr_opts.textinput && is_text) {
		sr_dbg("Using protocol handler text format %d.", is_text);
		inc->curr_opts.textinput = is_text;
	}

	if (!inc->curr_opts.samplerate) {
		sr_err("Need a samplerate.");
		return SR_ERR_DATA;
	}
	if (!inc->curr_opts.bitrate) {
		sr_err("Need a protocol bitrate.");
		return SR_ERR_DATA;
	}

	if (inc->curr_opts.samplerate < inc->curr_opts.bitrate) {
		sr_err("Bitrate cannot exceed samplerate.");
		return SR_ERR_DATA;
	}
	if (inc->curr_opts.samplerate / inc->curr_opts.bitrate < 3)
		sr_warn("Low oversampling, consider higher samplerate.");
	if (inc->curr_opts.prot_hdl->check_opts) {
		ret = inc->curr_opts.prot_hdl->check_opts(inc);
		if (ret != SR_OK) {
			sr_err("Options failed the protocol's check.");
			return SR_ERR_DATA;
		}
	}

	return SR_OK;
}

static int create_channels(struct sr_input *in)
{
	struct context *inc;
	struct sr_dev_inst *sdi;
	const struct proto_handler_t *handler;
	size_t index;
	const char *name;

	if (!in)
		return SR_ERR_ARG;
	inc = in->priv;
	if (!inc)
		return SR_ERR_ARG;
	sdi = in->sdi;
	handler = inc->curr_opts.prot_hdl;

	for (index = 0; index < handler->chans.count; index++) {
		name = handler->chans.names[index];
		sr_dbg("Channel %zu name %s.", index, name);
		sr_channel_new(sdi, index, SR_CHANNEL_LOGIC, TRUE, name);
	}

	inc->feed_logic = feed_queue_logic_alloc(in->sdi,
		CHUNK_SIZE, sizeof(uint8_t));
	if (!inc->feed_logic) {
		sr_err("Cannot create session feed.");
		return SR_ERR_MALLOC;
	}

	return SR_OK;
}

/*
 * Keep track of a previously created channel list, in preparation of
 * re-reading the input file. Gets called from reset()/cleanup() paths.
 */
static void keep_header_for_reread(const struct sr_input *in)
{
	struct context *inc;

	inc = in->priv;

	g_slist_free_full(inc->prev.sr_groups, sr_channel_group_free_cb);
	inc->prev.sr_groups = in->sdi->channel_groups;
	in->sdi->channel_groups = NULL;

	g_slist_free_full(inc->prev.sr_channels, sr_channel_free_cb);
	inc->prev.sr_channels = in->sdi->channels;
	in->sdi->channels = NULL;
}

/*
 * Check whether the input file is being re-read, and refuse operation
 * when essential parameters of the acquisition have changed in ways
 * that are unexpected to calling applications. Gets called after the
 * file header got parsed (again).
 *
 * Changing the channel list across re-imports of the same file is not
 * supported, by design and for valid reasons, see bug #1215 for details.
 * Users are expected to start new sessions when they change these
 * essential parameters in the acquisition's setup. When we accept the
 * re-read file, then make sure to keep using the previous channel list,
 * applications may still reference them.
 */
static gboolean check_header_in_reread(const struct sr_input *in)
{
	struct context *inc;

	if (!in)
		return FALSE;
	inc = in->priv;
	if (!inc)
		return FALSE;
	if (!inc->prev.sr_channels)
		return TRUE;

	if (sr_channel_lists_differ(inc->prev.sr_channels, in->sdi->channels)) {
		sr_err("Channel list change not supported for file re-read.");
		return FALSE;
	}

	g_slist_free_full(in->sdi->channel_groups, sr_channel_group_free_cb);
	in->sdi->channel_groups = inc->prev.sr_groups;
	inc->prev.sr_groups = NULL;

	g_slist_free_full(in->sdi->channels, sr_channel_free_cb);
	in->sdi->channels = inc->prev.sr_channels;
	inc->prev.sr_channels = NULL;

	return TRUE;
}

/* Process another chunk of accumulated input data. */
static int process_buffer(struct sr_input *in, gboolean is_eof)
{
	struct context *inc;
	GVariant *gvar;
	int ret;
	GString *buf;
	const struct proto_handler_t *handler;
	size_t seen;
	char *line, *next;
	uint8_t sample;

	inc = in->priv;
	buf = in->buf;
	handler = inc->curr_opts.prot_hdl;

	/*
	 * Send feed header and samplerate once before any sample data.
	 * Communicate an idle period before the first generated frame.
	 */
	if (!inc->started) {
		std_session_send_df_header(in->sdi);
		gvar = g_variant_new_uint64(inc->curr_opts.samplerate);
		ret = sr_session_send_meta(in->sdi, SR_CONF_SAMPLERATE, gvar);
		inc->started = TRUE;
		if (ret != SR_OK)
			return ret;

		ret = send_idle_capture(inc);
		if (ret != SR_OK)
			return ret;
	}

	/*
	 * Force proper line termination when EOF is seen and the data
	 * is in text format. This does not affect binary input, while
	 * properly terminated text input does not suffer from another
	 * line feed, because empty lines are considered acceptable.
	 * Increases robustness for text input from broken generators
	 * (popular editors which don't terminate the last line).
	 */
	if (inc->curr_opts.textinput == INPUT_TEXT && is_eof) {
		g_string_append_c(buf, '\n');
	}

	/*
	 * For text input: Scan for the completion of another text line.
	 * Process its values (or pseudo comments). Skip comment lines.
	 */
	if (inc->curr_opts.textinput == INPUT_TEXT) do {
		/* Get another line of text. */
		seen = 0;
		line = sr_text_next_line(buf->str, buf->len, &next, &seen);
		if (!line)
			break;
		/* Process non-empty input lines. */
		ret = *line ? process_textline(in, line) : 0;
		if (ret < 0)
			return ret;
		/* Discard processed input text. */
		g_string_erase(buf, 0, seen);
	} while (buf->len);

	/*
	 * For binary input: Pass data values (individual bytes) to the
	 * creation of protocol frames. Send the frame's waveform to
	 * logic channels in the session feed when the protocol handler
	 * signals the completion of another waveform (zero return value).
	 * Non-zero positive values translate to "need more input data".
	 * Negative values signal fatal errors. Remove processed input
	 * data from the receive buffer.
	 */
	if (inc->curr_opts.textinput == INPUT_BYTES) {
		seen = 0;
		while (seen < buf->len) {
			sample = buf->str[seen++];
			ret = 0;
			if (handler->proc_value)
				ret = handler->proc_value(inc, sample);
			if (ret < 0)
				return ret;
			if (ret > 0)
				continue;
			ret = send_frame(in);
			if (ret != SR_OK)
				return ret;
			ret = send_idle_interframe(inc);
			if (ret != SR_OK)
				return ret;
		}
		g_string_erase(buf, 0, seen);
	}

	/* Send idle level, and flush when end of input data is seen. */
	if (is_eof) {
		if (buf->len)
			sr_warn("Unprocessed input data remains.");

		ret = send_idle_capture(inc);
		if (ret != SR_OK)
			return ret;

		ret = feed_queue_logic_flush(inc->feed_logic);
		if (ret != SR_OK)
			return ret;
	}

	return SR_OK;
}

static int format_match(GHashTable *metadata, unsigned int *confidence)
{
	GString *buf, *tmpbuf;
	gboolean has_magic;

	buf = g_hash_table_lookup(metadata,
		GINT_TO_POINTER(SR_INPUT_META_HEADER));
	tmpbuf = g_string_new_len(buf->str, buf->len);

	check_remove_bom(tmpbuf);
	has_magic = have_magic(tmpbuf, NULL);
	g_string_free(tmpbuf, TRUE);

	if (!has_magic)
		return SR_ERR;

	*confidence = 1;
	return SR_OK;
}

static int init(struct sr_input *in, GHashTable *options)
{
	struct context *inc;
	GVariant *gvar;
	uint64_t rate;
	char *copy;
	const char *text;

	in->sdi = g_malloc0(sizeof(*in->sdi));
	inc = g_malloc0(sizeof(*inc));
	in->priv = inc;

	/*
	 * Store user specified options for later reference.
	 *
	 * TODO How to most appropriately hook up size strings with the
	 * input module's defaults, and applications and their input
	 * dialogs?
	 */
	gvar = g_hash_table_lookup(options, "samplerate");
	if (gvar) {
		rate = g_variant_get_uint64(gvar);
		if (rate)
			sr_dbg("User samplerate %" PRIu64 ".", rate);
		inc->user_opts.samplerate = rate;
	}

	gvar = g_hash_table_lookup(options, "bitrate");
	if (gvar) {
		rate = g_variant_get_uint64(gvar);
		if (rate)
			sr_dbg("User bitrate %" PRIu64 ".", rate);
		inc->user_opts.bitrate = rate;
	}

	gvar = g_hash_table_lookup(options, "protocol");
	if (gvar) {
		copy = g_strdup(g_variant_get_string(gvar, NULL));
		if (!copy)
			return SR_ERR_MALLOC;
		if (*copy)
			sr_dbg("User protocol %s.", copy);
		inc->user_opts.proto_name = copy;
	}

	gvar = g_hash_table_lookup(options, "frameformat");
	if (gvar) {
		copy = g_strdup(g_variant_get_string(gvar, NULL));
		if (!copy)
			return SR_ERR_MALLOC;
		if (*copy)
			sr_dbg("User frame format %s.", copy);
		inc->user_opts.fmt_text = copy;
	}

	inc->user_opts.textinput = INPUT_UNSPEC;
	gvar = g_hash_table_lookup(options, "textinput");
	if (gvar) {
		text = g_variant_get_string(gvar, NULL);
		if (!text)
			return SR_ERR_DATA;
		if (!*text)
			return SR_ERR_DATA;
		sr_dbg("User text input %s.", text);
		if (strcmp(text, input_format_texts[INPUT_UNSPEC]) == 0) {
			inc->user_opts.textinput = INPUT_UNSPEC;
		} else if (strcmp(text, input_format_texts[INPUT_BYTES]) == 0) {
			inc->user_opts.textinput = INPUT_BYTES;
		} else if (strcmp(text, input_format_texts[INPUT_TEXT]) == 0) {
			inc->user_opts.textinput = INPUT_TEXT;
		} else {
			return SR_ERR_DATA;
		}
	}

	return SR_OK;
}

static int receive(struct sr_input *in, GString *buf)
{
	struct context *inc;
	char *after_magic, *after_header;
	size_t consumed;
	int ret;

	inc = in->priv;

	/*
	 * Accumulate all input chunks, potential deferred processing.
	 *
	 * Remove an optional BOM at the very start of the input stream.
	 * BEWARE! This may affect binary input, and we cannot tell if
	 * the input is text or binary at this stage. Though probability
	 * for this issue is rather low. Workarounds are available (put
	 * another values before the first data which happens to match
	 * the BOM pattern, provide text input instead).
	 */
	g_string_append_len(in->buf, buf->str, buf->len);
	if (!inc->scanned_magic)
		check_remove_bom(in->buf);

	/*
	 * Must complete reception of the (optional) header first. Both
	 * end of header and absence of header will: Check options that
	 * were seen so far, then start processing the data part.
	 */
	if (!inc->got_header) {
		/* Check for magic file type marker. */
		if (!inc->scanned_magic) {
			inc->has_magic = have_magic(in->buf, &after_magic);
			inc->scanned_magic = TRUE;
			if (inc->has_magic) {
				consumed = after_magic - in->buf->str;
				sr_dbg("File format magic found (%zu).", consumed);
				g_string_erase(in->buf, 0, consumed);
			}
		}

		/* Complete header reception and processing. */
		if (inc->has_magic) {
			ret = have_header(in->buf, &after_header);
			if (ret < 0)
				return SR_OK;
			inc->has_header = ret;
			if (inc->has_header) {
				consumed = after_header - in->buf->str;
				sr_dbg("File header found (%zu), processing.", consumed);
				ret = parse_header(inc, in->buf, consumed);
				if (ret != SR_OK)
					return ret;
				g_string_erase(in->buf, 0, consumed);
			}
		}
		inc->got_header = TRUE;

		/*
		 * Postprocess the combination of all options. Create
		 * logic channels, prepare resources for data processing.
		 */
		ret = check_header_user_options(inc);
		if (ret != SR_OK)
			return ret;
		ret = create_channels(in);
		if (ret != SR_OK)
			return ret;
		if (!check_header_in_reread(in))
			return SR_ERR_DATA;
		ret = alloc_frame_storage(inc);
		if (ret != SR_OK)
			return ret;
		ret = assign_bit_widths(inc);
		if (ret != SR_OK)
			return ret;

		/* Notify the frontend that sdi is ready. */
		in->sdi_ready = TRUE;
		return SR_OK;
	}

	/*
	 * Process the input file's data section after the header section
	 * was received and processed.
	 */
	ret = process_buffer(in, FALSE);

	return ret;
}

static int end(struct sr_input *in)
{
	struct context *inc;
	int ret;

	inc = in->priv;

	/* Must complete processing of previously received chunks. */
	if (in->sdi_ready) {
		ret = process_buffer(in, TRUE);
		if (ret != SR_OK)
			return ret;
	}

	/* Must send DF_END when DF_HEADER was sent before. */
	if (inc->started) {
		ret = std_session_send_df_end(in->sdi);
		if (ret != SR_OK)
			return ret;
	}

	return SR_OK;
}

static void cleanup(struct sr_input *in)
{
	struct context *inc;

	inc = in->priv;

	keep_header_for_reread(in);

	g_free(inc->curr_opts.proto_name);
	inc->curr_opts.proto_name = NULL;
	g_free(inc->curr_opts.fmt_text);
	inc->curr_opts.fmt_text = NULL;
	g_free(inc->curr_opts.prot_priv);
	inc->curr_opts.prot_priv = NULL;
	feed_queue_logic_free(inc->feed_logic);
	inc->feed_logic = NULL;
	g_free(inc->sample_edges);
	inc->sample_edges = NULL;
	g_free(inc->sample_widths);
	inc->sample_widths = NULL;
	g_free(inc->sample_levels);
	inc->sample_levels = NULL;
	g_free(inc->bit_scale);
	inc->bit_scale = NULL;
}

static int reset(struct sr_input *in)
{
	struct context *inc;
	struct user_opts_t save_user_opts;
	struct proto_prev save_chans;

	inc = in->priv;

	/* Release previously allocated resources. */
	cleanup(in);
	g_string_truncate(in->buf, 0);

	/* Restore part of the context, init() won't run again. */
	save_user_opts = inc->user_opts;
	save_chans = inc->prev;
	memset(inc, 0, sizeof(*inc));
	inc->user_opts = save_user_opts;
	inc->prev = save_chans;

	return SR_OK;
}

enum proto_option_t {
	OPT_SAMPLERATE,
	OPT_BITRATE,
	OPT_PROTOCOL,
	OPT_FRAME_FORMAT,
	OPT_TEXTINPUT,
	OPT_MAX,
};

static struct sr_option options[] = {
	[OPT_SAMPLERATE] = {
		"samplerate", "Logic data samplerate",
		"Samplerate of generated logic traces",
		NULL, NULL,
	},
	[OPT_BITRATE] = {
		"bitrate", "Protocol bitrate",
		"Bitrate used in protocol's communication",
		NULL, NULL,
	},
	[OPT_PROTOCOL] = {
		"protocol", "Protocol type",
		"The type of protocol to generate waveforms for",
		NULL, NULL,
	},
	[OPT_FRAME_FORMAT] = {
		"frameformat", "Protocol frame format",
		"Textual description of the protocol's frame format",
		NULL, NULL,
	},
	[OPT_TEXTINPUT] = {
		"textinput", "Input data is in text format",
		"Input is not data bytes, but text formatted values",
		NULL, NULL,
	},
	[OPT_MAX] = ALL_ZERO,
};

static const struct sr_option *get_options(void)
{
	GSList *l;
	enum proto_type_t p_idx;
	enum textinput_t t_idx;
	const char *s;

	if (options[0].def)
		return options;

	options[OPT_SAMPLERATE].def = g_variant_ref_sink(g_variant_new_uint64(0));
	options[OPT_BITRATE].def = g_variant_ref_sink(g_variant_new_uint64(0));
	options[OPT_PROTOCOL].def = g_variant_ref_sink(g_variant_new_string(""));
	l = NULL;
	for (p_idx = 0; p_idx < ARRAY_SIZE(protocols); p_idx++) {
		s = protocols[p_idx].name;
		if (!s || !*s)
			continue;
		l = g_slist_append(l, g_variant_ref_sink(g_variant_new_string(s)));
	}
	options[OPT_PROTOCOL].values = l;
	options[OPT_FRAME_FORMAT].def = g_variant_ref_sink(g_variant_new_string(""));
	l = NULL;
	for (t_idx = INPUT_UNSPEC; t_idx <= INPUT_TEXT; t_idx++) {
		s = input_format_texts[t_idx];
		l = g_slist_append(l, g_variant_ref_sink(g_variant_new_string(s)));
	}
	options[OPT_TEXTINPUT].values = l;
	options[OPT_TEXTINPUT].def = g_variant_ref_sink(g_variant_new_string(
		input_format_texts[INPUT_UNSPEC]));
	return options;
}

SR_PRIV struct sr_input_module input_protocoldata = {
	.id = "protocoldata",
	.name = "Protocol data",
	.desc = "Generate logic traces from protocol's data values",
	.exts = (const char *[]){ "sr-protocol", "protocol", "bin", NULL, },
	.metadata = { SR_INPUT_META_HEADER | SR_INPUT_META_REQUIRED },
	.options = get_options,
	.format_match = format_match,
	.init = init,
	.receive = receive,
	.end = end,
	.reset = reset,
};
