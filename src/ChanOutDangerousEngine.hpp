//***********************************************************************************************
// ChanOut Dangerous Engine - Dangerous 2-BUS+ inspired saturation
// For C1-ChannelStrip VCV Rack Plugin
//
// Based on Dangerous Music 2-BUS+ circuit descriptions with:
// - 8× polyphase oversampling (SIMD-optimized)
// - Three parallel color circuits: Harmonics, Paralimit, X-Former
// - CHARACTER parameter: -1 (Blue/Euphoric) to +1 (Red/Aggressive)
// - Ultra-transparent base with optional parallel color blending
//
// Hardware Reference (informational only, not circuit emulation):
// - Dangerous Music 2-BUS+ specifications
// - THD: <0.0018% base, ~1% with Harmonics engaged
// - Three optional circuits with parallel blend architecture
//
// License: GPL-3.0-or-later
//
// IMPORTANT: This is an approximation inspired by published specifications,
// not a circuit-accurate emulation.
//***********************************************************************************************

#pragma once

#include "rack.hpp"
#include <cmath>
#include <vector>
#include <algorithm>

#if defined(__SSE2__)
#include <emmintrin.h>
#define HAS_SSE2 1
#else
#define HAS_SSE2 0
#endif

using namespace rack;

namespace ChanOutDangerous {

static inline double clampd(double x, double a, double b) {
    return (x < a) ? a : (x > b) ? b : x;
}

static inline double lerp(double a, double b, double t) {
    return a + (b - a) * t;
}

// Buffered Polyphase Oversampler with SIMD-optimized inner loop
class BufferedPolyphaseSIMD {
public:
    BufferedPolyphaseSIMD(int factor = 8, int tapsPerPhase = 64)
    : factor_(std::max(1, factor)), tapsPerPhase_(std::max(8, tapsPerPhase)) {
        buildKernel();
        setFactor(factor_);
        ring_.assign(tapsPerPhase_ + 8, 0.0);
        writeIdx_ = 0;
    }

    void setFactor(int f) {
        factor_ = std::max(1, f);
        buildPolyphase();
    }

    int factor() const { return factor_; }
    void reset() { std::fill(ring_.begin(), ring_.end(), 0.0); writeIdx_ = 0; }

    // ProcessUp: SIMD-accelerated inner convolution
    void processUp(const double* in, size_t n, double* out) {
        if (factor_ == 1) {
            for (size_t i = 0; i < n; ++i) out[i] = in[i];
            return;
        }
        const int P = tapsPerPhase_;
        const int F = factor_;
        const int ringSize = (int)ring_.size();

        for (size_t i = 0; i < n; ++i) {
            ring_[writeIdx_] = in[i];
            int base = writeIdx_ - (P - 1);
            if (base < 0) base += ringSize;

            for (int ph = 0; ph < F; ++ph) {
                const double* taps = &polyTaps_[ph * P];
                double s = 0.0;
#if HAS_SSE2
                // SIMD path: process two doubles per iteration using __m128d
                __m128d acc = _mm_setzero_pd();
                int idx = base;
                int k = 0;
                for (; k + 1 < P; k += 2) {
                    // load ring[idx] and ring[idx+1] (handle wrap)
                    double a0 = ring_[idx];
                    int idx1 = idx + 1; if (idx1 >= ringSize) idx1 -= ringSize;
                    double a1 = ring_[idx1];
                    __m128d va = _mm_set_pd(a1, a0);

                    // load taps[k], taps[k+1]
                    __m128d vt = _mm_loadu_pd(&taps[k]);
                    acc = _mm_add_pd(acc, _mm_mul_pd(va, vt));

                    idx += 2; if (idx >= ringSize) idx -= ringSize;
                }
                // horizontal add acc
                double tmp[2]; _mm_storeu_pd(tmp, acc);
                s = tmp[0] + tmp[1];
                // tail
                for (; k < P; ++k) { s += ring_[idx] * taps[k]; idx++; if (idx >= ringSize) idx -= ringSize; }
#else
                // scalar fallback
                int idx = base;
                for (int k = 0; k < P; ++k) { s += ring_[idx] * taps[k]; idx++; if (idx >= ringSize) idx -= ringSize; }
#endif
                out[i*F + ph] = s;
            }
            writeIdx_++; if (writeIdx_ >= ringSize) writeIdx_ = 0;
        }
    }

