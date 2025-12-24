# CHAN-OUT Module User Manual

CHAN-OUT is an output module for the C1-ChannelStrip system.</br>
This manual covers everything you need to know to use CHAN-OUT effectively.</br></br>
<img src="/docs/img/CHAN-OUT.jpeg" style="float:right; margin:0 0 1em 1em;" width="20%" alt="CHAN-OUT"> </br></br>
**Product**: CHAN-OUT</br>
**Type**: 8HP Output Module</br>
**Part of**: C1-ChannelStrip Plugin (GPL-3.0-or-later)</br>
**Manual Version**: 1.0</br>
**Date**: November 2025</br>

---

## How CHAN-OUT Works

CHAN-OUT processes audio through a flexible signal chain that includes </br>
pan, volume, character coloration, monitoring controls, and metering.

```
┌──────────────────────────┐
│ Audio Input (Stereo/Mono)│
│  • ±5V standard          │
│  • Mono normalization    │
└──────────┬───────────────┘
           ↓
┌──────────────────────────┐
│   Pan Processing         │
│  • Master: Linear        │
│  • Channel: Equal power  │
│  • Range: -100% to +100% │
└──────────┬───────────────┘
           ↓
┌──────────────────────────┐
│   Volume Control         │
│  • Master: -60dB to 0dB  │
│  • Channel: -60dB to +6dB│
│  • Anti-pop slewing      │
└──────────┬───────────────┘
           ↓
┌──────────────────────────┐
│   Character Engine       │
│   Selection              │
│  • Standard (Clean)      │
│  • 2520 (API op-amp)     │
│  • 8816 (Neve + Silk)    │
│  • DM2+ (3-circuit)      │
└──────────┬───────────────┘
           ↓
┌──────────────────────────┐
│   Drive Blend            │
│  • 0% to 100%            │
│  • Character mix         │
└──────────┬───────────────┘
           ↓
┌──────────────────────────┐
│   Dim/Mute Processing    │
│  • Dim: -30dB to -1dB    │
│  • Mute: True silence    │
│  • 125 units/sec slewing │
└──────────┬───────────────┘
           ↓
┌──────────────────────────┐
│   Trim (Final Gain)      │
│  • -12dB to +12dB        │
└──────────┬───────────────┘
           ↓
┌──────────────────────────┐
│   Output (±5V standard)  │
└──────────┬───────────────┘
           │
     ┌─────┴──────────┐
     ↓                ↓
┌──────────────┐  ┌──────────────────┐
│ VU Meters    │  │  LUFS Meter      │
│ • 17 LEDs × 2│  │  • EBU R128      │
│ • -60 to +6dB│  │  • 400ms window  │
└──────────────┘  └──────────────────┘
                  ┌──────────────────┐
                  │  Goniometer      │
                  │  • L/R display   │
                  │  • Phase check   │
                  └──────────────────┘
```

CHAN-OUT operates in two modes: </br>
Master mode (linear pan, -60dB to 0dB volume) for mix buses, </br>
and Channel mode (equal power pan, -60dB to +6dB volume) for individual tracks. </br>
All processing uses anti-pop slewing to prevent clicks.</br>

---

## Table of Contents

