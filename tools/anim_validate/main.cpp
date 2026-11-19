#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "SkeletonLoader.h"
#include "Validator.h"
#include "anim/integration/AnimationManifest.h"
#include "anim/integration/JsonWriter.h"

namespace {

namespace fs = std::filesystem;

struct Options {
    std::string manifest;
    std::string skeleton;
    std::string assetRoot;
    std::string json;
    std::vector<std::string> abFiles;
    bool strict = false;
    bool quiet = false;
    bool help = false;
};

void printUsage(std::ostream& out) {
    out << "usage:\n"
        << "  anim_validate --manifest anim_manifest.json [--skeleton character.skeleton.json]\n"
        << "                [--asset-root dir] [--json report.json] [--strict] [--quiet]\n"
        << "  anim_validate --ab Walk.ab [--ab Run.ab ...] [--skeleton character.skeleton.json]\n"
        << "                [--json report.json] [--strict] [--quiet]\n";
}

bool parseArgs(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto next = [&](std::string& value) {
            if (i + 1 >= argc) return false;
            value = argv[++i];
            return true;
        };

        if (arg == "--manifest") {
            if (!next(options.manifest)) return false;
        } else if (arg == "--skeleton") {
            if (!next(options.skeleton)) return false;
        } else if (arg == "--asset-root") {
            if (!next(options.assetRoot)) return false;
        } else if (arg == "--json") {
            if (!next(options.json)) return false;
        } else if (arg == "--ab") {
            std::string value;
            if (!next(value)) return false;
            options.abFiles.push_back(std::move(value));
        } else if (arg == "--strict") {
            options.strict = true;
        } else if (arg == "--quiet") {
            options.quiet = true;
        } else if (arg == "--help" || arg == "-h") {
            options.help = true;
        } else {
            return false;
        }
    }
    return true;
}

bool readTextFile(const fs::path& path, std::string& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

bool readBytes(const fs::path& path, std::vector<uint8_t>& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

std::string resolvePath(const std::string& path, const fs::path& base) {
    if (path.empty()) return {};
    fs::path candidate(path);
    if (candidate.is_relative()) candidate = base / candidate;
    return candidate.lexically_normal().string();
}

bool nearly(float a, float b) { return std::abs(a - b) <= 1e-5f; }

bool sameSkeleton(const anim::Skeleton& a, const anim::Skeleton& b) {
    if (a.boneCount() != b.boneCount()) return false;
    for (int i = 0; i < a.boneCount(); ++i) {
        const anim::Bone& left = a.bones[static_cast<std::size_t>(i)];
        const anim::Bone& right = b.bones[static_cast<std::size_t>(i)];
        if (left.name != right.name || left.parent != right.parent) return false;
        if (!nearly(left.restLocal.translation.x, right.restLocal.translation.x) ||
            !nearly(left.restLocal.translation.y, right.restLocal.translation.y) ||
            !nearly(left.restLocal.translation.z, right.restLocal.translation.z)) {
            return false;
        }
        if (!nearly(left.restLocal.rotation.x, right.restLocal.rotation.x) ||
            !nearly(left.restLocal.rotation.y, right.restLocal.rotation.y) ||
            !nearly(left.restLocal.rotation.z, right.restLocal.rotation.z) ||
            !nearly(left.restLocal.rotation.w, right.restLocal.rotation.w)) {
            return false;
        }
        if (!nearly(left.restLocal.scale.x, right.restLocal.scale.x) ||
            !nearly(left.restLocal.scale.y, right.restLocal.scale.y) ||
            !nearly(left.restLocal.scale.z, right.restLocal.scale.z)) {
            return false;
        }
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                if (!nearly(left.inverseBind[column][row], right.inverseBind[column][row])) {
                    return false;
                }
            }
        }
    }
    return true;
}

