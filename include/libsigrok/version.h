/*
 * version.h -- Static version definitions for libsigrok.
 *
 * Generated from version.h.in for the upstream libsigrok 0.6.0 build.
 * Hardcoded values replace the autotools #undef placeholders.
 * This file replaces version.h.in (which is the autotools template) so
 * that no configure_file step is needed in CMake.
 */

#ifndef LIBSIGROK_VERSION_H
#define LIBSIGROK_VERSION_H

/* No git-version suffix for release builds. */
#define SR_PACKAGE_VERSION_STRING_SUFFIX ""

/* Package version macros. */
#define SR_PACKAGE_VERSION_MAJOR 0
#define SR_PACKAGE_VERSION_MINOR 6
#define SR_PACKAGE_VERSION_MICRO 0

#define SR_PACKAGE_VERSION_STRING_PREFIX "0.6.0"
#define SR_PACKAGE_VERSION_STRING (SR_PACKAGE_VERSION_STRING_PREFIX SR_PACKAGE_VERSION_STRING_SUFFIX)

/* Library/libtool version macros. */
#define SR_LIB_VERSION_CURRENT 4
#define SR_LIB_VERSION_REVISION 0
#define SR_LIB_VERSION_AGE 0
#define SR_LIB_VERSION_STRING "4:0:0"

#endif /* LIBSIGROK_VERSION_H */
