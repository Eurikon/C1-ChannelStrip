# Shape Module - Technical Documentation RC1

## Overview

Shape is a noise gate module for VCV Rack that provides dynamic control through envelope-based gating. The module features a time-domain waveform display, multi-algorithm release curves, and sidechain input capability.

## Architecture

### Core DSP Engine

The Shape module is built around the `ShapeGateDSP` class, which implements a gate algorithm with the following characteristics:

- **Sample-rate independent operation**: All timing calculations use `APP->engine->getSampleRate()` to ensure compatibility across different sample rates (44.1kHz to 192kHz+)
- **Dual-channel processing**: Independent left and right channel gate processors
- **Envelope follower**: User-configurable attack (0.1ms to 25ms, default 0.1ms) with user-controllable release curves
- **Hold/sustain logic**: Prevents gate chatter with configurable sustain periods

**Attack Time Configuration**:
- **Range**: 0.1ms to 25ms
- **Default**: 0.1ms (fast attack for transients)
- **Access**: Right-click context menu
- **Storage**: Saved with patch via JSON serialization
- **Use Case**: Slower attack (10-25ms) for smooth gating, fast attack (0.1-1ms) for percussion

### Signal Processing Features

#### 1. Threshold Processing
- **Range**: -60dB to 0dB
- **Voltage mapping**: Dynamically maps dB range to practical VCV Rack voltages
- **Reference options**:
  - 5V Reference (subtle): Maps -60dB to 0dB → 0V to 4V range
  - 10V Reference (prominent): Maps -60dB to 0dB → 0V to 8V range
- **Calibration**: Based on real VCV Rack signal levels where typical kicks peak at ~5V

#### 2. Gate Modes
- **Soft Gate**: Gradual reduction using squared curve transition (ratio²)
- **Hard Gate**: Complete signal cutoff below threshold
- **Sustain Hold**: Prevents gate oscillation during threshold crossing

#### 3. Release Curves
Six different release characteristics for various applications:

1. **Linear (Default)**: Standard -2.2dB curve
2. **Exponential**: Faster release (-4.6dB curve) for punchy material
3. **Logarithmic**: Slower release (-1.1dB curve) for smooth transitions
4. **SSL G-Series**: Emulates classic SSL console behavior (-1.5dB curve)
5. **DBX 160X**: Emulates vintage DBX compressor release (-5.0dB curve)
6. **Drawmer DS201**: Emulates Drawmer gate/expander release (-1.0dB curve)

#### 4. Punch Enhancement
- **Range**: 0-100%
- **Function**: Adds transient emphasis during gate opening
- **Implementation**: Multiplies target gain by (1 + punchAmount) during attack phase
- **Decay Time**: 15ms exponential decay for transient boost

#### 5. Sidechain Processing
- **External Key Input**: When sidechain input has active channels (>0), uses external signal for envelope detection
- **Normal Mode**: When no sidechain or 0-channel cable, each gate uses its own input signal
- **Channel Detection**: Uses `getChannels()` to detect active sidechain signal
- **Implementation**: Separate `processSampleWithKey()` method applies gate gain to audio input based on external key envelope

### Time-Domain Waveform Display

#### Technical Specifications
- **Display Type**: Scrolling time-domain waveform showing amplitude envelope
- **Buffer Storage**: Min/max sample pairs in circular buffer
- **Time Windows**: 4 selectable time scales
  - Beat: 100ms (1024 samples, decimation 5)
  - Envelope: 1s (2048 samples, decimation 23)
  - Rhythm: 2s (2048 samples, decimation 47)
  - Measure: 4s (4096 samples, decimation 47)
- **Update Method**: Continuous sample decimation with min/max tracking
- **Display Range**: Normalized ±1.0 range from VCV Rack ±5V signals

#### Implementation Details
- **Circular Buffer**: Variable-size buffer (1024-4096 samples) with newest_sample pointer
- **Decimation**: Collects min/max over decimation period for peak-preserving downsampling
- **Dual Contour Drawing**: Top contour (min values) and bottom contour (max values) form filled polygon
- **Smooth Scrolling**: Sub-pixel interpolation based on elapsed time and sample rate
- **Scroll Speed**: `decimation * 60.0 / sampleRate` pixels per second
- **Fade Animation**: 300ms fade-out when signal stops