    void processDown(const double* in, size_t inLen, double* out) {
        if (factor_ == 1) { for (size_t i = 0; i < inLen; ++i) out[i] = in[i]; return; }
        // Proper decimation: keep every Nth sample (phase 0 only)
        // The anti-aliasing was already applied during processUp
        size_t outN = inLen / factor_;
        for (size_t i = 0; i < outN; ++i) {
            out[i] = in[i * factor_];  // Take phase 0 sample only
        }
    }

private:
    int factor_ = 8;
    int tapsPerPhase_ = 64;
    std::vector<double> kernel_;
    std::vector<double> polyTaps_;
    std::vector<double> ring_;
    int writeIdx_ = 0;

    void buildKernel() {
        int N = tapsPerPhase_ * factor_;
        kernel_.assign(N, 0.0);
        double fc = 0.45 / double(factor_);
        double M = double(N) - 1.0;
        for (int n = 0; n < N; ++n) {
            double x = double(n) - M/2.0;
            double sinc = (x == 0.0) ? 2.0*fc : std::sin(2.0*M_PI*fc*x)/(M_PI*x);
            double w = 0.42 - 0.5*std::cos(2.0*M_PI*n/M) + 0.08*std::cos(4.0*M_PI*n/M);
            kernel_[n] = sinc * w;
        }
        // Normalize: for polyphase upsampling, sum of taps must equal factor_
        double sum = 0.0;
        for (int n = 0; n < N; ++n) sum += kernel_[n];
        if (sum > 1e-12) {
            double scale = double(factor_) / sum;
            for (int n = 0; n < N; ++n) kernel_[n] *= scale;
        }
    }

    void buildPolyphase() {
        int N = tapsPerPhase_ * factor_;
        if ((int)kernel_.size() != N) buildKernel();
        polyTaps_.assign(factor_ * tapsPerPhase_, 0.0);
        for (int ph = 0; ph < factor_; ++ph) {
            for (int k = 0; k < tapsPerPhase_; ++k) {
                int idx = ph + k * factor_;
                if (idx < (int)kernel_.size()) polyTaps_[ph * tapsPerPhase_ + k] = kernel_[idx];
                else polyTaps_[ph * tapsPerPhase_ + k] = 0.0;
            }
        }
    }
};

// Harmonics Circuit - Parallel waveshaper with even/odd harmonic blend
class DangerousHarmonicsShaper {
public:
    void setEvenOdd(double evenW, double oddW) {
        evenW_ = clampd(evenW, 0.0, 1.0);
        oddW_ = clampd(oddW, 0.0, 1.0);
    }

    void setCurvature(double k) {
        k_ = clampd(k, 0.5, 3.0);
    }

    inline double process(double x) const {
        // even term ~ x + a2*x*|x|, odd term ~ x + a3*x^3
        const double a2 = 0.12; // gentle even emphasis
        const double a3 = 0.05; // small odd sprinkle
        double even = x + a2 * x * std::abs(x);
        double odd  = x + a3 * x * x * x;
        double mix = evenW_ * even + oddW_ * odd;
        // soft saturation to keep peaks musical
        double y = std::tanh(k_ * mix) / std::tanh(k_);
        return y;
    }

private:
    double evenW_ = 0.7;
    double oddW_  = 0.3;
    double k_ = 1.4;
};

// Paralimit Circuit - Parallel FET-style limiting with pre-emphasis
class DangerousParalimitSIMD {
public:
    void setSampleRate(double fs) {
        fs_ = (fs > 1.0 ? fs : 44100.0);
        updateCoeffs();
    }

    void setThreshold(double thr) {
        thr_ = clampd(thr, 0.6, 0.99);
    }

    void setTimesMs(double attMs, double relMs) {
        attMs_ = clampd(attMs, 0.1, 100.0);
        relMs_ = clampd(relMs, 1.0, 1000.0);
        updateCoeffs();
    }

    void setPreEmphasis(double freqHz, double gain) {
        preHz_ = clampd(freqHz, 100.0, 12000.0);
        preGain_ = clampd(gain, 0.0, 1.5);
        updateCoeffs();
    }

    void reset() {
        env_ = 0.0;
        lpPre_ = 0.0;
    }

