#pragma once

#include <JuceHeader.h>
#include <array>

/**
 * ThrillMe 2–style chain (inspired by published behaviour, not a licensed clone):
 *
 * 1) Spectral enhancer — serial path of four IIR bands (1× lowshelf + 2× peaking + 1× highshelf
 *    per channel). Original spec: “bipole shelving”; here we use JUCE RBJ-style shelves/peaks
 *    as a practical stand-in until fixtures tune them.
 *
 * 2) Dynamics — single-pole @ 1 kHz and 10 kHz (−6 dB/oct style) into low / mid / high; each
 *    band has its own envelope follower and gain (3 parallel compressors), then sum.
 *    Per-band attack/release are fixed from sample rate (auto by band), matching “set
 *    automatically depending the band (l/m/h)”.
 *
 * 3) Limiter — mathematical waveshape on the summed band output (tanh here).
 *
 * Ratio 1…128 stored inverted vs on-screen :1 (low stored = aggressive); see EQ tab labels.
 */
class ThrillMeTone
{
public:
    using IIR = juce::dsp::IIR::Filter<float>;
    using Coef = juce::dsp::IIR::Coefficients<float>;

    void prepare(double sampleRate, int maxChannels) noexcept
    {
        sr = juce::jmax(100.0, sampleRate);
        nCh = juce::jlimit(1, 4, maxChannels);
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sr;
        spec.maximumBlockSize = 512;
        spec.numChannels = 1;
        for (int c = 0; c < nCh; ++c)
        {
            auto& st = ch[(size_t) c];
            for (auto& f : st.spec)
            {
                f.prepare(spec);
                f.reset();
            }
            st.z1 = st.z10 = 0.f;
            st.eL = st.eM = st.eH = 1.0e-8f;
        }
        recomputeSplitCoeffs();
        const auto atkRelFromMs = [this](float atkMs, float relMs, float& atkOut, float& relOut) noexcept
        {
            atkOut = 1.f - static_cast<float>(std::exp(-1.0 / juce::jmax(1.0e-6, sr * (double) atkMs * 0.001)));
            relOut = 1.f - static_cast<float>(std::exp(-1.0 / juce::jmax(1.0e-6, sr * (double) relMs * 0.001)));
        };
        // Band-dependent times (doc: each band’s attack/release set automatically by band).
        atkRelFromMs(2.8f, 150.f, envAtkL, envRelL);
        atkRelFromMs(0.38f, 48.f, envAtkM, envRelM);
        atkRelFromMs(0.11f, 26.f, envAtkH, envRelH);
    }

    void reset() noexcept
    {
        for (int c = 0; c < nCh; ++c)
        {
            auto& st = ch[(size_t) c];
            for (auto& f : st.spec)
                f.reset();
            st.z1 = st.z10 = 0.f;
            st.eL = st.eM = st.eH = 1.0e-8f;
        }
    }

    /** Call when sample rate is known or spectral amount changes (block rate is fine). */
    void updateSpectralCoeffs(double sampleRate, float spectral01) noexcept
    {
        sr = juce::jmax(100.0, sampleRate);
        recomputeSplitCoeffs();
        const float a = juce::jlimit(0.f, 1.f, spectral01);
        const float gLoDb = a * 3.2f;
        const float gP1Db = a * 2.0f;
        const float gP2Db = a * 4.5f;
        const float gHiDb = a * 3.4f;
        const float gLoLin = juce::Decibels::decibelsToGain(gLoDb);
        const float gP1Lin = juce::Decibels::decibelsToGain(gP1Db);
        const float gP2Lin = juce::Decibels::decibelsToGain(gP2Db);
        const float gHiLin = juce::Decibels::decibelsToGain(gHiDb);
        auto coefLo = Coef::makeLowShelf(sr, 170.0f, 0.707f, gLoLin);
        auto coefPk1 = Coef::makePeakFilter(sr, 900.0f, 0.85f, gP1Lin);
        auto coefPk2 = Coef::makePeakFilter(sr, 4200.0f, 1.0f, gP2Lin);
        auto coefHi = Coef::makeHighShelf(sr, 11000.0f, 0.707f, gHiLin);
        for (int c = 0; c < nCh; ++c)
        {
            auto& st = ch[(size_t) c];
            st.spec[0].coefficients = coefLo;
            st.spec[1].coefficients = coefPk1;
            st.spec[2].coefficients = coefPk2;
            st.spec[3].coefficients = coefHi;
        }
    }

