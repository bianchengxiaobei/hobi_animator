#include "anim/integration/AnimationClipLibrary.h"

#include <cstdint>
#include <fstream>
#include <iterator>
#include <utility>

#include "anim/clip/ClipIO.h"

namespace anim {

bool AnimationClipLibrary::load(const AnimationSet& set, std::string& error) {
    error.clear();

    std::vector<Entry> entries;
    entries.reserve(set.clips.size());

    for (const ClipAssetInfo& info : set.clips) {
        for (const Entry& existing : entries) {
            if (existing.info.name == info.name) {
                error = "duplicate animation clip name '" + info.name + "'";
                return false;
            }
        }

        std::ifstream file(info.file, std::ios::binary);
        if (!file) {
            error = "cannot open animation clip '" + info.name + "' from '" + info.file + "'";
            return false;
        }

        const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                         std::istreambuf_iterator<char>());
        std::string decodeError;
        std::unique_ptr<Clip> clip = decodeClip(bytes, &decodeError);
        if (clip == nullptr) {
            error = "cannot decode animation clip '" + info.name + "' from '" + info.file + "'";
            if (!decodeError.empty()) error += ": " + decodeError;
            return false;
        }

        clip->looping = info.looping;
        entries.push_back(Entry{info, std::move(clip)});
    }

    _entries = std::move(entries);
    return true;
}

void AnimationClipLibrary::clear() noexcept { _entries.clear(); }

const Clip* AnimationClipLibrary::clip(int index) const noexcept {
    if (index < 0 || index >= count()) return nullptr;
    return _entries[static_cast<std::size_t>(index)].clip.get();
}

const ClipAssetInfo* AnimationClipLibrary::info(int index) const noexcept {
    if (index < 0 || index >= count()) return nullptr;
    return &_entries[static_cast<std::size_t>(index)].info;
}

const Clip* AnimationClipLibrary::find(std::string_view name) const noexcept {
    for (const Entry& entry : _entries) {
        if (entry.info.name == name) return entry.clip.get();
    }
    return nullptr;
}

const ClipAssetInfo* AnimationClipLibrary::findInfo(std::string_view name) const noexcept {
    for (const Entry& entry : _entries) {
        if (entry.info.name == name) return &entry.info;
    }
    return nullptr;
}

}