    inline double process(double x) {
        // pre-emphasis (HP via 1-pole LP-derived HP)
        lpPre_ += preAlpha_ * (x - lpPre_);
        double hp = x - lpPre_;
        double pre = x + preGain_ * hp;

        // envelope follower (peak, attack/release)
        double rect = std::abs(pre);
        double coeff = (rect > env_) ? envAtt_ : envRel_;
        env_ = coeff * env_ + (1.0 - coeff) * rect;

        // gain computer: infinite ratio above threshold
        double g = (env_ > thr_) ? (thr_ / std::max(env_, 1e-12)) : 1.0;
        double limited = pre * g;

        // de-emphasis
        double y = limited - preGain_ * hp;
        return y;
    }

private:
    double fs_ = 44100.0;
    double thr_ = 0.90;
    double attMs_ = 1.0, relMs_ = 80.0;
    double env_ = 0.0;
    double envAtt_ = 0.01, envRel_ = 0.999;
    double preHz_ = 2000.0; // mid/high boost region
    double preAlpha_ = 0.0;
    double preGain_ = 0.4;  // amount of pre-emphasis (0..1.5)
    double lpPre_ = 0.0;

    void updateCoeffs() {
        envAtt_ = std::exp(-1.0 / (std::max(1e-6, attMs_ * 1e-3) * fs_));
        envRel_ = std::exp(-1.0 / (std::max(1e-6, relMs_ * 1e-3) * fs_));
        preAlpha_ = 1.0 - std::exp(-2.0 * M_PI * preHz_ / fs_);
        preAlpha_ = clampd(preAlpha_, 1e-6, 1.0);
    }
};

// X-Former Circuit - Transformer core saturation with flux memory and asymmetry
class DangerousTransformerCoreSIMD {
public:
    void setSampleRate(double fs) {
        fs_ = (fs > 1.0 ? fs : 44100.0);
        setFluxTimeConstantMs(8.0);
    }

    void setFluxTimeConstantMs(double ms) {
        double seconds = std::max(0.1, ms) * 1e-3;
        double a = std::exp(-1.0 / (seconds * fs_));
        alpha_ = clampd(a, 0.0, 0.999999);
        beta_  = 1.0 - alpha_;
    }

    void setBiasGain(double g) {
        biasGain_ = clampd(g, 0.0, 0.2);
    }

    void setSymmetryK(double k) {
        satK_ = clampd(k, 0.8, 3.0);
    }

    void setThreshold(double thr) {
        thr_ = clampd(thr, 0.75, 0.995);
    }

    void setZenerDrive(double z) {
        zener_ = clampd(z, 0.0, 1.0);
    }

    void reset() {
        flux_ = 0.0;
    }

    inline double process(double x) {
        flux_ = alpha_ * flux_ + beta_ * x;
        double bias = biasGain_ * std::tanh(flux_ * 2.0);
        double u = x + bias;
        double sym = std::tanh(satK_ * u) / std::tanh(satK_);
        // controlled asymmetry
        double posK = 1.0 + 3.0 * zener_;
        double negK = 1.0 + 1.5 * zener_;
        double y = sym;
        if (sym >  thr_) { double over = sym -  thr_; y =  thr_ + std::atan(over * posK); }
        if (sym < -thr_) { double over = sym +  thr_; y = -thr_ + std::atan(over * negK); }
        return clampd(y, -1.0, 1.0);
    }

private:
    double fs_ = 44100.0;
    double flux_ = 0.0;
    double alpha_ = 0.995, beta_ = 1.0 - 0.995;
    double biasGain_ = 0.02;
    double satK_ = 1.4;
    double thr_ = 0.97;
    double zener_ = 0.0;
};

// Dangerous Engine Core - Three parallel color circuits
class DangerousEngineCore {
public:
    DangerousEngineCore(double sampleRate = 44100.0, int oversampleFactor = 8)
    : oversampler_(oversampleFactor, 64), oversampleFactor_(oversampleFactor) {
        fs_ = sampleRate;
        init();
    }

    void init() {
        oversampler_.reset();
        dcState_ = 0.0;
        drive_ = 1.0;
        character_ = 0.0;
        charGain_ = 0.9;

        // Pre-allocate upsample buffer (max: 32 samples × 8× oversampling = 256)
        upsampleBuffer_.resize(256);

        harmonics_.setCurvature(1.4);
        paralimit_.setSampleRate(fs_);
        paralimit_.setTimesMs(1.0, 80.0);
        paralimit_.setPreEmphasis(2000.0, 0.4);
        paralimit_.setThreshold(0.90);
        xformer_.setSampleRate(fs_);
        xformer_.setFluxTimeConstantMs(8.0);
        xformer_.setBiasGain(0.02);
        xformer_.setSymmetryK(1.4);
        xformer_.setThreshold(0.97);
        xformer_.setZenerDrive(0.0);

        updateFromCharacter();
    }

