# SHAPE Module User Manual

SHAPE is a noise gate module for the C1-ChannelStrip system.</br>
This manual covers everything you need to know to use SHAPE effectively.</br></br>
<img src="/docs/img/SHAPE.jpeg" style="float:right; margin:0 0 1em 1em;" width="10%" alt="SHAPE"> </br></br>
**Product**: SHAPE</br>
**Type**: 8HP Noise Gate Module</br>
**Part of**: C1-ChannelStrip Plugin (GPL-3.0-or-later)</br>
**Manual Version**: 1.0</br>
**Date**: November 2025</br>

---

## How SHAPE Works

SHAPE uses envelope following to detect when audio crosses a threshold, </br>
then opens or closes a gate to control what passes through. </br>

Here's the signal flow:</br>

```
┌──────────────────────────┐
│   Audio Input (L/R)      │
│   or SC Input (when      │
│   sidechain connected)   │
└──────────┬───────────────┘
           ↓
┌──────────────────────────┐
│   Envelope Follower      │
│  • Attack: 0.1-25ms      │
│  • Tracks amplitude      │
└──────────┬───────────────┘
           ↓
┌──────────────────────────┐
│  Threshold Detection     │
│    -60dB to 0dB          │
│  • 5V/10V reference      │
└──────────┬───────────────┘
           ↓
      Gate Opens?
           │
      ┌────┴────┐
      │         │
     No        Yes
      │         │
      ↓         ↓
  ┌────────┐  ┌──────────────────────┐
  │ Close  │  │   Hold Open          │
  │ Gate   │  │  (Sustain: 0-300ms)  │
  └───┬────┘  └──────────┬───────────┘
      │                  ↓
      │         ┌──────────────────────┐
      │         │  Punch Enhancement   │
      │         │  • 0% to 100%        │
      │         │  • 15ms decay        │
      │         └──────────┬───────────┘
      │                    ↓
      │         ┌──────────────────────┐
      │         │   Gate Fully Open    │
      │         │  • Soft: Squared     │
      │         │  • Hard: Complete    │
      │         └──────────┬───────────┘
      │                    │
      └────────────┬───────┘
                   ↓
          ┌────────────────┐
          │  Release Time  │
          │   0.1s to 4s   │
          │  • 6 curves    │
          └────────┬───────┘
                   ↓
          ┌────────────────┐
          │  Gate Output   │
          │   (L/R Audio)  │
          └────────┬───────┘
                   ↓
      ┌────────────┴────────────┐
      ↓                         ↓
┌─────────────────┐   ┌──────────────────┐
│ Waveform Display│   │  VU Meter (11)   │
│ • BEAT: 100ms   │   │  Processing      │
│ • ENV: 1s       │   │  Intensity       │
│ • BAR: 2s       │   │  Indicator       │
│ • PHRASE: 4s    │   │                  │
└─────────────────┘   └──────────────────┘
```

When audio exceeds the threshold, the gate opens and lets the signal through. </br>
When audio falls below the threshold, the gate holds open for the sustain time, then closes according to the release curve. </br>
Punch adds transient emphasis when the gate opens.</br>

---

## Table of Contents

