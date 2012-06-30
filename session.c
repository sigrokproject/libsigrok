/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2010-2012 Bert Vermeulen <bert@biot.com>
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
#include "libsigrok.h"
#include "libsigrok-internal.h"

/* demo.c. TODO: Should not be global! */
extern SR_PRIV GIOChannel channels[2];

struct source {
	int timeout;
	sr_receive_data_callback_t cb;
	void *cb_data;

	/* This is used to keep track of the object (fd, pollfd or channel) which is
	 * being polled and will be used to match the source when removing it again.
	 */
	gintptr poll_object;
};

/* There can only be one session at a time. */
/* 'session' is not static, it's used elsewhere (via 'extern'). */
struct sr_session *session;
static int num_sources = 0;

/* Both "sources" and "pollfds" are of the same size and contain pairs of
 * descriptor and callback function. We can not embed the GPollFD into the
 * source struct since we want to be able to pass the array of all poll
 * descriptors to g_poll.
 */
static struct source *sources = NULL;
static GPollFD *pollfds;
static int source_timeout = -1;

/**
 * Create a new session.
 *
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

	g_slist_free(session->devs);
	session->devs = NULL;

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
SR_API int sr_session_dev_remove_all(void)
{
	if (!session) {
		sr_err("session: %s: session was NULL", __func__);
		return SR_ERR_BUG;
	}

	g_slist_free(session->devs);
	session->devs = NULL;

	return SR_OK;
}

/**
 * Add a device to the current session.
 *
 * @param dev The device to add to the current session. Must not be NULL.
 *            Also, dev->driver and dev->driver->dev_open must not be NULL.
 *
 * @return SR_OK upon success, SR_ERR_ARG upon invalid arguments.
 */
