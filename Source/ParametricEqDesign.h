#pragma once

#include <JuceHeader.h>
#include <cmath>

/** Orfanidis second-order peaking EQ (unity at DC & Nyquist). Digital bandwidth Dw from Q via
    sinh mapping (Orfanidis / peqio style) and band-edge gain Gb = sqrt(G*G0) between reference
    and peak. Biquad matches peq.m (Intro. to Signal Processing). Ref: S.J. Orfanidis, peq.m */
inline juce::dsp::IIR::Coefficients<float>::Ptr makeOrfanidisPeakCoefficients(double sampleRate,
                                                                             double f0Hz,
                                                                             double Q,
                                                                             double linearGain)
{
    using Coeff = juce::dsp::IIR::Coefficients<float>;
    const double sr = juce::jmax(100.0, sampleRate);
    f0Hz = juce::jlimit(1.0, sr * 0.499, f0Hz);
    Q = juce::jmax(0.05, Q);

    const double w0 = juce::MathConstants<double>::twoPi * f0Hz / sr;
    const double cosw0 = std::cos(w0);
    const double sinw0 = std::sin(w0);

    const double G0 = 1.0;
    double G = static_cast<double>(linearGain);
    // Match EQ knob range (~+/-30 dB); avoids absurd linear values if mis-scaled.
    G = juce::jlimit(1.0 / 32.0, 32.0, G);

    if (std::abs(G - G0) < 1.0e-6)
        return Coeff::makePeakFilter(sr, (float) f0Hz, (float) Q, 1.f);

    const double GB = std::sqrt(juce::jmax(1.0e-20, G * G0));

    const double Dw = 2.0 * w0 * std::sinh((sinw0 / juce::jmax(1.0e-12, w0)) * std::asinh(1.0 / (2.0 * Q)));
    const double tanDw2 = std::tan(juce::jlimit(1.0e-10, juce::MathConstants<double>::halfPi - 1.0e-6, Dw * 0.5));

    // Orfanidis peq.m: beta = sqrt((GB^2 - G0^2) / (G^2 - GB^2)) * tan(Dw/2)
    const double den = juce::jmax(1.0e-24, (G * G - GB * GB));
    const double num = (GB * GB - G0 * G0);
    const double ratio = num / den;
    if (ratio <= 0.0 || !std::isfinite(ratio))
        return Coeff::makePeakFilter(sr, (float) f0Hz, (float) Q, (float) linearGain);

    double beta = std::sqrt(ratio) * tanDw2;
    beta = juce::jlimit(0.0, 100.0, beta);

    const double onePb = 1.0 + beta;
    const double b0 = (G0 + G * beta) / onePb;
    const double b1 = (-2.0 * G0 * cosw0) / onePb;
    const double b2 = (G0 - G * beta) / onePb;
    const double a0 = 1.0;
    const double a1 = (-2.0 * cosw0) / onePb;
    const double a2 = (1.0 - beta) / onePb;

    return *new Coeff(static_cast<float>(b0),
                       static_cast<float>(b1),
                       static_cast<float>(b2),
                       static_cast<float>(a0),
                       static_cast<float>(a1),
                       static_cast<float>(a2));
}

/** Stiff-string inharmonic partial: f_n = n * f0 * sqrt(1 + B * (n^2 - 1)), n >= 1. */
inline double stiffStringPartialHz(double f0, int n, double B) noexcept
{
    if (n < 1)
        return f0;
    const double nn = static_cast<double>(n * n);
    return static_cast<double>(n) * f0 * std::sqrt(juce::jmax(1.0e-12, 1.0 + B * (nn - 1.0)));
}
