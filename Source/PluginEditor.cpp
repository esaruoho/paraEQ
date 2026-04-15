#include "PluginEditor.h"
#include <cmath>
#include <vector>

namespace
{
    constexpr int kKnobSize = 52;
    // Rotary + TextBoxBelow must live inside setBounds height; captions go below getBottom().
    constexpr int kTextBoxH = 18;
    constexpr int kSliderColumnH = kKnobSize + kTextBoxH;
    constexpr int kCaptionH = 15;
    constexpr int kGapCaption = 4;
    constexpr int kRowHeight = kSliderColumnH + kGapCaption + kCaptionH;
    constexpr int kLabelWidth = 40;
    constexpr int kColCfLeft = kLabelWidth + 4;
    constexpr int kColCfCenter = kColCfLeft + kKnobSize + 6;
    constexpr int kColBw = kColCfCenter + kKnobSize + 6;
    constexpr int kColGain = kColBw + kKnobSize + 6;

    const juce::Colour kPanelBlack(0xff0a0a0a);
    const juce::Colour kTextBright(0xfff0f0f0);
    const juce::Colour kTextMuted(0xffb0b0b0);
    const juce::Colour kTextBoxBg(0xff1a1a1a);
    const juce::Colour kAccentGreen(0xff5cb85c);
    const juce::Colour kAccentBlue(0xff6eb5ff);

