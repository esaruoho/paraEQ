#include "PluginEditor.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

juce::Label* PeqPluginSliderValueLookAndFeel::createSliderTextBox(juce::Slider& slider)
{
    auto* l = juce::LookAndFeel_V4::createSliderTextBox(slider);
    if (l == nullptr)
        return nullptr;
    l->setFont(juce::Font(juce::FontOptions()
                              .withName(juce::Font::getDefaultMonospacedFontName())
                              .withHeight(11.0f)));
    l->setMinimumHorizontalScale(1.0f);
    const auto st = slider.getSliderStyle();
    if (st == juce::Slider::LinearHorizontal || st == juce::Slider::LinearVertical)
    {
        l->setJustificationType(juce::Justification::centredRight);
        l->setBorderSize(juce::BorderSize<int>(0, 3, 0, 5));
    }
    else
    {
        l->setJustificationType(juce::Justification::centred);
        l->setBorderSize(juce::BorderSize<int>(0, 2, 0, 2));
    }
    return l;
}

namespace
{
    /** Convert a long-text Label into a "(?)" button. Hides the source; click opens CallOutBox (auto-closes on outside click). */
    inline void wireInfoButton(juce::TextButton& btn, juce::Label& source, const juce::String& title)
    {
        btn.setButtonText("?");
        btn.setTooltip("Show description");
        btn.onClick = [src = &source, t = title, btnPtr = &btn]
        {
            auto content = std::make_unique<juce::Label>();
            const auto body = (t.isNotEmpty() ? "[" + t + "]\n\n" : juce::String()) + src->getText(true);
            content->setText(body, juce::dontSendNotification);
            content->setJustificationType(juce::Justification::topLeft);
            content->setFont(juce::Font(juce::FontOptions().withHeight(12.0f)));
            content->setColour(juce::Label::textColourId, juce::Colours::white);
            content->setColour(juce::Label::backgroundColourId, juce::Colour(0xff1a1a1a));
            content->setBorderSize(juce::BorderSize<int>(10, 12, 10, 12));
            content->setMinimumHorizontalScale(1.0f);
            juce::AttributedString a(body);
            a.setFont(juce::Font(juce::FontOptions().withHeight(12.0f)));
            juce::TextLayout layout;
            layout.createLayout(a, 420.0f);
            const int textH = juce::roundToInt(layout.getHeight()) + 28;
            content->setSize(440, juce::jlimit(60, 360, textH));
            juce::CallOutBox::launchAsynchronously(std::move(content), btnPtr->getScreenBounds(), nullptr);
        };
        source.setVisible(false);
    }

    constexpr int kKnobSize = 52;
    // Rotary + TextBoxBelow must live inside setBounds height; captions go below getBottom().
    constexpr int kTextBoxH = 18;
    constexpr int kSliderColumnH = kKnobSize + kTextBoxH;
    constexpr int kCaptionH = 15;
    constexpr int kGapCaption = 4;
    constexpr int kRowHeight = kSliderColumnH + kGapCaption + kCaptionH;

    /** Min height of LfoTabContent embedded on EQ (overview + 4 rows + stereo; no readout strip). */
    constexpr int kLfoMotionPanelMinH() noexcept
    {
        return 8 + 20 + 4 + 4 * kRowHeight + (20 + kSliderColumnH) + 8;
    }

    // Matches per-band gain parameter range (+/-30 dB). Combined chain can exceed this; draw clamps to plot edges.
    constexpr float kEqMagPlotDbMin = -30.f;
    constexpr float kEqMagPlotDbMax = 30.f;

    /** Horizontal insets inside EQ / Curve magnitude panels (+/-dB rulers); log-freq grid uses the inner band only. */
    constexpr int kEqMagFreqAxisInsetL = 54;
    constexpr int kEqMagFreqAxisInsetR = 44;

    /** Outer margin from tab content to graph stack (EQ tab + Curve tab must match for zero horizontal jump). */
    constexpr int kPeqTabPanelMargin = 8;

    /** EQ tab: main spectrum/curve lives in the editor chrome above tabs; no duplicate strip in the scroll view. */
    constexpr int kEqGraphMinOuterH = 0;
    constexpr int kEqMagGraphMaxOuterH = 0;

    /** Invisible hit target so help text lives in tooltips only (saves vertical space). */
    struct TooltipMouseProxy : juce::Component, juce::SettableTooltipClient
    {
        TooltipMouseProxy()
        {
            setInterceptsMouseClicks(true, false);
            setOpaque(false);
        }
        void paint(juce::Graphics&) override {}
    };

    const juce::Colour kPanelBlack(0xff0a0a0a);
    const juce::Colour kTextBright(0xfff0f0f0);
    const juce::Colour kTextMuted(0xffb0b0b0);
    const juce::Colour kTextBoxBg(0xff1a1a1a);
    const juce::Colour kAccentGreen(0xff5cb85c);
    const juce::Colour kAccentBlue(0xff6eb5ff);

    float meterNormalisedFromRms(float rmsLinear) noexcept
    {
        if (rmsLinear < 1.0e-9f)
            return 0.f;
        const float db = juce::Decibels::gainToDecibels(rmsLinear);
        return juce::jlimit(0.f, 1.f, (db + 72.f) / 72.f);
    }

    void paintLevelMeterBar(juce::Graphics& g, juce::Rectangle<int> barArea, float level01, juce::Colour c)
    {
        if (barArea.getWidth() < 2 || barArea.getHeight() < 2)
            return;
        auto r = barArea.toFloat();
        g.setColour(juce::Colour(0xff1e1e1e));
        g.fillRoundedRectangle(r, 3.f);
        g.setColour(juce::Colour(0xff3d3d3d));
        g.drawRoundedRectangle(r.reduced(0.4f), 3.f, 1.f);
        const float fillW = juce::jmax(1.f, (r.getWidth() - 4.f) * level01);
        auto fill = r.reduced(2.f).withWidth(fillW);
        juce::ColourGradient gr(c.brighter(0.15f), fill.getX(), fill.getCentreY(),
                                c.withAlpha(0.5f), fill.getRight(), fill.getCentreY(), false);
        g.setGradientFill(gr);
        g.fillRoundedRectangle(fill, 2.f);
    }

