#include "anim/clip/ClipIO.h"

#include <algorithm>
#include <cstring>
#include <string>

#include "anim/avatar/MuscleTable.h"
#include "anim/core/Assert.h"

namespace anim {
namespace {

constexpr uint16_t kClipIOVersion = kCurrentFormatVersion;

enum ClipFlags : uint32_t {
    kFlagLooping = 1u << 0,
    kFlagHasRootT = 1u << 1,
    kFlagHasRootQ = 1u << 2,
    kFlagHasFootT = 1u << 3,
    kFlagHasFootQ = 1u << 4,
    kFlagHasHandT = 1u << 5,
    kFlagHasHandQ = 1u << 6,
};

constexpr uint8_t kBonePosition = 1u << 0;
constexpr uint8_t kBoneRotation = 1u << 1;
constexpr uint8_t kBoneScale = 1u << 2;

void writeCurve(const Curve& curve, Writer& writer) {
    writer.writeU8(static_cast<uint8_t>(curve.preWrap));
    writer.writeU8(static_cast<uint8_t>(curve.postWrap));
    writer.writeU16(0);
    writer.writeU32(static_cast<uint32_t>(curve.keys.size()));

    for (const Keyframe& key : curve.keys) {
        writer.writeF32(key.time);
        writer.writeF32(key.value);
        writer.writeF32(key.inSlope);
        writer.writeF32(key.outSlope);
        writer.writeF32(key.inWeight);
        writer.writeF32(key.outWeight);
        writer.writeU8(key.tangentMode);
        writer.writeU8(key.weightedMode);
        writer.writeU8(key.constant ? 1 : 0);
        writer.writeU8(0);
    }
}

Curve readCurve(Reader& reader) {
    Curve curve;
    curve.preWrap = static_cast<WrapMode>(reader.readU8());
    curve.postWrap = static_cast<WrapMode>(reader.readU8());
    reader.readU16();

    const uint32_t count = reader.readU32();
    curve.keys.resize(count);
    for (Keyframe& key : curve.keys) {
        key.time = reader.readF32();
        key.value = reader.readF32();
        key.inSlope = reader.readF32();
        key.outSlope = reader.readF32();
        key.inWeight = reader.readF32();
        key.outWeight = reader.readF32();
        key.tangentMode = reader.readU8();
        key.weightedMode = reader.readU8();
        key.constant = reader.readU8() != 0;
        reader.readU8();
    }
    return curve;
}

void writeTransformCurves(const Curve* curves, int count, Writer& writer) {
    for (int i = 0; i < count; ++i) writeCurve(curves[i], writer);
}

void readTransformCurves(Curve* curves, int count, Reader& reader) {
    for (int i = 0; i < count; ++i) curves[i] = readCurve(reader);
}

void writeProperties(const Clip& clip, Writer& writer) {
    for (const PropertyChannel& property : clip.properties) {
        writer.writeU64(property.path);
        writer.writeU64(property.attribute);
        writeCurve(property.curve, writer);
    }
}

void readProperties(Clip& clip, uint32_t count, Reader& reader) {
    clip.properties.resize(count);
    for (PropertyChannel& property : clip.properties) {
        property.path = reader.readU64();
        property.attribute = reader.readU64();
        property.curve = readCurve(reader);
    }
}

uint32_t flagsFor(const Clip& clip) {
    uint32_t flags = clip.looping ? kFlagLooping : 0;
    if (clip.type != ClipType::HumanoidMuscle) return flags;

    const auto& humanoid = static_cast<const HumanoidClip&>(clip);
    if (humanoid.hasRootT) flags |= kFlagHasRootT;
    if (humanoid.hasRootQ) flags |= kFlagHasRootQ;
    if (humanoid.hasFootT) flags |= kFlagHasFootT;
    if (humanoid.hasFootQ) flags |= kFlagHasFootQ;
    if (humanoid.hasHandT) flags |= kFlagHasHandT;
    if (humanoid.hasHandQ) flags |= kFlagHasHandQ;
    return flags;
}

}  // namespace

bool writeClip(const Clip& clip, Writer& writer, std::string* error) {
    if (clip.type == ClipType::BoneLocal) {
        const auto& bone = static_cast<const BoneLocalClip&>(clip);
        int maxBone = -1;
        for (const BoneChannel& channel : bone.channels) {
            if (channel.boneIndex < 0) {
                if (error != nullptr) *error = "BoneLocalClip has a negative bone index";
                return false;
            }
            maxBone = std::max(maxBone, channel.boneIndex);
        }

        FileHeader header;
        header.version = kClipIOVersion;
        header.type = static_cast<uint16_t>(clip.type);
        writer.writeFileHeader(header);
        writer.writeU32(flagsFor(clip));
        writer.writeF32(clip.length);
        writer.writeF32(clip.averageSpeed);
        writer.writeU32(0);
        writer.writeU32(static_cast<uint32_t>(maxBone + 1));
        writer.writeU32(static_cast<uint32_t>(bone.channels.size()));
        writer.writeU32(0);
        writer.writeU32(static_cast<uint32_t>(clip.markers.size()));
        writer.writeU32(static_cast<uint32_t>(clip.properties.size()));

        for (const BoneChannel& channel : bone.channels) {
            writer.writeI32(channel.boneIndex);
            uint8_t mask = 0;
            if (channel.hasPosition) mask |= kBonePosition;
            if (channel.hasRotation) mask |= kBoneRotation;
            if (channel.hasScale) mask |= kBoneScale;
            writer.writeU8(mask);

            if (channel.hasPosition) writeTransformCurves(channel.position, 3, writer);
            if (channel.hasRotation) writeTransformCurves(channel.rotation, 4, writer);
            if (channel.hasScale) writeTransformCurves(channel.scale, 3, writer);
        }

        for (const EventMarker& marker : clip.markers) {
            writer.writeF32(marker.normalizedTime);
            writer.writeU64(marker.name);
        }
        writeProperties(clip, writer);
        return true;
    }

    if (clip.type != ClipType::HumanoidMuscle) {
        if (error != nullptr) *error = "unsupported clip type";
        return false;
    }

    const auto& humanoid = static_cast<const HumanoidClip&>(clip);
    FileHeader header;
    header.version = kClipIOVersion;
    header.type = static_cast<uint16_t>(clip.type);
    writer.writeFileHeader(header);
    writer.writeU32(flagsFor(clip));
    writer.writeF32(clip.length);
    writer.writeF32(clip.averageSpeed);
    writer.writeU32(0);
    writer.writeU32(0);
    writer.writeU32(0);
    writer.writeU32(kMuscleCount);
    writer.writeU32(static_cast<uint32_t>(clip.markers.size()));
    writer.writeU32(static_cast<uint32_t>(clip.properties.size()));

    writeTransformCurves(humanoid.rootT, 3, writer);
    writeTransformCurves(humanoid.rootQ, 4, writer);

    for (int i = 0; i < kMuscleCount; ++i) {
        static const Curve empty;
        const Curve* curve =
            i < static_cast<int>(humanoid.muscles.size()) ? &humanoid.muscles[i] : &empty;
        writeCurve(*curve, writer);
    }

    if (humanoid.hasFootT) {
        writeTransformCurves(humanoid.leftFootT, 3, writer);
        writeTransformCurves(humanoid.rightFootT, 3, writer);
    }
    if (humanoid.hasFootQ) {
        writeTransformCurves(humanoid.leftFootQ, 4, writer);
        writeTransformCurves(humanoid.rightFootQ, 4, writer);
    }
    if (humanoid.hasHandT) {
        writeTransformCurves(humanoid.leftHandT, 3, writer);
        writeTransformCurves(humanoid.rightHandT, 3, writer);
    }
    if (humanoid.hasHandQ) {
        writeTransformCurves(humanoid.leftHandQ, 4, writer);
        writeTransformCurves(humanoid.rightHandQ, 4, writer);
    }

    for (const EventMarker& marker : clip.markers) {
        writer.writeF32(marker.normalizedTime);
        writer.writeU64(marker.name);
    }
    writeProperties(clip, writer);
    return true;
}

std::unique_ptr<Clip> readClip(Reader& reader, std::string* error) {
    const FileHeader header = reader.readFileHeader();
    if (header.magic != kMagicAB1) {
        if (error != nullptr) *error = "invalid .ab magic";
        return nullptr;
    }
    if (header.version < 2 || header.version > kClipIOVersion) {
        if (error != nullptr) *error = "unsupported .ab version";
        return nullptr;
    }

    const uint32_t flags = reader.readU32();
    const float length = reader.readF32();
    const float averageSpeed = reader.readF32();
    const uint32_t stringCount = reader.readU32();
    const uint32_t boneCount = reader.readU32();
    const uint32_t channelCount = reader.readU32();
    const uint32_t muscleCount = reader.readU32();
    const uint32_t markerCount = reader.readU32();
    const uint32_t propertyCount = header.version >= 3 ? reader.readU32() : 0;
    (void)boneCount;
    (void)stringCount;

    const auto readMarkers = [&](Clip& clip) {
        clip.markers.resize(markerCount);
        for (EventMarker& marker : clip.markers) {
            marker.normalizedTime = reader.readF32();
            marker.name = reader.readU64();
        }
        readProperties(clip, propertyCount, reader);
    };

    if (header.type == static_cast<uint16_t>(ClipType::BoneLocal)) {
        auto clip = std::make_unique<BoneLocalClip>();
        clip->length = length;
        clip->averageSpeed = averageSpeed;
        clip->looping = (flags & kFlagLooping) != 0;
        clip->channels.resize(channelCount);

        for (BoneChannel& channel : clip->channels) {
            channel.boneIndex = reader.readI32();
            const uint8_t mask = reader.readU8();
            channel.hasPosition = (mask & kBonePosition) != 0;
            channel.hasRotation = (mask & kBoneRotation) != 0;
            channel.hasScale = (mask & kBoneScale) != 0;

            if (channel.hasPosition) readTransformCurves(channel.position, 3, reader);
            if (channel.hasRotation) readTransformCurves(channel.rotation, 4, reader);
            if (channel.hasScale) readTransformCurves(channel.scale, 3, reader);
        }

        readMarkers(*clip);
        return clip;
    }

    if (header.type != static_cast<uint16_t>(ClipType::HumanoidMuscle)) {
        if (error != nullptr) *error = "unsupported clip type";
        return nullptr;
    }

    auto clip = std::make_unique<HumanoidClip>();
    clip->length = length;
    clip->averageSpeed = averageSpeed;
    clip->looping = (flags & kFlagLooping) != 0;
    clip->hasRootT = (flags & kFlagHasRootT) != 0;
    clip->hasRootQ = (flags & kFlagHasRootQ) != 0;
    clip->hasFootT = (flags & kFlagHasFootT) != 0;
    clip->hasFootQ = (flags & kFlagHasFootQ) != 0;
    clip->hasHandT = (flags & kFlagHasHandT) != 0;
    clip->hasHandQ = (flags & kFlagHasHandQ) != 0;
    (void)muscleCount;

    readTransformCurves(clip->rootT, 3, reader);
    readTransformCurves(clip->rootQ, 4, reader);
    clip->muscles.resize(kMuscleCount);
    for (int i = 0; i < kMuscleCount; ++i) clip->muscles[i] = readCurve(reader);

    if (clip->hasFootT) {
        readTransformCurves(clip->leftFootT, 3, reader);
        readTransformCurves(clip->rightFootT, 3, reader);
    }
    if (clip->hasFootQ) {
        readTransformCurves(clip->leftFootQ, 4, reader);
        readTransformCurves(clip->rightFootQ, 4, reader);
    }
    if (clip->hasHandT) {
        readTransformCurves(clip->leftHandT, 3, reader);
        readTransformCurves(clip->rightHandT, 3, reader);
    }
    if (clip->hasHandQ) {
        readTransformCurves(clip->leftHandQ, 4, reader);
        readTransformCurves(clip->rightHandQ, 4, reader);
    }

    readMarkers(*clip);
    return clip;
}

std::vector<uint8_t> encodeClip(const Clip& clip, std::string* error) {
    Writer writer;
    if (!writeClip(clip, writer, error)) return {};
    return writer.finish();
}

std::unique_ptr<Clip> decodeClip(Span<const uint8_t> data, std::string* error) {
    try {
        Reader reader(data);
        return readClip(reader, error);
    } catch (const std::exception& exception) {
        if (error != nullptr) *error = exception.what();
        return nullptr;
    }
}

std::unique_ptr<Clip> decodeClip(const std::vector<uint8_t>& data, std::string* error) {
    return decodeClip(Span<const uint8_t>(data.data(), data.size()), error);
}

}  // namespace anim
