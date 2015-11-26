/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2010-2012 Bert Vermeulen <bert@biot.com>
 * Copyright (C) 2015 Daniel Elstner <daniel.kitta@gmail.com>
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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
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

struct datafeed_callback {
	sr_datafeed_callback cb;
	void *cb_data;
};

/** Custom GLib event source for generic descriptor I/O.
 * @see https://developer.gnome.org/glib/stable/glib-The-Main-Event-Loop.html
 * @internal
 */
struct fd_source {
	GSource base;

	int64_t timeout_us;
	int64_t due_us;

	/* Meta-data needed to keep track of installed sources */
	struct sr_session *session;
	void *key;

	GPollFD pollfd;
};

/** FD event source prepare() method.
 * This is called immediately before poll().
 */
static gboolean fd_source_prepare(GSource *source, int *timeout)
{
	int64_t now_us;
	struct fd_source *fsource;
	int remaining_ms;

	fsource = (struct fd_source *)source;

	if (fsource->timeout_us >= 0) {
		now_us = g_source_get_time(source);

		if (fsource->due_us == 0) {
			/* First-time initialization of the expiration time */
			fsource->due_us = now_us + fsource->timeout_us;
		}
		remaining_ms = (MAX(0, fsource->due_us - now_us) + 999) / 1000;
	} else {
		remaining_ms = -1;
	}
	*timeout = remaining_ms;

	return (remaining_ms == 0);
}

/** FD event source check() method.
 * This is called after poll() returns to check whether an event fired.
 */
static gboolean fd_source_check(GSource *source)
{
	struct fd_source *fsource;
	unsigned int revents;

	fsource = (struct fd_source *)source;
	revents = fsource->pollfd.revents;

	return (revents != 0 || (fsource->timeout_us >= 0
			&& fsource->due_us <= g_source_get_time(source)));
}

/** FD event source dispatch() method.
 * This is called if either prepare() or check() returned TRUE.
 */
static gboolean fd_source_dispatch(GSource *source,
		GSourceFunc callback, void *user_data)
{
	struct fd_source *fsource;
	unsigned int revents;
	gboolean keep;

	fsource = (struct fd_source *)source;
	revents = fsource->pollfd.revents;

	if (!callback) {
		sr_err("Callback not set, cannot dispatch event.");
		return G_SOURCE_REMOVE;
	}
	keep = (*(sr_receive_data_callback)callback)
			(fsource->pollfd.fd, revents, user_data);

	if (fsource->timeout_us >= 0 && G_LIKELY(keep)
			&& G_LIKELY(!g_source_is_destroyed(source)))
		fsource->due_us = g_source_get_time(source)
				+ fsource->timeout_us;
	return keep;
}

/** FD event source finalize() method.
 */
static void fd_source_finalize(GSource *source)
{
	struct fd_source *fsource;

	fsource = (struct fd_source *)source;

	sr_dbg("%s: key %p", __func__, fsource->key);

	sr_session_source_destroyed(fsource->session, fsource->key, source);
}

/** Create an event source for I/O on a file descriptor.
 *
 * In order to maintain API compatibility, this event source also doubles
 * as a timer event source.
 *
 * @param session The session the event source belongs to.
 * @param key The key used to identify this source.
 * @param fd The file descriptor or HANDLE.
 * @param timeout_ms The timeout interval in ms, or -1 to wait indefinitely.
 * @return A new event source object, or NULL on failure.
 */
static GSource *fd_source_new(struct sr_session *session, void *key,
		gintptr fd, int events, int timeout_ms)
{
	static GSourceFuncs fd_source_funcs = {
		.prepare  = &fd_source_prepare,
		.check    = &fd_source_check,
		.dispatch = &fd_source_dispatch,
		.finalize = &fd_source_finalize
	};
	GSource *source;
	struct fd_source *fsource;

	source = g_source_new(&fd_source_funcs, sizeof(struct fd_source));
	fsource = (struct fd_source *)source;

	g_source_set_name(source, (fd < 0) ? "timer" : "fd");

	if (timeout_ms >= 0) {
		fsource->timeout_us = 1000 * (int64_t)timeout_ms;
		fsource->due_us = 0;
	} else {
		fsource->timeout_us = -1;
		fsource->due_us = INT64_MAX;
	}
	fsource->session = session;
	fsource->key = key;

	fsource->pollfd.fd = fd;
	fsource->pollfd.events = events;
	fsource->pollfd.revents = 0;

	if (fd >= 0)
		g_source_add_poll(source, &fsource->pollfd);

	return source;
}

/**
 * Create a new session.
 *
 * @param ctx         The context in which to create the new session.
 * @param new_session This will contain a pointer to the newly created
 *                    session if the return value is SR_OK, otherwise the value
 *                    is undefined and should not be used. Must not be NULL.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_ARG Invalid argument.
 *
 * @since 0.4.0
 */
