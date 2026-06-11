#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <complex>
#include <cstring>

void VaNonlinearSvfChannel::setCoeffs(float fcHz, float Q, double sampleRate) noexcept
{
    const double sr = juce::jmax(100.0, sampleRate);
    fcHz = juce::jlimit(20.f, 19000.f, fcHz);
    Q = juce::jlimit(0.25f, 40.f, Q);
    const double g = std::tan(juce::MathConstants<double>::pi * (double) fcHz / sr);
    k = (float) (1.0 / (double) Q);
    const double a = 1.0 + g * (g + (double) k);
    a1 = (float) (1.0 / a);
    a2 = (float) (g * a1);
    a3 = (float) (g * a2);
}

float VaNonlinearSvfChannel::processBandpassNonlinear(float in, float nl01) noexcept
{
    const float v0 = in;
    float v3 = v0 - ic2eq;
    const float nl = juce::jlimit(0.f, 1.f, nl01);
    if (nl > 1.0e-6f)
    {
        const float t = 1.f + nl * 7.f;
        v3 = std::tanh(t * v3) / t;
    }
    const float v1 = a1 * ic1eq + a2 * v3;
    const float v2 = a2 * ic1eq + a3 * v3;
    ic1eq = 2.f * v1 - ic1eq;
    ic2eq = 2.f * v2 - ic2eq;
    return v1;
}

namespace
{
    constexpr float kShelfQ = 0.707f;
    constexpr float kTwoPi = juce::MathConstants<float>::twoPi;
    constexpr int kCoeffUpdateInterval = 4;

    /** All three ThrillMe knobs default to normalised 0.5 (centre = "affect nothing", like the original):
        spectral mid-travel, threshold 0 dB, ratio 1:1. Ranges below are symmetric about the transparent
        value so the centre detent is the neutral point; turn one way to engage GR (the far half stays
        transparent because the DSP clamps thrDb>0 and ratio>128 to no-op). */
    constexpr float kThrillSpecDefault = 0.5f;

    /** 0 dB = do not compress until band envelope exceeds full scale (line-level mixes stay open).
        Sits at the centre of the symmetric -80..+80 dB range → normalised 0.5. */
    inline float thrillThresholdDefaultDb() noexcept
    {
        return 0.0f;
    }

    /** Stored value 129 - R = 1 (true 1:1); wet path matches dry dynamics until ratio is turned harder.
        Sits at the centre of the 1..255 range → normalised 0.5 (the 128..255 half is clamped to 1:1). */
    inline float thrillRatioDefaultStored() noexcept
    {
        return 128.0f;
    }

    using C = std::complex<double>;

    /** Complex frequency response H(f) for JUCE IIR coefficients (same as mag·e^{jφ}). */
    static C iirCoeffComplexAtHz(const juce::dsp::IIR::Coefficients<float>* c, double fHz, double sr) noexcept
    {
        if (c == nullptr)
            return C(1.0, 0.0);
        const double mag = c->getMagnitudeForFrequency(fHz, sr);
        const double ph = c->getPhaseForFrequency(fHz, sr);
        return C(mag * std::cos(ph), mag * std::sin(ph));
    }

    /** Linear bandpass V1/V0 for VaNonlinearSvfChannel at drive 0 (matches setCoeffs / trapezoidal one-step update). */
    static C svfLinearBandpassTransferHz(double fcHz, double Q, double sr, double fHz) noexcept
    {
        fcHz = juce::jlimit(20.0, 19000.0, fcHz);
        Q = juce::jlimit(0.25, 40.0, Q);
        const double g = std::tan(juce::MathConstants<double>::pi * fcHz / sr);
        const double kk = 1.0 / Q;
        const double a = 1.0 + g * (g + kk);
        const double ca1 = 1.0 / a;
        const double ca2 = g * ca1;
        const double ca3 = g * ca2;
        const double w = juce::MathConstants<double>::twoPi * juce::jlimit(1.0, sr * 0.499, fHz) / sr;
        const C z(std::cos(w), std::sin(w));
        const C zp1 = z + C(1.0, 0.0);
        const C one(1.0, 0.0);
        const C two(2.0, 0.0);
        const C D = zp1 - two * ca1;
        if (std::abs(D) < 1.0e-20)
            return C(0.0, 0.0);
        const C v1_over_v3 = ca2 * zp1 / D;
        const C inner = two * (ca2 * ca2) / D + ca3;
        const C v3_over_v0 = one / (one + two * (one / zp1) * inner);
        return v1_over_v3 * v3_over_v0;
    }

    /** Positive vs negative drive → more even harmonics when dirt > 0. posMul/negMul scale asym (roast core flavour). */
    float shapeMusicalWetAsym(float x, float push, float dirt01, float posMul, float negMul) noexcept
    {
        const float d = juce::jlimit(0.f, 1.f, dirt01);
        const float aPos = push * (1.0f + 0.55f * posMul * d);
        const float aNeg = push * (1.0f - 0.38f * negMul * d);
        if (x >= 0.0f)
            return std::tanh(aPos * x);
        return std::tanh(aNeg * x);
    }

    struct RoastCoreFlavourCoeffs
    {
        float dirtScale;
        float crunchBias;
        float pushScale;
        float posAsymMul;
        float negAsymMul;
        float inputGain;
    };

