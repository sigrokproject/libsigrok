/* A bounded host-side workaround. A completed STOP is not a hardware ACK. */
#ifndef DLA32_STREAM_SETTLE_POLICY_H
#define DLA32_STREAM_SETTLE_POLICY_H
#include <stdint.h>
#define DLA_STREAM_SETTLE_USEC INT64_C(2000000)
static inline int64_t dla_stream_settle_remaining(int enabled, int next_stream,
		int64_t retired_us, int64_t now_us)
{
	int64_t elapsed;

	if (!enabled || next_stream || retired_us <= 0 || now_us < retired_us)
		return 0;
	elapsed = now_us - retired_us;
	return elapsed < DLA_STREAM_SETTLE_USEC ? DLA_STREAM_SETTLE_USEC - elapsed : 0;
}
#endif
