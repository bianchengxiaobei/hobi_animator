#include <bgfx/bgfx.h>
#include <bx/math.h>
#include <windows.h>

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "anim/avatar/HumanoidAvatar.h"
#include "anim/avatar/HumanoidIK.h"
#include "anim/avatar/MuscleSpace.h"
#include "anim/clip/Clip.h"
#include "anim/clip/ClipIO.h"
#include "anim/core/Transform.h"
#include "anim/skeleton/Palette.h"
#include "anim/skeleton/Pose.h"
#include "anim/skeleton/Skeleton.h"

#include "AvatarBuilder.h"
#include "AvatarLoader.h"
#include "SkeletonLoader.h"

namespace {

struct Options {
    std::string skeleton = std::string(ANIM_RETARGET_DATA_DIR) + "/DefaultHumanoid.skeleton.json";
    std::string avatar = std::string(ANIM_RETARGET_DATA_DIR) + "/DefaultHumanoid.source.avatar.json";
    std::vector<std::string> clips;
    std::string shaders = "shaders";
    std::string screenshot;
    int width = 1280;
    int height = 720;
    int frames = 0;
    int clipFrames = 120;
    int screenshotFrame = 30;
    float scale = 1.35f;
    float goalTolerance = 0.05f;
    bool noop = false;
    bool headless = false;
    bool strict = false;
};

void printUsage() {
    std::cerr
        << "usage: humanoid_retarget_demo --clips walk.ab,run.ab,idle.ab [options]\n"
        << "  --skeleton file.json  source skeleton (default tests/golden/DefaultHumanoid.skeleton.json)\n"
        << "  --avatar file.json    source avatar   (default tests/golden/DefaultHumanoid.source.avatar.json)\n"
        << "  --scale value         target skeleton scale (default 1.35)\n"
        << "  --goal-tolerance m    strict endpoint error threshold (default 0.05 m)\n"
        << "  --clip-frames N       frames per clip (default 120)\n"
        << "  --screenshot-frame N  frame inside each clip to capture (default 30)\n"
        << "  --screenshot prefix   writes prefix-<clip>.ppm\n"
        << "  --frames N            total frames (default all clips)\n"
        << "  --shaders dir, --width W, --height H, --strict, --noop, --headless\n";
}

bool parseArgs(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto next = [&](std::string& value) {
            if (i + 1 >= argc) return false;
            value = argv[++i];
            return true;
        };
        const auto nextInt = [&](int& value) {
            std::string text;
            if (!next(text)) return false;
            value = std::stoi(text);
            return true;
        };
        const auto nextFloat = [&](float& value) {
            std::string text;
            if (!next(text)) return false;
            value = std::stof(text);
            return true;
        };

        if (arg == "--skeleton") {
            if (!next(options.skeleton)) return false;
        } else if (arg == "--avatar") {
            if (!next(options.avatar)) return false;
        } else if (arg == "--clips") {
            std::string list;
            if (!next(list)) return false;
            std::size_t start = 0;
            while (start <= list.size()) {
                const std::size_t comma = list.find(',', start);
                const std::string item =
                    list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
                if (!item.empty()) options.clips.push_back(item);
                if (comma == std::string::npos) break;
                start = comma + 1;
            }
        } else if (arg == "--clip") {
            std::string clip;
            if (!next(clip)) return false;
            options.clips.push_back(clip);
        } else if (arg == "--shaders") {
            if (!next(options.shaders)) return false;
        } else if (arg == "--screenshot") {
            if (!next(options.screenshot)) return false;
        } else if (arg == "--frames") {
            if (!nextInt(options.frames)) return false;
        } else if (arg == "--clip-frames") {
            if (!nextInt(options.clipFrames)) return false;
        } else if (arg == "--screenshot-frame") {
            if (!nextInt(options.screenshotFrame)) return false;
        } else if (arg == "--scale") {
            if (!nextFloat(options.scale)) return false;
        } else if (arg == "--goal-tolerance") {
            if (!nextFloat(options.goalTolerance)) return false;
        } else if (arg == "--strict") {
            options.strict = true;
        } else if (arg == "--noop") {
            options.noop = true;
        } else if (arg == "--headless") {
            options.headless = true;
            options.noop = true;
        } else if (arg == "--width") {
            if (!nextInt(options.width)) return false;
        } else if (arg == "--height") {
            if (!nextInt(options.height)) return false;
        } else {
            return false;
        }
    }

