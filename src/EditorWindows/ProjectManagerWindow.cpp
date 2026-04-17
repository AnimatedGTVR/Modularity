#include "Engine.h"
#include "ModelLoader.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cfloat>
#include <cmath>
#include <fstream>
#include <functional>
#include <sstream>
#include <unordered_set>
#include <optional>
#include <future>
#include <chrono>
#include <future>

#ifdef _WIN32
#include <shlobj.h>
#endif

#pragma region ImGui Helpers
namespace ImGui {

// Animated progress bar that keeps circles moving while work happens in the background.
bool BufferingBar(const char* label, float value, const ImVec2& size_arg, const ImU32& bg_col, const ImU32& fg_col) {
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);

    ImVec2 pos = window->DC.CursorPos;
    ImVec2 size = size_arg;
    size.x -= style.FramePadding.x * 2;

    const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
    ItemSize(bb, style.FramePadding.y);
    if (!ItemAdd(bb, id))
        return false;

    const float circleStart = size.x * 0.7f;
    const float circleEnd = size.x;
    const float circleWidth = circleEnd - circleStart;

    window->DrawList->AddRectFilled(bb.Min, ImVec2(pos.x + circleStart, bb.Max.y), bg_col);
    window->DrawList->AddRectFilled(bb.Min, ImVec2(pos.x + circleStart * value, bb.Max.y), fg_col);

    const float t = g.Time;
    const float r = size.y / 2;
    const float speed = 1.5f;

    const float a = speed * 0;
    const float b = speed * 0.333f;
    const float c = speed * 0.666f;

    const float o1 = (circleWidth + r) * (t + a - speed * (int)((t + a) / speed)) / speed;
    const float o2 = (circleWidth + r) * (t + b - speed * (int)((t + b) / speed)) / speed;
    const float o3 = (circleWidth + r) * (t + c - speed * (int)((t + c) / speed)) / speed;

    window->DrawList->AddCircleFilled(ImVec2(pos.x + circleEnd - o1, bb.Min.y + r), r, bg_col);
    window->DrawList->AddCircleFilled(ImVec2(pos.x + circleEnd - o2, bb.Min.y + r), r, bg_col);
    window->DrawList->AddCircleFilled(ImVec2(pos.x + circleEnd - o3, bb.Min.y + r), r, bg_col);
    return true;
}

// Simple loading spinner for indeterminate tasks.
bool Spinner(const char* label, float radius, int thickness, const ImU32& color) {
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);

    ImVec2 pos = window->DC.CursorPos;
    ImVec2 size((radius) * 2, (radius + style.FramePadding.y) * 2);

    const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
    ItemSize(bb, style.FramePadding.y);
    if (!ItemAdd(bb, id))
        return false;

    window->DrawList->PathClear();

    int num_segments = 30;
    int start = abs(ImSin(g.Time * 1.8f) * (num_segments - 5));

    const float a_min = IM_PI * 2.0f * ((float)start) / (float)num_segments;
    const float a_max = IM_PI * 2.0f * ((float)num_segments - 3) / (float)num_segments;

    const ImVec2 centre = ImVec2(pos.x + radius, pos.y + radius + style.FramePadding.y);

    for (int i = 0; i < num_segments; i++) {
        const float a = a_min + ((float)i / (float)num_segments) * (a_max - a_min);
        window->DrawList->PathLineTo(ImVec2(centre.x + ImCos(a + g.Time * 8) * radius,
                                            centre.y + ImSin(a + g.Time * 8) * radius));
    }

    window->DrawList->PathStroke(color, false, thickness);
    return true;
}

bool ProgressCircle(const char* label, float radius, float thickness, float value,
                    const ImU32& color, const ImU32& bgColor) {
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);

    ImVec2 pos = window->DC.CursorPos;
    ImVec2 size((radius) * 2, (radius + style.FramePadding.y) * 2);
    const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
    ItemSize(bb, style.FramePadding.y);
    if (!ItemAdd(bb, id))
        return false;

    ImVec2 centre = ImVec2(pos.x + radius, pos.y + radius + style.FramePadding.y);
    float startAngle = -IM_PI * 0.5f;
    float endAngle = startAngle + IM_PI * 2.0f * ImClamp(value, 0.0f, 1.0f);

    window->DrawList->AddCircle(centre, radius, bgColor, 32, thickness);
    window->DrawList->PathClear();
    window->DrawList->PathArcTo(centre, radius, startAngle, endAngle, 32);
    window->DrawList->PathStroke(color, false, thickness);
    return true;
}

} // namespace ImGui
#pragma endregion

#pragma region Package Task State
namespace {
static void ComputeCoverUV(int texW, int texH, const ImVec2& targetSize, ImVec2& uv0, ImVec2& uv1) {
    uv0 = ImVec2(0.0f, 0.0f);
    uv1 = ImVec2(1.0f, 1.0f);
    if (texW <= 0 || texH <= 0 || targetSize.x <= 0.0f || targetSize.y <= 0.0f) return;

    const float texAspect = static_cast<float>(texW) / static_cast<float>(texH);
    const float targetAspect = targetSize.x / targetSize.y;

    if (texAspect > targetAspect) {
        const float newW = static_cast<float>(texH) * targetAspect;
        const float xOffset = (static_cast<float>(texW) - newW) * 0.5f;
        uv0.x = xOffset / static_cast<float>(texW);
        uv1.x = (xOffset + newW) / static_cast<float>(texW);
    } else if (texAspect < targetAspect) {
        const float newH = static_cast<float>(texW) / targetAspect;
        const float yOffset = (static_cast<float>(texH) - newH) * 0.5f;
        uv0.y = yOffset / static_cast<float>(texH);
        uv1.y = (yOffset + newH) / static_cast<float>(texH);
    }
}

static void DrawImageCover(ImDrawList* list, ImTextureID texId, const ImVec2& min,
                           const ImVec2& max, int texW, int texH, ImU32 tint, float rounding) {
    ImVec2 uv0, uv1;
    ComputeCoverUV(texW, texH, ImVec2(max.x - min.x, max.y - min.y), uv0, uv1);
    if (rounding > 0.0f) {
        list->AddImageRounded(texId, min, max, uv0, uv1, tint, rounding);
    } else {
        list->AddImage(texId, min, max, uv0, uv1, tint);
    }
}

static void DrawBlurredImageCover(ImDrawList* list, ImTextureID texId, const ImVec2& min,
                                  const ImVec2& max, int texW, int texH, float alpha, float radius) {
    const ImVec2 offsets[] = {
        ImVec2(0.0f, 0.0f),
        ImVec2(radius, 0.0f),
        ImVec2(-radius, 0.0f),
        ImVec2(0.0f, radius),
        ImVec2(0.0f, -radius),
        ImVec2(radius * 0.7f, radius * 0.7f),
        ImVec2(-radius * 0.7f, radius * 0.7f),
        ImVec2(radius * 0.7f, -radius * 0.7f),
        ImVec2(-radius * 0.7f, -radius * 0.7f),
        ImVec2(radius * 1.8f, 0.0f),
        ImVec2(-radius * 1.8f, 0.0f),
        ImVec2(0.0f, radius * 1.8f),
        ImVec2(0.0f, -radius * 1.8f)
    };
    const float weights[] = {
        0.227027f,
        0.1945946f, 0.1945946f, 0.1945946f, 0.1945946f,
        0.1216216f, 0.1216216f, 0.1216216f, 0.1216216f,
        0.054054f, 0.054054f, 0.054054f, 0.054054f
    };
    float total = 0.0f;
    for (float w : weights) total += w;

    for (size_t i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
        float w = weights[i] / total;
        ImU32 tint = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, alpha * w));
        DrawImageCover(list, texId,
                       ImVec2(min.x + offsets[i].x, min.y + offsets[i].y),
                       ImVec2(max.x + offsets[i].x, max.y + offsets[i].y),
                       texW, texH, tint, 0.0f);
    }
}

static fs::path MakeRelativeIfInside(const fs::path& absolutePath, const fs::path& baseRoot) {
    if (absolutePath.empty()) return absolutePath;
    std::error_code ec;
    fs::path abs = absolutePath.is_absolute() ? absolutePath : fs::absolute(absolutePath, ec);
    if (ec) abs = absolutePath;
    fs::path rel = fs::relative(abs, baseRoot, ec);
    if (ec || rel.empty()) return abs;
    for (const auto& part : rel) {
        if (part == "..") return abs;
    }
    return rel;
}

static bool SaveScriptsConfig(const fs::path& path, const ScriptBuildConfig& config, const fs::path& projectRoot, std::string& error) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) {
        error = "Failed to create config folder: " + ec.message();
        return false;
    }

    std::ofstream file(path);
    if (!file.is_open()) {
        error = "Failed to open scripts config for writing: " + path.string();
        return false;
    }

    file << "# scripts.modu\n";
    file << "cppStandard=" << config.cppStandard << "\n";
    file << "scriptsDir=" << MakeRelativeIfInside(config.scriptsDir, projectRoot).generic_string() << "\n";
    file << "outDir=" << MakeRelativeIfInside(config.outDir, projectRoot).generic_string() << "\n";
    for (const auto& include : config.includeDirs) {
        file << "includeDir=" << MakeRelativeIfInside(include, projectRoot).generic_string() << "\n";
    }
    for (const auto& define : config.defines) {
        if (define.empty()) continue;
        file << "define=" << define << "\n";
    }
    for (const auto& lib : config.linuxLinkLibs) {
        if (lib.empty()) continue;
        file << "linux.linkLib=" << lib << "\n";
    }
    for (const auto& lib : config.windowsLinkLibs) {
        if (lib.empty()) continue;
        file << "win.linkLib=" << lib << "\n";
    }
    return true;
}

static float SmoothApproach(float current, float target, float speed, float dt) {
    const float blend = 1.0f - std::exp(-speed * dt);
    return current + (target - current) * blend;
}

static float EaseOutCubic(float t) {
    t = ImClamp(t, 0.0f, 1.0f);
    const float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

static float EaseInOutCubic(float t) {
    t = ImClamp(t, 0.0f, 1.0f);
    if (t < 0.5f) {
        return 4.0f * t * t * t;
    }
    const float f = -2.0f * t + 2.0f;
    return 1.0f - (f * f * f) * 0.5f;
}

static float EaseOutBack(float t) {
    t = ImClamp(t, 0.0f, 1.0f);
    constexpr float c1 = 1.70158f;
    constexpr float c3 = c1 + 1.0f;
    const float p = t - 1.0f;
    return 1.0f + c3 * p * p * p + c1 * p * p;
}

struct LauncherIntroTimings {
    float fadeIn = 0.16f;
    float popIn = 0.44f;
    float hold = 0.10f;
    float drift = 0.64f;
};

struct LauncherIntroState {
    float elapsed = 0.0f;
    float introTotal = 0.0f;
    float textAlpha = 0.0f;
    float popT = 1.0f;
    float driftT = 1.0f;
    float driftEase = 1.0f;
    float contentRevealT = 1.0f;
    bool finished = true;
};

static LauncherIntroState EvaluateLauncherIntro(double now,
                                                double introStartTime,
                                                bool forceFinished,
                                                const LauncherIntroTimings& timings) {
    LauncherIntroState state;
    const float popStart = timings.fadeIn;
    const float driftStart = popStart + timings.popIn + timings.hold;
    state.introTotal = driftStart + timings.drift;
    state.elapsed = forceFinished
        ? state.introTotal
        : static_cast<float>(now - introStartTime);

    if (timings.fadeIn <= 0.0001f) {
        state.textAlpha = 1.0f;
    } else {
        state.textAlpha = ImClamp(state.elapsed / timings.fadeIn, 0.0f, 1.0f);
    }

    state.popT = timings.popIn <= 0.0001f
        ? 1.0f
        : ImClamp((state.elapsed - popStart) / timings.popIn, 0.0f, 1.0f);
    state.driftT = timings.drift <= 0.0001f
        ? 1.0f
        : ImClamp((state.elapsed - driftStart) / timings.drift, 0.0f, 1.0f);
    state.driftEase = EaseInOutCubic(state.driftT);
    const float contentWindow = std::max(0.001f, timings.drift + timings.hold);
    state.contentRevealT = ImClamp((state.elapsed - (driftStart - timings.hold * 0.55f)) / contentWindow, 0.0f, 1.0f);
    state.finished = forceFinished || state.driftT >= 1.0f;
    return state;
}

struct LauncherPackageSnapshot {
    std::string fingerprint;
    std::string sourceProjectName;
    fs::path sourceProjectRoot;
    std::vector<std::string> packageLabels;
    size_t externalCount = 0;
    bool hasSource = false;
};

static std::string TrimCopy(const std::string& value) {
    const size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

static std::vector<std::string> SplitString(const std::string& input, char delim) {
    std::vector<std::string> out;
    std::stringstream ss(input);
    std::string token;
    while (std::getline(ss, token, delim)) {
        out.push_back(token);
    }
    return out;
}

static fs::path ResolveRecentProjectRoot(const std::string& recentPath) {
    fs::path path(recentPath);
    if (path.empty()) return {};
    if (path.extension() == ".modu") {
        return path.parent_path();
    }
    if (fs::is_directory(path)) {
        return path;
    }
    if (path.has_parent_path()) {
        return path.parent_path();
    }
    return path;
}

static bool ParsePackageManifestLabels(const fs::path& manifestPath,
                                       std::vector<std::string>& outLabels,
                                       size_t& outExternalCount) {
    outLabels.clear();
    outExternalCount = 0;

    std::ifstream file(manifestPath);
    if (!file.is_open()) {
        return false;
    }

    std::unordered_set<std::string> seen;
    std::string line;
    while (std::getline(file, line)) {
        const std::string cleaned = TrimCopy(line);
        if (cleaned.empty() || cleaned[0] == '#') continue;

        if (cleaned.rfind("package=", 0) == 0) {
            const std::string id = TrimCopy(cleaned.substr(8));
            if (!id.empty() && seen.insert(id).second) {
                outLabels.push_back(id);
            }
            continue;
        }

        std::string sourceTag;
        std::string sourceSuffix;
        size_t sourcePrefixLen = 0;
        if (cleaned.rfind("git=", 0) == 0) {
            sourceTag = "git";
            sourceSuffix = " (Git)";
            sourcePrefixLen = 4;
        } else if (cleaned.rfind("modupak=", 0) == 0) {
            sourceTag = "modupak";
            sourceSuffix = " (.modupak)";
            sourcePrefixLen = 8;
        } else {
            continue;
        }

        const auto parts = SplitString(cleaned.substr(sourcePrefixLen), '|');
        if (parts.empty()) continue;

        const std::string id = TrimCopy(parts[0]);
        std::string label = id;
        if (parts.size() > 1) {
            const std::string parsedName = TrimCopy(parts[1]);
            if (!parsedName.empty()) {
                label = parsedName;
            }
        }

        if (label.empty()) continue;
        const std::string key = sourceTag + ":" + id;
        if (seen.insert(key).second) {
            outLabels.push_back(label + sourceSuffix);
            outExternalCount++;
        }
    }

    return true;
}

static std::string BuildLauncherSnapshotFingerprint(const ProjectManager& manager) {
    std::ostringstream oss;
    const size_t count = std::min<size_t>(manager.recentProjects.size(), 16);
    oss << count << "|";
    for (size_t i = 0; i < count; ++i) {
        const auto& rp = manager.recentProjects[i];
        oss << rp.name << "|" << rp.path << "|" << rp.lastOpened << ";";
    }
    return oss.str();
}

static const LauncherPackageSnapshot& GetLauncherPackageSnapshot(const ProjectManager& manager, bool forceRefresh = false) {
    static LauncherPackageSnapshot cache;

    const std::string fingerprint = BuildLauncherSnapshotFingerprint(manager);
    if (!forceRefresh && cache.fingerprint == fingerprint) {
        return cache;
    }

    cache = LauncherPackageSnapshot{};
    cache.fingerprint = fingerprint;

    for (const auto& rp : manager.recentProjects) {
        const fs::path projectRoot = ResolveRecentProjectRoot(rp.path);
        if (projectRoot.empty()) continue;

        const fs::path manifestPath = projectRoot / "packages.modu";
        if (!fs::exists(manifestPath)) continue;

        std::vector<std::string> labels;
        size_t externalCount = 0;
        if (!ParsePackageManifestLabels(manifestPath, labels, externalCount)) {
            continue;
        }

        cache.hasSource = true;
        cache.sourceProjectRoot = projectRoot;
        cache.sourceProjectName = rp.name.empty() ? projectRoot.filename().string() : rp.name;
        cache.packageLabels = std::move(labels);
        cache.externalCount = externalCount;
        break;
    }

    return cache;
}

struct LauncherTemplateEntry {
    std::string displayName;
    fs::path projectRoot;
    fs::path projectFile;
    fs::path previewImage;
};

static fs::path GetTemplateProjectsRoot() {
    std::error_code ec;
    fs::path root = fs::current_path() / "Template-Projects";
    fs::create_directories(root, ec);
    return root;
}

static fs::path FindTemplateProjectFile(const fs::path& candidateRoot) {
    const fs::path direct = candidateRoot / "project.modu";
    if (fs::exists(direct)) {
        return direct;
    }

    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(candidateRoot, ec);
         it != fs::recursive_directory_iterator(); ++it) {
        if (ec) break;
        if (!it->is_regular_file()) continue;
        if (it->path().filename() == "project.modu") {
            return it->path();
        }
    }
    return {};
}

static std::vector<LauncherTemplateEntry> GatherTemplateEntries() {
    std::vector<LauncherTemplateEntry> templates;
    const fs::path templatesRoot = GetTemplateProjectsRoot();
    if (!fs::exists(templatesRoot)) {
        return templates;
    }

    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(templatesRoot, ec)) {
        if (ec) break;
        if (!entry.is_directory()) continue;

        fs::path projectFile = FindTemplateProjectFile(entry.path());
        if (projectFile.empty()) continue;

        LauncherTemplateEntry t;
        t.projectFile = projectFile;
        t.projectRoot = projectFile.parent_path();
        t.displayName = entry.path().filename().string();
        t.previewImage = t.projectRoot / "ProjectUserSettings" / "ProjectPreview.png";
        templates.push_back(std::move(t));
    }

    std::sort(templates.begin(), templates.end(),
              [](const LauncherTemplateEntry& a, const LauncherTemplateEntry& b) {
                  return a.displayName < b.displayName;
              });
    return templates;
}

