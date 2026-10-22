#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <memory>
namespace { constexpr auto openParameter = "open"; constexpr auto airParameter = "air"; constexpr auto startParameter = "start"; }
juce::AudioProcessorValueTreeState::ParameterLayout StillAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{openParameter, 1}, "OPEN", 0.0f, 1.0f, 0.45f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{airParameter, 1}, "AIR", 0.0f, 1.0f, 0.20f));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{startParameter, 1}, "START", true));
    return layout;
}
StillAudioProcessor::StillAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters_(*this, nullptr, "STILL", createParameterLayout()) {}
void StillAudioProcessor::prepareToPlay(double rate, int)
{
    const auto open = parameters_.getRawParameterValue(openParameter)->load();
    const auto air = parameters_.getRawParameterValue(airParameter)->load();
    const auto running = parameters_.getRawParameterValue(startParameter)->load() >= 0.5f;
    engine_.prepare(rate, open, air, running);
}
void StillAudioProcessor::releaseResources() {}
bool StillAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto output = layouts.getMainOutputChannelSet();
    return layouts.inputBuses.isEmpty()
        && (output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo());
}
void StillAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const juce::ScopedNoDenormals noDenormals;
    const auto open = parameters_.getRawParameterValue(openParameter)->load(std::memory_order_relaxed);
    const auto air = parameters_.getRawParameterValue(airParameter)->load(std::memory_order_relaxed);
    const auto running = parameters_.getRawParameterValue(startParameter)->load(std::memory_order_relaxed) >= 0.5f;
    engine_.process(buffer, open, air, running);
}
juce::AudioProcessorEditor* StillAudioProcessor::createEditor() { return new StillAudioProcessorEditor(*this); }
void StillAudioProcessor::getStateInformation(juce::MemoryBlock& data)
{
    if (auto xml = parameters_.copyState().createXml()) copyXmlToBinary(*xml, data);
}
void StillAudioProcessor::setStateInformation(const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size))
        if (xml->hasTagName(parameters_.state.getType()))
            parameters_.replaceState(juce::ValueTree::fromXml(*xml));
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new StillAudioProcessor(); }
