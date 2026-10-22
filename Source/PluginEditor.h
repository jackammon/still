#pragma once

#include "PluginProcessor.h"
#include <array>
#include <vector>

class StillLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider(juce::Graphics&, int, int, int, int, float, float, float,
                          juce::Slider&) override;
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
    void updateReadouts();
    void configureSlider(juce::Slider&, juce::Label&, juce::Label&, const juce::String&);
    void refreshPackSelector();

    StillAudioProcessor& processor_;
    StillLookAndFeel lookAndFeel_;
    juce::Slider openSlider_, airSlider_, brightnessSlider_, motionSlider_, levelSlider_;
    juce::ComboBox keyBox_;
    juce::ComboBox packBox_;
    juce::TextButton startButton_{"DRONE ON"};
    juce::Label openValue_, openLabel_, airValue_, airLabel_, brightnessValue_, brightnessLabel_;
    juce::Label motionValue_, motionLabel_, levelValue_, levelLabel_;
    std::vector<StillAudioProcessor::PackChoice> displayedPackChoices_;
    bool refreshingPackBox_ = false;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 5> sliderAttachments_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keyAttachment_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> startAttachment_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StillAudioProcessorEditor)
};
