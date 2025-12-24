#include "VariMuCompressor.hpp"

VariMuCompressor::VariMuCompressor() {
    sampleRate = 44100.0f;
    thresholdDb = -18.0f;
    ratio = 2.0f;  // Vari-Mu typically uses very gentle ratios (2:1 to 4:1)
    attackMs = 20.0f;  // Vari-Mu default: very slow attack
    releaseMs = 800.0f;  // Vari-Mu default: very slow release
    makeupGain = dbToLin(0.0f);
    gainReductionDb = 0.0f;
    rmsState = 0.0f;
    tubeStateL = 0.0f;
    tubeStateR = 0.0f;
    autoReleaseMode = false;
    kneeWidth = 12.0f;  // Extra-soft 12dB knee by default

    recalculateCoefficients();
}

void VariMuCompressor::setSampleRate(float sr) {
    if (sr > 0.0f && sr != sampleRate) {
        sampleRate = sr;
        recalculateCoefficients();
    }
}

void VariMuCompressor::setThreshold(float db) {
    thresholdDb = db;
}

void VariMuCompressor::setRatio(float r) {
    // Vari-Mu typically has gentle ratios - cap at 6:1 for authentic character
    ratio = std::max(1.0f, std::min(r, 6.0f));
}

void VariMuCompressor::setAttack(float ms) {
    // Vari-Mu attack is inherently slow - enforce minimum 20ms
    attackMs = std::max(20.0f, ms);
    recalculateCoefficients();
}

void VariMuCompressor::setRelease(float ms) {
    // Scale release times longer for Vari-Mu character
    releaseMs = ms * 2.0f;  // Double the release time
    recalculateCoefficients();
}

void VariMuCompressor::setMakeup(float db) {
    makeupGain = dbToLin(db);
}

void VariMuCompressor::setAutoRelease(bool enable) {
    autoReleaseMode = enable;
}

void VariMuCompressor::setKnee(float db) {
    kneeWidth = (db < 0.0f) ? 12.0f : db;  // -1 or negative = use default (12.0 extra-soft knee)
}

void VariMuCompressor::recalculateCoefficients() {
    attackCoeff = std::exp(-1.0f / ((attackMs / 1000.0f) * sampleRate));
    releaseCoeff = std::exp(-1.0f / ((releaseMs / 1000.0f) * sampleRate));
}

float VariMuCompressor::tubeSaturate(float x, float& tubeState) {
    // Tube saturation with grid bias simulation
    // Asymmetric soft clipping (more even harmonics)

    // Update tube grid state (creates memory/hysteresis effect)
    tubeState = tubeState * 0.999f + x * 0.001f;

    // Apply asymmetric saturation curve
    float biased = x + tubeAsymmetry * tubeState;

    // Soft saturation using tanh-like curve
    float saturated;
    if (biased > 1.5f) {
        saturated = 1.0f - std::exp(-(biased - 1.5f) * 0.5f);
    } else if (biased < -1.5f) {
        saturated = -1.0f + std::exp((biased + 1.5f) * 0.5f);
    } else {
        // Soft knee region - cubic curve for smooth transition
        saturated = biased - (biased * biased * biased) / 9.0f;
    }

    return saturated;
}

