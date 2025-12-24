# CHAN-IN Module User Manual

CHAN-IN is a signal leveling module for the C1-ChannelStrip system.</br>
This manual will guide you through everything you need to know to get clean, controlled audio into the channel strip.</br></br>
<img src="/docs/img/CHAN-IN.jpeg" style="float:right; margin:0 0 1em 1em;" width="10%" alt="CHAN-IN"> </br></br>
**Product**: CHAN-IN</br>
**Type**: 8HP Input Module</br>
**Part of**: C1-ChannelStrip Plugin</br>
**Manual Version**: 1.0</br>
**Date**: November 2025</br>

---

## How CHAN-IN Works

CHAN-IN is the first stage in the signal chain, preparing incoming audio for processing.</br>
Here's what happens to your audio:</br>

```
    Audio Input (Left/Right)
              ↓
    ┌─────────────────────┐
    │   VCA Gain Stage    │
    │  (-60dB to +6dB)    │
    │   Anti-pop slew     │
    └─────────────────────┘
              ↓
    ┌─────────────────────┐
    │   Low Cut Filter    │
    │    (20-500Hz)       │
    │  Removes rumble     │
    └─────────────────────┘
              ↓
    ┌─────────────────────┐
    │   High Cut Filter   │
    │   (1kHz-20kHz)      │
    │  Reduces highs      │
    └─────────────────────┘
              ↓
    ┌─────────────────────┐
    │   Phase Control     │
    │  (Polarity flip)    │
    └─────────────────────┘
              ↓
    Audio Output + Metering
    (VU meters + horizontal display)
```

---

## Table of Contents

