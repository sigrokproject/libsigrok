/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2026 PXView Authors
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
#include <glib.h>
#ifdef HAVE_LIBUSB_1_0
#include <libusb.h>
#endif
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

/** @cond PRIVATE */
#define LOG_PREFIX "hotplug"
/** @endcond */

#ifdef HAVE_LIBUSB_1_0

/** Private hotplug state, opaque to the public API. */
struct sr_hotplug_state {
	libusb_hotplug_callback_handle hp_handle;
	GThread *hp_thread;
	volatile gboolean hp_running;
	sr_hotplug_callback user_cb;
	void *user_data;
};

/** libusb hotplug callback — runs on libusb event thread. */
static int LIBUSB_CALL sr_hotplug_libusb_cb(libusb_context *ctx,
		libusb_device *dev, libusb_hotplug_event event, void *user_data)
{
	struct sr_context *sr_ctx = (struct sr_context *)user_data;
	int ev;

	(void)ctx;
	(void)dev;

	if (!sr_ctx || !sr_ctx->hotplug_state)
		return 0;

	if (event == LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED)
		ev = SR_HOTPLUG_ATTACH;
	else if (event == LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT)
		ev = SR_HOTPLUG_DETACH;
	else
		return 0;

	if (sr_ctx->hotplug_state->user_cb)
		sr_ctx->hotplug_state->user_cb(ev, sr_ctx->hotplug_state->user_data);

	return 0;
}

/** Hotplug listener thread — pumps libusb events so callbacks fire. */
static gpointer sr_hotplug_thread_proc(gpointer data)
{
	struct sr_context *sr_ctx = (struct sr_context *)data;
	struct timeval tv = { 0, 100000 }; /* 100ms */

	sr_info("Hotplug listener thread start");

	while (sr_ctx->hotplug_state->hp_running) {
		libusb_handle_events_timeout(sr_ctx->libusb_ctx, &tv);
	}

	sr_info("Hotplug listener thread end");

	return NULL;
}

/**
 * Register a hotplug callback for USB device attach/detach events.
 *
 * @param ctx libsigrok context. Must not be NULL.
 * @param cb Callback invoked on attach/detach events. Receives
 *           SR_HOTPLUG_ATTACH or SR_HOTPLUG_DETACH plus the user_data pointer.
 * @param user_data opaque pointer passed to the callback.
 *
 * @retval SR_OK Success
 * @retval SR_ERR platform does not support libusb hotplug, or already listening
 *
 * @since 0.6.0
 */
SR_API int sr_listen_hotplug(struct sr_context *ctx,
		sr_hotplug_callback cb, void *user_data)
{
	int r;

	if (!ctx) {
		sr_err("%s(): libsigrok context was NULL.", __func__);
		return SR_ERR;
	}
	if (!cb) {
		sr_err("%s(): callback was NULL.", __func__);
		return SR_ERR;
	}

#if defined(LIBUSB_API_VERSION) && (LIBUSB_API_VERSION >= 0x01000102)
	if (!libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG)) {
		sr_warn("libusb hotplug capability not available; "
			"USB attach/detach events will not be detected.");
		return SR_ERR;
	}
#endif

	if (ctx->hotplug_state) {
		sr_warn("%s(): already listening, ignoring duplicate call.", __func__);
		return SR_OK;
	}

	struct sr_hotplug_state *st = g_malloc0(sizeof(*st));
	st->user_cb = cb;
	st->user_data = user_data;
	st->hp_running = TRUE;

	r = libusb_hotplug_register_callback(ctx->libusb_ctx,
			(libusb_hotplug_event)(LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED
				| LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT),
			LIBUSB_HOTPLUG_ENUMERATE,
			LIBUSB_HOTPLUG_MATCH_ANY,
			LIBUSB_HOTPLUG_MATCH_ANY,
			LIBUSB_HOTPLUG_MATCH_ANY,
			sr_hotplug_libusb_cb, ctx, &st->hp_handle);
	if (r != LIBUSB_SUCCESS) {
		sr_err("libusb_hotplug_register_callback failed: %s.",
			libusb_error_name(r));
		g_free(st);
		return SR_ERR;
	}

	ctx->hotplug_state = st;
	st->hp_thread = g_thread_new("sr_hotplug", sr_hotplug_thread_proc, ctx);

	sr_info("Hotplug listener registered.");
	return SR_OK;
}

/**
 * Stop hotplug listening and join the background thread.
 *
 * Safe to call multiple times (idempotent): if not listening, returns SR_OK.
 *
 * @param ctx libsigrok context. Must not be NULL.
 *
 * @retval SR_OK Success
 * @retval SR_ERR context was NULL
 *
 * @since 0.6.0
 */
SR_API int sr_close_hotplug(struct sr_context *ctx)
{
	struct sr_hotplug_state *st;

	if (!ctx) {
		sr_err("%s(): libsigrok context was NULL.", __func__);
		return SR_ERR;
	}
	if (!ctx->hotplug_state)
		return SR_OK;

	st = ctx->hotplug_state;
	st->hp_running = FALSE;

	libusb_hotplug_deregister_callback(ctx->libusb_ctx, st->hp_handle);

	if (st->hp_thread) {
		g_thread_join(st->hp_thread);
		st->hp_thread = NULL;
	}

	g_free(st);
	ctx->hotplug_state = NULL;

	sr_info("Hotplug listener stopped.");
	return SR_OK;
}

#endif /* HAVE_LIBUSB_1_0 */
