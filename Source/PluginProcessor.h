#pragma once

#include "Dsp/DroneEngine.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

class StillAudioProcessor final : public juce::AudioProcessor
{
public:
    struct PackChoice
    {
        juce::String id, version, displayName;
        bool factory = false;
    };

    StillAudioProcessor();
    ~StillAudioProcessor() override;
    void prepareToPlay(double, int) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState& getParameters() noexcept { return parameters_; }
    bool assetsReady() const noexcept { return engine_.assetsReady(); }
    bool packUnavailable() const noexcept { return packUnavailable_.load(std::memory_order_acquire); }
    bool packTransitioning() const noexcept { return engine_.packTransitioning(); }
    std::vector<PackChoice> getPackChoices() const;
    juce::String getPackStatus() const;
    juce::String getSelectedPackId() const;
    juce::String getSelectedPackVersion() const;
    void selectPack(const juce::String& id, const juce::String& version);

private:
    struct PackRequest
    {
        juce::String id, version;
        std::uint64_t generation = 0;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void runPackWorker();
    void refreshPackChoices();
    void setPackStatus(const juce::String&);

    juce::AudioProcessorValueTreeState parameters_;
    still::dsp::DroneEngine engine_;
    std::atomic<bool> packUnavailable_{false};
    mutable std::mutex packMutex_;
    std::condition_variable packCondition_;
    bool stopPackWorker_ = false;
    std::optional<PackRequest> pendingPackRequest_;
    std::uint64_t packRequestGeneration_ = 0;
    std::vector<PackChoice> packChoices_;
    juce::String selectedPackId_, selectedPackVersion_, packStatus_{"Factory Stage Drone"};
    std::thread packWorker_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StillAudioProcessor)
};
