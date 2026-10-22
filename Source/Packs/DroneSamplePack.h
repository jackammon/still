#pragma once

#include <array>
#include <memory>
#include <vector>
#include <juce_audio_formats/juce_audio_formats.h>

namespace still::packs
{
enum class DroneLayerRole : unsigned char { body = 0, swirl = 1, air = 2 };

struct DroneLayer
{
    juce::AudioBuffer<float> audio;
    double sampleRate = 48000.0;
    int pitchClass = -1;
    int loopStartFrame = 0;
    int loopEndFrame = 0;
    int loopXfadeFrames = 0;
    float gain = 1.0f;
    juce::String assetId;
    juce::String synchronizedGroup;
    bool valid = false;
};

struct PreparedDronePack
{
    juce::String id, version, displayName, sourceLicense;
    int rootPitchClass = -1;
    std::array<std::array<DroneLayer, 3>, 12> banks{};

    std::size_t residentAudioBytes() const noexcept;
    const DroneLayer* layer(int pitchClass, DroneLayerRole role) const noexcept;
};

struct InstalledPackInfo
{
    juce::String id, version, displayName;
    juce::File directory;
    bool factory = false;
};

class DronePackLoader
{
public:
    static juce::File userPackRoot();
    static bool scanInstalled(std::vector<InstalledPackInfo>& result, juce::String& error);
    static bool loadDirectory(const juce::File& directory,
                              std::unique_ptr<PreparedDronePack>& result, juce::String& error);
    static bool loadFactory(std::unique_ptr<PreparedDronePack>& result, juce::String& error);
    static bool inspectDirectory(const juce::File& directory, InstalledPackInfo& result,
                                 juce::String& error);
};
}
