# C1-ChannelStrip - Source Code Attribution & Provenance

This document provides complete attribution and provenance documentation for all code, algorithms, and design elements in the C1-ChannelStrip VCV Rack plugin. </br>
The project is licensed under GPL-3.0-or-later and builds upon excellent GPL-3.0 licensed work from the VCV Rack community.

**Plugin Version**: 2.1.0
**Date**: 3-11-2025
**Copyright**: © 2025 Twisted Cable
**License**: GPL-3.0-or-later

---

## Module-by-Module Attribution Summary

### Control1 (C1) - Console1 Hardware Controller (2HP)

**Implementation**: ~85% original, ~15% framework patterns
**Status**: Production-ready (100% complete)

**Code Provenance**:
- **Original C1-ChannelStrip** (~85%): Zero shared state architecture, position-based module detection, auto-connection algorithm, visual LED feedback system
- **VCV Rack MIDI API** (~10%): Standard InputQueue/Output patterns
- **MIDI Protocol** (~5%): MIDI 1.0 specification CC messages

**License**: Pure GPL-3.0-or-later with no open-source dependencies beyond VCV Rack framework

### CHAN-IN (CHAN-IN) - Input Module 
(8HP)

**Implementation**: ~30% open-source references, ~70% original
**Status**: Production-ready (100% complete)

**Code Provenance**:
- **Befaco Stereo Strip** (~30%): AeFilter biquad implementation, dB conversion formula, coefficient caching, mono/stereo handling
  - License: GPL-3.0-or-later
  - URL: https://github.com/VCVRack/Befaco
- **MindMeld MixMaster** (~10%): Anti-pop slewing timing (25V/s specification)
  - License: GPL-3.0-or-later
  - URL: https://github.com/MarcBoule/MindMeldModular
- **Original C1-ChannelStrip** (~60%): VU/RMS/PPM metering system, EURIKON visual design, Console1 integration, LED ring overlays

**License**: GPL-3.0-or-later (fully compatible with all dependencies)

### SHAPE (SHAPE) - Noise Gate 
(8HP)

**Implementation**: ~90% original, ~10% theory and framework
**Status**: Production-ready (100% complete)

**Code Provenance**:
- **Audio Engineering Theory** (~5%): Envelope follower design, exponential time constants, soft-knee gating theory
- **VCV Rack Framework** (~5%): Sample rate handling, threading patterns
- **Original C1-ChannelStrip** (~90%): Custom ShapeGateDSP algorithm, VCV Rack voltage calibration, processing intensity metering (3-factor algorithm), waveform visualization system (4 time windows), punch enhancement

**License**: Pure GPL-3.0-or-later with no open-source dependencies beyond VCV Rack framework

### C1-EQ (EQU) - 4-Band Parametric Equalizer (15HP)

**Implementation**: ~65.3% open-source references, ~34.7% original
**Status**: Production-ready (97% complete)

**Code Provenance**:
- **Mutable Instruments Shelves (AudibleInstruments)** (~35.3%): Anti-aliasing SOSFilter implementation, analog character modeling constants, VCA compression modeling, multi-stage circuit processing
  - License: GPL-3.0-or-later
  - URL: https://github.com/VCVRack/AudibleInstruments
  - Original Hardware: Mutable Instruments Shelves Eurorack module
- **FourBand Example Template** (~25.5%): Core EQ architecture, parameter smoothing, RBJ biquad filters, proportional Q algorithm, coefficient caching
  - License: GPL-3.0 compatible example code
- **Original C1-ChannelStrip** (~30.0%): VCV Rack integration, Console1 MIDI control, GUI/widgets, factory presets
- **Befaco Bandit** (~9.1%): SIMD-optimized filter array architecture pattern
  - License: GPL-3.0-or-later
  - URL: https://github.com/VCVRack/Befaco
- **MindMeld EqMaster** (~1.2%): Parameter smoothing timing strategy (not implementation)
  - License: GPL-3.0-or-later
  - URL: https://github.com/MarcBoule/MindMeldModular

