/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2026 Thomas Griffiths <thomas@tgriffiths.dev>
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
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "protocol.h"
#include "rp1_pio_if.h"

#define PIO_IOCTL(fd, op, argp) ioctl((fd), (op), (argp))

static struct rp1_pio_add_program_args sample_program = {
	.num_instrs = 1,
	.origin = RP1_PIO_ORIGIN_ANY,
	.instrs = { 0x4001 },
};

static inline int pio_sm_config_xfer(int fd, uint16_t sm, uint16_t dir,
				     uint32_t flags, uint32_t buf_size,
				     uint32_t buf_count)
{
	struct rp1_pio_sm_config_xfer_v2_args args = {
		.sm = sm,
		.dir = dir,
		.flags = flags,
		.buf_size = buf_size,
		.buf_count = buf_count,
	};
	return PIO_IOCTL(fd, PIO_IOC_SM_CONFIG_XFER_V2, &args);
}

static inline int pio_sm_xfer_data(int fd, uint16_t sm, uint16_t dir,
				   void *data, uint32_t data_bytes)
{
	struct rp1_pio_sm_xfer_data32_args args = {
		.sm = sm,
		.dir = dir,
		.data_bytes = data_bytes,
		.data = data,
	};
	return PIO_IOCTL(fd, PIO_IOC_SM_XFER_DATA32, &args);
}

SR_PRIV unsigned pio_step_for_hz(double hz)
{
	double want;
	unsigned step;

	if (!(hz > 0))
		return 0;
	want = (double)PIO_TICK_HZ / hz;
	if (want < PIO_STEP_MIN - 0.5 || want > PIO_STEP_MAX + 0.5)
		return 0;
	step = (unsigned)(want + 0.5);
	return step < PIO_STEP_MIN ? PIO_STEP_MIN :
	       step > PIO_STEP_MAX ? PIO_STEP_MAX :
				     step;
}

SR_PRIV double pio_step_hz(unsigned step)
{
	return (double)PIO_TICK_HZ / step;
}

static unsigned step_rate(unsigned step)
{
	return (unsigned)(PIO_TICK_HZ / step);
}

static inline uint32_t word_mask(unsigned bits)
{
	return bits >= 32 ? 0xffffffffu : (1u << bits) - 1;
}

SR_PRIV int la_build_sms(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	const struct pin_layout *layout = NULL;
	unsigned clock_hz = step_rate(devc->tick_step);
	size_t target_samples;
	int max_pin = -1;
	GSList *l;

	for (l = sdi->channels; l; l = l->next) {
		struct sr_channel *ch = l->data;
		if (ch->enabled && ch->type == SR_CHANNEL_LOGIC)
			max_pin = MAX(max_pin, (int)ch->index);
	}

	if (max_pin < 0) {
		sr_err("No channels enabled.");
		return SR_ERR;
	}

	for (size_t i = 0; i < G_N_ELEMENTS(sm_pin_layouts); i++) {
		if (devc->cur_samplerate <= sm_pin_layouts[i].max_freq &&
		    max_pin < sm_pin_layouts[i].total_width) {
			layout = &sm_pin_layouts[i];
			devc->selected_layout = i;
			break;
		}
	}

	if (!layout) {
		sr_err("No layout supports %" PRIu64 " Hz with GPIO%d enabled.",
		       devc->cur_samplerate, max_pin);
		return SR_ERR;
	}

	target_samples =
		MAX(((uint64_t)clock_hz * BUFFER_FILL_TIME_MS) / 1000, 1024);
	target_samples = (target_samples + 31) & ~31ULL;
	devc->samples_per_round = target_samples;

	devc->num_sms = 0;
	for (unsigned i = 0; i < MAX_SMS; i++) {
		int base = layout->pins[i];
		if (base < 0)
			break;

		int next = layout->total_width;
		for (unsigned j = i + 1; j < MAX_SMS; j++) {
			if (layout->pins[j] >= 0) {
				next = layout->pins[j];
				break;
			}
		}
		unsigned n = next - base;

		gboolean needed = FALSE;
		for (l = sdi->channels; l; l = l->next) {
			struct sr_channel *ch = l->data;
			if (ch->enabled && ch->type == SR_CHANNEL_LOGIC &&
			    ch->index >= base && ch->index < next) {
				needed = TRUE;
				break;
			}
		}
		if (!needed)
			continue;

		unsigned spw = 32 / n;
		struct la_sm *sm = &devc->sm[devc->num_sms++];
		memset(sm, 0, sizeof(*sm));
		sm->base = base;
		sm->n = n;
		sm->samples_per_word = spw;
		sm->words = target_samples / spw;
		sm->claimed = -1;

		sr_info("Layout %d SM idx %u: GPIO%u..GPIO%u (%u pins) at %.2f MHz",
			devc->selected_layout, i, sm->base,
			sm->base + sm->n - 1, sm->n,
			pio_step_hz(devc->tick_step) / 1e6);
	}

	return SR_OK;
}

