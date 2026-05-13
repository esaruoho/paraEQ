#pragma once

#include "PakettiShapers.h"
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>

/** Background Chebyshev LUT builder (double-buffered). Audio thread calls resolveLut(); sync rebuild only if publish lags. */
class ChebyLutBuilder final : private juce::Thread
{
public:
    ChebyLutBuilder();
    ~ChebyLutBuilder() override;

    void startBuilder();
    void stopBuilder();

    /** Queue coalesced rebuild (latest params win). Safe from audio or message thread. */
    void requestBuild(std::uint32_t hash, float yL, float yC, float yR, const float* harm12, float harmMacro01,
                      float polyPowGlobal, const float* polyPow12);

    /** Returns LUT for hash: published buffer if ready, else fills syncScratch on the caller thread. */
    const float* resolveLut(std::uint32_t hash, float yL, float yC, float yR, const float* harm12, float harmMacro01,
                            float polyPowGlobal, const float* polyPow12,
                            std::array<float, paketti::kChebyLutPoints>& syncScratch);

private:
    void run() override;

    struct Order
    {
        std::uint32_t hash = 0xffffffffu;
        float yL = -1.f;
        float yC = 0.f;
        float yR = 1.f;
        float harm12[12] {};
        float harmMacro01 = 1.f;
        float polyPowGlobal = 1.f;
        float polyPow12[12] { 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f };
        bool valid = false;
    };

    std::mutex mutex;
    std::condition_variable condition;
    std::atomic<bool> exitRequested { false };
    Order pending;

    std::array<std::array<float, paketti::kChebyLutPoints>, 2> buffers {};
    std::atomic<int> readBuffer { 0 };
    std::atomic<std::uint32_t> publishedHash { 0xffffffffu };
};
