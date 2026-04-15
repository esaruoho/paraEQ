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
               > 1.0e-5f;
    }

    bool eqBandHasMotion(juce::AudioProcessorValueTreeState& ap, int bandIndex) noexcept
    {
        switch (bandIndex)
        {
            case 0:
                return (ap.getRawParameterValue("lfoHiDepthGain")->load()
                        + ap.getRawParameterValue("lfoHiDepthCf")->load())
                       > 1.0e-5f;
            case 1:
                return (ap.getRawParameterValue("lfoM1DepthGain")->load()
                        + ap.getRawParameterValue("lfoM1DepthCf")->load()
                        + ap.getRawParameterValue("lfoM1DepthBw")->load())
                       > 1.0e-5f;
            case 2:
                return (ap.getRawParameterValue("lfoM2DepthGain")->load()
                        + ap.getRawParameterValue("lfoM2DepthCf")->load()
                        + ap.getRawParameterValue("lfoM2DepthBw")->load())
                       > 1.0e-5f;
            case 3:
                return (ap.getRawParameterValue("lfoLoDepthGain")->load()
                        + ap.getRawParameterValue("lfoLoDepthCf")->load())
                       > 1.0e-5f;
            default:
                return false;
        }
    }

    juce::String eqMotionTargetsSummary(juce::AudioProcessorValueTreeState& ap, int bandIndex)
    {
        auto on = [](float v) { return v > 1.0e-5f; };
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

    juce::String buildEqMotionStatusLine(juce::AudioProcessorValueTreeState& ap)
    {
        if (!motionLfoDepthActive(ap))
            return "Motion (LFO): idle - EQ is static. Open Curve + Motion and raise EQ gain / freq / width % to animate.\n"
                   "EQ knobs stay as center points.";

        return "Motion is moving the audio EQ (not these EQ knobs). Targets per band:\n"
               "Hi: " + eqMotionTargetsSummary(ap, 0) + "\n"
               "Mid1: " + eqMotionTargetsSummary(ap, 1) + "\n"
               "Mid2: " + eqMotionTargetsSummary(ap, 2) + "\n"
               "Low: " + eqMotionTargetsSummary(ap, 3) + "\n"
               "Curve + Motion: white/blue = FFT spectrum before/after EQ; amber = IIR model (not audio).";
    }

    juce::String formatMotionLiveReadout(const ParaEQ301AudioProcessor::MotionEffectiveEqSnapshot& s)
    {
        auto ri = [](float x) { return juce::String(juce::roundToInt(x)); };
        juce::String t;
        if (!s.motionEngaged)
            t = "Filters now (L channel) match the EQ tab. Set any amount below > 0% to hear LFO motion.\n";
        else
            t = "Filters now (L channel) - these numbers sweep with the LFO. EQ tab = rest point only (sliders do not move).\n";
        t += "Hi  " + ri(s.hiCfHz) + " Hz  " + juce::String(s.hiGainDb, 1) + " dB     ";
        t += "M1  " + ri(s.mid1CfHz) + " Hz  " + ri(s.mid1BwHz) + " Hz BW  " + juce::String(s.mid1GainDb, 1) + " dB\n";
        t += "M2  " + ri(s.mid2CfHz) + " Hz  " + ri(s.mid2BwHz) + " Hz BW  " + juce::String(s.mid2GainDb, 1) + " dB     ";
        t += "Lo  " + ri(s.loCfHz) + " Hz  " + juce::String(s.loGainDb, 1) + " dB";
        return t;
    }

    /** Fixed dB window (0 dB vertical centre = flat / unity gain). Optional coloured markers at Lo/M1/M2/Hi centre Hz. */
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
                                     juce::Colour curveStrokeColour,
                                     bool drawRoundedBackground)
    {
        if (graphOuter.getHeight() < 22 || nPts < 2)
            return;

        const double logLo = std::log(fLo);
        const double logHi = std::log(fHi);

        // Left ruler + plot + right scale labels (same numeric range, so max/min are obvious).
        constexpr int kRulerW = 54;
        constexpr int kRightScaleW = 44;
        juce::Rectangle<int> band = graphOuter;
        auto ruler = band.removeFromLeft(kRulerW);
        auto scaleRight = band.removeFromRight(kRightScaleW);
        auto curve = band;

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

        for (float db : { -24.f, -18.f, -12.f, -9.f, -6.f, -3.f, 3.f, 6.f, 9.f, 12.f, 18.f, 24.f })
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
                const float hz = apMarkers->getRawParameterValue(t.paramId)->load();
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
            return juce::jlimit(ruler.getY() + 1, ruler.getBottom() - 11, y);
        };

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
        g.strokePath(path, juce::PathStrokeType(2.2f));
    }

    /** Draws combined low shelf + 2 peaks + high shelf magnitude; emphasises 0 dB baseline. */
    void paintEqChainResponse(juce::Graphics& g, juce::Rectangle<int> fullArea,
                              ParaEQ301AudioProcessor& proc,
                              std::vector<double>& freqScratch, std::vector<float>& magScratch,
                              float dMin, float dMax, bool motionLfoActive, bool showShelfTopologyHint)
    {
        if (fullArea.getHeight() < 36)
            return;

        const int shelfHintH = showShelfTopologyHint ? 13 : 0;
        const int motionBlockH = motionLfoActive ? 28 : 14;
        auto header = fullArea.removeFromTop(shelfHintH + motionBlockH);

        if (showShelfTopologyHint)
        {
            g.setFont(9.2f);
            g.setColour(kAccentBlue.withAlpha(0.9f));
            g.drawText("Lo / Hi = low shelf & high shelf (tilt EQ). Shelf Hz = turnover where the curve aims at Gain - not a low-pass or high-pass (nothing is fully muted).",
                       header.removeFromTop(shelfHintH), juce::Justification::centredLeft, true);
        }

        g.setColour(kTextBright);
        if (motionLfoActive)
        {
            g.setFont(9.8f);
            g.setColour(kAccentGreen.withAlpha(0.95f));
            g.drawText("Green = combined EQ gain vs flat. Plot is +24 dB (top) to -24 dB (bottom); centre = 0 dB unity. Includes Motion.",
                       header.removeFromTop(14), juce::Justification::centredLeft, true);
            g.setColour(kTextMuted);
            g.drawText("EQ tab knobs stay put; they are the rest points the LFO moves around.",
                       header, juce::Justification::centredLeft, true);
        }
        else
        {
            g.setFont(10.5f);
            g.drawText("Signal path: Low shelf, Mid1, Mid2, High shelf. Dashed = 0 dB (flat). Top/bottom of plot = +24 / -24 dB combined gain (per-band knobs allow +/-30 dB). Tags = Hz knobs.",
                       header, juce::Justification::centredLeft, true);
        }
        g.setColour(kTextBright);

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

        paintEqMagnitudeCurveInRect(g, plot, freqScratch, magScratch.data(), nPts, dMin, dMax, fLo, fHi,
                                    &proc.getAPVTS(), kAccentGreen, true);
    }
}

