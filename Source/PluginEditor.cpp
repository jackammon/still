#include "PluginEditor.h"
#include <algorithm>
#include <cmath>

namespace
{
const juce::Colour canvas{0xff191c19};
const juce::Colour panel{0xff222721};
const juce::Colour line{0xff3b433a};
const juce::Colour text{0xfff0ede3};
const juce::Colour muted{0xffa8aa9c};
const juce::Colour accent{0xffc7c9a4};

juce::String percentText(double value)
{
    return juce::String(static_cast<int>(std::lround(value * 100.0))) + "%";
}
}

void StillLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                        float value, float startAngle, float endAngle,
                                        juce::Slider&)
{
    const auto cx = static_cast<float>(x) + static_cast<float>(width) * 0.5f;
    const auto cy = static_cast<float>(y) + static_cast<float>(height) * 0.47f;
    const auto radius = std::min(static_cast<float>(width), static_cast<float>(height)) * 0.37f;

    juce::Path arc;
    arc.addCentredArc(cx, cy, radius, radius, 0.0f, startAngle, endAngle, true);
    g.setColour(line);
    g.strokePath(arc, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved,
                                          juce::PathStrokeType::rounded));

    juce::Path progress;
    progress.addCentredArc(cx, cy, radius, radius, 0.0f, startAngle,
                           startAngle + value * (endAngle - startAngle), true);
    g.setColour(accent);
    g.strokePath(progress, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    g.setColour(panel);
    g.fillEllipse(cx - radius * 0.76f, cy - radius * 0.76f, radius * 1.52f, radius * 1.52f);
    g.setColour(line);
    g.drawEllipse(cx - radius * 0.76f, cy - radius * 0.76f,
                  radius * 1.52f, radius * 1.52f, 1.0f);

    const auto angle = startAngle + value * (endAngle - startAngle);
    juce::Path pointer;
    pointer.startNewSubPath(cx, cy);
    pointer.lineTo(cx + std::cos(angle) * radius * 0.58f,
                   cy + std::sin(angle) * radius * 0.58f);
    g.setColour(text);
    g.strokePath(pointer, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    g.setColour(accent);
    g.fillEllipse(cx - 2.5f, cy - 2.5f, 5.0f, 5.0f);
}

StillAudioProcessorEditor::StillAudioProcessorEditor(StillAudioProcessor& processor)
    : AudioProcessorEditor(processor), processor_(processor)
{
    setOpaque(true);
    setResizable(false, false);
    setSize(860, 520);
    setLookAndFeel(&lookAndFeel_);

    configureSlider(openSlider_, openValue_, openLabel_, "OPEN");
    configureSlider(airSlider_, airValue_, airLabel_, "AIR");
    configureSlider(brightnessSlider_, brightnessValue_, brightnessLabel_, "BRIGHTNESS");
    configureSlider(motionSlider_, motionValue_, motionLabel_, "MOTION");
    configureSlider(levelSlider_, levelValue_, levelLabel_, "LEVEL");

    openSlider_.setTooltip("Morph from the quiet BODY bed to the full aligned BODY and SWIRL sound.");
    airSlider_.setTooltip("Bring in the independent AIR sample layer.");
    brightnessSlider_.setTooltip("Gently darken the samples; full brightness preserves the source audio.");
    motionSlider_.setTooltip("Add very slow, restrained playback drift to the layers.");
    levelSlider_.setTooltip("Overall output level, independent of OPEN.");

    keyBox_.addItemList({"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}, 1);
    keyBox_.setColour(juce::ComboBox::backgroundColourId, panel);
    keyBox_.setColour(juce::ComboBox::textColourId, text);
    keyBox_.setColour(juce::ComboBox::outlineColourId, line);
    keyBox_.setColour(juce::ComboBox::arrowColourId, accent);
    keyBox_.setTooltip("Select a pre-rendered key. D uses the original unprocessed source samples.");
    addAndMakeVisible(keyBox_);

    startButton_.setClickingTogglesState(true);
    startButton_.setColour(juce::TextButton::buttonColourId, panel);
    startButton_.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff39453a));
    startButton_.setColour(juce::TextButton::textColourOffId, muted);
    startButton_.setColour(juce::TextButton::textColourOnId, accent);
    startButton_.setTooltip("Fade the drone in or out without restarting its sample loops.");
    addAndMakeVisible(startButton_);

    auto& parameters = processor_.getParameters();
    sliderAttachments_[0] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(parameters, "open", openSlider_);
    sliderAttachments_[1] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(parameters, "air", airSlider_);
    sliderAttachments_[2] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(parameters, "brightness", brightnessSlider_);
    sliderAttachments_[3] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(parameters, "motion", motionSlider_);
    sliderAttachments_[4] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(parameters, "level", levelSlider_);
    keyAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(parameters, "key", keyBox_);
    startAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(parameters, "start", startButton_);

    startTimerHz(10);
    timerCallback();
}

StillAudioProcessorEditor::~StillAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void StillAudioProcessorEditor::configureSlider(juce::Slider& slider, juce::Label& value,
                                                juce::Label& label, const juce::String& labelText)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.2f,
                               juce::MathConstants<float>::pi * 2.8f, true);
    addAndMakeVisible(slider);

    value.setJustificationType(juce::Justification::centred);
    value.setColour(juce::Label::textColourId, text);
    value.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
    addAndMakeVisible(value);

    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, muted);
    label.setFont(juce::Font(juce::FontOptions(9.5f)));
    label.setText(labelText, juce::dontSendNotification);
    addAndMakeVisible(label);
}

void StillAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(canvas);
    g.setColour(line);
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(10.0f), 9.0f, 1.0f);

    g.setColour(text);
    g.setFont(juce::Font(juce::FontOptions(23.0f, juce::Font::bold)));
    g.drawText("STILL", 32, 21, 180, 34, juce::Justification::centredLeft);
    g.setColour(muted);
    g.setFont(juce::Font(juce::FontOptions(9.5f)));
    g.drawText("AN ORGANIC DRONE INSTRUMENT", 34, 55, 250, 16, juce::Justification::centredLeft);
    g.setColour(line);
    g.drawLine(30.0f, 86.0f, static_cast<float>(getWidth() - 30), 86.0f, 1.0f);

    g.setColour(panel);
    g.fillRoundedRectangle(26.0f, 105.0f, 402.0f, 385.0f, 10.0f);
    g.fillRoundedRectangle(446.0f, 105.0f, 386.0f, 385.0f, 10.0f);
    g.setColour(line);
    g.drawRoundedRectangle(26.0f, 105.0f, 402.0f, 385.0f, 10.0f, 1.0f);
    g.drawRoundedRectangle(446.0f, 105.0f, 386.0f, 385.0f, 10.0f, 1.0f);

    g.setColour(muted);
    g.setFont(juce::Font(juce::FontOptions(9.0f)));
    g.drawText("A LAYER  /  BODY + SWIRL", 48, 121, 250, 16, juce::Justification::centredLeft);
    g.drawText("TEXTURE AND OUTPUT", 470, 121, 250, 16, juce::Justification::centredLeft);

    g.setColour(line);
    g.drawLine(48.0f, 144.0f, 405.0f, 144.0f, 1.0f);
    g.drawLine(470.0f, 144.0f, 808.0f, 144.0f, 1.0f);

    g.setColour(text);
    g.setFont(juce::Font(juce::FontOptions(12.0f)));
    g.drawText("BODY remains present as the sound recedes.", 468, 456, 340, 18,
               juce::Justification::centredLeft);
    g.setColour(muted);
    g.setFont(juce::Font(juce::FontOptions(9.0f)));
    g.drawText(processor_.assetsReady() ? "D SOURCE IS UNPROCESSED  ·  STEREO  ·  48 kHz"
                                       : "AUDIO BANK UNAVAILABLE",
               32, 496, 520, 14, juce::Justification::centredLeft);
}

void StillAudioProcessorEditor::resized()
{
    openSlider_.setBounds(72, 152, 310, 286);
    openValue_.setBounds(118, 426, 218, 22);
    openLabel_.setBounds(118, 448, 218, 18);

    airSlider_.setBounds(470, 156, 158, 127);
    airValue_.setBounds(478, 256, 142, 20);
    airLabel_.setBounds(478, 275, 142, 17);

    brightnessSlider_.setBounds(648, 156, 158, 127);
    brightnessValue_.setBounds(656, 256, 142, 20);
    brightnessLabel_.setBounds(656, 275, 142, 17);

    motionSlider_.setBounds(470, 310, 158, 127);
    motionValue_.setBounds(478, 410, 142, 20);
    motionLabel_.setBounds(478, 429, 142, 17);

    levelSlider_.setBounds(648, 310, 158, 127);
    levelValue_.setBounds(656, 410, 142, 20);
    levelLabel_.setBounds(656, 429, 142, 17);

    keyBox_.setBounds(631, 31, 84, 34);
    startButton_.setBounds(729, 31, 102, 34);
}

void StillAudioProcessorEditor::updateReadouts()
{
    openValue_.setText(percentText(openSlider_.getValue()), juce::dontSendNotification);
    airValue_.setText(percentText(airSlider_.getValue()), juce::dontSendNotification);
    brightnessValue_.setText(percentText(brightnessSlider_.getValue()), juce::dontSendNotification);
    motionValue_.setText(motionSlider_.getValue() < 0.005 ? "OFF" : percentText(motionSlider_.getValue()),
                         juce::dontSendNotification);
    const auto level = levelSlider_.getValue();
    levelValue_.setText(juce::String(level, 1) + " dB", juce::dontSendNotification);
}

void StillAudioProcessorEditor::timerCallback()
{
    updateReadouts();
    const auto text = startButton_.getToggleState() ? "DRONE ON" : "START DRONE";
    if (startButton_.getButtonText() != text)
        startButton_.setButtonText(text);
}
