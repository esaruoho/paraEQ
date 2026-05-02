#pragma once

#include <JuceHeader.h>
#include <array>

/**
 * Original “ThrillMe-style” mastering colour: serial mild spectral lift, 3-band dynamics
 * (1 kHz / 10 kHz first-order splits), then soft waveshape limiting. Not a clone of any
 * third-party product — behaviour is intentionally simple and stable.
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
        envCoeffAtk = 1.f - static_cast<float>(std::exp(-1.0 / juce::jmax(1.0e-6, sr * 0.00035)));
        envCoeffRel = 1.f - static_cast<float>(std::exp(-1.0 / juce::jmax(1.0e-6, sr * 0.045)));
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

        auto envStep = [&](float env, float z) noexcept -> float
        {
            const float az = std::abs(z);
            if (az > env)
                return env + envCoeffAtk * (az - env);
            return env + envCoeffRel * (az - env);
        };
        st.eL = envStep(st.eL, low);
        st.eM = envStep(st.eM, mid);
        st.eH = envStep(st.eH, high);

        const float rEff = juce::jmax(1.01f, ratio);
        auto bandGain = [&](float env) noexcept -> float
        {
            const float lvlDb = juce::Decibels::gainToDecibels(juce::jmax(1.0e-9f, env));
            if (lvlDb <= thrDb)
                return 1.f;
            const float overDb = lvlDb - thrDb;
            const float redDb = overDb * (1.f - 1.f / rEff);
            return juce::Decibels::decibelsToGain(-redDb);
        };

        const float gL = bandGain(st.eL);
        const float gM = bandGain(st.eM);
        const float gH = bandGain(st.eH);
        float sum = low * gL + mid * gM + high * gH;
        sum = std::tanh(1.05f * sum);
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
    float envCoeffAtk = 0.1f;
    float envCoeffRel = 0.001f;
};