**RBJ Biquad Filter Licensing Chain**:
1. Robert Bristow-Johnson (1994-2005): Mathematical formulas (not copyrightable)
2. Tom St Denis (2002): PUBLIC DOMAIN C implementation (biquad.c)
3. Nigel Redmon/EarLevel Engineering: Permissive C++ license (commercial use allowed)
4. FourBand Example → C1-EQ: GPL-3.0-or-later

**License**: GPL-3.0-or-later (fully compatible with entire chain)

### C1COMP (COMP) - Multi-Engine Compressor 
(8HP)

**Implementation**: ~30% LSP foundation, ~70% academic theory and hardware-inspired
**Status**: Production-ready (100% complete)

**Code Provenance**:
- **LSP Compressor** (~30%): Class structure, parameter interfaces, processStereo signature, dB conversion utilities, envelope follower structure
  - License: GPL-3.0-or-later
  - URL: https://github.com/sadko4u/lsp-plugins
  - **CRITICAL FIX**: Added dynamic sample rate handling (LSP hardcoded 44.1kHz)
- **Audio Engineering Theory** (~50%): Gain computer formulas (Giannoulis et al. 2012), exponential envelope followers (Zölzer 2011), RMS detection, soft-knee curves (McNally 1984)
- **Hardware Specifications** (~20%): SSL G-series characteristics, UREI 1176 specifications, Teletronix LA-2A behavior, Fairchild 670 specifications

**Important Disclaimer**: C1COMP implements **approximations inspired by** published hardware specifications and academic theory, **not** circuit-accurate hardware emulations.

**License**: GPL-3.0-or-later (fully compatible with LSP Compressor)

### CHAN-OUT (CHAN-OUT) - Output Module
(8HP)

**Implementation**: ~40% MindMeld foundation (clean engine), ~45% original, ~15% academic theory
**Status**: Production-ready (100% complete)

**Code Provenance**:
- **MindMeld MasterChannel** (~40% of clean engine): Soft-clip polynomial harmonic generation, DC blocker architecture, anti-pop slewing (125 units/sec), VU epsilon threshold
  - License: GPL-3.0-or-later
  - URL: https://github.com/MindMeldModular/PatchMaster
- **Academic Theory** (~15%): Saturation algorithms from academic literature (custom oversampled saturation engines)
- **Original C1-ChannelStrip** (~45%): LUFS metering (libebur128), goniometer display, dual-mode operation (Master/Channel), character engine system, Console1 integration

**Important Disclaimer**: Saturation engines are **approximations** based on academic theory and published specifications, **not** circuit-accurate hardware emulations.

**License**: GPL-3.0-or-later (fully compatible with MindMeld)

---

## Primary Open-Source Dependencies

### 1. VCV Rack Framework
- **License**: GPL-3.0-or-later
- **URL**: https://vcvrack.com/
- **Copyright**: © Andrew Belt
- **Usage**: Core framework, DSP library, MIDI API, component library
- **Compatibility**: ✅ GPL-3.0 compatible

### 2. Mutable Instruments Shelves (via AudibleInstruments)
- **License**: GPL-3.0-or-later
- **URL**: https://github.com/VCVRack/AudibleInstruments
- **Original Design**: © Émilie Gillet (Mutable Instruments)
- **VCV Port**: © Andrew Belt
- **Used In**: C1-EQ (35.3% of code)
- **Components**: Anti-aliasing SOSFilter, analog character modeling, VCA compression
- **Compatibility**: ✅ GPL-3.0 compatible

### 3. Befaco Plugin
- **License**: GPL-3.0-or-later
- **URL**: https://github.com/VCVRack/Befaco
- **Copyright**: © 2019-2021 VCV and contributors
- **Used In**: CHAN-IN (30% of code), C1-EQ (9.1% of code)
- **Components**:
  - AeFilter biquad (originally from repelzen with permission)
  - dB conversion formulas
  - SIMD filter array architecture (Bandit pattern)
- **Compatibility**: ✅ GPL-3.0 compatible

