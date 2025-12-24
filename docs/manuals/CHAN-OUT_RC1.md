# CHAN-OUT RC1 - Output Channel Module

## Overview

ChanOut (CHAN-OUT) is an output module for VCV Rack, designed as the final stage in the C1-ChannelStrip series. It provides output processing with metering, four character engines for analog coloration, and dual operating modes (Master/Channel) for different mixing scenarios.

## What It Is

ChanOut is an output processing module that handles the final stage of the signal chain:

- **Dual Operating Modes**: Master mode (linear pan, -60→0dB) for mix bus, Channel mode (equal power pan, -60→+6dB) for individual tracks
- **Character Engines**: Four distinct analog colorations (Standard, 2520, 8816, DM2+)
- **Metering**: Dual 17-LED VU meters, LUFS loudness monitoring, Goniometer display
- **Monitoring Features**: Dim (-30 to -1dB), Mute with anti-pop, Trim control
- **Console 1 Integration**: Hardware control with real-time VU feedback

## What It Does

### Signal Processing Chain

The audio flows through the following signal path:

1. **Input Stage**: Accepts stereo or mono signals (mono is automatically normalized to both channels)
2. **Pan Processing**: Dual-mode panning (linear for Master, equal power for Channel)
3. **Volume Control**: Mode-dependent ranges with anti-pop smoothing
4. **Character Engine**: Four distinct analog colorations with 8× oversampling
5. **Dim/Mute Processing**: Monitoring controls with fast slewing
6. **Output Stage**: Processed signal with visual monitoring

### Key Functions

- **Dual Operating Modes**: Master mode for mix bus, Channel mode for individual tracks
- **Pan Control**: Linear pan (Master) or equal power pan (Channel)
- **Volume Control**: -60dB to 0dB (Master) or -60dB to +6dB (Channel)
- **Character Drive**: 0% to 100% blend for each engine type
- **Dim**: -30dB to -1dB attenuation for monitoring
- **Mute**: True mute with anti-pop slewing
- **Trim**: Fine gain adjustment

### Metering Systems

- **VU Meters**: Dual 17-LED bars showing post-pan, post-volume, pre-clipping levels
- **LUFS**: EBU R128 compliant loudness monitoring with 2048-sample decimation
- **Goniometer**: Real-time L/R correlation visualization using ring buffer

## Why It Does What It Does

### Design Philosophy

ChanOut addresses output processing requirements in modular environments:

1. **Dual Mode Flexibility**: Master mode for mix bus (linear pan, 0dB max), Channel mode for tracks (equal power pan, +6dB headroom)
2. **Character Options**: Four distinct analog colorations provide tonal options without requiring multiple modules
3. **Metering**: VU (dynamics), LUFS (loudness), and Goniometer (stereo field) provide monitoring capabilities
4. **Compact Layout**: Controls grouped in 8HP format
5. **Hardware Integration**: Console 1 MK2 control with bidirectional VU feedback

### Problem Solving

- **Mix Bus Processing**: Master mode with linear pan and 0dB maximum prevents digital clipping
- **Individual Track Processing**: Channel mode with equal power pan and +6dB headroom provides proper gain staging
- **Analog Coloration**: Four character engines provide tonal shaping without external processing
- **Monitoring Controls**: Dim and Mute controls enable monitoring workflows
- **Loudness Compliance**: LUFS metering ensures broadcast/streaming compliance

## How It Works

### Dual Operating Mode Architecture

CHAN-OUT implements two distinct processing modes selected via parameter:

**Master Mode** (default):
- **Pan Law**: Linear (mono-compatible)
- **Volume Range**: -60dB to 0dB
- **Use Case**: Mix bus, submix groups, final output
- **Behavior**: Linear panning maintains constant mono sum, 0dB maximum prevents clipping

**Channel Mode**:
- **Pan Law**: Equal power (constant perceived level)
- **Volume Range**: -60dB to +6dB
- **Use Case**: Individual tracks, channel inserts
- **Behavior**: Equal power panning maintains constant perceived loudness, +6dB headroom for gain makeup

### Character Engine Architecture

ChanOut features four distinct analog coloration engines, each with unique harmonic characteristics:

#### 1. Standard (Clean)
- **Type**: MindMeld soft-clipping with polynomial waveshaper
- **Implementation**: `ChanOutCleanEngine.hpp` (135 lines)
- **Characteristics**: Transparent processing with gentle saturation at high levels
- **Harmonic Content**: Minimal even-order harmonics
- **Oversampling**: Optional 8× for alias-free processing
- **Use Case**: Clean output with subtle warmth

**Technical Details**:
```cpp
// Polynomial soft-clipper
float softClip = x - (x³ / 3.0f);
// Hyperbolic tangent saturation
output = std::tanh(input * drive);
```

#### 2. 2520 (API Discrete Op-Amp)
- **Type**: Asymmetric polynomial waveshaping with feedback loop
- **Implementation**: `ChanOutAPIEngine.hpp` (365 lines)
- **Characteristics**: Aggressive even-order harmonics, punchy transients
- **Harmonic Content**: Strong 2nd harmonic, moderate 3rd harmonic
- **Oversampling**: Mandatory 8× polyphase SIMD-optimized
- **Use Case**: API-style punch and clarity

**Technical Details**:
```cpp
// Asymmetric waveshaping (positive/negative handled differently)
if (x > 0.0f) {
    shaped = x * (1.0f + a₁*x + a₂*x²);
} else {
    shaped = x * (1.0f + b₁*x + b₂*x²);
}
// Feedback loop for harmonic enhancement
output = shaped + (feedback * 0.15f);
// Output limiting with soft knee
limited = limitWithKnee(output, 1.0f, 0.1f);
```

**Oversampling Architecture**:
- SIMD processing: `dsp::TBiquadFilter<float_4>` for stereo pairs
- Polyphase FIR filters for optimal frequency response
- 8× oversampling default (configurable 1x/2x/4x/8x)

#### 3. 8816 (Neve Transformer + Silk)
- **Type**: Parallel blend with transformer simulation and Silk Red/Blue filters
- **Implementation**: `ChanOutNeveEngine.hpp` (513 lines)
- **Characteristics**: Warm, thick midrange with smooth high-frequency roll-off
- **Harmonic Content**: Complex even/odd harmonics, flux integration artifacts
- **Oversampling**: 8× polyphase with parallel dry path
- **Use Case**: Neve-style warmth and weight

**Technical Details**:
```cpp
// Transformer core saturation model
float fluxIntegration = integratedSignal / sampleRate;
float saturation = std::tanh(fluxIntegration * satDrive);

// Silk Red filter (HF boost around 10kHz)
float silkRed = highShelfFilter(input, 10000.0f, +1.5dB);

// Silk Blue filter (HF smooth roll-off above 15kHz)
float silkBlue = lowPassFilter(input, 15000.0f, Q=0.707);

// Parallel blend architecture
float wet = transformer(saturated(input));
float dry = input;
output = (wet * blend) + (dry * (1.0f - blend));
```

**Flux Integration**:
- Simulates transformer core saturation
- Time-domain integration creates low-frequency compression
- Produces characteristic Neve "weight" and punch

#### 4. DM2+ (Dangerous Music Master Link)
- **Type**: Three parallel circuits with independent blend control
- **Implementation**: `ChanOutDangerousEngine.hpp` (588 lines)
- **Characteristics**: Transparent enhancement with optional saturation, parallel limiting, and transformer color
- **Harmonic Content**: Configurable via circuit blend ratios
- **Oversampling**: 8× for all three parallel paths
- **Use Case**: Enhancement with circuit blend control

**Technical Details**:
```cpp
// Circuit 1: Harmonics (polynomial waveshaping)
float harmonics = a₁*x + a₃*x³ + a₅*x⁵;

// Circuit 2: Paralimit (parallel soft limiting)
float limited = softLimit(input, threshold, ratio);
float paralimit = (input * dry) + (limited * wet);

// Circuit 3: X-Former (transformer simulation)
float xformer = transformerModel(input, saturation, hysteresis);

// Master blend (equal-loudness mixing)
output = (harmonics * 0.33f) + (paralimit * 0.33f) + (xformer * 0.34f);
```