    void styleTopBarMixKnob(juce::Slider& s)
    {
        s.setSliderStyle(juce::Slider::LinearHorizontal);
        s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 20);
        s.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff1a1a1a));
        s.setColour(juce::Slider::trackColourId, kAccentGreen.withAlpha(0.9f));
        s.setColour(juce::Slider::thumbColourId, kAccentBlue);
        s.setColour(juce::Slider::textBoxTextColourId, kTextBright);
        s.setColour(juce::Slider::textBoxBackgroundColourId, kTextBoxBg);
        s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xff555555));
    }

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

    void styleLinearSliderCompact(juce::Slider& s, juce::Colour fillCol)
    {
        s.setSliderStyle(juce::Slider::LinearHorizontal);
        // Default wide enough for "100 %" + Hz/dB readouts on tabs that forget a follow-up setTextBoxStyle.
        s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, 20);
        s.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff1a1a1a));
        s.setColour(juce::Slider::trackColourId, fillCol.withAlpha(0.85f));
        s.setColour(juce::Slider::thumbColourId, kAccentBlue);
        s.setColour(juce::Slider::textBoxTextColourId, kTextBright);
        s.setColour(juce::Slider::textBoxBackgroundColourId, kTextBoxBg);
        s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xff555555));
    }

    void styleCoreSatWideSlider(juce::Slider& s)
    {
        s.setSliderStyle(juce::Slider::LinearHorizontal);
        s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 20);
        s.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff1a1a1a));
        s.setColour(juce::Slider::trackColourId, kAccentGreen.withAlpha(0.9f));
        s.setColour(juce::Slider::thumbColourId, kAccentBlue);
        s.setColour(juce::Slider::textBoxTextColourId, kTextBright);
        s.setColour(juce::Slider::textBoxBackgroundColourId, kTextBoxBg);
        s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xff555555));
    }

    // EQ column: rotary + monospace readout. Cf/gain use kEqTextBoxW; Width Hz max "2000 Hz" uses a narrower box.
    constexpr int kEqSliderColW = 72;
    constexpr int kEqTextBoxW = 66;
    constexpr int kEqBwTextBoxW = 56;
    constexpr int kEqTextBoxH = 18;
    constexpr int kEqSliderColumnH = kKnobSize + kEqTextBoxH;
    constexpr int kEqRowHeight = kEqSliderColumnH + kGapCaption + kCaptionH;

    /** Min width for one Motion row when LFO rotaries match EQ band knobs (78px cols; M1/M2 four-knob row is widest). */
    constexpr int kLfoMotionInterleavedMinW() noexcept
    {
        constexpr int tw = 26;
        return 14 + 4 + tw + 6 + 4 * kEqSliderColW + 3 * 6;
    }

    /** Vertical chrome on EQ tab below the graph (shared by resized + minimum scroll height). */
    struct PeqEqTabLayoutMetrics
    {
        static constexpr int gapAfterGraph = 2;
        static constexpr int motionLineH = 26;
        static constexpr int gapAfterMotion = 1;
        static constexpr int coreRowH = 26;
        static constexpr int coreBetweenRows = 1;
        static constexpr int thrillRowH = 28;
        static constexpr int coreToneRowH = 28;
        static constexpr int gapBeforeBands = 0;
        static constexpr int coreStripH() noexcept
        {
            return coreRowH + coreBetweenRows + thrillRowH + coreBetweenRows + coreRowH + coreBetweenRows + thrillRowH
                   + coreBetweenRows + coreToneRowH;
        }
        static constexpr int bandRowsH() noexcept { return 4 * kEqRowHeight; }
        /** Motion overview strip moved to EQ tab (was inside LfoTabContent); stacked LFO body height without that strip. */
        static constexpr int kEqMotionOverviewRowH() noexcept { return 4; }
        static constexpr int kLfoMotionStackedBodyMinH() noexcept
        {
            return kLfoMotionPanelMinH() - kEqMotionOverviewRowH();
        }
        /** EQ overview + max(EQ band stack, stacked Motion body when Motion is below bands). */
        static constexpr int bandMotionPairH() noexcept
        {
            return kEqMotionOverviewRowH()
                   + (bandRowsH() > kLfoMotionStackedBodyMinH() ? bandRowsH() : kLfoMotionStackedBodyMinH());
        }
        /** Everything below the graph except the graph itself. */
        static constexpr int chromeBelowGraph() noexcept
        {
            return gapAfterGraph + motionLineH + gapAfterMotion + coreStripH() + gapBeforeBands + bandMotionPairH();
        }
        /** Minimum total content height: chrome + EQ graph strip + tab vertical margins (otherwise graphH == 0). */
        static constexpr int minimumContentHeight() noexcept
        {
            return chromeBelowGraph() + 2 * kPeqTabPanelMargin + kEqGraphMinOuterH;
        }
    };

    void styleEqSlider(juce::Slider& s, juce::Colour arcFill)
    {
        s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, kEqTextBoxW, kEqTextBoxH - 1);
        s.setColour(juce::Slider::rotarySliderFillColourId, arcFill);
        s.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff444444));
        s.setColour(juce::Slider::thumbColourId, kAccentBlue);
        s.setColour(juce::Slider::textBoxTextColourId, kTextBright);
        s.setColour(juce::Slider::textBoxBackgroundColourId, kTextBoxBg);
        s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xff555555));
    }

    const juce::Identifier peqLiveDisplayId("peqLiveDisplay");

    float cfMotionDepthForMarker(juce::AudioProcessorValueTreeState& ap, const char* centreParamId) noexcept
    {
        if (std::strcmp(centreParamId, "lowCf") == 0)
            return ap.getRawParameterValue("lfoLoDepthCf")->load();
        if (std::strcmp(centreParamId, "mid1Cf") == 0)
            return ap.getRawParameterValue("lfoM1DepthCf")->load();
        if (std::strcmp(centreParamId, "mid2Cf") == 0)
            return ap.getRawParameterValue("lfoM2DepthCf")->load();
        if (std::strcmp(centreParamId, "hiCf") == 0)
            return ap.getRawParameterValue("lfoHiDepthCf")->load();
        return 0.f;
    }

    /** Rotary strip: custom arc drawing; value box matches global monospace readout policy. */
    struct EqBandLookAndFeel : juce::LookAndFeel_V4
    {
        juce::Label* createSliderTextBox(juce::Slider& slider) override
        {
            auto* l = juce::LookAndFeel_V4::createSliderTextBox(slider);
            if (l == nullptr)
                return nullptr;
            l->setFont(juce::Font(juce::FontOptions()
                                      .withName(juce::Font::getDefaultMonospacedFontName())
                                      .withHeight(11.0f)));
            l->setMinimumHorizontalScale(1.0f);
            l->setJustificationType(juce::Justification::centred);
            l->setBorderSize(juce::BorderSize<int>(0, 2, 0, 2));
            juce::ignoreUnused(slider);
            return l;
        }

        void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                              float rotaryStartAngle, float rotaryEndAngle, juce::Slider& slider) override
        {
            if (!(bool) slider.getProperties()["peqEqBox"]
                || !slider.getProperties().contains(peqLiveDisplayId))
            {
                juce::LookAndFeel_V4::drawRotarySlider(g, x, y, width, height, sliderPos,
                                                       rotaryStartAngle, rotaryEndAngle, slider);
                return;
            }

            const double liveRaw = (double) slider.getProperties()[peqLiveDisplayId];
            auto outline = slider.findColour(juce::Slider::rotarySliderOutlineColourId);
            auto fill = slider.findColour(juce::Slider::rotarySliderFillColourId);

            auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(10.0f);
            const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) / 2.0f;
            const float lineW = juce::jmin(8.0f, radius * 0.5f);
            const float arcRadius = radius - lineW * 0.5f;

            juce::Path backgroundArc;
            backgroundArc.addCentredArc(bounds.getCentreX(), bounds.getCentreY(),
                                          arcRadius, arcRadius, 0.0f,
                                          rotaryStartAngle, rotaryEndAngle, true);
            g.setColour(outline);
            g.strokePath(backgroundArc, juce::PathStrokeType(lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            if (slider.isEnabled())
            {
                const double mn = slider.getMinimum();
                const double mx = slider.getMaximum();
                const double liveClamped = juce::jlimit(mn, mx, liveRaw);
                const float liveProp = (float) slider.valueToProportionOfLength(liveClamped);
                const float liveAngle = rotaryStartAngle + liveProp * (rotaryEndAngle - rotaryStartAngle);

                juce::Path valueArc;
                valueArc.addCentredArc(bounds.getCentreX(), bounds.getCentreY(),
                                       arcRadius, arcRadius, 0.0f,
                                       rotaryStartAngle, liveAngle, true);
                g.setColour(fill);
                g.strokePath(valueArc, juce::PathStrokeType(lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }

            const float restAngle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
            const float thumbW = lineW * 2.0f;
            const juce::Point<float> thumbPoint(
                bounds.getCentreX() + arcRadius * std::cos(restAngle - juce::MathConstants<float>::halfPi),
                bounds.getCentreY() + arcRadius * std::sin(restAngle - juce::MathConstants<float>::halfPi));
            g.setColour(slider.findColour(juce::Slider::thumbColourId));
            g.fillEllipse(juce::Rectangle<float>(thumbW, thumbW).withCentre(thumbPoint));
        }
    };

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

    void stylePeqComboBox(juce::ComboBox& c)
    {
        c.setColour(juce::ComboBox::backgroundColourId, kTextBoxBg);
        c.setColour(juce::ComboBox::textColourId, kTextBright);
        c.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff555555));
        c.setColour(juce::ComboBox::buttonColourId, juce::Colour(0xff2a2a2a));
        c.setColour(juce::ComboBox::arrowColourId, kTextMuted);
    }

    void styleMotionCaption(juce::Label& l, const juce::String& text)
    {
        l.setText(text, juce::dontSendNotification);
        l.setJustificationType(juce::Justification::centred);
        l.setFont(juce::Font(juce::FontOptions().withHeight(9.5f)));
        l.setColour(juce::Label::textColourId, kTextBright);
    }

    void paintMotionLfoDot(juce::Graphics& g, juce::Rectangle<int> area, float phase01, float brightness01, juce::Colour accent)
    {
        const float cx = (float) area.getCentreX();
        const float cy = (float) area.getCentreY();
        const float r = juce::jmin((float) area.getWidth(), (float) area.getHeight()) * 0.5f - 1.f;
        g.setColour(kTextMuted.withAlpha(0.28f + 0.4f * brightness01));
        g.drawEllipse(cx - r, cy - r, r * 2.f, r * 2.f, 1.f);
        const float a = phase01 * juce::MathConstants<float>::twoPi - juce::MathConstants<float>::halfPi;
        const float dotR = 2.8f;
        const float rr = juce::jmax(0.f, r - dotR);
        g.setColour(accent.withAlpha(0.35f + 0.65f * brightness01));
        g.fillEllipse(cx + std::cos(a) * rr - dotR, cy + std::sin(a) * rr - dotR, dotR * 2.f, dotR * 2.f);
    }

    bool motionLfoDepthActive(juce::AudioProcessorValueTreeState& ap) noexcept
    {
        return (ap.getRawParameterValue("lfoHiDepthGain")->load()
                + ap.getRawParameterValue("lfoHiDepthCf")->load()
                + ap.getRawParameterValue("lfoM1DepthGain")->load()
                + ap.getRawParameterValue("lfoM1DepthCf")->load()
                + ap.getRawParameterValue("lfoM1DepthBw")->load()
                + ap.getRawParameterValue("lfoM2DepthGain")->load()
                + ap.getRawParameterValue("lfoM2DepthCf")->load()
                + ap.getRawParameterValue("lfoM2DepthBw")->load()
                + ap.getRawParameterValue("lfoLoDepthGain")->load()
                + ap.getRawParameterValue("lfoLoDepthCf")->load())
               > 1.0e-6f;
    }

    bool eqBandHasMotion(juce::AudioProcessorValueTreeState& ap, int bandIndex) noexcept
    {
        switch (bandIndex)
        {
            case 0:
                return (ap.getRawParameterValue("lfoHiDepthGain")->load()
                        + ap.getRawParameterValue("lfoHiDepthCf")->load())
                       > 1.0e-6f;
            case 1:
                return (ap.getRawParameterValue("lfoM1DepthGain")->load()
                        + ap.getRawParameterValue("lfoM1DepthCf")->load()
                        + ap.getRawParameterValue("lfoM1DepthBw")->load())
                       > 1.0e-6f;
            case 2:
                return (ap.getRawParameterValue("lfoM2DepthGain")->load()
                        + ap.getRawParameterValue("lfoM2DepthCf")->load()
                        + ap.getRawParameterValue("lfoM2DepthBw")->load())
                       > 1.0e-6f;
            case 3:
                return (ap.getRawParameterValue("lfoLoDepthGain")->load()
                        + ap.getRawParameterValue("lfoLoDepthCf")->load())
                       > 1.0e-6f;
            default:
                return false;
        }
    }

    juce::String eqMotionTargetsSummary(juce::AudioProcessorValueTreeState& ap, int bandIndex)
    {
        auto on = [](float v) { return v > 1.0e-6f; };
        juce::String s;
        switch (bandIndex)
        {
            case 0:
                if (on(ap.getRawParameterValue("lfoHiDepthGain")->load()))
                    s << "Gain dB ";
                if (on(ap.getRawParameterValue("lfoHiDepthCf")->load()))
                    s << "Shelf Hz ";
                break;
            case 1:
                if (on(ap.getRawParameterValue("lfoM1DepthGain")->load()))
                    s << "Gain dB ";
                if (on(ap.getRawParameterValue("lfoM1DepthCf")->load()))
                    s << "Peak Hz ";
                if (on(ap.getRawParameterValue("lfoM1DepthBw")->load()))
                    s << "Width Hz ";
                break;
            case 2:
                if (on(ap.getRawParameterValue("lfoM2DepthGain")->load()))
                    s << "Gain dB ";
                if (on(ap.getRawParameterValue("lfoM2DepthCf")->load()))
                    s << "Peak Hz ";
                if (on(ap.getRawParameterValue("lfoM2DepthBw")->load()))
                    s << "Width Hz ";
                break;
            case 3:
                if (on(ap.getRawParameterValue("lfoLoDepthGain")->load()))
                    s << "Gain dB ";
                if (on(ap.getRawParameterValue("lfoLoDepthCf")->load()))
                    s << "Shelf Hz ";
                break;
            default:
                break;
        }
        s = s.trimEnd();
        return s.isEmpty() ? juce::String("(nothing - all 0%)") : s;
    }

    bool motionEqSnapshotShowsRightSplit(const ParaEQ301AudioProcessor::MotionEffectiveEqSnapshot& s) noexcept
    {
        if (!s.motionEngaged)
            return false;
        return std::abs(s.hiCfHz - s.hiCfHzR) > 0.35f || std::abs(s.mid1CfHz - s.mid1CfHzR) > 0.35f
            || std::abs(s.mid2CfHz - s.mid2CfHzR) > 0.35f || std::abs(s.loCfHz - s.loCfHzR) > 0.35f
            || std::abs(s.hiGainDb - s.hiGainDbR) > 0.02f || std::abs(s.mid1GainDb - s.mid1GainDbR) > 0.02f
            || std::abs(s.mid2GainDb - s.mid2GainDbR) > 0.02f || std::abs(s.loGainDb - s.loGainDbR) > 0.02f
            || std::abs(s.mid1BwHz - s.mid1BwHzR) > 0.35f || std::abs(s.mid2BwHz - s.mid2BwHzR) > 0.35f;
    }

    juce::String appendLiveEqSnapshotNumbers(const ParaEQ301AudioProcessor::MotionEffectiveEqSnapshot& s)
    {
        auto ri = [](float x) { return juce::String(juce::roundToInt(x)); };
        juce::String t;
        // One band per line so the Motion readout never soft-wraps mid-band as Hz/dB digit widths change.
        t << "Hi  " << ri(s.hiCfHz) << " Hz  " << juce::String(s.hiGainDb, 1) << " dB\n";
        t << "M1  " << ri(s.mid1CfHz) << " Hz  " << ri(s.mid1BwHz) << " Hz BW  " << juce::String(s.mid1GainDb, 1) << " dB\n";
        t << "M2  " << ri(s.mid2CfHz) << " Hz  " << ri(s.mid2BwHz) << " Hz BW  " << juce::String(s.mid2GainDb, 1) << " dB\n";
        t << "Lo  " << ri(s.loCfHz) << " Hz  " << juce::String(s.loGainDb, 1) << " dB";
        if (motionEqSnapshotShowsRightSplit(s))
        {
            t << "\n--- R ch (L/R LFO phase) ---\n";
            t << "Hi  " << ri(s.hiCfHzR) << " Hz  " << juce::String(s.hiGainDbR, 1) << " dB\n";
            t << "M1  " << ri(s.mid1CfHzR) << " Hz  " << ri(s.mid1BwHzR) << " Hz BW  " << juce::String(s.mid1GainDbR, 1) << " dB\n";
            t << "M2  " << ri(s.mid2CfHzR) << " Hz  " << ri(s.mid2BwHzR) << " Hz BW  " << juce::String(s.mid2GainDbR, 1) << " dB\n";
            t << "Lo  " << ri(s.loCfHzR) << " Hz  " << juce::String(s.loGainDbR, 1) << " dB";
        }
        return t;
    }

    juce::String buildEqMotionPanelTooltip(juce::AudioProcessorValueTreeState& ap,
                                           const ParaEQ301AudioProcessor::MotionEffectiveEqSnapshot& s)
    {
        if (!motionLfoDepthActive(ap))
            return "Motion is off (all LFO depth knobs at 0%).\n"
                   "On the EQ tab, scroll to the Motion strip (Hi / M1 / M2 / Lo) and raise EQ gain / freq / width % on a band.\n\n"
                   + appendLiveEqSnapshotNumbers(s);

        if (!s.motionEngaged)
            return "Motion is armed (depth > 0%) but the host is not running the plugin's audio graph yet.\n"
                   "Start playback so processBlock runs; then green arcs = live EQ, blue dot = stored rest value.\n\n"
                   "Targets - Hi: " + eqMotionTargetsSummary(ap, 0) + "  Mid1: " + eqMotionTargetsSummary(ap, 1) + "\n"
                   "Mid2: " + eqMotionTargetsSummary(ap, 2) + "  Low: " + eqMotionTargetsSummary(ap, 3) + "\n\n"
                   + appendLiveEqSnapshotNumbers(s);

        return "Motion on (stereo).\n"
               "Green arc = live filter value; blue dot = stored parameter (drag to edit rest).\n"
               "Lo / M1 / M2 / Hi: solid tags = left EQ; dashed verticals = right EQ when L/R LFO phase shifts the R-channel LFO sines.\n\n"
               "Targets - Hi: " + eqMotionTargetsSummary(ap, 0) + "  Mid1: " + eqMotionTargetsSummary(ap, 1) + "\n"
               "Mid2: " + eqMotionTargetsSummary(ap, 2) + "  Low: " + eqMotionTargetsSummary(ap, 3) + "\n\n"
               + appendLiveEqSnapshotNumbers(s);
    }

    juce::String buildEqMotionStatusShort(juce::AudioProcessorValueTreeState& ap,
                                          const ParaEQ301AudioProcessor::MotionEffectiveEqSnapshot& s)
    {
        if (!motionLfoDepthActive(ap))
            return "Motion off - use EQ tab Motion strip to raise LFO depth % on a band.";
        if (!s.motionEngaged)
            return "Motion armed - start playback; green arc = live EQ, blue dot = stored value.";
        return "Motion on: green arc = live EQ (L), blue dot = stored rest; graph tags track L (solid) and R (dashed) modulated Hz when stereo differs.";
    }

    juce::String formatMotionLiveReadout(const ParaEQ301AudioProcessor::MotionEffectiveEqSnapshot& s)
    {
        juce::String t;
        if (!s.motionEngaged)
            t = "Live filter (L) - EQ tab snapshot when Motion is idle.\n";
        else if (motionEqSnapshotShowsRightSplit(s))
            t = "Live filter L vs R - dashed tags / curves = R when stereo phase shifts R.\n";
        else
            t = "Live filter (L) - follows the LFO; EQ knobs track while audio runs.\n";
        t += appendLiveEqSnapshotNumbers(s);
        return t;
    }

    /** Fixed dB window (0 dB vertical centre = flat / unity gain).         Optional coloured markers at Lo/M1/M2/Hi centre Hz.
        freqAxisInsetLeftPx / RightPx: horizontal space reserved for +/-dB labels (defaults match EQ tab).
        drawDecorations: set false to stroke only the magnitude path (reuse same graph after drawing chrome once). */
    void paintEqMagnitudeCurveInRect(juce::Graphics& g,
                                     juce::Rectangle<int> graphOuter,
                                     const std::vector<double>& freqHz,
                                     const float* magDb,
                                     int nPts,
                                     float dMin,
                                     float dMax,
                                     double fLo,
                                     double fHi,
                                     juce::AudioProcessorValueTreeState* apMarkers,
                                     const ParaEQ301AudioProcessor::MotionEffectiveEqSnapshot* motionSnap,
                                     juce::Colour curveStrokeColour,
                                     bool drawRoundedBackground,
                                     int freqAxisInsetLeftPx = kEqMagFreqAxisInsetL,
                                     int freqAxisInsetRightPx = kEqMagFreqAxisInsetR,
                                     bool drawDecorations = true)
    {
        if (graphOuter.getHeight() < 22 || nPts < 2)
            return;

        const double logLo = std::log(fLo);
        const double logHi = std::log(fHi);

        juce::Rectangle<int> band = graphOuter;
        juce::Rectangle<int> ruler;
        juce::Rectangle<int> scaleRight;
        juce::Rectangle<int> curve;
        if (freqAxisInsetLeftPx > 0 || freqAxisInsetRightPx > 0)
        {
            ruler = band.removeFromLeft(freqAxisInsetLeftPx);
            scaleRight = band.removeFromRight(freqAxisInsetRightPx);
            curve = band;
        }
        else
        {
            curve = band;
        }

        if (drawDecorations)
        {
            if (drawRoundedBackground)
            {
                g.setColour(juce::Colour(0xff1a1a1a));
                g.fillRoundedRectangle(graphOuter.toFloat(), 3.f);
                g.setColour(juce::Colour(0xff333333));
                g.drawRoundedRectangle(graphOuter.toFloat(), 3.f, 1.f);
            }

            auto xOfFDec = [&](double f) -> float
            {
                f = juce::jlimit(fLo, fHi, f);
                const double lf = std::log(f);
                return (float) (curve.getX() + (lf - logLo) / (logHi - logLo) * (double) curve.getWidth());
            };

            auto yOfDbDec = [&](float db) -> float
            {
                const float n = juce::jlimit(dMin, dMax, db);
                const float norm = (n - dMin) / (dMax - dMin);
                return (float) curve.getBottom() - norm * (float) curve.getHeight();
            };

            for (float db : { -27.f, -24.f, -18.f, -12.f, -9.f, -6.f, -3.f, 3.f, 6.f, 9.f, 12.f, 18.f, 24.f, 27.f })
            {
                if (db <= dMin + 0.001f || db >= dMax - 0.001f || std::abs((double) db) < 0.01)
                    continue;
                const float y = yOfDbDec(db);
                g.setColour(juce::Colour(0xff2a2a2a));
                g.drawHorizontalLine(juce::roundToInt(y), (float) curve.getX(), (float) curve.getRight());
            }

            g.setColour(kTextMuted);
            g.setFont(8.5f);
            for (double hz : { 100.0, 1000.0, 10000.0 })
            {
                if (hz < fLo || hz > fHi)
                    continue;
                const float x = xOfFDec(hz);
                g.drawVerticalLine(juce::roundToInt(x), (float) curve.getY(), (float) curve.getBottom());
                const juce::String lab = hz == 100.0 ? "100" : (hz == 1000.0 ? "1k" : "10k");
                g.drawText(lab, juce::roundToInt(x) - 11, curve.getBottom() + 1, 22, 9, juce::Justification::centred);
            }

            if (apMarkers != nullptr)
            {
                struct BandTag
                {
                    const char* paramId;
                    const char* tag;
                    juce::Colour col;
                };
                const BandTag tags[] = { { "lowCf", "Lo", juce::Colour(0xff5dade2) },
                                         { "mid1Cf", "M1", juce::Colour(0xfff5b041) },
                                         { "mid2Cf", "M2", juce::Colour(0xffaf7ac5) },
                                         { "hiCf", "Hi", juce::Colour(0xff7ecbff) } };
                for (const auto& t : tags)
                {
                    float hz = apMarkers->getRawParameterValue(t.paramId)->load();
                    float hzR = hz;
                    if (motionSnap != nullptr && motionSnap->motionEngaged
                        && cfMotionDepthForMarker(*apMarkers, t.paramId) > 1.0e-6f)
                    {
                        if (std::strcmp(t.paramId, "lowCf") == 0)
                        {
                            hz = motionSnap->loCfHz;
                            hzR = motionSnap->loCfHzR;
                        }
                        else if (std::strcmp(t.paramId, "mid1Cf") == 0)
                        {
                            hz = motionSnap->mid1CfHz;
                            hzR = motionSnap->mid1CfHzR;
                        }
                        else if (std::strcmp(t.paramId, "mid2Cf") == 0)
                        {
                            hz = motionSnap->mid2CfHz;
                            hzR = motionSnap->mid2CfHzR;
                        }
                        else if (std::strcmp(t.paramId, "hiCf") == 0)
                        {
                            hz = motionSnap->hiCfHz;
                            hzR = motionSnap->hiCfHzR;
                        }
                    }
                    else if (motionSnap != nullptr && motionSnap->motionEngaged)
                    {
                        if (std::strcmp(t.paramId, "lowCf") == 0)
                            hzR = motionSnap->loCfHzR;
                        else if (std::strcmp(t.paramId, "mid1Cf") == 0)
                            hzR = motionSnap->mid1CfHzR;
                        else if (std::strcmp(t.paramId, "mid2Cf") == 0)
                            hzR = motionSnap->mid2CfHzR;
                        else if (std::strcmp(t.paramId, "hiCf") == 0)
                            hzR = motionSnap->hiCfHzR;
                    }
                    if (hz < (float) fLo || hz > (float) fHi)
                        continue;
                    const float x = xOfFDec(static_cast<double>(hz));
                    g.setColour(t.col.withAlpha(0.38f));
                    g.drawVerticalLine(juce::roundToInt(x), (float) curve.getY() + 13, (float) curve.getBottom());
                    g.setFont(8.2f);
                    g.setColour(t.col.withAlpha(0.92f));
                    g.drawText(t.tag, juce::roundToInt(x) - 10, curve.getY() + 1, 20, 11, juce::Justification::centred);

                    if (motionSnap != nullptr && motionSnap->motionEngaged && hzR >= (float) fLo && hzR <= (float) fHi
                        && std::abs(hzR - hz) > juce::jmax(0.5f, hz * 2.0e-5f))
                    {
                        const float xR = xOfFDec(static_cast<double>(hzR));
                        juce::Path pv;
                        pv.startNewSubPath(xR, (float) curve.getY() + 13.f);
                        pv.lineTo(xR, (float) curve.getBottom());
                        juce::Path dashed;
                        const float dashPat[] = { 4.f, 4.f };
                        juce::PathStrokeType pst(1.1f);
                        pst.createDashedStroke(dashed, pv, dashPat, 2);
                        g.setColour(t.col.withAlpha(0.4f));
                        g.strokePath(dashed, pst);
                        g.setFont(7.0f);
                        g.setColour(t.col.withAlpha(0.75f));
                        g.drawText("R", juce::roundToInt(xR) - 5, curve.getY() + 1, 10, 9, juce::Justification::centred);
                    }
                }
            }

            const float y0 = yOfDbDec(0.f);
            g.setColour(juce::Colour(0xff777777));
            for (float x = (float) curve.getX(); x < (float) curve.getRight(); x += 10.f)
                g.drawLine(x, y0, juce::jmin(x + 5.5f, (float) curve.getRight()), y0, 1.25f);

            const juce::String topDbLab = (dMax > 0.001f ? juce::String("+") : juce::String())
                                          + juce::String(static_cast<int>(dMax)) + " dB";
            const juce::String midDbLab = "0 dB";
            const juce::String botDbLab = juce::String(static_cast<int>(dMin)) + " dB";

            auto yForLabel = [&](float db) -> int
            {
                const int y = juce::roundToInt(yOfDbDec(db) - 5);
                const int yMin = curve.getY() + 1;
                const int yMax = curve.getBottom() - 11;
                return juce::jlimit(yMin, yMax, y);
            };

            if (ruler.getWidth() > 0 && scaleRight.getWidth() > 0)
            {
                g.setFont(8.2f);
                g.setColour(kTextBright.withAlpha(0.9f));
                g.drawText(topDbLab, ruler.getX() + 2, yForLabel(dMax), ruler.getWidth() - 4, 11, juce::Justification::right);
                g.drawText(midDbLab, ruler.getX() + 2, yForLabel(0.f), ruler.getWidth() - 4, 11, juce::Justification::right);
                g.drawText(botDbLab, ruler.getX() + 2, yForLabel(dMin), ruler.getWidth() - 4, 11, juce::Justification::right);

                g.setFont(8.2f);
                g.setColour(kTextMuted);
                g.drawText(topDbLab, scaleRight.getX(), yForLabel(dMax), scaleRight.getWidth() - 2, 11, juce::Justification::left);
                g.drawText(midDbLab, scaleRight.getX(), yForLabel(0.f), scaleRight.getWidth() - 2, 11, juce::Justification::left);
                g.drawText(botDbLab, scaleRight.getX(), yForLabel(dMin), scaleRight.getWidth() - 2, 11, juce::Justification::left);
            }
        }

        auto xOfF = [&](double f) -> float
        {
            f = juce::jlimit(fLo, fHi, f);
            const double lf = std::log(f);
            return (float) (curve.getX() + (lf - logLo) / (logHi - logLo) * (double) curve.getWidth());
        };

        auto yOfDb = [&](float db) -> float
        {
            const float n = juce::jlimit(dMin, dMax, db);
            const float norm = (n - dMin) / (dMax - dMin);
            return (float) curve.getBottom() - norm * (float) curve.getHeight();
        };

        juce::Path path;
        bool started = false;
        float prevDbg = 0.f;
        constexpr float kMagDbJumpBreak = 38.f;
        for (int i = 0; i < nPts; ++i)
        {
            float dbg = magDb[(size_t) i];
            if (!std::isfinite(dbg))
                dbg = 0.f;
            dbg = juce::jlimit(dMin, dMax, dbg);
            const float x = xOfF(freqHz[(size_t) i]);
            const float y = yOfDb(dbg);
            if (!started)
            {
                path.startNewSubPath(x, y);
                started = true;
            }
            else if (std::abs(dbg - prevDbg) > kMagDbJumpBreak)
            {
                path.startNewSubPath(x, y);
            }
            else
            {
                path.lineTo(x, y);
            }
            prevDbg = dbg;
        }
        g.setColour(curveStrokeColour.withAlpha(0.95f));
        {
            juce::Graphics::ScopedSaveState clipState(g);
            g.reduceClipRegion(curve);
            g.strokePath(path, juce::PathStrokeType(2.2f));
        }
    }

    /** One panel: FFT spectrum (pre / post chain, left ch) as a semi-transparent layer, then +/-30 dB EQ IIR (+ mint) on top - same as unified Curve view. */
    void paintMergedSpectrumAndEqInRect(juce::Graphics& g,
                                        juce::Rectangle<int> fullArea,
                                        ParaEQ301AudioProcessor& proc,
                                        std::vector<double>& freqHz,
                                        std::vector<float>& magScratch,
                                        std::vector<float>& eqMagSmoothed,
                                        std::vector<float>& comboMagSmoothed,
                                        std::vector<float>& specBefore,
                                        std::vector<float>& specAfter)
    {
        if (fullArea.getHeight() < 48 || fullArea.getWidth() < 120)
            return;

        constexpr int nPts = ParaEQ301AudioProcessor::kEqCurvePlotPoints;
        if ((int) freqHz.size() != nPts)
        {
            freqHz.resize((size_t) nPts);
            magScratch.resize((size_t) nPts);
            eqMagSmoothed.resize((size_t) nPts);
            comboMagSmoothed.resize((size_t) nPts);
            specBefore.resize((size_t) nPts);
            specAfter.resize((size_t) nPts);
        }

        const double sr = proc.getSampleRate() > 0.0 ? proc.getSampleRate() : 44100.0;
        const double fLo = 30.0;
        const double fHi = juce::jmin(20000.0, sr * 0.48);
        const double logLo = std::log(fLo);
        const double logHi = std::log(fHi);
        for (int i = 0; i < nPts; ++i)
        {
            const double t = (double) i / (double) (nPts - 1);
            freqHz[(size_t) i] = std::exp(logLo + t * (logHi - logLo));
        }

        proc.getSpectrumBeforeAfterDb(sr, freqHz.data(), nPts, specBefore.data(), specAfter.data());

        proc.getEqChainMagnitudeDb(sr, freqHz.data(), magScratch.data(), nPts);
        for (int i = 0; i < nPts; ++i)
        {
            float v = magScratch[(size_t) i];
            if (!std::isfinite(v))
                v = 0.f;
            magScratch[(size_t) i] = v;
        }
        for (int i = 0; i < nPts; ++i)
        {
            const int im = juce::jmax(0, i - 1);
            const int ip = juce::jmin(nPts - 1, i + 1);
            eqMagSmoothed[(size_t) i] = 0.25f * magScratch[(size_t) im] + 0.5f * magScratch[(size_t) i] + 0.25f * magScratch[(size_t) ip];
        }

        auto& apCurve = proc.getAPVTS();
        const bool linearEqListen = apCurve.getRawParameterValue("linearEqListen")->load() > 0.5f;
        const bool bankTheory = apCurve.getRawParameterValue("anharmBankEnable")->load() > 0.5f
                                 && apCurve.getRawParameterValue("anharmMix")->load() > 1.0e-6f;
        const bool svfTheory = apCurve.getRawParameterValue("svfEnable")->load() > 0.5f
                                 && apCurve.getRawParameterValue("svfMix")->load() > 1.0e-6f;
        const bool showMintTheory = !linearEqListen && (bankTheory || svfTheory);
        if (showMintTheory)
        {
            proc.getEqChainPlusAnharmLinearDb(sr, freqHz.data(), magScratch.data(), nPts);
            for (int i = 0; i < nPts; ++i)
            {
                float v = magScratch[(size_t) i];
                if (!std::isfinite(v))
                    v = 0.f;
                magScratch[(size_t) i] = v;
            }
            for (int i = 0; i < nPts; ++i)
            {
                const int im = juce::jmax(0, i - 1);
                const int ip = juce::jmin(nPts - 1, i + 1);
                comboMagSmoothed[(size_t) i] = 0.25f * magScratch[(size_t) im] + 0.5f * magScratch[(size_t) i] + 0.25f * magScratch[(size_t) ip];
            }
        }

        auto plot = fullArea.reduced(0, 2);
        plot.removeFromBottom(11);
        if (plot.getHeight() < 36)
            return;

        juce::Rectangle<int> graph = plot;
        graph.removeFromBottom(2);
        g.setColour(juce::Colour(0xff1c1c1c));
        g.fillRoundedRectangle(graph.toFloat(), 4.f);
        g.setColour(juce::Colour(0xff353535));
        g.drawRoundedRectangle(graph.toFloat(), 4.f, 1.f);

        juce::Rectangle<int> freqGraph = graph;
        freqGraph.removeFromLeft(kEqMagFreqAxisInsetL);
        freqGraph.removeFromRight(kEqMagFreqAxisInsetR);

        float peak = -200.f;
        for (int i = 0; i < nPts; ++i)
            peak = juce::jmax(peak, specBefore[(size_t) i], specAfter[(size_t) i]);
        if (!std::isfinite(peak))
            peak = -100.f;
        // Do not cap top at 0 dB - strong EQ / resonant peaks can read above 0 in this FFT view; without headroom
        // they pin to the top of the strip ("clip mode"). Keep air above the trace so boosts stay readable.
        constexpr float kSpectrumTopHeadroomDb = 10.f;
        constexpr float kSpectrumSpanDb = 64.f;
        const float topDb = peak + kSpectrumTopHeadroomDb;
        const float botDb = topDb - kSpectrumSpanDb;
        const float yBottom = (float) freqGraph.getBottom();
        const float yTopPad = (float) freqGraph.getY() + 2.f;
        const float yBotPad = yBottom - 3.f;
        auto ySpec = [&](float db) -> float
        {
            const float t = (db - botDb) / juce::jmax(1.0e-3f, topDb - botDb);
            const float yn = yBottom - juce::jlimit(0.f, 1.f, t) * (float) freqGraph.getHeight();
            return juce::jlimit(yTopPad, yBotPad, yn);
        };

        auto xOfF = [&](double f) -> float
        {
            f = juce::jlimit(fLo, fHi, f);
            const double lf = std::log(f);
            return (float) (freqGraph.getX() + (lf - logLo) / (logHi - logLo) * (double) freqGraph.getWidth());
        };

        {
            juce::Graphics::ScopedSaveState specLayer(g);
            g.reduceClipRegion(freqGraph);
            g.setOpacity(0.52f);

            juce::Path fillB;
            bool st = false;
            for (int i = 0; i < nPts; ++i)
            {
                const float x = xOfF(freqHz[(size_t) i]);
                const float y = ySpec(specBefore[(size_t) i]);
                if (!st)
                {
                    fillB.startNewSubPath(x, yBottom);
                    fillB.lineTo(x, y);
                    st = true;
                }
                else
                {
                    fillB.lineTo(x, y);
                }
            }
            fillB.lineTo(xOfF(freqHz[(size_t) (nPts - 1)]), yBottom);
            fillB.closeSubPath();
            g.setColour(kTextBright.withAlpha(0.14f));
            g.fillPath(fillB);

            juce::Path lineB;
            st = false;
            for (int i = 0; i < nPts; ++i)
            {
                const float x = xOfF(freqHz[(size_t) i]);
                const float y = ySpec(specBefore[(size_t) i]);
                if (!st)
                {
                    lineB.startNewSubPath(x, y);
                    st = true;
                }
                else
                {
                    lineB.lineTo(x, y);
                }
            }
            g.setColour(kTextBright.withAlpha(0.88f));
            g.strokePath(lineB, juce::PathStrokeType(1.35f));

            juce::Path lineA;
            st = false;
            for (int i = 0; i < nPts; ++i)
            {
                const float x = xOfF(freqHz[(size_t) i]);
                const float y = ySpec(specAfter[(size_t) i]);
                if (!st)
                {
                    lineA.startNewSubPath(x, y);
                    st = true;
                }
                else
                {
                    lineA.lineTo(x, y);
                }
            }
            g.setColour(kAccentBlue.withAlpha(0.95f));
            g.strokePath(lineA, juce::PathStrokeType(1.85f));
        }

        juce::Rectangle<int> freqBand = graph;
        freqBand.removeFromLeft(kEqMagFreqAxisInsetL);
        freqBand.removeFromRight(kEqMagFreqAxisInsetR);

        const juce::Colour kAnharmTheoryMint(0xff5ed4b6);
        ParaEQ301AudioProcessor::MotionEffectiveEqSnapshot curveSnap;
        proc.getMotionEffectiveEqSnapshot(curveSnap);
        if (apCurve.getRawParameterValue("anharmBankEnable")->load() > 0.5f && !linearEqListen)
        {
            const float f0 = apCurve.getRawParameterValue("anharmFundHz")->load();
            const double B = (double) apCurve.getRawParameterValue("anharmInharmB")->load();
            int nPartDraw = 4;
            if (auto* pip = dynamic_cast<juce::AudioParameterInt*>(apCurve.getParameter("anharmPartials")))
                nPartDraw = juce::jlimit(2, ParaEQ301AudioProcessor::kAnharmMaxPartials, pip->get());
            for (int p = 1; p <= nPartDraw; ++p)
            {
                const double fn = stiffStringPartialHz((double) f0, p, B);
                if (fn < fLo || fn > fHi)
                    continue;
                const double lfn = std::log(fn);
                const float xv = (float) (freqBand.getX() + (lfn - logLo) / (logHi - logLo) * (double) freqBand.getWidth());
                const float al = (p <= 2) ? 0.26f : juce::jmax(0.09f, 0.22f - 0.028f * (float) (p - 2));
                g.setColour(kAnharmTheoryMint.withAlpha(al));
                g.drawVerticalLine(juce::roundToInt(xv), (float) graph.getY() + 2.f, (float) graph.getBottom());
            }
        }
        if (svfTheory)
        {
            const float svc = apCurve.getRawParameterValue("svfCf")->load();
            if (svc >= (float) fLo && svc <= (float) fHi)
            {
                const double lfs = std::log((double) svc);
                const float xv = (float) (freqBand.getX() + (lfs - logLo) / (logHi - logLo) * (double) freqBand.getWidth());
                g.setColour(juce::Colour(0xff5dade2).withAlpha(0.3f));
                g.drawVerticalLine(juce::roundToInt(xv), (float) graph.getY() + 2.f, (float) graph.getBottom());
            }
        }

        paintEqMagnitudeCurveInRect(g, graph, freqHz, eqMagSmoothed.data(), nPts, kEqMagPlotDbMin, kEqMagPlotDbMax, fLo, fHi,
                                    &proc.getAPVTS(), &curveSnap, kAccentGreen, false);
        if (showMintTheory)
            paintEqMagnitudeCurveInRect(g, graph, freqHz, comboMagSmoothed.data(), nPts, kEqMagPlotDbMin, kEqMagPlotDbMax, fLo, fHi,
                                        nullptr, nullptr, kAnharmTheoryMint, false, kEqMagFreqAxisInsetL, kEqMagFreqAxisInsetR, false);

        g.setFont(7.8f);
        g.setColour(kTextMuted.withAlpha(0.88f));
        juce::String leg = "Spectrum (white pre-EQ / blue post) + 4-band IIR";
        if (showMintTheory)
            leg += "   Mint: linear (SVF0 + bank)";
        g.drawText(leg, graph.getX(), graph.getBottom() + 1, graph.getWidth(), 11, juce::Justification::centred);
    }
}

