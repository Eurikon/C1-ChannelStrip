# C1-EQ Module User Manual

C1-EQ is a 4-band parametric equalizer for the C1-ChannelStrip system.</br>
This manual covers everything you need to know to use C1-EQ effectively.</br></br>
<img src="/docs/img/C1-EQ.jpeg" style="float:right; margin:0 0 1em 1em;" width="30%" alt="C1-EQ"> </br></br>
**Product**: C1-EQ</br>
**Type**: 15HP 4-Band Parametric Equalizer</br>
**Part of**: C1-ChannelStrip Plugin (GPL-3.0-or-later)</br>
**Manual Version**: 1.0</br>
**Date**: November 2025</br>

---

## How C1-EQ Works

C1-EQ processes audio through four cascaded filter bands, each targeting a specific frequency range.</br>
Here's the signal flow:</br>

```
           Audio Input (L/R)
                   ↓
       ┌───────────────────────┐
       │   Global Gain Stage   │
       │    -24dB to +24dB     │
       └───────────┬───────────┘
                   ↓
       ┌───────────────────────┐
       │    Band 1 (LF)        │
       │    20Hz - 400Hz       │
       │  • Gain: ±20dB        │
       │  • Q: Fixed 0.8       │
       │  • Modes: Cut/Bell/   │
       │           Shelf       │
       └───────────┬───────────┘
                   ↓
       ┌───────────────────────┐
       │    Band 2 (LMF)       │
       │    200Hz - 2kHz       │
       │  • Gain: ±20dB        │
       │  • Q: 0.3 to 12.0     │
       │  • Type: Bell         │
       └───────────┬───────────┘
                   ↓
       ┌───────────────────────┐
       │    Band 3 (HMF)       │
       │    1kHz - 8kHz        │
       │  • Gain: ±20dB        │
       │  • Q: 0.3 to 12.0     │
       │  • Type: Bell         │
       └───────────┬───────────┘
                   ↓
       ┌───────────────────────┐
       │    Band 4 (HF)        │
       │    4kHz - 20kHz       │
       │  • Gain: ±20dB        │
       │  • Q: Fixed 1.0       │
       │  • Modes: Cut/Bell/   │
       │           Shelf       │
       └───────────┬───────────┘
                   ↓
       ┌───────────────────────┐
       │  Analog Character     │
       │  • Transparent        │
       │  • Light (2nd order)  │
       │  • Medium (2nd/3rd)   │
       │  • Full (2nd-5th)     │
       └───────────┬───────────┘
                   ↓
       ┌───────────────────────┐
       │   Oversampling        │
       │   (Optional 2x/4x)    │
       └───────────┬───────────┘
                   ↓
           Audio Output (L/R)
                   ↓
      ┌────────────┴────────────┐
      ↓                         ↓
┌────────────────┐    ┌─────────────────┐
│ Spectrum       │    │ Clipping        │
│ Analyzer       │    │ Indicator       │
│ • 128 bands    │    │ • Green/Amber/  │
│ • 20Hz-22kHz   │    │   Red LED       │
│ • FFT display  │    │ • 7V threshold  │
└────────────────┘    └─────────────────┘
```

Each band applies boost or cut at its frequency range. </br>
The bands process in sequence from low to high.</br>
Optional analog character modes add harmonic coloration.</br>

---

## Table of Contents

