#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <cstring>

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
    for (auto& p : motionLfoUiPhase)
        p.store(0.f, std::memory_order_relaxed);
    publishMotionEqUiSnapshot(0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, false);
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
    publishMotionEqUiSnapshot(apvts.getRawParameterValue("hiCf")->load(),
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
    publishEqCurveMagnitudeSnapshot();
}

void ParaEQ301AudioProcessor::publishMotionEqUiSnapshot(float hiCf, float hiGainDb,
                                                         float m1f, float m1bw, float m1GainDb,
                                                         float m2f, float m2bw, float m2GainDb,
                                                         float loCf, float loGainDb,
                                                         bool engaged) noexcept
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

void ParaEQ301AudioProcessor::publishEqCurveMagnitudeSnapshot() noexcept
{
    const double sr = currentSampleRate > 0.0 ? currentSampleRate : 44100.0;
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
    // Coefficients are built for currentSampleRate (prepareToPlay / processBlock). Evaluating |H(f)|
    // at any other rate skews the curve (often pushing the trace to the top of a fixed dB scale).
    juce::ignoreUnused(sampleRate);
    const double sr = currentSampleRate > 0.0 ? currentSampleRate : 44100.0;
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
                const float beforeEq = x;
                const size_t i = static_cast<size_t>(ch);
                x = lowShelfPerChannel[i].processSample(x);
                x = mid1PeakPerChannel[i].processSample(x);
                x = mid2PeakPerChannel[i].processSample(x);
                x = highShelfPerChannel[i].processSample(x);
                if (!std::isfinite(x))
                    x = 0.0f;
                data[n] = x;
                if (ch == 0)
                    spectrumPushPrePostEq(beforeEq, x);
            }
        }
        publishMotionEqUiSnapshot(baseHiCf, baseHiG, baseM1f, baseM1bw, baseM1g,
                                  baseM2f, baseM2bw, baseM2g, baseLoCf, baseLoG, false);
    }
    else
    {
        // Some hosts call processBlock with numChannels==0 (no I/O buffer yet). Motion must still advance
        // channel-0 coefficients and publish the EQ snapshot or motionEngaged stays false and Peak Hz never tracks.
        const int coeffChannels = juce::jmax(1, numCh);

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
                    if (ch == 0)
                        publishMotionEqUiSnapshot(hiCf, hiGain, m1f, m1bw, m1g, m2f, m2bw, m2g, loCf, loGain, true);
                }
            }

            for (int ch = 0; ch < numCh; ++ch)
            {
                float* data = buffer.getWritePointer(ch);
                float x = data[n];
                x = applyCoreSaturation(x, coreDrive);
                const float beforeEq = x;
                const size_t i = static_cast<size_t>(ch);
                x = lowShelfPerChannel[i].processSample(x);
                x = mid1PeakPerChannel[i].processSample(x);
                x = mid2PeakPerChannel[i].processSample(x);
                x = highShelfPerChannel[i].processSample(x);
                if (!std::isfinite(x))
                    x = 0.0f;
                data[n] = x;
                if (ch == 0)
                    spectrumPushPrePostEq(beforeEq, x);
            }
        }
    }

    publishEqCurveMagnitudeSnapshot();

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

    for (int i = 0; i < 4; ++i)
        motionLfoUiPhase[(size_t) i].store(lfoPhase[(size_t) i], std::memory_order_relaxed);
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