    return !options.clips.empty() && !options.skeleton.empty() && !options.avatar.empty() &&
           options.width > 0 && options.height > 0 && options.clipFrames > 0 &&
           options.screenshotFrame >= 0 && options.scale > 1e-4f &&
           options.goalTolerance > 0.0f;
}

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return {};
    const std::streamsize size = file.tellg();
    file.seekg(0);
    std::vector<uint8_t> data(static_cast<std::size_t>(size));
    if (size > 0) file.read(reinterpret_cast<char*>(data.data()), size);
    return file ? data : std::vector<uint8_t>{};
}

bgfx::ShaderHandle loadShader(const std::string& path) {
    const std::vector<uint8_t> data = readFile(path);
    if (data.empty()) return BGFX_INVALID_HANDLE;
    return bgfx::createShader(bgfx::copy(data.data(), static_cast<uint32_t>(data.size())));
}

bgfx::ProgramHandle loadProgram(const std::string& vertex, const std::string& fragment) {
    const bgfx::ShaderHandle vs = loadShader(vertex);
    const bgfx::ShaderHandle fs = loadShader(fragment);
    if (!bgfx::isValid(vs) || !bgfx::isValid(fs)) return BGFX_INVALID_HANDLE;
    return bgfx::createProgram(vs, fs, true);
}

struct DemoCallback final : bgfx::CallbackI {
    void fatal(const char* filePath, uint16_t line, bgfx::Fatal::Enum code,
               const char* message) override {
        std::fprintf(stderr, "bgfx fatal %s:%u code=%d: %s\n", filePath, line,
                     static_cast<int>(code), message ? message : "");
        std::fflush(stderr);
        std::abort();
    }

    void traceVargs(const char* filePath, uint16_t line, const char* format,
                    va_list args) override {
        std::fprintf(stderr, "%s (%u): ", filePath, line);
        std::vfprintf(stderr, format, args);
        std::fputc('\n', stderr);
    }

    void profilerBegin(const char*, uint32_t, const char*, uint16_t) override {}
    void profilerBeginLiteral(const char*, uint32_t, const char*, uint16_t) override {}
    void profilerEnd() override {}
    uint32_t cacheReadSize(uint64_t) override { return 0; }
    bool cacheRead(uint64_t, void*, uint32_t) override { return false; }
    void cacheWrite(uint64_t, const void*, uint32_t) override {}

    void screenShot(const char* filePath, uint32_t width, uint32_t height,
                    uint32_t pitch, const void* data, uint32_t, bool yflip) override {
        std::string path = filePath != nullptr ? filePath : "screenshot";
        if (path.size() < 4 || path.compare(path.size() - 4, 4, ".ppm") != 0) path += ".ppm";
        std::ofstream out(path, std::ios::binary);
        if (!out) return;
        out << "P6\n" << width << " " << height << "\n255\n";
        const auto* pixels = static_cast<const uint8_t*>(data);
        for (uint32_t row = 0; row < height; ++row) {
            const uint32_t sourceRow = yflip ? (height - 1 - row) : row;
            const uint8_t* source = pixels + sourceRow * pitch;
            for (uint32_t column = 0; column < width; ++column) {
                out.put(static_cast<char>(source[column * 4 + 2]));
                out.put(static_cast<char>(source[column * 4 + 1]));
                out.put(static_cast<char>(source[column * 4 + 0]));
            }
        }
    }

    void captureBegin(uint32_t, uint32_t, uint32_t, bgfx::TextureFormat::Enum, bool) override {}
    void captureEnd() override {}
    void captureFrame(const void*, uint32_t) override {}
};

