# C1-COMP Module - Technical Documentation RC1

## Overview

C1-COMP is a stereo compressor/limiter module for VCV Rack featuring four distinct compression algorithms modeled after classic hardware units. The module provides dynamic range control with visual feedback through peak metering and gain reduction displays.

## Architecture

### Core DSP Engine

The C1-COMP module implements four separate compression algorithms, each based on different analog circuit topologies:

1. **VCA (SSL G-Series)**: Voltage-controlled amplifier design with precise control characteristics
2. **FET (1176)**: Field-effect transistor design with fast attack and aggressive character
3. **Optical (LA-2A)**: Photoresistor-based design with smooth, program-dependent response
4. **Vari-Mu (Fairchild)**: Tube-based variable-gain design with gentle compression curves

Each compressor type is implemented in a dedicated C++ class inheriting from a common `CompressorEngine` base class, ensuring consistent parameter interfacing while maintaining distinct sonic characteristics.

### Signal Processing Features

#### 1. Compression Parameters

**Threshold** (-20dB to +10dB):
- Internal parameter range: 0.0 to 1.0
- Display mapping: -20dB to +10dB linear
- Controls the level at which compression begins

**Ratio** (1:1 to 20:1):
- Internal parameter range: 0.0 to 1.0
- Quadratic taper mapping for improved control resolution at lower ratios
- Scaling formula: `1.0 + (value² × 19.0)`

**Attack** (6 discrete positions):
- 0.1ms, 0.3ms, 1.0ms, 3.0ms, 10ms, 30ms
- Stepped parameter (0-5 integer values)
- Controls envelope follower rise time

**Release** (100ms to 1200ms or AUTO):
- Parameter range: 0.0 to 1.0
- Normal mode (0.0-0.9): Logarithmic scaling from 100ms to 1200ms
- AUTO mode (≥0.9): Program-dependent release timing
- Scaling formula: `100.0 × 12.0^(normalizedValue)`

**Dry/Wet Mix** (0% to 100%):
- Internal parameter range: 0.0 to 1.0
- Linear crossfade between unprocessed and compressed signals
- Enables parallel compression techniques

#### 2. Context Menu Options

**Input Gain**: -24dB to +24dB (default: 0dB)
- Pre-compression gain adjustment
- 1dB step resolution

**Output Gain**: -24dB to +24dB (default: 0dB)
- Post-compression gain adjustment
- 1dB step resolution

**Auto Makeup Gain**: On/Off (default: Off)
- Automatically compensates for threshold reduction
- Adds gain equal to threshold attenuation

**Knee Override**: Auto, 0dB to 12dB (default: Auto)
- Manual control of compression knee width
- Auto mode uses algorithm-specific default values

**Reference Level**: 5V or 10V (default: 10V)
- Calibrates compression threshold to VCV Rack signal levels
- 5V reference: Optimized for modular synthesis levels
- 10V reference: Optimized for audio-rate signals

**VU Meter Mode**: Dot or Bar (default: Bar)
- Changes gain reduction display style

### Compressor Type Specifications

#### VCA (SSL G-Series)
- **Knee**: 0dB (hard knee by default, configurable via context menu 0-12dB)
- **Attack Coefficient**: `std::exp(-1.0f / (attackMs × sampleRate / 1000.0f))`
- **Release Coefficient**: `std::exp(-1.0f / (releaseMs × sampleRate / 1000.0f))`
- **Characteristics**: Compression with minimal coloration
- **Use Case**: Dynamic control, bus compression, mastering

#### FET (1176)
- **Knee**: 1dB (hard knee)
- **Attack Coefficient**: Faster response multiplier
- **Release Coefficient**: Program-dependent in AUTO mode
- **Characteristics**: Fast transient response, aggressive character
- **Use Case**: Drums, vocals, punch enhancement

#### Optical (LA-2A)
- **Knee**: 6dB (soft knee)
- **Attack/Release**: Program-dependent response curves
- **Characteristics**: Smooth compression with gentle onset
- **Use Case**: Vocals, bass, mix bus, leveling

#### Vari-Mu (Fairchild)
- **Knee**: 4dB (medium soft knee)
- **Attack/Release**: Slower, gentler response
- **Characteristics**: Warm, vintage compression with harmonic saturation simulation
- **Use Case**: Mix bus, mastering, vintage warmth

### Visual Feedback System

#### Attack LED Ring
- **Type**: Discrete 6-position indicator
- **Implementation**: Custom `AttackLEDRingWidget` with labeled positions
- **Geometry**: 280° rotation arc, 80° bottom gap
- **LED Count**: 6 LEDs at fixed positions corresponding to attack times
- **Labels**: "0.1ms", "0.3ms", "1ms", "3ms", "10ms", "30ms"
- **Behavior**: Single LED illuminated at current attack setting