struct ParaEQ301AudioProcessorEditor::EqTabContent : public juce::Component, private juce::Timer
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
    juce::Label motionStatus;

    ParaEQ301AudioProcessor& proc;
    juce::Rectangle<int> eqGraphBounds;
    juce::Rectangle<int> motionRowRect[4];
    std::vector<double> freqScratch;
    std::vector<float> magScratch;

    EqBandLookAndFeel eqBandLookAndFeel;

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

        motionStatus.setJustificationType(juce::Justification::topLeft);
        motionStatus.setFont(juce::Font(juce::FontOptions().withHeight(9.6f)));
        motionStatus.setColour(juce::Label::textColourId, kAccentBlue);
        motionStatus.setMinimumHorizontalScale(1.0f);
        motionStatus.setText(buildEqMotionStatusLine(ap), juce::dontSendNotification);
        motionStatus.setTooltip("While Motion amounts on Curve + Motion are above 0%, those EQ parameters are swept in the DSP even though the knobs do not move. "
                                "Blue row outline = that band is affected. The green curve above shows the live result.");
        addAndMakeVisible(motionStatus);

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

        styleSliderDark(coreSat, kAccentGreen);
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

        startTimerHz(15);
    }

    ~EqTabContent() override
    {
        stopTimer();
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
        motionStatus.setText(buildEqMotionStatusLine(proc.getAPVTS()), juce::dontSendNotification);
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
            paintEqChainResponse(g, eqGraphBounds, proc, freqScratch, magScratch, -24.f, 24.f,
                                 motionLfoDepthActive(proc.getAPVTS()), true);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(8);
        constexpr int kEqGraphH = 158;
        eqGraphBounds = bounds.removeFromTop(kEqGraphH);
        bounds.removeFromTop(5);
        motionStatus.setBounds(bounds.removeFromTop(78));
        bounds.removeFromTop(6);

        constexpr int kFooterH = kSliderColumnH + kGapCaption + kCaptionH + 8;
        auto footer = bounds.removeFromBottom(kFooterH);
        coreOn.setBounds(footer.removeFromLeft(108).removeFromTop(24).translated(0, 4));
        const int cx = footer.getX() + 6;
        const int cy = footer.getY() + 4;
        coreSat.setBounds(cx, cy, kKnobSize, kSliderColumnH);
        coreSatLabel.setBounds(cx, coreSat.getBottom() + kGapCaption, kKnobSize, kCaptionH);

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
    juce::Label motionHint;
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
        motionHint.setText(
            "Hi / M1 / M2 / Lo rows = Motion for those EQ bands. EQ tab knobs = center; LFO moves coeffs. EQ gain depth up to +/-12 dB. Blue dots = LFO phase.",
            juce::dontSendNotification);
        motionHint.setJustificationType(juce::Justification::topLeft);
        motionHint.setFont(juce::Font(juce::FontOptions().withHeight(9.4f)));
        motionHint.setColour(juce::Label::textColourId, kAccentBlue);
        motionHint.setTooltip(
            "Hi row -> EQ Hi shelf: Gain dB + Shelf Hz.\n"
            "M1 / M2 -> EQ Mid1 / Mid2: Gain + Peak Hz + Width Hz.\n"
            "Lo row -> EQ Low shelf: Gain + Shelf Hz.\n"
            "EQ tab blue outline = band has Motion. Spectrum on this tab = audio FFT.");
        addAndMakeVisible(motionHint);

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
            r.rate.setTooltip("LFO speed in Hz. On its own it does nothing: turn up EQ gain / EQ freq / EQ width amounts to hear motion.");

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
        motionHint.setBounds(b.removeFromTop(30));
        b.removeFromTop(3);
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
        const bool anyMotion = (dHiG + dHiC + dM1G + dM1C + dM1B + dM2G + dM2C + dM2B + dLoG + dLoC) > 1.0e-5f;

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
    explicit CurveTabContent(ParaEQ301AudioProcessor& p) : proc(p)
    {
        startTimerHz(20);
        specBefore.assign(240, -100.f);
        specAfter.assign(240, -100.f);
    }

    ~CurveTabContent() override { stopTimer(); }

    void timerCallback() override { repaint(); }

    void resized() override { plotArea = getLocalBounds().reduced(8); }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(kPanelBlack);
        auto bounds = plotArea;
        if (bounds.getHeight() < 100)
            return;

        const int footerH = 48;
        auto footer = bounds.removeFromBottom(footerH);
        bounds.removeFromTop(2);
        g.setColour(kTextMuted);
        g.setFont(10.5f);
        g.drawFittedText("Top: FFT spectrum (L ch). White/light trace = before 4-band EQ (after Core if on). Blue = after EQ (before limiter). "
                         "Bottom: amber line = theoretical IIR magnitude only (same scale idea as EQ tab); not part of the FFT A/B.",
                         footer, juce::Justification::topLeft, 7);

        g.setFont(12.f);
        g.setColour(kTextBright);
        g.drawText("Spectrum: white = before, blue = after  |  below: IIR model (amber)", bounds.removeFromTop(16), juce::Justification::centred, true);

        const int meterBlockH = 44;
        auto meters = bounds.removeFromBottom(meterBlockH + 6);
        drawLevelBar(g, meters.removeFromTop(18), proc.getDebugInputRms(), "In", kAccentGreen);
        meters.removeFromTop(6);
        drawLevelBar(g, meters, proc.getDebugOutputRms(), "Out", kAccentBlue);

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

            g.setColour(kTextBright);
            g.setFont(9.5f);
            g.drawText("Before EQ", graph.getX() + 6, graph.getY() + 4, 90, 14, juce::Justification::centredLeft);
            g.setColour(kAccentBlue);
            g.drawText("After EQ", graph.getRight() - 96, graph.getY() + 4, 90, 14, juce::Justification::centredRight);
        }

        // --- Theoretical EQ curve (smaller) ---
        {
            g.setColour(kTextMuted);
            g.setFont(10.f);
            g.drawText("IIR model (amber) - math only, not FFT; same band tags as EQ tab", eqArea.removeFromTop(12), juce::Justification::centredLeft, true);
            juce::Rectangle<int> graph = eqArea;
            graph.removeFromBottom(12);
            if (graph.getHeight() < 24)
                return;

            g.setColour(juce::Colour(0xff1c1c1c));
            g.fillRoundedRectangle(graph.toFloat(), 4.f);
            g.setColour(juce::Colour(0xff353535));
            g.drawRoundedRectangle(graph.toFloat(), 4.f, 1.f);

            const juce::Colour kIirModelAmber(0xffffc266);
            paintEqMagnitudeCurveInRect(g, graph, freqScratch, eqMagSmoothed.data(), nPts, -24.f, 24.f, fLo, fHi,
                                        &proc.getAPVTS(), kIirModelAmber, false);
        }
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
        constexpr int kMotionMinH = 540;
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
    outPage = std::make_unique<OutTabContent>(ap, attachments, buttonAttachments);

    tabs.addTab("EQ", kPanelBlack, eqPage.get(), false);
    tabs.addTab("Curve + Motion", kPanelBlack, curveMotionPage.get(), false);
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

    setSize(500, 960);
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