    float processChannel(int channelIndex, float x, float thrDb, float ratio) noexcept
    {
        const int c = juce::jlimit(0, nCh - 1, channelIndex);
        auto& st = ch[(size_t) c];
        float y = x;
        for (auto& f : st.spec)
            y = f.processSample(y);

        st.z1 += splitLp1k * (y - st.z1);
        st.z10 += splitLp10k * (y - st.z10);
        const float low = st.z1;
        const float mid = st.z10 - st.z1;
        const float high = y - st.z10;

        auto envStep = [&](float env, float z, float cAtk, float cRel) noexcept -> float
        {
            const float az = std::abs(z);
            if (az > env)
                return env + cAtk * (az - env);
            return env + cRel * (az - env);
        };
        st.eL = envStep(st.eL, low, envAtkL, envRelL);
        st.eM = envStep(st.eM, mid, envAtkM, envRelM);
        st.eH = envStep(st.eH, high, envAtkH, envRelH);

        // Stored "ratio": 1 = hardest, 128 = 1:1 (displayed :1 = 129 − round(stored); see EQ tab).
        const float rCtl = juce::jlimit(1.f, 128.f, ratio);
        // Effective compression ratio R (input-over-threshold : output-over-threshold). At stored 128, R == 1 → no GR.
        // Do NOT use R = 1.01 at 1:1: (1 − 1/R) would leak ~1% of the exp knee, and 128→127 jumps R 1.01→2 (~50× GR).
        const float R = juce::jmax(1.f, 129.f - rCtl);
        auto bandGain = [&](float env) noexcept -> float
        {
            const float lvlDb = juce::Decibels::gainToDecibels(juce::jmax(1.0e-9f, env));
            if (lvlDb <= thrDb || R <= 1.f)
                return 1.f;
            const float overDb = lvlDb - thrDb;
            // Exponential knee on overshoot (doc: exponential threshold / VADP-style squash).
            const float t = juce::jmin(6.f, overDb * 0.11f);
            const float overShape = std::exp(t) - 1.f;
            float redDb = overShape * (1.f - 1.f / R);
            redDb = juce::jmin(36.f, redDb);
            // Floor: per-band GR must not mute a band (unlinked gains used to sum to << dry and kill output).
            return juce::jmax(0.22f, juce::Decibels::decibelsToGain(-redDb));
        };

        const float gL = bandGain(st.eL);
        const float gM = bandGain(st.eM);
        const float gH = bandGain(st.eH);
        float sum = low * gL + mid * gM + high * gH;
        // When L/M/H gains diverge, sum != y and can cancel; crossfade toward full-band y to preserve level.
        const float spread = (std::abs(gL - gM) + std::abs(gM - gH) + std::abs(gL - gH)) * (1.f / 3.f);
        const float cross = juce::jmin(0.45f, spread * 0.35f);
        sum = sum * (1.f - cross) + y * cross;
        // Limiter: mathematical waveshaping (spec) — soft clip / tame overshoots after band sum.
        sum = std::tanh(1.28f * sum);
        if (!std::isfinite(sum))
            return 0.f;
        return sum;
    }

private:
    struct Channel
    {
        std::array<IIR, 4> spec {};
        float z1 = 0.f;
        float z10 = 0.f;
        float eL = 1.0e-8f;
        float eM = 1.0e-8f;
        float eH = 1.0e-8f;
    };

    void recomputeSplitCoeffs() noexcept
    {
        const double fc1 = 1000.0;
        const double fc10 = 10000.0;
        splitLp1k = static_cast<float>(1.0 - std::exp(-juce::MathConstants<double>::twoPi * fc1 / sr));
        splitLp10k = static_cast<float>(1.0 - std::exp(-juce::MathConstants<double>::twoPi * fc10 / sr));
    }

    std::array<Channel, 4> ch {};
    double sr = 44100.0;
    int nCh = 2;
    float splitLp1k = 0.1f;
    float splitLp10k = 0.1f;
    float envAtkL = 0.1f, envRelL = 0.001f;
    float envAtkM = 0.1f, envRelM = 0.001f;
    float envAtkH = 0.1f, envRelH = 0.001f;
};
