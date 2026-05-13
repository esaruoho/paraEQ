#pragma once

#include <JuceHeader.h>
#include <cmath>

/** Paketti-style Chebyshev harmonic bank + Magnet asymmetric saturator (ported from PakettiChebyshevWaveshaper.lua ideas). */
namespace paketti
{
    static constexpr int kChebyLutPoints = 4096;
    static constexpr int kChebyHarmonics = 12;

    inline float clampf(float x, float lo, float hi) noexcept
    {
        return juce::jlimit(lo, hi, x);
    }

    /** Piecewise quadratic Bézier pre-warp on [-1,1] (matches Lua make_precurve). */
    inline float chebyPrecurve(float x, float yL, float yC, float yR) noexcept
    {
        yL = clampf(yL, -1.f, 1.f);
        yC = clampf(yC, -1.f, 1.f);
        yR = clampf(yR, -1.f, 1.f);
        auto bez2 = [](float p0, float p1, float p2, float t) noexcept
        {
            const float u = 1.f - t;
            return u * u * p0 + 2.f * u * t * p1 + t * t * p2;
        };
        if (x <= 0.f)
        {
            const float t = x + 1.f;
            return bez2(yL, (yL + yC) * 0.5f, yC, t);
        }
        const float t = x;
        return bez2(yC, (yC + yR) * 0.5f, yR, t);
    }

    /** Clenshaw for sum_{k=0}^{maxN} c_k T_k(x), coeffs[k] = c_k, typically c0=c1=0. */
    inline float chebyClenshaw(float x, const float* coeffs, int maxN) noexcept
    {
        float b1 = 0.f, b2 = 0.f;
        const float c0 = coeffs[0];
        for (int k = maxN; k >= 1; --k)
        {
            const float ck = coeffs[(size_t) k];
            const float b0 = 2.f * x * b1 - b2 + ck;
            b2 = b1;
            b1 = b0;
        }
        return x * b1 - b2 + c0;
    }

    inline bool chebyCurveHasDc(float yL, float yC, float yR) noexcept
    {
        return (std::abs(yL + yR) > 1.0e-6f) || (std::abs(yC) > 1.0e-6f);
    }

    /** Highest Chebyshev order index with nonzero coeff (>=1). If none, returns 1. */
    inline int chebyMaxActiveN(const float* harm12) noexcept
    {
        for (int k = 11; k >= 0; --k)
            if (std::abs(harm12[(size_t) k]) > 1.0e-24f)
                return k + 2;
        return 1;
    }

    /** Signed power: sign(t) * pow(|t|, k). Identity when k==1. */
    inline float signedPow(float t, float k) noexcept
    {
        if (std::abs(k - 1.f) < 1.0e-6f)
            return t;
        const float a = std::abs(t);
        if (a < 1.0e-20f)
            return 0.f;
        return std::copysign(std::pow(a, k), t);
    }

    /** Build normalized LUT: input in [-1,1] maps to nl(curve(x)) scaled to peak 0.99.
        `polyPowGlobal` is a macro exponent applied to every Tn. `polyPow12[i]` (i=0..11 for T_{i+2}) is per-polynomial.
        Effective exponent for Tn = polyPowGlobal * polyPow12[n-2]. All-1.0f is the original behavior. */
    inline void rebuildChebyLut(float yL, float yC, float yR, const float* harm12,
                                float polyPowGlobal, const float* polyPow12,
                                float* lut, int lutSize) noexcept
    {
        if (lut == nullptr || lutSize < 2)
            return;
        const int maxN = juce::jmax(1, chebyMaxActiveN(harm12));

        float keff[kChebyHarmonics];
        bool allUnity = std::abs(polyPowGlobal - 1.f) < 1.0e-6f;
        for (int i = 0; i < kChebyHarmonics; ++i)
        {
            const float per = (polyPow12 != nullptr) ? polyPow12[(size_t) i] : 1.f;
            keff[i] = polyPowGlobal * per;
            if (std::abs(keff[i] - 1.f) > 1.0e-6f)
                allUnity = false;
        }
        const bool useClenshaw = allUnity;

        float coeffs[14] {};
        if (useClenshaw)
        {
            coeffs[0] = 0.f;
            coeffs[1] = 0.f;
            for (int i = 0; i < kChebyHarmonics; ++i)
                coeffs[i + 2] = harm12[(size_t) i];
        }

        float maxAbs = 0.f;
        for (int i = 0; i < lutSize; ++i)
        {
            const float t = (lutSize <= 1) ? 0.f : (float) i / (float) (lutSize - 1);
            const float xDom = -1.f + 2.f * t;
            const float u = chebyPrecurve(xDom, yL, yC, yR);

            float y;
            if (useClenshaw)
            {
                y = chebyClenshaw(u, coeffs, maxN);
            }
            else
            {
                // Explicit Tn recurrence so per-polynomial signedPow can shape each harmonic independently.
                float Tprev = 1.f;     // T0(u)
                float Tcurr = u;       // T1(u)
                y = 0.f;
                for (int k = 2; k <= maxN; ++k)
                {
                    const float Tn = 2.f * u * Tcurr - Tprev;
                    Tprev = Tcurr;
                    Tcurr = Tn;
                    const float w = harm12[(size_t) (k - 2)];
                    if (std::abs(w) > 1.0e-24f)
                        y += w * signedPow(Tn, keff[(size_t) (k - 2)]);
                }
            }

            lut[(size_t) i] = y;
            maxAbs = juce::jmax(maxAbs, std::abs(y));
        }
        const float g = (maxAbs > 1.0e-20f) ? (0.99f / maxAbs) : 1.f;
        if (g != 1.f)
            for (int i = 0; i < lutSize; ++i)
                lut[(size_t) i] *= g;
    }

