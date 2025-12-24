//***********************************************************************************************
// ChanOut Clean Engine - MindMeld MasterChannel-based transparent processing
// For C1-ChannelStrip VCV Rack Plugin
//
// Based on MindMeld MasterChannel.cpp by Steve Baker and Marc Boulé
// Source: https://github.com/MindMeldModular/PatchMaster
// License: GPL-3.0-or-later
//
// This engine provides transparent processing with soft-clipping protection,
// derived from MindMeld's professional master channel architecture.
//***********************************************************************************************

#pragma once

#include "rack.hpp"

using namespace rack;

namespace ChanOutClean {

// Constants from MindMeld GlobalConst
static constexpr float ANTIPOP_SLEW_FAST = 125.0f;  // V/s for mute
static constexpr float ANTIPOP_SLEW_SLOW = 25.0f;   // V/s for fader
static constexpr int MASTER_FADER_SCALING_EXPONENT = 3;
static constexpr float MASTER_FADER_MAX_LINEAR_GAIN = 2.0f;

// Soft-clip polynomial function (from MindMeld MasterChannel.cpp lines 326-364)
// Piecewise portion that handles inputs between 6 and 12 V
// Unipolar only, caller must take care of signs
// Assumes that 6 <= x <= 12
// Assumes f(x) := x  when x < 6
// Assumes f(x) := 10  when x > 12
//
// Chosen polynomial:
// f(x) := a + b*x + c*x^2 + d*x^3
//
// Coefficient solving constraints:
// f(6) = 6
// f(12) = 10
// f'(6) = 1 (unity slope at 6V)
// f'(12) = 0 (horizontal at 12V)
// where:
// f'(x) := b + 2*c*x + 3*d*x^2
//
// solve(system(f(6)=6,f(12)=10,f'(6)=1,f'(12)=0),{a,b,c,d})
//
// solution:
// a=2 and b=0 and c=(1/6) and d=(-1/108)
inline float clipPoly(float inX) {
    return 2.0f + inX * inX * (1.0f/6.0f - inX * (1.0f/108.0f));
}

struct CleanEngine {
    // Operating mode (0 = Master Output, 1 = Channel Output)
    int outputMode = 0;

    // Settings
    int clipping = 0;  // 0 = soft, 1 = hard

    // Soft-clip thresholds (mode-dependent)
    float clipThresholdLinear = 6.0f;       // Master: 6V, Channel: 4V
    float clipThresholdTransition = 12.0f;  // Master: 12V, Channel: 10V

    CleanEngine() {
        reset();
    }

    void reset() {
        // Reset state variables if needed in future
    }

    void setOutputMode(int mode) {
        outputMode = mode;

        if (mode == 0) {
            // Master Output Mode: ±6V linear, ±6V to ±12V soft-clip
            clipThresholdLinear = 6.0f;
            clipThresholdTransition = 12.0f;
        }
        else {
            // Channel Output Mode: ±4V linear, ±4V to ±10V soft-clip
            clipThresholdLinear = 4.0f;
            clipThresholdTransition = 10.0f;
        }
    }

    void setSampleRate(float sr) {
        // Sample rate handling if needed in future
        (void)sr;  // Unused for now
    }

    // Soft/hard clipping function (from MindMeld MasterChannel.cpp lines 352-364)
    // Ensures output never exceeds ±10V (VCV Rack voltage standard compliance)
    float clip(float inX) {
        if (inX <= clipThresholdLinear && inX >= -clipThresholdLinear) {
            return inX;  // Linear passthrough region
        }

        if (clipping == 1) {  // Hard clip
            return clamp(inX, -10.0f, 10.0f);
        }

        // Soft clip (clipping == 0)
        inX = clamp(inX, -clipThresholdTransition, clipThresholdTransition);
        float output;
        if (inX >= 0.0f)
            output = clipPoly(inX);
        else
            output = -clipPoly(-inX);

        // Final safety hard limit to ±10V (VCV Rack voltage standards)
        // Ensures compliance even if polynomial or mode thresholds allow higher
        return clamp(output, -10.0f, 10.0f);
    }

    // Process stereo audio
    void process(float& left, float& right, float drive) {
        // Drive parameter has minimal effect in Clean engine
        // (just slight warmth at higher values in Channel mode)
        float driveAmount = (outputMode == 1) ? drive * 0.1f : 0.0f;

        if (driveAmount > 0.0f) {
            // Subtle warmth in Channel mode only
            left *= (1.0f + driveAmount);
            right *= (1.0f + driveAmount);
        }

        // Soft/hard clipping (final protection)
        left = clip(left);
        right = clip(right);
    }
};

} // namespace ChanOutClean
