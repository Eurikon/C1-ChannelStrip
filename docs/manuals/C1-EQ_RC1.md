# C1-EQ - 4-Band Parametric Equalizer with Analog Character
## Technical Specification Document - Release Candidate 1

## Overview

C1EQ is a 4-band parametric equalizer for VCV Rack, featuring SIMD-optimized DSP processing, real-time spectrum analysis, and four analog character modes. Designed as part of the C1-ChannelStrip system, C1-EQ provides frequency correction and tonal shaping with integrated Console 1 MK2 hardware control support.

### Key Features
- **4-Band Parametric EQ**: Two fully parametric mid bands with variable Q, two multimode bands (LF/HF) with switchable filter types
- **SIMD Processing**: Optimized stereo processing using `dsp::TBiquadFilter<float_4>` for ARM64 efficiency
- **Analog Character Modes**: Four circuit modeling modes (Transparent, Light, Medium, Full) for harmonic enhancement
- **Real-Time Spectrum Analyzer**: 2048-sample FFT with 128-band logarithmic display running on dedicated worker thread
- **Anti-Aliasing Oversampling**: Selectable 2x/4x oversampling with sample-rate-adaptive filtering
- **Console 1 MK2 Integration**: 13-parameter MIDI CC mapping with bidirectional feedback
- **Clipping Metering**: RGB clipping indicator with green-to-red progression
- **Stereo Processing**: True stereo with intelligent mono fallback

## Architecture

### Core DSP Engine
C1EQ implements a cascaded 4-band biquad filter topology with SIMD optimization:

```
Input → Global Gain → Band 1 (LF) → Band 2 (LMF) → Band 3 (HMF) → Band 4 (HF) → Analog Character → Output
         ±24dB         20-400Hz      200-2kHz        1k-8kHz        4k-20kHz      4 modes
```

### SIMD Implementation
Each band uses stereo processing with `dsp::TBiquadFilter<float_4>` for parallel left/right channel computation:
```cpp
dsp::TBiquadFilter<float_4> bands[4][2];  // [band][L/R]
```

### Band Architecture

**Band 1 (LF - Low Frequency)**:
- Frequency: 20Hz - 400Hz (default: 20Hz)
- Gain: ±20dB
- Q: Fixed at 0.8 (hardcoded for Console 1 compatibility)
- Mode: 3-way switch (High-Pass / Bell / Shelf)

**Band 2 (LMF - Low-Mid Frequency)**:
- Frequency: 200Hz - 2kHz (default: 250Hz)
- Gain: ±20dB
- Q: 0.3 - 12.0 (default: 1.0)

**Band 3 (HMF - High-Mid Frequency)**:
- Frequency: 1kHz - 8kHz (default: 2kHz)
- Gain: ±20dB
- Q: 0.3 - 12.0 (default: 1.0)

**Band 4 (HF - High Frequency)**:
- Frequency: 4kHz - 20kHz (default: 20kHz)
- Gain: ±20dB
- Q: Fixed at 1.0 (hardcoded for Console 1 compatibility)
- Mode: 3-way switch (Low-Pass / Bell / Shelf)

### Proportional Q Algorithm
C1EQ implements optional proportional Q compensation to maintain consistent bandwidth across gain changes:
```cpp
Qeff = Q × (1.0 + 0.02 × abs(gain))
```
**Note**: This feature is enabled by default but can be toggled via context menu (right-click module)

## Signal Processing Features

### 1. Global Gain Control
- **Range**: -24dB to +24dB
- **Default**: 0dB
- **Purpose**: Master output level adjustment
- **Console 1 CC**: 80

### 2. Frequency Band Controls

#### Band 1 (LF) - Low Frequency
- **Frequency Range**: 20Hz - 400Hz (logarithmic scale)
- **Gain Range**: ±20dB
- **Filter Modes**:
  - **High-Pass (mode 2)**: 12dB/oct slope, Q=0.707 (Butterworth), removes subsonic rumble
  - **Bell (mode 1)**: Parametric boost/cut with fixed Q=0.8
  - **Shelf (mode 0)**: Low-shelf response for bass enhancement/reduction
