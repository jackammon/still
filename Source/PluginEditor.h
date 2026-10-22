#pragma once
#include "PluginProcessor.h"
class StillLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider(juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
};
class StillAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit StillAudioProcessorEditor(StillAudioProcessor&);
    ~StillAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    StillAudioProcessor& processor_;
    StillLookAndFeel lookAndFeel_;
    juce::Slider openSlider_, airSlider_;
    juce::ToggleButton startButton_{"DRONE ON"};
    juce::Label openValue_, openLabel_, airValue_, airLabel_;
    juce::AudioProcessorValueTreeState::SliderAttachment openAttachment_, airAttachment_;
    juce::AudioProcessorValueTreeState::ButtonAttachment startAttachment_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StillAudioProcessorEditor)
};
