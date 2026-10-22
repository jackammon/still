#include "DroneSamplePack.h"
#include <StillAssets.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <limits>
#include <set>
#include <string>

namespace still::packs
{
namespace
{
constexpr const char* factoryManifestResource = "manifest_json";
constexpr const char* factoryPackId = "com.jackammon.still.factory-stage";
constexpr int minimumSampleRate = 8000;
constexpr int maximumSampleRate = 192000;
constexpr std::size_t maximumPackAudioBytes = 768u * 1024u * 1024u;

struct ManifestAsset
{
    juce::String id, path, role, synchronizedGroup;
    int pitchClass = -1, loopStart = 0, loopEnd = 0, loopXfade = 0;
    float gainDb = 0.0f;
};

struct ParsedManifest
{
    juce::String id, version, displayName, sourceLicense;
    int rootPitchClass = -1;
    juce::Array<juce::var> assets;
};

juce::var property(const juce::var& object, const juce::Identifier& name)
{
    const auto* dynamic = object.getDynamicObject();
    return dynamic != nullptr ? dynamic->getProperty(name) : juce::var{};
}

bool object(const juce::var& value) { return value.getDynamicObject() != nullptr; }

bool stringProperty(const juce::var& value, const juce::Identifier& name, juce::String& out)
{
    const auto field = property(value, name);
    if (!field.isString())
        return false;
    out = field.toString().trim();
    return out.isNotEmpty();
}

bool intProperty(const juce::var& value, const juce::Identifier& name, int& out)
{
    const auto field = property(value, name);
    if (!field.isInt() && !field.isInt64())
        return false;
    const auto numeric = static_cast<std::int64_t>(field);
    if (numeric < std::numeric_limits<int>::min() || numeric > std::numeric_limits<int>::max())
        return false;
    out = static_cast<int>(numeric);
    return true;
}

bool parseManifest(const juce::String& json, ParsedManifest& manifest, juce::String& error)
{
    const auto parsed = juce::JSON::parse(json);
    if (!object(parsed))
    {
        error = "manifest.json must contain a JSON object.";
        return false;
    }

    int schema = 0;
    if (!intProperty(parsed, "schema_version", schema))
    {
        error = "manifest.json is missing integer schema_version.";
        return false;
    }
    if (schema != 1)
    {
        error = "Unsupported sample-pack schema_version " + juce::String(schema)
            + "; STILL supports v1.";
        return false;
    }

    if (!stringProperty(parsed, "id", manifest.id)
        || !stringProperty(parsed, "version", manifest.version)
        || !stringProperty(parsed, "display_name", manifest.displayName)
        || !stringProperty(parsed, "source_license", manifest.sourceLicense))
    {
        error = "manifest.json requires non-empty id, version, display_name, and source_license.";
        return false;
    }

    juce::String instrument;
    if (!stringProperty(parsed, "instrument", instrument) || instrument != "still")
    {
        error = "This Audio Unit only accepts packs with instrument set to \"still\".";
        return false;
    }

    const auto validSlug = [](const std::string& value)
    {
        if (value.empty() || value.size() > 128
            || !std::isalnum(static_cast<unsigned char>(value.front())))
            return false;
        return std::all_of(value.begin(), value.end(), [](unsigned char c)
        {
            return std::isalnum(c) || c == '.' || c == '_' || c == '-';
        });
    };
    if (!validSlug(manifest.id.toStdString()) || !validSlug(manifest.version.toStdString()))
    {
        error = "Pack id and version may contain only letters, digits, dot, underscore, and hyphen.";
        return false;
    }

    const auto stillExtension = property(property(parsed, "extensions"), "still");
    if (!stillExtension.isVoid() && !object(stillExtension))
    {
        error = "extensions.still must be an object when present.";
        return false;
    }
    const auto rootPitch = property(stillExtension, "original_root_pitch_class");
    if (!rootPitch.isVoid()
        && (!intProperty(stillExtension, "original_root_pitch_class", manifest.rootPitchClass)
            || manifest.rootPitchClass < 0 || manifest.rootPitchClass > 11))
    {
        error = "extensions.still.original_root_pitch_class must be 0 through 11.";
        return false;
    }

    const auto assets = property(parsed, "assets");
    if (!assets.isArray() || assets.size() == 0)
    {
        error = "manifest.json assets must be a non-empty array.";
        return false;
    }
    for (int i = 0; i < assets.size(); ++i)
        manifest.assets.add(assets[i]);
    return true;
}

bool parseAsset(const juce::var& value, ManifestAsset& asset, juce::String& error)
{
    if (!object(value)
        || !stringProperty(value, "id", asset.id)
        || !stringProperty(value, "path", asset.path)
        || !stringProperty(value, "role", asset.role))
    {
        error = "Every asset requires a stable id, relative path, and role.";
        return false;
    }
    if (asset.role != "drone_body" && asset.role != "drone_swirl" && asset.role != "drone_air")
    {
        error = "Asset " + asset.id + " has unsupported STILL role: " + asset.role;
        return false;
    }

    const auto pitchClass = property(value, "pitch_class");
    if (!pitchClass.isVoid())
    {
        if (!intProperty(value, "pitch_class", asset.pitchClass)
            || asset.pitchClass < 0 || asset.pitchClass > 11)
        {
            error = "Asset " + asset.id + " pitch_class must be an integer from 0 through 11.";
            return false;
        }
    }
    else
    {
        int rootMidi = -1;
        if (!intProperty(value, "root_midi", rootMidi) || rootMidi < 0 || rootMidi > 127)
        {
            error = "Asset " + asset.id + " requires pitch_class (0 through 11) or root_midi.";
            return false;
        }
        asset.pitchClass = rootMidi % 12;
    }

    if (!intProperty(value, "loop_start_frame", asset.loopStart)
        || !intProperty(value, "loop_end_frame", asset.loopEnd))
    {
        error = "Asset " + asset.id + " requires loop_start_frame and loop_end_frame.";
        return false;
    }
    const auto xfade = property(value, "loop_xfade_frames");
    if (!xfade.isVoid() && !intProperty(value, "loop_xfade_frames", asset.loopXfade))
    {
        error = "Asset " + asset.id + " loop_xfade_frames must be an integer.";
        return false;
    }

    const auto gainValue = property(value, "gain_db");
    if (!gainValue.isVoid())
    {
        double gain = 0.0;
        if ((!gainValue.isDouble() && !gainValue.isInt() && !gainValue.isInt64())
            || !std::isfinite(gain = static_cast<double>(gainValue)) || gain < -60.0 || gain > 12.0)
        {
            error = "Asset " + asset.id + " gain_db must be a finite number from -60 to +12.";
            return false;
        }
        asset.gainDb = static_cast<float>(gain);
    }

    const auto assetExtensions = property(value, "extensions");
    if (!assetExtensions.isVoid() && !object(assetExtensions))
    {
        error = "Asset " + asset.id + " extensions must be an object.";
        return false;
    }
    const auto stillExtension = property(assetExtensions, "still");
    if (!stillExtension.isVoid() && !object(stillExtension))
    {
        error = "Asset " + asset.id + " extensions.still must be an object.";
        return false;
    }
    const auto synchronizedGroup = property(stillExtension, "synchronized_group");
    if (!synchronizedGroup.isVoid()
        && !stringProperty(stillExtension, "synchronized_group", asset.synchronizedGroup))
    {
        error = "Asset " + asset.id + " synchronized_group must be a non-empty string.";
        return false;
    }

    const auto rel = std::filesystem::path(asset.path.toStdString());
    if (rel.empty() || rel.is_absolute())
    {
        error = "Asset " + asset.id + " path must be relative.";
        return false;
    }
    for (const auto& component : rel)
    {
        if (component == "..")
        {
            error = "Asset " + asset.id + " path may not contain '..'.";
            return false;
        }
    }
    return true;
}

bool containedBy(const std::filesystem::path& root, const std::filesystem::path& candidate)
{
    const auto relative = candidate.lexically_relative(root);
    if (relative.empty())
        return candidate == root;
    return *relative.begin() != "..";
}

bool decodeAudio(const ManifestAsset& item, const juce::File& directory, bool embedded,
                 std::size_t& remainingAudioBudget, DroneLayer& destination,
                 juce::String& error)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader;
    if (embedded)
    {
        const auto resourceName = juce::File(item.path).getFileName().replaceCharacter('.', '_');
        int byteCount = 0;
        const auto* data = StillAssets::getNamedResource(resourceName.toRawUTF8(), byteCount);
        if (data == nullptr || byteCount <= 0)
        {
            error = "Embedded factory asset is missing: " + item.path;
            return false;
        }
        auto stream = std::make_unique<juce::MemoryInputStream>(
            data, static_cast<std::size_t>(byteCount), false);
        reader.reset(formats.createReaderFor(std::move(stream)));
    }
    else
    {
        const juce::File relative(item.path);
        if (!relative.getFileExtension().equalsIgnoreCase(".wav"))
        {
            error = "Asset " + item.id + " must be a WAV file for sample-pack v1.";
            return false;
        }
        std::filesystem::path root, candidate;
        try
        {
            root = std::filesystem::canonical(directory.getFullPathName().toStdString());
            candidate = std::filesystem::canonical(
                directory.getChildFile(item.path).getFullPathName().toStdString());
        }
        catch (const std::exception&)
        {
            error = "Asset " + item.id + " is missing or cannot be resolved: " + item.path;
            return false;
        }
        if (!containedBy(root, candidate))
        {
            error = "Asset " + item.id + " resolves outside its pack directory.";
            return false;
        }
        reader.reset(formats.createReaderFor(juce::File(candidate.string())));
    }

