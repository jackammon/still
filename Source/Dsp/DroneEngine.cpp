#include "Dsp/DroneEngine.h"
#include "Dsp/DroneMath.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace still::dsp
{
namespace
{
constexpr double twoPi = juce::MathConstants<double>::twoPi;
constexpr double motionRateA1 = 0.031;
constexpr double motionRateA2 = 0.019;
constexpr double motionRateAir1 = 0.027;
constexpr double motionRateAir2 = 0.014;
constexpr float motionDepthA = 0.00045f;
constexpr float motionDepthAir = 0.00065f;

}

bool DroneEngine::prepare(double hostSampleRate, float initialOpen, float initialAir,
                          bool initiallyRunning, int initialKey, float initialLevelDb,
                          float initialBrightness, float initialMotion)
{
    hostSampleRate_ = hostSampleRate > 0.0 ? hostSampleRate : 48000.0;
    filterStateLeft_ = filterStateRight_ = 0.0f;
    motionPhaseA1_ = 0.0;
    motionPhaseA2_ = 1.1;
    motionPhaseAir1_ = 2.3;
    motionPhaseAir2_ = 0.4;

    if (activeSlot_ < 0)
    {
        std::unique_ptr<packs::PreparedDronePack> factory;
        juce::String error;
        if (!packs::DronePackLoader::loadFactory(factory, error) || factory == nullptr)
        {
            assetsReady_.store(false, std::memory_order_release);
            return false;
        }
        auto& slot = slots_[0];
        slot.pack = std::move(factory);
        residentBytes_.store(slot.pack->residentAudioBytes(), std::memory_order_relaxed);
        slot.state.store(SlotState::active, std::memory_order_release);
        activeSlot_ = 0;
    }
    if (slots_[static_cast<std::size_t>(activeSlot_)].pack == nullptr)
    {
        assetsReady_.store(false, std::memory_order_release);
        return false;
    }

    const auto clampedKey = std::clamp(initialKey, 0, keyCount - 1);
    previousKey_ = targetKey_ = clampedKey;
    resetSlotForKey(slots_[static_cast<std::size_t>(activeSlot_)], clampedKey);
    if (transitionSlot_ >= 0)
        resetSlotForKey(slots_[static_cast<std::size_t>(transitionSlot_)], clampedKey);

    const auto safeRate = std::max(1.0, hostSampleRate_);
    for (std::size_t i = 0; i < brightnessAlpha_.size(); ++i)
    {
        const auto normalized = static_cast<double>(i) / static_cast<double>(brightnessAlpha_.size() - 1);
        const auto cutoff = 600.0 * std::pow(30.0, normalized);
        brightnessAlpha_[i] = static_cast<float>(1.0 - std::exp(-twoPi * cutoff / safeRate));
    }
    brightnessAlpha_.back() = 1.0f;
    prepareSmoothers(safeRate, initialOpen, initialAir, initiallyRunning,
                     initialLevelDb, initialBrightness, initialMotion);
    assetsReady_.store(true, std::memory_order_release);
    return true;
}

void DroneEngine::prepareSmoothers(double sampleRate, float initialOpen, float initialAir,
                                   bool initiallyRunning, float initialLevelDb,
                                   float initialBrightness, float initialMotion)
{
    openSmoother_.reset(sampleRate, 0.045);
    openSmoother_.setCurrentAndTargetValue(std::clamp(initialOpen, 0.0f, 1.0f));
    airSmoother_.reset(sampleRate, 0.045);
    airSmoother_.setCurrentAndTargetValue(std::clamp(initialAir, 0.0f, 1.0f));
    startSmoother_.reset(sampleRate, 0.080);
    startSmoother_.setCurrentAndTargetValue(0.0f);
    startSmoother_.setTargetValue(initiallyRunning ? 1.0f : 0.0f);
    levelGainSmoother_.reset(sampleRate, 0.045);
    levelGainSmoother_.setCurrentAndTargetValue(
        juce::Decibels::decibelsToGain(std::clamp(initialLevelDb, -36.0f, 6.0f)));
    brightnessSmoother_.reset(sampleRate, 0.080);
    brightnessSmoother_.setCurrentAndTargetValue(std::clamp(initialBrightness, 0.0f, 1.0f));
    motionSmoother_.reset(sampleRate, 0.250);
    motionSmoother_.setCurrentAndTargetValue(std::clamp(initialMotion, 0.0f, 1.0f));
    keySmoother_.reset(sampleRate, 0.100);
    keySmoother_.setCurrentAndTargetValue(1.0f);
    packSmoother_.reset(sampleRate, 0.100);
    if (transitionSlot_ >= 0)
    {
        packSmoother_.setCurrentAndTargetValue(0.0f);
        packSmoother_.setTargetValue(1.0f);
        packTransitioning_.store(true, std::memory_order_release);
    }
    else
    {
        packSmoother_.setCurrentAndTargetValue(1.0f);
        packTransitioning_.store(false, std::memory_order_release);
    }
}

const packs::DroneLayer* DroneEngine::findLayer(const packs::PreparedDronePack& pack, int key,
                                                 packs::DroneLayerRole role) noexcept
{
    if (const auto* exact = pack.layer(key, role))
        return exact;
    if (const auto* root = pack.layer(pack.rootPitchClass, role))
        return root;
    for (int pitch = 0; pitch < keyCount; ++pitch)
        if (const auto* available = pack.layer(pitch, role))
            return available;
    return nullptr;
}

const packs::DroneLayer* DroneEngine::findMainLayer(const packs::PreparedDronePack& pack,
                                                     int key) noexcept
{
    if (const auto* body = findLayer(pack, key, packs::DroneLayerRole::body))
        return body;
    return findLayer(pack, key, packs::DroneLayerRole::swirl);
}

int DroneEngine::semitoneOffset(int targetPitchClass, int assetPitchClass) noexcept
{
    return (targetPitchClass - assetPitchClass + 18) % 12 - 6;
}

float DroneEngine::renderChannel(const packs::PreparedDronePack& pack, const PackSlot& slot,
                                 int channel, int key, float bodyGain, float swirlGain,
                                 float airGain) const noexcept
{
    const auto channelIndex = channel == 0 ? 0 : 1;
    const auto relativeFor = [](const packs::DroneLayer* layer, double cursor, int cursorFrames)
    {
        if (layer == nullptr || cursorFrames <= 0)
            return 0.0;
        const auto loopFrames = layer->loopEndFrame - layer->loopStartFrame;
        return cursor / static_cast<double>(cursorFrames) * static_cast<double>(loopFrames);
    };
    const auto* body = findLayer(pack, key, packs::DroneLayerRole::body);
    const auto* swirl = findLayer(pack, key, packs::DroneLayerRole::swirl);
    const auto* air = findLayer(pack, key, packs::DroneLayerRole::air);

    auto readChannel = [&](const packs::DroneLayer* layer, double cursor, int cursorFrames)
    {
        if (layer == nullptr)
            return 0.0f;
        const auto position = relativeFor(layer, cursor, cursorFrames);
        const auto start = layer->loopStartFrame;
        const auto length = layer->loopEndFrame - start;
        auto wrapped = std::fmod(position, static_cast<double>(length));
        if (wrapped < 0.0)
            wrapped += length;
        const auto loopIndex = [start, length](int index)
        {
            index %= length;
            if (index < 0) index += length;
            return start + index;
        };
        const auto* data = layer->audio.getReadPointer(channelIndex);
        const auto cubic = [&](double relative) noexcept
        {
            auto pos = std::fmod(relative, static_cast<double>(length));
            if (pos < 0.0) pos += length;
            const auto idx = static_cast<int>(pos);
            const auto frac = static_cast<float>(pos - idx);
            const auto x0 = data[loopIndex(idx - 1)];
            const auto x1 = data[loopIndex(idx)];
            const auto x2 = data[loopIndex(idx + 1)];
            const auto x3 = data[loopIndex(idx + 2)];
            return x1 + 0.5f * frac * (x2 - x0 + frac * (2.0f * x0 - 5.0f * x1
                + 4.0f * x2 - x3 + frac * (3.0f * (x1 - x2) + x3 - x0)));
        };
        const auto fadeStart = static_cast<double>(length - layer->loopXfadeFrames);
        float value = 0.0f;
        if (layer->loopXfadeFrames > 0 && position >= fadeStart)
        {
            const auto amount = static_cast<float>(std::clamp(
                (position - fadeStart) / static_cast<double>(layer->loopXfadeFrames), 0.0, 1.0));
            value = cubic(position) * (1.0f - amount) + cubic(position - fadeStart) * amount;
        }
        else
        {
            value = cubic(position);
        }
        return value * layer->gain;
    };

    const auto bodyValue = readChannel(body, slot.positionA, slot.loopFramesA) * bodyGain;
    const auto swirlValue = readChannel(swirl, slot.positionA, slot.loopFramesA) * swirlGain;
    const auto airValue = readChannel(air, slot.positionAir, slot.loopFramesAir) * airGain;
    return bodyValue + swirlValue + airValue;
}

void DroneEngine::resetSlotForKey(PackSlot& slot, int key) noexcept
{
    if (slot.pack == nullptr)
        return;
    slot.cursorKey = key;
    const auto* main = findMainLayer(*slot.pack, key);
    const auto* air = findLayer(*slot.pack, key, packs::DroneLayerRole::air);
    slot.loopFramesA = main != nullptr ? main->loopEndFrame - main->loopStartFrame : 0;
    slot.loopFramesAir = air != nullptr ? air->loopEndFrame - air->loopStartFrame : 0;
    slot.positionA = main != nullptr ? static_cast<double>(main->loopXfadeFrames) : 0.0;
    slot.positionAir = air != nullptr ? static_cast<double>(air->loopXfadeFrames) : 0.0;
}

void DroneEngine::rescaleSlotForKey(PackSlot& slot, int newKey) noexcept
{
    if (slot.pack == nullptr || newKey == slot.cursorKey)
        return;
    const auto newMain = findMainLayer(*slot.pack, newKey);
    const auto newAir = findLayer(*slot.pack, newKey, packs::DroneLayerRole::air);
    const auto newMainFrames = newMain != nullptr ? newMain->loopEndFrame - newMain->loopStartFrame : 0;
    const auto newAirFrames = newAir != nullptr ? newAir->loopEndFrame - newAir->loopStartFrame : 0;
    if (slot.loopFramesA > 0 && newMainFrames > 0)
        slot.positionA = std::fmod(slot.positionA, static_cast<double>(slot.loopFramesA))
            / slot.loopFramesA * newMainFrames;
    else
        slot.positionA = 0.0;
    if (slot.loopFramesAir > 0 && newAirFrames > 0)
        slot.positionAir = std::fmod(slot.positionAir, static_cast<double>(slot.loopFramesAir))
            / slot.loopFramesAir * newAirFrames;
    else
        slot.positionAir = 0.0;
    slot.loopFramesA = newMainFrames;
    slot.loopFramesAir = newAirFrames;
    slot.cursorKey = newKey;
}

void DroneEngine::advanceSlot(PackSlot& slot, double hostRate, double modA, double modAir) noexcept
{
    if (slot.pack == nullptr)
        return;
    const auto advance = [hostRate](double& position, int cursorFrames,
                                    const packs::DroneLayer* layer, int targetKey, double modulation)
    {
        if (layer == nullptr || cursorFrames <= 0)
            return;
        const auto length = layer->loopEndFrame - layer->loopStartFrame;
        if (length <= 0)
            return;
        const auto period = std::max(1.0, static_cast<double>(length - layer->loopXfadeFrames));
        const auto semitones = semitoneOffset(targetKey, layer->pitchClass);
        const auto pitchRatio = std::pow(2.0, static_cast<double>(semitones) / 12.0);
        const auto step = layer->sampleRate / hostRate * pitchRatio * (period / length) * modulation;
        position += step;
        if (position >= length)
            position = layer->loopXfadeFrames
                + std::fmod(position - length, period);
    };

    const auto* main = findMainLayer(*slot.pack, slot.cursorKey);
    const auto* air = findLayer(*slot.pack, slot.cursorKey, packs::DroneLayerRole::air);
    advance(slot.positionA, slot.loopFramesA, main, slot.cursorKey, modA);
    advance(slot.positionAir, slot.loopFramesAir, air, slot.cursorKey, modAir);
}

bool DroneEngine::startReadyTransition() noexcept
{
    if (transitionSlot_ >= 0 || activeSlot_ < 0)
        return false;
    for (int i = 0; i < packSlotCount; ++i)
    {
        if (i == activeSlot_)
            continue;
        auto expected = SlotState::ready;
        if (slots_[static_cast<std::size_t>(i)].state.compare_exchange_strong(
                expected, SlotState::transitioning, std::memory_order_acq_rel))
        {
            transitionSlot_ = i;
            resetSlotForKey(slots_[static_cast<std::size_t>(i)], targetKey_);
            packSmoother_.setCurrentAndTargetValue(0.0f);
            packSmoother_.setTargetValue(1.0f);
            packTransitioning_.store(true, std::memory_order_release);
            return true;
        }
    }
    return false;
}

void DroneEngine::finishTransition() noexcept
{
    if (transitionSlot_ < 0 || activeSlot_ < 0)
        return;
    const auto oldSlot = activeSlot_;
    const auto newSlot = transitionSlot_;
    slots_[static_cast<std::size_t>(oldSlot)].state.store(SlotState::retired, std::memory_order_release);
    slots_[static_cast<std::size_t>(newSlot)].state.store(SlotState::active, std::memory_order_release);
    activeSlot_ = newSlot;
    transitionSlot_ = -1;
    packTransitioning_.store(false, std::memory_order_release);
}

bool DroneEngine::publishPreparedPack(std::unique_ptr<packs::PreparedDronePack>& pack) noexcept
{
    if (pack == nullptr)
        return false;
    for (auto& slot : slots_)
    {
        auto expected = SlotState::empty;
        if (!slot.state.compare_exchange_strong(expected, SlotState::loading,
                                                 std::memory_order_acq_rel))
            continue;
        const auto bytes = pack->residentAudioBytes();
        slot.pack = std::move(pack);
        residentBytes_.fetch_add(bytes, std::memory_order_relaxed);
        slot.state.store(SlotState::ready, std::memory_order_release);
        return true;
    }
    return false;
}

void DroneEngine::reclaimRetiredPacks()
{
    for (auto& slot : slots_)
    {
        auto expected = SlotState::retired;
        if (!slot.state.compare_exchange_strong(expected, SlotState::loading,
                                                 std::memory_order_acq_rel))
            continue;
        const auto bytes = slot.pack != nullptr ? slot.pack->residentAudioBytes() : 0;
        slot.pack.reset();
        residentBytes_.fetch_sub(bytes, std::memory_order_relaxed);
        slot.state.store(SlotState::empty, std::memory_order_release);
    }
}

void DroneEngine::process(juce::AudioBuffer<float>& output, float open, float air, bool running,
                          int key, float levelDb, float brightness, float motion) noexcept
{
    const auto count = output.getNumSamples();
    const auto channels = output.getNumChannels();
    if (count <= 0 || channels <= 0)
        return;
    if (!assetsReady() || activeSlot_ < 0
        || slots_[static_cast<std::size_t>(activeSlot_)].pack == nullptr)
    {
        output.clear();
        return;
    }

    startReadyTransition();
    const auto requestedKey = std::clamp(key, 0, keyCount - 1);
    if (requestedKey != targetKey_)
    {
        previousKey_ = targetKey_;
        targetKey_ = requestedKey;
        rescaleSlotForKey(slots_[static_cast<std::size_t>(activeSlot_)], targetKey_);
        if (transitionSlot_ >= 0)
            rescaleSlotForKey(slots_[static_cast<std::size_t>(transitionSlot_)], targetKey_);
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
        const auto packFade = transitionSlot_ >= 0 ? packSmoother_.getNextValue() : 1.0f;

        const auto render = [&](int channel)
        {
            const auto& active = slots_[static_cast<std::size_t>(activeSlot_)];
            const auto& activePack = *active.pack;
            const auto newKeyValue = renderChannel(activePack, active, channel, targetKey_,
                                                   gains.body, gains.swirl, airGain);
            const auto activeValue = previousKey_ == targetKey_ ? newKeyValue
                : renderChannel(activePack, active, channel, previousKey_, gains.body, gains.swirl,
                                airGain) * (1.0f - keyFade) + newKeyValue * keyFade;
            if (transitionSlot_ < 0)
                return activeValue * startGain * levelGain;

            const auto& target = slots_[static_cast<std::size_t>(transitionSlot_)];
            const auto& targetPack = *target.pack;
            const auto targetNewKey = renderChannel(targetPack, target, channel, targetKey_,
                                                    gains.body, gains.swirl, airGain);
            const auto targetValue = previousKey_ == targetKey_ ? targetNewKey
                : renderChannel(targetPack, target, channel, previousKey_, gains.body, gains.swirl,
                                airGain) * (1.0f - keyFade) + targetNewKey * keyFade;
            const auto mixed = activeValue * (1.0f - packFade) + targetValue * packFade;
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

        auto modA = 1.0;
        auto modAir = 1.0;
        if (motionValue > 0.0f)
        {
            modA += motionValue * (motionDepthA * std::sin(motionPhaseA1_)
                                   + 0.35f * motionDepthA * std::sin(motionPhaseA2_));
            modAir += motionValue * (motionDepthAir * std::sin(motionPhaseAir1_)
                                     + 0.40f * motionDepthAir * std::sin(motionPhaseAir2_));
        }
        advanceSlot(slots_[static_cast<std::size_t>(activeSlot_)], hostSampleRate_, modA, modAir);
        if (transitionSlot_ >= 0)
            advanceSlot(slots_[static_cast<std::size_t>(transitionSlot_)], hostSampleRate_, modA, modAir);

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
        if (transitionSlot_ >= 0 && packFade >= 1.0f)
            finishTransition();
    }
    for (int channel = 2; channel < channels; ++channel)
        output.clear(channel, 0, count);
}
}
