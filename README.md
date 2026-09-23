# Luf Meter

A custom Windows VST3 loudness meter built with **C++**, **JUCE**, and **CMake**. Luf Meter is designed for practical use on an Ableton Live master bus or audio track, with real-time loudness, peak, and dynamics readouts.

> **Project status:** active prototype / learning project. The plugin is already usable for studio metering, but it is still being validated against reference material before being treated as a fully standards-compliant delivery meter.

## Features

- Stereo VST3 audio-effect plugin for Windows
- Transparent audio passthrough: the plugin does not intentionally alter audio
- K-weighted loudness measurement
- Momentary loudness display using a 400 ms window
- Short-term loudness display using a 3 second window
- Integrated loudness measurement with EBU/BS.1770-style gating
- Loudness Range (LRA) display
- Left and right sample-peak meters
- Held sample peaks with reset control
- Clip detection and clip-warning state
- 4x FIR-based true-peak measurement path with dBTP display
- Integrated-loudness reset control
- Fixed histogram-based accumulation for bounded memory usage during extended sessions
- Custom JUCE UI with green/yellow/red peak zones and peak-hold lines

## Current Metering Scope

Luf Meter currently focuses on a stereo master-bus workflow:

```text
Audio input
  -> K-weighting
  -> Momentary / Short-Term energy windows
  -> Integrated loudness histogram and gating
  -> LRA histogram and percentile estimate
  -> UI meter display
```

### Loudness windows

| Metric | Window / behavior |
|---|---|
| Momentary | 400 ms K-weighted moving window |
| Short-Term | 3 second K-weighted moving window |
| Integrated | 400 ms blocks, 75% overlap, absolute and relative gating |
| LRA | Distribution of gated Short-Term loudness values |
| Sample Peak | Maximum individual sample amplitude per left/right channel |
| True Peak | 4x FIR reconstruction-based peak estimate, displayed in dBTP |

Integrated loudness uses the following gating model:

- Absolute gate: -70 LUFS
- Relative gate: 10 LU below the absolute-gated loudness
- Analysis blocks: 400 ms with 100 ms update spacing

LRA currently uses Short-Term loudness history with:

- Absolute gate: -70 LUFS
- Relative gate: 20 LU below absolute-gated loudness
- Loudness-range estimate: 95th percentile minus 10th percentile

## Validation Performed

The project has been tested in Ableton Live using a clean Operator sine source, Utility for gain trim, and Luf Meter placed after Utility.

### Steady dual-mono sine validation

At equal left/right sample peaks of -23.0 dBFS, the plugin produced approximately:

| Frequency | Measured loudness |
|---:|---:|
| 100 Hz | -24.9 LUFS |
| 1 kHz | -23.3 LUFS |
| 2 kHz | -21.0 LUFS |
| 4 kHz | -19.8 LUFS |
| 10 kHz | -19.4 LUFS |

This shows the intended K-weighting behavior: lower readings at low frequencies and a high-frequency shelf approaching approximately +4 LU relative to the 1 kHz region.

### Gain tracking validation

For a steady 1 kHz dual-mono sine source:

| Per-channel sample peak | Meter result |
|---:|---:|
| -29 dBFS | approximately -29.3 LUFS |
| -23 dBFS | approximately -23.3 LUFS |
| -17 dBFS | approximately -17.3 LUFS |

A ±6 dB signal change produced an approximately ±6 LU readout change. Momentary, Short-Term, and Integrated readings agreed for the steady sine, and LRA remained at 0.0 LU.

## Requirements

### Development

- Windows 10 or Windows 11
- Visual Studio 2026 / Visual Studio Build Tools with Desktop development with C++
- MSVC x64 compiler toolchain
- CMake 3.22 or newer
- Git
- Visual Studio Code recommended
- JUCE included as a Git submodule in `JUCE/`

### Runtime / testing

- A VST3-compatible Windows DAW, such as Ableton Live
- Standard Windows VST3 location:

```text
C:\Program Files\Common Files\VST3\
```

## Project Layout