    static constexpr RoastCoreFlavourCoeffs kRoastCoreFlavours[5] = {
        { 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
        { 1.22f, 0.0f, 1.0f, 1.18f, 1.0f, 1.0f },
        { 1.0f, 0.14f, 1.08f, 1.1f, 1.04f, 1.04f },
        { 0.88f, -0.12f, 0.94f, 0.92f, 0.92f, 1.0f },
        { 1.12f, 0.06f, 1.12f, 1.22f, 1.06f, 1.05f },
    };

    float dcBlockSample(float x, float& dcState, float leakCoeff) noexcept
    {
        dcState += leakCoeff * (x - dcState);
        return x - dcState;
    }

    float applyCoreSaturation(float x, float drive01, float dirt01, float crunch01, float& dcState, float leakCoeff,
                              int roastCoreShapeIdx) noexcept
    {
        if (drive01 <= 1.0e-8f)
            return x;
        const int fi = juce::jlimit(0, 4, roastCoreShapeIdx);
        const RoastCoreFlavourCoeffs& fc = kRoastCoreFlavours[(size_t) fi];
        x = juce::jlimit(-10.0f, 10.0f, x * fc.inputGain);
        const float push = 1.0f + drive01 * 24.0f * fc.pushScale;
        const float dUse = juce::jlimit(0.f, 1.f, dirt01 * fc.dirtScale);
        const float c = juce::jlimit(0.f, 1.f, crunch01 + fc.crunchBias);
        const float soft = shapeMusicalWetAsym(x, push, dUse, fc.posAsymMul, fc.negAsymMul);
        const float xc = juce::jlimit(-12.f, 12.f, x * 1.15f);
        const float roasted = xc / (1.f + std::abs(xc));
        const float wet = soft * (1.f - c) + roasted * c;
        const float y = x * (1.0f - drive01) + wet * drive01;
        return dcBlockSample(y, dcState, leakCoeff);
    }

    float applyRoastPunch(float x, float amt01, float& env, float decayCoeff) noexcept
    {
        if (amt01 <= 1.0e-8f)
            return x;
        const float pk = std::abs(x);
        env = juce::jmax(pk, env * decayCoeff);
        const float thresh = 0.85f - amt01 * 0.63f;
        float g = 1.f;
        if (env > thresh)
            g = 1.f + amt01 * (thresh / juce::jmax(1.0e-8f, env) - 1.f);
        return x * g;
    }

    float applyRoastGlue(float x, float amt01, float& env, float decayCoeff) noexcept
    {
        if (amt01 <= 1.0e-8f)
            return x;
        const float pk = std::abs(x);
        env = juce::jmax(pk, env * decayCoeff);
        const float thresh = 0.95f - amt01 * 0.57f;
        float g = 1.f;
        if (env > thresh)
            g = 1.f + amt01 * (thresh / juce::jmax(1.0e-8f, env) - 1.f);
        return x * g;
    }

    float applyRoastLoFi(float x, float amt01, int& counter, float& hold, int downsample) noexcept
    {
        if (amt01 <= 1.0e-8f)
            return x;
        const int ds = juce::jlimit(1, 12, downsample);
        ++counter;
        if (counter >= ds)
        {
            counter = 0;
            hold = x;
        }
        const int bits = juce::jlimit(5, 14, 14 - (int) (amt01 * 9.0f));
        const float scale = std::pow(2.f, float(bits - 1));
        float q = std::floor(hold * scale + 0.5f) / juce::jmax(1.0e-8f, scale);
        return x * (1.f - amt01) + q * amt01;
    }

    juce::NormalisableRange<float> freqRangeSkewed(float minHz, float maxHz, float centreHz)
    {
        juce::NormalisableRange<float> r(minHz, maxHz);
        r.setSkewForCentre(centreHz);
        return r;
    }

    float qFromBandwidthHz(float centreHz, float bwHz) noexcept
    {
        if (bwHz < 1.0f)
            bwHz = 1.0f;
        const float q = centreHz / bwHz;
        if (q < 0.1f)
            return 0.1f;
        if (q > 50.0f)
            return 50.0f;
        return q;
    }

    float modGainDb(float base, float sinVal, float depth01) noexcept
    {
        return juce::jlimit(-30.0f, 30.0f, base + sinVal * depth01 * 12.0f);
    }

    float modCfHz(float base, float minHz, float maxHz, float sinVal, float depth01) noexcept
    {
        const float mult = std::pow(2.0f, sinVal * depth01 * 0.45f);
        return juce::jlimit(minHz, maxHz, base * mult);
    }

    float modBwHz(float base, float minHz, float maxHz, float sinVal, float depth01) noexcept
    {
        const float span = (maxHz - minHz) * 0.35f;
        return juce::jlimit(minHz, maxHz, base + sinVal * depth01 * span);
    }

    /** Integer Hz for EQ frequencies — matches SliderAttachment + host display (avoids 7-decimal default). */
    juce::AudioParameterFloatAttributes eqHzParameterAttributes()
    {
        return juce::AudioParameterFloatAttributes()
            .withLabel("Hz")
            .withStringFromValueFunction([](float v, int)
            {
                return juce::String(juce::roundToInt(v)) + " Hz";
            })
            .withValueFromStringFunction([](const juce::String& text) { return text.getFloatValue(); });
    }

    /** LFO rate: two decimals, still readable in narrow text boxes. */
    juce::AudioParameterFloatAttributes lfoRateHzParameterAttributes()
    {
        return juce::AudioParameterFloatAttributes()
            .withLabel("Hz")
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 2) + " Hz"; })
            .withValueFromStringFunction([](const juce::String& text) { return text.getFloatValue(); });
    }

    float blockRms(const juce::AudioBuffer<float>& buf, int numCh, int numSamps) noexcept
    {
        if (numCh <= 0 || numSamps <= 0)
            return 0.f;
        double sumSq = 0.0;
        int n = 0;
        for (int c = 0; c < numCh; ++c)
        {
            const float* p = buf.getReadPointer(c);
            for (int i = 0; i < numSamps; ++i)
            {
                const double s = static_cast<double>(p[i]);
                sumSq += s * s;
                ++n;
            }
        }
        return static_cast<float>(std::sqrt(sumSq / static_cast<double>(juce::jmax(n, 1))));
    }

    /** Matches juce::dsp::Limiter::update() outputVolume target: applied even when compressors are at unity. */
    float juceLimiterFixedMakeupLinear(float secondStageThresholdDb) noexcept
    {
        constexpr float firstStageRatioInv = 1.0f / 4.0f;
        return std::pow(10.f, 10.f * (1.f - firstStageRatioInv) / 40.f)
            * juce::Decibels::decibelsToGain(-secondStageThresholdDb);
    }

    /** Presets used coreOn / core2On (true = saturator active). Now core1Bypass / core2Bypass (true = bypass). */
    void migrateLegacyCoreBypassXml(juce::XmlElement& e)
    {
        if (e.hasTagName("PARAM"))
        {
            const juce::String id = e.getStringAttribute("id");
            if (id == "coreOn")
            {
                const bool oldEnabled = e.getDoubleAttribute("value") > 0.5;
                e.setAttribute("id", "core1Bypass");
                e.setAttribute("value", oldEnabled ? 0.0 : 1.0);
            }
            else if (id == "core2On")
            {
                const bool oldEnabled = e.getDoubleAttribute("value") > 0.5;
                e.setAttribute("id", "core2Bypass");
                e.setAttribute("value", oldEnabled ? 0.0 : 1.0);
            }
        }
        for (auto* c : e.getChildIterator())
            migrateLegacyCoreBypassXml(*c);
    }

    static void walkThrillRatioFlipParams(juce::XmlElement& x)
    {
        if (x.hasTagName("PARAM"))
        {
            const juce::String id = x.getStringAttribute("id");
            if (id == "thrill1Ratio" || id == "thrill2Ratio")
            {
                const int vi = juce::jlimit(1, 128, juce::roundToInt(x.getDoubleAttribute("value")));
                x.setAttribute("value", (double) (129 - vi));
            }
        }
        for (auto* c : x.getChildIterator())
            walkThrillRatioFlipParams(*c);
    }

    /** Stored thrill*Ratio used to equal effective GR ratio; now stored is inverted vs :1 display (original: left = hard). */
    void migrateThrillRatioSemanticsXml(juce::XmlElement& root)
    {
        if (root.getIntAttribute("thrillRatioSemanticRev", 0) >= 2)
            return;
        walkThrillRatioFlipParams(root);
        root.setAttribute("thrillRatioSemanticRev", 2);
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout ParaEQ301AudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "hiCf", "Hi Cf",
        freqRangeSkewed(500.0f, 18000.0f, 3000.0f),
        5000.0f,
        eqHzParameterAttributes()));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "hiGain", "Hi Gain",
        juce::NormalisableRange<float>(-30.0f, 30.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "mid1Cf", "Mid1 Cf",
        freqRangeSkewed(20.0f, 18000.0f, 1000.0f),
        1000.0f,
        eqHzParameterAttributes()));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "mid1Bw", "Mid1 Bw",
        freqRangeSkewed(85.0f, 2000.0f, 400.0f),
        400.0f,
        eqHzParameterAttributes()));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "mid1Gain", "Mid1 Gain",
        juce::NormalisableRange<float>(-30.0f, 30.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "mid2Cf", "Mid2 Cf",
        freqRangeSkewed(20.0f, 18000.0f, 1000.0f),
        2500.0f,
        eqHzParameterAttributes()));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "mid2Bw", "Mid2 Bw",
        freqRangeSkewed(85.0f, 2000.0f, 400.0f),
        400.0f,
        eqHzParameterAttributes()));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "mid2Gain", "Mid2 Gain",
        juce::NormalisableRange<float>(-30.0f, 30.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "lowCf", "Low Cf",
        freqRangeSkewed(20.0f, 10000.0f, 200.0f),
        120.0f,
        eqHzParameterAttributes()));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "lowGain", "Low Gain",
        juce::NormalisableRange<float>(-30.0f, 30.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    layout.add(std::make_unique<juce::AudioParameterBool>("core1Bypass", "Bypass Saturator 1", true));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "coreSat", "ThrillMe 1 wet",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        1.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    layout.add(std::make_unique<juce::AudioParameterBool>("core2Bypass", "Bypass Saturator 2", true));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "core2Sat", "ThrillMe 2 wet",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        1.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "thrill1Spec", "ThrillMe 1 spectral",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        kThrillSpecDefault,
        juce::AudioParameterFloatAttributes().withLabel("%")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "thrill1ThreshDb", "ThrillMe 1 threshold",
        juce::NormalisableRange<float>(-80.0f, 80.0f, 0.1f),
        thrillThresholdDefaultDb(),
        juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "thrill1Ratio", "ThrillMe 1 ratio",
        juce::NormalisableRange<float>(1.0f, 255.0f, 0.1f),
        thrillRatioDefaultStored(),
        juce::AudioParameterFloatAttributes().withLabel(":1")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "thrill2Spec", "ThrillMe 2 spectral",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        kThrillSpecDefault,
        juce::AudioParameterFloatAttributes().withLabel("%")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "thrill2ThreshDb", "ThrillMe 2 threshold",
        juce::NormalisableRange<float>(-80.0f, 80.0f, 0.1f),
        thrillThresholdDefaultDb(),
        juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "thrill2Ratio", "ThrillMe 2 ratio",
        juce::NormalisableRange<float>(1.0f, 255.0f, 0.1f),
        thrillRatioDefaultStored(),
        juce::AudioParameterFloatAttributes().withLabel(":1")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "coreDirt", "Core dirt",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.28f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "coreLifeDepth", "Core life depth",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    auto coreLifeHzRange = juce::NormalisableRange<float>(0.03f, 2.5f, 0.01f, 0.4f);
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "coreLifeHz", "Core life rate",
        coreLifeHzRange,
        0.22f,
        lfoRateHzParameterAttributes()));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "coreCrunch", "Core crunch",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    juce::StringArray roastCoreShapeItems;
    roastCoreShapeItems.add("Classic");
    roastCoreShapeItems.add("Warm asym");
    roastCoreShapeItems.add("Aggro");
    roastCoreShapeItems.add("Tape soft");
    roastCoreShapeItems.add("Punch");
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        "roastCoreShape", "Roast core flavour", roastCoreShapeItems, 0));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "roastPreEmphDb", "Roast pre HF",
        juce::NormalisableRange<float>(0.0f, 14.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "roastPostTiltDb", "Roast post tilt",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "roastBoostTrack", "Roast boost track",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "roastMidChain", "Roast mid-chain",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "roastPunch", "Roast punch",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "roastGlue", "Roast glue",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "roastLoFi", "Roast lo-fi",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "roastRing", "Roast ring",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "roastEnvDrive", "Roast env drive",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "roastFlutter", "Roast flutter",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "roastStereoWide", "Roast stereo life",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "roastOutputTrimDb", "Roast output trim",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "roastLowChain", "Roast low-chain",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    layout.add(std::make_unique<juce::AudioParameterBool>("linearEqListen", "Linear EQ only", false));

    layout.add(std::make_unique<juce::AudioParameterBool>("eqPinkLevelBal", "EQ pink level balance", false));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        "oversample", "Oversampling",
        juce::StringArray{ "Off", "2x", "4x" }, 0));

    layout.add(std::make_unique<juce::AudioParameterBool>("svfEnable", "SVF resonator", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "svfMix", "SVF mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "svfCf", "SVF cf",
        freqRangeSkewed(80.0f, 12000.0f, 900.0f),
        950.0f,
        eqHzParameterAttributes()));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "svfQ", "SVF Q",
        juce::NormalisableRange<float>(0.35f, 40.0f, 0.01f, 0.42f),
        4.5f,
        juce::AudioParameterFloatAttributes()));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "svfDrive", "SVF VA drive",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.22f,
        juce::AudioParameterFloatAttributes().withLabel("%")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "svfGainDb", "SVF band gain",
        juce::NormalisableRange<float>(-18.0f, 18.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        "univBell", "Universal bell (Orfanidis)", false));

    layout.add(std::make_unique<juce::AudioParameterBool>("anharmBankEnable", "Anharmonic partial bank", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "anharmFundHz", "Anharm fund",
        freqRangeSkewed(40.0f, 6000.0f, 200.0f),
        220.0f,
        eqHzParameterAttributes()));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "anharmInharmB", "Inharmonicity B",
        juce::NormalisableRange<float>(0.0f, 0.012f, 0.00005f, 0.38f),
        0.0012f,
        juce::AudioParameterFloatAttributes().withLabel("B")));
    layout.add(std::make_unique<juce::AudioParameterInt>("anharmPartials", "Anharm partials", 2, kAnharmMaxPartials, 5));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "anharmMix", "Anharm mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "anharmPerPartialDb", "Anharm partial dB",
        juce::NormalisableRange<float>(-18.0f, 12.0f, 0.1f),
        -3.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "anharmQ", "Anharm Q",
        juce::NormalisableRange<float>(4.0f, 40.0f, 0.1f, 0.45f),
        18.0f,
        juce::AudioParameterFloatAttributes()));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "anharmNl", "Anharm wet sat",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "anharmEnvQ", "Anharm env to Q",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.35f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    layout.add(std::make_unique<juce::AudioParameterBool>("aprEnable", "APR", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "aprMix", "APR mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.22f,
        juce::AudioParameterFloatAttributes().withLabel("%")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "aprBaseHz", "APR base Hz",
        freqRangeSkewed(60.0f, 8000.0f, 440.0f),
        440.0f,
        eqHzParameterAttributes()));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "aprQ", "APR Q",
        juce::NormalisableRange<float>(0.5f, 40.0f, 0.05f, 0.42f),
        8.0f,
        juce::AudioParameterFloatAttributes()));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "aprPumpHz", "APR pump Hz",
        juce::NormalisableRange<float>(0.05f, 32.0f, 0.01f, 0.38f),
        2.2f,
        lfoRateHzParameterAttributes()));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "aprPumpDepth", "APR pump depth",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.35f,
        juce::AudioParameterFloatAttributes().withLabel("%")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "aprAutoTrack", "APR auto track",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.42f,
        juce::AudioParameterFloatAttributes().withLabel("%")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "aprDrive", "APR drive",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.22f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    layout.add(std::make_unique<juce::AudioParameterBool>("parexEnable", "Parametric excitation", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "parexMix", "ParEx mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.25f,
        juce::AudioParameterFloatAttributes().withLabel("%")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "parexBaseHz", "ParEx base Hz",
        freqRangeSkewed(40.0f, 8000.0f, 220.0f),
        110.0f,
        eqHzParameterAttributes()));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "parexQ", "ParEx Q",
        juce::NormalisableRange<float>(0.5f, 40.0f, 0.05f, 0.42f),
        8.0f,
        juce::AudioParameterFloatAttributes()));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        "parexRatio", "ParEx pump ratio",
        juce::StringArray{ "1/2 (sub)", "1/1", "2/1 (Mathieu)", "3/1", "4/1" }, 2));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "parexDepth", "ParEx depth",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.35f,
        juce::AudioParameterFloatAttributes().withLabel("%")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "parexDrive", "ParEx drive",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.30f,
        juce::AudioParameterFloatAttributes().withLabel("%")));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        "parexPumpSrc", "ParEx pump source",
        juce::StringArray{ "Internal", "Envelope" }, 1));

    layout.add(std::make_unique<juce::AudioParameterBool>("testToneOn", "Test sine", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "testToneHz", "Test sine Hz",
        freqRangeSkewed(20.0f, 16000.0f, 440.0f),
        220.0f,
        eqHzParameterAttributes()));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "testToneDb", "Test sine level",
        juce::NormalisableRange<float>(-60.0f, 0.0f, 0.1f),
        -18.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    juce::StringArray shaperModeItems;
    shaperModeItems.add("Off");
    shaperModeItems.add("Magnet");
    shaperModeItems.add("Chebyshev");
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        "shaperMode", "Paketti shaper", shaperModeItems, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "shaperMix", "Shaper mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "shaperPreGain", "Shaper input",
        juce::NormalisableRange<float>(0.25f, 16.0f, 0.01f, 0.35f),
        3.0f,
        juce::AudioParameterFloatAttributes().withLabel("x")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "shaperPostTrim", "Shaper trim",
        juce::NormalisableRange<float>(0.0f, 2.0f, 0.01f),
        1.0f,
        juce::AudioParameterFloatAttributes().withLabel("x")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "magDrive", "Magnet drive",
        juce::NormalisableRange<float>(0.0f, 2.5f, 0.01f, 0.5f),
        1.0f,
        juce::AudioParameterFloatAttributes()));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "magTilt", "Magnet tilt",
        juce::NormalisableRange<float>(-1.0f, 1.0f, 0.01f),
        0.0f,
        juce::AudioParameterFloatAttributes()));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "magBias", "Magnet bias",
        juce::NormalisableRange<float>(-1.0f, 1.0f, 0.01f),
        0.0f,
        juce::AudioParameterFloatAttributes()));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "magTiltLimit", "Magnet slew",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.5f,
        juce::AudioParameterFloatAttributes().withLabel("%")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "magFeedback", "Magnet FB",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "magOut", "Magnet out",
        juce::NormalisableRange<float>(0.2f, 2.0f, 0.01f),
        1.0f,
        juce::AudioParameterFloatAttributes().withLabel("x")));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        "magShape", "Magnet sat type",
        juce::StringArray { "Soft", "Tanh" },
        0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "magEnergy", "Magnet energy",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "magEnergyMs", "Magnet energy time",
        juce::NormalisableRange<float>(1.0f, 500.0f, 0.1f, 0.4f),
        80.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "chebyYL", "Cheby curve L",
        juce::NormalisableRange<float>(-1.0f, 1.0f, 0.01f),
        -1.0f,
        juce::AudioParameterFloatAttributes()));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "chebyYC", "Cheby curve C",
        juce::NormalisableRange<float>(-1.0f, 1.0f, 0.01f),
        0.0f,
        juce::AudioParameterFloatAttributes()));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "chebyYR", "Cheby curve R",
        juce::NormalisableRange<float>(-1.0f, 1.0f, 0.01f),
        1.0f,
        juce::AudioParameterFloatAttributes()));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "chebyHarmMacro", "Cheby harmonics macro",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        1.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "chebyPolyPow", "Cheby per-poly pow",
        juce::NormalisableRange<float>(0.25f, 4.0f, 0.001f, 0.5f, true),
        1.0f,
        juce::AudioParameterFloatAttributes().withLabel("k")));

    static const char* chebyHarmIds[12] = {
        "chebyH2", "chebyH3", "chebyH4", "chebyH5", "chebyH6", "chebyH7",
        "chebyH8", "chebyH9", "chebyH10", "chebyH11", "chebyH12", "chebyH13"
    };
    for (int i = 0; i < 12; ++i)
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            chebyHarmIds[i], juce::String("Cheby H") + juce::String(i + 2),
            juce::NormalisableRange<float>(-1.0f, 1.0f, 0.001f, 0.5f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel("wt")));

    static const char* chebyHarmPowIds[12] = {
        "chebyH2Pow", "chebyH3Pow", "chebyH4Pow", "chebyH5Pow", "chebyH6Pow", "chebyH7Pow",
        "chebyH8Pow", "chebyH9Pow", "chebyH10Pow", "chebyH11Pow", "chebyH12Pow", "chebyH13Pow"
    };
    for (int i = 0; i < 12; ++i)
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            chebyHarmPowIds[i], juce::String("Cheby H") + juce::String(i + 2) + " pow",
            juce::NormalisableRange<float>(0.25f, 4.0f, 0.001f, 0.5f, true),
            1.0f,
            juce::AudioParameterFloatAttributes().withLabel("k")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "lfoStereoPhase", "LFO L/R phase",
        juce::NormalisableRange<float>(0.0f, 180.0f, 1.0f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("deg")));

    layout.add(std::make_unique<juce::AudioParameterBool>("lfoHostSync", "LFO host tempo sync", false));

    // Fastest at the top, slowest at the bottom; each straight value is followed by its triplet
    // (and dotted, where present), so 1/16 -> 1/16 triplet -> 1/16 dot read in a column.
    // Indices must stay in lock-step with beatsPerCycle[] in motionLfoHzFromSyncDivision().
    juce::StringArray lfoSyncDivItems;
    lfoSyncDivItems.add("1/64");          // 0
    lfoSyncDivItems.add("1/32");          // 1
    lfoSyncDivItems.add("1/32 triplet");  // 2
    lfoSyncDivItems.add("1/16");          // 3
    lfoSyncDivItems.add("1/16 triplet");  // 4
    lfoSyncDivItems.add("1/16 dot");      // 5
    lfoSyncDivItems.add("1/8");           // 6
    lfoSyncDivItems.add("1/8 triplet");   // 7
    lfoSyncDivItems.add("1/4");           // 8
    lfoSyncDivItems.add("1/4 triplet");   // 9
    lfoSyncDivItems.add("1/2");           // 10
    lfoSyncDivItems.add("1 bar");         // 11
    lfoSyncDivItems.add("2 bars");        // 12
    lfoSyncDivItems.add("4 bars");        // 13
    lfoSyncDivItems.add("8 bars");        // 14
    lfoSyncDivItems.add("16 bars");       // 15
    lfoSyncDivItems.add("32 bars");       // 16
    lfoSyncDivItems.add("64 bars");       // 17
    layout.add(std::make_unique<juce::AudioParameterChoice>("lfoHostSyncDiv", "LFO sync division", lfoSyncDivItems, 6));

    // "Rate jitter": when BPM sync is on, a slow LFO morphs the synced rate between half-time and
    // double-time of the chosen division (depth 1 = 0.5x..2x). Off by default = steady synced rate.
    layout.add(std::make_unique<juce::AudioParameterBool>("lfoSyncJitterEnable", "LFO sync jitter", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "lfoSyncJitterDepth", "LFO jitter depth",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "lfoSyncJitterRateHz", "LFO jitter rate",
        juce::NormalisableRange<float>(0.01f, 4.0f, 0.01f, 0.4f), 0.2f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    // 0 Hz = LFO phase frozen (no sweep); >0 starts from very slow rates.
    auto lfoRateRange = juce::NormalisableRange<float>(0.0f, 14.0f, 0.01f, 0.35f);

    layout.add(std::make_unique<juce::AudioParameterFloat>("lfoHiRate", "Hi LFO Hz", lfoRateRange, 2.0f, lfoRateHzParameterAttributes()));
    const juce::NormalisableRange<float> lfoDepthRange(0.f, 1.f, 0.01f);

    layout.add(std::make_unique<juce::AudioParameterFloat>("lfoHiDepthGain", "Hi LFO gain", lfoDepthRange, 0.f, juce::AudioParameterFloatAttributes()));
    layout.add(std::make_unique<juce::AudioParameterFloat>("lfoHiDepthCf", "Hi LFO cf", lfoDepthRange, 0.f, juce::AudioParameterFloatAttributes()));

    layout.add(std::make_unique<juce::AudioParameterFloat>("lfoM1Rate", "M1 LFO Hz", lfoRateRange, 2.0f, lfoRateHzParameterAttributes()));
    layout.add(std::make_unique<juce::AudioParameterFloat>("lfoM1DepthGain", "M1 LFO gain", lfoDepthRange, 0.f, juce::AudioParameterFloatAttributes()));
    layout.add(std::make_unique<juce::AudioParameterFloat>("lfoM1DepthCf", "M1 LFO cf", lfoDepthRange, 0.f, juce::AudioParameterFloatAttributes()));
    layout.add(std::make_unique<juce::AudioParameterFloat>("lfoM1DepthBw", "M1 LFO bw", lfoDepthRange, 0.f, juce::AudioParameterFloatAttributes()));

    layout.add(std::make_unique<juce::AudioParameterFloat>("lfoM2Rate", "M2 LFO Hz", lfoRateRange, 2.0f, lfoRateHzParameterAttributes()));
    layout.add(std::make_unique<juce::AudioParameterFloat>("lfoM2DepthGain", "M2 LFO gain", lfoDepthRange, 0.f, juce::AudioParameterFloatAttributes()));
    layout.add(std::make_unique<juce::AudioParameterFloat>("lfoM2DepthCf", "M2 LFO cf", lfoDepthRange, 0.f, juce::AudioParameterFloatAttributes()));
    layout.add(std::make_unique<juce::AudioParameterFloat>("lfoM2DepthBw", "M2 LFO bw", lfoDepthRange, 0.f, juce::AudioParameterFloatAttributes()));

    layout.add(std::make_unique<juce::AudioParameterFloat>("lfoLoRate", "Lo LFO Hz", lfoRateRange, 2.0f, lfoRateHzParameterAttributes()));
    layout.add(std::make_unique<juce::AudioParameterFloat>("lfoLoDepthGain", "Lo LFO gain", lfoDepthRange, 0.f, juce::AudioParameterFloatAttributes()));
    layout.add(std::make_unique<juce::AudioParameterFloat>("lfoLoDepthCf", "Lo LFO cf", lfoDepthRange, 0.f, juce::AudioParameterFloatAttributes()));

    layout.add(std::make_unique<juce::AudioParameterBool>("outLimOn", "Output limiter", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "outLimThresh", "Limiter threshold",
        juce::NormalisableRange<float>(-16.0f, -0.3f, 0.1f),
        -2.5f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "outLimRelease", "Limiter release",
        juce::NormalisableRange<float>(20.0f, 400.0f, 1.0f, 0.35f),
        90.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "masterDryWet", "Dry/Wet",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
        1.0f,
        // DRY/WET IS ALWAYS A PERCENTAGE (0-100%), everywhere: custom UI, host automation, AU generic
        // view. Format on the PARAMETER so it is the single source of truth (SliderAttachment delegates
        // the slider's text box to this, overriding any slider-level textFromValueFunction).
        juce::AudioParameterFloatAttributes()
            .withLabel("%")
            .withStringFromValueFunction([](float v, int) { return juce::String(juce::roundToInt(v * 100.0f)) + " %"; })
            .withValueFromStringFunction([](const juce::String& t) { return juce::jlimit(0.0f, 1.0f, t.getFloatValue() / 100.0f); })));

    return layout;
}

ParaEQ301AudioProcessor::ParaEQ301AudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMS", createParameterLayout())
{
    chebyLutBuilder = std::make_unique<ChebyLutBuilder>();
    for (auto& p : motionLfoUiPhase)
        p.store(0.f, std::memory_order_relaxed);
    publishMotionEqUiSnapshot(0, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, false);
    publishMotionEqUiSnapshot(1, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, false);
    spectrumSeq.store(0, std::memory_order_relaxed);
    for (int i = 0; i < kSpectrumBins; ++i)
    {
        spectrumPubBefore[i] = -100.f;
        spectrumPubAfter[i] = -100.f;
        spectrumSmoothBefore[i] = -100.f;
        spectrumSmoothAfter[i] = -100.f;
    }
}

void ParaEQ301AudioProcessor::prepareToPlay(const double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = currentSampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock > 0 ? samplesPerBlock : 512);
    spec.numChannels = 1;

    // Must match actual bus width (mono = 1). Do not force 2 — limiter/compressors break on mono otherwise.
    maxChannelsPrepared = juce::jmax(1, juce::jmax(getTotalNumOutputChannels(), getTotalNumInputChannels()));
    maxChannelsPrepared = juce::jmin(maxChannelsPrepared, 4);

    dryMixScratch.setSize(maxChannelsPrepared, samplesPerBlock > 0 ? samplesPerBlock : 512, false, false, true);

    thrillMe1.prepare(currentSampleRate, maxChannelsPrepared);
    thrillMe2.prepare(currentSampleRate, maxChannelsPrepared);
    thrillMe1.updateSpectralCoeffs(currentSampleRate, apvts.getRawParameterValue("thrill1Spec")->load());
    thrillMe2.updateSpectralCoeffs(currentSampleRate, apvts.getRawParameterValue("thrill2Spec")->load());

    for (int ch = 0; ch < maxChannelsPrepared; ++ch)
    {
        const size_t si = static_cast<size_t>(ch);
        lowShelfPerChannel[si].prepare(spec);
        mid1PeakPerChannel[si].prepare(spec);
        mid2PeakPerChannel[si].prepare(spec);
        highShelfPerChannel[si].prepare(spec);
        roastPreHighShelf[si].prepare(spec);
        roastPostHighShelf[si].prepare(spec);
        for (int p = 0; p < kAnharmMaxPartials; ++p)
            anharmPeakPerChannel[si][(size_t) p].prepare(spec);
        aprResonator[si].prepare(currentSampleRate);
        parexResonator[si].prepare(currentSampleRate);
    }

    {
        const float srF = static_cast<float>(currentSampleRate);
        constexpr float dcCornerHz = 14.0f;
        coreDcLeakCoeff = 1.0f - std::exp(-kTwoPi * dcCornerHz / juce::jmax(100.f, srF));
        // Per-block one-pole for band-param smoothing (~20 ms time constant).
        const float spb = juce::jmax(1.f, (float) samplesPerBlock);
        bandSmoothBlockCoeff = 1.f - std::exp(-spb / juce::jmax(1.f, srF * 0.020f));
        bandSmooth.initialized = false;
        shaperMixSmooth.initialized = false;
        roastPunchDecay = static_cast<float>(std::exp(-1.0 / juce::jmax(10.0, currentSampleRate * 0.009)));
        roastGlueDecay = static_cast<float>(std::exp(-1.0 / juce::jmax(10.0, currentSampleRate * 0.13)));
        roastDriveEnvCoeff = static_cast<float>(std::exp(-1.0 / juce::jmax(10.0, currentSampleRate * 0.007)));
    }
    for (size_t i = 0; i < coreDcPre.size(); ++i)
    {
        coreDcPre[i] = 0.f;
        coreDcPost[i] = 0.f;
        coreDcMid[i] = 0.f;
        coreDcLow[i] = 0.f;
        vaSvfPerChannel[i].reset();
        aprResonator[i].reset();
        parexResonator[i].reset();
        pakettiMagnetState[i].y1 = 0.f;
        pakettiMagnetState[i].env = 0.f;
        pakettiChebyDcX1[i] = 0.f;
        pakettiChebyDcY1[i] = 0.f;
        for (int p = 0; p < kAnharmMaxPartials; ++p)
            anharmPeakPerChannel[i][(size_t) p].reset();
        roastPunchEnv[i] = 0.f;
        roastGlueEnv[i] = 0.f;
        roastDriveEnv[i] = 0.f;
        roastLoFiCounter[i] = 0;
        roastLoFiHold[i] = 0.f;
    }
    coreLifePhase = 0.f;
    roastFlutterPhase = 0.f;
    roastRingPhase = 0.f;
    for (float& d : anharmSmoothedDrive)
        d = 0.f;

    juce::dsp::ProcessSpec limSpec;
    limSpec.sampleRate = currentSampleRate;
    limSpec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock > 0 ? samplesPerBlock : 512);
    limSpec.numChannels = static_cast<juce::uint32>(juce::jmax(1, maxChannelsPrepared));
    outputLimiter.prepare(limSpec);
    outputLimiter.reset();

    for (float& p : lfoPhase)
        p = 0.f;
    for (auto& p : motionLfoUiPhase)
        p.store(0.f, std::memory_order_relaxed);

    spectrumRingFill = 0;
    for (int i = 0; i < kSpectrumFftSize; ++i)
        spectrumWindow[i] = 0.5f - 0.5f * std::cos(kTwoPi * (float) i / (float) juce::jmax(1, kSpectrumFftSize - 1));
    for (int i = 0; i < kSpectrumBins; ++i)
    {
        spectrumSmoothBefore[i] = -100.f;
        spectrumSmoothAfter[i] = -100.f;
        spectrumPubBefore[i] = -100.f;
        spectrumPubAfter[i] = -100.f;
    }
    spectrumSeq.store(0, std::memory_order_relaxed);

    debugSmoothedIn = debugSmoothedOut = 0.f;
    debugInRms.store(0.f, std::memory_order_relaxed);
    debugOutRms.store(0.f, std::memory_order_relaxed);

    updateFiltersUniform(currentSampleRate);
    updateRoastShelfFilters(currentSampleRate);
    publishMotionEqUiSnapshot(0,
                              apvts.getRawParameterValue("hiCf")->load(),
                              apvts.getRawParameterValue("hiGain")->load(),
                              apvts.getRawParameterValue("mid1Cf")->load(),
                              apvts.getRawParameterValue("mid1Bw")->load(),
                              apvts.getRawParameterValue("mid1Gain")->load(),
                              apvts.getRawParameterValue("mid2Cf")->load(),
                              apvts.getRawParameterValue("mid2Bw")->load(),
                              apvts.getRawParameterValue("mid2Gain")->load(),
                              apvts.getRawParameterValue("lowCf")->load(),
                              apvts.getRawParameterValue("lowGain")->load(),
                              false);
    publishMotionEqUiSnapshot(1,
                              apvts.getRawParameterValue("hiCf")->load(),
                              apvts.getRawParameterValue("hiGain")->load(),
                              apvts.getRawParameterValue("mid1Cf")->load(),
                              apvts.getRawParameterValue("mid1Bw")->load(),
                              apvts.getRawParameterValue("mid1Gain")->load(),
                              apvts.getRawParameterValue("mid2Cf")->load(),
                              apvts.getRawParameterValue("mid2Bw")->load(),
                              apvts.getRawParameterValue("mid2Gain")->load(),
                              apvts.getRawParameterValue("lowCf")->load(),
                              apvts.getRawParameterValue("lowGain")->load(),
                              false);

    {
        const double fLo = 30.0;
        const double fHi = juce::jmin(20000.0, currentSampleRate * 0.48);
        const double logLo = std::log(fLo);
        const double logHi = std::log(fHi);
        for (int i = 0; i < kEqCurvePlotPoints; ++i)
        {
            const double t = (double) i / (double) (kEqCurvePlotPoints - 1);
            eqCurveFreqHz[(size_t) i] = std::exp(logLo + t * (logHi - logLo));
        }
    }
    lastEqCurveEvalRate = currentSampleRate;
    roastOversampler.reset();
    preparedRoastOsFactorExp = 0;
    preparedRoastOsChannels = 0;
    preparedRoastOsHostSamples = 0;
    if (reportedOsLatencySamples != 0)
    {
        reportedOsLatencySamples = 0;
        setLatencySamples(0);
    }
    publishEqCurveMagnitudeSnapshot();

    if (chebyLutBuilder != nullptr)
        chebyLutBuilder->startBuilder();
}

