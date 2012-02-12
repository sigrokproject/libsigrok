/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2012 Bert Vermeulen <bert@biot.com>
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <glib.h>
#include "sigrok.h"
#include "sigrok-internal.h"

/* demo.c. TODO: Should not be global! */
extern SR_PRIV GIOChannel channels[2];

struct source {
	int fd;
	int events;
	int timeout;
	sr_receive_data_callback cb;
	void *user_data;
};

/* There can only be one session at a time. */
/* 'session' is not static, it's used elsewhere (via 'extern'). */
struct sr_session *session;
static int num_sources = 0;

static struct source *sources = NULL;
static int source_timeout = -1;

/**
 * Create a new session.
 *
 * TODO.
 *
 * TODO: Should return int?
 * TODO: Should it use the file-global "session" variable or take an argument?
 *       The same question applies to all the other session functions.
 *
 * @return A pointer to the newly allocated session, or NULL upon errors.
 */
SR_API struct sr_session *sr_session_new(void)
{
	if (!(session = g_try_malloc0(sizeof(struct sr_session)))) {
		sr_err("session: %s: session malloc failed", __func__);
		return NULL; /* TODO: SR_ERR_MALLOC? */
	}

	return session;
}

/**
 * Destroy the current session.
 *
 * This frees up all memory used by the session.
 *
 * @return SR_OK upon success, SR_ERR_BUG if no session exists.
 */
SR_API int sr_session_destroy(void)
{
	if (!session) {
		sr_err("session: %s: session was NULL", __func__);
		return SR_ERR_BUG;
	}

	g_slist_free(session->devices);
	session->devices = NULL;

	/* TODO: Error checks needed? */

	/* TODO: Loop over protocol decoders and free them. */

	g_free(session);
	session = NULL;

	return SR_OK;
}

/**
 * Remove all the devices from the current session. TODO?
 *
 * The session itself (i.e., the struct sr_session) is not free'd and still
 * exists after this function returns.
 *
 * @return SR_OK upon success, SR_ERR_BUG if no session exists.
 */
SR_API int sr_session_device_clear(void)
{
	if (!session) {
		sr_err("session: %s: session was NULL", __func__);
		return SR_ERR_BUG;
	}

	g_slist_free(session->devices);
	session->devices = NULL;

	return SR_OK;
}

/**
 * Add a device to the current session.
 *
 * @param device The device to add to the current session. Must not be NULL.
 *               Also, device->plugin and device->plugin->opendev must not
 *               be NULL.
 *
 * @return SR_OK upon success, SR_ERR_ARG upon invalid arguments.
 */