- **Console 1 CC**: Freq=91, Gain=92, Mode=93
- **Default**: 20Hz, 0dB, Shelf mode

**High-Pass Mode Auto-Configuration**:
When switching to High-Pass mode (mode 2):
- FREQ automatically jumps to 20Hz (minimum, neutral filtering position)
- GAIN automatically resets to 0dB and becomes locked (cannot be adjusted)
- GAIN remains locked at 0dB while in High-Pass mode
- Prevents user confusion (gain has no effect in filter Cut mode)

#### Band 2 (LMF) - Low-Mid Frequency
- **Frequency Range**: 200Hz - 2kHz (logarithmic scale)
- **Gain Range**: ±20dB
- **Q Range**: 0.3 - 12.0 (default: 1.0)
- **Filter Type**: Parametric bell
- **Console 1 CC**: Freq=88, Q=89, Gain=90
- **Default**: 250Hz, Q=1.0, 0dB

#### Band 3 (HMF) - High-Mid Frequency
- **Frequency Range**: 1kHz - 8kHz (logarithmic scale)
- **Gain Range**: ±20dB
- **Q Range**: 0.3 - 12.0 (default: 1.0)
- **Filter Type**: Parametric bell
- **Console 1 CC**: Freq=85, Q=86, Gain=87
- **Default**: 2kHz, Q=1.0, 0dB

#### Band 4 (HF) - High Frequency
- **Frequency Range**: 4kHz - 20kHz (logarithmic scale)
- **Gain Range**: ±20dB
- **Filter Modes**:
  - **Low-Pass (mode 2)**: 12dB/oct slope, Q=0.707 (Butterworth), removes ultrasonic content
  - **Bell (mode 1)**: Parametric boost/cut with fixed Q=1.0
  - **Shelf (mode 0)**: High-shelf response for air/presence control
- **Console 1 CC**: Freq=82, Gain=83, Mode=65
- **Default**: 20kHz, 0dB, Shelf mode

**Low-Pass Mode Auto-Configuration**:
When switching to Low-Pass mode (mode 2):
- FREQ automatically jumps to 20kHz (maximum, neutral filtering position)
- GAIN automatically resets to 0dB and becomes locked (cannot be adjusted)
- GAIN remains locked at 0dB while in Low-Pass mode
- Prevents user confusion (gain has no effect in filter Cut mode)

### 3. Analog Character Processing

C1EQ features four analog character modes inspired by classic console circuitry:

#### TRANSPARENT (Mode 0)
- Digital precision with no harmonic coloration
- Clipping detection only (no analog modeling)
- LED indicator: OFF

#### LIGHT (Mode 1)
- Subtle harmonic enhancement with soft-knee saturation
- Simulates discrete opamp circuitry
- Adds 2nd-order harmonics
- LED indicator: GREEN (0.5 brightness)

#### MEDIUM (Mode 2)
- Console-style saturation with VCA curve modeling
- Implements VCA state tracking and feedback dynamics
- Adds 2nd and 3rd-order harmonics with "glue" compression character
- LED indicator: BLUE (0.5 brightness)

#### FULL (Mode 3)
- Complete circuit modeling with transformer coloration
- Combines VCA dynamics, transformer saturation, and output stage clipping
- Maximum harmonic richness with 2nd through 5th-order harmonics
- LED indicator: RED (0.5 brightness)

**Console 1 CC**: Mode selector (4-position snap knob)

### 4. Anti-Aliasing Oversampling

C1EQ implements adaptive oversampling with sample-rate-specific anti-aliasing filters:

- **Oversampling Factors**: Sample-rate dependent (1x at 176.4kHz+, 2x at 88.2-96kHz, 3x at 44.1-48kHz)
- **Filter Design**: Second-order-section (SOS) biquad cascades
- **Upsampling**: Pre-processing with low-pass filtering
- **Downsampling**: Post-processing with matched anti-aliasing
- **Bypass**: Can be disabled for CPU efficiency (bypass LED indicator OFF)

### 5. Real-Time Spectrum Analyzer