1. [What is SHAPE?](#what-is-shape)
2. [The Controls](#the-controls)
3. [The Waveform Display](#the-waveform-display)
4. [The VU Meter](#the-vu-meter)
5. [Sidechain Input](#sidechain-input)
6. [Context Menu Settings](#context-menu-settings)
7. [Console 1 MK2 Integration](#console-1-mk2-integration)
8. [Troubleshooting](#troubleshooting)

---

## What is SHAPE?

SHAPE is a noise gate that removes unwanted audio below a threshold while preserving the material you want to keep. </br>
It provides dynamic control through envelope-based gating with visual feedback.</br>

### What SHAPE Does for You

**Envelope Following**:</br>
SHAPE continuously tracks the amplitude of your audio. When the signal crosses the threshold, the gate responds. </br>
Fast attack (0.1ms default) ensures transients pass through cleanly.</br>

**Sustain Hold**:</br>
After audio crosses the threshold, SHAPE holds the gate open for a set time. </br>
This prevents the gate from chattering when audio hovers around the threshold.</br>

**Punch Enhancement**:</br>
Adds transient emphasis when the gate opens. Use this to make drums and percussive material more prominent. </br>
The punch effect decays in 15ms.</br>

**Visual Feedback**:</br>
The waveform display shows the gate output in real-time. The VU meter shows how much processing is happening.</br>

### Core Features

- **Threshold**: -60dB to 0dB range
- **Release**: 0.1s to 4s with 6 different curve types
- **Sustain**: 0ms to 300ms hold time
- **Punch**: 0% to 100% transient boost
- **Soft/Hard Gate**: Gradual reduction or complete cutoff
- **Sidechain Input**: Gate based on external signal instead of main input (audio/CV)
- **Bypass**: True bypass mode

---

## The Controls

### Gate Threshold (Left Encoder)

**What it does**: </br>
Sets the level at which the gate opens</br>

**Range**: -60dB to 0dB</br>

**How to use it**:</br>
- Turn clockwise to increase threshold (gate opens for louder signals only)
- Turn counter-clockwise to decrease threshold (gate opens for quieter signals)
- Watch the LED ring to see the current threshold position

**When to adjust**:</br>
Set the threshold just below the level of the audio you want to keep. If the gate cuts off too much, lower the threshold. </br>
If unwanted signals get through, raise the threshold.</br>

---

### Release (Top Right Encoder)

**What it does**: </br>
Controls how long the gate takes to close after audio falls below threshold</br>

**Range**: 0.1s to 4s</br>

**How to use it**:</br>
- Fast release (0.1s - 0.5s): Gate closes quickly, good for drums and percussion</br>
- Medium release (0.5s - 2s): Natural decay, good for most material</br>
- Slow release (2s - 4s): Smooth transitions, good for vocals and sustained instruments</br>

**Note**: </br>
The release curve can be changed in the context menu (Linear, Exponential, Logarithmic, SSL G-Series, DBX 160X, Drawmer DS201).</br>

---

### Sustain (Middle Right Encoder)

**What it does**: </br>
Holds the gate open for a set time after audio crosses threshold</br>

**Range**: </br>
0ms to 300ms</br>

**How to use it**:</br>
- 0ms: Gate responds immediately to threshold crossings (can cause chattering)
- 50-150ms: Prevents rapid gate opening/closing on rhythmic material
- 200-300ms: Longer hold for slower material or creative effects

**When to adjust**:</br>
If the gate opens and closes rapidly (chattering), increase sustain. </br>
This gives the audio time to stabilize before the gate can close again.</br>

---

### Punch (Bottom Right Encoder)

**What it does**: </br>
Adds transient emphasis when the gate opens</br>

**Range**: 0% to 100%</br>

**How to use it**:</br>
- 0%: No punch, natural gate behavior
- 30-50%: Subtle transient emphasis
- 70-100%: Strong punch for making drums pop

**Technical note**: </br>
Punch multiplies the gate gain by (1 + punch amount) during gate opening, then decays in 15ms.</br>

---

### Hard Gate (Button Below Gate Encoder)

**What it does**: </br>
Switches between soft and hard gating</br>

**Modes**:</br>
- **Soft Gate** (LED off): Gradual signal reduction using squared curve transition
- **Hard Gate** (LED amber): Complete signal cutoff below threshold

**When to use each**:</br>
- Use soft gate for musical gating with smooth transitions
- Use hard gate for complete noise removal or creative hard cuts

---


## The Waveform Display

The waveform display shows the gate output in real-time. </br>
This helps you see exactly what's happening to your audio.</br>

### Display Toggle

Click the small switch in the upper-right corner of the display to turn it on or off:</br>
- **Amber fill**: Display is on
- **X mark**: Display is off

The switch shows at 50% opacity normally, 100% opacity when you hover over it.</br>

---

### Time Window Selection

Four small squares to the left of the display select the time scale:</br>

- **BEAT**: 100ms (transient analysis)
- **ENV**: 1s (envelope visualization, default)
- **BAR**: 2s (beat patterns)
- **PHRASE**: 4s (musical phrases)

Click a square to select that time window. The active window shows an amber checkmark.</br>

**When to use each**:</br>
- Use BEAT for tuning attack characteristics and seeing fast transients
- Use ENV for most gating work, shows envelope clearly
- Use BAR to see rhythmic patterns
- Use PHRASE for longer musical material

---

### What You're Seeing

The display shows min/max amplitude pairs forming a filled waveform polygon. </br>
When the gate closes, you'll see the waveform drop to zero. </br>
When the gate opens with punch, you'll see the transient spike.</br>

The display scrolls from right to left, with newer audio on the right.</br>

---

## The VU Meter

The VU meter (11 red LEDs below the waveform display) shows **processing intensity**, not input level.</br>

### What It Measures

Processing intensity is calculated from three factors:</br>

1. **Level Difference** (40% weight): How much the gate changes the signal level
2. **Crest Factor Change** (30% weight): How much the gate affects the transient-to-RMS ratio
3. **Dynamic Difference** (30% weight): Historical level changes

### Reading the Meter

- **Leftmost LED (-)**: Minimal processing (gate mostly open)
- **Center LEDs**: Moderate gating activity
- **Rightmost LED (+)**: Heavy processing (gate closing frequently or deeply)

If the meter stays at minimum, the gate isn't doing much. If it stays at maximum, the gate is closing aggressively.</br>

---

## Sidechain Input

The SC (sidechain) input lets you gate the main audio based on an external signal instead of the audio itself.</br>

### How It Works

**Normal Mode** (nothing connected to SC):</br>
The gate uses the main input signal to detect when to open and close.</br>

**Sidechain Mode** (cable connected to SC):</br>
The gate listens to the sidechain input for threshold detection, but applies the gating to the main audio input.</br>

### When to Use Sidechain

**Example 1: Ducking**</br>
Connect a kick drum to the sidechain input. </br>
Every time the kick hits, the gate closes on the main audio (bass, pads, etc.), creating a pumping effect.</br>

**Example 2: Rhythmic Gating**</br>
Send a rhythmic signal to the sidechain input (this can be audio or cv/gate/trigger)</br>
The gate follows that rhythm and applies it to sustained material on the main input.</br>

---

## Context Menu Settings

Right-click the module to access these settings:</br>

### Attack

**Range**: 0.1ms to 25ms (slider control)</br>
**Default**: Auto (0.1ms)</br>

The attack time controls how fast the gate opens. </br>
The default (0.1ms) ensures fast transients pass through cleanly. </br>
Increase for smoother gate opening.</br>

---

### Release Curves

Six different release curve types change how the gate closes:</br>

1. **Linear (Default)**: Standard -2.2dB curve, neutral release
2. **Exponential**: Faster release (-4.6dB curve) for punchy material
3. **Logarithmic**: Slower release (-1.1dB curve) for smooth transitions
4. **SSL G-Series**: Classic console behavior (-1.5dB curve)
5. **DBX 160X**: Vintage compressor-style release (-5.0dB curve)
6. **Drawmer DS201**: Gate-specific release characteristic (-1.0dB curve)

Try different curves to find what sounds best for your source material.</br>

---

### Threshold Reference

Two reference levels change how the threshold maps to VCV Rack voltages:</br>

- **5V Reference (Subtle)**: Maps -60dB to 0dB → 0V to 4V range
- **10V Reference (Prominent)**: Maps -60dB to 0dB → 0V to 8V range

The 5V reference is calibrated for typical VCV Rack signals where kicks peak around 5V. </br>
The 10V reference works better for hotter signals.</br>

---

## Console 1 MK2 Integration

When you connect SHAPE to the C1 module and Console 1 MK2 hardware, you get hands-on control with bidirectional feedback.</br>

### Hardware Control Mapping

**SHAPE uses 4 encoders + 2 buttons**:</br>

**1. THRESHOLD (Encoder)**:</br>
- Turn to adjust gate threshold (-60dB to 0dB)
- LED ring shows current threshold position
- Real-time updates when you change the parameter in VCV Rack

**2. RELEASE (Encoder)**:</br>
- Turn to adjust release time (0.1s to 4s)
- LED ring shows release time position
- Longer = slower gate closing

**3. SUSTAIN (Encoder)**:
- Turn to adjust hold time (0ms to 300ms)
- LED ring shows sustain position
- Higher = longer hold before release

**4. PUNCH (Encoder)**:
- Turn to adjust transient enhancement (0% to 100%)
- LED ring shows punch amount
- Higher = more transient emphasis

**5. BYPASS (Button)**:</br>
- Press to toggle bypass on/off
- Button LED lights when bypassed
- Same as clicking the bypass button on SHAPE panel

**6. HARD GATE (Button)**:</br>
- Press to toggle between soft and hard gating
- Button LED lights when hard gate is active
- Same as clicking the hard gate button on SHAPE panel

### Parameter Synchronization

**When you load a patch**:
C1 sends all current parameter values to the Console 1 MK2 hardware.
The LED rings immediately show the correct positions - no need to move every knob to "pick up" the parameters.

**Bidirectional control**:</br>
- Turn a hardware encoder → SHAPE parameter updates in VCV Rack
- Move a parameter in VCV Rack → Hardware LED ring updates to match

Everything stays synchronized automatically.</br>

### SHAPE Position in Chain

**Position 1**:</br>
When using Standard mode, SHAPE must be placed at position 1 (immediately to the right of CHAN-IN).</br>

**Detection**:</br>
C1's second module LED (green) indicates SHAPE is detected and active.</br>



---



## Troubleshooting

### Problem: Gate Chatters (Opens and Closes Rapidly)

**Causes**:</br>
- Threshold is set right at the signal level
- Sustain is too short
- Audio has fluctuating level near threshold

**Solutions**:</br>
- Increase sustain to 100ms - 200ms (holds gate open longer)
- Adjust threshold slightly higher or lower to avoid the fluctuating zone
- Use soft gate mode for smoother transitions

---

### Problem: Gate Cuts Off Transients

**Causes**:</br>
- Attack time is too slow
- Threshold is too high

**Solutions**:</br>
- Set attack to Auto (0.1ms) in the context menu
- Lower the threshold until transients pass through
- Check the waveform display on BEAT time window to see transients

---

### Problem: Background Noise Still Audible

**Causes**:</br>
- Threshold is too low
- Release is too slow
- Using soft gate instead of hard gate

**Solutions**:</br>
- Raise threshold until noise stays below it
- Use hard gate mode for complete cutoff
- Decrease release time so gate closes faster
- Check 10V Reference if signals are hot

---

### Problem: Gate Doesn't Respond to Audio

**Causes**:</br>
- Module is bypassed
- Threshold is set too high
- No audio reaching the module
- Sidechain mode active but no SC signal

**Solutions**:</br>
- Check bypass button (LED should be off)
- Lower threshold to -60dB and test
- Verify cables are connected to inputs and outputs
- Disconnect sidechain input if not intentionally using it


---


## Copyright and Legal

**Copyright © 2025 Twisted Cable. Licensed under GPL-3.0-or-later.**</br>

---

**Manual Version**: 1.0</br>
**Last Updated**: November 2025</br>
**Author**: Latif Karoumi / Twisted Cable</br>