void ParaEQ301AudioProcessor::releaseResources()
{
    if (chebyLutBuilder != nullptr)
        chebyLutBuilder->stopBuilder();
}

void ParaEQ301AudioProcessor::publishMotionEqUiSnapshot(int channelIndex, float hiCf, float hiGainDb,
                                                         float m1f, float m1bw, float m1GainDb,
                                                         float m2f, float m2bw, float m2GainDb,
                                                         float loCf, float loGainDb,
                                                         bool engaged) noexcept
{
    if (channelIndex <= 0)
    {
        motionUiHiCf.store(hiCf, std::memory_order_relaxed);
        motionUiHiGainDb.store(hiGainDb, std::memory_order_relaxed);
        motionUiM1Cf.store(m1f, std::memory_order_relaxed);
        motionUiM1Bw.store(m1bw, std::memory_order_relaxed);
        motionUiM1GainDb.store(m1GainDb, std::memory_order_relaxed);
        motionUiM2Cf.store(m2f, std::memory_order_relaxed);
        motionUiM2Bw.store(m2bw, std::memory_order_relaxed);
        motionUiM2GainDb.store(m2GainDb, std::memory_order_relaxed);
        motionUiLoCf.store(loCf, std::memory_order_relaxed);
        motionUiLoGainDb.store(loGainDb, std::memory_order_relaxed);
        motionUiEngaged.store(engaged ? (std::uint8_t) 1 : (std::uint8_t) 0, std::memory_order_relaxed);
    }
    else
    {
        motionUiHiCfR.store(hiCf, std::memory_order_relaxed);
        motionUiHiGainDbR.store(hiGainDb, std::memory_order_relaxed);
        motionUiM1CfR.store(m1f, std::memory_order_relaxed);
        motionUiM1BwR.store(m1bw, std::memory_order_relaxed);
        motionUiM1GainDbR.store(m1GainDb, std::memory_order_relaxed);
        motionUiM2CfR.store(m2f, std::memory_order_relaxed);
        motionUiM2BwR.store(m2bw, std::memory_order_relaxed);
        motionUiM2GainDbR.store(m2GainDb, std::memory_order_relaxed);
        motionUiLoCfR.store(loCf, std::memory_order_relaxed);
        motionUiLoGainDbR.store(loGainDb, std::memory_order_relaxed);
    }
}

void ParaEQ301AudioProcessor::publishEqCurveMagnitudeSnapshot() noexcept
{
    const double sr = lastEqCurveEvalRate > 0.0 ? lastEqCurveEvalRate : currentSampleRate;
    const double nyq = sr * 0.499;

    eqCurveMagSeq.fetch_add(1u, std::memory_order_acq_rel);

    const auto* cLo = lowShelfPerChannel[0].coefficients.get();
    const auto* cM1 = mid1PeakPerChannel[0].coefficients.get();
    const auto* cM2 = mid2PeakPerChannel[0].coefficients.get();
    const auto* cHi = highShelfPerChannel[0].coefficients.get();

    for (int i = 0; i < kEqCurvePlotPoints; ++i)
    {
        double f = juce::jlimit(1.0, nyq, eqCurveFreqHz[(size_t) i]);
        double m = 1.0;
        if (cLo != nullptr)
            m *= cLo->getMagnitudeForFrequency(f, sr);
        if (cM1 != nullptr)
            m *= cM1->getMagnitudeForFrequency(f, sr);
        if (cM2 != nullptr)
            m *= cM2->getMagnitudeForFrequency(f, sr);
        if (cHi != nullptr)
            m *= cHi->getMagnitudeForFrequency(f, sr);

        m = juce::jmax(1.0e-12, m);
        eqCurveMagPublished[(size_t) i] = static_cast<float>(juce::Decibels::gainToDecibels(m));
    }

    eqCurveMagSeq.fetch_add(1u, std::memory_order_release);
}

void ParaEQ301AudioProcessor::getEqChainMagnitudeDb(double sampleRate, const double* frequenciesHz,
                                                     float* magnitudesDb, int numPoints) const noexcept
{
    // Coefficients follow the rate used in the last audio block (host rate, or oversampled rate when OS is on).
    juce::ignoreUnused(sampleRate);
    const double sr = lastEqCurveEvalRate > 0.0 ? lastEqCurveEvalRate : currentSampleRate;
    const double nyq = sr * 0.499;

    if (numPoints == kEqCurvePlotPoints)
    {
        const std::uint32_t a = eqCurveMagSeq.load(std::memory_order_acquire);
        if (a >= 2u && (a & 1u) == 0u)
        {
            float tmp[kEqCurvePlotPoints];
            std::memcpy(tmp, eqCurveMagPublished, sizeof(tmp));
            const std::uint32_t b = eqCurveMagSeq.load(std::memory_order_acquire);
            if (a == b)
            {
                std::memcpy(magnitudesDb, tmp, sizeof(tmp));
                return;
            }
        }
    }

    const auto* cLo = lowShelfPerChannel[0].coefficients.get();
    const auto* cM1 = mid1PeakPerChannel[0].coefficients.get();
    const auto* cM2 = mid2PeakPerChannel[0].coefficients.get();
    const auto* cHi = highShelfPerChannel[0].coefficients.get();

    for (int i = 0; i < numPoints; ++i)
    {
        double f = juce::jlimit(1.0, nyq, (double) frequenciesHz[i]);
        double m = 1.0;
        if (cLo != nullptr)
            m *= cLo->getMagnitudeForFrequency(f, sr);
        if (cM1 != nullptr)
            m *= cM1->getMagnitudeForFrequency(f, sr);
        if (cM2 != nullptr)
            m *= cM2->getMagnitudeForFrequency(f, sr);
        if (cHi != nullptr)
            m *= cHi->getMagnitudeForFrequency(f, sr);

        m = juce::jmax(1.0e-12, m);
        magnitudesDb[i] = static_cast<float>(juce::Decibels::gainToDecibels(m));
    }
}

void ParaEQ301AudioProcessor::getEqChainPlusAnharmLinearDb(double sampleRate, const double* frequenciesHz,
                                                         float* magnitudesDb, int numPoints) const noexcept
{
    juce::ignoreUnused(sampleRate);
    const double sr = lastEqCurveEvalRate > 0.0 ? lastEqCurveEvalRate : currentSampleRate;
    const double nyq = sr * 0.499;
    const bool bankOn = apvts.getRawParameterValue("anharmBankEnable")->load() > 0.5f;
    const float aMix = apvts.getRawParameterValue("anharmMix")->load();
    const bool svfOn = apvts.getRawParameterValue("svfEnable")->load() > 0.5f;
    const float svfMix = apvts.getRawParameterValue("svfMix")->load();
    const float svfCf = apvts.getRawParameterValue("svfCf")->load();
    const float svfQ = apvts.getRawParameterValue("svfQ")->load();
    const float svfGainLin = juce::Decibels::decibelsToGain(apvts.getRawParameterValue("svfGainDb")->load());

    const auto* cLo = lowShelfPerChannel[0].coefficients.get();
    const auto* cM1 = mid1PeakPerChannel[0].coefficients.get();
    const auto* cM2 = mid2PeakPerChannel[0].coefficients.get();
    const auto* cHi = highShelfPerChannel[0].coefficients.get();

    int nPart = 4;
    if (auto* pip = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("anharmPartials")))
        nPart = juce::jlimit(2, kAnharmMaxPartials, pip->get());

    for (int i = 0; i < numPoints; ++i)
    {
        const double f = juce::jlimit(1.0, nyq, (double) frequenciesHz[i]);
        C Zeq = iirCoeffComplexAtHz(cLo, f, sr);
        Zeq *= iirCoeffComplexAtHz(cM1, f, sr);
        Zeq *= iirCoeffComplexAtHz(cM2, f, sr);
        Zeq *= iirCoeffComplexAtHz(cHi, f, sr);

        C Z = Zeq;
        if (svfOn && svfMix > 1.0e-7f)
        {
            const C Hbp = svfLinearBandpassTransferHz((double) svfCf, (double) svfQ, sr, f);
            Z *= (C(1.0, 0.0) + (double) svfMix * (double) svfGainLin * Hbp);
        }

        if (bankOn && aMix > 1.0e-7f)
        {
            C sumHpMinus1(0.0, 0.0);
            for (int p = 0; p < nPart; ++p)
            {
                const auto* cp = anharmPeakPerChannel[0][(size_t) p].coefficients.get();
                if (cp == nullptr)
                    continue;
                sumHpMinus1 += (iirCoeffComplexAtHz(cp, f, sr) - C(1.0, 0.0));
            }
            Z *= (C(1.0, 0.0) + (double) aMix * sumHpMinus1);
        }

        const double mOut = juce::jmax(1.0e-12, std::abs(Z));
        magnitudesDb[i] = static_cast<float>(juce::Decibels::gainToDecibels(mOut));
    }
}

void ParaEQ301AudioProcessor::pushDebugMeters(float rawInRms, float rawOutRms) noexcept
{
    auto smooth = [](float prev, float raw) noexcept
    {
        if (raw > prev)
            return prev * 0.35f + raw * 0.65f;
        return prev * 0.94f + raw * 0.06f;
    };
    debugSmoothedIn = smooth(debugSmoothedIn, rawInRms);
    debugSmoothedOut = smooth(debugSmoothedOut, rawOutRms);
    debugInRms.store(debugSmoothedIn, std::memory_order_relaxed);
    debugOutRms.store(debugSmoothedOut, std::memory_order_relaxed);
}

bool ParaEQ301AudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& mainOut = layouts.getMainOutputChannelSet();
    const auto& mainIn = layouts.getMainInputChannelSet();
    if (mainOut != mainIn)
        return false;
    return !mainOut.isDisabled()
        && (mainOut == juce::AudioChannelSet::mono()
            || mainOut == juce::AudioChannelSet::stereo());
}

void ParaEQ301AudioProcessor::updateFiltersUniform(double sampleRate) noexcept
{
    const float hiCf = apvts.getRawParameterValue("hiCf")->load();
    const float hiGain = apvts.getRawParameterValue("hiGain")->load();
    const float m1f = apvts.getRawParameterValue("mid1Cf")->load();
    const float m1bw = apvts.getRawParameterValue("mid1Bw")->load();
    const float m1g = apvts.getRawParameterValue("mid1Gain")->load();
    const float m2f = apvts.getRawParameterValue("mid2Cf")->load();
    const float m2bw = apvts.getRawParameterValue("mid2Bw")->load();
    const float m2g = apvts.getRawParameterValue("mid2Gain")->load();
    const float lowCf = apvts.getRawParameterValue("lowCf")->load();
    const float lowGain = apvts.getRawParameterValue("lowGain")->load();

    for (int ch = 0; ch < maxChannelsPrepared; ++ch)
        updateFiltersForChannel(ch, sampleRate, lowCf, lowGain, m1f, m1bw, m1g, m2f, m2bw, m2g, hiCf, hiGain);
}