    void styleSliderDark(juce::Slider& s, juce::Colour arcFill)
    {
        s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 52, kTextBoxH - 2);
        s.setColour(juce::Slider::rotarySliderFillColourId, arcFill);
        s.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff444444));
        s.setColour(juce::Slider::thumbColourId, kAccentBlue);
        s.setColour(juce::Slider::textBoxTextColourId, kTextBright);
        s.setColour(juce::Slider::textBoxBackgroundColourId, kTextBoxBg);
        s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xff555555));
    }

    void styleLabelDark(juce::Label& l, const juce::String& text, bool bright = true)
    {
        l.setText(text, juce::dontSendNotification);
        l.setJustificationType(juce::Justification::centred);
        l.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
        l.setColour(juce::Label::textColourId, bright ? kTextBright : kTextMuted);
    }

    void styleToggleDark(juce::ToggleButton& b)
    {
        b.setColour(juce::ToggleButton::textColourId, kTextBright);
        b.setColour(juce::ToggleButton::tickColourId, kAccentGreen);
        b.setColour(juce::ToggleButton::tickDisabledColourId, juce::Colour(0xff666666));
    }

    void styleMotionCaption(juce::Label& l, const juce::String& text)
    {
        l.setText(text, juce::dontSendNotification);
        l.setJustificationType(juce::Justification::centred);
        l.setFont(juce::Font(juce::FontOptions().withHeight(9.5f)));
        l.setColour(juce::Label::textColourId, kTextBright);
    }
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

    static void styleKnob(juce::Slider& s) { styleSliderDark(s, kAccentGreen); }

    static void styleLabel(juce::Label& l, const juce::String& text) { styleLabelDark(l, text, true); }

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
            band->bandLabel.setColour(juce::Label::textColourId, kTextBright);

            styleKnob(band->cf);
            addAndMakeVisible(band->cf);
            addAndMakeVisible(band->cfLabel);

            if (band->hasBw)
            {
                styleKnob(band->bw);
                addAndMakeVisible(band->bw);
                addAndMakeVisible(band->bwLabel);
            }

            styleKnob(band->gain);
            addAndMakeVisible(band->gain);
            addAndMakeVisible(band->gainLabel);
        }

        styleLabel(hi.cfLabel, "Shelf Hz");
        hi.cf.setTooltip("High shelf corner frequency. Frequencies above this are boosted or cut by the gain control.");
        styleLabel(hi.gainLabel, "Gain dB");
        hi.gain.setTooltip("High shelf gain in decibels (0 = flat).");

        styleLabel(mid1.cfLabel, "Peak Hz");
        styleLabel(mid1.bwLabel, "Width Hz");
        styleLabel(mid1.gainLabel, "Gain dB");
        mid1.cf.setTooltip("Centre frequency of the first mid peaking EQ.");
        mid1.bw.setTooltip("Bandwidth of the peak in Hz (smaller = narrower notch/boost).");
        mid1.gain.setTooltip("Gain at the peak centre in dB.");

        styleLabel(mid2.cfLabel, "Peak Hz");
        styleLabel(mid2.bwLabel, "Width Hz");
        styleLabel(mid2.gainLabel, "Gain dB");
        mid2.cf.setTooltip("Centre frequency of the second mid peaking EQ.");
        mid2.bw.setTooltip("Bandwidth of the peak in Hz.");
        mid2.gain.setTooltip("Gain at the peak centre in dB.");

        styleLabel(low.cfLabel, "Shelf Hz");
        styleLabel(low.gainLabel, "Gain dB");
        low.cf.setTooltip("Low shelf corner frequency. Frequencies below this are boosted or cut by the gain control.");
        low.gain.setTooltip("Low shelf gain in decibels (0 = flat).");

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
        styleToggleDark(coreOn);
        batts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(ap, "coreOn", coreOn));

        styleKnob(coreSat);
        styleLabel(coreSatLabel, "Sat %");
        addAndMakeVisible(coreSat);
        addAndMakeVisible(coreSatLabel);
        coreSat.setTooltip("Pre-EQ saturation amount (tanh blend). 0% = bypass.");
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

    void paint(juce::Graphics& g) override { g.fillAll(kPanelBlack); }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(8);
        constexpr int kFooterH = kSliderColumnH + kGapCaption + kCaptionH + 8;
        auto footer = bounds.removeFromBottom(kFooterH);
        coreOn.setBounds(footer.removeFromLeft(108).removeFromTop(24).translated(0, 4));
        const int cx = footer.getX() + 6;
        const int cy = footer.getY() + 4;
        coreSat.setBounds(cx, cy, kKnobSize, kSliderColumnH);
        coreSatLabel.setBounds(cx, coreSat.getBottom() + kGapCaption, kKnobSize, kCaptionH);

        auto placeRow = [&](BandKnobs& band, int rowIndex)
        {
            const int y = bounds.getY() + rowIndex * kRowHeight;
            band.bandLabel.setBounds(kLabelWidth - 36 + bounds.getX(), y + 18, 36, 22);

            auto placeKnob = [&](juce::Slider& sl, juce::Label& cap, int x)
            {
                sl.setBounds(x, y, kKnobSize, kSliderColumnH);
                cap.setBounds(x, sl.getBottom() + kGapCaption, kKnobSize, kCaptionH);
            };

            const int gainX = bounds.getX() + kColGain - kColCfLeft;
            placeKnob(band.gain, band.gainLabel, gainX);

            if (band.hasBw && band.hasCfInLeftColumn)
            {
                placeKnob(band.cf, band.cfLabel, bounds.getX());
                const int bwX = bounds.getX() + kColCfCenter - kColCfLeft;
                placeKnob(band.bw, band.bwLabel, bwX);
            }
            else
            {
                const int cfX = bounds.getX() + kColCfCenter - kColCfLeft;
                placeKnob(band.cf, band.cfLabel, cfX);
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
    juce::Label motionHint;
    Row hi, m1, m2, lo;

    static void sk(juce::Slider& s)
    {
        s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 48, kTextBoxH - 2);
        styleSliderDark(s, juce::Colour(0xff4a8ad4));
    }

    LfoTabContent(juce::AudioProcessorValueTreeState& ap,
                  std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>>& atts)
    {
        motionHint.setText("There is no separate \"Depth\" control. The percentage knobs are the amount:\n"
                           "Gain % / Freq % / Width % = how much that band moves. 0% = off. Column 1 = LFO speed (Hz) only.",
                           juce::dontSendNotification);
        motionHint.setJustificationType(juce::Justification::topLeft);
        motionHint.setFont(juce::Font(juce::FontOptions().withHeight(10.5f)));
        motionHint.setColour(juce::Label::textColourId, kAccentBlue);
        addAndMakeVisible(motionHint);

        stereoLabel.setText("L/R LFO phase", juce::dontSendNotification);
        stereoLabel.setJustificationType(juce::Justification::centredLeft);
        stereoLabel.setColour(juce::Label::textColourId, kTextBright);
        stereoLabel.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
        addAndMakeVisible(stereoLabel);
        sk(stereo);
        addAndMakeVisible(stereo);
        atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "lfoStereoPhase", stereo));
        stereo.textFromValueFunction = [](double v) { return juce::String(juce::roundToInt(v)) + " deg"; };
        stereo.setTooltip("Phase offset between left and right channel LFOs (stereo width of modulation).");

        auto setupRow = [&](Row& r, const juce::String& name, bool bw,
                            const char* rateId, const char* dg, const char* dc, const char* dbw)
        {
            r.title.setText(name, juce::dontSendNotification);
            r.title.setJustificationType(juce::Justification::centredRight);
            r.title.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
            r.title.setColour(juce::Label::textColourId, kTextBright);
            addAndMakeVisible(r.title);
            r.useBw = bw;

            sk(r.rate);
            styleMotionCaption(r.rateL, "Speed Hz");
            addAndMakeVisible(r.rate);
            addAndMakeVisible(r.rateL);
            atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, rateId, r.rate));
            r.rate.setTooltip("How fast the LFO runs (Hz). Does nothing by itself — raise Gain % / Freq % / Width % to hear motion.");

            sk(r.dGain);
            styleMotionCaption(r.dGainL, "Gain %");
            addAndMakeVisible(r.dGain);
            addAndMakeVisible(r.dGainL);
            atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, dg, r.dGain));
            r.dGain.setTooltip("How much the LFO pushes this band's EQ gain. 0% = off. 100% ≈ ±12 dB around the EQ tab gain.");

            sk(r.dCf);
            styleMotionCaption(r.dCfL, "Freq %");
            addAndMakeVisible(r.dCf);
            addAndMakeVisible(r.dCfL);
            atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, dc, r.dCf));
            r.dCf.setTooltip("How much the LFO moves centre or shelf frequency. 0% = off.");

            auto pct = [](double v) { return juce::String(juce::roundToInt(v * 100.0)) + " %"; };
            auto pctIn = [](const juce::String& t) { return juce::jlimit(0.0, 1.0, t.getDoubleValue() / 100.0); };
            r.dGain.textFromValueFunction = pct;
            r.dGain.valueFromTextFunction = pctIn;
            r.dCf.textFromValueFunction = pct;
            r.dCf.valueFromTextFunction = pctIn;

            if (bw)
            {
                sk(r.dBw);
                styleMotionCaption(r.dBwL, "Width %");
                addAndMakeVisible(r.dBw);
                addAndMakeVisible(r.dBwL);
                atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, dbw, r.dBw));
                r.dBw.textFromValueFunction = pct;
                r.dBw.valueFromTextFunction = pctIn;
                r.dBw.setTooltip("How much the LFO moves peaking bandwidth (mid bands only). 0% = off.");
            }
        };

        setupRow(hi, "Hi", false, "lfoHiRate", "lfoHiDepthGain", "lfoHiDepthCf", "");
        setupRow(m1, "M1", true, "lfoM1Rate", "lfoM1DepthGain", "lfoM1DepthCf", "lfoM1DepthBw");
        setupRow(m2, "M2", true, "lfoM2Rate", "lfoM2DepthGain", "lfoM2DepthCf", "lfoM2DepthBw");
        setupRow(lo, "Lo", false, "lfoLoRate", "lfoLoDepthGain", "lfoLoDepthCf", "");
    }

    void placeRow(Row& r, juce::Rectangle<int> rowArea)
    {
        const int kw = 48;
        const int colGap = 6;
        const int titleW = 38;
        auto a = rowArea;
        r.title.setBounds(a.getX(), a.getY() + 10, titleW, a.getHeight() - 12);
        int x = a.getX() + titleW + colGap;

        auto placeCol = [&](juce::Slider& sl, juce::Label& cap)
        {
            sl.setBounds(x, rowArea.getY(), kw, kSliderColumnH);
            cap.setBounds(x, sl.getBottom() + kGapCaption, kw, kCaptionH);
            x += kw + colGap;
        };

        placeCol(r.rate, r.rateL);
        placeCol(r.dGain, r.dGainL);
        placeCol(r.dCf, r.dCfL);
        if (r.useBw)
            placeCol(r.dBw, r.dBwL);
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced(8);
        motionHint.setBounds(b.removeFromTop(48));
        b.removeFromTop(4);
        const int topH = kSliderColumnH + 26;
        auto top = b.removeFromTop(topH);
        stereoLabel.setBounds(top.getX(), top.getY() + 8, 112, 18);
        stereo.setBounds(top.getX() + 118, top.getY() + 2, kKnobSize, kSliderColumnH);

        placeRow(hi, b.removeFromTop(kRowHeight));
        placeRow(m1, b.removeFromTop(kRowHeight));
        placeRow(m2, b.removeFromTop(kRowHeight));
        placeRow(lo, b.removeFromTop(kRowHeight));
    }

    void paint(juce::Graphics& g) override { g.fillAll(kPanelBlack); }
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
        styleToggleDark(limOn);
        batts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(ap, "outLimOn", limOn));

        styleSliderDark(limThresh, kAccentGreen);
        limThresh.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, kTextBoxH - 2);
        limThreshL.setText("Ceiling (dB)", juce::dontSendNotification);
        limThreshL.setJustificationType(juce::Justification::centred);
        limThreshL.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
        limThreshL.setColour(juce::Label::textColourId, kTextBright);
        addAndMakeVisible(limThresh);
        addAndMakeVisible(limThreshL);
        atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "outLimThresh", limThresh));
        limThresh.textFromValueFunction = [](double v) { return juce::String(v, 1) + " dB"; };

        styleSliderDark(limRelease, kAccentGreen);
        limRelease.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, kTextBoxH - 2);
        limRelL.setText("Release (ms)", juce::dontSendNotification);
        limRelL.setJustificationType(juce::Justification::centred);
        limRelL.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
        limRelL.setColour(juce::Label::textColourId, kTextBright);
        addAndMakeVisible(limRelease);
        addAndMakeVisible(limRelL);
        atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "outLimRelease", limRelease));
        limRelease.textFromValueFunction = [](double v) { return juce::String(juce::roundToInt(v)) + " ms"; };

        hint.setText("Output limiter catches EQ + saturation peaks (two-stage comp + clip).", juce::dontSendNotification);
        hint.setJustificationType(juce::Justification::topLeft);
        hint.setFont(juce::Font(juce::FontOptions().withHeight(12.0f)));
        hint.setColour(juce::Label::textColourId, kTextMuted);
        addAndMakeVisible(hint);
    }

    void paint(juce::Graphics& g) override { g.fillAll(kPanelBlack); }

    void resized() override
    {
        auto b = getLocalBounds().reduced(12);
        limOn.setBounds(b.removeFromTop(26));
        b.removeFromTop(10);
        const int knobW = 58;
        auto row = b.removeFromTop(kSliderColumnH + kGapCaption + kCaptionH);
        limThresh.setBounds(row.getX(), row.getY(), knobW, kSliderColumnH);
        limThreshL.setBounds(limThresh.getX() - 4, limThresh.getBottom() + kGapCaption, knobW + 16, kCaptionH);
        const int x2 = limThresh.getRight() + 20;
        limRelease.setBounds(x2, row.getY(), knobW, kSliderColumnH);
        limRelL.setBounds(limRelease.getX() - 6, limRelease.getBottom() + kGapCaption, knobW + 20, kCaptionH);
        b.removeFromTop(16);
        hint.setBounds(b.removeFromTop(juce::jmax(44, b.getHeight())));
    }
};

