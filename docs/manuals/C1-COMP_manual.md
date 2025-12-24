# C1-COMP Module User Manual

C1-COMP is a hybrid compressor module for the C1-ChannelStrip system.</br>
This manual covers everything you need to know to use C1-COMP effectively.</br></br>
<img src="/docs/img/C1-COMP.jpeg" style="float:right; margin:0 0 1em 1em;" width="10%" alt="C1-COMP"> </br></br>
**Product**: C1-COMP</br>
**Type**: 8HP Compressor Module</br>
**Part of**: C1-ChannelStrip Plugin (GPL-3.0-or-later)</br>
**Manual Version**: 1.0</br>
**Date**: November 2025</br>

---

## How C1-COMP Works

C1-COMP uses a hybrid architecture: </br>
all four compressor types share the same SSL G-Series control interface, 
but each engine internally locks certain characteristics to maintain its authentic compression behavior.</br>

Here's the signal flow:</br>

```
         Audio Input (L/R)
                 ↓
     ┌───────────────────────┐
     │   Compressor Type     │
     │   Selection           │
     │  • VCA (SSL G)        │
     │  • FET (1176)         │
     │  • Optical (LA-2A)    │
     │  • Vari-Mu (Fairchild)│
     └───────────┬───────────┘
                 ↓
     ┌───────────────────────┐
     │  SSL G-Series         │
     │  Controls             │
     │  • Attack: 6 times    │
     │  • Release: 100-1200ms│
     │  • Threshold: -20 to  │
     │    +10dB              │
     │  • Ratio: 1:1 to 20:1 │
     └───────────┬───────────┘
                 ↓
     ┌───────────────────────┐
     │  Engine-Specific      │
     │  Transformations      │
     │  (locked values)      │
     └───────────┬───────────┘
                 ↓
     ┌───────────────────────┐
     │  Compression Engine   │
     │  (Gain Reduction)     │
     └───────────┬───────────┘
                 ↓
     ┌───────────────────────┐
     │  Makeup Gain          │
     │  (optional auto)      │
     └───────────┬───────────┘
                 ↓
     ┌───────────────────────┐
     │  Dry/Wet Mix          │
     │  0% to 100%           │
     └───────────┬───────────┘
                 ↓
            Output (L/R)
                 ↓
      ┌──────────┴──────────┐
      ↓                     ↓
┌──────────────┐   ┌────────────────┐
│ Peak Meters  │   │  VU Meter (11) │
│ • IN (L/R)   │   │  Gain Reduction│
│ • GR (mono)  │   │  Display       │
│ • OUT (L/R)  │   │  -20dB to 0dB  │
└──────────────┘   └────────────────┘

Sidechain Mode (when SC input connected):
┌──────────────────────────┐
│  External Key (SC Input) │
└──────────┬───────────────┘
           ↓
┌──────────────────────────┐
│  Compression Detection   │
└──────────┬───────────────┘
           ↓
  Gain Reduction Applied
  to Main Audio Input
           ↓
      Output (L/R)
```

The SSL G-Series attack times (0.1ms, 0.3ms, 1ms, 3ms, 10ms, 30ms) serve as the baseline. </br>
Each engine then scales, limits, or transforms these values to match its character:</br>
- VCA uses values directly (SSL baseline)
- FET scales attack/release faster, adds distortion
- Optical enforces slower minimums, uses soft knee
- Vari-Mu enforces slowest minimums, doubles release, adds tube saturation

---

## Table of Contents

