# hsdaoh-la: libsigrok driver for the hsdaoh la8 logic analyzer

8 channel logic analyzer made of

- a Raspberry Pi Pico 2 (RP2350) running the **la8** app of
  [hsdaoh-rp2350](https://github.com/dovandung/hsdaoh-rp2350) (branch `la8`):
  it samples GP0..GP7 continuously and streams every sample over HDMI
- an **MS2130** USB3 HDMI capture stick, read on the PC by
  [libhsdaoh](https://github.com/dovandung/hsdaoh) (branch `la`)

Up to 140 MS/s (preset 140: system clock 280 MHz / divider 2, HDMI clock
384 MHz) down to about 4.3 kS/s (divider 65535).

## How it works

- The PC scans the stream for the trigger (first trigger stage of the
  PulseView/sigrok trigger: 0, 1, rising, falling, any edge per channel;
  all conditions of the stage must be true at the same sample).
- Pre-trigger (capture ratio) and post-trigger samples are collected in RAM,
  1 byte per sample. Only when the capture is complete is it sent to the
  session, so PulseView shows the data when the acquisition ends.
- Without a trigger the capture starts immediately.
- Every HDMI frame carries the Pico's sample counter. If data is lost while
  waiting for the trigger, the pre-trigger history is restarted (it never
  spans a gap); if data is lost during a capture, the capture is discarded
  and the driver waits for the trigger again.
- Stop in PulseView sends what has been captured after the trigger so far.

## Sample rate

The la8 firmware sets the rate: Pico system clock / integer divider.

- **With the Pico's USB serial port** given as connection (PulseView: Connect
  to Device > Serial Port, e.g. COM5 or /dev/ttyACM0; sigrok-cli:
  `conn=COM5`), the driver reads the firmware status at scan time and offers
  the achievable rates (the 6 fastest dividers plus the dividers closest to
  a 1-2-5 series). Choosing a rate sends `div <n>` to the firmware.
- **Without it** (Connect to Device > USB), the rate cannot be changed from
  PulseView; the rate the device reports in the HDMI metadata is used for
  the captured data (it is sent as a samplerate meta packet).

Close any terminal program on the Pico's COM port first.

## Building

1. Build and install libhsdaoh (branch `la` of the hsdaoh repository) so that
   `pkg-config --modversion libhsdaoh` works. libhsdaoh needs libusb-1.0 and
   libuvc.
2. Build libsigrok as usual (`./autogen.sh && ./configure && make install`).
   The driver is enabled automatically when libhsdaoh and serial support
   (libserialport) are found; the configure summary lists `hsdaoh-la`.
   `--disable-hsdaoh-la` / `--enable-hsdaoh-la` work like for other drivers.
3. Build PulseView / sigrok-cli against this libsigrok.

## Windows build (sigrok's MinGW cross build)

PulseView and sigrok-cli for Windows are built with
[sigrok-util](https://github.com/sigrokproject/sigrok-util)
`cross-compile/mingw/sigrok-cross-mingw` on Linux, with MXE. That build is
static and 32-bit by default (`TARGET="i686"`; `x86_64` for 64-bit).

1. Set up MXE as described in sigrok-util's `cross-compile/mingw/README`
   (the MXE package list there already includes `libusb1`).
2. Apply `sigrok-cross-mingw.patch` from this directory to sigrok-util:

       cd sigrok-util
       patch -p1 < .../sigrok-cross-mingw.patch

   It makes the script clone this libsigrok branch (`hsdaoh-la` of
   github.com/dovandung/libsigrok) and run `mxe-build-deps.sh` before
   building libsigrok.
3. Run `cross-compile/mingw/sigrok-cross-mingw` as usual.

`mxe-build-deps.sh` builds, statically, into the sigrok prefix:

- **libusb 1.0.30**, a private copy for libhsdaoh. sigrok's MXE ships an old
  libusb fork (1.0.20-rc3 "event-abstraction") that libsigrok's own USB code
  needs on Windows, but that has no WinUSB isochronous support (added in
  1.0.23) and is not what libhsdaoh is tested with.
- **libuvc** (github.com/libuvc/libuvc) and **libhsdaoh**
  (github.com/dovandung/hsdaoh, branch `la`, library only), both built
  against that libusb.
- Then every symbol the private libusb defines is renamed
  (`libusb_init` -> `hsdaoh_priv_libusb_init`, ...) in all three libraries,
  installed as `libhsdaoh.a`, `libhsdaoh_uvc.a`, `libhsdaoh_usb.a`, so both
  libusb versions link into the same program without clashing.
- A `libhsdaoh.pc` that links all three.

libsigrok's configure then finds `libhsdaoh` through pkg-config, and the
summary lists the driver as `hsdaoh-la`.

The sources can be changed with `LIBUSB_REPO`/`LIBUSB_BRANCH`,
`LIBUVC_REPO`/`LIBUVC_BRANCH`, `HSDAOH_REPO`/`HSDAOH_BRANCH`, or local
source trees with `LIBUSB_SRC`, `LIBUVC_SRC`, `HSDAOH_SRC`.

The script also works with the newer sigrok CI build
(github.com/sigrokproject/sigrok-build, `ci/mxe`, Docker image
`ghcr.io/sigrokproject/sigrok-mxe`): it takes `INSTALL_DIR` when `PREFIX`
is not set.

### 32-bit builds

In a 32-bit (i686) build the sample count is limited to 400 M samples
(64-bit: 4 G), because the capture is held in RAM twice (driver and
PulseView) within the 2 GB address space of a 32-bit process.

### Running it on Windows

- The MS2130 stick needs the WinUSB driver (Zadig is shipped with PulseView).
- The Pico's USB serial port (for setting the rate) is a normal COM port;
  close any terminal program using it.

## sigrok-cli examples

    sigrok-cli -d hsdaoh-la --scan
    sigrok-cli -d hsdaoh-la:conn=COM5 --config samplerate=10M --samples 1M \
        --triggers D0=r --config captureratio=10 -o capture.sr
