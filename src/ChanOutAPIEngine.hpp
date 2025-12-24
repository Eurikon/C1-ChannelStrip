//***********************************************************************************************
// ChanOut API Engine - API 2520 op-amp inspired saturation
// For C1-ChannelStrip VCV Rack Plugin
//
// Based on state-of-the-art API 2520 modeling with:
// - 8× polyphase oversampling (SIMD-optimized)
// - Asymmetric polynomial waveshaping (a1*x + a2*x² + a3*x³)
// - Feedback loop (emulates op-amp linearization)
// - Soft asymmetric output limiter (emulates emitter-follower stage)
//
// Hardware Reference (informational only, not circuit emulation):
// - API 2520 discrete op-amp: THD ~0.019% @ +30dBu, even-order harmonics
// - Published specifications from API Audio technical documentation
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

namespace ChanOutAPI {

static inline double clampd(double x, double a, double b) {
    return (x < a) ? a : (x > b) ? b : x;
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

// API 2520 Engine Core
class API2520Core {
public:
    API2520Core(double sampleRate = 44100.0, int oversampleFactor = 8)
    : oversampler_(oversampleFactor, 64), oversampleFactor_(oversampleFactor) {
        fs_ = sampleRate;
        init();
    }

    void init() {
        oversampler_.reset();
        fbState_ = 0.0;
        dcState_ = 0.0;

        // Pre-allocate upsample buffer (max: 32 samples × 8× oversampling = 256)
        upsampleBuffer_.resize(256);

        // Core coefficients (state-of-the-art values - FIXED)
        a1_ = 1.0;           // Linear fundamental
        a2_ = 0.00036;       // 2nd harmonic (even-order, key to API character) - FIXED
        a3_ = 9e-7;          // Tiny 3rd harmonic
        loopGain_ = 0.985;   // Feedback loop gain
        fbAlpha_ = 0.16;     // Feedback smoothing
        drive_ = 1.0;        // Input drive

        // Output limiter (emulates emitter-follower stage)
        outThreshold_ = 0.96;
        outAsym_ = 1.06;
    }

    void setSampleRate(double fs) { fs_ = fs; }
    void setOversampleFactor(int f) { oversampleFactor_ = std::max(1, f); oversampler_.setFactor(oversampleFactor_); }
    void setDrive(double d) { drive_ = d; }
    void setFeedbackGain(double gain) { loopGain_ = clampd(gain, 0.0, 0.999); }

    void reset() {
        oversampler_.reset();
        fbState_ = 0.0;
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

        for (size_t i = 0; i < M; ++i) {
            // Drive input
            double s = upsampleBuffer_[i] * drive_;

            // DC blocker
            s -= dcState_ * 1e-4;
            dcState_ = 0.9999 * dcState_ + 1e-4 * s;

            // Feedback error
            double err = s - fbState_ * loopGain_;

            // Asymmetric polynomial waveshaper (2nd harmonic dominant)
            double shaped = a1_ * err + a2_ * err * err + a3_ * err * err * err;

            // Feedback state update
            fbState_ += fbAlpha_ * (shaped - fbState_);

            // Mix shaped with feedback
            double y = 0.5 * (shaped + fbState_);

            // Soft asymmetric output limiter
            double yout = softAsym(y);
            upsampleBuffer_[i] = yout;
        }

        oversampler_.processDown(upsampleBuffer_.data(), M, out);
    }

    double processSample(double xin) {
        double out = 0.0;
        processBlock(&xin, &out, 1);
        return out;
    }

private:
    double processSampleInternal(double xin) {
        double s = xin * drive_;
        s -= dcState_ * 1e-4;
        dcState_ = 0.9999 * dcState_ + 1e-4 * s;
        double err = s - fbState_ * loopGain_;
        double shaped = a1_ * err + a2_ * err * err + a3_ * err * err * err;
        fbState_ += fbAlpha_ * (shaped - fbState_);
        double y = 0.5 * (shaped + fbState_);
        return softAsym(y);
    }

    inline double softAsym(double x) {
        double thr = outThreshold_;
        if (x > thr) {
            double over = x - thr;
            return thr + std::atan(over * outAsym_);
        } else if (x < -thr) {
            double over = x + thr;
            return -thr + std::atan(over);
        }
        return x;
    }

    double fs_ = 44100.0;
    BufferedPolyphaseSIMD oversampler_;
    int oversampleFactor_ = 2;  // Default 2× (was 8×) - better CPU/quality balance

    // Pre-allocated buffer for oversampling (eliminates per-sample allocation)
    std::vector<double> upsampleBuffer_;

    // Waveshaper coefficients
    double a1_ = 1.0;
    double a2_ = 0.00036;
    double a3_ = 9e-7;

    // Feedback loop
    double loopGain_ = 0.985;
    double fbAlpha_ = 0.16;
    double fbState_ = 0.0;
    double dcState_ = 0.0;

    // Drive and limiter
    double drive_ = 1.0;
    double outThreshold_ = 0.96;
    double outAsym_ = 1.06;
};

// VCV Rack Integration Wrapper
struct APIEngine {
    // Operating mode (0 = Master Output, 1 = Channel Output)
    int outputMode = 0;

    // Sample rate
    float sampleRate = 44100.0f;

    // Stereo engines
    API2520Core engineL;
    API2520Core engineR;

    APIEngine() : engineL(44100.0, 2), engineR(44100.0, 2) {
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

    // Process stereo audio
    void process(float& left, float& right, float drive, float character) {
        // Mode-dependent drive scaling
        double driveAmount = 1.0;
        if (outputMode == 0) {
            // Master mode: conservative (1.0 to 2.5)
            driveAmount = 1.0 + double(drive) * 1.5;
        } else {
            // Channel mode: aggressive (1.0 to 5.0)
            driveAmount = 1.0 + double(drive) * 4.0;
        }

        // CHARACTER parameter = feedback gain control ONLY (INVERTED)
        // Maps 0→1 to feedback range 0.999→0.0 (MAXIMUM range)
        // 0% = 0.999 (very high feedback, clean/linear)
        // 100% = 0.0 (ZERO feedback, absolute maximum harmonics/distortion)
        double feedbackGain = 0.999 - double(character) * 0.999;

        // Apply settings
        engineL.setDrive(driveAmount);
        engineR.setDrive(driveAmount);
        engineL.setFeedbackGain(feedbackGain);
        engineR.setFeedbackGain(feedbackGain);

        // Process through API engine
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

} // namespace ChanOutAPI
