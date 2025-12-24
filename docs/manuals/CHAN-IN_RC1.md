# CHAN-IN RC1 - Input Channel Module

## Overview

CHAN-IN (CHAN-IN) is an input channel module for VCV Rack, designed as the first stage in the C1-ChannelStrip series. It provides input processing, combining Befaco's filter implementations with MindMeld's anti-pop smoothing techniques.

## What It Is

CHAN-IN is an input processing module that handles the first stage of the signal chain:

- **Input Gain Control**: -60dB to +6dB range with anti-pop smoothing
- **Filtering**: Low-cut (HPF) and high-cut (LPF) filters
- **Phase Control**: Polarity inversion for phase alignment
- **Metering**: Three selectable metering modes (RMS/VU/PPM)
- **Console 1 Integration**: Hardware control with VU meter feedback

## What It Does

### Signal Processing Chain
The audio flows through the following signal path:

1. **Input Stage**: Accepts stereo or mono signals (mono is automatically normalized to both channels)
2. **VCA Processing**: Applies gain with anti-pop smoothing (-60dB to +6dB)
3. **Filter Section**: Serial processing through low-cut → high-cut filters
4. **Phase Processing**: Optional polarity inversion
5. **Output Stage**: Processed signal with visual level monitoring

### Key Functions

- **Input Gain**: Headroom management from -60dB to +6dB
- **Low Cut Filter (HPF)**: Removes low frequencies (20Hz - 500Hz)
- **High Cut Filter (LPF)**: Attenuates high frequencies (1kHz - 20kHz)
- **Phase Invert**: Polarity inversion control
- **Multi-Mode Metering**: Real-time stereo level monitoring with 4 selectable modes

## Why It Does What It Does

### Design Philosophy
CHAN-IN addresses input processing requirements in modular environments:

1. **Input Processing**: Gain control and filtering for signal conditioning
2. **Flexible Metering**: Three metering modes (RMS/VU/PPM) for different monitoring needs
3. **Compact Layout**: Controls grouped in 8HP format
4. **Visual Feedback**: Level monitoring for gain staging
5. **Hardware Integration**: Console 1 MK2 control with bidirectional feedback

### Problem Solving
- **Gain Staging**: Level management from source to mix bus
- **Frequency Control**: Removes frequencies before they affect downstream processing
- **Phase Issues**: Polarity inversion for phase correction
- **Monitoring**: Multiple metering standards for different contexts

## How It Works

### Hybrid Implementation Architecture

CHAN-IN combines techniques from two developers:

#### Befaco's Filter Implementation
- **Filter Implementation**: Uses Befaco's AeFilter biquad implementation from StereoStrip
- **Sample Rate Independence**: All timing calculations use `APP->engine->getSampleRate()`
- **Coefficient Optimization**: Parameters only recalculated when changed (16-sample update divider)
- **Mathematics**: Filter designs from repelzen

#### MindMeld's Smoothing
- **Anti-Pop Technology**: 25V/s slew limiting prevents audible artifacts
- **Gain Smoothing**: Parameter changes without clicks or pops
- **Response**: Mimics behavior of analog consoles

### Technical Implementation

#### VCA System
```cpp
class ChanInVCA {
    dsp::SlewLimiter gainSlewer;
    static constexpr float ANTIPOP_SLEW_RATE = 25.0f;  // V/s

    float processGain(float input, float gainDb, float sampleTime, float cvGain) {
        float targetGain = std::pow(10.0f, gainDb / 20.0f) * cvGain;
        float smoothedGain = gainSlewer.process(sampleTime, targetGain);
        return input * smoothedGain;
    }
};
```

#### Filter Architecture
- **Biquad Implementation**: 2nd-order IIR filters
- **Quality Factor**: Fixed Q=0.8 for musical response
- **Serial Processing**: Low-cut → High-cut signal path
- **Sample Rate Adaptive**: Coefficients automatically adjust to engine sample rate
- **Intelligent Bypass**: Filters conditionally bypassed for CPU efficiency
  - HPF bypassed when frequency ≤20Hz (at minimum position)
  - LPF bypassed when frequency ≥20kHz (at maximum position)
  - Inspired by MindMeld MixMaster optimization approach
  - Saves DSP cycles when filters inactive

#### Metering System
CHAN-IN features three metering modes accessible via mode switch:

**1. RMS (Root Mean Square)**
- Window Size: 2048 samples (~43ms at 48kHz)
- Smoothing: 5% alpha for stable display
- Peak Hold: 0.5 second hold with decay
- Use: General purpose level monitoring