### 4. MindMeld Modular
- **License**: GPL-3.0-or-later
- **URL**: https://github.com/MarcBoule/MindMeldModular
- **Copyright**: © 2019-2023 Marc Boulé and Steve Baker
- **Used In**: CHAN-IN (10% of code), C1-EQ (1.2% of code), CHAN-OUT (40% of clean engine)
- **Components**:
  - Anti-pop slewing specifications (25V/s VCA, 125 units/sec mute)
  - VU metering epsilon threshold (0.0001f)
  - Parameter smoothing timing strategy
  - MasterChannel soft-clip polynomial (harmonic generation algorithm)
  - DC blocker architecture (10Hz HPF)
  - SIMD framework structure
  - MixMaster channel filter bypass
- **Compatibility**: ✅ GPL-3.0 compatible

### 5. LSP Compressor (Linux Studio Plugins)
- **License**: GPL-3.0-or-later
- **URL**: https://github.com/sadko4u/lsp-plugins
- **Copyright**: © LSP Project
- **Used In**: C1COMP (30% of code structure)
- **Components**: Class structure, parameter interfaces, envelope follower framework
- **Modifications**: Fixed hardcoded 44.1kHz sample rate, changed RMS to peak detection (VCA)
- **Compatibility**: ✅ GPL-3.0 compatible

### 6. libebur128
- **License**: MIT License
- **URL**: https://github.com/jiixyj/libebur128
- **Copyright**: © 2011 Jan Kokemüller
- **Used In**: CHAN-IN, CHAN-OUT (LUFS metering implementation)
- **Components**: EBU R128 compliant loudness measurement library
- **Usage**: Momentary loudness measurement (400ms sliding window) for professional broadcast-standard LUFS metering
- **Compatibility**: ✅ MIT License is compatible with GPL-3.0-or-later