**Parallel Circuit Architecture**:
- Independent processing paths run simultaneously
- Equal-loudness blending prevents level jumps
- Master drive control scales all three circuits
- Each circuit optimized for specific enhancement type

### Anti-Pop Slewing System

CHAN-OUT implements two-tier slew limiting for artifact-free parameter changes:

**Slow Parameters** (Volume, Pan):
```cpp
static constexpr float SLOW_SLEW_RATE = 10.0f;  // units/second
float smoothedVolume = volumeSlewer.process(sampleTime, targetVolume);
float smoothedPan = panSlewer.process(sampleTime, targetPan);
```

**Fast Parameters** (Dim, Mute):
```cpp
static constexpr float FAST_SLEW_RATE = 125.0f;  // units/second
float smoothedDim = dimSlewer.process(sampleTime, targetDim);
float smoothedMute = muteSlewer.process(sampleTime, targetMute);
```

**Design Rationale**:
- Volume/Pan: Slower slewing (10 units/sec) prevents audible artifacts during mix automation
- Dim/Mute: Faster slewing (125 units/sec) provides immediate monitoring response
- All changes are click-free and sample-accurate

### VU Metering System

**17-LED Dual Metering** (post-pan, post-volume, pre-clipping):

**LED Mapping** (Custom Console 1 MK2 exact scaling):
- LED 0: -60dB (green, bottom)
- LED 1: -51dB (green)
- LED 2: -42dB (green)
- LED 3: -33dB (green)
- LED 4: -24dB (green, labeled)
- LED 5: -20dB (green)
- LED 6: -16dB (green)
- LED 7: -12dB (green, labeled)
- LED 8: -10dB (green)
- LED 9: -8dB (green)
- LED 10: -6dB (green, labeled, last green)
- LED 11: -4dB (yellow, first yellow)
- LED 12: -2dB (yellow)
- LED 13: 0dB (yellow, labeled, last yellow)
- LED 14: +2dB (red, first red)
- LED 15: +4dB (red)
- LED 16: +6dB (red, labeled, top)

**Ballistics**:
- **Attack**: Instantaneous (0ms) for accurate peak capture
- **Decay**: 300ms exponential for smooth visual response
- **Peak Hold**: 0.5 second hold with gentle decay
- **Update Rate**: Sample-accurate

**Console 1 MK2 Feedback**:
- **CC 112**: Left channel VU (0-16 LED index)
- **CC 113**: Right channel VU (0-16 LED index)
- Bidirectional: Hardware LED rings track software meters in real-time

### LUFS Loudness Metering

**EBU R128 / ITU-R BS.1770-4 Compliant**:
- **Standard**: Momentary loudness (Mode M, 400ms window)
- **Buffer Size**: 2048 samples (~46ms at 44.1kHz)
- **CPU Optimization**: Batched processing reduces calls from ~689/sec (64) to ~21/sec (2048)
- **Performance**: ~1.6% CPU (vs ~14% with 64-sample buffer)
- **Voltage Scaling**: VCV ±5V → libebur128 ±1.0f (multiply by 0.2f)
- **Sample Rate Adaptive**: Automatically adjusts to engine sample rate

**Gating**:
- Absolute gate: -70 LUFS
- Relative gate: -10 LUFS
- Prevents noise floor from affecting measurement

### Goniometer Display

**Real-Time L/R Correlation Visualization**:
- **Implementation**: `dsp::RingBuffer<Vec, 512>` for history tracking
- **X-Axis**: Left channel (-5V to +5V)
- **Y-Axis**: Right channel (-5V to +5V)
- **Display Scaling**: 95% width/height for optimal viewing area
- **Point Rendering**: 0.5px radius dots with age-based alpha fade (80-180)
- **Update Rate**: Every 4 samples (decimated for visual clarity)

**Interpretation**:
- **Vertical Line**: Mono (L=R)
- **Horizontal Line**: Anti-phase (L=-R)
- **Diagonal Line (45°)**: Full left or right pan
- **Circle/Ellipse**: Stereo width indication
- **Lissajous Figures**: Frequency/phase relationships

### Parameter Ranges

