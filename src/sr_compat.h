/*
 * sr_compat.h - Declarations for compatibility functions provided by
 *                  sr_compat.c (functions missing from upstream
 *                  libsigrok 0.6.0 but used by newer drivers).
 *
 * This header is NOT included by any upstream source file. It is force-
 * included via CMake set_source_files_properties ONLY into the specific
 * upstream source files that reference these compat functions, so that
 * the upstream sources remain 100% unmodified.
 *
 * Uses plain `extern` instead of SR_PRIV to avoid depending on libsigrok-
 * internal.h (which may not yet be included at -include time). The actual
 * definition in sr_compat.c uses SR_PRIV (hidden visibility); the
 * extern declaration here is compatible (visibility mismatch is harmless
 * because both resolve to the same symbol within the same shared library).
 */

#ifndef SR_COMPAT_H
#define SR_COMPAT_H

#include <glib.h>
#include <stdint.h>

/* Added in upstream libsigrok 0.6.1; missing from 0.6.0.
 * Used by sipeed-slogic-analyzer/api.c for int32 channel-count lookups. */
extern int std_i32_idx(GVariant *data, const int32_t a[], unsigned int n);

#endif /* SR_COMPAT_H */