```text
LufMeter/
├── CMakeLists.txt
├── README.md
├── JUCE/                         # JUCE Git submodule
└── Source/
    ├── PluginProcessor.h          # Audio processor and metering state
    ├── PluginProcessor.cpp        # Real-time metering / loudness engine
    ├── PluginEditor.h             # Plugin UI declaration
    ├── PluginEditor.cpp           # Plugin UI and meter drawing
    ├── KWeightingFilter.h         # K-weighting biquad filter implementation
    ├── KWeightingFilter.cpp
    └── TruePeakMeter.h            # 4x FIR reconstruction peak meter
```

## Build Instructions

### 1. Clone with JUCE

If cloning the repository for the first time:

```powershell
git clone --recurse-submodules <repository-url>
cd LufMeter
```

If the project was cloned without submodules:

```powershell
git submodule update --init --recursive
```

### 2. Open an x64 Visual Studio developer shell

Use one of the following Windows Start Menu entries:

```text
x64 Native Tools Command Prompt for VS 2026
```

or:

```text
Developer PowerShell for VS 2026
```

Confirm the compiler targets x64:

```powershell
cl
```

The output should include:

```text
for x64
```

### 3. Configure

From the repository root:

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
```

### 4. Build a Release VST3

```powershell
cmake --build build --config Release
```

The generated plugin bundle should appear at approximately:

```text
build\LufMeter_artefacts\Release\VST3\Luf Meter.vst3
```

## Install in Ableton Live

1. Close Ableton Live completely.
2. Copy the entire `Luf Meter.vst3` folder, not individual files inside it.
3. Copy it to:

```text
C:\Program Files\Common Files\VST3\Luf Meter.vst3
```

PowerShell command:

```powershell
Copy-Item `
  ".\build\LufMeter_artefacts\Release\VST3\Luf Meter.vst3" `
  "C:\Program Files\Common Files\VST3\Luf Meter.vst3" `
  -Recurse -Force
```

Administrator permission may be required for the system VST3 folder.

4. Reopen Ableton Live.
5. In **Preferences > Plug-Ins**, ensure that VST3 system folders are enabled.
6. Rescan plug-ins if Luf Meter does not immediately appear.
7. Insert Luf Meter on a track, group, or the Master device chain.

## Recommended Master-Bus Placement

For final loudness and peak measurement, place Luf Meter after all processing that changes final level:

```text
EQ
-> Compression
-> Saturation / clipping
-> Limiter
-> Luf Meter
-> Ableton Master fader
-> Audio interface
```

Ableton processes Master-track devices before its Master fader. Therefore, Luf Meter measures the signal at its insertion point and will not reflect movements of Ableton's Master fader if the plugin is inserted in the Master device chain.

Keep the Ableton Master fader at 0 dB for a predictable mastering workflow and make gain changes earlier in the chain.

## User Interface Notes

- **Integrated Reset** clears the Integrated LUFS and LRA measurement period.
- **Peak Reset** clears peak holds, the true-peak hold, and the clip indicator.
- The white line in each peak meter is the held peak.
- Green/yellow/red zones provide a quick sample-peak visual reference.
- The true-peak display is held until Peak Reset.

## Important Limitations

This is an evolving custom project. Treat the current build as a capable studio tool, but validate critical delivery decisions with a trusted commercial/reference meter until full compliance testing is complete.

Current limitations and planned validation work:

- The true-peak meter uses a practical 4x FIR reconstruction design and should be compared against official true-peak reference signals before being described as formally compliant.
- The plugin currently targets mono/stereo workflows. Full surround-channel weighting and LFE exclusion are not implemented.
- Loudness histograms use 0.1 LU bins. This provides bounded memory use and efficient long-running measurements, but can introduce small quantization differences from a per-block exact-history implementation.
- The meter should be checked at 44.1 kHz, 48 kHz, 88.2 kHz, and 96 kHz against reference files.
- Full validation against the official EBU loudness test material remains a future milestone.

## License

License selection is still pending. Do not assume this project is open source until a license file is added.

## Acknowledgments

- [JUCE](https://juce.com/) for the cross-platform audio/plugin framework.
- Steinberg for the VST3 SDK and VST3 format.
- EBU and ITU-R for loudness-metering specifications and test methodology.