| Parameter | Range | Mode-Dependent | Purpose |
|-----------|-------|----------------|---------|
| Volume | -60dB to 0dB (Master)<br>-60dB to +6dB (Channel) | Yes | Level control with mode-appropriate headroom |
| Pan | -100% (L) to +100% (R) | Pan law varies | Stereo positioning (linear or equal power) |
| Character | Standard / 2520 / 8816 / DM2+ | No | Analog coloration engine selection |
| Drive | 0% to 100% | No | Character engine blend amount |
| Dim | -30dB to -1dB | No | Monitoring attenuation |
| Mute | Off/On | No | True mute with anti-pop |
| Display Enable | Off/On | No | Show/hide metering displays |

## Console 1 Integration

### Hardware Control Mapping

ChanOut is fully integrated with the Control1 (C1) module for Console 1 MK2 hardware control:

**Parameter Mapping** (6 parameters, position 4 in chain):
- **CC 15**: CHARACTER (engine select) - Discrete mode switch (0/63/127/191)
- **CC 18**: DRIVE (0% to 100%) - Linear scaling
- **CC 10**: PAN (-100% to +100%) - Linear scaling
- **CC 7**: VOLUME - Mode-dependent ranges
  - Master: -60dB to 0dB
  - Channel: -60dB to +6dB
- **CC 13**: DIM_BUTTON (toggle) - Discrete on/off
- **CC 12**: MUTE_BUTTON (toggle) - Discrete on/off

**VU Meter Feedback** (bidirectional):
- **CC 112**: Left channel VU meter (0-16 LED index)
- **CC 113**: Right channel VU meter (0-16 LED index)
- Real-time LED ring updates on Console 1 hardware
- Reads highest active LED from 17-segment VU meter arrays

**Display Control**:
- **CC 102**: DisplayControl toggles visibility when module is hovered
- Synchronized across all C1-ChannelStrip modules

**Module Detection**:
- Position 4 in C1 expander chain
- Automatic detection via "CHAN-OUT" slug matching
- Green LED indicates successful detection
- Red LED blinks if wrong module at position 4

**Parameter Sync**:
- One-time burst sync on module connection
- Continuous bidirectional feedback during operation
- Hardware LED rings track parameter changes in real-time

### Signal Flow in C1-ChannelStrip Chain

```
Audio Source → CHAN-IN (Position 0) → Shape (Position 1) → C1-EQ (Position 2)
→ C1-COMP (Position 3) → CHAN-OUT (Position 4) → [Optional: C1-ORDER (Position 5)]
```

## Signal Flow

```
Input (Stereo/Mono)
    ↓
Mono Normalization (if only left connected)
    ↓
Pan Processing (mode-dependent law)
    ↓
Volume Control (mode-dependent range)
    ↓
Character Engine Selection ──→ Standard (Clean soft-clip)
                           ├──→ 2520 (API discrete op-amp)
                           ├──→ 8816 (Neve transformer + Silk)
                           └──→ DM2+ (Dangerous 3-circuit blend)
    ↓
Drive Blend (dry/wet mix)
    ↓
Dim/Mute Processing (fast slewing)
    ↓
Output (±5V standard)

Metering Path:
Post-Pan/Volume → VU Meters (17 LEDs × 2)
                → LUFS Meter (EBU R128, 2048-sample batch)
                → Goniometer (512-sample ring buffer)
                → Console 1 VU Feedback (CC 112/113)
```

## Parameter Specifications

| Parameter | Range | Default | Unit | Description |
|-----------|-------|---------|------|-------------|
| Volume | -60 to 0 (Master)<br>-60 to +6 (Channel) | 0 | dB | Output level control |
| Pan | -100 to +100 | 0 | % | Stereo positioning |
| Character | 0 to 100 | 50 | % | Character engine blend amount |
| Drive | 0 to 100 | 0 | % | Character engine drive intensity |
| Dim | Off/On | Off | toggle | Monitoring attenuation (toggle button) |
| Mute | Off/On | Off | toggle | True mute with anti-pop |
| Display Enable | Off/On | On | toggle | Meter display visibility |

**Note**: Character Engine selection (Standard/2520/8816/DM2+) is performed via GUI widget or context menu, not via the Character parameter knob.