const char* severityName(anim::validate::Severity severity) {
    switch (severity) {
        case anim::validate::Severity::Error:
            return "error";
        case anim::validate::Severity::Warning:
            return "warning";
        case anim::validate::Severity::Info:
            return "info";
    }
    return "error";
}

void recordClipStats(anim::validate::Report& report, const std::string& name,
                     const anim::Clip& clip, std::size_t events) {
    anim::validate::ClipStats stats;
    stats.length = clip.length;
    stats.markers = clip.markers.size();
    stats.events = events;
    if (const auto* boneLocal = dynamic_cast<const anim::BoneLocalClip*>(&clip)) {
        stats.channels = boneLocal->channels.size();
    }
    report.clipStats[name] = stats;
}

void printIssues(const anim::validate::Report& report) {
    for (const anim::validate::Issue& issue : report.sortedIssues()) {
        std::cout << severityName(issue.severity) << " [" << issue.code << "]";
        if (!issue.file.empty()) std::cout << " " << issue.file;
        if (!issue.clip.empty()) std::cout << " clip=" << issue.clip;
        if (!issue.path.empty()) std::cout << " " << issue.path;
        if (!issue.message.empty()) std::cout << ": " << issue.message;
        std::cout << "\n";
    }
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parseArgs(argc, argv, options)) {
        printUsage(std::cerr);
        return 2;
    }
    if (options.help) {
        printUsage(std::cout);
        return 0;
    }
    if (options.manifest.empty() && options.abFiles.empty()) {
        printUsage(std::cerr);
        return 2;
    }

    anim::validate::Report report;
    std::set<std::string> files;
    bool ioFailure = false;

    anim::converter::SkeletonAsset skeleton;
    bool hasSkeleton = false;
    std::string skeletonDisplay;
    std::string skeletonPath;

    anim::AnimationSet manifest;
    bool manifestLoaded = false;
    fs::path assetBase = options.assetRoot.empty() ? fs::path(".") : fs::path(options.assetRoot);
    std::string manifestDisplay;

    if (!options.manifest.empty()) {
        manifestDisplay = options.manifest;
        files.insert(manifestDisplay);
        std::string manifestText;
        if (!readTextFile(options.manifest, manifestText)) {
            if (!options.quiet) std::cerr << "cannot open '" << options.manifest << "'\n";
            return 2;
        }

        if (options.assetRoot.empty()) {
            const fs::path parent = fs::path(options.manifest).parent_path();
            assetBase = parent.empty() ? fs::path(".") : parent;
        }

        std::string error;
        if (!anim::loadAnimationManifest(manifestText, manifest, error)) {
            report.add(anim::validate::Severity::Error, "manifest.parse", manifestDisplay, {},
                       {}, error);
        } else {
            manifestLoaded = true;
        }
    }

    if (!options.skeleton.empty()) {
        skeletonDisplay = options.skeleton;
        skeletonPath = options.assetRoot.empty()
                           ? options.skeleton
                           : resolvePath(options.skeleton, assetBase);
    } else if (manifestLoaded) {
        skeletonDisplay = manifest.skeletonFile;
        skeletonPath = resolvePath(manifest.skeletonFile, assetBase);
    }

    if (!skeletonPath.empty()) {
        std::string error;
        if (!anim::converter::loadSkeletonFile(skeletonPath, skeleton, error)) {
            report.add(anim::validate::Severity::Error, "skeleton.load", skeletonDisplay, {},
                       {}, error);
            ioFailure = true;
        } else {
            hasSkeleton = true;
            files.insert(skeletonDisplay);
            anim::validate::validateSkeleton(skeleton.skeleton, skeletonDisplay,
                                             &skeleton.boneNames, report);
        }
    }

    anim::validate::ManifestValidationOptions manifestOptions;
    manifestOptions.file = manifestDisplay;
    manifestOptions.skeleton = hasSkeleton ? &skeleton.skeleton : nullptr;
    manifestOptions.boneNames = hasSkeleton ? &skeleton.boneNames : nullptr;

    if (manifestLoaded) {
        anim::validate::validateManifest(manifest, manifestOptions, report);

        if (!options.skeleton.empty() && !manifest.skeletonFile.empty()) {
            const std::string manifestSkeletonPath =
                resolvePath(manifest.skeletonFile, assetBase);
            std::error_code ec;
            const bool samePath = fs::equivalent(skeletonPath, manifestSkeletonPath, ec) && !ec;
            if (!samePath) {
                anim::converter::SkeletonAsset manifestSkeleton;
                std::string error;
                if (anim::converter::loadSkeletonFile(manifestSkeletonPath, manifestSkeleton,
                                                      error)) {
                    if (!sameSkeleton(skeleton.skeleton, manifestSkeleton.skeleton)) {
                        report.add(anim::validate::Severity::Error,
                                   "manifest.skeleton_mismatch", manifestDisplay, {},
                                   "skeleton",
                                   "skeleton loaded from --skeleton does not match manifest "
                                   "skeleton");
                    }
                } else {
                    report.add(anim::validate::Severity::Warning,
                               "manifest.skeleton_unreadable", manifestDisplay, {},
                               "skeleton", error);
                }
            }
        }

        for (std::size_t i = 0; i < manifest.clips.size(); ++i) {
            const anim::ClipAssetInfo& info = manifest.clips[i];
            const std::string displayFile = info.file;
            files.insert(displayFile);
            ++report.clipCount;

            const std::string path = resolvePath(info.file, assetBase);
            std::vector<uint8_t> bytes;
            if (!readBytes(path, bytes)) {
                report.add(anim::validate::Severity::Error, "manifest.file_missing",
                           displayFile, info.name, "file",
                           "cannot open clip file '" + path + "'");
                continue;
            }

            anim::validate::ClipValidationOptions clipOptions;
            clipOptions.file = displayFile;
            clipOptions.clipName = info.name;
            clipOptions.skeleton = hasSkeleton ? &skeleton.skeleton : nullptr;
            std::unique_ptr<anim::Clip> clip =
                anim::validate::validateClipBytes(bytes, clipOptions, report);
            if (clip != nullptr) {
                recordClipStats(report, info.name, *clip, info.events.size());
                anim::validate::validateClipManifest(*clip, info, i, manifestOptions, report);
            }
        }
    }

    for (const std::string& abFile : options.abFiles) {
        files.insert(abFile);
        ++report.clipCount;

        std::vector<uint8_t> bytes;
        if (!readBytes(abFile, bytes)) {
            report.add(anim::validate::Severity::Error, "ab.io", abFile, {}, {},
                       "cannot open .ab file");
            ioFailure = true;
            continue;
        }

        anim::validate::ClipValidationOptions clipOptions;
        clipOptions.file = abFile;
        clipOptions.clipName = fs::path(abFile).filename().string();
        clipOptions.skeleton = hasSkeleton ? &skeleton.skeleton : nullptr;
        std::unique_ptr<anim::Clip> clip =
            anim::validate::validateClipBytes(bytes, clipOptions, report);
        if (clip != nullptr) {
            recordClipStats(report, clipOptions.clipName, *clip, clip->markers.size());
        }
    }

    report.fileCount = files.size();

    if (!options.json.empty()) {
        std::string error;
        if (!anim::json::writeFile(options.json, report.toJson(options.strict), error)) {
            if (!options.quiet) std::cerr << error << "\n";
            return 2;
        }
    }

    if (!options.quiet) {
        std::cout << "anim_validate: " << report.count(anim::validate::Severity::Error)
                  << " errors, " << report.count(anim::validate::Severity::Warning)
                  << " warnings, " << report.clipCount << " clips\n";
        printIssues(report);
    }

    if (ioFailure) return 2;
    if (report.hasErrors()) return 1;
    if (options.strict && report.hasWarnings()) return 1;
    return 0;
}