struct ParaEQ301AudioProcessorEditor::LfoTabContent : public juce::Component, private juce::Timer
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

    ParaEQ301AudioProcessor& proc;
    juce::Label stereoLabel;
    juce::Slider stereo;
    Row hi, m1, m2, lo;
    juce::Rectangle<int> lfoDotHi, lfoDotM1, lfoDotM2, lfoDotLo;
    juce::Rectangle<int> motionLfoPaintRect[4];
    /** True: EQ tab places each band row beside its LFO row (same y). False: stacked strip below EQ. */
    bool motionLayoutInterleaved = false;

    static void sk(juce::Slider& s)
    {
        s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, kTextBoxH - 2);
        styleSliderDark(s, juce::Colour(0xff4a8ad4));
    }

    LfoTabContent(ParaEQ301AudioProcessor& processor,
                  juce::AudioProcessorValueTreeState& ap,
                  std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>>& atts)
        : proc(processor)
    {
        stereoLabel.setText("L/R LFO phase", juce::dontSendNotification);
        stereoLabel.setJustificationType(juce::Justification::centredLeft);
        stereoLabel.setColour(juce::Label::textColourId, kTextBright);
        stereoLabel.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
        addAndMakeVisible(stereoLabel);
        sk(stereo);
        addAndMakeVisible(stereo);
        atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "lfoStereoPhase", stereo));
        stereo.textFromValueFunction = [](double v) { return juce::String(juce::roundToInt(v)) + " deg"; };
        stereo.setTooltip("Phase offset (degrees) added to every band LFO sine on the **right** channel only. "
                          "Left channel tags on the EQ / Curve magnitude plots stay fixed to L; when R differs, dashed lines + small R mark the right channel peak/shelf Hz.");

        auto setupRow = [&](Row& r, const juce::String& name, bool bw,
                            const char* rateId, const char* dg, const char* dc, const char* dbw,
                            const juce::String& rowTargetsExplain)
        {
            r.title.setText(name, juce::dontSendNotification);
            r.title.setJustificationType(juce::Justification::centredRight);
            r.title.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
            r.title.setColour(juce::Label::textColourId, kTextBright);
            r.title.setTooltip(rowTargetsExplain);
            addAndMakeVisible(r.title);
            r.useBw = bw;

            sk(r.rate);
            styleMotionCaption(r.rateL, "LFO Hz");
            addAndMakeVisible(r.rate);
            addAndMakeVisible(r.rateL);
            atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, rateId, r.rate));
            r.rate.setTooltip("LFO speed in Hz. 0 Hz = frozen phase (no sweep). With depth % up, higher Hz = faster motion.");

            sk(r.dGain);
            styleMotionCaption(r.dGainL, "EQ gain");
            addAndMakeVisible(r.dGain);
            addAndMakeVisible(r.dGainL);
            atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, dg, r.dGain));
            r.dGain.setTooltip("How strong the LFO wobbles this band's EQ gain knob. 100% = full +/-12 dB swing around the EQ tab. "
                               "Not output volume - only the band boost/cut is animated.");

            sk(r.dCf);
            styleMotionCaption(r.dCfL, "EQ freq");
            addAndMakeVisible(r.dCf);
            addAndMakeVisible(r.dCfL);
            atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, dc, r.dCf));
            r.dCf.setTooltip("How much the LFO pushes this band's shelf corner or peak frequency around the Hz on the EQ tab.");

            auto pctNum = [](double v) { return juce::String(juce::roundToInt(v * 100.0)); };
            auto pctIn = [](const juce::String& t) { return juce::jlimit(0.0, 1.0, t.getDoubleValue() / 100.0); };
            r.dGain.setTextValueSuffix(" %");
            r.dCf.setTextValueSuffix(" %");
            r.dGain.textFromValueFunction = pctNum;
            r.dGain.valueFromTextFunction = pctIn;
            r.dCf.textFromValueFunction = pctNum;
            r.dCf.valueFromTextFunction = pctIn;

            if (bw)
            {
                sk(r.dBw);
                styleMotionCaption(r.dBwL, "EQ width");
                addAndMakeVisible(r.dBw);
                addAndMakeVisible(r.dBwL);
                atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, dbw, r.dBw));
                r.dBw.setTextValueSuffix(" %");
                r.dBw.textFromValueFunction = pctNum;
                r.dBw.valueFromTextFunction = pctIn;
                r.dBw.setTooltip("How much the LFO wobbles the mid peak's bandwidth (Width Hz on EQ). Hi/Lo shelves have no width control.");
            }
        };

        setupRow(hi, "Hi", false, "lfoHiRate", "lfoHiDepthGain", "lfoHiDepthCf", "",
                 "Motion for EQ Hi shelf: Gain dB and Shelf Hz.");
        setupRow(m1, "M1", true, "lfoM1Rate", "lfoM1DepthGain", "lfoM1DepthCf", "lfoM1DepthBw",
                 "Motion for EQ Mid1: Gain, Peak Hz, Width Hz.");
        setupRow(m2, "M2", true, "lfoM2Rate", "lfoM2DepthGain", "lfoM2DepthCf", "lfoM2DepthBw",
                 "Motion for EQ Mid2: Gain, Peak Hz, Width Hz.");
        setupRow(lo, "Lo", false, "lfoLoRate", "lfoLoDepthGain", "lfoLoDepthCf", "",
                 "Motion for EQ Low shelf: Gain dB and Shelf Hz.");

        startTimerHz(24);
    }

    ~LfoTabContent() override { stopTimer(); }

    void timerCallback() override { repaint(); }

    void placeRow(Row& r, juce::Rectangle<int> rowArea, juce::Rectangle<int>& lfoDotOut)
    {
        const int phaseS = 14;
        const int colGap = 6;
        const int numCols = r.useBw ? 4 : 3;

        int kw, colH, tbH;
        int titleW;
        if (motionLayoutInterleaved)
        {
            kw = kEqSliderColW;
            colH = kEqSliderColumnH;
            tbH = kEqTextBoxH - 1;
            titleW = 26;
        }
        else
        {
            titleW = rowArea.getWidth() >= 320 ? 30 : 22;
            const int head = phaseS + 4 + titleW + colGap;
            const int inner = juce::jmax(0, rowArea.getWidth() - head - 2);
            kw = numCols > 0 ? (inner - numCols * colGap) / numCols : 26;
            kw = juce::jlimit(26, 56, kw);
            colH = kSliderColumnH;
            tbH = kTextBoxH - 2;
        }

        auto a = rowArea;
        lfoDotOut = { a.getX() + 1, a.getY() + 8, phaseS, phaseS };
        r.title.setBounds(a.getX() + phaseS + 4, a.getY() + 10, titleW, a.getHeight() - 12);
        int x = a.getX() + phaseS + 4 + titleW + colGap;

        auto placeCol = [&](juce::Slider& sl, juce::Label& cap)
        {
            const int tbW = motionLayoutInterleaved ? kEqTextBoxW : juce::jmax(26, kw - 2);
            sl.setBounds(x, rowArea.getY(), kw, colH);
            sl.setTextBoxStyle(juce::Slider::TextBoxBelow, false, tbW, tbH);
            cap.setBounds(x, sl.getBottom() + kGapCaption, kw, kCaptionH);
            x += kw + colGap;
        };

        placeCol(r.rate, r.rateL);
        placeCol(r.dGain, r.dGainL);
        placeCol(r.dCf, r.dCfL);
        if (r.useBw)
            placeCol(r.dBw, r.dBwL);
    }

    void setMotionLayoutInterleaved(bool interleaved) noexcept
    {
        motionLayoutInterleaved = interleaved;
        resized();
    }

    void resized() override
    {
        if (motionLayoutInterleaved)
        {
            // EQ tab: one horizontal strip per band - no outer margin here so LFO sits flush after EQ knobs.
            auto b = getLocalBounds();
            Row* rows[4] = { &lo, &m1, &m2, &hi };
            juce::Rectangle<int>* dots[4] = { &lfoDotLo, &lfoDotM1, &lfoDotM2, &lfoDotHi };
            for (int i = 0; i < 4; ++i)
            {
                auto rowR = b.removeFromTop(kEqRowHeight);
                motionLfoPaintRect[(size_t) i] = rowR;
                placeRow(*rows[(size_t) i], rowR, *dots[(size_t) i]);
            }
            constexpr int stereoLabW = 112;
            const auto& row0 = motionLfoPaintRect[0];
            auto stereoRow = row0.reduced(4, 0);
            const int knobY = row0.getY();
            const int knobH = kEqSliderColumnH;
            const int stw = juce::jmin(kKnobSize, juce::jmax(kEqSliderColW - 8, (stereoRow.getWidth() - 8) / 5));
            stereo.setBounds(stereoRow.getRight() - stw, knobY, stw, knobH);
            const int labRight = stereo.getX() - 6;
            const int labW = juce::jmin(stereoLabW, juce::jmax(40, labRight - stereoRow.getX()));
            stereoLabel.setBounds(stereoRow.getX(), knobY, labW, knobH);
            stereoLabel.setJustificationType(juce::Justification::centredLeft);
            return;
        }

        auto b = getLocalBounds().reduced(8);
        constexpr int kRightPanelMinW = 148;
        const int bandsW = juce::jmin(kLfoMotionInterleavedMinW(), juce::jmax(200, b.getWidth() - kRightPanelMinW));
        const int motionBodyH = juce::jmax(4 * kRowHeight, b.getHeight());
        auto motionBody = b.removeFromTop(motionBodyH);
        auto bandsRect = motionBody.removeFromLeft(bandsW);
        auto rightPanel = motionBody;

        motionLfoPaintRect[0] = bandsRect.withHeight(kRowHeight);
        placeRow(lo, bandsRect.removeFromTop(kRowHeight), lfoDotLo);
        motionLfoPaintRect[1] = bandsRect.withHeight(kRowHeight);
        placeRow(m1, bandsRect.removeFromTop(kRowHeight), lfoDotM1);
        motionLfoPaintRect[2] = bandsRect.withHeight(kRowHeight);
        placeRow(m2, bandsRect.removeFromTop(kRowHeight), lfoDotM2);
        motionLfoPaintRect[3] = bandsRect.withHeight(kRowHeight);
        placeRow(hi, bandsRect.removeFromTop(kRowHeight), lfoDotHi);

        constexpr int stereoLabW = 112;
        const int bodyY = rightPanel.getY();
        auto stereoRow = juce::Rectangle<int>(rightPanel.getX(), bodyY, rightPanel.getWidth(), kRowHeight).reduced(4, 0);
        const int knobY = bodyY;
        const int knobH = kSliderColumnH;
        stereo.setBounds(stereoRow.getRight() - kKnobSize, knobY, kKnobSize, knobH);
        const int labRight = stereo.getX() - 6;
        const int labW = juce::jmin(stereoLabW, juce::jmax(40, labRight - stereoRow.getX()));
        stereoLabel.setBounds(stereoRow.getX(), knobY, labW, knobH);
        stereoLabel.setJustificationType(juce::Justification::centredLeft);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(kPanelBlack);
        auto& ap = proc.getAPVTS();
        for (int r = 0; r < 4; ++r)
        {
            if (!motionLfoPaintRect[(size_t) r].isEmpty() && motionLfoDepthActive(ap))
            {
                const bool bandOn = eqBandHasMotion(ap, r);
                g.setColour(kAccentBlue.withAlpha(0.09f));
                g.fillRoundedRectangle(motionLfoPaintRect[(size_t) r].toFloat().reduced(1), 5.f);
                g.setColour(kAccentBlue.withAlpha(bandOn ? 0.28f : 0.16f));
                g.drawRoundedRectangle(motionLfoPaintRect[(size_t) r].toFloat().reduced(1), 5.f, 1.f);
            }
        }
        float ph[4];
        proc.getMotionLfoPhases(ph);
        const float dHiG = ap.getRawParameterValue("lfoHiDepthGain")->load();
        const float dHiC = ap.getRawParameterValue("lfoHiDepthCf")->load();
        const float dM1G = ap.getRawParameterValue("lfoM1DepthGain")->load();
        const float dM1C = ap.getRawParameterValue("lfoM1DepthCf")->load();
        const float dM1B = ap.getRawParameterValue("lfoM1DepthBw")->load();
        const float dM2G = ap.getRawParameterValue("lfoM2DepthGain")->load();
        const float dM2C = ap.getRawParameterValue("lfoM2DepthCf")->load();
        const float dM2B = ap.getRawParameterValue("lfoM2DepthBw")->load();
        const float dLoG = ap.getRawParameterValue("lfoLoDepthGain")->load();
        const float dLoC = ap.getRawParameterValue("lfoLoDepthCf")->load();
        const bool anyMotion = (dHiG + dHiC + dM1G + dM1C + dM1B + dM2G + dM2C + dM2B + dLoG + dLoC) > 1.0e-6f;

        auto bandBright = [&](float sumDepth) -> float
        {
            if (!anyMotion)
                return 0.15f;
            if (sumDepth <= 1.0e-6f)
                return 0.4f;
            return 1.f;
        };

        paintMotionLfoDot(g, lfoDotHi, ph[0], bandBright(dHiG + dHiC), kAccentBlue);
        paintMotionLfoDot(g, lfoDotM1, ph[1], bandBright(dM1G + dM1C + dM1B), kAccentBlue);
        paintMotionLfoDot(g, lfoDotM2, ph[2], bandBright(dM2G + dM2C + dM2B), kAccentBlue);
        paintMotionLfoDot(g, lfoDotLo, ph[3], bandBright(dLoG + dLoC), kAccentBlue);
    }
};

