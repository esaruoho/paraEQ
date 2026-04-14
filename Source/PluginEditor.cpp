#include "PluginEditor.h"

namespace
{
    constexpr int kKnobSize = 52;
    constexpr int kRowHeight = 68;
    constexpr int kLabelWidth = 40;
    constexpr int kColCfLeft = kLabelWidth + 4;
    constexpr int kColCfCenter = kColCfLeft + kKnobSize + 6;
    constexpr int kColBw = kColCfCenter + kKnobSize + 6;
    constexpr int kColGain = kColBw + kKnobSize + 6;
}

struct ParaEQ301AudioProcessorEditor::EqTabContent : public juce::Component
{
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

    BandKnobs hi, mid1, mid2, low;
    juce::ToggleButton coreOn { "Core color" };
    juce::Slider coreSat;
    juce::Label coreSatLabel;

    static void styleKnob(juce::Slider& s)
    {
        s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 48, 14);
        s.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff4a9a4a));
        s.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff888888));
    }

    static void styleLabel(juce::Label& l, const juce::String& text)
    {
        l.setText(text, juce::dontSendNotification);
        l.setJustificationType(juce::Justification::centred);
        l.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
    }

    EqTabContent(juce::AudioProcessorValueTreeState& ap,
                 std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>>& atts,
                 std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>>& batts)
    {
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
            band->bandLabel.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));

            styleKnob(band->cf);
            styleLabel(band->cfLabel, "Cf");
            addAndMakeVisible(band->cf);
            addAndMakeVisible(band->cfLabel);

            if (band->hasBw)
            {
                styleKnob(band->bw);
                styleLabel(band->bwLabel, "Bw");
                addAndMakeVisible(band->bw);
                addAndMakeVisible(band->bwLabel);
            }

            styleKnob(band->gain);
            styleLabel(band->gainLabel, "Gain");
            addAndMakeVisible(band->gain);
            addAndMakeVisible(band->gainLabel);
        }

        atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "hiCf", hi.cf));
        atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "hiGain", hi.gain));
        atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "mid1Cf", mid1.cf));
        atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "mid1Bw", mid1.bw));
        atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "mid1Gain", mid1.gain));
        atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "mid2Cf", mid2.cf));
        atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "mid2Bw", mid2.bw));
        atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "mid2Gain", mid2.gain));
        atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "lowCf", low.cf));
        atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "lowGain", low.gain));

        addAndMakeVisible(coreOn);
        batts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(ap, "coreOn", coreOn));

        styleKnob(coreSat);
        styleLabel(coreSatLabel, "Sat");
        addAndMakeVisible(coreSat);
        addAndMakeVisible(coreSatLabel);
        atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "coreSat", coreSat));
        coreSat.textFromValueFunction = [](double v)
        {
            return juce::String(juce::roundToInt(v * 100.0)) + " %";
        };
        coreSat.valueFromTextFunction = [](const juce::String& t)
        {
            return juce::jlimit(0.0, 1.0, t.getDoubleValue() / 100.0);
        };

        auto hzStringFromValue = [](double v) { return juce::String(static_cast<int>(std::round(v))) + " Hz"; };
        auto dbStringFromValue = [](double v) { return juce::String(v, 1) + " dB"; };

        for (auto* s : {&hi.cf, &mid1.cf, &mid2.cf, &low.cf})
        {
            s->textFromValueFunction = hzStringFromValue;
            s->valueFromTextFunction = [](const juce::String& t) { return t.getDoubleValue(); };
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

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(6);
        auto footer = bounds.removeFromBottom(50);
        coreOn.setBounds(footer.removeFromLeft(100).reduced(0, 10));
        const int cx = footer.getX() + 4;
        coreSat.setBounds(cx, footer.getY(), kKnobSize, kKnobSize + 16);
        coreSatLabel.setBounds(cx, footer.getY() + kKnobSize, kKnobSize, 12);

        auto placeRow = [&](BandKnobs& band, int rowIndex)
        {
            const int y = bounds.getY() + rowIndex * kRowHeight;
            band.bandLabel.setBounds(kLabelWidth - 36 + bounds.getX(), y + 14, 36, 20);

            const int gainX = bounds.getX() + kColGain - kColCfLeft;
            band.gain.setBounds(gainX, y, kKnobSize, kKnobSize + 16);
            band.gainLabel.setBounds(gainX, y + kKnobSize, kKnobSize, 12);

            if (band.hasBw && band.hasCfInLeftColumn)
            {
                const int cfX = bounds.getX();
                band.cf.setBounds(cfX, y, kKnobSize, kKnobSize + 16);
                band.cfLabel.setBounds(cfX, y + kKnobSize, kKnobSize, 12);

                const int bwX = bounds.getX() + kColCfCenter - kColCfLeft;
                band.bw.setBounds(bwX, y, kKnobSize, kKnobSize + 16);
                band.bwLabel.setBounds(bwX, y + kKnobSize, kKnobSize, 12);
            }
            else
            {
                const int cfX = bounds.getX() + kColCfCenter - kColCfLeft;
                band.cf.setBounds(cfX, y, kKnobSize, kKnobSize + 16);
                band.cfLabel.setBounds(cfX, y + kKnobSize, kKnobSize, 12);
            }
        };

        placeRow(hi, 0);
        placeRow(mid1, 1);
        placeRow(mid2, 2);
        placeRow(low, 3);
    }
};