constexpr const char* kModularityTermsVersion = "modularity-tos-v1";

constexpr const char* kModularityTermsText = R"(Modularity Engine Terms of Service

Copyright (c) 2025-2026
Shock Interactive LLC and Tareno Labs LLC

By using the Modularity Engine, editor, runtime, tools, or official components, you agree to these Terms of Service.

1. Definitions
- Engine: the Modularity engine source code, core runtime, editor, tools, and official components distributed as part of the Modularity project.
- Software Built Using the Engine: any video game, application, service, tool, or other software created using the Engine.
- Marketplace Content: assets, plugins, ModuPaks, extensions, templates, or other packages intended for use with the Engine.

2. Permission to Use
You may use, copy, modify, merge, publish, and distribute software built using the Engine, free of charge, subject to these terms.

3. Commercial Use
You may use the Engine to develop commercial or closed-source software.

You may:
- Sell video games, applications, or services built using the Engine.
- Distribute commercial software built using the Engine.
- Keep the source code of software built using the Engine private.

Software built using the Engine is not required to be open source.

4. Marketplace Content
You may create and distribute Marketplace Content for use with the Engine.

Marketplace Content:
- May be distributed commercially or free of charge.
- May be licensed under any license chosen by its creator, including closed-source licenses.
- Does not automatically become part of the Engine.

Creators retain ownership and licensing control over their Marketplace Content.

5. Modifications to the Engine
You may modify the Engine for personal, research, or internal use without publishing those modifications.

If you distribute the Engine or a modified version of the Engine:
- The full corresponding source code of the modified Engine must be released under these same terms.
- The source code must be made available in a publicly accessible location without unreasonable access restrictions.
- Distributed versions must include a clear notice describing the modifications made.
- All original copyright notices must be retained.

These requirements apply only to the Engine itself, not to software built using the Engine.

6. Distribution of the Engine
You may distribute the Engine in original or modified form only under these terms.

You may not:
- Sell the Engine itself as a standalone commercial engine product.
- Sublicense the Engine under a different license.
- Rebrand the Engine and present it as a different engine.

Forks or modified versions must clearly acknowledge that they are based on the Modularity Engine.

7. Attribution
Software distributed using the Engine must include visible attribution to Modularity in at least one of the following locations:
- Software credits
- Documentation
- An About section
- A similar visible acknowledgment

Example attribution:
Powered by the Modularity Engine

8. Trademarks
The names "Modularity" and "ModuEngine" are trademarks of Shock Interactive LLC and Tareno Labs LLC.
These trademarks may not be used to imply endorsement, official status, or affiliation without explicit permission from the trademark holders.

9. Disclaimer
The Engine is provided "as is", without warranty of any kind, express or implied, including merchantability, fitness for a particular purpose, and noninfringement.

In no event shall the authors or copyright holders be liable for any claim, damages, or other liability arising from the use of the Engine.)";
} // namespace
#pragma endregion

bool Engine::requiresTermsOfServiceAcceptance() const {
#ifdef MODULARITY_PLAYER
    return false;
#else
    return projectManager.acceptedTermsVersion != kModularityTermsVersion;
#endif
}

void Engine::renderTermsOfServiceModal() {
#ifdef MODULARITY_PLAYER
    return;
#else
    if (!requiresTermsOfServiceAcceptance()) {
        termsPopupOpened = false;
        return;
    }
    if (showLauncher && !launcherIntroFinished) {
        return;
    }

    if (!termsPopupOpened) {
        ImGui::OpenPopup("Modularity Terms of Service");
        termsPopupOpened = true;
    }

    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    const ImVec2 popupSize(
        ImClamp(displaySize.x * 0.72f, 640.0f, 920.0f),
        ImClamp(displaySize.y * 0.80f, 520.0f, 760.0f));
    ImGui::SetNextWindowPos(ImVec2(displaySize.x * 0.5f, displaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(popupSize, ImGuiCond_Appearing);

    const ImGuiWindowFlags popupFlags =
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::BeginPopupModal("Modularity Terms of Service", nullptr, popupFlags)) {
        auto centeredText = [](const char* text, const ImVec4& color) {
            const ImVec2 textSize = ImGui::CalcTextSize(text);
            const float avail = ImGui::GetContentRegionAvail().x;
            if (avail > textSize.x) {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - textSize.x) * 0.5f);
            }
            ImGui::TextColored(color, "%s", text);
        };

        auto beginCenteredColumn = [](float maxWidth) {
            const float avail = ImGui::GetContentRegionAvail().x;
            const float width = ImMax(120.0f, ImMin(maxWidth, avail));
            if (avail > width) {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - width) * 0.5f);
            }
            ImGui::BeginGroup();
            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + width);
            return width;
        };

        auto endCenteredColumn = []() {
            ImGui::PopTextWrapPos();
            ImGui::EndGroup();
        };

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.0f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.12f, 0.16f, 0.96f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.24f, 0.30f, 0.40f, 0.95f));

        centeredText("Modularity Engine Terms of Service", ImVec4(0.92f, 0.96f, 1.00f, 1.0f));
        ImGui::Spacing();
        beginCenteredColumn(520.0f);
        ImGui::TextWrapped("Please review these terms before using Modularity. Accepting records this version for this installation so you only see it again if the terms change.");
        endCenteredColumn();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        const float footerHeight = ImGui::GetFrameHeightWithSpacing() * 3.6f;
        if (ImGui::BeginChild("TermsOfServiceScroll", ImVec2(0.0f, -footerHeight), true, ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
            beginCenteredColumn(720.0f);

            auto renderWrappedParagraph = [](const std::string& text, const ImVec4* color = nullptr) {
                if (color) {
                    ImGui::PushStyleColor(ImGuiCol_Text, *color);
                }
                ImGui::TextWrapped("%s", text.c_str());
                if (color) {
                    ImGui::PopStyleColor();
                }
            };

            auto renderWrappedBullet = [](const std::string& text) {
                const float bulletStartX = ImGui::GetCursorPosX();
                ImGui::Bullet();
                const float textStartX = ImGui::GetCursorPosX();
                ImGui::SameLine(0.0f, 6.0f);
                ImGui::PushTextWrapPos(textStartX + ImGui::GetContentRegionAvail().x);
                ImGui::TextUnformatted(text.c_str());
                ImGui::PopTextWrapPos();

                const float lineHeight = ImGui::GetTextLineHeight();
                if (ImGui::GetCursorPosX() < bulletStartX) {
                    ImGui::SetCursorPosX(bulletStartX);
                }
                if (ImGui::GetTextLineHeightWithSpacing() > lineHeight) {
                    ImGui::Spacing();
                }
            };

            std::istringstream termsStream(kModularityTermsText);
            std::string line;
            while (std::getline(termsStream, line)) {
                if (line.empty()) {
                    ImGui::Spacing();
                    continue;
                }

                if (line == "Modularity Engine Terms of Service") {
                    continue;
                }

                if (line.rfind("Copyright", 0) == 0) {
                    ImGui::TextDisabled("%s", line.c_str());
                    continue;
                }

                if (line.rfind("By using", 0) == 0) {
                    ImGui::Spacing();
                    renderWrappedParagraph(line);
                    continue;
                }

                if (std::isdigit(static_cast<unsigned char>(line[0])) && line.find('.') != std::string::npos) {
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(0.88f, 0.93f, 1.00f, 1.0f), "%s", line.c_str());
                    continue;
                }

                if (line.rfind("- ", 0) == 0) {
                    renderWrappedBullet(line.substr(2));
                    continue;
                }

                if (line.rfind("Example attribution:", 0) == 0) {
                    ImGui::Spacing();
                    const ImVec4 accentColor(0.70f, 0.80f, 0.96f, 1.0f);
                    renderWrappedParagraph(line, &accentColor);
                    continue;
                }

                renderWrappedParagraph(line);
            }

            endCenteredColumn();
        }
        ImGui::EndChild();

        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);

        ImGui::Spacing();
        beginCenteredColumn(520.0f);
        ImGui::TextDisabled("Version: %s", kModularityTermsVersion);
        endCenteredColumn();
        ImGui::Spacing();

        const ImGuiStyle& style = ImGui::GetStyle();
        const float declineWidth = 170.0f;
        const float acceptWidth = 190.0f;
        const float totalButtonWidth = declineWidth + style.ItemSpacing.x + acceptWidth;
        const float buttonAvail = ImGui::GetContentRegionAvail().x;
        if (buttonAvail > totalButtonWidth) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (buttonAvail - totalButtonWidth) * 0.5f);
        }

        if (ImGui::Button("Decline and Exit", ImVec2(170.0f, 0.0f))) {
            glfwSetWindowShouldClose(editorWindow, GLFW_TRUE);
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.38f, 0.66f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.44f, 0.74f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.18f, 0.33f, 0.58f, 1.0f));
        if (ImGui::Button("Accept and Continue", ImVec2(190.0f, 0.0f))) {
            projectManager.acceptedTermsVersion = kModularityTermsVersion;
            projectManager.saveLauncherSettings();
            termsPopupOpened = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor(3);

        ImGui::EndPopup();
    }
#endif
}