    if (reader == nullptr || reader->lengthInSamples <= 0
        || reader->lengthInSamples > std::numeric_limits<int>::max()
        || reader->numChannels < 1 || reader->numChannels > 2
        || reader->sampleRate < minimumSampleRate || reader->sampleRate > maximumSampleRate)
    {
        error = "Asset " + item.id
            + " is not valid mono/stereo audio at a supported sample rate.";
        return false;
    }

    const auto frames = static_cast<int>(reader->lengthInSamples);
    if (item.loopStart < 0 || item.loopStart >= item.loopEnd || item.loopEnd > frames
        || item.loopXfade < 0 || item.loopXfade * 2 >= item.loopEnd - item.loopStart)
    {
        error = "Asset " + item.id + " has invalid loop frame metadata.";
        return false;
    }

    const auto decodedBytes = static_cast<std::size_t>(frames) * 2u * sizeof(float);
    if (decodedBytes > remainingAudioBudget)
    {
        error = "Pack audio exceeds the 768 MiB decoded-memory limit.";
        return false;
    }

    juce::AudioBuffer<float> decoded(static_cast<int>(reader->numChannels), frames);
    if (!reader->read(&decoded, 0, frames, 0, true, true))
    {
        error = "Could not decode asset " + item.id + ".";
        return false;
    }
    destination.audio.setSize(2, frames, false, true, false);
    for (int channel = 0; channel < 2; ++channel)
    {
        const auto sourceChannel = reader->numChannels == 1 ? 0 : channel;
        const auto* source = decoded.getReadPointer(sourceChannel);
        auto* output = destination.audio.getWritePointer(channel);
        for (int frame = 0; frame < frames; ++frame)
        {
            const auto sample = source[frame];
            const auto pcmStep = reader->usesFloatingPointData || reader->bitsPerSample == 0
                ? 0.0 : std::ldexp(1.0, 1 - static_cast<int>(reader->bitsPerSample));
            const auto clipped = reader->usesFloatingPointData
                ? std::abs(sample) >= 1.0f
                : (sample <= -1.0f || (pcmStep > 0.0
                    && static_cast<double>(sample) >= 1.0 - pcmStep * 1.1));
            if (!std::isfinite(sample) || std::abs(sample) > 1.00001f || clipped)
            {
                error = "Asset " + item.id + " contains non-finite or clipped audio.";
                destination.audio.setSize(0, 0);
                return false;
            }
            output[frame] = sample;
        }
    }