SR_API int sr_session_dev_add(struct sr_dev *dev)
{
	int ret;

	if (!dev) {
		sr_err("session: %s: dev was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (!session) {
		sr_err("session: %s: session was NULL", __func__);
		return SR_ERR_BUG;
	}

	/* If dev->driver is NULL, this is a virtual device. */
	if (!dev->driver) {
		sr_dbg("session: %s: dev->driver was NULL, this seems to be "
		       "a virtual device; continuing", __func__);
		/* Just add the device, don't run dev_open(). */
		session->devs = g_slist_append(session->devs, dev);
		return SR_OK;
	}

	/* dev->driver is non-NULL (i.e. we have a real device). */
	if (!dev->driver->dev_open) {
		sr_err("session: %s: dev->driver->dev_open was NULL",
		       __func__);
		return SR_ERR_BUG;
	}

	if ((ret = dev->driver->dev_open(dev->driver_index)) != SR_OK) {
		sr_err("session: %s: dev_open failed (%d)", __func__, ret);
		return ret;
	}

	session->devs = g_slist_append(session->devs, dev);

	return SR_OK;
}

/**
 * Remove all datafeed callbacks in the current session.
 *
 * @return SR_OK upon success, SR_ERR_BUG if no session exists.
 */
SR_API int sr_session_datafeed_callback_remove_all(void)
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
 * @param cb Function to call when a chunk of data is received.
 *           Must not be NULL.
 *
 * @return SR_OK upon success, SR_ERR_BUG if no session exists.
 */
SR_API int sr_session_datafeed_callback_add(sr_datafeed_callback_t cb)
{
	if (!session) {
		sr_err("session: %s: session was NULL", __func__);
		return SR_ERR_BUG;
	}

	if (!cb) {
		sr_err("session: %s: cb was NULL", __func__);
		return SR_ERR_ARG;
	}

	session->datafeed_callbacks =
	    g_slist_append(session->datafeed_callbacks, cb);

	return SR_OK;
}

/**
 * TODO.
 */
static int sr_session_run_poll(void)
{
	int ret, i;

	while (session->running) {
		ret = g_poll(pollfds, num_sources, source_timeout);

		for (i = 0; i < num_sources; i++) {
			if (pollfds[i].revents > 0 || (ret == 0
				&& source_timeout == sources[i].timeout)) {
				/*
				 * Invoke the source's callback on an event,
				 * or if the poll timeout out and this source
				 * asked for that timeout.
				 */
				if (!sources[i].cb(pollfds[i].fd, pollfds[i].revents,
						  sources[i].cb_data))
					sr_session_source_remove(pollfds[i].fd);
			}
		}
	}

	return SR_OK;
}

/**
 * Start a session.
 *
 * There can only be one session at a time.
 *
 * @return SR_OK upon success, SR_ERR upon errors.
 */
SR_API int sr_session_start(void)
{
	struct sr_dev *dev;
	GSList *l;
	int ret;

	if (!session) {
		sr_err("session: %s: session was NULL; a session must be "
		       "created first, before starting it.", __func__);
		return SR_ERR_BUG;
	}

	if (!session->devs) {
		/* TODO: Actually the case? */
		sr_err("session: %s: session->devs was NULL; a session "
		       "cannot be started without devices.", __func__);
		return SR_ERR_BUG;
	}

	/* TODO: Check driver_index validity? */

	sr_info("session: starting");

	for (l = session->devs; l; l = l->next) {
		dev = l->data;
		/* TODO: Check for dev != NULL. */
		if ((ret = dev->driver->dev_acquisition_start(
				dev->driver_index, dev)) != SR_OK) {
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

	if (!session->devs) {
		/* TODO: Actually the case? */
		sr_err("session: %s: session->devs was NULL; a session "
		       "cannot be run without devices.", __func__);
		return SR_ERR_BUG;
	}

	sr_info("session: running");
	session->running = TRUE;

	/* Do we have real sources? */
	if (num_sources == 1 && pollfds[0].fd == -1) {
		/* Dummy source, freewheel over it. */
		while (session->running)
			sources[0].cb(-1, 0, sources[0].cb_data);
	} else {
		/* Real sources, use g_poll() main loop. */
		sr_session_run_poll();
	}

	return SR_OK;
}

/**
 * Halt the current session.
 *
 * This requests the current session be stopped as soon as possible, for
 * example on receiving an SR_DF_END packet.
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
 * The current session is stopped immediately, with all acquisition sessions
 * being stopped and hardware drivers cleaned up.
 *
 * @return SR_OK upon success, SR_ERR_BUG if no session exists.
 */
SR_API int sr_session_stop(void)
{
	struct sr_dev *dev;
	GSList *l;

	if (!session) {
		sr_err("session: %s: session was NULL", __func__);
		return SR_ERR_BUG;
	}

	sr_info("session: stopping");
	session->running = FALSE;

	for (l = session->devs; l; l = l->next) {
		dev = l->data;
		/* Check for dev != NULL. */
		if (dev->driver) {
			if (dev->driver->dev_acquisition_stop)
				dev->driver->dev_acquisition_stop(dev->driver_index, dev);
			if (dev->driver->cleanup)
				dev->driver->cleanup();
		}
	}

	return SR_OK;
}

/**
 * Debug helper.
 *
 * @param packet The packet to show debugging information for.
 */
static void datafeed_dump(struct sr_datafeed_packet *packet)
{
	struct sr_datafeed_logic *logic;
	struct sr_datafeed_analog *analog;

	switch (packet->type) {
	case SR_DF_HEADER:
		sr_dbg("bus: received SR_DF_HEADER");
		break;
	case SR_DF_TRIGGER:
		sr_dbg("bus: received SR_DF_TRIGGER");
		break;
	case SR_DF_META_LOGIC:
		sr_dbg("bus: received SR_DF_META_LOGIC");
		break;
	case SR_DF_LOGIC:
		logic = packet->payload;
		/* TODO: Check for logic != NULL. */
		sr_dbg("bus: received SR_DF_LOGIC %" PRIu64 " bytes", logic->length);
		break;
	case SR_DF_META_ANALOG:
		sr_dbg("bus: received SR_DF_META_LOGIC");
		break;
	case SR_DF_ANALOG:
		analog = packet->payload;
		/* TODO: Check for analog != NULL. */
		sr_dbg("bus: received SR_DF_ANALOG %d samples", analog->num_samples);
		break;
	case SR_DF_END:
		sr_dbg("bus: received SR_DF_END");
		break;
	case SR_DF_FRAME_BEGIN:
		sr_dbg("bus: received SR_DF_FRAME_BEGIN");
		break;
	case SR_DF_FRAME_END:
		sr_dbg("bus: received SR_DF_FRAME_END");
		break;
	default:
		sr_dbg("bus: received unknown packet type %d", packet->type);
		break;
	}
}

/**
 * Send a packet to whatever is listening on the datafeed bus.
 *
 * Hardware drivers use this to send a data packet to the frontend.
 *
 * @param dev TODO.
 * @param packet The datafeed packet to send to the session bus.
 *
 * @return SR_OK upon success, SR_ERR_ARG upon invalid arguments.
 */
SR_PRIV int sr_session_send(struct sr_dev *dev,
			    struct sr_datafeed_packet *packet)
{
	GSList *l;
	sr_datafeed_callback_t cb;

	if (!dev) {
		sr_err("session: %s: dev was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (!packet) {
		sr_err("session: %s: packet was NULL", __func__);
		return SR_ERR_ARG;
	}

	for (l = session->datafeed_callbacks; l; l = l->next) {
		if (sr_log_loglevel_get() >= SR_LOG_DBG)
			datafeed_dump(packet);
		cb = l->data;
		/* TODO: Check for cb != NULL. */
		cb(dev, packet);
	}

	return SR_OK;
}

static int _sr_session_source_add(GPollFD *pollfd, int timeout,
		sr_receive_data_callback_t cb, void *cb_data, gintptr poll_object)
{
	struct source *new_sources, *s;
	GPollFD *new_pollfds;

	if (!cb) {
		sr_err("session: %s: cb was NULL", __func__);
		return SR_ERR_ARG;
	}

	/* Note: cb_data can be NULL, that's not a bug. */

	new_pollfds = g_try_realloc(pollfds, sizeof(GPollFD) * (num_sources + 1));
	if (!new_pollfds) {
		sr_err("session: %s: new_sources malloc failed", __func__);
		return SR_ERR_MALLOC;
	}

	new_sources = g_try_realloc(sources, sizeof(struct source) * (num_sources + 1));
	if (!new_sources) {
		sr_err("session: %s: new_sources malloc failed", __func__);
		return SR_ERR_MALLOC;
	}

	new_pollfds[num_sources] = *pollfd;
	s = &new_sources[num_sources++];
	s->timeout = timeout;
	s->cb = cb;
	s->cb_data = cb_data;
	s->poll_object = poll_object;
	pollfds = new_pollfds;
	sources = new_sources;

	if (timeout != source_timeout && timeout > 0
	    && (source_timeout == -1 || timeout < source_timeout))
		source_timeout = timeout;

	return SR_OK;
}

/**
 * Add a event source for a file descriptor.
 *
 * @param fd The file descriptor.
 * @param events Events to check for.
 * @param timeout Max time to wait before the callback is called, ignored if 0.
 * @param cb Callback function to add. Must not be NULL.
 * @param cb_data Data for the callback function. Can be NULL.
 *
 * @return SR_OK upon success, SR_ERR_ARG upon invalid arguments, or
 *         SR_ERR_MALLOC upon memory allocation errors.
 */
SR_API int sr_session_source_add(int fd, int events, int timeout,
		sr_receive_data_callback_t cb, void *cb_data)
{
	GPollFD p;

#ifdef _WIN32
	g_io_channel_win32_make_pollfd(&channels[0],
			events, &p);
#else
	p.fd = fd;
	p.events = events;
#endif

	return _sr_session_source_add(&p, timeout, cb, cb_data, (gintptr)fd);
}

/**
 * Add a event source for a GPollFD
 *
 * TODO: More error checks etc.
 *
 * @param pollfd The GPollFD.
 * @param timeout Max time to wait before the callback is called, ignored if 0.
 * @param cb Callback function to add. Must not be NULL.
 * @param cb_data Data for the callback function. Can be NULL.
 *
 * @return SR_OK upon success, SR_ERR_ARG upon invalid arguments, or
 *         SR_ERR_MALLOC upon memory allocation errors.
 */
SR_API int sr_session_source_add_pollfd(GPollFD *pollfd, int timeout,
		sr_receive_data_callback_t cb, void *cb_data)
{
	return _sr_session_source_add(pollfd, timeout, cb, cb_data, (gintptr)pollfd);
}

/**
 * Add a event source for a GIOChannel
 *
 * TODO: More error checks etc.
 *
 * @param channel The GIOChannel.
 * @param events Events to poll on.
 * @param timeout Max time to wait before the callback is called, ignored if 0.
 * @param cb Callback function to add. Must not be NULL.
 * @param cb_data Data for the callback function. Can be NULL.
 *
 * @return SR_OK upon success, SR_ERR_ARG upon invalid arguments, or
 *         SR_ERR_MALLOC upon memory allocation errors.
 */
SR_API int sr_session_source_add_channel(GIOChannel *channel, int events, int timeout,
		sr_receive_data_callback_t cb, void *cb_data)
{
	GPollFD p;

#ifdef _WIN32
	g_io_channel_win32_make_pollfd(channel,
			events, &p);
#else
	p.fd = g_io_channel_unix_get_fd(channel);
	p.events = events;
#endif

	return _sr_session_source_add(&p, timeout, cb, cb_data, (gintptr)channel);
}


static int _sr_session_source_remove(gintptr poll_object)
{
	struct source *new_sources;
	GPollFD *new_pollfds;
	int old;

	if (!sources) {
		sr_err("session: %s: sources was NULL", __func__);
		return SR_ERR_BUG;
	}

	for (old = 0; old < num_sources; old++) {
		if (sources[old].poll_object == poll_object)
			break;
	}

	/* fd not found, nothing to do */
	if (old == num_sources)
		return SR_OK;

	num_sources -= 1;

	if (old != num_sources) {
		memmove(&pollfds[old], &pollfds[old+1],
			(num_sources - old) * sizeof(GPollFD));
		memmove(&sources[old], &sources[old+1],
			(num_sources - old) * sizeof(struct source));
	}

	new_pollfds = g_try_realloc(sources, sizeof(GPollFD) * num_sources);
	if (!new_pollfds && num_sources > 0) {
		sr_err("session: %s: new_sources malloc failed", __func__);
		return SR_ERR_MALLOC;
	}

	new_sources = g_try_realloc(sources, sizeof(struct source) * num_sources);
	if (!new_sources && num_sources > 0) {
		sr_err("session: %s: new_sources malloc failed", __func__);
		return SR_ERR_MALLOC;
	}

	pollfds = new_pollfds;
	sources = new_sources;

	return SR_OK;
}

/*
 * Remove the source belonging to the specified file descriptor.
 *
 * TODO: More error checks.
 *
 * @param fd: The file descriptor for which the source should be removed.
 *
 * @return SR_OK upon success, SR_ERR_ARG upon invalid arguments, or
 *         SR_ERR_MALLOC upon memory allocation errors, SR_ERR_BUG upon
 *         internal errors.
 */
SR_API int sr_session_source_remove(int fd)
{
	return _sr_session_source_remove((gintptr)fd);
}

/**
 * Remove the source belonging to the specified poll descriptor.
 *
 * TODO: More error checks.
 *
 * @param pollfd The poll descriptor for which the source should be removed.
 *
 * @return SR_OK upon success, SR_ERR_ARG upon invalid arguments, or
 *         SR_ERR_MALLOC upon memory allocation errors, SR_ERR_BUG upon
 *         internal errors.
 */
SR_API int sr_session_source_remove_pollfd(GPollFD *pollfd)
{
	return _sr_session_source_remove((gintptr)pollfd);
}

/*
 * Remove the source belonging to the specified channel.
 *
 * TODO: More error checks.
 *
 * @parama channel: The channel for which the source should be removed.
 *
 * @return SR_OK upon success, SR_ERR_ARG upon invalid arguments, or
 *         SR_ERR_MALLOC upon memory allocation errors, SR_ERR_BUG upon
 *         internal errors.
 */
SR_API int sr_session_source_remove_channel(GIOChannel *channel)
{
	return _sr_session_source_remove((gintptr)channel);
}
