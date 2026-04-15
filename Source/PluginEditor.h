#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

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
    struct CurveTabContent;
    struct LfoTabContent;
    struct CurveMotionTabContent;
    struct OutTabContent;

    ParaEQ301AudioProcessor& proc;

    std::unique_ptr<EqTabContent> eqPage;
    std::unique_ptr<CurveMotionTabContent> curveMotionPage;
    std::unique_ptr<OutTabContent> outPage;

    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };

    juce::Label meterInLabel;
    juce::Label meterOutLabel;

    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> attachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAttachments;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParaEQ301AudioProcessorEditor)
};
