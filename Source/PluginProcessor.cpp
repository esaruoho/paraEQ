#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    constexpr float kShelfQ = 0.707f;

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
    juce::ignoreUnused(samplesPerBlock);
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = currentSampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock > 0 ? samplesPerBlock : 512);
    spec.numChannels = 1;

    maxChannelsPrepared = juce::jmax(getTotalNumOutputChannels(), getTotalNumInputChannels(), 2);
    maxChannelsPrepared = juce::jmin(maxChannelsPrepared, 4);

    for (int ch = 0; ch < maxChannelsPrepared; ++ch)
    {
        lowShelfPerChannel[static_cast<size_t>(ch)].prepare(spec);
        mid1PeakPerChannel[static_cast<size_t>(ch)].prepare(spec);
        mid2PeakPerChannel[static_cast<size_t>(ch)].prepare(spec);
        highShelfPerChannel[static_cast<size_t>(ch)].prepare(spec);
    }

    updateFilters(currentSampleRate);
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

void ParaEQ301AudioProcessor::updateFilters(double sampleRate) noexcept
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

    const float q1 = qFromBandwidthHz(m1f, m1bw);
    const float q2 = qFromBandwidthHz(m2f, m2bw);

    for (int ch = 0; ch < maxChannelsPrepared; ++ch)
    {
        const size_t i = static_cast<size_t>(ch);
        *lowShelfPerChannel[i].coefficients = *Coefficients::makeLowShelf(sampleRate, static_cast<double>(lowCf), kShelfQ, lowGain);
        *mid1PeakPerChannel[i].coefficients = *Coefficients::makePeakFilter(sampleRate, static_cast<double>(m1f), q1, m1g);
        *mid2PeakPerChannel[i].coefficients = *Coefficients::makePeakFilter(sampleRate, static_cast<double>(m2f), q2, m2g);
        *highShelfPerChannel[i].coefficients = *Coefficients::makeHighShelf(sampleRate, static_cast<double>(hiCf), kShelfQ, hiGain);
    }
}

void ParaEQ301AudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused(midi);

    updateFilters(getSampleRate() > 0.0 ? getSampleRate() : currentSampleRate);

    const int numCh = juce::jmin(buffer.getNumChannels(), maxChannelsPrepared);
    const int numSamps = buffer.getNumSamples();

    for (int ch = 0; ch < numCh; ++ch)
    {
        float* data = buffer.getWritePointer(ch);
        for (int n = 0; n < numSamps; ++n)
        {
            float x = data[n];
            const size_t i = static_cast<size_t>(ch);
            x = lowShelfPerChannel[i].processSample(x);
            x = mid1PeakPerChannel[i].processSample(x);
            x = mid2PeakPerChannel[i].processSample(x);
            x = highShelfPerChannel[i].processSample(x);
            data[n] = x;
        }
    }
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
