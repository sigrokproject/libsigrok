/*
 * This file is part of the libsigrok project.
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

/** @cond PRIVATE */
#define LOG_PREFIX "session"
/** @endcond */

/**
 * @file
 *
 * Creating, using, or destroying libsigrok sessions.
 */

/**
 * @defgroup grp_session Session handling
 *
 * Creating, using, or destroying libsigrok sessions.
 *
 * @{
 */

struct source {
	int timeout;
	sr_receive_data_callback cb;
	void *cb_data;

	/* This is used to keep track of the object (fd, pollfd or channel) which is
	 * being polled and will be used to match the source when removing it again.
	 */
	gintptr poll_object;
};

struct datafeed_callback {
	sr_datafeed_callback cb;
	void *cb_data;
};

/* There can currently only be one session at a time. */
/* 'sr_current_session' is not static, it's used elsewhere (via 'extern'). */
struct sr_session *sr_current_session;

/**
 * Create a new session.
 * Currently, there can be only one session at a time within the same process.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_BUG A session exists already.
 *
 * @since 0.4.0
 */
SR_API int sr_session_new(struct sr_session **session)
{
	if (sr_current_session) {
		sr_err("%s: session already exists", __func__);
		return SR_ERR_BUG;
	}

	sr_current_session = g_malloc0(sizeof(struct sr_session));

	sr_current_session->source_timeout = -1;
	sr_current_session->running = FALSE;
	sr_current_session->abort_session = FALSE;
	g_mutex_init(&sr_current_session->stop_mutex);

	*session = sr_current_session;

	return SR_OK;
}

/**
 * Destroy a session.
 * This frees up all memory used by the session.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_ARG Invalid session passed.
 *
 * @since 0.4.0
 */
SR_API int sr_session_destroy(struct sr_session *session)
{
	if (!session) {
		sr_err("%s: session was NULL", __func__);
		return SR_ERR_ARG;
	}

	sr_session_dev_remove_all(session);
	g_mutex_clear(&session->stop_mutex);
	if (session->trigger)
		sr_trigger_free(session->trigger);

	g_free(session);

	if (session == sr_current_session)
		sr_current_session = NULL;

	return SR_OK;
}

/**
 * Remove all the devices from a session.
 *
 * The session itself (i.e., the struct sr_session) is not free'd and still
 * exists after this function returns.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_BUG Invalid session passed.
 *
 * @since 0.4.0
 */
SR_API int sr_session_dev_remove_all(struct sr_session *session)
{
	struct sr_dev_inst *sdi;
	GSList *l;

	if (!session) {
		sr_err("%s: session was NULL", __func__);
		return SR_ERR_ARG;
	}

	for (l = session->devs; l; l = l->next) {
		sdi = (struct sr_dev_inst *) l->data;
		sdi->session = NULL;
	}

	g_slist_free(session->devs);
	session->devs = NULL;

	return SR_OK;
}

/**
 * Add a device instance to a session.
 *
 * @param sdi The device instance to add to a session. Must not
 *            be NULL. Also, sdi->driver and sdi->driver->dev_open must
 *            not be NULL.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_ARG Invalid argument.
 *
 * @since 0.4.0
 */
SR_API int sr_session_dev_add(struct sr_session *session,
		struct sr_dev_inst *sdi)
{
	int ret;

