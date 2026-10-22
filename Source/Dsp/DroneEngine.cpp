#include "Dsp/DroneEngine.h"
#include "Dsp/DroneMath.h"
#include <StillAssets.h>
#include <cmath>
#include <limits>
#include <memory>
namespace still::dsp
{
bool DroneEngine::decodeStereo(const void* data, int byteCount, juce::AudioBuffer<float>& destination, double& sourceSampleRate)
{
    if (data == nullptr || byteCount <= 0) return false;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    auto stream = std::make_unique<juce::MemoryInputStream>(data, static_cast<std::size_t>(byteCount), false);
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(std::move(stream)));
    if (reader == nullptr || reader->numChannels < 2 || reader->lengthInSamples <= 0
        || reader->lengthInSamples > std::numeric_limits<int>::max()) return false;
    const auto frames = static_cast<int>(reader->lengthInSamples);
    destination.setSize(2, frames, false, true, false);
    if (!reader->read(&destination, 0, frames, 0, true, true))
    {
        destination.setSize(0, 0);
        return false;
    }
    sourceSampleRate = reader->sampleRate;
    return sourceSampleRate > 0.0;
}
bool DroneEngine::prepare(double hostSampleRate, float initialOpen, float initialAir, bool initiallyRunning)
{
    hostSampleRate_ = hostSampleRate > 0.0 ? hostSampleRate : 48000.0;
    positionA_ = positionAir_ = 0.0;
    auto bodyRate = 48000.0, swirlRate = 48000.0, airRate = 48000.0;
    const auto bodyLoaded = decodeStereo(StillAssets::D_A_BODY_STAGE_48k_wav, StillAssets::D_A_BODY_STAGE_48k_wavSize, body_, bodyRate);
    const auto swirlLoaded = decodeStereo(StillAssets::D_A_SWIRL_STAGE_48k_wav, StillAssets::D_A_SWIRL_STAGE_48k_wavSize, swirl_, swirlRate);
    const auto airLoaded = decodeStereo(StillAssets::D_C_AIR_STAGE_48k_wav, StillAssets::D_C_AIR_STAGE_48k_wavSize, air_, airRate);
    assetsReady_ = bodyLoaded && swirlLoaded && airLoaded
        && body_.getNumSamples() == swirl_.getNumSamples()
        && std::abs(bodyRate - swirlRate) < 0.01 && bodyRate > 0.0 && airRate > 0.0;
    if (!assetsReady_)
    {
        body_.setSize(0, 0); swirl_.setSize(0, 0); air_.setSize(0, 0);
        return false;
    }
    sourceSampleRateA_ = bodyRate;
    sourceSampleRateAir_ = airRate;
    openSmoother_.reset(hostSampleRate_, 0.045);
    openSmoother_.setCurrentAndTargetValue(std::clamp(initialOpen, 0.0f, 1.0f));
    airSmoother_.reset(hostSampleRate_, 0.045);
    airSmoother_.setCurrentAndTargetValue(std::clamp(initialAir, 0.0f, 1.0f));
    startSmoother_.reset(hostSampleRate_, 0.080);
    startSmoother_.setCurrentAndTargetValue(0.0f);
    startSmoother_.setTargetValue(initiallyRunning ? 1.0f : 0.0f);
    return true;
}
int DroneEngine::wrapIndex(int index, int sampleCount) noexcept
{
    index %= sampleCount;
    return index < 0 ? index + sampleCount : index;
}
float DroneEngine::interpolateWrapped(const float* data, int frames, double position) noexcept
{
    if (data == nullptr || frames <= 0) return 0.0f;
    const auto index = static_cast<int>(position);
    const auto t = static_cast<float>(position - static_cast<double>(index));
    const auto a = data[wrapIndex(index - 1, frames)];
    const auto b = data[index];
    const auto c = data[wrapIndex(index + 1, frames)];
    const auto d = data[wrapIndex(index + 2, frames)];
    // Catmull–Rom interpolation passes integer-rate source samples through exactly.
    return b + 0.5f * t * (c - a + t * (2.0f * a - 5.0f * b + 4.0f * c - d
        + t * (3.0f * (b - c) + d - a)));
}
void DroneEngine::process(juce::AudioBuffer<float>& output, float open, float air, bool running) noexcept
{
    const auto count = output.getNumSamples();
    const auto channels = output.getNumChannels();
    if (count <= 0 || channels <= 0) return;
    if (!assetsReady_) { output.clear(); return; }
    openSmoother_.setTargetValue(std::clamp(open, 0.0f, 1.0f));
    airSmoother_.setTargetValue(std::clamp(air, 0.0f, 1.0f));
    startSmoother_.setTargetValue(running ? 1.0f : 0.0f);
    const auto framesA = body_.getNumSamples(), framesAir = air_.getNumSamples();
    const auto* bodyL = body_.getReadPointer(0); const auto* bodyR = body_.getReadPointer(1);
    const auto* swirlL = swirl_.getReadPointer(0); const auto* swirlR = swirl_.getReadPointer(1);
    const auto* airL = air_.getReadPointer(0); const auto* airR = air_.getReadPointer(1);
    auto* outL = output.getWritePointer(0);
    auto* outR = channels > 1 ? output.getWritePointer(1) : nullptr;
    for (int i = 0; i < count; ++i)
    {
        const auto gains = openGains(openSmoother_.getNextValue());
        const auto airGain = airSmoother_.getNextValue();
        const auto startGain = startSmoother_.getNextValue();
        const auto left = (interpolateWrapped(bodyL, framesA, positionA_) * gains.body
            + interpolateWrapped(swirlL, framesA, positionA_) * gains.swirl
            + interpolateWrapped(airL, framesAir, positionAir_) * airGain) * startGain;
        const auto right = (interpolateWrapped(bodyR, framesA, positionA_) * gains.body
            + interpolateWrapped(swirlR, framesA, positionA_) * gains.swirl
            + interpolateWrapped(airR, framesAir, positionAir_) * airGain) * startGain;
        if (outR != nullptr) { outL[i] = left; outR[i] = right; }
        else outL[i] = 0.5f * (left + right);
        positionA_ += sourceSampleRateA_ / hostSampleRate_;
        if (positionA_ >= framesA) positionA_ = std::fmod(positionA_, static_cast<double>(framesA));
        positionAir_ += sourceSampleRateAir_ / hostSampleRate_;
        if (positionAir_ >= framesAir) positionAir_ = std::fmod(positionAir_, static_cast<double>(framesAir));
    }
    for (int channel = 2; channel < channels; ++channel) output.clear(channel, 0, count);
}
}
