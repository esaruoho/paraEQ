#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class ParaEQ301AudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit ParaEQ301AudioProcessorEditor(ParaEQ301AudioProcessor&);
    ~ParaEQ301AudioProcessorEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    ParaEQ301AudioProcessor& proc;

    struct BandKnobs
    {
        juce::Label bandLabel;
        juce::Slider cf;
        juce::Label cfLabel;
        juce::Slider bw;
        juce::Label bwLabel;
        juce::Slider gain;
        juce::Label gainLabel;
        bool hasBw = true;
        bool hasCfInLeftColumn = false;
    };

    BandKnobs hi;
    BandKnobs mid1;
    BandKnobs mid2;
    BandKnobs low;

    juce::Label coreSectionLabel;
    juce::Slider coreSat;
    juce::Label coreSatLabel;

    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> attachments;

    static void styleKnob(juce::Slider& s, const juce::String& name);
    static void styleLabel(juce::Label& l, const juce::String& text);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParaEQ301AudioProcessorEditor)
};