**MIT License Text**:
```
Copyright (c) 2011 Jan Kokemüller

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

---

## Academic Literature & Technical Standards

### Core DSP Algorithm References

All mathematical formulas and algorithms from academic literature are not copyrightable and are freely implementable.

1. **Bristow-Johnson, Robert (1994-2005)**. "Cookbook formulae for audio EQ biquad filter coefficients"
   - **URL**: https://www.w3.org/2011/audio/audio-eq-cookbook.html
   - **Used In**: C1-EQ, CHAN-IN
   - **License Status**: Mathematical formulas not copyrightable

2. **Tom St Denis (2002)**. biquad.c - Public Domain C Implementation
   - **URL**: https://www.musicdsp.org/en/latest/_downloads/2a80aec3df7303b2245e13650e70457b/biquad.c
   - **License**: **PUBLIC DOMAIN** (explicitly declared)
   - **Used In**: C1-EQ (via FourBand Example)

3. **Nigel Redmon / EarLevel Engineering**. Biquad C++ Implementation
   - **URL**: https://www.earlevel.com/main/2012/11/26/biquad-c-source-code/
   - **License**: Permissive (commercial use allowed)
   - **Used In**: C1-EQ (reference)

4. **Giannoulis, D., Massberg, M., & Reiss, J. D. (2012)**. "Digital Dynamic Range Compressor Design—A Tutorial and Analysis." *Journal of the Audio Engineering Society*, 60(6), 399-408.
   - **Used In**: C1COMP (gain computer theory)

5. **McNally, G. W. (1984)**. "Dynamic Range Control of Digital Audio Signals." *Journal of the Audio Engineering Society*, 32(5), 316-327.
   - **Used In**: C1COMP (soft-knee compression), Shape (soft-knee gating)

6. **Zölzer, U. (2011)**. *DAFX: Digital Audio Effects* (2nd ed.). Wiley. ISBN: 978-0-470-66599-2
   - **Used In**: C1COMP (envelope followers), CHAN-OUT (DC blocker theory)

7. **Pakarinen, J., & Yeh, D. T. (2009)**. "A Review of Digital Techniques for Modeling Vacuum-Tube Guitar Amplifiers." *Computer Music Journal*, 33(2), 85-100.
   - **Used In**: C1COMP (Vari-Mu tube saturation), CHAN-OUT (transformer saturation, planned)

8. **Smith, Julius O.** "Introduction to Digital Filters with Audio Applications." CCRMA, Stanford University.
   - **URL**: https://ccrma.stanford.edu/~jos/filters/
   - **Used In**: All modules (exponential smoothing, RC filter theory)

9. **Yeh, D. T., Abel, J. S., & Smith, J. O. (2007)**. "Automated Physical Modeling of Nonlinear Audio Circuits." *IEEE TASLP*, 18(4), 728-737.
   - **Used In**: CHAN-OUT (polynomial saturation theory, planned)

### Audio Standards

1. **MIDI 1.0 Specification** (MIDI Manufacturers Association)
   - **URL**: https://www.midi.org/specifications
   - **Used In**: Control1 (MIDI CC communication)

2. **IEC 60268-18**: "Peak programme level meters - Digital audio peak level meter"
   - **Used In**: CHAN-IN (studio-optimized fast PPM-style meter, not IEC-compliant)

3. **VCV Rack Voltage Standards**
   - **URL**: https://vcvrack.com/manual/VoltageStandards
   - **Used In**: All modules (±5V audio standard)

### Hardware Specifications (Informational References)

**Important**: Hardware specifications are used **only** to inform parameter ranges and design goals, **not** for circuit emulation.

1. **SSL G-Series Console** (Solid State Logic, 1980s)
   - **Used In**: C1COMP (VCA compressor behavior inspiration)
   - **Source**: Product manuals, professional audio literature
   - **Usage**: Published attack/release ranges, peak detection characteristics

2. **UREI 1176 Peak Limiter** (1967)
   - **Used In**: C1COMP (FET compressor behavior inspiration)
   - **Source**: Service manual, technical specifications
   - **Usage**: Ultra-fast attack specifications (20µs-800µs)

3. **Teletronix LA-2A Leveling Amplifier** (1965)
   - **Used In**: C1COMP (Optical compressor behavior inspiration)
   - **Source**: Product manual, T4 opto-attenuator specifications
   - **Usage**: Opto-resistor behavior characteristics

4. **Fairchild 670 Compressor/Limiter** (1959-1968)
   - **Used In**: C1COMP (Vari-Mu compressor behavior inspiration)
   - **Source**: Product manual, historical documentation
   - **Usage**: Tube saturation characteristics, timing specifications

5. **API 2520 Discrete Op-Amp, Neve Transformers, Dangerous Music 2-BUS+**
   - **Used In**: CHAN-OUT (planned saturation engines, informational only)
   - **Source**: Published product specifications and technical documentation
   - **Usage**: Parameter ranges, sonic goals (NOT circuit emulation)

---

## Original C1-ChannelStrip Contributions

### Architectural Innovations (~30-90% of each module)

1. **Zero Shared State Architecture** (Control1)
   - Complete isolation of parameter arrays between module types
   - Prevents cross-module interference in multi-module MIDI control
   - Industry-unique architectural pattern

2. **Position-Based Module Detection** (Control1)
   - Automatic hardware integration via expander chain scanning
   - Zero-configuration Console1 hardware synchronization
   - Visual LED feedback system for connection status

3. **VCV Rack Voltage Calibration** (Shape)
   - Empirical threshold mapping for real VCV Rack signal levels
   - Measured typical kick/snare voltages for practical operation
   - Original voltage-to-dB mapping algorithm

4. **Processing Intensity Metering** (Shape)
   - 3-factor algorithm: level difference (40%), crest factor change (30%), dynamic difference (30%)
   - Measures actual processing amount, not just input level
   - Original C1-ChannelStrip metering innovation

5. **Waveform Visualization System** (Shape)
   - 4 time windows (Beat, Envelope, Rhythm, Measure)
   - Adaptive buffer sizes with min/max oversampling
   - Transient and envelope analysis

6. **Metering Systems** (CHAN-IN)
   - RMS/VU/PPM with industry-standard ballistics
   - IEC 60268-18 Type II compliance
   - Multi-mode metering display system

7. **TC Design System** (All modules)
   - Studio aesthetic with Console1 hardware integration
   - Custom typography (Sono font family)
   - Amber accent color scheme (#FFC050)
   - Consistent positioning and alignment standards

### Console1 Hardware Integration (~10-15% of each module)

- 11-parameter MIDI mapping system (total of 37 parameters)
- Bidirectional MIDI feedback with LED ring synchronization
- Echo prevention algorithms with change tracking
- Rate limiting (60Hz) for hardware protection
- Auto-connection algorithm with device name matching
- VU meter data transmission to hardware displays

---

## Modifications Applied to Open-Source Code

All borrowed code has been modified for the C1-ChannelStrip ecosystem. Key modifications:

### From Befaco
- Integrated AeFilter with MindMeld anti-pop slewing
- Extended with C1-ChannelStrip metering system
- Added LED ring visualizations
- Integrated Console1 hardware control
- Adapted SIMD pattern from 4 cascaded stages to 2 stereo channels

### From MindMeld
- Applied timing strategy to FourBand Example smoothing implementation
- Adapted slew rate specifications to exponential RC smoothing
- Extended MasterChannel architecture for dual-mode operation

### From Shelves
- Integrated with FourBand Example EQ architecture
- Modified clipping stage placement (Stage 4 optimization)
- Added context menu for 4-mode analog character selection
- Extended oversampling to 4x option
- Integrated proportional Q algorithm

### From LSP Compressor
- **CRITICAL FIX**: Changed hardcoded 44.1kHz to dynamic sample rate handling
- Modified VCA detection from RMS to peak (SSL G-series authenticity)
- Added type-specific detection algorithms (FET/Optical/Vari-Mu RMS variants)
- Implemented 4 distinct compressor engines with hardware-inspired behavior
- Removed unused variables and optimized state management

### From FourBand Example
- Extended to frequency allocation (band-specific ranges)
- Enhanced with Shelves-based anti-aliasing (replaced simple 2x)
- Added SIMD optimization (Befaco pattern)
- Added analog character modeling (Shelves algorithms)
- Integrated Console1 hardware control (11 parameters)
- Enhanced factory presets with detailed parameter sets

---

## License Compliance Verification

### GPL-3.0-or-later Compatibility Matrix

| Dependency | License | Compatible | Notes |
|------------|---------|------------|-------|
| VCV Rack Framework | GPL-3.0-or-later | ✅ | Core framework |
| Mutable Instruments Shelves | GPL-3.0-or-later | ✅ | Via AudibleInstruments |
| Befaco Plugin | GPL-3.0-or-later | ✅ | AeFilter, SIMD patterns |
| MindMeld Modular | GPL-3.0-or-later | ✅ | Timing strategies, MasterChannel |
| LSP Compressor | GPL-3.0-or-later | ✅ | Class structure |
| libebur128 | MIT License | ✅ | EBU R128 loudness measurement |
| Tom St Denis biquad.c | PUBLIC DOMAIN | ✅ | Explicit public domain |
| Nigel Redmon Biquad | Permissive | ✅ | Commercial use allowed |
| Academic Literature | Not copyrightable | ✅ | Mathematical formulas/theory |
| Hardware Specifications | Not copyrightable | ✅ | Published specifications only |

### Attribution Requirements Met

- ✅ Original copyright notices preserved
- ✅ Modification dates documented
- ✅ GPL-3.0 license terms maintained
- ✅ Source code availability guaranteed
- ✅ Author credits properly attributed
- ✅ Complete provenance documentation in DSP reference files

---

## Development Credits

### C1-ChannelStrip Development Team
- **Copyright**: © 2025 Twisted Cable.
- **License**: GPL-3.0-or-later
- **Project**: Modular Channel Strip with Softube Console 1 MK2 integration
- **Development**: Latif Karoumi

### Acknowledgments

**VCV Rack Ecosystem**:
- **Andrew Belt**: VCV Rack framework and AudibleInstruments foundation
- **Marc Boulé & Steve Baker**: MindMeld parameter management and anti-pop algorithms
- **VCV Team**: Befaco plugin maintenance and filter implementations
- **sadko4u**: LSP Plugins compressor architecture
- **Émilie Gillet**: Original Mutable Instruments Shelves design
- **repelzen**: Original AeFilter mathematical foundations

**Academic & Technical Contributors**:
- **Robert Bristow-Johnson**: RBJ Audio EQ Cookbook (biquad filter formulas)
- **Tom St Denis**: Public domain biquad reference implementation
- **Nigel Redmon**: EarLevel Engineering biquad C++ implementation
- **Julius O. Smith III**: CCRMA digital filter theory and documentation
- **Dimitrios Giannoulis, Michael Massberg, Joshua D. Reiss**: Dynamic range compressor design research

**Hardware Manufacturers** (specifications used for behavioral inspiration):
- Solid State Logic (SSL G-Series)
- UREI / Universal Audio (1176)
- Teletronix (LA-2A)
- Fairchild Recording Equipment (670)
- API Audio (2520 op-amp)
- Rupert Neve Designs
- Dangerous Music

---

## Important Disclaimers

### Algorithm Sources & Limitations

1. **Approximations, Not Emulations**:
   - C1COMP and CHAN-OUT implement **approximations inspired by** published hardware specifications
   - **NOT** circuit-accurate emulations of specific hardware
   - Behavior characteristics based on published specifications and academic theory
   - No proprietary circuit designs or reverse-engineered code used

2. **Open-Source Code Usage**:
   - All open-source code properly attributed with GPL-3.0 compliance
   - Percentages documented in module-specific DSP reference files
   - Modifications clearly documented in source code comments

3. **Academic Theory Implementation**:
   - Mathematical formulas and DSP algorithms from academic literature
   - Not copyrightable knowledge, freely implementable
   - Proper citation of all academic sources

4. **Hardware Specification Usage**:
   - Published specifications used **only** for parameter ranges and sonic goals
   - No circuit schematics, firmware, or proprietary designs used
   - Functional behavior approximation, not circuit emulation

5. **Softube Disclaimer**
   - Console 1 MK2 is a registered trademark of Softube AB.
   - This plugin is an independent implementation and is not affiliated with, endorsed by, or sponsored by Softube AB.

### Ethical Considerations

This project strictly adheres to ethical software development practices:

✅ **All code derived from**:
- Open-source GPL-3.0 compatible code (properly attributed)
- Published academic research (fully cited)
- Publicly available specifications (manufacturer documentation)
- Mathematical models (not copyrightable, scientifically verifiable)

❌ **NO use of**:
- Proprietary code or reverse-engineered circuits
- Trade secrets or confidential information
- Unauthorized hardware analysis or firmware extraction
- Unlicensed commercial software components

---

## Links and References

### Project Resources
- **VCV Rack**: https://vcvrack.com/
- **GPL-3.0 License**: https://www.gnu.org/licenses/gpl-3.0.html
- **Twisted Cable**: https://recordfactory.org

### Dependency Repositories
- **VCV Rack**: https://github.com/VCVRack/Rack
- **AudibleInstruments**: https://github.com/VCVRack/AudibleInstruments
- **Befaco**: https://github.com/VCVRack/Befaco
- **MindMeld Modular**: https://github.com/MarcBoule/MindMeldModular
- **LSP Plugins**: https://github.com/sadko4u/lsp-plugins

### Technical Documentation
- **RBJ Audio EQ Cookbook**: https://www.w3.org/2011/audio/audio-eq-cookbook.html
- **Tom St Denis biquad.c**: https://www.musicdsp.org/
- **EarLevel Engineering**: https://www.earlevel.com/
- **CCRMA (Stanford)**: https://ccrma.stanford.edu/~jos/

### Complete Attribution Details

For comprehensive module-specific attribution with exact code percentages, algorithm provenance, and licensing chains, see:

- [CHAN-IN dsp references](/docs/manuals/chanin-dsp-references.md)
- [SHAPE dsp references](/docs/manuals/shape-dsp-references.md)
- [C1-EQ dsp references](/docs/manuals/c1eq-dsp-references.md)
- [C1-COMP dsp references](/docs/manuals/c1comp-dsp-references.md)
- [CHAN-OUT dsp references](/docs/manuals/chanout-dsp-references.md)

---

**This attribution document ensures full GPL-3.0 compliance and proper credit to all contributors in the VCV Rack ecosystem and broader audio DSP community.**

**Document Version**: 2.0
**Last Updated**: 03-11-2025
**Author**: Latif Karoumi
**License**: GPL-3.0-or-later