struct ParaEQ301AudioProcessorEditor::CurveTabContent : public juce::Component, private juce::Timer
{
    explicit CurveTabContent(ParaEQ301AudioProcessor& p) : proc(p) { startTimerHz(16); }

    ~CurveTabContent() override { stopTimer(); }

    void timerCallback() override { repaint(); }

    void resized() override { plotArea = getLocalBounds().reduced(8); }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(kPanelBlack);
        auto bounds = plotArea;
        if (bounds.getHeight() < 80)
            return;

        const int footerH = 42;
        auto footer = bounds.removeFromBottom(footerH);
        bounds.removeFromTop(2);
        g.setColour(kTextMuted);
        g.setFont(10.5f);
        g.drawFittedText("Green line: combined magnitude of low shelf + two peaking bands + high shelf (same IIR coefficients as the DSP, channel 0). "
                         "With Motion enabled, the curve tracks channel 0 in real time. Numeric In/Out RMS also appears in the top bar.",
                         footer, juce::Justification::topLeft, 6);

        g.setFont(12.f);
        g.setColour(kTextBright);
        g.drawText("EQ response & levels", bounds.removeFromTop(16), juce::Justification::centred, true);

        const int meterBlockH = 44;
        auto meters = bounds.removeFromBottom(meterBlockH + 6);
        drawLevelBar(g, meters.removeFromTop(18), proc.getDebugInputRms(), "In", kAccentGreen);
        meters.removeFromTop(6);
        drawLevelBar(g, meters, proc.getDebugOutputRms(), "Out", kAccentBlue);