**2. VU (Volume Unit)**
- Attack: 5ms (fast peak capture)
- Decay: 300ms (standard VU ballistics)
- Update Rate: 6kHz (decimation = 8)
- Use: Traditional analog-style metering

**3. PPM (Peak Programme Meter)**
- Attack: 0.1ms (very fast peak capture)
- Decay: 50ms (studio-optimized, NOT IEC-compliant)
- Update Rate: 6kHz (decimation = 8)
- Use: Studio input monitoring with fast response

**Implementation Constants**:
- **RMS Smoothing Alpha**: 0.05 (5% exponential averaging coefficient)
- **RMS Window Size**: 2048 samples exactly (not approximate)
- **VU/PPM Decimation**: 8 samples (6kHz update rate at 48kHz sample rate)
- **VU/PPM Attack/Decay**: Exponential coefficients calculated as `1.0 - exp(-1000.0 / (time_ms * sample_rate))`
- **Peak Hold Decay Rate**: 10.0 (linear decay when hold timer expires)
- **Peak Hold Timer**: 0.5 seconds for all modes

**Display Features**:
- Dual horizontal bars (left/right channels)
- -60dB to +6dB range (66dB total)
- Amber gradient (30% to 100% intensity)
- Peak hold indicators (white lines)
- 0dB reference line (grey)
- Visibility toggle via display enable parameter

### Parameter Ranges

| Control | Range | Purpose |
|---------|-------|---------|
| Input Gain | -60dB to +6dB | Headroom management |
| Low Cut (HPF) | 20Hz to 500Hz | Remove rumble, handling noise, proximity effect |
| High Cut (LPF) | 1kHz to 20kHz | Attenuate high frequencies |
| Phase | On/Off | Polarity inversion control |
| Display Enable | On/Off | Show/hide metering display |

## Console 1 Integration

### Hardware Control Mapping
CHAN-IN is fully integrated with the Control1 (C1) module for Console 1 MK2 hardware control:

**Parameter Mapping** (4 parameters):
- **CC 107**: LEVEL (Input Gain) - -60dB to +6dB
- **CC 105**: LPF (High Cut Filter) - 1kHz to 20kHz
- **CC 103**: HPF (Low Cut Filter) - 20Hz to 500Hz
- **CC 108**: PHASE (Phase Invert) - Toggle button

**VU Meter Feedback** (bidirectional):
- **CC 110**: Left channel VU meter (0-16 LED index)
- **CC 111**: Right channel VU meter (0-16 LED index)
- Real-time LED ring updates on Console 1 hardware
- Reads highest active LED from 17-segment VU meter arrays

**VU Meter LED Threshold Mapping** (17 LEDs, custom non-linear distribution):

| LED Index | Threshold | Step | Color | Label |
|-----------|-----------|------|-------|-------|
| 0 | -60dB | - | Green | Bottom LED |
| 1 | -51dB | 9dB | Green | |
| 2 | -42dB | 9dB | Green | |
| 3 | -33dB | 9dB | Green | |
| 4 | -24dB | 9dB | Green | -24dB |
| 5 | -20dB | 4dB | Green | |
| 6 | -16dB | 4dB | Green | |
| 7 | -12dB | 4dB | Green | -12dB |
| 8 | -10dB | 2dB | Green | |
| 9 | -8dB | 2dB | Green | |
| 10 | -6dB | 2dB | Green | -6dB, Last Green |
| 11 | -4dB | 2dB | Yellow | First Yellow |
| 12 | -2dB | 2dB | Yellow | |
| 13 | 0dB | 2dB | Yellow | 0dB, Last Yellow |
| 14 | +2dB | 2dB | Red | First Red |
| 15 | +4dB | 2dB | Red | |
| 16 | +6dB | 2dB | Red | +6dB, Top LED |

**Threshold Design**:
- Wide steps (9dB) at low levels for compression-style ballistics
- Medium steps (4dB) at mid levels for transition
- Fine steps (2dB) at critical levels for precision monitoring
- Color zones: Green (0-10), Yellow (11-13), Red (14-16)

**Display Control**:
- **CC 102**: DisplayControl toggles visibility when module is hovered
- Synchronized across all C1-ChannelStrip modules

**Module Detection**:
- Position 0 in C1 expander chain
- Automatic detection via "CHAN-IN" slug matching
- Green LED indicates successful detection
- Red LED blinks if wrong module at position 0

**Parameter Sync**:
- One-time burst sync on module connection
- Continuous bidirectional feedback during operation
- Hardware LED rings track parameter changes in real-time

