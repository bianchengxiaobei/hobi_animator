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
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "anim/hobi/HobiAnimator.h"
#include "anim/hobi/HobiLayer.h"
#include "anim/hobi/HobiPlayable.h"
#include "anim/hobi/HobiState.h"
#include "anim/hobi/LayerList.h"
#include "anim/hobi/Transition.h"
#include "anim/clip/Clip.h"
#include "anim/clip/ClipIO.h"
#include "anim/core/Hash.h"
#include "anim/integration/AnimationManifest.h"
#include "anim/integration/Skinning.h"
#include "anim/skeleton/Palette.h"

#include "AvatarBuilder.h"
#include "AvatarLoader.h"
#include "FbxLoader.h"
#include "Input.h"
#include "SkeletonDebug.h"
#include "SkeletonLoader.h"

#if ANIM_DEBUG_TOOLS
#include "anim/debug/Recorder.h"
#include "anim/debug/Snapshot.h"
#include "DebugOverlay.h"
#endif

namespace {

struct Options {
    std::string model;
    std::string meta;
    std::string avatar;
    std::string clip;
    std::string manifest;
    std::vector<std::string> clipNames;
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
    std::string snapshot;
    int snapshotFrame = 0;
    std::string snapshotHistory;
    bool debugOverlay = false;
    bool logEvents = false;
    float previewFadeDuration = -1.0f;
    anim::FadeMode previewFadeMode = anim::FadeMode::FixedSpeed;
    bool previewFadeModeSet = false;
    float previewStartTime = std::numeric_limits<float>::quiet_NaN();
    float previewSpeed = std::numeric_limits<float>::quiet_NaN();
    int previewLoop = -1;
    int previewSwitchFrame = 0;
    bool graph = false;
    bool showSkeleton = false;
};

bool parsePreviewFadeMode(const std::string& value, anim::FadeMode& mode) {
    if (value == "fixed-speed") {
        mode = anim::FadeMode::FixedSpeed;
    } else if (value == "fixed-duration") {
        mode = anim::FadeMode::FixedDuration;
    } else if (value == "from-start") {
        mode = anim::FadeMode::FromStart;
    } else if (value == "normalized-speed") {
        mode = anim::FadeMode::NormalizedSpeed;
    } else if (value == "normalized-duration") {
        mode = anim::FadeMode::NormalizedDuration;
    } else if (value == "normalized-from-start") {
        mode = anim::FadeMode::NormalizedFromStart;
    } else {
        return false;
    }
    return true;
}

void printUsage() {
    std::cerr << "usage: bgfx_demo --clip walk.ab [--model model.fbx] [--meta model.fbx.meta]\n"
                 "                  [--avatar avatar.json] [--shaders dir] [--frames N]\n"
                 "                  [--width W] [--height H] [--camera-yaw degrees]\n"
                 "                  [--fixed-dt seconds] [--screenshot file] [--screenshot-frame N]\n"
                 "                  [--snapshot file] [--snapshot-frame N] [--snapshot-history file]\n"
                 "                  [--debug-overlay] [--log-events] [--graph] [--show-skeleton]\n"
                 "                  [--preview-fade seconds] [--preview-fade-mode mode]\n"
                 "                  [--preview-start normalized] [--preview-speed speed]\n"
                 "                  [--preview-loop|--preview-no-loop] [--preview-switch-frame N]\n"
                 "       bgfx_demo --manifest anim_manifest.json [--clip-name Walk ...]\n"
                 "                  [--model model.fbx] [--avatar avatar.json]\n";
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
        } else if (arg == "--manifest") {
            if (!next(options.manifest)) return false;
        } else if (arg == "--clip-name") {
            std::string value;
            if (!next(value)) return false;
            options.clipNames.push_back(value);
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
        } else if (arg == "--snapshot") {
            if (!next(options.snapshot)) return false;
        } else if (arg == "--snapshot-frame") {
            std::string value;
            if (!next(value)) return false;
            options.snapshotFrame = std::stoi(value);
        } else if (arg == "--snapshot-history") {
            if (!next(options.snapshotHistory)) return false;
        } else if (arg == "--debug-overlay") {
            options.debugOverlay = true;
        } else if (arg == "--log-events") {
            options.logEvents = true;
        } else if (arg == "--graph") {
            options.graph = true;
        } else if (arg == "--show-skeleton") {
            options.showSkeleton = true;
        } else if (arg == "--preview-fade") {
            std::string value;
            if (!next(value)) return false;
            options.previewFadeDuration = std::stof(value);
            if (!std::isfinite(options.previewFadeDuration) ||
                options.previewFadeDuration < 0.0f) {
                return false;
            }
        } else if (arg == "--preview-fade-mode") {
            std::string value;
            if (!next(value)) return false;
            if (!parsePreviewFadeMode(value, options.previewFadeMode)) return false;
            options.previewFadeModeSet = true;
        } else if (arg == "--preview-start") {
            std::string value;
            if (!next(value)) return false;
            options.previewStartTime = std::stof(value);
            if (!std::isfinite(options.previewStartTime)) return false;
        } else if (arg == "--preview-speed") {
            std::string value;
            if (!next(value)) return false;
            options.previewSpeed = std::stof(value);
            if (!std::isfinite(options.previewSpeed) || options.previewSpeed < 0.0f) return false;
        } else if (arg == "--preview-loop") {
            options.previewLoop = 1;
        } else if (arg == "--preview-no-loop") {
            options.previewLoop = 0;
        } else if (arg == "--preview-switch-frame") {
            std::string value;
            if (!next(value)) return false;
            options.previewSwitchFrame = std::stoi(value);
            if (options.previewSwitchFrame <= 0) return false;
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
    if (!options.clip.empty() && !options.manifest.empty()) return false;
    if (options.manifest.empty() && !options.clipNames.empty()) return false;

    const bool wantsSnapshot =
        !options.snapshot.empty() || !options.snapshotHistory.empty();
    if (wantsSnapshot && options.snapshotFrame <= 0) return false;
#if !ANIM_DEBUG_TOOLS
    if (wantsSnapshot || options.debugOverlay || options.logEvents || options.graph) return false;
#endif
    if (options.headless && !wantsSnapshot) return false;

    return !options.clip.empty() || !options.manifest.empty() || !options.exportSkeleton.empty();
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

std::unique_ptr<anim::Clip> decodeClipFile(const std::string& path, std::string& error) {
    const std::vector<uint8_t> data = readFile(path);
    if (data.empty()) {
        error = "cannot read clip: " + path;
        return nullptr;
    }
    std::string decodeError;
    std::unique_ptr<anim::Clip> clip = anim::decodeClip(data, &decodeError);
    if (clip == nullptr) {
        error = "decode clip failed (" + path + "): " + decodeError;
        return nullptr;
    }
    return clip;
}

#if ANIM_DEBUG_TOOLS
bool writeSnapshotFile(const anim::HobiPlayable& playable, const std::string& path,
                       std::string& error) {
    return anim::debug::writeJsonFile(anim::debug::capture(playable), path, error);
}
#endif

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
demo::InputState g_input;

struct DemoClip {
    std::string name;
    std::unique_ptr<anim::Clip> clip;
    anim::ClipTransition transition;
    anim::FadeMode fadeMode = anim::FadeMode::FixedSpeed;

    float defaultFadeDuration = 0.0f;
    anim::FadeMode defaultFadeMode = anim::FadeMode::FixedSpeed;
    float defaultStartTime = std::numeric_limits<float>::quiet_NaN();
    float defaultSpeed = 1.0f;
    bool defaultLooping = false;
};

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
            switch (wparam) {
                case VK_ESCAPE: PostQuitMessage(0); break;
                case VK_SPACE: g_input.onKeyDown(demo::Key::Space); break;
                case VK_UP: g_input.onKeyDown(demo::Key::Up); break;
                case VK_DOWN: g_input.onKeyDown(demo::Key::Down); break;
                case VK_TAB: g_input.onKeyDown(demo::Key::Tab); break;
                case VK_OEM_PERIOD: g_input.onKeyDown(demo::Key::Period); break;
                case VK_OEM_4: g_input.onKeyDown(demo::Key::LeftBracket); break;
                case VK_OEM_6: g_input.onKeyDown(demo::Key::RightBracket); break;
                case VK_OEM_MINUS: g_input.onKeyDown(demo::Key::Minus); break;
                case VK_OEM_PLUS: g_input.onKeyDown(demo::Key::Equals); break;
                case 'R': g_input.onKeyDown(demo::Key::R); break;
                case 'D': g_input.onKeyDown(demo::Key::D); break;
                case 'E': g_input.onKeyDown(demo::Key::E); break;
                case 'G': g_input.onKeyDown(demo::Key::G); break;
                case 'S': g_input.onKeyDown(demo::Key::S); break;
                case 'F': g_input.onKeyDown(demo::Key::F); break;
                case 'M': g_input.onKeyDown(demo::Key::M); break;
                case 'T': g_input.onKeyDown(demo::Key::T); break;
                case 'L': g_input.onKeyDown(demo::Key::L); break;
                case 'K': g_input.onKeyDown(demo::Key::K); break;
                case 'V': g_input.onKeyDown(demo::Key::V); break;
                default: break;
            }
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

    if (!options.exportSkeleton.empty()) {
        if (!demo::exportSkeletonJson(model, options.exportSkeleton, error)) {
            std::cerr << "export skeleton failed: " << error << "\n";
            return 1;
        }
        std::cout << "skeleton exported: " << options.exportSkeleton << "\n";
        if (options.clip.empty() && options.manifest.empty()) return 0;
    }

    std::vector<std::unique_ptr<DemoClip>> clips;
    std::unordered_map<uint64_t, std::string> eventNames;
    if (!options.manifest.empty()) {
        anim::AnimationSet manifest;
        if (!anim::loadAnimationManifestFile(options.manifest, manifest, error)) {
            std::cerr << "load manifest failed: " << error << "\n";
            return 1;
        }
        if (manifest.clips.empty()) {
            std::cerr << "manifest has no clips: " << options.manifest << "\n";
            return 1;
        }
        if (!manifest.skeletonFile.empty()) {
            anim::converter::SkeletonAsset manifestSkeleton;
            if (!anim::converter::loadSkeletonFile(manifest.skeletonFile, manifestSkeleton,
                                                   error)) {
                std::cerr << "load manifest skeleton failed: " << error << "\n";
                return 1;
            }
            if (manifestSkeleton.skeleton.boneCount() != model.skeleton.boneCount()) {
                std::cerr << "manifest skeleton bone count " << manifestSkeleton.skeleton.boneCount()
                          << " does not match model bone count "
                          << model.skeleton.boneCount() << "\n";
                return 1;
            }
        }

        std::vector<const anim::ClipAssetInfo*> selected;
        if (options.clipNames.empty()) {
            for (const anim::ClipAssetInfo& info : manifest.clips) selected.push_back(&info);
        } else {
            for (const std::string& name : options.clipNames) {
                const anim::ClipAssetInfo* info = manifest.find(name);
                if (info == nullptr) {
                    std::cerr << "clip name not found in manifest: " << name << "\n";
                    return 1;
                }
                selected.push_back(info);
            }
        }

        for (const anim::ClipAssetInfo* info : selected) {
            std::cerr << "loading manifest clip: " << info->name << " (" << info->file
                      << ")" << std::endl;
            std::unique_ptr<anim::Clip> clip = decodeClipFile(info->file, error);
            if (clip == nullptr) {
                std::cerr << error << "\n";
                return 1;
            }
            clip->looping = info->looping;

            auto demoClip = std::make_unique<DemoClip>();
            demoClip->name = info->name;
            demoClip->clip = std::move(clip);
            demoClip->transition.clip = demoClip->clip.get();
            demoClip->transition.fadeDurationValue = info->fadeDuration;
            demoClip->transition.speedValue = info->speed;
            demoClip->transition.normalizedStartTime = info->normalizedStartTime;
            demoClip->fadeMode = demoClip->transition.fadeMode();
            demoClip->defaultFadeDuration = info->fadeDuration;
            demoClip->defaultFadeMode = demoClip->fadeMode;
            demoClip->defaultStartTime = info->normalizedStartTime;
            demoClip->defaultSpeed = info->speed;
            demoClip->defaultLooping = info->looping;
            for (const anim::ClipEventInfo& event : info->events) {
                demoClip->transition.eventSequence.add(event.normalizedTime, nullptr,
                                                       event.name);
                eventNames[anim::hashString(event.name)] = event.name;
            }
            clips.push_back(std::move(demoClip));
        }
    } else {
        std::cerr << "loading clip: " << options.clip << std::endl;
        std::unique_ptr<anim::Clip> clip = decodeClipFile(options.clip, error);
        if (clip == nullptr) {
            std::cerr << error << "\n";
            return 1;
        }
        auto demoClip = std::make_unique<DemoClip>();
        demoClip->name = options.clip;
        demoClip->clip = std::move(clip);
        demoClip->transition.clip = demoClip->clip.get();
        demoClip->transition.fadeDurationValue = 0.0f;
        demoClip->defaultFadeDuration = 0.0f;
        demoClip->defaultStartTime = demoClip->transition.normalizedStartTime;
        demoClip->defaultSpeed = demoClip->transition.speedValue;
        demoClip->defaultLooping = demoClip->clip->looping;
        clips.push_back(std::move(demoClip));
    }

    if (options.previewSwitchFrame > 0 && clips.size() < 2) {
        std::cerr << "--preview-switch-frame requires at least two clips\n";
        return 2;
    }
    for (const std::unique_ptr<DemoClip>& item : clips) {
        if (options.previewFadeDuration >= 0.0f) {
            item->transition.fadeDurationValue = options.previewFadeDuration;
        }
        if (options.previewFadeModeSet) item->fadeMode = options.previewFadeMode;
        if (std::isfinite(options.previewStartTime)) {
            item->transition.normalizedStartTime = options.previewStartTime;
        }
        if (std::isfinite(options.previewSpeed)) {
            item->transition.speedValue = options.previewSpeed;
        }
        if (options.previewLoop >= 0) {
            item->clip->looping = options.previewLoop != 0;
        }
    }

    bool humanoid = false;
    for (const std::unique_ptr<DemoClip>& item : clips) {
        if (item->clip->type == anim::ClipType::HumanoidMuscle) humanoid = true;
    }
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

#if ANIM_DEBUG_TOOLS
    if (options.graph) {
        anim::HobiAnimator graphAnimator;
        graphAnimator.initialize(model.skeleton, avatar);
        graphAnimator.play(*clips.front()->clip, 0.0f, anim::FadeMode::FixedSpeed);
        graphAnimator.update(0.0f);
        demo::DebugOverlay::dumpGraph(graphAnimator.playable());
        return 0;
    }
#endif

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
    const bgfx::ProgramHandle debugProgram =
        loadProgram(options.shaders + "/vs_debug.bin", options.shaders + "/fs_debug.bin");
    if (!bgfx::isValid(debugProgram)) {
        std::cerr << "cannot load debug shaders; --show-skeleton unavailable\n";
        if (options.showSkeleton) {
            bgfx::shutdown();
            return 1;
        }
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

    bgfx::VertexLayout debugLayout;
    debugLayout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true, true)
        .end();
    const uint32_t skeletonVertexCapacity =
        std::max<uint32_t>(2, static_cast<uint32_t>(model.skeleton.boneCount()) * 2);
    const bgfx::DynamicVertexBufferHandle skeletonVbh =
        bgfx::createDynamicVertexBuffer(skeletonVertexCapacity, debugLayout,
                                        BGFX_BUFFER_NONE);
    std::vector<demo::DebugVertex> skeletonVertices;
    skeletonVertices.reserve(skeletonVertexCapacity);
    std::vector<anim::Mat4> skeletonWorld;

    Bounds bounds;
    for (const demo::SkinVertex& vertex : model.vertices) bounds.add(vertex);

    anim::HobiAnimator animator;
    animator.initialize(model.skeleton, avatar);
    animator.playable().defaultFadeDuration = 0.0f;

    int selectedClip = 0;
    auto currentClip = [&]() -> DemoClip& { return *clips[selectedClip]; };
    std::vector<anim::HobiState*> retiringStates;
    auto playCurrent = [&]() -> anim::HobiState* {
        DemoClip& item = currentClip();
        anim::HobiState* state = animator.play(item.transition,
                                                item.transition.fadeDurationValue,
                                                item.fadeMode);
        if (state != nullptr) {
            float startTime = item.transition.normalizedStartTime;
            if (std::isnan(startTime)) {
                startTime = anim::EventSequence::defaultNormalizedStartTime(
                    item.transition.speedValue);
            }
            state->setNormalizedTimeD(startTime);
        }
        return state;
    };
    auto retireOldStates = [&](anim::HobiState* keep) {
        retiringStates.erase(
            std::remove(retiringStates.begin(), retiringStates.end(), keep),
            retiringStates.end());
        anim::HobiLayer* layer = animator.layer(0);
        if (layer == nullptr) return;
        for (int i = 0; i < layer->childCount(); ++i) {
            anim::HobiState* state = layer->getChild(i);
            if (state == nullptr || state == keep) continue;
            if (std::find(retiringStates.begin(), retiringStates.end(), state) ==
                retiringStates.end()) {
                retiringStates.push_back(state);
            }
        }
    };
    auto cleanupRetiringStates = [&] {
        anim::HobiLayer* layer = animator.layer(0);
        for (auto it = retiringStates.begin(); it != retiringStates.end();) {
            anim::HobiState* state = *it;
            const bool current = layer != nullptr && state == layer->currentState();
            if (state == nullptr ||
                (!current && state->weight() <= 0.0001f &&
                 state->targetWeight() <= 0.0001f)) {
                if (state != nullptr) state->destroy();
                it = retiringStates.erase(it);
            } else {
                ++it;
            }
        }
    };
    retireOldStates(playCurrent());

    int selectedLayer = 0;
    bool showSkeleton = options.showSkeleton;

    auto printPreviewStatus = [&] {
        DemoClip& item = currentClip();
        std::cout << "preview clip " << (selectedClip + 1) << "/" << clips.size()
                  << " " << item.name
                  << " fade=" << item.transition.fadeDurationValue
                  << " mode=" << anim::toString(item.fadeMode)
                  << " start=";
        if (std::isnan(item.transition.normalizedStartTime)) {
            std::cout << "auto";
        } else {
            std::cout << item.transition.normalizedStartTime;
        }
        std::cout << " speed=" << item.transition.speedValue
                  << " loop=" << (item.clip->looping ? "yes" : "no") << "\n";
    };
    auto cyclePreviewFade = [&] {
        DemoClip& item = currentClip();
        float& fade = item.transition.fadeDurationValue;
        if (fade <= 0.0f) {
            fade = 0.1f;
        } else if (fade < 0.25f) {
            fade = 0.25f;
        } else if (fade < 0.5f) {
            fade = 0.5f;
        } else if (fade < 1.0f) {
            fade = 1.0f;
        } else {
            fade = 0.0f;
        }
        printPreviewStatus();
    };
    auto cyclePreviewFadeMode = [&] {
        DemoClip& item = currentClip();
        item.fadeMode = static_cast<anim::FadeMode>(
            (static_cast<int>(item.fadeMode) + 1) % 6);
        printPreviewStatus();
    };
    auto cyclePreviewStart = [&] {
        DemoClip& item = currentClip();
        float& start = item.transition.normalizedStartTime;
        if (std::isnan(start)) {
            start = 0.0f;
        } else if (start < 0.25f) {
            start = 0.25f;
        } else if (start < 0.5f) {
            start = 0.5f;
        } else {
            start = std::numeric_limits<float>::quiet_NaN();
        }
        printPreviewStatus();
    };
    auto adjustPreviewSpeed = [&](int delta) {
        DemoClip& item = currentClip();
        const float next =
            std::clamp(item.transition.speedValue + 0.1f * static_cast<float>(delta),
                       0.1f, 4.0f);
        item.transition.speedValue = next;
        if (anim::HobiLayer* layer = animator.layer(0)) {
            if (anim::HobiState* state = layer->currentState()) state->setSpeed(next);
        }
        printPreviewStatus();
    };
    auto togglePreviewLoop = [&] {
        DemoClip& item = currentClip();
        item.clip->looping = !item.clip->looping;
        printPreviewStatus();
    };
    auto resetPreview = [&] {
        DemoClip& item = currentClip();
        item.transition.fadeDurationValue = item.defaultFadeDuration;
        item.fadeMode = item.defaultFadeMode;
        item.transition.normalizedStartTime = item.defaultStartTime;
        item.transition.speedValue = item.defaultSpeed;
        item.clip->looping = item.defaultLooping;
        if (anim::HobiLayer* layer = animator.layer(0)) {
            if (anim::HobiState* state = layer->currentState()) {
                state->setSpeed(item.defaultSpeed);
            }
        }
        printPreviewStatus();
    };

#if ANIM_DEBUG_TOOLS
    std::unique_ptr<anim::debug::Recorder> recorder =
        std::make_unique<anim::debug::Recorder>();
    recorder->attach(animator.playable());
    std::unique_ptr<demo::DebugOverlay> overlay = std::make_unique<demo::DebugOverlay>();
    overlay->setRecorder(recorder.get());
    overlay->setEventNames(&eventNames);
    overlay->setVisible(options.debugOverlay);
    auto syncPreviewStatus = [&] {
        DemoClip& item = currentClip();
        overlay->setPreviewStatus(item.name, selectedClip + 1,
                                  static_cast<int>(clips.size()),
                                  item.transition.fadeDurationValue, item.fadeMode,
                                  item.transition.normalizedStartTime,
                                  item.transition.speedValue, item.clip->looping);
    };
    uint64_t lastLoggedEventFrame = 0;
#endif

    std::cout << "model bones=" << model.skeleton.boneCount()
              << " vertices=" << model.vertices.size() << " indices=" << model.indices.size()
              << " humanoid=" << (humanoid ? "yes" : "no") << "\n";
    for (std::size_t i = 0; i < clips.size(); ++i) {
        const DemoClip& item = *clips[i];
        std::cout << "clip[" << i << "]=" << item.name << " length=" << item.clip->length
                  << " type="
                  << (item.clip->type == anim::ClipType::HumanoidMuscle ? "humanoid"
                                                                         : "bone-local")
                  << " loop=" << (item.clip->looping ? "yes" : "no") << "\n";
    }

    std::vector<anim::Mat4> rowMajor;
    rowMajor.resize(static_cast<std::size_t>(model.skeleton.boneCount()));

    auto last = std::chrono::high_resolution_clock::now();
    int frame = 0;
    int exitCode = 0;
    bool running = true;
    bool previewSwitched = false;
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

        if (g_input.toggleSkeleton) {
            g_input.toggleSkeleton = false;
            showSkeleton = !showSkeleton;
            std::cout << "skeleton overlay: " << (showSkeleton ? "on" : "off") << "\n";
        }
#if ANIM_DEBUG_TOOLS
        if (g_input.toggleOverlay) {
            overlay->toggleVisible();
            g_input.toggleOverlay = false;
        }
        if (g_input.toggleEvents) {
            overlay->toggleEvents();
            g_input.toggleEvents = false;
        }
#endif
        if (g_input.cyclePreviewFade) {
            g_input.cyclePreviewFade = false;
            cyclePreviewFade();
        }
        if (g_input.cyclePreviewFadeMode) {
            g_input.cyclePreviewFadeMode = false;
            cyclePreviewFadeMode();
        }
        if (g_input.cyclePreviewStart) {
            g_input.cyclePreviewStart = false;
            cyclePreviewStart();
        }
        if (g_input.previewSpeedDelta != 0) {
            adjustPreviewSpeed(g_input.previewSpeedDelta);
            g_input.previewSpeedDelta = 0;
        }
        if (g_input.togglePreviewLoop) {
            g_input.togglePreviewLoop = false;
            togglePreviewLoop();
        }
        if (g_input.resetPreview) {
            g_input.resetPreview = false;
            resetPreview();
        }
        if (g_input.speedDelta != 0) {
            const float speed = std::clamp(animator.speed() + 0.25f * g_input.speedDelta,
                                           0.0f, 4.0f);
            animator.setSpeed(speed);
            g_input.speedDelta = 0;
        }
        if (g_input.togglePlay) {
            g_input.togglePlay = false;
            if (animator.isGraphPlaying()) {
                animator.pauseGraph();
            } else {
                animator.unpauseGraph();
            }
        }
        if (g_input.stepRequested) {
            g_input.stepRequested = false;
            if (!animator.isGraphPlaying()) {
                const float step =
                    options.fixedDeltaTime > 0.0f ? options.fixedDeltaTime : (1.0f / 60.0f);
                animator.unpauseGraph();
                animator.update(step);
                animator.pauseGraph();
            }
        }
        if (g_input.layerDelta != 0) {
            const int layerCount = animator.playable().layers().count();
            if (layerCount > 0) {
                selectedLayer = (selectedLayer + g_input.layerDelta) % layerCount;
                if (selectedLayer < 0) selectedLayer += layerCount;
#if ANIM_DEBUG_TOOLS
                overlay->setSelectedLayer(selectedLayer);
#endif
            }
            g_input.layerDelta = 0;
        }
#if ANIM_DEBUG_TOOLS
        if (g_input.dumpGraph) {
            g_input.dumpGraph = false;
            demo::DebugOverlay::dumpGraph(animator.playable());
        }
        if (g_input.saveSnapshot) {
            g_input.saveSnapshot = false;
            const std::string path =
                options.snapshot.empty() ? std::string("snapshot.json") : options.snapshot;
            std::string snapshotError;
            if (writeSnapshotFile(animator.playable(), path, snapshotError)) {
                std::cout << "snapshot written: " << path << "\n";
            } else {
                std::cerr << "write snapshot failed: " << snapshotError << "\n";
            }
        }
#endif

        if (g_input.clipDelta != 0 && clips.size() > 1) {
            const int count = static_cast<int>(clips.size());
            int next = (selectedClip + g_input.clipDelta) % count;
            if (next < 0) next += count;
            if (next != selectedClip) {
                selectedClip = next;
                retireOldStates(playCurrent());
                std::cout << "switch clip: " << currentClip().name << "\n";
            }
        }
        g_input.clipDelta = 0;
        if (!previewSwitched && options.previewSwitchFrame > 0 &&
            frame == options.previewSwitchFrame && clips.size() > 1) {
            previewSwitched = true;
            selectedClip = (selectedClip + 1) % static_cast<int>(clips.size());
            retireOldStates(playCurrent());
            std::cout << "preview switch clip: " << currentClip().name << "\n";
        }
        if (g_input.restartRequested) {
            g_input.restartRequested = false;
            retireOldStates(playCurrent());
        }

        animator.update(deltaTime);
        cleanupRetiringStates();
#if ANIM_DEBUG_TOOLS
        if (recorder != nullptr) recorder->recordFrame();
        if (options.logEvents) {
            const int count = recorder->eventCount();
            for (int offset = count - 1; offset >= 0; --offset) {
                const anim::debug::EventRecord* event = recorder->eventAt(-offset);
                if (event == nullptr || event->frameId <= lastLoggedEventFrame) continue;
                std::cerr << "event f" << event->frameId << " l" << event->layer
                          << " s" << event->stateIndex << " i" << event->eventIndex
                          << " " << (event->kind == anim::EventKind::End ? "End" : "Normal")
                          << " " << (event->playingForwards ? "fwd" : "back")
                          << " n" << event->normalizedTime;
                const auto name = eventNames.find(event->eventNameHash);
                if (name != eventNames.end()) {
                    std::cerr << " name " << name->second
                              << " hash " << event->eventNameHash << "\n";
                } else {
                    std::cerr << " hash " << event->eventNameHash << "\n";
                }
            }
            if (count > 0) {
                if (const anim::debug::EventRecord* newest = recorder->eventAt(0)) {
                    lastLoggedEventFrame = newest->frameId;
                }
            }
        }
        syncPreviewStatus();
        overlay->update(animator.playable(), deltaTime);
#endif
        const bool snapshotDue =
            options.snapshotFrame > 0 && frame + 1 == options.snapshotFrame;

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

        if (showSkeleton && bgfx::isValid(debugProgram)) {
            const anim::Pose& pose = animator.playable().outputPose();
            anim::computeWorldMatrices(model.skeleton, pose, skeletonWorld);
            skeletonVertices.clear();
            demo::appendSkeletonLines(skeletonVertices, model.skeleton, skeletonWorld,
                                      demo::packDebugColor(80, 210, 255));
            if (!skeletonVertices.empty()) {
                bgfx::update(
                    skeletonVbh, 0,
                    bgfx::copy(skeletonVertices.data(),
                               static_cast<uint32_t>(skeletonVertices.size() *
                                                     sizeof(demo::DebugVertex))));
                bgfx::setVertexBuffer(0, skeletonVbh);
                bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                               BGFX_STATE_DEPTH_TEST_ALWAYS);
                bgfx::submit(0, debugProgram);
            }
        }

#if ANIM_DEBUG_TOOLS
        if (!options.headless) overlay->render();
#endif

        bgfx::frame();
        ++frame;

        if (snapshotDue) {
    #if ANIM_DEBUG_TOOLS
            if (!options.snapshot.empty() &&
                !writeSnapshotFile(animator.playable(), options.snapshot, error)) {
                std::cerr << "write snapshot failed: " << error << "\n";
                exitCode = 1;
            }
            if (exitCode == 0 && !options.snapshotHistory.empty() &&
                !recorder->writeJsonFile(options.snapshotHistory, error)) {
                std::cerr << "write snapshot history failed: " << error << "\n";
                exitCode = 1;
            }
#endif
            break;
        }

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
    if (bgfx::isValid(debugProgram)) bgfx::destroy(debugProgram);
    bgfx::destroy(skeletonVbh);
    bgfx::destroy(ibh);
    bgfx::destroy(vbh);
    bgfx::shutdown();
    return exitCode;
}