    destination.sampleRate = reader->sampleRate;
    destination.pitchClass = item.pitchClass;
    destination.loopStartFrame = item.loopStart;
    destination.loopEndFrame = item.loopEnd;
    destination.loopXfadeFrames = item.loopXfade;
    destination.gain = juce::Decibels::decibelsToGain(item.gainDb);
    destination.assetId = item.id;
    destination.synchronizedGroup = item.synchronizedGroup;
    destination.valid = true;
    remainingAudioBudget -= decodedBytes;
    return true;
}

int roleIndex(const juce::String& role)
{
    if (role == "drone_body") return static_cast<int>(DroneLayerRole::body);
    if (role == "drone_swirl") return static_cast<int>(DroneLayerRole::swirl);
    return static_cast<int>(DroneLayerRole::air);
}

bool loadParsed(const ParsedManifest& manifest, const juce::File& directory, bool embedded,
                std::unique_ptr<PreparedDronePack>& result, juce::String& error)
{
    auto pack = std::make_unique<PreparedDronePack>();
    pack->id = manifest.id;
    pack->version = manifest.version;
    pack->displayName = manifest.displayName;
    pack->sourceLicense = manifest.sourceLicense;
    pack->rootPitchClass = manifest.rootPitchClass;

    std::set<std::string> ids;
    auto remainingAudioBudget = maximumPackAudioBytes;
    for (const auto& value : manifest.assets)
    {
        ManifestAsset item;
        if (!parseAsset(value, item, error))
            return false;
        if (!ids.insert(item.id.toStdString()).second)
        {
            error = "Duplicate asset id: " + item.id;
            return false;
        }
        auto& layer = pack->banks[static_cast<std::size_t>(item.pitchClass)]
                                  [static_cast<std::size_t>(roleIndex(item.role))];
        if (layer.valid)
        {
            error = "Duplicate STILL role/pitch_class in manifest: " + item.role + " / "
                + juce::String(item.pitchClass);
            return false;
        }
        if (!decodeAudio(item, directory, embedded, remainingAudioBudget, layer, error))
            return false;
        if (pack->rootPitchClass < 0 && item.role != "drone_air")
            pack->rootPitchClass = item.pitchClass;
    }

    bool hasBodyOrSwirl = false;
    bool hasAnyBody = false, hasAnySwirl = false;
    std::array<bool, 12> bodyPitches{}, swirlPitches{};
    for (int pitch = 0; pitch < 12; ++pitch)
    {
        const auto& bank = pack->banks[static_cast<std::size_t>(pitch)];
        const auto& body = bank[static_cast<std::size_t>(DroneLayerRole::body)];
        const auto& swirl = bank[static_cast<std::size_t>(DroneLayerRole::swirl)];
        hasBodyOrSwirl = hasBodyOrSwirl || body.valid || swirl.valid;
        hasAnyBody = hasAnyBody || body.valid;
        hasAnySwirl = hasAnySwirl || swirl.valid;
        bodyPitches[static_cast<std::size_t>(pitch)] = body.valid;
        swirlPitches[static_cast<std::size_t>(pitch)] = swirl.valid;
        if (body.valid && swirl.valid)
        {
            const auto aligned = body.audio.getNumSamples() == swirl.audio.getNumSamples()
                && std::abs(body.sampleRate - swirl.sampleRate) < 0.01
                && body.loopStartFrame == swirl.loopStartFrame
                && body.loopEndFrame == swirl.loopEndFrame
                && body.loopXfadeFrames == swirl.loopXfadeFrames
                && body.synchronizedGroup.isNotEmpty()
                && body.synchronizedGroup == swirl.synchronizedGroup;
            if (!aligned)
            {
                error = "BODY and SWIRL must share frame count, sample rate, loop points, and synchronized_group for pitch_class "
                    + juce::String(pitch) + ".";
                return false;
            }
        }
    }
    if (!hasBodyOrSwirl)
    {
        error = "A STILL pack needs at least one drone_body or drone_swirl asset.";
        return false;
    }
    if (hasAnyBody && hasAnySwirl && bodyPitches != swirlPitches)
    {
        error = "BODY and SWIRL must provide the same pitch_class coverage so fallback selection stays synchronized.";
        return false;
    }
    if (pack->rootPitchClass < 0 || pack->rootPitchClass > 11)
    {
        error = "Could not determine a valid STILL pack root pitch class.";
        return false;
    }

    result = std::move(pack);
    return true;
}

bool readManifestFile(const juce::File& directory, ParsedManifest& parsed, juce::String& error)
{
    const auto manifestFile = directory.getChildFile("manifest.json");
    if (!manifestFile.existsAsFile())
    {
        error = "Pack directory is missing manifest.json.";
        return false;
    }
    if (manifestFile.getSize() > 1024 * 1024)
    {
        error = "manifest.json exceeds the 1 MiB safety limit.";
        return false;
    }
    return parseManifest(manifestFile.loadFileAsString(), parsed, error);
}
}