struct ParaEQ301AudioProcessorEditor::EqTabContent : public juce::Component,
                                                    private juce::Timer,
                                                    private juce::Slider::Listener
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
    juce::ToggleButton core1BypassToggle { "Bypass Saturator 1" };
    juce::Slider coreSat;
    juce::Label coreSatLabel;
    juce::ToggleButton core2BypassToggle { "Bypass Saturator 2" };
    juce::Slider core2Sat;
    juce::Label core2SatLabel;
    juce::Label thrill1Title;
    juce::Slider thrill1Spec;
    juce::Label thrill1SpecLabel;
    juce::Slider thrill1Thr;
    juce::Label thrill1ThrLabel;
    juce::Slider thrill1Ratio;
    juce::Label thrill1RatioLabel;
    juce::Label thrill2Title;
    juce::Slider thrill2Spec;
    juce::Label thrill2SpecLabel;
    juce::Slider thrill2Thr;
    juce::Label thrill2ThrLabel;
    juce::Slider thrill2Ratio;
    juce::Label thrill2RatioLabel;
    juce::Slider coreDirt;
    juce::Label coreDirtLabel;
    juce::Slider coreLifeDepth;
    juce::Label coreLifeDepthLabel;
    juce::Slider coreLifeHz;
    juce::Label coreLifeHzLabel;
    juce::Label motionStatus;
    juce::TextButton motionInfoBtn;
    juce::ToggleButton lfoHostSyncToggle { "BPM sync" };
    juce::ComboBox lfoHostSyncDivBox;
    juce::ToggleButton eqPinkBalToggle { "EQ level balance" };
    TooltipMouseProxy motionOverviewHitEq;
    std::unique_ptr<LfoTabContent> lfoStrip;

    ParaEQ301AudioProcessor& proc;
    juce::Rectangle<int> eqGraphBounds;
    juce::Rectangle<int> motionRowRect[4];
    std::vector<double> freqScratch;
    std::vector<float> magScratch;
    std::vector<float> graphEqMagSmoothed;
    std::vector<float> graphComboMagSmoothed;
    std::vector<float> graphSpecBefore;
    std::vector<float> graphSpecAfter;
    TooltipMouseProxy eqGraphTooltip;

    EqBandLookAndFeel eqBandLookAndFeel;

    int eqSliderGestureDepth = 0;

    bool eqTextBoxHasKeyboardFocus() const
    {
        auto* fc = juce::Component::getCurrentlyFocusedComponent();
        if (fc == nullptr)
            return false;
        auto* ps = fc->findParentComponentOfClass<juce::Slider>();
        return ps == &hi.cf || ps == &hi.gain || ps == &mid1.cf || ps == &mid1.bw || ps == &mid1.gain
            || ps == &mid2.cf || ps == &mid2.bw || ps == &mid2.gain || ps == &low.cf || ps == &low.gain;
    }

    void snapEqSliderToStoredParameter(juce::Slider* s)
    {
        auto& ap = proc.getAPVTS();
        float raw = 0.f;
        if (s == &hi.cf)
            raw = ap.getRawParameterValue("hiCf")->load();
        else if (s == &hi.gain)
            raw = ap.getRawParameterValue("hiGain")->load();
        else if (s == &mid1.cf)
            raw = ap.getRawParameterValue("mid1Cf")->load();
        else if (s == &mid1.bw)
            raw = ap.getRawParameterValue("mid1Bw")->load();
        else if (s == &mid1.gain)
            raw = ap.getRawParameterValue("mid1Gain")->load();
        else if (s == &mid2.cf)
            raw = ap.getRawParameterValue("mid2Cf")->load();
        else if (s == &mid2.bw)
            raw = ap.getRawParameterValue("mid2Bw")->load();
        else if (s == &mid2.gain)
            raw = ap.getRawParameterValue("mid2Gain")->load();
        else if (s == &low.cf)
            raw = ap.getRawParameterValue("lowCf")->load();
        else if (s == &low.gain)
            raw = ap.getRawParameterValue("lowGain")->load();
        else
            return;

        s->setValue((double) raw, juce::dontSendNotification);
    }

    void clearEqKnobLiveDisplayProps()
    {
        for (auto* s : {&hi.cf, &hi.gain, &mid1.cf, &mid1.bw, &mid1.gain, &mid2.cf, &mid2.bw, &mid2.gain, &low.cf, &low.gain})
            s->getProperties().remove(peqLiveDisplayId);
    }

    void setEqKnobLiveDisplayFromSnapshot(const ParaEQ301AudioProcessor::MotionEffectiveEqSnapshot& s)
    {
        hi.cf.getProperties().set(peqLiveDisplayId, (double) s.hiCfHz);
        hi.gain.getProperties().set(peqLiveDisplayId, (double) s.hiGainDb);
        mid1.cf.getProperties().set(peqLiveDisplayId, (double) s.mid1CfHz);
        mid1.bw.getProperties().set(peqLiveDisplayId, (double) s.mid1BwHz);
        mid1.gain.getProperties().set(peqLiveDisplayId, (double) s.mid1GainDb);
        mid2.cf.getProperties().set(peqLiveDisplayId, (double) s.mid2CfHz);
        mid2.bw.getProperties().set(peqLiveDisplayId, (double) s.mid2BwHz);
        mid2.gain.getProperties().set(peqLiveDisplayId, (double) s.mid2GainDb);
        low.cf.getProperties().set(peqLiveDisplayId, (double) s.loCfHz);
        low.gain.getProperties().set(peqLiveDisplayId, (double) s.loGainDb);
    }

    void repaintEqKnobs()
    {
        for (auto* s : {&hi.cf, &hi.gain, &mid1.cf, &mid1.bw, &mid1.gain, &mid2.cf, &mid2.bw, &mid2.gain, &low.cf, &low.gain})
            s->repaint();
    }

    void sliderValueChanged(juce::Slider*) override {}

    void sliderDragStarted(juce::Slider* slider) override
    {
        snapEqSliderToStoredParameter(slider);
        slider->getProperties().remove(peqLiveDisplayId);
        slider->repaint();
        ++eqSliderGestureDepth;
    }

    void sliderDragEnded(juce::Slider*) override
    {
        eqSliderGestureDepth = juce::jmax(0, eqSliderGestureDepth - 1);
    }

    static void styleEqBandKnob(juce::Slider& s) { styleEqSlider(s, kAccentGreen); }

    static void wireEqBandSlider(juce::Slider& s, EqBandLookAndFeel& lf)
    {
        s.getProperties().set("peqEqBox", true);
        s.setLookAndFeel(&lf);
        styleEqBandKnob(s);
    }

    static void styleLabel(juce::Label& l, const juce::String& text) { styleLabelDark(l, text, true); }

    EqTabContent(ParaEQ301AudioProcessor& processor,
                 juce::AudioProcessorValueTreeState& ap,
                 std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>>& atts,
                 std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>>& batts,
                 std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>>& comboAtts)
        : proc(processor)
    {
        hi.hasBw = false;
        hi.hasCfInLeftColumn = false;
        mid1.hasBw = true;
        mid1.hasCfInLeftColumn = true;
        mid2.hasBw = true;
        mid2.hasCfInLeftColumn = true;
        low.hasBw = false;
        low.hasCfInLeftColumn = false;

        hi.bandLabel.setText("Hi\nhigh shelf", juce::dontSendNotification);
        mid1.bandLabel.setText("Mid1\npeaking", juce::dontSendNotification);
        mid2.bandLabel.setText("Mid2\npeaking", juce::dontSendNotification);
        low.bandLabel.setText("Low\nlow shelf", juce::dontSendNotification);

        for (auto* band : {&hi, &mid1, &mid2, &low})
        {
            addAndMakeVisible(band->bandLabel);
            band->bandLabel.setJustificationType(juce::Justification::centredRight);
            band->bandLabel.setFont(juce::Font(juce::FontOptions(10.2f, juce::Font::bold)));
            band->bandLabel.setColour(juce::Label::textColourId, kTextBright);
            band->bandLabel.setPaintingIsUnclipped(false);

            wireEqBandSlider(band->cf, eqBandLookAndFeel);
            addAndMakeVisible(band->cf);
            addAndMakeVisible(band->cfLabel);

            if (band->hasBw)
            {
                wireEqBandSlider(band->bw, eqBandLookAndFeel);
                addAndMakeVisible(band->bw);
                addAndMakeVisible(band->bwLabel);
            }

            wireEqBandSlider(band->gain, eqBandLookAndFeel);
            addAndMakeVisible(band->gain);
            addAndMakeVisible(band->gainLabel);
        }

        styleLabel(hi.cfLabel, "Shelf Hz");
        hi.bandLabel.setTooltip("High shelf: boosts or cuts treble. It is not a high-pass filter - low frequencies still pass. \"Shelf Hz\" is the turnover: far above it the curve reaches the Gain value.");
        hi.cf.setTooltip("High shelf turnover frequency (Hz). Energy well above this frequency is tilted toward the Gain dB setting; below it the response flattens back toward 0 dB change (classic shelving EQ).");
        styleLabel(hi.gainLabel, "Gain dB");
        hi.gain.setTooltip("How much boost or cut applies in the treble region (0 dB = no change). Negative = gentle high cut.");

        styleLabel(mid1.cfLabel, "Peak Hz");
        styleLabel(mid1.bwLabel, "Width Hz");
        styleLabel(mid1.gainLabel, "Gain dB");
        mid1.bandLabel.setTooltip("Peaking (parametric) band: bell-shaped boost or cut centred at Peak Hz.");
        mid1.cf.setTooltip("Centre frequency of the first mid peaking EQ.");
        mid1.bw.setTooltip("Bandwidth of the peak in Hz (smaller = narrower notch/boost).");
        mid1.gain.setTooltip("Gain at the peak centre in dB.");

        styleLabel(mid2.cfLabel, "Peak Hz");
        styleLabel(mid2.bwLabel, "Width Hz");
        styleLabel(mid2.gainLabel, "Gain dB");
        mid2.bandLabel.setTooltip("Second peaking band: same idea as Mid1, different frequency and width.");
        mid2.cf.setTooltip("Centre frequency of the second mid peaking EQ.");
        mid2.bw.setTooltip("Bandwidth of the peak in Hz.");
        mid2.gain.setTooltip("Gain at the peak centre in dB.");

        styleLabel(low.cfLabel, "Shelf Hz");
        styleLabel(low.gainLabel, "Gain dB");
        low.bandLabel.setTooltip("Low shelf: boosts or cuts bass. It is not a low-pass filter - highs still pass. \"Shelf Hz\" is the turnover: far below it the curve reaches the Gain value.");
        low.cf.setTooltip("Low shelf turnover frequency (Hz). Energy well below this frequency is tilted toward the Gain dB setting; above it the response flattens back toward 0 dB change (classic shelving EQ).");
        low.gain.setTooltip("How much boost or cut applies in the bass region (0 dB = no change). Negative = gentle low cut.");

        motionStatus.setJustificationType(juce::Justification::centredLeft);
        motionStatus.setFont(juce::Font(juce::FontOptions()
                                            .withName(juce::Font::getDefaultMonospacedFontName())
                                            .withHeight(10.0f)));
        motionStatus.setColour(juce::Label::textColourId, kTextBright);
        motionStatus.setMinimumHorizontalScale(0.85f);
        ParaEQ301AudioProcessor::MotionEffectiveEqSnapshot snap0;
        processor.getMotionEffectiveEqSnapshot(snap0);
        motionStatus.setText(buildEqMotionStatusShort(ap, snap0), juce::dontSendNotification);
        motionStatus.setTooltip(buildEqMotionPanelTooltip(ap, snap0));
        motionStatus.setVisible(false);
        addAndMakeVisible(motionStatus);
        motionInfoBtn.setButtonText("?");
        motionInfoBtn.setTooltip("Motion status / live LFO readout");
        motionInfoBtn.onClick = [this]
        {
            auto content = std::make_unique<juce::Label>();
            content->setText(motionStatus.getText() + "\n\n" + motionStatus.getTooltip(), juce::dontSendNotification);
            content->setJustificationType(juce::Justification::topLeft);
            content->setFont(juce::Font(juce::FontOptions().withHeight(12.0f)));
            content->setColour(juce::Label::textColourId, juce::Colours::white);
            content->setColour(juce::Label::backgroundColourId, juce::Colour(0xff1a1a1a));
            content->setBorderSize(juce::BorderSize<int>(10, 12, 10, 12));
            content->setMinimumHorizontalScale(1.0f);
            content->setSize(440, 220);
            juce::CallOutBox::launchAsynchronously(std::move(content), motionInfoBtn.getScreenBounds(), nullptr);
        };
        addAndMakeVisible(motionInfoBtn);

        styleToggleDark(eqPinkBalToggle);
        eqPinkBalToggle.setTooltip(
            "When on (default), a gain trim derived from the four linear EQ bands is applied after the full colour path: "
            "ThrillMe 1 & 2, the four IIR bands, SVF, anharmonic bank, roast post shelf, lo-fi, glue, ring - then APR on top (if on) - "
            "so big EQ boosts do not stack as much extra loudness through that chain. Turn off for classic raw dB behaviour.");
        addAndMakeVisible(eqPinkBalToggle);
        batts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(ap, "eqPinkLevelBal", eqPinkBalToggle));

        styleToggleDark(lfoHostSyncToggle);
        lfoHostSyncToggle.setTooltip(
            "When on, Motion LFO rate follows the host tempo and the division menu (all four bands share one speed; Hz sliders are ignored for rate). "
            "Requires the transport to supply BPM (often while playing). When off, each band uses its Hz slider.");
        addAndMakeVisible(lfoHostSyncToggle);
        batts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(ap, "lfoHostSync", lfoHostSyncToggle));

        lfoHostSyncDivBox.setTooltip("Length of one Motion LFO cycle when BPM sync is on (straight and triplet divisions).");
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*>(ap.getParameter("lfoHostSyncDiv")))
            lfoHostSyncDivBox.addItemList(c->choices, 1);
        stylePeqComboBox(lfoHostSyncDivBox);
        addAndMakeVisible(lfoHostSyncDivBox);
        comboAtts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(ap, "lfoHostSyncDiv", lfoHostSyncDivBox));
        lfoHostSyncDivBox.setEnabled(ap.getRawParameterValue("lfoHostSync")->load() > 0.5f);

        motionOverviewHitEq.setTooltip(
            "Hi / M1 / M2 / Lo rows modulate those EQ bands around the values on the EQ tab (LFO). EQ gain depth up to +/-12 dB around the knob; freq and width sweep similarly. Blue dots = LFO phase.\n\n"
            "Hi row -> EQ Hi shelf: Gain dB + Shelf Hz.\n"
            "M1 / M2 -> EQ Mid1 / Mid2: Gain + Peak Hz + Width Hz.\n"
            "Lo row -> EQ Low shelf: Gain + Shelf Hz.\n"
            "EQ tab: same light blue row tint when any Motion depth is active; stronger outline on bands whose LFO depth is non-zero. While audio runs, EQ knobs show live modulated values.\n\n"
            "Hover this strip for this help.");
        addAndMakeVisible(motionOverviewHitEq);

        lfoStrip = std::make_unique<LfoTabContent>(processor, ap, atts);
        lfoStrip->setMotionLayoutInterleaved(false);
        addAndMakeVisible(*lfoStrip);

        eqGraphTooltip.setTooltip(
            "Plot = FFT spectrum (white pre-EQ / blue post, ~50% opacity layer) plus the same +/-30 dB 4-band IIR curve as the Curve tab (green).\n\n"
            "Low / High bands are shelves (tilt EQ), not pass filters. Shelf Hz is the turnover toward the Gain value.\n\n"
            "Signal path: pre-EQ core sat -> Low shelf -> Mid1 -> Mid2 -> High shelf -> post-EQ core sat. "
            "Coloured tags follow each band's centre Hz (including Motion). Knobs: green arc = live value, blue dot = stored rest when Motion is running.");
        addAndMakeVisible(eqGraphTooltip);

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

        addAndMakeVisible(core1BypassToggle);
        styleToggleDark(core1BypassToggle);
        batts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(ap, "core1Bypass", core1BypassToggle));
        core1BypassToggle.setTooltip("Hard-bypass pre-EQ ThrillMe 1 (ignores Mix %). At 0% mix there is no ThrillMe wet anyway. Raising Mix % clears bypass if it was on.");

        styleCoreSatWideSlider(coreSat);
        styleLabel(coreSatLabel, "Mix %");
        addAndMakeVisible(coreSat);
        addAndMakeVisible(coreSatLabel);
        coreSat.setTooltip("Dry/wet of ThrillMe 1 before the EQ. Default 100% so ThrillMe is always in circuit; with Init / flat Thrill sliders it stays close to transparent. Dragging above 0% clears bypass if it was on.");
        atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "coreSat", coreSat));
        coreSat.textFromValueFunction = [](double v)
        {
            return juce::String(juce::roundToInt(v * 100.0)) + " %";
        };
        coreSat.valueFromTextFunction = [](const juce::String& t)
        {
            return juce::jlimit(0.0, 1.0, t.getDoubleValue() / 100.0);
        };
        coreSat.onValueChange = [this]()
        {
            if (coreSat.getValue() <= 0.00002)
                return;
            auto& tree = proc.getAPVTS();
            if (tree.getRawParameterValue("core1Bypass")->load() <= 0.5f)
                return;
            if (auto* p = tree.getParameter("core1Bypass"))
                p->setValueNotifyingHost(0.f);
        };

        addAndMakeVisible(core2BypassToggle);
        styleToggleDark(core2BypassToggle);
        batts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(ap, "core2Bypass", core2BypassToggle));
        core2BypassToggle.setTooltip("Hard-bypass post-EQ ThrillMe 2 (ignores Mix %). At 0% mix there is no ThrillMe wet anyway. Raising Mix % clears bypass if it was on.");

        styleCoreSatWideSlider(core2Sat);
        styleLabel(core2SatLabel, "Mix %");
        addAndMakeVisible(core2Sat);
        addAndMakeVisible(core2SatLabel);
        core2Sat.setTooltip("Dry/wet of ThrillMe 2 after the EQ (and optional SVF / anharm taps), before roast tail. Default 100% (same idea as ThrillMe 1). Dragging above 0% clears bypass if it was on.");
        atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "core2Sat", core2Sat));
        core2Sat.textFromValueFunction = [](double v)
        {
            return juce::String(juce::roundToInt(v * 100.0)) + " %";
        };
        core2Sat.valueFromTextFunction = [](const juce::String& t)
        {
            return juce::jlimit(0.0, 1.0, t.getDoubleValue() / 100.0);
        };
        core2Sat.onValueChange = [this]()
        {
            if (core2Sat.getValue() <= 0.00002)
                return;
            auto& tree = proc.getAPVTS();
            if (tree.getRawParameterValue("core2Bypass")->load() <= 0.5f)
                return;
            if (auto* p = tree.getParameter("core2Bypass"))
                p->setValueNotifyingHost(0.f);
        };

        auto setupThrillSlider = [&](juce::Slider& s, juce::Label& cap, const char* pid, const char* capText,
                                     juce::Colour fill, const juce::String& tip)
        {
            styleLinearSliderCompact(s, fill);
            s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 18);
            styleLabel(cap, capText);
            addAndMakeVisible(s);
            addAndMakeVisible(cap);
            s.setTooltip(tip);
            atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, pid, s));
        };

        thrill1Title.setText("ThrillMe 1", juce::dontSendNotification);
        thrill1Title.setJustificationType(juce::Justification::centredLeft);
        thrill1Title.setColour(juce::Label::textColourId, kTextBright.withAlpha(0.85f));
        thrill1Title.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
        addAndMakeVisible(thrill1Title);
        setupThrillSlider(thrill1Spec, thrill1SpecLabel, "thrill1Spec", "Spectral", kAccentGreen,
                          "Spectral enhancer before the 3-band dynamics (shelves + peaks). Same control as the doc's spectral amount (readout is %). Default 50 % travel (original centre).");
        setupThrillSlider(thrill1Thr, thrill1ThrLabel, "thrill1ThreshDb", "Threshold", kAccentBlue,
                          "Multiband threshold (-80...0 dB, skewed like the original law). Same control as the doc's threshold (readout is dB). Lower = more gain reduction.");
        setupThrillSlider(thrill1Ratio, thrill1RatioLabel, "thrill1Ratio", "Ratio", kAccentGreen,
                          "Compression strength like the original 3-knob unit: left / high displayed :1 = harder; right / low :1 = gentle. Default is 50 % travel (not max gentle).");
        thrill1Ratio.textFromValueFunction = [](double v)
        {
            const int shown = 129 - juce::jlimit(1, 128, juce::roundToInt(v));
            return juce::String(shown) + ":1";
        };
        thrill1Ratio.valueFromTextFunction = [](const juce::String& t)
        {
            const int shown = juce::jlimit(1, 128, t.upToFirstOccurrenceOf(":", false, false).getIntValue());
            return (double) (129 - shown);
        };
        thrill1Thr.textFromValueFunction = [](double v) { return juce::String(v, 1) + " dB"; };
        thrill1Thr.valueFromTextFunction = [](const juce::String& t) { return t.getDoubleValue(); };

        thrill1Spec.textFromValueFunction = [](double v)
        {
            return juce::String(juce::roundToInt(v * 100.0)) + " %";
        };
        thrill1Spec.valueFromTextFunction = [](const juce::String& t)
        {
            return juce::jlimit(0.0, 1.0, t.getDoubleValue() / 100.0);
        };

        thrill2Title.setText("ThrillMe 2", juce::dontSendNotification);
        thrill2Title.setJustificationType(juce::Justification::centredLeft);
        thrill2Title.setColour(juce::Label::textColourId, kTextBright.withAlpha(0.85f));
        thrill2Title.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
        addAndMakeVisible(thrill2Title);
        setupThrillSlider(thrill2Spec, thrill2SpecLabel, "thrill2Spec", "Spectral", kAccentGreen,
                          "Spectral lift before ThrillMe 2 dynamics on the post-EQ path (doc spectral %). Default 50 % travel like ThrillMe 1.");
        setupThrillSlider(thrill2Thr, thrill2ThrLabel, "thrill2ThreshDb", "Threshold", kAccentBlue,
                          "Multiband threshold (-80...0 dB, skewed). Doc threshold control (readout dB). Lower = more gain reduction.");
        setupThrillSlider(thrill2Ratio, thrill2RatioLabel, "thrill2Ratio", "Ratio", kAccentGreen,
                          "Left / high displayed :1 = harder (original knob direction). Default 50 % travel.");
        thrill2Ratio.textFromValueFunction = thrill1Ratio.textFromValueFunction;
        thrill2Ratio.valueFromTextFunction = thrill1Ratio.valueFromTextFunction;
        thrill2Thr.textFromValueFunction = thrill1Thr.textFromValueFunction;
        thrill2Thr.valueFromTextFunction = thrill1Thr.valueFromTextFunction;

        thrill2Spec.textFromValueFunction = thrill1Spec.textFromValueFunction;
        thrill2Spec.valueFromTextFunction = thrill1Spec.valueFromTextFunction;

        styleLinearSliderCompact(coreDirt, kAccentGreen);
        coreDirt.setTextBoxStyle(juce::Slider::TextBoxRight, false, 54, 20);
        styleLabel(coreDirtLabel, "Dirt");
        addAndMakeVisible(coreDirt);
        addAndMakeVisible(coreDirtLabel);
        coreDirt.setTooltip("Asymmetric curve: always shapes a little on the ThrillMe 1 wet bus (before EQ), stronger with Mix % and Dirt %; plus the Roast Low/Mid taps between EQ bands. Inactive in Linear EQ only mode.");
        atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "coreDirt", coreDirt));
        coreDirt.textFromValueFunction = [](double v)
        {
            return juce::String(juce::roundToInt(v * 100.0)) + " %";
        };
        coreDirt.valueFromTextFunction = [](const juce::String& t)
        {
            return juce::jlimit(0.0, 1.0, t.getDoubleValue() / 100.0);
        };

        styleLinearSliderCompact(coreLifeDepth, kAccentGreen);
        coreLifeDepth.setTextBoxStyle(juce::Slider::TextBoxRight, false, 54, 20);
        styleLabel(coreLifeDepthLabel, "Life");
        addAndMakeVisible(coreLifeDepth);
        addAndMakeVisible(coreLifeDepthLabel);
        coreLifeDepth.setTooltip("Slow LFO on ThrillMe 1/2 wet amount and on Roast Low/Mid inter-band saturators (different phase pre vs post). Needs Thrill mix and/or Roast Low/Mid raised. Inactive in Linear EQ only mode.");
        atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "coreLifeDepth", coreLifeDepth));
        coreLifeDepth.textFromValueFunction = [](double v)
        {
            return juce::String(juce::roundToInt(v * 100.0)) + " %";
        };
        coreLifeDepth.valueFromTextFunction = [](const juce::String& t)
        {
            return juce::jlimit(0.0, 1.0, t.getDoubleValue() / 100.0);
        };

        styleLinearSliderCompact(coreLifeHz, kAccentBlue);
        coreLifeHz.setTextBoxStyle(juce::Slider::TextBoxRight, false, 72, 20);
        styleLabel(coreLifeHzLabel, "Life Hz");
        addAndMakeVisible(coreLifeHz);
        addAndMakeVisible(coreLifeHzLabel);
        coreLifeHz.setTooltip("Rate of the core Life LFO (independent of Motion band LFOs). Inactive in Linear EQ only mode.");
        atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "coreLifeHz", coreLifeHz));
        coreLifeHz.textFromValueFunction = [](double v)
        {
            return juce::String(v, 2) + " Hz";
        };
        coreLifeHz.valueFromTextFunction = [](const juce::String& t) { return t.getDoubleValue(); };

        auto hzStringFromValue = [](double v) { return juce::String(static_cast<int>(std::round(v))) + " Hz"; };
        auto dbStringFromValue = [](double v) { return juce::String(v, 1) + " dB"; };

        for (auto* s : {&hi.cf, &mid1.cf, &mid2.cf, &low.cf})
        {
            s->textFromValueFunction = hzStringFromValue;
            s->valueFromTextFunction = [](const juce::String& t) { return t.getDoubleValue(); };
        }
        mid1.bw.textFromValueFunction = hzStringFromValue;
        mid1.bw.valueFromTextFunction = [](const juce::String& t) { return t.getDoubleValue(); };
        mid1.bw.setTextBoxStyle(juce::Slider::TextBoxBelow, false, kEqBwTextBoxW, kEqTextBoxH - 1);
        mid2.bw.textFromValueFunction = hzStringFromValue;
        mid2.bw.valueFromTextFunction = [](const juce::String& t) { return t.getDoubleValue(); };
        mid2.bw.setTextBoxStyle(juce::Slider::TextBoxBelow, false, kEqBwTextBoxW, kEqTextBoxH - 1);

        for (auto* s : {&hi.gain, &mid1.gain, &mid2.gain, &low.gain})
        {
            s->textFromValueFunction = dbStringFromValue;
            s->valueFromTextFunction = [](const juce::String& t) { return t.getDoubleValue(); };
        }

        for (auto* s : {&hi.cf, &hi.gain, &mid1.cf, &mid1.bw, &mid1.gain, &mid2.cf, &mid2.bw, &mid2.gain, &low.cf, &low.gain})
            s->addListener(this);

        startTimerHz(15);
    }

    void visibilityChanged() override
    {
        juce::Component::visibilityChanged();
        if (isShowing())
            resized();
    }

    ~EqTabContent() override
    {
        stopTimer();
        for (auto* s : {&hi.cf, &hi.gain, &mid1.cf, &mid1.bw, &mid1.gain, &mid2.cf, &mid2.bw, &mid2.gain, &low.cf, &low.gain})
            s->removeListener(this);
        for (auto* band : {&hi, &mid1, &mid2, &low})
        {
            band->cf.setLookAndFeel(nullptr);
            band->gain.setLookAndFeel(nullptr);
            if (band->hasBw)
                band->bw.setLookAndFeel(nullptr);
        }
    }

    void timerCallback() override
    {
        auto& ap = proc.getAPVTS();
        ParaEQ301AudioProcessor::MotionEffectiveEqSnapshot s;
        proc.getMotionEffectiveEqSnapshot(s);
        motionStatus.setText(buildEqMotionStatusShort(ap, s), juce::dontSendNotification);
        motionStatus.setTooltip(buildEqMotionPanelTooltip(ap, s));

        const bool bpmSyncOn = ap.getRawParameterValue("lfoHostSync")->load() > 0.5f;
        lfoHostSyncDivBox.setEnabled(bpmSyncOn);

        const bool followLive = motionLfoDepthActive(ap) && s.motionEngaged && eqSliderGestureDepth == 0
                                && !eqTextBoxHasKeyboardFocus();
        if (followLive)
            setEqKnobLiveDisplayFromSnapshot(s);
        else
            clearEqKnobLiveDisplayProps();

        repaintEqKnobs();
        repaint();
    }

    int getMinimumContentHeight() const noexcept { return (int) PeqEqTabLayoutMetrics::minimumContentHeight(); }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(kPanelBlack);
        auto& ap = proc.getAPVTS();
        for (int r = 0; r < 4; ++r)
        {
            if (!motionRowRect[(size_t) r].isEmpty() && motionLfoDepthActive(ap))
            {
                const bool bandOn = eqBandHasMotion(ap, r);
                // Same fill for every row when Motion is armed so Lo/Hi shelves never read as "missing"
                // the panel tint (weak-only alpha on kPanelBlack looked like plain black on the Low row).
                g.setColour(kAccentBlue.withAlpha(0.09f));
                g.fillRoundedRectangle(motionRowRect[(size_t) r].toFloat().reduced(1), 5.f);
                g.setColour(kAccentBlue.withAlpha(bandOn ? 0.28f : 0.16f));
                g.drawRoundedRectangle(motionRowRect[(size_t) r].toFloat().reduced(1), 5.f, 1.f);
            }
        }
        if (eqGraphBounds.getHeight() > 24)
            paintMergedSpectrumAndEqInRect(g, eqGraphBounds, proc, freqScratch, magScratch, graphEqMagSmoothed,
                                           graphComboMagSmoothed, graphSpecBefore, graphSpecAfter);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(kPeqTabPanelMargin);
        if (bounds.getWidth() < 160 || bounds.getHeight() < 120)
            return;

        // Top -> bottom: EQ graph, one-line Motion hint, Core (pre-EQ), then Hi / Mid1 / Mid2 / Low rows.
        // Extra height goes to the graph. Never force a minimum graph height that steals from the band rows:
        // short windows used to clip the Low shelf off the bottom (graph used jmax(52, ...) on a negative remainder).
        const int restNoGraph = (int) PeqEqTabLayoutMetrics::chromeBelowGraph();
        int graphH = bounds.getHeight() - restNoGraph;
        graphH = juce::jmax(0, graphH);
        graphH = juce::jmin(graphH, kEqMagGraphMaxOuterH);

        eqGraphBounds = bounds.removeFromTop(graphH);
        bounds.removeFromTop((int) PeqEqTabLayoutMetrics::gapAfterGraph);
        {
            auto motionRow = bounds.removeFromTop((int) PeqEqTabLayoutMetrics::motionLineH);
            constexpr int kEqBalToggleW = 196;
            const int tw = juce::jmin(kEqBalToggleW, juce::jmax(140, motionRow.getWidth() / 3));
            auto balArea = motionRow.removeFromRight(tw);
            const int bcy = balArea.getCentreY() - 11;
            eqPinkBalToggle.setBounds(balArea.getX() + 2, bcy, balArea.getWidth() - 4, 22);

            const int rowCy = motionRow.getCentreY() - 11;
            int syncW = juce::jlimit(110, 140, motionRow.getWidth() / 5);
            int divW = juce::jlimit(96, 130, motionRow.getWidth() / 4);
            const int minStatus = 48;
            if (syncW + divW + minStatus > motionRow.getWidth())
            {
                divW = juce::jmax(72, motionRow.getWidth() - syncW - minStatus);
                if (divW < 72)
                {
                    syncW = juce::jmax(52, motionRow.getWidth() - 72 - minStatus);
                    divW = juce::jmax(72, motionRow.getWidth() - syncW - minStatus);
                }
            }
            auto syncArea = motionRow.removeFromLeft(syncW);
            auto divArea = motionRow.removeFromLeft(divW);
            lfoHostSyncToggle.setBounds(syncArea.getX() + 1, rowCy, juce::jmax(1, syncArea.getWidth() - 2), 22);
            lfoHostSyncDivBox.setBounds(divArea.reduced(2, 3));
            auto infoArea = motionRow.removeFromLeft(26);
            motionInfoBtn.setBounds(infoArea.getX() + 1, rowCy, 22, 22);
            motionStatus.setBounds(motionRow.reduced(4, 0));
        }
        bounds.removeFromTop((int) PeqEqTabLayoutMetrics::gapAfterMotion);

        {
            auto coreBlock = bounds.removeFromTop((int) PeqEqTabLayoutMetrics::coreStripH());
            auto placeCoreRow = [&](juce::Rectangle<int> coreRow,
                                    juce::ToggleButton& on, juce::Slider& sat, juce::Label& satLab)
            {
                constexpr int kToggleW = 148;
                auto toggleArea = coreRow.removeFromLeft(juce::jmin(kToggleW, juce::jmax(96, coreRow.getWidth() / 4)));
                const int cy = toggleArea.getCentreY() - 11;
                on.setBounds(toggleArea.getX() + 1, cy, toggleArea.getWidth() - 2, 22);
                auto rest = coreRow.reduced(6, 2);
                constexpr int kLabelSatW = 42;
                auto labArea = rest.removeFromRight(kLabelSatW);
                satLab.setBounds(labArea.getX(), rest.getCentreY() - 7, labArea.getWidth(), 14);
                sat.setBounds(rest.getX(), rest.getCentreY() - 11, juce::jmax(60, rest.getWidth()), 22);
            };
            const int kCR = (int) PeqEqTabLayoutMetrics::coreRowH;
            const int kCB = (int) PeqEqTabLayoutMetrics::coreBetweenRows;
            const int kThrill = (int) PeqEqTabLayoutMetrics::thrillRowH;
            const int kTR = (int) PeqEqTabLayoutMetrics::coreToneRowH;
            auto placeThrillRow = [&](juce::Rectangle<int> row,
                                      juce::Label& title,
                                      juce::Slider& sSpec,
                                      juce::Label& lSpec,
                                      juce::Slider& sThr,
                                      juce::Label& lThr,
                                      juce::Slider& sRat,
                                      juce::Label& lRat)
            {
                constexpr int kTitleW = 86;
                auto titleR = row.removeFromLeft(juce::jmin(kTitleW, juce::jmax(72, row.getWidth() / 8)));
                title.setBounds(titleR.getX() + 2, row.getCentreY() - 8, titleR.getWidth() - 2, 16);
                auto rest = row.reduced(4, 1);
                const int nCols = 3;
                // Doc-style labels ("Spectral", "Threshold") need a bit more width than old "Spec" / "Thr dB".
                const int colW = juce::jmax(64, rest.getWidth() / nCols);
                auto placeCell = [&](juce::Slider& sl, juce::Label& lab)
                {
                    auto cell = rest.removeFromLeft(juce::jmin(colW, rest.getWidth()));
                    lab.setBounds(cell.getX(), cell.getY(), cell.getWidth(), 12);
                    sl.setBounds(cell.getX(), cell.getY() + 12, juce::jmax(40, cell.getWidth() - 2), 16);
                };
                placeCell(sSpec, lSpec);
                placeCell(sThr, lThr);
                placeCell(sRat, lRat);
            };
            auto row1 = coreBlock.removeFromTop(kCR);
            coreBlock.removeFromTop(kCB);
            auto thrill1Row = coreBlock.removeFromTop(kThrill);
            coreBlock.removeFromTop(kCB);
            auto row2 = coreBlock.removeFromTop(kCR);
            coreBlock.removeFromTop(kCB);
            auto thrill2Row = coreBlock.removeFromTop(kThrill);
            placeCoreRow(row1, core1BypassToggle, coreSat, coreSatLabel);
            placeThrillRow(thrill1Row, thrill1Title, thrill1Spec, thrill1SpecLabel, thrill1Thr, thrill1ThrLabel,
                           thrill1Ratio, thrill1RatioLabel);
            placeCoreRow(row2, core2BypassToggle, core2Sat, core2SatLabel);
            placeThrillRow(thrill2Row, thrill2Title, thrill2Spec, thrill2SpecLabel, thrill2Thr, thrill2ThrLabel,
                           thrill2Ratio, thrill2RatioLabel);
            coreBlock.removeFromTop(kCB);
            auto toneRow = coreBlock.removeFromTop(kTR);
            {
                constexpr int kGapTone = 6;
                const int toneAvail = juce::jmax(1, toneRow.getWidth() - 2 * kGapTone);
                const int colW = toneAvail / 3;
                auto placeToneCell = [&](juce::Rectangle<int> cell, juce::Label& lab, juce::Slider& sl)
                {
                    constexpr int kLabW = 52;
                    auto la = cell.removeFromLeft(juce::jmin(kLabW, juce::jmax(40, cell.getWidth() / 3)));
                    lab.setBounds(la.getX(), cell.getCentreY() - 7, la.getWidth(), 14);
                    sl.setBounds(cell.getX(), cell.getCentreY() - 10, juce::jmax(96, cell.getWidth()), 20);
                };
                auto c0 = toneRow.removeFromLeft(colW);
                toneRow.removeFromLeft(kGapTone);
                auto c1 = toneRow.removeFromLeft(colW);
                toneRow.removeFromLeft(kGapTone);
                auto c2 = toneRow;
                placeToneCell(c0, coreDirtLabel, coreDirt);
                placeToneCell(c1, coreLifeDepthLabel, coreLifeDepth);
                placeToneCell(c2, coreLifeHzLabel, coreLifeHz);
            }
        }

        bounds.removeFromTop((int) PeqEqTabLayoutMetrics::gapBeforeBands);

        motionOverviewHitEq.setBounds(bounds.getX(), bounds.getY(), bounds.getWidth(),
                                      (int) PeqEqTabLayoutMetrics::kEqMotionOverviewRowH());
        bounds.removeFromTop((int) PeqEqTabLayoutMetrics::kEqMotionOverviewRowH());

        constexpr int bandStripW = 68;
        constexpr int gapAfterBandStrip = 8;
        constexpr int eqGap = 5;
        constexpr int kEqMotionColGap = 6;
        const int step = kEqSliderColW + eqGap;
        /** Right edge of the widest EQ row (Mid1/Mid2: cf, bw, gain in cols 0..2) measured from bandBounds X. */
        const int eqKnobsRightX = bandStripW + gapAfterBandStrip + 2 * step + kEqSliderColW;
        /** Same-row Motion only when the remainder fits full-size (78px) LFO columns; otherwise stacked strip below. */
        const int mergedWideMinW = eqKnobsRightX + kEqMotionColGap + kLfoMotionInterleavedMinW();

        juce::Rectangle<int> bandBounds = bounds;
        const bool mergedBandPlusLfo = (bandBounds.getWidth() >= mergedWideMinW);

        const int xCol0 = bandBounds.getX() + bandStripW + gapAfterBandStrip;

        auto placeRow = [&](BandKnobs& band, int rowIndex)
        {
            const int y = bandBounds.getY() + rowIndex * kEqRowHeight;
            band.bandLabel.setBounds(bandBounds.getX() + 3, y + 8, bandStripW - 6, 40);
            band.bandLabel.setJustificationType(juce::Justification::centredRight);

            auto placeKnobCol = [&](juce::Slider& sl, juce::Label& cap, int colIndex)
            {
                const int x = xCol0 + colIndex * step;
                sl.setBounds(x, y, kEqSliderColW, kEqSliderColumnH);
                cap.setBounds(x, sl.getBottom() + kGapCaption, kEqSliderColW, kCaptionH);
            };

            if (band.hasBw && band.hasCfInLeftColumn)
            {
                placeKnobCol(band.cf, band.cfLabel, 0);
                placeKnobCol(band.gain, band.gainLabel, 1);
                placeKnobCol(band.bw, band.bwLabel, 2);
            }
            else
            {
                placeKnobCol(band.cf, band.cfLabel, 0);
                placeKnobCol(band.gain, band.gainLabel, 1);
            }

            const int lastCol = (band.hasBw && band.hasCfInLeftColumn) ? 2 : 1;
            const int rowEqKnobRight = xCol0 + lastCol * step + kEqSliderColW;
            const int tintRight = mergedBandPlusLfo ? bandBounds.getRight() : rowEqKnobRight;
            motionRowRect[(size_t) rowIndex] = { bandBounds.getX(), y, tintRight - bandBounds.getX() + 2, kEqRowHeight };
        };

        placeRow(low, 0);
        placeRow(mid1, 1);
        placeRow(mid2, 2);
        placeRow(hi, 3);

        if (!mergedBandPlusLfo)
        {
            lfoStrip->setMotionLayoutInterleaved(false);
            const int motionY = bandBounds.getY() + 4 * kEqRowHeight;
            const int motionH = juce::jmax(kLfoMotionPanelMinH(), bandBounds.getBottom() - motionY);
            lfoStrip->setBounds(bandBounds.getX(), motionY, bandBounds.getWidth(), motionH);
        }
        else
        {
            const int lfoX = bandBounds.getX() + eqKnobsRightX + kEqMotionColGap;
            const int lfoW = juce::jmax(1, bandBounds.getRight() - lfoX);
            lfoStrip->setMotionLayoutInterleaved(true);
            lfoStrip->setBounds(lfoX, bandBounds.getY(), lfoW, 4 * kEqRowHeight);
        }

        eqGraphTooltip.setBounds(eqGraphBounds);
    }
};

