#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "AbWriter.h"
#include "AnimConverter.h"
#include "AvatarLoader.h"
#include "FbxMetaParser.h"
#include "SkeletonLoader.h"
#include "anim/clip/ClipIO.h"

namespace {

struct Options {
    std::string input;
    std::string output;
    std::string batch;
    std::string outDir;
    std::string skeleton;
    std::string avatar;
    std::string fbxMeta;
    std::string emitAvatar;
    bool verify = true;
};

int usage() {
    std::cerr << "usage:\n"
              << "  anim_converter --input clip.anim --output clip.ab [--skeleton skeleton.json]"
                 " [--avatar avatar.json]\n"
              << "  anim_converter --batch clips/ --out-dir out/ [--skeleton skeleton.json]"
                 " [--avatar avatar.json]\n"
              << "  anim_converter --fbx-meta model.fbx.meta [--emit-avatar avatar.json]\n";
    return 2;
}

bool writeAvatarDraft(const anim::converter::FbxHumanMap& map, const std::string& path,
                      const std::string& name) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;

    out << "{\n  \"name\": \"" << name << "\",\n  \"hipsHeight\": 0,\n  \"bones\": {\n";
    for (std::size_t i = 0; i < map.entries.size(); ++i) {
        out << "    \"" << map.entries[i].second << "\": \"" << map.entries[i].first
            << "\"";
        if (i + 1 < map.entries.size()) out << ",";
        out << "\n";
    }
    out << "  },\n  \"limitsOverride\": {}\n}\n";
    return static_cast<bool>(out);
}

bool parseArgs(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto next = [&](std::string& value) {
            if (i + 1 >= argc) return false;
            value = argv[++i];
            return true;
        };

        if (arg == "--input") {
            if (!next(options.input)) return false;
        } else if (arg == "--output") {
            if (!next(options.output)) return false;
        } else if (arg == "--batch") {
            if (!next(options.batch)) return false;
        } else if (arg == "--out-dir") {
            if (!next(options.outDir)) return false;
        } else if (arg == "--skeleton") {
            if (!next(options.skeleton)) return false;
        } else if (arg == "--avatar") {
            if (!next(options.avatar)) return false;
        } else if (arg == "--fbx-meta") {
            if (!next(options.fbxMeta)) return false;
        } else if (arg == "--emit-avatar") {
            if (!next(options.emitAvatar)) return false;
        } else if (arg == "--no-verify") {
            options.verify = false;
        } else {
            return false;
        }
    }
    return true;
}

bool verifyClip(const std::string& path, const anim::Clip& original) {
    const std::vector<uint8_t> bytes = [&] {
        std::ifstream file(path, std::ios::binary);
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                                  std::istreambuf_iterator<char>());
        return data;
    }();

    std::string error;
    const auto loaded = anim::decodeClip(bytes, &error);
    if (!loaded) {
        std::cerr << "verify failed: " << error << "\n";
        return false;
    }

    if (loaded->type != original.type ||
        std::abs(loaded->length - original.length) > 1e-6f) {
        std::cerr << "verify failed: header mismatch\n";
        return false;
    }

    return true;
}

int convertOne(const std::string& input, const std::string& output,
               const anim::converter::ConvertOptions& convertOptions, bool verify) {
    const anim::converter::ConvertResult result =
        anim::converter::convertFile(input, convertOptions);

    for (const std::string& warning : result.warnings) {
        std::cerr << "warning: " << warning << "\n";
    }
    if (!result.ok()) {
        std::cerr << input << ": " << result.error << "\n";
        return 1;
    }

    std::string error;
    if (!anim::converter::writeAbFile(*result.clip, output, error)) {
        std::cerr << output << ": " << error << "\n";
        return 1;
    }

    if (verify && !verifyClip(output, *result.clip)) return 1;
    std::cout << input << " -> " << output << "\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parseArgs(argc, argv, options)) return usage();

    if (!options.fbxMeta.empty()) {
        anim::converter::FbxHumanMap map;
        std::string error;
        if (!anim::converter::parseFbxMetaFile(options.fbxMeta, map, error)) {
            std::cerr << error << "\n";
            return 1;
        }
        for (const auto& entry : map.entries) {
            std::cout << entry.first << " -> " << entry.second << "\n";
        }
        if (!options.emitAvatar.empty()) {
            std::string name = std::filesystem::path(options.fbxMeta).filename().string();
            const auto stripSuffix = [&](const std::string& suffix) {
                if (name.size() > suffix.size() &&
                    name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
                    name.resize(name.size() - suffix.size());
                    return true;
                }
                return false;
            };
            stripSuffix(".meta");
            stripSuffix(".fbx");
            stripSuffix(".FBX");
            if (!writeAvatarDraft(map, options.emitAvatar, name)) {
                std::cerr << "cannot write avatar draft: " << options.emitAvatar << "\n";
                return 1;
            }
        }
        if (options.input.empty() && options.batch.empty()) return 0;
    }

    anim::converter::SkeletonAsset skeleton;
    if (!options.skeleton.empty()) {
        std::string error;
        if (!anim::converter::loadSkeletonFile(options.skeleton, skeleton, error)) {
            std::cerr << error << "\n";
            return 1;
        }
    }

    anim::converter::AvatarConfig avatar;
    if (!options.avatar.empty()) {
        std::string error;
        if (!anim::converter::loadAvatarFile(options.avatar, avatar, error)) {
            std::cerr << error << "\n";
            return 1;
        }
    }

    anim::converter::ConvertOptions convertOptions;
    convertOptions.skeleton = options.skeleton.empty() ? nullptr : &skeleton;
    convertOptions.avatar = options.avatar.empty() ? nullptr : &avatar;

    if (!options.batch.empty()) {
        if (options.outDir.empty()) return usage();

        std::error_code errorCode;
        std::filesystem::create_directories(options.outDir, errorCode);
        if (errorCode) {
            std::cerr << "cannot create output directory: " << errorCode.message() << "\n";
            return 1;
        }

        int failures = 0;
        for (const auto& entry :
             std::filesystem::recursive_directory_iterator(options.batch)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".anim") continue;
            const std::string output =
                (std::filesystem::path(options.outDir) / entry.path().stem()).string() + ".ab";
            failures += convertOne(entry.path().string(), output, convertOptions, options.verify);
        }
        return failures == 0 ? 0 : 1;
    }

    if (options.input.empty() || options.output.empty()) return usage();
    return convertOne(options.input, options.output, convertOptions, options.verify);
}