    void setSampleRate(double fs) {
        fs_ = (fs > 1.0 ? fs : fs_);
        paralimit_.setSampleRate(fs_);
        xformer_.setSampleRate(fs_);
    }

    void setOversampleFactor(int f) {
        oversampleFactor_ = std::max(1, f);
        oversampler_.setFactor(oversampleFactor_);
    }

    void setDriveDb(double db) {
        drive_ = std::pow(10.0, db / 20.0);
    }

    void setCharacter(double c) {
        character_ = clampd(c, -1.0, 1.0);
        updateFromCharacter();
    }

    void reset() {
        oversampler_.reset();
        paralimit_.reset();
        xformer_.reset();
        dcState_ = 0.0;
    }

    void processBlock(const double* in, double* out, size_t N) {
        if (oversampleFactor_ == 1) {
            for (size_t i = 0; i < N; ++i)
                out[i] = processSampleInternal(in[i]);
            return;
        }

        size_t M = N * oversampleFactor_;
        // Use pre-allocated buffer (no per-sample allocation)
        oversampler_.processUp(in, N, upsampleBuffer_.data());

        // Precompute weights based on Character
        double t = character_;
        double colorAmt = clampd(std::abs(t) * charGain_, 0.0, 1.0);
        double wH = 0.0, wP = 0.0, wX = 0.0;
        if (t >= 0.0) {
            wH = lerp(0.40, 0.20, t);
            wP = lerp(0.20, 0.60, t);
            wX = lerp(0.40, 0.60, t);
        } else {
            double u = -t;
            wH = lerp(0.40, 0.70, u);
            wP = lerp(0.20, 0.10, u);
            wX = lerp(0.40, 0.20, u);
        }
        double wSum = wH + wP + wX;
        if (wSum < 1e-12) { wH = 1.0; wP = wX = 0.0; wSum = 1.0; }
        double invWSum = 1.0 / wSum;

        for (size_t i = 0; i < M; ++i) {
            double s = upsampleBuffer_[i] * drive_;
            // gentle DC blocker
            s -= dcState_ * 1e-4;
            dcState_ = 0.9999 * dcState_ + 1e-4 * s;

            double yh = harmonics_.process(s);
            double yp = paralimit_.process(s);
            double yx = xformer_.process(s);
            double colored = (wH * yh + wP * yp + wX * yx) * invWSum;
            double y = s * (1.0 - colorAmt) + colored * colorAmt;
            upsampleBuffer_[i] = y;
        }

        oversampler_.processDown(upsampleBuffer_.data(), M, out);
    }

    double processSample(double xin) {
        double out = 0.0;
        processBlock(&xin, &out, 1);
        return out;
    }

private:
    double processSampleInternal(double x) {
        // Scalar path mirrors block path (used when OS=1)
        double t = character_;
        double colorAmt = clampd(std::abs(t) * charGain_, 0.0, 1.0);
        double wH = 0.0, wP = 0.0, wX = 0.0;
        if (t >= 0.0) {
            wH = lerp(0.40, 0.20, t);
            wP = lerp(0.20, 0.60, t);
            wX = lerp(0.40, 0.60, t);
        } else {
            double u = -t;
            wH = lerp(0.40, 0.70, u);
            wP = lerp(0.20, 0.10, u);
            wX = lerp(0.40, 0.20, u);
        }
        double wSum = wH + wP + wX;
        if (wSum < 1e-12) { wH = 1.0; wP = wX = 0.0; wSum = 1.0; }
        double invWSum = 1.0 / wSum;

        double s = x * drive_;
        s -= dcState_ * 1e-4;
        dcState_ = 0.9999 * dcState_ + 1e-4 * s;
        double yh = harmonics_.process(s);
        double yp = paralimit_.process(s);
        double yx = xformer_.process(s);
        double colored = (wH * yh + wP * yp + wX * yx) * invWSum;
        double y = s * (1.0 - colorAmt) + colored * colorAmt;
        return y;
    }