std::size_t PreparedDronePack::residentAudioBytes() const noexcept
{
    std::size_t bytes = 0;
    for (const auto& bank : banks)
        for (const auto& layer : bank)
            bytes += static_cast<std::size_t>(layer.audio.getNumChannels())
                * static_cast<std::size_t>(layer.audio.getNumSamples()) * sizeof(float);
    return bytes;
}

const DroneLayer* PreparedDronePack::layer(int pitchClass, DroneLayerRole role) const noexcept
{
    if (pitchClass < 0 || pitchClass > 11)
        return nullptr;
    const auto& found = banks[static_cast<std::size_t>(pitchClass)][static_cast<std::size_t>(role)];
    return found.valid ? &found : nullptr;
}

juce::File DronePackLoader::userPackRoot()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("STILL Instruments").getChildFile("Packs");
}

bool DronePackLoader::inspectDirectory(const juce::File& directory, InstalledPackInfo& result,
                                       juce::String& error)
{
    ParsedManifest parsed;
    if (!readManifestFile(directory, parsed, error))
        return false;
    if (parsed.id == factoryPackId)
    {
        error = "The Factory Stage Drone pack id is reserved.";
        return false;
    }
    result.id = parsed.id;
    result.version = parsed.version;
    result.displayName = parsed.displayName;
    result.directory = directory;
    result.factory = false;
    return true;
}

