#include "Dsp/DroneEngine.h"
#include "Dsp/DroneMath.h"
#include <StillTestReference.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace
{
int failures = 0;

void require(bool ok, const char* message)
{
    if (!ok)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

struct Stats
{
    double squares = 0.0;
    float peak = 0.0f;
    float maxStep = 0.0f;
    int count = 0;
};

void measure(const juce::AudioBuffer<float>& buffer, Stats& stats)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        const auto* samples = buffer.getReadPointer(channel);
        for (int index = 0; index < buffer.getNumSamples(); ++index)
        {
            require(std::isfinite(samples[index]), "output remains finite");
            stats.peak = std::max(stats.peak, std::abs(samples[index]));
            stats.squares += static_cast<double>(samples[index]) * samples[index];
            if (index > 0)
                stats.maxStep = std::max(stats.maxStep, std::abs(samples[index] - samples[index - 1]));
            ++stats.count;
        }
    }
}

double rms(const Stats& stats)
{
    return stats.count > 0 ? std::sqrt(stats.squares / stats.count) : 0.0;
}

void processBlocks(still::dsp::DroneEngine& engine, juce::AudioBuffer<float>& block,
                   int blocks, float open, float air, int key = 2, float level = 0.0f,
                   float brightness = 1.0f, float motion = 0.0f, Stats* stats = nullptr)
{
    for (int index = 0; index < blocks; ++index)
    {
        engine.process(block, open, air, true, key, level, brightness, motion);
        if (stats != nullptr)
            measure(block, *stats);
    }
}
}