1. [What is CHAN-OUT?](#what-is-chan-out)
2. [Getting Started](#getting-started)
3. [Operating Modes](#operating-modes)
4. [The Controls](#the-controls)
5. [Character Engines](#character-engines)
6. [The VU Meters](#the-vu-meters)
7. [The LUFS Meter](#the-lufs-meter)
8. [The Goniometer](#the-goniometer)
9. [Context Menu Settings](#context-menu-settings)
10. [Console 1 MK2 Integration](#console-1-mk2-integration)
11. [Troubleshooting](#troubleshooting)

---

## What is CHAN-OUT?

CHAN-OUT is the final stage in the C1-ChannelStrip signal chain, providing output processing with analog coloration, monitoring controls, and metering.

### What CHAN-OUT Does for You

**Dual Operating Modes**:</br>
Master mode provides linear panning and -60dB to 0dB volume range for mix bus work. </br>
Channel mode provides equal power panning and -60dB to +6dB volume range for individual tracks.</br>

**Character Engines**:</br>
Four distinct analog colorations add warmth, punch, or transparency. </br>
- Standard provides clean output with subtle warmth. </br>
- 2520 adds API-style punch and clarity. </br>
- 8816 provides Neve-style warmth and weight. </br>
- DM2+ offers transparent enhancement with circuit blending.</br>

**Metering**:</br>
VU meters display stereo levels across a -60dB to +6dB range with 17 LEDs per channel. Matching with Console 1 hardware leds.</br>
LUFS metering provides EBU R128 compliant loudness monitoring for broadcast and streaming compliance. </br>
The goniometer visualizes stereo field and phase correlation.</br>

**Monitoring Controls**:</br>
Dim applies adjustable attenuation (-30dB to -1dB, default -12dB) for monitoring workflows. </br>
Mute provides instant silence. Both use fast anti-pop slewing (125 units/second) for click-free operation.</br>

### Core Features

- **Volume**: -60dB to 0dB (Master) or -60dB to +6dB (Channel)
- **Pan**: -100% (left) to +100% (right) with mode-dependent pan law
- **Character**: 4 engines (Standard, 2520, 8816, DM2+)
- **Drive**: 0% to 100% character engine blend
- **Dim**: -30dB to -1dB attenuation (context menu adjustable)
- **Mute**: True mute with anti-pop slewing
- **Trim**: -12dB to +12dB fine gain adjustment (context menu)
- **VU Meters**: Dual 17-LED bars (-60dB to +6dB)
- **LUFS**: EBU R128 compliant loudness monitoring
- **Goniometer**: Real-time stereo field visualization

---

## Getting Started

### What You Need

1. **Audio source** connected to CHAN-OUT inputs
2. **CHAN-OUT module** in a VCV Rack patch
3. **Optional**: C1 module for Console 1 MK2 hardware control

### First-Time Setup

**Step 1: Place CHAN-OUT in the Rack**</br>
Add CHAN-OUT to the patch. </br>
If you're using it with the C1 module and Console 1 MK2 hardware, place CHAN-OUT at position 4 (after CHAN-IN, SHAPE, C1-EQ, and C1-COMP).</br>

**Step 2: Connect Your Audio**</br>
Plug your audio source into the LEFT and RIGHT inputs at the bottom of CHAN-OUT.</br>
- **Mono source**: Just connect to LEFT input - CHAN-OUT automatically duplicates to both channels
- **Stereo source**: Connect both LEFT and RIGHT for true stereo processing

**Step 3: Connect the Outputs**</br>
Patch the LEFT and RIGHT outputs to your audio interface or next module in the chain.

**Step 4: Set Initial Settings**</br>
Start with:</br>
- **Operating Mode**: Master Output (default, via context menu)
- **Character Engine**: Standard (clean)
- **Drive**: 0% (no coloration)
- **Character**: 50% (neutral)
- **Pan**: Center (0%)
- **Volume**: -6dB (conservative headroom)
- **Dim**: Off
- **Mute**: Off
- **Trim**: 0dB (context menu)

**Step 5: Check Your Meters**</br>
Play audio through CHAN-OUT and watch the meters:</br>
- VU meters show stereo levels (-60dB to +6dB range)
- LUFS meter displays loudness compliance (EBU R128 standard)
- Goniometer visualizes stereo field and phase correlation

### What to Expect When It Works</br>
When everything is connected properly:</br>
- Audio passes through cleanly with the volume setting applied
- VU meters light up from bottom to top showing signal level
- LUFS meter displays perceived loudness (after ~400ms initialization)
- Goniometer shows stereo field pattern (vertical line for mono, circular pattern for stereo)
- If you're using Console 1 MK2, the hardware encoders control CHAN-OUT parameters

---

## Operating Modes

CHAN-OUT operates in two distinct modes selected via the context menu. </br>
Each mode uses different pan laws and volume ranges optimized for specific use cases.</br>

### Master Output Mode (Default)

**Pan Law**: </br>
Linear (mono-compatible)</br>

**Volume Range**: -60dB to 0dB</br>

**Pan Behavior**:</br>
Linear panning maintains constant mono sum. </br>
The left and right gains add up to 1.0, ensuring that when summed to mono, the level remains constant. </br>
This prevents phase cancellation and maintains mix integrity.</br>

**When to use**:</br>
Use Master mode for mix buses, submix groups, and final output. </br>
The 0dB maximum prevents digital clipping, and the linear pan law ensures mono compatibility for broadcast and streaming.</br>

---

### Channel Output Mode

**Pan Law**: </br>
Equal power (constant perceived level)</br>

**Volume Range**: -60dB to +6dB</br>

**Pan Behavior**:</br>
Equal power panning maintains constant perceived loudness when panning a sound. </br>
The squared sum of left and right gains equals 1.0, creating a -3dB center pan that sounds balanced. </br>
This is standard for individual tracks.</br>

**When to use**:</br>
Use Channel mode for individual tracks and channel inserts. </br>
The +6dB headroom allows gain makeup after compression or EQ. </br>
The equal power pan law maintains natural perceived level while panning.</br>

---

## The Controls

### Volume (Right Side Encoder)

**What it does**: </br>
Controls output level</br>

**Range**: </br>
-60dB to 0dB (Master mode) or -60dB to +6dB (Channel mode)</br>


---

### Pan (Left Side Bottom Encoder)

**What it does**: </br>
Controls stereo positioning</br>

**Range**: </br>
-100% (full left) to +100% (full right)</br>

**Mode-Dependent Behavior**:</br>
- Master mode: Linear pan law (constant mono sum)</br>
- Channel mode: Equal power pan law (constant perceived level with -3dB center)</br>


---

### Drive (Left Side Top Encoder)

**What it does**: </br>
Controls how much character engine coloration is applied</br>

**Range**: 0% to 100%</br>

**How it works**:</br>
- 0%: Clean signal with no character processing</br>
- 50%: Equal blend of clean and characterized signal</br>
- 100%: Full character engine processing</br>

**When to adjust**:</br>
Start at 0% and increase until you hear the desired amount of coloration. </br>
Different engines respond differently to the drive amount. 2520 adds punch, 8816 adds warmth, DM2+ adds enhancement.</br>

---

### Character (Left Side Middle Encoder)

**What it does**: </br>
Adjusts engine-specific coloration parameters</br>

**Range**: 0% to 100%</br>

**Engine-Specific Behavior**:</br>
- **Standard**: No effect (parameter not used)</br>
- **2520**: Adjusts harmonic content and feedback loop intensity</br>
- **8816**: Blends Silk Red (HF boost) and Silk Blue (HF smooth) filters</br>
- **DM2+**: Adjusts circuit blend ratio (Harmonics/Paralimit/X-Former)</br>

**When to adjust**:</br>
The Character parameter fine-tunes each engine's tonal characteristics. </br>
Experiment to find the sweet spot for your audio. </br>
This parameter is set to 50% by default when switching engines (read section below to understand the character knob settings).</br>

---

### Dim (Button Left of Volume Encoder)


**Default**: </br>
-12dB (adjustable -30dB to -1dB in context menu)</br>

**LED State**:</br>
- Off: No dim applied</br>
- Amber: Dim active, signal attenuated</br>

**Anti-pop Slewing**: for fast, click-free response</br>

**When to use**:</br>
Enable dim when you need to reduce monitoring level quickly without changing the volume setting. </br>
Useful for taking phone calls, comparing at lower levels, or protecting your ears during loud sections.</br>

---

### Mute (Button Right of Volume Encoder)


**LED State**:</br>
- Off: Audio passing through</br>
- Amber: Output muted</br>

**Anti-pop Slewing**: for click-free muting</br>



---

## Character Engines

CHAN-OUT provides four distinct character engines. </br>
Select the engine by clicking one of the four rectangles at the top of the display area. </br>
The active engine shows an amber checkmark. </br>
The engine name appears in amber text to the right of the switches.</br>

### Standard (Clean)

**Character**: </br>
Transparent processing with gentle saturation at high levels</br>

**What it does**:</br>
Standard uses MindMeld soft-clipping with polynomial waveshaping. </br>
At low to moderate levels, the signal passes through transparently. </br>
At high levels, soft saturation adds minimal even-order harmonics for subtle warmth.</br>

**Technical Details**:</br>
- Polynomial soft-clipper: `x - (x³ / 3.0)`</br>
- Hyperbolic tangent saturation at high drive</br>
- Optional 8× oversampling (context menu)</br>

**When to use**:</br>
Use Standard when you want clean output with just a hint of analog warmth. </br>
This engine adds minimal coloration while preventing harsh digital clipping at high levels.</br>

**Drive Effect**: Increases saturation amount</br>
**Character Effect**: Not used</br>

---

### 2520 (API Discrete Op-Amp)

**Character**: </br>
Aggressive even-order harmonics, punchy transients</br>

**What it does**:</br>
The 2520 engine models the API 2520 discrete operational amplifier. </br>
It uses asymmetric polynomial waveshaping (positive and negative signals handled differently) with a feedback loop for harmonic enhancement. </br>
This creates strong 2nd harmonic content and moderate 3rd harmonic content.</br>

**Technical Details**:</br>
- Asymmetric waveshaping with different curves for positive/negative signals</br>
- Feedback loop adds 15% harmonic enhancement</br>
- Soft-knee output limiting</br>
- 8× polyphase SIMD-optimized oversampling (adjustable in context menu)</br>

**When to use**:</br>
Use 2520 for drums, bass, vocals, or any material that needs punch and clarity. </br>
The aggressive even-order harmonics add presence and make sounds cut through the mix. </br>
The asymmetric waveshaping creates a forward, energetic character.</br>

**Drive Effect**: </br>
Increases waveshaping intensity and feedback</br>
**Character Effect**: </br>
Adjusts harmonic content and feedback loop intensity</br>

---

### 8816 (Neve Transformer + Silk)

**Character**: </br>
Warm, thick midrange with smooth high-frequency roll-off</br>

**What it does**:</br>
The 8816 engine models the Neve 8816 summing mixer with transformer saturation and Silk Red/Blue filtering. </br>
It uses parallel blending (wet transformer signal + dry signal) with flux integration to simulate transformer core saturation. </br>
The Silk filters provide HF boost (Red) or HF smoothing (Blue).</br>

**Technical Details**:</br>
- Transformer core saturation model with flux integration
- Silk Red: High shelf filter boost around 10kHz (+1.5dB)
- Silk Blue: Low-pass filter smooth roll-off above 15kHz
- Parallel blend architecture (wet × blend + dry × (1 - blend))
- 8× polyphase oversampling (adjustable in context menu)

**Flux Integration**:</br>
The engine integrates the signal over time to simulate transformer core saturation. </br>
This creates low-frequency compression and produces the characteristic Neve "weight" and punch.</br>

**When to use**:</br>
Use 8816 for mix buses, vocals, or any material that benefits from Neve-style warmth and weight. </br>
The transformer saturation adds complexity and harmonics, while the Silk filters provide tonal shaping.</br>

**Drive Effect**: </br>
Increases transformer saturation intensity</br>
**Character Effect**: </br>
Blends Silk Red (0% - 50%) to Silk Blue (50% - 100%), with neutral response at 50%</br>

---

### DM2+ (Dangerous Music Master Link)

**Character**: </br>
Transparent enhancement with configurable circuit blending</br>

**What it does**:</br>
The DM2+ engine models the Dangerous Music Master Link with three parallel circuits that run simultaneously. </br>
Circuit 1 (Harmonics) uses polynomial waveshaping. </br>
Circuit 2 (Paralimit) applies parallel soft limiting. </br>
Circuit 3 (X-Former) provides transformer simulation. </br>
The outputs are blended with equal-loudness mixing.</br>

**Technical Details**:</br>
- Circuit 1 (Harmonics): Polynomial waveshaping (`a₁x + a₃x³ + a₅x⁵`)</br>
- Circuit 2 (Paralimit): Parallel soft limiting (dry + limited signal blend)</br>
- Circuit 3 (X-Former): Transformer simulation with saturation and hysteresis</br>
- Master blend: 33% Harmonics + 33% Paralimit + 34% X-Former</br>
- 8× oversampling for all three parallel paths (adjustable in context menu)</br>

**When to use**:</br>
Use DM2+ for mastering, mix bus enhancement, or any material that needs transparent enhancement. </br>
The three-circuit architecture provides complex harmonic content without obvious coloration. </br>
The parallel nature maintains dynamics while adding character.</br>

**Drive Effect**: </br>
Scales all three circuits simultaneously</br>
**Character Effect**: </br>
Adjusts circuit blend ratio </br>

---

## The VU Meters

The VU meters (17 LEDs per channel, positioned in the right column) display stereo output levels after all processing including trim. </br>
The meters show post-pan, post-volume, pre-clipping levels.</br>

### LED Mapping

**Range**: -60dB to +6dB (66dB total range)</br>

**LED Colors**:</br>
- LEDs 0-10 (green): -60dB to -6dB</br>
- LEDs 11-13 (yellow): -6dB to 0dB</br>
- LEDs 14-16 (red): 0dB to +6dB</br>

**Specific Thresholds**:</br>
- LED 0 (bottom, green): -60dB</br>
- LED 4 (green): -24dB (labeled)</br>
- LED 7 (green): -12dB (labeled)</br>
- LED 10 (green): -6dB (labeled, last green LED)</br>
- LED 11 (yellow): -4dB (first yellow LED)</br>
- LED 13 (yellow): 0dB (labeled, last yellow LED)</br>
- LED 14 (red): +2dB (first red LED)</br>
- LED 16 (top, red): +6dB (labeled, clip warning)</br>


---

## The LUFS Meter

The LUFS meter (horizontal bar below the goniometer) displays EBU R128 compliant loudness monitoring. </br>
This measures perceived loudness rather than peak level.</br>

### What It Shows

**Range**: -60 LUFS to 0 LUFS</br>

**Standard**: </br>
Momentary loudness (400ms sliding window)</br>

**Display**:</br>
- Dual horizontal bars (one per channel, both showing the same stereo loudness value)</br>
- Amber gradient from 30% (left) to 100% (right)</br>
- White vertical line shows peak hold</br>
- Thin grey vertical line marks 0 LUFS reference (far right)</br>
- "LUFS" text label inside the meter bar</br>
- Scale labels below: "-60" at left, "0" at right</br>

### How LUFS Works

LUFS (Loudness Units relative to Full Scale) measures perceived loudness using K-weighting filters that match human hearing sensitivity. </br>
The meter processes 2048-sample batches for CPU efficiency (~1.6% CPU usage).</br>

**Voltage Scaling**: </br>
VCV Rack ±5V signals are scaled to ±1.0f for the libebur128 library (multiplication by 0.2f).</br>

### Target LUFS Values (subject to change)

**Streaming Services**:</br>
- Spotify: -14 LUFS
- Apple Music: -16 LUFS
- YouTube: -13 to -15 LUFS

**Broadcast**:</br>
- EBU R128: -23 LUFS (European broadcast standard)
- ATSC A/85: -24 LUFS (North American broadcast standard)

**CD Mastering**: -9 to -13 LUFS</br>



---

## The Goniometer

The goniometer (rectangular display between engine switches and LUFS meter) visualizes the stereo field </br>
by plotting left channel on the X-axis and right channel on the Y-axis.</br>

### What It Shows

**X-Axis**: Left channel (-5V to +5V, horizontal)</br>
**Y-Axis**: Right channel (-5V to +5V, vertical)</br>

**Display Elements**:</br>
- Center crosshair grid (vertical and horizontal lines marking center)
- Amber dots representing recent samples (128 samples displayed)
- Age-based fading (older samples dimmer, alpha 80-180)
- Independent X/Y scaling (95% of display width/height)

### Reading the Display

**Vertical Line**: </br>
Mono signal (L = R)</br>
- Perfect center line means left and right are identical

**Horizontal Line**: </br>
Anti-phase signal (L = -R)</br>
- This indicates phase problems that will cancel in mono

**Diagonal Line (45°)**: </br>
Full left or right pan</br>
- Upper-right diagonal: panned right
- Lower-left diagonal: panned left (if right is inverted)

**Circle/Ellipse**: </br>
Stereo width indication</br>
- Wide circle: wide stereo field
- Narrow ellipse: narrow stereo field
- Centered pattern: balanced stereo image

**Lissajous Figures**: </br>
Complex patterns show frequency and phase relationships</br>

### When to Watch It

Use the goniometer to verify stereo width, check for phase problems, and ensure your mix translates to mono. </br>
A balanced stereo mix produces a roughly circular pattern centered around the origin. </br>
Horizontal streaks indicate phase issues.</br>

---

## Context Menu Settings

Right-click the module to access these settings:</br>

### Operating Mode

**Options**: Master Output (default) or Channel Output</br>

**Master Output**:</br>
- Linear pan law (mono-compatible)
- Volume range: -60dB to 0dB

**Channel Output**:</br>
- Equal power pan law (constant perceived level)
- Volume range: -60dB to +6dB

**When to change**: </br>
Use Master mode for mix buses and final output. </br>
Use Channel mode for individual tracks and channel inserts.</br>

---

### Trim

**Range**: -12dB to +12dB (slider control)</br>
**Default**: 0dB</br>

**What it does**: </br>
Applies final gain adjustment after all processing</br>

**When to adjust**: </br>
Use trim for fine gain adjustments without changing the main volume setting. </br>
This is useful for balancing multiple CHAN-OUT instances or compensating for character engine gain changes.</br>

---

### Dim Gain

**Range**: -30dB to -1dB (slider control)</br>
**Default**: -12dB</br>

**What it does**: </br>
Sets the attenuation amount when the Dim button is engaged</br>

**Display**: </br>
Shows integer dB values (rounded)</br>

**When to adjust**: </br>
Set dim gain to your preferred monitoring attenuation. </br>
-12dB is a common choice for quick level reduction.</br>
-20dB or -30dB provides more dramatic attenuation.</br>

---

### Character Engine

**Options**: </br>
Standard, 2520, 8816, DM2+</br>

Each engine except Standard has an oversampling submenu:</br>

**Oversampling Options**: </br>
8×, 4×, 2×, OFF (1×)</br>

**8×**:</br>
- Highest quality alias reduction
- Most CPU usage (~2-4% per instance)
- Polyphase FIR filters with >96dB stopband attenuation

**4× / 2×**:</br>
- Moderate quality/CPU balance
- Reduced alias rejection

**OFF (1×)**:</br>
- No oversampling
- Lowest CPU usage
- Potential aliasing artifacts at high drive settings

**When to adjust**: </br>
Use 8× oversampling for final mixes and mastering. </br>
Use lower settings during production if CPU is limited. </br>

---

## Console 1 MK2 Integration

When you connect CHAN-OUT to the C1 module and Console 1 MK2 hardware, you get hands-on control with bidirectional feedback.</br>

### Hardware Control Mapping

**CHAN-OUT uses 6 parameters**:</br>

**1. CHARACTER (Encoder)**:</br>
- Turn to adjust engine-specific coloration (0% to 100%)
- LED ring shows current character position
- Engine-specific behavior (2520: harmonic content, 8816: Silk blend, DM2+: circuit blend)

**2. DRIVE (Encoder)**:</br>
- Turn to adjust character engine blend (0% to 100%)
- LED ring shows current drive amount
- 0% = clean, 100% = full character processing

**3. PAN (Encoder)**:</br>
- Turn to adjust stereo positioning (-100% to +100%)
- LED ring shows pan position (center = 0%)
- Mode-dependent pan law (linear or equal power)

**4. VOLUME (Encoder)**:</br>
- Turn to adjust output level
- LED ring shows volume position
- Master mode: -60dB to 0dB, Channel mode: -60dB to +6dB

**5. DIM (Button)**:
- Press to toggle dim on/off
- Button LED lights when dim is active
- Applies attenuation set in context menu (-30dB to -1dB, default -12dB)

**6. MUTE (Button)**:</br>
- Press to toggle mute on/off
- Button LED lights when muted

All encoders have LED rings showing current parameter positions, updating in real-time when you change parameters in VCV Rack or on the hardware.</br>

### Parameter Synchronization

**When you load a patch**:</br>
C1 sends all current parameter values to the Console 1 MK2 hardware.</br>
The LED rings immediately show the correct positions - no need to move every knob to "pick up" the parameters.</br>

**Bidirectional control**:</br>
- Turn a hardware encoder → CHAN-OUT parameter updates in VCV Rack
- Move a parameter in VCV Rack → Hardware LED ring updates to match

Everything stays synchronized automatically.</br>

### CHAN-OUT Position in Chain

**Position 4**:</br>
When using Standard mode, CHAN-OUT must be placed at position 4 (after CHAN-IN, SHAPE, C1-EQ, and C1-COMP).</br>

**Detection**:</br>
C1's fifth module LED (green) indicates CHAN-OUT is detected and active.</br>

---

## Troubleshooting

### Problem: VU Meters Not Lighting Up

**Causes**:</br>
- No audio reaching the module
- Volume set too low
- Mute is engaged

**Solutions**:</br>
- Verify cables are connected to inputs
- Check that other modules are producing output
- Increase volume from -60dB
- Disengage mute button (LED should be off)

---

### Problem: Output Is Distorted or Clipped

**Causes**:</br>
- Drive set too high
- Volume too high in Channel mode
- Character engine saturating
- Input signal too hot

**Solutions**:</br>
- Reduce drive to 0-50% range
- Reduce volume to prevent clipping (watch VU meters for sustained red)
- Try Standard engine for cleanest output
- Reduce input gain using CHAN-IN or upstream processing
- Use Trim to reduce final output level (context menu)

---

### Problem: LUFS Meter Shows No Reading

**Causes**:</br>
- No audio present
- Less than 400ms of audio processed (LUFS requires minimum time window)
- Very quiet signal

**Solutions**:</br>
- Verify audio is present (check VU meters)
- Wait at least 400ms for LUFS meter to initialize
- Increase signal level if the audio is extremely quiet
- LUFS meter measures perceived loudness; silence reads very low (-70 LUFS)

---

### Problem: Goniometer Shows Only Vertical Line

**Causes**:</br>
- Mono signal (left and right are identical)
- Pan set to center with mono input
- Phase-locked stereo signal

**Solutions**:</br>
- This is normal for mono material
- If the input should be stereo, verify both left and right inputs are connected
- Check upstream processing for stereo width reduction
- A vertical line is correct for mono material - no action needed

---

### Problem: Goniometer Shows Horizontal Line

**Causes**:</br>
- Anti-phase signal (left and right are inverted relative to each other)
- Polarity inversion somewhere in the chain
- Extreme stereo widening processing

**Solutions**:
- This indicates phase problems that will cancel in mono
- Check for polarity inversion on one channel
- Reduce stereo widening processing upstream
- Fix the phase issue to ensure mono compatibility

---

### Problem: Character Engine Not Affecting Sound

**Causes**:</br>
- Drive set to 0%
- Standard engine selected (minimal coloration at low drive)
- Character parameter not adjusted (engine-dependent)

**Solutions**:</br>
- Increase drive above 0% to apply character processing
- Try 2520, 8816, or DM2+ engines for more obvious coloration
- Adjust Character parameter to change engine-specific behavior
- Verify the correct engine is selected (check amber checkmark and engine name)

---

## Copyright and Legal

**Copyright © 2025 Twisted Cable. Licensed under GPL-3.0-or-later.**</br>

---

**Manual Version**: 1.0</br>
**Last Updated**: November 2025</br>
**Author**: Latif Karoumi / Twisted Cable</br>