## Technical Implementation Notes

### Sample Rate Handling

All timing calculations use dynamic sample rate:
```cpp
float sr = APP->engine->getSampleRate();
float sampleTime = 1.0f / sr;

// Slew limiter update
smoothedValue = slewer.process(sampleTime, targetValue);
```

### Thread Safety

- **Audio Thread**: All DSP processing in `process()` method
- **UI Thread**: All visual updates and meter drawing
- **Atomic Pointers**: `std::atomic<WidgetType*>` for thread-safe widget access
- **Shutdown Flag**: `std::atomic<bool> isShuttingDown` prevents crashes during module deletion

### Pan Law Implementation

**Master Mode - Linear Pan** (mono-compatible):
```cpp
float panNorm = (pan + 1.0f) / 2.0f;  // -1→+1 becomes 0→1
leftGain = 1.0f - panNorm;            // 1→0
rightGain = panNorm;                  // 0→1
// Sum of gains = 1.0 (constant mono sum)
```

**Channel Mode - Equal Power Pan** (constant perceived level):
```cpp
float panAngle = panNorm * M_PI / 2.0f;  // 0→1 becomes 0→π/2
leftGain = std::cos(panAngle);           // 1→0 (cosine taper)
rightGain = std::sin(panAngle);          // 0→1 (sine taper)
// Sum of squared gains = 1.0 (constant power)
```

### Oversampling Implementation

**8× Polyphase SIMD-Optimized**:
```cpp
// Upsample 1× → 8×
upsampler.process(input, upsampledBuffer);

// Process at 8× sample rate (SIMD stereo pairs)
for (int i = 0; i < 8; i++) {
    float_4 samples = float_4(upsampled[i*2], upsampled[i*2+1], 0.f, 0.f);
    float_4 processed = characterEngine.process(samples);
    processedBuffer[i*2] = processed[0];
    processedBuffer[i*2+1] = processed[1];
}

// Downsample 8× → 1×
output = downsampler.process(processedBuffer);
```

**Filter Characteristics**:
- Polyphase FIR design for phase distortion reduction
- >96dB stopband attenuation
- Linear phase response in passband

### VU Meter LED Calculation

```cpp
// Convert voltage to dB (5V reference = 0dB)
float dbValue = 20.0f * std::log10(std::abs(voltage) / 5.0f);

// Custom Console 1 MK2 LED thresholds (exact scaling)
static const float ledThresholds[17] = {
    -60.0f, -51.0f, -42.0f, -33.0f, -24.0f, -20.0f, -16.0f, -12.0f,
    -10.0f, -8.0f, -6.0f, -4.0f, -2.0f, 0.0f, +2.0f, +4.0f, +6.0f
};

// Map dB to LED index (0-16) using custom thresholds
int ledIndex = -1;
for (int i = 16; i >= 0; i--) {
    if (dbValue >= ledThresholds[i]) {
        ledIndex = i;
        break;
    }
}

// Set LED brightness (bar graph display)
for (int i = 0; i <= 16; i++) {
    lights[VU_LIGHTS_LEFT + i].setBrightness(
        (i <= ledIndex) ? 1.0f : 0.0f
    );
}
```

### LUFS Decimation Optimization

```cpp
static constexpr int LUFS_DECIMATION = 2048;  // MANDATORY

// Accumulate samples in buffer
lufsBuffer[lufsBufferIndex++] = leftSample;
lufsBuffer[lufsBufferIndex++] = rightSample;

// Process batch when full
if (lufsBufferIndex >= LUFS_DECIMATION * 2) {
    ebur128_add_frames_float(ebur128State, lufsBuffer.data(), LUFS_DECIMATION);
    double loudness;
    ebur128_loudness_momentary(ebur128State, &loudness);
    currentLufs = (float)loudness;
    lufsBufferIndex = 0;
}
```

**Why 2048 samples**:
- Reduces libebur128 calls from ~689/sec (64) to ~21/sec (2048)
- Function call overhead is expensive
- K-weighting filters operate on entire buffer
- CPU usage: ~1.6% vs ~14% with 64-sample buffer

## Use Cases