struct ParaEQ301AudioProcessorEditor::EqTabScrollHost : public juce::Viewport
{
    explicit EqTabScrollHost(ParaEQ301AudioProcessor& processor,
                             juce::AudioProcessorValueTreeState& ap,
                             std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>>& atts,
                             std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>>& batts,
                             std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>>& comboAtts)
        : content(std::make_unique<EqTabContent>(processor, ap, atts, batts, comboAtts))
    {
        setViewedComponent(content.get(), false);
        setScrollBarsShown(true, false);
        setScrollBarThickness(9);
    }

    void resized() override
    {
        const int w = juce::jmax(1, getWidth());
        const int h = juce::jmax(1, getHeight());
        const int minH = content->getMinimumContentHeight();
        content->setBounds(0, 0, w, juce::jmax(h, minH));
        juce::Viewport::resized();
    }

    std::unique_ptr<EqTabContent> content;
};

struct ParaEQ301AudioProcessorEditor::OutTabContent : public juce::Component,
                                                    public juce::SettableTooltipClient,
                                                    private juce::Timer
{
    ParaEQ301AudioProcessor& proc;
    juce::ToggleButton limOn { "Limiter" };
    juce::Slider limThresh;
    juce::Slider limRelease;
    juce::Label lfoDebugReadout;
    juce::Label note;
    juce::TextButton infoBtn;

    OutTabContent(ParaEQ301AudioProcessor& processor,
                  juce::AudioProcessorValueTreeState& ap,
                  std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>>& atts,
                  std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>>& batts)
        : proc(processor)
    {
        setTooltip("Output limiter plus Motion LFO debug readout (live EQ targets). Limiter: on/off, ceiling (dB), release (ms).");
        styleToggleDark(limOn);
        limOn.setTooltip("Output limiter on/off.");
        addAndMakeVisible(limOn);
        batts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(ap, "outLimOn", limOn));

        styleLinearSliderCompact(limThresh, kAccentGreen);
        limThresh.setTooltip("Limiter ceiling (dB).");
        addAndMakeVisible(limThresh);
        atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "outLimThresh", limThresh));
        limThresh.textFromValueFunction = [](double v) { return juce::String(v, 1) + " dB"; };

        styleLinearSliderCompact(limRelease, kAccentGreen);
        limRelease.setTooltip("Limiter release (ms).");
        addAndMakeVisible(limRelease);
        atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "outLimRelease", limRelease));
        limRelease.textFromValueFunction = [](double v) { return juce::String(juce::roundToInt(v)) + " ms"; };

        lfoDebugReadout.setJustificationType(juce::Justification::topLeft);
        lfoDebugReadout.setFont(juce::Font(juce::FontOptions()
                                               .withName(juce::Font::getDefaultMonospacedFontName())
                                               .withHeight(9.2f)));
        lfoDebugReadout.setColour(juce::Label::textColourId, kTextBright);
        lfoDebugReadout.setMinimumHorizontalScale(1.0f);
        lfoDebugReadout.setTooltip("Motion LFO debug: live effective EQ (L channel, plus R block when stereo LFO phase splits). "
                                   "Same monospace snapshot as the former Curve tab strip; Motion knobs live on the EQ tab.");
        addAndMakeVisible(lfoDebugReadout);
        ParaEQ301AudioProcessor::MotionEffectiveEqSnapshot snap0;
        proc.getMotionEffectiveEqSnapshot(snap0);
        lfoDebugReadout.setText(formatMotionLiveReadout(snap0), juce::dontSendNotification);

        styleLabelDark(note,
                       "Limiter runs at the end of the chain (after Core 2 and trim). "
                       "Controls above; I/O meters stay at the top of the window.",
                       true);
        note.setJustificationType(juce::Justification::topLeft);
        note.setFont(juce::Font(juce::FontOptions().withHeight(11.5f)));
        addAndMakeVisible(note);
        wireInfoButton(infoBtn, note, "Output");
        addAndMakeVisible(infoBtn);

        startTimerHz(24);
    }

    ~OutTabContent() override { stopTimer(); }

    void timerCallback() override
    {
        ParaEQ301AudioProcessor::MotionEffectiveEqSnapshot s;
        proc.getMotionEffectiveEqSnapshot(s);
        lfoDebugReadout.setText(formatMotionLiveReadout(s), juce::dontSendNotification);
    }

    void paint(juce::Graphics& g) override { g.fillAll(kPanelBlack); }

    void resized() override
    {
        auto b = getLocalBounds().reduced(10, 10);
        constexpr int kRow = 40;
        auto row = b.removeFromTop(kRow);
        infoBtn.setBounds(row.removeFromRight(26).reduced(0, 8));
        row.removeFromRight(6);
        constexpr int kLimToggleW = 92;
        limOn.setBounds(row.removeFromLeft(juce::jmin(kLimToggleW, juce::jmax(72, row.getWidth() / 5))).reduced(0, 5));
        row.removeFromLeft(10);
        const int sw = juce::jmax(96, (row.getWidth() - 8) / 2);
        limThresh.setBounds(row.removeFromLeft(sw));
        row.removeFromLeft(8);
        limRelease.setBounds(row.removeFromLeft(sw));
        b.removeFromTop(12);
        const int dbgH = b.getHeight();
        lfoDebugReadout.setBounds(b.removeFromTop(dbgH).reduced(0, 2));
    }
};

struct ParaEQ301AudioProcessorEditor::CurveTabContent : public juce::Component, private juce::Timer
{
    explicit CurveTabContent(ParaEQ301AudioProcessor& p) : proc(p)
    {
        startTimerHz(20);
        specBefore.assign((size_t) ParaEQ301AudioProcessor::kEqCurvePlotPoints, -100.f);
        specAfter.assign((size_t) ParaEQ301AudioProcessor::kEqCurvePlotPoints, -100.f);

        curvePlotTooltip.setTooltip(
            "Single plot: FFT spectrum (left ch) overlaid at ~50% opacity with the theoretical 4-band IIR curve (+/-30 dB, green). "
            "White fill/trace = after pre-EQ Core only; blue = after 4-band EQ and optional post-EQ Core 2 (before limiter). "
            "Mint = linear small-signal model when SVF/bank theory is active (same as EQ tab graph).\n\n"
            "The same merged view is drawn on the EQ tab magnitude strip. "
            "I/O levels: meters at the top of the window (dBFS RMS).");
        addAndMakeVisible(curvePlotTooltip);
    }

    ~CurveTabContent() override { stopTimer(); }

    void timerCallback() override { repaint(); }

    void resized() override
    {
        // Width matches EqTabContent graph strip: outer margin lives on CurveMotionTabContent (kPeqTabPanelMargin).
        plotArea = getLocalBounds();
        curvePlotTooltip.setBounds(plotArea);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(kPanelBlack);
        auto plot = plotArea.reduced(0, 2);
        if (plot.getHeight() < 48)
            return;

        paintMergedSpectrumAndEqInRect(g, plot, proc, freqScratch, magScratch, eqMagSmoothed, comboMagSmoothed, specBefore, specAfter);
    }

    ParaEQ301AudioProcessor& proc;
    juce::Rectangle<int> plotArea;
    TooltipMouseProxy curvePlotTooltip;
    std::vector<double> freqScratch;
    std::vector<float> magScratch;
    std::vector<float> eqMagSmoothed;
    std::vector<float> comboMagSmoothed;
    std::vector<float> specBefore;
    std::vector<float> specAfter;
};

/** Spectrum / meters (top) + Motion LFO controls (bottom) on one tab. */
struct ParaEQ301AudioProcessorEditor::RoastTabContent : public juce::Component
{
    juce::Label programLabel;
    juce::ComboBox programBox;
    juce::Label oversampleLabel;
    juce::ComboBox oversampleBox;
    juce::ToggleButton linearEqToggle { "Linear EQ only (RBJ A/B)" };
    juce::Label intro;
    juce::TextButton infoBtn;
    juce::Slider coreCrunch;
    juce::Label coreCrunchL;
    juce::Label roastCoreShapeL;
    juce::ComboBox roastCoreShapeBox;
    juce::Slider roastPreEmphDb;
    juce::Label roastPreEmphDbL;
    juce::Slider roastPostTiltDb;
    juce::Label roastPostTiltDbL;
    juce::Slider roastBoostTrack;
    juce::Label roastBoostTrackL;
    juce::Slider roastMidChain;
    juce::Label roastMidChainL;
    juce::Slider roastLowChain;
    juce::Label roastLowChainL;
    juce::Slider roastPunch;
    juce::Label roastPunchL;
    juce::Slider roastGlue;
    juce::Label roastGlueL;
    juce::Slider roastLoFi;
    juce::Label roastLoFiL;
    juce::Slider roastRing;
    juce::Label roastRingL;
    juce::Slider roastEnvDrive;
    juce::Label roastEnvDriveL;
    juce::Slider roastFlutter;
    juce::Label roastFlutterL;
    juce::Slider roastStereoWide;
    juce::Label roastStereoWideL;
    juce::Slider roastOutputTrimDb;
    juce::Label roastOutputTrimDbL;

    juce::Label svfTitle;
    juce::ToggleButton svfEnableToggle { "SVF on" };
    juce::Slider svfMix;
    juce::Label svfMixL;
    juce::Slider svfCf;
    juce::Label svfCfL;
    juce::Slider svfQ;
    juce::Label svfQL;
    juce::Slider svfDrive;
    juce::Label svfDriveL;
    juce::Slider svfGainDb;
    juce::Label svfGainDbL;

    ParaEQ301AudioProcessor& proc;

    static void placeRow(juce::Rectangle<int>& col, juce::Label& cap, juce::Slider& sl, int rowH)
    {
        auto row = col.removeFromTop(rowH);
        cap.setBounds(row.getX(), row.getY(), row.getWidth(), 14);
        sl.setBounds(row.getX(), row.getY() + 16, row.getWidth(), row.getHeight() - 18);
    }

