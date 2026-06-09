#pragma once

#include "VaNonlinearSvf.h"
#include <cmath>

/**
 * Parametric excitation channel.
 *
 * Physics: a resonator with cutoff modulated at a rational multiple of its own natural frequency
 * (Mathieu / Hill equation). The first instability tongue sits at pump rate = 2 * f0 and pumps
 * energy into the resonator at f0 — sub-octave generation that is phase-coherent with the source.
 * Other ratios (1:1, 2:1, 3:1, 4:1) produce weaker tongues with different harmonic content.
 *
 * Structure: ZDF SVF bandpass (same as VaNonlinearSvf used by SVF + APR), cutoff is
 *   f_c(t) = f0 * (1 + depth * pump(t))
 * where pump(t) is either an internal sinusoid (phase-locked to f0 * ratio) or the program
 * envelope (signal-driven excitation: the resonator only rings when there's energy to pump it).
 *
 * Stability: the tanh in the SVF loop bounds exponential growth past the Mathieu boundary.
 * That's the whole point — we want to cross the instability threshold and let the soft clipper
 * settle the amplitude. DC blocker on output kills the offset that asymmetric pumping creates.
 */
class ParametricExcitationChannel
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr = juce::jmax(100.0, sampleRate);
        pumpPhase = 0.0;
        env = 1.0e-8f;
        dcX1 = 0.f;
        dcY1 = 0.f;
        fcSmoother = 440.f;
        snapFcSmoother = true;
        svf.reset();
    }

    void reset() noexcept { prepare(sr); }

    /** Pump-source modes match the editor combo: 0 = internal sinusoid, 1 = program envelope. */
    enum PumpSource { kInternal = 0, kEnvelope = 1 };

    /** Parallel bandpass output, intended for additive mix into the hot path. */
    float process(float x,
                  float baseHz,
                  float q,
                  float ratio,
                  float depth01,
                  float drive01,
                  int pumpSource,
                  float envCoeff) noexcept
    {
        env += envCoeff * (std::abs(x) - env);
        env = juce::jmax(1.0e-10f, env);

        const float baseHzC = juce::jlimit(20.f, juce::jmin(16000.f, (float) sr * 0.45f), baseHz);
        const float ratioC = juce::jlimit(0.25f, 8.f, ratio);
        const float pumpHz = baseHzC * ratioC;
        pumpPhase += juce::MathConstants<double>::twoPi * (double) pumpHz / sr;
        if (pumpPhase >= juce::MathConstants<double>::twoPi)
            pumpPhase -= juce::MathConstants<double>::twoPi * std::floor(pumpPhase / juce::MathConstants<double>::twoPi);

        const float sinPump = (float) std::sin(pumpPhase);
        const float envPump = juce::jlimit(0.f, 4.f, env * 12.f) * sinPump;
        const float pump = (pumpSource == kEnvelope) ? envPump : sinPump;

        // Stability-aware depth: first Mathieu tongue boundary is roughly h ~ 1/Q. Clamp the
        // multiplier so depth01 ~ 1 sits a touch past the boundary, letting tanh do the rest.
        const float qC = juce::jmax(0.5f, q);
        const float maxH = juce::jlimit(0.05f, 0.65f, 1.6f / qC);
        const float h = depth01 * maxH;

        float fc = baseHzC * (1.f + h * pump);
        fc = juce::jlimit(20.f, juce::jmin(16000.f, (float) sr * 0.45f), fc);

        constexpr float fcSmooth = 0.18f;
        if (snapFcSmoother)
        {
            fcSmoother = fc;
            snapFcSmoother = false;
        }
        else
        {
            fcSmoother += fcSmooth * (fc - fcSmoother);
        }

        svf.setCoeffs(fcSmoother, qC, sr);
        const float nl = juce::jlimit(0.f, 1.f, drive01);
        float bp = svf.processBandpassNonlinear(x, nl);

        // 1-pole DC blocker (R ~ 0.995 @ 44.1k gives -3dB ~ 35 Hz). Asymmetric pumping shifts DC.
        const float R = 0.995f;
        const float y = bp - dcX1 + R * dcY1;
        dcX1 = bp;
        dcY1 = y;
        return y;
    }

private:
    VaNonlinearSvfChannel svf {};
    double sr = 44100.0;
    double pumpPhase = 0.0;
    float env = 1.0e-8f;
    float dcX1 = 0.f, dcY1 = 0.f;
    float fcSmoother = 440.f;
    bool snapFcSmoother = true;
};