#pragma region Launcher
void Engine::renderLauncher() {
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 displaySize = io.DisplaySize;
    const double now = glfwGetTime();
    if (!launcherIntroStarted) {
        launcherIntroStarted = true;
        launcherIntroStartTime = now;
    }
    if (!launcherIntroSoundPlayed) {
        playEditorFeedbackPreview("Resources/Sounds/ModuIntro.mp3", 0.85f, false, EditorFeedbackSoundCategory::Boot);
        launcherIntroSoundPlayed = true;
    }

    const LauncherIntroTimings introTimings{};
    LauncherIntroState introState = EvaluateLauncherIntro(now, launcherIntroStartTime, launcherIntroFinished, introTimings);
    if (!launcherIntroFinished && introState.finished) {
        launcherIntroFinished = true;
        introState = EvaluateLauncherIntro(now, launcherIntroStartTime, true, introTimings);
    }

    const float transitionDuration = 0.45f;
    float transitionT = 0.0f;
    if (launcherTransitionActive) {
        transitionT = ImClamp(static_cast<float>((now - launcherTransitionStartTime) / transitionDuration), 0.0f, 1.0f);
    }
    const float transitionEase = 1.0f - std::pow(1.0f - transitionT, 3.0f);
    const float transitionAlpha = 1.0f - transitionEase;
    const float menuBuildT = launcherIntroFinished ? 1.0f : EaseOutCubic(introState.contentRevealT);
    const float sidebarBuildSeed = launcherIntroFinished ? 1.0f : ImClamp((menuBuildT - 0.02f) / 0.92f, 0.0f, 1.0f);
    const float contentBuildSeed = launcherIntroFinished ? 1.0f : ImClamp((menuBuildT - 0.16f) / 0.94f, 0.0f, 1.0f);
    const float sidebarBuildT = launcherIntroFinished ? 1.0f : std::pow(sidebarBuildSeed, 1.32f);
    const float contentBuildT = launcherIntroFinished ? 1.0f : std::pow(contentBuildSeed, 1.40f);
    const float introMenuScale = launcherIntroFinished ? 1.0f : ImLerp(1.07f, 1.0f, menuBuildT);
    const float uiScale = (1.0f + 0.06f * transitionEase) * introMenuScale;
    const float introOffsetT = launcherIntroFinished ? 0.0f : (1.0f - introState.driftEase);
    const float contentAlpha = launcherIntroFinished ? 1.0f : ImClamp(0.12f + introState.contentRevealT * 0.88f, 0.0f, 1.0f);
    const float introHeroOffsetY = -90.0f * uiScale * introOffsetT;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f * uiScale, 24.0f * uiScale));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f * uiScale);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 18.0f * uiScale);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(displaySize);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize   |
        ImGuiWindowFlags_NoMove     |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoDocking  |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    if (ImGui::Begin("Launcher", nullptr, flags)) {
        const float sidebarWidth = ImClamp(displaySize.x * 0.25f,
                                           230.0f * uiScale,
                                           320.0f * uiScale);
        const ImVec4 bgTopLeft = ImVec4(0.09f, 0.10f, 0.13f, 1.0f);
        const ImVec4 bgTopRight = ImVec4(0.11f, 0.13f, 0.17f, 1.0f);
        const ImVec4 bgBottomRight = ImVec4(0.06f, 0.07f, 0.10f, 1.0f);
        const ImVec4 bgBottomLeft = ImVec4(0.08f, 0.09f, 0.12f, 1.0f);
        const ImVec4 shellBg = ImVec4(0.10f, 0.11f, 0.14f, 0.97f);
        const ImVec4 sidebarBg = ImVec4(0.12f, 0.13f, 0.17f, 1.0f);

        static int launcherSection = 0; // 0 = Projects, 1 = Installed Packages, 2 = Settings
        static int launcherSectionPrevious = 0;
        static double launcherSectionSwitchTime = 0.0;
        static char launcherSearch[160] = "";
        static float projectsPanelVisual = 0.0f;

        const float frameDt = ImGui::GetIO().DeltaTime;
        const float projectsPanelTarget = projectManager.showNewProjectDialog ? 1.0f : 0.0f;
        projectsPanelVisual = SmoothApproach(projectsPanelVisual, projectsPanelTarget, 12.0f, frameDt);
        const float sectionAnimDuration = 0.26f;
        const float sectionAnimT = ImClamp(static_cast<float>((glfwGetTime() - launcherSectionSwitchTime) / sectionAnimDuration), 0.0f, 1.0f);
        const float sectionAnimEase = EaseOutCubic(sectionAnimT);

        const bool launcherBusy = projectLoadInProgress || sceneLoadInProgress ||
                                  launcherTransitionActive || launcherTransitionPendingHide;

        auto openNewProjectDialog = [&]() {
            projectManager.showNewProjectDialog = true;
            projectManager.errorMessage.clear();
            projectManager.newProjectImportLastPackages = true;
            projectManager.newProjectTemplatePath.clear();
            projectManager.newProjectTemplateName.clear();
            std::memset(projectManager.newProjectName, 0, sizeof(projectManager.newProjectName));
            projectManager.newProjectPipelineMode = 0;
            projectManager.newProjectRendererMode = 0;
            (void)GetTemplateProjectsRoot();
            if (projectManager.defaultProjectLocation[0] != '\0') {
                std::snprintf(projectManager.newProjectLocation,
                              sizeof(projectManager.newProjectLocation),
                              "%s",
                              projectManager.defaultProjectLocation);
            } else {
                const std::string fallback = (fs::current_path() / "Projects").string();
                std::snprintf(projectManager.newProjectLocation,
                              sizeof(projectManager.newProjectLocation),
                              "%s",
                              fallback.c_str());
            }
        };

        auto openProjectDialog = [&]() {
            projectManager.showOpenProjectDialog = true;
            projectManager.errorMessage.clear();
        };

        auto setLauncherSection = [&](int newSection) {
            if (launcherSection == newSection) return;
            launcherSectionPrevious = launcherSection;
            launcherSection = newSection;
            launcherSectionSwitchTime = glfwGetTime();
        };

        auto launchRecentProject = [&](const RecentProject& rp, const ImVec2& focus) {
            if (launcherBusy) return;
            playEditorFeedbackPreview("Resources/Sounds/Selection.mp3", 0.95f, false, EditorFeedbackSoundCategory::Click);
            launcherTransitionActive = true;
            launcherTransitionPendingHide = false;
            launcherTransitionStartTime = glfwGetTime();
            launcherTransitionFocus = focus;
            fs::path previewPath = getProjectPreviewPath(rp.path);
            if (!previewPath.empty() && fs::exists(previewPath)) {
                launcherLoadingPreviewPath = previewPath.string();
            }
            OpenProjectPath(rp.path);
        };

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 windowPos = ImGui::GetWindowPos();
        ImVec2 windowSize = ImGui::GetWindowSize();

        ImTextureID previewImageId = static_cast<ImTextureID>(0);
        int previewImageWidth = 0;
        int previewImageHeight = 0;
        if ((projectLoadInProgress || sceneLoadInProgress || launcherTransitionActive || launcherTransitionPendingHide) &&
            !launcherLoadingPreviewPath.empty()) {
            fs::path previewPath = launcherLoadingPreviewPath;
            if (fs::exists(previewPath)) {
                if (usingVulkan() && vulkanRendererInitialized && vulkanRenderer) {
                    previewImageId = vulkanRenderer->getOrCreateUIImage(previewPath.string(),
                                                                       &previewImageWidth,
                                                                       &previewImageHeight);
                } else if (!usingVulkan()) {
                    if (Texture* previewTexture = renderer.getTexture(previewPath.string())) {
                        previewImageId = (ImTextureID)(intptr_t)previewTexture->GetID();
                        previewImageWidth = previewTexture->GetWidth();
                        previewImageHeight = previewTexture->GetHeight();
                    }
                }
            }
        }

        if (previewImageId != static_cast<ImTextureID>(0)) {
            DrawBlurredImageCover(drawList,
                                  previewImageId,
                                  windowPos,
                                  ImVec2(windowPos.x + windowSize.x, windowPos.y + windowSize.y),
                                  previewImageWidth,
                                  previewImageHeight,
                                  0.55f,
                                  10.0f * uiScale);
        }

        const float bgAlpha = (previewImageId != static_cast<ImTextureID>(0)) ? 0.58f : 1.0f;
        ImVec4 gradTL = bgTopLeft; gradTL.w = bgAlpha;
        ImVec4 gradTR = bgTopRight; gradTR.w = bgAlpha;
        ImVec4 gradBR = bgBottomRight; gradBR.w = bgAlpha;
        ImVec4 gradBL = bgBottomLeft; gradBL.w = bgAlpha;
        drawList->AddRectFilledMultiColor(
            windowPos,
            ImVec2(windowPos.x + windowSize.x, windowPos.y + windowSize.y),
            ImGui::GetColorU32(gradTL),
            ImGui::GetColorU32(gradTR),
            ImGui::GetColorU32(gradBR),
            ImGui::GetColorU32(gradBL));

        if (launcherTransitionActive) {
            ImVec2 focus = launcherTransitionFocus;
            if (focus.x <= 0.0f && focus.y <= 0.0f) {
                focus = ImVec2(windowPos.x + windowSize.x * 0.5f, windowPos.y + windowSize.y * 0.5f);
            }
            const float maxRadius = std::sqrt(windowSize.x * windowSize.x + windowSize.y * windowSize.y);
            const float radius = ImLerp(64.0f * uiScale, maxRadius, transitionEase);
            const float overlayAlpha = 0.18f * transitionEase;
            drawList->AddCircleFilled(focus, radius, ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, overlayAlpha)), 64);
        }

        ImVec2 contentStart = ImGui::GetCursorPos();
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, contentAlpha * transitionAlpha);
        ImGui::SetWindowFontScale(uiScale);
        if (!launcherIntroFinished) {
            ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
        }

        const float shellInsetX = 10.0f * uiScale;
        const float shellInsetTop = 10.0f * uiScale;
        const float shellInsetBottom = 10.0f * uiScale;
        const float paneGap = 12.0f * uiScale;
        const float shellHeight = ImMax(0.0f, ImGui::GetContentRegionAvail().y - shellInsetTop - shellInsetBottom);

        ImGui::SetCursorPos(ImVec2(contentStart.x + shellInsetX,
                                   contentStart.y + introHeroOffsetY + shellInsetTop));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, shellBg);
        ImGui::BeginChild("LauncherShell", ImVec2(-shellInsetX, shellHeight), false);
        ImGui::PopStyleColor();

        const ImVec2 sidebarPadding(14.0f * uiScale, 14.0f * uiScale);
        const ImVec2 contentPadding(14.0f * uiScale, 12.0f * uiScale);
        const ImVec2 sectionInset(0.0f, 0.0f);
        const ImVec2 shellLayoutOrigin = ImGui::GetCursorPos();
        const ImVec2 shellLayoutAvail = ImGui::GetContentRegionAvail();
        const float paneHeight = ImMax(0.0f, shellLayoutAvail.y);
        const float contentPaneWidth = ImMax(0.0f, shellLayoutAvail.x - sidebarWidth - paneGap);

        const float sidebarPaneT = launcherIntroFinished ? 1.0f : EaseOutCubic(sidebarBuildT);
        const float sidebarPaneOffsetX = -(1.0f - sidebarPaneT) * 88.0f * uiScale;
        const float sidebarPaneOffsetY = (1.0f - sidebarPaneT) * 14.0f * uiScale;
        const float sidebarPaneAlpha = ImLerp(0.0f, 1.0f, sidebarPaneT);

        const float contentPaneT = launcherIntroFinished ? 1.0f : EaseOutCubic(contentBuildT);
        const float contentPaneOffsetX = (1.0f - contentPaneT) * 96.0f * uiScale;
        const float contentPaneOffsetY = (1.0f - contentPaneT) * 12.0f * uiScale;
        const float contentPaneAlpha = ImLerp(0.0f, 1.0f, contentPaneT);

        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, sidebarPaneAlpha);
        ImGui::SetCursorPos(ImVec2(shellLayoutOrigin.x + sidebarPaneOffsetX,
                                   shellLayoutOrigin.y + sidebarPaneOffsetY));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, sidebarPadding);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, sidebarBg);
        ImGui::BeginChild("LauncherSidebar", ImVec2(sidebarWidth, paneHeight), false);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        int sidebarAnimIndex = 0;
        auto beginSidebarBuildStep = [&](float delayStep = 0.132f) {
            const float delay = std::min(0.86f, delayStep * static_cast<float>(sidebarAnimIndex));
            const float localT = ImClamp(
                (sidebarBuildT - delay) / std::max(0.0001f, 1.0f - delay),
                0.0f,
                1.0f);
            const float eased = EaseOutCubic(localT);
            const float alpha = ImLerp(0.0f, 1.0f, eased);
            const float yOffset = (1.0f - eased) * (24.0f + static_cast<float>(sidebarAnimIndex) * 2.0f) * uiScale;
            const float xOffset = -(1.0f - eased) * 34.0f * uiScale;
            ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + xOffset, ImGui::GetCursorPosY() + yOffset));
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
            ++sidebarAnimIndex;
        };
        auto endSidebarBuildStep = [&]() {
            ImGui::PopStyleVar();
        };

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14.0f * uiScale, 10.0f * uiScale));
        beginSidebarBuildStep();
        ImGui::PushStyleColor(ImGuiCol_Button, launcherSection == 0 ? ImVec4(0.21f, 0.32f, 0.47f, 1.0f) : ImVec4(0.16f, 0.19f, 0.28f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.36f, 0.54f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.21f, 0.32f, 0.47f, 1.0f));
        if (ImGui::Button("Projects", ImVec2(-1, 38.0f * uiScale))) {
            setLauncherSection(0);
        }
        ImGui::PopStyleColor(3);
        endSidebarBuildStep();

        beginSidebarBuildStep();
        ImGui::PushStyleColor(ImGuiCol_Button, launcherSection == 1 ? ImVec4(0.21f, 0.32f, 0.47f, 1.0f) : ImVec4(0.16f, 0.19f, 0.28f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.36f, 0.54f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.21f, 0.32f, 0.47f, 1.0f));
        if (ImGui::Button("Installed Packages", ImVec2(-1, 38.0f * uiScale))) {
            setLauncherSection(1);
        }
        ImGui::PopStyleColor(3);

        endSidebarBuildStep();
        beginSidebarBuildStep();
        ImGui::PushStyleColor(ImGuiCol_Button, launcherSection == 2 ? ImVec4(0.21f, 0.32f, 0.47f, 1.0f) : ImVec4(0.16f, 0.19f, 0.28f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.36f, 0.54f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.21f, 0.32f, 0.47f, 1.0f));
        if (ImGui::Button("Settings", ImVec2(-1, 38.0f * uiScale))) {
            setLauncherSection(2);
        }
        ImGui::PopStyleColor(3);
        endSidebarBuildStep();
        ImGui::PopStyleVar();
        const float footerY = ImGui::GetWindowHeight() - 60.0f * uiScale;
        if (ImGui::GetCursorPosY() < footerY) {
            ImGui::SetCursorPosY(footerY);
        }
        ImGui::Separator();
        beginSidebarBuildStep();
        if (ImGui::Button("Modularity Website", ImVec2(-1, 28.0f * uiScale))) {
            #ifdef _WIN32
            system("start https://moduengine.xyz");
            #else
            system("xdg-open https://moduengine.xyz &");
            #endif
        }
        endSidebarBuildStep();
        beginSidebarBuildStep();
        if (ImGui::Button("Documentation", ImVec2(-1, 28.0f * uiScale))) {
            #ifdef _WIN32
            system("start https://moduengine.xyz/docs");
            #else
            system("xdg-open https://moduengine.xyz/docs &");
            #endif
        }
        endSidebarBuildStep();
        beginSidebarBuildStep();
        if (ImGui::Button("Exit", ImVec2(-1, 28.0f * uiScale))) {
            glfwSetWindowShouldClose(editorWindow, GLFW_TRUE);
        }
        endSidebarBuildStep();
        ImGui::EndChild();
        ImGui::PopStyleVar();

        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, contentPaneAlpha);
        ImGui::SetCursorPos(ImVec2(shellLayoutOrigin.x + sidebarWidth + paneGap + contentPaneOffsetX,
                                   shellLayoutOrigin.y + contentPaneOffsetY));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, contentPadding);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::BeginChild("LauncherContent", ImVec2(contentPaneWidth, paneHeight), false);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        auto toLower = [](std::string value) {
            std::transform(value.begin(), value.end(), value.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return value;
        };

        auto renderProjectsView = [&]() {
            const float projectsInsetX = 10.0f * uiScale;
            const float projectsInsetY = 6.0f * uiScale;
            const float projectsHeaderGap = 12.0f * uiScale;
            const float panelBuildT = launcherIntroFinished ? 1.0f : EaseOutCubic(contentBuildT);
            const float headerAlpha = ImLerp(0.0f, 1.0f, panelBuildT);
            const float headerOffsetY = (1.0f - panelBuildT) * 24.0f * uiScale;
            const float headerOffsetX = (1.0f - panelBuildT) * 30.0f * uiScale;
            ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + projectsInsetX,
                                       ImGui::GetCursorPosY() + projectsInsetY + headerOffsetY));
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, headerAlpha);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + headerOffsetX);
            ImGui::TextColored(ImVec4(0.92f, 0.94f, 0.98f, 1.0f), "Projects");
            const float buttonWidth = 112.0f * uiScale;
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float headerWidth = ImGui::GetContentRegionAvail().x;

            if (headerWidth < (560.0f * uiScale)) {
                ImGui::SetNextItemWidth(-1);
                ImGui::InputTextWithHint("##ProjectSearch", "Search projects", launcherSearch, sizeof(launcherSearch));
                ImGui::BeginDisabled(launcherBusy);
                const float twoButtonWidth = (ImGui::GetContentRegionAvail().x - spacing) * 0.5f;
                if (ImGui::Button("Open...", ImVec2(twoButtonWidth, 0))) {
                    openProjectDialog();
                }
                ImGui::SameLine();
                if (ImGui::Button("New Project", ImVec2(twoButtonWidth, 0))) {
                    openNewProjectDialog();
                }
                ImGui::EndDisabled();
            } else {
                ImGui::SameLine();
                float searchWidth = ImGui::GetContentRegionAvail().x - ((buttonWidth * 2.0f) + (spacing * 2.0f));
                searchWidth = std::max(searchWidth, 160.0f * uiScale);

                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - (searchWidth + buttonWidth * 2.0f + spacing * 2.0f));
                ImGui::SetNextItemWidth(searchWidth);
                ImGui::InputTextWithHint("##ProjectSearch", "Search projects", launcherSearch, sizeof(launcherSearch));
                ImGui::SameLine();
                ImGui::BeginDisabled(launcherBusy);
                if (ImGui::Button("Open...", ImVec2(buttonWidth, 0))) {
                    openProjectDialog();
                }
                ImGui::SameLine();
                if (ImGui::Button("New Project", ImVec2(buttonWidth, 0))) {
                    openNewProjectDialog();
                }
                ImGui::EndDisabled();
            }

            ImGui::Dummy(ImVec2(0.0f, projectsHeaderGap));
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0.0f, projectsHeaderGap));
            ImGui::PopStyleVar();

            const std::string filter = toLower(TrimCopy(launcherSearch));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGui::BeginChild("RecentProjectsList", ImVec2(0, 0), false);
            bool hadVisible = false;
            bool removedProject = false;
            int visibleRowIndex = 0;
            const float cardsBuildT = launcherIntroFinished ? 1.0f : EaseOutCubic(contentBuildT);

            for (size_t i = 0; i < projectManager.recentProjects.size(); ++i) {
                const auto& rp = projectManager.recentProjects[i];
                const std::string haystack = toLower(rp.name + " " + rp.path);
                if (!filter.empty() && haystack.find(filter) == std::string::npos) {
                    continue;
                }

                hadVisible = true;
                const int rowIndex = visibleRowIndex++;
                const float rowDelay = std::min(0.74f, static_cast<float>(rowIndex) * 0.072f);
                const float rowInput = ImClamp(
                    (cardsBuildT - rowDelay) / std::max(0.0001f, 1.0f - rowDelay),
                    0.0f,
                    1.0f);
                const float rowReveal = EaseOutCubic(rowInput);
                const float rowSlideX = (1.0f - rowReveal) * 56.0f * uiScale;
                const float rowSlideY = (1.0f - rowReveal) * (20.0f + static_cast<float>(rowIndex) * 2.8f) * uiScale;
                const float rowAlpha = ImLerp(0.0f, 1.0f, rowReveal);
                ImGui::PushID(static_cast<int>(i));

                ImTextureID previewTexId = static_cast<ImTextureID>(0);
                int previewTexWidth = 0;
                int previewTexHeight = 0;
                fs::path previewPath = getProjectPreviewPath(rp.path);
                if (!previewPath.empty() && fs::exists(previewPath)) {
                    if (usingVulkan() && vulkanRendererInitialized && vulkanRenderer) {
                        previewTexId = vulkanRenderer->getOrCreateUIImage(previewPath.string(),
                                                                          &previewTexWidth,
                                                                          &previewTexHeight);
                    } else if (!usingVulkan()) {
                        if (Texture* previewTex = renderer.getTexture(previewPath.string())) {
                            previewTexId = (ImTextureID)(intptr_t)previewTex->GetID();
                            previewTexWidth = previewTex->GetWidth();
                            previewTexHeight = previewTex->GetHeight();
                        }
                    }
                }

                const float cardHeight = 92.0f * uiScale;
                const ImVec2 cardSize(ImGui::GetContentRegionAvail().x, cardHeight);
                const ImVec2 rowBasePos = ImGui::GetCursorScreenPos();
                ImGui::SetCursorScreenPos(ImVec2(rowBasePos.x + rowSlideX, rowBasePos.y + rowSlideY));
                const ImVec2 cardPos = ImGui::GetCursorScreenPos();
                ImGui::InvisibleButton("RecentProjectCard", cardSize);
                const bool hovered = ImGui::IsItemHovered();
                const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
                const bool doubleClicked = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
                bool shouldOpen = clicked || doubleClicked;
                bool shouldRemove = false;
                const ImVec2 cardMin = ImGui::GetItemRectMin();
                const ImVec2 cardMax = ImGui::GetItemRectMax();
                const ImVec2 rowCenter((cardMin.x + cardMax.x) * 0.5f, (cardMin.y + cardMax.y) * 0.5f);

                if (ImGui::BeginPopupContextItem("RecentProjectRowContext")) {
                    if (ImGui::MenuItem("Open", nullptr, false, !launcherBusy)) {
                        shouldOpen = true;
                    }
                    if (ImGui::MenuItem("Remove from Recent")) {
                        shouldRemove = true;
                    }
                    ImGui::EndPopup();
                }

                ImDrawList* list = ImGui::GetWindowDrawList();
                auto withRowAlpha = [rowAlpha](const ImVec4& col) {
                    return ImVec4(col.x, col.y, col.z, col.w * rowAlpha);
                };
                const float hoverT = EaseOutCubic(hovered ? 1.0f : 0.0f);
                const ImU32 cardBg = ImGui::GetColorU32(hovered
                    ? withRowAlpha(ImVec4(0.18f, 0.22f, 0.31f, 1.0f))
                    : withRowAlpha(ImVec4(0.14f, 0.16f, 0.22f, 0.98f)));
                const ImU32 cardBorder = ImGui::GetColorU32(hovered
                    ? withRowAlpha(ImVec4(0.30f, 0.52f, 0.80f, 1.0f))
                    : withRowAlpha(ImVec4(0.22f, 0.27f, 0.36f, 0.95f)));
                const float cardRounding = 12.0f * uiScale;
                list->AddRectFilled(cardPos, ImVec2(cardPos.x + cardSize.x, cardPos.y + cardSize.y), cardBg, cardRounding);
                list->AddRect(cardPos, ImVec2(cardPos.x + cardSize.x, cardPos.y + cardSize.y), cardBorder, cardRounding, 0, hovered ? 2.0f : 1.0f);
                if (hovered) {
                    list->AddRectFilled(cardPos,
                                        ImVec2(cardPos.x + cardSize.x, cardPos.y + cardSize.y),
                                        ImGui::GetColorU32(withRowAlpha(ImVec4(0.22f, 0.36f, 0.58f, 0.06f + 0.08f * hoverT))),
                                        cardRounding);
                }

                const float padding = 14.0f * uiScale;
                const ImVec2 thumbMin(cardPos.x + padding, cardPos.y + padding);
                const ImVec2 thumbSize(118.0f * uiScale, cardHeight - padding * 2.0f);
                const ImVec2 thumbMax(thumbMin.x + thumbSize.x, thumbMin.y + thumbSize.y);
                if (previewTexId != static_cast<ImTextureID>(0)) {
                    DrawImageCover(list, previewTexId, thumbMin, thumbMax, previewTexWidth, previewTexHeight,
                                   ImGui::GetColorU32(withRowAlpha(ImVec4(1, 1, 1, 1))), 8.0f * uiScale);
                } else {
                    list->AddRectFilled(thumbMin, thumbMax, ImGui::GetColorU32(withRowAlpha(ImVec4(0.11f, 0.13f, 0.18f, 1.0f))), 8.0f * uiScale);
                    const char* noPreview = "No Preview";
                    ImVec2 txt = ImGui::CalcTextSize(noPreview);
                    list->AddText(ImVec2(thumbMin.x + (thumbSize.x - txt.x) * 0.5f,
                                         thumbMin.y + (thumbSize.y - txt.y) * 0.5f),
                                  ImGui::GetColorU32(withRowAlpha(ImVec4(0.66f, 0.70f, 0.78f, 1.0f))),
                                  noPreview);
                }
                list->AddRect(thumbMin, thumbMax, ImGui::GetColorU32(withRowAlpha(ImVec4(0.24f, 0.28f, 0.38f, 0.95f))), 8.0f * uiScale);

                const float actionWidth = 112.0f * uiScale;
                const float textStartX = thumbMax.x + 16.0f * uiScale;
                const float textWidth = std::max(140.0f * uiScale, cardMax.x - textStartX - actionWidth - 28.0f * uiScale);
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, rowAlpha);
                ImGui::SetCursorScreenPos(ImVec2(textStartX, cardPos.y + 15.0f * uiScale));
                ImGui::PushTextWrapPos(textStartX + textWidth);
                ImGui::TextColored(ImVec4(0.92f, 0.94f, 0.98f, 1.0f), "%s",
                                   rp.name.empty() ? "(Unnamed Project)" : rp.name.c_str());
                ImGui::PopTextWrapPos();

                ImGui::SetCursorScreenPos(ImVec2(textStartX, cardPos.y + 44.0f * uiScale));
                ImGui::PushTextWrapPos(textStartX + textWidth);
                ImGui::TextDisabled("%s", rp.path.c_str());
                ImGui::PopTextWrapPos();

                ImGui::SetCursorScreenPos(ImVec2(textStartX, cardPos.y + 66.0f * uiScale));
                ImGui::TextColored(ImVec4(0.63f, 0.69f, 0.78f, 1.0f), "Last opened: %s",
                                   rp.lastOpened.empty() ? "-" : rp.lastOpened.c_str());

                ImGui::SetCursorScreenPos(ImVec2(cardMax.x - actionWidth - padding, cardPos.y + (cardHeight - 34.0f * uiScale) * 0.5f));
                ImGui::BeginDisabled(launcherBusy);
                ImGui::PushStyleColor(ImGuiCol_Button, hovered ? ImVec4(0.20f, 0.48f, 0.83f, 1.0f) : ImVec4(0.18f, 0.40f, 0.70f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.54f, 0.90f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.17f, 0.42f, 0.75f, 1.0f));
                if (ImGui::Button("Open", ImVec2(actionWidth, 34.0f * uiScale))) {
                    shouldOpen = true;
                }
                ImGui::PopStyleColor(3);
                ImGui::EndDisabled();
                ImGui::PopStyleVar();

                if (shouldRemove) {
                    projectManager.recentProjects.erase(
                        projectManager.recentProjects.begin() +
                        static_cast<std::vector<RecentProject>::difference_type>(i));
                    projectManager.saveRecentProjects();
                    removedProject = true;
                    ImGui::PopID();
                    break;
                }

                if (shouldOpen && !launcherBusy) {
                    launchRecentProject(rp, rowCenter);
                }

                ImGui::SetCursorScreenPos(ImVec2(rowBasePos.x, rowBasePos.y + cardHeight));
                ImGui::Dummy(ImVec2(cardSize.x, 5.0f * uiScale));
                ImGui::PopID();
            }

            if (!hadVisible && !removedProject) {
                if (projectManager.recentProjects.empty()) {
                    ImGui::TextDisabled("No recent projects yet. Create or open one to get started.");
                } else {
                    ImGui::TextDisabled("No projects match your search.");
                }
            }
            ImGui::EndChild();
            ImGui::PopStyleVar();
        };

        auto renderNewProjectView = [&]() {
            static char templateSearch[128] = "";
            const std::vector<LauncherTemplateEntry> templates = GatherTemplateEntries();
            const fs::path templatesRoot = GetTemplateProjectsRoot();
            const std::string templateFilter = toLower(TrimCopy(templateSearch));

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.21f, 0.30f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.31f, 0.44f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.21f, 0.28f, 0.40f, 1.0f));
            if (ImGui::Button("< Back to Projects", ImVec2(170.0f * uiScale, 0))) {
                projectManager.showNewProjectDialog = false;
                projectManager.errorMessage.clear();
            }
            ImGui::PopStyleColor(3);
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.93f, 0.95f, 0.99f, 1.0f), "New Project");
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            auto renderTemplatePane = [&]() {
                ImGui::TextColored(ImVec4(0.88f, 0.91f, 0.97f, 1.0f), "Templates");
                ImGui::TextDisabled("Drop template project folders into Template-Projects.");
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                    ImGui::SetTooltip("%s", templatesRoot.string().c_str());
                }
                ImGui::SetNextItemWidth(-1);
                ImGui::InputTextWithHint("##TemplateSearch", "Search templates", templateSearch, sizeof(templateSearch));
                ImGui::Spacing();

                ImGui::BeginChild("TemplateGrid", ImVec2(0, 0), false);
                const float cardWidth = 250.0f * uiScale;
                const float cardHeight = 212.0f * uiScale;
                const float imageHeight = 145.0f * uiScale;
                float availWidth = ImGui::GetContentRegionAvail().x;
                int columnCount = std::max(1, static_cast<int>(std::floor(availWidth / (cardWidth + 10.0f * uiScale))));
                if (ImGui::BeginTable("TemplateCards", columnCount, ImGuiTableFlags_SizingFixedFit)) {
                    bool hadVisible = false;
                    for (const auto& t : templates) {
                        const std::string lowerName = toLower(t.displayName);
                        if (!templateFilter.empty() && lowerName.find(templateFilter) == std::string::npos) {
                            continue;
                        }
                        hadVisible = true;
                        ImGui::TableNextColumn();
                        ImGui::PushID(t.projectRoot.string().c_str());

                        const bool selected = (!projectManager.newProjectTemplatePath.empty() &&
                            fs::path(projectManager.newProjectTemplatePath) == t.projectRoot);

                        ImVec2 cardPos = ImGui::GetCursorScreenPos();
                        ImGui::InvisibleButton("TemplateCardHit", ImVec2(cardWidth, cardHeight));
                        if (ImGui::IsItemClicked()) {
                            projectManager.newProjectTemplatePath = t.projectRoot.string();
                            projectManager.newProjectTemplateName = t.displayName;
                        }

                        ImDrawList* list = ImGui::GetWindowDrawList();
                        ImU32 bg = ImGui::GetColorU32(ImVec4(0.15f, 0.18f, 0.25f, 1.0f));
                        ImU32 border = ImGui::GetColorU32(selected ? ImVec4(0.24f, 0.60f, 0.98f, 1.0f)
                                                                   : ImVec4(0.24f, 0.28f, 0.37f, 0.95f));
                        list->AddRectFilled(cardPos, ImVec2(cardPos.x + cardWidth, cardPos.y + cardHeight), bg, 8.0f * uiScale);

                        ImTextureID templateTexId = static_cast<ImTextureID>(0);
                        int templateTexWidth = 0;
                        int templateTexHeight = 0;
                        if (!t.previewImage.empty() && fs::exists(t.previewImage)) {
                            if (usingVulkan() && vulkanRendererInitialized && vulkanRenderer) {
                                templateTexId = vulkanRenderer->getOrCreateUIImage(t.previewImage.string(),
                                                                                   &templateTexWidth,
                                                                                   &templateTexHeight);
                            } else if (!usingVulkan()) {
                                if (Texture* templateTex = renderer.getTexture(t.previewImage.string())) {
                                    templateTexId = (ImTextureID)(intptr_t)templateTex->GetID();
                                    templateTexWidth = templateTex->GetWidth();
                                    templateTexHeight = templateTex->GetHeight();
                                }
                            }
                        }

                        ImVec2 imageMin = cardPos;
                        ImVec2 imageMax(cardPos.x + cardWidth, cardPos.y + imageHeight);
                        if (templateTexId != static_cast<ImTextureID>(0)) {
                            DrawImageCover(list, templateTexId, imageMin, imageMax,
                                           templateTexWidth, templateTexHeight,
                                           ImGui::GetColorU32(ImVec4(1, 1, 1, 1)), 8.0f * uiScale);
                        } else {
                            list->AddRectFilled(imageMin, imageMax,
                                                ImGui::GetColorU32(ImVec4(0.12f, 0.14f, 0.20f, 1.0f)),
                                                8.0f * uiScale);
                            const char* noPreview = "No Template Preview";
                            ImVec2 txt = ImGui::CalcTextSize(noPreview);
                            list->AddText(ImVec2(imageMin.x + (cardWidth - txt.x) * 0.5f,
                                                 imageMin.y + (imageHeight - txt.y) * 0.5f),
                                          ImGui::GetColorU32(ImVec4(0.66f, 0.70f, 0.78f, 1.0f)), noPreview);
                        }
                        list->AddRect(cardPos, ImVec2(cardPos.x + cardWidth, cardPos.y + cardHeight), border, 8.0f * uiScale, 0, 2.0f);

                        ImGui::SetCursorScreenPos(ImVec2(cardPos.x + 12.0f * uiScale, cardPos.y + imageHeight + 10.0f * uiScale));
                        ImGui::TextColored(ImVec4(0.91f, 0.93f, 0.98f, 1.0f), "%s", t.displayName.c_str());

                        if (selected) {
                            ImGui::SetCursorScreenPos(ImVec2(cardPos.x + cardWidth - 86.0f * uiScale, cardPos.y + cardHeight - 30.0f * uiScale));
                            ImGui::TextColored(ImVec4(0.24f, 0.60f, 0.98f, 1.0f), "Selected");
                        }

                        ImGui::SetCursorScreenPos(ImVec2(cardPos.x, cardPos.y + cardHeight + 10.0f * uiScale));
                        ImGui::Dummy(ImVec2(0.0f, 0.0f));
                        ImGui::PopID();
                    }

                    if (!hadVisible) {
                        ImGui::TableNextColumn();
                        if (templates.empty()) {
                            ImGui::TextDisabled("No templates found.");
                            ImGui::TextDisabled("Add a folder with project.modu in Template-Projects.");
                        } else {
                            ImGui::TextDisabled("No templates match your search.");
                        }
                    }
                    ImGui::EndTable();
                }
                ImGui::EndChild();
            };

            auto renderSettingsPane = [&]() {
                ImGui::TextColored(ImVec4(0.92f, 0.94f, 0.98f, 1.0f), "Project");
                ImGui::TextUnformatted("Project Name");
                ImGui::SetNextItemWidth(-1);
                ImGui::InputText("##ProjectNameInline", projectManager.newProjectName, sizeof(projectManager.newProjectName));

                ImGui::Spacing();
                if (projectManager.newProjectTemplatePath.empty()) {
                    ImGui::TextDisabled("Template: None (blank project)");
                } else {
                    ImGui::TextWrapped("Template: %s", projectManager.newProjectTemplateName.c_str());
                }

                ImGui::Spacing();
                const char* pipelineOptions[] = { "3D Pipeline", "2.5D Pipeline (Experimental)", "2D Pipeline" };
                ImGui::TextUnformatted("Pipeline");
                ImGui::SetNextItemWidth(-1);
                ImGui::Combo("##Pipeline", &projectManager.newProjectPipelineMode, pipelineOptions, IM_ARRAYSIZE(pipelineOptions));

                const char* rendererOptions[] = { "OpenGL", "Vulkan (Experimental)" };
                ImGui::TextUnformatted("Renderer");
                ImGui::SetNextItemWidth(-1);
                ImGui::Combo("##Renderer", &projectManager.newProjectRendererMode, rendererOptions, IM_ARRAYSIZE(rendererOptions));
#if !MODULARITY_HAS_VULKAN
                if (projectManager.newProjectRendererMode == 1) {
                    projectManager.newProjectRendererMode = 0;
                    ImGui::TextDisabled("Vulkan is unavailable in this build.");
                }
#else
                if (projectManager.newProjectRendererMode == 1 && !hasVulkanPipelinePackage()) {
                    projectManager.newProjectRendererMode = 0;
                    ImGui::TextDisabled("Install moduengine.vulkan-pipeline to enable Vulkan.");
                }
#endif

                ImGui::TextDisabled("Project location is set in Settings.");

                const LauncherPackageSnapshot& packageSnapshot = GetLauncherPackageSnapshot(projectManager);
                if (packageSnapshot.hasSource) {
                    ImGui::Checkbox("Import packages from latest project", &projectManager.newProjectImportLastPackages);
                    if (projectManager.newProjectImportLastPackages) {
                        ImGui::TextWrapped("Source: %s", packageSnapshot.sourceProjectName.c_str());
                    }
                } else {
                    projectManager.newProjectImportLastPackages = false;
                    ImGui::TextDisabled("No installed-package snapshot found.");
                }

                if (!projectManager.errorMessage.empty()) {
                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", projectManager.errorMessage.c_str());
                }

                const float buttonY = ImGui::GetWindowHeight() - 44.0f * uiScale;
                if (ImGui::GetCursorPosY() < buttonY) {
                    ImGui::SetCursorPosY(buttonY);
                }
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.17f, 0.53f, 0.94f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.60f, 0.98f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.13f, 0.46f, 0.86f, 1.0f));
                if (ImGui::Button("+ Create Project", ImVec2(-1, 32.0f * uiScale))) {
                    if (strlen(projectManager.newProjectName) == 0) {
                        projectManager.errorMessage = "Please enter a project name";
                    } else if (strlen(projectManager.newProjectLocation) == 0) {
                        projectManager.errorMessage = "Set a default location in Settings first.";
                    } else {
                        createNewProject(projectManager.newProjectName, projectManager.newProjectLocation);
                        if (projectManager.errorMessage.empty()) {
                            projectManager.showNewProjectDialog = false;
                        }
                    }
                }
                ImGui::PopStyleColor(3);
            };

            const float contentWidth = ImGui::GetContentRegionAvail().x;
            const float contentHeight = ImGui::GetContentRegionAvail().y;
            const bool useStackedLayout = contentWidth < (1100.0f * uiScale);

            if (useStackedLayout) {
                const float templateHeight = std::max(260.0f * uiScale, contentHeight * 0.52f);
                ImGui::BeginChild("TemplateCatalogPane", ImVec2(0, templateHeight), true);
                renderTemplatePane();
                ImGui::EndChild();

                ImGui::Spacing();
                ImGui::BeginChild("NewProjectSettingsPane", ImVec2(0, 0), true);
                renderSettingsPane();
                ImGui::EndChild();
            } else {
                const float settingsWidth = 400.0f * uiScale;
                const float gap = ImGui::GetStyle().ItemSpacing.x;
                float templatePaneWidth = ImGui::GetContentRegionAvail().x - settingsWidth - gap;
                templatePaneWidth = std::max(templatePaneWidth, 280.0f * uiScale);

                ImGui::BeginChild("TemplateCatalogPane", ImVec2(templatePaneWidth, 0), true);
                renderTemplatePane();
                ImGui::EndChild();

                ImGui::SameLine();
                ImGui::BeginChild("NewProjectSettingsPane", ImVec2(0, 0), true);
                renderSettingsPane();
                ImGui::EndChild();
            }
        };

        auto renderPackagesView = [&]() {
            if (ImGui::Button("Refresh##PackageSnapshot")) {
                (void)GetLauncherPackageSnapshot(projectManager, true);
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(launcherBusy);
            if (ImGui::Button("New Project...", ImVec2(130.0f * uiScale, 0))) {
                openNewProjectDialog();
            }
            ImGui::EndDisabled();

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.92f, 0.94f, 0.98f, 1.0f), "Installed Packages");
            ImGui::Separator();
            ImGui::Spacing();

            const LauncherPackageSnapshot& snapshot = GetLauncherPackageSnapshot(projectManager);
            if (!snapshot.hasSource) {
                ImGui::TextDisabled("No recent project with a packages manifest was found.");
                ImGui::TextDisabled("Open or create a project first, then return here.");
            } else {
                ImGui::TextDisabled("Source Project: %s", snapshot.sourceProjectName.c_str());
                ImGui::TextDisabled("Packages: %zu (%zu external)",
                                    snapshot.packageLabels.size(),
                                    snapshot.externalCount);
                ImGui::Spacing();

                ImGui::BeginChild("InstalledPackagesList", ImVec2(0, 0), true);
                if (snapshot.packageLabels.empty()) {
                    ImGui::TextDisabled("No optional packages were saved in the source manifest.");
                } else {
                    for (const auto& label : snapshot.packageLabels) {
                        ImGui::BulletText("%s", label.c_str());
                    }
                }
                ImGui::EndChild();
            }
        };

        auto renderSettingsView = [&]() {
            static std::string settingsStatus;
            static bool settingsStatusError = false;

            ImGui::TextColored(ImVec4(0.92f, 0.94f, 0.98f, 1.0f), "Settings");
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::TextUnformatted("Default Project Location");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##DefaultProjectLocation",
                             projectManager.defaultProjectLocation,
                             sizeof(projectManager.defaultProjectLocation));
            ImGui::TextDisabled("New projects will be created in this folder by default.");

            if (ImGui::Button("Save Settings", ImVec2(140.0f * uiScale, 0))) {
                const std::string trimmed = TrimCopy(projectManager.defaultProjectLocation);
                if (trimmed.empty()) {
                    settingsStatus = "Default project location cannot be empty.";
                    settingsStatusError = true;
                } else {
                    std::error_code ec;
                    fs::create_directories(trimmed, ec);
                    if (ec) {
                        settingsStatus = "Failed to create folder: " + ec.message();
                        settingsStatusError = true;
                    } else {
                        std::snprintf(projectManager.defaultProjectLocation,
                                      sizeof(projectManager.defaultProjectLocation),
                                      "%s",
                                      trimmed.c_str());
                        std::snprintf(projectManager.newProjectLocation,
                                      sizeof(projectManager.newProjectLocation),
                                      "%s",
                                      projectManager.defaultProjectLocation);
                        projectManager.saveLauncherSettings();
                        settingsStatus = "Saved.";
                        settingsStatusError = false;
                    }
                }
            }

            ImGui::SameLine();
            if (ImGui::Button("Use Home Default", ImVec2(150.0f * uiScale, 0))) {
#ifdef _WIN32
                const char* userProfile = std::getenv("USERPROFILE");
                const std::string fallback = userProfile && *userProfile
                    ? (fs::path(userProfile) / "Documents" / "ModularityProjects").string()
                    : (fs::current_path() / "Projects").string();
#else
                const char* home = std::getenv("HOME");
                const std::string fallback = home && *home
                    ? (fs::path(home) / "ModularityProjects").string()
                    : (fs::current_path() / "Projects").string();
#endif
                std::snprintf(projectManager.defaultProjectLocation,
                              sizeof(projectManager.defaultProjectLocation),
                              "%s",
                              fallback.c_str());
                std::snprintf(projectManager.newProjectLocation,
                              sizeof(projectManager.newProjectLocation),
                              "%s",
                              projectManager.defaultProjectLocation);
                projectManager.saveLauncherSettings();
                settingsStatus = "Reset to home default.";
                settingsStatusError = false;
            }

            if (!settingsStatus.empty()) {
                ImGui::Spacing();
                ImGui::TextColored(settingsStatusError
                                       ? ImVec4(1.0f, 0.42f, 0.42f, 1.0f)
                                       : ImVec4(0.61f, 0.86f, 0.70f, 1.0f),
                                   "%s",
                                   settingsStatus.c_str());
            }
        };

        const ImVec2 contentOrigin = ImGui::GetCursorPos();
        const ImVec2 contentAvail = ImGui::GetContentRegionAvail();
        const int sectionDirection = (launcherSection >= launcherSectionPrevious) ? 1 : -1;
        const float sectionOffsetY = (1.0f - sectionAnimEase) * 42.0f * uiScale * static_cast<float>(sectionDirection);
        const ImVec2 sectionPos(contentOrigin.x + sectionInset.x, contentOrigin.y + sectionInset.y + sectionOffsetY);
        const ImVec2 sectionSize(ImMax(0.0f, contentAvail.x - sectionInset.x * 2.0f),
                                 ImMax(0.0f, contentAvail.y - sectionInset.y * 2.0f));
        ImGui::SetCursorPos(sectionPos);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, sectionAnimEase);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f * uiScale, 14.0f * uiScale));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, shellBg);
        ImGui::BeginChild("LauncherActiveSection", sectionSize, false,
                          ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse);

        if (launcherSection == 0) {
            const ImVec2 projectsRoot = ImGui::GetCursorPos();
            const ImVec2 projectsAvail = ImGui::GetContentRegionAvail();
            ImDrawList* projectsDrawList = ImGui::GetWindowDrawList();
            const ImVec2 projectsClipMin = ImGui::GetCursorScreenPos();
            const ImVec2 projectsClipMax(projectsClipMin.x + projectsAvail.x, projectsClipMin.y + projectsAvail.y);
            projectsDrawList->PushClipRect(projectsClipMin, projectsClipMax, true);

            auto beginProjectsSubpanel = [&](const char* id, float offsetUnits) {
                const float eased = EaseOutCubic(projectsPanelVisual);
                const float offsetX = (offsetUnits - eased) * projectsAvail.x;
                ImGui::SetCursorPos(ImVec2(projectsRoot.x + offsetX, projectsRoot.y));
                ImGui::BeginChild(id, projectsAvail, false,
                                  ImGuiWindowFlags_NoScrollbar |
                                  ImGuiWindowFlags_NoScrollWithMouse);
            };

            beginProjectsSubpanel("ProjectsListPanel", 0.0f);
            renderProjectsView();
            ImGui::EndChild();

            beginProjectsSubpanel("ProjectsNewPanel", 1.0f);
            renderNewProjectView();
            ImGui::EndChild();

            projectsDrawList->PopClipRect();
        } else if (launcherSection == 1) {
            renderPackagesView();
        } else {
            renderSettingsView();
        }

        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);

        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::EndChild();
        ImGui::PopStyleVar();

        if (!launcherIntroFinished) {
            ImGui::PopItemFlag();
        }
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleVar();

        if (introState.textAlpha > 0.001f) {
            ImDrawList* overlay = ImGui::GetForegroundDrawList(ImGui::GetMainViewport());
            const char* title = "Modularity";
            ImFont* font = ImGui::GetFont();
            const float baseFontSize = ImGui::GetFontSize();
            const float centerFontSize = baseFontSize * 2.62f;
            const float headerFontSize = baseFontSize * 1.24f;
            const ImVec4 baseCol = ImVec4(0.95f, 0.96f, 0.98f, 1.0f);

            ImVec2 center = ImVec2(displaySize.x * 0.5f, displaySize.y * 0.36f);
            float centerTotalWidth = 0.0f;
            float headerTotalWidth = 0.0f;
            for (const char* c = title; *c; ++c) {
                char letter[2] = { *c, 0 };
                centerTotalWidth += font->CalcTextSizeA(centerFontSize, FLT_MAX, 0.0f, letter).x;
                headerTotalWidth += font->CalcTextSizeA(headerFontSize, FLT_MAX, 0.0f, letter).x;
            }

            const ImVec2 centerTextPos(center.x - centerTotalWidth * 0.5f,
                                       center.y - centerFontSize * 0.5f);
            const float headerMarginLeft = 26.0f * uiScale;
            const float headerTop = windowPos.y + 14.0f * uiScale;
            const ImVec2 headerTextPos(
                windowPos.x + headerMarginLeft,
                headerTop);

            float centerAdvanceX = 0.0f;
            float headerAdvanceX = 0.0f;
            int index = 0;
            const int totalLetters = static_cast<int>(std::strlen(title));
            const float popDelayStep = 0.055f;
            const float driftDelayStep = 0.024f;
            for (const char* c = title; *c; ++c, ++index) {
                char letter[2] = { *c, 0 };
                const float popDelay = (totalLetters > 1) ? (popDelayStep * static_cast<float>(index)) : 0.0f;
                const float popInput = ImClamp(
                    (introState.elapsed - introTimings.fadeIn - popDelay) / std::max(0.0001f, introTimings.popIn),
                    0.0f,
                    1.0f);
                const float popEase = EaseOutBack(popInput);
                const float popAlpha = EaseOutCubic(popInput);

                const float driftDelay = (totalLetters > 1) ? (driftDelayStep * static_cast<float>(index)) : 0.0f;
                const float driftInput = ImClamp(
                    (introState.driftT - driftDelay) / std::max(0.0001f, 1.0f - driftDelay),
                    0.0f,
                    1.0f);
                const float driftEase = EaseInOutCubic(driftInput);

                const float centerWidth = font->CalcTextSizeA(centerFontSize, FLT_MAX, 0.0f, letter).x;
                const float headerWidth = font->CalcTextSizeA(headerFontSize, FLT_MAX, 0.0f, letter).x;
                const ImVec2 centerLetterPos(centerTextPos.x + centerAdvanceX, centerTextPos.y);
                const ImVec2 headerLetterPos(headerTextPos.x + headerAdvanceX, headerTextPos.y);

                const float popScale = std::max(0.05f, 0.34f + 0.66f * popEase);
                const float popSize = centerFontSize * popScale;
                const ImVec2 spawnPos(centerLetterPos.x, centerLetterPos.y + (1.0f - popAlpha) * 22.0f * uiScale);
                const ImVec2 popPos(
                    ImLerp(spawnPos.x, centerLetterPos.x, popAlpha),
                    ImLerp(spawnPos.y, centerLetterPos.y, popAlpha));

                const float letterSize = ImLerp(popSize, headerFontSize, driftEase);
                const ImVec2 letterPos(
                    ImLerp(popPos.x, headerLetterPos.x, driftEase),
                    ImLerp(popPos.y, headerLetterPos.y, driftEase));
                const float letterAlpha = introState.textAlpha * popAlpha * ImLerp(0.92f, 1.0f, driftEase);
                const ImU32 textCol = ImGui::GetColorU32(ImVec4(baseCol.x, baseCol.y, baseCol.z, letterAlpha));
                overlay->AddText(font, letterSize, letterPos, textCol, letter);

                centerAdvanceX += centerWidth;
                headerAdvanceX += headerWidth;
            }

            const float packageTagAlpha = introState.textAlpha * ImClamp((introState.driftT - 0.23f) / 0.62f, 0.0f, 1.0f);
            if (packageTagAlpha > 0.001f) {
                const char* packageLabel = "Project manager";
                const float packageFontSize = baseFontSize * 0.95f;
                const ImU32 tagCol = ImGui::GetColorU32(ImVec4(0.79f, 0.84f, 0.92f, packageTagAlpha));
                const ImVec2 tagPos(headerTextPos.x + headerTotalWidth + 14.0f * uiScale,
                                    headerTextPos.y + headerFontSize * 0.18f);
                overlay->AddText(font, packageFontSize, tagPos, tagCol, packageLabel);
            }
        }
    }

    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(5);

    if (launcherTransitionActive && transitionT >= 1.0f) {
        launcherTransitionActive = false;
        if (launcherTransitionPendingHide || (!projectLoadInProgress && !sceneLoadInProgress)) {
            launcherTransitionPendingHide = false;
            showLauncher = false;
        }
    } else if (!launcherTransitionActive && launcherTransitionPendingHide &&
               !projectLoadInProgress && !sceneLoadInProgress) {
        launcherTransitionPendingHide = false;
        showLauncher = false;
    }

    if (projectManager.showOpenProjectDialog)
        renderOpenProjectDialog();

    if (projectLoadInProgress || sceneLoadInProgress) {
        float elapsed = static_cast<float>(glfwGetTime() - projectLoadStartTime);
        if (elapsed > 0.15f) {
            ImGuiIO& io = ImGui::GetIO();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.06f, 0.08f, 0.65f));
            ImGui::Begin("ProjectLoadOverlay", nullptr,
                         ImGuiWindowFlags_NoTitleBar |
                         ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoDocking |
                         ImGuiWindowFlags_NoBringToFrontOnFocus |
                         ImGuiWindowFlags_NoInputs);
            ImGui::End();
            ImGui::PopStyleColor();

            ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
            ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(420, 160));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 20.0f));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.13f, 0.18f, 0.98f));
            ImGui::Begin("ProjectLoadCard", nullptr,
                         ImGuiWindowFlags_NoTitleBar |
                         ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoDocking |
                         ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoSavedSettings);

            const char* headline = sceneLoadInProgress ? "Loading scene..." : "Loading project...";
            ImGui::TextColored(ImVec4(0.88f, 0.90f, 0.96f, 1.0f), "%s", headline);
            ImGui::Spacing();
            if (sceneLoadInProgress && !sceneLoadStatus.empty()) {
                ImGui::TextDisabled("%s", sceneLoadStatus.c_str());
            } else if (!projectLoadPath.empty()) {
                ImGui::TextDisabled("%s", projectLoadPath.c_str());
            }
            ImGui::Spacing();
            if (sceneLoadInProgress) {
                ImGui::ProgressCircle("##project_load_circle", 16.0f, 4.0f, sceneLoadProgress,
                                      ImGui::GetColorU32(ImGuiCol_ButtonHovered),
                                      ImGui::GetColorU32(ImGuiCol_Button));
                ImGui::SameLine();
                ImGui::BufferingBar("##project_load_bar", sceneLoadProgress,
                                    ImVec2(ImGui::GetContentRegionAvail().x - 40.0f, 8.0f),
                                    ImGui::GetColorU32(ImGuiCol_Button),
                                    ImGui::GetColorU32(ImGuiCol_ButtonHovered));
            } else {
                ImGui::Spinner("##project_load_spinner", 16.0f, 4, ImGui::GetColorU32(ImGuiCol_ButtonHovered));
                ImGui::SameLine();
                ImGui::BufferingBar("##project_load_bar", std::fmod(elapsed * 0.25f, 1.0f),
                                    ImVec2(ImGui::GetContentRegionAvail().x - 40.0f, 8.0f),
                                    ImGui::GetColorU32(ImGuiCol_Button),
                                    ImGui::GetColorU32(ImGuiCol_ButtonHovered));
            }

            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
        }
    }
}
#pragma endregion

