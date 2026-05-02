#include "ChebyLutBuilder.h"
#include <cstring>

ChebyLutBuilder::ChebyLutBuilder()
    : juce::Thread("ParaEQ ChebyLUT")
{
}

ChebyLutBuilder::~ChebyLutBuilder()
{
    stopBuilder();
}

void ChebyLutBuilder::startBuilder()
{
    if (isThreadRunning())
        return;
    exitRequested.store(false, std::memory_order_release);
    startThread(juce::Thread::Priority::low);
}

void ChebyLutBuilder::stopBuilder()
{
    exitRequested.store(true, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(mutex);
        pending.valid = false;
    }
    condition.notify_all();
    signalThreadShouldExit();
    stopThread(6000);
}

void ChebyLutBuilder::requestBuild(std::uint32_t hash, float yL, float yC, float yR, const float* harm12, float harmMacro01)
{
    if (harm12 == nullptr)
        return;
    {
        std::lock_guard<std::mutex> lock(mutex);
        pending.hash = hash;
        pending.yL = yL;
        pending.yC = yC;
        pending.yR = yR;
        pending.harmMacro01 = harmMacro01;
        std::memcpy(pending.harm12, harm12, sizeof(pending.harm12));
        pending.valid = true;
    }
    condition.notify_one();
}

const float* ChebyLutBuilder::resolveLut(std::uint32_t hash, float yL, float yC, float yR, const float* harm12, float harmMacro01,
                                           std::array<float, paketti::kChebyLutPoints>& syncScratch)
{
    if (harm12 == nullptr)
        return syncScratch.data();

    const std::uint32_t pub = publishedHash.load(std::memory_order_acquire);
    if (pub == hash)
        return buffers[(size_t) readBuffer.load(std::memory_order_acquire)].data();

    requestBuild(hash, yL, yC, yR, harm12, harmMacro01);

    const std::uint32_t pub2 = publishedHash.load(std::memory_order_acquire);
    if (pub2 == hash)
        return buffers[(size_t) readBuffer.load(std::memory_order_acquire)].data();

    float scaled[12];
    for (int i = 0; i < 12; ++i)
        scaled[(size_t) i] = harm12[(size_t) i] * harmMacro01;
    paketti::rebuildChebyLut(yL, yC, yR, scaled, syncScratch.data(), paketti::kChebyLutPoints);
    return syncScratch.data();
}

void ChebyLutBuilder::run()
{
    while (!threadShouldExit())
    {
        Order job {};
        bool haveJob = false;
        {
            std::unique_lock<std::mutex> lock(mutex);
            condition.wait(lock, [this]
                             { return exitRequested.load(std::memory_order_acquire) || pending.valid; });
            if (threadShouldExit() || exitRequested.load(std::memory_order_acquire))
                break;
            if (pending.valid)
            {
                job = pending;
                pending.valid = false;
                haveJob = true;
            }
        }
        if (!haveJob)
            continue;

        float scaled[12];
        for (int i = 0; i < 12; ++i)
            scaled[(size_t) i] = job.harm12[(size_t) i] * job.harmMacro01;

        const int writeIx = 1 - readBuffer.load(std::memory_order_relaxed);
        paketti::rebuildChebyLut(job.yL, job.yC, job.yR, scaled, buffers[(size_t) writeIx].data(), paketti::kChebyLutPoints);
        readBuffer.store(writeIx, std::memory_order_release);
        publishedHash.store(job.hash, std::memory_order_release);
    }
}