    RoastTabContent(ParaEQ301AudioProcessor& processor,
                    juce::AudioProcessorValueTreeState& ap,
                    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>>& atts,
                    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>>& batts,
                    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>>& comboAtts)
        : proc(processor)
    {
        styleLabelDark(programLabel, "Factory preset", true);
        addAndMakeVisible(programLabel);
        programBox.setTooltip("Built-in snapshots (Init, roast, Motion, linear A/B, anharm, APR by source). Host preset save still stores full APVTS.");
        addAndMakeVisible(programBox);
        for (int i = 0; i < ParaEQ301AudioProcessor::kNumFactoryPrograms; ++i)
            programBox.addItem(proc.getProgramName(i), i + 1);
        programBox.onChange = [this]()
        {
            const int id = programBox.getSelectedId();
            if (id > 0)
                proc.setCurrentProgram(id - 1);
        };
        programBox.setSelectedId(proc.getCurrentProgram() + 1, juce::dontSendNotification);

        styleLabelDark(oversampleLabel, "OS", true);
        oversampleLabel.setTooltip("Oversampling: run the roast + EQ + SVF path at 2x or 4x the host rate to reduce aliasing from saturators, SVF, and lo-fi. Higher CPU. Disabled in Linear EQ only.");
        addAndMakeVisible(oversampleLabel);
        oversampleBox.setTooltip(oversampleLabel.getTooltip());
        if (auto* oc = dynamic_cast<juce::AudioParameterChoice*>(ap.getParameter("oversample")))
            oversampleBox.addItemList(oc->choices, 1);
        addAndMakeVisible(oversampleBox);
        comboAtts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(ap, "oversample", oversampleBox));

        styleToggleDark(linearEqToggle);
        linearEqToggle.setTooltip(
            "Bypass ThrillMe, cores, shaper, SVF, anharmonic bank, and the rest of the roast chain - only the four RBJ bands (+ pink balance) stay. "
            "APR still runs after those bands so the APR tab always affects audio.");
        addAndMakeVisible(linearEqToggle);
        batts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(ap, "linearEqListen", linearEqToggle));

        styleLabelDark(intro,
                       "Crunch / roast: pre HF -> cores -> low/mid taps (Roast core flavour shapes those taps) -> linear EQ -> nonlinear SVF (parallel BP) -> core 2 -> post tilt -> lo-fi -> glue -> ring. "
                       "Boost track follows Motion when engaged.",
                       true);
        intro.setJustificationType(juce::Justification::topLeft);
        intro.setFont(juce::Font(juce::FontOptions()
                                     .withName(juce::Font::getDefaultMonospacedFontName())
                                     .withHeight(11.0f)));
        addAndMakeVisible(intro);
        wireInfoButton(infoBtn, intro, "Roast");
        addAndMakeVisible(infoBtn);

        auto wirePct = [&](juce::Slider& s, juce::Label& lab, const char* id, const juce::String& name, const juce::String& tip)
        {
            styleLinearSliderCompact(s, kAccentGreen);
            styleLabelDark(lab, name, true);
            addAndMakeVisible(s);
            addAndMakeVisible(lab);
            s.setTooltip(tip);
            atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, id, s));
            s.textFromValueFunction = [](double v) { return juce::String(juce::roundToInt(v * 100.0)) + " %"; };
            s.valueFromTextFunction = [](const juce::String& t) { return juce::jlimit(0.0, 1.0, t.getDoubleValue() / 100.0); };
        };

        auto wireDb = [&](juce::Slider& s, juce::Label& lab, const char* id, const juce::String& name, const juce::String& tip)
        {
            styleLinearSliderCompact(s, kAccentBlue);
            styleLabelDark(lab, name, true);
            addAndMakeVisible(s);
            addAndMakeVisible(lab);
            s.setTooltip(tip);
            atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, id, s));
            s.textFromValueFunction = [](double v) { return juce::String(v, 1) + " dB"; };
            s.valueFromTextFunction = [](const juce::String& t) { return t.getDoubleValue(); };
        };

        wirePct(coreCrunch, coreCrunchL, "coreCrunch", "Crunch %",
                "Blend toward a harder curve on Roast low-chain and mid-chain saturation taps (between EQ bands).");
        styleLabelDark(roastCoreShapeL, "Core flavour", true);
        addAndMakeVisible(roastCoreShapeL);
        roastCoreShapeBox.setTooltip("Roast low-/mid-chain saturator recipe: Classic = original; others skew asymmetry, crunch blend, and drive growth (Dirt/Crunch still apply).");
        addAndMakeVisible(roastCoreShapeBox);
        if (auto* rc = dynamic_cast<juce::AudioParameterChoice*>(ap.getParameter("roastCoreShape")))
            roastCoreShapeBox.addItemList(rc->choices, 1);
        comboAtts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(ap, "roastCoreShape", roastCoreShapeBox));
        wireDb(roastPreEmphDb, roastPreEmphDbL, "roastPreEmphDb", "Pre HF dB",
                "High shelf (~3 kHz) before the first core - more fizz and crunch when driven.");
        wireDb(roastPostTiltDb, roastPostTiltDbL, "roastPostTiltDb", "Post tilt dB",
                "High shelf (~5.5 kHz) after the last core - dark (negative) or crispy (positive).");
        wirePct(roastBoostTrack, roastBoostTrackL, "roastBoostTrack", "Boost track %",
                "More drive when bands are boosted in dB - uses Motion snapshot gains when Motion is engaged, else EQ-tab values.");
        wirePct(roastMidChain, roastMidChainL, "roastMidChain", "Mid-chain %",
                "Extra core tap between Mid1 and Mid2 (amount x max of effective pre/post drive).");
        wirePct(roastLowChain, roastLowChainL, "roastLowChain", "Low-chain %",
                "Core tap after the low shelf, before Mid1 (amount x max of effective pre/post drive).");
        wirePct(roastPunch, roastPunchL, "roastPunch", "Punch %",
                "Fast peak-aware gain reduction before the first core so you can lean harder into sat.");
        wirePct(roastGlue, roastGlueL, "roastGlue", "Glue %",
                "Slower peak-aware leveling after lo-fi / before ring, to seat the distortion.");
        wirePct(roastLoFi, roastLoFiL, "roastLoFi", "Lo-fi %",
                "Bit-depth + hold decimation on the wet portion of the tail chain.");
        wirePct(roastRing, roastRingL, "roastRing", "Ring %",
                "AM-style ring in the low audio range - metallic edge at higher values.");
        wirePct(roastEnvDrive, roastEnvDriveL, "roastEnvDrive", "Env drive %",
                "Smoothed level at the pre-core input pushes effective sat drive (per channel).");
        wirePct(roastFlutter, roastFlutterL, "roastFlutter", "Flutter %",
                "Audio-rate wobble on effective drive (~42 Hz scaled by amount).");
        wirePct(roastStereoWide, roastStereoWideL, "roastStereoWide", "Stereo life %",
                "Offsets core 'life' phase on the right channel for wider moving saturation.");
        wireDb(roastOutputTrimDb, roastOutputTrimDbL, "roastOutputTrimDb", "Out trim dB",
                "Gain trim after the roast chain, before the Output tab limiter.");

        styleLabelDark(svfTitle, "Nonlinear SVF (VA resonator)", true);
        svfTitle.setFont(juce::Font(juce::FontOptions(11.0f).withStyle("Bold")));
        addAndMakeVisible(svfTitle);
        styleToggleDark(svfEnableToggle);
        svfEnableToggle.setTooltip("Trapezoidal ZDF SVF bandpass, parallel into the hot path after the high shelf (before Core 2).");
        addAndMakeVisible(svfEnableToggle);
        batts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(ap, "svfEnable", svfEnableToggle));

        wirePct(svfMix, svfMixL, "svfMix", "SVF mix %",
                "Dry + wet bandpass injection; use modest mix at high Q.");
        styleLinearSliderCompact(svfCf, kAccentBlue);
        styleLabelDark(svfCfL, "SVF Hz", true);
        addAndMakeVisible(svfCf);
        addAndMakeVisible(svfCfL);
        atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "svfCf", svfCf));
        svfCf.textFromValueFunction = [](double v) { return juce::String(juce::roundToInt(v)) + " Hz"; };
        svfCf.valueFromTextFunction = [](const juce::String& t) { return t.getDoubleValue(); };
        svfCf.setTooltip("Bandpass centre (skewed Hz).");

        styleLinearSliderCompact(svfQ, kAccentBlue);
        styleLabelDark(svfQL, "SVF Q", true);
        addAndMakeVisible(svfQ);
        addAndMakeVisible(svfQL);
        atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "svfQ", svfQ));
        svfQ.textFromValueFunction = [](double v) { return juce::String(v, 2); };
        svfQ.valueFromTextFunction = [](const juce::String& t) { return t.getDoubleValue(); };
        svfQ.setTooltip("Resonance (higher Q = narrower peak, more energy at cf).");

        wirePct(svfDrive, svfDriveL, "svfDrive", "SVF VA %",
                "Saturates the SVF loop input for VA-style resonance limiting / crunch at extremes.");
        wireDb(svfGainDb, svfGainDbL, "svfGainDb", "SVF BP dB",
                "Gain of the bandpass branch before it is mixed in (+/-18 dB).");

        constexpr int kRoastValW = 66;
        constexpr int kRoastValH = 20;
        constexpr int kRoastHzW = 76;
        auto styleRoastValueBox = [&](juce::Slider& s, int boxW)
        {
            s.setTextBoxStyle(juce::Slider::TextBoxRight, false, boxW, kRoastValH);
        };
        for (auto* s : { &coreCrunch,
                         &roastPreEmphDb,
                         &roastPostTiltDb,
                         &roastBoostTrack,
                         &roastMidChain,
                         &roastLowChain,
                         &roastPunch,
                         &roastGlue,
                         &roastLoFi,
                         &roastRing,
                         &roastEnvDrive,
                         &roastFlutter,
                         &roastStereoWide,
                         &roastOutputTrimDb,
                         &svfMix,
                         &svfDrive })
            styleRoastValueBox(*s, kRoastValW);
        styleRoastValueBox(svfCf, kRoastHzW);
        styleRoastValueBox(svfQ, kRoastValW);
        styleRoastValueBox(svfGainDb, kRoastValW);
    }

    void paint(juce::Graphics& g) override { g.fillAll(kPanelBlack); }

    int getMinimumContentHeight() const noexcept
    {
        constexpr int kChrome = 28 + 6 + 22 + 6 + 6;
        constexpr int kIntroCap = 96;
        constexpr int kRoastSliderRows = 9;
        constexpr int kRoastRowH = 32;
        constexpr int kGridPad = 6;
        constexpr int kSvfStripH = 108;
        return kChrome + kIntroCap + 6 + kRoastSliderRows * kRoastRowH + kGridPad + kSvfStripH + 8;
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced(8);
        auto top = b.removeFromTop(28);
        programLabel.setBounds(top.removeFromLeft(96).reduced(0, 4));
        programBox.setBounds(top.removeFromLeft(juce::jmin(200, top.getWidth())).reduced(0, 2));
        top.removeFromLeft(10);
        oversampleLabel.setBounds(top.removeFromLeft(22).reduced(0, 4));
        oversampleBox.setBounds(top.removeFromLeft(72).reduced(0, 2));
        b.removeFromTop(6);
        auto linRow = b.removeFromTop(22);
        infoBtn.setBounds(linRow.removeFromRight(26));
        linRow.removeFromRight(4);
        linearEqToggle.setBounds(linRow);
        b.removeFromTop(6);

        constexpr int kRoastSliderRows = 9;
        constexpr int kRoastRowH = 32;
        constexpr int kGridPad = 6;
        constexpr int kSvfStripH = 108;
        const int gridH = kRoastSliderRows * kRoastRowH + kGridPad;
        const int bandAvail = juce::jmax(1, b.getHeight());
        const int bodyNeed = gridH + kSvfStripH;
        const int slack = juce::jmax(0, bandAvail - bodyNeed);
        if (slack > 0)
            b.removeFromBottom(slack);

        const int hBody = b.getHeight();
        const int gridHUse = juce::jmin(gridH, juce::jmax(kRoastSliderRows * 26, hBody - 72));
        const int rowH = juce::jmin(kRoastRowH, juce::jmax(26, gridHUse / kRoastSliderRows));
        auto gridBand = b.removeFromTop(gridHUse);
        constexpr int kRoastMidGutter = 8;
        const int inner = juce::jmax(1, gridBand.getWidth() - kRoastMidGutter);
        const int half = inner / 2;
        {
            auto flav = gridBand.removeFromTop(rowH);
            roastCoreShapeL.setBounds(flav.removeFromLeft(96).withTrimmedTop(4));
            roastCoreShapeBox.setBounds(flav.reduced(4, 2));
        }
        auto left = gridBand.removeFromLeft(half);
        auto right = gridBand;
        right.removeFromLeft(8);
        placeRow(left, coreCrunchL, coreCrunch, rowH);
        placeRow(left, roastPreEmphDbL, roastPreEmphDb, rowH);
        placeRow(left, roastPostTiltDbL, roastPostTiltDb, rowH);
        placeRow(left, roastBoostTrackL, roastBoostTrack, rowH);
        placeRow(left, roastMidChainL, roastMidChain, rowH);
        placeRow(left, roastLowChainL, roastLowChain, rowH);
        placeRow(left, roastPunchL, roastPunch, rowH);
        placeRow(left, roastGlueL, roastGlue, rowH);
        placeRow(right, roastLoFiL, roastLoFi, rowH);
        placeRow(right, roastRingL, roastRing, rowH);
        placeRow(right, roastEnvDriveL, roastEnvDrive, rowH);
        placeRow(right, roastFlutterL, roastFlutter, rowH);
        placeRow(right, roastStereoWideL, roastStereoWide, rowH);
        placeRow(right, roastOutputTrimDbL, roastOutputTrimDb, rowH);

        auto svfArea = b;
        svfTitle.setBounds(svfArea.removeFromTop(14));
        svfArea.removeFromTop(4);
        constexpr int kSvfCoreH = 24 + 6 + 36;
        const int svfSlack = juce::jmin(18, juce::jmax(0, svfArea.getHeight() - kSvfCoreH));
        const int bumpR1 = svfSlack / 3;
        const int bumpR2 = svfSlack - bumpR1;
        auto svfR1 = svfArea.removeFromTop(24 + bumpR1);
        svfEnableToggle.setBounds(svfR1.removeFromLeft(100));
        svfR1.removeFromLeft(8);
        svfMixL.setBounds(svfR1.removeFromLeft(52).withTrimmedTop(4));
        svfMix.setBounds(svfR1.removeFromLeft(juce::jmin(220, juce::jmax(120, svfR1.getWidth()))).reduced(0, 2));
        svfArea.removeFromTop(6);
        auto svfR2 = svfArea.removeFromTop(36 + bumpR2);
        const int gap = 8;
        const int colW = juce::jmax(72, (svfR2.getWidth() - 3 * gap) / 4);
        const int svfKnobStripH = svfR2.getHeight();
        auto takeCol = [&](juce::Label& lab, juce::Slider& sl)
        {
            auto c = svfR2.removeFromLeft(colW);
            lab.setBounds(c.getX(), c.getY(), colW, 12);
            sl.setBounds(c.getX(), c.getY() + 14, colW, juce::jmax(22, svfKnobStripH - 16));
            svfR2.removeFromLeft(gap);
        };
        takeCol(svfCfL, svfCf);
        takeCol(svfQL, svfQ);
        takeCol(svfDriveL, svfDrive);
        takeCol(svfGainDbL, svfGainDb);
    }
};

struct ParaEQ301AudioProcessorEditor::AnharmTabContent : public juce::Component
{
    juce::Label intro;
    juce::TextButton infoBtn;
    juce::ToggleButton univBellToggle { "Universal bell (Orfanidis)" };
    juce::ToggleButton anharmOnToggle { "Inharmonic partial bank" };
    juce::Slider anharmFund;
    juce::Label anharmFundL;
    juce::Slider anharmB;
    juce::Label anharmBL;
    juce::Slider anharmPartials;
    juce::Label anharmPartialsL;
    juce::Slider anharmMix;
    juce::Label anharmMixL;
    juce::Slider anharmPerDb;
    juce::Label anharmPerDbL;
    juce::Slider anharmQ;
    juce::Label anharmQL;
    juce::Slider anharmNl;
    juce::Label anharmNlL;
    juce::Slider anharmEnvQ;
    juce::Label anharmEnvQL;

    /** Label column + wide horizontal slider (text box on slider's right). */
    static void placeWideSliderRow(juce::Rectangle<int>& area, juce::Label& cap, juce::Slider& sl, int rowH, int labelColW)
    {
        auto row = area.removeFromTop(rowH);
        cap.setBounds(row.getX(), row.getY() + (rowH - 14) / 2, labelColW, 14);
        const int slX = row.getX() + labelColW + 8;
        sl.setBounds(slX, row.getY(), juce::jmax(160, row.getRight() - slX), rowH);
    }

    AnharmTabContent(juce::AudioProcessorValueTreeState& ap,
                     std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>>& atts,
                     std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>>& batts)
    {
        styleLabelDark(intro,
                       "1) Universal bell: Orfanidis-style mid peaking (tighter bandwidth vs naive RBJ at high cf). "
                       "2) Inharmonic bank: stiff-string partials f_n = n*f0*sqrt(1+B(n^2-1)), parallel after SVF. "
                       "3) Level-dependent Q + wet tanh: env->Q widens peaks when signal is hot; Anharm wet sat soft-limits the bank sum.",
                       true);
        intro.setJustificationType(juce::Justification::topLeft);
        intro.setFont(juce::Font(juce::FontOptions()
                                     .withName(juce::Font::getDefaultMonospacedFontName())
                                     .withHeight(11.0f)));
        addAndMakeVisible(intro);
        wireInfoButton(infoBtn, intro, "Anharm");
        addAndMakeVisible(infoBtn);

        styleToggleDark(univBellToggle);
        univBellToggle.setTooltip("When on, Mid1/Mid2 use Orfanidis peaking design (Dw from Q via sinh mapping, Gb=sqrt(G)).");
        addAndMakeVisible(univBellToggle);
        batts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(ap, "univBell", univBellToggle));

        styleToggleDark(anharmOnToggle);
        anharmOnToggle.setTooltip("Parallel peaking filters on stiff-string partials of the fundamental (before Core 2).");
        addAndMakeVisible(anharmOnToggle);
        batts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(ap, "anharmBankEnable", anharmOnToggle));

        constexpr int kAnharmValueBoxW = 78;
        constexpr int kAnharmValueBoxH = 20;
        auto styleAnharmValueBox = [&](juce::Slider& s)
        {
            s.setTextBoxStyle(juce::Slider::TextBoxRight, false, kAnharmValueBoxW, kAnharmValueBoxH);
        };

        auto wirePct = [&](juce::Slider& s, juce::Label& lab, const char* id, const juce::String& name, const juce::String& tip)
        {
            styleLinearSliderCompact(s, kAccentGreen);
            styleLabelDark(lab, name, true);
            addAndMakeVisible(s);
            addAndMakeVisible(lab);
            s.setTooltip(tip);
            atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, id, s));
            s.textFromValueFunction = [](double v) { return juce::String(juce::roundToInt(v * 100.0)) + " %"; };
            s.valueFromTextFunction = [](const juce::String& t) { return juce::jlimit(0.0, 1.0, t.getDoubleValue() / 100.0); };
            styleAnharmValueBox(s);
        };

        auto wireDb = [&](juce::Slider& s, juce::Label& lab, const char* id, const juce::String& name, const juce::String& tip)
        {
            styleLinearSliderCompact(s, kAccentBlue);
            styleLabelDark(lab, name, true);
            addAndMakeVisible(s);
            addAndMakeVisible(lab);
            s.setTooltip(tip);
            atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, id, s));
            s.textFromValueFunction = [](double v) { return juce::String(v, 1) + " dB"; };
            s.valueFromTextFunction = [](const juce::String& t) { return t.getDoubleValue(); };
            styleAnharmValueBox(s);
        };

        styleLinearSliderCompact(anharmFund, kAccentBlue);
        styleLabelDark(anharmFundL, "Fundamental Hz", true);
        addAndMakeVisible(anharmFund);
        addAndMakeVisible(anharmFundL);
        atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "anharmFundHz", anharmFund));
        anharmFund.textFromValueFunction = [](double v) { return juce::String(juce::roundToInt(v)) + " Hz"; };
        anharmFund.valueFromTextFunction = [](const juce::String& t) { return t.getDoubleValue(); };
        styleAnharmValueBox(anharmFund);

        styleLinearSliderCompact(anharmB, kAccentBlue);
        styleLabelDark(anharmBL, "Inharmonicity B", true);
        addAndMakeVisible(anharmB);
        addAndMakeVisible(anharmBL);
        atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "anharmInharmB", anharmB));
        styleAnharmValueBox(anharmB);
        anharmB.textFromValueFunction = [](double v)
        {
            return juce::String::formatted("%g", (float) v);
        };
        anharmB.valueFromTextFunction = [](const juce::String& t) { return t.getDoubleValue(); };

        styleLinearSliderCompact(anharmPartials, kAccentGreen);
        styleLabelDark(anharmPartialsL, "Partials (2-6)", true);
        addAndMakeVisible(anharmPartials);
        addAndMakeVisible(anharmPartialsL);
        atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "anharmPartials", anharmPartials));
        anharmPartials.setRange(2.0, 6.0, 1.0);
        anharmPartials.textFromValueFunction = [](double v) { return juce::String(juce::roundToInt(v)); };
        anharmPartials.valueFromTextFunction = [](const juce::String& t) { return (double) t.getIntValue(); };
        styleAnharmValueBox(anharmPartials);

        wirePct(anharmMix, anharmMixL, "anharmMix", "Bank mix %", "Wet amount of parallel inharmonic peak sum (after SVF, before Core 2).");
        wireDb(anharmPerDb, anharmPerDbL, "anharmPerPartialDb", "1st partial dB", "Gain of partial n=1; higher partials roll off ~2.2 dB each.");
        styleLinearSliderCompact(anharmQ, kAccentBlue);
        styleLabelDark(anharmQL, "Bank Q", true);
        addAndMakeVisible(anharmQ);
        addAndMakeVisible(anharmQL);
        atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "anharmQ", anharmQ));
        anharmQ.textFromValueFunction = [](double v) { return juce::String(v, 1); };
        anharmQ.valueFromTextFunction = [](const juce::String& t) { return t.getDoubleValue(); };
        styleAnharmValueBox(anharmQ);

        wirePct(anharmNl, anharmNlL, "anharmNl", "Wet sat %", "tanh on the summed bank injection - amplitude-dependent narrowing of large resonant adds.");
        wirePct(anharmEnvQ, anharmEnvQL, "anharmEnvQ", "Env->Q %", "Follower on |x| lowers effective Q when the band is driven (anharmonic resonance curve).");
    }

    int getMinimumContentHeight() const noexcept
    {
        constexpr int kOuterPad = 16;
        constexpr int kIntroMax = 100;
        constexpr int kAfterIntro = 6 + 26 + 6;
        constexpr int kAnharmRowH = 32;
        constexpr int kParamRows = 8;
        constexpr int kRows = kParamRows * kAnharmRowH;
        return kOuterPad + kIntroMax + kAfterIntro + kRows + 8;
    }

    void paint(juce::Graphics& g) override { g.fillAll(kPanelBlack); }

    void resized() override
    {
        auto b = getLocalBounds().reduced(8);
        auto toggles = b.removeFromTop(26);
        infoBtn.setBounds(toggles.removeFromRight(26));
        toggles.removeFromRight(4);
        univBellToggle.setBounds(toggles.removeFromLeft(220));
        toggles.removeFromLeft(12);
        anharmOnToggle.setBounds(toggles.removeFromLeft(200));
        b.removeFromTop(6);
        constexpr int kParamRows = 8;
        constexpr int kAnharmRowH = 32;
        const int paramNeed = kParamRows * kAnharmRowH;
        int bodyH = juce::jmax(1, b.getHeight());
        if (bodyH > paramNeed)
            b.removeFromBottom(bodyH - paramNeed);
        bodyH = juce::jmax(1, b.getHeight());
        const int rowH = juce::jmin(kAnharmRowH, juce::jmax(20, bodyH / kParamRows));
        const int labelColW = juce::jlimit(120, 188, b.getWidth() / 3);

        placeWideSliderRow(b, anharmFundL, anharmFund, rowH, labelColW);
        placeWideSliderRow(b, anharmBL, anharmB, rowH, labelColW);
        placeWideSliderRow(b, anharmPartialsL, anharmPartials, rowH, labelColW);
        placeWideSliderRow(b, anharmMixL, anharmMix, rowH, labelColW);
        placeWideSliderRow(b, anharmPerDbL, anharmPerDb, rowH, labelColW);
        placeWideSliderRow(b, anharmQL, anharmQ, rowH, labelColW);
        placeWideSliderRow(b, anharmNlL, anharmNl, rowH, labelColW);
        placeWideSliderRow(b, anharmEnvQL, anharmEnvQ, rowH, labelColW);
    }
};