static int sm_program(int fd, struct dev_context *devc, struct la_sm *sm,
		      unsigned sm_idx, unsigned tick_step)
{
	struct rp1_pio_add_program_args prog = sample_program;
	rp1_pio_sm_config config;
	unsigned threshold, div_int, div_frac;
	size_t bytes;
	int offset, claimed;
	uint16_t gpio;
	uint16_t pio_fn = RP1_GPIO_FUNC_PIO;
	uint32_t dma_flags = RP1_PIO_SM_CONFIG_XFER_FL_DMA_CYCLE;

	claimed = PIO_IOCTL(fd, PIO_IOC_SM_CLAIM,
			    (&(struct rp1_pio_sm_claim_args){ .mask = 0 }));
	if (claimed < 0) {
		sr_err("No PIO state machine available.");
		return SR_ERR;
	}
	sm->claimed = claimed;

	prog.instrs[0] = 0x4000 | (sm->n & 0x1f);
	offset = PIO_IOCTL(fd, PIO_IOC_ADD_PROGRAM, &prog);
	if (offset < 0) {
		sr_err("Cannot load sampling program into the PIO: %s.",
		       g_strerror(errno));
		return SR_ERR;
	}

	for (gpio = sm->base; gpio < sm->base + sm->n; gpio++) {
		PIO_IOCTL(
			fd, PIO_IOC_GPIO_SET_FUNCTION,
			(&(struct rp1_gpio_set_function_args){ gpio, pio_fn }));
		PIO_IOCTL(fd, PIO_IOC_GPIO_SET_INPUT_ENABLED,
			  (&(struct rp1_gpio_set_args){ gpio, 1 }));
		PIO_IOCTL(fd, PIO_IOC_GPIO_SET_OUTOVER,
			  (&(struct rp1_gpio_set_args){ gpio, 0 }));
		PIO_IOCTL(fd, PIO_IOC_GPIO_SET_DRIVE_STRENGTH,
			  (&(struct rp1_gpio_set_args){ gpio, 3 }));
	}
	PIO_IOCTL(fd, PIO_IOC_SM_SET_PINDIRS,
		  (&(struct rp1_pio_sm_set_pindirs_args){
			  .sm = claimed,
			  .dirs = 0,
			  .mask = word_mask(sm->n) << sm->base }));
	PIO_IOCTL(fd, PIO_IOC_SM_CLEAR_FIFOS,
		  (&(struct rp1_pio_sm_clear_fifos_args){ claimed }));

	threshold = sm->n * sm->samples_per_word;
	div_int = tick_step / PIO_TICK_PER_CLK;
	div_frac = tick_step % PIO_TICK_PER_CLK;

	config = (rp1_pio_sm_config){
		.clkdiv = ((div_int & 0xffff) << 16) | (div_frac << 8),
		.execctrl = (uint32_t)offset << 7 | (uint32_t)offset << 12,
		.shiftctrl = (0 << 18) | (1 << 16) | ((threshold % 32) << 20) |
			     (1u << 31),
		.pinctrl = (uint32_t)sm->base << 15,
	};
	PIO_IOCTL(fd, PIO_IOC_SM_INIT,
		  (&(struct rp1_pio_sm_init_args){ .sm = claimed,
						   .initial_pc = offset,
						   .config = config }));

	bytes = sm->words * sizeof(uint32_t);
	sm->buffer = g_malloc0(bytes);

	if (claimed < PIO_DMA_HEAVY_COUNT) {
		dma_flags |= (bytes > PER_BUFFER_FILL_TIME(PIO_DMA_LIGHT)) ?
				     RP1_PIO_SM_CONFIG_XFER_FL_DMA_FORCE_HEAVY :
				     RP1_PIO_SM_CONFIG_XFER_FL_DMA_PREFER_HEAVY;
	} else {
		dma_flags |= RP1_PIO_SM_CONFIG_XFER_FL_DMA_FORCE_LIGHT;
	}

	if (pio_sm_config_xfer(fd, claimed, RP1_PIO_DIR_FROM_SM, dma_flags,
			       bytes, 4) < 0) {
		sr_err("Cannot configure transfers for sm %d (idx %u): %s.",
		       claimed, sm_idx, g_strerror(errno));
		return SR_ERR;
	}
	return SR_OK;
}

