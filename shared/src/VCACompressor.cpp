#include "VCACompressor.hpp"

VCACompressor::VCACompressor() {
    sampleRate = 44100.0f;  // Default, will be overridden by setSampleRate()
    thresholdDb = -18.0f;
    ratio = 4.0f;
    attackMs = 10.0f;
    releaseMs = 200.0f;
    makeupGain = dbToLin(0.0f);
    gainReductionDb = 0.0f;
    autoReleaseMode = false;
    kneeWidth = 0.0f;  // Hard knee by default

    // Calculate initial coefficients
    recalculateCoefficients();
}

void VCACompressor::setSampleRate(float sr) {
    if (sr > 0.0f && sr != sampleRate) {
        sampleRate = sr;
        recalculateCoefficients();
    }
}

void VCACompressor::setThreshold(float db) {
    thresholdDb = db;
}

void VCACompressor::setRatio(float r) {
    ratio = std::max(1.0f, r);
}

void VCACompressor::setAttack(float ms) {
    attackMs = ms;
    recalculateCoefficients();
}

void VCACompressor::setRelease(float ms) {
    releaseMs = ms;
    recalculateCoefficients();
}

void VCACompressor::setMakeup(float db) {
    makeupGain = dbToLin(db);
}

void VCACompressor::setAutoRelease(bool enable) {
    autoReleaseMode = enable;
}

void VCACompressor::setKnee(float db) {
    kneeWidth = (db < 0.0f) ? 0.0f : db;  // -1 or negative = use default (0.0 hard knee)
}

void VCACompressor::recalculateCoefficients() {
    // Calculate attack coefficient from ms and current sample rate
    // Formula: coeff = exp(-1 / (time_in_seconds * sample_rate))
    attackCoeff = std::exp(-1.0f / ((attackMs / 1000.0f) * sampleRate));

    // Calculate release coefficient
    releaseCoeff = std::exp(-1.0f / ((releaseMs / 1000.0f) * sampleRate));
}

void VCACompressor::processStereo(float inL, float inR, float* outL, float* outR) {
    // PEAK detection (SSL G-style, not RMS)
    // Use maximum absolute value of stereo pair
    float inputLevel = std::max(std::abs(inL), std::abs(inR));
    float inputDb = linToDb(inputLevel);

    // Gain computer (hard/soft knee based on kneeWidth)
    float overThreshold = inputDb - thresholdDb;
    float targetGR = 0.0f;
    if (overThreshold > 0.0f) {
        if (kneeWidth > 0.0f && overThreshold < kneeWidth) {
            // Soft knee region: quadratic curve
            targetGR = (overThreshold * overThreshold) / (2.0f * kneeWidth) * (1.0f - 1.0f / ratio);
        } else if (kneeWidth > 0.0f) {
            // Above knee: standard compression with knee offset
            targetGR = (kneeWidth / 2.0f) * (1.0f - 1.0f / ratio) +
                       (overThreshold - kneeWidth) * (1.0f - 1.0f / ratio);
        } else {
            // Hard knee (kneeWidth = 0)
            targetGR = overThreshold - (overThreshold / ratio);
        }
    }

    // Envelope follower (GR smoothing)
    if (targetGR > gainReductionDb) {
        // Attack phase - signal getting louder
        gainReductionDb = attackCoeff * gainReductionDb + (1.0f - attackCoeff) * targetGR;
    } else {
        // Release phase - signal getting quieter
        if (autoReleaseMode) {
            // AUTO release: program-dependent release time
            // Adapt release speed based on how fast GR is changing
            float grDelta = std::abs(targetGR - gainReductionDb);

            // Fast release for transient material (large delta)
            // Slow release for sustained material (small delta)
            // Range: 100ms (fast) to 1200ms (slow)
            float adaptiveReleaseMs = 100.0f + (1100.0f * (1.0f - std::min(grDelta / 20.0f, 1.0f)));
            float adaptiveCoeff = std::exp(-1.0f / ((adaptiveReleaseMs / 1000.0f) * sampleRate));

            gainReductionDb = adaptiveCoeff * gainReductionDb + (1.0f - adaptiveCoeff) * targetGR;
        } else {
            // Fixed release time
            gainReductionDb = releaseCoeff * gainReductionDb + (1.0f - releaseCoeff) * targetGR;
        }
    }

    // Apply gain reduction + makeup
    float gain = dbToLin(-gainReductionDb) * makeupGain;
    *outL = inL * gain;
    *outR = inR * gain;
}

void VCACompressor::processStereoWithKey(float inL, float inR, float keyLevel, float* outL, float* outR) {
    // Use external key signal for detection instead of audio input
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

    // Envelope follower (identical to processStereo)
    if (targetGR > gainReductionDb) {
        gainReductionDb = attackCoeff * gainReductionDb + (1.0f - attackCoeff) * targetGR;
    } else {
        if (autoReleaseMode) {
            float grDelta = std::abs(targetGR - gainReductionDb);
            float adaptiveReleaseMs = 100.0f + (1100.0f * (1.0f - std::min(grDelta / 20.0f, 1.0f)));
            float adaptiveCoeff = std::exp(-1.0f / ((adaptiveReleaseMs / 1000.0f) * sampleRate));
            gainReductionDb = adaptiveCoeff * gainReductionDb + (1.0f - adaptiveCoeff) * targetGR;
        } else {
            gainReductionDb = releaseCoeff * gainReductionDb + (1.0f - releaseCoeff) * targetGR;
        }
    }

    // Apply gain reduction + makeup to audio (not key signal)
    float gain = dbToLin(-gainReductionDb) * makeupGain;
    *outL = inL * gain;
    *outR = inR * gain;
}