void ParaEQ301AudioProcessor::updateFiltersForChannel(int ch, double sampleRate,
                                                      float lowCf, float lowGainDb,
                                                      float m1f, float m1bw, float m1GainDb,
                                                      float m2f, float m2bw, float m2GainDb,
                                                      float hiCf, float hiGainDb) noexcept
{
    const size_t i = static_cast<size_t>(ch);
    const float q1 = qFromBandwidthHz(m1f, m1bw);
    const float q2 = qFromBandwidthHz(m2f, m2bw);
    // JUCE IIR shelf/peak helpers take a linear gain (1.0 = unity), not dB — passing 0 for "0 dB"
    // becomes ~0 linear and clamps to a tiny value → effective silence.
    const float lowLin = juce::Decibels::decibelsToGain(lowGainDb);
    const float m1Lin = juce::Decibels::decibelsToGain(m1GainDb);
    const float m2Lin = juce::Decibels::decibelsToGain(m2GainDb);
    const float hiLin = juce::Decibels::decibelsToGain(hiGainDb);
    const bool univBell = apvts.getRawParameterValue("univBell")->load() > 0.5f;
    *lowShelfPerChannel[i].coefficients = *Coefficients::makeLowShelf(sampleRate, lowCf, kShelfQ, lowLin);
    if (univBell)
    {
        *mid1PeakPerChannel[i].coefficients = *makeOrfanidisPeakCoefficients(sampleRate, static_cast<double>(m1f), (double) q1, (double) m1Lin);
        *mid2PeakPerChannel[i].coefficients = *makeOrfanidisPeakCoefficients(sampleRate, static_cast<double>(m2f), (double) q2, (double) m2Lin);
    }
    else
    {
        *mid1PeakPerChannel[i].coefficients = *Coefficients::makePeakFilter(sampleRate, m1f, q1, m1Lin);
        *mid2PeakPerChannel[i].coefficients = *Coefficients::makePeakFilter(sampleRate, m2f, q2, m2Lin);
    }
    *highShelfPerChannel[i].coefficients = *Coefficients::makeHighShelf(sampleRate, hiCf, kShelfQ, hiLin);
}

namespace
{
    /** Log-spaced magnitude samples; sqrt(mean(|H|^2)) approximates pink RMS vs flat reference. */
    float eqFourBandPinkRmsMagLinear(double sr,
                                     const juce::dsp::IIR::Coefficients<float>* cLo,
                                     const juce::dsp::IIR::Coefficients<float>* cM1,
                                     const juce::dsp::IIR::Coefficients<float>* cM2,
                                     const juce::dsp::IIR::Coefficients<float>* cHi) noexcept
    {
        if (cLo == nullptr || cM1 == nullptr || cM2 == nullptr || cHi == nullptr)
            return 1.f;
        const double nyq = sr * 0.499;
        const double fMin = 45.0;
        const double fMax = juce::jmin(16000.0, nyq);
        if (fMax <= fMin * 1.01)
            return 1.f;
        constexpr int N = 56;
        double sumM2 = 0.0;
        const double logA = std::log(fMin);
        const double logB = std::log(fMax);
        for (int i = 0; i < N; ++i)
        {
            const double t = (N <= 1) ? 0.5 : (double) i / (double) (N - 1);
            const double f = std::exp(logA + t * (logB - logA));
            double m = cLo->getMagnitudeForFrequency(f, sr);
            m *= cM1->getMagnitudeForFrequency(f, sr);
            m *= cM2->getMagnitudeForFrequency(f, sr);
            m *= cHi->getMagnitudeForFrequency(f, sr);
            sumM2 += m * m;
        }
        return static_cast<float>(std::sqrt(juce::jmax(1.0e-30, sumM2 / (double) N)));
    }

    /** LFO Hz when tempo-synced; `divIdx` matches APVTS `lfoHostSyncDiv` (quarter note = 1 beat). */
    float motionLfoHzFromSyncDivision(float bpm, int divIdx) noexcept
    {
        // Quarter note = 1 beat; whole note = 4 beats; one bar (4/4) = 4 beats.
        // Triplet = base * 2/3; dotted = base * 3/2. Order matches lfoSyncDivItems above.
        static constexpr float beatsPerCycle[] = {
            4.f / 64.f,             // 0:  1/64
            4.f / 32.f,             // 1:  1/32
            (4.f / 32.f) * 2.f / 3.f, // 2:  1/32 triplet
            4.f / 16.f,             // 3:  1/16
            (4.f / 16.f) * 2.f / 3.f, // 4:  1/16 triplet
            (4.f / 16.f) * 3.f / 2.f, // 5:  1/16 dot
            4.f / 8.f,              // 6:  1/8
            (4.f / 8.f) * 2.f / 3.f,  // 7:  1/8 triplet
            1.f,                    // 8:  1/4
            1.f * 2.f / 3.f,        // 9:  1/4 triplet
            2.f,                    // 10: 1/2
            4.f,                    // 11: 1 bar (4/4)
            8.f,                    // 12: 2 bars
            16.f,                   // 13: 4 bars
            32.f,                   // 14: 8 bars
            64.f,                   // 15: 16 bars
            128.f,                  // 16: 32 bars
            256.f                   // 17: 64 bars
        };
        constexpr int n = int(sizeof(beatsPerCycle) / sizeof(beatsPerCycle[0]));
        divIdx = juce::jlimit(0, n - 1, divIdx);
        const float bp = juce::jmax(0.001f, bpm);
        return bp / (60.f * beatsPerCycle[(size_t) divIdx]);
    }
}

float ParaEQ301AudioProcessor::computeEqFourBandPinkLevelCompensation(double sampleRate, int channelIndex) const noexcept
{
    const int ch = juce::jlimit(0, juce::jmax(0, maxChannelsPrepared - 1), channelIndex);
    const size_t ci = static_cast<size_t>(ch);
    const auto* cLo = lowShelfPerChannel[ci].coefficients.get();
    const auto* cM1 = mid1PeakPerChannel[ci].coefficients.get();
    const auto* cM2 = mid2PeakPerChannel[ci].coefficients.get();
    const auto* cHi = highShelfPerChannel[ci].coefficients.get();
    const float mag = eqFourBandPinkRmsMagLinear(sampleRate, cLo, cM1, cM2, cHi);
    const float inv = 1.f / juce::jmax(1.0e-12f, mag);
    return juce::jlimit(1.0e-4f, 1.0e4f, inv);
}

void ParaEQ301AudioProcessor::updateRoastShelfFilters(double sampleRate) noexcept
{
    const float preDb = apvts.getRawParameterValue("roastPreEmphDb")->load();
    const float postDb = apvts.getRawParameterValue("roastPostTiltDb")->load();
    const float preLin = juce::Decibels::decibelsToGain(preDb);
    const float postLin = juce::Decibels::decibelsToGain(postDb);
    for (int ch = 0; ch < maxChannelsPrepared; ++ch)
    {
        const size_t i = static_cast<size_t>(ch);
        *roastPreHighShelf[i].coefficients = *Coefficients::makeHighShelf(sampleRate, 3000.0, kShelfQ, preLin);
        *roastPostHighShelf[i].coefficients = *Coefficients::makeHighShelf(sampleRate, 5500.0, kShelfQ, postLin);
    }
}

void ParaEQ301AudioProcessor::updateAnharmonicBank(double sampleRate) noexcept
{
    const float f0 = apvts.getRawParameterValue("anharmFundHz")->load();
    const float B = apvts.getRawParameterValue("anharmInharmB")->load();
    int nPart = 4;
    if (auto* pip = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("anharmPartials")))
        nPart = juce::jlimit(2, kAnharmMaxPartials, pip->get());

    const float db0 = apvts.getRawParameterValue("anharmPerPartialDb")->load();
    const float qBase = apvts.getRawParameterValue("anharmQ")->load();
    const float envToQ = apvts.getRawParameterValue("anharmEnvQ")->load();

    for (int ch = 0; ch < maxChannelsPrepared; ++ch)
    {
        const size_t ci = static_cast<size_t>(ch);
        const float qMod = juce::jlimit(0.55f, 55.f, qBase / (1.f + envToQ * anharmSmoothedDrive[ci] * 7.f));

        for (int p = 0; p < nPart; ++p)
        {
            const int n = p + 1;
            double fn = stiffStringPartialHz((double) f0, n, (double) B);
            fn = juce::jlimit(1.0, sampleRate * 0.499, fn);
            const float gDb = db0 - 2.2f * (float) p;
            const float lin = juce::Decibels::decibelsToGain(gDb);
            *anharmPeakPerChannel[ci][(size_t) p].coefficients = *Coefficients::makePeakFilter(sampleRate, (float) fn, qMod, lin);
        }
    }
}

void ParaEQ301AudioProcessor::ensureRoastOversampler(int factorExp, int numCh, int numHostSamples)
{
    if (factorExp < 1 || factorExp > 2 || numCh < 1 || numHostSamples < 1)
        return;

    const bool needNew = (!roastOversampler)
        || preparedRoastOsFactorExp != factorExp
        || preparedRoastOsChannels != numCh
        || preparedRoastOsHostSamples != static_cast<size_t>(numHostSamples);

    if (needNew)
    {
        roastOversampler = std::make_unique<juce::dsp::Oversampling<float>>(
            static_cast<size_t>(numCh),
            static_cast<size_t>(factorExp),
            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
            true,
            true);
        roastOversampler->initProcessing(static_cast<size_t>(numHostSamples));
        roastOversampler->reset();
        preparedRoastOsFactorExp = factorExp;
        preparedRoastOsChannels = numCh;
        preparedRoastOsHostSamples = static_cast<size_t>(numHostSamples);
    }

    const int lat = roastOversampler != nullptr
        ? static_cast<int>(std::ceil((double) roastOversampler->getLatencyInSamples()))
        : 0;
    if (lat != reportedOsLatencySamples)
    {
        reportedOsLatencySamples = lat;
        setLatencySamples(lat);
    }
}

