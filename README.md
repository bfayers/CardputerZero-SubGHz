# SubGHz for CardputerZero (Cap CC1101)

A **Flipper Zero-inspired** Sub-GHz transceiver application for the **M5Stack CardputerZero** and **Cap CC1101** accessory, built on the CardputerZero C++17 / LVGL 9.5 MVVM Template architecture.

## Features

- **`.sub` File Format Support (`SubGhzFileFormats.md`)**:
  - Reads and writes `Flipper SubGhz Key File` and `Flipper SubGhz RAW File` (`.sub`) files.
  - Supports standard asynchronous CC1101 presets (`FuriHalSubGhzPresetOok270Async`, `FuriHalSubGhzPresetOok650Async`, `FuriHalSubGhzPreset2FSKDev238Async`, `FuriHalSubGhzPreset2FSKDev476Async`) and `FuriHalSubGhzPresetCustom` (`Custom_preset_data`).
  - Supports `RAW` (`RAW_Data:`), `BinRAW` (`Bit_RAW:` + `Data_RAW:`), and decoded protocols (`Princeton`, `CAME`, `Nice FLO`, `GateTX`, `Linear`, `Chamberlain`, `Holtek`, `KeeLoq`, plus generic OOK PWM fallback).
- **Sub-GHz Modes**:
  1. **Read**: Live decoding of static/dynamic remotes with fixed or hopping frequencies (`315`, `433.92`, `868.35`, `915 MHz`), instant replay (`Enter`/`Space`), and saving to `.sub` (`S`).
  2. **Read RAW**: Uncompressed microsecond pulse capture gated by a configurable RSSI squelch threshold (`T`), live 40-bar oscilloscope display, playback (`Space`), and `.sub` export (`S`).
  3. **Saved**: Hierarchical `.sub` directory browser (`~/subg/files/` on device) and Signal Inspector to view metadata and transmit saved `.sub` files.
  4. **Frequency Analyzer**: Sweeps 18 standard Sub-GHz frequencies across `300–928 MHz` with live RSSI bar, peak hold, and one-key jump to `Read` at the locked peak frequency.
  5. **Add Manually**: Create and transmit virtual remotes (`Princeton`, `CAME`, `Nice FLO`, `GateTX`, `Linear`, `Chamberlain`, `Holtek`, `KeeLoq`) with custom or randomized hex keys.
  6. **CC1101 Info**: Hardware status (`/dev/spidev0.1`, `PARTNUM`, `VERSION`, `MARCSTATE`, `RF_SW0`/`GDO2` band switch, TX power dBm, and retry initialization).

## Device Storage Layout

- **Config**: `~/subg/config/cc1101-subghz.conf`
- **`.sub` Files**: `~/subg/files/` (captures and manually created remotes are saved to `~/subg/files/Captured/`)

## Build & Test (macOS / Linux Desktop Simulator)

```bash
# macOS (Apple Silicon)
cmake --preset darwin-arm64
cmake --build --preset darwin-arm64-dbg
ctest --test-dir build/darwin-arm64 --build-config Debug --output-on-failure

# Run Desktop Simulator
./build/darwin-arm64/Debug/cc1101_subghz
```

## CardputerZero Cross Build & Debian Package

```bash
cmake --preset cp0-cross -DBUILD_TESTING=OFF
cmake --build --preset cp0-cross-rel
cpack --preset cp0-cross-deb
```

## Attributions & References

- **[CardputerZero Application Template](https://github.com/CardputerZero/Template)** (M5Stack Technology CO LTD, MIT License): MVVM / LVGL 9.5 architecture, reactive observer bindings, theme/config system, and SDL desktop simulator.
- **[Cap-CC1101-SubG-Chat](https://github.com/CardputerZero)** (M5Stack Technology CO LTD): CardputerZero Cap CC1101 SPI, `libgpiod`, `pinctrl` pinmux snapshot/restore, and `ext_5v_out` power control HAL.
- **[Flipper Zero Firmware (`flipperzero-firmware`)](https://github.com/flipperdevices/flipperzero-firmware)**: Inspiration for the Sub-GHz application workflow, `.sub` file format specification (`SubGhzFileFormats.md`), and CC1101 asynchronous modulation preset parameters.
- **[LVGL](https://github.com/lvgl/lvgl)** (`v9.5.0`), **[fmt](https://github.com/fmtlib/fmt)**, and **[libgpiod](https://git.kernel.org/pub/scm/libs/libgpiod/libgpiod.git/)**.
- **Third-Party Fonts & Icons**: Phosphor Icons (`Phosphor-Fill.ttf`) and Kenney Input Prompts (`kenney_input_keyboard_and_mouse.ttf`, `kenny_keyboard_extra.ttf`) — see [assets/fonts/LICENSE.txt](assets/fonts/LICENSE.txt).
