# FNIRSI DLA-32 Plus

The `fnirsi-dla32` driver provides 32 physical logic inputs, Buffer and Stream
acquisition, supported single-stage trigger conditions, threshold presets and
four independent PWM output groups. Channel names/indices follow the physical
inputs. Sparse-channel packing does not renumber enabled physical inputs.

The protocol reference includes whatshu's DLA-16 contribution
<https://github.com/sigrokproject/libsigrok/pull/306> and the GPL-3.0
TechBirdCompany overlay <https://github.com/TechBirdCompany/PXView-DLA32-overlay>.
The DLA-32 driver does not require the DLA-16 driver to be built.

## Windows transport

On Windows x64, the default transport uses a worker thread and the externally
installed WCH runtime `CH375DLL64.dll`. The FNIRSI/WCH USB driver and DLL must
already be installed. The driver loads the DLL from the FNIRSI installation
under Program Files; `FNIRSI_WCH_DLL` can select another absolute DLL path.
It does not redistribute the DLL, change Windows USB bindings or update device
firmware. Device association is intentionally restricted rather than guessing
which of several WCH devices belongs to the requested USB identity.

Linux/libusb discovery checks interface 0 for the expected bulk endpoints and
confirms the DLA-32 model with an identification request before listing a device.
The shared WCH VID/PID (`1a86:5537`) alone does not identify an analyzer. A busy
interface is skipped without detaching its driver. Install the supplied
`contrib/60-libsigrok.rules` using the distribution's libsigrok udev procedure
and reconnect the device if ordinary users lack USB access. The rule matches
the shared WCH VID/PID; the model check is performed by the driver.

An explicit connection such as `fnirsi-dla32:conn=1.49` selects a bus/address
without the discovery transaction. USB addresses can change after reconnection.
Linux/libusb acquisition has not received the equivalent hardware qualification.
Acceptance of the Windows transport dependency is requested for
maintainer review. This contribution does not establish vendor-independent
transport support on all platforms.

Build with the usual libsigrok configuration and `--enable-fnirsi-dla32`.
No private compile-time worker switch is needed. A physical USB 3 connection
is required for Stream. Rate/depth availability depends on mode and enabled
channels; query the driver rather than assuming all rates fit all masks.
In Buffer mode, 1-8 enabled channels allow up to 1 GS/s, 9-16 up to 500 MS/s,
and 17-32 up to 250 MS/s. USB 2 limits download throughput, not these internal
Buffer sampling rates. Stream remains restricted to USB 3.

The optional `FNIRSI_DLA32_WIRE_TRACE` absolute destination is a diagnostic raw
USB trace. An explicitly requested trace is created exclusively; trace-write
failure aborts acquisition. It is off by default and should remain off during
ordinary use or performance measurement.

## Acquisition outcomes

The proposed capture-status/error keys report host acquisition outcome and are
also carried in metadata and saved sessions. `complete` means the requested
sample count was delivered. It does not certify physical signal integrity,
electrical calibration or absence of device-side dropped/repeated samples.
User Stop, disconnection and errors remain distinguishable. API naming/key
allocation and saved-file representation are subject to upstream review.

The current host workaround waits up to two seconds after Stream retirement
before a following Buffer command. This is a bounded, tested mitigation; an
acknowledged USB write is not an independent device-readiness measurement.
Reconnection requires explicit Scan and device selection; there is no automatic
acquisition retry.

## Cleanup and diagnostics

Worker log retention is bounded. Dropped/truncated debug messages are counted
and reported separately; they do not stop acquisition or change its outcome.
Transport results and failed worker notifications still determine failure.

If driver cleanup cannot finish, the C API retains the context for a later
`sr_exit()` retry. The C++ last-owner deleter likewise retains the complete
owner without throwing from a destructor. `Context::retry_cleanup()` retries
retirement and reports a continuing failure as a normal `Error`; `create()`
also retries before initializing another context. Resources are released only
after successful cleanup. Unfinished retirement can retain memory/handles
until a successful retry or process exit; it does not fabricate a successful
hardware STOP.

## Validation and limits

The preceding Windows delivery was tested with bounded Buffer/Stream captures,
sparse/grouped masks, supported rates/depths, protocol decoding, PWM controls,
Stop/error handling and physical USB interruption/recovery. The independent
eight-line Kingst comparison applies to its recorded rate/threshold scope.
These results describe that delivery, not a guarantee for every device or OS.
The submitted branch's separate build/test results belong in its PR description.

Offline protocol tests use fixed command/CRC vectors and independently generated
time-domain sample expectations. They cover every physical bit, sparse masks,
fragmentation across USB read boundaries and output bounds.

Known limits remain: arbitrary-duration lossless all32 Stream at 50 MS/s /
200 MB/s is unqualified; high-rate waveform-width deviations remain recorded;
exact physical trigger position is not qualified and no precise synthetic
trigger marker is emitted. Advanced multi-stage/serial triggering is not
implemented. A large interrupted export failed during temporary-file rename
(EXP-001), and its cause remains unresolved. No filtering, sample repair,
empirical duty correction or channel remapping is used to hide these findings.

PWM settings are commanded/cache state, not electrical readback; their state
becomes unknown after reopening. Threshold presets have functional validation,
not calibrated hysteresis/threshold specifications for every channel.