void ParaEQ301AudioProcessor::processRoastAndEqBlock(juce::dsp::AudioBlock<float> block, double processSampleRate, int spectrumStride) noexcept
{
    const int numCh = juce::jmin((int) block.getNumChannels(), maxChannelsPrepared);
    const int numSamps = (int) block.getNumSamples();
    if (numCh <= 0 || numSamps <= 0)
        return;

    const double sr = processSampleRate > 0.0 ? processSampleRate : 44100.0;
    const bool bypass1 = apvts.getRawParameterValue("core1Bypass")->load() > 0.5f;
    const float coreDrive = bypass1 ? 0.f : apvts.getRawParameterValue("coreSat")->load();
    const bool bypass2 = apvts.getRawParameterValue("core2Bypass")->load() > 0.5f;
    const float core2Drive = bypass2 ? 0.f : apvts.getRawParameterValue("core2Sat")->load();
    const float coreDirt = apvts.getRawParameterValue("coreDirt")->load();
    const float coreCrunch = apvts.getRawParameterValue("coreCrunch")->load();
    int roastCoreShapeIdx = 0;
    if (auto* rc = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("roastCoreShape")))
        roastCoreShapeIdx = juce::jlimit(0, 4, rc->getIndex());
    const float lifeDepth = apvts.getRawParameterValue("coreLifeDepth")->load();
    const float lifeHz = apvts.getRawParameterValue("coreLifeHz")->load();
    const float srF = static_cast<float>(sr);
    const float lifeInc = kTwoPi * lifeHz / juce::jmax(1.f, srF);
    const float lifeSwing = lifeDepth * 0.48f;

    const float roastPunchDecayProc = static_cast<float>(std::exp(-1.0 / juce::jmax(10.0, sr * 0.009)));
    const float roastGlueDecayProc = static_cast<float>(std::exp(-1.0 / juce::jmax(10.0, sr * 0.13)));
    const float roastDriveEnvCoeffProc = static_cast<float>(std::exp(-1.0 / juce::jmax(10.0, sr * 0.007)));
    const float aprEnvCoeffProc = 1.f - static_cast<float>(std::exp(-1.0 / juce::jmax(5.0, sr * 0.014)));
    const float coreDcLeakCoeffProc = 1.0f - std::exp(-kTwoPi * 14.0f / juce::jmax(100.f, srF));

    const float roastPunchAmt = apvts.getRawParameterValue("roastPunch")->load();
    const float roastGlueAmt = apvts.getRawParameterValue("roastGlue")->load();
    const float roastLoFiAmt = apvts.getRawParameterValue("roastLoFi")->load();
    const float roastRingAmt = apvts.getRawParameterValue("roastRing")->load();
    const float roastEnvDriveAmt = apvts.getRawParameterValue("roastEnvDrive")->load();
    const float roastFlutterAmt = apvts.getRawParameterValue("roastFlutter")->load();
    const float roastStereoWide = apvts.getRawParameterValue("roastStereoWide")->load();
    const float roastBoostTrackAmt = apvts.getRawParameterValue("roastBoostTrack")->load();
    const float roastMidChain = apvts.getRawParameterValue("roastMidChain")->load();
    const float roastPreDbVal = apvts.getRawParameterValue("roastPreEmphDb")->load();
    const float roastPostTiltDbVal = apvts.getRawParameterValue("roastPostTiltDb")->load();
    const bool useRoastPreShelf = roastPreDbVal > 0.02f;
    const bool useRoastPostShelf = std::abs(roastPostTiltDbVal) > 0.02f;
    const int roastLoFiDown = 1 + (int) (roastLoFiAmt * 10.0f);
    const float flutterInc = roastFlutterAmt > 1.0e-6f ? (kTwoPi * 42.f / juce::jmax(1.f, srF)) : 0.f;
    const float ringInc = kTwoPi * (1.15f + roastRingAmt * 7.5f) / juce::jmax(1.f, srF);
    const bool linearListen = apvts.getRawParameterValue("linearEqListen")->load() > 0.5f;
    const float roastLowChainAmt = apvts.getRawParameterValue("roastLowChain")->load();
    const bool svfOn = apvts.getRawParameterValue("svfEnable")->load() > 0.5f;
    const float svfMix = apvts.getRawParameterValue("svfMix")->load();
    const float svfCf = apvts.getRawParameterValue("svfCf")->load();
    const float svfQ = apvts.getRawParameterValue("svfQ")->load();
    const float svfDrive = apvts.getRawParameterValue("svfDrive")->load();
    const float svfGainLin = juce::Decibels::decibelsToGain(apvts.getRawParameterValue("svfGainDb")->load());

    const bool anharmBankOn = apvts.getRawParameterValue("anharmBankEnable")->load() > 0.5f;
    const float anharmMix = apvts.getRawParameterValue("anharmMix")->load();
    const float anharmNl = apvts.getRawParameterValue("anharmNl")->load();
    int nAnharmPartials = 4;
    if (auto* pip = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("anharmPartials")))
        nAnharmPartials = juce::jlimit(2, kAnharmMaxPartials, pip->get());

    const bool aprOn = apvts.getRawParameterValue("aprEnable")->load() > 0.5f;
    const float aprMix = apvts.getRawParameterValue("aprMix")->load();
    const float aprBaseHz = apvts.getRawParameterValue("aprBaseHz")->load();
    const float aprQ = apvts.getRawParameterValue("aprQ")->load();
    const float aprPumpHz = apvts.getRawParameterValue("aprPumpHz")->load();
    const float aprPumpDepth = apvts.getRawParameterValue("aprPumpDepth")->load();
    const float aprAutoTrack = apvts.getRawParameterValue("aprAutoTrack")->load();
    const float aprDrive = apvts.getRawParameterValue("aprDrive")->load();
    // Narrow Q bandpass passes little broadband energy; scale wet so high-Q presets stay hearable (Q=8 → ~1×).
    const float aprQmakeup = juce::jlimit(0.4f, 5.5f, std::sqrt(juce::jmax(0.5f, aprQ) * 0.125f));

    const bool parexOn = apvts.getRawParameterValue("parexEnable")->load() > 0.5f;
    const float parexMix = apvts.getRawParameterValue("parexMix")->load();
    const float parexBaseHz = apvts.getRawParameterValue("parexBaseHz")->load();
    const float parexQ = apvts.getRawParameterValue("parexQ")->load();
    const float parexDepth = apvts.getRawParameterValue("parexDepth")->load();
    const float parexDrive = apvts.getRawParameterValue("parexDrive")->load();
    int parexRatioIdx = 2;
    if (auto* c = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("parexRatio")))
        parexRatioIdx = juce::jlimit(0, 4, c->getIndex());
    static constexpr float kParexRatioTable[5] = { 0.5f, 1.f, 2.f, 3.f, 4.f };
    const float parexRatio = kParexRatioTable[(size_t) parexRatioIdx];
    int parexPumpSrc = 1;
    if (auto* c = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("parexPumpSrc")))
        parexPumpSrc = juce::jlimit(0, 1, c->getIndex());
    // Same Q-makeup philosophy as APR — narrow BP passes little broadband energy, so wet level
    // gets compensated when Q is high. Keeps the Mix knob psychoacoustically uniform.
    const float parexQmakeup = juce::jlimit(0.4f, 5.5f, std::sqrt(juce::jmax(0.5f, parexQ) * 0.125f));

    const float dHiG = apvts.getRawParameterValue("lfoHiDepthGain")->load();
    const float dHiC = apvts.getRawParameterValue("lfoHiDepthCf")->load();
    const float dM1G = apvts.getRawParameterValue("lfoM1DepthGain")->load();
    const float dM1C = apvts.getRawParameterValue("lfoM1DepthCf")->load();
    const float dM1B = apvts.getRawParameterValue("lfoM1DepthBw")->load();
    const float dM2G = apvts.getRawParameterValue("lfoM2DepthGain")->load();
    const float dM2C = apvts.getRawParameterValue("lfoM2DepthCf")->load();
    const float dM2B = apvts.getRawParameterValue("lfoM2DepthBw")->load();
    const float dLoG = apvts.getRawParameterValue("lfoLoDepthGain")->load();
    const float dLoC = apvts.getRawParameterValue("lfoLoDepthCf")->load();

    const float stereoDeg = apvts.getRawParameterValue("lfoStereoPhase")->load();
    const float stereoRad = stereoDeg * kTwoPi / 360.f;

    const bool anyLfo = (dHiG + dHiC + dM1G + dM1C + dM1B + dM2G + dM2C + dM2B + dLoG + dLoC) > 1.0e-6f;
    const bool eqPinkBalOn = apvts.getRawParameterValue("eqPinkLevelBal")->load() > 0.5f;
    std::array<float, 4> eqPinkGain { { 1.f, 1.f, 1.f, 1.f } };

    float hzHi = apvts.getRawParameterValue("lfoHiRate")->load();
    float hzM1 = apvts.getRawParameterValue("lfoM1Rate")->load();
    float hzM2 = apvts.getRawParameterValue("lfoM2Rate")->load();
    float hzLo = apvts.getRawParameterValue("lfoLoRate")->load();
    if (apvts.getRawParameterValue("lfoHostSync")->load() > 0.5f)
    {
        int divIdx = 3;
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("lfoHostSyncDiv")))
            divIdx = c->getIndex();
        double bpm = 120.0;
        bool haveBpm = false;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
                if (auto bpmOpt = pos->getBpm())
                {
                    const double bpmVal = *bpmOpt;
                    if (bpmVal > 0.1)
                    {
                        bpm = bpmVal;
                        haveBpm = true;
                    }
                }
        if (haveBpm)
            hzHi = hzM1 = hzM2 = hzLo = motionLfoHzFromSyncDivision((float) bpm, divIdx);
    }

    // Rate jitter: a slow LFO morphs the effective Motion rate between half-time and double-time.
    // Applies to the synced rate (when BPM sync is on) AND the free-running per-band Hz, so the
    // effect is audible/visible in any host - including the Standalone, which supplies no tempo.
    {
        const bool jitterOn = apvts.getRawParameterValue("lfoSyncJitterEnable")->load() > 0.5f;
        const float jitterDepth = apvts.getRawParameterValue("lfoSyncJitterDepth")->load();
        if (jitterOn && jitterDepth > 1.0e-6f)
        {
            const float jitterHz = apvts.getRawParameterValue("lfoSyncJitterRateHz")->load();
            // factor in [2^-depth, 2^+depth]; depth 1 -> 0.5x .. 2x around the current rate.
            const float lfo = std::sin(kTwoPi * static_cast<float>(syncJitterPhase));
            const float factor = std::pow(2.f, jitterDepth * lfo);
            hzHi *= factor; hzM1 *= factor; hzM2 *= factor; hzLo *= factor;
            syncJitterPhase += static_cast<double>(jitterHz) * static_cast<double>(numSamps) / sr;
            syncJitterPhase -= std::floor(syncJitterPhase);
        }
    }

    const float rHi = hzHi / static_cast<float>(sr);
    const float rM1 = hzM1 / static_cast<float>(sr);
    const float rM2 = hzM2 / static_cast<float>(sr);
    const float rLo = hzLo / static_cast<float>(sr);

    const float tgtHiCf = apvts.getRawParameterValue("hiCf")->load();
    const float tgtHiG = apvts.getRawParameterValue("hiGain")->load();
    const float tgtM1f = apvts.getRawParameterValue("mid1Cf")->load();
    const float tgtM1bw = apvts.getRawParameterValue("mid1Bw")->load();
    const float tgtM1g = apvts.getRawParameterValue("mid1Gain")->load();
    const float tgtM2f = apvts.getRawParameterValue("mid2Cf")->load();
    const float tgtM2bw = apvts.getRawParameterValue("mid2Bw")->load();
    const float tgtM2g = apvts.getRawParameterValue("mid2Gain")->load();
    const float tgtLoCf = apvts.getRawParameterValue("lowCf")->load();
    const float tgtLoG = apvts.getRawParameterValue("lowGain")->load();

    if (!bandSmooth.initialized)
    {
        bandSmooth.hiCf = tgtHiCf; bandSmooth.hiG = tgtHiG;
        bandSmooth.m1f = tgtM1f; bandSmooth.m1bw = tgtM1bw; bandSmooth.m1g = tgtM1g;
        bandSmooth.m2f = tgtM2f; bandSmooth.m2bw = tgtM2bw; bandSmooth.m2g = tgtM2g;
        bandSmooth.lowCf = tgtLoCf; bandSmooth.lowG = tgtLoG;
        bandSmooth.initialized = true;
    }
    const float bsc = bandSmoothBlockCoeff;
    bandSmooth.hiCf += (tgtHiCf - bandSmooth.hiCf) * bsc;
    bandSmooth.hiG  += (tgtHiG  - bandSmooth.hiG)  * bsc;
    bandSmooth.m1f  += (tgtM1f  - bandSmooth.m1f)  * bsc;
    bandSmooth.m1bw += (tgtM1bw - bandSmooth.m1bw) * bsc;
    bandSmooth.m1g  += (tgtM1g  - bandSmooth.m1g)  * bsc;
    bandSmooth.m2f  += (tgtM2f  - bandSmooth.m2f)  * bsc;
    bandSmooth.m2bw += (tgtM2bw - bandSmooth.m2bw) * bsc;
    bandSmooth.m2g  += (tgtM2g  - bandSmooth.m2g)  * bsc;
    bandSmooth.lowCf += (tgtLoCf - bandSmooth.lowCf) * bsc;
    bandSmooth.lowG  += (tgtLoG  - bandSmooth.lowG)  * bsc;

    const float baseHiCf = bandSmooth.hiCf;
    const float baseHiG  = bandSmooth.hiG;
    const float baseM1f  = bandSmooth.m1f;
    const float baseM1bw = bandSmooth.m1bw;
    const float baseM1g  = bandSmooth.m1g;
    const float baseM2f  = bandSmooth.m2f;
    const float baseM2bw = bandSmooth.m2bw;
    const float baseM2g  = bandSmooth.m2g;
    const float baseLoCf = bandSmooth.lowCf;
    const float baseLoG  = bandSmooth.lowG;

    if (!anyLfo)
        updateFiltersUniform(sr);
    updateRoastShelfFilters(sr);

    if (svfOn && !linearListen)
    {
        for (int ch = 0; ch < maxChannelsPrepared; ++ch)
            vaSvfPerChannel[static_cast<size_t>(ch)].setCoeffs(svfCf, svfQ, sr);
    }

    std::array<float, 4> anharmBlkPeak {};
    if (anharmBankOn && !linearListen)
        updateAnharmonicBank(sr);

    const int coeffChannels = juce::jmax(1, numCh);
    const int specStride = juce::jmax(1, spectrumStride);

    if (eqPinkBalOn)
    {
        for (int ch = 0; ch < coeffChannels; ++ch)
            eqPinkGain[(size_t) ch] = computeEqFourBandPinkLevelCompensation(sr, ch);
    }

    const float thrill1Spec = apvts.getRawParameterValue("thrill1Spec")->load();
    const float thrill1ThreshDb = apvts.getRawParameterValue("thrill1ThreshDb")->load();
    const float thrill1Ratio = apvts.getRawParameterValue("thrill1Ratio")->load();
    const float thrill2Spec = apvts.getRawParameterValue("thrill2Spec")->load();
    const float thrill2ThreshDb = apvts.getRawParameterValue("thrill2ThreshDb")->load();
    const float thrill2Ratio = apvts.getRawParameterValue("thrill2Ratio")->load();
    thrillMe1.updateSpectralCoeffs(sr, thrill1Spec);
    thrillMe2.updateSpectralCoeffs(sr, thrill2Spec);

    int shaperModeIdx = 0;
    if (auto* sm = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("shaperMode")))
        shaperModeIdx = juce::jlimit(0, 2, sm->getIndex());
    const float tgtShaperMix = apvts.getRawParameterValue("shaperMix")->load();
    const float tgtShaperPreGain = apvts.getRawParameterValue("shaperPreGain")->load();
    const float tgtShaperPostTrim = apvts.getRawParameterValue("shaperPostTrim")->load();
    if (!shaperMixSmooth.initialized)
    {
        shaperMixSmooth.mix = tgtShaperMix;
        shaperMixSmooth.preGain = tgtShaperPreGain;
        shaperMixSmooth.postTrim = tgtShaperPostTrim;
        shaperMixSmooth.initialized = true;
    }
    shaperMixSmooth.mix      += (tgtShaperMix      - shaperMixSmooth.mix)      * bandSmoothBlockCoeff;
    shaperMixSmooth.preGain  += (tgtShaperPreGain  - shaperMixSmooth.preGain)  * bandSmoothBlockCoeff;
    shaperMixSmooth.postTrim += (tgtShaperPostTrim - shaperMixSmooth.postTrim) * bandSmoothBlockCoeff;
    const float shaperMix = shaperMixSmooth.mix;
    const float shaperPreGain = shaperMixSmooth.preGain;
    const float shaperPostTrim = shaperMixSmooth.postTrim;
    const float magDrive = apvts.getRawParameterValue("magDrive")->load();
    const float magTilt = apvts.getRawParameterValue("magTilt")->load();
    const float magBias = apvts.getRawParameterValue("magBias")->load();
    const float magTiltLimit = apvts.getRawParameterValue("magTiltLimit")->load();
    const float magFeedback = apvts.getRawParameterValue("magFeedback")->load();
    const float magOut = apvts.getRawParameterValue("magOut")->load();
    const int magShape = (int) apvts.getRawParameterValue("magShape")->load();
    const float magEnergy = apvts.getRawParameterValue("magEnergy")->load();
    const float magEnergyMs = apvts.getRawParameterValue("magEnergyMs")->load();
    const float magEnergyCoeff = paketti::magnetEnergyCoeffFromMs(magEnergyMs, (float) currentSampleRate);
    const float chebyYL = apvts.getRawParameterValue("chebyYL")->load();
    const float chebyYC = apvts.getRawParameterValue("chebyYC")->load();
    const float chebyYR = apvts.getRawParameterValue("chebyYR")->load();
    const float chebyHarmMacro = apvts.getRawParameterValue("chebyHarmMacro")->load();
    const float chebyPolyPow = apvts.getRawParameterValue("chebyPolyPow")->load();
    static const char* kChebyHarmPowPid[12] = {
        "chebyH2Pow", "chebyH3Pow", "chebyH4Pow", "chebyH5Pow", "chebyH6Pow", "chebyH7Pow",
        "chebyH8Pow", "chebyH9Pow", "chebyH10Pow", "chebyH11Pow", "chebyH12Pow", "chebyH13Pow"
    };
    float chebyPolyPow12[12];
    for (int hi = 0; hi < 12; ++hi)
        chebyPolyPow12[(size_t) hi] = apvts.getRawParameterValue(kChebyHarmPowPid[hi])->load();
    float harm12[12];
    static const char* kChebyHarmPid[12] = {
        "chebyH2", "chebyH3", "chebyH4", "chebyH5", "chebyH6", "chebyH7",
        "chebyH8", "chebyH9", "chebyH10", "chebyH11", "chebyH12", "chebyH13"
    };
    for (int hi = 0; hi < 12; ++hi)
        harm12[(size_t) hi] = apvts.getRawParameterValue(kChebyHarmPid[hi])->load();
    float harmScaled[12];
    for (int hi = 0; hi < 12; ++hi)
        harmScaled[(size_t) hi] = harm12[(size_t) hi] * chebyHarmMacro;
    const int chebyMaxN = paketti::chebyMaxActiveN(harmScaled);
    const bool chebyHarmonicsOn = chebyMaxN > 1;
    const bool chebyCurveDc = paketti::chebyCurveHasDc(chebyYL, chebyYC, chebyYR);
    // Cheby DC blocker: any even-order harmonic (T2/T4/.../T12) introduces DC, as do the curve or per-poly pow. Run always in cheby mode.
    const bool chebyNeedsDcBlock = (shaperModeIdx == 2);
    juce::ignoreUnused(chebyCurveDc);
    const bool shaperEngaged = (!linearListen && shaperModeIdx > 0 && shaperMix > 1.0e-7f
                                 && ((shaperModeIdx == 1) || (shaperModeIdx == 2 && chebyHarmonicsOn)));

    const float* chebyLutRead = nullptr;
    if (shaperEngaged && shaperModeIdx == 2 && chebyLutBuilder != nullptr)
    {
        const std::uint32_t nh = paketti::hashChebyParams(chebyYL, chebyYC, chebyYR, harm12, chebyHarmMacro, chebyPolyPow, chebyPolyPow12);
        chebyLutRead = chebyLutBuilder->resolveLut(nh, chebyYL, chebyYC, chebyYR, harm12, chebyHarmMacro, chebyPolyPow, chebyPolyPow12, chebyLutSyncScratch);
    }

    for (int n = 0; n < numSamps; ++n)
    {
        if (anyLfo)
        {
            lfoPhase[0] += rHi;
            lfoPhase[1] += rM1;
            lfoPhase[2] += rM2;
            lfoPhase[3] += rLo;
            for (float& ph : lfoPhase)
                if (ph >= 1.f)
                    ph -= std::floor(ph);

            const bool updateCoeffs = (n % kCoeffUpdateInterval) == 0;
            if (updateCoeffs)
            {
                for (int ch = 0; ch < coeffChannels; ++ch)
                {
                    const float phaseShift = (coeffChannels > 1 && ch == 1) ? stereoRad : 0.f;
                    const float sHi = std::sin(kTwoPi * lfoPhase[0] + phaseShift);
                    const float sM1 = std::sin(kTwoPi * lfoPhase[1] + phaseShift);
                    const float sM2 = std::sin(kTwoPi * lfoPhase[2] + phaseShift);
                    const float sLo = std::sin(kTwoPi * lfoPhase[3] + phaseShift);

                    const float hiCf = modCfHz(baseHiCf, 500.f, 18000.f, sHi, dHiC);
                    const float hiGain = modGainDb(baseHiG, sHi, dHiG);
                    const float m1f = modCfHz(baseM1f, 20.f, 18000.f, sM1, dM1C);
                    const float m1bw = modBwHz(baseM1bw, 85.f, 2000.f, sM1, dM1B);
                    const float m1g = modGainDb(baseM1g, sM1, dM1G);
                    const float m2f = modCfHz(baseM2f, 20.f, 18000.f, sM2, dM2C);
                    const float m2bw = modBwHz(baseM2bw, 85.f, 2000.f, sM2, dM2B);
                    const float m2g = modGainDb(baseM2g, sM2, dM2G);
                    const float loCf = modCfHz(baseLoCf, 20.f, 10000.f, sLo, dLoC);
                    const float loGain = modGainDb(baseLoG, sLo, dLoG);

                    updateFiltersForChannel(ch, sr, loCf, loGain, m1f, m1bw, m1g, m2f, m2bw, m2g, hiCf, hiGain);
                    if (eqPinkBalOn)
                        eqPinkGain[(size_t) ch] = computeEqFourBandPinkLevelCompensation(sr, ch);
                    publishMotionEqUiSnapshot(ch, hiCf, hiGain, m1f, m1bw, m1g, m2f, m2bw, m2g, loCf, loGain, true);
                    if (coeffChannels < 2 && ch == 0)
                        publishMotionEqUiSnapshot(1, hiCf, hiGain, m1f, m1bw, m1g, m2f, m2bw, m2g, loCf, loGain, true);
                }
            }
        }

        if (linearListen)
        {
            for (int ch = 0; ch < numCh; ++ch)
            {
                float* data = block.getChannelPointer((size_t) ch);
                float x = data[n];
                const float beforeEq = x;
                const size_t i = static_cast<size_t>(ch);
                x = lowShelfPerChannel[i].processSample(x);
                x = mid1PeakPerChannel[i].processSample(x);
                x = mid2PeakPerChannel[i].processSample(x);
                x = highShelfPerChannel[i].processSample(x);
                if (eqPinkBalOn)
                    x *= eqPinkGain[i];
                // Linear-EQ path: APR still runs last (after EQ + pink), same as full path ordering.
                if (aprOn && aprMix > 1.0e-7f)
                {
                    const float bp = aprResonator[i].process(x, aprBaseHz, aprQ, aprPumpHz, aprPumpDepth, aprAutoTrack, aprDrive, aprEnvCoeffProc);
                    x += aprMix * (bp * aprQmakeup);
                }
                if (parexOn && parexMix > 1.0e-7f)
                {
                    const float bp = parexResonator[i].process(x, parexBaseHz, parexQ, parexRatio,
                                                                parexDepth, parexDrive, parexPumpSrc, aprEnvCoeffProc);
                    x += parexMix * (bp * parexQmakeup);
                }
                if (!std::isfinite(x))
                    x = 0.0f;
                data[n] = x;
                if (ch == 0 && (n % specStride) == 0)
                    spectrumPushPrePostEq(beforeEq, x);
            }
            continue;
        }

        float sumBoostDb = 0.f;
        if (motionUiEngaged.load(std::memory_order_relaxed) != 0)
        {
            const float hg = motionUiHiGainDb.load(std::memory_order_relaxed);
            const float g1 = motionUiM1GainDb.load(std::memory_order_relaxed);
            const float g2 = motionUiM2GainDb.load(std::memory_order_relaxed);
            const float lg = motionUiLoGainDb.load(std::memory_order_relaxed);
            if (hg > 0.f) sumBoostDb += hg;
            if (g1 > 0.f) sumBoostDb += g1;
            if (g2 > 0.f) sumBoostDb += g2;
            if (lg > 0.f) sumBoostDb += lg;
        }
        else
        {
            if (baseHiG > 0.f) sumBoostDb += baseHiG;
            if (baseM1g > 0.f) sumBoostDb += baseM1g;
            if (baseM2g > 0.f) sumBoostDb += baseM2g;
            if (baseLoG > 0.f) sumBoostDb += baseLoG;
        }
        const float trackMul = 1.f + roastBoostTrackAmt * juce::jmin(1.f, sumBoostDb / 26.f) * 0.62f;

        coreLifePhase += lifeInc;
        if (coreLifePhase >= kTwoPi)
            coreLifePhase -= kTwoPi * std::floor(coreLifePhase / kTwoPi);

        roastFlutterPhase += flutterInc;
        if (roastFlutterPhase >= kTwoPi)
            roastFlutterPhase -= kTwoPi * std::floor(roastFlutterPhase / kTwoPi);

        roastRingPhase += ringInc;
        if (roastRingPhase >= kTwoPi)
            roastRingPhase -= kTwoPi * std::floor(roastRingPhase / kTwoPi);

        const float flutterMul = 1.f + roastFlutterAmt * 0.055f * std::sin(roastFlutterPhase);

        for (int ch = 0; ch < numCh; ++ch)
        {
            float* data = block.getChannelPointer((size_t) ch);
            float x = data[n];
            const size_t i = static_cast<size_t>(ch);

            x = applyRoastPunch(x, roastPunchAmt, roastPunchEnv[i], roastPunchDecayProc);
            if (useRoastPreShelf)
                x = roastPreHighShelf[i].processSample(x);

            const float xPreSat = x;

            const float stereoWideRad = roastStereoWide * 1.55f * ((numCh > 1 && ch == 1) ? 1.f : 0.f);
            const float phLife = coreLifePhase + stereoWideRad;
            const float mPre = 1.0f + lifeSwing * std::sin(phLife);
            const float mPost = 1.0f + lifeSwing * std::sin(phLife + 2.17f);

            const float envMul = 1.f + roastEnvDriveAmt * juce::jmin(2.2f, roastDriveEnv[i] * 5.0f);
            float effPre = bypass1 ? 0.0f : juce::jlimit(0.0f, 1.0f, coreDrive * mPre * trackMul * flutterMul * envMul);
            float effPost = bypass2 ? 0.0f : juce::jlimit(0.0f, 1.0f, core2Drive * mPost * trackMul * flutterMul * envMul);

            {
                const float wet1 = thrillMe1.processChannel(ch, x, thrill1ThreshDb, thrill1Ratio);
                x = (1.f - effPre) * x + effPre * wet1;
            }
            if (shaperEngaged)
            {
                const float u = juce::jlimit(-1.f, 1.f, x * shaperPreGain);
                float y = u;
                if (shaperModeIdx == 1)
                {
                    const float osF = (float) juce::jmax(1, specStride);
                    const float slewStep = paketti::magnetSlewStepFromLimit(magTiltLimit, osF);
                    y = paketti::magnetProcessSample(u, magDrive, magTilt, magBias, magFeedback, magOut,
                                                     slewStep, magShape, magEnergy, magEnergyCoeff,
                                                     pakettiMagnetState[i]);
                }
                else if (shaperModeIdx == 2 && chebyLutRead != nullptr)
                {
                    y = paketti::chebyLutEval(chebyLutRead, paketti::kChebyLutPoints, u);
                }
                float delta = (y - u) * shaperPostTrim;
                if (shaperModeIdx == 2 && chebyNeedsDcBlock)
                    delta = paketti::dcBlockIir(delta, pakettiChebyDcX1[i], pakettiChebyDcY1[i]);
                x += shaperMix * delta;
            }
            // EQ-tab Dirt/Crunch: always audible when ThrillMe 1 is in the mix (Roast Low/Mid taps are optional extra).
            const float dirtThrillBus = juce::jlimit(0.f, 0.55f, effPre * (0.42f * coreDirt + 0.28f * coreCrunch));
            if (dirtThrillBus > 1.0e-7f)
                x = applyCoreSaturation(x, dirtThrillBus, coreDirt, coreCrunch, coreDcPre[i], coreDcLeakCoeffProc, roastCoreShapeIdx);

            const float beforeEq = x;
            x = lowShelfPerChannel[i].processSample(x);
            if (roastLowChainAmt > 1.0e-6f)
            {
                // Drive from Roast Low % and Life (mPre); do not gate on ThrillMe wet — effPre/effPost can be 0 while
                // these taps should still apply Dirt / core crunch between EQ bands.
                const float effLow = juce::jlimit(0.0f, 1.0f, roastLowChainAmt * mPre);
                x = applyCoreSaturation(x, effLow, coreDirt, coreCrunch, coreDcLow[i], coreDcLeakCoeffProc, roastCoreShapeIdx);
            }
            x = mid1PeakPerChannel[i].processSample(x);
            if (roastMidChain > 1.0e-6f)
            {
                const float effMid = juce::jlimit(0.0f, 1.0f, roastMidChain * mPost);
                x = applyCoreSaturation(x, effMid, coreDirt, coreCrunch, coreDcMid[i], coreDcLeakCoeffProc, roastCoreShapeIdx);
            }
            x = mid2PeakPerChannel[i].processSample(x);
            x = highShelfPerChannel[i].processSample(x);
            if (svfOn && !linearListen && svfMix > 1.0e-7f)
            {
                const float bp = vaSvfPerChannel[i].processBandpassNonlinear(x, svfDrive);
                x = x + svfMix * (svfGainLin * bp);
            }
            if (anharmBankOn && anharmMix > 1.0e-7f)
            {
                anharmBlkPeak[i] = juce::jmax(anharmBlkPeak[i], std::abs(x));
                float delta = 0.f;
                for (int p = 0; p < nAnharmPartials; ++p)
                {
                    const float yp = anharmPeakPerChannel[i][(size_t) p].processSample(x);
                    delta += (yp - x);
                }
                float wet = anharmMix * delta;
                if (anharmNl > 1.0e-6f)
                {
                    const float t = 1.f + anharmNl * 5.f;
                    wet = std::tanh(t * wet) / t;
                }
                x += wet;
            }
            {
                const float wet2 = thrillMe2.processChannel(ch, x, thrill2ThreshDb, thrill2Ratio);
                x = (1.f - effPost) * x + effPost * wet2;
            }
            if (useRoastPostShelf)
                x = roastPostHighShelf[i].processSample(x);
            x = applyRoastLoFi(x, roastLoFiAmt, roastLoFiCounter[i], roastLoFiHold[i], roastLoFiDown);
            x = applyRoastGlue(x, roastGlueAmt, roastGlueEnv[i], roastGlueDecayProc);
            x *= 1.f + roastRingAmt * 0.38f * std::sin(roastRingPhase + (float) ch * 0.7f);
            if (eqPinkBalOn)
                x *= eqPinkGain[i];
            // APR last in the roast chain (after ring / pink), before output — parallel resonator "cherry on top".
            if (aprOn && aprMix > 1.0e-7f)
            {
                const float bp = aprResonator[i].process(x, aprBaseHz, aprQ, aprPumpHz, aprPumpDepth, aprAutoTrack, aprDrive, aprEnvCoeffProc);
                x += aprMix * (bp * aprQmakeup);
            }
            // Parametric Excitation: bandpass with cutoff pumped at ratio*f0 (Mathieu). Sits in parallel with APR.
            if (parexOn && parexMix > 1.0e-7f)
            {
                const float bp = parexResonator[i].process(x, parexBaseHz, parexQ, parexRatio,
                                                            parexDepth, parexDrive, parexPumpSrc, aprEnvCoeffProc);
                x += parexMix * (bp * parexQmakeup);
            }
            if (!std::isfinite(x))
                x = 0.0f;
            data[n] = x;
            if (ch == 0 && (n % specStride) == 0)
                spectrumPushPrePostEq(beforeEq, x);

            const float ax = std::abs(xPreSat);
            roastDriveEnv[i] = roastDriveEnv[i] * roastDriveEnvCoeffProc + ax * (1.f - roastDriveEnvCoeffProc);
            roastDriveEnv[i] = juce::jmin(1.f, roastDriveEnv[i]);
        }
    }

    if (!linearListen)
    {
        for (size_t ci = 0; ci < anharmSmoothedDrive.size(); ++ci)
        {
            const float pk = anharmBankOn ? anharmBlkPeak[ci] : 0.f;
            anharmSmoothedDrive[ci] = anharmSmoothedDrive[ci] * roastDriveEnvCoeffProc + pk * (1.f - roastDriveEnvCoeffProc);
            anharmSmoothedDrive[ci] = juce::jmin(1.f, anharmSmoothedDrive[ci]);
        }
    }

    if (!anyLfo)
    {
        publishMotionEqUiSnapshot(0, baseHiCf, baseHiG, baseM1f, baseM1bw, baseM1g,
                                  baseM2f, baseM2bw, baseM2g, baseLoCf, baseLoG, false);
        publishMotionEqUiSnapshot(1, baseHiCf, baseHiG, baseM1f, baseM1bw, baseM1g,
                                  baseM2f, baseM2bw, baseM2g, baseLoCf, baseLoG, false);
    }

    const float trimLin = juce::Decibels::decibelsToGain(apvts.getRawParameterValue("roastOutputTrimDb")->load());
    if (std::abs(trimLin - 1.f) > 1.0e-5f)
    {
        for (int ch = 0; ch < numCh; ++ch)
            juce::FloatVectorOperations::multiply(block.getChannelPointer((size_t) ch), trimLin, numSamps);
    }

    lastEqCurveEvalRate = sr;
    publishEqCurveMagnitudeSnapshot();
}

