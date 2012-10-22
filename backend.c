/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2010-2012 Bert Vermeulen <bert@biot.com>
 * Copyright (C) 2012 Peter Stuge <peter@stuge.se>
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

#include <glib.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"

/**
 * Initialize libsigrok.
 *
 * This function must be called before any other libsigrok function.
 *
 * @param ctx Pointer to a libsigrok context struct pointer. Must not be NULL.
 *            This will be a pointer to a newly allocated libsigrok context
 *            object upon success, and is undefined upon errors.
 *
 * @return SR_OK upon success, a (negative) error code otherwise. Upon errors
 *         the 'ctx' pointer is undefined and should not be used. Upon success,
 *         the context will be free'd by sr_exit() as part of the libsigrok
 *         shutdown.
 */
SR_API int sr_init(struct sr_context **ctx)
{
	int ret = SR_ERR;
	struct sr_context *context;

	if (!ctx) {
		sr_err("%s(): libsigrok context was NULL.", __func__);
		return SR_ERR;
	}

	/* + 1 to handle when struct sr_context has no members. */
	context = g_try_malloc0(sizeof(struct sr_context) + 1);

	if (!context) {
		ret = SR_ERR_MALLOC;
		goto done;
	}

#ifdef HAVE_LIBUSB_1_0
	ret = libusb_init(&context->libusb_ctx);
	if (LIBUSB_SUCCESS != ret) {
		sr_err("libusb_init() returned %s.\n", libusb_error_name(ret));
		goto done;
	}
#endif

	*ctx = context;
	ret = SR_OK;

done:
	return ret;
}

/**
 * Shutdown libsigrok.
 *
 * @param ctx Pointer to a libsigrok context struct. Must not be NULL.
 *
 * @return SR_OK upon success, a (negative) error code otherwise.
 */
SR_API int sr_exit(struct sr_context *ctx)
{
	if (!ctx) {
		sr_err("%s(): libsigrok context was NULL.", __func__);
		return SR_ERR;
	}

	sr_hw_cleanup_all();

#ifdef HAVE_LIBUSB_1_0
	libusb_exit(ctx->libusb_ctx);
#endif

	g_free(ctx);

	return SR_OK;
}