	if (!sdi) {
		sr_err("%s: sdi was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (!session) {
		sr_err("%s: session was NULL", __func__);
		return SR_ERR_ARG;
	}

	/* If sdi->session is not NULL, the device is already in this or
	 * another session. */
	if (sdi->session) {
		sr_err("%s: already assigned to session", __func__);
		return SR_ERR_ARG;
	}

	/* If sdi->driver is NULL, this is a virtual device. */
	if (!sdi->driver) {
		sr_dbg("%s: sdi->driver was NULL, this seems to be "
		       "a virtual device; continuing", __func__);
		/* Just add the device, don't run dev_open(). */
		session->devs = g_slist_append(session->devs, (gpointer)sdi);
		sdi->session = session;
		return SR_OK;
	}

	/* sdi->driver is non-NULL (i.e. we have a real device). */
	if (!sdi->driver->dev_open) {
		sr_err("%s: sdi->driver->dev_open was NULL", __func__);
		return SR_ERR_BUG;
	}

	session->devs = g_slist_append(session->devs, (gpointer)sdi);
	sdi->session = session;

	if (session->running) {
		/* Adding a device to a running session. Commit settings
		 * and start acquisition on that device now. */
		if ((ret = sr_config_commit(sdi)) != SR_OK) {
			sr_err("Failed to commit device settings before "
			       "starting acquisition in running session (%s)",
			       sr_strerror(ret));
			return ret;
		}
		if ((ret = sdi->driver->dev_acquisition_start(sdi,
						(void *)sdi)) != SR_OK) {
			sr_err("Failed to start acquisition of device in "
			       "running session (%s)", sr_strerror(ret));
			return ret;
		}
	}

	return SR_OK;
}

/**
 * List all device instances attached to a session.
 *
 * @param devlist A pointer where the device instance list will be
 *                stored on return. If no devices are in the session,
 *                this will be NULL. Each element in the list points
 *                to a struct sr_dev_inst *.
 *                The list must be freed by the caller, but not the
 *                elements pointed to.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_ARG Invalid argument.
 *
 * @since 0.4.0
 */
SR_API int sr_session_dev_list(struct sr_session *session, GSList **devlist)
{
	if (!session)
		return SR_ERR_ARG;

	if (!devlist)
		return SR_ERR_ARG;

	*devlist = g_slist_copy(session->devs);

	return SR_OK;
}

/**
 * Remove all datafeed callbacks in a session.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_ARG Invalid session passed.
 *
 * @since 0.4.0
 */
SR_API int sr_session_datafeed_callback_remove_all(struct sr_session *session)
{
	if (!session) {
		sr_err("%s: session was NULL", __func__);
		return SR_ERR_ARG;
	}

	g_slist_free_full(session->datafeed_callbacks, g_free);
	session->datafeed_callbacks = NULL;

	return SR_OK;
}

/**
 * Add a datafeed callback to a session.
 *
 * @param cb Function to call when a chunk of data is received.
 *           Must not be NULL.
 * @param cb_data Opaque pointer passed in by the caller.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_BUG No session exists.
 *
 * @since 0.3.0
 */
SR_API int sr_session_datafeed_callback_add(struct sr_session *session,
		sr_datafeed_callback cb, void *cb_data)
{
	struct datafeed_callback *cb_struct;

	if (!session) {
		sr_err("%s: session was NULL", __func__);
		return SR_ERR_BUG;
	}

	if (!cb) {
		sr_err("%s: cb was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (!(cb_struct = g_try_malloc0(sizeof(struct datafeed_callback))))
		return SR_ERR_MALLOC;

	cb_struct->cb = cb;
	cb_struct->cb_data = cb_data;

	session->datafeed_callbacks =
	    g_slist_append(session->datafeed_callbacks, cb_struct);

	return SR_OK;
}

SR_API struct sr_trigger *sr_session_trigger_get(struct sr_session *session)
{
	return session->trigger;
}

SR_API int sr_session_trigger_set(struct sr_session *session, struct sr_trigger *trig)
{
	session->trigger = trig;

	return SR_OK;
}

/**
 * Call every device in the current session's callback.
 *
 * For sessions not driven by select loops such as sr_session_run(),
 * but driven by another scheduler, this can be used to poll the devices
 * from within that scheduler.
 *
 * @param block If TRUE, this call will wait for any of the session's
 *              sources to fire an event on the file descriptors, or
 *              any of their timeouts to activate. In other words, this
 *              can be used as a select loop.
 *              If FALSE, all sources have their callback run, regardless
 *              of file descriptor or timeout status.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR Error occured.
 */
static int sr_session_iteration(gboolean block)
{
	struct sr_session *session = sr_current_session;
	unsigned int i;
	int ret;

	ret = g_poll(session->pollfds, session->num_sources,
			block ? session->source_timeout : 0);
	for (i = 0; i < session->num_sources; i++) {
		if (session->pollfds[i].revents > 0 || (ret == 0
			&& session->source_timeout == session->sources[i].timeout)) {
			/*
			 * Invoke the source's callback on an event,
			 * or if the poll timed out and this source
			 * asked for that timeout.
			 */
			if (!session->sources[i].cb(session->pollfds[i].fd,
					session->pollfds[i].revents,
					session->sources[i].cb_data))
				sr_session_source_remove(session,
						session->sources[i].poll_object);
		}
		/*
		 * We want to take as little time as possible to stop
		 * the session if we have been told to do so. Therefore,
		 * we check the flag after processing every source, not
		 * just once per main event loop.
		 */
		g_mutex_lock(&session->stop_mutex);
		if (session->abort_session) {
			sr_session_stop_sync(session);
			/* But once is enough. */
			session->abort_session = FALSE;
		}
		g_mutex_unlock(&session->stop_mutex);
	}

	return SR_OK;
}


static int verify_trigger(struct sr_trigger *trigger)
{
	struct sr_trigger_stage *stage;
	struct sr_trigger_match *match;
	GSList *l, *m;

	if (!trigger->stages) {
		sr_err("No trigger stages defined.");
		return SR_ERR;
	}

	sr_spew("Checking trigger:");
	for (l = trigger->stages; l; l = l->next) {
		stage = l->data;
		if (!stage->matches) {
			sr_err("Stage %d has no matches defined.", stage->stage);
			return SR_ERR;
		}
		for (m = stage->matches; m; m = m->next) {
			match = m->data;
			if (!match->channel) {
				sr_err("Stage %d match has no channel.", stage->stage);
				return SR_ERR;
			}
			if (!match->match) {
				sr_err("Stage %d match is not defined.", stage->stage);
				return SR_ERR;
			}
			sr_spew("Stage %d match on channel %s, match %d", stage->stage,
					match->channel->name, match->match);
		}
	}

	return SR_OK;
}
/**
 * Start a session.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_ARG Invalid session passed.
 *
 * @since 0.4.0
 */
SR_API int sr_session_start(struct sr_session *session)
{
	struct sr_dev_inst *sdi;
	GSList *l;
	int ret;

	if (!session) {
		sr_err("%s: session was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (!session->devs) {
		sr_err("%s: session->devs was NULL; a session "
		       "cannot be started without devices.", __func__);
		return SR_ERR_ARG;
	}

	if (session->trigger && verify_trigger(session->trigger) != SR_OK)
		return SR_ERR;

	sr_info("Starting.");

	ret = SR_OK;
	for (l = session->devs; l; l = l->next) {
		sdi = l->data;
		if ((ret = sr_config_commit(sdi)) != SR_OK) {
			sr_err("Failed to commit device settings before "
			       "starting acquisition (%s)", sr_strerror(ret));
			break;
		}
		if ((ret = sdi->driver->dev_acquisition_start(sdi, sdi)) != SR_OK) {
			sr_err("%s: could not start an acquisition "
			       "(%s)", __func__, sr_strerror(ret));
			break;
		}
	}

	/* TODO: What if there are multiple devices? Which return code? */

	return ret;
}

/**
 * Run a session.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_ARG Invalid session passed.
 *
 * @since 0.4.0
 */
SR_API int sr_session_run(struct sr_session *session)
{
	if (!session) {
		sr_err("%s: session was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (!session->devs) {
		/* TODO: Actually the case? */
		sr_err("%s: session->devs was NULL; a session "
		       "cannot be run without devices.", __func__);
		return SR_ERR_ARG;
	}
	session->running = TRUE;

	sr_info("Running.");

	/* Do we have real sources? */
	if (session->num_sources == 1 && session->pollfds[0].fd == -1) {
		/* Dummy source, freewheel over it. */
		while (session->num_sources)
			session->sources[0].cb(-1, 0, session->sources[0].cb_data);
	} else {
		/* Real sources, use g_poll() main loop. */
		while (session->num_sources)
			sr_session_iteration(TRUE);
	}

	return SR_OK;
}

/**
 * Stop a session.
 *
 * The session is stopped immediately, with all acquisition sessions stopped
 * and hardware drivers cleaned up.
 *
 * This must be called from within the session thread, to prevent freeing
 * resources that the session thread will try to use.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_ARG Invalid session passed.
 *
 * @private
 */
SR_PRIV int sr_session_stop_sync(struct sr_session *session)
{
	struct sr_dev_inst *sdi;
	GSList *l;

	if (!session) {
		sr_err("%s: session was NULL", __func__);
		return SR_ERR_ARG;
	}

	sr_info("Stopping.");

	for (l = session->devs; l; l = l->next) {
		sdi = l->data;
		if (sdi->driver) {
			if (sdi->driver->dev_acquisition_stop)
				sdi->driver->dev_acquisition_stop(sdi, sdi);
		}
	}
	session->running = FALSE;

	return SR_OK;
}

/**
 * Stop a session.
 *
 * The session is stopped immediately, with all acquisition sessions being
 * stopped and hardware drivers cleaned up.
 *
 * If the session is run in a separate thread, this function will not block
 * until the session is finished executing. It is the caller's responsibility
 * to wait for the session thread to return before assuming that the session is
 * completely decommissioned.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_ARG Invalid session passed.
 *
 * @since 0.4.0
 */
SR_API int sr_session_stop(struct sr_session *session)
{
	if (!session) {
		sr_err("%s: session was NULL", __func__);
		return SR_ERR_BUG;
	}

	g_mutex_lock(&session->stop_mutex);
	session->abort_session = TRUE;
	g_mutex_unlock(&session->stop_mutex);

	return SR_OK;
}

/**
 * Debug helper.
 *
 * @param packet The packet to show debugging information for.
 */
static void datafeed_dump(const struct sr_datafeed_packet *packet)
{
	const struct sr_datafeed_logic *logic;
	const struct sr_datafeed_analog *analog;

	switch (packet->type) {
	case SR_DF_HEADER:
		sr_dbg("bus: Received SR_DF_HEADER packet.");
		break;
	case SR_DF_TRIGGER:
		sr_dbg("bus: Received SR_DF_TRIGGER packet.");
		break;
	case SR_DF_META:
		sr_dbg("bus: Received SR_DF_META packet.");
		break;
	case SR_DF_LOGIC:
		logic = packet->payload;
		sr_dbg("bus: Received SR_DF_LOGIC packet (%" PRIu64 " bytes, "
		       "unitsize = %d).", logic->length, logic->unitsize);
		break;
	case SR_DF_ANALOG:
		analog = packet->payload;
		sr_dbg("bus: Received SR_DF_ANALOG packet (%d samples).",
		       analog->num_samples);
		break;
	case SR_DF_END:
		sr_dbg("bus: Received SR_DF_END packet.");
		break;
	case SR_DF_FRAME_BEGIN:
		sr_dbg("bus: Received SR_DF_FRAME_BEGIN packet.");
		break;
	case SR_DF_FRAME_END:
		sr_dbg("bus: Received SR_DF_FRAME_END packet.");
		break;
	default:
		sr_dbg("bus: Received unknown packet type: %d.", packet->type);
		break;
	}
}

/**
 * Send a packet to whatever is listening on the datafeed bus.
 *
 * Hardware drivers use this to send a data packet to the frontend.
 *
 * @param sdi TODO.
 * @param packet The datafeed packet to send to the session bus.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_ARG Invalid argument.
 *
 * @private
 */
SR_PRIV int sr_session_send(const struct sr_dev_inst *sdi,
			    const struct sr_datafeed_packet *packet)
{
	GSList *l;
	struct datafeed_callback *cb_struct;

	if (!sdi) {
		sr_err("%s: sdi was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (!packet) {
		sr_err("%s: packet was NULL", __func__);
		return SR_ERR_ARG;
	}

	for (l = sr_current_session->datafeed_callbacks; l; l = l->next) {
		if (sr_log_loglevel_get() >= SR_LOG_DBG)
			datafeed_dump(packet);
		cb_struct = l->data;
		cb_struct->cb(sdi, packet, cb_struct->cb_data);
	}

	return SR_OK;
}

/**
 * Add an event source for a file descriptor.
 *
 * @param pollfd The GPollFD.
 * @param[in] timeout Max time to wait before the callback is called,
 *              ignored if 0.
 * @param cb Callback function to add. Must not be NULL.
 * @param cb_data Data for the callback function. Can be NULL.
 * @param poll_object TODO.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_ARG Invalid argument.
 * @retval SR_ERR_MALLOC Memory allocation error.
 */
static int _sr_session_source_add(struct sr_session *session, GPollFD *pollfd,
		int timeout, sr_receive_data_callback cb, void *cb_data, gintptr poll_object)
{
	struct source *new_sources, *s;
	GPollFD *new_pollfds;

	if (!cb) {
		sr_err("%s: cb was NULL", __func__);
		return SR_ERR_ARG;
	}

	/* Note: cb_data can be NULL, that's not a bug. */

	new_pollfds = g_try_realloc(session->pollfds,
			sizeof(GPollFD) * (session->num_sources + 1));
	if (!new_pollfds) {
		sr_err("%s: new_pollfds malloc failed", __func__);
		return SR_ERR_MALLOC;
	}

	new_sources = g_try_realloc(session->sources, sizeof(struct source) *
			(session->num_sources + 1));
	if (!new_sources) {
		sr_err("%s: new_sources malloc failed", __func__);
		return SR_ERR_MALLOC;
	}

	new_pollfds[session->num_sources] = *pollfd;
	s = &new_sources[session->num_sources++];
	s->timeout = timeout;
	s->cb = cb;
	s->cb_data = cb_data;
	s->poll_object = poll_object;
	session->pollfds = new_pollfds;
	session->sources = new_sources;

	if (timeout != session->source_timeout && timeout > 0
	    && (session->source_timeout == -1 || timeout < session->source_timeout))
		session->source_timeout = timeout;

	return SR_OK;
}

/**
 * Add an event source for a file descriptor.
 *
 * @param fd The file descriptor.
 * @param events Events to check for.
 * @param timeout Max time to wait before the callback is called, ignored if 0.
 * @param cb Callback function to add. Must not be NULL.
 * @param cb_data Data for the callback function. Can be NULL.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_ARG Invalid argument.
 * @retval SR_ERR_MALLOC Memory allocation error.
 *
 * @since 0.3.0
 */
SR_API int sr_session_source_add(struct sr_session *session, int fd,
		int events, int timeout, sr_receive_data_callback cb, void *cb_data)
{
	GPollFD p;

	(void) session;

	p.fd = fd;
	p.events = events;

	return _sr_session_source_add(session, &p, timeout, cb, cb_data, (gintptr)fd);
}

/**
 * Add an event source for a GPollFD.
 *
 * @param pollfd The GPollFD.
 * @param timeout Max time to wait before the callback is called, ignored if 0.
 * @param cb Callback function to add. Must not be NULL.
 * @param cb_data Data for the callback function. Can be NULL.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_ARG Invalid argument.
 * @retval SR_ERR_MALLOC Memory allocation error.
 *
 * @since 0.3.0
 */
SR_API int sr_session_source_add_pollfd(struct sr_session *session,
		GPollFD *pollfd, int timeout, sr_receive_data_callback cb,
		void *cb_data)
{
	(void) session;

	return _sr_session_source_add(session, pollfd, timeout, cb,
				      cb_data, (gintptr)pollfd);
}

/**
 * Add an event source for a GIOChannel.
 *
 * @param channel The GIOChannel.
 * @param events Events to poll on.
 * @param timeout Max time to wait before the callback is called, ignored if 0.
 * @param cb Callback function to add. Must not be NULL.
 * @param cb_data Data for the callback function. Can be NULL.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_ARG Invalid argument.
 * @retval SR_ERR_MALLOC Memory allocation error.
 *
 * @since 0.3.0
 */
SR_API int sr_session_source_add_channel(struct sr_session *session,
		GIOChannel *channel, int events, int timeout,
		sr_receive_data_callback cb, void *cb_data)
{
	GPollFD p;

	(void) session;

#ifdef _WIN32
	g_io_channel_win32_make_pollfd(channel, events, &p);
#else
	p.fd = g_io_channel_unix_get_fd(channel);
	p.events = events;
#endif

	return _sr_session_source_add(session, &p, timeout, cb, cb_data, (gintptr)channel);
}

/**
 * Remove the source belonging to the specified channel.
 *
 * @todo Add more error checks and logging.
 *
 * @param poll_object The channel for which the source should be removed.
 *
 * @retval SR_OK Success
 * @retval SR_ERR_ARG Invalid arguments
 * @retval SR_ERR_MALLOC Memory allocation error
 * @retval SR_ERR_BUG Internal error
 */
static int _sr_session_source_remove(struct sr_session *session, gintptr poll_object)
{
	struct source *new_sources;
	GPollFD *new_pollfds;
	unsigned int old;

	if (!session->sources || !session->num_sources) {
		sr_err("%s: sources was NULL", __func__);
		return SR_ERR_BUG;
	}

	for (old = 0; old < session->num_sources; old++) {
		if (session->sources[old].poll_object == poll_object)
			break;
	}

	/* fd not found, nothing to do */
	if (old == session->num_sources)
		return SR_OK;

	session->num_sources -= 1;

	if (old != session->num_sources) {
		memmove(&session->pollfds[old], &session->pollfds[old+1],
			(session->num_sources - old) * sizeof(GPollFD));
		memmove(&session->sources[old], &session->sources[old+1],
			(session->num_sources - old) * sizeof(struct source));
	}

	new_pollfds = g_try_realloc(session->pollfds, sizeof(GPollFD) * session->num_sources);
	if (!new_pollfds && session->num_sources > 0) {
		sr_err("%s: new_pollfds malloc failed", __func__);
		return SR_ERR_MALLOC;
	}

	new_sources = g_try_realloc(session->sources, sizeof(struct source) * session->num_sources);
	if (!new_sources && session->num_sources > 0) {
		sr_err("%s: new_sources malloc failed", __func__);
		return SR_ERR_MALLOC;
	}

	session->pollfds = new_pollfds;
	session->sources = new_sources;

	return SR_OK;
}

/**
 * Remove the source belonging to the specified file descriptor.
 *
 * @param fd The file descriptor for which the source should be removed.
 *
 * @retval SR_OK Success
 * @retval SR_ERR_ARG Invalid argument
 * @retval SR_ERR_MALLOC Memory allocation error.
 * @retval SR_ERR_BUG Internal error.
 *
 * @since 0.3.0
 */
SR_API int sr_session_source_remove(struct sr_session *session, int fd)
{
	(void) session;

	return _sr_session_source_remove(session, (gintptr)fd);
}

/**
 * Remove the source belonging to the specified poll descriptor.
 *
 * @param pollfd The poll descriptor for which the source should be removed.
 *
 * @return SR_OK upon success, SR_ERR_ARG upon invalid arguments, or
 *         SR_ERR_MALLOC upon memory allocation errors, SR_ERR_BUG upon
 *         internal errors.
 *
 * @since 0.2.0
 */
SR_API int sr_session_source_remove_pollfd(struct sr_session *session,
		GPollFD *pollfd)
{
	(void) session;

	return _sr_session_source_remove(session, (gintptr)pollfd);
}

/**
 * Remove the source belonging to the specified channel.
 *
 * @param channel The channel for which the source should be removed.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_ARG Invalid argument.
 * @retval SR_ERR_MALLOC Memory allocation error.
 * @return SR_ERR_BUG Internal error.
 *
 * @since 0.2.0
 */
SR_API int sr_session_source_remove_channel(struct sr_session *session,
		GIOChannel *channel)
{
	(void) session;

	return _sr_session_source_remove(session, (gintptr)channel);
}

/** @} */
