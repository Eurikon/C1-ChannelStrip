#include "OpticalCompressor.hpp"

OpticalCompressor::OpticalCompressor() {
    sampleRate = 44100.0f;
    thresholdDb = -18.0f;
    ratio = 3.0f;  // Optical typically has lower ratios (3:1 to 8:1)
    attackMs = 10.0f;  // Optical default: smooth attack
    releaseMs = 500.0f;  // Optical default: slow release
    makeupGain = dbToLin(0.0f);
    gainReductionDb = 0.0f;
    rmsState = 0.0f;
    optoState = 0.0f;
    autoReleaseMode = true;  // Optical is inherently program-dependent
    kneeWidth = 6.0f;  // 6dB soft knee by default

    recalculateCoefficients();
}

void OpticalCompressor::setSampleRate(float sr) {
    if (sr > 0.0f && sr != sampleRate) {
        sampleRate = sr;
        recalculateCoefficients();
    }
}

void OpticalCompressor::setThreshold(float db) {
    thresholdDb = db;
}

void OpticalCompressor::setRatio(float r) {
    // Optical compressors typically have softer ratios
    ratio = std::max(1.0f, std::min(r, 10.0f));  // Cap at 10:1 for optical character
}

void OpticalCompressor::setAttack(float ms) {
    // Optical attack is inherently slower - enforce minimum 10ms
    attackMs = std::max(10.0f, ms);
    recalculateCoefficients();
}

void OpticalCompressor::setRelease(float ms) {
    releaseMs = ms;
    recalculateCoefficients();
}

void OpticalCompressor::setMakeup(float db) {
    makeupGain = dbToLin(db);
}

void OpticalCompressor::setAutoRelease(bool enable) {
    autoReleaseMode = enable;
}

void OpticalCompressor::setKnee(float db) {
    kneeWidth = (db < 0.0f) ? 6.0f : db;  // -1 or negative = use default (6.0 soft knee)
}

void OpticalCompressor::recalculateCoefficients() {
    attackCoeff = std::exp(-1.0f / ((attackMs / 1000.0f) * sampleRate));
    releaseCoeff = std::exp(-1.0f / ((releaseMs / 1000.0f) * sampleRate));
}

float OpticalCompressor::calculateOptoRelease(float grLevel) {
    // Simulate opto-resistor behavior:
    // - Heavy compression (high GR) → slow release (opto saturated)
    // - Light compression (low GR) → fast release (opto recovering)
    // This creates the classic LA-2A "breathing" character

    // Map GR level (0-20dB) to release time multiplier (0.5x to 3x)
    float grNormalized = std::min(grLevel / 20.0f, 1.0f);
    float releaseMultiplier = 0.5f + (grNormalized * 2.5f);

    return releaseMultiplier;
}

void OpticalCompressor::processStereo(float inL, float inR, float* outL, float* outR) {
    // RMS detection (optical uses RMS)
    float inputSquared = 0.5f * (inL * inL + inR * inR);

    // RMS smoothing
    float rmsCoeff = std::exp(-1.0f / (rmsTimeConstant * sampleRate));
    rmsState = rmsCoeff * rmsState + (1.0f - rmsCoeff) * inputSquared;
    float rmsLevel = std::sqrt(rmsState);
    float inputDb = linToDb(rmsLevel);

    // Gain computer (soft knee based on kneeWidth)
    float overThreshold = inputDb - thresholdDb;
    float targetGR = 0.0f;
    if (overThreshold > 0.0f) {
        if (kneeWidth > 0.0f && overThreshold < kneeWidth) {
            // Soft knee region: quadratic curve
            targetGR = (overThreshold * overThreshold) / (2.0f * kneeWidth) * (1.0f - 1.0f / ratio);
        } else if (kneeWidth > 0.0f) {
            // Above knee: standard compression
            targetGR = (kneeWidth / 2.0f) * (1.0f - 1.0f / ratio) +
                       (overThreshold - kneeWidth) * (1.0f - 1.0f / ratio);
        } else {
            // Hard knee (kneeWidth = 0, unusual for optical)
            targetGR = overThreshold - (overThreshold / ratio);
        }
    }

    // Opto-resistor simulation: slow decay state
    optoState = optoState * optoDecay + targetGR * (1.0f - optoDecay);

    // Envelope follower with program-dependent release
    if (targetGR > gainReductionDb) {
        // Attack phase
        gainReductionDb = attackCoeff * gainReductionDb + (1.0f - attackCoeff) * targetGR;
    } else {
        // Release phase: time-varying based on compression depth
        float releaseMultiplier = calculateOptoRelease(gainReductionDb);
        float adaptiveReleaseMs = releaseMs * releaseMultiplier;
        float adaptiveCoeff = std::exp(-1.0f / ((adaptiveReleaseMs / 1000.0f) * sampleRate));

        // Blend with opto state for smooth, natural release curve
        gainReductionDb = adaptiveCoeff * gainReductionDb + (1.0f - adaptiveCoeff) * optoState;
    }

    // Apply gain reduction + makeup
    float gain = dbToLin(-gainReductionDb) * makeupGain;
    *outL = inL * gain;
    *outR = inR * gain;
}

void OpticalCompressor::processStereoWithKey(float inL, float inR, float keyLevel, float* outL, float* outR) {
    // Use external key for detection - convert to dB directly
    float inputDb = linToDb(keyLevel);

    // Gain computer (identical to processStereo)
    float overThreshold = inputDb - thresholdDb;
    float targetGR = 0.0f;
    if (overThreshold > 0.0f) {
        if (kneeWidth > 0.0f && overThreshold < kneeWidth) {
            // Soft knee region: quadratic curve
            targetGR = (overThreshold * overThreshold) / (2.0f * kneeWidth) * (1.0f - 1.0f / ratio);
        } else if (kneeWidth > 0.0f) {
            // Above knee: standard compression
            targetGR = (kneeWidth / 2.0f) * (1.0f - 1.0f / ratio) +
                       (overThreshold - kneeWidth) * (1.0f - 1.0f / ratio);
        } else {
            // Hard knee (kneeWidth = 0, unusual for optical)
            targetGR = overThreshold - (overThreshold / ratio);
        }
    }

    // Opto-resistor simulation: slow decay state
    optoState = optoState * optoDecay + targetGR * (1.0f - optoDecay);

    // Envelope follower with program-dependent release
    if (targetGR > gainReductionDb) {
        // Attack phase
        gainReductionDb = attackCoeff * gainReductionDb + (1.0f - attackCoeff) * targetGR;
    } else {
        // Release phase: time-varying based on compression depth
        float releaseMultiplier = calculateOptoRelease(gainReductionDb);
        float adaptiveReleaseMs = releaseMs * releaseMultiplier;
        float adaptiveCoeff = std::exp(-1.0f / ((adaptiveReleaseMs / 1000.0f) * sampleRate));

        // Blend with opto state for smooth, natural release curve
        gainReductionDb = adaptiveCoeff * gainReductionDb + (1.0f - adaptiveCoeff) * optoState;
    }

    // Apply gain reduction + makeup to audio (not key signal)
    float gain = dbToLin(-gainReductionDb) * makeupGain;
    *outL = inL * gain;
    *outR = inR * gain;
}