DemoCallback g_callback;

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_SIZE:
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_KEYDOWN:
            if (wparam == VK_ESCAPE) PostQuitMessage(0);
            break;
        default:
            break;
    }
    return DefWindowProc(window, message, wparam, lparam);
}

struct DebugVertex {
    float position[3];
    uint8_t color[4];
};

uint32_t packColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8) |
           (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(a) << 24);
}

void addLine(std::vector<DebugVertex>& vertices, const anim::Vec3& from,
             const anim::Vec3& to, uint32_t color) {
    const uint8_t r = static_cast<uint8_t>(color & 0xff);
    const uint8_t g = static_cast<uint8_t>((color >> 8) & 0xff);
    const uint8_t b = static_cast<uint8_t>((color >> 16) & 0xff);
    const uint8_t a = static_cast<uint8_t>((color >> 24) & 0xff);
    for (const anim::Vec3& point : {from, to}) {
        DebugVertex vertex{};
        vertex.position[0] = point.x;
        vertex.position[1] = point.y;
        vertex.position[2] = point.z;
        vertex.color[0] = r;
        vertex.color[1] = g;
        vertex.color[2] = b;
        vertex.color[3] = a;
        vertices.push_back(vertex);
    }
}

void addCross(std::vector<DebugVertex>& vertices, const anim::Vec3& position, float size,
              uint32_t color) {
    addLine(vertices, position - anim::Vec3(size, 0.0f, 0.0f),
            position + anim::Vec3(size, 0.0f, 0.0f), color);
    addLine(vertices, position - anim::Vec3(0.0f, size, 0.0f),
            position + anim::Vec3(0.0f, size, 0.0f), color);
    addLine(vertices, position - anim::Vec3(0.0f, 0.0f, size),
            position + anim::Vec3(0.0f, 0.0f, size), color);
}

void addSkeletonLines(std::vector<DebugVertex>& vertices, const anim::Skeleton& skeleton,
                     const std::vector<anim::Mat4>& world, uint32_t color) {
    for (int i = 0; i < skeleton.boneCount(); ++i) {
        const int parent = skeleton.bone(i).parent;
        if (parent < 0) continue;
        const anim::Vec3 from = anim::Vec3(world[static_cast<std::size_t>(parent)][3]);
        const anim::Vec3 to = anim::Vec3(world[static_cast<std::size_t>(i)][3]);
        addLine(vertices, from, to, color);
    }
}

void addGroundGrid(std::vector<DebugVertex>& vertices, float halfExtent, int divisions,
                   uint32_t color) {
    for (int i = 0; i <= divisions; ++i) {
        const float t = -halfExtent + (2.0f * halfExtent * static_cast<float>(i)) /
                                           static_cast<float>(divisions);
        addLine(vertices, anim::Vec3(t, 0.0f, -halfExtent),
                anim::Vec3(t, 0.0f, halfExtent), color);
        addLine(vertices, anim::Vec3(-halfExtent, 0.0f, t),
                anim::Vec3(halfExtent, 0.0f, t), color);
    }
}

void scaleRig(anim::Skeleton& skeleton, anim::HumanoidAvatar& avatar, float scale) {
    for (anim::Bone& bone : skeleton.bones) bone.restLocal.translation *= scale;

    anim::Pose rest;
    rest.resetToRest(skeleton);
    std::vector<anim::Mat4> world;
    anim::computeWorldMatrices(skeleton, rest, world);
    for (std::size_t i = 0; i < skeleton.bones.size(); ++i) {
        skeleton.bones[i].inverseBind = glm::inverse(world[i]);
    }

    for (int i = 0; i < anim::kHumanBoneCount; ++i) {
        anim::Transform& restLocal = avatar.restLocal[static_cast<std::size_t>(i)];
        restLocal.translation *= scale;
        anim::HumanoidBoneAxes& axes = avatar.axes[static_cast<std::size_t>(i)];
        if (axes.valid) axes.length *= scale;
    }
    avatar.hipsHeight *= scale;
}

anim::Vec3 rootSpaceToWorld(const anim::Transform& root, const anim::Vec3& value,
                            float scale) {
    return root.translation + root.rotation * (value * scale);
}