struct ParaEQ301AudioProcessorEditor::RoastTabScrollHost : public juce::Viewport
{
    explicit RoastTabScrollHost(ParaEQ301AudioProcessor& processor,
                                juce::AudioProcessorValueTreeState& ap,
                                std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>>& atts,
                                std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>>& batts,
                                std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>>& comboAtts)
        : content(std::make_unique<RoastTabContent>(processor, ap, atts, batts, comboAtts))
    {
        setViewedComponent(content.get(), false);
        setScrollBarsShown(true, false);
        setScrollBarThickness(9);
    }

    void resized() override
    {
        const int w = juce::jmax(1, getWidth());
        const int h = juce::jmax(1, getHeight());
        const int minH = content->getMinimumContentHeight();
        content->setBounds(0, 0, w, juce::jmax(h, minH));
        juce::Viewport::resized();
    }

    std::unique_ptr<RoastTabContent> content;
};

struct ParaEQ301AudioProcessorEditor::AnharmTabScrollHost : public juce::Viewport
{
    explicit AnharmTabScrollHost(juce::AudioProcessorValueTreeState& ap,
                                 std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>>& atts,
                                 std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>>& batts)
        : content(std::make_unique<AnharmTabContent>(ap, atts, batts))
    {
        setViewedComponent(content.get(), false);
        setScrollBarsShown(true, false);
        setScrollBarThickness(9);
    }

    void resized() override
    {
        const int w = juce::jmax(1, getWidth());
        const int h = juce::jmax(1, getHeight());
        const int minH = content->getMinimumContentHeight();
        content->setBounds(0, 0, w, juce::jmax(h, minH));
        juce::Viewport::resized();
    }

    std::unique_ptr<AnharmTabContent> content;
};

struct ParaEQ301AudioProcessorEditor::ParametricTabContent : public juce::Component
{
    juce::Label intro;
    juce::TextButton infoBtn;
    juce::ToggleButton aprEnableToggle { "APR" };
    juce::Slider aprMix;
    juce::Label aprMixL;
    juce::Slider aprBaseHz;
    juce::Label aprBaseHzL;
    juce::Slider aprQ;
    juce::Label aprQL;
    juce::Slider aprPumpHz;
    juce::Label aprPumpHzL;
    juce::Slider aprPumpDepth;
    juce::Label aprPumpDepthL;
    juce::Slider aprAutoTrack;
    juce::Label aprAutoTrackL;
    juce::Slider aprDrive;
    juce::Label aprDriveL;

    static void placeWideSliderRow(juce::Rectangle<int>& area, juce::Label& cap, juce::Slider& sl, int rowH, int labelColW)
    {
        auto row = area.removeFromTop(rowH);
        cap.setBounds(row.getX(), row.getY() + (rowH - 14) / 2, labelColW, 14);
        const int slX = row.getX() + labelColW + 8;
        sl.setBounds(slX, row.getY(), juce::jmax(160, row.getRight() - slX), rowH);
    }

    ParametricTabContent(juce::AudioProcessorValueTreeState& ap,
                         std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>>& atts,
                         std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>>& batts)
    {
        styleLabelDark(intro,
                       "Parallel bandpass resonator: last in the roast chain (after ring / pink balance), and last in Linear EQ only mode too (after the four bands + pink).",
                       true);
        intro.setJustificationType(juce::Justification::topLeft);
        intro.setFont(juce::Font(juce::FontOptions()
                                     .withName(juce::Font::getDefaultMonospacedFontName())
                                     .withHeight(11.0f)));
        addAndMakeVisible(intro);
        wireInfoButton(infoBtn, intro, "APR");
        addAndMakeVisible(infoBtn);

        styleToggleDark(aprEnableToggle);
        aprEnableToggle.setTooltip("Parallel bandpass resonator. Needs Mix % > 0 to hear wet signal.");
        addAndMakeVisible(aprEnableToggle);
        batts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(ap, "aprEnable", aprEnableToggle));

        auto wirePct = [&](juce::Slider& s, juce::Label& lab, const char* id, const juce::String& name, const juce::String& tip)
        {
            styleLinearSliderCompact(s, kAccentGreen);
            styleLabelDark(lab, name, true);
            addAndMakeVisible(s);
            addAndMakeVisible(lab);
            s.setTooltip(tip);
            atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, id, s));
            s.textFromValueFunction = [](double v) { return juce::String(juce::roundToInt(v * 100.0)) + " %"; };
            s.valueFromTextFunction = [](const juce::String& t) { return juce::jlimit(0.0, 1.0, t.getDoubleValue() / 100.0); };
        };

        wirePct(aprMix, aprMixL, "aprMix", "Mix %", "How loud the resonator is.");

        styleLinearSliderCompact(aprBaseHz, kAccentBlue);
        styleLabelDark(aprBaseHzL, "Base Hz", true);
        addAndMakeVisible(aprBaseHz);
        addAndMakeVisible(aprBaseHzL);
        aprBaseHz.setTooltip("Resonator centre frequency before auto-tracking and pump modulation.");
        atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "aprBaseHz", aprBaseHz));
        aprBaseHz.textFromValueFunction = [](double v) { return juce::String(juce::roundToInt(v)) + " Hz"; };
        aprBaseHz.valueFromTextFunction = [](const juce::String& t) { return t.getDoubleValue(); };

        styleLinearSliderCompact(aprQ, kAccentGreen);
        styleLabelDark(aprQL, "Q", true);
        addAndMakeVisible(aprQ);
        addAndMakeVisible(aprQL);
        aprQ.setTooltip("Bandwidth (higher Q = narrower peak). Very high Q only passes a thin slice of spectrum - set Base Hz near strong content in the source, or expect a subtle tail.");
        atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "aprQ", aprQ));
        aprQ.textFromValueFunction = [](double v) { return juce::String(v, 2); };
        aprQ.valueFromTextFunction = [](const juce::String& t) { return t.getDoubleValue(); };

        styleLinearSliderCompact(aprPumpHz, kAccentBlue);
        styleLabelDark(aprPumpHzL, "Pump Hz", true);
        addAndMakeVisible(aprPumpHz);
        addAndMakeVisible(aprPumpHzL);
        aprPumpHz.setTooltip("Rate of sinusoidal modulation of effective centre frequency (parametric-style pumping).");
        atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "aprPumpHz", aprPumpHz));
        aprPumpHz.textFromValueFunction = [](double v) { return juce::String(v, 2) + " Hz"; };
        aprPumpHz.valueFromTextFunction = [](const juce::String& t) { return t.getDoubleValue(); };

        wirePct(aprPumpDepth, aprPumpDepthL, "aprPumpDepth", "Pump depth %", "Depth of pump modulation around the tracked centre frequency.");
        wirePct(aprAutoTrack, aprAutoTrackL, "aprAutoTrack", "Auto track %", "How strongly smoothed |x| shifts centre frequency upward when the band is loud.");
        wirePct(aprDrive, aprDriveL, "aprDrive", "Drive %", "Nonlinear bandpass loop drive (amplitude-dependent resonance limiting).");

        constexpr int kAprValH = 20;
        constexpr int kAprHzBoxW = 92;
        constexpr int kAprPctBoxW = 92;
        constexpr int kAprQBoxW = 92;
        auto styleAprReadout = [&](juce::Slider& s, int boxW)
        {
            s.setTextBoxStyle(juce::Slider::TextBoxRight, false, boxW, kAprValH);
        };
        styleAprReadout(aprMix, kAprPctBoxW);
        styleAprReadout(aprBaseHz, kAprHzBoxW);
        styleAprReadout(aprQ, kAprQBoxW);
        styleAprReadout(aprPumpHz, kAprHzBoxW);
        styleAprReadout(aprPumpDepth, kAprPctBoxW);
        styleAprReadout(aprAutoTrack, kAprPctBoxW);
        styleAprReadout(aprDrive, kAprPctBoxW);
    }

    int getMinimumContentHeight() const noexcept
    {
        constexpr int kOuterPad = 16;
        constexpr int kIntroMax = 120;
        constexpr int kAfterIntro = 6 + 26 + 6;
        constexpr int kRowH = 32;
        constexpr int kParamRows = 7;
        return kOuterPad + kIntroMax + kAfterIntro + kParamRows * kRowH + 10;
    }

    void paint(juce::Graphics& g) override { g.fillAll(kPanelBlack); }

    void resized() override
    {
        auto b = getLocalBounds().reduced(8);
        auto toggles = b.removeFromTop(26);
        infoBtn.setBounds(toggles.removeFromRight(26));
        toggles.removeFromRight(4);
        aprEnableToggle.setBounds(toggles.removeFromLeft(280));
        b.removeFromTop(6);
        constexpr int kParamRows = 7;
        constexpr int kRowH = 32;
        const int paramNeed = kParamRows * kRowH;
        int bodyH = juce::jmax(1, b.getHeight());
        if (bodyH > paramNeed)
            b.removeFromBottom(bodyH - paramNeed);
        bodyH = juce::jmax(1, b.getHeight());
        const int rowH = juce::jmin(kRowH, juce::jmax(20, bodyH / kParamRows));
        const int labelColW = juce::jlimit(120, 188, b.getWidth() / 3);

        placeWideSliderRow(b, aprMixL, aprMix, rowH, labelColW);
        placeWideSliderRow(b, aprBaseHzL, aprBaseHz, rowH, labelColW);
        placeWideSliderRow(b, aprQL, aprQ, rowH, labelColW);
        placeWideSliderRow(b, aprPumpHzL, aprPumpHz, rowH, labelColW);
        placeWideSliderRow(b, aprPumpDepthL, aprPumpDepth, rowH, labelColW);
        placeWideSliderRow(b, aprAutoTrackL, aprAutoTrack, rowH, labelColW);
        placeWideSliderRow(b, aprDriveL, aprDrive, rowH, labelColW);
    }
};

struct ParaEQ301AudioProcessorEditor::ParametricTabScrollHost : public juce::Viewport
{
    explicit ParametricTabScrollHost(juce::AudioProcessorValueTreeState& ap,
                                     std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>>& atts,
                                     std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>>& batts)
        : content(std::make_unique<ParametricTabContent>(ap, atts, batts))
    {
        setViewedComponent(content.get(), false);
        setScrollBarsShown(true, false);
        setScrollBarThickness(9);
    }

    void resized() override
    {
        const int w = juce::jmax(1, getWidth());
        const int h = juce::jmax(1, getHeight());
        const int minH = content->getMinimumContentHeight();
        content->setBounds(0, 0, w, juce::jmax(h, minH));
        juce::Viewport::resized();
    }

    std::unique_ptr<ParametricTabContent> content;
};

struct ParaEQ301AudioProcessorEditor::ShaperTabContent : public juce::Component
{
    juce::Label intro;
    juce::TextButton infoBtn;
    juce::Label shaperModeL;
    juce::ComboBox shaperModeBox;
    juce::Slider shaperMix;
    juce::Label shaperMixL;
    juce::Slider shaperPreGain;
    juce::Label shaperPreGainL;
    juce::Slider shaperPostTrim;
    juce::Label shaperPostTrimL;
    juce::Label magnetHead;
    juce::Slider magDrive;
    juce::Label magDriveL;
    juce::Slider magTilt;
    juce::Label magTiltL;
    juce::Slider magBias;
    juce::Label magBiasL;
    juce::Slider magTiltLimit;
    juce::Label magTiltLimitL;
    juce::Slider magFeedback;
    juce::Label magFeedbackL;
    juce::Slider magOut;
    juce::Label magOutL;
    juce::ComboBox magShape;
    juce::Label magShapeL;
    juce::Slider magEnergy;
    juce::Label magEnergyL;
    juce::Slider magEnergyMs;
    juce::Label magEnergyMsL;
    juce::Label chebyHead;
    juce::Slider chebyHarmMacro;
    juce::Label chebyHarmMacroL;
    juce::Slider chebyPolyPow;
    juce::Label chebyPolyPowL;
    juce::ToggleButton chebyDetailToggle { "Show H2-H13 detail sliders" };
    juce::Slider chebyYL;
    juce::Label chebyYLL;
    juce::Slider chebyYC;
    juce::Label chebyYCL;
    juce::Slider chebyYR;
    juce::Label chebyYRL;
    std::array<juce::Slider, 12> chebyH {};
    std::array<juce::Label, 12> chebyHL {};
    std::array<juce::Slider, 12> chebyHPow {};
    std::array<juce::Label, 12> chebyHPowL {};

    static void placeWideSliderRow(juce::Rectangle<int>& area, juce::Label& cap, juce::Slider& sl, int rowH, int labelColW)
    {
        auto row = area.removeFromTop(rowH);
        cap.setBounds(row.getX(), row.getY() + (rowH - 14) / 2, labelColW, 14);
        const int slX = row.getX() + labelColW + 8;
        sl.setBounds(slX, row.getY(), juce::jmax(160, row.getRight() - slX), rowH);
    }

    static void placeVertCol(juce::Rectangle<int>& row, juce::Label& cap, juce::Slider& sl, int colW)
    {
        if (row.getWidth() < colW) return;
        auto col = row.removeFromLeft(colW);
        cap.setBounds(col.getX(), col.getY(), col.getWidth(), 14);
        sl.setBounds(col.getX(), col.getY() + 14, col.getWidth(), col.getHeight() - 14);
        row.removeFromLeft(4);
    }

