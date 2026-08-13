# Bug-Fix Plan & Execution Log — branch `bug-fixes/checks`

Date: 2026-08-14
Base: `cake` (886f52b)
Branch: `bug-fixes/checks`

## 1. Reconnaissance (Step 1)

Ran on the `cake` base:
1. **`verify-mbed-host-syntax.py`** (host-side MSVC stub check) — **PASSED** (ALL_ADHOC_CHECKS_PASS). Only catches syntax/braces — no ARM codegen, no link.
2. **Full ARM GCC build** (arm-none-eabi-gcc 14.2 + mbed-os 6.16.0 + cmake/ninja) — **FAILED** with multiple distinct defects.

## 2. Bugs identified (Step 2)

### A. Build-environment defects (mbed-os 6.16 cmake is broken)
| # | Bug | Symptom | Root cause |
|---|-----|---------|------------|
| A1 | App target has NO include dirs | `mbed_rtx.h`, `cmsis_compiler.h`, `PinNames.h` not found | `mbed_configure_app_target()` is an **empty function** in mbed-os/CMakeLists.txt:206 — it no longer wires target includes |
| A2 | App target has NO compile definitions | `MBED_CONF_*` macros undeclared (ADC_VREF, RTOS_THREAD_STACK_SIZE, etc.) | Same empty function — dropped `MBED_TARGET_DEFINITIONS`/`MBED_CONFIG_DEFINITIONS` |
| A3 | No CPU flags | CMSIS RTX "Unknown Arm Architecture!" | Empty function dropped `-mcpu=cortex-m3 -mthumb` |
| A4 | Windows DLL link flags | `ld: unrecognized option '--major-image-version'` | CMake 4.4 Windows platform rules leak into ARM link (rules.ninja hardcodes them) |
| A5 | Windows system libs in link | `cannot find -luuid/-lcomdlg32/-ladvapi32` | CMake adds host libs to LINK_LIBRARIES |
| A6 | Target device sources not compiled | `serial_baud`, `port_write`, HAL_ADC_* undefined at link | `EXCLUDE_FROM_ALL` target libs never built |
| A7 | HAL driver sources not compiled | `HAL_ADC_Init`, `HAL_FLASHEx_Erase` undefined | STM32Cube HAL .c files not in build |
| A8 | Linker script not preprocessed | `MBED_CONF_TARGET_BOOT_STACK_SIZE`, `MBED_RAM_START` undefined | mbed's `mbed_set_linker_script` (preprocesses .ld via gcc -E) not wired |
| A9 | No --gc-sections / minimal-printf | FLASH overflow (153KB > 128KB) | Full newlib printf + unused HAL code linked |

### B. App-level config issues (hardware)
| # | Bug | Status |
|---|-----|--------|
| B1 | `TRACKER_FWD_DUTY 0.250f` — 5ms pulse, WAY above SG90 spec max (2.5ms), servo damage risk | **Fixed** → 0.100f (safe nominal) |

## 3. Strategic plan (Step 3) — fix order by dependency

1. **A1+A2+A3** — CMakeLists.txt: add include dirs + compile definitions (from mbed_config.cmake) + CPU flags directly to app target.
2. **A4+A5** — patch rules.ninja (strip DLL flags) + build.ninja (strip Windows libs) after configure.
3. **A6** — add STM32F1 device sources + common TARGET_STM HAL API files to target_sources.
4. **A7** — add STM32F1xx HAL driver .c files + PeripheralPins.c.
5. **A8** — preprocess linker script (gcc -E) → stm32f103xb_preprocessed.ld, use -T.
6. **A9** — add --gc-sections + MBED_MINIMAL_PRINTF + printf --wrap flags.
7. **B1** — config.h: TRACKER_FWD_DUTY 0.250 → 0.100.

## 4. Fixes applied (Step 4) — all verified by successful link

- **CMakeLists.txt**: comprehensive target_include_directories, target_compile_definitions (`${MBED_TARGET_DEFINITIONS} ${MBED_CONFIG_DEFINITIONS}` + STM32F103xB + USE_HAL_DRIVER + MBED_MINIMAL_PRINTF), target_compile_options (-mcpu=cortex-m3 -mthumb -ffunction-sections...), target_link_options (--wrap=main, --gc-sections, printf wraps, -T preprocessed.ld), ~60 target device/HAL sources added.
- **config.h**: TRACKER_FWD_DUTY 0.250f → 0.100f (safe).
- **mbed-os toolchain patch** (local, gitignored): response-file flags → 0 (empty .rsp bug).
- **Post-configure ninja patch** (local): strip `--out-implib --major-image-version` + Windows libs.
- **Linker script**: preprocessed with gcc -E → resolves cmsis_nvic.h macros.

## 5. Final verification (Step 5)

- ✅ `verify-mbed-host-syntax.py` → ALL_ADHOC_CHECKS_PASS
- ✅ ARM GCC build: 204/204 objects compiled, **linked successfully**
- ✅ Output: `mbed-os-example-blinky.bin` (127,664 bytes — **fits 128KB flash**)
- ✅ `.hex` generated (359KB)
