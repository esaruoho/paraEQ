#pragma once

#include "VaNonlinearSvf.h"
#include <cmath>

/**
 * Autoparametric resonator — bandpass resonator whose centre frequency is gently modulated by (1) a slow
 * envelope follower on |x| (“auto” tracking) and (2) a sinusoidal pump, evoking classical parametric excitation
 * (time-varying reactance) in the spirit of Mandelstam–Papaleksi–type analyses, without modelling any specific circuit.
 */
class AutoparametricResonatorChannel
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr = juce::jmax(100.0, sampleRate);
        pumpPhase = 0.0;
        env = 1.0e-8f;
        fcSmoother = 440.f;
    }

    void reset() noexcept
    {
        svf.reset();
        pumpPhase = 0.0;
        env = 1.0e-8f;
        fcSmoother = 440.f;
    }

    /** Bandpass output for parallel mix into the hot path. */
    float process(float x,
                  float baseHz,
                  float q,
                  float pumpHz,
                  float pumpDepth01,
                  float autoTrack01,
                  float drive01,
                  float envCoeff) noexcept
    {
        env += envCoeff * (std::abs(x) - env);
        env = juce::jmax(1.0e-10f, env);

        pumpPhase += juce::MathConstants<double>::twoPi * (double) juce::jmax(0.01f, pumpHz) / sr;
        if (pumpPhase >= juce::MathConstants<double>::twoPi)
            pumpPhase -= juce::MathConstants<double>::twoPi * std::floor(pumpPhase / juce::MathConstants<double>::twoPi);

        const float autoBend = 1.f + autoTrack01 * juce::jlimit(0.f, 2.2f, env * 12.f);
        const float pump = 1.f + pumpDepth01 * 0.06f * (float) std::sin(pumpPhase);
        float fc = baseHz * autoBend * pump;
        fc = juce::jlimit(40.f, juce::jmin(16000.f, (float) sr * 0.48f), fc);

        constexpr float fcSmooth = 0.11f;
        fcSmoother += fcSmooth * (fc - fcSmoother);

        svf.setCoeffs(fcSmoother, juce::jmax(0.28f, q), sr);
        return svf.processBandpassNonlinear(x, drive01);
    }

private:
    VaNonlinearSvfChannel svf {};
    double sr = 44100.0;
    double pumpPhase = 0.0;
    float env = 1.0e-8f;
    float fcSmoother = 440.f;
};