**Technical Specifications**:
- **FFT Size**: 2048 samples
- **Display Bands**: 128 logarithmically-spaced frequency bins
- **Frequency Range**: 20Hz - 22kHz
- **Processing**: Dedicated worker thread with mutex-protected buffers
- **Update Rate**: Dynamic (buffer-fill dependent)
- **Peak Hold**: Visual peak tracking with decay timers
- **Channels**: Stereo (independent left/right analysis)

**Implementation**:
- Uses `dsp::RealFFT` for real-valued FFT computation
- Logarithmic frequency mapping for perceptually-linear display
- Thread-safe spectrum data transfer via `std::mutex`
- Can be toggled ON/OFF to save CPU resources
- Default: ENABLED

## Parameter Specifications

| Parameter | Range | Default | Unit | Console 1 CC | Description |
|-----------|-------|---------|------|--------------|-------------|
| **GLOBAL_GAIN** | -24 to +24 | 0 | dB | 80 | Master output level |
| **B1_FREQ** | 20 to 400 | 20 | Hz | 91 | Low frequency band center |
| **B1_GAIN** | -20 to +20 | 0 | dB | 92 | Low frequency gain |
| **B1_MODE** | 0, 1, 2 | 2 (Shelf) | - | 93 | Low frequency filter type (Shelf/Bell/HPF) |
| **B2_FREQ** | 200 to 2000 | 250 | Hz | 88 | Low-mid frequency band center |
| **B2_Q** | 0.3 to 12.0 | 1.0 | - | 89 | Low-mid Q factor (bandwidth) |
| **B2_GAIN** | -20 to +20 | 0 | dB | 90 | Low-mid frequency gain |
| **B3_FREQ** | 1000 to 8000 | 2000 | Hz | 85 | High-mid frequency band center |
| **B3_Q** | 0.3 to 12.0 | 1.0 | - | 86 | High-mid Q factor (bandwidth) |
| **B3_GAIN** | -20 to +20 | 0 | dB | 87 | High-mid frequency gain |
| **B4_FREQ** | 4000 to 20000 | 20000 | Hz | 82 | High frequency band center |
| **B4_GAIN** | -20 to +20 | 0 | dB | 83 | High frequency gain |
| **B4_MODE** | 0, 1, 2 | 2 (Shelf) | - | 65 | High frequency filter type (Shelf/Bell/LPF) |
| **ANALOG_MODE** | 0, 1, 2, 3 | 0 (Transparent) | - | - | Analog character mode |
| **OVERSAMPLE** | 0, 1 | 0 (Off) | - | - | Enable/disable oversampling |
| **BYPASS** | 0, 1 | 0 (Off) | - | - | True bypass |
| **ANALYSER_ENABLE** | 0, 1 | 1 (On) | - | - | Enable spectrum analyzer |

## Context Menu Options

Right-click the module to access advanced settings:

### Enable VCA Compression
- **Default**: OFF (disabled)
- **Purpose**: Enables VCA-style compression in analog character modes
- **Effect**: Adds dynamic gain reduction and "glue" compression character
- **Recommendation**: Enable for mix bus processing, disable for corrective EQ

### Enable Proportional Q
- **Default**: ON (enabled)
- **Purpose**: Automatically widens filter bandwidth at extreme gain settings
- **Formula**: `Qeff = Q × (1.0 + 0.02 × abs(gain))`
- **Effect**: Prevents filters from becoming uncontrollably narrow at ±20dB boost/cut
- **Recommendation**: Keep enabled for musical response, disable for surgical precision

## Visual Feedback System

### LED Ring Encoders
- **15-LED Rings**: Custom `LedRingOverlay` widgets with 280° rotation
- **Smooth Tracking**: Continuous parameter value representation
- **Crossfade Animation**: Adjacent LEDs fade for smooth visual feedback
- **Bottom Gap**: 80° gap (4 LED positions) for visual reference

### Mode Selector Rings
- **4-Position Rings**: `LedRingOverlaySkip4` for analog character mode
- **Discrete Positions**: LEDs at positions 0, 5, 9, 14 (equally spaced)
- **Instant Switching**: No crossfade on mode parameters

### Status Lights

**Clipping Indicator (RGB LED)**:
- **Green (0.5 brightness)**: Clean signal, no clipping
- **Green→Amber→Red**: Progressive clipping severity indication
- **Pure Red (0.9 brightness)**: Maximum clipping detected
- Monitors post-analog-processing signal level