static void *acquisition_worker(void *arg)
{
	struct sr_dev_inst *sdi = arg;
	struct dev_context *devc = sdi->priv;

	while (g_atomic_int_get(&devc->running)) {
		for (unsigned i = 0; i < devc->num_sms; i++) {
			pio_sm_xfer_data(devc->fd, devc->sm[i].claimed,
					 RP1_PIO_DIR_FROM_SM,
					 devc->sm[i].buffer,
					 devc->sm[i].words * sizeof(uint32_t));
		}

		if (!g_atomic_int_get(&devc->running))
			break;

		g_mutex_lock(&devc->sm_lock);
		devc->buffers_full = TRUE;

		uint64_t val = 1;
		(void)write(devc->event_fd, &val, sizeof(val));

		while (devc->buffers_full && g_atomic_int_get(&devc->running))
			g_cond_wait(&devc->sm_cond, &devc->sm_lock);

		g_mutex_unlock(&devc->sm_lock);
	}

	return NULL;
}

SR_PRIV int la_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	uint16_t enable_mask = 0;
	unsigned i;

	g_mutex_init(&devc->sm_lock);
	g_cond_init(&devc->sm_cond);
	devc->buffers_full = FALSE;

	for (i = 0; i < devc->num_sms; i++) {
		if (sm_program(devc->fd, devc, &devc->sm[i], i,
			       devc->tick_step) != SR_OK) {
			g_mutex_clear(&devc->sm_lock);
			g_cond_clear(&devc->sm_cond);
			return SR_ERR;
		}
		enable_mask |= 1u << devc->sm[i].claimed;
	}

	devc->samples_sent = 0;
	g_atomic_int_set(&devc->running, 1);
	devc->worker_thread =
		g_thread_new("rp1_worker", acquisition_worker, (void *)sdi);

	PIO_IOCTL(devc->fd, PIO_IOC_SM_RESTART,
		  (&(struct rp1_pio_sm_restart_args){ enable_mask }));
	PIO_IOCTL(devc->fd, PIO_IOC_SM_CLKDIV_RESTART,
		  (&(struct rp1_pio_sm_clkdiv_restart_args){ enable_mask }));
	PIO_IOCTL(devc->fd, PIO_IOC_SM_ENABLE_SYNC,
		  (&(struct rp1_pio_sm_enable_sync_args){ enable_mask }));

	return SR_OK;
}

