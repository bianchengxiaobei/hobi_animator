#include <bgfx/bgfx.h>
#include <bx/math.h>
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "anim/hobi/HobiAnimator.h"
#include "anim/hobi/HobiPlayable.h"
#include "anim/clip/Clip.h"
#include "anim/clip/ClipIO.h"
#include "anim/integration/Skinning.h"
#include "anim/skeleton/Palette.h"

#include "AvatarBuilder.h"
#include "AvatarLoader.h"
#include "FbxLoader.h"
#include "SkeletonLoader.h"

namespace {

struct Options {
    std::string model;
    std::string meta;
    std::string avatar;
    std::string clip;
    std::string shaders = "shaders";
    int width = 1280;
    int height = 720;
    int frames = 0;
    int screenshotFrame = 10;
    float cameraYaw = 0.0f;
    float fixedDeltaTime = 0.0f;
    bool noop = false;
    bool headless = false;
    std::string screenshot;
    std::string exportSkeleton;
};

void printUsage() {
    std::cerr << "usage: bgfx_demo --clip walk.ab [--model model.fbx] [--meta model.fbx.meta]\n"
                 "                  [--avatar avatar.json] [--shaders dir] [--frames N]\n"
                 "                  [--width W] [--height H] [--camera-yaw degrees]\n"
                 "                  [--fixed-dt seconds] [--screenshot file] [--screenshot-frame N]\n";
}

bool parseArgs(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto next = [&](std::string& value) {
            if (i + 1 >= argc) return false;
            value = argv[++i];
            return true;
        };
        if (arg == "--model") {
            if (!next(options.model)) return false;
        } else if (arg == "--meta") {
            if (!next(options.meta)) return false;
        } else if (arg == "--avatar") {
            if (!next(options.avatar)) return false;
        } else if (arg == "--clip") {
            if (!next(options.clip)) return false;
        } else if (arg == "--shaders") {
            if (!next(options.shaders)) return false;
        } else if (arg == "--headless") {
            options.headless = true;
            options.noop = true;
        } else if (arg == "--export-skeleton") {
            if (!next(options.exportSkeleton)) return false;
        } else if (arg == "--screenshot") {
            if (!next(options.screenshot)) return false;
        } else if (arg == "--screenshot-frame") {
            std::string value;
            if (!next(value)) return false;
            options.screenshotFrame = std::stoi(value);
        } else if (arg == "--noop") {
            options.noop = true;
        } else if (arg == "--frames") {
            std::string value;
            if (!next(value)) return false;
            options.frames = std::stoi(value);
        } else if (arg == "--width") {
            std::string value;
            if (!next(value)) return false;
            options.width = std::stoi(value);
        } else if (arg == "--height") {
            std::string value;
            if (!next(value)) return false;
            options.height = std::stoi(value);
        } else if (arg == "--camera-yaw") {
            std::string value;
            if (!next(value)) return false;
            options.cameraYaw = std::stof(value);
        } else if (arg == "--fixed-dt") {
            std::string value;
            if (!next(value)) return false;
            options.fixedDeltaTime = std::stof(value);
        } else {
            return false;
        }
    }
    return !options.clip.empty() || !options.exportSkeleton.empty();
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
    return bgfx::createShader(
        bgfx::copy(data.data(), static_cast<uint32_t>(data.size())));
}

bgfx::ProgramHandle loadProgram(const std::string& vertex, const std::string& fragment) {
    bgfx::ShaderHandle vs = loadShader(vertex);
    bgfx::ShaderHandle fs = loadShader(fragment);
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
        std::string path = filePath ? filePath : "screenshot";
        if (path.size() < 4 || path.compare(path.size() - 4, 4, ".ppm") != 0) {
            path += ".ppm";
        }
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
    void captureBegin(uint32_t, uint32_t, uint32_t, bgfx::TextureFormat::Enum,
                      bool) override {}
    void captureEnd() override {}
    void captureFrame(const void*, uint32_t) override {}
};

DemoCallback g_callback;
struct Bounds {
    anim::Vec3 min{1e30f};
    anim::Vec3 max{-1e30f};

    void add(const demo::SkinVertex& vertex) {
        for (int axis = 0; axis < 3; ++axis) {
            min[axis] = std::min(min[axis], vertex.position[axis]);
            max[axis] = std::max(max[axis], vertex.position[axis]);
        }
    }

    anim::Vec3 center() const { return (min + max) * 0.5f; }
    float radius() const { return std::max(glm::length(max - min) * 0.5f, 0.01f); }
};