void VariMuCompressor::processStereo(float inL, float inR, float* outL, float* outR) {
    // RMS detection (Vari-Mu uses RMS with longer averaging)
    float inputSquared = 0.5f * (inL * inL + inR * inR);

    // RMS smoothing (slowest of all types)
    float rmsCoeff = std::exp(-1.0f / (rmsTimeConstant * sampleRate));
    rmsState = rmsCoeff * rmsState + (1.0f - rmsCoeff) * inputSquared;
    float rmsLevel = std::sqrt(rmsState);
    float inputDb = linToDb(rmsLevel);

    // Gain computer (soft knee based on kneeWidth)
    float overThreshold = inputDb - thresholdDb;
    float targetGR = 0.0f;
    if (overThreshold > 0.0f) {
        if (kneeWidth > 0.0f && overThreshold < kneeWidth) {
            // Smooth quadratic curve in knee region
            targetGR = (overThreshold * overThreshold) / (2.0f * kneeWidth) * (1.0f - 1.0f / ratio);
        } else if (kneeWidth > 0.0f) {
            // Above knee
            targetGR = (kneeWidth / 2.0f) * (1.0f - 1.0f / ratio) +
                       (overThreshold - kneeWidth) * (1.0f - 1.0f / ratio);
        } else {
            // Hard knee (kneeWidth = 0, unusual for Vari-Mu)
            targetGR = overThreshold - (overThreshold / ratio);
        }
    }

    // Envelope follower (very slow attack and release)
    if (targetGR > gainReductionDb) {
        gainReductionDb = attackCoeff * gainReductionDb + (1.0f - attackCoeff) * targetGR;
    } else {
        if (autoReleaseMode) {
            // Vari-Mu AUTO: even slower release for sustained material
            float grNormalized = std::min(gainReductionDb / 20.0f, 1.0f);
            float autoMultiplier = 1.0f + grNormalized * 2.0f;  // 1x to 3x slower
            float autoCoeff = std::exp(-1.0f / ((releaseMs * autoMultiplier / 1000.0f) * sampleRate));
            gainReductionDb = autoCoeff * gainReductionDb + (1.0f - autoCoeff) * targetGR;
        } else {
            gainReductionDb = releaseCoeff * gainReductionDb + (1.0f - releaseCoeff) * targetGR;
        }
    }

    // Apply gain reduction
    float gain = dbToLin(-gainReductionDb);
    float compressedL = inL * gain;
    float compressedR = inR * gain;

    // Apply tube saturation (adds warmth and harmonics)
    // More saturation when compressing heavily
    float saturationMix = std::min(gainReductionDb / 12.0f, 1.0f) * tubeSaturation;

    float cleanL = compressedL * makeupGain;
    float cleanR = compressedR * makeupGain;

    float saturatedL = tubeSaturate(compressedL * makeupGain * 1.3f, tubeStateL);
    float saturatedR = tubeSaturate(compressedR * makeupGain * 1.3f, tubeStateR);

    *outL = (1.0f - saturationMix) * cleanL + saturationMix * saturatedL;
    *outR = (1.0f - saturationMix) * cleanR + saturationMix * saturatedR;
}

void VariMuCompressor::processStereoWithKey(float inL, float inR, float keyLevel, float* outL, float* outR) {
    // Use external key for detection - convert to dB directly
    float inputDb = linToDb(keyLevel);

    // Gain computer (identical to processStereo)
    float overThreshold = inputDb - thresholdDb;
    float targetGR = 0.0f;
    if (overThreshold > 0.0f) {
        if (kneeWidth > 0.0f && overThreshold < kneeWidth) {
            targetGR = (overThreshold * overThreshold) / (2.0f * kneeWidth) * (1.0f - 1.0f / ratio);
        } else if (kneeWidth > 0.0f) {
            targetGR = (kneeWidth / 2.0f) * (1.0f - 1.0f / ratio) +
                       (overThreshold - kneeWidth) * (1.0f - 1.0f / ratio);
        } else {
            targetGR = overThreshold - (overThreshold / ratio);
        }
    }

    // Slow, smooth envelope follower (identical to processStereo)
    if (targetGR > gainReductionDb) {
        gainReductionDb = attackCoeff * gainReductionDb + (1.0f - attackCoeff) * targetGR;
    } else {
        gainReductionDb = releaseCoeff * gainReductionDb + (1.0f - releaseCoeff) * targetGR;
    }

    // Apply gain reduction to audio (not key signal)
    float gain = dbToLin(-gainReductionDb);
    float compressedL = inL * gain;
    float compressedR = inR * gain;

    // Tube saturation (identical to processStereo)
    float saturationMix = std::min(gainReductionDb / 12.0f, 1.0f) * tubeSaturation;
    float cleanL = compressedL * makeupGain;
    float cleanR = compressedR * makeupGain;
    float saturatedL = tubeSaturate(compressedL * makeupGain * 1.3f, tubeStateL);
    float saturatedR = tubeSaturate(compressedR * makeupGain * 1.3f, tubeStateR);

    *outL = (1.0f - saturationMix) * cleanL + saturationMix * saturatedL;
    *outR = (1.0f - saturationMix) * cleanR + saturationMix * saturatedR;
}