#define UNPACK_WORDS_IMMEDIATE(T, out_ptr, buf, words, N, base, is_first)  \
	do {                                                                   \
		const unsigned SPW = 32 / (N);                                     \
		const uint32_t MASK = (1u << (N)) - 1;                             \
		T *dst = (T *)(out_ptr);                                           \
		for (size_t w = 0; w < (words); w++) {                             \
			uint32_t word = (buf)[w];                                      \
			for (int sub = (int)SPW - 1; sub >= 0; sub--) {                \
				T val = (T)(((word >> (sub * (N))) & MASK)                 \
					    << (base));                                        \
				if (is_first)                                              \
					*dst++ = val;                                          \
				else                                                       \
					*dst++ |= val;                                         \
			}                                                              \
		}                                                                  \
	} while (0)

#define UNPACK_N(T, out, sm, is_first)                                     \
	do {                                                                   \
		switch ((sm)->n) {                                                 \
		case 1:                                                            \
			UNPACK_WORDS_IMMEDIATE(T, out, (sm)->buffer,                   \
					       (sm)->words, 1, (sm)->base,                     \
					       is_first);                                      \
			break;                                                         \
		case 2:                                                            \
			UNPACK_WORDS_IMMEDIATE(T, out, (sm)->buffer,                   \
					       (sm)->words, 2, (sm)->base,                     \
					       is_first);                                      \
			break;                                                         \
		case 4:                                                            \
			UNPACK_WORDS_IMMEDIATE(T, out, (sm)->buffer,                   \
					       (sm)->words, 4, (sm)->base,                     \
					       is_first);                                      \
			break;                                                         \
		case 8:                                                            \
			UNPACK_WORDS_IMMEDIATE(T, out, (sm)->buffer,                   \
					       (sm)->words, 8, (sm)->base,                     \
					       is_first);                                      \
			break;                                                         \
		case 16:                                                           \
			UNPACK_WORDS_IMMEDIATE(T, out, (sm)->buffer,                   \
					       (sm)->words, 16, (sm)->base,                    \
					       is_first);                                      \
			break;                                                         \
		default: {                                                         \
			T *dst = (T *)(out);                                           \
			const unsigned spw = (sm)->samples_per_word;                   \
			const unsigned n = (sm)->n;                                    \
			const uint32_t mask = (1u << n) - 1;                           \
			const unsigned base = (sm)->base;                              \
			for (size_t w = 0; w < (sm)->words; w++) {                     \
				uint32_t word = (sm)->buffer[w];                           \
				for (int sub = (int)spw - 1; sub >= 0;                     \
				     sub--) {                                              \
					T val = (T)(((word >> (sub * n)) &                     \
						     mask)                                         \
						    << base);                                      \
					if (is_first)                                          \
						*dst++ = val;                                      \
					else                                                   \
						*dst++ |= val;                                     \
				}                                                          \
			}                                                              \
			break;                                                         \
		}                                                                  \
		}                                                                  \
	} while (0)

static void mux_multi_sm_single_pass(struct dev_context *devc)
{
	uint8_t *out = devc->outbuf;

	if (!out || devc->num_sms == 0)
		return;

	for (unsigned i = 0; i < devc->num_sms; i++) {
		const struct la_sm *sm = &devc->sm[i];
		const gboolean is_first = (i == 0);

		switch (devc->unitsize) {
		case 1:
			UNPACK_N(uint8_t, out, sm, is_first);
			break;
		case 2:
			UNPACK_N(uint16_t, out, sm, is_first);
			break;
		case 4:
			UNPACK_N(uint32_t, out, sm, is_first);
			break;
		default:
			break;
		}
	}
}

