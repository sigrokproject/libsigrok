/* SPDX-License-Identifier: GPL-3.0-only */
/* Native Windows transport, informed by TechBirdCompany's GPL-3.0 overlay.
 * WCH function signatures correspond to the installed CH375DLL64 runtime.
 * The vendor DLL stays in its original installation; no driver replacement.
 */
#ifndef LIBSIGROK_FNIRSI_DLA32_WCH_H
#define LIBSIGROK_FNIRSI_DLA32_WCH_H
#ifndef DLA_WCH_WORKER
#ifdef _WIN32
#define DLA_WCH_WORKER 1
#else
#define DLA_WCH_WORKER 0
#endif
#endif
#if DLA_WCH_WORKER != 0 && DLA_WCH_WORKER != 1
#error "DLA_WCH_WORKER must be 0 or 1"
#endif
#if DLA_WCH_WORKER && !defined(_WIN32)
#error "The isolated WCH worker supports Win32 SDK0 only"
#endif
#ifdef _WIN32
#include <windows.h>
#include <stdarg.h>

struct dla_wch {
	ULONG index;
	gboolean opened;
#if DLA_WCH_WORKER
	/* Per-handle sink; configured before thread handoff, removed after exit.
	 * Do not change the process-global frontend logger on a worker thread. */
	void (*diagnostic_sink)(void *, int, const char *);
	void *diagnostic_context;
	DWORD last_operation_error;
#endif
};

static struct {
	HMODULE module;
	HANDLE (WINAPI *open)(ULONG);
	VOID (WINAPI *close)(ULONG);
	ULONG (WINAPI *usb_id)(ULONG);
	BOOL (WINAPI *write)(ULONG, PVOID, PULONG);
	BOOL (WINAPI *read)(ULONG, ULONG, PVOID, PULONG);
	BOOL (WINAPI *timeout)(ULONG, ULONG, ULONG);
	BOOL (WINAPI *timeout_ex)(ULONG, ULONG, ULONG, ULONG, ULONG);
} wch_api;

static void dla_wch_log(struct dla_wch *wch, int level, const char *format, ...)
{
	va_list args;
	char *message;

	va_start(args, format);
	message = g_strdup_vprintf(format, args);
	va_end(args);
#if DLA_WCH_WORKER
	if (wch->diagnostic_sink)
		wch->diagnostic_sink(wch->diagnostic_context, level, message);
	else
#else
	(void)wch;
#endif
		sr_log(level, LOG_PREFIX ": %s", message);
	g_free(message);
}

static gboolean dla_wch_load(void)
{
	const char *override = g_getenv("FNIRSI_WCH_DLL");
	const char *program_files = g_getenv("ProgramW6432");
	char *path;
	gunichar2 *wide_path;

	if (wch_api.module)
		return TRUE;
	if (!program_files)
		program_files = "C:\\Program Files";
	path = override ? g_strdup(override) : g_build_filename(program_files,
		"FNIRSI", "DLA Logic", "CH375DLL64.dll", NULL);
	if (!g_path_is_absolute(path)) {
		sr_err("FNIRSI_WCH_DLL must name an absolute DLL path.");
		g_free(path);
		return FALSE;
	}
	wide_path = g_utf8_to_utf16(path, -1, NULL, NULL, NULL);
	if (wide_path)
		wch_api.module = LoadLibraryExW((LPCWSTR)wide_path, NULL,
			LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
	g_free(wide_path);
	g_free(path);
	if (!wch_api.module)
		return FALSE;
#define WCH_LOAD(field, symbol) do { \
	FARPROC address = GetProcAddress(wch_api.module, symbol); \
	G_STATIC_ASSERT(sizeof(wch_api.field) == sizeof(address)); \
	memcpy(&wch_api.field, &address, sizeof(address)); \
} while (0)
	WCH_LOAD(open, "CH375OpenDevice");
	WCH_LOAD(close, "CH375CloseDevice");
	WCH_LOAD(usb_id, "CH375GetUsbID");
	WCH_LOAD(write, "CH375WriteData");
	WCH_LOAD(read, "CH375ReadEndP");
	WCH_LOAD(timeout, "CH375SetTimeout");
	WCH_LOAD(timeout_ex, "CH375SetTimeoutEx");
#undef WCH_LOAD
	if (!wch_api.open || !wch_api.close || !wch_api.usb_id ||
		!wch_api.write || !wch_api.read || !wch_api.timeout
		) {
		FreeLibrary(wch_api.module);
		memset(&wch_api, 0, sizeof(wch_api));
		sr_err("WCH runtime lacks required functions.");
		return FALSE;
	}
	return TRUE;
}