#### Release LED Ring
- **Type**: Continuous + animated indicator
- **Implementation**: Custom `ReleaseLEDRingWidget` with 15 LEDs
- **Geometry**: 280° rotation arc, 80° bottom gap
- **Normal Mode (0-90%)**:
  - Smooth LED tracking of parameter value
  - Linear mapping across 280° arc
- **AUTO Mode (90-100%)**:
  - Alternating animation pattern
  - 4 complete alternations at 2.5Hz over 0.8s
  - 0.5s silent gap between animation cycles
  - Burst pattern repeats continuously when in AUTO

#### Peak Metering System

**Input Meters** (Stereo):
- **Range**: -60dB to 0dB
- **Attack Time**: Instantaneous (0ms)
- **Decay Time**: 300ms exponential decay
- **Peak Hold**: 0.5s hold time
- **LED Count**: 11 LEDs per channel
- **Display**: Vertical bar graph

**Gain Reduction Meter** (Mono):
- **Range**: 0dB to -20dB (inverted scale)
- **Attack Time**: Instantaneous (0ms)
- **Decay Time**: 300ms exponential decay
- **Peak Hold**: 0.1s hold time (faster for GR visualization)
- **LED Count**: 11 LEDs
- **Display**: Vertical bar graph (inverted)
- **Color**: Amber LEDs

**Output Meters** (Stereo):
- **Range**: -60dB to 0dB
- **Attack Time**: Instantaneous (0ms)
- **Decay Time**: 300ms exponential decay
- **Peak Hold**: 0.5s hold time
- **LED Count**: 11 LEDs per channel
- **Display**: Vertical bar graph

#### VU Meter (Gain Reduction Display)

**11-LED Vertical Meter**:
- **Function**: Real-time gain reduction visualization
- **Range**: 0dB (no compression) to -20dB (maximum GR)
- **Update Rate**: Sample-accurate
- **Smoothing**: Envelope follower matching compressor release
- **Display Modes**:
  - **Dot Mode**: Single LED indicates current GR level
  - **Bar Mode**: Filled bar from bottom to current GR level
- **LED Mapping**: Linear dB scale across 11 discrete levels

### User Interface

#### Typography Integration
- **Title Font**: Sono Bold 18pt with black outline for contrast
- **Label Font**: Sono Medium 10pt with black outline for clarity
- **Parameter Labels**: Sono Light 9pt for secondary information
- **Styling**: White text on dark background with 0.5px black outline

#### Control Layout
- **Top Row**: Bypass button, Attack encoder with LED ring, Release encoder with LED ring
- **Middle Row**: Threshold encoder, Ratio encoder, Dry/Wet encoder
- **Bottom Section**: Input meters (L/R), GR meter (center), Output meters (L/R)
- **Bottom Center**: VU meter (11-LED gain reduction display)
- **I/O Section**: Stereo inputs, stereo outputs, sidechain input

#### Interactive Elements
- **Custom Round Buttons**: `C1WhiteRoundButton` toggle switches with amber LED indication
- **LED Ring Encoders**: `C1Knob280` with 15-LED rings (280° rotation, 80° bottom gap)
- **Themed I/O Jacks**: `ThemedPJ301MPort` for all inputs and outputs
- **Dynamic Button Background**: Dull ivory when off, dark gray when lit

## Signal Flow

```
Input → Input Gain → Envelope Follower → Compression Detection → Gain Computer
   ↓                                                                      ↓
Input Meter                                                    Gain Reduction Amount
   ↓                                                                      ↓
   └──→ Gain Reduction Application ←──────────────────────────────────────┘
              ↓
        Compressed Signal
              ↓
        Auto Makeup Gain (optional)
              ↓
        Output Gain
              ↓
        Dry/Wet Mixer ←─────────── Dry Signal
              ↓
          Output → Output Meter

Sidechain Mode:
Sidechain Input → Envelope Follower → Gain Computer → Apply to Audio Input

Metering Path:
Input RMS → Input Peak Meter (L/R)
Gain Reduction → GR Peak Meter (Center) + VU Meter (11-LED)
Output RMS → Output Peak Meter (L/R)
```

## Parameter Specifications

| Parameter | Range | Default | Unit | Description |
|-----------|-------|---------|------|-------------|
| Threshold | -20 to +10 | 0 | dB | Compression threshold level |
| Ratio | 1:1 to 20:1 | 1:1 | ratio | Compression ratio |
| Attack | 0.1, 0.3, 1.0, 3.0, 10, 30 | 0.1 | ms | Envelope attack time (discrete) |
| Release | 100 to 1200 or AUTO | 100 | ms | Envelope release time |
| Dry/Wet | 0 to 100 | 100 | % | Parallel compression mix |
| Bypass | Off/On | Off | boolean | True bypass mode |
| Display Enable | Off/On | On | boolean | Meter display visibility |

