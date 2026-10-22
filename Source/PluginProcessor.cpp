#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>

namespace
{
constexpr auto openParameter = "open";
constexpr auto airParameter = "air";
constexpr auto startParameter = "start";
constexpr auto keyParameter = "key";
constexpr auto levelParameter = "level";
constexpr auto brightnessParameter = "brightness";
constexpr auto motionParameter = "motion";
constexpr auto packIdProperty = "pack_id";
constexpr auto packVersionProperty = "pack_version";
const juce::String factoryPackId{"com.jackammon.still.factory-stage"};
const juce::String factoryPackVersion{"1.0.0"};
const juce::StringArray keyNames{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

bool isFactoryPack(const juce::String& id, const juce::String& version)
{
    return (id.isEmpty() || id == factoryPackId)
        && (version.isEmpty() || version == factoryPackVersion);
}
}

juce::AudioProcessorValueTreeState::ParameterLayout StillAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{openParameter, 1}, "OPEN", 0.0f, 1.0f, 0.45f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{airParameter, 1}, "AIR", 0.0f, 1.0f, 0.20f));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{startParameter, 1}, "START", true));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{keyParameter, 1}, "KEY", keyNames, 2));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{levelParameter, 1}, "LEVEL",
        juce::NormalisableRange<float>{-36.0f, 6.0f, 0.1f}, 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{brightnessParameter, 1}, "BRIGHTNESS",
        0.0f, 1.0f, 1.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{motionParameter, 1}, "MOTION",
        0.0f, 1.0f, 0.0f));
    return layout;
}

StillAudioProcessor::StillAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters_(*this, nullptr, "STILL", createParameterLayout())
{
    parameters_.state.setProperty(packIdProperty, factoryPackId, nullptr);
    parameters_.state.setProperty(packVersionProperty, factoryPackVersion, nullptr);
    selectedPackId_ = factoryPackId;
    selectedPackVersion_ = factoryPackVersion;
    packChoices_.push_back({factoryPackId, factoryPackVersion, "Factory Stage Drone", true});
    packWorker_ = std::thread([this] { runPackWorker(); });
}

StillAudioProcessor::~StillAudioProcessor()
{
    {
        const std::lock_guard<std::mutex> lock(packMutex_);
        stopPackWorker_ = true;
    }
    packCondition_.notify_all();
    if (packWorker_.joinable())
        packWorker_.join();
}

void StillAudioProcessor::prepareToPlay(double rate, int)
{
    const auto open = parameters_.getRawParameterValue(openParameter)->load();
    const auto air = parameters_.getRawParameterValue(airParameter)->load();
    const auto running = parameters_.getRawParameterValue(startParameter)->load() >= 0.5f;
    const auto key = static_cast<int>(std::lround(parameters_.getRawParameterValue(keyParameter)->load()));
    const auto level = parameters_.getRawParameterValue(levelParameter)->load();
    const auto brightness = parameters_.getRawParameterValue(brightnessParameter)->load();
    const auto motion = parameters_.getRawParameterValue(motionParameter)->load();
    if (!engine_.prepare(rate, open, air, running, key, level, brightness, motion))
    {
        packUnavailable_.store(true, std::memory_order_release);
        setPackStatus("Factory sample pack could not be loaded.");
    }
    packCondition_.notify_all();
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
    if (packUnavailable())
    {
        buffer.clear();
        return;
    }
    const auto open = parameters_.getRawParameterValue(openParameter)->load(std::memory_order_relaxed);
    const auto air = parameters_.getRawParameterValue(airParameter)->load(std::memory_order_relaxed);
    const auto running = parameters_.getRawParameterValue(startParameter)->load(std::memory_order_relaxed) >= 0.5f;
    const auto key = static_cast<int>(std::lround(parameters_.getRawParameterValue(keyParameter)->load(std::memory_order_relaxed)));
    const auto level = parameters_.getRawParameterValue(levelParameter)->load(std::memory_order_relaxed);
    const auto brightness = parameters_.getRawParameterValue(brightnessParameter)->load(std::memory_order_relaxed);
    const auto motion = parameters_.getRawParameterValue(motionParameter)->load(std::memory_order_relaxed);
    engine_.process(buffer, open, air, running, key, level, brightness, motion);
}

juce::AudioProcessorEditor* StillAudioProcessor::createEditor() { return new StillAudioProcessorEditor(*this); }

std::vector<StillAudioProcessor::PackChoice> StillAudioProcessor::getPackChoices() const
{
    const std::lock_guard<std::mutex> lock(packMutex_);
    return packChoices_;
}

juce::String StillAudioProcessor::getPackStatus() const
{
    const std::lock_guard<std::mutex> lock(packMutex_);
    return packStatus_;
}

juce::String StillAudioProcessor::getSelectedPackId() const
{
    const std::lock_guard<std::mutex> lock(packMutex_);
    return selectedPackId_;
}

juce::String StillAudioProcessor::getSelectedPackVersion() const
{
    const std::lock_guard<std::mutex> lock(packMutex_);
    return selectedPackVersion_;
}

void StillAudioProcessor::setPackStatus(const juce::String& value)
{
    const std::lock_guard<std::mutex> lock(packMutex_);
    packStatus_ = value;
}