void ParaEQ301AudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused(midi);

    const double srHost = getSampleRate() > 0.0 ? getSampleRate() : currentSampleRate;
    const int numCh = juce::jmin(buffer.getNumChannels(), maxChannelsPrepared);
    const int numSamps = buffer.getNumSamples();

    const bool testToneOn = apvts.getRawParameterValue("testToneOn")->load() > 0.5f;
    if (testToneOn && numCh > 0 && numSamps > 0)
    {
        const float testHz = apvts.getRawParameterValue("testToneHz")->load();
        const float testAmp = juce::Decibels::decibelsToGain(apvts.getRawParameterValue("testToneDb")->load());
        const double phaseInc = juce::MathConstants<double>::twoPi
                                * (double) juce::jmax(0.01f, testHz) / juce::jmax(1.0, srHost);
        for (int n = 0; n < numSamps; ++n)
        {
            const float s = testAmp * (float) std::sin(testTonePhase);
            for (int ch = 0; ch < numCh; ++ch)
                buffer.getWritePointer(ch)[n] = s;
            testTonePhase += phaseInc;
            if (testTonePhase >= juce::MathConstants<double>::twoPi)
                testTonePhase -= juce::MathConstants<double>::twoPi;
        }
    }

    if (numCh > 0 && numSamps > 0
        && dryMixScratch.getNumChannels() >= numCh && dryMixScratch.getNumSamples() >= numSamps)
    {
        for (int ch = 0; ch < numCh; ++ch)
            dryMixScratch.copyFrom(ch, 0, buffer, ch, 0, numSamps);

        // Capture INPUT into scope ring (post test-tone substitution, pre-chain).
        scopeInSeq.fetch_add(1, std::memory_order_acq_rel);
        const float* srcIn = buffer.getReadPointer(0);
        int posIn = scopeRingInPos;
        for (int n = 0; n < numSamps; ++n)
        {
            scopeRingIn[posIn] = srcIn[n];
            posIn = (posIn + 1) & (kScopeRingSize - 1);
        }
        scopeRingInPos = posIn;
        scopeInSeq.fetch_add(1, std::memory_order_acq_rel);
    }

    const float inRms = blockRms(buffer, numCh, numSamps);

    int osIdx = 0;
    if (auto* p = apvts.getParameter("oversample"))
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*>(p))
            osIdx = c->getIndex();

    const bool linearEq = apvts.getRawParameterValue("linearEqListen")->load() > 0.5f;
    const int osExp = (linearEq || osIdx <= 0) ? 0 : osIdx;

    juce::dsp::AudioBlock<float> fullBlock(buffer);
    auto work = fullBlock.getSubsetChannelBlock(0, static_cast<size_t>(numCh));

    if (osExp > 0 && numSamps > 0)
    {
        ensureRoastOversampler(osExp, numCh, numSamps);
        if (roastOversampler != nullptr)
        {
            auto osAudio = roastOversampler->processSamplesUp(work);
            const double srOs = srHost * (double) roastOversampler->getOversamplingFactor();
            processRoastAndEqBlock(osAudio, srOs, (int) roastOversampler->getOversamplingFactor());
            roastOversampler->processSamplesDown(work);
        }
        else
        {
            processRoastAndEqBlock(work, srHost, 1);
        }
    }
    else
    {
        if (roastOversampler != nullptr)
        {
            roastOversampler.reset();
            preparedRoastOsFactorExp = 0;
            preparedRoastOsChannels = 0;
            preparedRoastOsHostSamples = 0;
        }
        if (reportedOsLatencySamples != 0)
        {
            reportedOsLatencySamples = 0;
            setLatencySamples(0);
        }
        processRoastAndEqBlock(work, srHost, 1);
    }

    const bool limOn = apvts.getRawParameterValue("outLimOn")->load() > 0.5f;
    if (limOn)
    {
        const float limThreshDb = apvts.getRawParameterValue("outLimThresh")->load();
        outputLimiter.setThreshold(limThreshDb);
        outputLimiter.setRelease(apvts.getRawParameterValue("outLimRelease")->load());
        auto chBlock = fullBlock.getSubsetChannelBlock(0, static_cast<size_t>(numCh));
        auto region = chBlock.getSubBlock(0, static_cast<size_t>(numSamps));
        juce::dsp::ProcessContextReplacing<float> ctx(region);
        outputLimiter.process(ctx);
        const float makeup = juceLimiterFixedMakeupLinear(limThreshDb);
        const float compensate = 1.0f / juce::jmax(1.0e-12f, makeup);
        for (int ch = 0; ch < numCh; ++ch)
            juce::FloatVectorOperations::multiply(buffer.getWritePointer(ch), compensate, numSamps);
    }

    for (int ch = 0; ch < numCh; ++ch)
    {
        float* data = buffer.getWritePointer(ch);
        for (int n = 0; n < numSamps; ++n)
        {
            if (!std::isfinite(data[n]))
                data[n] = 0.f;
        }
    }

    const float mixWet = apvts.getRawParameterValue("masterDryWet")->load();
    if (mixWet < 0.9995f && numCh > 0 && numSamps > 0
        && dryMixScratch.getNumChannels() >= numCh && dryMixScratch.getNumSamples() >= numSamps)
    {
        const float w = juce::jlimit(0.f, 1.f, mixWet);
        const float dryG = 1.f - w;
        for (int ch = 0; ch < numCh; ++ch)
        {
            float* wet = buffer.getWritePointer(ch);
            const float* dry = dryMixScratch.getReadPointer(ch);
            for (int n = 0; n < numSamps; ++n)
                wet[n] = dryG * dry[n] + w * wet[n];
        }
    }

    const float outRms = blockRms(buffer, numCh, numSamps);
    pushDebugMeters(inRms, outRms);

    if (numCh > 0 && numSamps > 0)
    {
        scopeSeq.fetch_add(1, std::memory_order_acq_rel);
        const float* src = buffer.getReadPointer(0);
        int pos = scopeRingPos;
        for (int n = 0; n < numSamps; ++n)
        {
            scopeRing[pos] = src[n];
            pos = (pos + 1) & (kScopeRingSize - 1);
        }
        scopeRingPos = pos;
        scopeSeq.fetch_add(1, std::memory_order_acq_rel);
    }

    for (int i = 0; i < 4; ++i)
        motionLfoUiPhase[(size_t) i].store(lfoPhase[(size_t) i], std::memory_order_relaxed);
}

double ParaEQ301AudioProcessor::getScopeSamples(float* outSamples, int numSamples) const noexcept
{
    if (outSamples == nullptr || numSamples <= 0)
        return currentSampleRate;
    numSamples = juce::jmin(numSamples, kScopeRingSize);
    // Seqlock-style read: retry if writer is in the middle of an update.
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const auto s0 = scopeSeq.load(std::memory_order_acquire);
        if ((s0 & 1u) != 0u)
            continue;
        const int pos = scopeRingPos;
        const int start = (pos - numSamples + kScopeRingSize) & (kScopeRingSize - 1);
        for (int i = 0; i < numSamples; ++i)
            outSamples[i] = scopeRing[(start + i) & (kScopeRingSize - 1)];
        const auto s1 = scopeSeq.load(std::memory_order_acquire);
        if (s0 == s1)
            return currentSampleRate;
    }
    return currentSampleRate;
}