bool buildAvatarFromJson(const demo::Model& model, const std::string& path,
                         anim::HumanoidAvatar& avatar, std::string& error) {
    anim::converter::AvatarConfig config;
    if (!anim::converter::loadAvatarFile(path, config, error)) return false;

    anim::converter::SkeletonAsset asset;
    asset.skeleton = model.skeleton;
    asset.boneNames = model.boneNames;
    return anim::converter::buildHumanoidAvatar(asset, config, avatar, error);
}

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

}  // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parseArgs(argc, argv, options)) {
        printUsage();
        return 2;
    }

    const std::string modelPath =
        options.model.empty()
            ? std::string(ANIM_ASSETS_DIR
                          "/Plugins/Animancer/Examples/Art/Default Humanoid/DefaultHumanoid.fbx")
            : options.model;
    const std::string metaPath =
        options.meta.empty() ? (readFile(modelPath + ".meta").empty() ? std::string()
                                                                      : modelPath + ".meta")
                             : options.meta;

    std::cerr << "loading model: " << modelPath << std::endl;
    demo::Model model;
    std::string error;
    if (!demo::loadFbx(modelPath, metaPath, model, error)) {
        std::cerr << "load fbx failed: " << error << "\n";
        return 1;
    }

    std::cerr << "loading clip: " << options.clip << std::endl;
    if (!options.exportSkeleton.empty()) {
        if (!demo::exportSkeletonJson(model, options.exportSkeleton, error)) {
            std::cerr << "export skeleton failed: " << error << "\n";
            return 1;
        }
        std::cout << "skeleton exported: " << options.exportSkeleton << "\n";
        if (options.clip.empty()) return 0;
    }

    const std::vector<uint8_t> clipData = readFile(options.clip);
    if (clipData.empty()) {
        std::cerr << "cannot read clip: " << options.clip << "\n";
        return 1;
    }
    std::string clipError;
    const std::unique_ptr<anim::Clip> clip = anim::decodeClip(clipData, &clipError);
    if (clip == nullptr) {
        std::cerr << "decode clip failed: " << clipError << "\n";
        return 1;
    }

    const bool humanoid = clip->type == anim::ClipType::HumanoidMuscle;
    anim::HumanoidAvatar loadedAvatar;
    const anim::HumanoidAvatar* avatar = nullptr;
    std::string avatarPath = options.avatar;
    if (avatarPath.empty() && options.model.empty()) {
        const std::string defaultAvatar =
            std::string(ANIM_RETARGET_DATA_DIR) + "/DefaultHumanoid.source.avatar.json";
        if (std::ifstream(defaultAvatar).good()) avatarPath = defaultAvatar;
    }
    if (humanoid) {
        if (!avatarPath.empty()) {
            if (!buildAvatarFromJson(model, avatarPath, loadedAvatar, error)) {
                std::cerr << "load avatar failed: " << error << "\n";
                return 1;
            }
            avatar = &loadedAvatar;
            std::cerr << "using calibrated avatar: " << avatarPath << std::endl;
        } else if (model.hasAvatar) {
            avatar = &model.avatar;
        } else {
            std::cerr << "humanoid clip requires --meta with humanDescription.human "
                         "or --avatar avatar.json\n";
            return 1;
        }

        int mappedBones = 0;
        for (int i = 0; i < anim::kHumanBoneCount; ++i) {
            if (avatar->hasBone(static_cast<anim::HumanBone>(i))) ++mappedBones;
        }
        std::cout << "humanoid avatar bones=" << mappedBones << "/" << anim::kHumanBoneCount
                  << "\n";
    }

    std::cerr << "creating window" << std::endl;
    HWND window = nullptr;
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.lpszClassName = L"HobiBgfxDemo";
    if (RegisterClassExW(&windowClass) == 0) {
        std::cerr << "RegisterClassEx failed\n";
        return 1;
    }

    RECT rect{0, 0, options.width, options.height};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    if (!options.headless) window = CreateWindowExW(0, windowClass.lpszClassName, L"hobi_animator bgfx demo",
                                  WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                  rect.right - rect.left, rect.bottom - rect.top, nullptr,
                                  nullptr, instance, nullptr);
    if (window == nullptr && !options.headless) {
        std::cerr << "CreateWindowEx failed\n";
        return 1;
    }

    std::cerr << "bgfx init" << std::endl;
    if (!options.headless) {
        ShowWindow(window, SW_SHOW);
        UpdateWindow(window);
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
    const bool initialized = bgfx::init(init);
    std::cerr << "bgfx init returned " << initialized << std::endl;
    if (!initialized) {
        std::cerr << "bgfx::init failed\n";
        return 1;
    }

    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x20242cff, 1.0f, 0);
    bgfx::setViewRect(0, 0, 0, static_cast<uint16_t>(options.width),
                      static_cast<uint16_t>(options.height));

    std::cerr << "loading shaders" << std::endl;
    const bgfx::ProgramHandle program = loadProgram(options.shaders + "/vs_skin.bin",
                                                    options.shaders + "/fs_skin.bin");
    if (!bgfx::isValid(program)) {
        std::cerr << "cannot load shaders; run from the build directory or pass --shaders\n";
        bgfx::shutdown();
        return 1;
    }

    const bgfx::UniformHandle uBonesTex =
        bgfx::createUniform("u_bonesTex", bgfx::UniformType::Sampler);
    const bgfx::UniformHandle uBoneCount =
        bgfx::createUniform("u_boneCount", bgfx::UniformType::Vec4);
    const bgfx::TextureHandle boneTex = bgfx::createTexture2D(
        4, static_cast<uint16_t>(model.skeleton.boneCount()), false, 1,
        bgfx::TextureFormat::RGBA32F, BGFX_TEXTURE_NONE, nullptr);

    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Indices, 4, bgfx::AttribType::Uint8)
        .add(bgfx::Attrib::Weight, 4, bgfx::AttribType::Uint8, true, true)
        .end();

    const bgfx::VertexBufferHandle vbh = bgfx::createVertexBuffer(
        bgfx::copy(model.vertices.data(),
                   static_cast<uint32_t>(model.vertices.size() * sizeof(demo::SkinVertex))),
        layout);
    const bgfx::IndexBufferHandle ibh = bgfx::createIndexBuffer(
        bgfx::copy(model.indices.data(),
                   static_cast<uint32_t>(model.indices.size() * sizeof(uint32_t))),
        BGFX_BUFFER_INDEX32);

    Bounds bounds;
    for (const demo::SkinVertex& vertex : model.vertices) bounds.add(vertex);

    anim::HobiAnimator animator;
    animator.initialize(model.skeleton, avatar);
    animator.playable().defaultFadeDuration = 0.0f;
    animator.play(*clip);

    std::cout << "model bones=" << model.skeleton.boneCount()
              << " vertices=" << model.vertices.size() << " indices=" << model.indices.size()
              << " humanoid=" << (humanoid ? "yes" : "no") << "\n";
    std::cout << "clip=" << options.clip << " length=" << clip->length
              << " type=" << (humanoid ? "humanoid" : "bone-local") << "\n";

    std::vector<anim::Mat4> rowMajor;
    rowMajor.resize(static_cast<std::size_t>(model.skeleton.boneCount()));

    auto last = std::chrono::high_resolution_clock::now();
    int frame = 0;
    bool running = true;
    while (running) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
            if (message.message == WM_QUIT) running = false;
        }
        if (!running) break;

        const auto now = std::chrono::high_resolution_clock::now();
        float deltaTime =
            std::chrono::duration<float>(now - last).count();
        last = now;
        deltaTime = std::clamp(deltaTime, 0.0f, 0.1f);
        if (options.fixedDeltaTime > 0.0f) deltaTime = options.fixedDeltaTime;

        animator.update(deltaTime);
        const std::vector<anim::Mat4>& palette = animator.palette();
        const uint16_t boneCount = static_cast<uint16_t>(palette.size());
        anim::transposePalette(palette, rowMajor);
        bgfx::updateTexture2D(
            boneTex, 0, 0, 0, 0, 4, boneCount,
            bgfx::copy(rowMajor.data(),
                       static_cast<uint32_t>(rowMajor.size() * sizeof(anim::Mat4))),
            64);
        const float boneCountValue[4] = {static_cast<float>(boneCount), 0.0f, 0.0f, 0.0f};
        bgfx::setUniform(uBoneCount, boneCountValue);
        bgfx::setTexture(0, uBonesTex, boneTex,
                         BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);

        const anim::Vec3 center = bounds.center();
        const float radius = bounds.radius();
        anim::Vec3 target = center;
        if (avatar != nullptr) {
            const int hips = avatar->boneIndex(anim::HumanBone::Hips);
            const anim::Pose& pose = animator.playable().outputPose();
            if (hips >= 0 && static_cast<std::size_t>(hips) < pose.local.size()) {
                const anim::Transform& rest = avatar->rest(anim::HumanBone::Hips);
                const anim::Transform& current =
                    pose.local[static_cast<std::size_t>(hips)];
                target.x += current.translation.x - rest.translation.x;
                target.z += current.translation.z - rest.translation.z;
            }
        }
        const float yaw = glm::radians(options.cameraYaw);
        const float distance = radius * (avatar != nullptr ? 2.4f : 4.0f);
        const bx::Vec3 eye{target.x + std::sin(yaw) * distance,
                           target.y + radius * 0.15f,
                           target.z + std::cos(yaw) * distance};
        const bx::Vec3 at{target.x, target.y, target.z};

        float view[16];
        bx::mtxLookAt(view, eye, at);
        float proj[16];
        bx::mtxProj(proj, 55.0f, static_cast<float>(options.width) / options.height,
                    radius * 0.01f, radius * 100.0f,
                    bgfx::getCaps()->homogeneousDepth);
        bgfx::setViewTransform(0, view, proj);

        bgfx::setVertexBuffer(0, vbh);
        bgfx::setIndexBuffer(ibh);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
                       BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA);
        bgfx::submit(0, program);

        bgfx::frame();
        ++frame;

        if (frame % 120 == 0) {
            std::cout << "frame " << frame << " bones=" << boneCount << " dt=" << deltaTime
                      << "\n";
        }
        if (!options.screenshot.empty() && frame == options.screenshotFrame) {
            bgfx::requestScreenShot(BGFX_INVALID_HANDLE, options.screenshot.c_str());
        }
        if (options.frames > 0 && frame >= options.frames) break;
    }

    bgfx::destroy(boneTex);
    bgfx::destroy(uBoneCount);
    bgfx::destroy(uBonesTex);
    bgfx::destroy(program);
    bgfx::destroy(ibh);
    bgfx::destroy(vbh);
    bgfx::shutdown();
    return 0;
}