SR_API int sr_session_device_add(struct sr_device *device)
{
	int ret;

	if (!device) {
		sr_err("session: %s: device was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (!device->plugin) {
		sr_err("session: %s: device->plugin was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (!device->plugin->opendev) {
		sr_err("session: %s: device->plugin->opendev was NULL",
		       __func__);
		return SR_ERR_ARG;
	}

	if (!session) {
		sr_err("session: %s: session was NULL", __func__);
		return SR_ERR; /* TODO: SR_ERR_BUG? */
	}

	if ((ret = device->plugin->opendev(device->plugin_index)) != SR_OK) {
		sr_err("session: %s: opendev failed (%d)", __func__, ret);
		return ret;
	}

	session->devices = g_slist_append(session->devices, device);

	return SR_OK;
}

/**
 * Clear all datafeed callbacks in the current session.
 *
 * TODO.
 *
 * @return SR_OK upon success, SR_ERR_BUG if no session exists.
 */
SR_API int sr_session_datafeed_callback_clear(void)
{
	if (!session) {
		sr_err("session: %s: session was NULL", __func__);
		return SR_ERR_BUG;
	}

	g_slist_free(session->datafeed_callbacks);
	session->datafeed_callbacks = NULL;

	return SR_OK;
}

/**
 * Add a datafeed callback to the current session.
 *
 * @param callback TODO.
 * @return SR_OK upon success, SR_ERR_BUG if no session exists.
 */
SR_API int sr_session_datafeed_callback_add(sr_datafeed_callback callback)
{
	if (!session) {
		sr_err("session: %s: session was NULL", __func__);
		return SR_ERR_BUG;
	}

	/* TODO: Is 'callback' allowed to be NULL? */

	session->datafeed_callbacks =
	    g_slist_append(session->datafeed_callbacks, callback);

	return SR_OK;
}

/**
 * TODO.
 */
static int sr_session_run_poll(void)
{
	GPollFD *fds, my_gpollfd;
	int ret, i;

	fds = NULL;
	while (session->running) {
		/* TODO: Add comment. */
		g_free(fds);

		/* Construct g_poll()'s array. */
		if (!(fds = g_try_malloc(sizeof(GPollFD) * num_sources))) {
			sr_err("session: %s: fds malloc failed", __func__);
			return SR_ERR_MALLOC;
		}
		for (i = 0; i < num_sources; i++) {
#ifdef _WIN32
			g_io_channel_win32_make_pollfd(&channels[0],
					sources[i].events, &my_gpollfd);
#else
			my_gpollfd.fd = sources[i].fd;
			my_gpollfd.events = sources[i].events;
			fds[i] = my_gpollfd;
#endif
		}

		ret = g_poll(fds, num_sources, source_timeout);

		for (i = 0; i < num_sources; i++) {
			if (fds[i].revents > 0 || (ret == 0
				&& source_timeout == sources[i].timeout)) {
				/*
				 * Invoke the source's callback on an event,
				 * or if the poll timeout out and this source
				 * asked for that timeout.
				 */
				if (!sources[i].cb(fds[i].fd, fds[i].revents,
						  sources[i].user_data))
					sr_session_source_remove(sources[i].fd);
			}
		}
	}
	g_free(fds);

	return SR_OK;
}

/**
 * Start a session.
 *
 * There can only be one session at a time. TODO
 *
 * @return SR_OK upon success, SR_ERR upon errors.
 */
SR_API int sr_session_start(void)
{
	struct sr_device *device;
	GSList *l;
	int ret;

	if (!session) {
		sr_err("session: %s: session was NULL; a session must be "
		       "created first, before starting it.", __func__);
		return SR_ERR; /* TODO: SR_ERR_BUG? */
	}

	if (!session->devices) {
		/* TODO: Actually the case? */
		sr_err("session: %s: session->devices was NULL; a session "
		       "cannot be started without devices.", __func__);
		return SR_ERR; /* TODO: SR_ERR_BUG? */
	}

	/* TODO: Check plugin_index validity? */

	sr_info("session: starting");

	for (l = session->devices; l; l = l->next) {
		device = l->data;
		/* TODO: Check for device != NULL. */
		if ((ret = device->plugin->start_acquisition(
				device->plugin_index, device)) != SR_OK) {
			sr_err("session: %s: could not start an acquisition "
			       "(%d)", __func__, ret);
			break;
		}
	}

	/* TODO: What if there are multiple devices? Which return code? */

	return ret;
}

/**
 * Run the session.
 *
 * TODO: Various error checks etc.
 *
 * @return SR_OK upon success, SR_ERR_BUG upon errors.
 */
SR_API int sr_session_run(void)
{
	if (!session) {
		sr_err("session: %s: session was NULL; a session must be "
		       "created first, before running it.", __func__);
		return SR_ERR_BUG;
	}

	if (!session->devices) {
		/* TODO: Actually the case? */
		sr_err("session: %s: session->devices was NULL; a session "
		       "cannot be run without devices.", __func__);
		return SR_ERR_BUG;
	}

	sr_info("session: running");
	session->running = TRUE;

	/* Do we have real sources? */
	if (num_sources == 1 && sources[0].fd == -1) {
		/* Dummy source, freewheel over it. */
		while (session->running)
			sources[0].cb(-1, 0, sources[0].user_data);
	} else {
		/* Real sources, use g_poll() main loop. */
		sr_session_run_poll();
	}

	return SR_OK;
}

/**
 * Halt the current session.
 *
 * TODO.
 *
 * @return SR_OK upon success, SR_ERR_BUG if no session exists.
 */
SR_API int sr_session_halt(void)
{
	if (!session) {
		sr_err("session: %s: session was NULL", __func__);
		return SR_ERR_BUG;
	}

	sr_info("session: halting");
	session->running = FALSE;

	return SR_OK;
}

/**
 * Stop the current session.
 *
 * TODO: Difference to halt?
 *
 * @return SR_OK upon success, SR_ERR_BUG if no session exists.
 */
SR_API int sr_session_stop(void)
{
	struct sr_device *device;
	GSList *l;

	if (!session) {
		sr_err("session: %s: session was NULL", __func__);
		return SR_ERR_BUG;
	}

	sr_info("session: stopping");
	session->running = FALSE;

	for (l = session->devices; l; l = l->next) {
		device = l->data;
		/* Check for device != NULL. */
		if (device->plugin) {
			if (device->plugin->stop_acquisition)
				device->plugin->stop_acquisition(device->plugin_index, device);
			if (device->plugin->cleanup)
				device->plugin->cleanup();
		}
	}

	return SR_OK;
}

/**
 * @brief debug helper
 *
 * @param packet TODO.
 *
 */
static void datafeed_dump(struct sr_datafeed_packet *packet)
{
	struct sr_datafeed_logic *logic;

	switch (packet->type) {
	case SR_DF_HEADER:
		sr_dbg("bus: received SR_DF_HEADER");
		break;
	case SR_DF_TRIGGER:
		sr_dbg("bus: received SR_DF_TRIGGER");
		break;
	case SR_DF_LOGIC:
		logic = packet->payload;
		/* TODO: Check for logic != NULL. */
		sr_dbg("bus: received SR_DF_LOGIC %" PRIu64 " bytes", logic->length);
		break;
	case SR_DF_END:
		sr_dbg("bus: received SR_DF_END");
		break;
	default:
		sr_dbg("bus: received unknown packet type %d", packet->type);
		break;
	}

}

/**
 * TODO.
 *
 * @param device TODO.
 * @param packet TODO.
 * @return SR_OK upon success, SR_ERR_ARG upon invalid arguments.
 */
SR_API int sr_session_bus(struct sr_device *device,
			  struct sr_datafeed_packet *packet)
{
	GSList *l;
	sr_datafeed_callback cb;

	if (!device) {
		sr_err("session: %s: device was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (!device->plugin) {
		sr_err("session: %s: device->plugin was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (!packet) {
		sr_err("session: %s: packet was NULL", __func__);
		return SR_ERR_ARG;
	}

	/*
	 * TODO: Send packet through PD pipe, and send the output of that to
	 * the callbacks as well.
	 */
	for (l = session->datafeed_callbacks; l; l = l->next) {
		if (sr_log_loglevel_get() >= SR_LOG_DBG)
			datafeed_dump(packet);
		cb = l->data;
		/* TODO: Check for cb != NULL. */
		cb(device, packet);
	}

	return SR_OK;
}

/**
 * TODO.
 *
 * TODO: More error checks etc.
 *
 * @param fd TODO.
 * @param events TODO.
 * @param timeout TODO.
 * @param callback TODO.
 * @param user_data TODO.
 * @return SR_OK upon success, SR_ERR_ARG upon invalid arguments, or
 *         SR_ERR_MALLOC upon memory allocation errors.
 */
SR_API int sr_session_source_add(int fd, int events, int timeout,
		sr_receive_data_callback callback, void *user_data)
{
	struct source *new_sources, *s;

	if (!callback) {
		sr_err("session: %s: callback was NULL", __func__);
		return SR_ERR_ARG;
	}

	/* Note: user_data can be NULL, that's not a bug. */

	new_sources = g_try_malloc0(sizeof(struct source) * (num_sources + 1));
	if (!new_sources) {
		sr_err("session: %s: new_sources malloc failed", __func__);
		return SR_ERR_MALLOC;
	}

	if (sources) {
		memcpy(new_sources, sources,
		       sizeof(struct source) * num_sources);
		g_free(sources);
	}

	s = &new_sources[num_sources++];
	s->fd = fd;
	s->events = events;
	s->timeout = timeout;
	s->cb = callback;
	s->user_data = user_data;
	sources = new_sources;

	if (timeout != source_timeout && timeout > 0
	    && (source_timeout == -1 || timeout < source_timeout))
		source_timeout = timeout;

	return SR_OK;
}

/**
 * Remove the source belonging to the specified file descriptor.
 *
 * TODO: More error checks.
 *
 * @param fd TODO.
 * @return SR_OK upon success, SR_ERR_ARG upon invalid arguments, or
 *         SR_ERR_MALLOC upon memory allocation errors, SR_ERR_BUG upon
 *         internal errors.
 */
SR_API int sr_session_source_remove(int fd)
{
	struct source *new_sources;
	int old, new;

	if (!sources) {
		sr_err("session: %s: sources was NULL", __func__);
		return SR_ERR_BUG; /* TODO: Other? */
	}

	/* TODO: Check if 'fd' valid. */

	new_sources = g_try_malloc0(sizeof(struct source) * num_sources);
	if (!new_sources) {
		sr_err("session: %s: new_sources malloc failed", __func__);
		return SR_ERR_MALLOC;
	}

	for (old = 0, new = 0; old < num_sources; old++) {
		if (sources[old].fd != fd)
			memcpy(&new_sources[new++], &sources[old],
			       sizeof(struct source));
	}

	if (old != new) {
		g_free(sources);
		sources = new_sources;
		num_sources--;
	} else {
		/* Target fd was not found. */
		g_free(new_sources);
	}

	return SR_OK;
}
