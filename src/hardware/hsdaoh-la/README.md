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

For Windows, the sigrok cross build (sigrok-util, `cross-compile/mingw`)
needs libhsdaoh (and its libusb/libuvc) added to its dependency list.

On Windows the MS2130 stick needs the WinUSB driver (Zadig), as for the
hsdaoh command line tools.

## sigrok-cli examples

    sigrok-cli -d hsdaoh-la --scan
    sigrok-cli -d hsdaoh-la:conn=COM5 --config samplerate=10M --samples 1M \
        --triggers D0=r --config captureratio=10 -o capture.sr