bool DronePackLoader::scanInstalled(std::vector<InstalledPackInfo>& result, juce::String& error)
{
    result.clear();
    error.clear();
    const auto root = userPackRoot();
    if (!root.isDirectory())
        return true;

    juce::Array<juce::File> idDirectories;
    root.findChildFiles(idDirectories, juce::File::findDirectories, false);
    for (const auto& idDirectory : idDirectories)
    {
        juce::Array<juce::File> versions;
        idDirectory.findChildFiles(versions, juce::File::findDirectories, false);
        for (const auto& versionDirectory : versions)
        {
            InstalledPackInfo info;
            juce::String itemError;
            if (inspectDirectory(versionDirectory, info, itemError))
                result.push_back(std::move(info));
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b)
    {
        const auto byName = a.displayName.compareIgnoreCase(b.displayName);
        return byName == 0 ? a.version.compareIgnoreCase(b.version) < 0 : byName < 0;
    });
    return true;
}

bool DronePackLoader::loadDirectory(const juce::File& directory,
                                    std::unique_ptr<PreparedDronePack>& result,
                                    juce::String& error)
{
    result.reset();
    ParsedManifest parsed;
    if (!readManifestFile(directory, parsed, error))
        return false;
    if (parsed.id == factoryPackId)
    {
        error = "The Factory Stage Drone pack id is reserved.";
        return false;
    }
    return loadParsed(parsed, directory, false, result, error);
}

bool DronePackLoader::loadFactory(std::unique_ptr<PreparedDronePack>& result, juce::String& error)
{
    result.reset();
    int byteCount = 0;
    const auto* data = StillAssets::getNamedResource(factoryManifestResource, byteCount);
    if (data == nullptr || byteCount <= 0)
    {
        error = "Embedded factory sample-pack manifest is missing.";
        return false;
    }
    ParsedManifest parsed;
    if (!parseManifest(juce::String::fromUTF8(reinterpret_cast<const char*>(data), byteCount),
                       parsed, error))
        return false;
    return loadParsed(parsed, {}, true, result, error);
}
}