#pragma region New Project Dialog
void Engine::renderNewProjectDialog() {
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(560, 390), ImGuiCond_Appearing);

    if (ImGui::Begin("New Project", &projectManager.showNewProjectDialog,
                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking)) {

        ImGui::Text("Project Name:");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##ProjectName", projectManager.newProjectName,
                       sizeof(projectManager.newProjectName));

        ImGui::Spacing();

        const char* pipelineOptions[] = { "3D Pipeline", "2.5D Pipeline (Experimental)", "2D Pipeline" };
        ImGui::SetNextItemWidth(-1);
        ImGui::Combo("Pipeline", &projectManager.newProjectPipelineMode, pipelineOptions, IM_ARRAYSIZE(pipelineOptions));
        ImGui::TextDisabled("This can be changed later in Project Settings.");

        ImGui::Spacing();

        const char* rendererOptions[] = { "OpenGL", "Vulkan (Experimental)" };
        ImGui::SetNextItemWidth(-1);
        ImGui::Combo("Renderer", &projectManager.newProjectRendererMode, rendererOptions, IM_ARRAYSIZE(rendererOptions));
#if !MODULARITY_HAS_VULKAN
        if (projectManager.newProjectRendererMode == 1) {
            projectManager.newProjectRendererMode = 0;
            ImGui::TextDisabled("Vulkan is unavailable in this build.");
        }
