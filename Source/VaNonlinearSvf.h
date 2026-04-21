#pragma once

#include <JuceHeader.h>

/** Trapezoidal ZDF SVF (Simper / Huovilainen form). Bandpass output; `nl01` soft-saturates the loop input v0−ic2
    for amplitude-dependent resonance limiting (VA-style). */
struct VaNonlinearSvfChannel
{
    float ic1eq = 0.f, ic2eq = 0.f;
    float a1 = 0.f, a2 = 0.f, a3 = 0.f, k = 0.f;

    void reset() noexcept
    {
        ic1eq = ic2eq = 0.f;
    }

    void setCoeffs(float fcHz, float Q, double sampleRate) noexcept;

    /** Returns bandpass sample; updates integrator state. */
    float processBandpassNonlinear(float in, float nl01) noexcept;
};