        auto plot = bounds.reduced(6, 2);
        if (plot.getHeight() < 36)
            return;

        const double sr = proc.getSampleRate() > 0.0 ? proc.getSampleRate() : 44100.0;
        constexpr int nPts = 240;
        if ((int) freqScratch.size() != nPts)
        {
            freqScratch.resize((size_t) nPts);
            magScratch.resize((size_t) nPts);
        }
        const double fLo = 30.0;
        const double fHi = juce::jmin(20000.0, sr * 0.48);
        const double logLo = std::log(fLo);
        const double logHi = std::log(fHi);
        for (int i = 0; i < nPts; ++i)
        {
            const double t = (double) i / (double) (nPts - 1);
            freqScratch[(size_t) i] = std::exp(logLo + t * (logHi - logLo));
        }
        proc.getEqChainMagnitudeDb(sr, freqScratch.data(), magScratch.data(), nPts);

        const float dMin = -18.0f;
        const float dMax = 18.0f;

        juce::Rectangle<int> graph = plot;
        graph.removeFromBottom(12);

        g.setColour(juce::Colour(0xff1c1c1c));
        g.fillRoundedRectangle(graph.toFloat(), 4.f);
        g.setColour(juce::Colour(0xff353535));
        g.drawRoundedRectangle(graph.toFloat(), 4.f, 1.f);