### Signal Flow in C1-ChannelStrip Chain
```
Audio Source → CHAN-IN (Position 0) → Shape (Position 1) → C1-EQ (Position 2)
→ C1-COMP (Position 3) → CHAN-OUT (Position 4) → [Optional: C1-ORDER (Position 5)]
```

## CV Expander Integration (CHI-X)

### CHI-X Expander Module
CHAN-IN supports CV modulation via the CHI-X (CHAN-IN-CV) expander module:

**Expander Position**: CHI-X connects to CHAN-IN's right expander port

**CV Inputs** (4 parameters):
- **Level CV**: ±12dB modulation range
  - Input range: -1.0 to +1.0 (attenuated CV)
  - Maps to full parameter range: -60dB to +6dB
  - Applied additively to LEVEL knob position
  - Final gain clamped to -60dB to +6dB
- **HPF Frequency CV**: ±5V (1V/oct center-relative control)
  - Center frequency: ~115Hz (geometric center of 20Hz-500Hz range)
  - ±5V modulation spans full HPF range
  - CV overrides knob when connected
- **LPF Frequency CV**: ±5V (1V/oct center-relative control)
  - Center frequency: ~4.47kHz (geometric center of 1kHz-20kHz range)
  - ±5V modulation spans full LPF range
  - CV overrides knob when connected
- **Phase Invert CV**: Gate input (>1V = invert phase)
  - Logical OR with PHASE button
  - Both button and CV can trigger phase inversion

**Expander Message Protocol**:
- Thread-safe double-buffered messaging
- Message struct: `ChanInExpanderMessage` with 4 float fields
- Update rate: Every audio sample (no decimation)
- Automatic detection via `modelChanInCV` slug matching

**CV Behavior**:
- Frequency CVs use 1V/octave exponential scaling
- Level CV uses linear dB addition
- All CV inputs respect parameter range limits
- CV modulation occurs before filter processing

## Integration

### Modular Workflow
CHAN-IN is designed as the first module in a signal chain:

**Typical Chain**: Source → **CHAN-IN** → Shape → C1-EQ → C1-COMP → CHAN-OUT

### Expander Chain
- Must be positioned at **Position 0** after Control1 (C1) module
- Detection uses expander chain walking from Control1
- All parameters controlled via C1 module when connected

## Specifications

- **Module Width**: 8HP
- **Sample Rate**: Independent (supports all VCV Rack sample rates)
- **Audio Range**: ±5V (modular standard)
- **Frequency Response**: 20Hz to 20kHz (full audio spectrum)
- **Dynamic Range**: >90dB
- **Metering Range**: -60dB to +6dB (66dB total range)
- **Metering Modes**: 3 (RMS, VU, PPM)
- **Parameters**: 5 (Level, High Cut, Low Cut, Phase, Display Enable)
- **Inputs**: 2 (Left, Right)
- **Outputs**: 2 (Left, Right)
- **Console 1 CCs**: 4 parameters + 2 VU meters

## Architecture Details

### Thread Safety
- Atomic widget pointers for cross-platform safety (ARM64, x86-64)
- Widget pointers nulled in destructor to prevent dangling references
- Safe shutdown protocol with `isShuttingDown` flag
- Proper UI/audio thread separation

### Metering Display
- Three independent meter widgets (RMS/VU/PPM)
- Horizontal bar display with peak hold indicators
- Atomic widget pointers for cross-platform thread safety
- Mode switch for selecting active meter type
- Dynamic dB readout showing peak hold value

### Performance Optimization
- Filter coefficient caching (only update when parameters change)
- Clock dividers for non-critical updates (16-sample divider for filters)
- Metering decimation (RMS: 2048 samples, VU: 8 samples)
- SIMD-ready biquad filter architecture

## Version History

### RC1 (Release Candidate 1)
- Initial implementation with hybrid Befaco/MindMeld architecture
- Complete signal chain: Gain → Filters → Phase → Metering
- Three metering modes (RMS/VU/PPM)
- Console 1 MK2 hardware integration with bidirectional feedback
- VU meter feedback to hardware (CC 110/111)
- Display visibility control
- TC Design System aesthetic matching other C1-ChannelStrip modules
- Thread-safe widget pointer management
- Dynamic dB readout with peak hold display

---

**Document Version**: RC1</br>
**Project Inception**: 21-09-2025</br>
**Author**: Latif Karoumi</br>

Copyright © 2025 Twisted Cable. Licensed under GPL-3.0-or-later.
