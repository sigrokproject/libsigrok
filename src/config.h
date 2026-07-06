/* config.h -- Build configuration for libsigrok.
 *
 * This is NOT an upstream file (upstream generates config.h via autoconf).
 * libsigrok provides its own config.h to satisfy #include <config.h>
 * directives in upstream src/ files. The upstream source files remain
 * unmodified; only this local config.h is libsigrok's own copy.
 */

#ifndef SR_CONFIG_H
#define SR_CONFIG_H

/* Define to 1 if you have the <dlfcn.h> header file. */
#define HAVE_DLFCN_H 1

/* Define to 1 if you have the <fcntl.h> header file. */
#define HAVE_FCNTL_H 1

/* Define to 1 if you have the `gettimeofday' function. */
#define HAVE_GETTIMEOFDAY 1

/* Define to 1 if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H 1

/* Specifies whether we have libusb. */
#define HAVE_LIBUSB_1_0 1

/* Specifies whether we have serial communication support.
 * Required for sr_serial_dev_inst struct definition in libsigrok-internal.h.
 * Even without libserialport/termios, tcpraw/USB serial paths need this. */
#define HAVE_SERIAL_COMM 1

/* Specifies whether we have zlib (compression for session files). */
#define HAVE_ZLIB 1

/* Specifies whether we have libzip (session file archive support). */
#define HAVE_LIBZIP 1

/* Build-time version strings for sr_buildinfo_*() in backend.c */
#define CONF_ZLIB_VERSION "1.3.2"
#define CONF_LIBZIP_VERSION "1.11.4"
#define CONF_HOST "x86_64-w64-mingw32"

/* Define to 1 if you have the <memory.h> header file. */
#define HAVE_MEMORY_H 1

/* Define to 1 if you have the `memset' function. */
#define HAVE_MEMSET 1

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have the `strchr' function. */
#define HAVE_STRCHR 1

/* Define to 1 if you have the `strcspn' function. */
#define HAVE_STRCSPN 1

/* Define to 1 if you have the `strdup' function. */
#define HAVE_STRDUP 1

/* Define to 1 if you have the `strerror' function. */
#define HAVE_STRERROR 1

/* Define to 1 if you have the <strings.h> header file. */
#define HAVE_STRINGS_H 1

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if you have the `strncasecmp' function. */
#define HAVE_STRNCASECMP 1

/* Define to 1 if you have the `strstr' function. */
#define HAVE_STRSTR 1

/* Define to 1 if you have the `strtol' function. */
#define HAVE_STRTOL 1

/* Define to 1 if you have the `strtoul' function. */
#define HAVE_STRTOUL 1

/* Define to 1 if you have the `strtoull' function. */
#define HAVE_STRTOULL 1

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/time.h> header file. */
#define HAVE_SYS_TIME_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define to 1 if you have the <termios.h> header file. */
/* #undef HAVE_TERMIOS_H */

/* Define to 1 if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H 1

/* Name of package */
#define PACKAGE "libsigrok"

/* Define to the full name of this package. */
#define PACKAGE_NAME "libsigrok"

/* Define to the version of this package. */
#define PACKAGE_VERSION "0.6.0"

/* Version number of package */
#define VERSION "0.6.0"

/* Define to 1 if you have the ANSI C header files. */
#define STDC_HEADERS 1

#endif /* SR_CONFIG_H */
