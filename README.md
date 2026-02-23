# C1-ChannelStrip

Free classic channel strip plugin for VCV Rack.</br></br>
![C1-ChannelStrip](/docs/img/C1-ChannelStrip.jpeg)

---

## Overview

**Twisted Cable - C1-ChannelStrip** </br>
is a classic channel strip for VCV Rack, providing signal processing across 5 modules. </br>

The plugin follows standard mixing console signal flow: </br>
input leveling, noise gate, equalization, compression, and channel output monitoring. </br></br>

The limited amount of UI controls are based on the hardware controller Softube Console 1 MK2, as it is designed to work in tandem with it.</br> 
Therefor certain common/expected controls that are not present on the hardware, can be found inside the right-click context menu.</br> 
When combined with the "C1-Control" plugin (separate purchase), optional hardware integration with Softube Console 1 MK2 becomes fully enabled.</br>

The plugin spans 47 HP (not including C1 and the CV expanders) and processes stereo audio through a signal chain. </br>
Each module provides DSP processing with visual feedback through meters, spectrum analyzers, and LED indicators.</br>

## Signal Flow

```
  C1-ChannelStrip (47 HP, 5 modules)

  ┌─────────┐    ┌─────────┐    ┌──────────────┐    ┌─────────┐    ┌──────────┐
  │ CHAN-IN │───▶│  SHAPE  │───▶│    C1-EQ     │───▶│ C1-COMP │───▶│ CHAN-OUT │
  └─────────┘    └─────────┘    └──────────────┘    └─────────┘    └──────────┘
      8HP            8HP              15HP              8HP             8HP
     Input          Gate            4-Band EQ        Compressor        Output

  Standard Signal Path: CHAN-IN → SHAPE → C1-EQ → C1-COMP → CHAN-OUT

```

## Module Overview

### CHAN-IN - 8HP 
Input module providing: gain staging, filtering, phase control, and metering</br></br>
<img src="/docs/img/CHAN-IN.jpeg" style="float:right; margin:0 0 1em 1em;" width="10%" alt="CHAN-IN"> </br></br>
[USER MANUAL](/docs/manuals/CHAN-IN_manual.md)</br>

**Signal Processing:** </br>
- VCA with anti-pop slew limiting (25Hz)
- Level control: -60dB to +6dB
- High-pass filter: 20Hz to 500Hz (12dB/oct)
- Low-pass filter: 1kHz to 20kHz (12dB/oct)
- Phase invert control
- True stereo processing with mono fallback

**Metering:** </br>
- 3 switchable modes: RMS, VU (300ms), PPM
- 17-LED vertical bar graph per channel

### SHAPE - 8HP
Noise gate with envelope following, sidechain input, and waveform visualization.</br></br>
<img src="/docs/img/SHAPE.jpeg" style="float:right; margin:0 0 1em 1em;" width="10%" alt="SHAPE"> </br></br>
[USER MANUAL](/docs/manuals/SHAPE_manual.md)</br>

**Gate:** </br>
- Threshold: -60dB to 0dB
- PUNCH control: 0% to 100% (attack time: 0%=5ms slow, 100%=0.1ms fast/punchy)
- Sustain: 0ms to 300ms (hold time)
- Release: 0.1s to 4s
- Hard/Soft gate modes
- Bypass control

**Sidechain:** </br>
- External sidechain input for gating control (both audio/CV)

**Visualization:** </br>
- Waveform display with envelope tracking
- Horizontal 11 LED gate status indicator

### C1-EQ - 15HP
4-band equalizer with four analog character modes and spectrum analyzer.</br></br>
<img src="/docs/img/C1-EQ.jpeg" style="float:right; margin:0 0 1em 1em;" width="30%" alt="C1-EQ"> </br></br>
[USER MANUAL](/docs/manuals/C1-EQ_manual.md)</br>

**Equalizer:** </br>
- 4 bands: LF (20-400Hz), LMF (200-2kHz), HMF (1k-8kHz), HF (4k-20kHz)
- Gain: ±20dB per band
- Q control with proportional Q algorithm
- Band-specific mode switches: Cut/Bell/Shelf for LF and HF bands

**Analog Character Modes:** </br>
- Transparent: Clean digital processing
- Light: Subtle analog warmth
- Medium: Moderate analog coloration
- Full: Maximum analog character

**Spectrum Analyzer:** </br>
- Real-time FFT display (2048-sample window)
- 128-band resolution (20Hz to 22kHz)
- Unified peak hold across all bands
- Worker thread prevents audio thread blocking

**Processing:** </br>
- SIMD-optimized using float_4 vector operations
- Oversampling: On/Off toggle (typically 4x when enabled, varies by sample rate)
- Anti-aliasing filtering