        auto xOfF = [&](double f) -> float
        {
            f = juce::jlimit(fLo, fHi, f);
            const double lf = std::log(f);
            return (float) (graph.getX() + (lf - logLo) / (logHi - logLo) * (double) graph.getWidth());
        };

        auto yOfDb = [&](float db) -> float
        {
            const float n = juce::jlimit(dMin, dMax, db);
            const float norm = (n - dMin) / (dMax - dMin);
            return (float) graph.getBottom() - norm * (float) graph.getHeight();
        };

        for (float db : { -12.f, -6.f, 0.f, 6.f, 12.f })
        {
            if (db < dMin || db > dMax)
                continue;
            const float y = yOfDb(db);
            g.setColour(db == 0.f ? juce::Colour(0xff555555) : juce::Colour(0xff303030));
            g.drawHorizontalLine(juce::roundToInt(y), (float) graph.getX(), (float) graph.getRight());
        }

        g.setColour(kTextMuted);
        g.setFont(9.0f);
        for (double hz : { 100.0, 1000.0, 10000.0 })
        {
            if (hz < fLo || hz > fHi)
                continue;
            const float x = xOfF(hz);
            g.drawVerticalLine(juce::roundToInt(x), (float) graph.getY(), (float) graph.getBottom());
            const juce::String lab = hz == 100.0 ? "100" : (hz == 1000.0 ? "1k" : "10k");
            g.drawText(lab, juce::roundToInt(x) - 12, graph.getBottom() + 2, 24, 10, juce::Justification::centred);
        }

        juce::Path path;
        bool started = false;
        for (int i = 0; i < nPts; ++i)
        {
            const float x = xOfF(freqScratch[(size_t) i]);
            const float y = yOfDb(magScratch[(size_t) i]);
            if (!started)
            {
                path.startNewSubPath(x, y);
                started = true;
            }
            else
            {
                path.lineTo(x, y);
            }
        }
        g.setColour(kAccentGreen.withAlpha(0.95f));
        g.strokePath(path, juce::PathStrokeType(2.0f));
    }

    static void drawLevelBar(juce::Graphics& g, juce::Rectangle<int> r, float rmsLinear, const juce::String& name, juce::Colour col)
    {
        if (r.isEmpty())
            return;
        const float db = rmsLinear < 1.0e-10f ? -90.f : juce::Decibels::gainToDecibels(rmsLinear);
        const float dbClamped = juce::jlimit(-48.f, 6.f, db);
        const float t = (dbClamped + 48.f) / 54.f;

        g.setColour(juce::Colour(0xff1e1e1e));
        g.fillRoundedRectangle(r.toFloat(), 3.f);
        g.setColour(juce::Colour(0xff333333));
        g.drawRoundedRectangle(r.toFloat(), 3.f, 1.f);

        const auto barBg = r.withTrimmedLeft(38).reduced(1, 2);
        const int fillW = juce::roundToInt((float) barBg.getWidth() * juce::jlimit(0.f, 1.f, t));
        auto fill = barBg.withWidth(juce::jmax(0, fillW));
        g.setColour(col.withAlpha(0.88f));
        g.fillRoundedRectangle(fill.toFloat(), 2.f);

        g.setColour(kTextBright);
        g.setFont(10.f);
        g.drawText(name, r.getX() + 3, r.getY(), 34, r.getHeight(), juce::Justification::centredLeft);
        const juce::String s = rmsLinear < 1.0e-10f ? "-inf" : juce::String(db, 1) + " dB";
        g.setColour(kTextMuted);
        g.drawText(s, r.getRight() - 54, r.getY(), 52, r.getHeight(), juce::Justification::centredRight);
    }

    ParaEQ301AudioProcessor& proc;
    juce::Rectangle<int> plotArea;
    std::vector<double> freqScratch;
    std::vector<float> magScratch;
};

