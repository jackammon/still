#include "Dsp/DroneEngine.h"
#include "Dsp/DroneMath.h"
#include <StillAssets.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

namespace still::dsp
{
namespace
{
constexpr int dKeyIndex = 2;
constexpr double twoPi = juce::MathConstants<double>::twoPi;
constexpr double motionRateA1 = 0.031;
constexpr double motionRateA2 = 0.019;
constexpr double motionRateAir1 = 0.027;
constexpr double motionRateAir2 = 0.014;
constexpr float motionDepthA = 0.00045f;
constexpr float motionDepthAir = 0.00065f;
constexpr std::array<const char*, DroneEngine::keyCount> keyResources{
    "C", "Cs", "D", "Ds", "E", "F", "Fs", "G", "Gs", "A", "As", "B"};

bool sameAudioShape(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    return a.getNumChannels() == 2 && b.getNumChannels() == 2
        && a.getNumSamples() == b.getNumSamples();
}

}

bool DroneEngine::decodeStereo(const void* data, int byteCount,
                               juce::AudioBuffer<float>& destination, double& sourceSampleRate)
{
    if (data == nullptr || byteCount <= 0)
        return false;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    auto stream = std::make_unique<juce::MemoryInputStream>(data, static_cast<std::size_t>(byteCount), false);
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(std::move(stream)));
    if (reader == nullptr || reader->numChannels < 2 || reader->lengthInSamples <= 0
        || reader->lengthInSamples > std::numeric_limits<int>::max())
        return false;

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

bool DroneEngine::loadBank(int key, SampleBank& bank)
{
    double bodyRate = 48000.0, swirlRate = 48000.0, airRate = 48000.0;
    bool loaded = false;
    if (key == dKeyIndex)
    {
        loaded = decodeStereo(StillAssets::D_A_BODY_STAGE_48k_wav,
                              StillAssets::D_A_BODY_STAGE_48k_wavSize, bank.body, bodyRate)
            && decodeStereo(StillAssets::D_A_SWIRL_STAGE_48k_wav,
                            StillAssets::D_A_SWIRL_STAGE_48k_wavSize, bank.swirl, swirlRate)
            && decodeStereo(StillAssets::D_C_AIR_STAGE_48k_wav,
                            StillAssets::D_C_AIR_STAGE_48k_wavSize, bank.air, airRate);
    }
    else
    {
        const auto keyName = keyResources[static_cast<std::size_t>(key)];
        auto bodyName = juce::String(keyName) + "_BODY_flac";
        auto swirlName = juce::String(keyName) + "_SWIRL_flac";
        auto airName = juce::String(keyName) + "_AIR_flac";
        int bodySize = 0, swirlSize = 0, airSize = 0;
        const auto* bodyData = StillAssets::getNamedResource(bodyName.toRawUTF8(), bodySize);
        const auto* swirlData = StillAssets::getNamedResource(swirlName.toRawUTF8(), swirlSize);
        const auto* airData = StillAssets::getNamedResource(airName.toRawUTF8(), airSize);
        loaded = decodeStereo(bodyData, bodySize, bank.body, bodyRate)
            && decodeStereo(swirlData, swirlSize, bank.swirl, swirlRate)
            && decodeStereo(airData, airSize, bank.air, airRate);
    }

    if (!loaded || !sameAudioShape(bank.body, bank.swirl)
        || std::abs(bodyRate - swirlRate) > 0.01 || bodyRate <= 0.0 || airRate <= 0.0)
        return false;

    bank.sourceSampleRateA = bodyRate;
    bank.sourceSampleRateAir = airRate;
    return true;
}

int DroneEngine::wrapIndex(int index, int sampleCount) noexcept
{
    index %= sampleCount;
    return index < 0 ? index + sampleCount : index;
}

float DroneEngine::interpolateWrapped(const float* data, int frames, double position) noexcept
{
    if (data == nullptr || frames <= 0)
        return 0.0f;

    const auto index = static_cast<int>(position);
    const auto t = static_cast<float>(position - static_cast<double>(index));
    const auto a = data[wrapIndex(index - 1, frames)];
    const auto b = data[wrapIndex(index, frames)];
    const auto c = data[wrapIndex(index + 1, frames)];
    const auto d = data[wrapIndex(index + 2, frames)];
    // Catmull–Rom interpolation passes integer-rate source samples through exactly.
    return b + 0.5f * t * (c - a + t * (2.0f * a - 5.0f * b + 4.0f * c - d
        + t * (3.0f * (b - c) + d - a)));
}

float DroneEngine::readLoop(const float* data, int frames, double position, bool smoothLoop) const noexcept
{
    if (!smoothLoop || frames <= loopCrossfadeFrames_)
        return interpolateWrapped(data, frames, position);

    const auto fadeStart = static_cast<double>(frames - loopCrossfadeFrames_);
    if (position < fadeStart)
        return interpolateWrapped(data, frames, position);

    const auto fadePosition = (position - fadeStart) / static_cast<double>(loopCrossfadeFrames_);
    const auto amount = static_cast<float>(std::clamp(fadePosition, 0.0, 1.0));
    const auto tail = interpolateWrapped(data, frames, position);
    const auto head = interpolateWrapped(data, frames, position - fadeStart);
    return tail * (1.0f - amount) + head * amount;
}

bool DroneEngine::prepare(double hostSampleRate, float initialOpen, float initialAir,
                          bool initiallyRunning, int initialKey, float initialLevelDb,
                          float initialBrightness, float initialMotion)
{
    hostSampleRate_ = hostSampleRate > 0.0 ? hostSampleRate : 48000.0;
    positionA_ = positionAir_ = 0.0;
    filterStateLeft_ = filterStateRight_ = 0.0f;
    motionPhaseA1_ = 0.0;
    motionPhaseA2_ = 1.1;
    motionPhaseAir1_ = 2.3;
    motionPhaseAir2_ = 0.4;
    loopCrossfadeFrames_ = 240;

    if (!banksLoaded_)
    {
        assetsReady_ = true;
        for (int key = 0; key < keyCount; ++key)
            assetsReady_ = loadBank(key, banks_[static_cast<std::size_t>(key)]) && assetsReady_;
        banksLoaded_ = assetsReady_;
    }

    if (!assetsReady_)
        return false;

    const auto& reference = banks_[dKeyIndex];
    for (const auto& bank : banks_)
        if (!sameAudioShape(reference.body, bank.body) || !sameAudioShape(reference.swirl, bank.swirl)
            || !sameAudioShape(reference.air, bank.air)
            || !sameAudioShape(bank.body, bank.swirl))
            assetsReady_ = false;
    if (!assetsReady_)
        return false;

    const auto sampleCount = reference.body.getNumSamples();
    loopCrossfadeFrames_ = std::min(loopCrossfadeFrames_, std::max(0, sampleCount / 64));

    const auto safeRate = std::max(1.0, hostSampleRate_);
    for (std::size_t i = 0; i < brightnessAlpha_.size(); ++i)
    {
        const auto normalized = static_cast<double>(i) / static_cast<double>(brightnessAlpha_.size() - 1);
        const auto cutoff = 600.0 * std::pow(30.0, normalized);
        brightnessAlpha_[i] = static_cast<float>(1.0 - std::exp(-twoPi * cutoff / safeRate));
    }
    brightnessAlpha_.back() = 1.0f;

    const auto clampedKey = std::clamp(initialKey, 0, keyCount - 1);
    previousKey_ = targetKey_ = clampedKey;
    if (clampedKey != dKeyIndex)
        positionA_ = positionAir_ = static_cast<double>(loopCrossfadeFrames_);
    openSmoother_.reset(safeRate, 0.045);
    openSmoother_.setCurrentAndTargetValue(std::clamp(initialOpen, 0.0f, 1.0f));
    airSmoother_.reset(safeRate, 0.045);
    airSmoother_.setCurrentAndTargetValue(std::clamp(initialAir, 0.0f, 1.0f));
    startSmoother_.reset(safeRate, 0.080);
    startSmoother_.setCurrentAndTargetValue(0.0f);
    startSmoother_.setTargetValue(initiallyRunning ? 1.0f : 0.0f);
    levelGainSmoother_.reset(safeRate, 0.045);
    levelGainSmoother_.setCurrentAndTargetValue(
        juce::Decibels::decibelsToGain(std::clamp(initialLevelDb, -36.0f, 6.0f)));
    brightnessSmoother_.reset(safeRate, 0.080);
    brightnessSmoother_.setCurrentAndTargetValue(std::clamp(initialBrightness, 0.0f, 1.0f));
    motionSmoother_.reset(safeRate, 0.250);
    motionSmoother_.setCurrentAndTargetValue(std::clamp(initialMotion, 0.0f, 1.0f));
    keySmoother_.reset(safeRate, 0.100);
    keySmoother_.setCurrentAndTargetValue(1.0f);
    keySmoother_.setTargetValue(1.0f);
    return true;
}

float DroneEngine::renderChannel(const SampleBank& bank, int channel, double positionA,
                                double positionAir, float bodyGain, float swirlGain,
                                float airGain, int key) const noexcept
{
    const auto framesA = bank.body.getNumSamples();
    const auto framesAir = bank.air.getNumSamples();
    const auto smoothLoop = key != dKeyIndex;
    return readLoop(bank.body.getReadPointer(channel), framesA, positionA, smoothLoop) * bodyGain
        + readLoop(bank.swirl.getReadPointer(channel), framesA, positionA, smoothLoop) * swirlGain
        + readLoop(bank.air.getReadPointer(channel), framesAir, positionAir, smoothLoop) * airGain;
}

void DroneEngine::process(juce::AudioBuffer<float>& output, float open, float air, bool running,
                          int key, float levelDb, float brightness, float motion) noexcept
{
    const auto count = output.getNumSamples();
    const auto channels = output.getNumChannels();
    if (count <= 0 || channels <= 0)
        return;
    if (!assetsReady_)
    {
        output.clear();
        return;
    }

    const auto requestedKey = std::clamp(key, 0, keyCount - 1);
    if (requestedKey != targetKey_)
    {
        previousKey_ = targetKey_;
        targetKey_ = requestedKey;
        keySmoother_.setCurrentAndTargetValue(0.0f);
        keySmoother_.setTargetValue(1.0f);
    }

    openSmoother_.setTargetValue(std::clamp(open, 0.0f, 1.0f));
    airSmoother_.setTargetValue(std::clamp(air, 0.0f, 1.0f));
    startSmoother_.setTargetValue(running ? 1.0f : 0.0f);
    levelGainSmoother_.setTargetValue(
        juce::Decibels::decibelsToGain(std::clamp(levelDb, -36.0f, 6.0f)));
    brightnessSmoother_.setTargetValue(std::clamp(brightness, 0.0f, 1.0f));
    motionSmoother_.setTargetValue(std::clamp(motion, 0.0f, 1.0f));

    auto* outLeft = output.getWritePointer(0);
    auto* outRight = channels > 1 ? output.getWritePointer(1) : nullptr;
    for (int i = 0; i < count; ++i)
    {
        const auto gains = openGains(openSmoother_.getNextValue());
        const auto airGain = airSmoother_.getNextValue();
        const auto startGain = startSmoother_.getNextValue();
        const auto levelGain = levelGainSmoother_.getNextValue();
        const auto brightnessValue = brightnessSmoother_.getNextValue();
        const auto motionValue = motionSmoother_.getNextValue();
        const auto keyFade = keySmoother_.getNextValue();
        const auto& oldBank = banks_[static_cast<std::size_t>(previousKey_)];
        const auto& newBank = banks_[static_cast<std::size_t>(targetKey_)];

        const auto render = [&](int channel)
        {
            const auto newValue = renderChannel(newBank, channel, positionA_, positionAir_,
                                                gains.body, gains.swirl, airGain, targetKey_);
            const auto mixed = previousKey_ == targetKey_ ? newValue
                : renderChannel(oldBank, channel, positionA_, positionAir_, gains.body, gains.swirl,
                                airGain, previousKey_) * (1.0f - keyFade) + newValue * keyFade;
            return mixed * startGain * levelGain;
        };

        auto left = render(0);
        auto right = render(1);
        if (outRight == nullptr)
            left = 0.5f * (left + right);

        float alpha = 1.0f;
        if (brightnessValue < 0.99999f)
        {
            const auto tablePosition = brightnessValue * static_cast<float>(brightnessAlpha_.size() - 1);
            const auto tableIndex = static_cast<std::size_t>(tablePosition);
            const auto fraction = tablePosition - static_cast<float>(tableIndex);
            const auto next = std::min(tableIndex + 1, brightnessAlpha_.size() - 1);
            alpha = brightnessAlpha_[tableIndex]
                + fraction * (brightnessAlpha_[next] - brightnessAlpha_[tableIndex]);
        }

        filterStateLeft_ += alpha * (left - filterStateLeft_);
        outLeft[i] = filterStateLeft_;
        if (outRight != nullptr)
        {
            filterStateRight_ += alpha * (right - filterStateRight_);
            outRight[i] = filterStateRight_;
        }

        auto shiftedBlend = 0.0f;
        if (previousKey_ != dKeyIndex)
            shiftedBlend += 1.0f - keyFade;
        if (targetKey_ != dKeyIndex)
            shiftedBlend += keyFade;

        const auto framesA = newBank.body.getNumSamples();
        const auto framesAir = newBank.air.getNumSamples();
        const auto offsetA = static_cast<double>(loopCrossfadeFrames_) * shiftedBlend;
        const auto offsetAir = static_cast<double>(loopCrossfadeFrames_) * shiftedBlend;
        const auto periodA = std::max(1.0, static_cast<double>(framesA) - offsetA);
        const auto periodAir = std::max(1.0, static_cast<double>(framesAir) - offsetAir);

        auto modA = 1.0;
        auto modAir = 1.0;
        if (motionValue > 0.0f)
        {
            modA += motionValue * (motionDepthA * std::sin(motionPhaseA1_)
                                   + 0.35f * motionDepthA * std::sin(motionPhaseA2_));
            modAir += motionValue * (motionDepthAir * std::sin(motionPhaseAir1_)
                                     + 0.40f * motionDepthAir * std::sin(motionPhaseAir2_));
        }

        positionA_ += newBank.sourceSampleRateA / hostSampleRate_
            * (periodA / static_cast<double>(framesA)) * modA;
        positionAir_ += newBank.sourceSampleRateAir / hostSampleRate_
            * (periodAir / static_cast<double>(framesAir)) * modAir;
        if (positionA_ >= framesA)
            positionA_ = offsetA + std::fmod(positionA_ - framesA, periodA);
        if (positionAir_ >= framesAir)
            positionAir_ = offsetAir + std::fmod(positionAir_ - framesAir, periodAir);

        if (motionValue > 0.0f)
        {
            motionPhaseA1_ += twoPi * motionRateA1 / hostSampleRate_;
            motionPhaseA2_ += twoPi * motionRateA2 / hostSampleRate_;
            motionPhaseAir1_ += twoPi * motionRateAir1 / hostSampleRate_;
            motionPhaseAir2_ += twoPi * motionRateAir2 / hostSampleRate_;
            if (motionPhaseA1_ >= twoPi) motionPhaseA1_ -= twoPi;
            if (motionPhaseA2_ >= twoPi) motionPhaseA2_ -= twoPi;
            if (motionPhaseAir1_ >= twoPi) motionPhaseAir1_ -= twoPi;
            if (motionPhaseAir2_ >= twoPi) motionPhaseAir2_ -= twoPi;
        }

        if (keyFade >= 1.0f && previousKey_ != targetKey_)
            previousKey_ = targetKey_;
    }

    for (int channel = 2; channel < channels; ++channel)
        output.clear(channel, 0, count);
}

std::size_t DroneEngine::residentAudioBytes() const noexcept
{
    std::size_t bytes = 0;
    for (const auto& bank : banks_)
        for (const auto* buffer : {&bank.body, &bank.swirl, &bank.air})
            bytes += static_cast<std::size_t>(buffer->getNumChannels())
                * static_cast<std::size_t>(buffer->getNumSamples()) * sizeof(float);
    return bytes;
}
}
