#include "PluginEditor.h"
#include <algorithm>
#include <cmath>
namespace
{
const juce::Colour canvas{0xff1d201c}, panel{0xff232720}, line{0xff3a4037};
const juce::Colour text{0xffece8dc}, muted{0xffa5a394}, accent{0xffc2c39d};
}
void StillLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
                                        float value, float start, float end, juce::Slider&)
{
    const auto cx = static_cast<float>(x) + w * 0.5f, cy = static_cast<float>(y) + h * 0.47f;
    const auto r = std::min(static_cast<float>(w), static_cast<float>(h)) * 0.37f;
    juce::Path track;
    track.addCentredArc(cx, cy, r, r, 0.0f, start, end, true);
    g.setColour(line);
    g.strokePath(track, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    juce::Path progress;
    progress.addCentredArc(cx, cy, r, r, 0.0f, start, start + value * (end - start), true);
    g.setColour(accent);
    g.strokePath(progress, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(panel); g.fillEllipse(cx-r*.76f, cy-r*.76f, r*1.52f, r*1.52f);
    g.setColour(line); g.drawEllipse(cx-r*.76f, cy-r*.76f, r*1.52f, r*1.52f, 1.0f);
    const auto a = start + value * (end-start);
    juce::Path pointer; pointer.startNewSubPath(cx, cy);
    pointer.lineTo(cx + std::cos(a)*r*.58f, cy + std::sin(a)*r*.58f);
    g.setColour(text);
    g.strokePath(pointer, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(accent); g.fillEllipse(cx-2.5f, cy-2.5f, 5.0f, 5.0f);
}
StillAudioProcessorEditor::StillAudioProcessorEditor(StillAudioProcessor& p)
    : AudioProcessorEditor(p), processor_(p),
      openAttachment_(p.getParameters(), "open", openSlider_),
      airAttachment_(p.getParameters(), "air", airSlider_),
      startAttachment_(p.getParameters(), "start", startButton_)
{
    setOpaque(true); setResizable(false, false); setSize(660, 420); setLookAndFeel(&lookAndFeel_);
    auto configure = [this](juce::Slider& s)
    {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        s.setRotaryParameters(juce::MathConstants<float>::pi*1.2f, juce::MathConstants<float>::pi*2.8f, true);
        addAndMakeVisible(s);
    };
    configure(openSlider_); configure(airSlider_);
    for (auto* label : {&openValue_, &openLabel_, &airValue_, &airLabel_})
    {
        label->setJustificationType(juce::Justification::centred); addAndMakeVisible(*label);
    }
    openValue_.setColour(juce::Label::textColourId, text); openValue_.setFont(juce::Font(17.0f, juce::Font::bold));
    openLabel_.setColour(juce::Label::textColourId, muted); openLabel_.setFont(juce::Font(11.0f));
    openLabel_.setText("OPEN", juce::dontSendNotification);
    airValue_.setColour(juce::Label::textColourId, text); airValue_.setFont(juce::Font(13.0f, juce::Font::bold));
    airLabel_.setColour(juce::Label::textColourId, muted); airLabel_.setFont(juce::Font(10.0f));
    airLabel_.setText("AIR", juce::dontSendNotification);
    startButton_.setClickingTogglesState(true);
    startButton_.setColour(juce::ToggleButton::textColourId, text);
    startButton_.setColour(juce::ToggleButton::tickColourId, accent);
    startButton_.setColour(juce::ToggleButton::tickDisabledColourId, line);
    startButton_.setTooltip("Fade the drone in or out without restarting its sample loops.");
    addAndMakeVisible(startButton_);
    startTimerHz(15); timerCallback();
}
StillAudioProcessorEditor::~StillAudioProcessorEditor() { stopTimer(); setLookAndFeel(nullptr); }
void StillAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(canvas); g.setColour(line);
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(12.0f), 8.0f, 1.0f);
    g.setColour(text); g.setFont(juce::Font(23.0f, juce::Font::bold));
    g.drawText("STILL", 34, 20, 190, 38, juce::Justification::centredLeft);
    g.setColour(muted); g.setFont(juce::Font(9.5f));
    g.drawText("A DRONE FOR THE HELD MOMENT", 36, 57, 220, 18, juce::Justification::centredLeft);
    g.drawText("D  ·  ORIGINAL PITCH", 403, 35, 110, 24, juce::Justification::centredRight);
    g.drawLine(32, 87, getWidth()-32, 87, 1.0f);
    g.setFont(juce::Font(10.0f));
    g.drawFittedText("BODY stays present as OPEN recedes. SWIRL joins gradually, returning to the approved full D sound at the top.",
                     420, 304, 198, 62, juce::Justification::centredLeft, 3);
    g.drawFittedText("AIR runs independently.", 420, 365, 195, 18, juce::Justification::centredLeft, 1);
    if (!processor_.assetsReady())
    {
        g.setColour(juce::Colour(0xffe0a77c));
        g.drawText("Audio assets unavailable", 420, 390, 190, 18, juce::Justification::centredLeft);
    }
    g.setColour(line); g.drawLine(32, 398, getWidth()-32, 398, 1.0f);
    g.setColour(muted); g.setFont(juce::Font(9.0f));
    g.drawText("STAGE-CLEANED SAMPLES  ·  STEREO  ·  48 kHz SOURCE", 34, 400, 500, 14,
               juce::Justification::centredLeft);
}
void StillAudioProcessorEditor::resized()
{
    openSlider_.setBounds(185, 102, 290, 244); openValue_.setBounds(230, 340, 200, 24);
    openLabel_.setBounds(230, 364, 200, 19); airSlider_.setBounds(37, 245, 142, 124);
    airValue_.setBounds(55, 353, 106, 19); airLabel_.setBounds(55, 372, 106, 17);
    startButton_.setBounds(504, 29, 122, 36);
}
void StillAudioProcessorEditor::timerCallback()
{
    openValue_.setText(juce::String(static_cast<int>(std::lround(openSlider_.getValue()*100.0)))+"%", juce::dontSendNotification);
    airValue_.setText(juce::String(static_cast<int>(std::lround(airSlider_.getValue()*100.0)))+"%", juce::dontSendNotification);
    startButton_.setButtonText(startButton_.getToggleState() ? "DRONE ON" : "START DRONE");
    repaint();
}