1. [What is CHAN-IN?](#what-is-chan-in)
2. [Getting Started](#getting-started)
3. [The Panel Controls](#the-panel-controls)
4. [Understanding the Meters](#understanding-the-meters)
5. [Working with Filters](#working-with-filters)
6. [Phase Control](#phase-control)
7. [Console 1 MK2 Integration](#console-1-mk2-integration)
8. [Troubleshooting](#troubleshooting)


---

## What is CHAN-IN?

CHAN-IN is an input module that sits at the beginning of the channel strip. </br>
It's where your audio enters the C1-ChannelStrip processing chain.</br>

### What CHAN-IN Does for You

**Gain Control**:</br>
Adjust incoming levels from -60dB (almost silent) to +6dB (boosted).</br>

**Filtering**:</br>
Two filters let you remove unwanted low frequencies (rumble, handling noise) and reduce high frequencies before they hit the EQ and compressor.

**Phase Alignment**:</br>
The phase button lets you flip the polarity to fix phase cancellation issues.

**Multi-Mode Metering**:</br>
Three different meter types (RMS, VU, PPM) in the display area, let you monitor your levels the way you prefer.</br>
Vertical VU meters show you at a glance where your signal sits.</br>

### Why You Need It

Think of CHAN-IN as your signal's first stop. It ensures:</br>
- Your audio enters at the right level (not too hot, not too quiet)
- Unwanted frequencies are removed before processing
- Phase relationships are correct
- You can check what's happening with your levels

---

## Getting Started

### What You Need

1. **Audio source** connected to CHAN-IN inputs
2. **CHAN-IN module** in a VCV Rack patch
3. **Optional**: C1 module for Console 1 MK2 hardware control

### First-Time Setup

**Step 1: Place CHAN-IN in the Rack**</br>

Add CHAN-IN to the patch. </br>
If you're using it with the C1 module and Console 1 MK2 hardware, place CHAN-IN directly to the right of C1 (position 0).</br>

**Step 2: Connect Your Audio**</br>

Plug your audio source into the LEFT and RIGHT inputs at the bottom of CHAN-IN.</br>
- **Mono source**: Just connect to LEFT input - CHAN-IN automatically sends it to both channels
- **Stereo source**: Connect both LEFT and RIGHT

**Step 3: Connect the Outputs**</br>

Patch the LEFT and RIGHT outputs to the next module in the chain (typically SHAPE, the noise gate).</br>

**Step 4: Set Your Initial Levels**</br>

Start with:</br>
- **LEVEL knob**: 0dB, unity gain
- **LOW CUT**: Fully counter-clockwise (20Hz, minimal filtering)
- **HIGH CUT**: Fully clockwise (20kHz, no filtering)
- **PHASE**: Off

**Step 5: Check Your Meters**</br>

Play audio through CHAN-IN and watch the vertical VU meters light up.</br>
You should see green LEDs for normal levels, yellow as you approach 0dB, and red only on peaks above 0dB.</br>

### What to Expect When It Works

When everything is connected properly:</br>
- Audio passes through cleanly at the set gain level
- The VU meters show your signal level with green/yellow/red LEDs
- If you're using Console 1 MK2, the hardware knobs control CHAN-IN parameters

---

## The Panel Controls

CHAN-IN has four main controls.</br>

### LEVEL Knob (below vu meter)

**What it does**: Controls input gain from -60dB to +6dB.</br>





### LOW CUT Knob (Filter, 20-500Hz)

**What it does**: </br>
Removes low frequencies below the set point.</br>

**Why it matters**: </br>
Low-end rumble, handling noise, and proximity effect can muddy your mix.</br>
This filter removes them before they affect the rest of the chain.</br>





### HIGH CUT Knob (Filter, 1kHz-20kHz)

**What it does**: </br>
removes high frequencies above the set point.</br>

**Why it matters**: </br>
Harsh, brittle, or overly bright sources can fatigue your ears and create problems in the mix.</br>
This filter reduces high frequencies.</br>





### PHASE Button (Bottom Left)

**What it does**: </br>
Flips the polarity of both left and right channels (multiplies signal by -1).</br>

**Why it matters**: </br>
When using multiple microphones on the same source, phase cancellation can occur.</br>
Flipping the phase of one source can restore lost low-end and clarity.</br>





### Display Visibility Toggle

**What it does**: </br>
Shows or hides the horizontal meter display.</br>
If you don't need the metering, you can switch off the display.</br>

Click the small switch in the upper-right corner of the display to turn it on or off:</br>
- **Amber fill**: Display is on
- **X mark**: Display is off

The switch shows at 50% opacity normally, 100% opacity when you hover over it.</br>

---

## Understanding the Meters

CHAN-IN gives you two types of metering: </br>
vertical VU meters (one for each channel) and a horizontal display with three selectable modes.</br>

### Vertical VU Meters

**What you see**: </br>
Two vertical columns of LEDs (left and right channels), 17 LEDs each.

**Color coding**:</br>
- **Green** (LEDs 0-10): Normal operating level, -60dB to -6dB
- **Yellow** (LEDs 11-13): Getting hot, -4dB to 0dB
- **Red** (LEDs 14-16): Very hot, +2dB to +6dB 

**Labeled markers**:</br>
- **-60dB**: Bottom (barely audible)
- **-24dB**: Lower third
- **-12dB**: Mid point
- **-6dB**: Last green LED (good average level)
- **0dB**: Last yellow LED (reference level)
- **+6dB**: Top red LED (maximum)

**How to read them**:</br>
Watch where the LEDs light up most of the time.</br>
If you're constantly in the red zone, you're too hot - turn down the LEVEL knob.</br>

### Horizontal Meter Display

**What you see**: </br>
A horizontal bar display showing left channel (top bar) and right channel (bottom bar).</br>

**Features**:</br>
- **Amber gradient**: Bars start dim (30% amber) on the left and get brighter (100% amber) toward the right
- **0dB reference line**: Thin grey vertical line showing where 0dB sits
- **Peak hold indicators**: White vertical lines showing the highest peak reached (in RMS mode)
- **Stereo separator**: Thin black line between top and bottom bars

**Meter mode switches**: </br>
Three small squares in the upper left corner let you select the meter mode.</br>

### Three Meter Modes

Click the small squares to switch between metering modes.</br>
You can switch modes at any given moment without affecting your audio.</br>

**1. RMS (First Square) - General Purpose**</br>

**What it measures**: </br>
Root Mean Square - the average power of your signal over time.</br>

**Why use it**:</br>
RMS gives you a sense of the perceived loudness of your audio.</br>
It's smooth, easy to read, and good for general mixing.</br>

**Ballistics**:</br>
- Fast enough to catch changes
- Smooth enough to be readable
- Peak hold indicators show the highest level reached


**2. VU (Second Square) - Traditional**</br>

**What it measures**: </br>
Volume Units with classic analog ballistics.</br>

**Why use it**:</br>
If you've worked with analog gear, VU meters feel familiar.</br>
They respond slowly (300ms decay), giving you a sense of average level like an analog console.</br>

**Ballistics**:</br>
- Fast attack (5ms) catches peaks</br>
- Slow decay (300ms) like classic VU meters</br>



**3. PPM (Third Square) - Broadcast**</br>

**What it measures**: </br>
Peak Programme Meter with true peak detection.</br>

**Why use it**:</br>
PPM meters catch true peaks and hold them longer, making it easy to see if you're hitting 0dB or clipping.</br>

**Ballistics**:</br>
- Very fast attack (<5ms)
- Slow decay (2.8 seconds, broadcast standard)




---

## Working with Filters

CHAN-IN's filters are serial (cascaded): audio goes through Low Cut first, then High Cut.</br>

### Low Cut Filter (High-Pass)

**Purpose**: </br>
Removes frequencies below the set point.</br>

**Filter type**: </br>
12dB/octave (2nd-order), gentle slope.</br>

**Q Factor**: </br>
Fixed at 0.8 (slightly resonant).</br>

### When to Use Low Cut

**Vocals**:</br>
- **Problem**: Room rumble, mic handling noise, proximity effect makes vocals muddy
- **Solution**: Set Low Cut to 80-100Hz
- **Result**: Clean, clear vocal without low-end mud


### High Cut Filter (Low-Pass)

**Purpose**: </br>
Attenuates frequencies above the set point.</br>

**Filter type**: </br>
12dB/octave (2nd-order), gentle slope.</br>

**Q Factor**: </br>
Fixed at 0.8 (slightly resonant).</br>

### When to Use High Cut

**Harsh Vocals**:</br>
- **Problem**: Sibilance, harshness, or digital edge
- **Solution**: Set High Cut to 12-15kHz
- **Result**: Smoother, less fatiguing vocal


### Filter Interaction

**Both filters active**:</br>
When you set both Low Cut and High Cut, they work together to create a bandpass effect.</br>

Example: Telephone effect</br>
- Low Cut: 300Hz
- High Cut: 3kHz
- Result: Only frequencies between 300Hz and 3kHz pass through

### Filter Bypass

**To bypass the filters**:</br>
- **Low Cut**: Turn fully counter-clockwise to 20Hz (minimal filtering)
- **High Cut**: Turn fully clockwise to 20kHz (no filtering)


---

## Phase Control

The PHASE button flips the polarity of your audio signal.</br>

### What Phase Invert Does

When you press the PHASE button, CHAN-IN multiplies your audio signal by -1.</br>
This flips the waveform upside down.</br>

```
Normal Signal:              Inverted Signal:
    /\                              /\ 
   /  \                            /  \
  /    \                          /    \
 /      \    /    becomes   \    /      \
         \  /                \  /
          \/                  \/  
```

### Why This Matters

**Phase Cancellation**:</br>
When two microphones capture the same source from different positions, their waveforms might be out of phase.</br>
When you combine them, frequencies cancel out, making the sound thin and weak (especially in the low end).</br>

**Phase Alignment**:</br>
Flipping the polarity of one microphone can restore the low-end and make the combined sound fuller and stronger.</br>


---

## Console 1 MK2 Integration

When you connect CHAN-IN to the C1 module and Console 1 MK2 hardware, you get hands-on control with bidirectional feedback.</br>

### Hardware Control Mapping

**CHAN-IN uses 3 encoders + 1 button**:</br>

**1. LEVEL (Input Gain - Encoder)**:</br>
- Turn to adjust input gain (-60dB to +6dB)
- LED ring shows current level position
- Real-time updates when you change the parameter in VCV Rack

**2. LPF - (High Cut - Encoder)**:</br>
- Turn to adjust high cut frequency (1kHz to 20kHz)
- LED ring shows frequency position
- Lower = darker sound

**3. HPF - (Low Cut - Encoder)**:</br>
- Turn to adjust low cut frequency (20Hz to 500Hz)
- LED ring shows frequency position
- Higher = thinner sound

**4. PHASE (Button)**:</br>
- Press to toggle phase invert
- Button LED lights when phase is inverted
- Same as clicking the phase button on CHAN-IN panel

### VU Meter Feedback to Hardware

**Console 1 MK2 VU Meters**:</br>
The two VU meter displays on the Console 1 MK2 hardware show CHAN-IN's left and right levels in real time.</br>

**What you see**:</br>
The hardware VU meters matches the vertical LED meters on CHAN-IN, giving you at-a-glance level monitoring.</br>

**LED correspondence**:</br>
The hardware reads the 17-segment VU meter array and displays the full active LED index (0-16).</br>

### Parameter Synchronization

**When you load a patch**:</br>
C1 sends all current parameter values to the Console 1 MK2 hardware.</br>
The LED rings immediately show the correct positions - no need to move every knob to "pick up" the parameters.</br>

**Bidirectional control**:</br>
- Turn a hardware encoder → CHAN-IN parameter updates in VCV Rack
- Move a parameter in VCV Rack → Hardware LED ring updates to match

Everything stays synchronized automatically.</br>

### CHAN-IN Position in Chain

**Position 0**: </br>
When using Standard mode, CHAN-IN must be placed at position 0 (immediately to the right of C1).</br>

**Detection**:</br>
C1's first module LED (green) indicates CHAN-IN is detected and active.</br>


---

## Troubleshooting

### Problem: No Audio Output

**Possible Causes**:</br>
- No audio connected to inputs
- LEVEL knob at -60dB (minimum)
- Outputs not patched to next module
- Module bypassed or disabled

**Solutions**:</br>
- Check that audio is connected to LEFT (and RIGHT if stereo)
- Turn LEVEL knob to 0dB
- Verify outputs are patched to the next module in the chain
- Check that you're not accidentally in VCV bypass mode (CHAN-IN doesn't have bypass, but check the right click context menu for "bypass")

### Problem: Audio Sounds Distorted or Harsh

**Possible Causes**:</br>
- Input level too high (LEVEL knob set too hot)
- Source itself is clipping before CHAN-IN
- No high cut filtering on a harsh source

**Solutions**:</br>
- Watch the VU meters - if they're constantly red, lower the LEVEL knob
- Check your source - does it sound clean before CHAN-IN?
- Add some HIGH CUT filtering (try 12-15kHz first, go lower if needed)

### Problem: Audio Sounds Thin or Weak

**Possible Causes**:</br>
- LOW CUT set too high (removing too much low end)
- LEVEL knob set too low
- Phase cancellation (if using multiple sources)

**Solutions**:</br>
- Lower the LOW CUT frequency (try 80Hz for most sources)
- Raise the LEVEL knob to boost the signal
- Try toggling the PHASE button - does it sound fuller with phase inverted?

### Problem: Filters Don't Seem to Work

**Possible Causes**:</br>
- Filter knobs at extreme settings (20Hz for LOW CUT, 20kHz for HIGH CUT)
- Subtle filter slopes (12dB/octave is gentle)
- Not listening in the right frequency range

**Solutions**:</br>
- LOW CUT: Turn clockwise from 20Hz to hear it remove lows
- HIGH CUT: Turn counter-clockwise from 20kHz to hear it remove highs
- Use headphones and listen carefully to the low or high frequencies
- Try extreme settings first to hear the effect (LOW CUT at 500Hz, HIGH CUT at 2kHz), then dial back

### Problem: VU Meters Not Responding

**Possible Causes**:</br>
- No audio signal present
- LEVEL knob at minimum (-60dB)
- Meters work but signal is too quiet to register

**Solutions**:</br>
- Verify audio is present (check your source)
- Turn up the LEVEL knob
- Play a louder audio source to test the meters

### Problem: Horizontal Meter Display Not Visible

**Possible Causes**:</br>
- Display visibility toggled off
- Wrong meter mode selected

**Solutions**:</br>
- Click display on/off toggle switch in the display right corner
- Click the meter mode switches to verify your selected mode is active


---

**Copyright © 2025 Twisted Cable. Licensed under GPL-3.0-or-later.**</br>

**Console 1 MK2** is a registered trademark of Softube AB.</br>

---

**Manual Version**: 1.0</br>
**Last Updated**: November 2025</br>
**Author**: Latif Karoumi / Twisted Cable</br>