std::string clipLabel(const std::string& path) {
    std::size_t begin = path.find_last_of("\\/");
    begin = begin == std::string::npos ? 0 : begin + 1;
    std::size_t end = path.find_last_of('.');
    if (end == std::string::npos || end < begin) end = path.size();
    return path.substr(begin, end - begin);
}

void drawDebugView(bgfx::ViewId view, const std::vector<DebugVertex>& vertices,
                   const bgfx::VertexLayout& layout, bgfx::ProgramHandle program) {
    if (vertices.empty()) return;
    if (bgfx::getAvailTransientVertexBuffer(static_cast<uint32_t>(vertices.size()), layout) <
        vertices.size()) {
        return;
    }
    bgfx::TransientVertexBuffer buffer;
    bgfx::allocTransientVertexBuffer(&buffer, static_cast<uint32_t>(vertices.size()), layout);
    std::memcpy(buffer.data, vertices.data(), vertices.size() * sizeof(DebugVertex));
    bgfx::setVertexBuffer(0, &buffer);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_PT_LINES |
                   BGFX_STATE_MSAA);
    bgfx::submit(view, program);
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parseArgs(argc, argv, options)) {
        printUsage();
        return 2;
    }

    anim::converter::SkeletonAsset skeletonAsset;
    std::string error;
    if (!anim::converter::loadSkeletonFile(options.skeleton, skeletonAsset, error)) {
        std::cerr << "load skeleton failed: " << error << "\n";
        return 1;
    }

    anim::converter::AvatarConfig avatarConfig;
    if (!anim::converter::loadAvatarFile(options.avatar, avatarConfig, error)) {
        std::cerr << "load avatar failed: " << error << "\n";
        return 1;
    }

    anim::HumanoidAvatar sourceAvatar;
    if (!anim::converter::buildHumanoidAvatar(skeletonAsset, avatarConfig, sourceAvatar, error)) {
        std::cerr << "build avatar failed: " << error << "\n";
        return 1;
    }

    anim::Skeleton sourceSkeleton = skeletonAsset.skeleton;
    anim::Skeleton targetSkeleton = sourceSkeleton;
    anim::HumanoidAvatar targetAvatar = sourceAvatar;
    scaleRig(targetSkeleton, targetAvatar, options.scale);

    std::vector<std::unique_ptr<anim::HumanoidClip>> clips;
    for (const std::string& path : options.clips) {
        const std::vector<uint8_t> data = readFile(path);
        if (data.empty()) {
            std::cerr << "cannot read clip: " << path << "\n";
            return 1;
        }
        std::unique_ptr<anim::Clip> decoded = anim::decodeClip(data, &error);
        if (decoded == nullptr) {
            std::cerr << "decode clip failed: " << path << ": " << error << "\n";
            return 1;
        }
        auto* humanoid = dynamic_cast<anim::HumanoidClip*>(decoded.get());
        if (humanoid == nullptr) {
            std::cerr << "retarget scene requires a humanoid clip: " << path << "\n";
            return 1;
        }
        decoded.release();
        clips.emplace_back(humanoid);
    }

    std::cout << "skeleton=" << clipLabel(options.skeleton)
              << " bones=" << sourceSkeleton.boneCount()
              << " sourceHips=" << sourceAvatar.hipsHeight
              << " targetHips=" << targetAvatar.hipsHeight << " scale=" << options.scale
              << "\n";
    for (std::size_t i = 0; i < clips.size(); ++i) {
        std::cout << "clip=" << clipLabel(options.clips[i]) << " length="
                  << clips[i]->length << "\n";
    }

    HWND window = nullptr;
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.lpszClassName = L"AnimancerHumanoidRetargetDemo";
    if (RegisterClassExW(&windowClass) == 0) {
        std::cerr << "RegisterClassEx failed\n";
        return 1;
    }

    RECT rect{0, 0, options.width, options.height};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    if (!options.headless) {
        window = CreateWindowExW(0, windowClass.lpszClassName,
                                 L"AnimancerCpp humanoid retarget acceptance",
                                 WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                 rect.right - rect.left, rect.bottom - rect.top, nullptr,
                                 nullptr, instance, nullptr);
    }
    if (window == nullptr && !options.headless) {
        std::cerr << "CreateWindowEx failed\n";
        return 1;
    }

    bgfx::Init init;
    init.callback = &g_callback;
    if (options.noop) init.type = bgfx::RendererType::Noop;
    init.platformData.nwh = window;
    if (options.headless) {
        init.resolution.width = 0;
        init.resolution.height = 0;
        init.resolution.reset = 0;
    } else {
        init.resolution.width = static_cast<uint32_t>(options.width);
        init.resolution.height = static_cast<uint32_t>(options.height);
        init.resolution.reset = BGFX_RESET_VSYNC;
    }
    if (!bgfx::init(init)) {
        std::cerr << "bgfx::init failed\n";
        return 1;
    }
    if (!options.headless) {
        ShowWindow(window, SW_SHOW);
        UpdateWindow(window);
    }

    const uint16_t halfWidth = static_cast<uint16_t>(options.width / 2);
    const uint16_t rightWidth = static_cast<uint16_t>(options.width - options.width / 2);
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x1b2028ff, 1.0f, 0);
    bgfx::setViewClear(1, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x28211bff, 1.0f, 0);
    bgfx::setViewRect(0, 0, 0, halfWidth, static_cast<uint16_t>(options.height));
    bgfx::setViewRect(1, halfWidth, 0, rightWidth, static_cast<uint16_t>(options.height));

    const bgfx::ProgramHandle program =
        loadProgram(options.shaders + "/vs_debug.bin", options.shaders + "/fs_debug.bin");
    if (!bgfx::isValid(program)) {
        std::cerr << "cannot load debug shaders; pass --shaders\n";
        bgfx::shutdown();
        return 1;
    }

    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true, true)
        .end();

    const float cameraHeight = std::max(targetAvatar.hipsHeight * 1.8f, 1.6f);
    const float centerY = cameraHeight * 0.5f;
    const float radius = cameraHeight * 0.56f;
    const bx::Vec3 eye{radius * 0.9f, centerY + radius * 0.10f, -radius * 2.2f};
    const bx::Vec3 at{0.0f, centerY, 0.0f};
    float viewMatrix[16];
    float projMatrix[16];
    bx::mtxLookAt(viewMatrix, eye, at);
    const float aspect =
        static_cast<float>(halfWidth) / static_cast<float>(std::max(options.height, 1));
    bx::mtxProj(projMatrix, 50.0f, aspect, 0.01f, 100.0f,
                bgfx::getCaps()->homogeneousDepth);
    bgfx::setViewTransform(0, viewMatrix, projMatrix);
    bgfx::setViewTransform(1, viewMatrix, projMatrix);

    const anim::HumanoidGoalWeights fullGoalWeights = [] {
        anim::HumanoidGoalWeights weights;
        weights.leftFootPosition = 1.0f;
        weights.leftFootRotation = 1.0f;
        weights.rightFootPosition = 1.0f;
        weights.rightFootRotation = 1.0f;
        weights.leftHandPosition = 1.0f;
        weights.leftHandRotation = 1.0f;
        weights.rightHandPosition = 1.0f;
        weights.rightHandRotation = 1.0f;
        return weights;
    }();

    const int totalFrames = options.frames > 0
                                ? options.frames
                                : static_cast<int>(clips.size()) * options.clipFrames;
    const int clipCount = static_cast<int>(clips.size());
    std::vector<bool> captured(clips.size(), false);
    std::vector<float> clipMaxGoalError(clips.size(), 0.0f);
    std::vector<float> clipMaxFootError(clips.size(), 0.0f);
    std::vector<float> clipMaxHandError(clips.size(), 0.0f);
    std::string pendingScreenshot;
    bool failed = false;
    int frame = 0;
    bool running = true;
    while (running && (options.frames > 0 || options.screenshot.empty() || frame < totalFrames)) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
            if (message.message == WM_QUIT) running = false;
        }
        if (!running) break;

        const int clipIndex = (frame / options.clipFrames) % clipCount;
        const int localFrame = frame % options.clipFrames;
        const anim::HumanoidClip& clip = *clips[static_cast<std::size_t>(clipIndex)];
        const float time =
            clip.length > 0.0f
                ? (static_cast<float>(localFrame) / static_cast<float>(options.clipFrames)) *
                      clip.length
                : 0.0f;

        const anim::HumanoidSample sourceSample = anim::sampleHumanoidClip(clip, time);
        anim::Pose sourcePose;
        anim::humanoidToLocal(sourceSample, sourceAvatar, sourceSkeleton, sourcePose, 1.0f);
        anim::applyHumanoidGoals(sourceSample, sourceAvatar, sourceSkeleton, sourcePose,
                                 fullGoalWeights, 1.0f);

        const anim::HumanoidSample targetSample = anim::retargetHumanoidFromPose(
            sourceAvatar, sourceSkeleton, sourcePose, {}, sourceAvatar.hipsHeight);
        anim::Pose targetPose;
        anim::humanoidToLocal(targetSample, targetAvatar, targetSkeleton, targetPose,
                              targetAvatar.hipsHeight);
        anim::applyHumanoidGoals(targetSample, targetAvatar, targetSkeleton, targetPose,
                                 fullGoalWeights, targetAvatar.hipsHeight);

        std::vector<anim::Mat4> sourceWorld;
        std::vector<anim::Mat4> targetWorld;
        anim::computeWorldMatrices(sourceSkeleton, sourcePose, sourceWorld);
        anim::computeWorldMatrices(targetSkeleton, targetPose, targetWorld);

        std::vector<DebugVertex> sourceVertices;
        std::vector<DebugVertex> targetVertices;
        addSkeletonLines(sourceVertices, sourceSkeleton, sourceWorld, packColor(80, 210, 255));
        addSkeletonLines(targetVertices, targetSkeleton, targetWorld, packColor(255, 190, 80));
        addGroundGrid(sourceVertices, targetAvatar.hipsHeight * 1.5f, 12,
                      packColor(55, 65, 82));
        addGroundGrid(targetVertices, targetAvatar.hipsHeight * 1.5f, 12,
                      packColor(82, 68, 55));

        const int hips = targetAvatar.boneIndex(anim::HumanBone::Hips);
        const anim::Transform root =
            hips >= 0 ? anim::Transform::fromMatrix(targetWorld[static_cast<std::size_t>(hips)])
                      : anim::Transform::identity();
        const float goalCrossSize = targetAvatar.hipsHeight * 0.045f;
        float maxGoalError = 0.0f;
        float maxFootError = 0.0f;
        float maxHandError = 0.0f;

        struct GoalCheck {
            anim::HumanBone bone;
            bool foot;
            anim::Vec3 position;
            anim::Quat rotation;
            bool hasPosition;
            bool hasRotation;
        };
        const GoalCheck checks[] = {
            {anim::HumanBone::LeftFoot, true, targetSample.leftFootT,
             targetSample.leftFootQ, targetSample.hasFootT, targetSample.hasFootQ},
            {anim::HumanBone::RightFoot, true, targetSample.rightFootT,
             targetSample.rightFootQ, targetSample.hasFootT, targetSample.hasFootQ},
            {anim::HumanBone::LeftHand, false, targetSample.leftHandT,
             targetSample.leftHandQ, targetSample.hasHandT, targetSample.hasHandQ},
            {anim::HumanBone::RightHand, false, targetSample.rightHandT,
             targetSample.rightHandQ, targetSample.hasHandT, targetSample.hasHandQ},
        };

        for (const GoalCheck& check : checks) {
            if (!check.hasPosition) continue;
            const int bone = targetAvatar.boneIndex(check.bone);
            if (bone < 0) continue;

            const anim::Vec3 goalWorld =
                rootSpaceToWorld(root, check.position, targetAvatar.hipsHeight);
            anim::Vec3 boneGoal = goalWorld;
            const anim::HumanoidBoneAxes& axes =
                targetAvatar.axes[static_cast<std::size_t>(anim::humanBoneIndex(check.bone))];
            if (check.foot && axes.valid && check.hasRotation) {
                const anim::Quat goalRotation = anim::safeNormalize(check.rotation);
                boneGoal -= (root.rotation * goalRotation) * anim::Vec3(axes.length, 0.0f, 0.0f);
            }

            const anim::Vec3 actual = anim::Vec3(targetWorld[static_cast<std::size_t>(bone)][3]);
            const float goalError = glm::distance(actual, boneGoal);
            maxGoalError = std::max(maxGoalError, goalError);
            if (check.foot) {
                maxFootError = std::max(maxFootError, goalError);
            } else {
                maxHandError = std::max(maxHandError, goalError);
            }
            if (localFrame == options.screenshotFrame) {
                std::cout << "  goal=" << anim::humanBoneName(check.bone)
                          << " error=" << goalError;
            }

            const uint32_t goalColor = check.foot ? packColor(90, 255, 130)
                                                  : packColor(255, 100, 225);
            const uint32_t actualColor = check.foot ? packColor(60, 170, 90)
                                                    : packColor(180, 70, 160);
            addCross(targetVertices, goalWorld, goalCrossSize, goalColor);
            addCross(targetVertices, actual, goalCrossSize * 0.65f, actualColor);
            addLine(targetVertices, actual, goalWorld, packColor(255, 230, 100));
        }
        if (localFrame == options.screenshotFrame) std::cout << "\n";
        float& clipMax = clipMaxGoalError[static_cast<std::size_t>(clipIndex)];
        clipMax = std::max(clipMax, maxGoalError);
        clipMaxFootError[static_cast<std::size_t>(clipIndex)] =
            std::max(clipMaxFootError[static_cast<std::size_t>(clipIndex)], maxFootError);
        clipMaxHandError[static_cast<std::size_t>(clipIndex)] =
            std::max(clipMaxHandError[static_cast<std::size_t>(clipIndex)], maxHandError);

        drawDebugView(0, sourceVertices, layout, program);
        drawDebugView(1, targetVertices, layout, program);
        bgfx::frame();

        if (!options.screenshot.empty() &&
            !captured[static_cast<std::size_t>(clipIndex)]) {
            if (localFrame == options.screenshotFrame) {
                const std::string label = clipLabel(options.clips[static_cast<std::size_t>(clipIndex)]);
                pendingScreenshot = options.screenshot + "-" + label;
                bgfx::requestScreenShot(BGFX_INVALID_HANDLE, pendingScreenshot.c_str());
                captured[static_cast<std::size_t>(clipIndex)] = true;
                const bool passed = clipMax <= options.goalTolerance;
                std::cout << "capture clip=" << label << " frame=" << localFrame
                          << " time=" << time << " frameGoalError=" << maxGoalError
                          << " foot=" << maxFootError << " hand=" << maxHandError
                          << " clipMaxGoalError=" << clipMax
                          << (passed ? " PASS" : " WARN") << "\n";
            }
        }

        ++frame;
        if (options.frames > 0 && frame >= options.frames) break;
    }

    // Let the queued screenshot be delivered before shutdown.
    if (!pendingScreenshot.empty()) bgfx::frame();

    for (std::size_t i = 0; i < clips.size(); ++i) {
        const float maximum = clipMaxGoalError[i];
        if (maximum <= 0.0f) continue;
        const bool passed = maximum <= options.goalTolerance;
        if (!passed) failed = true;
        std::cout << "result clip=" << clipLabel(options.clips[i])
                  << " maxGoalError=" << maximum
                  << " maxFootError=" << clipMaxFootError[i]
                  << " maxHandError=" << clipMaxHandError[i]
                  << " tolerance=" << options.goalTolerance
                  << (passed ? " PASS" : " FAIL") << "\n";
    }

    bgfx::destroy(program);
    bgfx::shutdown();
    return options.strict && failed ? 1 : 0;
}
