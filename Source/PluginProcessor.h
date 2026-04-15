#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <cstdint>

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

    /** Combined linear IIR chain magnitude (low shelf -> mid peaks -> high shelf), channel 0 coeffs. dB = 20·log10|H|.
        Frequencies are in Hz; magnitudes are evaluated at currentSampleRate (must match coefficient design). sampleRate arg ignored. */
    void getEqChainMagnitudeDb(double sampleRate, const double* frequenciesHz, float* magnitudesDb, int numPoints) const noexcept;

    /** Per-band LFO phase 0–1 for Motion tab UI (Hi, M1, M2, Lo). Audio thread writes; GUI reads. */
    void getMotionLfoPhases(float* outFour) const noexcept;

    /** Last-published channel-0 filter targets (Hz / dB) for Motion tab; updated each processBlock. */
    struct MotionEffectiveEqSnapshot
    {
        float hiCfHz = 0, hiGainDb = 0;
        float mid1CfHz = 0, mid1BwHz = 0, mid1GainDb = 0;
        float mid2CfHz = 0, mid2BwHz = 0, mid2GainDb = 0;
        float loCfHz = 0, loGainDb = 0;
        bool motionEngaged = false;
    };
    void getMotionEffectiveEqSnapshot(MotionEffectiveEqSnapshot& s) const noexcept;

    /** Log-spaced Hz → smoothed FFT spectrum dB for Curve tab (pre-EQ vs post-EQ, L ch). Returns false if a concurrent FFT write skipped the copy. */
    bool getSpectrumBeforeAfterDb(double sampleRate, const double* frequenciesHz, int numPoints,
                                 float* outBeforeDb, float* outAfterDb) const noexcept;

    static constexpr int kSpectrumFftOrder = 10;
    static constexpr int kSpectrumFftSize = 1 << kSpectrumFftOrder;
    static constexpr int kSpectrumBins = kSpectrumFftSize / 2 + 1;

    /** Must match EQ / Curve tab log-spaced plot point count (PluginEditor.cpp nPts). */
    static constexpr int kEqCurvePlotPoints = 220;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    void pushDebugMeters(float rawInRms, float rawOutRms) noexcept;
    void updateFiltersUniform(double sampleRate) noexcept;
    void updateFiltersForChannel(int ch, double sampleRate,
                                 float lowCf, float lowGainDb,
                                 float m1f, float m1bw, float m1GainDb,
                                 float m2f, float m2bw, float m2GainDb,
                                 float hiCf, float hiGainDb) noexcept;

    void publishMotionEqUiSnapshot(float hiCf, float hiGainDb,
                                   float m1f, float m1bw, float m1GainDb,
                                   float m2f, float m2bw, float m2GainDb,
                                   float loCf, float loGainDb,
                                   bool engaged) noexcept;

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

    std::atomic<float> motionLfoUiPhase[4];

    std::atomic<float> motionUiHiCf, motionUiHiGainDb;
    std::atomic<float> motionUiM1Cf, motionUiM1Bw, motionUiM1GainDb;
    std::atomic<float> motionUiM2Cf, motionUiM2Bw, motionUiM2GainDb;
    std::atomic<float> motionUiLoCf, motionUiLoGainDb;
    std::atomic<std::uint8_t> motionUiEngaged { 0 };

    std::atomic<float> debugInRms { 0.f };
    std::atomic<float> debugOutRms { 0.f };
    float debugSmoothedIn = 0.f;
    float debugSmoothedOut = 0.f;

    juce::dsp::FFT spectrumFft { kSpectrumFftOrder };
    float spectrumWindow[kSpectrumFftSize] {};
    float spectrumRingBefore[kSpectrumFftSize] {};
    float spectrumRingAfter[kSpectrumFftSize] {};
    int spectrumRingFill = 0;
    float spectrumFftScratch[2 * kSpectrumFftSize] {};
    float spectrumPubBefore[kSpectrumBins] {};
    float spectrumPubAfter[kSpectrumBins] {};
    float spectrumSmoothBefore[kSpectrumBins] {};
    float spectrumSmoothAfter[kSpectrumBins] {};
    mutable std::atomic<std::uint32_t> spectrumSeq { 0 };

    void spectrumPushPrePostEq(float beforeEq, float afterEq) noexcept;

    /** Rebuilds log-spaced Hz table (same formula as editor) and publishes |H| dB for ch0 IIR chain. */
    void publishEqCurveMagnitudeSnapshot() noexcept;

    double eqCurveFreqHz[kEqCurvePlotPoints] {};
    float eqCurveMagPublished[kEqCurvePlotPoints] {};
    mutable std::atomic<std::uint32_t> eqCurveMagSeq { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParaEQ301AudioProcessor)
};