ParaEQ301AudioProcessorEditor::ParaEQ301AudioProcessorEditor(ParaEQ301AudioProcessor& p)
    : AudioProcessorEditor(&p), proc(p)
{
    auto& ap = proc.getAPVTS();

    eqPage = std::make_unique<EqTabContent>(ap, attachments, buttonAttachments);
    curvePage = std::make_unique<CurveTabContent>(proc);
    lfoPage = std::make_unique<LfoTabContent>(ap, attachments);
    outPage = std::make_unique<OutTabContent>(ap, attachments, buttonAttachments);

    tabs.addTab("EQ", kPanelBlack, eqPage.get(), false);
    tabs.addTab("Curve", kPanelBlack, curvePage.get(), false);
    tabs.addTab("Motion", kPanelBlack, lfoPage.get(), false);
    tabs.addTab("Output", kPanelBlack, outPage.get(), false);

    tabs.setColour(juce::TabbedComponent::backgroundColourId, kPanelBlack);
    tabs.getTabbedButtonBar().setColour(juce::TabbedButtonBar::tabOutlineColourId, juce::Colour(0xff333333));
    tabs.getTabbedButtonBar().setColour(juce::TabbedButtonBar::frontOutlineColourId, juce::Colour(0xff666666));
    tabs.getTabbedButtonBar().setColour(juce::TabbedButtonBar::tabTextColourId, kTextMuted);
    tabs.getTabbedButtonBar().setColour(juce::TabbedButtonBar::frontTextColourId, kTextBright);

    addAndMakeVisible(tabs);

    meterInLabel.setJustificationType(juce::Justification::centredLeft);
    meterOutLabel.setJustificationType(juce::Justification::centredLeft);
    meterInLabel.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
    meterOutLabel.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
    meterInLabel.setColour(juce::Label::textColourId, kAccentGreen);
    meterOutLabel.setColour(juce::Label::textColourId, kAccentBlue);
    addAndMakeVisible(meterInLabel);
    addAndMakeVisible(meterOutLabel);
    meterInLabel.setText("In:  (waiting for audio…)", juce::dontSendNotification);
    meterOutLabel.setText("Out: (waiting for audio…)", juce::dontSendNotification);

    startTimerHz(20);

    setSize(420, 600);
}

ParaEQ301AudioProcessorEditor::~ParaEQ301AudioProcessorEditor()
{
    stopTimer();
}

void ParaEQ301AudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(kPanelBlack);
    g.setColour(kTextBright);
    g.setFont(juce::Font(juce::FontOptions().withHeight(15.0f)));
    g.drawText("ParaEQ 301", 0, 4, getWidth(), 18, juce::Justification::centred);
}

void ParaEQ301AudioProcessorEditor::resized()
{
    constexpr int kMeterTop = 22;
    constexpr int kMeterH = 18;
    meterInLabel.setBounds(8, kMeterTop, juce::jmax(120, getWidth() / 2 - 12), kMeterH);
    meterOutLabel.setBounds(getWidth() / 2 + 2, kMeterTop, juce::jmax(120, getWidth() / 2 - 10), kMeterH);
    tabs.setBounds(0, kMeterTop + kMeterH, getWidth(), getHeight() - (kMeterTop + kMeterH));
}

void ParaEQ301AudioProcessorEditor::timerCallback()
{
    auto fmtDb = [](float rms) -> juce::String
    {
        if (rms < 1.0e-10f)
            return juce::String("-inf / silence");
        return juce::String(juce::Decibels::gainToDecibels(rms), 1) + " dBFS";
    };
    meterInLabel.setText("In:  " + fmtDb(proc.getDebugInputRms()) + " RMS", juce::dontSendNotification);
    meterOutLabel.setText("Out: " + fmtDb(proc.getDebugOutputRms()) + " RMS", juce::dontSendNotification);
}