    void styleVertSlider(juce::Slider& s)
    {
        s.setSliderStyle(juce::Slider::LinearVertical);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 18);
        s.setColour(juce::Slider::trackColourId, kAccentGreen.withAlpha(0.9f));
        s.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff1a1a1a));
        s.setColour(juce::Slider::thumbColourId, kAccentBlue);
    }

    ShaperTabContent(juce::AudioProcessorValueTreeState& ap,
                     std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>>& atts,
                     std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>>&,
                     std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>>& comboAtts)
    {
        styleLabelDark(intro,
                       "Paketti-style harmonic shaping (ported from Paketti Chebyshev / Magnet tools): Chebyshev = Bezier pre-curve plus "
                       "weighted T2-T13 harmonics via a normalized LUT (rebuilt on a background thread; audio falls back to a one-shot sync build if the worker lags). "
                       "Harmonics macro scales all partial weights together. Magnet = asymmetric soft saturation with feedback and slew. "
                       "Inserted after ThrillMe 1, before the EQ chain, as parallel harmonic delta (mix times (y - u)). Oversampled host rate widens Magnet slew steps.",
                       true);
        intro.setJustificationType(juce::Justification::topLeft);
        intro.setFont(juce::Font(juce::FontOptions()
                                     .withName(juce::Font::getDefaultMonospacedFontName())
                                     .withHeight(11.0f)));
        addAndMakeVisible(intro);
        wireInfoButton(infoBtn, intro, "Shaper");
        addAndMakeVisible(infoBtn);

        styleLabelDark(shaperModeL, "Shaper mode", true);
        addAndMakeVisible(shaperModeL);
        shaperModeBox.setTooltip("Off | Magnet (asymmetric sat) | Chebyshev (controlled harmonics).");
        addAndMakeVisible(shaperModeBox);
        if (auto* pc = dynamic_cast<juce::AudioParameterChoice*>(ap.getParameter("shaperMode")))
            shaperModeBox.addItemList(pc->choices, 1);
        comboAtts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(ap, "shaperMode", shaperModeBox));
        shaperModeBox.onChange = [this]
        {
            resized();
            if (auto* vp = findParentComponentOfClass<juce::Viewport>())
                vp->resized();
        };

        auto wirePct = [&](juce::Slider& s, juce::Label& lab, const char* id, const juce::String& name, const juce::String& tip)
        {
            styleLinearSliderCompact(s, kAccentGreen);
            styleLabelDark(lab, name, true);
            addAndMakeVisible(s);
            addAndMakeVisible(lab);
            s.setTooltip(tip);
            atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, id, s));
            s.textFromValueFunction = [](double v) { return juce::String(juce::roundToInt(v * 100.0)) + " %"; };
            s.valueFromTextFunction = [](const juce::String& t) { return juce::jlimit(0.0, 1.0, t.getDoubleValue() / 100.0); };
        };

        wirePct(shaperMix, shaperMixL, "shaperMix", "Mix %", "Blend of harmonic delta added to the signal.");
        styleLinearSliderCompact(shaperPreGain, kAccentBlue);
        styleLabelDark(shaperPreGainL, "Input", true);
        addAndMakeVisible(shaperPreGain);
        addAndMakeVisible(shaperPreGainL);
        shaperPreGain.setTooltip("Gain into the normalized shaper domain before clamp to ±1.");
        atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "shaperPreGain", shaperPreGain));
        shaperPreGain.textFromValueFunction = [](double v) { return juce::String(v, 2) + " x"; };
        shaperPreGain.valueFromTextFunction = [](const juce::String& t) { return t.getDoubleValue(); };

        styleLinearSliderCompact(shaperPostTrim, kAccentBlue);
        styleLabelDark(shaperPostTrimL, "Trim", true);
        addAndMakeVisible(shaperPostTrim);
        addAndMakeVisible(shaperPostTrimL);
        shaperPostTrim.setTooltip("Gain on (y-u) before applying mix.");
        atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "shaperPostTrim", shaperPostTrim));
        shaperPostTrim.textFromValueFunction = [](double v) { return juce::String(v, 2) + " x"; };
        shaperPostTrim.valueFromTextFunction = [](const juce::String& t) { return t.getDoubleValue(); };

        styleLabelDark(magnetHead, "Magnet", true);
        magnetHead.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
        addAndMakeVisible(magnetHead);

        auto wireMag = [&](juce::Slider& s, juce::Label& lab, const char* id, const juce::String& name, const juce::String& tip)
        {
            styleLinearSliderCompact(s, kAccentGreen);
            styleLabelDark(lab, name, true);
            addAndMakeVisible(s);
            addAndMakeVisible(lab);
            s.setTooltip(tip);
            atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, id, s));
            s.textFromValueFunction = [](double v) { return juce::String(v, 2); };
            s.valueFromTextFunction = [](const juce::String& t) { return t.getDoubleValue(); };
        };
        wireMag(magDrive, magDriveL, "magDrive", "Drive", "Exponent base 2^drive into pos/neg softsat gains.");
        wireMag(magTilt, magTiltL, "magTilt", "Tilt", "Skews tone between positive and negative halves.");
        wireMag(magBias, magBiasL, "magBias", "Bias", "Asymmetry between positive and negative lobes.");
        wirePct(magTiltLimit, magTiltLimitL, "magTiltLimit", "Slew %", "Higher = faster slew toward target (less low-pass on motion).");
        wirePct(magFeedback, magFeedbackL, "magFeedback", "Feedback %", "Feeds previous output into tilt (program-dependent).");
        wireMag(magOut, magOutL, "magOut", "Out", "Output gain inside Magnet path (before delta trim).");

        styleLabelDark(magShapeL, "Sat type", true);
        addAndMakeVisible(magShapeL);
        magShape.addItem("Soft", 1);
        magShape.addItem("Tanh", 2);
        magShape.setTooltip("Saturator shape inside MagnetShaper. Soft = v/(1+|v|) (legacy). Tanh = std::tanh(v) (Lassi's stated ingredient).");
        addAndMakeVisible(magShape);
        comboAtts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(ap, "magShape", magShape));

        wirePct(magEnergy, magEnergyL, "magEnergy", "Energy %",
                "Energy accumulator (one-pole on |x|) feeding effective drive — Lassi's energy-accumulation-in-wave-motion ingredient. 0 = off.");
        styleLinearSliderCompact(magEnergyMs, kAccentGreen);
        styleLabelDark(magEnergyMsL, "Energy ms", true);
        addAndMakeVisible(magEnergyMs);
        addAndMakeVisible(magEnergyMsL);
        magEnergyMs.setTooltip("Time constant for the energy follower (ms). Faster = more responsive, snappier; slower = sustained loudness drives the saturator.");
        atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "magEnergyMs", magEnergyMs));
        magEnergyMs.textFromValueFunction = [](double v) { return juce::String(v, 1) + " ms"; };
        magEnergyMs.valueFromTextFunction = [](const juce::String& t) { return t.getDoubleValue(); };

        styleLabelDark(chebyHead, "Chebyshev curve & harmonics", true);
        chebyHead.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
        addAndMakeVisible(chebyHead);

        wirePct(chebyHarmMacro, chebyHarmMacroL, "chebyHarmMacro", "Harmonics %",
                 "Scales every H2-H13 weight together (shape from detail sliders, overall amount from here).");
        styleLinearSliderCompact(chebyPolyPow, kAccentGreen);
        styleLabelDark(chebyPolyPowL, "Poly pow", true);
        addAndMakeVisible(chebyPolyPow);
        addAndMakeVisible(chebyPolyPowL);
        chebyPolyPow.setTooltip("Per-polynomial signed pow() shaper applied to each Tn(x) before the weighted sum (Lassi's original tuning). "
                                "1.0 = identity. < 1.0 = compress harmonic curves. > 1.0 = expand. DC blocker engages automatically when != 1.0.");
        atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "chebyPolyPow", chebyPolyPow));
        chebyPolyPow.textFromValueFunction = [](double v) { return juce::String(v, 2); };
        chebyPolyPow.valueFromTextFunction = [](const juce::String& t) { return t.getDoubleValue(); };
        styleToggleDark(chebyDetailToggle);
        chebyDetailToggle.setTooltip("Reveal per-partial sliders. When off, use Harmonics % and the Bezier curve only.");
        addAndMakeVisible(chebyDetailToggle);
        chebyDetailToggle.setToggleState(false, juce::dontSendNotification);
        chebyDetailToggle.onClick = [this]
        {
            const bool on = chebyDetailToggle.getToggleState();
            for (auto& s : chebyH)
                s.setVisible(on);
            for (auto& l : chebyHL)
                l.setVisible(on);
            for (auto& s : chebyHPow)
                s.setVisible(on);
            for (auto& l : chebyHPowL)
                l.setVisible(on);
            resized();
            if (auto* vp = findParentComponentOfClass<juce::Viewport>())
                vp->resized();
        };
        for (auto& s : chebyH)
            s.setVisible(false);
        for (auto& l : chebyHL)
            l.setVisible(false);
        for (auto& s : chebyHPow)
            s.setVisible(false);
        for (auto& l : chebyHPowL)
            l.setVisible(false);

        auto wireChebyY = [&](juce::Slider& s, juce::Label& lab, const char* id, const juce::String& name, const juce::String& tip)
        {
            styleLinearSliderCompact(s, kAccentBlue);
            styleLabelDark(lab, name, true);
            addAndMakeVisible(s);
            addAndMakeVisible(lab);
            s.setTooltip(tip);
            atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, id, s));
            s.textFromValueFunction = [](double v) { return juce::String(v, 2); };
            s.valueFromTextFunction = [](const juce::String& t) { return t.getDoubleValue(); };
        };
        wireChebyY(chebyYL, chebyYLL, "chebyYL", "Curve L", "Bezier control at x = -1.");
        wireChebyY(chebyYC, chebyYCL, "chebyYC", "Curve C", "Bezier control at x = 0.");
        wireChebyY(chebyYR, chebyYRL, "chebyYR", "Curve R", "Bezier control at x = +1.");

        static const char* chebyIds[12] = {
            "chebyH2", "chebyH3", "chebyH4", "chebyH5", "chebyH6", "chebyH7",
            "chebyH8", "chebyH9", "chebyH10", "chebyH11", "chebyH12", "chebyH13"
        };
        for (int i = 0; i < 12; ++i)
        {
            styleLinearSliderCompact(chebyH[(size_t) i], kAccentGreen);
            styleLabelDark(chebyHL[(size_t) i], juce::String("H") + juce::String(i + 2), true);
            addAndMakeVisible(chebyH[(size_t) i]);
            addAndMakeVisible(chebyHL[(size_t) i]);
            chebyH[(size_t) i].setTooltip("Weight for Chebyshev T harmonic (series applied after pre-curve).");
            atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, chebyIds[i], chebyH[(size_t) i]));
            chebyH[(size_t) i].textFromValueFunction = [](double v) { return juce::String(v, 3); };
            chebyH[(size_t) i].valueFromTextFunction = [](const juce::String& t) { return t.getDoubleValue(); };
        }

        static const char* chebyPowIds[12] = {
            "chebyH2Pow", "chebyH3Pow", "chebyH4Pow", "chebyH5Pow", "chebyH6Pow", "chebyH7Pow",
            "chebyH8Pow", "chebyH9Pow", "chebyH10Pow", "chebyH11Pow", "chebyH12Pow", "chebyH13Pow"
        };
        for (int i = 0; i < 12; ++i)
        {
            styleLinearSliderCompact(chebyHPow[(size_t) i], kAccentBlue);
            styleLabelDark(chebyHPowL[(size_t) i], juce::String("H") + juce::String(i + 2) + " pow", true);
            addAndMakeVisible(chebyHPow[(size_t) i]);
            addAndMakeVisible(chebyHPowL[(size_t) i]);
            chebyHPow[(size_t) i].setTooltip("Per-polynomial signed pow() exponent for Tn(x) before the weighted sum. Final k = global Poly pow * this. 1.0 = identity.");
            atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, chebyPowIds[i], chebyHPow[(size_t) i]));
            chebyHPow[(size_t) i].textFromValueFunction = [](double v) { return juce::String(v, 2); };
            chebyHPow[(size_t) i].valueFromTextFunction = [](const juce::String& t) { return t.getDoubleValue(); };
        }

        constexpr int kShValH = 20;
        constexpr int kShPctW = 72;
        constexpr int kShGainW = 72;
        constexpr int kShMagW = 72;
        constexpr int kShChebyYW = 72;
        constexpr int kShChebyHW = 72;
        auto styleShaperReadout = [&](juce::Slider& s, int boxW)
        {
            s.setTextBoxStyle(juce::Slider::TextBoxRight, false, boxW, kShValH);
        };
        styleShaperReadout(shaperMix, kShPctW);
        styleShaperReadout(shaperPreGain, kShGainW);
        styleShaperReadout(shaperPostTrim, kShGainW);
        styleShaperReadout(magDrive, kShMagW);
        styleShaperReadout(magTilt, kShMagW);
        styleShaperReadout(magBias, kShMagW);
        styleShaperReadout(magTiltLimit, kShPctW);
        styleShaperReadout(magFeedback, kShPctW);
        styleShaperReadout(magOut, kShMagW);
        styleShaperReadout(magEnergy, kShPctW);
        styleShaperReadout(magEnergyMs, kShPctW);
        styleShaperReadout(chebyHarmMacro, kShPctW);
        styleShaperReadout(chebyPolyPow, kShPctW);
        styleShaperReadout(chebyYL, kShChebyYW);
        styleShaperReadout(chebyYC, kShChebyYW);
        styleShaperReadout(chebyYR, kShChebyYW);
        for (auto& s : chebyH)
            styleShaperReadout(s, kShChebyHW);
        for (auto& s : chebyHPow)
            styleShaperReadout(s, kShChebyHW);

        // Convert all shaper-tab sliders to LinearVertical (textbox below).
        juce::Slider* vertSliders[] = {
            &shaperMix, &shaperPreGain, &shaperPostTrim,
            &magDrive, &magTilt, &magBias, &magTiltLimit, &magFeedback, &magOut,
            &magEnergy, &magEnergyMs,
            &chebyHarmMacro, &chebyPolyPow,
            &chebyYL, &chebyYC, &chebyYR
        };
        for (auto* s : vertSliders)
        {
            s->setSliderStyle(juce::Slider::LinearVertical);
            s->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 18);
            s->setColour(juce::Slider::backgroundColourId, juce::Colour(0xff1a1a1a));
            s->setColour(juce::Slider::trackColourId, kAccentGreen.withAlpha(0.9f));
            s->setColour(juce::Slider::thumbColourId, kAccentBlue);
        }
        for (auto& s : chebyH)
        {
            s.setSliderStyle(juce::Slider::LinearVertical);
            s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 18);
            s.setColour(juce::Slider::trackColourId, kAccentGreen.withAlpha(0.9f));
            s.setColour(juce::Slider::thumbColourId, kAccentBlue);
        }
        for (auto& s : chebyHPow)
        {
            s.setSliderStyle(juce::Slider::LinearVertical);
            s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 18);
            s.setColour(juce::Slider::trackColourId, kAccentBlue.withAlpha(0.9f));
            s.setColour(juce::Slider::thumbColourId, kAccentGreen);
        }
    }

    int getMinimumContentHeight() const noexcept
    {
        // Vertical-slider layout: one main row (~110px) + optional Cheby H + Pow rows (~110px each).
        constexpr int kColH = 110;
        constexpr int kHeaderH = 30;
        constexpr int kPad = 24;
        const int modeIdx = shaperModeBox.getSelectedItemIndex();
        const int extraRows = (modeIdx == 2) ? 2 : ((modeIdx == 1) ? 1 : 0); // cheby: 2 detail rows; magnet: shape combo row.
        return kPad + kHeaderH + kColH + extraRows * (kColH + 6) + kPad;
    }

    void paint(juce::Graphics& g) override { g.fillAll(kPanelBlack); }

    void resized() override
    {
        auto b = getLocalBounds().reduced(8);
        auto modeRow = b.removeFromTop(26);
        infoBtn.setBounds(modeRow.removeFromRight(26));
        modeRow.removeFromRight(4);
        shaperModeL.setBounds(modeRow.removeFromLeft(100));
        shaperModeBox.setBounds(modeRow.removeFromLeft(juce::jmin(280, modeRow.getWidth())));
        b.removeFromTop(6);

        const int modeIdx = shaperModeBox.getSelectedItemIndex(); // 0=Off, 1=Magnet, 2=Chebyshev
        const bool magOn = (modeIdx == 1);
        const bool chebyOn = (modeIdx == 2);
        const bool detailOn = chebyOn;  // H2-H13 + Pow always visible in Chebyshev mode.
        chebyDetailToggle.setVisible(false);
        const int harmRows = (chebyOn && detailOn) ? 24 : 0;
        const int magRows = magOn ? 9 : 0;
        const int magHeadRows = magOn ? 1 : 0;
        const int chebyHeadRows = chebyOn ? 1 : 0;
        const int chebyCurveRows = chebyOn ? (2 + 1 + 3) : 0;
        const int kParamRows = 3 + magRows + magHeadRows + chebyHeadRows + chebyCurveRows + harmRows;

        magnetHead.setVisible(magOn);
        juce::Component* magBits[] = {
            &magDrive, &magDriveL, &magTilt, &magTiltL, &magBias, &magBiasL,
            &magTiltLimit, &magTiltLimitL, &magFeedback, &magFeedbackL,
            &magOut, &magOutL, &magShape, &magShapeL, &magEnergy, &magEnergyL,
            &magEnergyMs, &magEnergyMsL
        };
        for (auto* c : magBits)
            c->setVisible(magOn);

        chebyHead.setVisible(chebyOn);
        chebyHarmMacro.setVisible(chebyOn); chebyHarmMacroL.setVisible(chebyOn);
        chebyPolyPow.setVisible(chebyOn);   chebyPolyPowL.setVisible(chebyOn);
        chebyDetailToggle.setVisible(chebyOn);
        chebyYL.setVisible(chebyOn); chebyYLL.setVisible(chebyOn);
        chebyYC.setVisible(chebyOn); chebyYCL.setVisible(chebyOn);
        chebyYR.setVisible(chebyOn); chebyYRL.setVisible(chebyOn);
        for (auto& s : chebyH)    s.setVisible(chebyOn && detailOn);
        for (auto& l : chebyHL)   l.setVisible(chebyOn && detailOn);
        for (auto& s : chebyHPow) s.setVisible(chebyOn && detailOn);
        for (auto& l : chebyHPowL) l.setVisible(chebyOn && detailOn);

        juce::ignoreUnused(kParamRows);

        // Single row of all main controls per mode; H/Pow detail sliders 12-per-row when shown.
        constexpr int kColH = 110;
        const int colCountMain = magOn ? 11 : (chebyOn ? 8 : 3);
        const int availW = b.getWidth();
        const int colWMain = juce::jlimit(48, 80, (availW - 4) / juce::jmax(1, colCountMain) - 2);
        const int colWDetail = juce::jlimit(40, 70, (availW - 4) / 12 - 2);

        auto placeRowCols = [&](std::initializer_list<std::pair<juce::Label*, juce::Slider*>> cols, int colW)
        {
            auto row = b.removeFromTop(kColH);
            for (auto& [lab, sl] : cols)
                placeVertCol(row, *lab, *sl, colW);
        };

        if (magOn)
        {
            auto mh = b.removeFromTop(20);
            magnetHead.setBounds(mh.getX(), mh.getY(), 260, 18);
            placeRowCols({ {&shaperMixL,&shaperMix}, {&shaperPreGainL,&shaperPreGain}, {&shaperPostTrimL,&shaperPostTrim},
                           {&magDriveL,&magDrive}, {&magTiltL,&magTilt}, {&magBiasL,&magBias},
                           {&magTiltLimitL,&magTiltLimit}, {&magFeedbackL,&magFeedback}, {&magOutL,&magOut},
                           {&magEnergyL,&magEnergy}, {&magEnergyMsL,&magEnergyMs} }, colWMain);
            b.removeFromTop(6);
            auto shapeRow = b.removeFromTop(28);
            magShapeL.setBounds(shapeRow.getX(), shapeRow.getY() + 4, 80, 20);
            magShape.setBounds(shapeRow.getX() + 84, shapeRow.getY() + 2, juce::jmin(180, shapeRow.getWidth() - 84), 24);
            b.removeFromTop(4);
        }
        else if (chebyOn)
        {
            auto ch = b.removeFromTop(20);
            chebyHead.setBounds(ch.getX(), ch.getY(), 320, 18);
            placeRowCols({ {&shaperMixL,&shaperMix}, {&shaperPreGainL,&shaperPreGain}, {&shaperPostTrimL,&shaperPostTrim},
                           {&chebyHarmMacroL,&chebyHarmMacro}, {&chebyPolyPowL,&chebyPolyPow},
                           {&chebyYLL,&chebyYL}, {&chebyYCL,&chebyYC}, {&chebyYRL,&chebyYR} }, colWMain);
            b.removeFromTop(4);
            // 12 H weights on one row, 12 H pow on one row — always shown in Chebyshev mode.
            auto hRow = b.removeFromTop(kColH);
            for (int i = 0; i < 12; ++i)
                placeVertCol(hRow, chebyHL[(size_t) i], chebyH[(size_t) i], colWDetail);
            b.removeFromTop(2);
            auto pRow = b.removeFromTop(kColH);
            for (int i = 0; i < 12; ++i)
                placeVertCol(pRow, chebyHPowL[(size_t) i], chebyHPow[(size_t) i], colWDetail);
            b.removeFromTop(2);
            juce::ignoreUnused(detailOn);
        }
        else
        {
            placeRowCols({ {&shaperMixL,&shaperMix}, {&shaperPreGainL,&shaperPreGain}, {&shaperPostTrimL,&shaperPostTrim} }, colWMain);
        }
    }
};

struct ParaEQ301AudioProcessorEditor::ShaperTabScrollHost : public juce::Viewport
{
    explicit ShaperTabScrollHost(juce::AudioProcessorValueTreeState& ap,
                                 std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>>& atts,
                                 std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>>& batts,
                                 std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>>& comboAtts)
        : content(std::make_unique<ShaperTabContent>(ap, atts, batts, comboAtts))
    {
        setViewedComponent(content.get(), false);
        setScrollBarsShown(true, false);
        setScrollBarThickness(9);
    }

    void resized() override
    {
        const int w = juce::jmax(1, getWidth());
        const int h = juce::jmax(1, getHeight());
        const int minH = content->getMinimumContentHeight();
        content->setBounds(0, 0, w, juce::jmax(h, minH));
        juce::Viewport::resized();
    }

    std::unique_ptr<ShaperTabContent> content;
};

struct ParaEQ301AudioProcessorEditor::CurveMotionTabContent : public juce::Component
{
    explicit CurveMotionTabContent(ParaEQ301AudioProcessor& processor)
        : curve(processor)
    {
        addAndMakeVisible(curve);
    }

    void resized() override
    {
        curve.setBounds(getLocalBounds().reduced(kPeqTabPanelMargin));
    }

    CurveTabContent curve;
};

ParaEQ301AudioProcessorEditor::ParaEQ301AudioProcessorEditor(ParaEQ301AudioProcessor& p)
    : AudioProcessorEditor(&p), proc(p)
{
    auto& ap = proc.getAPVTS();

    eqTabScroll = std::make_unique<EqTabScrollHost>(proc, ap, attachments, buttonAttachments, comboAttachments);
    curveMotionPage = std::make_unique<CurveMotionTabContent>(proc);
    roastTabScroll = std::make_unique<RoastTabScrollHost>(proc, ap, attachments, buttonAttachments, comboAttachments);
    shaperTabScroll = std::make_unique<ShaperTabScrollHost>(ap, attachments, buttonAttachments, comboAttachments);
    anharmTabScroll = std::make_unique<AnharmTabScrollHost>(ap, attachments, buttonAttachments);
    parametricTabScroll = std::make_unique<ParametricTabScrollHost>(ap, attachments, buttonAttachments);
    outPage = std::make_unique<OutTabContent>(proc, ap, attachments, buttonAttachments);

    tabs.addTab("EQ", kPanelBlack, eqTabScroll.get(), false);
    tabs.addTab("Roast", kPanelBlack, roastTabScroll.get(), false);
    tabs.addTab("Shaper", kPanelBlack, shaperTabScroll.get(), false);
    tabs.addTab("Anharm", kPanelBlack, anharmTabScroll.get(), false);
    tabs.addTab("APR", kPanelBlack, parametricTabScroll.get(), false);
    tabs.addTab("Output", kPanelBlack, outPage.get(), false);

    tabs.setColour(juce::TabbedComponent::backgroundColourId, kPanelBlack);
    tabs.getTabbedButtonBar().setColour(juce::TabbedButtonBar::tabOutlineColourId, juce::Colour(0xff333333));
    tabs.getTabbedButtonBar().setColour(juce::TabbedButtonBar::frontOutlineColourId, juce::Colour(0xff666666));
    tabs.getTabbedButtonBar().setColour(juce::TabbedButtonBar::tabTextColourId, kTextMuted);
    tabs.getTabbedButtonBar().setColour(juce::TabbedButtonBar::frontTextColourId, kTextBright);

    addAndMakeVisible(tabs);

    meterInLabel.setJustificationType(juce::Justification::centredLeft);
    meterOutLabel.setJustificationType(juce::Justification::centredLeft);
    const juce::Font meterMono(juce::FontOptions()
                                   .withName(juce::Font::getDefaultMonospacedFontName())
                                   .withHeight(11.0f));
    meterInLabel.setFont(meterMono);
    meterOutLabel.setFont(meterMono);
    meterInLabel.setColour(juce::Label::textColourId, kAccentGreen);
    meterOutLabel.setColour(juce::Label::textColourId, kAccentBlue);
    addAndMakeVisible(meterInLabel);
    addAndMakeVisible(meterOutLabel);
    meterInLabel.setText("In:  (waiting for audio...)", juce::dontSendNotification);
    meterOutLabel.setText("Out: (waiting for audio...)", juce::dontSendNotification);
    meterInLabel.setTooltip("Smoothed block RMS at the plugin audio input (monitoring).");
    meterOutLabel.setTooltip("Smoothed block RMS at the plugin audio output (monitoring).");

    styleTopBarMixKnob(masterDryWetSlider);
    masterDryWetCaption.setText("Dry/Wet", juce::dontSendNotification);
    masterDryWetCaption.setJustificationType(juce::Justification::centred);
    masterDryWetCaption.setFont(juce::Font(juce::FontOptions(9.0f)));
    masterDryWetCaption.setColour(juce::Label::textColourId, kTextBright.withAlpha(0.9f));
    masterDryWetSlider.setTooltip("Parallel mix of dry input vs the full processed chain (EQ, ThrillMe, Roast, output limiter, etc.). 0% = dry only, 100% = wet only. Default 100%.");
    masterDryWetSlider.textFromValueFunction = [](double v)
    {
        return juce::String(juce::roundToInt(v * 100.0)) + " %";
    };
    masterDryWetSlider.valueFromTextFunction = [](const juce::String& t)
    {
        return juce::jlimit(0.0, 1.0, t.getDoubleValue() / 100.0);
    };
    addAndMakeVisible(masterDryWetSlider);
    addAndMakeVisible(masterDryWetCaption);
    attachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "masterDryWet", masterDryWetSlider));
    masterDryWetSlider.updateText();

    startTimerHz(20);

    setLookAndFeel(&pluginSliderValueLf);

    setSize(900, 900);
}

ParaEQ301AudioProcessorEditor::~ParaEQ301AudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void ParaEQ301AudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(kPanelBlack);
    paintLevelMeterBar(g, meterInBarBounds, meterNormalisedFromRms(proc.getDebugInputRms()), kAccentGreen);
    paintLevelMeterBar(g, meterOutBarBounds, meterNormalisedFromRms(proc.getDebugOutputRms()), kAccentBlue);
    if (mainSpectrumCurveBounds.getHeight() >= 48 && mainSpectrumCurveBounds.getWidth() >= 120)
        paintMergedSpectrumAndEqInRect(g, mainSpectrumCurveBounds, proc, mainCurveFreqHz, mainCurveMagScratch,
                                       mainCurveEqSmoothed, mainCurveComboSmoothed, mainCurveSpecBefore, mainCurveSpecAfter);
}

void ParaEQ301AudioProcessorEditor::resized()
{
    constexpr int kMeterTop = 4;
    constexpr int kLineH = 14;
    constexpr int kBarH = 11;
    constexpr int kLabelW = 118;
    constexpr int kPad = 8;
    constexpr int kGapLabelBar = 4;
    constexpr int kTopStripH = 30;
    constexpr int kMixColOuterW = 128;
    constexpr int kCurveMaxH = 240;
    constexpr int kTabMinRemainH = 260;

    auto belowTabsTop = getLocalBounds();
    auto topStrip = belowTabsTop.removeFromTop(kTopStripH);
    auto mixCol = topStrip.removeFromRight(kMixColOuterW + kPad);
    mixCol.removeFromRight(kPad);

    const int meterTotalW = topStrip.getWidth();
    const int half = juce::jmax(1, meterTotalW / 2);

    meterInLabel.setBounds(kPad + topStrip.getX(), kMeterTop, kLabelW, kLineH);
    const int inBarX = kPad + kLabelW + kGapLabelBar + topStrip.getX();
    const int inBarW = juce::jmax(24, half - (inBarX - topStrip.getX()) - kPad);
    meterInBarBounds = { inBarX, kMeterTop + 1, inBarW, kBarH };

    const int outColX = topStrip.getX() + half + kPad;
    meterOutLabel.setBounds(outColX, kMeterTop, kLabelW, kLineH);
    const int outBarX = outColX + kLabelW + kGapLabelBar;
    const int outBarW = juce::jmax(24, topStrip.getRight() - outBarX - kPad);
    meterOutBarBounds = { outBarX, kMeterTop + 1, outBarW, kBarH };

    {
        const int sliderH = 20;
        masterDryWetSlider.setBounds(mixCol.getX() + 2, 2, juce::jmax(72, mixCol.getWidth() - 4), sliderH);
        masterDryWetCaption.setBounds(mixCol.getX() + 2, kTopStripH - 10, mixCol.getWidth() - 4, 9);
    }

    int availForCurveAndTabs = belowTabsTop.getHeight();
    int curveH = juce::jlimit(48, kCurveMaxH, availForCurveAndTabs - kTabMinRemainH);

    auto curveArea = belowTabsTop.removeFromTop(curveH);
    mainSpectrumCurveBounds = curveArea.reduced(kPad, 4);
    tabs.setBounds(belowTabsTop);
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
    repaint();
}