double ParaEQ301AudioProcessor::getScopeInputSamples(float* outSamples, int numSamples) const noexcept
{
    if (outSamples == nullptr || numSamples <= 0)
        return currentSampleRate;
    numSamples = juce::jmin(numSamples, kScopeRingSize);
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const auto s0 = scopeInSeq.load(std::memory_order_acquire);
        if ((s0 & 1u) != 0u)
            continue;
        const int pos = scopeRingInPos;
        const int start = (pos - numSamples + kScopeRingSize) & (kScopeRingSize - 1);
        for (int i = 0; i < numSamples; ++i)
            outSamples[i] = scopeRingIn[(start + i) & (kScopeRingSize - 1)];
        const auto s1 = scopeInSeq.load(std::memory_order_acquire);
        if (s0 == s1)
            return currentSampleRate;
    }
    return currentSampleRate;
}

void ParaEQ301AudioProcessor::getMotionLfoPhases(float* outFour) const noexcept
{
    for (int i = 0; i < 4; ++i)
        outFour[(size_t) i] = motionLfoUiPhase[(size_t) i].load(std::memory_order_relaxed);
}

void ParaEQ301AudioProcessor::getMotionEffectiveEqSnapshot(MotionEffectiveEqSnapshot& s) const noexcept
{
    s.hiCfHz = motionUiHiCf.load(std::memory_order_relaxed);
    s.hiGainDb = motionUiHiGainDb.load(std::memory_order_relaxed);
    s.mid1CfHz = motionUiM1Cf.load(std::memory_order_relaxed);
    s.mid1BwHz = motionUiM1Bw.load(std::memory_order_relaxed);
    s.mid1GainDb = motionUiM1GainDb.load(std::memory_order_relaxed);
    s.mid2CfHz = motionUiM2Cf.load(std::memory_order_relaxed);
    s.mid2BwHz = motionUiM2Bw.load(std::memory_order_relaxed);
    s.mid2GainDb = motionUiM2GainDb.load(std::memory_order_relaxed);
    s.loCfHz = motionUiLoCf.load(std::memory_order_relaxed);
    s.loGainDb = motionUiLoGainDb.load(std::memory_order_relaxed);
    s.hiCfHzR = motionUiHiCfR.load(std::memory_order_relaxed);
    s.hiGainDbR = motionUiHiGainDbR.load(std::memory_order_relaxed);
    s.mid1CfHzR = motionUiM1CfR.load(std::memory_order_relaxed);
    s.mid1BwHzR = motionUiM1BwR.load(std::memory_order_relaxed);
    s.mid1GainDbR = motionUiM1GainDbR.load(std::memory_order_relaxed);
    s.mid2CfHzR = motionUiM2CfR.load(std::memory_order_relaxed);
    s.mid2BwHzR = motionUiM2BwR.load(std::memory_order_relaxed);
    s.mid2GainDbR = motionUiM2GainDbR.load(std::memory_order_relaxed);
    s.loCfHzR = motionUiLoCfR.load(std::memory_order_relaxed);
    s.loGainDbR = motionUiLoGainDbR.load(std::memory_order_relaxed);
    s.motionEngaged = motionUiEngaged.load(std::memory_order_relaxed) != 0;
}

void ParaEQ301AudioProcessor::spectrumPushPrePostEq(float beforeEq, float afterEq) noexcept
{
    if (spectrumRingFill < kSpectrumFftSize)
    {
        spectrumRingBefore[spectrumRingFill] = beforeEq;
        spectrumRingAfter[spectrumRingFill] = afterEq;
        ++spectrumRingFill;
        if (spectrumRingFill < kSpectrumFftSize)
            return;
    }

    const auto runOne = [this](const float* ring, float* smoothState, float* publish)
    {
        for (int i = 0; i < kSpectrumFftSize; ++i)
            spectrumFftScratch[i] = ring[i] * spectrumWindow[i];
        std::memset(spectrumFftScratch + kSpectrumFftSize, 0, sizeof(float) * (size_t) kSpectrumFftSize);
        spectrumFft.performFrequencyOnlyForwardTransform(spectrumFftScratch, true);
        for (int i = 0; i < kSpectrumBins; ++i)
        {
            const float m = spectrumFftScratch[i];
            const float db = 20.f * std::log10(juce::jmax(m, 1.0e-14f));
            smoothState[i] = 0.9f * smoothState[i] + 0.1f * db;
            publish[i] = smoothState[i];
        }
    };

    spectrumSeq.fetch_add(1, std::memory_order_acq_rel);
    runOne(spectrumRingBefore, spectrumSmoothBefore, spectrumPubBefore);
    runOne(spectrumRingAfter, spectrumSmoothAfter, spectrumPubAfter);
    spectrumSeq.fetch_add(1, std::memory_order_release);

    spectrumRingFill = 0;
}

bool ParaEQ301AudioProcessor::getSpectrumBeforeAfterDb(double sampleRate, const double* frequenciesHz, int numPoints,
                                                        float* outBeforeDb, float* outAfterDb) const noexcept
{
    const uint32_t a = spectrumSeq.load(std::memory_order_acquire);
    if ((a & 1u) != 0)
        return false;

    float lb[kSpectrumBins];
    float la[kSpectrumBins];
    std::memcpy(lb, spectrumPubBefore, sizeof(lb));
    std::memcpy(la, spectrumPubAfter, sizeof(la));

    const uint32_t b = spectrumSeq.load(std::memory_order_acquire);
    if (a != b)
        return false;

    const double srUse = sampleRate > 0.0 ? sampleRate : currentSampleRate;
    const double nyq = srUse * 0.499;
    const double binHz = srUse / (double) kSpectrumFftSize;

    for (int i = 0; i < numPoints; ++i)
    {
        double f = juce::jlimit(1.0, nyq, frequenciesHz[i]);
        const float binF = (float) (f / binHz);
        int i0 = (int) std::floor((double) binF);
        const float frac = binF - (float) i0;
        if (i0 < 0)
            i0 = 0;
        if (i0 >= kSpectrumBins - 1)
        {
            outBeforeDb[i] = lb[kSpectrumBins - 1];
            outAfterDb[i] = la[kSpectrumBins - 1];
        }
        else
        {
            outBeforeDb[i] = (1.f - frac) * lb[i0] + frac * lb[i0 + 1];
            outAfterDb[i] = (1.f - frac) * la[i0] + frac * la[i0 + 1];
        }
    }
    return true;
}

namespace
{
    void apvtsSetFloatPlain(juce::AudioProcessorValueTreeState& ap, const char* id, float plainValue)
    {
        if (auto* p = ap.getParameter(id))
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p))
                p->setValueNotifyingHost(rp->convertTo0to1(plainValue));
    }

    void apvtsSetBool01(juce::AudioProcessorValueTreeState& ap, const char* id, bool v)
    {
        if (auto* p = ap.getParameter(id))
            p->setValueNotifyingHost(v ? 1.f : 0.f);
    }

    void apvtsSetIntPlain(juce::AudioProcessorValueTreeState& ap, const char* id, int v)
    {
        if (auto* p = dynamic_cast<juce::AudioParameterInt*>(ap.getParameter(id)))
            p->setValueNotifyingHost(p->convertTo0to1(v));
    }

    void apvtsSetChoiceIndex(juce::AudioProcessorValueTreeState& ap, const char* id, int index)
    {
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*>(ap.getParameter(id)))
            if (auto* p = ap.getParameter(id))
                p->setValueNotifyingHost(c->convertTo0to1(index));
    }
}

