#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "anim/animancer/AnimancerAnimator.h"
#include "anim/animancer/AnimancerPlayable.h"
#include "anim/clip/Clip.h"
#include "anim/jobs/ThreadPoolJobScheduler.h"
#include "anim/skeleton/Skeleton.h"

namespace {

anim::Skeleton makeSkeleton(int bones) {
    anim::Skeleton skeleton;
    for (int i = 0; i < bones; ++i) {
        anim::Transform rest;
        rest.translation = anim::Vec3(0.0f, i == 0 ? 0.0f : 0.25f, 0.0f);
        skeleton.addBone(anim::hashString("Bone" + std::to_string(i)), i - 1, rest);
    }
    return skeleton;
}

anim::BoneLocalClip makeClip(int bones, float length) {
    anim::BoneLocalClip clip;
    clip.length = length;
    clip.looping = true;

    for (int i = 0; i < bones; ++i) {
        anim::BoneChannel channel;
        channel.boneIndex = i;
        channel.hasRotation = 1;
        const float phase = static_cast<float>(i) * 0.15f;
        channel.rotation[0].keys = {
            anim::Keyframe{0.0f, 0.0f, 0.0f, 0.0f},
            anim::Keyframe{length * 0.5f, std::sin(phase) * 0.3f, 0.4f, 0.4f},
            anim::Keyframe{length, 0.0f, 0.0f, 0.0f},
        };
        channel.rotation[3].keys = {
            anim::Keyframe{0.0f, 1.0f, 0.0f, 0.0f},
            anim::Keyframe{length * 0.5f, 1.0f, 0.0f, 0.0f},
            anim::Keyframe{length, 1.0f, 0.0f, 0.0f},
        };
        clip.channels.push_back(channel);
    }
    return clip;
}

}  // namespace

int main(int argc, char** argv) {
    int characters = 200;
    int bones = 64;
    int frames = 300;
    bool parallel = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto next = [&](int& value) {
            if (i + 1 >= argc) return;
            value = std::stoi(argv[++i]);
        };
        if (arg == "--characters") next(characters);
        else if (arg == "--bones") next(bones);
        else if (arg == "--frames") next(frames);
        else if (arg == "--parallel") parallel = true;
    }

    anim::Skeleton skeleton = makeSkeleton(bones);
    anim::BoneLocalClip clip = makeClip(bones, 1.0f);

    std::vector<std::unique_ptr<anim::AnimancerAnimator>> animators;
    animators.reserve(static_cast<std::size_t>(characters));
    for (int i = 0; i < characters; ++i) {
        auto animator = std::make_unique<anim::AnimancerAnimator>();
        animator->initialize(skeleton);
        animator->playable().defaultFadeDuration = 0.0f;
        animator->play(clip);
        animators.push_back(std::move(animator));
    }

    std::unique_ptr<anim::ThreadPoolJobScheduler> scheduler;
    if (parallel) scheduler = std::make_unique<anim::ThreadPoolJobScheduler>();

    const float dt = 1.0f / 60.0f;
    const auto start = std::chrono::high_resolution_clock::now();
    for (int frame = 0; frame < frames; ++frame) {
        if (parallel) {
            scheduler->parallelFor(animators.size(), [&](std::size_t begin, std::size_t end) {
                for (std::size_t i = begin; i < end; ++i) animators[i]->update(dt);
            });
        } else {
            for (auto& animator : animators) animator->update(dt);
        }
    }
    const auto end = std::chrono::high_resolution_clock::now();

    const double seconds = std::chrono::duration<double>(end - start).count();
    const double msPerFrame = seconds * 1000.0 / frames;
    const double usPerCharacter = msPerFrame * 1000.0 / characters;
    const double fps = 1.0 / (seconds / frames);

    std::cout << "characters=" << characters << " bones=" << bones << " frames=" << frames
              << " mode=" << (parallel ? "parallel" : "serial") << "\n";
    std::cout << "total=" << seconds << "s  ms/frame=" << msPerFrame
              << "  us/character=" << usPerCharacter << "  fps=" << fps << "\n";
    return 0;
}