### 1. Mix Bus Output
- **Mode**: Master
- **Settings**: Standard character, 0% drive, center pan, -6dB volume
- **Purpose**: Final output with metering
- **LUFS Target**: -14 LUFS (streaming), -16 LUFS (CD)
- **Metering**: VU for dynamics, LUFS for loudness compliance

### 2. Analog Mix Bus Coloration
- **Mode**: Master
- **Settings**: 2520 or 8816 character, 30-60% drive, center pan
- **Purpose**: Add analog warmth and glue to mix bus
- **Character**: 2520 for punch/clarity, 8816 for warmth/weight
- **LUFS**: Monitor to ensure coloration doesn't affect loudness target

### 3. Individual Track Output
- **Mode**: Channel
- **Settings**: Any character, variable drive, variable pan, -6dB to 0dB volume
- **Purpose**: Process individual tracks with gain staging headroom
- **Pan**: Equal power law maintains constant perceived level
- **Headroom**: +6dB maximum allows gain makeup after compression

### 4. Enhancement Processing
- **Mode**: Master
- **Settings**: DM2+ character, 20-40% drive, center pan
- **Purpose**: Transparent enhancement
- **DM2+ Circuits**: Harmonics + Paralimit + X-Former for circuit blend control
- **Goniometer**: Monitor stereo field and phase correlation

### 5. Monitoring Workflow
- **Setup**: Route mix bus through CHAN-OUT
- **Dim**: Assign Console 1 MK2 button for quick -20dB monitoring
- **Mute**: Instant silence with anti-pop slewing
- **VU Meters**: Track mix dynamics in real-time
- **Console 1 Integration**: Hardware LED rings show levels hands-free

## Performance Characteristics

- **CPU Usage**: Low to moderate depending on oversampling and character engine
  - Standard (no oversampling): ~0.5% CPU
  - 2520/8816/DM2+ (8× oversampling): ~2-4% CPU per instance
  - LUFS metering: ~1.6% CPU (2048-sample decimation)
- **Latency**: Zero-latency processing (feed-forward design)
- **Memory**: Static allocation, approximately 10KB per instance
- **Precision**: 32-bit floating point throughout signal path
- **Polyphony**: Stereo processing with independent L/R channels

## Design Rationale

CHAN-OUT implements the following design decisions:

1. **Dual Operating Modes**: Master mode for mix bus (linear pan, 0dB max), Channel mode for tracks (equal power pan, +6dB headroom)
2. **Character Engine Variety**: Four distinct analog colorations (clean, API, Neve, Dangerous) provide tonal options
3. **Metering**: VU (dynamics), LUFS (loudness), Goniometer (stereo field) provide monitoring
4. **Anti-Pop Slewing**: Two-tier slew limiting (slow for volume/pan, fast for dim/mute) prevents artifacts
5. **Oversampling**: 8× polyphase SIMD-optimized processing for alias reduction
6. **LUFS Optimization**: 2048-sample batching reduces CPU usage by 90% while maintaining accuracy
7. **Console 1 Integration**: Bidirectional VU feedback and parameter control
8. **Sample-rate Independence**: Consistent behavior across all sample rates prevents timing issues

The dual operating modes, character engine options, and metering support both mix bus processing and individual track output in modular synthesis and music production.

## Integration

### Modular Workflow

ChanOut is designed as the final module in the signal chain:

**Typical Chain**: Source → CHAN-IN → Shape → C1-EQ → C1-COMP → **CHAN-OUT**

### Expander Chain

- Must be positioned at **Position 4** after Control1 (C1) module
- Detection uses expander chain walking from Control1
- All parameters controlled via C1 module when connected

## Specifications

- **Module Width**: 8HP
- **Sample Rate**: Independent (supports all VCV Rack sample rates)
- **Audio Range**: ±5V (modular standard)
- **Frequency Response**: 20Hz to 20kHz (full audio spectrum, character-dependent)
- **Dynamic Range**: >90dB
- **VU Metering Range**: -60dB to +6dB (66dB total range, 17 LEDs per channel)
- **LUFS Range**: -70 LUFS to 0 LUFS (EBU R128 compliant)
- **Parameters**: 7 (Volume, Pan, Character, Drive, Dim, Mute, Display Enable)
- **Inputs**: 2 (Left, Right)
- **Outputs**: 2 (Left, Right)
- **Console 1 CCs**: 6 parameters + 2 VU meters

