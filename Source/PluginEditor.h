#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

/** Shared by the editor and EQ rotary strip: monospaced numeric readouts, no horizontal glyph stretch. */
struct PeqPluginSliderValueLookAndFeel : juce::LookAndFeel_V4
{
    juce::Label* createSliderTextBox(juce::Slider& slider) override;
};

class ParaEQ301AudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ParaEQ301AudioProcessorEditor(ParaEQ301AudioProcessor&);
    ~ParaEQ301AudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    struct EqTabContent;
    struct EqTabScrollHost;
    struct CurveTabContent;
    struct LfoTabContent;
    struct CurveMotionTabContent;
    struct RoastTabContent;
    struct AnharmTabContent;
    struct RoastTabScrollHost;
    struct AnharmTabScrollHost;
    struct ParametricTabContent;
    struct ParametricTabScrollHost;
    struct ShaperTabContent;
    struct ShaperTabScrollHost;
    struct OutTabContent;

    /** Destroyed last among subobjects (declared first): sliders resolve this for value boxes. */
    PeqPluginSliderValueLookAndFeel pluginSliderValueLf;
    ParaEQ301AudioProcessor& proc;

    std::unique_ptr<EqTabScrollHost> eqTabScroll;
    std::unique_ptr<CurveMotionTabContent> curveMotionPage;
    std::unique_ptr<RoastTabScrollHost> roastTabScroll;
    std::unique_ptr<AnharmTabScrollHost> anharmTabScroll;
    std::unique_ptr<ParametricTabScrollHost> parametricTabScroll;
    std::unique_ptr<ShaperTabScrollHost> shaperTabScroll;
    std::unique_ptr<OutTabContent> outPage;

    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };

    juce::Label meterInLabel;
    juce::Label meterOutLabel;
    juce::Rectangle<int> meterInBarBounds;
    juce::Rectangle<int> meterOutBarBounds;

    juce::Slider masterDryWetSlider;
    juce::Label masterDryWetCaption;
    /** FFT + IIR curve between top meter strip and tabbed pages (same paint as Curve / old EQ graph). */
    juce::Rectangle<int> mainSpectrumCurveBounds;
    std::vector<double> mainCurveFreqHz;
    std::vector<float> mainCurveMagScratch;
    std::vector<float> mainCurveEqSmoothed;
    std::vector<float> mainCurveComboSmoothed;
    std::vector<float> mainCurveSpecBefore;
    std::vector<float> mainCurveSpecAfter;

    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> attachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAttachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> comboAttachments;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParaEQ301AudioProcessorEditor)
};
