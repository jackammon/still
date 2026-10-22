#pragma once
#include "Dsp/DroneEngine.h"
#include <juce_audio_processors/juce_audio_processors.h>
class StillAudioProcessor final : public juce::AudioProcessor
{
public:
    StillAudioProcessor();
    ~StillAudioProcessor() override = default;
    void prepareToPlay(double, int) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
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
private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState parameters_;
    still::dsp::DroneEngine engine_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StillAudioProcessor)
};
