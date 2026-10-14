/* SPDX-License-Identifier: GPL-3.0-only */
/* Isolated SDK0 experiment. No session, frontend, trace-file or global logger
 * callback is invoked here. The main thread owns decoding and datafeed. */
#ifndef LIBSIGROK_FNIRSI_DLA32_WORKER_WCH_H
#define LIBSIGROK_FNIRSI_DLA32_WORKER_WCH_H
#if defined(_WIN32) && DLA_WCH_WORKER

#define DLA_WORKER_BUFFER_SLOTS 2
#define DLA_WORKER_LOG_SLOTS 64
#define DLA_WORKER_LOG_LENGTH 512

struct dla_worker_log {
	int level;
	int64_t original_us;
	char message[DLA_WORKER_LOG_LENGTH];
};

struct dla_worker_result {
	uint64_t read_id;
	int status, count;
	DWORD windows_error;
	int64_t start_us, end_us;
	uint8_t *buffer;
};

struct dla_worker_terminal {
	gboolean stop_pending, diagnostics_failed, drain_quiet, device_disconnected;
	int stop_status, drain_status;
	uint64_t read_count, drain_bytes;
	unsigned int drain_reads, drain_empty;
	int64_t stop_start_us, stop_end_us, drain_end_us;
};

struct dla_wch_worker {
	struct dla_wch *wch;
	HANDLE thread;
	HANDLE ready; /* Borrowed; main removes its poll source before closing. */
	GMutex mutex;
	GCond condition;
	uint64_t generation;
	gboolean stop_requested, terminal_ready, diagnostics_failed;
	gboolean busy[DLA_WORKER_BUFFER_SLOTS];
	unsigned int queue_head, queue_count, claimed_slot;
	unsigned int queue[DLA_WORKER_BUFFER_SLOTS];
	uint8_t *buffers[DLA_WORKER_BUFFER_SLOTS], *drain_buffer;
	struct dla_worker_result results[DLA_WORKER_BUFFER_SLOTS];
	struct dla_worker_terminal terminal;
	struct dla_worker_log logs[DLA_WORKER_LOG_SLOTS];
	unsigned int log_start, log_count;
};

/* Called with the mutex held. A failed notification stops further I/O;
 * the session watchdog still polls and reconciles the terminal state. */
static void dla_worker_notify_locked(struct dla_wch_worker *worker)
{
	if (!SetEvent(worker->ready)) {
		worker->diagnostics_failed = worker->stop_requested = TRUE;
		g_cond_signal(&worker->condition);
	}
}

static void dla_worker_acknowledge(struct dla_wch_worker *worker)
{
	g_mutex_lock(&worker->mutex);
	if (!ResetEvent(worker->ready)) {
		worker->diagnostics_failed = worker->stop_requested = TRUE;
		g_cond_signal(&worker->condition);
	}
	g_mutex_unlock(&worker->mutex);
}

static void dla_worker_sink(void *context, int level, const char *message)
{
	struct dla_wch_worker *worker = context;
	struct dla_worker_log *entry;

	g_mutex_lock(&worker->mutex);
	if (worker->log_count == DLA_WORKER_LOG_SLOTS ||
			strlen(message) >= DLA_WORKER_LOG_LENGTH) {
		worker->diagnostics_failed = TRUE;
		worker->stop_requested = TRUE;
		g_cond_signal(&worker->condition);
	} else {
		entry = &worker->logs[(worker->log_start + worker->log_count) % DLA_WORKER_LOG_SLOTS];
		entry->level = level;
		entry->original_us = g_get_monotonic_time();
		g_strlcpy(entry->message, message, sizeof(entry->message));
		worker->log_count++;
	}
	dla_worker_notify_locked(worker);
	g_mutex_unlock(&worker->mutex);
}

static void dla_worker_logf(struct dla_wch_worker *worker, int level,
		const char *format, ...) G_GNUC_PRINTF(3, 4);