void ParaEQ301AudioProcessor::applyFactoryPreset(int index)
{
    index = juce::jlimit(0, kNumFactoryPrograms - 1, index);
    auto& ap = apvts;

    auto zeroRoast = [&]()
    {
        apvtsSetFloatPlain(ap, "coreCrunch", 0.f);
        apvtsSetChoiceIndex(ap, "roastCoreShape", 0);
        apvtsSetFloatPlain(ap, "roastPreEmphDb", 0.f);
        apvtsSetFloatPlain(ap, "roastPostTiltDb", 0.f);
        apvtsSetFloatPlain(ap, "roastBoostTrack", 0.f);
        apvtsSetFloatPlain(ap, "roastMidChain", 0.f);
        apvtsSetFloatPlain(ap, "roastLowChain", 0.f);
        apvtsSetFloatPlain(ap, "roastPunch", 0.f);
        apvtsSetFloatPlain(ap, "roastGlue", 0.f);
        apvtsSetFloatPlain(ap, "roastLoFi", 0.f);
        apvtsSetFloatPlain(ap, "roastRing", 0.f);
        apvtsSetFloatPlain(ap, "roastEnvDrive", 0.f);
        apvtsSetFloatPlain(ap, "roastFlutter", 0.f);
        apvtsSetFloatPlain(ap, "roastStereoWide", 0.f);
        apvtsSetFloatPlain(ap, "roastOutputTrimDb", 0.f);
        apvtsSetFloatPlain(ap, "coreDirt", 0.28f);
        apvtsSetFloatPlain(ap, "coreLifeDepth", 0.f);
        apvtsSetFloatPlain(ap, "coreLifeHz", 0.22f);
        apvtsSetBool01(ap, "svfEnable", false);
        apvtsSetFloatPlain(ap, "svfMix", 0.f);
        apvtsSetFloatPlain(ap, "svfCf", 950.f);
        apvtsSetFloatPlain(ap, "svfQ", 4.5f);
        apvtsSetFloatPlain(ap, "svfDrive", 0.22f);
        apvtsSetFloatPlain(ap, "svfGainDb", 0.f);
        apvtsSetBool01(ap, "univBell", false);
        apvtsSetBool01(ap, "anharmBankEnable", false);
        apvtsSetFloatPlain(ap, "anharmMix", 0.f);
        apvtsSetFloatPlain(ap, "anharmNl", 0.f);
        apvtsSetFloatPlain(ap, "anharmEnvQ", 0.35f);
        apvtsSetBool01(ap, "aprEnable", false);
        apvtsSetFloatPlain(ap, "aprMix", 0.f);
        apvtsSetChoiceIndex(ap, "shaperMode", 0);
        apvtsSetFloatPlain(ap, "shaperMix", 0.f);
        apvtsSetFloatPlain(ap, "shaperPreGain", 3.f);
        apvtsSetFloatPlain(ap, "shaperPostTrim", 1.f);
        apvtsSetFloatPlain(ap, "magDrive", 1.f);
        apvtsSetFloatPlain(ap, "magTilt", 0.f);
        apvtsSetFloatPlain(ap, "magBias", 0.f);
        apvtsSetFloatPlain(ap, "magTiltLimit", 0.5f);
        apvtsSetFloatPlain(ap, "magFeedback", 0.f);
        apvtsSetFloatPlain(ap, "magOut", 1.f);
        apvtsSetFloatPlain(ap, "magShape", 0.f);
        apvtsSetFloatPlain(ap, "magEnergy", 0.f);
        apvtsSetFloatPlain(ap, "magEnergyMs", 80.f);
        apvtsSetFloatPlain(ap, "chebyYL", -1.f);
        apvtsSetFloatPlain(ap, "chebyYC", 0.f);
        apvtsSetFloatPlain(ap, "chebyYR", 1.f);
        apvtsSetFloatPlain(ap, "chebyHarmMacro", 1.f);
        apvtsSetFloatPlain(ap, "chebyPolyPow", 1.f);
        static const char* zChebyH[12] = {
            "chebyH2", "chebyH3", "chebyH4", "chebyH5", "chebyH6", "chebyH7",
            "chebyH8", "chebyH9", "chebyH10", "chebyH11", "chebyH12", "chebyH13"
        };
        for (const char* hid : zChebyH)
            apvtsSetFloatPlain(ap, hid, 0.f);
        static const char* zChebyHPow[12] = {
            "chebyH2Pow", "chebyH3Pow", "chebyH4Pow", "chebyH5Pow", "chebyH6Pow", "chebyH7Pow",
            "chebyH8Pow", "chebyH9Pow", "chebyH10Pow", "chebyH11Pow", "chebyH12Pow", "chebyH13Pow"
        };
        for (const char* hid : zChebyHPow)
            apvtsSetFloatPlain(ap, hid, 1.f);
        apvtsSetFloatPlain(ap, "thrill1Spec", kThrillSpecDefault);
        apvtsSetFloatPlain(ap, "thrill1ThreshDb", thrillThresholdDefaultDb());
        apvtsSetFloatPlain(ap, "thrill1Ratio", thrillRatioDefaultStored());
        apvtsSetFloatPlain(ap, "thrill2Spec", kThrillSpecDefault);
        apvtsSetFloatPlain(ap, "thrill2ThreshDb", thrillThresholdDefaultDb());
        apvtsSetFloatPlain(ap, "thrill2Ratio", thrillRatioDefaultStored());
    };

    auto flatEq = [&]()
    {
        apvtsSetFloatPlain(ap, "hiCf", 5000.f);
        apvtsSetFloatPlain(ap, "hiGain", 0.f);
        apvtsSetFloatPlain(ap, "mid1Cf", 1000.f);
        apvtsSetFloatPlain(ap, "mid1Bw", 400.f);
        apvtsSetFloatPlain(ap, "mid1Gain", 0.f);
        apvtsSetFloatPlain(ap, "mid2Cf", 2500.f);
        apvtsSetFloatPlain(ap, "mid2Bw", 400.f);
        apvtsSetFloatPlain(ap, "mid2Gain", 0.f);
        apvtsSetFloatPlain(ap, "lowCf", 120.f);
        apvtsSetFloatPlain(ap, "lowGain", 0.f);
    };

    auto motionOff = [&]()
    {
        apvtsSetBool01(ap, "lfoHostSync", false);
        apvtsSetFloatPlain(ap, "lfoHiDepthGain", 0.f);
        apvtsSetFloatPlain(ap, "lfoHiDepthCf", 0.f);
        apvtsSetFloatPlain(ap, "lfoM1DepthGain", 0.f);
        apvtsSetFloatPlain(ap, "lfoM1DepthCf", 0.f);
        apvtsSetFloatPlain(ap, "lfoM1DepthBw", 0.f);
        apvtsSetFloatPlain(ap, "lfoM2DepthGain", 0.f);
        apvtsSetFloatPlain(ap, "lfoM2DepthCf", 0.f);
        apvtsSetFloatPlain(ap, "lfoM2DepthBw", 0.f);
        apvtsSetFloatPlain(ap, "lfoLoDepthGain", 0.f);
        apvtsSetFloatPlain(ap, "lfoLoDepthCf", 0.f);
    };

    auto setApr = [&](bool on, float mix, float baseHz, float q, float pumpHz, float pumpDepth, float autoTrack, float drive)
    {
        apvtsSetBool01(ap, "aprEnable", on);
        apvtsSetFloatPlain(ap, "aprMix", mix);
        apvtsSetFloatPlain(ap, "aprBaseHz", baseHz);
        apvtsSetFloatPlain(ap, "aprQ", q);
        apvtsSetFloatPlain(ap, "aprPumpHz", pumpHz);
        apvtsSetFloatPlain(ap, "aprPumpDepth", pumpDepth);
        apvtsSetFloatPlain(ap, "aprAutoTrack", autoTrack);
        apvtsSetFloatPlain(ap, "aprDrive", drive);
    };

    switch (index)
    {
        case 0:
            apvtsSetBool01(ap, "linearEqListen", false);
            apvtsSetBool01(ap, "core1Bypass", true);
            apvtsSetBool01(ap, "core2Bypass", true);
            apvtsSetFloatPlain(ap, "coreSat", 1.f);
            apvtsSetFloatPlain(ap, "core2Sat", 1.f);
            apvtsSetFloatPlain(ap, "thrill1ThreshDb", 0.f);
            apvtsSetFloatPlain(ap, "thrill2ThreshDb", 0.f);
            apvtsSetFloatPlain(ap, "thrill1Ratio", 128.f);
            apvtsSetFloatPlain(ap, "thrill2Ratio", 128.f);
            zeroRoast();
            flatEq();
            motionOff();
            break;
        case 1:
            apvtsSetBool01(ap, "linearEqListen", false);
            apvtsSetBool01(ap, "core1Bypass", false);
            apvtsSetBool01(ap, "core2Bypass", false);
            apvtsSetFloatPlain(ap, "coreSat", 0.48f);
            apvtsSetFloatPlain(ap, "core2Sat", 0.38f);
            apvtsSetFloatPlain(ap, "coreCrunch", 0.24f);
            apvtsSetFloatPlain(ap, "coreDirt", 0.42f);
            apvtsSetFloatPlain(ap, "roastPreEmphDb", 4.5f);
            apvtsSetFloatPlain(ap, "roastPostTiltDb", -1.5f);
            apvtsSetFloatPlain(ap, "roastBoostTrack", 0.45f);
            apvtsSetFloatPlain(ap, "roastMidChain", 0.22f);
            apvtsSetFloatPlain(ap, "roastLowChain", 0.12f);
            apvtsSetFloatPlain(ap, "roastPunch", 0.22f);
            apvtsSetFloatPlain(ap, "roastGlue", 0.18f);
            apvtsSetFloatPlain(ap, "roastLoFi", 0.f);
            apvtsSetFloatPlain(ap, "roastRing", 0.08f);
            apvtsSetFloatPlain(ap, "roastEnvDrive", 0.25f);
            apvtsSetFloatPlain(ap, "roastFlutter", 0.12f);
            apvtsSetFloatPlain(ap, "roastStereoWide", 0.35f);
            apvtsSetFloatPlain(ap, "roastOutputTrimDb", -1.5f);
            apvtsSetBool01(ap, "svfEnable", true);
            apvtsSetFloatPlain(ap, "svfMix", 0.18f);
            apvtsSetFloatPlain(ap, "svfCf", 1100.f);
            apvtsSetFloatPlain(ap, "svfQ", 7.f);
            apvtsSetFloatPlain(ap, "svfDrive", 0.28f);
            apvtsSetFloatPlain(ap, "svfGainDb", 2.5f);
            flatEq();
            motionOff();
            break;
        case 2:
            apvtsSetBool01(ap, "linearEqListen", false);
            apvtsSetBool01(ap, "core1Bypass", false);
            apvtsSetBool01(ap, "core2Bypass", false);
            apvtsSetFloatPlain(ap, "coreSat", 0.28f);
            apvtsSetFloatPlain(ap, "core2Sat", 0.5f);
            apvtsSetFloatPlain(ap, "coreCrunch", 0.15f);
            apvtsSetFloatPlain(ap, "roastLoFi", 0.62f);
            apvtsSetFloatPlain(ap, "roastGlue", 0.35f);
            apvtsSetFloatPlain(ap, "roastRing", 0.18f);
            apvtsSetFloatPlain(ap, "roastPostTiltDb", 2.f);
            apvtsSetFloatPlain(ap, "roastBoostTrack", 0.f);
            apvtsSetFloatPlain(ap, "roastMidChain", 0.f);
            apvtsSetFloatPlain(ap, "roastLowChain", 0.f);
            apvtsSetFloatPlain(ap, "roastPunch", 0.f);
            apvtsSetFloatPlain(ap, "roastPreEmphDb", 2.f);
            apvtsSetFloatPlain(ap, "roastEnvDrive", 0.f);
            apvtsSetFloatPlain(ap, "roastFlutter", 0.f);
            apvtsSetFloatPlain(ap, "roastStereoWide", 0.f);
            apvtsSetFloatPlain(ap, "roastOutputTrimDb", -2.f);
            apvtsSetBool01(ap, "svfEnable", false);
            apvtsSetFloatPlain(ap, "svfMix", 0.f);
            flatEq();
            motionOff();
            break;
        case 3:
            apvtsSetBool01(ap, "linearEqListen", false);
            apvtsSetBool01(ap, "core1Bypass", false);
            apvtsSetBool01(ap, "core2Bypass", false);
            zeroRoast();
            apvtsSetFloatPlain(ap, "coreSat", 0.32f);
            apvtsSetFloatPlain(ap, "core2Sat", 0.28f);
            apvtsSetFloatPlain(ap, "lfoHiRate", 0.35f);
            apvtsSetFloatPlain(ap, "lfoM1Rate", 0.28f);
            apvtsSetFloatPlain(ap, "lfoHiDepthGain", 0.55f);
            apvtsSetFloatPlain(ap, "lfoM1DepthCf", 0.45f);
            apvtsSetFloatPlain(ap, "lfoM2DepthGain", 0.35f);
            apvtsSetFloatPlain(ap, "roastBoostTrack", 0.5f);
            apvtsSetFloatPlain(ap, "coreLifeDepth", 0.35f);
            apvtsSetFloatPlain(ap, "coreLifeHz", 0.18f);
            apvtsSetBool01(ap, "svfEnable", false);
            apvtsSetFloatPlain(ap, "svfMix", 0.f);
            flatEq();
            break;
        case 4:
            apvtsSetBool01(ap, "linearEqListen", true);
            apvtsSetBool01(ap, "svfEnable", false);
            apvtsSetFloatPlain(ap, "svfMix", 0.f);
            zeroRoast();
            apvtsSetBool01(ap, "core1Bypass", true);
            apvtsSetBool01(ap, "core2Bypass", false);
            apvtsSetFloatPlain(ap, "coreSat", 0.f);
            apvtsSetFloatPlain(ap, "core2Sat", 0.f);
            flatEq();
            motionOff();
            break;
        case 5:
            apvtsSetBool01(ap, "linearEqListen", false);
            apvtsSetBool01(ap, "core1Bypass", false);
            apvtsSetBool01(ap, "core2Bypass", false);
            zeroRoast();
            apvtsSetFloatPlain(ap, "coreDirt", 0.42f);
            apvtsSetFloatPlain(ap, "coreSat", 0.38f);
            apvtsSetFloatPlain(ap, "core2Sat", 0.32f);
            apvtsSetFloatPlain(ap, "roastMidChain", 0.48f);
            apvtsSetFloatPlain(ap, "roastLowChain", 0.28f);
            apvtsSetFloatPlain(ap, "roastBoostTrack", 0.55f);
            apvtsSetFloatPlain(ap, "roastPunch", 0.12f);
            apvtsSetFloatPlain(ap, "mid1Gain", 6.5f);
            apvtsSetFloatPlain(ap, "mid2Gain", 4.5f);
            apvtsSetFloatPlain(ap, "mid1Cf", 900.f);
            apvtsSetFloatPlain(ap, "mid2Cf", 2800.f);
            apvtsSetFloatPlain(ap, "hiGain", 1.5f);
            apvtsSetFloatPlain(ap, "lowGain", 0.f);
            apvtsSetBool01(ap, "svfEnable", false);
            apvtsSetFloatPlain(ap, "svfMix", 0.f);
            motionOff();
            break;
        case 6:
            apvtsSetBool01(ap, "linearEqListen", false);
            apvtsSetBool01(ap, "core1Bypass", false);
            apvtsSetBool01(ap, "core2Bypass", false);
            zeroRoast();
            apvtsSetFloatPlain(ap, "coreSat", 0.12f);
            apvtsSetFloatPlain(ap, "core2Sat", 0.f);
            apvtsSetBool01(ap, "univBell", true);
            apvtsSetBool01(ap, "anharmBankEnable", false);
            apvtsSetFloatPlain(ap, "anharmMix", 0.f);
            flatEq();
            apvtsSetFloatPlain(ap, "mid1Cf", 1200.f);
            apvtsSetFloatPlain(ap, "mid1Bw", 320.f);
            apvtsSetFloatPlain(ap, "mid1Gain", 5.5f);
            apvtsSetFloatPlain(ap, "mid2Cf", 3200.f);
            apvtsSetFloatPlain(ap, "mid2Bw", 500.f);
            apvtsSetFloatPlain(ap, "mid2Gain", 2.5f);
            apvtsSetFloatPlain(ap, "lowGain", 1.f);
            apvtsSetFloatPlain(ap, "hiGain", 0.5f);
            motionOff();
            break;
        case 7:
            apvtsSetBool01(ap, "linearEqListen", false);
            apvtsSetBool01(ap, "core1Bypass", false);
            apvtsSetBool01(ap, "core2Bypass", false);
            zeroRoast();
            apvtsSetFloatPlain(ap, "coreSat", 0.22f);
            apvtsSetFloatPlain(ap, "core2Sat", 0.18f);
            apvtsSetBool01(ap, "univBell", false);
            apvtsSetBool01(ap, "anharmBankEnable", true);
            apvtsSetFloatPlain(ap, "anharmFundHz", 240.f);
            apvtsSetFloatPlain(ap, "anharmInharmB", 0.00012f);
            apvtsSetIntPlain(ap, "anharmPartials", 5);
            apvtsSetFloatPlain(ap, "anharmMix", 0.42f);
            apvtsSetFloatPlain(ap, "anharmPerPartialDb", -1.5f);
            apvtsSetFloatPlain(ap, "anharmQ", 22.f);
            apvtsSetFloatPlain(ap, "anharmNl", 0.14f);
            apvtsSetFloatPlain(ap, "anharmEnvQ", 0.45f);
            flatEq();
            apvtsSetFloatPlain(ap, "mid1Gain", 1.5f);
            apvtsSetFloatPlain(ap, "hiGain", -0.5f);
            apvtsSetBool01(ap, "svfEnable", false);
            apvtsSetFloatPlain(ap, "svfMix", 0.f);
            motionOff();
            break;
        case 8:
            apvtsSetBool01(ap, "linearEqListen", false);
            apvtsSetBool01(ap, "core1Bypass", false);
            apvtsSetBool01(ap, "core2Bypass", false);
            zeroRoast();
            apvtsSetFloatPlain(ap, "coreSat", 0.2f);
            apvtsSetFloatPlain(ap, "core2Sat", 0.15f);
            apvtsSetBool01(ap, "univBell", true);
            apvtsSetBool01(ap, "anharmBankEnable", true);
            apvtsSetFloatPlain(ap, "anharmFundHz", 180.f);
            apvtsSetFloatPlain(ap, "anharmInharmB", 0.00035f);
            apvtsSetIntPlain(ap, "anharmPartials", 6);
            apvtsSetFloatPlain(ap, "anharmMix", 0.38f);
            apvtsSetFloatPlain(ap, "anharmPerPartialDb", 0.5f);
            apvtsSetFloatPlain(ap, "anharmQ", 18.f);
            apvtsSetFloatPlain(ap, "anharmNl", 0.22f);
            apvtsSetFloatPlain(ap, "anharmEnvQ", 0.5f);
            flatEq();
            apvtsSetFloatPlain(ap, "mid1Cf", 700.f);
            apvtsSetFloatPlain(ap, "mid1Bw", 280.f);
            apvtsSetFloatPlain(ap, "mid1Gain", 3.f);
            apvtsSetFloatPlain(ap, "mid2Cf", 2400.f);
            apvtsSetFloatPlain(ap, "mid2Gain", 2.f);
            apvtsSetFloatPlain(ap, "lowGain", 2.f);
            apvtsSetBool01(ap, "svfEnable", false);
            apvtsSetFloatPlain(ap, "svfMix", 0.f);
            motionOff();
            break;
        case 9:
            apvtsSetBool01(ap, "linearEqListen", false);
            apvtsSetBool01(ap, "core1Bypass", false);
            apvtsSetBool01(ap, "core2Bypass", false);
            zeroRoast();
            apvtsSetFloatPlain(ap, "coreSat", 0.26f);
            apvtsSetFloatPlain(ap, "core2Sat", 0.2f);
            apvtsSetBool01(ap, "univBell", false);
            apvtsSetBool01(ap, "anharmBankEnable", true);
            apvtsSetFloatPlain(ap, "anharmFundHz", 320.f);
            apvtsSetFloatPlain(ap, "anharmInharmB", 0.0002f);
            apvtsSetIntPlain(ap, "anharmPartials", 4);
            apvtsSetFloatPlain(ap, "anharmMix", 0.35f);
            apvtsSetFloatPlain(ap, "anharmPerPartialDb", -0.5f);
            apvtsSetFloatPlain(ap, "anharmQ", 26.f);
            apvtsSetFloatPlain(ap, "anharmNl", 0.18f);
            apvtsSetFloatPlain(ap, "anharmEnvQ", 0.4f);
            flatEq();
            apvtsSetFloatPlain(ap, "mid1Gain", 2.f);
            apvtsSetBool01(ap, "svfEnable", true);
            apvtsSetFloatPlain(ap, "svfMix", 0.22f);
            apvtsSetFloatPlain(ap, "svfCf", 1250.f);
            apvtsSetFloatPlain(ap, "svfQ", 6.5f);
            apvtsSetFloatPlain(ap, "svfDrive", 0.26f);
            apvtsSetFloatPlain(ap, "svfGainDb", 3.f);
            motionOff();
            break;
        case 10:
            apvtsSetBool01(ap, "linearEqListen", false);
            apvtsSetBool01(ap, "core1Bypass", false);
            apvtsSetBool01(ap, "core2Bypass", false);
            zeroRoast();
            apvtsSetFloatPlain(ap, "coreSat", 0.08f);
            apvtsSetFloatPlain(ap, "core2Sat", 0.06f);
            flatEq();
            apvtsSetFloatPlain(ap, "mid1Cf", 1200.f);
            apvtsSetFloatPlain(ap, "mid1Bw", 350.f);
            apvtsSetFloatPlain(ap, "mid1Gain", 2.2f);
            apvtsSetFloatPlain(ap, "hiGain", 1.2f);
            motionOff();
            setApr(true, 0.24f, 2100.f, 11.f, 0.42f, 0.26f, 0.62f, 0.18f);
            break;
        case 11:
            apvtsSetBool01(ap, "linearEqListen", false);
            apvtsSetBool01(ap, "core1Bypass", false);
            apvtsSetBool01(ap, "core2Bypass", false);
            zeroRoast();
            apvtsSetFloatPlain(ap, "coreSat", 0.18f);
            apvtsSetFloatPlain(ap, "core2Sat", 0.14f);
            flatEq();
            apvtsSetFloatPlain(ap, "lowGain", 1.8f);
            apvtsSetFloatPlain(ap, "mid1Cf", 650.f);
            apvtsSetFloatPlain(ap, "mid1Bw", 500.f);
            apvtsSetFloatPlain(ap, "mid1Gain", 1.5f);
            apvtsSetFloatPlain(ap, "hiGain", -0.8f);
            motionOff();
            setApr(true, 0.22f, 480.f, 9.f, 1.35f, 0.32f, 0.52f, 0.3f);
            break;
        case 12:
            apvtsSetBool01(ap, "linearEqListen", false);
            apvtsSetBool01(ap, "core1Bypass", false);
            apvtsSetBool01(ap, "core2Bypass", false);
            zeroRoast();
            apvtsSetFloatPlain(ap, "coreSat", 0.05f);
            apvtsSetFloatPlain(ap, "core2Sat", 0.04f);
            flatEq();
            apvtsSetFloatPlain(ap, "hiGain", -1.2f);
            apvtsSetFloatPlain(ap, "mid2Gain", 1.f);
            apvtsSetFloatPlain(ap, "mid2Cf", 900.f);
            motionOff();
            setApr(true, 0.32f, 280.f, 14.f, 0.2f, 0.5f, 0.34f, 0.14f);
            break;
        case 13:
            apvtsSetBool01(ap, "linearEqListen", false);
            apvtsSetBool01(ap, "core1Bypass", false);
            apvtsSetBool01(ap, "core2Bypass", false);
            zeroRoast();
            apvtsSetFloatPlain(ap, "coreSat", 0.12f);
            apvtsSetFloatPlain(ap, "core2Sat", 0.1f);
            flatEq();
            apvtsSetFloatPlain(ap, "lowCf", 95.f);
            apvtsSetFloatPlain(ap, "lowGain", 1.2f);
            apvtsSetFloatPlain(ap, "hiGain", -1.5f);
            motionOff();
            setApr(true, 0.16f, 105.f, 6.5f, 0.38f, 0.2f, 0.46f, 0.28f);
            break;
        case 14:
            apvtsSetBool01(ap, "linearEqListen", false);
            apvtsSetBool01(ap, "core1Bypass", false);
            apvtsSetBool01(ap, "core2Bypass", false);
            zeroRoast();
            apvtsSetFloatPlain(ap, "coreSat", 0.1f);
            apvtsSetFloatPlain(ap, "core2Sat", 0.22f);
            flatEq();
            apvtsSetFloatPlain(ap, "mid1Cf", 1400.f);
            apvtsSetFloatPlain(ap, "mid1Bw", 600.f);
            apvtsSetFloatPlain(ap, "mid1Gain", -1.f);
            motionOff();
            setApr(true, 0.38f, 1650.f, 22.f, 5.5f, 0.58f, 0.22f, 0.48f);
            break;
        default:
            break;
    }
}

void ParaEQ301AudioProcessor::setCurrentProgram(int index)
{
    const int n = juce::jlimit(0, kNumFactoryPrograms - 1, index);
    if (n == currentFactoryProgram)
        return;
    currentFactoryProgram = n;
    applyFactoryPreset(n);
}

const juce::String ParaEQ301AudioProcessor::getProgramName(int index)
{
    switch (juce::jlimit(0, kNumFactoryPrograms - 1, index))
    {
        case 0: return "Init";
        case 1: return "Roast forward";
        case 2: return "Lo-fi crush";
        case 3: return "Motion bake";
        case 4: return "Linear EQ only";
        case 5: return "Mids cooked";
        case 6: return "Univ bell lift";
        case 7: return "Anharm shell";
        case 8: return "Bell + partials";
        case 9: return "Anharm + SVF";
        case 10: return "APR vox";
        case 11: return "APR drums";
        case 12: return "APR pads";
        case 13: return "APR bass";
        case 14: return "APR foley";
        default: return {};
    }
}

void ParaEQ301AudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    const auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    if (xml != nullptr && xml->getIntAttribute("thrillRatioSemanticRev", 0) < 2)
        xml->setAttribute("thrillRatioSemanticRev", 2);
    copyXmlToBinary(*xml, destData);
}

void ParaEQ301AudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName(apvts.state.getType()))
    {
        migrateLegacyCoreBypassXml(*xmlState);
        migrateThrillRatioSemanticsXml(*xmlState);
        apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
    }
}

juce::AudioProcessorEditor* ParaEQ301AudioProcessor::createEditor()
{
    return new ParaEQ301AudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ParaEQ301AudioProcessor();
}