**Analog Mode Indicator (RGB LED)**:
- **OFF**: Transparent mode (digital precision)
- **Green (0.5)**: Light mode (subtle harmonics)
- **Blue (0.5)**: Medium mode (console saturation)
- **Red (0.5)**: Full mode (complete circuit modeling)

**Oversampling Indicator (Invisible When OFF)**:
- **Visible (1.0 brightness)**: Oversampling enabled
- **Invisible (0.0 brightness)**: Oversampling disabled

**Mode Indicator LEDs (Tiny Yellow Lights)**:
- **LF Band**: 3 LEDs indicating High-Pass (top) / Bell (middle) / Shelf (bottom)
- **HF Band**: 3 LEDs indicating Low-Pass (top) / Bell (middle) / Shelf (bottom)
- **Brightness**: 0.7f when active, 0.0f when inactive

**Bypass Button Light**:
- **White (0.65 brightness)**: Bypass active (signal passes unprocessed)
- **OFF**: Processing active

## User Interface

### Typography
- **Module Title**: Sono_Proportional-Bold 18pt, centered at Y=16
- **Band Labels**: "LF", "LMF", "HMF", "HF" in Sono_Proportional-Medium 10pt
- **Parameter Labels**: "FREQ", "GAIN", "Q" in Sono_Proportional-Medium 10pt
- **I/O Labels**: "INPUT", "OUTPUT" in Sono_Proportional-Bold 10pt at Y=325
- **Text Alignment**: All labels use CENTER | MIDDLE alignment

### Control Layout
- **15HP Width**: 225mm panel (3 × 5HP)
- **Module Size**: Largest in C1-ChannelStrip system
- **Encoder Rows**:
  - Top row: B1 Freq/Gain, B2 Freq/Q/Gain, B3 Freq/Q/Gain, B4 Freq/Gain
  - Bottom row: Master Gain (left), Analog Mode (right)
- **Spectrum Display**: 200×100px centered in upper panel area (Y=49)
- **Mode Switches**: White round buttons at Y=131 for LF/HF mode selection
- **Decorative Shape**: #252525 filled rectangle (3, 26, 219×231) behind controls

### I/O Configuration
- **Inputs**: Stereo pair (L/R) at bottom left (40, 57)
- **Outputs**: Stereo pair (L/R) at bottom right (170, 185)
- **Signal Flow**: Left-to-right with mono fallback (right input normalled to left)

### Interactive Elements
- **C1Knob280**: 280° rotation knobs with integrated LED rings
- **C1WhiteRoundButton**: 3-way cycling buttons (0→1→2→0) for mode selection
- **Spectrum Display**: Real-time FFT visualization with peak hold
- **ThemedPJ301MPort**: Standard VCV Rack stereo jacks

## Signal Flow

```
                    ┌─────────────────────────────────────────────────┐
                    │              C1-EQ (15HP)                       │
                    │                                                 │
 INPUT L ──────────►│  Global    Band 1  Band 2   Band 3   Band 4     │──────────► OUTPUT L
                    │  Gain  ──► (LF) ─► (LMF) ─► (HMF) ──►(HF)       │
 INPUT R ──────────►│  ±24dB     20-     200-     1k-      4k-        │──────────► OUTPUT R
   (or normalled)   │            400Hz   2kHz     8kHz     20kHz      │
                    │                                                 │
                    │            ▼ Optional Oversampling (2x/4x)      │
                    │            ▼ Analog Character (4 modes)         │
                    │                                                 │
                    │  [Spectrum Analyzer: 2048-sample FFT]           │
                    │  [Clipping Detector: RGB LED feedback]          │
                    └─────────────────────────────────────────────────┘
```

## Technical Implementation Notes

### Thread Safety
C1EQ implements careful thread separation:

```cpp
// Audio Thread (process() function)
- Reads parameter values (thread-safe via VCV Rack's atomic params)
- Processes audio through biquad filter cascade
- Feeds samples to spectrum analyzer buffer (mutex-protected)
- Updates clipping detector

// UI Thread (draw() function)
- Updates LED ring positions
- Renders spectrum display
- Reads spectrum data (mutex-protected copy)

// Worker Thread (spectrum analysis)
- Waits on condition variable for new buffer data
- Performs FFT computation
- Maps to logarithmic display bands
- Updates peak hold timers
```

### Sample Rate Handling
C1EQ adapts to sample rate changes dynamically:

```cpp
void onSampleRateChange(const SampleRateChangeEvent& e) override {
    sampleRate = e.sampleRate;

    // Reinitialize oversampler with new rate
    oversampler.init(sampleRate);

    // Reinitialize upsampling/downsampling filters
    up_filter_[0].Init(sampleRate);
    up_filter_[1].Init(sampleRate);
    down_filter_[0].Init(sampleRate);
    down_filter_[1].Init(sampleRate);

    // Update spectrum analyzer sample rate
    if (spectrumAnalyzer) {
        spectrumAnalyzer->setSampleRate(sampleRate);
    }
}
```

### SIMD Stereo Processing
Stereo processing using SIMD float_4 vectors:

```cpp
// Pack stereo into SIMD vector
float_4 sigVec = float_4(leftSample, rightSample, 0.f, 0.f);

// Process all 4 bands with SIMD
for (int band = 0; band < 4; band++) {
    sigVec = bands[band][0].process(sigVec);  // Single SIMD operation
}

// Extract results
leftOutput = sigVec[0];
rightOutput = sigVec[1];
```

### Console 1 MK2 Parameter Sync
Bidirectional MIDI feedback ensures hardware knobs match software state:

```cpp
// Hardware → Software (MIDI CC input)
if (cc == 91) {  // B1 FREQ
    float minLog2 = std::log2(20.0f);
    float maxLog2 = std::log2(400.0f);
    params[B1_FREQ_PARAM].setValue(minLog2 + (scaledValue * (maxLog2 - minLog2)));
}

// Software → Hardware (parameter sync burst)
float freqValue = params[B1_FREQ_PARAM].getValue();
float freqHz = std::pow(2.0f, freqValue);
float normalizedValue = (freqValue - std::log2(20.0f)) / (std::log2(400.0f) - std::log2(20.0f));
int midiValue = std::round(normalizedValue * 127.0f);
sendMidiCC(91, midiValue);
```

## Use Cases

### 1. Frequency Correction
**Scenario**: Removing resonant frequencies from recorded instruments
- Set ANALOG_MODE to **TRANSPARENT** for digital precision
- Use **Band 2 or Band 3** with narrow Q (8.0-12.0)
- Sweep frequency to locate problematic resonance
- Apply narrow cut (-6 to -12dB) at resonant frequency
- Enable spectrum analyzer to visualize frequency response

### 2. Mix Bus Processing
**Scenario**: Adding analog character to a mix
- Set ANALOG_MODE to **MEDIUM** for VCA console character
- Enable **OVERSAMPLE** for smoother harmonic generation
- Apply shelf boosts: **B1 LF Shelf** (+2dB at 80Hz), **B4 HF Shelf** (+3dB at 10kHz)
- Use **Band 2** for slight low-mid scoop (-2dB at 400Hz, Q=1.5)
- Drive signal moderately to engage VCA saturation

### 3. Creative Sound Design
**Scenario**: Shaping synthesizer timbres
- Set ANALOG_MODE to **FULL** for maximum harmonic richness
- Use **Band 1 HPF mode** to remove unwanted low end
- Boost **Band 3** (2-4kHz, Q=2.0) for presence and "bite"
- Use **Band 4 Bell mode** for air enhancement (12-16kHz)
- Experiment with extreme Q values (10+) for resonant peaks

### 4. Mastering Processing
**Scenario**: Final processing on stereo mix
- Set ANALOG_MODE to **LIGHT** for subtle harmonic addition
- Disable **OVERSAMPLE** to preserve transients
- Apply broad shelf curves: **B1** (+1dB at 60Hz), **B4** (+2dB at 8kHz)
- Use **Band 2/Band 3** for midrange contouring (Q=0.7-1.0)
- Monitor clipping LED - keep signal in green/amber range