struct ParaEQ301AudioProcessorEditor::LfoTabContent : public juce::Component
{
    struct Row
    {
        juce::Label title;
        juce::Slider rate;
        juce::Label rateL;
        juce::Slider dGain;
        juce::Label dGainL;
        juce::Slider dCf;
        juce::Label dCfL;
        juce::Slider dBw;
        juce::Label dBwL;
        bool useBw = false;
    };

    juce::Label stereoLabel;
    juce::Slider stereo;
    Row hi, m1, m2, lo;

    static void sk(juce::Slider& s)
    {
        s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 40, 12);
        s.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff3a6a9a));
        s.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff888888));
    }

    static void lb(juce::Label& l, const juce::String& t)
    {
        l.setText(t, juce::dontSendNotification);
        l.setJustificationType(juce::Justification::centred);
        l.setFont(juce::Font(juce::FontOptions().withHeight(10.0f)));
    }

    LfoTabContent(juce::AudioProcessorValueTreeState& ap,
                  std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>>& atts)
    {
        stereoLabel.setText("L/R LFO phase", juce::dontSendNotification);
        stereoLabel.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(stereoLabel);
        sk(stereo);
        addAndMakeVisible(stereo);
        atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "lfoStereoPhase", stereo));
        stereo.textFromValueFunction = [](double v) { return juce::String(juce::roundToInt(v)) + " deg"; };

        auto setupRow = [&](Row& r, const juce::String& name, bool bw,
                            const char* rateId, const char* dg, const char* dc, const char* dbw)
        {
            r.title.setText(name, juce::dontSendNotification);
            r.title.setJustificationType(juce::Justification::centredRight);
            r.title.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
            addAndMakeVisible(r.title);
            r.useBw = bw;

            sk(r.rate);
            lb(r.rateL, "Hz");
            addAndMakeVisible(r.rate);
            addAndMakeVisible(r.rateL);
            atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, rateId, r.rate));

            sk(r.dGain);
            lb(r.dGainL, "Gn");
            addAndMakeVisible(r.dGain);
            addAndMakeVisible(r.dGainL);
            atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, dg, r.dGain));

            sk(r.dCf);
            lb(r.dCfL, "Cf");
            addAndMakeVisible(r.dCf);
            addAndMakeVisible(r.dCfL);
            atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, dc, r.dCf));

            auto pct = [](double v) { return juce::String(juce::roundToInt(v * 100.0)) + " %"; };
            auto pctIn = [](const juce::String& t) { return juce::jlimit(0.0, 1.0, t.getDoubleValue() / 100.0); };
            r.dGain.textFromValueFunction = pct;
            r.dGain.valueFromTextFunction = pctIn;
            r.dCf.textFromValueFunction = pct;
            r.dCf.valueFromTextFunction = pctIn;

            if (bw)
            {
                sk(r.dBw);
                lb(r.dBwL, "Bw");
                addAndMakeVisible(r.dBw);
                addAndMakeVisible(r.dBwL);
                atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, dbw, r.dBw));
                r.dBw.textFromValueFunction = pct;
                r.dBw.valueFromTextFunction = pctIn;
            }
        };

        setupRow(hi, "Hi", false, "lfoHiRate", "lfoHiDepthGain", "lfoHiDepthCf", "");
        setupRow(m1, "M1", true, "lfoM1Rate", "lfoM1DepthGain", "lfoM1DepthCf", "lfoM1DepthBw");
        setupRow(m2, "M2", true, "lfoM2Rate", "lfoM2DepthGain", "lfoM2DepthCf", "lfoM2DepthBw");
        setupRow(lo, "Lo", false, "lfoLoRate", "lfoLoDepthGain", "lfoLoDepthCf", "");
    }

    void placeRow(Row& r, juce::Rectangle<int> rowArea)
    {
        auto a = rowArea;
        r.title.setBounds(a.removeFromLeft(34));
        const int kw = 44;
        const int gap = 4;
        r.rate.setBounds(a.removeFromLeft(kw).withHeight(kw + 14).translated(0, -2));
        r.rateL.setBounds(r.rate.getX(), r.rate.getBottom() - 2, kw, 10);
        a.removeFromLeft(gap);
        r.dGain.setBounds(a.removeFromLeft(kw).withHeight(kw + 14).translated(0, -2));
        r.dGainL.setBounds(r.dGain.getX(), r.dGain.getBottom() - 2, kw, 10);
        a.removeFromLeft(gap);
        r.dCf.setBounds(a.removeFromLeft(kw).withHeight(kw + 14).translated(0, -2));
        r.dCfL.setBounds(r.dCf.getX(), r.dCf.getBottom() - 2, kw, 10);
        if (r.useBw)
        {
            a.removeFromLeft(gap);
            r.dBw.setBounds(a.removeFromLeft(kw).withHeight(kw + 14).translated(0, -2));
            r.dBwL.setBounds(r.dBw.getX(), r.dBw.getBottom() - 2, kw, 10);
        }
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced(6);
        auto top = b.removeFromTop(52);
        stereoLabel.setBounds(top.removeFromLeft(90).reduced(0, 12));
        stereo.setBounds(top.removeFromLeft(50).withHeight(50 + 14));

        const int rh = 58;
        placeRow(hi, b.removeFromTop(rh));
        placeRow(m1, b.removeFromTop(rh));
        placeRow(m2, b.removeFromTop(rh));
        placeRow(lo, b.removeFromTop(rh));
    }
};