static void dla_worker_logf(struct dla_wch_worker *worker, int level,
		const char *format, ...)
{
	char message[DLA_WORKER_LOG_LENGTH];
	va_list args;
	int written;

	va_start(args, format);
	written = vsnprintf(message, sizeof(message), format, args);
	va_end(args);
	if (written < 0 || (size_t)written >= sizeof(message)) {
		g_mutex_lock(&worker->mutex);
		worker->diagnostics_failed = TRUE;
		worker->stop_requested = TRUE;
		g_cond_signal(&worker->condition);
		dla_worker_notify_locked(worker);
		g_mutex_unlock(&worker->mutex);
		return;
	}
	dla_worker_sink(worker, level, message);
}

static DWORD WINAPI dla_worker_thread(LPVOID context)
{
	struct dla_wch_worker *worker = context;
	struct dla_worker_result result;
	struct dla_worker_terminal terminal = {0};
	uint8_t packet[DLA_COMMAND_SIZE], *drain_buffer;
	unsigned int i, slot;
	char prefix[109];
	int count, length, status;
	int64_t deadline, started, ended;

	/* CreateThread is the exclusive I/O handoff. Before it, only main owns
	 * WCH; after it, main performs no WCH operation until native exit. */
	for (;;) {
		g_mutex_lock(&worker->mutex);
		for (;;) {
			for (slot = 0; slot < DLA_WORKER_BUFFER_SLOTS; slot++)
				if (!worker->busy[slot])
					break;
			if (worker->stop_requested || slot < DLA_WORKER_BUFFER_SLOTS)
				break;
			g_cond_wait(&worker->condition, &worker->mutex);
		}
		if (worker->stop_requested) {
			g_mutex_unlock(&worker->mutex);
			break;
		}
		worker->busy[slot] = TRUE;
		memset(&result, 0, sizeof(result));
		result.buffer = worker->buffers[slot];
		result.read_id = ++terminal.read_count;
		g_mutex_unlock(&worker->mutex);

		result.start_us = g_get_monotonic_time();
		result.status = dla_wch_read(worker->wch, 1, result.buffer,
			TRANSFER_SIZE, &result.count, TRANSFER_TIMEOUT_MS);
		result.windows_error = worker->wch->last_operation_error;
		result.end_us = g_get_monotonic_time();

		g_mutex_lock(&worker->mutex);
		/* FIFO publication keeps the claimed buffer immutable while the
		 * next SDK read uses the other slot. Full queues apply backpressure. */
		worker->results[slot] = result;
		worker->queue[(worker->queue_head + worker->queue_count) %
			DLA_WORKER_BUFFER_SLOTS] = slot;
		worker->queue_count++;
		if (result.status != LIBUSB_SUCCESS && result.status != LIBUSB_ERROR_TIMEOUT)
			worker->stop_requested = TRUE;
		if (result.status == LIBUSB_ERROR_NO_DEVICE)
			terminal.device_disconnected = TRUE;
		dla_worker_notify_locked(worker);
		g_mutex_unlock(&worker->mutex);
	}

	terminal.stop_pending = TRUE;
	if (terminal.device_disconnected) {
		/* Explicit OS device-loss errors permit host-handle retirement, never
		 * a claim that the hardware accepted STOP or that data was complete. */
		terminal.stop_status = SR_ERR_IO;
		terminal.drain_status = LIBUSB_ERROR_NO_DEVICE;
		terminal.stop_start_us = terminal.stop_end_us = g_get_monotonic_time();
		dla_worker_logf(worker, SR_LOG_ERR,
			"WCH device disconnected; capture incomplete, hardware STOP unacknowledged.");
		goto publish_terminal;
	}
	dla_stop(packet);
	for (i = 0; i < 54; i++)
		g_snprintf(prefix + 2 * i, 3, "%02x", packet[i]);
	terminal.stop_start_us = g_get_monotonic_time();
	dla_worker_logf(worker, SR_LOG_DBG, "WCH WORKER command: generation %" PRIu64
		", opcode 15, io_start_us %" PRId64 ", prefix54 %s.",
		worker->generation, terminal.stop_start_us, prefix);
	terminal.stop_status = dla_wch_write(worker->wch, packet, DLA_COMMAND_SIZE);
	terminal.stop_end_us = g_get_monotonic_time();
	terminal.stop_pending = terminal.stop_status != SR_OK;
	if (terminal.stop_pending && (worker->wch->last_operation_error ==
			ERROR_DEVICE_NOT_CONNECTED || worker->wch->last_operation_error == ERROR_NO_SUCH_DEVICE))
		terminal.device_disconnected = TRUE;
	if (terminal.stop_pending) {
		dla_worker_logf(worker, SR_LOG_ERR, "WCH capture STOP failed: phase stop; pending ownership retained.");
	} else {
		dla_worker_logf(worker, SR_LOG_INFO, "WCH capture STOP complete: phase stop; command accepted.");
		/* Drain storage never aliases either queued or claimed result. */
		drain_buffer = worker->drain_buffer;
		deadline = g_get_monotonic_time() + STOP_DRAIN_USEC;
		terminal.drain_status = LIBUSB_SUCCESS;
		while (terminal.drain_empty < STOP_EMPTY_READS &&
				terminal.drain_bytes < STOP_DRAIN_BYTES &&
				g_get_monotonic_time() < deadline) {
			count = 0;
			length = MIN((uint64_t)TRANSFER_SIZE, STOP_DRAIN_BYTES - terminal.drain_bytes);
			started = g_get_monotonic_time();
			status = dla_wch_read(worker->wch, 1, drain_buffer,
				length, &count, 20);
			ended = g_get_monotonic_time();
			terminal.drain_reads++;
			terminal.drain_status = status;
			dla_worker_logf(worker, SR_LOG_DBG, "WCH WORKER drain read: generation %" PRIu64
				", io_start_us %" PRId64 ", duration_us %" PRId64
				", status %d, requested %u, timeout_ms 20, count %d.",
				worker->generation, started, ended - started, status,
				(unsigned int)length, count);
			if (status != LIBUSB_SUCCESS && status != LIBUSB_ERROR_TIMEOUT) {
				if (status == LIBUSB_ERROR_NO_DEVICE)
					terminal.device_disconnected = TRUE;
				dla_worker_logf(worker, SR_LOG_WARN, "Post-stop drain failed: %s.", libusb_error_name(status));
				break;
			}
			if (!count) {
				terminal.drain_empty++;
			} else {
				terminal.drain_empty = 0;
				terminal.drain_bytes += count;
			}
		}
		terminal.drain_quiet = terminal.drain_empty >= STOP_EMPTY_READS;
		if (!terminal.drain_quiet)
			dla_worker_logf(worker, SR_LOG_WARN, "Post-stop endpoint did not become quiet within the drain bound.");
	}
publish_terminal:
	terminal.drain_end_us = g_get_monotonic_time();
	g_mutex_lock(&worker->mutex);
	terminal.diagnostics_failed = worker->diagnostics_failed;
	worker->terminal = terminal;
	worker->terminal_ready = TRUE;
	dla_worker_notify_locked(worker);
	g_mutex_unlock(&worker->mutex);
	/* Main must additionally observe the native HANDLE signaled. Publication
	 * does not permit destroying a still-running thread or any of its state. */
	return 0;
}

