#pragma once

#include <JuceHeader.h>

class ParaEQ301AudioProcessor : public juce::AudioProcessor
{
public:
    ParaEQ301AudioProcessor();
    ~ParaEQ301AudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    void updateFiltersUniform(double sampleRate) noexcept;
    void updateFiltersForChannel(int ch, double sampleRate,
                                 float lowCf, float lowGain,
                                 float m1f, float m1bw, float m1g,
                                 float m2f, float m2bw, float m2g,
                                 float hiCf, float hiGain) noexcept;

    juce::AudioProcessorValueTreeState apvts;

    using IIRFilter = juce::dsp::IIR::Filter<float>;
    using Coefficients = juce::dsp::IIR::Coefficients<float>;

    std::array<IIRFilter, 4> lowShelfPerChannel;
    std::array<IIRFilter, 4> mid1PeakPerChannel;
    std::array<IIRFilter, 4> mid2PeakPerChannel;
    std::array<IIRFilter, 4> highShelfPerChannel;

    juce::dsp::Limiter<float> outputLimiter;

    int maxChannelsPrepared = 0;
    double currentSampleRate = 44100.0;
    std::array<float, 4> lfoPhase { { 0.f, 0.f, 0.f, 0.f } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParaEQ301AudioProcessor)
};
