/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Public API only: synthetic cleanup callbacks, no driver scans or USB I/O. */
#include <libsigrokcxx/libsigrokcxx.hpp>
#include <cstdio>
#include <stdexcept>

static int cleanup_result, cleanup_calls, marker;

static int cleanup(const sr_dev_driver *)
{
	cleanup_calls++;
	return cleanup_result;
}

static void require(bool condition, const char *message)
{
	if (!condition)
		throw std::runtime_error(message);
}

int main()
{
	try {
		/* Driver descriptors are shared between C contexts. The inspection
		 * context gives this test access without C++ private/test hooks. */
		sr_context *inspection = nullptr;
		require(sr_init(&inspection) == SR_OK, "inspection context init");
		auto **drivers = sr_driver_list(inspection);
		if (!drivers || !drivers[0]) {
			sr_exit(inspection);
			return 77;
		}
		auto *driver = drivers[0];
		auto original_cleanup = driver->cleanup;
		for (int scenario = 0; scenario < 3; scenario++) {
			auto context = sigrok::Context::create();
			require(!driver->context, "driver must be uninitialized");
			driver->context = &marker;
			driver->cleanup = cleanup;
			cleanup_calls = 0;
			cleanup_result = scenario ? SR_ERR_IO : SR_OK;
			context.reset();
			require(cleanup_calls == 1, "last-owner cleanup count");
			if (scenario) {
				require(driver->context == &marker, "failed cleanup retained resources");
				bool retry_error = false, create_error = false;
				try {
					sigrok::Context::retry_cleanup();
				} catch (const sigrok::Error &error) {
					retry_error = error.result == SR_ERR_IO;
				}
				try {
					auto next = sigrok::Context::create();
				} catch (const sigrok::Error &error) {
					create_error = error.result == SR_ERR_IO;
				}
				require(retry_error && create_error && cleanup_calls == 3 &&
					driver->context == &marker, "retryable failure and initialization gate");
				cleanup_result = SR_OK;
				if (scenario == 1)
					sigrok::Context::retry_cleanup();
				else {
					auto next = sigrok::Context::create();
				}
				require(cleanup_calls == 4, "successful retirement count");
			}
			require(!driver->context, "successful cleanup uninitializes driver");
			const int previous_calls = cleanup_calls;
			sigrok::Context::retry_cleanup();
			require(cleanup_calls == previous_calls, "retirement is not repeated");
			driver->cleanup = original_cleanup;
		}
		require(sr_exit(inspection) == SR_OK, "inspection context cleanup");
		std::puts("PASS C++ context cleanup: success, failure, explicit and automatic retry");
		return 0;
	} catch (const std::exception &error) {
		std::fprintf(stderr, "%s\n", error.what());
		return 1;
	}
}