### 5. Vocal Processing
**Scenario**: Processing lead vocal tracks
- Set ANALOG_MODE to **LIGHT** or **MEDIUM**
- Use **Band 1 HPF mode** at 80-100Hz to remove rumble
- Apply **Band 2** boost (2-4dB at 300-500Hz) for body
- Use **Band 3** for clarity boost (3-5dB at 3-5kHz, Q=1.5-2.0)
- Apply **Band 4 Shelf** for air (+2dB at 8kHz)

## Performance Characteristics

### CPU Usage
- **Base Processing**: ~2-3% CPU (44.1kHz, bypass OFF, no oversampling)
- **With Oversampling (2x)**: ~4-5% CPU
- **With Oversampling (4x)**: ~6-8% CPU
- **Spectrum Analyzer**: ~0.5-1% CPU (worker thread)
- **Total (all features)**: ~8-10% CPU maximum

### Latency
- **Base Latency**: <1 sample (biquad filters are IIR, near-instantaneous)
- **With 2x Oversampling**: +2048 samples internal buffering
- **With 4x Oversampling**: +4096 samples internal buffering
- **Spectrum Display**: 2048-sample analysis window (no audio latency)

### Memory Usage
- **Module Instance**: ~150KB per instance
- **FFT Buffers**: ~32KB (spectrum analyzer)
- **Total**: ~200KB per C1-EQ module

### Thread Count
- **Audio Thread**: 1 (VCV Rack engine thread)
- **Worker Thread**: 1 (spectrum analyzer FFT computation)
- **Total**: 2 threads per C1-EQ instance

## Design Rationale

### 1. Fixed Q on LF/HF Bands
Bands 1 and 4 use hardcoded Q values (Band 1: Q=0.8, Band 4: Q=1.0) when in Bell mode because:
- Simplifies Console 1 MK2 hardware mapping (limited encoder count)
- Provides consistent, predictable behavior for shelving filters
- Most users adjust LF/HF with shelves rather than parametric bells
- Reduces cognitive load (fewer parameters to manage)

### 2. SIMD Processing
Float_4 SIMD processing provides:
- 2× throughput for stereo processing (L/R computed in parallel)
- Cache-coherent memory access patterns
- Native ARM64 NEON instruction utilization
- Minimal code complexity increase

### 3. Worker Thread for FFT
Spectrum analysis runs on dedicated thread because:
- FFT computation is CPU-intensive (~1-2ms at 44.1kHz)
- Keeps audio thread responsive and artifact-free
- Allows spectrum display updates without blocking audio processing
- Mutex-protected data transfer ensures thread safety

### 4. Analog Character Modes
Four modes provide progressive harmonic enhancement:
- TRANSPARENT: Phase-linear precision without coloration
- LIGHT: Subtle harmonic addition
- MEDIUM: Console saturation
- FULL: Saturation for sound design

### 5. Logarithmic Frequency Scaling
All frequency parameters use log2 scaling because:
- Matches human perception (octaves, not Hz)
- Provides fine control at low frequencies
- Coarse control at high frequencies
- Standard practice in EQ design

### 6. Proportional Q Algorithm
Dynamic Q adjustment maintains consistent bandwidth across gain changes:
- Wide boosts/cuts don't become uncontrollably narrow at extreme settings
- Mimics behavior of analog EQs (transformer-coupled designs)
- Provides predictable response
- Formula: `Qeff = Q × (1.0 + 0.02 × abs(gain))`

### 7. 128-Band Spectrum Display
Display band count balances:
- Resolution for visual feedback (128 bins covers 20Hz-22kHz)
- FFT computation (2048-sample window)
- Smooth logarithmic frequency mapping
- CPU overhead for rendering

### 8. Oversampling Toggle
User-selectable oversampling:
- Enables processing when analog modes are used
- Can be disabled to save CPU (~50% reduction)
- Sample-rate-adaptive filters for anti-aliasing
- Beneficial for FULL analog mode (high harmonic content)

---


**Document Version**: RC1</br>
**Project Inception**: 21-09-2025</br>
**Author**: Latif Karoumi</br>

Copyright © 2025 Twisted Cable. Licensed under GPL-3.0-or-later.