## Console 1 MK2 Integration

C1-COMP supports bidirectional hardware control via the Softube Console 1 MK2 when connected to the C1 module.

### MIDI CC Mapping

| Parameter | Console 1 CC | MIDI Range | Value Mapping | Description |
|-----------|--------------|------------|---------------|-------------|
| **BYPASS** | 46 | 0/127 | Toggle | Bypass on/off |
| **ATTACK** | 51 | 0-127 | Custom remapping to 6 positions | Discrete attack times with hardware encoder optimization |
| **RELEASE** | 48 | 0-127 | 0.0-1.0 linear | 100ms-1200ms or AUTO (≥90%) |
| **THRESHOLD** | 47 | 0-127 | 0.0-1.0 linear → -20dB to +10dB | Compression threshold |
| **RATIO** | 49 | 0-127 | 0.0-1.0 linear (quadratic taper in module) | 1:1 to 20:1 compression ratio |
| **DRY/WET** | 50 | 0-127 | 0.0-1.0 linear → 0%-100% | Parallel compression mix |
| **VU Meter Feedback** | 115 | 0-127 | Gain reduction level | Real-time GR visualization on hardware |

### Parameter Sync Burst

When C1-COMP connects to C1 or patch loads:
- All 6 parameters transmitted once to Console 1 MK2 hardware
- Ensures hardware encoder positions match software state
- Prevents "pickup mode" issues

### AUTO Mode LED Animation

When Release parameter enters AUTO mode (≥90%):
- **Release LED Ring** displays animated burst pattern
- **Animation Timing**: 4 alternations at 2.5Hz over 0.8s, followed by 0.5s silent gap
- **Total Cycle**: 1.3s (repeats continuously while in AUTO)
- **Purpose**: Visual confirmation that program-dependent release is active
- **Hardware Feedback**: MIDI CC burst pattern syncs Console 1 MK2 encoder LEDs

### Bidirectional Feedback

- **Hardware → Software**: MIDI CC input updates C1-COMP parameters in real-time
- **Software → Hardware**: Parameter changes in VCV Rack update Console 1 MK2 LED rings
- **Echo Prevention**: Feedback loop protection prevents MIDI oscillation
- **Rate Limiting**: 60Hz update rate (every 256 samples at 44.1kHz)

## CV Expander Integration (COM-X)

C1-COMP supports CV modulation via the COM-X (COMP-CV) expander module.

### Expander Position
COM-X connects to C1-COMP's **right expander port**.

### CV Inputs (4 Parameters)

| CV Input | Range | Modulation Type | Target Parameter | Mapping |
|----------|-------|-----------------|------------------|---------|
| **Ratio CV** | -1.0 to +1.0 | Additive | RATIO_PARAM | Maps to full 0.0-1.0 range (1:1 to 20:1 with quadratic taper) |
| **Threshold CV** | -1.0 to +1.0 | Additive | THRESHOLD_PARAM | Maps to full 0.0-1.0 range (-20dB to +10dB) |
| **Release CV** | -1.0 to +1.0 | Additive | RELEASE_PARAM | Maps to full 0.0-1.0 range (100ms to 1200ms or AUTO) |
| **Mix CV** | -1.0 to +1.0 | Additive | DRY_WET_PARAM | Maps to full 0.0-1.0 range (0% to 100% wet) |

### Expander Message Protocol

- **Thread-safe**: Double-buffered messaging via `C1COMPExpanderMessage` struct
- **Update Rate**: Every audio sample (no decimation)
- **Detection**: Automatic via `modelC1COMPCV` slug matching
- **CV Behavior**: All CVs use linear scaling with additive modulation
- **Range Limiting**: Final parameter values clamped to valid ranges after CV application
- **Smoothing**: Applied via module's internal parameter smoothing for stable operation

### CV Modulation Formula

```cpp
finalValue = clamp(parameterValue + cvInput, minValue, maxValue)
```

Example: Threshold with CV
```cpp
float thresholdBase = rescale(params[THRESHOLD_PARAM].getValue(), 0.0f, 1.0f, -20.0f, 10.0f);
float threshold = clamp(thresholdBase + thresholdCVMod, -20.0f, 10.0f);
```

## Technical Implementation Notes

### Sample Rate Handling
All timing calculations use dynamic sample rate:
```cpp
float sr = APP->engine->getSampleRate();
attackCoeff = std::exp(-1.0f / (attackMs * sr / 1000.0f));
releaseCoeff = std::exp(-1.0f / (releaseMs * sr / 1000.0f));
```

