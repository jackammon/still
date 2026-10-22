#pragma once

#include <array>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

namespace still::dsp
{
class DroneEngine
{
public:
    static constexpr int keyCount = 12;

    bool prepare(double hostSampleRate, float initialOpen, float initialAir, bool initiallyRunning,
                 int initialKey = 2, float initialLevelDb = 0.0f, float initialBrightness = 1.0f,
                 float initialMotion = 0.0f);
    void process(juce::AudioBuffer<float>& output, float open, float air, bool running,
                 int key = 2, float levelDb = 0.0f, float brightness = 1.0f,
                 float motion = 0.0f) noexcept;

    bool assetsReady() const noexcept { return assetsReady_; }
    std::size_t residentAudioBytes() const noexcept;

private:
    struct SampleBank
    {
        juce::AudioBuffer<float> body;
        juce::AudioBuffer<float> swirl;
        juce::AudioBuffer<float> air;
        double sourceSampleRateA = 48000.0;
        double sourceSampleRateAir = 48000.0;
    };

    static bool decodeStereo(const void* data, int byteCount, juce::AudioBuffer<float>& destination,
                             double& sourceSampleRate);
    static int wrapIndex(int index, int sampleCount) noexcept;
    static float interpolateWrapped(const float* data, int frames, double position) noexcept;
    float readLoop(const float* data, int frames, double position, bool smoothLoop) const noexcept;
    bool loadBank(int key, SampleBank& bank);
    float renderChannel(const SampleBank& bank, int channel, double positionA, double positionAir,
                        float bodyGain, float swirlGain, float airGain, int key) const noexcept;

    std::array<SampleBank, keyCount> banks_;
    std::array<float, 256> brightnessAlpha_{};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> openSmoother_, airSmoother_,
        startSmoother_, levelGainSmoother_, brightnessSmoother_, motionSmoother_, keySmoother_;
    double hostSampleRate_ = 48000.0;
    double positionA_ = 0.0, positionAir_ = 0.0;
    double motionPhaseA1_ = 0.0, motionPhaseA2_ = 1.1;
    double motionPhaseAir1_ = 2.3, motionPhaseAir2_ = 0.4;
    int previousKey_ = 2, targetKey_ = 2;
    int loopCrossfadeFrames_ = 240;
    float filterStateLeft_ = 0.0f, filterStateRight_ = 0.0f;
    bool assetsReady_ = false;
    bool banksLoaded_ = false;
};
}
