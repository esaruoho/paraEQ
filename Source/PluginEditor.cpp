#include "PluginEditor.h"

namespace
{
    constexpr int kKnobSize = 56;
    constexpr int kRowHeight = 72;
    constexpr int kLabelWidth = 44;
    constexpr int kColCfLeft = kLabelWidth + 4;
    constexpr int kColCfCenter = kColCfLeft + kKnobSize + 8;
    constexpr int kColBw = kColCfCenter + kKnobSize + 8;
    constexpr int kColGain = kColBw + kKnobSize + 8;
    constexpr int kTopMargin = 10;
}

void ParaEQ301AudioProcessorEditor::styleKnob(juce::Slider& s, const juce::String& name)
{
    s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 52, 16);
    s.setName(name);
    s.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff4a9a4a));
    s.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff888888));
}

void ParaEQ301AudioProcessorEditor::styleLabel(juce::Label& l, const juce::String& text)
{
    l.setText(text, juce::dontSendNotification);
    l.setJustificationType(juce::Justification::centred);
    l.setFont(juce::Font(juce::FontOptions().withHeight(12.0f)));
}

ParaEQ301AudioProcessorEditor::ParaEQ301AudioProcessorEditor(ParaEQ301AudioProcessor& p)
    : AudioProcessorEditor(&p), proc(p)
{
    setSize(320, 4 * kRowHeight + kTopMargin + 18);

    auto& ap = proc.getAPVTS();

    hi.hasBw = false;
    hi.hasCfInLeftColumn = false;
    mid1.hasBw = true;
    mid1.hasCfInLeftColumn = true;
    mid2.hasBw = true;
    mid2.hasCfInLeftColumn = true;
    low.hasBw = false;
    low.hasCfInLeftColumn = false;

    hi.bandLabel.setText("Hi:", juce::dontSendNotification);
    mid1.bandLabel.setText("Mid1:", juce::dontSendNotification);
    mid2.bandLabel.setText("Mid2:", juce::dontSendNotification);
    low.bandLabel.setText("Low:", juce::dontSendNotification);

    for (auto* band : {&hi, &mid1, &mid2, &low})
    {
        addAndMakeVisible(band->bandLabel);
        band->bandLabel.setJustificationType(juce::Justification::centredRight);
        band->bandLabel.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));

        styleKnob(band->cf, "Cf");
        styleLabel(band->cfLabel, "Cf");
        addAndMakeVisible(band->cf);
        addAndMakeVisible(band->cfLabel);

        if (band->hasBw)
        {
            styleKnob(band->bw, "Bw");
            styleLabel(band->bwLabel, "Bw");
            addAndMakeVisible(band->bw);
            addAndMakeVisible(band->bwLabel);
        }

        styleKnob(band->gain, "Gain");
        styleLabel(band->gainLabel, "Gain");
        addAndMakeVisible(band->gain);
        addAndMakeVisible(band->gainLabel);
    }

    attachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "hiCf", hi.cf));
    attachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "hiGain", hi.gain));
    attachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "mid1Cf", mid1.cf));
    attachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "mid1Bw", mid1.bw));
    attachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "mid1Gain", mid1.gain));
    attachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "mid2Cf", mid2.cf));
    attachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "mid2Bw", mid2.bw));
    attachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "mid2Gain", mid2.gain));
    attachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "lowCf", low.cf));
    attachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "lowGain", low.gain));

    auto hzStringFromValue = [](double v) { return juce::String(static_cast<int>(std::round(v))) + " Hz"; };
    auto dbStringFromValue = [](double v) { return juce::String(v, 1) + " dB"; };

    for (auto* s : {&hi.cf, &mid1.cf, &mid2.cf, &low.cf})
    {
        s->textFromValueFunction = hzStringFromValue;
        s->valueFromTextFunction = [](const juce::String& t)
        {
            return t.getDoubleValue();
        };
    }
    mid1.bw.textFromValueFunction = hzStringFromValue;
    mid1.bw.valueFromTextFunction = [](const juce::String& t) { return t.getDoubleValue(); };
    mid2.bw.textFromValueFunction = hzStringFromValue;
    mid2.bw.valueFromTextFunction = [](const juce::String& t) { return t.getDoubleValue(); };

    for (auto* s : {&hi.gain, &mid1.gain, &mid2.gain, &low.gain})
    {
        s->textFromValueFunction = dbStringFromValue;
        s->valueFromTextFunction = [](const juce::String& t) { return t.getDoubleValue(); };
    }
}

void ParaEQ301AudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xffd8d8d8));
    g.setColour(juce::Colours::black);
    g.setFont(15.0f);
    g.drawFittedText("ParaEQ 301", getLocalBounds().removeFromTop(18), juce::Justification::centred, 1);
}

void ParaEQ301AudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced(8);
    bounds.removeFromTop(18);

    auto placeRow = [&](BandKnobs& band, int rowIndex)
    {
        const int y = bounds.getY() + rowIndex * kRowHeight;
        band.bandLabel.setBounds(kLabelWidth - 40 + bounds.getX(), y + 18, 40, 22);

        const int gainX = bounds.getX() + kColGain - kColCfLeft;
        band.gain.setBounds(gainX, y, kKnobSize, kKnobSize + 18);
        band.gainLabel.setBounds(gainX, y + kKnobSize + 2, kKnobSize, 14);

        if (band.hasBw && band.hasCfInLeftColumn)
        {
            const int cfX = bounds.getX();
            band.cf.setBounds(cfX, y, kKnobSize, kKnobSize + 18);
            band.cfLabel.setBounds(cfX, y + kKnobSize + 2, kKnobSize, 14);

            const int bwX = bounds.getX() + kColCfCenter - kColCfLeft;
            band.bw.setBounds(bwX, y, kKnobSize, kKnobSize + 18);
            band.bwLabel.setBounds(bwX, y + kKnobSize + 2, kKnobSize, 14);
        }
        else
        {
            const int cfX = bounds.getX() + kColCfCenter - kColCfLeft;
            band.cf.setBounds(cfX, y, kKnobSize, kKnobSize + 18);
            band.cfLabel.setBounds(cfX, y + kKnobSize + 2, kKnobSize, 14);
        }
    };

    placeRow(hi, 0);
    placeRow(mid1, 1);
    placeRow(mid2, 2);
    placeRow(low, 3);
}
