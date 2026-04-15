#pragma once

#include <JuceHeader.h>
#include <atomic>

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

    /** Smoothed block RMS (linear, ~0–1+); for debug UI. Audio thread writes, GUI reads. */
    float getDebugInputRms() const noexcept { return debugInRms.load(std::memory_order_relaxed); }
    float getDebugOutputRms() const noexcept { return debugOutRms.load(std::memory_order_relaxed); }

    /** Combined linear IIR chain magnitude (low shelf → mid peaks → high shelf), channel 0 coeffs. dB = 20·log10|H|. */
    void getEqChainMagnitudeDb(double sampleRate, const double* frequenciesHz, float* magnitudesDb, int numPoints) const noexcept;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    void pushDebugMeters(float rawInRms, float rawOutRms) noexcept;
    void updateFiltersUniform(double sampleRate) noexcept;
    void updateFiltersForChannel(int ch, double sampleRate,
                                 float lowCf, float lowGainDb,
                                 float m1f, float m1bw, float m1GainDb,
                                 float m2f, float m2bw, float m2GainDb,
                                 float hiCf, float hiGainDb) noexcept;

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

    std::atomic<float> debugInRms { 0.f };
    std::atomic<float> debugOutRms { 0.f };
    float debugSmoothedIn = 0.f;
    float debugSmoothedOut = 0.f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParaEQ301AudioProcessor)
};
