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
    void updateFilters(double sampleRate) noexcept;

    juce::AudioProcessorValueTreeState apvts;

    using IIRFilter = juce::dsp::IIR::Filter<float>;
    using Coefficients = juce::dsp::IIR::Coefficients<float>;

    std::array<IIRFilter, 4> lowShelfPerChannel;
    std::array<IIRFilter, 4> mid1PeakPerChannel;
    std::array<IIRFilter, 4> mid2PeakPerChannel;
    std::array<IIRFilter, 4> highShelfPerChannel;

    int maxChannelsPrepared = 0;
    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParaEQ301AudioProcessor)
};