#else
        if (projectManager.newProjectRendererMode == 1 && !hasVulkanPipelinePackage()) {
            projectManager.newProjectRendererMode = 0;
            ImGui::TextDisabled("Install moduengine.vulkan-pipeline to enable Vulkan.");
        }
#endif
        ImGui::TextDisabled("OpenGL is default. Renderer selection applies after restart.");

        ImGui::Spacing();

        ImGui::Text("Location:");
        ImGui::SetNextItemWidth(-70);
        ImGui::InputText("##Location", projectManager.newProjectLocation,
                       sizeof(projectManager.newProjectLocation));
        ImGui::SameLine();
        if (ImGui::Button("Browse")) {
        }

        ImGui::Spacing();

        const LauncherPackageSnapshot& packageSnapshot = GetLauncherPackageSnapshot(projectManager);
        if (packageSnapshot.hasSource) {
            ImGui::Checkbox("Import installed packages from latest project", &projectManager.newProjectImportLastPackages);
            if (projectManager.newProjectImportLastPackages) {
                ImGui::TextDisabled("Importing %zu package entries from \"%s\"",
                                    packageSnapshot.packageLabels.size(),
                                    packageSnapshot.sourceProjectName.c_str());
            } else {
                ImGui::TextDisabled("Packages will not be imported.");
            }
        } else {
            projectManager.newProjectImportLastPackages = false;
            ImGui::TextDisabled("No recent package snapshot available to import.");
        }

        ImGui::Spacing();

        if (strlen(projectManager.newProjectName) > 0) {
            fs::path previewPath = fs::path(projectManager.newProjectLocation) /
                                  projectManager.newProjectName;
            ImGui::TextDisabled("Project will be created at:");
            ImGui::TextWrapped("%s", previewPath.string().c_str());
        }

        if (!projectManager.errorMessage.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s",
                              projectManager.errorMessage.c_str());
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float buttonWidth = 100;
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonWidth * 2 - 20);

        if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0))) {
            projectManager.showNewProjectDialog = false;
            memset(projectManager.newProjectName, 0, sizeof(projectManager.newProjectName));
        }

        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.3f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.6f, 0.4f, 1.0f));
        if (ImGui::Button("Create", ImVec2(buttonWidth, 0))) {
            if (strlen(projectManager.newProjectName) == 0) {
                projectManager.errorMessage = "Please enter a project name";
            } else if (strlen(projectManager.newProjectLocation) == 0) {
                projectManager.errorMessage = "Please specify a location";
            } else {
                createNewProject(projectManager.newProjectName,
                               projectManager.newProjectLocation);
                projectManager.showNewProjectDialog = false;
            }
        }
        ImGui::PopStyleColor(2);
    }
    ImGui::End();
}
#pragma endregion

