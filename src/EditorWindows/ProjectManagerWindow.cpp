#include "Engine.h"
#include "ModelLoader.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <cstdlib>
#include <cfloat>
#include <cmath>
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
struct PackageTaskResult {
    bool success = false;
    std::string message;
};

struct PackageTaskState {
    bool active = false;
    float startTime = 0.0f;
    std::string label;
    std::future<PackageTaskResult> future;
};

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
} // namespace
#pragma endregion

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
        if (audio.isReady()) {
            audio.playPreview("Resources/Sounds/ModuIntro.mp3", 0.85f, false);
        }
        launcherIntroSoundPlayed = true;
    }

    const float introFadeIn = 0.6f;
    const float introHold = 0.5f;
    const float introFadeOut = 0.7f;
    const float introSlide = 0.8f;
    const float introSlideStart = introFadeIn + introHold + introFadeOut;
    const float introTotal = introSlideStart + introSlide;
    const double introElapsedRaw = launcherIntroFinished ? introTotal : (now - launcherIntroStartTime);
    const float introElapsed = static_cast<float>(introElapsedRaw);

    float textAlpha = 0.0f;
    if (!launcherIntroFinished) {
        if (introElapsed < introFadeIn) {
            textAlpha = introElapsed / introFadeIn;
        } else if (introElapsed < introFadeIn + introHold) {
            textAlpha = 1.0f;
        } else if (introElapsed < introSlideStart) {
            textAlpha = 1.0f - (introElapsed - (introFadeIn + introHold)) / introFadeOut;
        }
    }

    float slideT = 1.0f;
    if (!launcherIntroFinished) {
        slideT = ImClamp((introElapsed - introSlideStart) / introSlide, 0.0f, 1.0f);
        if (slideT >= 1.0f) {
            launcherIntroFinished = true;
        }
    }
    const float slideEase = 1.0f - std::pow(1.0f - slideT, 3.0f);

    const float transitionDuration = 0.45f;
    float transitionT = 0.0f;
    if (launcherTransitionActive) {
        transitionT = ImClamp(static_cast<float>((now - launcherTransitionStartTime) / transitionDuration), 0.0f, 1.0f);
    }
    const float transitionEase = 1.0f - std::pow(1.0f - transitionT, 3.0f);
    const float transitionAlpha = 1.0f - transitionEase;
    const float uiScale = 1.0f + 0.06f * transitionEase;
    const float introOffsetT = launcherIntroFinished ? 0.0f : (1.0f - slideEase);
    const float contentAlpha = launcherIntroFinished ? 1.0f : slideEase;
    const float introLeftOffsetX = -140.0f * uiScale * introOffsetT;
    const float introRightOffsetX = 140.0f * uiScale * introOffsetT;
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

    if (ImGui::Begin("Launcher", nullptr, flags))
    {
        const float leftPanelWidth = 300.0f * uiScale;
        const float heroHeight = 120.0f * uiScale;
        const ImVec4 bgTopLeft = ImVec4(0.10f, 0.11f, 0.16f, 1.0f);
        const ImVec4 bgTopRight = ImVec4(0.15f, 0.16f, 0.22f, 1.0f);
        const ImVec4 bgBottomRight = ImVec4(0.07f, 0.08f, 0.12f, 1.0f);
        const ImVec4 bgBottomLeft = ImVec4(0.08f, 0.09f, 0.13f, 1.0f);
        const ImVec4 cardBg = ImVec4(0.14f, 0.15f, 0.21f, 0.98f);
        const ImVec4 cardOutline = ImVec4(0.20f, 0.22f, 0.30f, 0.70f);
        const ImVec4 accent = ImVec4(0.91f, 0.42f, 0.78f, 1.0f);
        const ImVec4 accentCool = ImVec4(0.42f, 0.72f, 0.96f, 1.0f);

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 windowPos = ImGui::GetWindowPos();
        ImVec2 windowSize = ImGui::GetWindowSize();

        Texture* previewTexture = nullptr;
        if ((projectLoadInProgress || sceneLoadInProgress || launcherTransitionActive || launcherTransitionPendingHide) &&
            !launcherLoadingPreviewPath.empty()) {
            fs::path previewPath = launcherLoadingPreviewPath;
            if (fs::exists(previewPath)) {
                previewTexture = renderer.getTexture(previewPath.string());
            }
        }

        if (previewTexture) {
            DrawBlurredImageCover(drawList,
                                  (ImTextureID)(intptr_t)previewTexture->GetID(),
                                  windowPos,
                                  ImVec2(windowPos.x + windowSize.x, windowPos.y + windowSize.y),
                                  previewTexture->GetWidth(),
                                  previewTexture->GetHeight(),
                                  0.55f,
                                  10.0f * uiScale);
        }

        const float bgAlpha = previewTexture ? 0.60f : 1.0f;
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
            ImGui::GetColorU32(gradBL)
        );

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

        ImGui::SetCursorPos(ImVec2(contentStart.x, contentStart.y + introHeroOffsetY));
        ImGui::BeginChild("LauncherHero", ImVec2(0, heroHeight), true,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);

        ImDrawList* heroDraw = ImGui::GetWindowDrawList();
        ImVec2 heroPos = ImGui::GetWindowPos();
        ImVec2 heroSize = ImGui::GetWindowSize();
        heroDraw->AddRectFilled(heroPos, ImVec2(heroPos.x + heroSize.x, heroPos.y + heroSize.y),
                                ImGui::GetColorU32(cardBg), 18.0f * uiScale);
        heroDraw->AddRect(heroPos, ImVec2(heroPos.x + heroSize.x, heroPos.y + heroSize.y),
                          ImGui::GetColorU32(cardBg), 18.0f * uiScale);

        ImGui::SetCursorPos(ImVec2(28.0f * uiScale, 24.0f * uiScale));
        ImGui::TextDisabled("Project Manager");
        ImGui::SetWindowFontScale(1.4f * uiScale);
        ImGui::TextColored(ImVec4(0.95f, 0.96f, 0.98f, 1.0f), "Modularity");
        ImGui::SetWindowFontScale(1.0f * uiScale);
        ImGui::TextColored(ImVec4(0.70f, 0.73f, 0.80f, 1.0f), "Modularity | Beta V6.3");


        ImGui::EndChild();

        ImVec2 heroItemSize = ImGui::GetItemRectSize();
        ImGui::SetCursorPos(ImVec2(contentStart.x, contentStart.y + heroItemSize.y));

        ImGui::Spacing();
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_ChildBg, cardBg);
        ImVec2 leftStart = ImGui::GetCursorPos();
        ImGui::SetCursorPos(ImVec2(leftStart.x + introLeftOffsetX, leftStart.y));
        ImGui::BeginChild("LauncherLeft", ImVec2(leftPanelWidth, 0), true);
        ImGui::PopStyleColor();

        ImGui::TextColored(ImVec4(0.78f, 0.80f, 0.86f, 1.0f), "GET STARTED");
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.32f, 0.22f, 0.54f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.43f, 0.30f, 0.70f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.38f, 0.26f, 0.60f, 1.0f));

        if (ImGui::Button("New Project", ImVec2(-1, 40.0f * uiScale)))
        {
            projectManager.showNewProjectDialog = true;
            projectManager.errorMessage.clear();
            std::memset(projectManager.newProjectName, 0, sizeof(projectManager.newProjectName));

            #ifdef _WIN32
            char documentsPath[MAX_PATH];
            SHGetFolderPathA(NULL, CSIDL_MYDOCUMENTS, NULL, 0, documentsPath);
            std::strcpy(projectManager.newProjectLocation, documentsPath);
            std::strcat(projectManager.newProjectLocation, "\\ModularityProjects");
            #else
            const char* home = std::getenv("HOME");
            if (home)
            {
                std::strcpy(projectManager.newProjectLocation, home);
                std::strcat(projectManager.newProjectLocation, "/ModularityProjects");
            }
            #endif
        }

        ImGui::Spacing();

        if (ImGui::Button("Open Project", ImVec2(-1, 40.0f * uiScale)))
        {
            projectManager.showOpenProjectDialog = true;
            projectManager.errorMessage.clear();
        }

        ImGui::PopStyleColor(3);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextColored(ImVec4(0.78f, 0.80f, 0.86f, 1.0f), "QUICK ACTIONS");
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.22f, 0.32f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.30f, 0.42f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.24f, 0.28f, 0.40f, 1.0f));

        if (ImGui::Button("Documentation", ImVec2(-1, 34.0f * uiScale)))
        {
            #ifdef _WIN32
            system("start https://docs.shockinteractive.xyz");
            #else
            system("xdg-open https://docs.shockinteractive.xyz &");
            #endif
        }

        if (ImGui::Button("Exit", ImVec2(-1, 34.0f * uiScale)))
        {
            glfwSetWindowShouldClose(editorWindow, GLFW_TRUE);
        }

        ImGui::PopStyleColor(3);

        ImGui::EndChild();

        ImVec2 leftSize = ImGui::GetItemRectSize();
        ImGui::SetCursorPos(leftStart);
        ImGui::Dummy(leftSize);

        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_ChildBg, cardBg);
        ImVec2 rightStart = ImGui::GetCursorPos();
        ImGui::SetCursorPos(ImVec2(rightStart.x + introRightOffsetX, rightStart.y));
        ImGui::BeginChild("LauncherRight", ImVec2(0, 0), true);
        ImGui::PopStyleColor();

        ImGui::TextColored(ImVec4(0.78f, 0.80f, 0.86f, 1.0f), "RECENT PROJECTS");
        ImGui::Spacing();

        if (projectManager.recentProjects.empty())
        {
            ImGui::Spacing();
            ImGui::TextDisabled("There are no recent projects yet.\nCreate or open a project to get started.");
        }
        else
        {
            float availWidth = ImGui::GetContentRegionAvail().x;
            for (size_t i = 0; i < projectManager.recentProjects.size(); ++i)
            {
                const auto& rp = projectManager.recentProjects[i];
                ImGui::PushID(static_cast<int>(i));

                const ImVec2 cardSize(availWidth, 72.0f * uiScale);
                const ImVec2 cardPos = ImGui::GetCursorScreenPos();
                ImGui::InvisibleButton("RecentCard", cardSize);
                bool hovered = ImGui::IsItemHovered();
                bool clicked = ImGui::IsItemClicked();
                ImVec2 afterPos = ImGui::GetCursorScreenPos();

                if (ImGui::BeginPopupContextItem("RecentProjectContext"))
                {
                    if (ImGui::MenuItem("Open"))
                    {
                        if (audio.isReady()) {
                            audio.playPreview("Resources/Sounds/Selection.mp3", 0.95f, false);
                        }
                        launcherTransitionActive = true;
                        launcherTransitionPendingHide = false;
                        launcherTransitionStartTime = glfwGetTime();
                        launcherTransitionFocus = ImVec2(cardPos.x + cardSize.x * 0.5f,
                                                         cardPos.y + cardSize.y * 0.5f);
                        fs::path previewPath = getProjectPreviewPath(rp.path);
                        if (!previewPath.empty() && fs::exists(previewPath)) {
                            launcherLoadingPreviewPath = previewPath.string();
                        }
                        OpenProjectPath(rp.path);
                    }

                    if (ImGui::MenuItem("Remove from Recent"))
                    {
                        projectManager.recentProjects.erase(
                            projectManager.recentProjects.begin() + i
                        );
                        projectManager.saveRecentProjects();
                        ImGui::EndPopup();
                        ImGui::PopID();
                        break;
                    }

                    ImGui::EndPopup();
                }

                ImDrawList* list = ImGui::GetWindowDrawList();
                Texture* previewTex = nullptr;
                fs::path previewPath = getProjectPreviewPath(rp.path);
                if (!previewPath.empty() && fs::exists(previewPath)) {
                    previewTex = renderer.getTexture(previewPath.string());
                }
                if (previewTex) {
                    DrawImageCover(list,
                                   (ImTextureID)(intptr_t)previewTex->GetID(),
                                   cardPos,
                                   ImVec2(cardPos.x + cardSize.x, cardPos.y + cardSize.y),
                                   previewTex->GetWidth(),
                                   previewTex->GetHeight(),
                                   ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)),
                                   14.0f * uiScale);
                    ImVec4 overlayCol = hovered ? ImVec4(0.08f, 0.09f, 0.13f, 0.55f)
                                                : ImVec4(0.08f, 0.09f, 0.13f, 0.65f);
                    list->AddRectFilled(cardPos, ImVec2(cardPos.x + cardSize.x, cardPos.y + cardSize.y),
                                        ImGui::GetColorU32(overlayCol), 14.0f * uiScale);
                } else {
                    ImU32 cardCol = ImGui::GetColorU32(hovered ? ImVec4(0.18f, 0.19f, 0.27f, 1.0f)
                                                              : ImVec4(0.16f, 0.17f, 0.24f, 1.0f));
                    list->AddRectFilled(cardPos, ImVec2(cardPos.x + cardSize.x, cardPos.y + cardSize.y),
                                        cardCol, 14.0f * uiScale);
                }
                list->AddRect(cardPos, ImVec2(cardPos.x + cardSize.x, cardPos.y + cardSize.y),
                              ImGui::GetColorU32(cardOutline), 14.0f * uiScale);

                ImVec2 textPos = ImVec2(cardPos.x + 16.0f * uiScale, cardPos.y + 14.0f * uiScale);
                ImGui::SetCursorScreenPos(textPos);
                ImGui::TextColored(ImVec4(0.92f, 0.93f, 0.96f, 1.0f), "%s", rp.name.c_str());
                ImGui::SetCursorScreenPos(ImVec2(textPos.x, textPos.y + 22.0f * uiScale));
                ImGui::TextDisabled("%s", rp.path.c_str());

                const float buttonWidth = 88.0f * uiScale;
                ImGui::SetCursorScreenPos(ImVec2(cardPos.x + cardSize.x - buttonWidth - 16.0f * uiScale,
                                                 cardPos.y + 20.0f * uiScale));
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.24f, 0.28f, 0.40f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.32f, 0.38f, 0.55f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.30f, 0.35f, 0.50f, 1.0f));
                bool openClicked = ImGui::Button("Open", ImVec2(buttonWidth, 30.0f * uiScale));
                ImGui::PopStyleColor(3);

                if ((clicked && !openClicked) || openClicked)
                {
                    if (audio.isReady()) {
                        audio.playPreview("Resources/Sounds/Selection.mp3", 0.95f, false);
                    }
                    launcherTransitionActive = true;
                    launcherTransitionPendingHide = false;
                    launcherTransitionStartTime = glfwGetTime();
                    launcherTransitionFocus = ImVec2(cardPos.x + cardSize.x * 0.5f,
                                                     cardPos.y + cardSize.y * 0.5f);
                    fs::path previewPath = getProjectPreviewPath(rp.path);
                    if (!previewPath.empty() && fs::exists(previewPath)) {
                        launcherLoadingPreviewPath = previewPath.string();
                    }
                    OpenProjectPath(rp.path);
                }

                ImGui::SetCursorScreenPos(afterPos);

                ImGui::PopID();
                ImGui::Spacing();
            }
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextDisabled("Modularity Engine - Beta V6.3");
        ImGui::EndChild();

        ImVec2 rightSize = ImGui::GetItemRectSize();
        ImGui::SetCursorPos(rightStart);
        ImGui::Dummy(rightSize);

        if (!launcherIntroFinished) {
            ImGui::PopItemFlag();
        }
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleVar();

        if (textAlpha > 0.001f) {
            ImDrawList* overlay = ImGui::GetWindowDrawList();
            const char* title = "Modularity";
            ImFont* font = ImGui::GetFont();
            const float baseFontSize = ImGui::GetFontSize();
            const float fadeOutT = ImClamp((introElapsed - (introFadeIn + introHold)) / introFadeOut, 0.0f, 1.0f);
            const float globalScale = 2.0f - 0.18f * fadeOutT;
            const float fontSizeNormal = baseFontSize * 2.6f * globalScale;
            const ImVec4 baseCol = ImVec4(0.95f, 0.96f, 0.98f, 1.0f);
            const float letterDelay = 0.07f;
            const float letterIn = 0.18f;

            auto ease = [](float t) {
                t = ImClamp(t, 0.0f, 1.0f);
                return t * t * (3.0f - 2.0f * t);
            };

            ImVec2 center = ImVec2(displaySize.x * 0.5f, displaySize.y * 0.38f);
            float totalWidth = 0.0f;
            for (const char* c = title; *c; ++c) {
                char letter[2] = { *c, 0 };
                totalWidth += font->CalcTextSizeA(fontSizeNormal, FLT_MAX, 0.0f, letter).x;
            }

            ImVec2 textPos = ImVec2(center.x - totalWidth * 0.5f,
                                    center.y - fontSizeNormal * 0.5f);

            float advanceX = 0.0f;
            int index = 0;
            for (const char* c = title; *c; ++c, ++index) {
                char letter[2] = { *c, 0 };
                float letterT = (introElapsed - (letterDelay * index)) / letterIn;
                float letterEase = ease(letterT);
                float letterAlpha = textAlpha * letterEase;
                if (letterAlpha <= 0.001f) {
                    float letterWidth = font->CalcTextSizeA(fontSizeNormal, FLT_MAX, 0.0f, letter).x;
                    advanceX += letterWidth;
                    continue;
                }

                float letterScale = (1.15f - 0.15f * letterEase) * globalScale;
                float letterFontSize = baseFontSize * 2.6f * letterScale;
                float letterWidth = font->CalcTextSizeA(fontSizeNormal, FLT_MAX, 0.0f, letter).x;
                float offsetX = (letterWidth * (letterScale / globalScale - 1.0f)) * 0.5f;
                ImVec2 letterPos = ImVec2(textPos.x + advanceX - offsetX, textPos.y);
                ImU32 textCol = ImGui::GetColorU32(ImVec4(baseCol.x, baseCol.y, baseCol.z, letterAlpha));
                overlay->AddText(font, letterFontSize, letterPos, textCol, letter);
                advanceX += letterWidth;
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

    if (projectManager.showNewProjectDialog)
        renderNewProjectDialog();
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
    ImGui::SetNextWindowSize(ImVec2(500, 250), ImGuiCond_Appearing);

    if (ImGui::Begin("New Project", &projectManager.showNewProjectDialog,
                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking)) {

        ImGui::Text("Project Name:");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##ProjectName", projectManager.newProjectName,
                       sizeof(projectManager.newProjectName));

        ImGui::Spacing();

        if (ImGui::Button("Choose Pipeline Mode")) {
        }

        ImGui::Spacing();

        ImGui::Text("Location:");
        ImGui::SetNextItemWidth(-70);
        ImGui::InputText("##Location", projectManager.newProjectLocation,
                       sizeof(projectManager.newProjectLocation));
        ImGui::SameLine();
        if (ImGui::Button("Browse")) {
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
                if (audio.isReady()) {
                    audio.playPreview("Resources/Sounds/Selection.mp3", 0.95f, false);
                }
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

    ImGui::Begin("Project Settings", &showProjectBrowser);

    if (!projectManager.currentProject.isLoaded) {
        ImGui::TextDisabled("No project loaded");
        ImGui::End();
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

    static int selectedTab = 0;
    const char* tabs[] = { "Scenes", "Packages", "Assets", "Editor", "Build", "Compilation" };
    constexpr int tabCount = static_cast<int>(IM_ARRAYSIZE(tabs));
    if (selectedTab < 0 || selectedTab >= tabCount) {
        selectedTab = 0;
    }

    ImGui::BeginChild("SettingsNav", ImVec2(180, 0), true);
    ImGui::TextDisabled("Categories");
    ImGui::Separator();
    for (int i = 0; i < tabCount; ++i) {
        if (ImGui::Selectable(tabs[i], selectedTab == i, 0, ImVec2(0, 32))) {
            selectedTab = i;
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
        static PackageTaskState packageTask;
        static std::string packageStatus;
        static char gitUrlBuf[256] = "";
        static char gitNameBuf[128] = "";
        static char gitIncludeBuf[128] = "include";

        auto pollPackageTask = [&]() {
            if (!packageTask.active) return;
            if (!packageTask.future.valid()) {
                packageTask.active = false;
                return;
            }
            auto state = packageTask.future.wait_for(std::chrono::milliseconds(0));
            if (state == std::future_status::ready) {
                PackageTaskResult result = packageTask.future.get();
                packageStatus = result.message;
                packageTask.active = false;
                packageTask.future = std::future<PackageTaskResult>();
            }
        };

        auto startPackageTask = [&](const char* label, std::function<PackageTaskResult()> fn) {
            if (packageTask.active) return;
            packageTask.active = true;
            packageTask.label = label;
            packageTask.startTime = static_cast<float>(ImGui::GetTime());
            packageTask.future = std::async(std::launch::async, std::move(fn));
        };

        pollPackageTask();

        if (!packageStatus.empty()) {
            ImGui::TextWrapped("%s", packageStatus.c_str());
        }

        if (packageTask.active) {
            const ImU32 col = ImGui::GetColorU32(ImGuiCol_ButtonHovered);
            const ImU32 bg = ImGui::GetColorU32(ImGuiCol_Button);
            float elapsed = static_cast<float>(ImGui::GetTime()) - packageTask.startTime;
            float phase = std::fmod(elapsed * 0.25f, 1.0f);

            ImGui::Separator();
            ImGui::Text("%s", packageTask.label.c_str());
            ImGui::BufferingBar("##pkg_buffer", phase, ImVec2(ImGui::GetContentRegionAvail().x, 6.0f), bg, col);
            ImGui::Spinner("##pkg_spinner", 10.0f, 4, col);
        }

        ImGui::BeginDisabled(packageTask.active);
        ImGui::TextDisabled("Add package from Git");
        ImGui::InputTextWithHint("URL", "https://github.com/user/repo.git", gitUrlBuf, sizeof(gitUrlBuf));
        ImGui::InputTextWithHint("Name (optional)", "use repo name", gitNameBuf, sizeof(gitNameBuf));
        ImGui::InputTextWithHint("Include dir", "include", gitIncludeBuf, sizeof(gitIncludeBuf));
        if (ImGui::Button("Add as submodule")) {
            std::string url = gitUrlBuf;
            std::string name = gitNameBuf;
            std::string include = gitIncludeBuf;
            startPackageTask("Installing package...", [this, url, name, include]() {
                PackageTaskResult result;
                std::string newId;
                if (packageManager.installGitPackage(url, name, include, newId)) {
                    result.success = true;
                    result.message = "Installed package: " + newId;
                } else {
                    result.message = packageManager.getLastError();
                }
                return result;
            });
        }

        ImGui::EndDisabled();
        ImGui::Separator();
        ImGui::TextDisabled("Installed packages");
        if (packageTask.active) {
            ImGui::TextDisabled("Working...");
        } else {
            const auto& registry = packageManager.getRegistry();
            const auto& installedIds = packageManager.getInstalled();
            if (installedIds.empty()) {
                ImGui::TextDisabled("None installed");
            } else {
                for (const auto& id : installedIds) {
                    const PackageInfo* pkg = nullptr;
                    for (const auto& p : registry) {
                        if (p.id == id) { pkg = &p; break; }
                    }
                    if (!pkg) continue;

                    ImGui::PushID(pkg->id.c_str());
                    ImGui::Separator();
                    ImGui::Text("%s", pkg->name.c_str());
                    ImGui::TextDisabled("%s", pkg->description.c_str());
                    if (!pkg->external) {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.4f, 0.7f, 0.95f, 1.0f), "[bundled]");
                        ImGui::PopID();
                        continue;
                    }

                    ImGui::TextDisabled("Path: %s", pkg->localPath.string().c_str());
                    ImGui::TextDisabled("Git: %s", pkg->gitUrl.c_str());

                    if (ImGui::Button("Check updates")) {
                        std::string id = pkg->id;
                        startPackageTask("Checking package status...", [this, id]() {
                            PackageTaskResult result;
                            std::string status;
                            if (packageManager.checkGitStatus(id, status)) {
                                result.success = true;
                                result.message = status;
                            } else {
                                result.message = packageManager.getLastError();
                            }
                            return result;
                        });
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Update")) {
                        std::string id = pkg->id;
                        std::string name = pkg->name;
                        startPackageTask("Updating package...", [this, id, name]() {
                            PackageTaskResult result;
                            std::string log;
                            if (packageManager.updateGitPackage(id, log)) {
                                result.success = true;
                                result.message = "Updated " + name + "\n" + log;
                            } else {
                                result.message = packageManager.getLastError();
                            }
                            return result;
                        });
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Uninstall")) {
                        std::string id = pkg->id;
                        std::string name = pkg->name;
                        startPackageTask("Removing package...", [this, id, name]() {
                            PackageTaskResult result;
                            if (packageManager.remove(id)) {
                                result.success = true;
                                result.message = "Removed " + name;
                            } else {
                                result.message = packageManager.getLastError();
                            }
                            return result;
                        });
                    }
                    ImGui::PopID();
                }
            }
        }
    } else if (selectedTab == 2) {
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
    } else if (selectedTab == 3) {
        bool editorSettingsChanged = false;
        bool buildSettingsChanged = false;

        if (ImGui::CollapsingHeader("Player / Viewport", ImGuiTreeNodeFlags_DefaultOpen)) {
            const char* resolutionOptions[] = { "Window", "1080p", "720p", "1440p", "Custom" };

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
            if (ImGui::SliderFloat("Preview Zoom", &zoomPercent, 20.0f, 400.0f, "%.0f%%")) {
                gameViewportZoom = std::clamp(zoomPercent / 100.0f, 0.2f, 4.0f);
                editorSettingsChanged = true;
            }
            ImGui::EndDisabled();

            if (ImGui::Checkbox("Show Game Profiler", &showGameProfiler)) editorSettingsChanged = true;
            if (ImGui::Checkbox("Canvas Guides", &showCanvasOverlay)) editorSettingsChanged = true;
            if (ImGui::Checkbox("UI World Grid", &showUIWorldGrid)) editorSettingsChanged = true;
        }

        if (ImGui::CollapsingHeader("Renderer", ImGuiTreeNodeFlags_DefaultOpen)) {
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
            if (ImGui::Checkbox("Collision Wireframe", &collisionWireframe)) editorSettingsChanged = true;
            if (ImGui::Checkbox("FPS Cap", &fpsCapEnabled)) editorSettingsChanged = true;
            ImGui::BeginDisabled(!fpsCapEnabled);
            if (ImGui::DragFloat("FPS Target", &fpsCap, 1.0f, 1.0f, 500.0f, "%.0f")) {
                fpsCap = std::max(1.0f, fpsCap);
                editorSettingsChanged = true;
            }
            ImGui::EndDisabled();
        }

        if (editorSettingsChanged) {
            saveEditorUserSettings();
        }
        if (buildSettingsChanged) {
            saveBuildSettings();
        }
    } else if (selectedTab == 4) {
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
    } else if (selectedTab == 5) {
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

            const char* cppStd[] = {"c++17", "c++20", "c++23", "gnu++20"};
            int cppIdx = 1;
            for (int i = 0; i < IM_ARRAYSIZE(cppStd); ++i) {
                if (ui.config.cppStandard == cppStd[i]) {
                    cppIdx = i;
                    break;
                }
            }
            if (ImGui::Combo("C++ Standard", &cppIdx, cppStd, IM_ARRAYSIZE(cppStd))) {
                ui.config.cppStandard = cppStd[cppIdx];
                ui.dirty = true;
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
                    autoCompileQueue.clear();
                    autoCompileQueued.clear();
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
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);
}
#pragma endregion
