# hantek-dso2xxx libsigrok driver

A libsigrok driver for the Hantek DSO2xxx oscilloscope series (DSO2C10,
DSO2C15, DSO2D10, DSO2D15) that receives waveform data over TCP using the
`quick-fetch` firmware patch by phmarek:
https://github.com/phmarek/hantek-dso2000-quick-fetch

Initial contents generated via Claude.AI,
https://claude.ai/share/bcc56b39-53ab-45be-a91d-251cbe64fa98

---

## How it works

The patched firmware (`quick-fetch.so`, `LD_PRELOAD`'d on the scope) listens
on a TCP port. When the user presses `SAVE_TO_USB` the scope freezes the
current waveform and streams a compact binary frame to the connected client.

---

## Prerequisites

1. Install the quick-fetch patch on your scope following the instructions
   at https://github.com/phmarek/hantek-dso2000-quick-fetch

2. Use DavidAlfa's USB-networking kernel so the scope appears at (e.g.)
   172.31.254.254 via USB networking. Without this, the TCP server is not
   reachable.

---


## No configurable acquisition options

All scope settings (timebase, volts/div, coupling, trigger, memory depth)
are configured directly on the instrument front panel. The driver reads
them passively from the binary frame header.

If there's much interest I'll update both the `quick-fetch` patch 
and the driver to allow modifying the trigger level, position, etc.

---

## Firmware versions

The quick-fetch patch supports:
  3.0.0(220727.00) - 2022-07-27
  3.0.0(230327.00) - 2023-03-27
  3.0.1(250418.00) - 2025-04-18

---

## License

GPL-3.0-or-later, matching the rest of libsigrok.
