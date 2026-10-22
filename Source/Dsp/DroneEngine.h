#pragma once

#include "Packs/DroneSamplePack.h"
#include <array>
#include <atomic>
#include <memory>
#include <juce_dsp/juce_dsp.h>

namespace still::dsp
{
class DroneEngine
{
public:
    static constexpr int keyCount = 12;
    static constexpr int packSlotCount = 2;
    static constexpr std::size_t maxResidentAudioBytes = 1024u * 1024u * 1024u;

    bool prepare(double hostSampleRate, float initialOpen, float initialAir, bool initiallyRunning,
                 int initialKey = 2, float initialLevelDb = 0.0f, float initialBrightness = 1.0f,
                 float initialMotion = 0.0f);
    void process(juce::AudioBuffer<float>& output, float open, float air, bool running,
                 int key = 2, float levelDb = 0.0f, float brightness = 1.0f,
                 float motion = 0.0f) noexcept;

    bool publishPreparedPack(std::unique_ptr<packs::PreparedDronePack>&) noexcept;
    void reclaimRetiredPacks();
    bool assetsReady() const noexcept { return assetsReady_.load(std::memory_order_acquire); }
    bool packTransitioning() const noexcept { return packTransitioning_.load(std::memory_order_acquire); }
    std::size_t residentAudioBytes() const noexcept { return residentBytes_.load(std::memory_order_relaxed); }

private:
    enum class SlotState : unsigned char { empty, loading, ready, active, transitioning, retired };
    struct PackSlot
    {
        std::unique_ptr<packs::PreparedDronePack> pack;
        std::atomic<SlotState> state{SlotState::empty};
        double positionA = 0.0, positionAir = 0.0;
        int cursorKey = 2, loopFramesA = 0, loopFramesAir = 0;
    };

    static const packs::DroneLayer* findLayer(const packs::PreparedDronePack&, int,
                                               packs::DroneLayerRole) noexcept;
    static const packs::DroneLayer* findMainLayer(const packs::PreparedDronePack&, int) noexcept;
    static int semitoneOffset(int targetPitchClass, int assetPitchClass) noexcept;
    float renderChannel(const packs::PreparedDronePack&, const PackSlot&, int, int,
                        float, float, float) const noexcept;
    void advanceSlot(PackSlot&, double, double, double) noexcept;
    void resetSlotForKey(PackSlot&, int) noexcept;
    void rescaleSlotForKey(PackSlot&, int) noexcept;
    bool startReadyTransition() noexcept;
    void finishTransition() noexcept;
    void prepareSmoothers(double, float, float, bool, float, float, float);

    std::array<PackSlot, packSlotCount> slots_;
    std::array<float, 256> brightnessAlpha_{};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> openSmoother_, airSmoother_,
        startSmoother_, levelGainSmoother_, brightnessSmoother_, motionSmoother_, keySmoother_,
        packSmoother_;
    std::atomic<std::size_t> residentBytes_{0};
    std::atomic<bool> assetsReady_{false}, packTransitioning_{false};
    double hostSampleRate_ = 48000.0;
    double motionPhaseA1_ = 0.0, motionPhaseA2_ = 1.1;
    double motionPhaseAir1_ = 2.3, motionPhaseAir2_ = 0.4;
    int activeSlot_ = -1, transitionSlot_ = -1;
    int previousKey_ = 2, targetKey_ = 2;
    float filterStateLeft_ = 0.0f, filterStateRight_ = 0.0f;
};
}
