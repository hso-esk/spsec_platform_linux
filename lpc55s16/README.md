# LPC55S16-EVK Platform Support

This directory contains bare-metal implementations of the HAL interfaces for the
NXP LPC55S16-EVK board using MCUXpresso SDK.

## Prerequisites

1. **MCUXpresso SDK** for LPC55S16 (download from NXP MCUXpresso SDK Builder)
   - Components needed: FlexCAN, DWT, TRNG, FLASH/IAP, GPIO, UART/DEBUG_CONSOLE, OCOTP
2. **ARM GCC Toolchain** (arm-none-eabi-gcc) - version 10+ recommended
3. **CMake** 3.10+
4. **Ninja** or **Make** build system

## Environment Setup

Set the SDK path environment variable:

```bash
export MCUX_SDK_PATH=/path/to/mcuxpresso-sdk-2.14.0-LPC55S16
```

## Building for LPC55S16

```bash
mkdir build_lpc55s16
cd build_lpc55s16

cmake -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain-arm-none-eabi.cmake \
  -DSPSEC_PLATFORM=lpc55s16 \
  -DSPSEC_CRYPTO_BACKEND=wolfssl \
  -DCMAKE_BUILD_TYPE=Release \
  ..

ninja
```

Or using the provided CMake presets (if configured):

```bash
cmake --preset lpc55s16-release
cmake --build build_lpc55s16
```

## Build Outputs

- Static library: `libspsec_core.a` (links into your application)
- No executable is built by default - you provide your own `main.c` that:
  1. Initializes board hardware (clocks, pins, debug console)
  2. Calls `participant_init()` / `participant_run()` from the SPsec library
  3. Runs the main loop or RTOS scheduler

## Integrating into Your Application

```c
// Your main.c
#include "board.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "fsl_debug_console.h"
#include "spsec_participant.h"

int main(void) {
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitDebugConsole();

    // Initialize SPsec participant
    ParticipantConfig config = {0};
    config.participant_id = 42;
    config.crypto_algorithm = CRYPTO_ALGO_AES_GCM;
    // ... fill other config ...

    if (participant_init(&config) != 0) {
        PRINTF("SPsec init failed\r\n");
        while(1);
    }

    // Run participant (blocking) or integrate with RTOS
    participant_run();

    return 0;
}
```

Add to your CMakeLists.txt:
```cmake
add_executable(my_app main.c)
target_link_libraries(my_app spsec_core)
target_include_directories(my_app PRIVATE ${CMAKE_SOURCE_DIR}/common/hal/include)
```

## Platform Files

| File | HAL Interface | Description |
|------|---------------|-------------|
| `can_interface.c` | `communication_interface.h` | FlexCAN FD driver |
| `timer.c` | `timer.h` | DWT cycle counter (0.1ms ticks) |
| `nvol_storage.c` | `nvol_storage.h` | Flash IAP for key storage |
| `randomgen.c` | `randomgen.h` | TRNG for CSPRNG |
| `logging.c` | `logging.h` | Debug console (UART/RTT) |
| `device_info.c` | `device_info.h` | OCOTP unique ID, versions |
| `led_status.c` | `led_status.h` | RGB LED status patterns |
| `can_bitrate.c` | `can_bitrate.h` | CAN FD bitrate tables |
| `dll_events.c` | `dll_events.h` | FlexCAN error counters |
| `system.c` | `platform_system.h` | Stop request via GPIO/WDT |

## Key Implementation Notes

### Timer (0.1ms resolution)
Uses DWT cycle counter (Cortex-M33). Tracks 64-bit cycle count with overflow
handling. Applies SPsec time scaling (seconds_symbol_index 0-15).

### Non-Volatile Storage
Uses MCUXpresso IAP/FLASH API. Stores key-value pairs in last 16KB of flash
(sector-aligned). Atomic write via full-sector erase + rewrite.

### CAN FD
FlexCAN driver with FD and BRS support. Message buffers:
- RX_MB_IDX (0): Receive
- TX_MB_IDX (1): Transmit

### Crypto
wolfSSL with ASCON/ChaCha20-Poly1305 enabled via `user_settings.h`.

## Troubleshooting

**SDK not found**: Set `MCUX_SDK_PATH` environment variable.

**Linker script missing**: Check SDK version - path may differ.

**FPU errors**: Ensure `-mfpu=fpv5-sp-d16 -mfloat-abi=hard` in toolchain.

**Flash write fails**: Verify sector alignment and IAP API usage.

## Extending

To add a new platform (e.g., STM32H7, RT1170):
1. Create `platform/<new_platform>/` with same HAL implementations
2. Add toolchain file in `cmake/toolchain-<new>.cmake`
3. Add platform to `CMakeLists.txt` SPSEC_PLATFORM options
4. Update `CONTEXT.md` HAL table