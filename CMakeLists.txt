#===============================================================================
#= libsigrok -- Shared library with 100% unmodified upstream libsigrok source
#-------------------------------------------------------------------------------
# This builds the upstream libsigrok 0.6.0 as an isolated SHARED library.
# All source files under src/ are COPIED VERBATIM from upstream libsigrok
# (C:\Users\admin\Downloads\libsigrok\src) -- zero modifications to upstream.
# Driver registration uses the upstream SR_REGISTER_DEV_DRIVER section
# mechanism (driver_list_start.c + driver_list_stop.c + per-driver api.c).
# HAVE_DRIVERS is defined so sr_drivers_init() collects drivers from the
# __sr_driver_list section automatically.
#===============================================================================

# -- Collect all upstream .c files (recursive) --------------------------------
# GLOB_RECURSE picks up src/*.c, src/hardware/*/*.c, src/input/*.c, src/output/*.c,
# src/scpi/*.c, src/modbus/*.c, src/transform/*.c, src/minilzo/*.c, src/bt/*.c,
# src/dmm/*.c, src/lcr/*.c, src/scale/*.c.
# CONFIGURE_DEPENDS makes CMake re-scan when files are added/removed.
file(GLOB_RECURSE SR_UPSTREAM_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

# -- driver_list_start.c / driver_list_stop.c need special link ordering -------
# start MUST link before all drivers; stop MUST link after all drivers.
# They define sr_driver_list__start[] / sr_driver_list__stop[] section boundaries.
set(SR_DRIVER_LIST_START "${CMAKE_CURRENT_SOURCE_DIR}/src/driver_list_start.c")
set(SR_DRIVER_LIST_STOP  "${CMAKE_CURRENT_SOURCE_DIR}/src/driver_list_stop.c")

# -- Files to exclude from the GLOB result ------------------------------------
#   driver_list_start.c / driver_list_stop.c -- handled separately for ordering
#   driver_registry.c                       -- deleted (Phase 3, no longer used)
#   scpi_libgpib.c                          -- needs GPIB headers (unavailable)
#   scpi_visa.c                             -- needs VISA headers (unavailable)
#   scpi_vxi.c / vxi_clnt.c / vxi_xdr.c     -- need rpc/rpc.h (unavailable on MinGW)
#   serial_hid_bu86x.c / ch9325 / cp2110 / victor -- need hidapi (serial_hid.c
#                                            has #ifdef HAVE_LIBHIDAPI stubs, so
#                                            chip-specific files are not needed)
#   minilzo/testmini.c                       -- test executable, not library code
#   hardware/dcttech-usbrelay               -- upstream uses <hidapi.h>
#   hardware/link-mso19                     -- upstream uses <libserialport.h>
list(REMOVE_ITEM SR_UPSTREAM_SOURCES
    ${SR_DRIVER_LIST_START}
    ${SR_DRIVER_LIST_STOP}
    "${CMAKE_CURRENT_SOURCE_DIR}/src/driver_registry.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/scpi/scpi_libgpib.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/scpi/scpi_visa.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/scpi/scpi_vxi.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/scpi/vxi_clnt.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/scpi/vxi_xdr.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/serial_hid_bu86x.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/serial_hid_ch9325.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/serial_hid_cp2110.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/serial_hid_victor.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/minilzo/testmini.c"
)
list(FILTER SR_UPSTREAM_SOURCES EXCLUDE REGEX
    "/src/hardware/(dcttech-usbrelay|link-mso19|beaglelogic|baylibre-acme|hung-chang-dso-2100|hp-3478a|mooshimeter-dmm)/.*\\.c$"
)
#   beaglelogic        -- Linux-specific (needs sys/mman.h, sys/ioctl.h,
#                         /dev/beaglelogic kernel node). Unavailable on MinGW.
#   baylibre-acme      -- Linux-specific (needs sys/timerfd.h, arpa/inet.h
#                         without #ifdef guards). Unavailable on MinGW.
#   hung-chang-dso-2100 -- needs ieee1284.h (parallel port). Unavailable on MinGW.
#   hp-3478a           -- unconditionally calls sr_scpi_gpib_spoll() which is
#                         only declared under #ifdef HAVE_LIBGPIB (scpi.h).
#                         Without HAVE_LIBGPIB, compilation fails. The upstream
#                         driver assumes GPIB is available.
#   mooshimeter-dmm    -- unconditionally calls sr_bt_*() which is only
#                         declared under #ifdef HAVE_BLUETOOTH. Without
#                         HAVE_BLUETOOTH, compilation fails.

# -- Combined source list with correct link ordering --------------------------
# Order: driver_list_start.c FIRST, then all upstream sources, then
# driver_list_stop.c LAST. The start/stop ordering matters for the
# __sr_driver_list section layout (start defines the section head with a
# dummy NULL, stop defines the section tail with a dummy NULL;
# sr_drivers_init iterates from start+1 to stop exclusive).
# sr_compat.c (in src/) is picked up by GLOB_RECURSE automatically.
set(libsigrok_SOURCES
    ${SR_DRIVER_LIST_START}
    ${SR_UPSTREAM_SOURCES}
    ${SR_DRIVER_LIST_STOP}
)

# -- Static library target -----------------------------------------------------
# 构建为静态库，直接链接进 PXView.exe。
# 之前用 SHARED DLL 时，扫描阶段 fx2lafw 调用 libusb_open 报 LIBUSB_ERROR_ACCESS，
# 同时 usb_source_new 报 "Failed to get libusb file descriptors"。
# 怀疑是 DLL 边界的 libusb context 在某些路径上行为异常，改为静态链接让所有
# libsigrok/libusb 代码运行在同一 PXView.exe 进程映像内，避免 DLL 间状态隔离。
add_library(libsigrok STATIC ${libsigrok_SOURCES})

# -- Include paths -------------------------------------------------------------
# libsigrok/include -- upstream libsigrok/libsigrok.h, proto.h, version.h
#                         (100% unmodified upstream copies)
# libsigrok/src      -- config.h (local), libsigrok-internal.h, scpi.h,
#                         serial_hid.h (all upstream)
target_include_directories(libsigrok BEFORE PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# -- Visibility ---------------------------------------------------------------
# NOTE: -fvisibility=hidden is intentionally NOT set on Windows.
# Upstream libsigrok's SR_API/SR_PRIV macros are EMPTY on _WIN32 (upstream
# uses autotools/libtool, not __declspec). The actual DLL export is handled
# by WINDOWS_EXPORT_ALL_SYMBOLS=ON below.
# Setting -fvisibility=hidden here would hide all symbols at the compiler
# level, and combined with empty SR_API/SR_PRIV would leave no exports.
if(NOT WIN32)
    target_compile_options(libsigrok PRIVATE
        -fvisibility=hidden
        -fvisibility-inlines-hidden
    )
endif()

# -- Compile definitions -------------------------------------------------------
# HAVE_LIBUSB_1_0  -- libusb backend
# HAVE_SERIAL_COMM -- sr_serial_dev_inst struct definition
# HAVE_ZLIB        -- session file compression
# HAVE_LIBZIP      -- session file archive support
# HAVE_DRIVERS     -- ENABLE upstream section-based driver registration
#                    (sr_drivers_init iterates __sr_driver_list section)
# NOTE: No __declspec(dllexport) annotations appear anywhere in the build.
# This is REQUIRED for WINDOWS_EXPORT_ALL_SYMBOLS to work -- any dllexport
# annotation would cause MinGW ld to silently disable --export-all-symbols.
target_compile_definitions(libsigrok PRIVATE
    HAVE_LIBUSB_1_0
    HAVE_SERIAL_COMM
    HAVE_ZLIB
    HAVE_LIBZIP
    HAVE_DRIVERS
    _GNU_SOURCE
    # FIRMWARE_DIR: hard-coded firmware search path (resource.c).
    # Points to the install.dir layout where sigrok-firmware files are staged.
    # Without this, #ifdef FIRMWARE_DIR in resource.c is not compiled, and
    # libsigrok only searches $SIGROK_FIRMWARE_DIR env var and system data dirs
    # (C:\Users\<user>\AppData\Local\sigrok-firmware etc.) — missing the
    # install.dir/share/sigrok-firmware bundled with PXView.
    "FIRMWARE_DIR=\"${CMAKE_SOURCE_DIR}/install.dir/share/sigrok-firmware\""
)

# -- C standard ----------------------------------------------------------------
set_target_properties(libsigrok PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
)

# -- Output name + output directory --------------------------------------------
# OUTPUT_NAME "sigrok" makes CMake produce `libsigrok.dll/.so/.dylib` on all
# platforms (CMake auto-prepends the `lib` prefix on Unix-like platforms and
# MinGW). Without this, the target name `libsigrok` would yield `liblibsigrok.*`
# (double `lib` prefix) on Windows/MinGW. The target name is kept as `libsigrok`
# for semantic clarity in CMake code; only the artifact filename is normalized.
set_target_properties(libsigrok PROPERTIES
    OUTPUT_NAME "sigrok"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build.dir"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build.dir"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build.dir"
)

# -- Link dependencies ---------------------------------------------------------
# glib-2.0, libusb-1.0, zlib are already found by the parent CMakeLists.txt
# (deps.cmake) and their include paths are in the global include_directories().
# libzip is needed for session file I/O (session_driver.c, session_file.c, srzip.c).
# libftdi1 is needed by asix-sigma, chronovu-la, ftdi-la, ikalogic-scanaplus,
# pipistrello-ols drivers (FTDI-based USB acquisition).
# nettle is needed by rdtech-tc driver (AES-256 firmware decryption).
pkg_search_module(LIBZIP libzip)
if(NOT LIBZIP_FOUND)
    message(FATAL_ERROR "Please install libzip!")
endif()
# libftdi1 is needed by FTDI-based USB acquisition drivers (asix-sigma,
# chronovu-la, ftdi-la, ikalogic-scanaplus, pipistrello-ols). If not found,
# only those 4-5 drivers fail to compile (acceptable); the rest of libsigrok
# builds normally. The -lftdi1 link flag below is kept conditional so a missing
# libftdi1 does not break the link step when its drivers are excluded.
pkg_search_module(LIBFTDI1 libftdi1)
if(NOT LIBFTDI1_FOUND)
    message(WARNING "libftdi1 not found - FTDI-based drivers will be disabled")
else()
    target_include_directories(libsigrok PRIVATE ${LIBFTDI1_INCLUDE_DIRS})
endif()
target_link_libraries(libsigrok PUBLIC
    ${LIBUSB_1_LIBRARIES}
    ${ZLIB_LIBRARIES}
    ${LIBZIP_LIBRARIES}
    -lglib-2.0
    -lws2_32
    $<$<BOOL:${LIBFTDI1_FOUND}>:-lftdi1>
    -lnettle
)

# -- Compat declaration injection for sipeed-slogic-analyzer -------------------
# sipeed-slogic-analyzer/api.c (from the slogic-dev branch) calls std_i32_idx()
# which was added in upstream libsigrok 0.6.1 but is missing from 0.6.0. The
# implementation is in src/sr_compat.c; the declaration lives in
# src/sr_compat.h. To avoid modifying the upstream api.c source, the
# declaration is force-injected via -include ONLY into this one file.
set_source_files_properties(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/hardware/sipeed-slogic-analyzer/api.c"
    PROPERTIES
    COMPILE_OPTIONS "-include;${CMAKE_CURRENT_SOURCE_DIR}/src/sr_compat.h"
)

# =============================================================================
# Test executables (NOT built by default -- run `ninja test_sr_*` explicitly)
# =============================================================================
# Test executables removed: the original test sources (tests/test_srstd_*.c)
# were written for Phase 1/2 when srstd.h and srstd_rename.h existed. Those
# headers and the bridge/ directory have been removed; the test sources
# referenced deleted headers and were EXCLUDE_FROM_ALL (never built). The
# tests/ directory has been deleted. Add new tests here if needed in the future.
# =============================================================================