void StillAudioProcessor::selectPack(const juce::String& id, const juce::String& version)
{
    const auto selectedId = id.isEmpty() ? factoryPackId : id;
    const auto selectedVersion = version.isEmpty() && selectedId == factoryPackId
        ? factoryPackVersion : version;
    parameters_.state.setProperty(packIdProperty, selectedId, nullptr);
    parameters_.state.setProperty(packVersionProperty, selectedVersion, nullptr);
    {
        const std::lock_guard<std::mutex> lock(packMutex_);
        if (selectedPackId_ == selectedId && selectedPackVersion_ == selectedVersion
            && !packUnavailable_.load(std::memory_order_acquire))
            return;
        selectedPackId_ = selectedId;
        selectedPackVersion_ = selectedVersion;
        ++packRequestGeneration_;
        pendingPackRequest_ = PackRequest{selectedId, selectedVersion, packRequestGeneration_};
        packStatus_ = "Loading " + (isFactoryPack(selectedId, selectedVersion)
            ? juce::String("Factory Stage Drone") : selectedId + " " + selectedVersion) + "…";
    }
    packUnavailable_.store(false, std::memory_order_release);
    packCondition_.notify_all();
}

void StillAudioProcessor::refreshPackChoices()
{
    std::vector<still::packs::InstalledPackInfo> installed;
    juce::String error;
    still::packs::DronePackLoader::scanInstalled(installed, error);
    std::vector<PackChoice> choices;
    choices.push_back({factoryPackId, factoryPackVersion, "Factory Stage Drone", true});
    for (const auto& pack : installed)
        choices.push_back({pack.id, pack.version, pack.displayName, false});
    const std::lock_guard<std::mutex> lock(packMutex_);
    packChoices_ = std::move(choices);
}

void StillAudioProcessor::runPackWorker()
{
    refreshPackChoices();
    auto nextScan = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    for (;;)
    {
        std::optional<PackRequest> request;
        {
            std::unique_lock<std::mutex> lock(packMutex_);
            packCondition_.wait_for(lock, std::chrono::milliseconds(100), [this]
            {
                return stopPackWorker_ || pendingPackRequest_.has_value();
            });
            if (stopPackWorker_)
                return;
            request = std::move(pendingPackRequest_);
            pendingPackRequest_.reset();
        }

        engine_.reclaimRetiredPacks();
        const auto now = std::chrono::steady_clock::now();
        if (now >= nextScan)
        {
            refreshPackChoices();
            nextScan = now + std::chrono::seconds(3);
        }
        if (!request.has_value())
            continue;

        std::unique_ptr<still::packs::PreparedDronePack> prepared;
        juce::String error;
        bool loaded = false;
        if (isFactoryPack(request->id, request->version))
        {
            loaded = still::packs::DronePackLoader::loadFactory(prepared, error);
        }
        else
        {
            std::vector<still::packs::InstalledPackInfo> installed;
            still::packs::DronePackLoader::scanInstalled(installed, error);
            const auto found = std::find_if(installed.begin(), installed.end(), [&](const auto& pack)
            {
                return pack.id == request->id && pack.version == request->version;
            });
            if (found == installed.end())
                error = "Sample pack is missing: " + request->id + " " + request->version;
            else
                loaded = still::packs::DronePackLoader::loadDirectory(found->directory, prepared, error);
        }

        const auto requestIsCurrent = [this, generation = request->generation]
        {
            const std::lock_guard<std::mutex> lock(packMutex_);
            return !stopPackWorker_ && generation == packRequestGeneration_;
        };
        if (!requestIsCurrent())
            continue;
        if (!loaded || prepared == nullptr)
        {
            packUnavailable_.store(true, std::memory_order_release);
            setPackStatus("Pack unavailable: " + error);
            continue;
        }
        const auto candidateBytes = prepared->residentAudioBytes();
        const auto residentBytes = engine_.residentAudioBytes();
        if (candidateBytes > still::dsp::DroneEngine::maxResidentAudioBytes
            || residentBytes > still::dsp::DroneEngine::maxResidentAudioBytes - candidateBytes)
        {
            packUnavailable_.store(true, std::memory_order_release);
            setPackStatus("Pack switch exceeds STILL's 1 GiB transition-memory limit.");
            continue;
        }

        while (!engine_.assetsReady() && requestIsCurrent())
        {
            std::unique_lock<std::mutex> lock(packMutex_);
            packCondition_.wait_for(lock, std::chrono::milliseconds(50));
        }
        const auto loadedName = prepared->displayName;
        bool published = false;
        while (requestIsCurrent())
        {
            engine_.reclaimRetiredPacks();
            if (engine_.publishPreparedPack(prepared))
            {
                published = true;
                break;
            }
            std::unique_lock<std::mutex> lock(packMutex_);
            packCondition_.wait_for(lock, std::chrono::milliseconds(25));
        }
        if (!published)
            continue;

        packUnavailable_.store(false, std::memory_order_release);
        setPackStatus("Loaded " + loadedName);
        refreshPackChoices();
    }
}

void StillAudioProcessor::getStateInformation(juce::MemoryBlock& data)
{
    if (auto xml = parameters_.copyState().createXml()) copyXmlToBinary(*xml, data);
}

void StillAudioProcessor::setStateInformation(const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size))
    {
        if (!xml->hasTagName(parameters_.state.getType()))
            return;
        parameters_.replaceState(juce::ValueTree::fromXml(*xml));
        auto id = parameters_.state.getProperty(packIdProperty).toString();
        auto version = parameters_.state.getProperty(packVersionProperty).toString();
        if (id.isEmpty() && version.isEmpty())
        {
            id = factoryPackId;
            version = factoryPackVersion;
        }
        const auto restoringDifferentPack = getSelectedPackId() != id
            || getSelectedPackVersion() != version;
        if (restoringDifferentPack)
            packUnavailable_.store(true, std::memory_order_release);
        selectPack(id, version);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new StillAudioProcessor(); }