#### Interactive Features
- **Display Toggle**: Clickable switch in upper-right corner enables/disables waveform display
- **Time Window Selection**: 4 clickable boxes select time scale (Beat/Envelope/Rhythm/Measure)
- **Hover Feedback**: Switch opacity transitions from 50% to 100% on hover
- **Visual States**: Amber fill when enabled, X mark when disabled

### VU Metering System

#### Processing Intensity Algorithm
The VU meter displays real-time processing intensity calculated from three factors:

1. **Level Difference** (40% weight): `|outputDb - inputDb| / 30dB`
2. **Crest Factor Change** (30% weight): `|outputCrest - inputCrest| / 2.0`
3. **Dynamic Difference** (30% weight): Historical level difference over 128 samples

#### Display Characteristics
- **11 LEDs**: From minimum (-) to maximum (+) processing
- **Smoothed Response**: 128-sample history buffer with 70% historical weighting
- **Range Mapping**: 0.0 to 1.0 processing intensity mapped to 11 discrete levels

### User Interface

#### Typography Integration
- **Title Font**: Sono Bold 18pt with black outline for contrast
- **Label Font**: Sono Medium 10pt with black outline for clarity
- **Styling**: White text on dark background with 0.5px black outline

#### Control Layout
- **Left Column**: Gate threshold encoder and hard gate button
- **Right Column**: Release, sustain, and punch encoders
- **I/O Section**: Stereo inputs/outputs plus sidechain input
- **Visual Feedback**: Bypass and hard gate LED indicators with 65% brightness

#### Interactive Elements
- **Custom Round Buttons**: `C1WhiteRoundButton` toggle switches with amber LED indication
- **LED Ring Encoders**: `C1Knob280` with 15-LED rings (280° rotation, 80° bottom gap)
- **Themed I/O Jacks**: `ThemedPJ301MPort` for all inputs and outputs
- **Dynamic Button Background**: Dull ivory when off, dark gray when lit

## Signal Flow

```
Input → Envelope Follower → Gate Logic → Sustain Hold → Punch Enhancement → Output
   ↓                                                                           ↓
Waveform Buffer ← ← ← ← ← ← ← ← ← ← ← ← ← ← ← ← ← ← ← ← ← ← ← ← ← ← ← ← ←
   ↓
Time-Domain Display

Processing Intensity Calculation:
Input RMS/Peak → Compare → Level/Crest Analysis → VU Meter Display
Output RMS/Peak ↗

Sidechain Mode (when SC input has channels > 0):
Sidechain Input → Envelope Follower → Gate Logic → Apply to Audio Input → Output
Audio Input ────────────────────────────────────────────────────────────────↗
```

## Parameter Specifications

| Parameter | Range | Default | Unit | Description |
|-----------|-------|---------|------|-------------|
| Threshold | -60 to 0 | -60 | dB | Gate opening threshold |
| Release | 0.1 to 4 | 0.1 | seconds | Gate close time |
| Sustain | 0 to 300 | 0 | ms | Hold time after threshold crossing |
| Punch | 0 to 100 | 0 | % | Transient enhancement amount |
| Hard Gate | Off/On | Off | boolean | Soft vs hard gating mode |
| Bypass | Off/On | Off | boolean | True bypass mode |
| Display Enable | Off/On | On | boolean | Waveform display visibility |

## Technical Implementation Notes

### Sample Rate Handling
Critical design principle: **Never hardcode sample rates**. All timing calculations use:
```cpp
float sr = APP->engine->getSampleRate();
coefficient = std::exp(-2.2f / (timeMs * sr / 1000.0f));
```

### Thread Safety
- **Audio Thread**: All DSP processing in `process()` method
- **UI Thread**: All visual updates and waveform drawing
- **Atomic Pointers**: `std::atomic<GateWaveformWidget*>` for thread-safe widget access
- **Shutdown Flag**: `std::atomic<bool> isShuttingDown` prevents crashes during module deletion

### Waveform Display Memory
- **Storage**: `std::vector<std::pair<float, float>>` for min/max pairs
- **Maximum Buffer**: 4096 samples to accommodate all time windows
- **Atomic Index**: `std::atomic<int> newest_sample` for thread-safe circular buffer access
- **Decimation Counter**: Tracks samples between buffer updates based on time window