static struct dla_wch_worker *dla_worker_prepare(struct dla_wch *wch,
		uint64_t generation, HANDLE ready)
{
	struct dla_wch_worker *worker = g_try_malloc0(sizeof(*worker));
	unsigned int i;

	if (!worker)
		return NULL;
	g_mutex_init(&worker->mutex);
	g_cond_init(&worker->condition);
	worker->wch = wch;
	worker->generation = generation;
	worker->ready = ready;
	worker->claimed_slot = DLA_WORKER_BUFFER_SLOTS;
	for (i = 0; i < DLA_WORKER_BUFFER_SLOTS; i++) {
		worker->buffers[i] = g_try_malloc(TRANSFER_SIZE);
		if (!worker->buffers[i]) {
			g_free(worker->buffers[0]);
			g_cond_clear(&worker->condition);
			g_mutex_clear(&worker->mutex);
			g_free(worker);
			return NULL;
		}
	}
	worker->drain_buffer = g_try_malloc(TRANSFER_SIZE);
	if (!worker->drain_buffer) {
		for (i = 0; i < DLA_WORKER_BUFFER_SLOTS; i++)
			g_free(worker->buffers[i]);
		g_cond_clear(&worker->condition);
		g_mutex_clear(&worker->mutex);
		g_free(worker);
		return NULL;
	}
	return worker;
}

