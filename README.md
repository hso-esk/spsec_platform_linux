# Platform Abstraction Layer (HAL)

`platform/` is the **only** place with OS/hardware-specific code, and it contains
**only implementations** (`.c` files). The HAL **interface headers** (the contract)
live separately in [`common/hal/include/`](../common/hal/include) so that `platform/` can be split
into its own git submodule per target, while the contract stays in the main repo.

The portable layers — `common/`, `spsec_participant/`, `spsec_can_protocol/` — depend
solely on the `common/hal/include/` interface headers plus the C standard library. **To run
SPsec on another target (e.g. a microcontroller), reimplement the `.c` files in this
folder against your SDK; nothing outside `platform/` needs to change.**

This boundary is enforceable: a grep of `common/`, `spsec_participant/`,
`spsec_can_protocol/` for POSIX/Linux headers (`<sys/*>`, `<linux/*>`, `<net/*>`,
`<pthread.h>`), `fopen`/`FILE`, or `socket`/`select`/`ioctl` returns nothing. (The one
`<signal.h>` in `spsec_participant/participant.c` is for the ISO-C `sig_atomic_t` stop
flag, which is portable — it is not POSIX-specific.)

## HAL modules — interface (`include/*.h`) → Linux implementation (`*.c`)

| Interface header | Linux impl | What to reimplement for a new target |
|---|---|---|
| `communication_interface.h` (in `spsec_can_protocol/include`) | `can_interface.c` | CAN FD send/receive. `can_channel_receive_frame()` returns a portable `CanFrame`; `channel_send_appdata()` transmits one. Replace SocketCAN with your CAN driver. |
| `can_bitrate.h` | `can_bitrate.c` | Map SPsec bitrate codes to your controller's timing config. |
| `timer.h` / `time_utils.h` | `timer.c` | A free-running 0.1 ms tick counter + `platform_time_format()`. The Linux build uses a pthread; a `#if defined(SDK_OS_BAREMETAL) \|\| defined(LPC55_SERIES)` variant of `FreeRunningTimer` (DWT cycle counter, no pthread) is already declared in `timer.h` — define that macro and implement the baremetal timer. |
| `nvol_storage.h` | `nvol_storage.c` | Persist/read key–value blobs and load provisioning key material. Replace the filesystem with flash/EEPROM/OTP. |
| `randomgen.h` | `randomgen.c` | A CSPRNG source. The Linux build wraps the crypto backend's HMAC-DRBG; use your hardware TRNG. |
| `logging.h` | `logging.c` | The log sink: `platform_log_write()`/`platform_log_flush()`. Portable code formats each line; you decide where bytes go — typically a UART instead of stdout/file. |
| `platform_system.h` | `system.c` | `platform_request_stop_on_signal()` — wire the OS "stop" event (SIGINT/SIGTERM on Linux) to a callback. On an MCU this can be a GPIO/watchdog hook or a no-op. |
| `device_info.h` | `device_info.c` | Device identity / serial / public auth key. |
| `led_status.h` | `led_status.c` | Status LED indication (mocked to logs on Linux; drive real GPIOs on hardware). |
| `dll_events.h` | `dll_events.c` | Data-link-layer event detection (RX/TX overrun, address guard) for injection detection. |

## Repository layout (submodule-ready)

```
common/hal/include/          HAL contract (interface headers) — stays in main repo
platform/                    Linux/PC implementations only — future git submodule
  *.c
common/, spsec_participant/, spsec_can_protocol/, src/   portable code + PC entry
```

`platform/*.c` is compiled within the parent build, which puts `common/hal/include/`,
`common/include/`, and `spsec_can_protocol/include/` on the include path. So a platform
implementation includes: its HAL contract headers (`common/hal/include/`), the portable logging
API and helpers (`common/include/spsec_common.h`), and the CAN frame/channel types
(`spsec_can_protocol/include/communication_interface.h`, `spsec_protocol_can.h`). A future
`platform/` submodule keeps only the `.c` files; all headers it needs are provided by the
parent's include paths.

## Notes

- **`src/main_participant.c` is the PC entry point**, not part of the portable library —
  it parses CLI args and boots the participant. On a target you write your own entry
  (e.g. an RTOS task) that performs the same `participant_init()` → `participant_start_main_loop()`
  → `participant_destroy()` sequence and calls `platform_request_stop_on_signal()` (or your
  equivalent).
- The `CommChannel.socket` field is an opaque transport handle (a POSIX fd on Linux); on a
  target it can hold any driver handle.
- Heap (`malloc`/`free`) is used throughout; provide an allocator or a heap in your toolchain.