    inline float chebyLutEval(const float* lut, int lutSize, float x) noexcept
    {
        if (lut == nullptr || lutSize < 2)
            return 0.f;
        x = clampf(x, -1.f, 1.f);
        const float scale = 0.5f * (float) (lutSize - 1);
        float t = x * scale + scale;
        int i = (int) std::floor(t);
        const float frac = t - (float) i;
        i = juce::jlimit(0, lutSize - 2, i);
        const float a = lut[(size_t) i];
        const float b = lut[(size_t) i + 1];
        return a + (b - a) * frac;
    }

    inline float magnetSoftsat(float v) noexcept
    {
        const float a = std::abs(v);
        return v / (1.f + a);
    }

    inline float magnetSlewLimit(float yTarget, float yPrev, float step) noexcept
    {
        const float dy = yTarget - yPrev;
        const float ady = std::abs(dy);
        if (ady <= step)
            return yTarget;
        return yPrev + (dy >= 0.f ? step : -step);
    }

    inline void magnetComputeGains(float drive, float tilt, float bias, float fbTerm,
                                   float& gp, float& gn) noexcept
    {
        const float gBase = std::pow(2.f, drive);
        const float tEff = clampf(tilt + fbTerm, -1.f, 1.f);
        constexpr float sk = 0.85f;
        gp = gBase * (1.f + sk * (tEff + bias));
        gn = gBase * (1.f + sk * (-tEff - bias));
        gp = clampf(gp, 0.1f, 16.f);
        gn = clampf(gn, 0.1f, 16.f);
    }

    inline float magnetSlewStepFromLimit(float user01, float osFactor) noexcept
    {
        const float u = clampf(user01, 0.f, 1.f);
        const float base = 0.001f + 0.029f * u;
        const float scale = std::sqrt(juce::jmax(1.f, osFactor));
        return base * scale;
    }

    struct MagnetShaperState
    {
        float y1 = 0.f;
    };

    /** u should be in [-1,1]. Returns shaped sample in [-1,1]. slewStep from magnetSlewStepFromLimit. */
    inline float magnetProcessSample(float u, float drive, float tilt, float bias,
                                     float feedback01, float magOut, float slewStep, MagnetShaperState& st) noexcept
    {
        const float yPrev = st.y1;
        const float fbTerm = feedback01 * (0.5f * yPrev);
        float gp = 1.f, gn = 1.f;
        magnetComputeGains(drive, tilt, bias, fbTerm, gp, gn);
        const float xp = (u > 0.f) ? u : 0.f;
        const float xn = (u < 0.f) ? u : 0.f;
        const float yp = magnetSoftsat(gp * xp);
        const float yn = magnetSoftsat(gn * xn);
        const float yLin = yp + yn;
        const float y = magnetSlewLimit(yLin, yPrev, slewStep);
        st.y1 = y;
        return clampf(y * magOut, -1.f, 1.f);
    }

    inline float dcBlockIir(float x, float& x1, float& y1, float R = 0.995f) noexcept
    {
        const float y = x - x1 + R * y1;
        x1 = x;
        y1 = y;
        return y;
    }

    inline uint32_t hashChebyParams(float yL, float yC, float yR, const float* h12, float harmMacro01,
                                    float polyPowGlobal, const float* polyPow12) noexcept
    {
        auto q = [](float v) -> uint32_t
        {
            return (uint32_t) juce::roundToInt(v * 10000.f);
        };
        uint32_t h = q(yL) ^ (q(yC) << 1) ^ (q(yC) >> 3) ^ (q(yR) << 2) ^ (q(harmMacro01) << 5) ^ (q(polyPowGlobal) << 7);
        for (int i = 0; i < kChebyHarmonics; ++i)
        {
            h ^= q(h12[(size_t) i]) << (unsigned) (i % 17);
            if (polyPow12 != nullptr)
                h ^= q(polyPow12[(size_t) i]) << (unsigned) ((i + 7) % 19);
        }
        return h;
    }
} // namespace paketti