static void process_and_send_data(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	size_t samples_avail = devc->samples_per_round;
	size_t samples_to_send = samples_avail;
	uint8_t *send_ptr = devc->outbuf;

	packet.type = SR_DF_LOGIC;
	packet.payload = &logic;
	logic.unitsize = devc->unitsize;

	if (!devc->trigger_fired && devc->stl) {
		int pre_trigger_samples = 0;
		int trigger_offset = soft_trigger_logic_check(
			devc->stl, devc->outbuf,
			devc->samples_per_round * devc->unitsize,
			&pre_trigger_samples);

		if (trigger_offset < 0)
			return;

		struct sr_datafeed_packet trig_packet = {
			.type = SR_DF_TRIGGER,
			.payload = NULL,
		};
		sr_session_send(sdi, &trig_packet);
		devc->trigger_fired = TRUE;

		devc->samples_sent += pre_trigger_samples;
		send_ptr =
			devc->outbuf + (size_t)trigger_offset * devc->unitsize;
		samples_to_send = samples_avail - (size_t)trigger_offset;
	}

	if (devc->limit_samples) {
		uint64_t remaining = devc->limit_samples - devc->samples_sent;
		if (samples_to_send > remaining)
			samples_to_send = remaining;
	}

	if (samples_to_send > 0) {
		logic.data = send_ptr;
		logic.length = samples_to_send * devc->unitsize;
		sr_session_send(sdi, &packet);
		devc->samples_sent += samples_to_send;
	}
}

SR_PRIV void la_stop(struct dev_context *devc)
{
	uint16_t mask = 0;
	unsigned i;

	for (i = 0; i < devc->num_sms; i++) {
		if (devc->sm[i].claimed >= 0)
			mask |= 1u << devc->sm[i].claimed;
	}

	if (g_atomic_int_get(&devc->running)) {
		g_atomic_int_set(&devc->running, 0);

		g_mutex_lock(&devc->sm_lock);
		g_cond_broadcast(&devc->sm_cond);
		g_mutex_unlock(&devc->sm_lock);

		if (devc->worker_thread) {
			g_thread_join(devc->worker_thread);
			devc->worker_thread = NULL;
		}

		g_mutex_clear(&devc->sm_lock);
		g_cond_clear(&devc->sm_cond);
	}

	if (mask) {
		PIO_IOCTL(devc->fd, PIO_IOC_SM_SET_ENABLED,
			  (&(struct rp1_pio_sm_set_enabled_args){
				  .mask = mask, .enable = false }));
		PIO_IOCTL(devc->fd, PIO_IOC_SM_UNCLAIM,
			  (&(struct rp1_pio_sm_claim_args){ mask }));
	}
	PIO_IOCTL(devc->fd, PIO_IOC_CLEAR_INSTR_MEM, 0);

	for (i = 0; i < devc->num_sms; i++) {
		g_free(devc->sm[i].buffer);
		devc->sm[i].buffer = NULL;
		devc->sm[i].claimed = -1;
	}
}

SR_PRIV int la_receive_data(int fd, int revents, void *cb_data)
{
	const struct sr_dev_inst *sdi = cb_data;
	struct dev_context *devc = sdi->priv;

	(void)fd;
	(void)revents;

	if (devc->event_fd >= 0) {
		uint64_t val;
		(void)read(devc->event_fd, &val, sizeof(val));
	}

	g_mutex_lock(&devc->sm_lock);
	if (!devc->buffers_full) {
		g_mutex_unlock(&devc->sm_lock);
		return G_SOURCE_CONTINUE;
	}

	mux_multi_sm_single_pass(devc);
	process_and_send_data(sdi);

	if (devc->limit_samples && devc->samples_sent >= devc->limit_samples) {
		sr_info("Requested sample limit reached.");
		g_mutex_unlock(&devc->sm_lock);
		sr_dev_acquisition_stop((struct sr_dev_inst *)sdi);
		return G_SOURCE_CONTINUE;
	}

	devc->buffers_full = FALSE;
	g_cond_signal(&devc->sm_cond);
	g_mutex_unlock(&devc->sm_lock);

	return G_SOURCE_CONTINUE;
}