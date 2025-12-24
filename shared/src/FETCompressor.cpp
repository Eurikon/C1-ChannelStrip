#include "FETCompressor.hpp"

FETCompressor::FETCompressor() {
    sampleRate = 44100.0f;
    thresholdDb = -18.0f;
    ratio = 4.0f;
    attackMs = 0.1f;  // FET default: ultra-fast attack
    releaseMs = 50.0f;  // FET default: fast release
    makeupGain = dbToLin(0.0f);
    gainReductionDb = 0.0f;
    rmsState = 0.0f;
    autoReleaseMode = false;
    kneeWidth = 0.0f;  // Hard knee by default

    recalculateCoefficients();
}

void FETCompressor::setSampleRate(float sr) {
    if (sr > 0.0f && sr != sampleRate) {
        sampleRate = sr;
        recalculateCoefficients();
    }
}

void FETCompressor::setThreshold(float db) {
    thresholdDb = db;
}

void FETCompressor::setRatio(float r) {
    ratio = std::max(1.0f, r);
}

void FETCompressor::setAttack(float ms) {
    // FET compressors have ultra-fast attack - convert normal range to FET range
    // Map 0.1-30ms to 0.02-0.8ms (20µs to 800µs)
    attackMs = 0.02f + (ms / 30.0f) * 0.78f;
    recalculateCoefficients();
}

void FETCompressor::setRelease(float ms) {
    // FET release is typically faster - scale down by factor of 3
    releaseMs = ms / 3.0f;
    recalculateCoefficients();
}

void FETCompressor::setMakeup(float db) {
    makeupGain = dbToLin(db);
}

void FETCompressor::setAutoRelease(bool enable) {
    autoReleaseMode = enable;  // FET doesn't really use AUTO, but accept for interface compatibility
}

void FETCompressor::setKnee(float db) {
    kneeWidth = (db < 0.0f) ? 0.0f : db;  // -1 or negative = use default (0.0 hard knee)
}

void FETCompressor::recalculateCoefficients() {
    attackCoeff = std::exp(-1.0f / ((attackMs / 1000.0f) * sampleRate));
    releaseCoeff = std::exp(-1.0f / ((releaseMs / 1000.0f) * sampleRate));
}

float FETCompressor::softClip(float x) {
    // Soft saturation curve (simulates FET non-linearity)
    // tanh approximation for efficiency
    if (x > 1.0f) {
        return 1.0f - std::exp(-(x - 1.0f));
    } else if (x < -1.0f) {
        return -1.0f + std::exp((x + 1.0f));
    }
    return x;
}

void FETCompressor::processStereo(float inL, float inR, float* outL, float* outR) {
    // RMS detection (FET uses RMS, not peak)
    float inputSquared = 0.5f * (inL * inL + inR * inR);

    // RMS smoothing with time constant
    float rmsCoeff = std::exp(-1.0f / (rmsTimeConstant * sampleRate));
    rmsState = rmsCoeff * rmsState + (1.0f - rmsCoeff) * inputSquared;
    float rmsLevel = std::sqrt(rmsState);
    float inputDb = linToDb(rmsLevel);

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

    // Envelope follower (ultra-fast attack)
    if (targetGR > gainReductionDb) {
        gainReductionDb = attackCoeff * gainReductionDb + (1.0f - attackCoeff) * targetGR;
    } else {
        gainReductionDb = releaseCoeff * gainReductionDb + (1.0f - releaseCoeff) * targetGR;
    }

    // Apply gain reduction + makeup
    float gain = dbToLin(-gainReductionDb) * makeupGain;
    float compressedL = inL * gain;
    float compressedR = inR * gain;

    // FET-style non-linear distortion/saturation
    // Add harmonics based on compression amount (more compression = more distortion)
    float distortionMix = std::min(gainReductionDb / 20.0f, 1.0f) * distortionAmount;

    *outL = (1.0f - distortionMix) * compressedL + distortionMix * softClip(compressedL * 1.5f);
    *outR = (1.0f - distortionMix) * compressedR + distortionMix * softClip(compressedR * 1.5f);
}

void FETCompressor::processStereoWithKey(float inL, float inR, float keyLevel, float* outL, float* outR) {
    // Use external key for detection - convert to dB directly (bypass RMS for key signal)
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
        gainReductionDb = releaseCoeff * gainReductionDb + (1.0f - releaseCoeff) * targetGR;
    }

    // Apply gain reduction + makeup to audio (not key signal)
    float gain = dbToLin(-gainReductionDb) * makeupGain;
    float compressedL = inL * gain;
    float compressedR = inR * gain;

    // FET-style distortion (identical to processStereo)
    float distortionMix = std::min(gainReductionDb / 20.0f, 1.0f) * distortionAmount;
    *outL = (1.0f - distortionMix) * compressedL + distortionMix * softClip(compressedL * 1.5f);
    *outR = (1.0f - distortionMix) * compressedR + distortionMix * softClip(compressedR * 1.5f);
}