static int dla_wch_open(struct dla_wch *wch)
{
	HANDLE handle;

	if (!dla_wch_load())
		return SR_ERR_NA;
	handle = wch_api.open(wch->index);
	if (!handle || handle == INVALID_HANDLE_VALUE)
		return SR_ERR_IO;
	wch->opened = TRUE;
	if (wch_api.usb_id(wch->index) != 0x55371a86 ||
		!wch_api.timeout(wch->index, 1000, 20)) {
		wch_api.close(wch->index);
		wch->opened = FALSE;
		return SR_ERR_IO;
	}
	return SR_OK;
}

static void dla_wch_close(struct dla_wch *wch)
{
	if (wch->opened)
		wch_api.close(wch->index);
	wch->opened = FALSE;
}

/* The installed SDK/kernel uses the first timeout field for these I/O
 * operations. Set all fields per operation; each write restores 1000 ms.
 * SDK upload ownership/order is unchanged from V3. Keep setter and I/O
 * adjacent on success so no log callback can alter the global deadline. */
static gboolean dla_wch_operation_timeout(struct dla_wch *wch,
	unsigned int timeout_ms, const char *operation)
{
	BOOL ok;
	DWORD error;

	SetLastError(ERROR_SUCCESS);
	ok = wch_api.timeout_ex ?
		wch_api.timeout_ex(wch->index, timeout_ms, timeout_ms, timeout_ms, timeout_ms) :
		wch_api.timeout(wch->index, timeout_ms, timeout_ms);
	error = GetLastError();
#if DLA_WCH_WORKER
	wch->last_operation_error = error;
#endif
	if (!ok)
		dla_wch_log(wch, SR_LOG_ERR, "WCH %s timeout configuration failed (%u ms, Windows error %lu).",
			operation, timeout_ms, error);
	return ok != FALSE;
}

static int dla_wch_write(struct dla_wch *wch, const uint8_t *data, ULONG length)
{
	ULONG actual = length;
	BOOL ok;
	DWORD error;

	if (!wch->opened) {
#if DLA_WCH_WORKER
		wch->last_operation_error = ERROR_INVALID_HANDLE;
#endif
		return SR_ERR_DEV_CLOSED;
	}
	if (!dla_wch_operation_timeout(wch, 1000, "write"))
		return SR_ERR_IO;
	ok = wch_api.write(wch->index, (PVOID)data, &actual);
	error = GetLastError();
#if DLA_WCH_WORKER
	wch->last_operation_error = error;
#endif
	if (!ok || actual != length) {
		dla_wch_log(wch, SR_LOG_ERR, "WCH write failed (%lu/%lu bytes, Windows error %lu).",
			actual, length, error);
		return SR_ERR_IO;
	}
	return SR_OK;
}

static int dla_wch_read(struct dla_wch *wch, ULONG pipe, uint8_t *data,
	ULONG length, int *actual, unsigned int timeout_ms)
{
	ULONG received = length;
	DWORD error;
	BOOL ok;

	*actual = 0;
	if (!wch->opened) {
#if DLA_WCH_WORKER
		wch->last_operation_error = ERROR_INVALID_HANDLE;
#endif
		return LIBUSB_ERROR_NO_DEVICE;
	}
	if (!dla_wch_operation_timeout(wch, timeout_ms, "read"))
		return LIBUSB_ERROR_IO;
	SetLastError(ERROR_SUCCESS);
	ok = wch_api.read(wch->index, pipe, data, &received);
	error = GetLastError();
#if DLA_WCH_WORKER
	wch->last_operation_error = error;
#endif
	if (ok) {
		if (received > length)
			return LIBUSB_ERROR_OVERFLOW;
		*actual = received;
		return LIBUSB_SUCCESS;
	}
	/* The DLL reports a partial timeout by updating the length. Never use an
	 * unchanged requested length after failure: that could expose stale bytes. */
	if (received < length)
		*actual = received;
	if (error == ERROR_SEM_TIMEOUT || error == ERROR_TIMEOUT || error == WAIT_TIMEOUT)
		return LIBUSB_ERROR_TIMEOUT;
	dla_wch_log(wch, SR_LOG_ERR, "WCH pipe %lu read failed (Windows error %lu, %d bytes).",
		pipe, error, *actual);
	if (error == ERROR_DEVICE_NOT_CONNECTED || error == ERROR_NO_SUCH_DEVICE)
		return LIBUSB_ERROR_NO_DEVICE;
	return LIBUSB_ERROR_IO;
}

static gboolean dla_wch_model32(struct dla_wch *wch)
{
	uint8_t command[512] = {0x0a, 0x10}, reply[512];
	int actual, ret;

	if (dla_wch_write(wch, command, sizeof(command)) != SR_OK)
		return FALSE;
	ret = dla_wch_read(wch, 2, reply, sizeof(reply), &actual, 500);
	return ret == LIBUSB_SUCCESS && actual >= 19 && reply[0] == 0x0a &&
		reply[1] == 0x02 && reply[2] == 0x0d &&
		!memcmp(reply + 13, "DL32", 4) && reply[18] == 0x0b;
}
#endif
#endif