### C1-COMP - 8HP
Stereo compressor with four compressor types, and sidechain processing (both audio/CV).</br></br>
<img src="/docs/img/C1-COMP.jpeg" style="float:right; margin:0 0 1em 1em;" width="10%" alt="C1-COMP"> </br></br> 
[USER MANUAL](/docs/manuals/C1-COMP_manual.md)</br>

**Compressor Types:** </br>
- VCA (SSL G-Series)
- FET (1176-style)
- Optical (LA-2A-style)
- Vari-Mu (Fairchild-style)

**Controls:** </br>
- Threshold: -20dB to +10dB
- Ratio: 1:1 to 20:1 (quadratic taper)
- Attack: 6 discrete positions (0.1ms, 0.3ms, 1ms, 3ms, 10ms, 30ms)
- Release: 100ms to 1200ms, plus AUTO mode
- Dry/Wet: 0% to 100% (parallel compression)
- Bypass control

**Metering:** </br>
- Input/Output/Gain Reduction peak meters
- Horizontal 11 LED Gain Reduction indicator

**Extra Features:** </br>
- Reference level switching: 5V or 10V
- Auto makeup gain option
- Auto-knee override (0-12dB)
- Input/Output gain control


### CHAN-OUT - 8HP
Output module with dual operating modes, four saturation engines, enhanced metering.</br></br>
<img src="/docs/img/CHAN-OUT.jpeg" style="float:right; margin:0 0 1em 1em;" width="10%" alt="CHAN-OUT"> </br></br> 
[USER MANUAL](/docs/manuals/CHAN-OUT_manual.md)</br>

**Operating Modes:** </br>
- Master mode: -60dB to 0dB, linear pan
- Channel mode: -60dB to +6dB, equal power pan

**Character Engines:** </br>
- Standard: Clean transparent output
- 2520: Discrete op-amp coloration
- 8816: Console summing character
- DM2+: Saturated "analog warmth"

**Each engine provides selectable oversampling:** </br>
- OFF, 2x, 4x, or 8x oversampling
- Anti-aliasing filtering
- Analog behavioral modeling

**Output Controls:** </br>
- Volume control with operating mode range
- Pan control with mode-appropriate law
- Dim: -30dB to -1dB attenuation
- Mute with anti-pop slew limiting

**Metering:** </br>
- 17-segment VU meter (post-pan, post-volume)
- LUFS loudness meter (EBU R128-compliant)

**Goniometer:** </br>
- L/R correlation visualization
- Displays phase relationships and stereo width

**Processing:** </br>
- Custom polyphase SIMD oversampling (SSE2-optimized) for character engines
- Oversampling: Selectable OFF, 2×, 4×, or 8× per engine (2520, 8816, DM2+)
- Standard engine: Transparent processing without oversampling
- Anti-aliasing filtering via polyphase filter decomposition


## Hardware Integration

### Console 1 MK2 Support

C1-Control plugin (separate purchase) provides optional integration with Softube Console 1 MK2 hardware, </br>
enabling hardware control over VCV Rack parameters with bidirectional feedback.</br>

**Supported Modules:** </br>
- CHAN-IN: 4 parameters
- SHAPE: 6 parameters
- C1-EQ: 13 parameters
- C1-COMP: 6 parameters
- CHAN-OUT: 6 parameters
- C1-ORDER: 2 parameters

**Features:** </br>
- Automatic module detection in chain positions
- Bidirectional MIDI parameter sync
- Real-time LED ring feedback
- Hardware encoder control over software parameters
- VU meter display on hardware
- Zero-configuration operation

**Connection:** </br>
1. Connect Console 1 MK2 via USB
2. Add C1 module to VCV Rack patch
3. Connect supported modules to right of C1
4. Hardware automatically maps to detected modules

### C1 Expert Mode

Expert Mode enables chain validation for Console 1 MK2 hardware control.</br>

**Chain Validation**:</br>
- Detects unsupported modules in the expander chain (CV expanders, third-party modules)
- LED position blinks red continuously when unsupported module detected
- MIDI control halts until chain contains only supported modules
- Prevents LED overflow: positions 1-5 reserved for audio modules only
- ORDER LED (position 6) only used for ORDER module error indication

**Supported Modules** (5 audio processing modules):</br>
- CHAN-IN (position 0)
- SHAPE (position 1)
- C1-EQ (position 2)
- C1-COMP (position 3)
- CHAN-OUT (position 4)
- C1-ORDER (position 5, triggers red blink in Expert Mode)

**Design Rationale**:</br>
CV expanders are designed for standalone VCV Rack use without Console 1 MK2 hardware. </br>
Expert Mode enforces this separation by rejecting CV expanders in the hardware-controlled chain.</br>

**Console 1 MK2** is a registered trademark of Softube AB.</br>
This plugin is an independent implementation and is not affiliated with, endorsed by, or sponsored by Softube AB.</br>


## Building from Source

### Requirements

- **VCV Rack**: Either VCV Rack SDK or full Rack source tree
- **C++ Compiler**: GCC, Clang, or MSVC with C++11 support (or later)
- **C Compiler**: For compiling libebur128 dependency (included in repository)