### Thread Safety
- **Audio Thread**: All DSP processing in `process()` method
- **UI Thread**: All visual updates and meter drawing
- **Atomic Pointers**: `std::atomic<PeakMeterWidget*>` for thread-safe widget access
- **Shutdown Flag**: `std::atomic<bool> isShuttingDown` prevents crashes during module deletion

### Envelope Follower Implementation
Each compressor type implements custom envelope detection:
- **VCA**: Standard exponential follower with fixed coefficients
- **FET**: Asymmetric attack/release with faster attack multiplier
- **Optical**: Program-dependent coefficients simulating photoresistor behavior
- **Vari-Mu**: Slower, gentler coefficients with additional smoothing

### Gain Reduction Calculation
```cpp
float excess = inputDb - thresholdDb;
if (excess > 0.0f) {
    float gainReductionDb = excess * (1.0f - (1.0f / ratio));
    gainReductionDb = applyKnee(excess, gainReductionDb, kneeWidth);
    float gainLin = std::pow(10.0f, gainReductionDb / 20.0f);
    output = input * gainLin;
}
```

### AUTO Release Mode
When release parameter ≥ 0.9:
- Release time adapts to signal envelope slope
- Faster release for transient material
- Slower release for sustained material
- Implementation varies by compressor type

### Peak Meter Decay
```cpp
float decayCoeff = std::exp(-1.0f / (decayTimeMs * sampleRate / 1000.0f));
currentPeak = std::max(inputLevel, currentPeak * decayCoeff);
```

### LED Ring Animation Timing
AUTO mode animation (lines 244-265 in C1-COMP.cpp):
```cpp
// 4 alternations at 2.5Hz = 0.4s per cycle × 2 (on+off) = 0.8s
// Followed by 0.5s silent gap
// Total cycle: 1.3s
float burstTime = 0.8f;  // Animation duration
float silentTime = 0.5f; // Gap duration
float cycleTime = burstTime + silentTime;
```

## Use Cases

### 1. Mix Bus Compression
- **Settings**: VCA or Vari-Mu, low ratio (2:1-4:1), slow attack (3-10ms), medium release (400-800ms)
- **Purpose**: Reduce dynamic range across mix elements
- **Threshold**: -6dB to -3dB
- **Dry/Wet**: 100% or 70-80% for parallel

### 2. Drum Bus Processing
- **Settings**: FET, medium ratio (4:1-8:1), fast attack (0.3-1ms), AUTO release
- **Purpose**: Add punch and control transients
- **Threshold**: -10dB to -5dB
- **Dry/Wet**: 100%

### 3. Vocal Leveling
- **Settings**: Optical, medium ratio (4:1-6:1), medium attack (3-10ms), slow release (800-1200ms)
- **Purpose**: Reduce dynamic range, maintain level
- **Threshold**: -15dB to -10dB
- **Dry/Wet**: 100%

### 4. Parallel Compression
- **Settings**: Any type, high ratio (8:1-20:1), fast attack (0.1-1ms), medium release
- **Purpose**: Add density without losing dynamics
- **Threshold**: -20dB to -15dB
- **Dry/Wet**: 30-50%

### 5. Sidechain Ducking
- **Settings**: VCA, medium-high ratio (4:1-10:1), fast attack (0.1-0.3ms), fast-medium release (100-400ms)
- **Purpose**: Create rhythmic ducking effects
- **Sidechain**: Connect external trigger signal
- **Threshold**: Adjust to taste

## Performance Characteristics

- **CPU Usage**: Moderate overhead with per-sample DSP processing
- **Latency**: Zero-latency processing (feed-forward design)
- **Memory**: Static allocation, approximately 8KB per instance
- **Precision**: 32-bit floating point throughout signal path
- **Polyphony**: Stereo processing with independent L/R envelope followers

## Design Rationale

C1-COMP implements the following design decisions:

1. **Algorithm Diversity**: Four distinct compressor types cover different use cases and sonic characteristics
2. **Visual Feedback**: Multiple metering systems provide insight into compression behavior
3. **Discrete Attack Control**: Stepped attack times match hardware behavior
4. **AUTO Release Mode**: Program-dependent release adapts to signal envelope
5. **Parallel Compression**: Dry/Wet control enables parallel compression techniques
6. **Sample-rate Independence**: Consistent behavior across all sample rates prevents timing issues
7. **Reference Level Options**: Calibration for both modular synthesis and audio signal levels
8. **LED Ring Animation**: Visual indication of AUTO mode provides operational feedback

C1-COMP provides dynamic control and sound shaping in modular synthesis and music production.

---


**Document Version**: RC1</br>
**Project Inception**: 21-09-2025</br>
**Author**: Latif Karoumi</br>

Copyright © 2025 Twisted Cable. Licensed under GPL-3.0-or-later.