#pragma region Open Project Dialog
void Engine::renderOpenProjectDialog() {
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(500, 180), ImGuiCond_Appearing);

    if (ImGui::Begin("Open Project", &projectManager.showOpenProjectDialog,
                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking)) {

        ImGui::Text("Project File Path (.modu):");
        ImGui::SetNextItemWidth(-70);
        ImGui::InputText("##OpenPath", projectManager.openProjectPath,
                       sizeof(projectManager.openProjectPath));
        ImGui::SameLine();
        if (ImGui::Button("Browse")) {
        }

        ImGui::TextDisabled("Select a project.modu file");

        if (!projectManager.errorMessage.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s",
                              projectManager.errorMessage.c_str());
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float buttonWidth = 100;
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonWidth * 2 - 20);

        if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0))) {
            projectManager.showOpenProjectDialog = false;
            memset(projectManager.openProjectPath, 0, sizeof(projectManager.openProjectPath));
        }

        ImGui::SameLine();

        if (ImGui::Button("Open", ImVec2(buttonWidth, 0))) {
            if (strlen(projectManager.openProjectPath) == 0) {
                projectManager.errorMessage = "Please enter a project path";
            } else {
                playEditorFeedbackPreview("Resources/Sounds/Selection.mp3", 0.95f, false, EditorFeedbackSoundCategory::Click);
                OpenProjectPath(projectManager.openProjectPath);
                if (!projectManager.errorMessage.empty()) {
                    // Error handled in OpenProjectPath
                } else {
                    projectManager.showOpenProjectDialog = false;
                }
            }
        }
    }
    ImGui::End();
}
#pragma endregion

