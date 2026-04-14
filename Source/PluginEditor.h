#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class ParaEQ301AudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit ParaEQ301AudioProcessorEditor(ParaEQ301AudioProcessor&);
    ~ParaEQ301AudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct EqTabContent;
    struct LfoTabContent;
    struct OutTabContent;

    ParaEQ301AudioProcessor& proc;

    std::unique_ptr<EqTabContent> eqPage;
    std::unique_ptr<LfoTabContent> lfoPage;
    std::unique_ptr<OutTabContent> outPage;

    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };

    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> attachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAttachments;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParaEQ301AudioProcessorEditor)
};
