#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
namespace still::dsp
{
class DroneEngine
{
public:
    bool prepare(double hostSampleRate, float initialOpen, float initialAir, bool initiallyRunning);
    void process(juce::AudioBuffer<float>& output, float open, float air, bool running) noexcept;
    bool assetsReady() const noexcept { return assetsReady_; }
private:
    static bool decodeStereo(const void*, int, juce::AudioBuffer<float>&, double&);
    static int wrapIndex(int index, int sampleCount) noexcept;
    static float interpolateWrapped(const float*, int, double) noexcept;
    juce::AudioBuffer<float> body_, swirl_, air_;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> openSmoother_, airSmoother_, startSmoother_;
    double hostSampleRate_ = 48000.0;
    double sourceSampleRateA_ = 48000.0, sourceSampleRateAir_ = 48000.0;
    double positionA_ = 0.0, positionAir_ = 0.0;
    bool assetsReady_ = false;
};
}