SR_API int sr_session_new(struct sr_context *ctx,
		struct sr_session **new_session)
{
	struct sr_session *session;

	if (!new_session)
		return SR_ERR_ARG;

	session = g_malloc0(sizeof(struct sr_session));

	session->ctx = ctx;

	g_mutex_init(&session->main_mutex);

	/* To maintain API compatibility, we need a lookup table
	 * which maps poll_object IDs to GSource* pointers.
	 */
	session->event_sources = g_hash_table_new(NULL, NULL);

	*new_session = session;

	return SR_OK;
}

/**
 * Destroy a session.
 * This frees up all memory used by the session.
 *
 * @param session The session to destroy. Must not be NULL.
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
	g_slist_free_full(session->owned_devs, (GDestroyNotify)sr_dev_inst_free);

	g_hash_table_unref(session->event_sources);

	g_mutex_clear(&session->main_mutex);

	g_free(session);

	return SR_OK;
}

/**
 * Remove all the devices from a session.
 *
 * The session itself (i.e., the struct sr_session) is not free'd and still
 * exists after this function returns.
 *
 * @param session The session to use. Must not be NULL.
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
 * @param session The session to add to. Must not be NULL.
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
		/* Just add the device, don't run dev_open(). */
		session->devs = g_slist_append(session->devs, sdi);
		sdi->session = session;
		return SR_OK;
	}

	/* sdi->driver is non-NULL (i.e. we have a real device). */
	if (!sdi->driver->dev_open) {
		sr_err("%s: sdi->driver->dev_open was NULL", __func__);
		return SR_ERR_BUG;
	}

	session->devs = g_slist_append(session->devs, sdi);
	sdi->session = session;

	/* TODO: This is invalid if the session runs in a different thread.
	 * The usage semantics and restrictions need to be documented.
	 */
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
					sdi)) != SR_OK) {
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
 * @param session The session to use. Must not be NULL.
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
 * Remove a device instance from a session.
 *
 * @param session The session to remove from. Must not be NULL.
 * @param sdi The device instance to remove from a session. Must not
 *            be NULL. Also, sdi->driver and sdi->driver->dev_open must
 *            not be NULL.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_ARG Invalid argument.
 *
 * @since 0.4.0
 */
SR_API int sr_session_dev_remove(struct sr_session *session,
		struct sr_dev_inst *sdi)
{
	if (!sdi) {
		sr_err("%s: sdi was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (!session) {
		sr_err("%s: session was NULL", __func__);
		return SR_ERR_ARG;
	}

	/* If sdi->session is not session, the device is not in this
	 * session. */
	if (sdi->session != session) {
		sr_err("%s: not assigned to this session", __func__);
		return SR_ERR_ARG;
	}

	session->devs = g_slist_remove(session->devs, sdi);
	sdi->session = NULL;

	return SR_OK;
}

/**
 * Remove all datafeed callbacks in a session.
 *
 * @param session The session to use. Must not be NULL.
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
 * @param session The session to use. Must not be NULL.
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

	cb_struct = g_malloc0(sizeof(struct datafeed_callback));
	cb_struct->cb = cb;
	cb_struct->cb_data = cb_data;

	session->datafeed_callbacks =
	    g_slist_append(session->datafeed_callbacks, cb_struct);

	return SR_OK;
}

/**
 * Get the trigger assigned to this session.
 *
 * @param session The session to use.
 *
 * @retval NULL Invalid (NULL) session was passed to the function.
 * @retval other The trigger assigned to this session (can be NULL).
 *
 * @since 0.4.0
 */
SR_API struct sr_trigger *sr_session_trigger_get(struct sr_session *session)
{
	if (!session)
		return NULL;

	return session->trigger;
}

/**
 * Set the trigger of this session.
 *
 * @param session The session to use. Must not be NULL.
 * @param trig The trigger to assign to this session. Can be NULL.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_ARG Invalid argument.
 *
 * @since 0.4.0
 */
SR_API int sr_session_trigger_set(struct sr_session *session, struct sr_trigger *trig)
{
	if (!session)
		return SR_ERR_ARG;

	session->trigger = trig;

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

/** Set up the main context the session will be executing in.
 *
 * Must be called just before the session starts, by the thread which
 * will execute the session main loop. Once acquired, the main context
 * pointer is immutable for the duration of the session run.
 */
static int set_main_context(struct sr_session *session)
{
	GMainContext *main_context;

	g_mutex_lock(&session->main_mutex);

	/* May happen if sr_session_start() is called a second time
	 * while the session is still running.
	 */
	if (session->main_context) {
		sr_err("Main context already set.");

		g_mutex_unlock(&session->main_mutex);
		return SR_ERR;
	}
	main_context = g_main_context_ref_thread_default();
	/*
	 * Try to use an existing main context if possible, but only if we
	 * can make it owned by the current thread. Otherwise, create our
	 * own main context so that event source callbacks can execute in
	 * the session thread.
	 */
	if (g_main_context_acquire(main_context)) {
		g_main_context_release(main_context);

		sr_dbg("Using thread-default main context.");
	} else {
		g_main_context_unref(main_context);

		sr_dbg("Creating our own main context.");
		main_context = g_main_context_new();
	}
	session->main_context = main_context;

	g_mutex_unlock(&session->main_mutex);

	return SR_OK;
}

/** Unset the main context used for the current session run.
 *
 * Must be called right after stopping the session. Note that if the
 * session is stopped asynchronously, the main loop may still be running
 * after the main context has been unset. This is OK as long as no new
 * event sources are created -- the main loop holds its own reference
 * to the main context.
 */
static int unset_main_context(struct sr_session *session)
{
	int ret;

	g_mutex_lock(&session->main_mutex);

	if (session->main_context) {
		g_main_context_unref(session->main_context);
		session->main_context = NULL;
		ret = SR_OK;
	} else {
		/* May happen if the set/unset calls are not matched.
		 */
		sr_err("No main context to unset.");
		ret = SR_ERR;
	}
	g_mutex_unlock(&session->main_mutex);

	return ret;
}

static unsigned int session_source_attach(struct sr_session *session,
		GSource *source)
{
	unsigned int id = 0;

	g_mutex_lock(&session->main_mutex);

	if (session->main_context)
		id = g_source_attach(source, session->main_context);
	else
		sr_err("Cannot add event source without main context.");

	g_mutex_unlock(&session->main_mutex);

	return id;
}

/* Idle handler; invoked when the number of registered event sources
 * for a running session drops to zero.
 */
static gboolean delayed_stop_check(void *data)
{
	struct sr_session *session;

	session = data;
	session->stop_check_id = 0;

	/* Session already ended? */
	if (!session->running)
		return G_SOURCE_REMOVE;

	/* New event sources may have been installed in the meantime. */
	if (g_hash_table_size(session->event_sources) != 0)
		return G_SOURCE_REMOVE;

	session->running = FALSE;
	unset_main_context(session);

	sr_info("Stopped.");

	/* This indicates a bug in user code, since it is not valid to
	 * restart or destroy a session while it may still be running.
	 */
	if (!session->main_loop && !session->stopped_callback) {
		sr_err("BUG: Session stop left unhandled.");
		return G_SOURCE_REMOVE;
	}
	if (session->main_loop)
		g_main_loop_quit(session->main_loop);

	if (session->stopped_callback)
		(*session->stopped_callback)(session->stopped_cb_data);

	return G_SOURCE_REMOVE;
}

static int stop_check_later(struct sr_session *session)
{
	GSource *source;
	unsigned int source_id;

	if (session->stop_check_id != 0)
		return SR_OK; /* idle handler already installed */

	source = g_idle_source_new();
	g_source_set_callback(source, &delayed_stop_check, session, NULL);

	source_id = session_source_attach(session, source);
	session->stop_check_id = source_id;

	g_source_unref(source);

	return (source_id != 0) ? SR_OK : SR_ERR;
}

/**
 * Start a session.
 *
 * When this function returns with a status code indicating success, the
 * session is running. Use sr_session_stopped_callback_set() to receive
 * notification upon completion, or call sr_session_run() to block until
 * the session stops.
 *
 * Session events will be processed in the context of the current thread.
 * If a thread-default GLib main context has been set, and is not owned by
 * any other thread, it will be used. Otherwise, libsigrok will create its
 * own main context for the current thread.
 *
 * @param session The session to use. Must not be NULL.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_ARG Invalid session passed.
 * @retval SR_ERR Other error.
 *
 * @since 0.4.0
 */
SR_API int sr_session_start(struct sr_session *session)
{
	struct sr_dev_inst *sdi;
	struct sr_channel *ch;
	GSList *l, *c, *lend;
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

	if (session->running) {
		sr_err("Cannot (re-)start session while it is still running.");
		return SR_ERR;
	}

	if (session->trigger) {
		ret = verify_trigger(session->trigger);
		if (ret != SR_OK)
			return ret;
	}

	/* Check enabled channels and commit settings of all devices. */
	for (l = session->devs; l; l = l->next) {
		sdi = l->data;
		for (c = sdi->channels; c; c = c->next) {
			ch = c->data;
			if (ch->enabled)
				break;
		}
		if (!c) {
			sr_err("%s device %s has no enabled channels.",
				sdi->driver->name, sdi->connection_id);
			return SR_ERR;
		}

		ret = sr_config_commit(sdi);
		if (ret != SR_OK) {
			sr_err("Failed to commit %s device %s settings "
				"before starting acquisition.",
				sdi->driver->name, sdi->connection_id);
			return ret;
		}
	}

	ret = set_main_context(session);
	if (ret != SR_OK)
		return ret;

	sr_info("Starting.");

	session->running = TRUE;

	/* Have all devices start acquisition. */
	for (l = session->devs; l; l = l->next) {
		sdi = l->data;
		ret = sdi->driver->dev_acquisition_start(sdi, sdi);
		if (ret != SR_OK) {
			sr_err("Could not start %s device %s acquisition.",
				sdi->driver->name, sdi->connection_id);
			break;
		}
	}

	if (ret != SR_OK) {
		/* If there are multiple devices, some of them may already have
		 * started successfully. Stop them now before returning. */
		lend = l->next;
		for (l = session->devs; l != lend; l = l->next) {
			sdi = l->data;
			if (sdi->driver->dev_acquisition_stop)
				sdi->driver->dev_acquisition_stop(sdi, sdi);
		}
		/* TODO: Handle delayed stops. Need to iterate the event
		 * sources... */
		session->running = FALSE;

		unset_main_context(session);
		return ret;
	}

	if (g_hash_table_size(session->event_sources) == 0)
		stop_check_later(session);

	return SR_OK;
}

/**
 * Block until the running session stops.
 *
 * This is a convenience function which creates a GLib main loop and runs
 * it to process session events until the session stops.
 *
 * Instead of using this function, applications may run their own GLib main
 * loop, and use sr_session_stopped_callback_set() to receive notification
 * when the session finished running.
 *
 * @param session The session to use. Must not be NULL.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_ARG Invalid session passed.
 * @retval SR_ERR Other error.
 *
 * @since 0.4.0
 */
SR_API int sr_session_run(struct sr_session *session)
{
	if (!session) {
		sr_err("%s: session was NULL", __func__);
		return SR_ERR_ARG;
	}
	if (!session->running) {
		sr_err("No session running.");
		return SR_ERR;
	}
	if (session->main_loop) {
		sr_err("Main loop already created.");
		return SR_ERR;
	}

	g_mutex_lock(&session->main_mutex);

	if (!session->main_context) {
		sr_err("Cannot run without main context.");
		g_mutex_unlock(&session->main_mutex);
		return SR_ERR;
	}
	session->main_loop = g_main_loop_new(session->main_context, FALSE);

	g_mutex_unlock(&session->main_mutex);

	g_main_loop_run(session->main_loop);

	g_main_loop_unref(session->main_loop);
	session->main_loop = NULL;

	return SR_OK;
}

static gboolean session_stop_sync(void *user_data)
{
	struct sr_session *session;
	struct sr_dev_inst *sdi;
	GSList *node;

	session = user_data;

	if (!session->running)
		return G_SOURCE_REMOVE;

	sr_info("Stopping.");

	for (node = session->devs; node; node = node->next) {
		sdi = node->data;
		if (sdi->driver && sdi->driver->dev_acquisition_stop)
			sdi->driver->dev_acquisition_stop(sdi, sdi);
	}

	return G_SOURCE_REMOVE;
}

/**
 * Stop a session.
 *
 * This requests the drivers of each device participating in the session to
 * abort the acquisition as soon as possible. Even after this function returns,
 * event processing still continues until all devices have actually stopped.
 *
 * Use sr_session_stopped_callback_set() to receive notification when the event
 * processing finished.
 *
 * This function is reentrant. That is, it may be called from a different
 * thread than the one executing the session, as long as it can be ensured
 * that the session object is valid.
 *
 * If the session is not running, sr_session_stop() silently does nothing.
 *
 * @param session The session to use. Must not be NULL.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_ARG Invalid session passed.
 *
 * @since 0.4.0
 */
SR_API int sr_session_stop(struct sr_session *session)
{
	GMainContext *main_context;

	if (!session) {
		sr_err("%s: session was NULL", __func__);
		return SR_ERR_ARG;
	}

	g_mutex_lock(&session->main_mutex);

	main_context = (session->main_context)
		? g_main_context_ref(session->main_context)
		: NULL;

	g_mutex_unlock(&session->main_mutex);

	if (!main_context) {
		sr_dbg("No main context set; already stopped?");
		/* Not an error; as it would be racy. */
		return SR_OK;
	}
	g_main_context_invoke(main_context, &session_stop_sync, session);
	g_main_context_unref(main_context);

	return SR_OK;
}

/**
 * Return whether the session is currently running.
 *
 * Note that this function should be called from the same thread
 * the session was started in.
 *
 * @param session The session to use. Must not be NULL.
 *
 * @retval TRUE Session is running.
 * @retval FALSE Session is not running.
 * @retval SR_ERR_ARG Invalid session passed.
 *
 * @since 0.4.0
 */
SR_API int sr_session_is_running(struct sr_session *session)
{
	if (!session) {
		sr_err("%s: session was NULL", __func__);
		return SR_ERR_ARG;
	}
	return session->running;
}

/**
 * Set the callback to be invoked after a session stopped running.
 *
 * Install a callback to receive notification when a session run stopped.
 * This can be used to integrate session execution with an existing main
 * loop, without having to block in sr_session_run().
 *
 * Note that the callback will be invoked in the context of the thread
 * that calls sr_session_start().
 *
 * @param session The session to use. Must not be NULL.
 * @param cb The callback to invoke on session stop. May be NULL to unset.
 * @param cb_data User data pointer to be passed to the callback.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_ARG Invalid session passed.
 *
 * @since 0.4.0
 */
SR_API int sr_session_stopped_callback_set(struct sr_session *session,
		sr_session_stopped_callback cb, void *cb_data)
{
	if (!session) {
		sr_err("%s: session was NULL", __func__);
		return SR_ERR_ARG;
	}
	session->stopped_callback = cb;
	session->stopped_cb_data = cb_data;

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
	const struct sr_datafeed_analog_old *analog_old;
	const struct sr_datafeed_analog *analog;

	/* Please use the same order as in libsigrok.h. */
	switch (packet->type) {
	case SR_DF_HEADER:
		sr_dbg("bus: Received SR_DF_HEADER packet.");
		break;
	case SR_DF_END:
		sr_dbg("bus: Received SR_DF_END packet.");
		break;
	case SR_DF_META:
		sr_dbg("bus: Received SR_DF_META packet.");
		break;
	case SR_DF_TRIGGER:
		sr_dbg("bus: Received SR_DF_TRIGGER packet.");
		break;
	case SR_DF_LOGIC:
		logic = packet->payload;
		sr_dbg("bus: Received SR_DF_LOGIC packet (%" PRIu64 " bytes, "
		       "unitsize = %d).", logic->length, logic->unitsize);
		break;
	case SR_DF_ANALOG_OLD:
		analog_old = packet->payload;
		sr_dbg("bus: Received SR_DF_ANALOG_OLD packet (%d samples).",
		       analog_old->num_samples);
		break;
	case SR_DF_FRAME_BEGIN:
		sr_dbg("bus: Received SR_DF_FRAME_BEGIN packet.");
		break;
	case SR_DF_FRAME_END:
		sr_dbg("bus: Received SR_DF_FRAME_END packet.");
		break;
	case SR_DF_ANALOG:
		analog = packet->payload;
		sr_dbg("bus: Received SR_DF_ANALOG packet (%d samples).",
		       analog->num_samples);
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
	struct sr_datafeed_packet *packet_in, *packet_out;
	struct sr_transform *t;
	int ret;

	if (!sdi) {
		sr_err("%s: sdi was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (!packet) {
		sr_err("%s: packet was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (!sdi->session) {
		sr_err("%s: session was NULL", __func__);
		return SR_ERR_BUG;
	}

	if (packet->type == SR_DF_ANALOG_OLD) {
		/* Convert to SR_DF_ANALOG. */
		const struct sr_datafeed_analog_old *analog_old = packet->payload;
		struct sr_analog_encoding encoding;
		struct sr_analog_meaning meaning;
		struct sr_analog_spec spec;
		struct sr_datafeed_analog analog;
		struct sr_datafeed_packet new_packet;
		new_packet.type = SR_DF_ANALOG;
		new_packet.payload = &analog;
		analog.data = analog_old->data;
		analog.num_samples = analog_old->num_samples;
		analog.encoding = &encoding;
		analog.meaning = &meaning;
		analog.spec = &spec;
		encoding.unitsize = sizeof(float);
		encoding.is_signed = TRUE;
		encoding.is_float = TRUE;
#ifdef WORDS_BIGENDIAN
		encoding.is_bigendian = TRUE;
#else
		encoding.is_bigendian = FALSE;
#endif
		encoding.digits = 0;
		encoding.is_digits_decimal = FALSE;
		encoding.scale.p = 1;
		encoding.scale.q = 1;
		encoding.offset.p = 0;
		encoding.offset.q = 1;
		meaning.mq = analog_old->mq;
		meaning.unit = analog_old->unit;
		meaning.mqflags = analog_old->mqflags;
		meaning.channels = analog_old->channels;
		spec.spec_digits = 0;
		return sr_session_send(sdi, &new_packet);
	}

	/*
	 * Pass the packet to the first transform module. If that returns
	 * another packet (instead of NULL), pass that packet to the next
	 * transform module in the list, and so on.
	 */
	packet_in = (struct sr_datafeed_packet *)packet;
	for (l = sdi->session->transforms; l; l = l->next) {
		t = l->data;
		sr_spew("Running transform module '%s'.", t->module->id);
		ret = t->module->receive(t, packet_in, &packet_out);
		if (ret < 0) {
			sr_err("Error while running transform module: %d.", ret);
			return SR_ERR;
		}
		if (!packet_out) {
			/*
			 * If any of the transforms don't return an output
			 * packet, abort.
			 */
			sr_spew("Transform module didn't return a packet, aborting.");
			return SR_OK;
		} else {
			/*
			 * Use this transform module's output packet as input
			 * for the next transform module.
			 */
			packet_in = packet_out;
		}
	}
	packet = packet_in;

	/*
	 * If the last transform did output a packet, pass it to all datafeed
	 * callbacks.
	 */
	for (l = sdi->session->datafeed_callbacks; l; l = l->next) {
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
 * @param session The session to use. Must not be NULL.
 * @param key The key which identifies the event source.
 * @param source An event source object. Must not be NULL.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_ARG Invalid argument.
 * @retval SR_ERR_BUG Event source with @a key already installed.
 * @retval SR_ERR Other error.
 *
 * @private
 */
SR_PRIV int sr_session_source_add_internal(struct sr_session *session,
		void *key, GSource *source)
{
	/*
	 * This must not ever happen, since the source has already been
	 * created and its finalize() method will remove the key for the
	 * already installed source. (Well it would, if we did not have
	 * another sanity check there.)
	 */
	if (g_hash_table_contains(session->event_sources, key)) {
		sr_err("Event source with key %p already exists.", key);
		return SR_ERR_BUG;
	}
	g_hash_table_insert(session->event_sources, key, source);

	if (session_source_attach(session, source) == 0)
		return SR_ERR;

	return SR_OK;
}

SR_PRIV int sr_session_fd_source_add(struct sr_session *session,
		void *key, gintptr fd, int events, int timeout,
		sr_receive_data_callback cb, void *cb_data)
{
	GSource *source;
	int ret;

	source = fd_source_new(session, key, fd, events, timeout);
	if (!source)
		return SR_ERR;

	g_source_set_callback(source, (GSourceFunc)cb, cb_data, NULL);

	ret = sr_session_source_add_internal(session, key, source);
	g_source_unref(source);

	return ret;
}

/**
 * Add an event source for a file descriptor.
 *
 * @param session The session to use. Must not be NULL.
 * @param fd The file descriptor, or a negative value to create a timer source.
 * @param events Events to check for.
 * @param timeout Max time in ms to wait before the callback is called,
 *                or -1 to wait indefinitely.
 * @param cb Callback function to add. Must not be NULL.
 * @param cb_data Data for the callback function. Can be NULL.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_ARG Invalid argument.
 *
 * @since 0.3.0
 * @private
 */
SR_PRIV int sr_session_source_add(struct sr_session *session, int fd,
		int events, int timeout, sr_receive_data_callback cb, void *cb_data)
{
	if (fd < 0 && timeout < 0) {
		sr_err("Cannot create timer source without timeout.");
		return SR_ERR_ARG;
	}
	return sr_session_fd_source_add(session, GINT_TO_POINTER(fd),
			fd, events, timeout, cb, cb_data);
}

/**
 * Add an event source for a GPollFD.
 *
 * @param session The session to use. Must not be NULL.
 * @param pollfd The GPollFD. Must not be NULL.
 * @param timeout Max time in ms to wait before the callback is called,
 *                or -1 to wait indefinitely.
 * @param cb Callback function to add. Must not be NULL.
 * @param cb_data Data for the callback function. Can be NULL.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_ARG Invalid argument.
 *
 * @since 0.3.0
 * @private
 */
SR_PRIV int sr_session_source_add_pollfd(struct sr_session *session,
		GPollFD *pollfd, int timeout, sr_receive_data_callback cb,
		void *cb_data)
{
	if (!pollfd) {
		sr_err("%s: pollfd was NULL", __func__);
		return SR_ERR_ARG;
	}
	return sr_session_fd_source_add(session, pollfd, pollfd->fd,
			pollfd->events, timeout, cb, cb_data);
}

/**
 * Add an event source for a GIOChannel.
 *
 * @param session The session to use. Must not be NULL.
 * @param channel The GIOChannel.
 * @param events Events to poll on.
 * @param timeout Max time in ms to wait before the callback is called,
 *                or -1 to wait indefinitely.
 * @param cb Callback function to add. Must not be NULL.
 * @param cb_data Data for the callback function. Can be NULL.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_ARG Invalid argument.
 *
 * @since 0.3.0
 * @private
 */
SR_PRIV int sr_session_source_add_channel(struct sr_session *session,
		GIOChannel *channel, int events, int timeout,
		sr_receive_data_callback cb, void *cb_data)
{
	GPollFD pollfd;

	if (!channel) {
		sr_err("%s: channel was NULL", __func__);
		return SR_ERR_ARG;
	}
	/* We should be using g_io_create_watch(), but can't without
	 * changing the driver API, as the callback signature is different.
	 */
#ifdef G_OS_WIN32
	g_io_channel_win32_make_pollfd(channel, events, &pollfd);
#else
	pollfd.fd = g_io_channel_unix_get_fd(channel);
	pollfd.events = events;
#endif
	return sr_session_fd_source_add(session, channel, pollfd.fd,
			pollfd.events, timeout, cb, cb_data);
}

/**
 * Remove the source identified by the specified poll object.
 *
 * @param session The session to use. Must not be NULL.
 * @param key The key by which the source is identified.
 *
 * @retval SR_OK Success
 * @retval SR_ERR_BUG No event source for poll_object found.
 *
 * @private
 */
SR_PRIV int sr_session_source_remove_internal(struct sr_session *session,
		void *key)
{
	GSource *source;

	source = g_hash_table_lookup(session->event_sources, key);
	/*
	 * Trying to remove an already removed event source is problematic
	 * since the poll_object handle may have been reused in the meantime.
	 */
	if (!source) {
		sr_warn("Cannot remove non-existing event source %p.", key);
		return SR_ERR_BUG;
	}
	g_source_destroy(source);

	return SR_OK;
}

/**
 * Remove the source belonging to the specified file descriptor.
 *
 * @param session The session to use. Must not be NULL.
 * @param fd The file descriptor for which the source should be removed.
 *
 * @retval SR_OK Success
 * @retval SR_ERR_ARG Invalid argument
 * @retval SR_ERR_BUG Internal error.
 *
 * @since 0.3.0
 * @private
 */
SR_PRIV int sr_session_source_remove(struct sr_session *session, int fd)
{
	return sr_session_source_remove_internal(session, GINT_TO_POINTER(fd));
}

/**
 * Remove the source belonging to the specified poll descriptor.
 *
 * @param session The session to use. Must not be NULL.
 * @param pollfd The poll descriptor for which the source should be removed.
 *               Must not be NULL.
 * @return SR_OK upon success, SR_ERR_ARG upon invalid arguments, or
 *         SR_ERR_MALLOC upon memory allocation errors, SR_ERR_BUG upon
 *         internal errors.
 *
 * @since 0.2.0
 * @private
 */
SR_PRIV int sr_session_source_remove_pollfd(struct sr_session *session,
		GPollFD *pollfd)
{
	if (!pollfd) {
		sr_err("%s: pollfd was NULL", __func__);
		return SR_ERR_ARG;
	}
	return sr_session_source_remove_internal(session, pollfd);
}

/**
 * Remove the source belonging to the specified channel.
 *
 * @param session The session to use. Must not be NULL.
 * @param channel The channel for which the source should be removed.
 *                Must not be NULL.
 * @retval SR_OK Success.
 * @retval SR_ERR_ARG Invalid argument.
 * @return SR_ERR_BUG Internal error.
 *
 * @since 0.2.0
 * @private
 */
SR_PRIV int sr_session_source_remove_channel(struct sr_session *session,
		GIOChannel *channel)
{
	if (!channel) {
		sr_err("%s: channel was NULL", __func__);
		return SR_ERR_ARG;
	}
	return sr_session_source_remove_internal(session, channel);
}

/** Unregister an event source that has been destroyed.
 *
 * This is intended to be called from a source's finalize() method.
 *
 * @param session The session to use. Must not be NULL.
 * @param key The key used to identify @a source.
 * @param source The source object that was destroyed.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_BUG Event source for @a key does not match @a source.
 * @retval SR_ERR Other error.
 *
 * @private
 */
SR_PRIV int sr_session_source_destroyed(struct sr_session *session,
		void *key, GSource *source)
{
	GSource *registered_source;

	registered_source = g_hash_table_lookup(session->event_sources, key);
	/*
	 * Trying to remove an already removed event source is problematic
	 * since the poll_object handle may have been reused in the meantime.
	 */
	if (!registered_source) {
		sr_err("No event source for key %p found.", key);
		return SR_ERR_BUG;
	}
	if (registered_source != source) {
		sr_err("Event source for key %p does not match"
			" destroyed source.", key);
		return SR_ERR_BUG;
	}
	g_hash_table_remove(session->event_sources, key);

	if (g_hash_table_size(session->event_sources) > 0)
		return SR_OK;

	/* If no event sources are left, consider the acquisition finished.
	 * This is pretty crude, as it requires all event sources to be
	 * registered via the libsigrok API.
	 */
	return stop_check_later(session);
}

static void copy_src(struct sr_config *src, struct sr_datafeed_meta *meta_copy)
{
	g_variant_ref(src->data);
	meta_copy->config = g_slist_append(meta_copy->config,
	                                   g_memdup(src, sizeof(struct sr_config)));
}

SR_PRIV int sr_packet_copy(const struct sr_datafeed_packet *packet,
		struct sr_datafeed_packet **copy)
{
	const struct sr_datafeed_meta *meta;
	struct sr_datafeed_meta *meta_copy;
	const struct sr_datafeed_logic *logic;
	struct sr_datafeed_logic *logic_copy;
	const struct sr_datafeed_analog_old *analog_old;
	struct sr_datafeed_analog_old *analog_old_copy;
	const struct sr_datafeed_analog *analog;
	struct sr_datafeed_analog *analog_copy;
	uint8_t *payload;

	*copy = g_malloc0(sizeof(struct sr_datafeed_packet));
	(*copy)->type = packet->type;

	switch (packet->type) {
	case SR_DF_TRIGGER:
	case SR_DF_END:
		/* No payload. */
		break;
	case SR_DF_HEADER:
		payload = g_malloc(sizeof(struct sr_datafeed_header));
		memcpy(payload, packet->payload, sizeof(struct sr_datafeed_header));
		(*copy)->payload = payload;
		break;
	case SR_DF_META:
		meta = packet->payload;
		meta_copy = g_malloc0(sizeof(struct sr_datafeed_meta));
		g_slist_foreach(meta->config, (GFunc)copy_src, meta_copy->config);
		(*copy)->payload = meta_copy;
		break;
	case SR_DF_LOGIC:
		logic = packet->payload;
		logic_copy = g_malloc(sizeof(logic));
		logic_copy->length = logic->length;
		logic_copy->unitsize = logic->unitsize;
		memcpy(logic_copy->data, logic->data, logic->length * logic->unitsize);
		(*copy)->payload = logic_copy;
		break;
	case SR_DF_ANALOG_OLD:
		analog_old = packet->payload;
		analog_old_copy = g_malloc(sizeof(analog_old));
		analog_old_copy->channels = g_slist_copy(analog_old->channels);
		analog_old_copy->num_samples = analog_old->num_samples;
		analog_old_copy->mq = analog_old->mq;
		analog_old_copy->unit = analog_old->unit;
		analog_old_copy->mqflags = analog_old->mqflags;
		analog_old_copy->data = g_malloc(analog_old->num_samples * sizeof(float));
		memcpy(analog_old_copy->data, analog_old->data,
				analog_old->num_samples * sizeof(float));
		(*copy)->payload = analog_old_copy;
		break;
	case SR_DF_ANALOG:
		analog = packet->payload;
		analog_copy = g_malloc(sizeof(analog));
		analog_copy->data = g_malloc(
				analog->encoding->unitsize * analog->num_samples);
		memcpy(analog_copy->data, analog->data,
				analog->encoding->unitsize * analog->num_samples);
		analog_copy->num_samples = analog->num_samples;
		analog_copy->encoding = g_memdup(analog->encoding,
				sizeof(struct sr_analog_encoding));
		analog_copy->meaning = g_memdup(analog->meaning,
				sizeof(struct sr_analog_meaning));
		analog_copy->meaning->channels = g_slist_copy(
				analog->meaning->channels);
		analog_copy->spec = g_memdup(analog->spec,
				sizeof(struct sr_analog_spec));
		(*copy)->payload = analog_copy;
		break;
	default:
		sr_err("Unknown packet type %d", packet->type);
		return SR_ERR;
	}

	return SR_OK;
}

void sr_packet_free(struct sr_datafeed_packet *packet)
{
	const struct sr_datafeed_meta *meta;
	const struct sr_datafeed_logic *logic;
	const struct sr_datafeed_analog_old *analog_old;
	const struct sr_datafeed_analog *analog;
	struct sr_config *src;
	GSList *l;

	switch (packet->type) {
	case SR_DF_TRIGGER:
	case SR_DF_END:
		/* No payload. */
		break;
	case SR_DF_HEADER:
		/* Payload is a simple struct. */
		g_free((void *)packet->payload);
		break;
	case SR_DF_META:
		meta = packet->payload;
		for (l = meta->config; l; l = l->next) {
			src = l->data;
			g_variant_unref(src->data);
			g_free(src);
		}
		g_slist_free(meta->config);
		g_free((void *)packet->payload);
		break;
	case SR_DF_LOGIC:
		logic = packet->payload;
		g_free(logic->data);
		g_free((void *)packet->payload);
		break;
	case SR_DF_ANALOG_OLD:
		analog_old = packet->payload;
		g_slist_free(analog_old->channels);
		g_free(analog_old->data);
		g_free((void *)packet->payload);
		break;
	case SR_DF_ANALOG:
		analog = packet->payload;
		g_free(analog->data);
		g_free(analog->encoding);
		g_slist_free(analog->meaning->channels);
		g_free(analog->meaning);
		g_free(analog->spec);
		g_free((void *)packet->payload);
		break;
	default:
		sr_err("Unknown packet type %d", packet->type);
	}
	g_free(packet);

}

/** @} */