### Build Instructions

**1. Clone the repository:** </br>

```bash
git clone https://github.com/Eurikon/C1-ChannelStrip.git
cd C1-ChannelStrip
```

**2. Build the plugin:** </br>

```bash
make
```

The build system will automatically:</br>
- Compile libebur128 from source (included in plugin build)
- Build the C1-ChannelStrip plugin
- Link all components together

**3. Install (for development):** </br>

```bash
make install
```

This installs the plugin to your VCV Rack user plugins directory. </br>

### Platform-Specific Notes

**macOS:** </br>
- Requires Xcode Command Line Tools
- Install via: `xcode-select --install`

**Linux:** </br>
- Install build tools: `sudo apt-get install build-essential`
- Ensure VCV Rack source or SDK is available

**Windows:** </br>
- Requires Visual Studio 2019 or later with C++ support
- Use MSYS2 or Visual Studio command prompt

### Troubleshooting Build Issues

**"Cannot find Rack headers":** </br>
- Ensure plugin is in `Rack/plugins/C1-ChannelStrip/` directory
- Or set `RACK_DIR` environment variable to point to Rack installation


## Usage

### Basic Channel Strip Setup

1. Add modules in signal flow order: CHAN-IN → SHAPE → C1-EQ → C1-COMP → CHAN-OUT
2. Connect audio input to CHAN-IN left/right inputs
3. Patch between modules following signal chain
4. Connect CHAN-OUT to a channel or master bus

### With Console 1 MK2 Hardware

1. Connect Console 1 MK2 hardware via USB
2. Add C1 module as first module in chain (requires C1-Control plugin, separate purchase)
3. Add processing modules to right of C1 in order
4. Hardware automatically detects and maps to modules
5. Use Console 1 MK2 encoders to control parameters
6. LED rings on hardware reflect VCV Rack parameter positions

### Alternative Routing (only available with the additional commercial plugin)

1. Add C1-ORDER module after C1-COMP
2. Use Order mode control to change signal chain arrangement
3. Use SC mode control to route external sidechain to COMP or SHAPE
4. Yellow LEDs indicate active routing paths

## Technical Specifications

- **Voltage Range**: ±5V (VCV Rack standard)
- **Reference Level**: 5V = 0dB
- **Stereo Processing**: True stereo with mono fallback
- **Thread Safety**: Separate audio and UI threads
- **Sample Rate**: Supports all VCV Rack sample rates
- **Total HP Width**: 59 HP (all 7 modules when used with C1-Control)
- **Total Source Code**: ~16,000 lines of DSP code (including C1-Control source)


## Hardware References Disclaimer

  **C1-ChannelStrip** </br>
  references hardware brands and models (SSL, API, Neve, UREI, Teletronix,
  Fairchild, Dangerous Music) solely to describe behavioral characteristics and parameter ranges
  derived from published specifications and academic literature. </br>

  All implementations are mathematical approximations based on publicly available documentation/product specifications,
  **not** circuit-accurate emulations. </br>
  No proprietary circuit designs, reverse-engineered code, trade secrets, or firmware have been
  used. </br>

  All algorithms are derived from open-source GPL-3.0 code, academic research, and scientifically
  verifiable mathematical models. </br>

  The plugin's design philosophy provides both transparent processing and intentional sonic
  coloration options, </br>
  allowing users to shape their sound with character-inspired processing or maintain clean
  accuracy as needed.</br>


## License

This project builds upon GPL-3.0-licensed work from:
- **Befaco**: Befaco
- **MindMeld Modular**: (Marc Boulé & Steve Baker)
- **AudibleInstruments**: (Andrew Belt)

This project also uses:
- **libebur128**: MIT License (Jan Kokemüller) - EBU R128 loudness metering

See [ATTRIBUTION.md](ATTRIBUTION.md) for complete source code credits.</br>

All contributions must be compatible with GPL-3.0 license terms.


## Documentation

Technical documentation for each module is available in `docs/`: </br>
- [CHAN-IN technical specification](/docs/manuals/CHAN-IN_RC1.md)
- [SHAPE technical specification](/docs/manuals/SHAPE_RC1.md)
- [C1-EQ technical specification](/docs/manuals/C1-EQ_RC1.md)
- [C1-COMP technical specification](/docs/manuals/C1-COMP_RC1.md)
- [CHAN-OUT technical specification](/docs/manuals/CHAN-OUT_RC1.md)

## Contributing

This project follows GPL-3.0 licensing. All contributions must be compatible with GPL-3.0 terms. </br>

---

**Date**: 03-11-2025</br>
**Author**: Latif Karoumi</br>

**Console 1 MK2** is a registered trademark of Softube AB. </br>
**Copyright © 2025 Twisted Cable. Licensed under GPL-3.0-or-later.** </br>