static gboolean dla_worker_start(struct dla_wch_worker *worker, gboolean stop)
{
	/* No SDK calls occurred while PREPARED. CreateThread failure leaves main
	 * the sole owner, so its existing synchronous rollback remains legal. */
	worker->stop_requested = stop;
	worker->wch->diagnostic_sink = dla_worker_sink;
	worker->wch->diagnostic_context = worker;
	worker->thread = CreateThread(NULL, 0, dla_worker_thread, worker, 0, NULL);
	if (!worker->thread) {
		worker->wch->diagnostic_sink = NULL;
		worker->wch->diagnostic_context = NULL;
		return FALSE;
	}
	return TRUE;
}

static void dla_worker_request_stop(struct dla_wch_worker *worker)
{
	g_mutex_lock(&worker->mutex);
	worker->stop_requested = TRUE;
	g_cond_signal(&worker->condition);
	dla_worker_notify_locked(worker);
	g_mutex_unlock(&worker->mutex);
}

static gboolean dla_worker_take_result(struct dla_wch_worker *worker,
		struct dla_worker_result *result)
{
	gboolean available;

	g_mutex_lock(&worker->mutex);
	available = worker->queue_count &&
		worker->claimed_slot == DLA_WORKER_BUFFER_SLOTS;
	if (available) {
		worker->claimed_slot = worker->queue[worker->queue_head];
		worker->queue_head = (worker->queue_head + 1) % DLA_WORKER_BUFFER_SLOTS;
		worker->queue_count--;
		*result = worker->results[worker->claimed_slot];
	}
	g_mutex_unlock(&worker->mutex);
	return available;
}

static void dla_worker_release_result(struct dla_wch_worker *worker,
		gboolean allow_read)
{
	g_mutex_lock(&worker->mutex);
	if (worker->claimed_slot < DLA_WORKER_BUFFER_SLOTS) {
		worker->busy[worker->claimed_slot] = FALSE;
		worker->claimed_slot = DLA_WORKER_BUFFER_SLOTS;
	}
	if (!allow_read)
		worker->stop_requested = TRUE;
	if (worker->queue_count || worker->log_count)
		dla_worker_notify_locked(worker);
	g_cond_signal(&worker->condition);
	g_mutex_unlock(&worker->mutex);
}

static gboolean dla_worker_take_log(struct dla_wch_worker *worker,
		struct dla_worker_log *entry)
{
	gboolean available;

	g_mutex_lock(&worker->mutex);
	available = worker->log_count != 0;
	if (available) {
		*entry = worker->logs[worker->log_start];
		worker->log_start = (worker->log_start + 1) % DLA_WORKER_LOG_SLOTS;
		worker->log_count--;
	}
	g_mutex_unlock(&worker->mutex);
	return available;
}

static gboolean dla_worker_exited(struct dla_wch_worker *worker,
		struct dla_worker_terminal *terminal)
{
	gboolean complete;

	g_mutex_lock(&worker->mutex);
	complete = worker->terminal_ready && !worker->queue_count &&
		worker->claimed_slot == DLA_WORKER_BUFFER_SLOTS && !worker->log_count;
	if (complete)
		*terminal = worker->terminal;
	g_mutex_unlock(&worker->mutex);
	return complete && worker->thread &&
		WaitForSingleObject(worker->thread, 0) == WAIT_OBJECT_0;
}

static gboolean dla_worker_free(struct dla_wch_worker *worker)
{
	gboolean reconciled;

	if (!worker)
		return TRUE;
	if (worker->thread) {
		if (WaitForSingleObject(worker->thread, 0) != WAIT_OBJECT_0)
			return FALSE;
		g_mutex_lock(&worker->mutex);
		reconciled = worker->terminal_ready && !worker->queue_count &&
		worker->claimed_slot == DLA_WORKER_BUFFER_SLOTS && !worker->log_count;
		g_mutex_unlock(&worker->mutex);
		if (!reconciled)
			return FALSE;
		if (!CloseHandle(worker->thread))
			return FALSE;
		worker->wch->diagnostic_sink = NULL;
		worker->wch->diagnostic_context = NULL;
	}
	g_free(worker->buffers[0]);
	g_free(worker->buffers[1]);
	g_free(worker->drain_buffer);
	g_cond_clear(&worker->condition);
	g_mutex_clear(&worker->mutex);
	g_free(worker);
	return TRUE;
}
#endif
#endif