struct ParaEQ301AudioProcessorEditor::OutTabContent : public juce::Component
{
    juce::ToggleButton limOn { "Limiter on" };
    juce::Label limThreshL;
    juce::Slider limThresh;
    juce::Label limRelL;
    juce::Slider limRelease;
    juce::Label hint;

    OutTabContent(juce::AudioProcessorValueTreeState& ap,
                  std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>>& atts,
                  std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>>& batts)
    {
        addAndMakeVisible(limOn);
        batts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(ap, "outLimOn", limOn));

        limThresh.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        limThresh.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 52, 14);
        limThreshL.setText("Ceiling (dB)", juce::dontSendNotification);
        limThreshL.setJustificationType(juce::Justification::centred);
        limThreshL.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
        addAndMakeVisible(limThresh);
        addAndMakeVisible(limThreshL);
        atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "outLimThresh", limThresh));
        limThresh.textFromValueFunction = [](double v) { return juce::String(v, 1) + " dB"; };

        limRelease.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        limRelease.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 52, 14);
        limRelL.setText("Release (ms)", juce::dontSendNotification);
        limRelL.setJustificationType(juce::Justification::centred);
        limRelL.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
        addAndMakeVisible(limRelease);
        addAndMakeVisible(limRelL);
        atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "outLimRelease", limRelease));
        limRelease.textFromValueFunction = [](double v) { return juce::String(juce::roundToInt(v)) + " ms"; };

        hint.setText("Output limiter catches EQ + saturation peaks (two-stage comp + clip).", juce::dontSendNotification);
        hint.setJustificationType(juce::Justification::topLeft);
        hint.setFont(juce::Font(juce::FontOptions().withHeight(12.0f)));
        hint.setColour(juce::Label::textColourId, juce::Colours::darkgrey);
        addAndMakeVisible(hint);
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced(10);
        limOn.setBounds(b.removeFromTop(28));
        b.removeFromTop(8);
        auto row = b.removeFromTop(72);
        limThresh.setBounds(row.removeFromLeft(60).withHeight(58));
        limThreshL.setBounds(limThresh.getX(), limThresh.getBottom(), 70, 14);
        row.removeFromLeft(16);
        limRelease.setBounds(row.removeFromLeft(60).withHeight(58));
        limRelL.setBounds(limRelease.getX(), limRelease.getBottom(), 80, 14);
        b.removeFromTop(12);
        hint.setBounds(b.removeFromTop(60));
    }
};

ParaEQ301AudioProcessorEditor::ParaEQ301AudioProcessorEditor(ParaEQ301AudioProcessor& p)
    : AudioProcessorEditor(&p), proc(p)
{
    auto& ap = proc.getAPVTS();

    eqPage = std::make_unique<EqTabContent>(ap, attachments, buttonAttachments);
    lfoPage = std::make_unique<LfoTabContent>(ap, attachments);
    outPage = std::make_unique<OutTabContent>(ap, attachments, buttonAttachments);

    tabs.addTab("EQ", juce::Colour(0xffe8e8e8), eqPage.get(), false);
    tabs.addTab("Motion", juce::Colour(0xffe8e8e8), lfoPage.get(), false);
    tabs.addTab("Output", juce::Colour(0xffe8e8e8), outPage.get(), false);

    addAndMakeVisible(tabs);

    setSize(340, 360);
}

ParaEQ301AudioProcessorEditor::~ParaEQ301AudioProcessorEditor() = default;

void ParaEQ301AudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xffd0d0d0));
    g.setColour(juce::Colours::black);
    g.setFont(juce::Font(juce::FontOptions().withHeight(15.0f)));
    g.drawText("ParaEQ 301", 0, 4, getWidth(), 18, juce::Justification::centred);
}

void ParaEQ301AudioProcessorEditor::resized()
{
    tabs.setBounds(0, 22, getWidth(), getHeight() - 22);
}
