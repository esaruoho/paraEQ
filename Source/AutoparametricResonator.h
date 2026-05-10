#pragma once

#include "VaNonlinearSvf.h"
#include <cmath>

/**
 * Autoparametric resonator — bandpass resonator whose centre frequency is gently modulated by (1) a slow
 * envelope follower on |x| (“auto” tracking) and (2) a sinusoidal pump. Mixed in parallel as the last roast-stage
 * process (after ring / EQ pink balance), not mid-chain.
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
        snapFcSmoother = true;
    }

    void reset() noexcept
    {
        svf.reset();
        pumpPhase = 0.0;
        env = 1.0e-8f;
        fcSmoother = 440.f;
        snapFcSmoother = true;
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
        if (snapFcSmoother)
        {
            fcSmoother = fc;
            snapFcSmoother = false;
        }
        else
        {
            fcSmoother += fcSmooth * (fc - fcSmoother);
        }

        svf.setCoeffs(fcSmoother, juce::jmax(0.28f, q), sr);
        // Full "drive %" maps to strong tanh in the SVF; at 1.0 it can flatten the loop and kill the BP.
        const float nl = juce::jlimit(0.f, 1.f, drive01 * 0.65f);
        return svf.processBandpassNonlinear(x, nl);
    }

private:
    VaNonlinearSvfChannel svf {};
    double sr = 44100.0;
    double pumpPhase = 0.0;
    float env = 1.0e-8f;
    float fcSmoother = 440.f;
    bool snapFcSmoother = true;
};
