#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

namespace
{
    constexpr float kShelfQ = 0.707f;
    constexpr float kTwoPi = juce::MathConstants<float>::twoPi;
    constexpr int kCoeffUpdateInterval = 4;

    float applyCoreSaturation(float x, float drive01) noexcept
    {
        if (drive01 <= 1.0e-8f)
            return x;
        x = juce::jlimit(-8.0f, 8.0f, x);
        const float push = 1.0f + drive01 * 22.0f;
        const float wet = std::tanh(push * x);
        return x * (1.0f - drive01) + wet * drive01;
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
}

juce::AudioProcessorValueTreeState::ParameterLayout ParaEQ301AudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "hiCf", "Hi Cf",
        freqRangeSkewed(500.0f, 18000.0f, 3000.0f),
        5000.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "hiGain", "Hi Gain",
        juce::NormalisableRange<float>(-30.0f, 30.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "mid1Cf", "Mid1 Cf",
        freqRangeSkewed(20.0f, 18000.0f, 1000.0f),
        1000.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "mid1Bw", "Mid1 Bw",
        freqRangeSkewed(85.0f, 2000.0f, 400.0f),
        400.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "mid1Gain", "Mid1 Gain",
        juce::NormalisableRange<float>(-30.0f, 30.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "mid2Cf", "Mid2 Cf",
        freqRangeSkewed(20.0f, 18000.0f, 1000.0f),
        2500.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "mid2Bw", "Mid2 Bw",
        freqRangeSkewed(85.0f, 2000.0f, 400.0f),
        400.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "mid2Gain", "Mid2 Gain",
        juce::NormalisableRange<float>(-30.0f, 30.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "lowCf", "Low Cf",
        freqRangeSkewed(20.0f, 10000.0f, 200.0f),
        120.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "lowGain", "Low Gain",
        juce::NormalisableRange<float>(-30.0f, 30.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    layout.add(std::make_unique<juce::AudioParameterBool>("coreOn", "Core color", true));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "coreSat", "Core sat",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "lfoStereoPhase", "LFO L/R phase",
        juce::NormalisableRange<float>(0.0f, 180.0f, 1.0f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("deg")));

    auto lfoRateRange = juce::NormalisableRange<float>(0.02f, 14.0f, 0.01f, 0.35f);

    layout.add(std::make_unique<juce::AudioParameterFloat>("lfoHiRate", "Hi LFO Hz", lfoRateRange, 2.0f, juce::AudioParameterFloatAttributes().withLabel("Hz")));
    const juce::NormalisableRange<float> lfoDepthRange(0.f, 1.f, 0.01f);

    layout.add(std::make_unique<juce::AudioParameterFloat>("lfoHiDepthGain", "Hi LFO gain", lfoDepthRange, 0.f, juce::AudioParameterFloatAttributes()));
    layout.add(std::make_unique<juce::AudioParameterFloat>("lfoHiDepthCf", "Hi LFO cf", lfoDepthRange, 0.f, juce::AudioParameterFloatAttributes()));

    layout.add(std::make_unique<juce::AudioParameterFloat>("lfoM1Rate", "M1 LFO Hz", lfoRateRange, 2.0f, juce::AudioParameterFloatAttributes().withLabel("Hz")));
    layout.add(std::make_unique<juce::AudioParameterFloat>("lfoM1DepthGain", "M1 LFO gain", lfoDepthRange, 0.f, juce::AudioParameterFloatAttributes()));
    layout.add(std::make_unique<juce::AudioParameterFloat>("lfoM1DepthCf", "M1 LFO cf", lfoDepthRange, 0.f, juce::AudioParameterFloatAttributes()));
    layout.add(std::make_unique<juce::AudioParameterFloat>("lfoM1DepthBw", "M1 LFO bw", lfoDepthRange, 0.f, juce::AudioParameterFloatAttributes()));

    layout.add(std::make_unique<juce::AudioParameterFloat>("lfoM2Rate", "M2 LFO Hz", lfoRateRange, 2.0f, juce::AudioParameterFloatAttributes().withLabel("Hz")));
    layout.add(std::make_unique<juce::AudioParameterFloat>("lfoM2DepthGain", "M2 LFO gain", lfoDepthRange, 0.f, juce::AudioParameterFloatAttributes()));
    layout.add(std::make_unique<juce::AudioParameterFloat>("lfoM2DepthCf", "M2 LFO cf", lfoDepthRange, 0.f, juce::AudioParameterFloatAttributes()));
    layout.add(std::make_unique<juce::AudioParameterFloat>("lfoM2DepthBw", "M2 LFO bw", lfoDepthRange, 0.f, juce::AudioParameterFloatAttributes()));

    layout.add(std::make_unique<juce::AudioParameterFloat>("lfoLoRate", "Lo LFO Hz", lfoRateRange, 2.0f, juce::AudioParameterFloatAttributes().withLabel("Hz")));
    layout.add(std::make_unique<juce::AudioParameterFloat>("lfoLoDepthGain", "Lo LFO gain", lfoDepthRange, 0.f, juce::AudioParameterFloatAttributes()));
    layout.add(std::make_unique<juce::AudioParameterFloat>("lfoLoDepthCf", "Lo LFO cf", lfoDepthRange, 0.f, juce::AudioParameterFloatAttributes()));

    layout.add(std::make_unique<juce::AudioParameterBool>("outLimOn", "Output limiter", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "outLimThresh", "Lim threshold",
        juce::NormalisableRange<float>(-16.0f, -0.3f, 0.1f),
        -2.5f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "outLimRelease", "Lim release",
        juce::NormalisableRange<float>(20.0f, 400.0f, 1.0f, 0.35f),
        90.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));

    return layout;
}

ParaEQ301AudioProcessor::ParaEQ301AudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMS", createParameterLayout())
{
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

    for (int ch = 0; ch < maxChannelsPrepared; ++ch)
    {
        lowShelfPerChannel[static_cast<size_t>(ch)].prepare(spec);
        mid1PeakPerChannel[static_cast<size_t>(ch)].prepare(spec);
        mid2PeakPerChannel[static_cast<size_t>(ch)].prepare(spec);
        highShelfPerChannel[static_cast<size_t>(ch)].prepare(spec);
    }

    juce::dsp::ProcessSpec limSpec;
    limSpec.sampleRate = currentSampleRate;
    limSpec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock > 0 ? samplesPerBlock : 512);
    limSpec.numChannels = static_cast<juce::uint32>(juce::jmax(1, maxChannelsPrepared));
    outputLimiter.prepare(limSpec);
    outputLimiter.reset();

    for (float& p : lfoPhase)
        p = 0.f;

    debugSmoothedIn = debugSmoothedOut = 0.f;
    debugInRms.store(0.f, std::memory_order_relaxed);
    debugOutRms.store(0.f, std::memory_order_relaxed);

    updateFiltersUniform(currentSampleRate);
}

void ParaEQ301AudioProcessor::getEqChainMagnitudeDb(double sampleRate, const double* frequenciesHz,
                                                     float* magnitudesDb, int numPoints) const noexcept
{
    const double sr = sampleRate > 0.0 ? sampleRate : currentSampleRate;
    const double nyq = sr * 0.499;

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
    *lowShelfPerChannel[i].coefficients = *Coefficients::makeLowShelf(sampleRate, static_cast<double>(lowCf), kShelfQ, lowLin);
    *mid1PeakPerChannel[i].coefficients = *Coefficients::makePeakFilter(sampleRate, static_cast<double>(m1f), q1, m1Lin);
    *mid2PeakPerChannel[i].coefficients = *Coefficients::makePeakFilter(sampleRate, static_cast<double>(m2f), q2, m2Lin);
    *highShelfPerChannel[i].coefficients = *Coefficients::makeHighShelf(sampleRate, static_cast<double>(hiCf), kShelfQ, hiLin);
}

void ParaEQ301AudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused(midi);

    const double sr = getSampleRate() > 0.0 ? getSampleRate() : currentSampleRate;
    const int numCh = juce::jmin(buffer.getNumChannels(), maxChannelsPrepared);
    const int numSamps = buffer.getNumSamples();

    const float inRms = blockRms(buffer, numCh, numSamps);

    const bool coreOn = apvts.getRawParameterValue("coreOn")->load() > 0.5f;
    const float coreDrive = coreOn ? apvts.getRawParameterValue("coreSat")->load() : 0.f;

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

    // Motion off until at least one LFO amount (Gain % / Freq % / Width %) is > 0 — LFO Hz alone does not modulate.
    const bool anyLfo = (dHiG + dHiC + dM1G + dM1C + dM1B + dM2G + dM2C + dM2B + dLoG + dLoC) > 1.0e-6f;

    const float rHi = apvts.getRawParameterValue("lfoHiRate")->load() / static_cast<float>(sr);
    const float rM1 = apvts.getRawParameterValue("lfoM1Rate")->load() / static_cast<float>(sr);
    const float rM2 = apvts.getRawParameterValue("lfoM2Rate")->load() / static_cast<float>(sr);
    const float rLo = apvts.getRawParameterValue("lfoLoRate")->load() / static_cast<float>(sr);

    const float baseHiCf = apvts.getRawParameterValue("hiCf")->load();
    const float baseHiG = apvts.getRawParameterValue("hiGain")->load();
    const float baseM1f = apvts.getRawParameterValue("mid1Cf")->load();
    const float baseM1bw = apvts.getRawParameterValue("mid1Bw")->load();
    const float baseM1g = apvts.getRawParameterValue("mid1Gain")->load();
    const float baseM2f = apvts.getRawParameterValue("mid2Cf")->load();
    const float baseM2bw = apvts.getRawParameterValue("mid2Bw")->load();
    const float baseM2g = apvts.getRawParameterValue("mid2Gain")->load();
    const float baseLoCf = apvts.getRawParameterValue("lowCf")->load();
    const float baseLoG = apvts.getRawParameterValue("lowGain")->load();

    if (!anyLfo)
    {
        updateFiltersUniform(sr);
        for (int ch = 0; ch < numCh; ++ch)
        {
            float* data = buffer.getWritePointer(ch);
            for (int n = 0; n < numSamps; ++n)
            {
                float x = data[n];
                x = applyCoreSaturation(x, coreDrive);
                const size_t i = static_cast<size_t>(ch);
                x = lowShelfPerChannel[i].processSample(x);
                x = mid1PeakPerChannel[i].processSample(x);
                x = mid2PeakPerChannel[i].processSample(x);
                x = highShelfPerChannel[i].processSample(x);
                if (!std::isfinite(x))
                    x = 0.0f;
                data[n] = x;
            }
        }
    }
    else
    {
        for (int n = 0; n < numSamps; ++n)
        {
            lfoPhase[0] += rHi;
            lfoPhase[1] += rM1;
            lfoPhase[2] += rM2;
            lfoPhase[3] += rLo;
            for (float& ph : lfoPhase)
                if (ph >= 1.f)
                    ph -= std::floor(ph);

            const bool updateCoeffs = (n % kCoeffUpdateInterval) == 0;

            for (int ch = 0; ch < numCh; ++ch)
            {
                const float phaseShift = (numCh > 1 && ch == 1) ? stereoRad : 0.f;
                const float sHi = std::sin(kTwoPi * lfoPhase[0] + phaseShift);
                const float sM1 = std::sin(kTwoPi * lfoPhase[1] + phaseShift);
                const float sM2 = std::sin(kTwoPi * lfoPhase[2] + phaseShift);
                const float sLo = std::sin(kTwoPi * lfoPhase[3] + phaseShift);

                if (updateCoeffs)
                {
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
                }

                float* data = buffer.getWritePointer(ch);
                float x = data[n];
                x = applyCoreSaturation(x, coreDrive);
                const size_t i = static_cast<size_t>(ch);
                x = lowShelfPerChannel[i].processSample(x);
                x = mid1PeakPerChannel[i].processSample(x);
                x = mid2PeakPerChannel[i].processSample(x);
                x = highShelfPerChannel[i].processSample(x);
                if (!std::isfinite(x))
                    x = 0.0f;
                data[n] = x;
            }
        }
    }

    const bool limOn = apvts.getRawParameterValue("outLimOn")->load() > 0.5f;
    if (limOn)
    {
        const float limThreshDb = apvts.getRawParameterValue("outLimThresh")->load();
        outputLimiter.setThreshold(limThreshDb);
        outputLimiter.setRelease(apvts.getRawParameterValue("outLimRelease")->load());
        juce::dsp::AudioBlock<float> fullBlock(buffer);
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

    const float outRms = blockRms(buffer, numCh, numSamps);
    pushDebugMeters(inRms, outRms);
}

void ParaEQ301AudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    const auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void ParaEQ301AudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

juce::AudioProcessorEditor* ParaEQ301AudioProcessor::createEditor()
{
    return new ParaEQ301AudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ParaEQ301AudioProcessor();
}
