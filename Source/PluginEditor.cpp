#include "PluginEditor.h"
#include <cmath>
#include <cstring>
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

    // Matches per-band gain parameter range (±30 dB). Combined chain can exceed this; draw clamps to plot edges.
    constexpr float kEqMagPlotDbMin = -30.f;
    constexpr float kEqMagPlotDbMax = 30.f;

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
        s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 18);
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

    // EQ value column: wide enough for "18000 Hz" / "-30.0 dB" in monospace without ellipsis or horizontal squish.
    constexpr int kEqSliderColW = 78;
    constexpr int kEqTextBoxW = 74;
    constexpr int kEqTextBoxH = 20;
    constexpr int kEqSliderColumnH = kKnobSize + kEqTextBoxH;
    constexpr int kEqRowHeight = kEqSliderColumnH + kGapCaption + kCaptionH;

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

    /** Wires EQ band value labels: monospace, no horizontal glyph stretch, padding (see styleEqSlider sizing). */
    struct EqBandLookAndFeel : juce::LookAndFeel_V4
    {
        juce::Label* createSliderTextBox (juce::Slider& slider) override
        {
            auto* l = juce::LookAndFeel_V4::createSliderTextBox (slider);
            if ((bool) slider.getProperties()["peqEqBox"])
            {
                l->setFont(juce::Font(juce::FontOptions()
                                          .withName(juce::Font::getDefaultMonospacedFontName())
                                          .withHeight(11.0f)));
                l->setMinimumHorizontalScale(1.0f);
                l->setJustificationType(juce::Justification::centred);
                l->setBorderSize(juce::BorderSize<int>(0, 4, 0, 4));
            }
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

    juce::String appendLiveEqSnapshotNumbers(const ParaEQ301AudioProcessor::MotionEffectiveEqSnapshot& s)
    {
        auto ri = [](float x) { return juce::String(juce::roundToInt(x)); };
        juce::String t;
        t += "Hi  " + ri(s.hiCfHz) + " Hz  " + juce::String(s.hiGainDb, 1) + " dB     ";
        t += "M1  " + ri(s.mid1CfHz) + " Hz  " + ri(s.mid1BwHz) + " Hz BW  " + juce::String(s.mid1GainDb, 1) + " dB\n";
        t += "M2  " + ri(s.mid2CfHz) + " Hz  " + ri(s.mid2BwHz) + " Hz BW  " + juce::String(s.mid2GainDb, 1) + " dB     ";
        t += "Lo  " + ri(s.loCfHz) + " Hz  " + juce::String(s.loGainDb, 1) + " dB";
        return t;
    }

    juce::String buildEqMotionPanelTooltip(juce::AudioProcessorValueTreeState& ap,
                                           const ParaEQ301AudioProcessor::MotionEffectiveEqSnapshot& s)
    {
        if (!motionLfoDepthActive(ap))
            return "Motion is off (all LFO depth knobs at 0%).\n"
                   "Open Curve + Motion and raise EQ gain / freq / width % on a band to add LFO motion.\n\n"
                   + appendLiveEqSnapshotNumbers(s);

        if (!s.motionEngaged)
            return "Motion is armed (depth > 0%) but the host is not running the plugin’s audio graph yet.\n"
                   "Start playback so processBlock runs; then green arcs = live EQ, blue dot = stored rest value.\n\n"
                   "Targets — Hi: " + eqMotionTargetsSummary(ap, 0) + "  Mid1: " + eqMotionTargetsSummary(ap, 1) + "\n"
                   "Mid2: " + eqMotionTargetsSummary(ap, 2) + "  Low: " + eqMotionTargetsSummary(ap, 3) + "\n\n"
                   + appendLiveEqSnapshotNumbers(s);

        return "Motion on (left channel).\n"
               "Green arc = live filter value; blue dot = stored parameter (drag to edit rest).\n"
               "Lo / M1 / M2 / Hi tags on the graph follow peak/shelf Hz when freq LFO depth is up.\n\n"
               "Targets — Hi: " + eqMotionTargetsSummary(ap, 0) + "  Mid1: " + eqMotionTargetsSummary(ap, 1) + "\n"
               "Mid2: " + eqMotionTargetsSummary(ap, 2) + "  Low: " + eqMotionTargetsSummary(ap, 3) + "\n\n"
               + appendLiveEqSnapshotNumbers(s);
    }

    juce::String buildEqMotionStatusShort(juce::AudioProcessorValueTreeState& ap,
                                          const ParaEQ301AudioProcessor::MotionEffectiveEqSnapshot& s)
    {
        if (!motionLfoDepthActive(ap))
            return "Motion off - use Curve + Motion tab to raise LFO depth % on a band.";
        if (!s.motionEngaged)
            return "Motion armed - start playback; green arc = live EQ, blue dot = stored value.";
        return "Motion on: green arc = live EQ (L), blue dot = stored rest; graph tags track modulated Hz.";
    }

    juce::String formatMotionLiveReadout(const ParaEQ301AudioProcessor::MotionEffectiveEqSnapshot& s)
    {
        juce::String t;
        if (!s.motionEngaged)
            t = "Live filter (L ch) — same as EQ tab when Motion is idle.\n";
        else
            t = "Live filter (L ch) — sweeps with the LFO; EQ tab knobs track these values while audio runs.\n";
        t += appendLiveEqSnapshotNumbers(s);
        return t;
    }

    /** Fixed dB window (0 dB vertical centre = flat / unity gain). Optional coloured markers at Lo/M1/M2/Hi centre Hz.
        freqAxisInsetLeftPx / RightPx: horizontal space reserved for ±dB labels. Use 0,0 when stacking under a spectrum
        that uses the full width for log-Hz (e.g. Curve + Motion tab) so 100 / 1k / 10k grid lines line up. */
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
                                     int freqAxisInsetLeftPx = 54,
                                     int freqAxisInsetRightPx = 44)
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

        if (drawRoundedBackground)
        {
            g.setColour(juce::Colour(0xff1a1a1a));
            g.fillRoundedRectangle(graphOuter.toFloat(), 3.f);
            g.setColour(juce::Colour(0xff333333));
            g.drawRoundedRectangle(graphOuter.toFloat(), 3.f, 1.f);
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

        for (float db : { -27.f, -24.f, -18.f, -12.f, -9.f, -6.f, -3.f, 3.f, 6.f, 9.f, 12.f, 18.f, 24.f, 27.f })
        {
            if (db <= dMin + 0.001f || db >= dMax - 0.001f || std::abs((double) db) < 0.01)
                continue;
            const float y = yOfDb(db);
            g.setColour(juce::Colour(0xff2a2a2a));
            g.drawHorizontalLine(juce::roundToInt(y), (float) curve.getX(), (float) curve.getRight());
        }

        g.setColour(kTextMuted);
        g.setFont(8.5f);
        for (double hz : { 100.0, 1000.0, 10000.0 })
        {
            if (hz < fLo || hz > fHi)
                continue;
            const float x = xOfF(hz);
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
                if (motionSnap != nullptr && motionSnap->motionEngaged
                    && cfMotionDepthForMarker(*apMarkers, t.paramId) > 1.0e-6f)
                {
                    if (std::strcmp(t.paramId, "lowCf") == 0)
                        hz = motionSnap->loCfHz;
                    else if (std::strcmp(t.paramId, "mid1Cf") == 0)
                        hz = motionSnap->mid1CfHz;
                    else if (std::strcmp(t.paramId, "mid2Cf") == 0)
                        hz = motionSnap->mid2CfHz;
                    else if (std::strcmp(t.paramId, "hiCf") == 0)
                        hz = motionSnap->hiCfHz;
                }
                if (hz < (float) fLo || hz > (float) fHi)
                    continue;
                const float x = xOfF(static_cast<double>(hz));
                g.setColour(t.col.withAlpha(0.38f));
                g.drawVerticalLine(juce::roundToInt(x), (float) curve.getY() + 13, (float) curve.getBottom());
                g.setFont(8.2f);
                g.setColour(t.col.withAlpha(0.92f));
                g.drawText(t.tag, juce::roundToInt(x) - 10, curve.getY() + 1, 20, 11, juce::Justification::centred);
            }
        }

        const float y0 = yOfDb(0.f);
        g.setColour(juce::Colour(0xff777777));
        for (float x = (float) curve.getX(); x < (float) curve.getRight(); x += 10.f)
            g.drawLine(x, y0, juce::jmin(x + 5.5f, (float) curve.getRight()), y0, 1.25f);

        const juce::String topDbLab = (dMax > 0.001f ? juce::String("+") : juce::String())
                                      + juce::String(static_cast<int>(dMax)) + " dB";
        const juce::String midDbLab = "0 dB";
        const juce::String botDbLab = juce::String(static_cast<int>(dMin)) + " dB";

        auto yForLabel = [&](float db) -> int
        {
            const int y = juce::roundToInt(yOfDb(db) - 5);
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

        juce::Path path;
        bool started = false;
        for (int i = 0; i < nPts; ++i)
        {
            float dbg = magDb[(size_t) i];
            if (!std::isfinite(dbg))
                dbg = 0.f;
            const float x = xOfF(freqHz[(size_t) i]);
            const float y = yOfDb(dbg);
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
        g.setColour(curveStrokeColour.withAlpha(0.95f));
        {
            juce::Graphics::ScopedSaveState clipState(g);
            g.reduceClipRegion(curve);
            g.strokePath(path, juce::PathStrokeType(2.2f));
        }
    }

    /** Draws combined low shelf + 2 peaks + high shelf magnitude; emphasises 0 dB baseline. */
    void paintEqChainResponse(juce::Graphics& g, juce::Rectangle<int> fullArea,
                              ParaEQ301AudioProcessor& proc,
                              std::vector<double>& freqScratch, std::vector<float>& magScratch,
                              float dMin, float dMax)
    {
        if (fullArea.getHeight() < 36)
            return;

        auto plot = fullArea.reduced(0, 2);
        plot.removeFromBottom(11);

        const double sr = proc.getSampleRate() > 0.0 ? proc.getSampleRate() : 44100.0;
        constexpr int nPts = 220;
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

        ParaEQ301AudioProcessor::MotionEffectiveEqSnapshot snap;
        proc.getMotionEffectiveEqSnapshot(snap);
        paintEqMagnitudeCurveInRect(g, plot, freqScratch, magScratch.data(), nPts, dMin, dMax, fLo, fHi,
                                    &proc.getAPVTS(), &snap, kAccentGreen, true);
    }
}

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
    juce::Label motionStatus;

    ParaEQ301AudioProcessor& proc;
    juce::Rectangle<int> eqGraphBounds;
    juce::Rectangle<int> motionRowRect[4];
    std::vector<double> freqScratch;
    std::vector<float> magScratch;
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
                 std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>>& batts)
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
        hi.bandLabel.setTooltip("High shelf: boosts or cuts treble. It is not a high-pass filter — low frequencies still pass. \"Shelf Hz\" is the turnover: far above it the curve reaches the Gain value.");
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
        low.bandLabel.setTooltip("Low shelf: boosts or cuts bass. It is not a low-pass filter — highs still pass. \"Shelf Hz\" is the turnover: far below it the curve reaches the Gain value.");
        low.cf.setTooltip("Low shelf turnover frequency (Hz). Energy well below this frequency is tilted toward the Gain dB setting; above it the response flattens back toward 0 dB change (classic shelving EQ).");
        low.gain.setTooltip("How much boost or cut applies in the bass region (0 dB = no change). Negative = gentle low cut.");

        motionStatus.setJustificationType(juce::Justification::centredLeft);
        motionStatus.setFont(juce::Font(juce::FontOptions().withHeight(10.0f)));
        motionStatus.setColour(juce::Label::textColourId, kTextBright);
        motionStatus.setMinimumHorizontalScale(0.85f);
        ParaEQ301AudioProcessor::MotionEffectiveEqSnapshot snap0;
        processor.getMotionEffectiveEqSnapshot(snap0);
        motionStatus.setText(buildEqMotionStatusShort(ap, snap0), juce::dontSendNotification);
        motionStatus.setTooltip(buildEqMotionPanelTooltip(ap, snap0));
        addAndMakeVisible(motionStatus);

        eqGraphTooltip.setTooltip(
            "Low / High bands are shelves (tilt EQ), not pass filters. Shelf Hz is the turnover toward the Gain value.\n\n"
            "Signal path: pre-EQ Core saturation, Low shelf, Mid1, Mid2, High shelf, optional post-EQ Core 2. "
            "Dashed line = 0 dB. Scale is ±30 dB. Coloured tags follow each band’s centre Hz (including Motion).\n\n"
            "Green curve = combined EQ. Knobs: green arc = live value, blue dot = stored rest when Motion is running.");
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
        core1BypassToggle.setTooltip("Checked = pre-EQ saturator fully off (Sat % ignored). Unchecked = apply Sat 1 amount.");

        styleCoreSatWideSlider(coreSat);
        styleLabel(coreSatLabel, "Sat %");
        addAndMakeVisible(coreSat);
        addAndMakeVisible(coreSatLabel);
        coreSat.setTooltip("Pre-EQ saturation (tanh blend). Crank both stages for heavy core drive.");
        atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "coreSat", coreSat));
        coreSat.textFromValueFunction = [](double v)
        {
            return juce::String(juce::roundToInt(v * 100.0)) + " %";
        };
        coreSat.valueFromTextFunction = [](const juce::String& t)
        {
            return juce::jlimit(0.0, 1.0, t.getDoubleValue() / 100.0);
        };

        addAndMakeVisible(core2BypassToggle);
        styleToggleDark(core2BypassToggle);
        batts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(ap, "core2Bypass", core2BypassToggle));
        core2BypassToggle.setTooltip("Checked = post-EQ saturator off (Sat % ignored). Unchecked = apply Sat 2 after EQ peaks.");

        styleCoreSatWideSlider(core2Sat);
        styleLabel(core2SatLabel, "Sat %");
        addAndMakeVisible(core2Sat);
        addAndMakeVisible(core2SatLabel);
        core2Sat.setTooltip("Post-EQ saturation after the EQ (tanh blend). Use with Sat 1 + EQ boosts for aggressive tone.");
        atts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "core2Sat", core2Sat));
        core2Sat.textFromValueFunction = [](double v)
        {
            return juce::String(juce::roundToInt(v * 100.0)) + " %";
        };
        core2Sat.valueFromTextFunction = [](const juce::String& t)
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

        const bool followLive = motionLfoDepthActive(ap) && s.motionEngaged && eqSliderGestureDepth == 0
                                && !eqTextBoxHasKeyboardFocus();
        if (followLive)
            setEqKnobLiveDisplayFromSnapshot(s);
        else
            clearEqKnobLiveDisplayProps();

        repaintEqKnobs();
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(kPanelBlack);
        auto& ap = proc.getAPVTS();
        for (int r = 0; r < 4; ++r)
        {
            if (!motionRowRect[(size_t) r].isEmpty() && eqBandHasMotion(ap, r))
            {
                g.setColour(kAccentBlue.withAlpha(0.1f));
                g.fillRoundedRectangle(motionRowRect[(size_t) r].toFloat().reduced(1), 5.f);
                g.setColour(kAccentBlue.withAlpha(0.28f));
                g.drawRoundedRectangle(motionRowRect[(size_t) r].toFloat().reduced(1), 5.f, 1.f);
            }
        }
        if (eqGraphBounds.getHeight() > 24)
            paintEqChainResponse(g, eqGraphBounds, proc, freqScratch, magScratch, kEqMagPlotDbMin, kEqMagPlotDbMax);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(8);
        if (bounds.getWidth() < 160 || bounds.getHeight() < 120)
            return;

        // Top → bottom: EQ graph, one-line Motion hint, Core (pre-EQ), then Hi / Mid1 / Mid2 / Low rows.
        // Extra height goes to the graph so there is no dead band between Low shelf and the window bottom.
        constexpr int kGapAfterGraph = 5;
        constexpr int kMotionLineH = 28;
        constexpr int kGapAfterMotion = 4;
        constexpr int kCoreRowH = 36;
        constexpr int kCoreBetweenRows = 5;
        constexpr int kCoreStripH = kCoreRowH + kCoreBetweenRows + kCoreRowH;
        constexpr int kGapBeforeBands = 6;
        constexpr int kBandRowsH = 4 * kEqRowHeight;
        constexpr int kMinGraphH = 52;

        const int fixedBelowGraph = kGapAfterGraph + kMotionLineH + kGapAfterMotion + kCoreStripH + kGapBeforeBands + kBandRowsH;
        int graphH = bounds.getHeight() - fixedBelowGraph;
        graphH = juce::jmax(kMinGraphH, graphH);

        eqGraphBounds = bounds.removeFromTop(graphH);
        bounds.removeFromTop(kGapAfterGraph);
        motionStatus.setBounds(bounds.removeFromTop(kMotionLineH));
        bounds.removeFromTop(kGapAfterMotion);

        {
            auto coreBlock = bounds.removeFromTop(kCoreStripH);
            auto placeCoreRow = [&](juce::Rectangle<int> coreRow,
                                    juce::ToggleButton& on, juce::Slider& sat, juce::Label& satLab)
            {
                constexpr int kToggleW = 148;
                auto toggleArea = coreRow.removeFromLeft(juce::jmin(kToggleW, juce::jmax(96, coreRow.getWidth() / 4)));
                const int cy = toggleArea.getCentreY() - 11;
                on.setBounds(toggleArea.getX() + 2, cy, toggleArea.getWidth() - 4, 22);
                auto rest = coreRow.reduced(6, 2);
                constexpr int kLabelSatW = 42;
                auto labArea = rest.removeFromRight(kLabelSatW);
                satLab.setBounds(labArea.getX(), rest.getCentreY() - 7, labArea.getWidth(), 14);
                sat.setBounds(rest.getX(), rest.getCentreY() - 11, juce::jmax(60, rest.getWidth()), 22);
            };
            auto row1 = coreBlock.removeFromTop(kCoreRowH);
            coreBlock.removeFromTop(kCoreBetweenRows);
            auto row2 = coreBlock.removeFromTop(kCoreRowH);
            placeCoreRow(row1, core1BypassToggle, coreSat, coreSatLabel);
            placeCoreRow(row2, core2BypassToggle, core2Sat, core2SatLabel);
        }

        bounds.removeFromTop(kGapBeforeBands);

        constexpr int bandStripW = 80;
        constexpr int gapAfterBandStrip = 14;
        constexpr int eqGap = 6;
        const int step = kEqSliderColW + eqGap;
        const int xCol0 = bounds.getX() + bandStripW + gapAfterBandStrip;

        auto placeRow = [&](BandKnobs& band, int rowIndex)
        {
            const int y = bounds.getY() + rowIndex * kEqRowHeight;
            band.bandLabel.setBounds(bounds.getX() + 3, y + 8, bandStripW - 6, 40);
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
                placeKnobCol(band.bw, band.bwLabel, 1);
                placeKnobCol(band.gain, band.gainLabel, 3);
            }
            else
            {
                placeKnobCol(band.cf, band.cfLabel, 1);
                placeKnobCol(band.gain, band.gainLabel, 3);
            }

            const int rowRight = xCol0 + 3 * step + kEqSliderColW;
            motionRowRect[(size_t) rowIndex] = { bounds.getX(), y, rowRight - bounds.getX() + 2, kEqRowHeight };
        };

        placeRow(hi, 0);
        placeRow(mid1, 1);
        placeRow(mid2, 2);
        placeRow(low, 3);

        eqGraphTooltip.setBounds(eqGraphBounds);
    }
};

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
    TooltipMouseProxy motionOverviewProxy;
    juce::Label liveEqReadout;
    Row hi, m1, m2, lo;
    juce::Rectangle<int> lfoDotHi, lfoDotM1, lfoDotM2, lfoDotLo;

    static void sk(juce::Slider& s)
    {
        s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        // Motion row: LFO Hz uses "12.34 Hz" from parameter string; 56px avoids ellipsis in monospace.
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, kTextBoxH - 2);
        styleSliderDark(s, juce::Colour(0xff4a8ad4));
    }

    LfoTabContent(ParaEQ301AudioProcessor& processor,
                  juce::AudioProcessorValueTreeState& ap,
                  std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>>& atts)
        : proc(processor)
    {
        motionOverviewProxy.setTooltip(
            "Hi / M1 / M2 / Lo rows modulate those EQ bands around the values on the EQ tab (LFO). EQ gain depth up to ±12 dB around the knob; freq and width sweep similarly. Blue dots = LFO phase.\n\n"
            "Hi row → EQ Hi shelf: Gain dB + Shelf Hz.\n"
            "M1 / M2 → EQ Mid1 / Mid2: Gain + Peak Hz + Width Hz.\n"
            "Lo row → EQ Low shelf: Gain + Shelf Hz.\n"
            "EQ tab blue outline = band has Motion. While audio runs, EQ knobs show live modulated values.\n\n"
            "Hover the narrow strip at the top of the Motion section for this help.");
        addAndMakeVisible(motionOverviewProxy);

        liveEqReadout.setJustificationType(juce::Justification::topLeft);
        liveEqReadout.setFont(juce::Font(juce::FontOptions()
                                             .withName(juce::Font::getDefaultMonospacedFontName())
                                             .withHeight(9.2f)));
        liveEqReadout.setColour(juce::Label::textColourId, kTextBright);
        liveEqReadout.setMinimumHorizontalScale(1.0f);
        addAndMakeVisible(liveEqReadout);

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
            r.dCf.setTooltip("How much the LFO pushes this band’s shelf corner or peak frequency around the Hz on the EQ tab.");

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
                r.dBw.setTooltip("How much the LFO wobbles the mid peak’s bandwidth (Width Hz on EQ). Hi/Lo shelves have no width control.");
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

    void timerCallback() override
    {
        ParaEQ301AudioProcessor::MotionEffectiveEqSnapshot snap;
        proc.getMotionEffectiveEqSnapshot(snap);
        liveEqReadout.setText(formatMotionLiveReadout(snap), juce::dontSendNotification);
        repaint();
    }

    void placeRow(Row& r, juce::Rectangle<int> rowArea, juce::Rectangle<int>& lfoDotOut)
    {
        const int kw = 56;
        const int colGap = 6;
        const int phaseS = 14;
        const int titleW = 30;
        auto a = rowArea;
        lfoDotOut = { a.getX() + 1, a.getY() + 8, phaseS, phaseS };
        r.title.setBounds(a.getX() + phaseS + 4, a.getY() + 10, titleW, a.getHeight() - 12);
        int x = a.getX() + phaseS + 4 + titleW + colGap;

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
        motionOverviewProxy.setBounds(b.removeFromTop(20));
        b.removeFromTop(4);
        const int topH = kSliderColumnH + 14;
        auto top = b.removeFromTop(topH);
        stereoLabel.setBounds(top.getX(), top.getY() + 6, 112, 16);
        stereo.setBounds(top.getX() + 118, top.getY() + 1, kKnobSize, kSliderColumnH);
        b.removeFromTop(3);
        liveEqReadout.setBounds(b.removeFromTop(44));
        b.removeFromTop(3);

        placeRow(hi, b.removeFromTop(kRowHeight), lfoDotHi);
        placeRow(m1, b.removeFromTop(kRowHeight), lfoDotM1);
        placeRow(m2, b.removeFromTop(kRowHeight), lfoDotM2);
        placeRow(lo, b.removeFromTop(kRowHeight), lfoDotLo);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(kPanelBlack);
        float ph[4];
        proc.getMotionLfoPhases(ph);
        auto& ap = proc.getAPVTS();
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

struct ParaEQ301AudioProcessorEditor::OutTabContent : public juce::Component, public juce::SettableTooltipClient
{
    juce::Label note;

    OutTabContent()
    {
        setTooltip("Output limiter (two-stage compression + clip). Controls sit on the tab strip next to EQ / Curve + Motion / Output.");
        styleLabelDark(note,
                       "Limiter controls are on the tab bar (right): Lim on/off, ceiling (dB), release (ms).",
                       true);
        note.setJustificationType(juce::Justification::topLeft);
        note.setFont(juce::Font(juce::FontOptions().withHeight(11.5f)));
        addAndMakeVisible(note);
    }

    void paint(juce::Graphics& g) override { g.fillAll(kPanelBlack); }

    void resized() override { note.setBounds(getLocalBounds().reduced(14, 14)); }
};

struct ParaEQ301AudioProcessorEditor::CurveTabContent : public juce::Component, private juce::Timer
{
    explicit CurveTabContent(ParaEQ301AudioProcessor& p) : proc(p)
    {
        startTimerHz(20);
        specBefore.assign(240, -100.f);
        specAfter.assign(240, -100.f);

        curvePlotTooltip.setTooltip(
            "FFT spectrum (left channel). White / light fill + trace = after pre-EQ Core only (before EQ). "
            "Blue = after 4-band EQ and optional post-EQ Core 2 (before limiter).\n\n"
            "Amber plot = theoretical IIR magnitude (same ±30 dB idea as the EQ tab); it is not part of the FFT before/after comparison.\n\n"
            "Input / output levels: text + horizontal meters at the very top of the plugin (dBFS RMS).");
        addAndMakeVisible(curvePlotTooltip);
    }

    ~CurveTabContent() override { stopTimer(); }

    void timerCallback() override { repaint(); }

    void resized() override
    {
        plotArea = getLocalBounds().reduced(8);
        curvePlotTooltip.setBounds(plotArea);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(kPanelBlack);
        auto bounds = plotArea;
        if (bounds.getHeight() < 100)
            return;

        bounds.removeFromTop(2);
        auto plot = bounds.reduced(6, 2);
        if (plot.getHeight() < 80)
            return;

        const double sr = proc.getSampleRate() > 0.0 ? proc.getSampleRate() : 44100.0;
        constexpr int nPts = 240;
        if ((int) freqScratch.size() != nPts)
        {
            freqScratch.resize((size_t) nPts);
            magScratch.resize((size_t) nPts);
            eqMagSmoothed.resize((size_t) nPts);
            specBefore.resize((size_t) nPts);
            specAfter.resize((size_t) nPts);
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

        proc.getSpectrumBeforeAfterDb(sr, freqScratch.data(), nPts, specBefore.data(), specAfter.data());

        proc.getEqChainMagnitudeDb(sr, freqScratch.data(), magScratch.data(), nPts);
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

        auto specArea = plot.removeFromTop(juce::jmax(110, plot.getHeight() * 58 / 100));
        auto eqArea = plot;
        specArea.removeFromBottom(10);
        eqArea.removeFromTop(4);

        auto drawLogGrid = [&](juce::Rectangle<int> graph)
        {
            g.setColour(kTextMuted);
            g.setFont(9.0f);
            for (double hz : { 100.0, 1000.0, 10000.0 })
            {
                if (hz < fLo || hz > fHi)
                    continue;
                const double lf = std::log(hz);
                const float x = (float) (graph.getX() + (lf - logLo) / (logHi - logLo) * (double) graph.getWidth());
                g.drawVerticalLine(juce::roundToInt(x), (float) graph.getY(), (float) graph.getBottom());
                const juce::String lab = hz == 100.0 ? "100" : (hz == 1000.0 ? "1k" : "10k");
                g.drawText(lab, juce::roundToInt(x) - 12, graph.getBottom() + 2, 24, 10, juce::Justification::centred);
            }
        };

        auto xOfF = [&](double f, const juce::Rectangle<int>& graph) -> float
        {
            f = juce::jlimit(fLo, fHi, f);
            const double lf = std::log(f);
            return (float) (graph.getX() + (lf - logLo) / (logHi - logLo) * (double) graph.getWidth());
        };

        // --- Spectrum panel ---
        {
            juce::Rectangle<int> graph = specArea;
            graph.removeFromBottom(14);
            g.setColour(juce::Colour(0xff141414));
            g.fillRoundedRectangle(graph.toFloat(), 4.f);
            g.setColour(juce::Colour(0xff353535));
            g.drawRoundedRectangle(graph.toFloat(), 4.f, 1.f);

            float peak = -200.f;
            for (int i = 0; i < nPts; ++i)
            {
                peak = juce::jmax(peak, specBefore[(size_t) i], specAfter[(size_t) i]);
            }
            const float topDb = juce::jmin(0.f, peak + 4.f);
            const float botDb = topDb - 56.f;
            const float yBottom = (float) graph.getBottom();
            const float yTopPad = (float) graph.getY() + 2.f;
            const float yBotPad = yBottom - 3.f;
            auto ySpec = [&](float db) -> float
            {
                const float t = (db - botDb) / juce::jmax(1.0e-3f, topDb - botDb);
                const float yn = yBottom - juce::jlimit(0.f, 1.f, t) * (float) graph.getHeight();
                return juce::jlimit(yTopPad, yBotPad, yn);
            };

            drawLogGrid(graph);

            juce::Path fillB;
            bool st = false;
            for (int i = 0; i < nPts; ++i)
            {
                const float x = xOfF(freqScratch[(size_t) i], graph);
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
            fillB.lineTo(xOfF(freqScratch[(size_t) (nPts - 1)], graph), yBottom);
            fillB.closeSubPath();
            g.setColour(kTextBright.withAlpha(0.14f));
            g.fillPath(fillB);

            juce::Path lineB;
            st = false;
            for (int i = 0; i < nPts; ++i)
            {
                const float x = xOfF(freqScratch[(size_t) i], graph);
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
                const float x = xOfF(freqScratch[(size_t) i], graph);
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

        // --- Theoretical EQ curve (smaller) ---
        {
            juce::Rectangle<int> graph = eqArea;
            graph.removeFromBottom(12);
            if (graph.getHeight() < 24)
                return;

            g.setColour(juce::Colour(0xff1c1c1c));
            g.fillRoundedRectangle(graph.toFloat(), 4.f);
            g.setColour(juce::Colour(0xff353535));
            g.drawRoundedRectangle(graph.toFloat(), 4.f, 1.f);

            const juce::Colour kIirModelAmber(0xffffc266);
            ParaEQ301AudioProcessor::MotionEffectiveEqSnapshot curveSnap;
            proc.getMotionEffectiveEqSnapshot(curveSnap);
            paintEqMagnitudeCurveInRect(g, graph, freqScratch, eqMagSmoothed.data(), nPts, kEqMagPlotDbMin, kEqMagPlotDbMax, fLo, fHi,
                                        &proc.getAPVTS(), &curveSnap, kIirModelAmber, false, 0, 0);
        }
    }

    ParaEQ301AudioProcessor& proc;
    juce::Rectangle<int> plotArea;
    TooltipMouseProxy curvePlotTooltip;
    std::vector<double> freqScratch;
    std::vector<float> magScratch;
    std::vector<float> eqMagSmoothed;
    std::vector<float> specBefore;
    std::vector<float> specAfter;
};

/** Spectrum / meters (top) + Motion LFO controls (bottom) on one tab. */
struct ParaEQ301AudioProcessorEditor::CurveMotionTabContent : public juce::Component
{
    CurveMotionTabContent(ParaEQ301AudioProcessor& processor,
                          juce::AudioProcessorValueTreeState& ap,
                          std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>>& atts)
        : curve(processor), motion(processor, ap, atts)
    {
        addAndMakeVisible(curve);
        addAndMakeVisible(motion);
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced(4);
        constexpr int kGap = 6;
        // LfoTabContent needs ~530px inner height for Hi+M1+M2+Lo rows + compact chrome; reserve it or rows clip.
        constexpr int kMotionMinH = 420;
        const int totalH = b.getHeight();
        int curveH = totalH * 28 / 100;
        curveH = juce::jlimit(140, 260, curveH);
        if (curveH + kGap + kMotionMinH > totalH)
            curveH = juce::jmax(120, totalH - kGap - kMotionMinH);
        curve.setBounds(b.removeFromTop(curveH));
        b.removeFromTop(kGap);
        motion.setBounds(b);
    }

    CurveTabContent curve;
    LfoTabContent motion;
};

ParaEQ301AudioProcessorEditor::ParaEQ301AudioProcessorEditor(ParaEQ301AudioProcessor& p)
    : AudioProcessorEditor(&p), proc(p)
{
    auto& ap = proc.getAPVTS();

    eqPage = std::make_unique<EqTabContent>(proc, ap, attachments, buttonAttachments);
    curveMotionPage = std::make_unique<CurveMotionTabContent>(proc, ap, attachments);
    outPage = std::make_unique<OutTabContent>();

    tabs.addTab("EQ", kPanelBlack, eqPage.get(), false);
    tabs.addTab("Curve + Motion", kPanelBlack, curveMotionPage.get(), false);
    tabs.addTab("Output", kPanelBlack, outPage.get(), false);

    tabs.setColour(juce::TabbedComponent::backgroundColourId, kPanelBlack);
    tabs.getTabbedButtonBar().setColour(juce::TabbedButtonBar::tabOutlineColourId, juce::Colour(0xff333333));
    tabs.getTabbedButtonBar().setColour(juce::TabbedButtonBar::frontOutlineColourId, juce::Colour(0xff666666));
    tabs.getTabbedButtonBar().setColour(juce::TabbedButtonBar::tabTextColourId, kTextMuted);
    tabs.getTabbedButtonBar().setColour(juce::TabbedButtonBar::frontTextColourId, kTextBright);

    addAndMakeVisible(tabs);

    styleToggleDark(limOn);
    limOn.setTooltip("Output limiter on/off.");
    addAndMakeVisible(limOn);
    buttonAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(ap, "outLimOn", limOn));

    styleLinearSliderCompact(limThresh, kAccentGreen);
    limThresh.setTooltip("Limiter ceiling (dB).");
    addAndMakeVisible(limThresh);
    attachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "outLimThresh", limThresh));
    limThresh.textFromValueFunction = [](double v) { return juce::String(v, 1) + " dB"; };

    styleLinearSliderCompact(limRelease, kAccentGreen);
    limRelease.setTooltip("Limiter release (ms).");
    addAndMakeVisible(limRelease);
    attachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(ap, "outLimRelease", limRelease));
    limRelease.textFromValueFunction = [](double v) { return juce::String(juce::roundToInt(v)) + " ms"; };

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
    meterInLabel.setTooltip("Smoothed block RMS at the plugin audio input (monitoring).");
    meterOutLabel.setTooltip("Smoothed block RMS at the plugin audio output (monitoring).");

    startTimerHz(20);

    setSize(520, 760);
}

ParaEQ301AudioProcessorEditor::~ParaEQ301AudioProcessorEditor()
{
    stopTimer();
}

void ParaEQ301AudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(kPanelBlack);
    paintLevelMeterBar(g, meterInBarBounds, meterNormalisedFromRms(proc.getDebugInputRms()), kAccentGreen);
    paintLevelMeterBar(g, meterOutBarBounds, meterNormalisedFromRms(proc.getDebugOutputRms()), kAccentBlue);
}

void ParaEQ301AudioProcessorEditor::resized()
{
    constexpr int kMeterTop = 5;
    constexpr int kLineH = 15;
    constexpr int kBarH = 12;
    constexpr int kLabelW = 122;
    constexpr int kPad = 8;
    constexpr int kGapLabelBar = 5;
    constexpr int kMeterStripH = kMeterTop + kLineH + kMeterTop + 4;

    const int w = juce::jmax(200, getWidth());
    const int half = w / 2;

    meterInLabel.setBounds(kPad, kMeterTop, kLabelW, kLineH);
    const int inBarX = kPad + kLabelW + kGapLabelBar;
    const int inBarW = juce::jmax(24, half - inBarX - kPad);
    meterInBarBounds = { inBarX, kMeterTop + 1, inBarW, kBarH };

    const int outColX = half + kPad;
    meterOutLabel.setBounds(outColX, kMeterTop, kLabelW, kLineH);
    const int outBarX = outColX + kLabelW + kGapLabelBar;
    const int outBarW = juce::jmax(24, w - outBarX - kPad);
    meterOutBarBounds = { outBarX, kMeterTop + 1, outBarW, kBarH };

    tabs.setBounds(0, kMeterStripH, w, getHeight() - kMeterStripH);

    const int tabDepth = juce::jmax(28, tabs.getTabBarDepth());
    constexpr int kLimCtrlH = 21;
    const int limY = kMeterStripH + (tabDepth - kLimCtrlH) / 2;
    int xR = w - 8;
    const int sw = juce::jlimit(72, 104, (w - 200) / 2);
    limRelease.setBounds(xR - sw, limY, sw, kLimCtrlH);
    xR -= sw + 6;
    limThresh.setBounds(xR - sw, limY, sw, kLimCtrlH);
    xR -= sw + 8;
    limOn.setBounds(xR - 44, limY, 44, kLimCtrlH);

    limOn.toFront(false);
    limThresh.toFront(false);
    limRelease.toFront(false);
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