1. [What is C1-COMP?](#what-is-c1-comp)
2. [Getting Started](#getting-started)
3. [Compressor Types](#compressor-types)
4. [The Controls](#the-controls)
5. [The Peak Meters](#the-peak-meters)
6. [The VU Meter](#the-vu-meter)
7. [Sidechain Input](#sidechain-input)
8. [Context Menu Settings](#context-menu-settings)
9. [Console 1 MK2 Integration](#console-1-mk2-integration)
10. [Troubleshooting](#troubleshooting)

---

## What is C1-COMP?

C1-COMP is a dynamics processor that reduces the volume of loud signals while preserving quieter material. </br>
It provides four different compressor types, each with distinct sonic characteristics, </br>
all controlled through a unified SSL G-Series interface.</br>

### What C1-COMP Does for You

**Hybrid Architecture**:</br>
C1-COMP combines SSL G-Series control flexibility with authentic compression engines. </br>
The front panel provides SSL-style controls (6 discrete attack times, wide release range, broad threshold/ratio ranges), while each engine internally adapts these settings to maintain its authentic character.</br>

**Four Compression Engines**:</br>
- VCA (SSL G): Clean, transparent glue compression with peak detection
- FET (1176): Fast, punchy limiting with harmonic distortion
- Optical (LA-2A): Smooth, musical leveling with program-dependent release
- Vari-Mu (Fairchild): Gentle tube compression with warmth and glue

**Visual Feedback**:</br>
Three peak meters (IN, GR, OUT) show stereo input levels, gain reduction amount, and stereo output levels. </br>
The VU meter displays gain reduction intensity across 11 LEDs, matching the Console 1 hardware.</br>

**Parallel Compression**:</br>
The Dry/Wet control blends uncompressed and compressed signals, allowing parallel compression techniques within a single module.</br>

### Core Features

- **Threshold**: -20dB to +10dB range (SSL G-Series calibration)
- **Ratio**: 1:1 to 20:1 with quadratic taper for musical control
- **Attack**: 6 discrete settings (0.1ms, 0.3ms, 1ms, 3ms, 10ms, 30ms)
- **Release**: 100ms to 1200ms (logarithmic) or AUTO mode
- **Dry/Wet**: 0% to 100% parallel compression mix
- **Peak Metering**: IN (stereo), GR (mono inverted), OUT (stereo)
- **VU Meter**: 11-LED gain reduction display with dot or bar mode
- **Sidechain Input**: External key signal for ducking and creative effects
- **Bypass**: True bypass mode

---

## Getting Started

### What You Need

1. **Audio source** connected to C1-COMP inputs
2. **C1-COMP module** in a VCV Rack patch
3. **Optional**: C1 module for Console 1 MK2 hardware control

### First-Time Setup

**Step 1: Place C1-COMP in the Rack**</br>

Add C1-COMP to the patch. </br>
If you're using it with the C1 module and Console 1 MK2 hardware, place C1-COMP at position 3 (after CHAN-IN, SHAPE, and C1-EQ).</br>

**Step 2: Connect Your Audio**</br>

Plug your audio source into the LEFT and RIGHT inputs at the bottom of C1-COMP.</br>
- **Mono source**: Just connect to LEFT input - C1-COMP automatically processes both channels identically
- **Stereo source**: Connect both LEFT and RIGHT for true stereo compression

**Step 3: Connect the Outputs**</br>

Patch the LEFT and RIGHT outputs to the next module in the chain (typically CHAN-OUT) or directly to a mixer/output.</br>

**Step 4: Set Initial Settings**</br>

Start with:</br>
- **Compressor Type**: VCA (default)
- **Attack**: 3ms (position 3)
- **Release**: 400ms (mid-range)
- **Threshold**: 0dB (center position)
- **Ratio**: 4:1 (moderate compression)
- **Dry/Wet**: 100% (full compression)
- **Bypass**: Off

**Step 5: Check Your Meters**</br>

Play audio through C1-COMP and watch the meters:</br>
- IN meter shows input level
- GR meter shows gain reduction amount
- OUT meter shows output level after compression
- VU meter displays compression intensity

### What to Expect When It Works</br>

When everything is connected properly:</br>
- Audio passes through with compression applied based on the threshold
- The GR meter shows gain reduction when audio exceeds the threshold
- The VU meter LEDs light up showing compression intensity
- The OUT meter shows the compressed output level
- If you're using Console 1 MK2, the hardware encoders control C1-COMP parameters

---

## Compressor Types

C1-COMP provides four compressor engines. </br>
Select the type using the four rectangles at the top of the display area. </br>
Click a rectangle to activate that engine. </br>
The active engine shows an amber checkmark.</br>

### VCA (SSL G)

**Character**: </br>
Clean, transparent compression</br>

**What it does**:</br>
The VCA engine models the SSL G-Series bus compressor. </br>
It uses peak detection (not RMS) and provides fast, accurate gain reduction without coloration. </br>
This is the baseline SSL configuration that the other engines derive from.</br>

**Locked Characteristics**:</br>
- Attack times: Used directly as selected (0.1-30ms)</br>
- Knee: Hard knee (0dB)</br>
- Detection: Peak</br>
- Release: 100-1200ms or AUTO mode (program-dependent)</br>

**When to use**:</br>
Use VCA for transparent glue compression on mix buses, drums, or any material where you want gain reduction without adding color. </br>
VCA provides the most direct, uncolored compression response.</br>

---

### FET (1176)

**Character**: </br>
Fast, aggressive compression with harmonic distortion</br>

**What it does**:</br>
The FET engine models the UREI 1176 limiting amplifier.</br> 
It uses RMS detection with ultra-fast attack and release times, and adds harmonic distortion during compression. </br>
The attack is much faster than VCA, and the release is scaled for punchy material.</br>

**Locked Characteristics**:</br>
- Attack times: Remapped to ultra-fast range (0.02-0.8ms instead of 0.1-30ms)
- Release: Divided by 3 (faster than SSL setting)
- Knee: Hard knee (0dB, inherited from SSL)
- Detection: RMS with 5ms time constant
- Distortion: 15% non-linear saturation added during compression

**When to use**:</br>
Use FET for drums, bass, vocals, or any material that needs aggressive, punchy compression. </br>
The ultra-fast attack catches transients, and the harmonic distortion adds presence and edge.</br>

---

### Optical (LA-2A)

**Character**: </br>
Smooth, musical compression with program-dependent release</br>

**What it does**:</br>
The Optical engine models the Teletronix LA-2A leveling amplifier.</br> 
It uses RMS detection with a slower attack than VCA or FET, and features an opto-resistor simulation that creates program-dependent release behavior. </br>
The release time varies based on how much compression is happening.</br>

**Locked Characteristics**:</br>
- Attack times: Minimum 10ms enforced (slower than SSL allows)
- Ratio: Capped at 10:1 maximum (gentler than SSL's 20:1 range)
- Knee: Soft knee (6dB default instead of hard)
- Detection: RMS with 10ms time constant
- Release: Program-dependent (varies with compression depth)

**When to use**:</br>
Use Optical for vocals, bass, or any material that needs smooth, transparent compression with natural release characteristics. </br>
The opto simulation prevents pumping and creates a musical "breathing" effect.</br>

---

### Vari-Mu (Fairchild)

**Character**: </br>
Very slow, gentle tube compression with warmth</br>

**What it does**:</br>
The Vari-Mu engine models the Fairchild 670 variable-mu tube compressor. </br>
It uses RMS detection with the slowest attack and release times of all four engines, and adds tube saturation for harmonic warmth. </br>
The ratio is capped at gentle settings, and the knee is extra-soft.</br>

**Locked Characteristics**:</br>
- Attack times: Minimum 20ms enforced (slowest of all engines)
- Release: Doubled (2× slower than SSL setting)
- Ratio: Capped at 6:1 maximum (gentlest)
- Knee: Extra-soft knee (12dB default)
- Detection: RMS with 20ms time constant (slowest)
- Saturation: 25% tube harmonics added during compression

**When to use**:</br>
Use Vari-Mu for mix bus glue, mastering, or any material that needs gentle, smooth compression with added warmth. </br>
The slow attack and release prevent transient distortion, and the tube saturation adds vintage character.</br>

---

## The Controls

### Bypass (Button in Upper Left)

---

### Attack (Top Right Encoder)

**What it does**: </br>
Controls how fast the compressor responds when audio exceeds the threshold</br>

**Range**: 6 discrete positions (SSL G-Series attack times)</br>
- Position 0: 0.1 ms (fastest)
- Position 1: 0.3 ms
- Position 2: 1 ms
- Position 3: 3 ms
- Position 4: 10 ms
- Position 5: 30 ms (slowest)

**LED Ring**: </br>
Shows 6 discrete LED positions with time labels (0.1, 0.3, 1.0, 3.0, 10, 30)</br>

**How each engine uses attack**:</br>
- VCA: Uses these times directly
- FET: Remaps to ultra-fast range (0.02-0.8ms)
- Optical: Enforces minimum 10ms (positions 0-3 become 10ms)
- Vari-Mu: Enforces minimum 20ms (positions 0-4 become 20ms)

**When to adjust**:</br>
Fast attack (0.1-1ms) catches transients and reduces peaks. </br>
Slow attack (10-30ms) lets transients through while compressing the body. </br>
Choose based on the material and the engine's natural speed.</br>

---

### Release (Middle Right Encoder)

**What it does**: </br>
Controls how long the compressor takes to return to zero gain reduction after audio falls below threshold</br>

**Range**: 100ms to 1200ms (logarithmic) or AUTO mode (90-100% range)</br>

**LED Ring**: </br>
Smooth tracking for 0-90%, alternating animation for AUTO mode (90-100%)</br>

**How to use it**:</br>
- 0-90% range: Continuous release time from 100ms to 1200ms
- 90-100% range: AUTO mode (program-dependent release)

**AUTO Mode Behavior**:</br>
- VCA: Adapts release based on how fast gain reduction changes (100-1200ms range)
- FET: Not used (FET doesn't implement AUTO)
- Optical: Already program-dependent (AUTO has no additional effect)
- Vari-Mu: Slows release even further during heavy compression (1-3× slower)

**When to adjust**:</br>
Fast release (100-300ms) for drums and transient material. 
Medium release (300-800ms) for most musical content. 
Slow release (800-1200ms) for smooth, sustained compression. 
AUTO mode adapts to the program material dynamically.

---

### Threshold (Bottom Right Encoder)

**What it does**: </br>
Sets the level above which compression begins</br>

**Range**: -20dB to +10dB (SSL G-Series range)</br>

**Default**: </br>
0dB (center position at 66.7% knob rotation)</br>

**How to use it**:</br>
- Turn clockwise to raise threshold (less compression)
- Turn counter-clockwise to lower threshold (more compression)
- Watch the GR meter to see when compression starts

**When to adjust**:</br>
Set the threshold so the peaks of your audio trigger compression. </br>
Lower the threshold until you see the desired amount of gain reduction on the GR meter. </br>
The VU meter shows the compression intensity in real-time.</br>

---

### Ratio (Top Left Encoder)

**What it does**: </br>
Controls how much gain reduction is applied for every dB above the threshold</br>

**Range**: 1:1 to 20:1 with quadratic taper</br>

**Quadratic Taper**:</br>
The ratio uses a quadratic (squared) curve for musical control:</br>
- 25% knob position: ~2:1 (gentle compression)
- 50% knob position: ~4:1 (moderate compression)
- 75% knob position: ~8:1 (heavy compression)
- 100% knob position: 20:1 (limiting)

**How each engine limits ratio**:</br>
- VCA: Full range (1:1 to 20:1)
- FET: Full range (1:1 to 20:1)
- Optical: Capped at 10:1 maximum
- Vari-Mu: Capped at 6:1 maximum

**When to adjust**:</br>
Low ratios (1:1 to 3:1) provide subtle gain control. </br>
Medium ratios (3:1 to 6:1) are standard for musical compression. High ratios (6:1 to 20:1) create limiting and peak control. </br>
Vari-Mu and Optical automatically constrain the ratio to maintain authentic character.</br>

---

### Dry/Wet (Bottom Left Encoder)

**What it does**: </br>
Blends uncompressed (dry) and compressed (wet) signals</br>

**Range**: 0% to 100%</br>

**How it works**:</br>
- 0%: Fully dry (no compression)
- 50%: Equal blend of dry and compressed signals (parallel compression)
- 100%: Fully wet (standard compression)

**When to use**:</br>
Parallel compression (50-80% wet) allows you to add heavy compression without completely losing dynamics. </br>
This technique is common on drums, vocals, and mix buses. </br>
Set aggressive compression (high ratio, low threshold), then blend to taste.</br>

---

## The Peak Meters

C1-COMP provides three horizontal bar meters showing input levels, gain reduction, and output levels. </br>
These meters use peak detection with 300ms decay and 0.5-second peak hold (white lines).</br>

### IN Meter (Top Bar)

**What it shows**: </br>
Stereo input levels before compression</br>

**Range**: -60dB to +6dB</br>

**How to read it**:</br>
- Two bars (top = left channel, bottom = right channel)
- Amber gradient from dark (low level) to bright (high level)
- Grey vertical line at 0dB reference
- White peak hold lines show recent peaks

**When to watch it**:</br>
Check the IN meter to see if your audio is reaching the threshold. 
If the input level is below the threshold setting, no compression will occur.

---

### GR Meter (Middle Bar)

**What it shows**: </br>
Gain reduction amount (mono, inverted display)

**Range**: 0dB to -20dB (displayed right-to-left)</br>

**How to read it**:</br>
- Single bar showing combined L/R gain reduction
- Fills from right (0dB, no compression) to left (-20dB, heavy compression)
- White peak hold line shows recent maximum GR

**When to watch it**:</br>
The GR meter shows how much compression is happening. 
Light compression (1-3dB) appears as a short bar from the right. 
Heavy compression (10-20dB) fills most of the bar. 
This meter matches the VU meter below.

---

### OUT Meter (Bottom Bar)

**What it shows**: </br>
Stereo output levels after compression and makeup gain

**Range**: -60dB to +6dB</br>

**How to read it**:</br>
- Two bars (top = left, bottom = right)
- Amber gradient from dark (low level) to bright (high level)
- Grey vertical line at 0dB reference
- White peak hold lines show recent peaks

**When to watch it**:</br>
Check the OUT meter to verify your output level matches your target. </br>
If using auto makeup gain, the OUT meter should be similar to IN meter. </br>
If not using makeup, the OUT meter will be lower than IN during compression.</br>

---

## The VU Meter

The VU meter (11 red LEDs below the peak meters) displays gain reduction intensity in real-time, matching the leds on the Console 1 hardware</br>

### What It Shows

**Range**: -20dB (left) to 0dB (right)</br>

**Scale**:</br>
- LED 0 (leftmost): -20dB (heavy compression)
- LED 5 (center): -10dB
- LED 10 (rightmost): 0dB (no compression)

**Spacing**: 2dB per LED</br>

### VU meter modes

**Dot Mode** (default):</br>
Only the current gain reduction LED lights up. This shows the exact GR amount.</br>

**Bar Mode** (context menu option):</br>
All LEDs from LED 0 up to the current GR position light up. This shows the compression "depth" visually.</br>


### Display Toggle

Click the small switch in the upper-right corner of the display to turn it on or off:</br>
- **Amber fill**: Display is on
- **X mark**: Display is off

The switch shows at 50% opacity normally, 100% opacity when you hover over it.</br>



---

## Sidechain Input

The SC (sidechain) input allows the compressor to respond to a different external signal (audio/cv) instead of the audio input.</br>

### How It Works

**Normal Mode** (nothing connected to SC):</br>
The compressor detects the main audio input (L/R) to determine when to compress.</br>

**Sidechain Mode** (cable connected to SC):</br>
The compressor listens to the sidechain input and applies the gain reduction to the main audio input.</br>

### When to Use Sidechain

**Example 1: Ducking**</br>
Connect a kick drum to the sidechain input. </br>
The compressor reduces the bass or pads every time the kick hits, creating pumping or ducking effects.</br>

**Example 2: De-essing**</br>
Filter the vocal signal (boost high frequencies), send the filtered signal to the sidechain input, and send the unfiltered vocal to the main input. </br>
The compressor responds only to sibilance but reduces the entire vocal.</br>

**Example 3: Frequency-Selective Compression**</br>
Send an EQ'd version of your audio to the sidechain input. </br>
The compressor responds to specific frequency ranges while processing the full-bandwidth signal.</br>

---

## Context Menu Settings

Right-click the module to access these settings:</br>

### Auto Makeup Gain

**Range**: Off or On (default: Off)</br>

**What it does**: </br>
Automatically compensates for threshold-based gain reduction</br>

**How it works**: </br>
Applies makeup gain equal to `-threshold × 0.5`</br>
- Threshold at -20dB → ~10dB makeup gain
- Threshold at 0dB → 0dB makeup gain
- Threshold at +10dB → -5dB makeup gain

**When to use**: </br>
Enable for quick gain compensation during mixing. </br>
Disable for precise manual control via output gain slider.</br>

---

### Input Gain

**Range**: -24dB to +24dB (slider control)</br>
**Default**: 0dB</br>

**What it does**: </br>
Applies gain before compression</br>

**When to adjust**: </br>
Increase input gain if your audio is too quiet to trigger compression. </br>
Decrease input gain if your audio is too hot and causes excessive gain reduction.</br>

---

### Output Gain

**Range**: -24dB to +24dB (slider control)</br>
**Default**: 0dB</br>

**What it does**: </br>
Applies gain after compression (makeup gain)</br>

**When to adjust**: </br>
Increase output gain to compensate for gain reduction. This is manual makeup gain when Auto Makeup is disabled.</br>

---

### Knee

**Range**: </br>
Auto (default) or 0dB to 12dB (slider control)</br>

**What it does**: </br>
Overrides the engine's default knee width</br>

**Default Knee Values** (when set to Auto):</br>
- VCA: 0dB (hard knee)
- FET: 0dB (hard knee)
- Optical: 6dB (soft knee)
- Vari-Mu: 12dB (extra-soft knee)

**How knee works**:</br>
- Hard knee (0dB): Compression starts abruptly at the threshold
- Soft knee (6-12dB): Compression starts gradually before the threshold, reaching full ratio above the threshold

**When to override**: </br>
Set a specific knee value to change the compression curve. </br>
Soft knee (6-12dB) creates smoother, more transparent compression. </br>
Hard knee (0dB) provides precise, aggressive compression.</br>

---

### Input Reference Level

**Options**: </br>
0dBFS = 5V (default) or 0dBFS = 10V</br>

**What it does**: </br>
Changes the voltage-to-dB conversion for input signals</br>

**5V Reference** (default):</br>
Maps VCV Rack's typical ±5V audio range to the threshold scale. </br>
Calibrated for standard VCV patches where kick drums peak around 5V.</br>

**10V Reference**:
Maps ±10V signals to the threshold scale. </br>
Use this if your audio signals are hotter than normal (±10V range).</br>

**When to change**: </br>
If compression seems too sensitive or not sensitive enough, try switching the reference level. </br>
Most patches work correctly with 5V reference.</br>

---

### VU Meter Mode

**Options**: </br>
Dot Mode (default) or Bar Mode</br>

**Dot Mode**:</br>
Only the current gain reduction LED lights up. </br>
Shows the exact GR amount.</br>

**Bar Mode**:</br>
All LEDs from the left up to the current GR position light up. </br>
Shows compression "depth" visually.</br>

---

## Console 1 MK2 Integration

When you connect C1-COMP to the C1 module and Console 1 MK2 hardware, you get hands-on control with bidirectional feedback.</br>

### Hardware Control Mapping

**C1-COMP uses 5 encoders + 1 button**:</br>

**1. BYPASS (Button)**:</br>
- Press to toggle bypass on/off
- Button LED lights when bypassed
- Same as clicking the bypass button on C1-COMP panel
- Console 1 MK2 CC 46

**2. ATTACK (Encoder)**:</br>
- Turn to cycle through 6 discrete attack times (0.1, 0.3, 1.0, 3.0, 10, 30 ms)
- LED ring shows 6 discrete positions with time labels
- Custom attack LED ring with labeled positions
- Console 1 MK2 CC 51

**3. RELEASE (Encoder)**:
- Turn to adjust release time (100ms to 1200ms) or AUTO mode
- LED ring shows smooth tracking for 0-90%, alternating animation for AUTO (90-100%)
- Custom release LED ring with smooth tracking + AUTO animation
- Console 1 MK2 CC 48

**4. THRESHOLD (Encoder)**:</br>
- Turn to adjust compression threshold (-20dB to +10dB)
- LED ring shows current threshold position
- 0dB at 66.7% rotation (center position)
- Console 1 MK2 CC 47

**5. RATIO (Encoder)**:</br>
- Turn to adjust compression ratio (1:1 to 20:1, quadratic taper)
- LED ring shows current ratio position
- Quadratic scaling for musical control
- Console 1 MK2 CC 49

**6. DRY/WET (Encoder)**:</br>
- Turn to adjust parallel compression mix (0% to 100%)
- LED ring shows current mix position
- 0% = fully dry, 100% = fully wet
- Console 1 MK2 CC 50

All encoders have LED rings showing current parameter positions, updating in real-time when you change parameters in VCV Rack or on the hardware.</br>

### Parameter Synchronization

**When you load a patch**:</br>
C1 sends all current parameter values to the Console 1 MK2 hardware.</br>
The LED rings immediately show the correct positions - no need to move every knob to "pick up" the parameters.</br>

**Bidirectional control**:</br>
- Turn a hardware encoder → C1-COMP parameter updates in VCV Rack
- Move a parameter in VCV Rack → Hardware LED ring updates to match

Everything stays synchronized automatically.</br>

### C1-COMP Position in Chain

**Position 3**:</br>
When using Standard mode, C1-COMP must be placed at position 3 (after CHAN-IN, SHAPE, and C1-EQ).</br>

**Detection**:</br>
C1's fourth module LED (green) indicates C1-COMP is detected and active.</br>

---

## Troubleshooting

### Problem: No Compression Happening

**Causes**:</br>
- Threshold is set too high
- Input level is too low
- Module is bypassed
- Ratio is set to 1:1

**Solutions**:</br>
- Lower the threshold until the GR meter shows activity
- Check the IN meter - if the signal is below -20dB, increase input gain in context menu
- Verify bypass button LED is off (not amber)
- Increase ratio above 1:1 (turn Ratio knob clockwise)
- If using sidechain, verify the SC input has a signal

---

### Problem: Compression is Too Aggressive

**Causes**:</br>
- Threshold is too low
- Ratio is too high
- Attack is too fast
- Input gain is too high

**Solutions**:</br>
- Raise the threshold until gain reduction is 3-6dB on peaks
- Reduce the ratio to 2:1 or 3:1
- Use a slower attack time (10ms or 30ms) to let transients through
- Reduce input gain in context menu
- Try Vari-Mu or Optical engine for gentler compression

---

### Problem: Compressor Pumps or Breathes

**Causes**:</br>
- Release is too fast
- Too much gain reduction (threshold too low or ratio too high)
- Using hard knee when soft knee would be better

**Solutions**:</br>
- Increase release time to 500-1200ms or use AUTO mode
- Reduce gain reduction to 3-6dB by raising threshold or lowering ratio
- Switch to Optical or Vari-Mu engine (program-dependent release prevents pumping)
- Override knee to 6-12dB in context menu for softer compression curve

---

### Problem: Output Level Drops Too Much

**Causes**:</br>
- Makeup gain not applied
- Output gain set too low

**Solutions**:</br>
- Enable Auto Makeup Gain in context menu
- Increase Output Gain slider in context menu
- Check the OUT meter - adjust Output Gain until it matches the IN meter level

---

## Copyright and Legal

**Copyright © 2025 Twisted Cable. Licensed under GPL-3.0-or-later.**</br>

---

**Manual Version**: 1.0</br>
**Last Updated**: November 2025</br>
**Author**: Latif Karoumi / Twisted Cable</br>
