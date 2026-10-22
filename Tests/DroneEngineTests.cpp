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

bool writeTestWave(const juce::File& file, float level)
{
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    if (stream == nullptr)
        return false;
    const auto options = juce::AudioFormatWriterOptions{}
        .withSampleRate(48000.0)
        .withNumChannels(2)
        .withBitsPerSample(24);
    auto writer = wav.createWriterFor(stream, options);
    if (writer == nullptr)
        return false;
    juce::AudioBuffer<float> samples(2, 512);
    for (int frame = 0; frame < samples.getNumSamples(); ++frame)
    {
        const auto value = level * std::sin(juce::MathConstants<float>::twoPi
                                             * static_cast<float>(frame) / 128.0f);
        samples.setSample(0, frame, value);
        samples.setSample(1, frame, value);
    }
    const auto wrote = writer->writeFromAudioSampleBuffer(samples, 0, samples.getNumSamples());
    writer.reset();
    return wrote;
}

void checkSamplePackLoader()
{
    auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("still-pack-loader-test-" + juce::Uuid().toString());
    auto audio = root.getChildFile("audio");
    audio.createDirectory();
    require(writeTestWave(audio.getChildFile("body.wav"), 0.1f), "temporary BODY WAV writes");
    require(writeTestWave(audio.getChildFile("swirl.wav"), 0.08f), "temporary SWIRL WAV writes");
    auto manifest = root.getChildFile("manifest.json");
    const auto validManifest = juce::String(R"json({
      "schema_version": 1,
      "id": "test.still.pack",
      "version": "1.0.0",
      "display_name": "Loader Test Pack",
      "instrument": "still",
      "source_license": "Synthetic test audio; generated for automated tests.",
      "extensions": {"still": {"original_root_pitch_class": 2}},
      "assets": [
        {"id":"body-d","path":"audio/body.wav","role":"drone_body","pitch_class":2,
         "loop_start_frame":0,"loop_end_frame":512,"loop_xfade_frames":0,
         "extensions":{"still":{"synchronized_group":"d-main"}}},
        {"id":"swirl-d","path":"audio/swirl.wav","role":"drone_swirl","pitch_class":2,
         "loop_start_frame":0,"loop_end_frame":512,"loop_xfade_frames":0,
         "extensions":{"still":{"synchronized_group":"d-main"}}}
      ]
    })json");
    manifest.replaceWithText(validManifest);

    std::unique_ptr<still::packs::PreparedDronePack> loaded;
    juce::String error;
    require(still::packs::DronePackLoader::loadDirectory(root, loaded, error),
            "external sample-pack v1 manifest and WAV assets load");
    require(loaded != nullptr && loaded->layer(2, still::packs::DroneLayerRole::body) != nullptr
                && loaded->layer(2, still::packs::DroneLayerRole::swirl) != nullptr,
            "external pack exposes the synchronized D BODY and SWIRL layers");

    const auto outside = root.getSiblingFile("outside-" + juce::Uuid().toString() + ".wav");
    require(writeTestWave(outside, 0.1f), "temporary outside WAV writes");
    manifest.replaceWithText(validManifest.replace("audio/body.wav", "../" + outside.getFileName()));
    loaded.reset();
    require(!still::packs::DronePackLoader::loadDirectory(root, loaded, error),
            "external sample-pack loader rejects path traversal");
    root.deleteRecursively();
    outside.deleteFile();
}

std::unique_ptr<still::packs::PreparedDronePack> makeTransitionPack(const juce::String& id,
                                                                     float level)
{
    auto pack = std::make_unique<still::packs::PreparedDronePack>();
    pack->id = id;
    pack->version = "1.0.0";
    pack->displayName = id;
    pack->rootPitchClass = 2;
    auto& body = pack->banks[2][static_cast<std::size_t>(still::packs::DroneLayerRole::body)];
    body.audio.setSize(2, 4096);
    for (int frame = 0; frame < body.audio.getNumSamples(); ++frame)
    {
        const auto sample = level * std::sin(juce::MathConstants<float>::twoPi
                                              * static_cast<float>(frame) / 1024.0f);
        body.audio.setSample(0, frame, sample);
        body.audio.setSample(1, frame, sample);
    }
    body.pitchClass = 2;
    body.loopStartFrame = 0;
    body.loopEndFrame = body.audio.getNumSamples();
    body.sampleRate = 48000.0;
    body.valid = true;
    body.assetId = id + ".body";
    return pack;
}
}

int main()
{
    checkSamplePackLoader();
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

    still::dsp::DroneEngine packEngine;
    require(packEngine.prepare(48000.0, 0.65f, 0.0f, true), "pack transition engine prepares");
    auto firstPack = makeTransitionPack("test.pack.one", 0.12f);
    require(packEngine.publishPreparedPack(firstPack) && firstPack == nullptr,
            "first prepared pack publishes without audio-thread decoding");
    Stats firstPackTransition;
    processBlocks(packEngine, block, 14, 0.65f, 0.0f, 2, 0.0f, 1.0f, 0.0f,
                  &firstPackTransition);
    require(!packEngine.packTransitioning(), "first sample pack crossfade completes");
    packEngine.reclaimRetiredPacks();
    auto secondPack = makeTransitionPack("test.pack.two", 0.08f);
    require(packEngine.publishPreparedPack(secondPack) && secondPack == nullptr,
            "second prepared pack publishes after worker-side retirement");
    Stats secondPackTransition;
    processBlocks(packEngine, block, 14, 0.65f, 0.0f, 2, 0.0f, 1.0f, 0.0f,
                  &secondPackTransition);
    require(!packEngine.packTransitioning(), "back-to-back sample pack crossfade completes");
    require(secondPackTransition.peak < 1.0f,
            "back-to-back pack changes remain finite and below full scale");
    packEngine.reclaimRetiredPacks();

    if (failures == 0)
        std::puts("STILL DSP and key-bank checks passed.");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