    void updateFromCharacter() {
        double t = character_;
        double red  = std::max(0.0,  t); // aggressive
        double blue = std::max(0.0, -t); // euphoric

        // Harmonics balance: Blue→more even, Red→more odd
        double evenW = lerp(0.6, 0.8, blue) + lerp(0.0, -0.2, red);
        double oddW  = 1.0 - clampd(evenW, 0.2, 0.9);
        harmonics_.setEvenOdd(clampd(evenW, 0.2, 0.9), clampd(oddW, 0.1, 0.8));
        harmonics_.setCurvature(lerp(1.3, 1.6, red));

        // Paralimit: Red→lower threshold and stronger pre-emphasis; Blue→softer
        double thr = lerp(0.92, 0.85, red) + lerp(0.0, 0.03, blue);
        paralimit_.setThreshold(clampd(thr, 0.80, 0.97));
        double preHz = lerp(1800.0, 3000.0, red) + lerp(0.0, -500.0, blue);
        double preGain = lerp(0.35, 0.60, red) + lerp(0.0, -0.15, blue);
        paralimit_.setPreEmphasis(clampd(preHz, 800.0, 6000.0), clampd(preGain, 0.1, 1.0));
        double att = lerp(1.0, 0.5, red) + lerp(0.0, 0.5, blue);
        double rel = lerp(80.0, 120.0, red) + lerp(0.0, -20.0, blue);
        paralimit_.setTimesMs(clampd(att, 0.2, 5.0), clampd(rel, 20.0, 200.0));

        // Transformer core: mostly subtle. Red→slightly more asym; Blue→slightly more bias
        xformer_.setZenerDrive(clampd(0.10 + 0.40 * red, 0.0, 0.8));
        xformer_.setBiasGain(clampd(0.02 + 0.06 * blue, 0.0, 0.2));
        xformer_.setSymmetryK(clampd(1.4 + 0.3 * red - 0.2 * blue, 0.8, 2.4));
        xformer_.setThreshold(clampd(0.97 - 0.05 * red + 0.02 * blue, 0.85, 0.99));
    }

    double fs_ = 44100.0;
    BufferedPolyphaseSIMD oversampler_;
    int oversampleFactor_ = 2;  // Default 2× (was 8×) - better CPU/quality balance

    // Pre-allocated buffer for oversampling (eliminates per-sample allocation)
    std::vector<double> upsampleBuffer_;

    double drive_ = 1.0;
    double character_ = 0.0; // -1..+1
    double charGain_ = 0.9;

    double dcState_ = 0.0;

    DangerousHarmonicsShaper harmonics_;
    DangerousParalimitSIMD  paralimit_;
    DangerousTransformerCoreSIMD xformer_;
};

// VCV Rack Integration Wrapper
struct DangerousEngine {
    // Operating mode (0 = Master Output, 1 = Channel Output)
    int outputMode = 0;

    // Sample rate
    float sampleRate = 44100.0f;

    // Stereo engines
    DangerousEngineCore engineL;
    DangerousEngineCore engineR;

    DangerousEngine() : engineL(44100.0, 2), engineR(44100.0, 2) {
        reset();
    }

    void reset() {
        engineL.reset();
        engineR.reset();
    }

    void setOutputMode(int mode) {
        outputMode = mode;
    }

    void setSampleRate(float sr) {
        sampleRate = sr;
        engineL.setSampleRate(double(sr));
        engineR.setSampleRate(double(sr));
    }

    void setOversampleFactor(int f) {
        engineL.setOversampleFactor(f);
        engineR.setOversampleFactor(f);
    }

    // Process stereo audio with DRIVE and CHARACTER controls
    void process(float& left, float& right, float drive, float character) {
        // Mode-dependent drive scaling
        double driveDb = 0.0;
        if (outputMode == 0) {
            // Master mode: conservative (0dB to +12dB)
            driveDb = double(drive) * 12.0;
        } else {
            // Channel mode: aggressive (0dB to +18dB)
            driveDb = double(drive) * 18.0;
        }

        // CHARACTER parameter: 0→1 maps to -1→+1 (Blue to Red)
        // 0% = -1.0 (Blue/Euphoric - even harmonics, soft limiting)
        // 50% = 0.0 (Neutral - balanced)
        // 100% = +1.0 (Red/Aggressive - odd harmonics, hard limiting)
        double characterMapped = (double(character) * 2.0) - 1.0;

        // Apply settings
        engineL.setDriveDb(driveDb);
        engineR.setDriveDb(driveDb);
        engineL.setCharacter(characterMapped);
        engineR.setCharacter(characterMapped);

        // Process through Dangerous engine
        double inL = double(left);
        double inR = double(right);
        double outL = 0.0;
        double outR = 0.0;

        engineL.processBlock(&inL, &outL, 1);
        engineR.processBlock(&inR, &outR, 1);

        // Apply VCV Rack voltage compliance
        left = clamp(float(outL), -10.0f, 10.0f);
        right = clamp(float(outR), -10.0f, 10.0f);
    }
};

} // namespace ChanOutDangerous