int main()
{
    const auto closed = still::dsp::openGains(0.0f);
    const auto half = still::dsp::openGains(0.5f);
    const auto full = still::dsp::openGains(1.0f);
    require(std::abs(closed.body - 0.34f) < 1.0e-6f, "closed OPEN retains BODY floor");
    require(std::abs(closed.swirl) < 1.0e-6f, "closed OPEN mutes SWIRL");
    require(half.body > closed.body && half.body < 1.0f, "BODY rises through OPEN");
    require(half.swirl > 0.0f && half.swirl < half.body, "SWIRL rises later");
    require(std::abs(full.body - 1.0f) < 1.0e-6f && std::abs(full.swirl - 1.0f) < 1.0e-6f,
            "full OPEN reaches unity");

    constexpr int blockSize = 512;
    constexpr int aFrames = 276000;
    constexpr int airFrames = 249600;
    constexpr int seamProbeFrames = aFrames + 32;
    juce::AudioBuffer<float> block(2, blockSize);
    juce::AudioBuffer<float> seamProbe(2, seamProbeFrames);
    juce::AudioBuffer<float> expected(2, blockSize);
    still::dsp::DroneEngine engine;

    require(engine.prepare(48000.0, 1.0f, 0.0f, true), "all embedded stereo key banks decode");
    require(engine.assetsReady(), "runtime audio banks are ready");
    const auto residentBytes = engine.residentAudioBytes();
    require(residentBytes > 70u * 1024u * 1024u && residentBytes < 90u * 1024u * 1024u,
            "all-key decoded audio stays within the expected memory budget");

    Stats fullOpen;
    processBlocks(engine, block, (48000 * 10) / blockSize, 1.0f, 0.0f, 2, 0.0f, 1.0f, 0.0f, &fullOpen);
    require(rms(fullOpen) > 0.10, "full-open drone produces a healthy signal");
    require(fullOpen.peak < 0.9f, "full-open D reference keeps safe headroom");
    require(fullOpen.maxStep < 0.12f, "D loop output has no large discontinuity");

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    auto referenceStream = std::make_unique<juce::MemoryInputStream>(
        StillTestReference::D_A_FULL_STAGE_BACKUP_48k_wav,
        static_cast<std::size_t>(StillTestReference::D_A_FULL_STAGE_BACKUP_48k_wavSize), false);
    std::unique_ptr<juce::AudioFormatReader> referenceReader(formats.createReaderFor(std::move(referenceStream)));
    require(referenceReader != nullptr, "full-open reference decodes for render comparison");
    if (referenceReader != nullptr)
    {
        constexpr int rampBlocks = 8;
        require(engine.prepare(48000.0, 1.0f, 0.0f, true), "reference comparison engine prepares");
        processBlocks(engine, block, rampBlocks, 1.0f, 0.0f);
        const auto referenceOffset = rampBlocks * blockSize;
        require(referenceReader->read(&expected, 0, blockSize, referenceOffset, true, true),
                "reference segment decodes");
        engine.process(block, 1.0f, 0.0f, true, 2, 0.0f, 1.0f, 0.0f);
        float maxDifference = 0.0f;
        for (int channel = 0; channel < 2; ++channel)
            for (int index = 0; index < blockSize; ++index)
                maxDifference = std::max(maxDifference,
                    std::abs(block.getSample(channel, index) - expected.getSample(channel, index)));
        require(maxDifference <= 5.0e-7f, "48 kHz full-open D output matches the supplied reference");
    }

    require(engine.prepare(48000.0, 0.65f, 1.0f, true), "AIR wet comparison prepares");
    engine.process(block, 0.65f, 1.0f, true, 2, 0.0f, 1.0f, 0.0f);
    juce::AudioBuffer<float> wetBlock(2, blockSize);
    wetBlock.makeCopyOf(block);
    require(engine.prepare(48000.0, 0.65f, 0.0f, true), "AIR dry comparison prepares");
    engine.process(block, 0.65f, 0.0f, true, 2, 0.0f, 1.0f, 0.0f);
    float maxAirDifference = 0.0f;
    for (int channel = 0; channel < 2; ++channel)
        for (int index = 0; index < blockSize; ++index)
            maxAirDifference = std::max(maxAirDifference,
                std::abs(wetBlock.getSample(channel, index) - block.getSample(channel, index)));
    require(maxAirDifference > 1.0e-4f, "AIR changes output independently of OPEN");

    for (int key = 0; key < still::dsp::DroneEngine::keyCount; ++key)
    {
        require(engine.prepare(48000.0, 1.0f, 0.0f, true, key, 0.0f, 1.0f, 0.0f),
                "selected key prepares without loading in the audio callback");
        require(engine.residentAudioBytes() == residentBytes, "decoded bank memory remains stable across keys");
        engine.process(seamProbe, 1.0f, 0.0f, true, key, 0.0f, 1.0f, 0.0f);
        const auto seamStepLeft = std::abs(seamProbe.getSample(0, aFrames) - seamProbe.getSample(0, aFrames - 1));
        const auto seamStepRight = std::abs(seamProbe.getSample(1, aFrames) - seamProbe.getSample(1, aFrames - 1));
        require(seamStepLeft < 0.08f && seamStepRight < 0.08f,
                "each key bank loops without a large BODY/SWIRL discontinuity");
        Stats keyStats;
        measure(seamProbe, keyStats);
        require(keyStats.peak < 1.0f, "each key bank stays below full-scale at unity output");
    }

    require(engine.prepare(44100.0, 0.5f, 0.0f, true), "44.1 kHz host prepares");
    Stats alternateRate;
    processBlocks(engine, block, 44100 / blockSize, 0.5f, 0.0f, 2, 0.0f, 1.0f, 0.0f, &alternateRate);
    require(alternateRate.peak < 0.9f && rms(alternateRate) > 0.04,
            "44.1 kHz render remains active and safe");

    juce::AudioBuffer<float> mono(1, blockSize);
    require(engine.prepare(48000.0, 0.5f, 0.0f, true), "mono output engine prepares");
    engine.process(mono, 0.5f, 0.0f, true, 2, 0.0f, 1.0f, 0.0f);
    for (int index = 0; index < blockSize; ++index)
        require(std::isfinite(mono.getSample(0, index)), "mono output stays finite");

    require(engine.prepare(48000.0, 0.65f, 0.0f, true), "level control reference prepares");
    processBlocks(engine, block, 8, 0.65f, 0.0f);
    engine.process(block, 0.65f, 0.0f, true, 2, 0.0f, 1.0f, 0.0f);
    const auto unityRms = [&]
    {
        Stats stats;
        measure(block, stats);
        return rms(stats);
    }();
    require(engine.prepare(48000.0, 0.65f, 0.0f, true, 2, -6.0f, 1.0f, 0.0f),
            "independent output-level setting prepares");
    processBlocks(engine, block, 8, 0.65f, 0.0f, 2, -6.0f);
    engine.process(block, 0.65f, 0.0f, true, 2, -6.0f, 1.0f, 0.0f);
    Stats reducedLevel;
    measure(block, reducedLevel);
    require(std::abs(rms(reducedLevel) / unityRms - 0.5012) < 0.01,
            "LEVEL applies independent decibel gain");

    require(engine.prepare(48000.0, 0.65f, 0.0f, true), "brightness automation prepares");
    processBlocks(engine, block, 8, 0.65f, 0.0f);
    Stats brightnessTransition;
    engine.process(block, 0.65f, 0.0f, true, 2, 0.0f, 0.2f, 0.0f);
    measure(block, brightnessTransition);
    require(brightnessTransition.maxStep < 0.12f, "brightness filter automation is smoothed");

    require(engine.prepare(48000.0, 0.65f, 0.0f, true), "neutral motion baseline prepares");
    juce::AudioBuffer<float> motionBaseline(2, blockSize);
    processBlocks(engine, motionBaseline, 240, 0.65f, 0.0f);
    require(engine.prepare(48000.0, 0.65f, 0.0f, true), "motion comparison prepares");
    juce::AudioBuffer<float> motionBlock(2, blockSize);
    processBlocks(engine, motionBlock, 240, 0.65f, 0.0f, 2, 0.0f, 1.0f, 1.0f);
    float motionDifference = 0.0f;
    for (int channel = 0; channel < 2; ++channel)
        for (int index = 0; index < blockSize; ++index)
            motionDifference = std::max(motionDifference,
                std::abs(motionBlock.getSample(channel, index) - motionBaseline.getSample(channel, index)));
    require(motionDifference > 1.0e-4f, "MOTION adds restrained playback variation");

    require(engine.prepare(48000.0, 0.65f, 0.0f, true), "key change starts in original D");
    processBlocks(engine, block, 8, 0.65f, 0.0f);
    Stats keyTransition;
    for (int index = 0; index < 12; ++index)
    {
        engine.process(block, 0.65f, 0.0f, true, 7, 0.0f, 1.0f, 0.0f);
        measure(block, keyTransition);
    }
    require(keyTransition.maxStep < 0.12f, "key changes crossfade without a large click");

    if (failures == 0)
        std::puts("STILL DSP and key-bank checks passed.");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