### Envelope Coefficients
All release curves use exponential coefficient calculation:
```cpp
releaseCoeff = std::exp(-decayDb / (releaseTimeMs * sampleRate / 1000.0f));
```

Decay values by curve type:
- Linear: -2.2 dB
- Exponential: -4.6 dB
- Logarithmic: -1.1 dB
- SSL G-Series: -1.5 dB
- DBX 160X: -5.0 dB
- Drawmer DS201: -1.0 dB

## Use Cases

### 1. Drum Gate
- **Settings**: Hard gate, fast release (0.1-0.5s), minimal sustain
- **Purpose**: Clean up drum tracks, remove bleed
- **Curve**: DBX 160X for vintage punch

### 2. Vocal De-noise
- **Settings**: Soft gate, slow release (2-4s), moderate sustain (200-500ms)
- **Purpose**: Remove background noise between phrases
- **Curve**: SSL G-Series for smooth release

### 3. Creative Gating
- **Settings**: Soft gate, punch enhancement, variable release
- **Purpose**: Add dynamics and movement to sustained material
- **Curve**: Logarithmic for smooth gating

### 4. Transient Shaping
- **Settings**: High punch (70-100%), fast release, minimal threshold
- **Purpose**: Enhance attack characteristics
- **Curve**: Exponential for aggressive transients

## Performance Characteristics

- **CPU Usage**: Single-threaded DSP with minimal overhead
- **Latency**: Zero-latency processing (feed-forward design)
- **Memory**: Variable based on time window selection (1-4KB per instance)
- **Precision**: 32-bit floating point throughout signal path

## Design Rationale

Shape implements the following design decisions:

1. **Real-world Calibration**: Threshold mapping based on actual VCV Rack signal levels rather than traditional audio standards
2. **Time-Domain Feedback**: Waveform display shows gate output over time for visual confirmation of gate behavior
3. **Multiple Release Curves**: Different curve types provide different release characteristics for various signal types
4. **Processing Intensity Display**: VU meter shows the actual amount of processing, not just input level
5. **Sample-rate Independence**: Consistent behavior across all sample rates prevents timing issues
6. **Sidechain Flexibility**: External key input enables ducking and rhythmic gating effects

The envelope-based gating with visual feedback supports both corrective and creative applications in modular synthesis and music production.

## CV Expander Integration (SH-X)

### SH-X CV Expander Module
Shape supports CV modulation via the SH-X (SHAPE-CV) expander module:

**Expander Position**: SH-X connects to Shape's right expander port

**CV Inputs** (4 parameters):
- **Threshold CV**: ±60dB modulation range
  - Input range: -1.0 to +1.0 (attenuated CV)
  - Maps to full parameter range: -60dB to 0dB
  - Applied additively to THRESHOLD knob position
  - Final threshold clamped to -60dB to 0dB
- **Sustain CV**: 0-300ms modulation range
  - Input range: -1.0 to +1.0 (attenuated CV)
  - Maps to full parameter range: 0ms to 300ms
  - Applied additively to SUSTAIN knob position
  - Final sustain clamped to 0ms to 300ms
- **Release CV**: 0.1s-4s modulation range
  - Input range: -1.0 to +1.0 (attenuated CV)
  - Maps to full parameter range: 0.1s to 4s
  - Applied additively to RELEASE knob position
  - Final release clamped to 0.1s to 4s
- **Hard Gate CV**: Gate input (>1V = hard gate mode)
  - Logical OR with HARD GATE button
  - Both button and CV can trigger hard gate mode

**Expander Message Protocol**:
- Thread-safe double-buffered messaging
- Message struct: `ShapeExpanderMessage` with 4 float fields
- Update rate: Every audio sample (no decimation)
- Automatic detection via `modelShapeCV` slug matching

**CV Behavior**:
- All CVs use linear scaling with additive modulation
- CV modulation applied before gate processing
- All CV inputs respect parameter range limits
- Smoothing applied via 20-sample low-pass filter for stable operation

---

**Document Version**: RC1</br>
**Project Inception**: 21-09-2025</br>
**Author**: Latif Karoumi</br>

Copyright © 2025 Twisted Cable. Licensed under GPL-3.0-or-later.