#pragma region Project Browser Panel
void Engine::renderProjectBrowserPanel() {
    ImVec4 headerCol = ImVec4(0.20f, 0.27f, 0.36f, 1.0f);
    ImVec4 headerColActive = ImVec4(0.24f, 0.34f, 0.46f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Header, headerCol);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, headerColActive);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, headerColActive);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 5.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 4.0f));

    const bool wasOpen = showProjectBrowser;
    ImGui::Begin("Project Settings", &showProjectBrowser);

    if (!projectManager.currentProject.isLoaded) {
        ImGui::TextDisabled("No project loaded");
        ImGui::End();
        if (wasOpen != showProjectBrowser) {
            saveEditorUserSettings();
        }
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(3);
        return;
    }

    ImGui::TextColored(ImVec4(0.4f, 0.7f, 0.95f, 1.0f), "%s", projectManager.currentProject.name.c_str());
    if (projectManager.currentProject.hasUnsavedChanges) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "*");
    }

    ImGui::Separator();
    ImGui::TextDisabled("Packages are managed in Modupak Manager.");
    ImGui::SameLine();
    if (ImGui::SmallButton("Open Modupak Manager")) {
        showRegistryPackagesWindow = true;
        saveEditorUserSettings();
    }

    struct ProjectSettingsUiIcon {
        ImTextureID id = static_cast<ImTextureID>(0);
        bool flipY = false;
    };
    const bool hasVulkanUiImages = usingVulkan() && vulkanRendererInitialized && (vulkanRenderer != nullptr);
    auto resolveProjectSettingsIcon = [&](const char* iconPath) -> ProjectSettingsUiIcon {
        if (!iconPath || !*iconPath) {
            return {};
        }
        if (rendererInitialized) {
            if (Texture* icon = renderer.getTexture(iconPath, MaterialProperties::TextureFilter::Bilinear);
                icon && icon->GetID()) {
                return { static_cast<ImTextureID>(icon->GetID()), true };
            }
        }
        if (hasVulkanUiImages && vulkanRenderer) {
            ImTextureID icon = vulkanRenderer->getOrCreateUIImage(iconPath);
            if (icon != static_cast<ImTextureID>(0)) {
                return { icon, false };
            }
        }
        return {};
    };

    struct ProjectSettingsTabInfo {
        const char* label;
        const char* iconPath;
        const char* fallback;
    };

    static constexpr ProjectSettingsTabInfo tabs[] = {
        { "Scenes", "Resources/Engine-Root/Project Settings/Tabs/Scenes.png", "S" },
        { "Assets", "Resources/Engine-Root/Project Settings/Tabs/Assets.png", "A" },
        { "Editor", "Resources/Engine-Root/Project Settings/Tabs/Editor.png", "E" },
        { "Build", "Resources/Engine-Root/Project Settings/Tabs/Build.png", "B" },
        { "Compilation", "Resources/Engine-Root/Project Settings/Tabs/Compilation.png", "C" }
    };

    static int selectedTab = 0;
    constexpr int tabCount = static_cast<int>(IM_ARRAYSIZE(tabs));
    static std::array<float, tabCount> tabSelectionAnim = {};
    if (selectedTab < 0 || selectedTab >= tabCount) {
        selectedTab = 0;
    }

    const float tabAnimLerpSelected = std::clamp(ImGui::GetIO().DeltaTime * 10.0f, 0.0f, 1.0f);
    const float tabAnimLerpDeselected = std::clamp(ImGui::GetIO().DeltaTime * 16.0f, 0.0f, 1.0f);
    for (int i = 0; i < tabCount; ++i) {
        const float target = (selectedTab == i) ? 1.0f : 0.0f;
        const float lerpT = (selectedTab == i) ? tabAnimLerpSelected : tabAnimLerpDeselected;
        tabSelectionAnim[i] = ImLerp(tabSelectionAnim[i], target, lerpT);
    }

    auto drawProjectSettingsTab = [&](int index) {
        const ProjectSettingsTabInfo& tab = tabs[index];
        const bool isCurrentSelection = (selectedTab == index);
        const float selectedBlend = tabSelectionAnim[index];
        const float pulse = isCurrentSelection ? std::sin(static_cast<float>(ImGui::GetTime()) * 7.5f) : 0.0f;
        const float brighten = std::max(0.0f, pulse) * selectedBlend;
        const float darken = std::max(0.0f, -pulse) * selectedBlend;

        const ImVec2 slotPos = ImGui::GetCursorScreenPos();
        const ImVec2 slotSize(ImGui::GetContentRegionAvail().x, 38.0f);
        ImGui::PushID(index);
        const bool pressed = ImGui::InvisibleButton("##ProjectSettingsNavTab", slotSize);
        const bool hovered = ImGui::IsItemHovered();
        const bool held = ImGui::IsItemActive();
        ImGui::PopID();

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImRect slot(slotPos, ImVec2(slotPos.x + slotSize.x, slotPos.y + slotSize.y));
        const float expand = isCurrentSelection
            ? selectedBlend * (0.75f + brighten * 1.35f - darken * 0.55f)
            : selectedBlend * 0.2f;
        const float insetX = std::max(3.0f, 5.0f - expand);
        const float insetY = std::max(1.5f, 2.5f - expand * 0.45f);
        const ImVec2 cardMin(slot.Min.x + insetX, slot.Min.y + insetY);
        const ImVec2 cardMax(slot.Max.x - insetX, slot.Max.y - insetY);
        const float rounding = 13.0f;

        ImVec4 bg = ImVec4(0.12f, 0.14f, 0.19f, 0.92f);
        ImVec4 border = ImVec4(0.22f, 0.27f, 0.36f, 0.90f);
        ImVec4 accent = ImVec4(0.38f, 0.63f, 0.98f, 0.95f);
        if (hovered) {
            bg = ImVec4(0.15f, 0.18f, 0.24f, 0.95f);
            border = ImVec4(0.32f, 0.42f, 0.56f, 0.95f);
        }
        if (selectedBlend > 0.001f) {
            bg = ImVec4(0.14f + brighten * 0.06f - darken * 0.03f,
                        0.20f + brighten * 0.08f - darken * 0.05f,
                        0.30f + brighten * 0.12f - darken * 0.06f,
                        0.96f);
            border = ImVec4(0.32f + brighten * 0.10f,
                            0.58f + brighten * 0.10f,
                            0.95f,
                            0.98f);
            accent = ImVec4(0.45f + brighten * 0.10f,
                            0.78f + brighten * 0.08f,
                            1.0f,
                            0.98f);
        }
        if (held) {
            bg.x += 0.03f;
            bg.y += 0.03f;
            bg.z += 0.03f;
        }

        if (isCurrentSelection && selectedBlend > 0.05f) {
            drawList->AddRectFilled(
                ImVec2(cardMin.x + 1.0f, cardMin.y + 3.0f),
                ImVec2(cardMax.x + 1.0f, cardMax.y + 3.0f),
                ImGui::GetColorU32(ImVec4(0.02f, 0.04f, 0.08f, 0.16f + 0.08f * selectedBlend)),
                rounding);
        }
        drawList->AddRectFilled(cardMin, cardMax, ImGui::GetColorU32(bg), rounding);
        drawList->AddRect(cardMin, cardMax, ImGui::GetColorU32(border), rounding, 0, 1.0f + selectedBlend);
        if (selectedBlend > 0.02f) {
            const float stripeWidth = isCurrentSelection ? 3.0f + brighten * 1.4f : 2.0f;
            drawList->AddRectFilled(
                ImVec2(cardMin.x, cardMin.y + 7.0f),
                ImVec2(cardMin.x + stripeWidth, cardMax.y - 7.0f),
                ImGui::GetColorU32(accent),
                4.0f);
        }

        const ProjectSettingsUiIcon icon = resolveProjectSettingsIcon(tab.iconPath);
        const float iconSize = isCurrentSelection
            ? 18.0f + selectedBlend * (2.2f + brighten * 1.5f - darken * 0.6f)
            : 18.0f + selectedBlend * 0.4f;
        const ImVec2 iconMin(cardMin.x + 14.0f, cardMin.y + (cardMax.y - cardMin.y - iconSize) * 0.5f);
        const ImVec2 iconMax(iconMin.x + iconSize, iconMin.y + iconSize);
        const int iconAlpha = selectedBlend > 0.05f ? 255 : hovered ? 235 : 214;
        if (icon.id != static_cast<ImTextureID>(0)) {
            const ImVec2 uvMin = icon.flipY ? ImVec2(0.0f, 1.0f) : ImVec2(0.0f, 0.0f);
            const ImVec2 uvMax = icon.flipY ? ImVec2(1.0f, 0.0f) : ImVec2(1.0f, 1.0f);
            drawList->AddImage(icon.id, iconMin, iconMax, uvMin, uvMax, IM_COL32(255, 255, 255, iconAlpha));
        } else {
            drawList->AddText(
                ImVec2(cardMin.x + 16.0f, cardMin.y + (cardMax.y - cardMin.y - ImGui::GetTextLineHeight()) * 0.5f),
                IM_COL32(255, 255, 255, iconAlpha),
                tab.fallback);
        }

        const ImVec2 textSize = ImGui::CalcTextSize(tab.label);
        const float textX = iconMax.x + 12.0f;
        const float textY = cardMin.y + (cardMax.y - cardMin.y - textSize.y) * 0.5f;
        const ImU32 textColor = ImGui::GetColorU32(
            selectedBlend > 0.05f
                ? ImVec4(0.95f, 0.98f, 1.0f, 1.0f)
                : hovered
                    ? ImVec4(0.90f, 0.94f, 0.98f, 1.0f)
                    : ImVec4(0.78f, 0.83f, 0.89f, 1.0f));
        drawList->AddText(ImVec2(textX, textY), textColor, tab.label);
        return pressed;
    };

    ImGui::BeginChild("SettingsNav", ImVec2(214.0f, 0), true);
    for (int i = 0; i < tabCount; ++i) {
        if (drawProjectSettingsTab(i)) {
            if (selectedTab != i) {
                selectedTab = i;
                playEditorFeedbackPreview("Resources/Sounds/Selection Tick Main Editor.mp3", 0.95f, false, EditorFeedbackSoundCategory::Click);
            }
        }
        if (i + 1 < tabCount) {
            ImGui::Dummy(ImVec2(0.0f, 0.0f));
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("SettingsBody", ImVec2(0, 0), false);

    if (selectedTab == 0) {
        if (ImGui::Button("+ New Scene")) {
            showNewSceneDialog = true;
            memset(newSceneName, 0, sizeof(newSceneName));
        }

        ImGui::Spacing();

        auto scenes = projectManager.currentProject.getSceneList();
        for (const auto& scene : scenes) {
            bool isCurrentScene = (scene == projectManager.currentProject.currentSceneName);

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf |
                                      ImGuiTreeNodeFlags_SpanAvailWidth |
                                      ImGuiTreeNodeFlags_NoTreePushOnOpen;
            if (isCurrentScene) flags |= ImGuiTreeNodeFlags_Selected;

            ImGui::TreeNodeEx(scene.c_str(), flags, "[S] %s", scene.c_str());

            if (ImGui::IsItemClicked() && !isCurrentScene) {
                loadScene(scene);
            }

            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Load") && !isCurrentScene) {
                    loadScene(scene);
                }
                if (ImGui::MenuItem("Duplicate")) {
                    addConsoleMessage("Scene duplication not yet implemented.", ConsoleMessageType::Info);
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Delete") && !isCurrentScene) {
                    fs::remove(projectManager.currentProject.getSceneFilePath(scene));
                    addConsoleMessage("Deleted scene: " + scene, ConsoleMessageType::Info);
                }
                ImGui::EndPopup();
            }
        }

        if (scenes.empty()) {
            ImGui::TextDisabled("No scenes yet");
        }
    } else if (selectedTab == 1) {
        ImGui::TextDisabled("Loaded OBJ Meshes");
        const auto& meshesObj = g_objLoader.getAllMeshes();
        if (meshesObj.empty()) {
            ImGui::TextDisabled("No meshes loaded");
            ImGui::TextDisabled("Import .obj files from File Browser");
        } else {
            for (size_t i = 0; i < meshesObj.size(); i++) {
                const auto& mesh = meshesObj[i];
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf |
                                          ImGuiTreeNodeFlags_SpanAvailWidth |
                                          ImGuiTreeNodeFlags_NoTreePushOnOpen;
                
                ImGui::TreeNodeEx((void*)(intptr_t)i, flags, "[M] %s", mesh.name.c_str());
                
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("Vertices: %d", mesh.vertexCount);
                    ImGui::Text("Faces: %d", mesh.faceCount);
                    ImGui::Text("Has Normals: %s", mesh.hasNormals ? "Yes" : "No");
                    ImGui::Text("Has UVs: %s", mesh.hasTexCoords ? "Yes" : "No");
                    ImGui::TextDisabled("%s", mesh.path.c_str());
                    ImGui::EndTooltip();
                }
            }
        }

        ImGui::Separator();
        ImGui::TextDisabled("Loaded Models (Assimp)");
        const auto& meshesAssimp = getModelLoader().getAllMeshes();
        if (meshesAssimp.empty()) {
            ImGui::TextDisabled("No models loaded");
            ImGui::TextDisabled("Import FBX/GLTF/other supported models from File Browser");
        } else {
            for (size_t i = 0; i < meshesAssimp.size(); i++) {
                const auto& mesh = meshesAssimp[i];
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf |
                                          ImGuiTreeNodeFlags_SpanAvailWidth |
                                          ImGuiTreeNodeFlags_NoTreePushOnOpen;

                ImGui::TreeNodeEx((void*)(intptr_t)(10000 + i), flags, "[A] %s", mesh.name.c_str());

                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("Vertices: %d", mesh.vertexCount);
                    ImGui::Text("Faces: %d", mesh.faceCount);
                    ImGui::Text("Has Normals: %s", mesh.hasNormals ? "Yes" : "No");
                    ImGui::Text("Has UVs: %s", mesh.hasTexCoords ? "Yes" : "No");
                    ImGui::TextDisabled("%s", mesh.path.c_str());
                    ImGui::EndTooltip();
                }
            }
        }
    } else if (selectedTab == 2) {
        bool editorSettingsChanged = false;
        bool buildSettingsChanged = false;

        if (ImGui::CollapsingHeader("Project Pipeline", ImGuiTreeNodeFlags_DefaultOpen)) {
            int pipelineIndex = ProjectPipelineToUiIndex(projectManager.currentProject.pipeline);
            const char* pipelineOptions[] = { "3D Pipeline", "2.5D Pipeline (Experimental)", "2D Pipeline" };
            if (ImGui::Combo("Mode", &pipelineIndex, pipelineOptions, IM_ARRAYSIZE(pipelineOptions))) {
                projectManager.currentProject.pipeline = ProjectPipelineFromUiIndex(pipelineIndex);
                projectManager.currentProject.saveProjectFile();
                applyProjectPipelineDefaults(false);
                projectManager.currentProject.hasUnsavedChanges = true;
            }

            int rendererIndex = (projectManager.currentProject.rendererBackend == Modularity::GraphicsBackend::Vulkan) ? 1 : 0;
            const char* rendererOptions[] = { "OpenGL", "Vulkan (Experimental)" };
            if (ImGui::Combo("Renderer API##ProjectRendererApi", &rendererIndex, rendererOptions, IM_ARRAYSIZE(rendererOptions))) {
#if !MODULARITY_HAS_VULKAN
                if (rendererIndex == 1) {
                    rendererIndex = 0;
                    addConsoleMessage("Vulkan renderer is unavailable in this build. Keeping OpenGL.",
                                      ConsoleMessageType::Warning);
                }
#else
                if (rendererIndex == 1 && !hasVulkanPipelinePackage()) {
                    rendererIndex = 0;
                    addConsoleMessage("Install moduengine.vulkan-pipeline to enable Vulkan. Keeping OpenGL.",
                                      ConsoleMessageType::Warning);
                }
#endif
                projectManager.currentProject.rendererBackend =
                    (rendererIndex == 1) ? Modularity::GraphicsBackend::Vulkan : Modularity::GraphicsBackend::OpenGL;
                projectManager.currentProject.saveProjectFile();
            }
            if (projectManager.currentProject.rendererBackend != graphicsBackend) {
                ImGui::TextDisabled("Current session renderer: %s", Modularity::ToString(graphicsBackend));
                ImGui::TextDisabled("Restart editor to apply project renderer.");
            }

            if (projectManager.currentProject.pipeline == ProjectPipeline::Pipeline2D) {
                ImGui::TextDisabled("2D world editing is always enabled in this project.");
                if (ImGui::Checkbox("Pixel Grid Snap", &pixelGridSnapEnabled)) editorSettingsChanged = true;
                ImGui::BeginDisabled(!pixelGridSnapEnabled);
                if (ImGui::DragInt("Snap Step (px)", &pixelGridSnapStep, 1.0f, 1, 64)) {
                    pixelGridSnapStep = std::clamp(pixelGridSnapStep, 1, 64);
                    editorSettingsChanged = true;
                }
                ImGui::EndDisabled();
            } else if (projectManager.currentProject.pipeline == ProjectPipeline::Pipeline25D) {
                ImGui::TextDisabled("2.5D projects keep 2D world overlay enabled without forcing pure 2D camera mode.");
                ImGui::SeparatorText("2D Overlay Editing");
                ImGui::TextDisabled("These controls affect the 2D/world overlay editor grid only.");
                if (ImGui::Checkbox("Pixel Grid Snap", &pixelGridSnapEnabled)) editorSettingsChanged = true;
                ImGui::BeginDisabled(!pixelGridSnapEnabled);
                if (ImGui::DragInt("Snap Step (px)", &pixelGridSnapStep, 1.0f, 1, 64)) {
                    pixelGridSnapStep = std::clamp(pixelGridSnapStep, 1, 64);
                    editorSettingsChanged = true;
                }
                ImGui::EndDisabled();

                ImGui::SeparatorText("2.5D Presentation");
                ImGui::TextDisabled("These controls affect the TM/vexel presentation layer.");
                auto& tmPresentation = tmOpenGLRenderer.getPresentationSettings();

                if (ImGui::Checkbox("Pitch Stretch", &tmPresentation.lookPitchStretchEnabled)) editorSettingsChanged = true;
                ImGui::BeginDisabled(!tmPresentation.lookPitchStretchEnabled);
                if (ImGui::DragFloat("Stretch Strength", &tmPresentation.lookPitchStretchStrength, 0.01f, 0.0f, 1.5f, "%.2f")) {
                    tmPresentation.lookPitchStretchStrength = std::clamp(tmPresentation.lookPitchStretchStrength, 0.0f, 1.5f);
                    editorSettingsChanged = true;
                }
                if (ImGui::DragFloat("Compress Strength", &tmPresentation.lookPitchCompressStrength, 0.01f, 0.0f, 1.5f, "%.2f")) {
                    tmPresentation.lookPitchCompressStrength = std::clamp(tmPresentation.lookPitchCompressStrength, 0.0f, 1.5f);
                    editorSettingsChanged = true;
                }
                if (ImGui::DragFloat("Shear Strength", &tmPresentation.lookPitchShearStrength, 0.01f, 0.0f, 1.0f, "%.2f")) {
                    tmPresentation.lookPitchShearStrength = std::clamp(tmPresentation.lookPitchShearStrength, 0.0f, 1.0f);
                    editorSettingsChanged = true;
                }
                ImGui::EndDisabled();

                if (ImGui::Checkbox("World Snap", &tmPresentation.presentationSnapEnabled)) editorSettingsChanged = true;
                ImGui::BeginDisabled(!tmPresentation.presentationSnapEnabled);
                if (ImGui::DragFloat("World Snap Step", &tmPresentation.presentationSnapStep, 0.005f, 0.001f, 8.0f, "%.3f")) {
                    tmPresentation.presentationSnapStep = std::clamp(tmPresentation.presentationSnapStep, 0.001f, 8.0f);
                    editorSettingsChanged = true;
                }
                ImGui::EndDisabled();

                if (ImGui::Checkbox("Camera-Relative Snap", &tmPresentation.cameraRelativeSnapEnabled)) editorSettingsChanged = true;
                ImGui::BeginDisabled(!tmPresentation.cameraRelativeSnapEnabled);
                if (ImGui::DragFloat("Camera Snap Step", &tmPresentation.cameraRelativeSnapStep, 0.005f, 0.001f, 8.0f, "%.3f")) {
                    tmPresentation.cameraRelativeSnapStep = std::clamp(tmPresentation.cameraRelativeSnapStep, 0.001f, 8.0f);
                    editorSettingsChanged = true;
                }
                ImGui::EndDisabled();

                if (ImGui::Checkbox("Vertex Snap", &tmPresentation.vertexSnapEnabled)) editorSettingsChanged = true;
                ImGui::BeginDisabled(!tmPresentation.vertexSnapEnabled);
                if (ImGui::DragFloat("Vertex Snap Step", &tmPresentation.vertexSnapStep, 0.0025f, 0.0005f, 4.0f, "%.4f")) {
                    tmPresentation.vertexSnapStep = std::clamp(tmPresentation.vertexSnapStep, 0.0005f, 4.0f);
                    editorSettingsChanged = true;
                }
                ImGui::EndDisabled();

                if (ImGui::Checkbox("Screen Snap", &tmPresentation.screenSnapEnabled)) editorSettingsChanged = true;
                ImGui::BeginDisabled(!tmPresentation.screenSnapEnabled);
                if (ImGui::DragFloat("Screen Snap Step (px)", &tmPresentation.screenSnapStep, 0.25f, 0.25f, 16.0f, "%.2f")) {
                    tmPresentation.screenSnapStep = std::clamp(tmPresentation.screenSnapStep, 0.25f, 16.0f);
                    editorSettingsChanged = true;
                }
                ImGui::EndDisabled();

                if (ImGui::Button("Use High Precision")) {
                    tmPresentation.presentationSnapEnabled = false;
                    tmPresentation.cameraRelativeSnapEnabled = false;
                    tmPresentation.vertexSnapEnabled = false;
                    tmPresentation.screenSnapEnabled = false;
                    editorSettingsChanged = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Use Retro Snap")) {
                    tmPresentation.presentationSnapEnabled = true;
                    tmPresentation.presentationSnapStep = 0.125f;
                    tmPresentation.cameraRelativeSnapEnabled = true;
                    tmPresentation.cameraRelativeSnapStep = 0.125f;
                    tmPresentation.vertexSnapEnabled = true;
                    tmPresentation.vertexSnapStep = 0.0625f;
                    tmPresentation.screenSnapEnabled = true;
                    tmPresentation.screenSnapStep = 2.0f;
                    editorSettingsChanged = true;
                }
            } else {
                ImGui::TextDisabled("2D world overlay remains optional in 3D projects.");
            }
        }

        if (ImGui::CollapsingHeader("Physics", ImGuiTreeNodeFlags_DefaultOpen)) {
            ProjectPhysicsSettings& physicsSettings = projectManager.currentProject.physicsSettings;
            int massUnitIndex = static_cast<int>(physicsSettings.massUnit);
            const char* massUnitOptions[] = { "Kilograms (kg)", "Grams (g)", "Pounds (lb)", "Ounces (oz)" };
            if (ImGui::Combo("Mass Units", &massUnitIndex, massUnitOptions, IM_ARRAYSIZE(massUnitOptions))) {
                const ProjectMassUnit previousUnit = physicsSettings.massUnit;
                const ProjectMassUnit nextUnit = static_cast<ProjectMassUnit>(
                    std::clamp(massUnitIndex, 0, static_cast<int>(IM_ARRAYSIZE(massUnitOptions)) - 1));
                if (previousUnit != nextUnit) {
                    const float previousScale = std::max(0.000001f, ProjectMassUnitToKilograms(previousUnit));
                    const float nextScale = std::max(0.000001f, ProjectMassUnitToKilograms(nextUnit));
                    for (SceneObject& obj : sceneObjects) {
                        if (!obj.hasRigidbody) continue;
                        obj.rigidbody.mass = std::max(0.0001f, obj.rigidbody.mass * (previousScale / nextScale));
                    }
                    physicsSettings.massUnit = nextUnit;
                    projectManager.currentProject.saveProjectFile();
                    projectManager.currentProject.hasUnsavedChanges = true;
                    physics.setProjectSettings(physicsSettings);
                    if ((isPlaying || specMode || testMode) && physics.isReady()) {
                        physics.onPlayStart(sceneObjects);
                    }
                }
            }

            float gravityScale = physicsSettings.globalGravityScale;
            if (ImGui::DragFloat("Global Gravity Scale", &gravityScale, 0.01f, 0.0f, 10.0f, "%.2f")) {
                physicsSettings.globalGravityScale = std::clamp(gravityScale, 0.0f, 10.0f);
                projectManager.currentProject.saveProjectFile();
                projectManager.currentProject.hasUnsavedChanges = true;
                physics.setProjectSettings(physicsSettings);
            }

            ImGui::TextDisabled("3D and 2D runtime gravity use this project-wide multiplier.");
        }

        if (ImGui::CollapsingHeader("Player / Viewport", ImGuiTreeNodeFlags_DefaultOpen)) {
            const char* resolutionOptions[] = { "Default (1280x720)", "1080p", "720p", "1440p", "Custom" };

            if (gameViewportResolutionIndex < 0 || gameViewportResolutionIndex >= static_cast<int>(IM_ARRAYSIZE(resolutionOptions))) {
                gameViewportResolutionIndex = 0;
            }
            int resolutionIndex = gameViewportResolutionIndex;
            if (ImGui::Combo("Preview Resolution", &resolutionIndex, resolutionOptions, IM_ARRAYSIZE(resolutionOptions)))
            {
                gameViewportResolutionIndex = resolutionIndex;
                editorSettingsChanged = true;
            }

            if (gameViewportResolutionIndex == 4) {
                if (ImGui::DragInt("Custom Width", &gameViewportCustomWidth, 1.0f, 64, 8192)) {
                    gameViewportCustomWidth = std::clamp(gameViewportCustomWidth, 64, 8192);
                    editorSettingsChanged = true;
                }
                if (ImGui::DragInt("Custom Height", &gameViewportCustomHeight, 1.0f, 64, 8192)) {
                    gameViewportCustomHeight = std::clamp(gameViewportCustomHeight, 64, 8192);
                    editorSettingsChanged = true;
                }
            }
            if (ImGui::Checkbox("Auto Fit Preview", &gameViewportAutoFit)) {
                editorSettingsChanged = true;
            }
            ImGui::BeginDisabled(gameViewportAutoFit);
            float zoomPercent = gameViewportZoom * 100.0f;
            if (ImGui::SliderFloat("Preview Zoom", &zoomPercent, 100.0f, 800.0f, "%.0f%%")) {
                gameViewportZoom = std::clamp(zoomPercent / 100.0f, 1.0f, 8.0f);
                editorSettingsChanged = true;
            }
            ImGui::EndDisabled();

            if (ImGui::Checkbox("Show Viewport Profiler Overlay", &showGameProfiler)) editorSettingsChanged = true;
            if (ImGui::Checkbox("Reveal Debug Sections and Menus", &revealDebugSectionsAndMenus)) editorSettingsChanged = true;
            if (ImGui::Checkbox("Canvas Guides", &showCanvasOverlay)) editorSettingsChanged = true;
            if (ImGui::Checkbox("UI World Grid", &showUIWorldGrid)) editorSettingsChanged = true;
            if (ImGui::Checkbox("Viewport Hint Overlay", &showViewportHintOverlay)) editorSettingsChanged = true;
            const char* toolbarCornerOptions[] = { "Bottom Left", "Bottom Right", "Top Left", "Top Right" };
            int toolbarCornerIndex = static_cast<int>(sceneViewportToolbarCorner);
            if (toolbarCornerIndex < 0 || toolbarCornerIndex >= static_cast<int>(IM_ARRAYSIZE(toolbarCornerOptions))) {
                toolbarCornerIndex = 0;
            }
            if (ImGui::Combo("Viewport Toolbar Corner", &toolbarCornerIndex, toolbarCornerOptions, IM_ARRAYSIZE(toolbarCornerOptions))) {
                sceneViewportToolbarCorner = static_cast<ViewportToolbarCorner>(toolbarCornerIndex);
                editorSettingsChanged = true;
            }
            if (ImGui::Checkbox("2D Light Stats Overlay", &showLight2DStatsOverlay)) editorSettingsChanged = true;
            if (ImGui::SliderFloat("2D Light Buffer Scale", &light2DLightingBufferScale, 0.5f, 1.0f, "%.2fx")) {
                light2DLightingBufferScale = std::clamp(light2DLightingBufferScale, 0.5f, 1.0f);
                editorSettingsChanged = true;
            }
        }

        if (ImGui::CollapsingHeader("Renderer##ProjectRendererSection", ImGuiTreeNodeFlags_DefaultOpen)) {
            const bool openGlSettingsAvailable = rendererInitialized && !usingVulkan();
            if (!openGlSettingsAvailable) {
                ImGui::TextDisabled("OpenGL renderer settings are editable only in OpenGL sessions.");
                ImGui::TextDisabled("Current session renderer: %s", Modularity::ToString(graphicsBackend));
            }
            ImGui::BeginDisabled(!openGlSettingsAvailable);
            glm::vec3 ambient = renderer.getAmbientColor();
            if (ImGui::ColorEdit3("Ambient Color", &ambient.x)) {
                renderer.setAmbientColor(ambient);
                buildSettings.rendererAmbientColor = ambient;
                buildSettingsChanged = true;
            }

            int shadowResolution = renderer.getShadowMapResolution();
            if (ImGui::SliderInt("Shadow Resolution", &shadowResolution, 128, 4096)) {
                renderer.setShadowMapResolution(shadowResolution);
                buildSettings.rendererShadowResolution = renderer.getShadowMapResolution();
                buildSettingsChanged = true;
            }

            bool shaderAutoReload = renderer.isShaderAutoReloadEnabled();
            if (ImGui::Checkbox("Auto Reload Shaders", &shaderAutoReload)) {
                renderer.setShaderAutoReload(shaderAutoReload);
                buildSettings.rendererAutoReloadShaders = shaderAutoReload;
                buildSettingsChanged = true;
            }
            ImGui::EndDisabled();
        }

        if (ImGui::CollapsingHeader("Editor Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::DragFloat("Move Speed", &camera.moveSpeed, 0.1f, 0.01f, 100.0f, "%.2f")) {
                camera.moveSpeed = std::max(0.01f, camera.moveSpeed);
                camera.sprintSpeed = std::max(camera.moveSpeed, camera.sprintSpeed);
                editorSettingsChanged = true;
            }
            if (ImGui::DragFloat("Sprint Speed", &camera.sprintSpeed, 0.1f, 0.01f, 200.0f, "%.2f")) {
                camera.sprintSpeed = std::max(camera.moveSpeed, camera.sprintSpeed);
                editorSettingsChanged = true;
            }
            if (ImGui::Checkbox("Smooth Movement", &camera.smoothMovement)) editorSettingsChanged = true;
            ImGui::BeginDisabled(!camera.smoothMovement);
            if (ImGui::DragFloat("Acceleration", &camera.acceleration, 0.1f, 0.1f, 200.0f, "%.2f")) {
                camera.acceleration = std::max(0.1f, camera.acceleration);
                editorSettingsChanged = true;
            }
            ImGui::EndDisabled();
            if (ImGui::SliderFloat("Mouse Sensitivity", &camera.mouseSensitivity, 0.001f, 1.0f, "%.3f")) {
                camera.mouseSensitivity = std::clamp(camera.mouseSensitivity, 0.001f, 1.0f);
                editorSettingsChanged = true;
            }
            ImGui::Separator();
            if (ImGui::SliderFloat("Projection FOV", &buildSettings.editorCameraFov, 20.0f, 140.0f, "%.1f")) {
                buildSettingsChanged = true;
            }
            if (ImGui::DragFloat("Near Clip", &buildSettings.editorCameraNear, 0.01f, 0.01f, buildSettings.editorCameraFar - 0.01f, "%.3f")) {
                buildSettings.editorCameraNear = std::max(0.01f, std::min(buildSettings.editorCameraNear, buildSettings.editorCameraFar - 0.01f));
                buildSettingsChanged = true;
            }
            if (ImGui::DragFloat("Far Clip", &buildSettings.editorCameraFar, 0.1f, buildSettings.editorCameraNear + 0.05f, 5000.0f, "%.1f")) {
                buildSettings.editorCameraFar = std::max(buildSettings.editorCameraNear + 0.05f, buildSettings.editorCameraFar);
                buildSettingsChanged = true;
            }
        }

        if (ImGui::CollapsingHeader("Debug / Performance", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Checkbox("Scene Gizmos", &showSceneGizmos)) editorSettingsChanged = true;
            if (ImGui::Checkbox("3D Grid", &showSceneGrid3D)) editorSettingsChanged = true;
            if (ImGui::Checkbox("Selection Collider Bounds", &collisionWireframe)) editorSettingsChanged = true;
            if (ImGui::Checkbox("FPS Cap", &fpsCapEnabled)) editorSettingsChanged = true;
            ImGui::BeginDisabled(!fpsCapEnabled);
            if (ImGui::DragFloat("FPS Target", &fpsCap, 1.0f, 1.0f, 500.0f, "%.0f")) {
                fpsCap = std::max(1.0f, fpsCap);
                editorSettingsChanged = true;
            }
            ImGui::EndDisabled();
            if (revealDebugSectionsAndMenus) {
                ImGui::Separator();
                if (ImGui::CollapsingHeader("Game Profiler", ImGuiTreeNodeFlags_DefaultOpen)) {
                    drawGameProfilerContent();
                }
            }
        }

        if (editorSettingsChanged) {
            saveEditorUserSettings();
        }
        if (buildSettingsChanged) {
            saveBuildSettings();
        }
    } else if (selectedTab == 3) {
        bool changed = false;

        if (ImGui::CollapsingHeader("Build Targets", ImGuiTreeNodeFlags_DefaultOpen)) {
            const char* targets[] = {"Windows", "Linux", "Android"};
            int targetIndex = static_cast<int>(buildSettings.platform);
            if (ImGui::Combo("Platform", &targetIndex, targets, IM_ARRAYSIZE(targets))) {
                buildSettings.platform = static_cast<BuildPlatform>(targetIndex);
                changed = true;
            }

            const char* arches[] = {"x86_64", "x86"};
            int archIndex = (buildSettings.architecture == "x86") ? 1 : 0;
            if (ImGui::Combo("Architecture", &archIndex, arches, IM_ARRAYSIZE(arches))) {
                buildSettings.architecture = arches[archIndex];
                changed = true;
            }

            if (ImGui::Checkbox("Development Build", &buildSettings.developmentBuild)) changed = true;
            if (ImGui::Checkbox("Script Debugging", &buildSettings.scriptDebugging)) changed = true;
            if (ImGui::Checkbox("Auto-connect Profiler", &buildSettings.autoConnectProfiler)) changed = true;
            if (ImGui::Checkbox("Deep Profiling", &buildSettings.deepProfiling)) changed = true;
            if (ImGui::Checkbox("Scripts Only Build", &buildSettings.scriptsOnlyBuild)) changed = true;
            if (ImGui::Checkbox("Server Build", &buildSettings.serverBuild)) changed = true;

            const char* compressionOptions[] = {"Default", "None", "LZ4", "LZ4HC"};
            int compressionIndex = 0;
            for (int i = 0; i < IM_ARRAYSIZE(compressionOptions); ++i) {
                if (buildSettings.compressionMethod == compressionOptions[i]) {
                    compressionIndex = i;
                    break;
                }
            }
            if (ImGui::Combo("Compression", &compressionIndex, compressionOptions, IM_ARRAYSIZE(compressionOptions))) {
                buildSettings.compressionMethod = compressionOptions[compressionIndex];
                changed = true;
            }
        }

        if (ImGui::CollapsingHeader("Scenes In Build", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::BeginChild("ProjectBuildScenes", ImVec2(0, 180), true);
            for (int i = 0; i < static_cast<int>(buildSettings.scenes.size()); ++i) {
                BuildSceneEntry& entry = buildSettings.scenes[i];
                ImGui::PushID(i);
                bool enabled = entry.enabled;
                if (ImGui::Checkbox("##enabled", &enabled)) {
                    entry.enabled = enabled;
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::Selectable(entry.name.c_str(), buildSettingsSelectedIndex == i, ImGuiSelectableFlags_SpanAllColumns)) {
                    buildSettingsSelectedIndex = i;
                }
                ImGui::PopID();
            }
            ImGui::EndChild();

            if (ImGui::Button("Add Current Scene")) {
                if (addSceneToBuildSettings(projectManager.currentProject.currentSceneName, true)) {
                    changed = true;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Add All Scenes")) {
                auto scenes = projectManager.currentProject.getSceneList();
                for (const auto& scene : scenes) {
                    if (addSceneToBuildSettings(scene, scene == projectManager.currentProject.currentSceneName)) {
                        changed = true;
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Remove Selected")) {
                if (buildSettingsSelectedIndex >= 0 &&
                    buildSettingsSelectedIndex < static_cast<int>(buildSettings.scenes.size())) {
                    buildSettings.scenes.erase(buildSettings.scenes.begin() + buildSettingsSelectedIndex);
                    if (buildSettingsSelectedIndex >= static_cast<int>(buildSettings.scenes.size())) {
                        buildSettingsSelectedIndex = static_cast<int>(buildSettings.scenes.size()) - 1;
                    }
                    changed = true;
                }
            }
        }

        ImGui::Separator();
        ImGui::TextDisabled("Export");
        if (ImGui::Button("Open Advanced Build Window")) {
            showBuildSettings = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Save Build Profile")) {
            saveBuildSettings();
        }

        if (changed) {
            saveBuildSettings();
        }
    } else if (selectedTab == 4) {
        struct CompilationUiState {
            fs::path configPath;
            ScriptBuildConfig config;
            bool loaded = false;
            bool dirty = false;
            std::string status;
        };
        static CompilationUiState ui;

        fs::path configPath = resolveScriptsConfigPath(projectManager.currentProject);
        if (!ui.loaded || ui.configPath != configPath) {
            ui = CompilationUiState{};
            ui.configPath = configPath;
            std::string error;
            if (!scriptCompiler.loadConfig(configPath, ui.config, error)) {
                ui.status = "Creating new scripts config (previous load failed): " + error;
                ui.config = ScriptBuildConfig{};
                ui.config.scriptsDir = projectManager.currentProject.projectPath / "Assets" / "Scripts";
                ui.config.outDir = projectManager.currentProject.projectPath / "Library" / "CompiledScripts";
                ui.loaded = true;
            } else {
                ui.status = "Loaded " + configPath.filename().string();
                ui.loaded = true;
            }
        }

        {
            const ProjectSettingsUiIcon cppLogo = resolveProjectSettingsIcon(
                "Resources/Engine-Root/Project Settings/CompilationTab/CPPLogo.png");
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.11f, 0.16f, 0.84f));
            ImGui::BeginChild("CompilationHeader", ImVec2(0, 76.0f), true,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            if (cppLogo.id != static_cast<ImTextureID>(0)) {
                const ImVec2 uvMin = cppLogo.flipY ? ImVec2(0.0f, 1.0f) : ImVec2(0.0f, 0.0f);
                const ImVec2 uvMax = cppLogo.flipY ? ImVec2(1.0f, 0.0f) : ImVec2(1.0f, 1.0f);
                ImGui::Image(cppLogo.id, ImVec2(44.0f, 44.0f), uvMin, uvMax);
                ImGui::SameLine();
            }
            ImGui::BeginGroup();
            ImGui::Text("Compilation");
            ImGui::TextDisabled("C++26 default, C++23 fallback, pre-C++20 marked deprecated.");
            ImGui::TextDisabled("Script compiler flags are normalized per toolchain.");
            ImGui::EndGroup();
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::Spacing();
        }

        bool editorSettingsChanged = false;
        if (ImGui::CollapsingHeader("Compiler Workflow", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Checkbox("Auto-compile on Save", &scriptEditorState.autoCompileOnSave)) {
                editorSettingsChanged = true;
            }
            float interval = static_cast<float>(scriptAutoCompileInterval);
            if (ImGui::SliderFloat("Auto-compile Scan Interval (s)", &interval, 0.1f, 10.0f, "%.2f")) {
                scriptAutoCompileInterval = std::clamp(static_cast<double>(interval), 0.1, 10.0);
                editorSettingsChanged = true;
            }
        }

        if (ImGui::CollapsingHeader("scripts.modu", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto editString = [&](const char* label, std::string& value, size_t capacity = 1024) {
                std::vector<char> buf(capacity, '\0');
                std::snprintf(buf.data(), buf.size(), "%s", value.c_str());
                if (ImGui::InputText(label, buf.data(), buf.size())) {
                    value = buf.data();
                    ui.dirty = true;
                }
            };
            auto editPath = [&](const char* label, fs::path& value, size_t capacity = 1024) {
                std::string text = value.generic_string();
                std::vector<char> buf(capacity, '\0');
                std::snprintf(buf.data(), buf.size(), "%s", text.c_str());
                if (ImGui::InputText(label, buf.data(), buf.size())) {
                    value = fs::path(buf.data());
                    ui.dirty = true;
                }
            };

            auto canonicalCppStd = [](std::string value) {
                std::transform(value.begin(), value.end(), value.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (value == "c++2b") return std::string("c++23");
                if (value == "gnu++2b") return std::string("gnu++23");
                if (value == "c++2c") return std::string("c++26");
                if (value == "gnu++2c") return std::string("gnu++26");
                return value;
            };
            auto isDeprecatedCppStd = [&](const std::string& value) {
                const std::string normalized = canonicalCppStd(value);
                return normalized == "c++17" || normalized == "gnu++17" ||
                       normalized == "c++14" || normalized == "gnu++14";
            };

            static const char* cppStdLabels[] = {
                "c++26 (Default)",
                "c++23",
                "c++20",
                "c++17 (Deprecated)",
                "gnu++26",
                "gnu++23",
                "gnu++20",
                "gnu++17 (Deprecated)"
            };
            static const char* cppStdValues[] = {
                "c++26",
                "c++23",
                "c++20",
                "c++17",
                "gnu++26",
                "gnu++23",
                "gnu++20",
                "gnu++17"
            };

            int cppIdx = 0;
            const std::string currentCppStandard = canonicalCppStd(ui.config.cppStandard);
            for (int i = 0; i < IM_ARRAYSIZE(cppStdValues); ++i) {
                if (currentCppStandard == cppStdValues[i]) {
                    cppIdx = i;
                    break;
                }
            }
            if (ImGui::Combo("C++ Standard", &cppIdx, cppStdLabels, IM_ARRAYSIZE(cppStdLabels))) {
                ui.config.cppStandard = cppStdValues[cppIdx];
                ui.dirty = true;
            }
            if (isDeprecatedCppStd(ui.config.cppStandard)) {
                ImGui::TextDisabled("ModuCPP standards below C++20 are deprecated and may be removed later.");
            } else {
                ImGui::TextDisabled("Default is C++26. Use C++23 if your toolchain is not ready for C++26 yet.");
            }

            editPath("Scripts Directory", ui.config.scriptsDir);
            editPath("Output Directory", ui.config.outDir);

            ImGui::Separator();
            ImGui::TextDisabled("Include Directories");
            for (size_t i = 0; i < ui.config.includeDirs.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                editPath("##inc", ui.config.includeDirs[i], 1200);
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove")) {
                    ui.config.includeDirs.erase(ui.config.includeDirs.begin() + static_cast<long>(i));
                    ui.dirty = true;
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            if (ImGui::SmallButton("Add Include Directory")) {
                ui.config.includeDirs.emplace_back(fs::path());
                ui.dirty = true;
            }

            ImGui::Separator();
            ImGui::TextDisabled("Defines");
            for (size_t i = 0; i < ui.config.defines.size(); ++i) {
                ImGui::PushID(static_cast<int>(1000 + i));
                editString("##def", ui.config.defines[i], 1024);
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove")) {
                    ui.config.defines.erase(ui.config.defines.begin() + static_cast<long>(i));
                    ui.dirty = true;
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            if (ImGui::SmallButton("Add Define")) {
                ui.config.defines.push_back("");
                ui.dirty = true;
            }

            ImGui::Separator();
            ImGui::TextDisabled("Linux Link Libraries");
            for (size_t i = 0; i < ui.config.linuxLinkLibs.size(); ++i) {
                ImGui::PushID(static_cast<int>(2000 + i));
                editString("##linlib", ui.config.linuxLinkLibs[i], 512);
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove")) {
                    ui.config.linuxLinkLibs.erase(ui.config.linuxLinkLibs.begin() + static_cast<long>(i));
                    ui.dirty = true;
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            if (ImGui::SmallButton("Add Linux Lib")) {
                ui.config.linuxLinkLibs.push_back("");
                ui.dirty = true;
            }

            ImGui::Separator();
            ImGui::TextDisabled("Windows Link Libraries");
            for (size_t i = 0; i < ui.config.windowsLinkLibs.size(); ++i) {
                ImGui::PushID(static_cast<int>(3000 + i));
                editString("##winlib", ui.config.windowsLinkLibs[i], 512);
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove")) {
                    ui.config.windowsLinkLibs.erase(ui.config.windowsLinkLibs.begin() + static_cast<long>(i));
                    ui.dirty = true;
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            if (ImGui::SmallButton("Add Windows Lib")) {
                ui.config.windowsLinkLibs.push_back("");
                ui.dirty = true;
            }

            ImGui::Spacing();
            if (ImGui::Button("Reload scripts.modu")) {
                ui.loaded = false;
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(!ui.dirty);
            if (ImGui::Button("Save scripts.modu")) {
                std::string error;
                if (SaveScriptsConfig(ui.configPath, ui.config, projectManager.currentProject.projectPath, error)) {
                    ui.status = "Saved " + ui.configPath.filename().string();
                    ui.dirty = false;
                    scriptingFilesDirty = true;
                    scriptLastAutoCompileTime.clear();
                    scriptAutoCompileCheckedSourceTime.clear();
                    scriptAutoCompileBinaryCache.clear();
                    scriptAutoCompileDiscoveredSources.clear();
                    autoCompileQueue.clear();
                    autoCompileQueued.clear();
                    scriptAutoCompileLastDirectoryScan = 0.0;
                    managedAutoCompileLastScan = 0.0;
                    managedAutoCompileCachedProjectDir.clear();
                    managedAutoCompileNewestSource = fs::file_time_type{};
                    managedAutoCompileHasSource = false;
                    addConsoleMessage("Saved scripts config: " + ui.configPath.string(), ConsoleMessageType::Success);
                } else {
                    ui.status = error;
                    addConsoleMessage(error, ConsoleMessageType::Error);
                }
            }
            ImGui::EndDisabled();
            if (!ui.status.empty()) {
                ImGui::TextDisabled("%s", ui.status.c_str());
            }
        }

        if (editorSettingsChanged) {
            saveEditorUserSettings();
        }
    }

    ImGui::EndChild();

    ImGui::End();
    if (wasOpen != showProjectBrowser) {
        saveEditorUserSettings();
    }
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);
}
#pragma endregion
