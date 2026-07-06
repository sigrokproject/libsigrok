/*
 * sr_compat.c - Compatibility shims for functions missing from
 *                  upstream libsigrok 0.6.0 but used by newer drivers.
 *
 * Functions provided:
 *   std_i32_idx() — added in upstream libsigrok 0.6.1; missing from 0.6.0.
 *                   The sipeed-slogic-analyzer driver uses it for int32
 *                   channel-count table lookups.
 *   sr_scpi_gpib_spoll() — defined in scpi_libgpib.c which is excluded from
 *                          the build (needs libgpib headers). Stubbed out
 *                          so hp-3478a and scpi-pps compile; GPIB hardware
 *                          is unsupported in this build anyway.
 */

#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

/*
 * std_i32_idx - Find the index of a GVariant int32 value in an int32_t array.
 *
 * Mirrors the upstream implementation from std.c:
 *   return find_in_array(data, G_VARIANT_TYPE_INT32, a, n);
 *
 * Implemented standalone because find_in_array() is static in std.c and
 * the 0.6.0 version lacks a G_VARIANT_CLASS_INT32 case anyway.
 */
SR_PRIV int std_i32_idx(GVariant *data, const int32_t a[], unsigned int n)
{
	gint32 val;
	unsigned int i;

	if (!g_variant_is_of_type(data, G_VARIANT_TYPE_INT32))
		return -1;

	val = g_variant_get_int32(data);
	for (i = 0; i < n; i++)
		if (val == a[i])
			return (int)i;

	return -1;
}

/*
 * sr_scpi_gpib_spoll - GPIB serial poll stub.
 *
 * The real implementation lives in scpi_libgpib.c, which is excluded from
 * the build because it requires the libgpib headers (unavailable on MinGW/
 * Windows). hp-3478a and scpi-pps reference this function; without the stub
 * they fail to link. Returning SR_ERR means GPIB serial polls always fail,
 * which is acceptable — no GPIB hardware is supported in this environment.
 */
SR_PRIV int sr_scpi_gpib_spoll(struct sr_scpi_dev_inst *scpi, char *buf)
{
	(void)scpi;
	(void)buf;
	return SR_ERR;
}
