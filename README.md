# Universal Analog Plugin for Finalmouse Centerpiece Pro

This fork adds **Finalmouse Centerpiece Pro (CPPRO)** analog-key support to the [Wooting Analog SDK](https://github.com/WootingKb/wooting-analog-sdk). It allows games and applications that use the Wooting Analog SDK to read the CPPRO's per-key travel as values from `0.0` to `1.0`.

The implementation has been tested on Windows with a physical Centerpiece Pro (`VID 361D`, `PID 0200`). It requests telemetry directly from the keyboard's MCU, maps all 68 hardware keys, and converts the reported `0–4 mm` travel range into the SDK's normalized analog range. Finalmouse XPanel does not need to be running.

This project is a CPPRO-focused fork of [AnalogSense/universal-analog-plugin](https://github.com/AnalogSense/universal-analog-plugin).

## Installation

### Prebuilt package

Check this fork's [releases page](https://github.com/LeiterConsulting/universal-analog-plugin/releases) for a `Windows.zip` asset. If no Windows archive is attached to the latest release, use the source-build instructions below. The upstream project's releases do not contain CPPRO support.

Close games and other applications using the Wooting Analog SDK before replacing a plugin.

1. Extract the `universal-analog-plugin` directory from `Windows.zip`.
2. Copy that directory into `C:\Program Files\WootingAnalogPlugins`.
3. Approve the Windows administrator prompt if one appears.
4. Restart the game or application that uses the Wooting Analog SDK.

The resulting layout should be:

```text
C:\Program Files\WootingAnalogPlugins\
└── universal-analog-plugin\
    ├── abiv0.dll
    └── abiv1.dll
```

Use `universal-analog-plugin`, not `universal-analog-plugin-with-wooting-device-support`, unless this plugin is also intended to replace the standard Wooting device plugin.

## Building on Windows

### Requirements

- Git
- A C++20 Windows toolchain supported by [Sun](https://github.com/calamity-inc/Sun)
- [Sun](https://github.com/calamity-inc/Sun/releases) available as `sun` on `PATH`

The included ABI 0 Rust library targets the Microsoft C++ runtime. Use an MSVC/LLVM-compatible toolchain for complete ABI 0 and ABI 1 packages; MinGW can compile and test the CPPRO code but cannot link the included ABI 0 library.

Clone the tagged CPPRO source and its Soup dependency:

```powershell
git clone --recurse-submodules --branch v0.1.1-cppro https://github.com/LeiterConsulting/universal-analog-plugin.git
Set-Location universal-analog-plugin
```

Build the normal plugin:

```powershell
sun abiv0
sun abiv1

New-Item -ItemType Directory -Force universal-analog-plugin | Out-Null
Move-Item abiv0.dll universal-analog-plugin\abiv0.dll
Move-Item abiv1.dll universal-analog-plugin\abiv1.dll
```

To build the variant that also handles native Wooting devices:

```powershell
sun abiv0-pluswooting
sun abiv1-pluswooting

New-Item -ItemType Directory -Force universal-analog-plugin-with-wooting-device-support | Out-Null
Move-Item abiv0-pluswooting.dll universal-analog-plugin-with-wooting-device-support\abiv0.dll
Move-Item abiv1-pluswooting.dll universal-analog-plugin-with-wooting-device-support\abiv1.dll
```

Copy the resulting plugin directory into `C:\Program Files\WootingAnalogPlugins` as described above.

## Automated protocol test

Before building release DLLs, compile and run the deterministic CPPRO protocol test:

```powershell
.\tools\run_tests.ps1
```

The test runner locates and initializes Visual Studio automatically. It validates the command frame, all 68 hardware-key mappings, supported event types, malformed-report rejection, release values, distance clamping, monotonic `0–4 mm` normalization, ABI 0/1 translation-unit compilation, and the HID probe build. The same test runs on every push and pull request through GitHub Actions.

To include live assertions against a connected CPPRO:

```powershell
.\tools\run_tests.ps1 -Hardware
```

## Verifying the CPPRO connection

The repository includes a read-only Windows HID probe. It lists the CPPRO HID collections, sends the same key-report request used by the plugin, and prints changing input reports.

With MinGW-w64:

```powershell
g++ -std=c++20 -O2 -Wall -Wextra -pedantic tools/cppro_hid_probe.cpp -o cppro_hid_probe.exe -lhid -lcfgmgr32
.\cppro_hid_probe.exe 10
```

Press several keys at different depths during the ten-second capture. A working CPPRO connection should show:

- product name `Centerpiece-Pro-MCU`;
- vendor collection usage `0xFF00:0x01`;
- 64-byte input and output reports;
- command acknowledgement beginning with `04 02 F1 1D`; and
- changing reports beginning with `04 08 03` while key telemetry is active.

The probe does not change actuation settings, lighting, firmware, or onboard profiles.

## Device support

### Validated by this fork

- Finalmouse Centerpiece Pro

### Inherited from the upstream plugin

- Razer Huntsman V2 Analog<sup>R</sup>
- Razer Huntsman Mini Analog<sup>R</sup>
- Razer Huntsman V3 Pro, Mini, and Tenkeyless<sup>R</sup>
- NuPhy analog keyboards
- DrunkDeer analog keyboards
- Keychron Q1 HE, Q3 HE, Q5 HE, and K2 HE<sup>P, F</sup>
- Lemokey P1 HE<sup>P, F</sup>
- Madlions MAD60HE, MAD68HE, and MAD68R<sup>P</sup>

These inherited devices were not revalidated as part of the CPPRO work.

<sup>R</sup> Razer Synapse must be installed and running for analog input from these keyboards.

<sup>P</sup> The official firmware exposes a polling interface, which may introduce lag or missed inputs.

<sup>F</sup> [AnalogSense custom firmware](https://analogsense.org/firmware/) provides full analog-report functionality for supported models.

## Implementation

The plugin-facing SDK bridge is in [`main.cpp`](main.cpp). CPPRO device detection, telemetry parsing, travel normalization, and the 68-key mapping are implemented in [`soup::AnalogueKeyboard`](https://github.com/LeiterConsulting/Soup/blob/agent/cppro-support/soup/AnalogueKeyboard.cpp).

CPPRO support currently targets the MCU HID interface at `361D:0200`, usage page `FF00`, usage `01`. The GPU interface is not used for analog input.

## Issues and upstream

Report CPPRO problems in this fork's [issue tracker](https://github.com/LeiterConsulting/universal-analog-plugin/issues). Include the Windows version, keyboard firmware version, application using the Wooting Analog SDK, and probe output when possible.

Upstream projects:

- [AnalogSense/universal-analog-plugin](https://github.com/AnalogSense/universal-analog-plugin)
- [calamity-inc/Soup](https://github.com/calamity-inc/Soup)