## Architecture Details

### Thread Safety

- Atomic widget pointers for cross-platform safety (ARM64, x86-64)
- Widget pointers nulled in destructor to prevent dangling references
- Safe shutdown protocol with `isShuttingDown` flag
- Proper UI/audio thread separation

### LUFS Implementation

- libebur128 library integration (EBU R128 compliant)
- Mode M (momentary, 400ms window)
- 2048-sample buffer for optimal CPU usage (~1.6% CPU vs 14% with 64 samples)
- Voltage scaling: VCV ±5V → libebur128 ±1.0f (multiply by 0.2f)
- Sample rate awareness via `APP->engine->getSampleRate()`

### Performance Optimization

- Character engine coefficient caching (only update when parameters change)
- LUFS decimation (2048-sample batching)
- Goniometer decimation (every 4 samples)
- SIMD-ready oversampling architecture (float_4 vector operations)
- Zero dynamic allocation in audio thread

## Version History

### RC1 (Release Candidate 1)

- Complete dual operating mode implementation (Master/Channel)
- Four character engines: Standard (clean), 2520 (API), 8816 (Neve), DM2+ (Dangerous)
- 8× polyphase SIMD-optimized oversampling
- Dual 17-LED VU metering (post-pan, post-volume, pre-clipping)
- LUFS loudness monitoring (EBU R128, 2048-sample decimation)
- Goniometer display (512-sample ring buffer)
- Dim/Mute monitoring controls with fast slewing (125 units/sec)
- Console 1 MK2 hardware integration with bidirectional VU feedback
- Display visibility control (CC 102)
- EURIKON aesthetic matching other C1-ChannelStrip modules
- Thread-safe widget pointer management

## CV Expander Integration (CHO-X)

CHAN-OUT supports CV modulation via the CHO-X (ChanOut-CV) expander module.

### Expander Position
CHO-X connects to CHAN-OUT's **right expander port**.

### CV Inputs (4 Parameters)

| CV Input | Range | Modulation Type | Target Parameter | Mapping |
|----------|-------|-----------------|------------------|---------|
| **Gain CV** | -1.0 to +1.0 | Additive | VOLUME_PARAM | ±66dB range (covers both Master -60/0dB and Channel -60/+6dB modes) |
| **Pan CV** | -1.0 to +1.0 | Additive | PAN_PARAM | ±100% pan range (-100% L to +100% R) |
| **Dim CV** | -1.0 to +1.0 | Additive | DIM attenuation | Full dim range modulation |
| **Character CV** | -1.0 to +1.0 | Additive | CHARACTER_PARAM | 0-100% character blend modulation |

### Expander Message Protocol

- **Thread-safe**: Double-buffered messaging via `ChanOutExpanderMessage` struct
- **Update Rate**: Every audio sample (no decimation)
- **Detection**: Automatic via `modelChanOutCV` slug matching
- **CV Behavior**: All CVs use linear scaling with additive modulation
- **Range Limiting**: Final parameter values clamped to valid ranges after CV application
- **Smoothing**: Applied via module's internal parameter slewing for artifact-free operation

### CV Modulation Formula

```cpp
finalValue = clamp(parameterValue + cvInput, minValue, maxValue)
```

Example: Gain with CV (Master mode)
```cpp
float gainCVMod = cvInput * 66.0f;  // ±66dB range
float volumeDB = clamp(params[VOLUME_PARAM].getValue() + gainCVMod, -60.0f, 6.0f);
```

Example: Pan with CV
```cpp
float panCVMod = cvInput;  // ±1.0 range
float panTarget = clamp(params[PAN_PARAM].getValue() + panCVMod, -1.0f, 1.0f);
```

---


**Document Version**: RC1</br>
**Project Inception**: 21-09-2025</br>
**Author**: Latif Karoumi</br>

Copyright © 2025 Twisted Cable. Licensed under GPL-3.0-or-later.
