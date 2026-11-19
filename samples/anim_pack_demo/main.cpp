#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "SkeletonLoader.h"
#include "anim/animancer/AnimancerAnimator.h"
#include "anim/animancer/AnimancerPlayable.h"
#include "anim/animancer/AnimancerState.h"
#include "anim/animancer/Transition.h"
#include "anim/animancer/rootmotion/Targets.h"
#include "anim/clip/ClipIO.h"
#include "anim/integration/AnimationClipLibrary.h"
#include "anim/integration/AnimationManifest.h"
#include "anim/integration/AnimationTransitions.h"

namespace {

struct Options {
    std::string skeleton;
    std::string clip;
    std::string manifest;
    std::string clipName;
    float seconds = 1.0f;
    float deltaTime = 1.0f / 30.0f;
    int samples = 4;
};

int usage() {
    std::cerr
        << "usage:\n"
        << "  anim_pack_demo --skeleton character.skeleton.json --clip Walk.ab\n"
        << "                 [--seconds 1] [--dt 0.0333333] [--samples 4]\n"
        << "  anim_pack_demo --manifest anim_manifest.json [--clip-name Walk]\n"
        << "                 [--seconds 1] [--dt 0.0333333] [--samples 4]\n";
    return 2;
}

bool parseArgs(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto next = [&](std::string& value) {
            if (i + 1 >= argc) return false;
            value = argv[++i];
            return true;
        };

        if (arg == "--skeleton") {
            if (!next(options.skeleton)) return false;
        } else if (arg == "--clip") {
            if (!next(options.clip)) return false;
        } else if (arg == "--manifest") {
            if (!next(options.manifest)) return false;
        } else if (arg == "--clip-name") {
            if (!next(options.clipName)) return false;
        } else if (arg == "--seconds") {
            std::string value;
            if (!next(value)) return false;
            options.seconds = std::stof(value);
        } else if (arg == "--dt") {
            std::string value;
            if (!next(value)) return false;
            options.deltaTime = std::stof(value);
        } else if (arg == "--samples") {
            std::string value;
            if (!next(value)) return false;
            options.samples = std::stoi(value);
        } else {
            return false;
        }
    }

    const bool direct = !options.skeleton.empty() && !options.clip.empty();
    const bool manifest = !options.manifest.empty();
    return (direct || manifest) && !(direct && manifest) && options.seconds > 0.0f &&
           options.deltaTime > 0.0f && options.samples > 0;
}

std::unique_ptr<anim::Clip> loadClip(const std::string& path, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "cannot open " + path;
        return nullptr;
    }
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                     std::istreambuf_iterator<char>());
    return anim::decodeClip(bytes, &error);
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parseArgs(argc, argv, options)) return usage();

    std::string error;
    anim::converter::SkeletonAsset skeleton;
    anim::AnimationSet manifest;
    anim::AnimationClipLibrary library;
    anim::AnimationTransitions transitions;
    std::unique_ptr<anim::Clip> standaloneClip;
    anim::ClipTransition standaloneTransition;
    std::string activeName = options.clipName;
    const anim::ClipAssetInfo* activeInfo = nullptr;

    if (!options.manifest.empty()) {
        if (!anim::loadAnimationManifestFile(options.manifest, manifest, error)) {
            std::cerr << error << "\n";
            return 1;
        }
        if (manifest.clips.empty()) {
            std::cerr << "animation manifest has no clips\n";
            return 1;
        }
        if (!anim::converter::loadSkeletonFile(manifest.skeletonFile, skeleton, error)) {
            std::cerr << error << "\n";
            return 1;
        }
        if (!library.load(manifest, error)) {
            std::cerr << error << "\n";
            return 1;
        }

        anim::EventCallbackMap callbacks;
        for (const anim::ClipAssetInfo& clip : manifest.clips) {
            for (const anim::ClipEventInfo& event : clip.events) {
                if (callbacks.find(event.name) != callbacks.end()) continue;
                const std::string name = event.name;
                callbacks.emplace(name, [name] { std::cout << "event: " << name << "\n"; });
            }
        }
        if (!transitions.build(library, callbacks, error)) {
            std::cerr << error << "\n";
            return 1;
        }

        if (activeName.empty()) activeName = manifest.clips.front().name;
        activeInfo = transitions.info(activeName);
        if (activeInfo == nullptr) {
            std::cerr << "animation manifest has no clip named '" << activeName << "'\n";
            return 1;
        }
    } else {
        if (!anim::converter::loadSkeletonFile(options.skeleton, skeleton, error)) {
            std::cerr << error << "\n";
            return 1;
        }

        standaloneClip = loadClip(options.clip, error);
        if (standaloneClip == nullptr) {
            std::cerr << error << "\n";
            return 1;
        }
        if (standaloneClip->type != anim::ClipType::BoneLocal) {
            std::cerr << "anim_pack_demo requires a BoneLocal clip\n";
            return 1;
        }

        activeName = options.clip;
        standaloneTransition.clip = standaloneClip.get();
        standaloneTransition.fadeDurationValue = 0.0f;
    }

    anim::Transform character;
    anim::RootMotionToTransform rootTarget(&character);
    anim::AnimancerAnimator animator;
    animator.initialize(skeleton.skeleton);
    animator.playable().setRootMotionTarget(&rootTarget);

    anim::AnimancerState* state = nullptr;
    if (!options.manifest.empty()) {
        state = transitions.play(animator, activeName, &error);
    } else {
        state = animator.play(standaloneTransition);
        animator.playable().setRootMotionRootBone(0);
    }
    if (state == nullptr) {
        std::cerr << (error.empty() ? "failed to create animation state" : error) << "\n";
        return 1;
    }

    std::cout << "playing " << activeName << " with " << skeleton.boneNames.size()
              << " bones";
    if (activeInfo != nullptr) {
        std::cout << ", rootBone="
                  << (activeInfo->rootBone.empty() ? "<none>" : activeInfo->rootBone);
    }
    std::cout << "\n";

    float elapsed = 0.0f;
    float nextSample = options.seconds / static_cast<float>(options.samples);
    int sample = 1;
    while (elapsed < options.seconds) {
        const float step = std::min(options.deltaTime, options.seconds - elapsed);
        animator.update(step);
        elapsed += step;

        if (elapsed + 1e-6f >= nextSample) {
            const anim::Pose& pose = animator.playable().outputPose();
            const anim::Transform& root = pose.local[0];
            std::cout << "  sample " << sample << ": time=" << state->time()
                      << " root=(" << root.translation.x << ", "
                      << root.translation.y << ", " << root.translation.z << ")\n";
            ++sample;
            nextSample += options.seconds / static_cast<float>(options.samples);
        }
    }

    const anim::RootMotionDelta rootMotion = animator.consumeRootMotion();
    std::cout << "root motion: (" << rootMotion.deltaPosition.x << ", "
              << rootMotion.deltaPosition.y << ", " << rootMotion.deltaPosition.z
              << "), hasMotion=" << (rootMotion.hasMotion ? "true" : "false")
              << ", character=(" << character.translation.x << ", "
              << character.translation.y << ", " << character.translation.z << ")\n";
    return 0;
}