1. [What is C1-EQ?](#what-is-c1-eq)
2. [Getting Started](#getting-started)
3. [The Four Bands](#the-four-bands)
4. [Global Gain Control](#global-gain-control)
5. [Analog Character Modes](#analog-character-modes)
6. [The Spectrum Analyzer](#the-spectrum-analyzer)
7. [The Clipping Indicator](#the-clipping-indicator)
8. [Oversampling](#oversampling-switch)
9. [Console 1 MK2 Integration](#console-1-mk2-integration)
10. [Troubleshooting](#troubleshooting)

---

## What is C1-EQ?

C1-EQ is a parametric equalizer that shapes the frequency content of audio. </br>
It provides precise control over four frequency bands, from deep bass to high treble.</br>

### What C1-EQ Does for You

**Frequency Correction**:</br>
Remove unwanted resonances or boomy frequencies. Use narrow Q settings to target specific problem frequencies.</br>

**Tonal Shaping**:</br>
Enhance or reduce frequency ranges to change the character of audio. Brighten vocals, add warmth to synths, or remove mud from mixes.</br>

**Analog Character**:</br>
Three modes add harmonic coloration inspired by classic console circuitry. </br>
From transparent digital precision to full transformer saturation.</br>

**Visual Feedback**:</br>
The spectrum analyzer shows the frequency content of audio in real-time. </br>
The clipping indicator warns when the signal is too hot.</br>

### Core Features

- **4 Frequency Bands**: LF (20-400Hz), LMF (200-2kHz), HMF (1k-8kHz), HF (4k-20kHz)
- **Gain Range**: ±20dB per band
- **Variable Q**: Bands 2 and 3 have adjustable bandwidth (Q: 0.3 to 12.0)
- **Filter Modes**: Bands 1 and 4 switch between Cut/Bell/Shelf modes
- **Analog Modes**: Transparent, Light, Medium, Full
- **Spectrum Analyzer**: 128-band display, 20Hz to 22kHz
- **Oversampling**: Optional 2x/4x anti-aliasing
- **True Stereo**: Independent left/right processing with mono fallback

---

## Getting Started

### What You Need

1. **Audio source** connected to C1-EQ inputs
2. **C1-EQ module** in a VCV Rack patch
3. **Optional**: C1 module for Console 1 MK2 hardware control

### First-Time Setup

**Step 1: Place C1-EQ in the Rack**</br>

Add C1-EQ to the patch. </br>
If you're using it with the C1 module and Console 1 MK2 hardware, place C1-EQ at position 2 (after CHAN-IN and SHAPE).</br>

**Step 2: Connect Your Audio**</br>

Plug your audio source into the LEFT and RIGHT inputs at the bottom of C1-EQ.</br>
- **Mono source**: Just connect to LEFT input - C1-EQ automatically processes both channels identically
- **Stereo source**: Connect both LEFT and RIGHT for true stereo processing

**Step 3: Connect the Outputs**</br>

Patch the LEFT and RIGHT outputs to the next module in the chain (typically C1-COMP, the compressor) or directly to a mixer/output.</br>

**Step 4: Set Initial Settings**</br>

Start with:</br>
- **Global Gain**: 0dB (unity gain)
- **All Band Gains**: 0dB (no boost or cut)
- **Analog Character**: Transparent (no coloration)
- **Oversampling**: Off



### What to Expect When It Works

When everything is connected properly:</br>
- Audio passes through cleanly with no EQ applied (all bands at 0dB)
- The spectrum analyzer shows real-time frequency content
- The clipping indicator shows green (clean signal)
- If you're using Console 1 MK2, the hardware encoders control C1-EQ parameters

---

## The Four Bands

### Band 1 (LF - Low Frequency)

**Frequency Range**: 20Hz to 400Hz (default: 80Hz)</br>
**Gain Range**: ±20dB</br>
**Q Factor**: Fixed at 0.8 (when in Bell mode)</br>

**Filter Modes** (cycle with mode button):</br>
- **Shelf** (default): Low-shelf response for bass enhancement or reduction
- **Bell**: Parametric boost/cut with fixed Q
- **Cut**: Mutes this band completely

**When to use each mode**:</br>
- **Shelf**: General bass control, broad boost or cut below the frequency
- **Bell**: Target specific bass frequency without affecting everything below
- **Cut**: Disable this band when you don't need low frequency processing

**Controls**:</br>
- **FREQ encoder**: Sets center frequency (left encoder)
- **GAIN encoder**: Sets boost/cut amount (right encoder)
- **Mode button**: Cycles through Shelf → Bell → Cut modes
- **Mode LEDs**: Three tiny yellow LEDs show active mode (top=Cut, middle=Bell, bottom=Shelf)

**Example use**:</br>
Set to 80Hz Shelf mode, boost +3dB to add warmth to a thin vocal.</br>
Or use Cut mode to disable this band when you don't need low frequency processing.</br>

---

### Band 2 (LMF - Low-Mid Frequency)

**Frequency Range**: 200Hz to 2kHz (default: 250Hz)</br>
**Gain Range**: ±20dB</br>
**Q Range**: 0.3 to 12.0 (default: 1.0)</br>
**Filter Type**: Bell (parametric)</br>

**Controls**:</br>
- **FREQ encoder**: Sets center frequency (left encoder)
- **Q encoder**: Sets bandwidth (middle encoder) - low Q = wide, high Q = narrow
- **GAIN encoder**: Sets boost/cut amount (right encoder)

**Q Factor guide**:</br>
- **0.3 - 0.7**: Very wide, gentle slopes
- **1.0 - 2.0**: Medium width,
- **3.0 - 6.0**: Narrow,
- **8.0 - 12.0**: Very narrow,

**Example use**: </br>
Cut -4dB at 400Hz with Q=1.5 to remove muddiness from a mix. </br>
Or boost +3dB at 1kHz with Q=0.7 for presence on vocals.</br>

---

### Band 3 (HMF - High-Mid Frequency)

**Frequency Range**: 1kHz to 8kHz (default: 2kHz)</br>
**Gain Range**: ±20dB</br>
**Q Range**: 0.3 to 12.0 (default: 1.0)</br>
**Filter Type**: Bell (parametric)</br>

**Controls**:</br>
- **FREQ encoder**: Sets center frequency (left encoder)
- **Q encoder**: Sets bandwidth (middle encoder)
- **GAIN encoder**: Sets boost/cut amount (right encoder)

**Frequency guide**:
- **1kHz - 2kHz**: Presence, clarity, definition
- **2kHz - 4kHz**: Bite, attack, consonants in vocals
- **4kHz - 8kHz**: Air, brilliance, cymbals, hi-hats

**Example use**: </br>
Boost +5dB at 3kHz with Q=2.0 to make a vocal cut through the mix. </br>
Or cut -6dB at 5kHz with Q=4.0 to remove harshness.</br>

---

### Band 4 (HF - High Frequency)

**Frequency Range**: 4kHz to 20kHz (default: 7kHz)</br>
**Gain Range**: ±20dB</br>
**Q Factor**: Fixed at 1.0 (when in Bell mode)</br>

Filter Modes (cycle with mode button):</br>
- Shelf (default): High-shelf response for treble enhancement or reduction
- Bell: Parametric boost/cut with fixed Q
- Cut: Mutes this band completely

When to use each mode:</br>
- Shelf: General treble control, broad boost or cut above the frequency
- Bell: Target specific high frequency without affecting everything above
- Cut: Disable this band when you don't need high frequency processing

Controls:</br>
- FREQ encoder: Sets center frequency (left encoder)
- GAIN encoder: Sets boost/cut amount (right encoder)
- Mode button: Cycles through Shelf → Bell → Cut modes
- Mode LEDs: Three tiny yellow LEDs show active mode (top=Cut, middle=Bell, bottom=Shelf)

Example use: </br>
Set to 8kHz Shelf mode, boost +2dB to add air and sparkle to a dull mix. </br>
Or use Bell mode at 12kHz with +3dB to enhance cymbal presence.</br>

---

## Global Gain Control

**Range**: -24dB to +24dB</br>
**Default**: 0dB</br>
**Location**: Bottom left encoder</br>

**What it does**: </br>
Adjusts the overall output level after all EQ processing. </br>
Use this to compensate for level changes caused by EQ boosts or cuts.</br>

**When to adjust**:</br>
- After adding significant EQ boosts, reduce global gain to prevent clipping
- After cutting frequencies, increase global gain to restore level
- Watch the clipping indicator (top LED) - keep it green or amber, avoid red

---

## Analog Character Modes

Four modes add harmonic coloration inspired by classic console circuitry.</br>

**Location**: </br>
Bottom right encoder (4-position snap knob)</br>
**LED Indicator**: </br>
RGB LED right-side to the encoder shows the active mode</br>

---

### Transparent (Mode 0)

**LED**: Off
**Character**: Digital precision with no harmonic coloration
**CPU**: Lowest (no extra processing)

**When to use**: </br>
Clean, transparent EQ work. </br>
Frequency correction where you don't want any coloration. Mastering where precision is critical.</br>

---

### Light (Mode 1)

**LED**: Green (50% brightness)</br>
**Character**: Subtle harmonic enhancement with soft-knee saturation</br>
**Harmonics**: 2nd order</br>

**When to use**: </br>
Gentle warmth and cohesion. Slight analog character without obvious distortion. </br>
Vocals, acoustic instruments, or anywhere you want subtle richness.</br>

---

### Medium (Mode 2)

**LED**: Blue (50% brightness)</br>
**Character**: Console-style saturation with VCA curve modeling</br>
**Harmonics**: 2nd and 3rd order with "glue" compression character</br>

**When to use**: </br>
Mix bus processing. Adding analog console character to a mix. </br>
When you want the EQ to impart some of the cohesive "glue" of classic consoles.</br>

---

### Full (Mode 3)

**LED**: Red (50% brightness)</br>
**Character**: Complete circuit modeling with transformer coloration</br>
**Harmonics**: 2nd through 5th order, maximum harmonic richness</br>

**When to use**: </br>
Creative sound design. Aggressive tonal shaping. </br>
When you want maximum analog character and saturation. </br>
Synthesizers, drums, or any source that benefits from rich harmonics.</br>

**Note**: </br>
Enable oversampling (context menu) when using Full mode to reduce aliasing artifacts.</br>

---

## The Spectrum Analyzer

The spectrum analyzer shows the frequency content of audio in real-time.</br>

**Display**: 200×100 pixel area in the upper panel</br>
**Frequency Range**: 20Hz to 22kHz</br>
**Bands**: 128 logarithmically-spaced bins</br>
**Update**: Dynamic, based on audio buffer fill</br>

### Reading the Display

**Vertical Axis**: Amplitude (louder = taller bars)</br>
**Horizontal Axis**: Frequency (left = low, right = high)</br>
**Color**: Amber bars show frequency content</br>
**Peak Hold**: Bright dots at peak levels with decay timers</br>



### Display Toggle

Click the small switch in the upper-right corner of the display to turn it on or off:</br>
- **Amber fill**: Display is on
- **X mark**: Display is off

The switch shows at 50% opacity normally, 100% opacity when you hover over it.</br>
Turn off the analyzer to save CPU if you don't need it.</br>

---

## The Clipping Indicator

The RGB LED left-side of the GAIN knob warns when the signal is too hot.</br>


**Colors**:</br>
- **Green (50% brightness)**: Clean signal, no clipping, good level
- **Green → Amber**: Approaching clipping threshold
- **Amber → Red**: Signal entering clipping range
- **Pure Red (90% brightness)**: Maximum clipping detected

### What the Indicator Monitors

The clipping detector watches the signal **after** all EQ and analog processing. </br>
It monitors for signals exceeding 7V (3dB headroom from the ±10V VCV Rack limit).</br>



---

## Oversampling Switch

### Oversampling

**Options**: Off/On</br>
**Default**: Off</br>

When enabled, C1-EQ processes audio at 2× or 4× the sample rate (automatically determined by engine sample rate), then downsamples back to normal.</br>

**Benefits**:</br>
- Reduces aliasing artifacts from analog character modes
- Smoother harmonic generation
- More accurate filter response at high frequencies

**Drawbacks**:</br>
- Increases CPU usage (approximately doubles processing load)

**When to enable**:</br>
- Using Medium or Full analog modes
- Applying extreme EQ boosts
- Working with bright, harmonically-rich material

**When to disable**:</br>
- Using Transparent or Light modes
- Need to conserve CPU
- Working with material that doesn't benefit from oversampling

---

## Console 1 MK2 Integration

When you connect C1-EQ to the C1 module and Console 1 MK2 hardware, you get hands-on control with bidirectional feedback.</br>

### Hardware Control Mapping

**C1-EQ uses 10 encoders + 3 buttons**:</br>

**Band 1 (LF) - 3 Controls**:</br>
1. **LF FREQ (Encoder)**: Low frequency band center (20Hz to 400Hz)
2. **LF GAIN (Encoder)**: Low frequency boost/cut (±20dB)
3. **LF MODE (Button)**: Cycles through Shelf → Bell → Cut modes

**Band 2 (LMF) - 3 Controls**:</br>
4. **LMF FREQ (Encoder)**: Low-mid frequency center (200Hz to 2kHz)
5. **LMF Q (Encoder)**: Bandwidth control (Q: 0.3 to 12.0)
6. **LMF GAIN (Encoder)**: Low-mid boost/cut (±20dB)

**Band 3 (HMF) - 3 Controls**:</br>
7. **HMF FREQ (Encoder)**: High-mid frequency center (1kHz to 8kHz)
8. **HMF Q (Encoder)**: Bandwidth control (Q: 0.3 to 12.0)
9. **HMF GAIN (Encoder)**: High-mid boost/cut (±20dB)

**Band 4 (HF) - 3 Controls**:</br>
10. **HF FREQ (Encoder)**: High frequency band center (4kHz to 20kHz)
11. **HF GAIN (Encoder)**: High frequency boost/cut (±20dB)
12. **HF MODE (Button)**: Cycles through Shelf → Bell → Cut modes

**Global Controls - 2 Controls**:</br>
13. **CHARACTER (Encoder)**: Analog character mode (Transparent/Light/Medium/Full)
14. **BYPASS (Button)**: Toggle EQ bypass on/off

All encoders have LED rings showing current parameter positions, updating in real-time when you change parameters in VCV Rack or on the hardware.</br>

### Parameter Synchronization

**When you load a patch**:</br>
C1 sends all current parameter values to the Console 1 MK2 hardware.</br>
The LED rings immediately show the correct positions - no need to move every knob to "pick up" the parameters.</br>

**Bidirectional control**:</br>
- Turn a hardware encoder → C1-EQ parameter updates in VCV Rack
- Move a parameter in VCV Rack → Hardware LED ring updates to match

Everything stays synchronized automatically.</br>

### C1-EQ Position in Chain

**Position 2**:</br>
When using Standard mode, C1-EQ must be placed at position 2 (after CHAN-IN and SHAPE).</br>

**Detection**:</br>
C1's third module LED (green) indicates C1-EQ is detected and active.</br>

---

## Troubleshooting

### Problem: Clipping Indicator Shows Red

**Causes**:</br>
- Too much EQ boost applied
- Global Gain too high
- Input signal already too hot
- Analog character mode adding gain

**Solutions**:</br>
- Reduce Global Gain by 3-6dB
- Lower individual band boost amounts
- Switch to a lighter analog mode (Full → Medium → Light → Transparent)
- Reduce the level of the source feeding into C1-EQ

---

### Problem: Spectrum Analyzer Not Updating

**Causes**:</br>
- Analyzer is disabled
- No audio signal present
- Module bypassed

**Solutions**:</br>
- Right-click module → Enable "Enable Spectrum Analyzer"
- Check cables are connected to inputs
- Verify bypass button is off (LED should be dark)
- Play audio through the module

---

### Problem: EQ Sounds Harsh or Aliased

**Causes**:</br>
- Using Full analog mode without oversampling
- Extreme EQ boosts creating harmonics
- Low sample rate (44.1kHz) with aggressive processing

**Solutions**:</br>
- Enable Oversampling in context menu
- Reduce EQ boost amounts
- Switch to a lighter analog mode
- Use gentler Q settings (lower Q = wider, smoother)

---

### Problem: No Audible EQ Effect

**Causes**:</br>
- Band gain set to 0dB (no boost or cut)
- Wrong frequency selected for the material
- Q too narrow (extreme Q settings affect very small frequency range)
- Module bypassed

**Solutions**:</br>
- Check all band GAIN encoders are actually boosting or cutting (not at 0dB)
- Sweep the FREQ encoder while audio plays to find the right frequency
- Lower Q setting to broaden the affected frequency range
- Verify bypass button is off

---

### Problem: Audio Sounds Muddy After EQ

**Causes**:</br>
- Too much low or low-mid boost
- Not enough high frequency content
- Q too wide, affecting too broad a range

**Solutions**:</br>
- Reduce Band 1 and Band 2 boost amounts
- Add a gentle high-shelf boost with Band 4 (Shelf mode, +2dB at 8-10kHz)
- Increase Q to narrow the boost (affects smaller frequency range)
- Cut instead of boost - use Band 2 to cut at 300-500Hz instead

---

## Copyright and Legal

**Copyright © 2025 Twisted Cable. Licensed under GPL-3.0-or-later.**</br>

---

**Manual Version**: 1.0</br>
**Last Updated**: November 2025</br>
**Author**: Latif Karoumi / Twisted Cable</br>
