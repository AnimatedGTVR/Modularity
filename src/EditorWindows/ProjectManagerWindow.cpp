#include "Engine.h"
#include "ModelLoader.h"
#include "ProjectVersionControl.h"
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
#include <source_location>
#include <unordered_set>
#include <optional>
#include <future>
#include <memory>
#include <chrono>
#include <future>

#ifdef _WIN32
#include <shlobj.h>
#endif

#ifdef __ANDROID__
#include "AndroidRuntime/AndroidRuntime.h"
#endif

namespace {
struct ProjectGitResult {
    bool success = false;
    std::string output;
};

struct ProjectGitUiState {
    fs::path projectPath;
    char remoteUrl[512] = "";
    char commitMessage[256] = "Update project";
    bool privateRepository = true;
    int provider = 0;
    bool busy = false;
    std::future<ProjectGitResult> future;
    std::string action;
    std::string status = "Press Refresh to inspect this project.";
};

std::string QuoteProjectGitArgument(const std::string& value) {
#ifdef _WIN32
    std::string quoted = "\"";
    for (char c : value) {
        if (c == '"') quoted += "\\\"";
        else quoted += c;
    }
    return quoted + "\"";
#else
    std::string quoted = "'";
    for (char c : value) {
        if (c == '\'') quoted += "'\"'\"'";
        else quoted += c;
    }
    return quoted + "'";
#endif
}

ProjectGitResult RunProjectGitCommand(const fs::path& projectPath,
                                      const std::string& arguments) {
    ProjectGitResult result;
    const std::string command =
        "git -C " + QuoteProjectGitArgument(projectPath.string()) + " " +
        arguments + " 2>&1";
#ifdef _WIN32
    FILE* pipe = _popen(command.c_str(), "r");
#else
    FILE* pipe = popen(command.c_str(), "r");
#endif
    if (!pipe) {
        result.output = "Unable to start Git. Make sure Git is installed.";
        return result;
    }
    char buffer[1024];
    while (std::fgets(buffer, sizeof(buffer), pipe)) result.output += buffer;
#ifdef _WIN32
    const int exitCode = _pclose(pipe);
#else
    const int exitCode = pclose(pipe);
#endif
    result.success = exitCode == 0;
    if (result.output.empty()) result.output = result.success ? "Done." : "Git command failed.";
    return result;
}

ProjectGitResult RunProjectGitHubPublish(const fs::path& projectPath,
                                         const std::string& repositoryName,
                                         bool privateRepository) {
    ProjectGitResult result;
    const std::string command =
        "gh repo create " + QuoteProjectGitArgument(repositoryName) +
        " --source " + QuoteProjectGitArgument(projectPath.string()) +
        " --remote origin --push " +
        (privateRepository ? "--private" : "--public") + " 2>&1";
#ifdef _WIN32
    FILE* pipe = _popen(command.c_str(), "r");
#else
    FILE* pipe = popen(command.c_str(), "r");
#endif
    if (!pipe) {
        result.output = "Unable to start GitHub CLI. Install gh and sign in with: gh auth login";
        return result;
    }
    char buffer[1024];
    while (std::fgets(buffer, sizeof(buffer), pipe)) result.output += buffer;
#ifdef _WIN32
    const int exitCode = _pclose(pipe);
#else
    const int exitCode = pclose(pipe);
#endif
    result.success = exitCode == 0;
    if (result.output.empty()) result.output = result.success ? "Published to GitHub." : "GitHub publish failed.";
    return result;
}

ProjectGitResult RunProjectGitLabPublish(const fs::path& projectPath,
                                         const std::string& repositoryName,
                                         bool privateRepository) {
    ProjectGitResult result;
    const std::string command =
        "cd " + QuoteProjectGitArgument(projectPath.string()) + " && glab repo create " +
        QuoteProjectGitArgument(repositoryName) + " --remoteName origin " +
        (privateRepository ? "--private" : "--public") + " 2>&1";
#ifdef _WIN32
    FILE* pipe = _popen(command.c_str(), "r");
#else
    FILE* pipe = popen(command.c_str(), "r");
#endif
    if (!pipe) {
        result.output = "Unable to start GitLab CLI. Install glab and sign in with: glab auth login";
        return result;
    }
    char buffer[1024];
    while (std::fgets(buffer, sizeof(buffer), pipe)) result.output += buffer;
#ifdef _WIN32
    const int exitCode = _pclose(pipe);
#else
    const int exitCode = pclose(pipe);
#endif
    result.success = exitCode == 0;
    if (result.output.empty()) result.output = result.success ? "Published to GitLab." : "GitLab publish failed.";
    return result;
}

ProjectGitResult CheckProjectGitHubLogin() {
    ProjectGitResult result;
    const std::string command = "gh auth status --hostname github.com 2>&1";
#ifdef _WIN32
    FILE* pipe = _popen(command.c_str(), "r");
#else
    FILE* pipe = popen(command.c_str(), "r");
#endif
    if (!pipe) {
        result.output = "Unable to start GitHub CLI. Install gh first.";
        return result;
    }
    char buffer[1024];
    while (std::fgets(buffer, sizeof(buffer), pipe)) result.output += buffer;
#ifdef _WIN32
    const int exitCode = _pclose(pipe);
#else
    const int exitCode = pclose(pipe);
#endif
    result.success = exitCode == 0;
    if (result.output.empty()) {
        result.output = result.success ? "Signed in to GitHub." : "Not signed in to GitHub.";
    }
    return result;
}

bool LaunchProjectGitHubLogin() {
#ifdef _WIN32
    return std::system("start \"GitHub Login\" cmd /k \"gh auth login --hostname github.com --git-protocol https --web\"") == 0;
#elif defined(__APPLE__)
    return std::system(
        "osascript -e 'tell application \"Terminal\" to do script \"gh auth login --hostname github.com --git-protocol https --web\"'") == 0;
#else
    const char* loginCommand =
        "gh auth login --hostname github.com --git-protocol https --web; "
        "printf '\\nLogin finished. You can close this terminal.\\n'; exec sh";
    const std::string quotedLogin = QuoteProjectGitArgument(loginCommand);
    const std::string command =
        "if command -v x-terminal-emulator >/dev/null 2>&1; then "
        "x-terminal-emulator -e sh -c " + quotedLogin + " >/dev/null 2>&1 & "
        "elif command -v gnome-terminal >/dev/null 2>&1; then "
        "gnome-terminal -- sh -c " + quotedLogin + " >/dev/null 2>&1 & "
        "elif command -v konsole >/dev/null 2>&1; then "
        "konsole -e sh -c " + quotedLogin + " >/dev/null 2>&1 & "
        "else exit 1; fi";
    return std::system(command.c_str()) == 0;
#endif
}

ProjectGitResult CheckProjectGitLabLogin() {
    ProjectGitResult result;
    const std::string command = "glab auth status 2>&1";
#ifdef _WIN32
    FILE* pipe = _popen(command.c_str(), "r");
#else
    FILE* pipe = popen(command.c_str(), "r");
#endif
    if (!pipe) {
        result.output = "Unable to start GitLab CLI. Install glab first.";
        return result;
    }
    char buffer[1024];
    while (std::fgets(buffer, sizeof(buffer), pipe)) result.output += buffer;
#ifdef _WIN32
    const int exitCode = _pclose(pipe);
#else
    const int exitCode = pclose(pipe);
#endif
    result.success = exitCode == 0;
    if (result.output.empty()) result.output = result.success ? "Signed in to GitLab." : "Not signed in to GitLab.";
    return result;
}

bool LaunchProjectGitLabLogin() {
#ifdef _WIN32
    return std::system("start \"GitLab Login\" cmd /k \"glab auth login\"") == 0;
#elif defined(__APPLE__)
    return std::system(
        "osascript -e 'tell application \"Terminal\" to do script \"glab auth login\"'") == 0;
#else
    const char* loginCommand =
        "glab auth login; printf '\\nLogin finished. You can close this terminal.\\n'; exec sh";
    const std::string quotedLogin = QuoteProjectGitArgument(loginCommand);
    const std::string command =
        "if command -v x-terminal-emulator >/dev/null 2>&1; then "
        "x-terminal-emulator -e sh -c " + quotedLogin + " >/dev/null 2>&1 & "
        "elif command -v gnome-terminal >/dev/null 2>&1; then "
        "gnome-terminal -- sh -c " + quotedLogin + " >/dev/null 2>&1 & "
        "elif command -v konsole >/dev/null 2>&1; then "
        "konsole -e sh -c " + quotedLogin + " >/dev/null 2>&1 & "
        "else exit 1; fi";
    return std::system(command.c_str()) == 0;
#endif
}

} // namespace

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

enum class ProjectSettingsVisibilityMode {
    Simple = 0,
    Advanced = 1,
    Developer = 2
};

static const char* ProjectSettingsModeHelp(ProjectSettingsVisibilityMode mode) {
    switch (mode) {
        case ProjectSettingsVisibilityMode::Advanced:
            return "Advanced shows technical settings most engine users may need.";
        case ProjectSettingsVisibilityMode::Developer:
            return "Developer shows debug, internal, and experimental controls.";
        case ProjectSettingsVisibilityMode::Simple:
        default:
            return "Simple shows common safe settings and hides noisy options.";
    }
}

static bool IsSettingVisibleForMode(ProjectSettingsVisibilityMode current,
                                    ProjectSettingsVisibilityMode required) {
    return static_cast<int>(current) >= static_cast<int>(required);
}

static std::string ProjectSettingsLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

static bool ProjectSettingsMatchesSearch(const char* query,
                                         const char* section,
                                         const char* label,
                                         const char* tags = nullptr) {
    if (!query || !*query) {
        return true;
    }

    const std::string needle = ProjectSettingsLowerCopy(TrimCopy(query));
    if (needle.empty()) {
        return true;
    }

    std::string haystack;
    if (section) haystack += section;
    haystack += ' ';
    if (label) haystack += label;
    haystack += ' ';
    if (tags) haystack += tags;

    return ProjectSettingsLowerCopy(haystack).find(needle) != std::string::npos;
}

static bool IsValidProjectNameToken(const std::string& value) {
    if (TrimCopy(value).empty()) return false;
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == ' ' || c == '_' || c == '-') continue;
        return false;
    }
    return true;
}

static bool HasDuplicateProjectNameToken(const std::vector<std::string>& values,
                                         const std::string& candidate,
                                         size_t selfIndex) {
    const std::string lowered = ProjectSettingsLowerCopy(TrimCopy(candidate));
    if (lowered.empty()) return false;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i == selfIndex) continue;
        if (ProjectSettingsLowerCopy(TrimCopy(values[i])) == lowered) return true;
    }
    return false;
}

static void DrawHelpText(const char* text) {
    if (!text || !*text) {
        return;
    }
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
    ImGui::TextColored(ImVec4(0.52f, 0.59f, 0.67f, 1.0f), "%s", text);
    ImGui::PopTextWrapPos();
}

static void DrawHelpTooltip(const char* text) {
    if (!text || !*text) {
        return;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// Two-column flow for the Project Settings body. Between Begin/End the section
// headers drawn by DrawSettingSection are distributed across two table columns
// when the panel is wide enough, keeping whole sections together per column.
// Section heights measured on the previous frame pick the most balanced split;
// until a tab has been measured once it renders as a single column.
struct ProjectSettingsColumnFlowState {
    bool tableOpen = false;
    bool twoColumns = false;
    bool inSecondColumn = false;
    int sectionIndex = 0;
    int splitIndex = 0;
    float sectionStartY = 0.0f;
    std::vector<float> frameHeights;
    std::vector<float>* storedHeights = nullptr;
};
static ProjectSettingsColumnFlowState g_projectSettingsColumnFlow;

static constexpr float kProjectSettingsTwoColumnMinWidth = 900.0f;

static int PickBalancedColumnSplit(const std::vector<float>& heights) {
    const int count = static_cast<int>(heights.size());
    float total = 0.0f;
    for (float h : heights) total += h;
    int bestSplit = (count + 1) / 2;
    float bestTallest = FLT_MAX;
    float leftSum = 0.0f;
    for (int split = 1; split < count; ++split) {
        leftSum += heights[split - 1];
        const float tallest = std::max(leftSum, total - leftSum);
        if (tallest < bestTallest) {
            bestTallest = tallest;
            bestSplit = split;
        }
    }
    return bestSplit;
}

static void BeginProjectSettingsColumns(std::vector<float>& sectionHeights) {
    ProjectSettingsColumnFlowState& flow = g_projectSettingsColumnFlow;
    flow = ProjectSettingsColumnFlowState{};
    flow.twoColumns = sectionHeights.size() >= 2 &&
                      ImGui::GetContentRegionAvail().x >= kProjectSettingsTwoColumnMinWidth;
    if (flow.twoColumns) {
        flow.splitIndex = PickBalancedColumnSplit(sectionHeights);
    }
    // Same table id for 1 and 2 columns so section open/collapse state survives
    // resizing across the two-column threshold.
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8.0f, 2.0f));
    flow.tableOpen = ImGui::BeginTable("##ProjectSettingsColumns", flow.twoColumns ? 2 : 1,
                                       ImGuiTableFlags_SizingStretchSame |
                                       ImGuiTableFlags_NoPadOuterX |
                                       ImGuiTableFlags_NoSavedSettings);
    ImGui::PopStyleVar();
    if (!flow.tableOpen) {
        flow = ProjectSettingsColumnFlowState{};
        return;
    }
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    flow.storedHeights = &sectionHeights;
    flow.frameHeights.swap(sectionHeights);
    flow.frameHeights.clear();
}

// Called by DrawSettingSection just before a header is drawn: closes out the
// previous section's height measurement and jumps to the second column at the
// balanced split point. Inert outside Begin/EndProjectSettingsColumns.
static void ProjectSettingsColumnsOnSectionHeader() {
    ProjectSettingsColumnFlowState& flow = g_projectSettingsColumnFlow;
    if (!flow.storedHeights) {
        return;
    }
    if (flow.sectionIndex > 0) {
        flow.frameHeights.push_back(std::max(0.0f, ImGui::GetCursorPosY() - flow.sectionStartY));
    }
    if (flow.twoColumns && !flow.inSecondColumn && flow.sectionIndex >= flow.splitIndex) {
        ImGui::TableNextColumn();
        flow.inSecondColumn = true;
    }
    flow.sectionStartY = ImGui::GetCursorPosY();
    ++flow.sectionIndex;
}

static void EndProjectSettingsColumns() {
    ProjectSettingsColumnFlowState& flow = g_projectSettingsColumnFlow;
    if (!flow.storedHeights) {
        flow = ProjectSettingsColumnFlowState{};
        return;
    }
    if (flow.sectionIndex > 0) {
        flow.frameHeights.push_back(std::max(0.0f, ImGui::GetCursorPosY() - flow.sectionStartY));
    }
    ImGui::EndTable();
    *flow.storedHeights = std::move(flow.frameHeights);
    flow = ProjectSettingsColumnFlowState{};
}

static bool DrawSettingSection(const char* icon,
                               const char* title,
                               const char* helper,
                               bool defaultOpen = true) {
    ProjectSettingsColumnsOnSectionHeader();
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.13f, 0.17f, 0.24f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.18f, 0.23f, 0.32f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.20f, 0.28f, 0.40f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 3.0f));

    std::string label;
    if (icon && *icon) {
        label += icon;
        label += "  ";
    }
    label += title ? title : "";

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
    if (defaultOpen) {
        flags |= ImGuiTreeNodeFlags_DefaultOpen;
    }
    const bool open = ImGui::CollapsingHeader(label.c_str(), flags);
    if (helper && *helper && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextUnformatted(helper);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    return open;
}

// image-icon header variant: texture in front of the title, bracket-letter fallback while
// the texture isn't loadable. the "###" suffix keeps the ID (and open state) stable either way.
static bool DrawSettingSection(ImTextureID iconTexture, bool iconFlipY,
                               const char* fallbackIcon,
                               const char* title,
                               const char* helper,
                               bool defaultOpen = true) {
    ProjectSettingsColumnsOnSectionHeader();
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.13f, 0.17f, 0.24f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.18f, 0.23f, 0.32f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.20f, 0.28f, 0.40f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 3.0f));

    const bool hasImage = iconTexture != static_cast<ImTextureID>(0);
    const float iconSize = ImGui::GetFontSize() + 2.0f;
    std::string label;
    if (hasImage) {
        // Reserve blank space in the label; the image is drawn over it after
        // the header exists so it tracks style/scale automatically.
        const float spaceWidth = std::max(1.0f, ImGui::CalcTextSize(" ").x);
        label.assign(static_cast<size_t>(std::ceil((iconSize + 6.0f) / spaceWidth)), ' ');
    } else if (fallbackIcon && *fallbackIcon) {
        label += fallbackIcon;
        label += "  ";
    }
    label += title ? title : "";
    label += "###";
    label += title ? title : "";

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
    if (defaultOpen) {
        flags |= ImGuiTreeNodeFlags_DefaultOpen;
    }
    const bool open = ImGui::CollapsingHeader(label.c_str(), flags);
    if (hasImage) {
        const ImVec2 rectMin = ImGui::GetItemRectMin();
        const ImVec2 rectMax = ImGui::GetItemRectMax();
        // Framed tree nodes place their text at fontSize + framePadding.x * 3
        // (see ImGui::TreeNodeBehavior); we pushed FramePadding.x = 6.
        const float textStartX = rectMin.x + ImGui::GetFontSize() + 6.0f * 3.0f;
        const float iconY = rectMin.y + ((rectMax.y - rectMin.y) - iconSize) * 0.5f;
        const ImVec2 uvMin = iconFlipY ? ImVec2(0.0f, 1.0f) : ImVec2(0.0f, 0.0f);
        const ImVec2 uvMax = iconFlipY ? ImVec2(1.0f, 0.0f) : ImVec2(1.0f, 1.0f);
        ImGui::GetWindowDrawList()->AddImage(iconTexture,
                                             ImVec2(textStartX, iconY),
                                             ImVec2(textStartX + iconSize, iconY + iconSize),
                                             uvMin, uvMax);
    }
    if (helper && *helper && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextUnformatted(helper);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    return open;
}

static bool DrawSettingRow(const char* label,
                           const char* helper,
                           const std::function<bool()>& drawControl,
                           std::source_location idLocation = std::source_location::current()) {
    bool changed = false;
    ImGui::PushID(idLocation.file_name());
    ImGui::PushID(static_cast<int>(idLocation.line()));
    ImGui::PushID(static_cast<int>(idLocation.column()));
    ImGui::PushID(label ? label : "");
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0.0f, 1.0f));
    if (ImGui::BeginTable("##SettingRow", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 0.34f);
        ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthStretch, 0.66f);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextColored(ImVec4(0.86f, 0.90f, 0.96f, 1.0f), "%s", label ? label : "");
        DrawHelpTooltip(helper);
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1);
        changed = drawControl ? drawControl() : false;
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
    ImGui::PopID();
    ImGui::PopID();
    ImGui::PopID();
    ImGui::PopID();
    return changed;
}

/*static bool DrawRecommendedResetButton(const std::function<void()>& resetAction) {
    if (ImGui::Button("Reset to Recommended")) {
        if (resetAction) {
            resetAction();
        }
        return true;
    }
    return false;
}*/

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
    std::string description;
    std::string presetId;
    fs::path projectRoot;
    fs::path projectFile;
    fs::path previewImage;
    ProjectPipeline pipeline = ProjectPipeline::Pipeline3D;
    Modularity::GraphicsBackend rendererBackend = Modularity::GraphicsBackend::OpenGL;
    bool isBlankPreset = false;
};

static std::vector<LauncherTemplateEntry> BuiltInProjectPresets() {
    std::vector<LauncherTemplateEntry> presets;
    auto add = [&](const char* id, const char* name, const char* description, ProjectPipeline pipeline) {
        LauncherTemplateEntry entry;
        entry.presetId = id;
        entry.displayName = name;
        entry.description = description;
        entry.pipeline = pipeline;
        entry.rendererBackend = Modularity::GraphicsBackend::OpenGL;
        entry.isBlankPreset = true;
        presets.push_back(std::move(entry));
    };
    add("empty", "Empty", "Clean project folders with no starter scene content.", ProjectPipeline::Pipeline3D);
    add("2d-game", "2D Game", "2D pipeline setup with sprite folders and a starter controller script.", ProjectPipeline::Pipeline2D);
    add("tool-app", "Tool/App", "UI-focused app setup with runtime/editor script folders and UI assets.", ProjectPipeline::Pipeline2D);
    add("runtime-only", "Runtime Only", "Minimal runtime-oriented project with no starter editor script content.", ProjectPipeline::Pipeline3D);
    return presets;
}

struct ProjectFileMetadata {
    std::string name;
    ProjectPipeline pipeline = ProjectPipeline::Pipeline3D;
    Modularity::GraphicsBackend rendererBackend = Modularity::GraphicsBackend::OpenGL;
    bool found = false;
};

struct LauncherRecentProjectDisplayEntry {
    RecentProject recent;
    fs::path projectRoot;
    ProjectFileMetadata metadata;
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

static ProjectFileMetadata ReadProjectFileMetadata(const fs::path& projectFile) {
    ProjectFileMetadata metadata;
    std::ifstream file(projectFile);
    if (!file.is_open()) {
        return metadata;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.rfind("name=", 0) == 0) {
            metadata.name = TrimCopy(line.substr(5));
        } else if (line.rfind("pipeline=", 0) == 0) {
            metadata.pipeline = ParseProjectPipeline(TrimCopy(line.substr(9)));
            metadata.found = true;
        } else if (line.rfind("renderer=", 0) == 0) {
            metadata.rendererBackend = Modularity::GraphicsBackendFromString(TrimCopy(line.substr(9)));
            metadata.found = true;
        }
    }

    metadata.found = metadata.found || !metadata.name.empty();
    return metadata;
}

static std::string BuildLauncherRecentProjectsFingerprint(const ProjectManager& manager) {
    std::ostringstream oss;
    oss << manager.recentProjects.size() << "|";
    for (const auto& rp : manager.recentProjects) {
        const fs::path projectFile = ResolveRecentProjectRoot(rp.path) / "project.modu";
        std::error_code ec;
        // cast to int64: NDK/arm64 file_time_type is __int128 which can't operator<<.
        // it's just a cache-key timestamp anyway.
        const long long writeTime = static_cast<long long>(
            fs::exists(projectFile, ec) && !ec
                ? fs::last_write_time(projectFile, ec).time_since_epoch().count()
                : 0);
        oss << rp.name << "|" << rp.path << "|" << rp.lastOpened << "|" << writeTime << ";";
    }
    return oss.str();
}

static const std::vector<LauncherRecentProjectDisplayEntry>& GetLauncherRecentProjectDisplayEntries(const ProjectManager& manager,
                                                                                                    bool forceRefresh = false) {
    static std::string cachedFingerprint;
    static std::vector<LauncherRecentProjectDisplayEntry> cachedEntries;

    const std::string fingerprint = BuildLauncherRecentProjectsFingerprint(manager);
    if (!forceRefresh && fingerprint == cachedFingerprint) {
        return cachedEntries;
    }

    cachedFingerprint = fingerprint;
    cachedEntries.clear();
    cachedEntries.reserve(manager.recentProjects.size());

    for (const RecentProject& rp : manager.recentProjects) {
        LauncherRecentProjectDisplayEntry entry;
        entry.recent = rp;
        entry.projectRoot = ResolveRecentProjectRoot(rp.path);
        const fs::path projectFile = entry.projectRoot / "project.modu";
        if (fs::exists(projectFile)) {
            entry.metadata = ReadProjectFileMetadata(projectFile);
        }
        cachedEntries.push_back(std::move(entry));
    }

    return cachedEntries;
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
        const ProjectFileMetadata metadata = ReadProjectFileMetadata(projectFile);
        t.pipeline = metadata.pipeline;
        t.rendererBackend = metadata.rendererBackend;
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
- Engine Handbook
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

void Engine::renderWindowsDisclaimerPopup() {
#if defined(_WIN32) && !defined(MODULARITY_PLAYER)
    if (projectManager.windowsDisclaimerAcknowledgedV68) {
        windowsDisclaimerPopupOpened = false;
        return;
    }
    if (requiresTermsOfServiceAcceptance()) {
        return;
    }
    if (showLauncher && !launcherIntroFinished) {
        return;
    }

    constexpr const char* popupTitle = "Windows Disclaimer!";
    if (!windowsDisclaimerPopupOpened) {
        ImGui::OpenPopup(popupTitle);
        windowsDisclaimerPopupOpened = true;
    }

    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    const float popupWidth = ImClamp(displaySize.x * 0.46f, 440.0f, 620.0f);
    ImGui::SetNextWindowPos(ImVec2(displaySize.x * 0.5f, displaySize.y * 0.5f),
                            ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(popupWidth, 0.0f), ImGuiCond_Appearing);

    const ImGuiWindowFlags popupFlags =
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::BeginPopupModal(popupTitle, nullptr, popupFlags)) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + popupWidth - ImGui::GetStyle().WindowPadding.x * 2.0f);
        ImGui::TextWrapped("Hey!");
        ImGui::Spacing();
        ImGui::TextWrapped("Modularity on Windows is currently not fully stable due to Windows having its own quirks and slower filesystem/toolchain behavior compared to Linux builds.");
        ImGui::Spacing();
        ImGui::TextWrapped("Because of this, you may experience:");
        ImGui::BulletText("Slower compilation times");
        ImGui::BulletText("Longer project loading times");
        ImGui::BulletText("Occasional editor instability");
        ImGui::Spacing();
        ImGui::TextWrapped("Linux builds are currently the primary recommended experience for development.");
        ImGui::Spacing();
        ImGui::TextWrapped("Thank you for your patience while Windows support continues improving!");
        ImGui::PopTextWrapPos();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        constexpr float buttonWidth = 120.0f;
        const float available = ImGui::GetContentRegionAvail().x;
        if (available > buttonWidth) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (available - buttonWidth) * 0.5f);
        }
        if (ImGui::Button("Got it!", ImVec2(buttonWidth, 0.0f))) {
            projectManager.windowsDisclaimerAcknowledgedV68 = true;
            projectManager.saveLauncherSettings();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
#else
    windowsDisclaimerPopupOpened = false;
#endif
}

void Engine::renderAndroidStorageAccessPopup() {
#if defined(__ANDROID__) && !defined(MODULARITY_PLAYER)
    const bool hasAccess = Modularity::AndroidRuntime::HasAllFilesAccess();

    // once access lands, move the *default location pref* to public Documents so new projects
    // match desktop. only touches prefs still pointing at private storage (custom paths stay),
    // and never moves existing project files.
    if (hasAccess) {
        const std::string docs = Modularity::AndroidRuntime::GetExternalDocumentsPath();
        const char* dataPath = Modularity::AndroidRuntime::GetInternalDataPath();
        const bool defaultIsPrivate = dataPath && dataPath[0] != '\0' &&
            std::strncmp(projectManager.defaultProjectLocation, dataPath, std::strlen(dataPath)) == 0;
        if (!docs.empty() && defaultIsPrivate) {
            const std::string docsProjects = (fs::path(docs) / "ModularityProjects").string();
            std::error_code ec;
            fs::create_directories(docsProjects, ec);
            if (!ec) {
                std::snprintf(projectManager.defaultProjectLocation,
                              sizeof(projectManager.defaultProjectLocation),
                              "%s", docsProjects.c_str());
                std::snprintf(projectManager.newProjectLocation,
                              sizeof(projectManager.newProjectLocation),
                              "%s", projectManager.defaultProjectLocation);
                projectManager.saveLauncherSettings();
            }
        }
    }

    // Don't nag once it's granted or the user has waved it off, and stay out of
    // the way of the terms screen / launcher intro just like the other popups.
    if (hasAccess || projectManager.androidStoragePromptDismissedV1) {
        androidStorageAccessPopupOpened = false;
        return;
    }
    if (requiresTermsOfServiceAcceptance()) {
        return;
    }
    if (showLauncher && !launcherIntroFinished) {
        return;
    }

    constexpr const char* popupTitle = "Allow File Access";
    if (!androidStorageAccessPopupOpened) {
        ImGui::OpenPopup(popupTitle);
        androidStorageAccessPopupOpened = true;
    }

    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    const float popupWidth = ImClamp(displaySize.x * 0.46f, 440.0f, 620.0f);
    ImGui::SetNextWindowPos(ImVec2(displaySize.x * 0.5f, displaySize.y * 0.5f),
                            ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(popupWidth, 0.0f), ImGuiCond_Appearing);

    const ImGuiWindowFlags popupFlags =
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::BeginPopupModal(popupTitle, nullptr, popupFlags)) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + popupWidth - ImGui::GetStyle().WindowPadding.x * 2.0f);
        ImGui::TextWrapped("Hey!");
        ImGui::Spacing();
        ImGui::TextWrapped("Modularity would like access to your device storage so it "
                           "can keep your projects in Documents/ModularityProjects, just "
                           "like on desktop.");
        ImGui::Spacing();
        ImGui::TextWrapped("Without it, projects are stored in private app storage that "
                           "other apps and your file manager can't reach, and they're "
                           "wiped if you uninstall Modularity.");
        ImGui::Spacing();
        ImGui::TextWrapped("You can change this anytime under Settings -> File Access.");
        ImGui::PopTextWrapPos();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        const float buttonWidth = 150.0f * uiDpiScale;
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float groupWidth = buttonWidth * 2.0f + spacing;
        const float available = ImGui::GetContentRegionAvail().x;
        if (available > groupWidth) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (available - groupWidth) * 0.5f);
        }
        if (ImGui::Button("Grant Access", ImVec2(buttonWidth, 0.0f))) {
            std::string reqErr;
            Modularity::AndroidRuntime::RequestAllFilesAccess(reqErr);
            // leave the dismiss flag unset so backing out = gentle reminder next launch.
            // granting flips hasAccess and the popup won't reopen.
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Not Now", ImVec2(buttonWidth, 0.0f))) {
            projectManager.androidStoragePromptDismissedV1 = true;
            projectManager.saveLauncherSettings();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
#else
    androidStorageAccessPopupOpened = false;
#endif
}

#pragma region Launcher
void Engine::renderLauncher() {
    ImGuiIO& io = ImGui::GetIO();
    ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    ImVec2 displaySize = mainViewport ? mainViewport->Size : io.DisplaySize;
    ImVec2 displayPos = mainViewport ? mainViewport->Pos : ImVec2(0.0f, 0.0f);
    const double now = glfwGetTime();
    if (!launcherIntroStarted) {
        launcherIntroStarted = true;
        launcherIntroStartTime = now;
    }
    if (!launcherIntroSoundPlayed) {
        // One-shot so that any other UI sound (like the card click) can layer on top
        // instead of cutting the intro chime off mid-play.
        playEditorFeedbackOneShot("Resources/Sounds/ModuIntro.mp3", 0.85f, EditorFeedbackSoundCategory::Boot);
        launcherIntroSoundPlayed = true;
    }

    const LauncherIntroTimings introTimings{};
    LauncherIntroState introState = EvaluateLauncherIntro(now, launcherIntroStartTime, launcherIntroFinished, introTimings);
    if (!launcherIntroFinished && introState.finished) {
        launcherIntroFinished = true;
        introState = EvaluateLauncherIntro(now, launcherIntroStartTime, true, introTimings);
    }

    const float transitionDuration = 0.72f;
    float transitionT = 0.0f;
    if (launcherTransitionActive) {
        transitionT = ImClamp(static_cast<float>((now - launcherTransitionStartTime) / transitionDuration), 0.0f, 1.0f);
    }
    // Smooth S-curve ease so the zoom feels like it accelerates and decelerates.
    const float transitionEase = EaseInOutCubic(transitionT);

    // hold the loading-screen expansion for the whole project/scene load, not just the 0.42s
    // zoom, or the cards pop back in while assets are still loading. Ugly.
    const bool isLoadingActive = projectLoadInProgress || sceneLoadInProgress;
    float loadingScreenT = launcherTransitionActive ? transitionEase : 0.0f;
    if (!launcherTransitionActive && isLoadingActive && launcherTransitionStartTime > 0.0) {
        loadingScreenT = 1.0f;
    }
    // launcher content fades at the same pace the rect grows: cards stay visible under it early,
    // gone by the time it's fullscreen. fading faster makes them vanish before the rect can
    // hide them, which reads as an ugly snap. Don't.
    const float transitionAlpha = ImClamp(1.0f - loadingScreenT * 1.1f, 0.0f, 1.0f);
    const float menuBuildT = launcherIntroFinished ? 1.0f : EaseOutCubic(introState.contentRevealT);
    const float introMenuScale = launcherIntroFinished ? 1.0f : ImLerp(1.04f, 1.0f, menuBuildT);
    // fold in the global DPI scale so the launcher's hand-sized chrome scales on
    // high-DPI / Android like the rest of the editor.
    const float uiScale = (1.0f + 0.04f * transitionEase) * introMenuScale * uiDpiScale;
    const float contentAlpha = launcherIntroFinished ? 1.0f : ImClamp(0.10f + introState.contentRevealT * 0.90f, 0.0f, 1.0f);
    // Static header logo+text only become visible at the end of the drift, which avoids
    // double-rendering with the intro overlay that lands at the same spot.
    const float headerStaticAlpha = launcherIntroFinished ? 1.0f : ImClamp((introState.driftT - 0.88f) / 0.12f, 0.0f, 1.0f);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 9.0f * uiScale);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.0f * uiScale);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

    if (mainViewport) {
        ImGui::SetNextWindowViewport(mainViewport->ID);
    }
    ImGui::SetNextWindowPos(displayPos);
    ImGui::SetNextWindowSize(displaySize);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize   |
        ImGuiWindowFlags_NoMove     |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoDocking  |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    if (ImGui::Begin("Launcher", nullptr, flags)) {
        static int launcherSection = 0; // 0 = Projects, 1 = Packages, 2 = Settings
        static int launcherSectionPrevious = 0;
        static double launcherSectionSwitchTime = 0.0;
        static char launcherSearch[160] = "";
        static float projectsPanelVisual = 0.0f;
        static float tabIndicatorVisualX = -1.0f;
        static float tabIndicatorVisualW = 0.0f;

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
            projectManager.newProjectImportLastPackages = false;
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
            // One-shot so the click sits on top of the intro/wind sounds instead of
            // stopping them via the single preview slot.
            playEditorFeedbackOneShot("Resources/Sounds/Selection.mp3", 0.95f, EditorFeedbackSoundCategory::Click);
            // capture the timer FIRST and resolve the preview path BEFORE OpenProjectPath;
            // anything after startTime eats into the visible transition window.
            launcherTransitionActive = true;
            launcherTransitionPendingHide = false;
            launcherTransitionFocus = focus;
            launcherTransitionProjectName = rp.name;
            fs::path previewPath = getProjectPreviewPath(rp.path);
            if (!previewPath.empty() && fs::exists(previewPath)) {
                launcherLoadingPreviewPath = previewPath.string();
            }
            launcherTransitionStartTime = glfwGetTime();
            OpenProjectPath(rp.path);
        };

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 windowPos = ImGui::GetWindowPos();
        ImVec2 windowSize = ImGui::GetWindowSize();

        // Backdrop: optional blurred preview + gradient
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

        const float bgAlpha = (previewImageId != static_cast<ImTextureID>(0)) ? 0.62f : 1.0f;
        const ImVec4 bgTopLeft     = ImVec4(0.118f, 0.130f, 0.165f, bgAlpha);
        const ImVec4 bgTopRight    = ImVec4(0.138f, 0.152f, 0.195f, bgAlpha);
        const ImVec4 bgBottomRight = ImVec4(0.092f, 0.102f, 0.135f, bgAlpha);
        const ImVec4 bgBottomLeft  = ImVec4(0.105f, 0.118f, 0.150f, bgAlpha);
        drawList->AddRectFilledMultiColor(
            windowPos,
            ImVec2(windowPos.x + windowSize.x, windowPos.y + windowSize.y),
            ImGui::GetColorU32(bgTopLeft),
            ImGui::GetColorU32(bgTopRight),
            ImGui::GetColorU32(bgBottomRight),
            ImGui::GetColorU32(bgBottomLeft));

        // (The transition zoom rect is drawn at the end of this Begin block so it sits
        //  ON TOP of the launcher content rather than being painted over by the cards.)

        // Header geometry
        const float headerHeight   = 62.0f * uiScale;
        const float headerPadX     = 30.0f * uiScale;
        const float headerLogoSize = 28.0f * uiScale;
        const float headerLogoGap  = 12.0f * uiScale;
        const float baseFontSize   = ImGui::GetFontSize();
        const float titleFontSize  = baseFontSize * 1.22f;
        const float tabFontSize    = baseFontSize * 1.00f;
        const float linkFontSize   = baseFontSize * 0.95f;

        const ImVec2 headerLogoPos(
            windowPos.x + headerPadX,
            windowPos.y + (headerHeight - headerLogoSize) * 0.5f);
        const ImVec2 headerTextPos(
            windowPos.x + headerPadX + headerLogoSize + headerLogoGap,
            windowPos.y + (headerHeight - titleFontSize) * 0.5f);

        // Resolve logo texture
        ImTextureID logoTexId = static_cast<ImTextureID>(0);
        int logoTexWidth = 0;
        int logoTexHeight = 0;
        {
            const fs::path logoPath = fs::path("Resources") / "Engine-Root" / "Modu-Logo.png";
            if (fs::exists(logoPath)) {
                if (usingVulkan() && vulkanRendererInitialized && vulkanRenderer) {
                    logoTexId = vulkanRenderer->getOrCreateUIImage(logoPath.string(), &logoTexWidth, &logoTexHeight);
                } else if (!usingVulkan()) {
                    if (Texture* logoTexture = renderer.getTexture(logoPath.string())) {
                        logoTexId = (ImTextureID)(intptr_t)logoTexture->GetID();
                        logoTexWidth = logoTexture->GetWidth();
                        logoTexHeight = logoTexture->GetHeight();
                    }
                }
            }
        }
        const ImVec2 logoUvMin = usingVulkan() ? ImVec2(0.0f, 0.0f) : ImVec2(0.0f, 1.0f);
        const ImVec2 logoUvMax = usingVulkan() ? ImVec2(1.0f, 1.0f) : ImVec2(1.0f, 0.0f);

        // Header background + bottom rule
        const ImVec2 headerMin(windowPos.x, windowPos.y);
        const ImVec2 headerMax(windowPos.x + windowSize.x, windowPos.y + headerHeight);
        drawList->AddRectFilled(headerMin, headerMax,
                                ImGui::GetColorU32(ImVec4(0.078f, 0.085f, 0.112f, 0.92f)));
        drawList->AddLine(ImVec2(headerMin.x, headerMax.y - 0.5f),
                          ImVec2(headerMax.x, headerMax.y - 0.5f),
                          ImGui::GetColorU32(ImVec4(0.18f, 0.21f, 0.27f, 1.0f)),
                          1.0f);

        // Static header brand (logo + "Modularity"). Fades in at end of intro so it
        // doesn't collide with the intro overlay text that lands on this exact spot.
        if (headerStaticAlpha > 0.001f) {
            if (logoTexId != static_cast<ImTextureID>(0)) {
                drawList->AddImage(logoTexId,
                                   headerLogoPos,
                                   ImVec2(headerLogoPos.x + headerLogoSize, headerLogoPos.y + headerLogoSize),
                                   logoUvMin, logoUvMax,
                                   ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, headerStaticAlpha)));
            }
            const ImU32 titleCol = ImGui::GetColorU32(ImVec4(0.95f, 0.97f, 1.0f, headerStaticAlpha));
            drawList->AddText(ImGui::GetFont(), titleFontSize, headerTextPos, titleCol, "Modularity");
        }

        // Top tabs
        struct TabDef { const char* label; int index; };
        const TabDef tabs[] = {
            {"Projects", 0},
            {"ModuPak", 1},
            {"Settings", 2},
            {"Source Control", 3}
        };
        const int tabCount = (int)(sizeof(tabs) / sizeof(tabs[0]));

        const float titleTextWidth = ImGui::GetFont()->CalcTextSizeA(titleFontSize, FLT_MAX, 0.0f, "Modularity").x;
        const float tabsStartX = headerTextPos.x + titleTextWidth + 44.0f * uiScale;
        const float tabHPad = 22.0f * uiScale;

        float tabWidths[8] = {0};
        float tabXs[8] = {0};
        {
            float currentX = tabsStartX;
            for (int i = 0; i < tabCount; ++i) {
                const float w = ImGui::GetFont()->CalcTextSizeA(tabFontSize, FLT_MAX, 0.0f, tabs[i].label).x + tabHPad * 2.0f;
                tabWidths[i] = w;
                tabXs[i] = currentX;
                currentX += w;
            }
        }

        // Right side link buttons
        struct LinkDef { const char* label; const char* url; };
        const LinkDef rightLinks[] = {
            {"Website", "https://moduengine.xyz"},
            {"Handbook",    "https://moduengine.xyz/docs"}
        };
        const int rightLinkCount = (int)(sizeof(rightLinks) / sizeof(rightLinks[0]));
        const float linkHPad = 14.0f * uiScale;
        const float linkSpacing = 4.0f * uiScale;
        const float exitW = ImGui::GetFont()->CalcTextSizeA(linkFontSize, FLT_MAX, 0.0f, "Exit").x + linkHPad * 2.0f;
        float linkWidths[4] = {0};
        float linksTotalW = exitW;
        for (int i = 0; i < rightLinkCount; ++i) {
            const float w = ImGui::GetFont()->CalcTextSizeA(linkFontSize, FLT_MAX, 0.0f, rightLinks[i].label).x + linkHPad * 2.0f;
            linkWidths[i] = w;
            linksTotalW += w + linkSpacing;
        }
        const float linksStartX = windowPos.x + windowSize.x - headerPadX - linksTotalW;

        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, contentAlpha * transitionAlpha);

        // Render tabs
        for (int i = 0; i < tabCount; ++i) {
            const float tx = tabXs[i];
            const float tw = tabWidths[i];
            const float th = headerHeight;
            const ImVec2 tabMin(tx, windowPos.y);
            const ImVec2 tabMax(tx + tw, windowPos.y + th);

            ImGui::SetCursorScreenPos(tabMin);
            ImGui::PushID(tabs[i].index);
            ImGui::InvisibleButton("##LauncherTab", ImVec2(tw, th));
            const bool hovered = ImGui::IsItemHovered();
            const bool pressed = ImGui::IsItemClicked();
            ImGui::PopID();
            if (pressed) setLauncherSection(tabs[i].index);

            const bool selected = launcherSection == tabs[i].index;
            ImU32 textCol;
            if (selected) {
                textCol = ImGui::GetColorU32(ImVec4(0.96f, 0.98f, 1.0f, 1.0f));
            } else if (hovered) {
                textCol = ImGui::GetColorU32(ImVec4(0.88f, 0.92f, 0.97f, 1.0f));
            } else {
                textCol = ImGui::GetColorU32(ImVec4(0.66f, 0.72f, 0.80f, 1.0f));
            }
            if (hovered && !selected) {
                drawList->AddRectFilled(ImVec2(tabMin.x + 6.0f * uiScale, tabMin.y + 12.0f * uiScale),
                                        ImVec2(tabMax.x - 6.0f * uiScale, tabMax.y - 12.0f * uiScale),
                                        ImGui::GetColorU32(ImVec4(0.13f, 0.16f, 0.22f, 0.65f)),
                                        7.0f * uiScale);
            }
            // Soft glow behind the selected tab. Gently breathes in and out.
            if (selected) {
                const float pulse = 0.5f + 0.5f * std::sin((float)ImGui::GetTime() * 2.0f);
                const float glowAlpha = ImLerp(0.10f, 0.22f, pulse);
                const ImVec2 glowMin(tabMin.x + 4.0f * uiScale, tabMin.y + 10.0f * uiScale);
                const ImVec2 glowMax(tabMax.x - 4.0f * uiScale, tabMax.y - 10.0f * uiScale);
                const float glowRound = 9.0f * uiScale;
                drawList->AddRectFilled(glowMin, glowMax,
                                        ImGui::GetColorU32(ImVec4(0.48f, 0.66f, 0.92f, glowAlpha * 0.40f)),
                                        glowRound);
                // Outer halo: two expanding faded rings for a soft bloom feel.
                for (int g = 0; g < 2; ++g) {
                    const float gp = (float)(g + 1) * 3.0f * uiScale;
                    const float ga = glowAlpha * (0.22f - 0.08f * (float)g);
                    if (ga <= 0.0f) break;
                    drawList->AddRect(ImVec2(glowMin.x - gp, glowMin.y - gp),
                                      ImVec2(glowMax.x + gp, glowMax.y + gp),
                                      ImGui::GetColorU32(ImVec4(0.55f, 0.74f, 0.98f, ga)),
                                      glowRound + gp, 0, 1.0f * uiScale);
                }
            }
            const float textW = ImGui::GetFont()->CalcTextSizeA(tabFontSize, FLT_MAX, 0.0f, tabs[i].label).x;
            const ImVec2 textPos(tx + (tw - textW) * 0.5f,
                                 windowPos.y + (th - tabFontSize) * 0.5f);
            drawList->AddText(ImGui::GetFont(), tabFontSize, textPos, textCol, tabs[i].label);
        }
        {
            const int activeIdx = launcherSection;
            const float targetX = tabXs[activeIdx] + 14.0f * uiScale;
            const float targetW = tabWidths[activeIdx] - 28.0f * uiScale;
            if (tabIndicatorVisualX < 0.0f) { tabIndicatorVisualX = targetX; tabIndicatorVisualW = targetW; }
            tabIndicatorVisualX = SmoothApproach(tabIndicatorVisualX, targetX, 16.0f, frameDt);
            tabIndicatorVisualW = SmoothApproach(tabIndicatorVisualW, targetW, 16.0f, frameDt);

            const float indH = 3.0f * uiScale;
            const float indY = windowPos.y + headerHeight - indH - 0.5f;
            const float indLeft  = tabIndicatorVisualX;
            const float indRight = tabIndicatorVisualX + tabIndicatorVisualW;
            const float indMid   = (indLeft + indRight) * 0.5f;

            // Dip the indicator alpha while it's mid-flight between tabs,
            const float motion = std::abs(targetX - tabIndicatorVisualX) +
                                 std::abs(targetW - tabIndicatorVisualW);
            const float motionT = ImClamp(motion / (48.0f * uiScale), 0.0f, 1.0f);
            const float breathe = 0.5f + 0.5f * std::sin((float)ImGui::GetTime() * 2.0f);
            const float restAlpha = ImLerp(0.55f, 0.85f, breathe);
            const float centerAlpha = ImLerp(restAlpha, 0.40f, motionT);

            const ImU32 cCenter = ImGui::GetColorU32(ImVec4(0.55f, 0.78f, 1.0f, centerAlpha));
            const ImU32 cEdge   = ImGui::GetColorU32(ImVec4(0.55f, 0.78f, 1.0f, 0.0f));
           
            drawList->AddRectFilledMultiColor(// Left
                ImVec2(indLeft, indY),
                ImVec2(indMid,  indY + indH),
                cEdge, cCenter, cCenter, cEdge);
            drawList->AddRectFilledMultiColor(// Right
                ImVec2(indMid,   indY),
                ImVec2(indRight, indY + indH),
                cCenter, cEdge, cEdge, cCenter);
        }

        // Right side link buttons (Website / Docs / Exit)
        auto drawLinkButton = [&](float x, float w, const char* label) -> bool {
            const float bh = 32.0f * uiScale;
            const ImVec2 bMin(x, windowPos.y + (headerHeight - bh) * 0.5f);
            const ImVec2 bMax(x + w, bMin.y + bh);
            ImGui::SetCursorScreenPos(bMin);
            ImGui::PushID(label);
            ImGui::InvisibleButton("##HeaderLink", ImVec2(w, bh));
            const bool hovered = ImGui::IsItemHovered();
            const bool pressed = ImGui::IsItemClicked();
            ImGui::PopID();
            if (hovered) {
                drawList->AddRectFilled(bMin, bMax,
                                        ImGui::GetColorU32(ImVec4(0.16f, 0.20f, 0.28f, 0.88f)),
                                        7.0f * uiScale);
                drawList->AddRect(bMin, bMax,
                                  ImGui::GetColorU32(ImVec4(0.28f, 0.40f, 0.55f, 1.0f)),
                                  7.0f * uiScale, 0, 1.0f);
            }
            const ImU32 col = ImGui::GetColorU32(hovered
                ? ImVec4(0.95f, 0.97f, 1.0f, 1.0f)
                : ImVec4(0.72f, 0.78f, 0.86f, 1.0f));
            const float tw = ImGui::GetFont()->CalcTextSizeA(linkFontSize, FLT_MAX, 0.0f, label).x;
            drawList->AddText(ImGui::GetFont(), linkFontSize,
                              ImVec2(x + (w - tw) * 0.5f, bMin.y + (bh - linkFontSize) * 0.5f),
                              col, label);
            return pressed;
        };

        {
            float lx = linksStartX;
            for (int i = 0; i < rightLinkCount; ++i) {
                if (drawLinkButton(lx, linkWidths[i], rightLinks[i].label)) {
                    #ifdef _WIN32
                    const std::string cmd = std::string("start ") + rightLinks[i].url;
                    system(cmd.c_str());
                    #else
                    const std::string cmd = std::string("xdg-open ") + rightLinks[i].url + " &";
                    system(cmd.c_str());
                    #endif
                }
                lx += linkWidths[i] + linkSpacing;
            }
            if (drawLinkButton(lx, exitW, "Exit")) {
                glfwSetWindowShouldClose(editorWindow, GLFW_TRUE);
            }
        }

        ImGui::PopStyleVar(); // header alpha

        // Content area (below header)
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, contentAlpha * transitionAlpha);
        ImGui::SetWindowFontScale(uiScale);
        if (!launcherIntroFinished) {
            ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
        }

        const float contentPadX = 64.0f * uiScale;
        const float contentPadY = 20.0f * uiScale;
        const ImVec2 sectionInnerPadding(40.0f * uiScale, 28.0f * uiScale);
        const float contentTopY = headerHeight + contentPadY;
        const float contentW    = windowSize.x - contentPadX * 2.0f;
        const float contentH    = windowSize.y - contentTopY - contentPadY;

        // Slight slide-in offset for the active tab content during intro and tab switches.
        const int sectionDirection = (launcherSection >= launcherSectionPrevious) ? 1 : -1;
        const float sectionOffsetY = (1.0f - sectionAnimEase) * 24.0f * uiScale * (float)sectionDirection;
        const float introContentOffset = launcherIntroFinished
            ? 0.0f
            : (1.0f - EaseOutCubic(introState.contentRevealT)) * 18.0f * uiScale;

        ImGui::SetCursorPos(ImVec2(contentPadX, contentTopY + sectionOffsetY + introContentOffset));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * sectionAnimEase);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, sectionInnerPadding);
        // Force transparent ChildBg for the section AND its subpanels so the rounded
        // inner shell does not render, so the cards float directly on the lighter gradient.
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::BeginChild("LauncherActiveSection",
                          ImVec2(contentW, contentH),
                          false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar(); // WindowPadding (only applies to the BeginChild above)

        auto toLower = [](std::string value) {
            std::transform(value.begin(), value.end(), value.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return value;
        };

        // Projects view (hybrid card list)
        auto renderProjectsView = [&]() {
            const auto& recentEntries = GetLauncherRecentProjectDisplayEntries(projectManager);
            const std::string filter = toLower(TrimCopy(launcherSearch));

            // -- Title block (left) + toolbar (right), vertically aligned --
            const float titleRowStartY = ImGui::GetCursorPosY();
            const float titleRowStartX = ImGui::GetCursorPosX();
            const float titleRowAvailW = ImGui::GetContentRegionAvail().x;

            ImGui::TextColored(ImVec4(0.95f, 0.97f, 1.0f, 1.0f), "Recent Projects");
            ImGui::TextColored(ImVec4(0.59f, 0.66f, 0.75f, 1.0f),
                               "%zu tracked project%s",
                               recentEntries.size(),
                               recentEntries.size() == 1 ? "" : "s");
            const float titleRowEndY = ImGui::GetCursorPosY();
            const float titleBlockH  = titleRowEndY - titleRowStartY - ImGui::GetStyle().ItemSpacing.y;

            // Toolbar metrics
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float primaryBtnW   = 142.0f * uiScale;
            const float secondaryBtnW = 96.0f * uiScale;
            const float btnH          = 34.0f * uiScale;
            const float searchW       = ImClamp(titleRowAvailW * 0.36f,
                                                220.0f * uiScale,
                                                380.0f * uiScale);
            const float controlsW = searchW + primaryBtnW + secondaryBtnW + spacing * 2.0f;

            const bool toolbarOnRight = titleRowAvailW > controlsW + 24.0f * uiScale;
            if (toolbarOnRight) {
                // Vertically center the toolbar against the two-line title block.
                const float toolbarY = titleRowStartY + (titleBlockH - btnH) * 0.5f;
                const float toolbarX = titleRowStartX + titleRowAvailW - controlsW;
                ImGui::SetCursorPos(ImVec2(toolbarX, toolbarY));
            } else {
                ImGui::Dummy(ImVec2(0.0f, 10.0f * uiScale));
            }

            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 9.0f * uiScale);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f * uiScale, 8.0f * uiScale));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.10f, 0.12f, 0.16f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.12f, 0.15f, 0.20f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.13f, 0.17f, 0.23f, 1.0f));
            ImGui::SetNextItemWidth(searchW);
            ImGui::InputTextWithHint("##ProjectSearch", "Search projects...", launcherSearch, sizeof(launcherSearch));
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(2);

            ImGui::SameLine();
            ImGui::BeginDisabled(launcherBusy);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.17f, 0.22f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.22f, 0.28f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.16f, 0.20f, 0.26f, 1.0f));
            if (ImGui::Button("Open", ImVec2(secondaryBtnW, btnH))) openProjectDialog();
            ImGui::PopStyleColor(3);
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.92f, 0.66f, 0.16f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.98f, 0.74f, 0.24f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.84f, 0.58f, 0.12f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.08f, 0.07f, 0.04f, 1.0f));
            if (ImGui::Button("New Project", ImVec2(primaryBtnW, btnH))) openNewProjectDialog();
            ImGui::PopStyleColor(4);
            ImGui::EndDisabled();
            const float afterToolbarY = ImGui::GetCursorPosY();
            ImGui::SetCursorPosY(std::max(afterToolbarY, titleRowEndY) + 14.0f * uiScale);

            // Card list
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::BeginChild("ProjectsCardList", ImVec2(0.0f, 0.0f), false);
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();

            const float listWidth = ImGui::GetContentRegionAvail().x;
            const float cardHeight = 92.0f * uiScale;
            const float cardGap    = 10.0f * uiScale;
            const float cardPadX   = 10.0f * uiScale;
            const float thumbW     = 116.0f * uiScale;

            bool hadVisible = false;
            int  visibleIdx = 0;
            bool removedProject = false;
            const float rowsBuildT = launcherIntroFinished ? 1.0f : EaseOutCubic(introState.contentRevealT);

            ImDrawList* listDL = ImGui::GetWindowDrawList();

            for (size_t i = 0; i < recentEntries.size(); ++i) {
                const LauncherRecentProjectDisplayEntry& entry = recentEntries[i];
                const std::string haystack = toLower(entry.recent.name + " " + entry.recent.path + " " +
                                                     Modularity::ToString(entry.metadata.rendererBackend) + " " +
                                                     ProjectPipelineLabel(entry.metadata.pipeline));
                if (!filter.empty() && haystack.find(filter) == std::string::npos) continue;

                hadVisible = true;
                const int rowIdx = visibleIdx++;
                const float rowDelay = std::min(0.72f, (float)rowIdx * 0.055f);
                const float rowInput = ImClamp((rowsBuildT - rowDelay) / std::max(0.0001f, 1.0f - rowDelay), 0.0f, 1.0f);
                const float rowReveal = EaseOutCubic(rowInput);
                const float rowSlideY = (1.0f - rowReveal) * 14.0f * uiScale;
                const float rowAlpha  = rowReveal;

                ImGui::PushID((int)i);
                const ImVec2 cursorPos = ImGui::GetCursorScreenPos();
                const ImVec2 cardBasePos(cursorPos.x, cursorPos.y + rowSlideY);
                const ImVec2 cardSize(listWidth, cardHeight);

                ImGui::SetCursorScreenPos(cardBasePos);
                ImGui::InvisibleButton("##ProjectCard", cardSize);
                const bool hovered    = ImGui::IsItemHovered();
                const bool clicked    = ImGui::IsItemClicked(ImGuiMouseButton_Left);
                const bool dblClicked = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
                bool shouldOpen   = clicked || dblClicked;
                bool shouldRemove = false;
                const ImVec2 rowMin = ImGui::GetItemRectMin();
                const ImVec2 rowMax = ImGui::GetItemRectMax();
                const ImVec2 rowCenter((rowMin.x + rowMax.x) * 0.5f, (rowMin.y + rowMax.y) * 0.5f);

                if (ImGui::BeginPopupContextItem("##CardContext")) {
                    if (ImGui::MenuItem("Open", nullptr, false, !launcherBusy)) shouldOpen = true;
                    if (ImGui::MenuItem("Remove from Recent")) shouldRemove = true;
                    ImGui::EndPopup();
                }

                auto modAlpha = [rowAlpha](ImVec4 c){ c.w *= rowAlpha; return c; };
                const ImVec4 baseBg   = ImVec4(0.108f, 0.122f, 0.158f, 0.95f);
                const ImVec4 hoverBg  = ImVec4(0.138f, 0.160f, 0.212f, 0.97f);
                const ImVec4 baseBrd  = ImVec4(0.20f, 0.24f, 0.32f, 0.92f);
                const ImVec4 hoverBrd = ImVec4(0.40f, 0.58f, 0.88f, 0.65f);
                const float radius = 11.0f * uiScale;

                listDL->AddRectFilled(rowMin, rowMax,
                                      ImGui::GetColorU32(modAlpha(hovered ? hoverBg : baseBg)),
                                      radius);
                listDL->AddRect(rowMin, rowMax,
                                ImGui::GetColorU32(modAlpha(hovered ? hoverBrd : baseBrd)),
                                radius, 0,
                                hovered ? 1.2f : 1.0f);
                if (hovered) {
                    const float cardPulse = 0.5f + 0.5f * std::sin((float)ImGui::GetTime() * 2.0f);
                    const float cardPulseA = ImLerp(0.55f, 0.95f, cardPulse);
                    // Soft glow halo around the card.
                    const float ga = 0.18f * cardPulseA;
                    listDL->AddRect(ImVec2(rowMin.x - 2.0f * uiScale, rowMin.y - 2.0f * uiScale),
                                    ImVec2(rowMax.x + 2.0f * uiScale, rowMax.y + 2.0f * uiScale),
                                    ImGui::GetColorU32(modAlpha(ImVec4(0.55f, 0.74f, 0.98f, ga))),
                                    radius + 2.0f * uiScale, 0, 1.0f * uiScale);
                    // Breathing left accent bar.
                    listDL->AddRectFilled(ImVec2(rowMin.x, rowMin.y + 12.0f * uiScale),
                                          ImVec2(rowMin.x + 3.0f * uiScale, rowMax.y - 12.0f * uiScale),
                                          ImGui::GetColorU32(modAlpha(ImVec4(0.55f, 0.78f, 1.0f, cardPulseA))),
                                          1.5f * uiScale);
                }

                // Thumbnail
                ImTextureID previewTexId = static_cast<ImTextureID>(0);
                int previewTexWidth = 0;
                int previewTexHeight = 0;
                const fs::path previewPath = getProjectPreviewPath(entry.recent.path);
                if (!previewPath.empty() && fs::exists(previewPath)) {
                    if (usingVulkan() && vulkanRendererInitialized && vulkanRenderer) {
                        previewTexId = vulkanRenderer->getOrCreateUIImage(previewPath.string(),
                                                                          &previewTexWidth, &previewTexHeight);
                    } else if (!usingVulkan()) {
                        if (Texture* previewTex = renderer.getTexture(previewPath.string())) {
                            previewTexId = (ImTextureID)(intptr_t)previewTex->GetID();
                            previewTexWidth = previewTex->GetWidth();
                            previewTexHeight = previewTex->GetHeight();
                        }
                    }
                }

                const float thumbPad = 10.0f * uiScale;
                const ImVec2 thumbMin(rowMin.x + cardPadX, rowMin.y + thumbPad);
                const ImVec2 thumbMax(thumbMin.x + thumbW, rowMax.y - thumbPad);
                if (previewTexId != static_cast<ImTextureID>(0)) {
                    DrawImageCover(listDL, previewTexId, thumbMin, thumbMax,
                                   previewTexWidth, previewTexHeight,
                                   ImGui::GetColorU32(modAlpha(ImVec4(1, 1, 1, 1))),
                                   7.0f * uiScale);
                } else {
                    listDL->AddRectFilled(thumbMin, thumbMax,
                                          ImGui::GetColorU32(modAlpha(ImVec4(0.06f, 0.075f, 0.105f, 1.0f))),
                                          7.0f * uiScale);
                    const char* nopv = "No Preview";
                    const ImVec2 ts = ImGui::CalcTextSize(nopv);
                    listDL->AddText(ImVec2(thumbMin.x + (thumbW - ts.x) * 0.5f,
                                           thumbMin.y + ((thumbMax.y - thumbMin.y) - ts.y) * 0.5f),
                                    ImGui::GetColorU32(modAlpha(ImVec4(0.55f, 0.62f, 0.72f, 1.0f))),
                                    nopv);
                }
                listDL->AddRect(thumbMin, thumbMax,
                                ImGui::GetColorU32(modAlpha(ImVec4(0.20f, 0.25f, 0.34f, 1.0f))),
                                7.0f * uiScale);

                const std::string displayName = entry.recent.name.empty()
                    ? (entry.metadata.name.empty() ? "(Unnamed Project)" : entry.metadata.name)
                    : entry.recent.name;
                const std::string locationLabel = entry.projectRoot.empty() ? entry.recent.path : entry.projectRoot.string();

                // Right action button (real ImGui so it's interactable above the card)
                const float openBtnW = 90.0f * uiScale;
                const float openBtnH = 34.0f * uiScale;
                const float rightPad = cardPadX;
                const float openBtnX = rowMax.x - rightPad - openBtnW;

                // Right metadata column
                const float metaColW = 210.0f * uiScale;
                const float metaColX = openBtnX - 16.0f * uiScale - metaColW;

                const float textX = thumbMax.x + 14.0f * uiScale;
                const float textMaxX = metaColX - 12.0f * uiScale;

                // Project name
                const float nameFontSize = baseFontSize * 1.12f;
                listDL->AddText(ImGui::GetFont(), nameFontSize,
                                ImVec2(textX, rowMin.y + 16.0f * uiScale),
                                ImGui::GetColorU32(modAlpha(ImVec4(0.95f, 0.97f, 1.0f, 1.0f))),
                                displayName.c_str(), nullptr,
                                std::max(0.0f, textMaxX - textX));

                // Path (clipped)
                listDL->PushClipRect(ImVec2(textX, rowMin.y + 32.0f * uiScale),
                                     ImVec2(textMaxX, rowMax.y - 12.0f * uiScale), true);
                listDL->AddText(ImGui::GetFont(), baseFontSize * 0.93f,
                                ImVec2(textX, rowMin.y + 40.0f * uiScale),
                                ImGui::GetColorU32(modAlpha(ImVec4(0.56f, 0.63f, 0.72f, 1.0f))),
                                locationLabel.c_str());
                listDL->PopClipRect();

                // Renderer + pipeline as a rounded badge
                std::string badgeText = std::string(Modularity::ToString(entry.metadata.rendererBackend)) +
                                        "  ·  " + ProjectPipelineLabel(entry.metadata.pipeline);
                const float badgeFontSize = baseFontSize * 0.88f;
                const ImVec2 badgeSize = ImGui::GetFont()->CalcTextSizeA(badgeFontSize, FLT_MAX, 0.0f, badgeText.c_str());
                const float badgePadX = 11.0f * uiScale;
                const float badgePadY = 5.0f * uiScale;
                const ImVec2 badgeMin(metaColX, rowMin.y + 22.0f * uiScale);
                const ImVec2 badgeMax(badgeMin.x + badgeSize.x + badgePadX * 2.0f,
                                      badgeMin.y + badgeSize.y + badgePadY * 2.0f);
                listDL->AddRectFilled(badgeMin, badgeMax,
                                      ImGui::GetColorU32(modAlpha(ImVec4(0.085f, 0.110f, 0.155f, 1.0f))),
                                      999.0f);
                listDL->AddRect(badgeMin, badgeMax,
                                ImGui::GetColorU32(modAlpha(ImVec4(0.26f, 0.34f, 0.48f, 1.0f))),
                                999.0f);
                listDL->AddText(ImGui::GetFont(), badgeFontSize,
                                ImVec2(badgeMin.x + badgePadX, badgeMin.y + badgePadY),
                                ImGui::GetColorU32(modAlpha(ImVec4(0.86f, 0.90f, 0.96f, 1.0f))),
                                badgeText.c_str());

                // Last opened (small caption under badge)
                const std::string lastOpened = entry.recent.lastOpened.empty() ? "-" : entry.recent.lastOpened;
                listDL->AddText(ImGui::GetFont(), baseFontSize * 0.84f,
                                ImVec2(metaColX, badgeMax.y + 7.0f * uiScale),
                                ImGui::GetColorU32(modAlpha(ImVec4(0.62f, 0.70f, 0.80f, 1.0f))),
                                lastOpened.c_str());

                // Open button overlay
                ImGui::SetCursorScreenPos(ImVec2(openBtnX, rowMin.y + (cardHeight - openBtnH) * 0.5f));
                ImGui::SetNextItemAllowOverlap();
                ImGui::BeginDisabled(launcherBusy);
                ImGui::PushStyleColor(ImGuiCol_Button, hovered
                    ? ImVec4(0.22f, 0.52f, 0.90f, 1.0f)
                    : ImVec4(0.16f, 0.38f, 0.70f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.58f, 0.96f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.18f, 0.45f, 0.82f, 1.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * rowAlpha);
                if (ImGui::Button("Open##Card", ImVec2(openBtnW, openBtnH))) {
                    shouldOpen = true;
                }
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(3);
                ImGui::EndDisabled();

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
                    launchRecentProject(entry.recent, rowCenter);
                }

                ImGui::SetCursorScreenPos(ImVec2(cursorPos.x, rowMax.y));
                ImGui::Dummy(ImVec2(listWidth, cardGap));
                ImGui::PopID();
            }

            if (!hadVisible && !removedProject) {
                const char* message = projectManager.recentProjects.empty()
                    ? "No recent projects yet — create or open one to get started."
                    : "No projects match your search.";
                ImGui::Dummy(ImVec2(0.0f, 18.0f * uiScale));
                const ImVec2 sp = ImGui::GetCursorScreenPos();
                const ImVec2 ss(listWidth, 110.0f * uiScale);
                listDL->AddRectFilled(sp, ImVec2(sp.x + ss.x, sp.y + ss.y),
                                      ImGui::GetColorU32(ImVec4(0.098f, 0.112f, 0.150f, 0.88f)),
                                      11.0f * uiScale);
                listDL->AddRect(sp, ImVec2(sp.x + ss.x, sp.y + ss.y),
                                ImGui::GetColorU32(ImVec4(0.20f, 0.24f, 0.32f, 1.0f)),
                                11.0f * uiScale, 0, 1.0f);
                ImGui::SetCursorScreenPos(ImVec2(sp.x + 20.0f * uiScale, sp.y + 24.0f * uiScale));
                ImGui::TextColored(ImVec4(0.94f, 0.97f, 1.0f, 1.0f), "%s", message);
                ImGui::SetCursorScreenPos(ImVec2(sp.x + 20.0f * uiScale, sp.y + 56.0f * uiScale));
                ImGui::TextColored(ImVec4(0.59f, 0.66f, 0.75f, 1.0f),
                                   "Use the toolbar above to open an existing project or create a new one.");
                ImGui::Dummy(ss);
            }

            ImGui::EndChild();
        };

        // New Project view (template chooser, inline)
        auto renderNewProjectView = [&]() {
            static char templateSearch[128] = "";
            static int templateCategory = 0; // 0 = Blank Project, 1 = Template Projects
            const std::vector<LauncherTemplateEntry> templates = GatherTemplateEntries();
            const std::vector<LauncherTemplateEntry> builtInPresets = BuiltInProjectPresets();
            const fs::path templatesRoot = GetTemplateProjectsRoot();
            const std::string templateFilter = toLower(TrimCopy(templateSearch));

            LauncherTemplateEntry blankEntry = builtInPresets.empty() ? LauncherTemplateEntry{} : builtInPresets.front();

            auto selectTemplateEntry = [&](const LauncherTemplateEntry& entry) {
                if (entry.isBlankPreset) {
                    projectManager.newProjectTemplatePath.clear();
                    projectManager.newProjectTemplateName = entry.displayName;
                    projectManager.newProjectPresetId = entry.presetId.empty() ? "empty" : entry.presetId;
                } else {
                    projectManager.newProjectTemplatePath = entry.projectRoot.string();
                    projectManager.newProjectTemplateName = entry.displayName;
                    projectManager.newProjectPresetId.clear();
                }
                projectManager.newProjectPipelineMode = ProjectPipelineToUiIndex(entry.pipeline);
                projectManager.newProjectRendererMode = (entry.rendererBackend == Modularity::GraphicsBackend::Vulkan) ? 1 : 0;
#if !MODULARITY_HAS_VULKAN
                if (projectManager.newProjectRendererMode == 1) {
                    projectManager.newProjectRendererMode = 0;
                }
#else
                if (projectManager.newProjectRendererMode == 1 && !hasVulkanPipelinePackage()) {
                    projectManager.newProjectRendererMode = 0;
                }
#endif
                projectManager.newProjectImportLastPackages = false;
            };

            if (projectManager.newProjectTemplateName.empty()) {
                projectManager.newProjectTemplateName = blankEntry.displayName;
                projectManager.newProjectPresetId = blankEntry.presetId;
            }

            const LauncherTemplateEntry* selectedTemplate = nullptr;
            if (projectManager.newProjectTemplatePath.empty()) {
                selectedTemplate = &blankEntry;
                for (const auto& preset : builtInPresets) {
                    if (preset.presetId == projectManager.newProjectPresetId ||
                        preset.displayName == projectManager.newProjectTemplateName) {
                        selectedTemplate = &preset;
                        break;
                    }
                }
            } else {
                for (const auto& t : templates) {
                    if (fs::path(projectManager.newProjectTemplatePath) == t.projectRoot) {
                        selectedTemplate = &t;
                        break;
                    }
                }
            }
            if (!selectedTemplate) {
                projectManager.newProjectTemplatePath.clear();
                projectManager.newProjectTemplateName = blankEntry.displayName;
                projectManager.newProjectPresetId = blankEntry.presetId;
                selectedTemplate = &blankEntry;
            }
            projectManager.newProjectPipelineMode = ProjectPipelineToUiIndex(selectedTemplate->pipeline);
            projectManager.newProjectRendererMode = (selectedTemplate->rendererBackend == Modularity::GraphicsBackend::Vulkan) ? 1 : 0;
            projectManager.newProjectImportLastPackages = false;
#if !MODULARITY_HAS_VULKAN
            if (projectManager.newProjectRendererMode == 1) {
                projectManager.newProjectRendererMode = 0;
            }
#else
            if (projectManager.newProjectRendererMode == 1 && !hasVulkanPipelinePackage()) {
                projectManager.newProjectRendererMode = 0;
            }
#endif

            auto renderCategoryButton = [&](const char* label, int category, float width) {
                const bool selected = templateCategory == category;
                ImGui::PushStyleColor(ImGuiCol_Button, selected ? ImVec4(0.20f, 0.31f, 0.46f, 1.0f)
                                                                : ImVec4(0.13f, 0.16f, 0.22f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, selected ? ImVec4(0.24f, 0.36f, 0.53f, 1.0f)
                                                                       : ImVec4(0.18f, 0.22f, 0.30f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.21f, 0.33f, 0.49f, 1.0f));
                if (ImGui::Button(label, ImVec2(width, 0.0f))) {
                    templateCategory = category;
                    if (templateCategory == 0) {
                        selectTemplateEntry(blankEntry);
                        selectedTemplate = &blankEntry;
                    } else if (selectedTemplate && selectedTemplate->isBlankPreset && !templates.empty()) {
                        selectTemplateEntry(templates.front());
                        selectedTemplate = &templates.front();
                    }
                }
                ImGui::PopStyleColor(3);
            };

            auto resolvePreviewTexture = [&](const LauncherTemplateEntry& entry, ImTextureID& outId, int& outW, int& outH) {
                outId = static_cast<ImTextureID>(0);
                outW = 0;
                outH = 0;
                if (entry.previewImage.empty() || !fs::exists(entry.previewImage)) {
                    return;
                }
                if (usingVulkan() && vulkanRendererInitialized && vulkanRenderer) {
                    outId = vulkanRenderer->getOrCreateUIImage(entry.previewImage.string(), &outW, &outH);
                } else if (!usingVulkan()) {
                    if (Texture* templateTex = renderer.getTexture(entry.previewImage.string())) {
                        outId = (ImTextureID)(intptr_t)templateTex->GetID();
                        outW = templateTex->GetWidth();
                        outH = templateTex->GetHeight();
                    }
                }
            };

            auto renderTemplateListRow = [&](const LauncherTemplateEntry& entry) {
                const bool selected = selectedTemplate &&
                    ((entry.isBlankPreset && selectedTemplate->isBlankPreset &&
                      entry.presetId == selectedTemplate->presetId) ||
                     (!entry.isBlankPreset && !selectedTemplate->isBlankPreset &&
                      selectedTemplate->projectRoot == entry.projectRoot));

                ImGui::PushID(entry.isBlankPreset ? entry.presetId.c_str() : entry.projectRoot.string().c_str());
                const ImVec2 rowPos = ImGui::GetCursorScreenPos();
                const ImVec2 rowSize(std::max(48.0f * uiScale, ImGui::GetContentRegionAvail().x), 62.0f * uiScale);
                ImGui::InvisibleButton("TemplateSelectRow", rowSize);
                const bool hovered = ImGui::IsItemHovered();
                if (ImGui::IsItemClicked()) {
                    selectTemplateEntry(entry);
                    selectedTemplate = &entry;
                }

                ImDrawList* list = ImGui::GetWindowDrawList();
                const ImVec2 rowMax(rowPos.x + rowSize.x, rowPos.y + rowSize.y);
                if (hovered || selected) {
                    list->AddRectFilled(rowPos,
                                        rowMax,
                                        ImGui::GetColorU32(selected ? ImVec4(0.135f, 0.175f, 0.245f, 0.92f)
                                                                    : ImVec4(0.11f, 0.14f, 0.19f, 0.78f)),
                                        5.0f * uiScale);
                }
                const float tmplPulse = 0.5f + 0.5f * std::sin((float)ImGui::GetTime() * 2.0f);
                const float tmplPulseA = ImLerp(0.62f, 0.95f, tmplPulse);
                if (selected) {
                    // Soft halo behind the selected template row.
                    list->AddRect(ImVec2(rowPos.x - 1.0f * uiScale, rowPos.y - 1.0f * uiScale),
                                  ImVec2(rowMax.x + 1.0f * uiScale, rowMax.y + 1.0f * uiScale),
                                  ImGui::GetColorU32(ImVec4(0.55f, 0.74f, 0.98f, 0.18f * tmplPulseA)),
                                  5.0f * uiScale, 0, 1.0f * uiScale);
                }
                list->AddRectFilled(ImVec2(rowPos.x, rowPos.y + 6.0f * uiScale),
                                    ImVec2(rowPos.x + (selected ? 3.0f : 2.0f) * uiScale, rowMax.y - 6.0f * uiScale),
                                    ImGui::GetColorU32(selected ? ImVec4(0.55f, 0.78f, 1.0f, tmplPulseA)
                                                                : ImVec4(0.27f, 0.38f, 0.52f, hovered ? 0.55f : 0.0f)),
                                    2.0f * uiScale);
                list->AddLine(ImVec2(rowPos.x, rowMax.y),
                              ImVec2(rowMax.x, rowMax.y),
                              ImGui::GetColorU32(ImVec4(0.18f, 0.22f, 0.30f, 1.0f)),
                              1.0f);

                const ImVec2 thumbMin(rowPos.x + 10.0f * uiScale, rowPos.y + 7.0f * uiScale);
                const ImVec2 thumbMax(thumbMin.x + 80.0f * uiScale, rowMax.y - 7.0f * uiScale);
                ImTextureID textureId = static_cast<ImTextureID>(0);
                int textureW = 0;
                int textureH = 0;
                resolvePreviewTexture(entry, textureId, textureW, textureH);
                if (textureId != static_cast<ImTextureID>(0)) {
                    DrawImageCover(list, textureId, thumbMin, thumbMax,
                                   textureW, textureH,
                                   ImGui::GetColorU32(ImVec4(1, 1, 1, 1)), 4.0f * uiScale);
                } else {
                    list->AddRectFilled(thumbMin, thumbMax, ImGui::GetColorU32(ImVec4(0.08f, 0.10f, 0.14f, 1.0f)), 4.0f * uiScale);
                    const char* placeholder = entry.isBlankPreset ? "Default" : "Preview";
                    const ImVec2 textSize = ImGui::CalcTextSize(placeholder);
                    list->AddText(ImVec2(thumbMin.x + ((thumbMax.x - thumbMin.x) - textSize.x) * 0.5f,
                                         thumbMin.y + ((thumbMax.y - thumbMin.y) - textSize.y) * 0.5f),
                                  ImGui::GetColorU32(ImVec4(0.63f, 0.69f, 0.77f, 1.0f)),
                                  placeholder);
                }

                // labels go through the draw list so they don't submit ImGui items; the InvisibleButton
                // is the row's only real layout item so rows advance by row height.
                list->AddText(ImVec2(thumbMax.x + 12.0f * uiScale, rowPos.y + 10.0f * uiScale),
                              ImGui::GetColorU32(ImVec4(0.93f, 0.96f, 1.0f, 1.0f)),
                              entry.displayName.c_str());
                list->AddText(ImVec2(thumbMax.x + 12.0f * uiScale, rowPos.y + 29.0f * uiScale),
                              ImGui::GetColorU32(ImVec4(0.61f, 0.68f, 0.77f, 1.0f)),
                              ProjectPipelineLabel(entry.pipeline));
                ImGui::PopID();
            };

            static int templateViewMode = 1; // 0 = Grid, 1 = List
            auto renderTemplateViewButton = [&](const char* label, int mode, float width) {
                const bool selected = templateViewMode == mode;
                ImGui::PushStyleColor(ImGuiCol_Button, selected ? ImVec4(0.20f, 0.31f, 0.46f, 1.0f)
                                                                : ImVec4(0.13f, 0.16f, 0.22f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, selected ? ImVec4(0.24f, 0.36f, 0.53f, 1.0f)
                                                                       : ImVec4(0.18f, 0.22f, 0.30f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.21f, 0.33f, 0.49f, 1.0f));
                if (ImGui::Button(label, ImVec2(width, 0.0f))) {
                    templateViewMode = mode;
                }
                ImGui::PopStyleColor(3);
            };

            auto renderTemplateGridTile = [&](const LauncherTemplateEntry& entry, float tileWidth) {
                const bool selected = selectedTemplate &&
                    ((entry.isBlankPreset && selectedTemplate->isBlankPreset &&
                      entry.presetId == selectedTemplate->presetId) ||
                     (!entry.isBlankPreset && !selectedTemplate->isBlankPreset &&
                      selectedTemplate->projectRoot == entry.projectRoot));

                ImGui::PushID(entry.isBlankPreset ? entry.presetId.c_str() : entry.projectRoot.string().c_str());
                const float tileHeight = 114.0f * uiScale;
                const float previewHeight = 64.0f * uiScale;
                const ImVec2 tilePos = ImGui::GetCursorScreenPos();
                const ImVec2 tileSize(std::max(48.0f * uiScale, tileWidth), tileHeight);
                ImGui::InvisibleButton("TemplateGridTile", tileSize);
                const bool hovered = ImGui::IsItemHovered();
                if (ImGui::IsItemClicked()) {
                    selectTemplateEntry(entry);
                    selectedTemplate = &entry;
                }

                ImDrawList* list = ImGui::GetWindowDrawList();
                const ImVec2 tileMax(tilePos.x + tileSize.x, tilePos.y + tileSize.y);
                const ImVec2 thumbMin(tilePos.x, tilePos.y);
                const ImVec2 thumbMax(tileMax.x, tilePos.y + previewHeight);
                list->AddRectFilled(tilePos,
                                    tileMax,
                                    ImGui::GetColorU32(selected ? ImVec4(0.13f, 0.18f, 0.25f, 0.96f)
                                                                : ImVec4(0.10f, 0.13f, 0.18f, hovered ? 0.90f : 0.78f)),
                                    3.0f * uiScale);
                list->AddRect(tilePos,
                              tileMax,
                              ImGui::GetColorU32(selected ? ImVec4(0.39f, 0.66f, 0.97f, 1.0f)
                                                          : ImVec4(0.20f, 0.25f, 0.33f, hovered ? 1.0f : 0.85f)),
                              3.0f * uiScale);
                if (selected || hovered) {
                    list->AddRectFilled(ImVec2(tilePos.x, tilePos.y),
                                        ImVec2(tilePos.x + tileSize.x, tilePos.y + 2.0f * uiScale),
                                        ImGui::GetColorU32(selected ? ImVec4(0.42f, 0.72f, 1.0f, 1.0f)
                                                                    : ImVec4(0.28f, 0.42f, 0.60f, 0.85f)));
                }

                ImTextureID textureId = static_cast<ImTextureID>(0);
                int textureW = 0;
                int textureH = 0;
                resolvePreviewTexture(entry, textureId, textureW, textureH);
                if (textureId != static_cast<ImTextureID>(0)) {
                    DrawImageCover(list, textureId, thumbMin, thumbMax,
                                   textureW, textureH,
                                   ImGui::GetColorU32(ImVec4(1, 1, 1, 1)), 3.0f * uiScale);
                } else {
                    list->AddRectFilled(thumbMin, thumbMax, ImGui::GetColorU32(ImVec4(0.08f, 0.10f, 0.14f, 1.0f)), 3.0f * uiScale);
                    const char* placeholder = entry.isBlankPreset ? "Default" : "Preview";
                    const ImVec2 textSize = ImGui::CalcTextSize(placeholder);
                    list->AddText(ImVec2(thumbMin.x + ((thumbMax.x - thumbMin.x) - textSize.x) * 0.5f,
                                         thumbMin.y + ((thumbMax.y - thumbMin.y) - textSize.y) * 0.5f),
                                  ImGui::GetColorU32(ImVec4(0.63f, 0.69f, 0.77f, 1.0f)),
                                  placeholder);
                }

                // same draw-list trick as the rows: the InvisibleButton is the tile's only layout item
                // so SameLine()/wrapping stays anchored to the tile bounds.
                const float titleWrapWidth = std::max(1.0f, tileSize.x - 16.0f * uiScale);
                list->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                              ImVec2(tilePos.x + 8.0f * uiScale, thumbMax.y + 8.0f * uiScale),
                              ImGui::GetColorU32(ImVec4(0.93f, 0.96f, 1.0f, 1.0f)),
                              entry.displayName.c_str(), nullptr, titleWrapWidth);
                list->AddText(ImVec2(tilePos.x + 8.0f * uiScale, tileMax.y - 22.0f * uiScale),
                              ImGui::GetColorU32(ImVec4(0.61f, 0.68f, 0.77f, 1.0f)),
                              ProjectPipelineLabel(entry.pipeline));
                ImGui::PopID();
            };

            auto createProjectFromCurrentState = [&]() {
                if (std::strlen(projectManager.newProjectName) == 0) {
                    projectManager.errorMessage = "Hey! You'll need a Project name first!";
                } else if (std::strlen(projectManager.newProjectLocation) == 0) {
                    projectManager.errorMessage = "Hey! Please Set a default location in Settings first.";
                } else {
                    createNewProject(projectManager.newProjectName, projectManager.newProjectLocation);
                    if (projectManager.errorMessage.empty()) {
                        projectManager.showNewProjectDialog = false;
                    }
                }
            };

            ImGui::TextColored(ImVec4(0.94f, 0.97f, 1.0f, 1.0f), "Create New Project");
            /*ImGui::TextColored(ImVec4(0.59f, 0.66f, 0.75f, 1.0f), "Pick a starting point, review the preview, then confirm name and location.");*/
            ImGui::Dummy(ImVec2(0.0f, 8.0f * uiScale));

            renderCategoryButton("Defaults", 0, 140.0f * uiScale);
            ImGui::SameLine();
            renderCategoryButton("Template Projects", 1, 164.0f * uiScale);
            ImGui::SameLine();
            const float backBtnW = 96.0f * uiScale;
            ImGui::SameLine(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - backBtnW);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.20f, 0.27f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.28f, 0.37f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.19f, 0.25f, 0.34f, 1.0f));
            /*if (ImGui::Button("Back", ImVec2(backBtnW, 0.0f))) {
                projectManager.showNewProjectDialog = false;
                projectManager.errorMessage.clear();
            }*/
            ImGui::PopStyleColor(3);
            ImGui::Dummy(ImVec2(0.0f, 8.0f * uiScale));

            const float splitHeight = std::max(320.0f * uiScale, ImGui::GetContentRegionAvail().y);
            const float leftWidth = ImClamp(ImGui::GetContentRegionAvail().x * 0.56f, 380.0f * uiScale, 620.0f * uiScale);
            const float splitGap = 14.0f * uiScale;

            ImGui::BeginChild("NewProjectTemplateListPane", ImVec2(leftWidth, splitHeight), false);
            ImGui::TextColored(ImVec4(0.89f, 0.93f, 0.98f, 1.0f), "Template Selection");
            const float toggleWidth = 112.0f * uiScale;
            ImGui::SameLine();
            ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),
                                          ImGui::GetWindowWidth() - toggleWidth - ImGui::GetStyle().WindowPadding.x));
            renderTemplateViewButton("Grid", 0, 52.0f * uiScale);
            ImGui::SameLine(0.0f, 4.0f * uiScale);
            renderTemplateViewButton("List", 1, 52.0f * uiScale);
            if (templateCategory == 1) {
                ImGui::TextDisabled("Template folders are loaded from %s", templatesRoot.filename().string().c_str());
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                    ImGui::SetTooltip("%s", templatesRoot.string().c_str());
                }
                ImGui::SetNextItemWidth(-1);
                ImGui::InputTextWithHint("##TemplateSearch", "Search templates", templateSearch, sizeof(templateSearch));
            } else {
                ImGui::TextDisabled("Start from a clean project with no template content.");
            }
            ImGui::Dummy(ImVec2(0.0f, 6.0f * uiScale));
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0.0f, 4.0f * uiScale));

            ImGui::BeginChild("TemplateRows", ImVec2(0.0f, 0.0f), false);
            if (templateCategory == 0) {
                if (templateViewMode == 0) {
                    int tileIndex = 0;
                    const float tileSpacing = 8.0f * uiScale;
                    const float contentRegionWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
                    const float minTileWidth = 146.0f * uiScale;
                    const int tileColumns = std::max(1, static_cast<int>((contentRegionWidth + tileSpacing) / (minTileWidth + tileSpacing)));
                    const float tileWidth = std::max(48.0f * uiScale,
                                                     (contentRegionWidth - tileSpacing * static_cast<float>(tileColumns - 1)) / static_cast<float>(tileColumns));
                    for (const auto& preset : builtInPresets) {
                        if (tileIndex > 0 && (tileIndex % tileColumns) != 0) ImGui::SameLine(0.0f, tileSpacing);
                        renderTemplateGridTile(preset, tileWidth);
                        ++tileIndex;
                    }
                } else {
                    for (const auto& preset : builtInPresets) renderTemplateListRow(preset);
                }
            } else {
                bool hadVisible = false;
                int tileIndex = 0;
                const float tileSpacing = 8.0f * uiScale;
                const float contentRegionWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
                const float minTileWidth = 146.0f * uiScale;
                const int tileColumns = std::max(1, static_cast<int>((contentRegionWidth + tileSpacing) / (minTileWidth + tileSpacing)));
                const float tileWidth = std::max(48.0f * uiScale,
                                                 (contentRegionWidth - tileSpacing * static_cast<float>(tileColumns - 1)) / static_cast<float>(tileColumns));
                for (const auto& t : templates) {
                    const std::string lowerName = toLower(t.displayName);
                    if (!templateFilter.empty() && lowerName.find(templateFilter) == std::string::npos) {
                        continue;
                    }
                    if (templateViewMode == 0) {
                        if (tileIndex > 0 && (tileIndex % tileColumns) != 0) {
                            ImGui::SameLine(0.0f, tileSpacing);
                        }
                        renderTemplateGridTile(t, tileWidth);
                        ++tileIndex;
                    } else {
                        renderTemplateListRow(t);
                    }
                    hadVisible = true;
                }
                if (!hadVisible) {
                    if (templates.empty()) {
                        ImGui::TextDisabled("No templates found.");
                        ImGui::TextDisabled("Add folders with project.modu to Template-Projects.");
                    } else {
                        ImGui::TextDisabled("No templates match your search.");
                    }
                }
            }
            ImGui::EndChild();
            ImGui::EndChild();

            ImGui::SameLine(0.0f, splitGap);

            ImGui::BeginChild("NewProjectTemplateDetailPane", ImVec2(0.0f, splitHeight), false);
            ImGui::TextColored(ImVec4(0.89f, 0.93f, 0.98f, 1.0f), "Preview");
            ImGui::Dummy(ImVec2(0.0f, 6.0f * uiScale));
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0.0f, 6.0f * uiScale));
            {
                const float previewHeight = std::min(160.0f * uiScale, ImGui::GetContentRegionAvail().y * 0.26f);
                const ImVec2 previewPos = ImGui::GetCursorScreenPos();
                const ImVec2 previewSize(ImGui::GetContentRegionAvail().x, previewHeight);
                const ImVec2 previewMax(previewPos.x + previewSize.x, previewPos.y + previewSize.y);
                ImDrawList* list = ImGui::GetWindowDrawList();
                list->AddRectFilled(previewPos, previewMax, ImGui::GetColorU32(ImVec4(0.08f, 0.10f, 0.14f, 1.0f)), 3.0f * uiScale);
                list->AddRect(previewPos, previewMax, ImGui::GetColorU32(ImVec4(0.22f, 0.27f, 0.35f, 1.0f)), 3.0f * uiScale);

                ImTextureID previewTexId = static_cast<ImTextureID>(0);
                int previewTexWidth = 0;
                int previewTexHeight = 0;
                resolvePreviewTexture(*selectedTemplate, previewTexId, previewTexWidth, previewTexHeight);
                if (previewTexId != static_cast<ImTextureID>(0)) {
                    DrawImageCover(list, previewTexId, previewPos, previewMax,
                                   previewTexWidth, previewTexHeight,
                                   ImGui::GetColorU32(ImVec4(1, 1, 1, 1)), 3.0f * uiScale);
                } else {
                    const char* placeholder = selectedTemplate->isBlankPreset ? "Standard 3D Project" : "No Preview Available";
                    const ImVec2 textSize = ImGui::CalcTextSize(placeholder);
                    list->AddText(ImVec2(previewPos.x + (previewSize.x - textSize.x) * 0.5f,
                                         previewPos.y + (previewSize.y - textSize.y) * 0.5f),
                                  ImGui::GetColorU32(ImVec4(0.66f, 0.72f, 0.80f, 1.0f)),
                                  placeholder);
                }
                ImGui::Dummy(previewSize);
            }

            const std::string selectedDescription = selectedTemplate->isBlankPreset
                ? "This option creates a clean Modularity project with the default folders, scripts layout, and no starter content, this option offers a clean slate to start off with."
                : "This Uses the selected template project as the starting point.";
            const std::string sourceLabel = selectedTemplate->isBlankPreset
                ? "Built-in preset"
                : selectedTemplate->projectRoot.filename().string();
            const std::string summaryLabel = std::string(ProjectPipelineLabel(ProjectPipelineFromUiIndex(projectManager.newProjectPipelineMode))) +
                "  •  " +
                Modularity::ToString(projectManager.newProjectRendererMode == 1 ? Modularity::GraphicsBackend::Vulkan
                                                                                : Modularity::GraphicsBackend::OpenGL) +
                "  •  " + sourceLabel;

            ImGui::Dummy(ImVec2(0.0f, 8.0f * uiScale));
            ImGui::TextColored(ImVec4(0.94f, 0.97f, 1.0f, 1.0f), "%s", selectedTemplate->displayName.c_str());
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
            ImGui::TextColored(ImVec4(0.60f, 0.67f, 0.76f, 1.0f), "%s", selectedDescription.c_str());
            ImGui::PopTextWrapPos();
            ImGui::Dummy(ImVec2(0.0f, 6.0f * uiScale));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
            ImGui::TextColored(ImVec4(0.72f, 0.78f, 0.86f, 1.0f), "%s", summaryLabel.c_str());
            ImGui::PopTextWrapPos();
            ImGui::Dummy(ImVec2(0.0f, 10.0f * uiScale));
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0.0f, 8.0f * uiScale));

            ImGui::TextDisabled("Project Name");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##ProjectNameInline", projectManager.newProjectName, sizeof(projectManager.newProjectName));
            ImGui::Dummy(ImVec2(0.0f, 8.0f * uiScale));
            ImGui::TextDisabled("Project Location");
            const float browseWidth = 88.0f * uiScale;
            ImGui::SetNextItemWidth(-(browseWidth + ImGui::GetStyle().ItemSpacing.x));
            ImGui::InputText("##ProjectLocationInline", projectManager.newProjectLocation, sizeof(projectManager.newProjectLocation));
            ImGui::SameLine();
            if (ImGui::Button("Browse##ProjectLocationInline", ImVec2(browseWidth, 0.0f))) {
            }

            if (!projectManager.errorMessage.empty()) {
                ImGui::Dummy(ImVec2(0.0f, 8.0f * uiScale));
                ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.42f, 1.0f), "%s", projectManager.errorMessage.c_str());
            }

            if (std::strlen(projectManager.newProjectName) > 0 && std::strlen(projectManager.newProjectLocation) > 0) {
                ImGui::Dummy(ImVec2(0.0f, 6.0f * uiScale));
                const fs::path previewRoot = fs::path(projectManager.newProjectLocation) / projectManager.newProjectName;
                ImGui::TextColored(ImVec4(0.57f, 0.64f, 0.73f, 1.0f), "Project folder: %s", previewRoot.string().c_str());
            }

            ImGui::Dummy(ImVec2(0.0f, 10.0f * uiScale));
            const float cancelWidth = 108.0f * uiScale;
            const float createWidth = 148.0f * uiScale;
            const float actionsWidth = cancelWidth + createWidth + ImGui::GetStyle().ItemSpacing.x;
            const float actionsOffset = std::max(0.0f, ImGui::GetContentRegionAvail().x - actionsWidth);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + actionsOffset);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.20f, 0.27f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.28f, 0.37f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.19f, 0.25f, 0.34f, 1.0f));
            if (ImGui::Button("Cancel##NewProjectFooter", ImVec2(cancelWidth, 0.0f))) {
                projectManager.showNewProjectDialog = false;
                projectManager.errorMessage.clear();
            }
            ImGui::PopStyleColor(3);
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.90f, 0.66f, 0.17f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.96f, 0.72f, 0.24f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.84f, 0.60f, 0.13f, 1.0f));
            if (ImGui::Button("Create Project", ImVec2(createWidth, 0.0f))) {
                createProjectFromCurrentState();
            }
            ImGui::PopStyleColor(3);
            ImGui::EndChild();
        };

        // Packages view
        auto renderPackagesView = [&]() {
            ImGui::TextColored(ImVec4(0.95f, 0.97f, 1.0f, 1.0f), "Installed ModuPAKs");
            ImGui::TextColored(ImVec4(0.59f, 0.66f, 0.75f, 1.0f),
                               "These are ModuPackages tracked from the most recent project's manifest.");

            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float refreshW = 110.0f * uiScale;
            const float newProjW = 142.0f * uiScale;
            const float btnH = 32.0f * uiScale;
            const float controlsW = refreshW + newProjW + spacing;
            if (ImGui::GetContentRegionAvail().x > controlsW) {
                ImGui::SameLine(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - controlsW);
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() - (ImGui::GetTextLineHeight() + 2.0f * uiScale));
            } else {
                ImGui::Dummy(ImVec2(0.0f, 8.0f * uiScale));
            }
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.17f, 0.22f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.22f, 0.28f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.16f, 0.20f, 0.26f, 1.0f));
            if (ImGui::Button("Refresh", ImVec2(refreshW, btnH))) {
                (void)GetLauncherPackageSnapshot(projectManager, true);
            }
            ImGui::PopStyleColor(3);
            /*ImGui::SameLine();
            ImGui::BeginDisabled(launcherBusy);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.92f, 0.66f, 0.16f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.98f, 0.74f, 0.24f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.84f, 0.58f, 0.12f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.08f, 0.07f, 0.04f, 1.0f));
            if (ImGui::Button("+ New Project", ImVec2(newProjW, btnH))) {
                openNewProjectDialog();
            }
            ImGui::PopStyleColor(4);*/
            ImGui::Dummy(ImVec2(0.0f, 14.0f * uiScale));

            const LauncherPackageSnapshot& snapshot = GetLauncherPackageSnapshot(projectManager);
            if (!snapshot.hasSource) {
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.098f, 0.112f, 0.150f, 0.88f));
                ImGui::BeginChild("##PkgEmpty", ImVec2(0.0f, 110.0f * uiScale), true);
                ImGui::TextColored(ImVec4(0.94f, 0.97f, 1.0f, 1.0f), "No project available");
                ImGui::TextColored(ImVec4(0.59f, 0.66f, 0.75f, 1.0f),
                                   "Open or create a project first — installed packages will appear here.");
                ImGui::EndChild();
                ImGui::PopStyleColor();
            } else {
                ImGui::TextDisabled("Source Project: %s", snapshot.sourceProjectName.c_str());
                ImGui::TextDisabled("Packages: %zu (%zu external)",
                                    snapshot.packageLabels.size(),
                                    snapshot.externalCount);
                ImGui::Dummy(ImVec2(0.0f, 8.0f * uiScale));

                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.098f, 0.112f, 0.150f, 0.88f));
                ImGui::BeginChild("InstalledPackagesList", ImVec2(0, 0), true);
                if (snapshot.packageLabels.empty()) {
                    ImGui::TextDisabled("No optional packages were saved in the source manifest.");
                } else {
                    for (const auto& label : snapshot.packageLabels) {
                        ImGui::BulletText("%s", label.c_str());
                    }
                }
                ImGui::EndChild();
                ImGui::PopStyleColor();
            }
        };

        // Settings view
        auto renderSettingsView = [&]() {
            static std::string settingsStatus;
            static bool settingsStatusError = false;

            ImGui::TextColored(ImVec4(0.95f, 0.97f, 1.0f, 1.0f), "Settings");
            ImGui::TextColored(ImVec4(0.59f, 0.66f, 0.75f, 1.0f),
                               "Defaults that apply to every new project you create.");
            ImGui::Dummy(ImVec2(0.0f, 14.0f * uiScale));

            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.098f, 0.112f, 0.150f, 0.88f));
            ImGui::BeginChild("##SettingsCard", ImVec2(0.0f, 0.0f), true);

            ImGui::TextUnformatted("Default Project Location");
            ImGui::TextDisabled("New projects will be created in this folder by default.");
            ImGui::Dummy(ImVec2(0.0f, 4.0f * uiScale));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f * uiScale, 8.0f * uiScale));
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##DefaultProjectLocation",
                             projectManager.defaultProjectLocation,
                             sizeof(projectManager.defaultProjectLocation));
            ImGui::PopStyleVar();

            ImGui::Dummy(ImVec2(0.0f, 10.0f * uiScale));

            if (ImGui::Button("Save Settings", ImVec2(140.0f * uiScale, 34.0f * uiScale))) {
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
            if (ImGui::Button("Use Home Default", ImVec2(160.0f * uiScale, 34.0f * uiScale))) {
#if defined(__ANDROID__)
                // No HOME on Android: use Documents when access is granted, else
                // the app-private data dir (current_path is the read-only "/").
                std::string fallback;
                const std::string docs = Modularity::AndroidRuntime::GetExternalDocumentsPath();
                if (!docs.empty() && Modularity::AndroidRuntime::HasAllFilesAccess()) {
                    fallback = (fs::path(docs) / "ModularityProjects").string();
                } else if (const char* dataPath = Modularity::AndroidRuntime::GetInternalDataPath()) {
                    fallback = (fs::path(dataPath) / "ModularityProjects").string();
                } else {
                    fallback = "/data/local/tmp/ModularityProjects";
                }
#elif defined(_WIN32)
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

#ifdef __ANDROID__
            // Android can't write public Documents until storage access is granted; surface that here
            // so projects can live in Documents/ModularityProjects like desktop.
            {
                ImGui::Dummy(ImVec2(0.0f, 14.0f * uiScale));
                ImGui::Separator();
                ImGui::Dummy(ImVec2(0.0f, 10.0f * uiScale));
                ImGui::TextUnformatted("File Access");

                const bool hasAccess = Modularity::AndroidRuntime::HasAllFilesAccess();
                const std::string docsRoot = Modularity::AndroidRuntime::GetExternalDocumentsPath();
                const std::string docsProjects = docsRoot.empty()
                    ? std::string()
                    : (fs::path(docsRoot) / "ModularityProjects").string();

                if (hasAccess) {
                    ImGui::TextColored(ImVec4(0.61f, 0.86f, 0.70f, 1.0f),
                                       "Granted - projects can live in shared storage.");
                    if (!docsProjects.empty()) {
                        ImGui::Dummy(ImVec2(0.0f, 6.0f * uiScale));
                        if (ImGui::Button("Use Documents Folder",
                                          ImVec2(220.0f * uiScale, 34.0f * uiScale))) {
                            std::error_code ec;
                            fs::create_directories(docsProjects, ec);
                            if (ec) {
                                settingsStatus = "Failed to create folder: " + ec.message();
                                settingsStatusError = true;
                            } else {
                                std::snprintf(projectManager.defaultProjectLocation,
                                              sizeof(projectManager.defaultProjectLocation),
                                              "%s", docsProjects.c_str());
                                std::snprintf(projectManager.newProjectLocation,
                                              sizeof(projectManager.newProjectLocation),
                                              "%s", projectManager.defaultProjectLocation);
                                projectManager.saveLauncherSettings();
                                settingsStatus = "Projects will be stored in Documents/ModularityProjects.";
                                settingsStatusError = false;
                            }
                        }
                    }
                } else {
                    ImGui::TextDisabled("Not granted. Projects are kept in private app storage,");
                    ImGui::TextDisabled("which other apps and your file manager can't see.");
                    ImGui::Dummy(ImVec2(0.0f, 6.0f * uiScale));
                    if (ImGui::Button("Grant File Access",
                                      ImVec2(220.0f * uiScale, 34.0f * uiScale))) {
                        std::string reqErr;
                        if (Modularity::AndroidRuntime::RequestAllFilesAccess(reqErr)) {
                            settingsStatus = "Grant access in the settings screen, then return here.";
                            settingsStatusError = false;
                        } else {
                            settingsStatus = reqErr.empty()
                                ? std::string("Could not open the storage permission screen.")
                                : reqErr;
                            settingsStatusError = true;
                        }
                    }
                }
            }
#endif

            if (!settingsStatus.empty()) {
                ImGui::Dummy(ImVec2(0.0f, 8.0f * uiScale));
                ImGui::TextColored(settingsStatusError
                                       ? ImVec4(1.0f, 0.42f, 0.42f, 1.0f)
                                       : ImVec4(0.61f, 0.86f, 0.70f, 1.0f),
                                   "%s",
                                   settingsStatus.c_str());
            }

            ImGui::EndChild();
            ImGui::PopStyleColor();
        };

        auto renderGitHubView = [&]() {
            static int selectedRecentProject = 0;
            static ProjectGitUiState gitUi;
            static bool githubLoginStarted = false;
            const auto& projects = projectManager.recentProjects;

            ImGui::TextColored(ImVec4(0.95f, 0.97f, 1.0f, 1.0f), "Hosted Git Projects");
            ImGui::TextColored(ImVec4(0.59f, 0.66f, 0.75f, 1.0f),
                               "Publish and synchronize Modularity projects with GitHub, GitLab, or another Git host.");
            ImGui::Dummy(ImVec2(0.0f, 12.0f * uiScale));

            if (projects.empty()) {
                ImGui::TextDisabled("No projects are registered in the Hub yet.");
                return;
            }

            selectedRecentProject = std::clamp(
                selectedRecentProject, 0, static_cast<int>(projects.size()) - 1);
            const RecentProject& selectedProject = projects[static_cast<size_t>(selectedRecentProject)];
            if (ImGui::BeginCombo("Project", selectedProject.name.c_str())) {
                for (size_t i = 0; i < projects.size(); ++i) {
                    const bool selected = selectedRecentProject == static_cast<int>(i);
                    if (ImGui::Selectable(projects[i].name.c_str(), selected)) {
                        selectedRecentProject = static_cast<int>(i);
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            fs::path selectedPath = selectedProject.path;
            const fs::path projectPath = fs::is_directory(selectedPath)
                ? selectedPath
                : selectedPath.parent_path();
            if (gitUi.projectPath != projectPath) {
                gitUi = ProjectGitUiState{};
                gitUi.projectPath = projectPath;
            }
            if (gitUi.busy && gitUi.future.valid() &&
                gitUi.future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                ProjectGitResult result = gitUi.future.get();
                gitUi.busy = false;
                gitUi.status = result.output;
            }
            auto startAction = [&](std::string action, auto task) {
                if (gitUi.busy) return;
                gitUi.busy = true;
                gitUi.action = std::move(action);
                gitUi.status = gitUi.action + "...";
                gitUi.future = std::async(std::launch::async, std::move(task));
            };

            const char* providerNames[] = { "GitHub", "GitLab", "Other Git Host" };
            ImGui::SetNextItemWidth(220.0f * uiScale);
            ImGui::Combo("Provider", &gitUi.provider, providerNames, IM_ARRAYSIZE(providerNames));

            ImGui::SeparatorText("Provider Account");
            ImGui::BeginDisabled(gitUi.busy);
            const char* connectLabel = gitUi.provider == 0
                ? "Connect GitHub to Modularity"
                : gitUi.provider == 1
                    ? "Connect GitLab to Modularity"
                    : "Connect Using Remote URL";
            if (ImGui::Button(connectLabel)) {
                githubLoginStarted = false;
                if (gitUi.provider == 2) {
                    gitUi.status = "Enter the repository clone URL below. Authentication is handled by Git or your credential manager.";
                } else {
                    ImGui::OpenPopup("Connect Git Provider to Modularity");
                }
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(gitUi.provider == 2);
            if (ImGui::Button("Check Connection")) {
                const int provider = gitUi.provider;
                startAction("Checking provider login", [provider]() {
                    return provider == 0 ? CheckProjectGitHubLogin()
                                         : CheckProjectGitLabLogin();
                });
            }
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            ImGui::TextDisabled("Credentials stay with the provider CLI or Git credential manager; Modularity never stores tokens.");
            ImGui::Dummy(ImVec2(0.0f, 8.0f * uiScale));

            ImGui::SetNextWindowSize(ImVec2(520.0f * uiScale, 300.0f * uiScale),
                                     ImGuiCond_Appearing);
            if (ImGui::BeginPopupModal("Connect Git Provider to Modularity", nullptr,
                                       ImGuiWindowFlags_NoResize |
                                           ImGuiWindowFlags_NoSavedSettings)) {
                ImGui::TextColored(ImVec4(0.92f, 0.96f, 1.0f, 1.0f),
                                   "Connect your hosted Git account");
                ImGui::Separator();
                ImGui::TextWrapped(
                    gitUi.provider == 0
                        ? "Modularity uses GitHub's official CLI login. A secure browser page will open for authorization."
                        : "Modularity uses GitLab's official CLI login. Follow its secure authorization prompts. Your password and token are never stored by Modularity.");
                ImGui::Spacing();
                ImGui::TextUnformatted("1. Start the GitHub login.");
                ImGui::TextUnformatted("2. Finish authorization in your browser.");
                ImGui::TextUnformatted("3. Return here and check the connection.");
                ImGui::Spacing();

                ImGui::BeginDisabled(gitUi.busy);
                if (ImGui::Button(githubLoginStarted ? "Open Login Again" : "Open Secure Provider Login")) {
                    githubLoginStarted = gitUi.provider == 0
                        ? LaunchProjectGitHubLogin()
                        : LaunchProjectGitLabLogin();
                    gitUi.status = githubLoginStarted
                        ? "Login started. Complete GitHub authorization, then check the connection."
                        : "Could not open the login flow. Install GitHub CLI and run: gh auth login";
                }
                ImGui::SameLine();
                if (ImGui::Button("Check Connection")) {
                    const int provider = gitUi.provider;
                    startAction("Checking provider connection", [provider]() {
                        return provider == 0 ? CheckProjectGitHubLogin()
                                             : CheckProjectGitLabLogin();
                    });
                }
                ImGui::EndDisabled();

                if (gitUi.busy) {
                    ImGui::Spacing();
                    ImGui::Spinner("##GitHubLoginSpinner", 7.0f, 2,
                                   ImGui::GetColorU32(ImGuiCol_SliderGrabActive));
                    ImGui::SameLine();
                    ImGui::TextUnformatted("Checking connection...");
                }
                ImGui::Spacing();
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextDisabled("%s", gitUi.status.c_str());
                ImGui::PopTextWrapPos();

                ImGui::SetCursorPosY(ImGui::GetWindowHeight() -
                                     ImGui::GetFrameHeightWithSpacing() - 12.0f * uiScale);
                if (ImGui::Button("Done", ImVec2(100.0f * uiScale, 0.0f))) {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            ImGui::TextDisabled("%s", projectPath.string().c_str());
            if (gitUi.busy) {
                ImGui::Spinner("##HubGitSpinner", 7.0f, 2,
                               ImGui::GetColorU32(ImGuiCol_SliderGrabActive));
                ImGui::SameLine();
                ImGui::TextUnformatted(gitUi.action.c_str());
            }

            ImGui::BeginDisabled(gitUi.busy);
            if (ImGui::Button("Refresh")) {
                startAction("Refreshing Git status", [projectPath]() {
                    return RunProjectGitCommand(
                        projectPath,
                        "status --short --branch && git -C " +
                            QuoteProjectGitArgument(projectPath.string()) + " remote -v");
                });
            }
            ImGui::SameLine();
            if (ImGui::Button("Initialize Git")) {
                startAction("Initializing repository", [projectPath]() {
                    WriteModularityGitIgnore(projectPath);
                    ProjectGitResult result = RunProjectGitCommand(projectPath, "init -b main");
                    if (!result.success) result = RunProjectGitCommand(projectPath, "init");
                    return result;
                });
            }
            ImGui::EndDisabled();

            ImGui::SeparatorText("Publish or connect");
            ImGui::SetNextItemWidth(std::min(620.0f * uiScale, ImGui::GetContentRegionAvail().x));
            const char* remoteHint = gitUi.provider == 0
                ? "https://github.com/owner/repository.git"
                : gitUi.provider == 1
                    ? "https://gitlab.com/owner/repository.git"
                    : "https://your-git-host.example/owner/repository.git";
            ImGui::InputTextWithHint("##HubGitRemote", remoteHint,
                                     gitUi.remoteUrl, sizeof(gitUi.remoteUrl));
            ImGui::BeginDisabled(gitUi.busy || gitUi.remoteUrl[0] == '\0');
            if (ImGui::Button("Connect Existing Repository")) {
                const std::string remoteUrl = gitUi.remoteUrl;
                startAction("Connecting GitHub repository", [projectPath, remoteUrl]() {
                    ProjectGitResult probe = RunProjectGitCommand(projectPath, "remote get-url origin");
                    return RunProjectGitCommand(
                        projectPath,
                        std::string("remote ") + (probe.success ? "set-url origin " : "add origin ") +
                            QuoteProjectGitArgument(remoteUrl));
                });
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::Checkbox("Private", &gitUi.privateRepository);
            ImGui::SameLine();
            ImGui::BeginDisabled(gitUi.busy || gitUi.provider == 2);
            const char* createLabel = gitUi.provider == 1
                ? "Create New GitLab Repository"
                : "Create New GitHub Repository";
            if (ImGui::Button(createLabel)) {
                const std::string repositoryName = selectedProject.name;
                const bool privateRepository = gitUi.privateRepository;
                const int provider = gitUi.provider;
                startAction("Publishing repository", [projectPath, repositoryName, privateRepository, provider]() {
                    WriteModularityGitIgnore(projectPath);
                    ProjectGitResult init = RunProjectGitCommand(projectPath, "rev-parse --git-dir");
                    if (!init.success) {
                        init = RunProjectGitCommand(projectPath, "init -b main");
                        if (!init.success) return init;
                    }
                    ProjectGitResult add = RunProjectGitCommand(projectPath, "add -A");
                    if (!add.success) return add;
                    ProjectGitResult commit = RunProjectGitCommand(
                        projectPath, "commit -m " + QuoteProjectGitArgument("Initial Modularity project"));
                    if (!commit.success && commit.output.find("nothing to commit") == std::string::npos)
                        return commit;
                    return provider == 0
                        ? RunProjectGitHubPublish(projectPath, repositoryName, privateRepository)
                        : RunProjectGitLabPublish(projectPath, repositoryName, privateRepository);
                });
            }
            ImGui::EndDisabled();

            ImGui::SeparatorText("Changes");
            ImGui::SetNextItemWidth(std::min(520.0f * uiScale, ImGui::GetContentRegionAvail().x));
            ImGui::InputTextWithHint("##HubGitCommit", "Commit message",
                                     gitUi.commitMessage, sizeof(gitUi.commitMessage));
            ImGui::BeginDisabled(gitUi.busy);
            if (ImGui::Button("Stage All")) {
                startAction("Staging all project files", [projectPath]() {
                    WriteModularityGitIgnore(projectPath);
                    return RunProjectGitCommand(projectPath, "add .");
                });
            }
            ImGui::SameLine();
            if (ImGui::Button("Stage Tracked")) {
                startAction("Staging tracked files", [projectPath]() {
                    return RunProjectGitCommand(projectPath, "add -u");
                });
            }
            ImGui::SameLine();
            if (ImGui::Button("Unstage All")) {
                startAction("Unstaging files", [projectPath]() {
                    ProjectGitResult result = RunProjectGitCommand(projectPath, "restore --staged .");
                    if (!result.success) result = RunProjectGitCommand(projectPath, "reset");
                    return result;
                });
            }
            ImGui::EndDisabled();

            ImGui::BeginDisabled(gitUi.busy || gitUi.commitMessage[0] == '\0');
            if (ImGui::Button("Commit Staged")) {
                const std::string message = gitUi.commitMessage;
                startAction("Committing project", [projectPath, message]() {
                    return RunProjectGitCommand(projectPath,
                                                "commit -m " + QuoteProjectGitArgument(message));
                });
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(gitUi.busy);
            if (ImGui::Button("Fetch")) {
                startAction("Fetching remote changes", [projectPath]() {
                    return RunProjectGitCommand(projectPath, "fetch --prune origin");
                });
            }
            ImGui::SameLine();
            if (ImGui::Button("Pull")) {
                startAction("Pulling remote changes", [projectPath]() {
                    return RunProjectGitCommand(projectPath, "pull --ff-only");
                });
            }
            ImGui::SameLine();
            if (ImGui::Button("Push")) {
                startAction("Pushing project", [projectPath]() {
                    return RunProjectGitCommand(projectPath, "push -u origin HEAD");
                });
            }
            ImGui::EndDisabled();

            ImGui::Dummy(ImVec2(0.0f, 8.0f * uiScale));
            ImGui::BeginChild("HubGitOutput", ImVec2(0.0f, 150.0f * uiScale), true);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(gitUi.status.c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndChild();
            ImGui::TextDisabled(
                gitUi.provider == 0 ? "GitHub creation uses gh."
                : gitUi.provider == 1 ? "GitLab creation uses glab."
                : "Other hosts work through a repository clone URL; create the empty repository on that host first.");
        };

        // Section dispatch (with cross-fade between Projects list and New Project)
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
        } else if (launcherSection == 2) {
            renderSettingsView();
        } else {
            renderGitHubView();
        }

        ImGui::EndChild();      // LauncherActiveSection
        ImGui::PopStyleColor(); // transparent ChildBg (covered section + subpanels)
        ImGui::PopStyleVar();   // section alpha
        ImGui::PopStyleVar();   // content alpha

        if (!launcherIntroFinished) {
            ImGui::PopItemFlag();
        }
        ImGui::SetWindowFontScale(1.0f);

        // intro overlay: logo pops in at center then drifts to the header. the static header brand
        // only fades in after the drift so there's never two "Modularity"s on screen.
        if (!launcherIntroFinished && introState.textAlpha > 0.001f) {
            ImDrawList* overlay = ImGui::GetForegroundDrawList(ImGui::GetMainViewport());
            const char* title = "Modularity";
            ImFont* font = ImGui::GetFont();
            const float baseFs = ImGui::GetFontSize();
            const float centerFontSize = baseFs * 2.62f;
            const float headerFontSize = titleFontSize;
            const ImVec4 baseCol = ImVec4(0.95f, 0.96f, 0.98f, 1.0f);

            // Measure widths at both sizes
            float centerTotalWidth = 0.0f;
            float headerTotalWidth = 0.0f;
            for (const char* c = title; *c; ++c) {
                char letter[2] = { *c, 0 };
                centerTotalWidth += font->CalcTextSizeA(centerFontSize, FLT_MAX, 0.0f, letter).x;
                headerTotalWidth += font->CalcTextSizeA(headerFontSize, FLT_MAX, 0.0f, letter).x;
            }

            // Center layout: logo sits to the left of the text, group is centered.
            const float centerLogoSize = centerFontSize * 1.18f;
            const float centerLogoGap  = 18.0f * uiScale;
            const float centerGroupW   = centerLogoSize + centerLogoGap + centerTotalWidth;
            const ImVec2 centerOrigin(displaySize.x * 0.5f - centerGroupW * 0.5f,
                                      displaySize.y * 0.40f);
            const ImVec2 centerLogoMin(centerOrigin.x,
                                       centerOrigin.y + (centerFontSize - centerLogoSize) * 0.5f);
            const ImVec2 centerTextPos (centerOrigin.x + centerLogoSize + centerLogoGap,
                                        centerOrigin.y);

            // Logo: fades in during fadeIn phase (before letters pop in), then drifts to header pos.
            const float logoFadeT = ImClamp(introState.elapsed / std::max(0.0001f, introTimings.fadeIn), 0.0f, 1.0f);
            const float logoFadeAlpha = EaseOutCubic(logoFadeT);
            const float driftEase = introState.driftEase;
            if (logoTexId != static_cast<ImTextureID>(0)) {
                const float logoSize = ImLerp(centerLogoSize, headerLogoSize, driftEase);
                const ImVec2 lMin(
                    ImLerp(centerLogoMin.x, headerLogoPos.x, driftEase),
                    ImLerp(centerLogoMin.y, headerLogoPos.y, driftEase));
                const ImVec2 lMax(lMin.x + logoSize, lMin.y + logoSize);
                const float lAlpha = logoFadeAlpha * ImLerp(1.0f, 1.0f, driftEase);
                overlay->AddImage(logoTexId, lMin, lMax, logoUvMin, logoUvMax,
                                  ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, lAlpha)));
            }

            // Letters: each pops in beside the logo with a slight delay, then drifts to header text.
            float centerAdvanceX = 0.0f;
            float headerAdvanceX = 0.0f;
            int index = 0;
            const int totalLetters = static_cast<int>(std::strlen(title));
            const float popDelayStep = 0.055f;
            const float driftDelayStep = 0.022f;
            for (const char* c = title; *c; ++c, ++index) {
                char letter[2] = { *c, 0 };
                const float popDelay = (totalLetters > 1) ? (popDelayStep * (float)index) : 0.0f;
                const float popInput = ImClamp(
                    (introState.elapsed - introTimings.fadeIn - popDelay) / std::max(0.0001f, introTimings.popIn),
                    0.0f, 1.0f);
                const float popEase  = EaseOutBack(popInput);
                const float popAlpha = EaseOutCubic(popInput);

                const float driftDelay = (totalLetters > 1) ? (driftDelayStep * (float)index) : 0.0f;
                const float driftInput = ImClamp(
                    (introState.driftT - driftDelay) / std::max(0.0001f, 1.0f - driftDelay),
                    0.0f, 1.0f);
                const float driftL = EaseInOutCubic(driftInput);

                const float centerWidth = font->CalcTextSizeA(centerFontSize, FLT_MAX, 0.0f, letter).x;
                const float headerWidth = font->CalcTextSizeA(headerFontSize, FLT_MAX, 0.0f, letter).x;
                const ImVec2 centerLetterPos(centerTextPos.x + centerAdvanceX, centerTextPos.y);
                const ImVec2 headerLetterPos(headerTextPos.x + headerAdvanceX, headerTextPos.y);

                const float popScale = std::max(0.05f, 0.34f + 0.66f * popEase);
                const float popSize  = centerFontSize * popScale;
                const ImVec2 spawnPos(centerLetterPos.x, centerLetterPos.y + (1.0f - popAlpha) * 22.0f * uiScale);
                const ImVec2 popPos(
                    ImLerp(spawnPos.x, centerLetterPos.x, popAlpha),
                    ImLerp(spawnPos.y, centerLetterPos.y, popAlpha));

                const float letterSize = ImLerp(popSize, headerFontSize, driftL);
                const ImVec2 letterPos(
                    ImLerp(popPos.x, headerLetterPos.x, driftL),
                    ImLerp(popPos.y, headerLetterPos.y, driftL));
                const float letterAlpha = introState.textAlpha * popAlpha * ImLerp(0.92f, 1.0f, driftL);
                const ImU32 textCol = ImGui::GetColorU32(ImVec4(baseCol.x, baseCol.y, baseCol.z, letterAlpha));
                overlay->AddText(font, letterSize, letterPos, textCol, letter);

                centerAdvanceX += centerWidth;
                headerAdvanceX += headerWidth;
            }
        }

        // project-open zoom overlay, drawn LAST so it sits on top of everything. the clicked card's
        // preview expands to fill the screen, then HOLDS at fullscreen while the load finishes.
        // that hold is what makes it feel like a transition instead of a snap-back.
        if (loadingScreenT > 0.0001f) {
            ImVec2 focus = launcherTransitionFocus;
            if (focus.x <= 0.0f && focus.y <= 0.0f) {
                focus = ImVec2(windowPos.x + windowSize.x * 0.5f, windowPos.y + windowSize.y * 0.5f);
            }

            ImTextureID zoomTexId = static_cast<ImTextureID>(0);
            int zoomTexW = 0;
            int zoomTexH = 0;
            if (!launcherLoadingPreviewPath.empty()) {
                const fs::path previewP = launcherLoadingPreviewPath;
                if (fs::exists(previewP)) {
                    if (usingVulkan() && vulkanRendererInitialized && vulkanRenderer) {
                        zoomTexId = vulkanRenderer->getOrCreateUIImage(previewP.string(),
                                                                       &zoomTexW, &zoomTexH);
                    } else if (!usingVulkan()) {
                        if (Texture* tex = renderer.getTexture(previewP.string())) {
                            zoomTexId = (ImTextureID)(intptr_t)tex->GetID();
                            zoomTexW = tex->GetWidth();
                            zoomTexH = tex->GetHeight();
                        }
                    }
                }
            }

            // start at the clicked card's footprint; target size is measured to the furthest screen
            // edge so the rect always covers the whole window.
            const float cardW0 = 1100.0f * uiScale;
            const float cardH0 = 92.0f   * uiScale;
            const float distLeft   = focus.x - windowPos.x;
            const float distRight  = (windowPos.x + windowSize.x) - focus.x;
            const float distTop    = focus.y - windowPos.y;
            const float distBottom = (windowPos.y + windowSize.y) - focus.y;
            const float targetHalfW = std::max(distLeft, distRight) + 120.0f * uiScale;
            const float targetHalfH = std::max(distTop, distBottom)  + 120.0f * uiScale;
            const float curW = ImLerp(cardW0, targetHalfW * 2.0f, loadingScreenT);
            const float curH = ImLerp(cardH0, targetHalfH * 2.0f, loadingScreenT);
            const ImVec2 zoomMin(focus.x - curW * 0.5f, focus.y - curH * 0.5f);
            const ImVec2 zoomMax(focus.x + curW * 0.5f, focus.y + curH * 0.5f);
            const float radius    = ImLerp(11.0f, 0.0f, loadingScreenT) * uiScale;
            const float coreAlpha = ImLerp(0.0f, 1.0f, loadingScreenT);

            if (zoomTexId != static_cast<ImTextureID>(0)) {
                DrawImageCover(drawList, zoomTexId,
                               zoomMin, zoomMax,
                               zoomTexW, zoomTexH,
                               ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, coreAlpha)),
                               radius);
                // Dark scrim ramps up as it covers the screen so it transitions into
                // the loading backdrop instead of just sitting as a bright thumbnail.
                const float scrimAlpha = ImLerp(0.0f, 0.62f, loadingScreenT);
                drawList->AddRectFilled(zoomMin, zoomMax,
                                        ImGui::GetColorU32(ImVec4(0.04f, 0.05f, 0.08f, scrimAlpha)),
                                        radius);
            } else {
                drawList->AddRectFilled(zoomMin, zoomMax,
                                        ImGui::GetColorU32(ImVec4(0.05f, 0.07f, 0.10f, coreAlpha)),
                                        radius);
            }
            // Thin accent border that fades quickly so the rect dissolves into the bg.
            const float borderAlpha = ImLerp(0.85f, 0.0f, ImClamp(loadingScreenT * 1.6f, 0.0f, 1.0f));
            if (borderAlpha > 0.001f) {
                drawList->AddRect(zoomMin, zoomMax,
                                  ImGui::GetColorU32(ImVec4(0.42f, 0.72f, 1.0f, borderAlpha)),
                                  radius, 0, 1.8f * uiScale);
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
        const float elapsed = static_cast<float>(glfwGetTime() - projectLoadStartTime);
        // Wait for the zoom to have made meaningful progress before fading the loading
        // card in, so we don't get a snappy double-overlay during the first frames.
        const float introT  = ImClamp((elapsed - 0.32f) / 0.32f, 0.0f, 1.0f);
        const float introEase = EaseOutCubic(introT);

        // one-shot wind ambience if the load takes long enough. layered through the one-shot slot
        // so it doesn't clobber the intro/click sounds, and the flag stops per-frame re-triggers.
        if (!launcherWindSoundActive && elapsed > 1.4f) {
            if (playEditorFeedbackOneShot("Resources/Sounds/Wind Sound Load Project.mp3",
                                          0.55f, EditorFeedbackSoundCategory::Boot)) {
                launcherWindSoundActive = true;
            }
        }

        ImGuiIO& io2 = ImGui::GetIO();
        ImGuiViewport* overlayViewport = ImGui::GetMainViewport();
        const ImVec2 overlayPos = overlayViewport ? overlayViewport->Pos : ImVec2(0.0f, 0.0f);
        const ImVec2 overlaySize = overlayViewport ? overlayViewport->Size : io2.DisplaySize;
        if (overlayViewport) {
            ImGui::SetNextWindowViewport(overlayViewport->ID);
        }
        ImGui::SetNextWindowPos(overlayPos);
        ImGui::SetNextWindowSize(overlaySize);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.06f, 0.08f, 0.42f * introEase));
        ImGui::Begin("ProjectLoadOverlay", nullptr,
                     ImGuiWindowFlags_NoTitleBar |
                     ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoDocking |
                     ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoInputs);
        ImGui::End();
        ImGui::PopStyleColor();

        ImVec2 center = overlayViewport ? overlayViewport->GetCenter()
                                        : ImVec2(io2.DisplaySize.x * 0.5f, io2.DisplaySize.y * 0.5f);
        if (overlayViewport) {
            ImGui::SetNextWindowViewport(overlayViewport->ID);
        }
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(480.0f, 184.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, introEase);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 18.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28.0f, 24.0f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.105f, 0.118f, 0.155f, 0.97f));
        ImGui::Begin("ProjectLoadCard", nullptr,
                     ImGuiWindowFlags_NoTitleBar |
                     ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoDocking |
                     ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoSavedSettings);

        std::string headline;
        if (sceneLoadInProgress) {
            headline = "Loading scene...";
        } else if (!launcherTransitionProjectName.empty()) {
            headline = "Loading " + launcherTransitionProjectName + "...";
        } else {
            headline = "Loading project...";
        }
        ImGui::TextColored(ImVec4(0.94f, 0.96f, 1.0f, 1.0f), "%s", headline.c_str());
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
        ImGui::PopStyleVar(3);
    } else if (launcherWindSoundActive) {
        launcherWindSoundActive = false;
    }
}
#pragma endregion

#pragma region New Project Dialog
void Engine::renderNewProjectDialog() {
    ImGuiIO& io = ImGui::GetIO();
    ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    ImVec2 center = mainViewport ? mainViewport->GetCenter()
                                 : ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);

    if (mainViewport) {
        ImGui::SetNextWindowViewport(mainViewport->ID);
    }
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(1040, 720), ImGuiCond_Appearing);

    if (ImGui::Begin("New Project", &projectManager.showNewProjectDialog,
                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings)) {
        static int templateCategory = 0; // 0 = Blank Project, 1 = Template Projects
        static char templateSearch[128] = "";
        const std::vector<LauncherTemplateEntry> templates = GatherTemplateEntries();
        const std::vector<LauncherTemplateEntry> builtInPresets = BuiltInProjectPresets();
        const fs::path templatesRoot = GetTemplateProjectsRoot();
        const std::string templateFilter = TrimCopy(templateSearch);

        LauncherTemplateEntry blankEntry = builtInPresets.empty() ? LauncherTemplateEntry{} : builtInPresets.front();

        auto selectTemplateEntry = [&](const LauncherTemplateEntry& entry) {
            if (entry.isBlankPreset) {
                projectManager.newProjectTemplatePath.clear();
                projectManager.newProjectTemplateName = entry.displayName;
                projectManager.newProjectPresetId = entry.presetId.empty() ? "empty" : entry.presetId;
            } else {
                projectManager.newProjectTemplatePath = entry.projectRoot.string();
                projectManager.newProjectTemplateName = entry.displayName;
                projectManager.newProjectPresetId.clear();
            }
            projectManager.newProjectPipelineMode = ProjectPipelineToUiIndex(entry.pipeline);
            projectManager.newProjectRendererMode = (entry.rendererBackend == Modularity::GraphicsBackend::Vulkan) ? 1 : 0;
#if !MODULARITY_HAS_VULKAN
            if (projectManager.newProjectRendererMode == 1) {
                projectManager.newProjectRendererMode = 0;
            }
#else
            if (projectManager.newProjectRendererMode == 1 && !hasVulkanPipelinePackage()) {
                projectManager.newProjectRendererMode = 0;
            }
#endif
            projectManager.newProjectImportLastPackages = false;
        };

        if (projectManager.newProjectTemplateName.empty()) {
            projectManager.newProjectTemplateName = blankEntry.displayName;
            projectManager.newProjectPresetId = blankEntry.presetId;
        }

        const LauncherTemplateEntry* selectedTemplate = nullptr;
        if (projectManager.newProjectTemplatePath.empty()) {
            selectedTemplate = &blankEntry;
            for (const auto& preset : builtInPresets) {
                if (preset.presetId == projectManager.newProjectPresetId ||
                    preset.displayName == projectManager.newProjectTemplateName) {
                    selectedTemplate = &preset;
                    break;
                }
            }
        } else {
            for (const auto& t : templates) {
                if (fs::path(projectManager.newProjectTemplatePath) == t.projectRoot) {
                    selectedTemplate = &t;
                    break;
                }
            }
        }
        if (!selectedTemplate) {
            projectManager.newProjectTemplatePath.clear();
            projectManager.newProjectTemplateName = blankEntry.displayName;
            projectManager.newProjectPresetId = blankEntry.presetId;
            selectedTemplate = &blankEntry;
        }
        projectManager.newProjectPipelineMode = ProjectPipelineToUiIndex(selectedTemplate->pipeline);
        projectManager.newProjectRendererMode = (selectedTemplate->rendererBackend == Modularity::GraphicsBackend::Vulkan) ? 1 : 0;
        projectManager.newProjectImportLastPackages = false;
#if !MODULARITY_HAS_VULKAN
        if (projectManager.newProjectRendererMode == 1) {
            projectManager.newProjectRendererMode = 0;
        }
#else
        if (projectManager.newProjectRendererMode == 1 && !hasVulkanPipelinePackage()) {
            projectManager.newProjectRendererMode = 0;
        }
#endif

        auto resolvePreviewTexture = [&](const LauncherTemplateEntry& entry, ImTextureID& outId, int& outW, int& outH) {
            outId = static_cast<ImTextureID>(0);
            outW = 0;
            outH = 0;
            if (entry.previewImage.empty() || !fs::exists(entry.previewImage)) {
                return;
            }
            if (usingVulkan() && vulkanRendererInitialized && vulkanRenderer) {
                outId = vulkanRenderer->getOrCreateUIImage(entry.previewImage.string(), &outW, &outH);
            } else if (!usingVulkan()) {
                if (Texture* templateTex = renderer.getTexture(entry.previewImage.string())) {
                    outId = (ImTextureID)(intptr_t)templateTex->GetID();
                    outW = templateTex->GetWidth();
                    outH = templateTex->GetHeight();
                }
            }
        };

        auto createProjectFromCurrentState = [&]() {
            if (std::strlen(projectManager.newProjectName) == 0) {
                projectManager.errorMessage = "Please enter a project name";
            } else if (std::strlen(projectManager.newProjectLocation) == 0) {
                projectManager.errorMessage = "Please specify a location";
            } else {
                createNewProject(projectManager.newProjectName, projectManager.newProjectLocation);
                if (projectManager.errorMessage.empty()) {
                    projectManager.showNewProjectDialog = false;
                }
            }
        };

        auto renderCategoryButton = [&](const char* label, int category, float width) {
            const bool selected = templateCategory == category;
            ImGui::PushStyleColor(ImGuiCol_Button, selected ? ImVec4(0.20f, 0.31f, 0.46f, 1.0f)
                                                            : ImVec4(0.13f, 0.16f, 0.22f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, selected ? ImVec4(0.24f, 0.36f, 0.53f, 1.0f)
                                                                   : ImVec4(0.18f, 0.22f, 0.30f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.21f, 0.33f, 0.49f, 1.0f));
            if (ImGui::Button(label, ImVec2(width, 0.0f))) {
                templateCategory = category;
                if (templateCategory == 0) {
                    selectTemplateEntry(blankEntry);
                    selectedTemplate = &blankEntry;
                } else if (!templates.empty() && selectedTemplate && selectedTemplate->isBlankPreset) {
                    selectTemplateEntry(templates.front());
                    selectedTemplate = &templates.front();
                }
            }
            ImGui::PopStyleColor(3);
        };

        auto renderTemplateListRow = [&](const LauncherTemplateEntry& entry) {
            const bool selected = selectedTemplate &&
                ((entry.isBlankPreset && selectedTemplate->isBlankPreset &&
                  entry.presetId == selectedTemplate->presetId) ||
                 (!entry.isBlankPreset && !selectedTemplate->isBlankPreset &&
                  selectedTemplate->projectRoot == entry.projectRoot));

            ImGui::PushID(entry.isBlankPreset ? "NewProjectBlankTemplate" : entry.projectRoot.string().c_str());
            const ImVec2 rowPos = ImGui::GetCursorScreenPos();
            const ImVec2 rowSize(ImGui::GetContentRegionAvail().x, 64.0f);
            ImGui::InvisibleButton("TemplateListRow", rowSize);
            const bool hovered = ImGui::IsItemHovered();
            if (ImGui::IsItemClicked()) {
                selectTemplateEntry(entry);
                selectedTemplate = &entry;
            }

            ImDrawList* list = ImGui::GetWindowDrawList();
            const ImVec2 rowMax(rowPos.x + rowSize.x, rowPos.y + rowSize.y);
            if (hovered || selected) {
                list->AddRectFilled(rowPos,
                                    rowMax,
                                    ImGui::GetColorU32(selected ? ImVec4(0.14f, 0.19f, 0.27f, 0.96f)
                                                                : ImVec4(0.11f, 0.14f, 0.19f, 0.82f)),
                                    4.0f);
            }
            list->AddRectFilled(ImVec2(rowPos.x, rowPos.y + 6.0f),
                                ImVec2(rowPos.x + (selected ? 3.0f : 2.0f), rowMax.y - 6.0f),
                                ImGui::GetColorU32(selected ? ImVec4(0.42f, 0.72f, 1.0f, 1.0f)
                                                            : ImVec4(0.27f, 0.38f, 0.52f, hovered ? 0.65f : 0.0f)),
                                2.0f);
            list->AddLine(ImVec2(rowPos.x, rowMax.y),
                          ImVec2(rowMax.x, rowMax.y),
                          ImGui::GetColorU32(ImVec4(0.18f, 0.22f, 0.30f, 1.0f)),
                          1.0f);

            const ImVec2 thumbMin(rowPos.x + 10.0f, rowPos.y + 8.0f);
            const ImVec2 thumbMax(thumbMin.x + 82.0f, rowMax.y - 8.0f);
            ImTextureID textureId = static_cast<ImTextureID>(0);
            int textureW = 0;
            int textureH = 0;
            resolvePreviewTexture(entry, textureId, textureW, textureH);
            if (textureId != static_cast<ImTextureID>(0)) {
                DrawImageCover(list, textureId, thumbMin, thumbMax, textureW, textureH, ImGui::GetColorU32(ImVec4(1, 1, 1, 1)), 4.0f);
            } else {
                list->AddRectFilled(thumbMin, thumbMax, ImGui::GetColorU32(ImVec4(0.08f, 0.10f, 0.14f, 1.0f)), 4.0f);
                const char* placeholder = entry.isBlankPreset ? "Default" : "Preview";
                const ImVec2 textSize = ImGui::CalcTextSize(placeholder);
                list->AddText(ImVec2(thumbMin.x + ((thumbMax.x - thumbMin.x) - textSize.x) * 0.5f,
                                     thumbMin.y + ((thumbMax.y - thumbMin.y) - textSize.y) * 0.5f),
                              ImGui::GetColorU32(ImVec4(0.63f, 0.69f, 0.77f, 1.0f)),
                              placeholder);
            }

            ImGui::SetCursorScreenPos(ImVec2(thumbMax.x + 12.0f, rowPos.y + 10.0f));
            ImGui::TextColored(ImVec4(0.93f, 0.96f, 1.0f, 1.0f), "%s", entry.displayName.c_str());
            ImGui::SetCursorScreenPos(ImVec2(thumbMax.x + 12.0f, rowPos.y + 30.0f));
            ImGui::TextColored(ImVec4(0.61f, 0.68f, 0.77f, 1.0f), "%s", ProjectPipelineLabel(entry.pipeline));
            ImGui::Dummy(ImVec2(rowSize.x, 2.0f));
            ImGui::PopID();
        };

        static int templateViewMode = 1; // 0 = Grid, 1 = List

        auto renderTemplateViewButton = [&](const char* label, int mode, float width) {
            const bool selected = templateViewMode == mode;
            ImGui::PushStyleColor(ImGuiCol_Button, selected ? ImVec4(0.20f, 0.31f, 0.46f, 1.0f)
                                                            : ImVec4(0.13f, 0.16f, 0.22f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, selected ? ImVec4(0.24f, 0.36f, 0.53f, 1.0f)
                                                                   : ImVec4(0.18f, 0.22f, 0.30f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.21f, 0.33f, 0.49f, 1.0f));
            if (ImGui::Button(label, ImVec2(width, 0.0f))) {
                templateViewMode = mode;
            }
            ImGui::PopStyleColor(3);
        };

        auto renderTemplateGridTile = [&](const LauncherTemplateEntry& entry, float tileWidth) {
            const bool selected = selectedTemplate &&
                ((entry.isBlankPreset && selectedTemplate->isBlankPreset &&
                  entry.presetId == selectedTemplate->presetId) ||
                 (!entry.isBlankPreset && !selectedTemplate->isBlankPreset &&
                  selectedTemplate->projectRoot == entry.projectRoot));

            ImGui::PushID(entry.isBlankPreset ? "NewProjectBlankTemplateGrid" : entry.projectRoot.string().c_str());
            const float tileHeight = 114.0f;
            const float previewHeight = 64.0f;
            const ImVec2 tilePos = ImGui::GetCursorScreenPos();
            const ImVec2 tileSize(tileWidth, tileHeight);
            ImGui::InvisibleButton("TemplateGridTile", tileSize);
            const bool hovered = ImGui::IsItemHovered();
            if (ImGui::IsItemClicked()) {
                selectTemplateEntry(entry);
                selectedTemplate = &entry;
            }

            ImDrawList* list = ImGui::GetWindowDrawList();
            const ImVec2 tileMax(tilePos.x + tileSize.x, tilePos.y + tileSize.y);
            const ImVec2 thumbMin(tilePos.x, tilePos.y);
            const ImVec2 thumbMax(tileMax.x, tilePos.y + previewHeight);
            list->AddRectFilled(tilePos,
                                tileMax,
                                ImGui::GetColorU32(selected ? ImVec4(0.13f, 0.18f, 0.25f, 0.96f)
                                                            : ImVec4(0.10f, 0.13f, 0.18f, hovered ? 0.90f : 0.78f)),
                                3.0f);
            list->AddRect(tilePos,
                          tileMax,
                          ImGui::GetColorU32(selected ? ImVec4(0.39f, 0.66f, 0.97f, 1.0f)
                                                      : ImVec4(0.20f, 0.25f, 0.33f, hovered ? 1.0f : 0.85f)),
                          3.0f);
            if (selected || hovered) {
                list->AddRectFilled(ImVec2(tilePos.x, tilePos.y),
                                    ImVec2(tilePos.x + tileSize.x, tilePos.y + 2.0f),
                                    ImGui::GetColorU32(selected ? ImVec4(0.42f, 0.72f, 1.0f, 1.0f)
                                                                : ImVec4(0.28f, 0.42f, 0.60f, 0.85f)));
            }

            ImTextureID textureId = static_cast<ImTextureID>(0);
            int textureW = 0;
            int textureH = 0;
            resolvePreviewTexture(entry, textureId, textureW, textureH);
            if (textureId != static_cast<ImTextureID>(0)) {
                DrawImageCover(list, textureId, thumbMin, thumbMax, textureW, textureH, ImGui::GetColorU32(ImVec4(1, 1, 1, 1)), 3.0f);
            } else {
                list->AddRectFilled(thumbMin, thumbMax, ImGui::GetColorU32(ImVec4(0.08f, 0.10f, 0.14f, 1.0f)), 3.0f);
                const char* placeholder = entry.isBlankPreset ? "Default" : "Preview";
                const ImVec2 textSize = ImGui::CalcTextSize(placeholder);
                list->AddText(ImVec2(thumbMin.x + ((thumbMax.x - thumbMin.x) - textSize.x) * 0.5f,
                                     thumbMin.y + ((thumbMax.y - thumbMin.y) - textSize.y) * 0.5f),
                              ImGui::GetColorU32(ImVec4(0.63f, 0.69f, 0.77f, 1.0f)),
                              placeholder);
            }

            ImGui::SetCursorScreenPos(ImVec2(tilePos.x + 8.0f, thumbMax.y + 8.0f));
            ImGui::PushTextWrapPos(tilePos.x + tileSize.x - 8.0f);
            ImGui::TextColored(ImVec4(0.93f, 0.96f, 1.0f, 1.0f), "%s", entry.displayName.c_str());
            ImGui::PopTextWrapPos();
            ImGui::SetCursorScreenPos(ImVec2(tilePos.x + 8.0f, tileMax.y - 22.0f));
            ImGui::TextColored(ImVec4(0.61f, 0.68f, 0.77f, 1.0f), "%s", ProjectPipelineLabel(entry.pipeline));
            ImGui::PopID();
        };

        ImGui::TextColored(ImVec4(0.94f, 0.97f, 1.0f, 1.0f), "Create New Project");
        ImGui::TextColored(ImVec4(0.59f, 0.66f, 0.75f, 1.0f),
                           "Choose a base project, review the preview, then configure the project details below.");
        ImGui::Dummy(ImVec2(0.0f, 8.0f));

        renderCategoryButton("Defaults", 0, 130.0f);
        ImGui::SameLine();
        renderCategoryButton("Template Projects", 1, 164.0f);
        ImGui::Dummy(ImVec2(0.0f, 8.0f));

        const float splitHeight = std::max(340.0f, ImGui::GetContentRegionAvail().y);
        const float leftWidth = ImClamp(ImGui::GetContentRegionAvail().x * 0.56f, 400.0f, 620.0f);

        ImGui::BeginChild("TemplateSelectionPane", ImVec2(leftWidth, splitHeight), false);
        ImGui::TextColored(ImVec4(0.89f, 0.93f, 0.98f, 1.0f), "Templates");
        const float toggleWidth = 112.0f;
        ImGui::SameLine();
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),
                                      ImGui::GetWindowWidth() - toggleWidth - ImGui::GetStyle().WindowPadding.x));
        renderTemplateViewButton("Grid", 0, 52.0f);
        ImGui::SameLine(0.0f, 4.0f);
        renderTemplateViewButton("List", 1, 52.0f);
        if (templateCategory == 1) {
            ImGui::TextDisabled("Source: %s", templatesRoot.string().c_str());
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##DialogTemplateSearch", "Search templates", templateSearch, sizeof(templateSearch));
        } else {
            ImGui::TextDisabled("Start from an empty project layout.");
        }
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 4.0f));

        ImGui::BeginChild("TemplateSelectionRows", ImVec2(0.0f, 0.0f), false);
        if (templateCategory == 0) {
            if (templateViewMode == 0) {
                int tileIndex = 0;
                const float tileSpacing = 8.0f;
                const float contentWidth = ImGui::GetContentRegionAvail().x;
                const float minTileWidth = 146.0f;
                const int tileColumns = std::max(1, static_cast<int>((contentWidth + tileSpacing) / (minTileWidth + tileSpacing)));
                const float tileWidth = (contentWidth - tileSpacing * static_cast<float>(tileColumns - 1)) / static_cast<float>(tileColumns);
                for (const auto& preset : builtInPresets) {
                    if (tileIndex > 0 && (tileIndex % tileColumns) != 0) {
                        ImGui::SameLine(0.0f, tileSpacing);
                    }
                    renderTemplateGridTile(preset, tileWidth);
                    ++tileIndex;
                }
            } else {
                for (const auto& preset : builtInPresets) {
                    renderTemplateListRow(preset);
                }
            }
        } else {
            bool hadVisible = false;
            int tileIndex = 0;
            const float tileSpacing = 8.0f;
            const float contentWidth = ImGui::GetContentRegionAvail().x;
            const float minTileWidth = 146.0f;
            const int tileColumns = std::max(1, static_cast<int>((contentWidth + tileSpacing) / (minTileWidth + tileSpacing)));
            const float tileWidth = (contentWidth - tileSpacing * static_cast<float>(tileColumns - 1)) / static_cast<float>(tileColumns);
            for (const auto& t : templates) {
                const std::string lowered = TrimCopy(t.displayName);
                std::string loweredSearch = lowered;
                std::transform(loweredSearch.begin(), loweredSearch.end(), loweredSearch.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
                std::string searchLower = templateFilter;
                std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
                if (!searchLower.empty() && loweredSearch.find(searchLower) == std::string::npos) {
                    continue;
                }
                if (templateViewMode == 0) {
                    if (tileIndex > 0 && (tileIndex % tileColumns) != 0) {
                        ImGui::SameLine(0.0f, tileSpacing);
                    }
                    renderTemplateGridTile(t, tileWidth);
                    ++tileIndex;
                } else {
                    renderTemplateListRow(t);
                }
                hadVisible = true;
            }
            if (!hadVisible) {
                if (templates.empty()) {
                    ImGui::TextDisabled("No templates found.");
                    ImGui::TextDisabled("Add folders with project.modu to Template-Projects.");
                } else {
                    ImGui::TextDisabled("No templates match your search.");
                }
            }
        }
        ImGui::EndChild();
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("TemplatePreviewPane", ImVec2(0.0f, splitHeight), false);
        ImGui::TextColored(ImVec4(0.89f, 0.93f, 0.98f, 1.0f), "Preview");
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        {
            const float previewHeight = std::min(162.0f, ImGui::GetContentRegionAvail().y * 0.26f);
            const ImVec2 previewPos = ImGui::GetCursorScreenPos();
            const ImVec2 previewSize(ImGui::GetContentRegionAvail().x, previewHeight);
            const ImVec2 previewMax(previewPos.x + previewSize.x, previewPos.y + previewSize.y);
            ImDrawList* list = ImGui::GetWindowDrawList();
            list->AddRectFilled(previewPos, previewMax, ImGui::GetColorU32(ImVec4(0.08f, 0.10f, 0.14f, 1.0f)), 3.0f);
            list->AddRect(previewPos, previewMax, ImGui::GetColorU32(ImVec4(0.22f, 0.27f, 0.35f, 1.0f)), 3.0f);

            ImTextureID previewTexId = static_cast<ImTextureID>(0);
            int previewTexWidth = 0;
            int previewTexHeight = 0;
            resolvePreviewTexture(*selectedTemplate, previewTexId, previewTexWidth, previewTexHeight);
            if (previewTexId != static_cast<ImTextureID>(0)) {
                DrawImageCover(list, previewTexId, previewPos, previewMax, previewTexWidth, previewTexHeight, ImGui::GetColorU32(ImVec4(1, 1, 1, 1)), 3.0f);
            } else {
                const char* placeholder = selectedTemplate->isBlankPreset ? "Standard 3D Project" : "No Preview Available";
                const ImVec2 textSize = ImGui::CalcTextSize(placeholder);
                list->AddText(ImVec2(previewPos.x + (previewSize.x - textSize.x) * 0.5f,
                                     previewPos.y + (previewSize.y - textSize.y) * 0.5f),
                              ImGui::GetColorU32(ImVec4(0.66f, 0.72f, 0.80f, 1.0f)),
                              placeholder);
            }
            ImGui::Dummy(previewSize);
        }

            const std::string selectedDescription = selectedTemplate->isBlankPreset
                ? (selectedTemplate->description.empty() ? "Creates a clean Modularity project with the standard folders and scripting setup." : selectedTemplate->description)
                : "Copies the selected template project into your new project folder.";
        const std::string sourceLabel = selectedTemplate->isBlankPreset
            ? "Built-in preset"
            : selectedTemplate->projectRoot.filename().string();
        const std::string summaryLabel = std::string(ProjectPipelineLabel(ProjectPipelineFromUiIndex(projectManager.newProjectPipelineMode))) +
            "  •  " +
            Modularity::ToString(projectManager.newProjectRendererMode == 1 ? Modularity::GraphicsBackend::Vulkan
                                                                            : Modularity::GraphicsBackend::OpenGL) +
            "  •  " + sourceLabel;

        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        ImGui::TextColored(ImVec4(0.94f, 0.97f, 1.0f, 1.0f), "%s", selectedTemplate->displayName.c_str());
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
        ImGui::TextColored(ImVec4(0.60f, 0.67f, 0.76f, 1.0f), "%s", selectedDescription.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
        ImGui::TextColored(ImVec4(0.72f, 0.78f, 0.86f, 1.0f), "%s", summaryLabel.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0.0f, 10.0f));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        ImGui::TextDisabled("Project Name");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##ProjectNameDialog", projectManager.newProjectName, sizeof(projectManager.newProjectName));
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        ImGui::TextDisabled("Location");
        const float browseWidth = 84.0f;
        ImGui::SetNextItemWidth(-(browseWidth + ImGui::GetStyle().ItemSpacing.x));
        ImGui::InputText("##LocationDialog", projectManager.newProjectLocation, sizeof(projectManager.newProjectLocation));
        ImGui::SameLine();
        if (ImGui::Button("Browse", ImVec2(browseWidth, 0.0f))) {
        }

        if (std::strlen(projectManager.newProjectName) > 0 && std::strlen(projectManager.newProjectLocation) > 0) {
            ImGui::Dummy(ImVec2(0.0f, 6.0f));
            fs::path previewPath = fs::path(projectManager.newProjectLocation) / projectManager.newProjectName;
            ImGui::TextColored(ImVec4(0.57f, 0.64f, 0.73f, 1.0f), "Project folder: %s", previewPath.string().c_str());
        }

        if (!projectManager.errorMessage.empty()) {
            ImGui::Dummy(ImVec2(0.0f, 8.0f));
            ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.42f, 1.0f), "%s", projectManager.errorMessage.c_str());
        }

        ImGui::Dummy(ImVec2(0.0f, 10.0f));
        const float cancelWidth = 108.0f;
        const float createWidth = 148.0f;
        const float actionsWidth = cancelWidth + createWidth + ImGui::GetStyle().ItemSpacing.x;
        const float actionsOffset = std::max(0.0f, ImGui::GetContentRegionAvail().x - actionsWidth);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + actionsOffset);
        if (ImGui::Button("Cancel", ImVec2(cancelWidth, 0.0f))) {
            projectManager.showNewProjectDialog = false;
            std::memset(projectManager.newProjectName, 0, sizeof(projectManager.newProjectName));
            projectManager.errorMessage.clear();
        }

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.90f, 0.66f, 0.17f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.96f, 0.72f, 0.24f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.84f, 0.60f, 0.13f, 1.0f));
        if (ImGui::Button("Create Project", ImVec2(createWidth, 0.0f))) {
            createProjectFromCurrentState();
        }
        ImGui::PopStyleColor(3);
        ImGui::EndChild();
    }
    ImGui::End();
}
#pragma endregion

#pragma region Open Project Dialog
void Engine::renderOpenProjectDialog() {
    ImGuiIO& io = ImGui::GetIO();
    ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    ImVec2 center = mainViewport ? mainViewport->GetCenter()
                                 : ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);

    if (mainViewport) {
        ImGui::SetNextWindowViewport(mainViewport->ID);
    }
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(500, 180), ImGuiCond_Appearing);

    if (ImGui::Begin("Open Project", &projectManager.showOpenProjectDialog,
                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings)) {

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

    ImGui::TextColored(ImVec4(0.4f, 0.7f, 0.95f, 1.0f), "Project Name: %s", projectManager.currentProject.name.c_str());
    if (projectManager.currentProject.hasUnsavedChanges) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "*");
    }
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
            if (Texture* icon = renderer.getTexture(iconPath, MaterialProperties::TextureFilter::Point);
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

    // Section headers with a real icon from Resources/.../Dropdowns/<tab>/,
    // keeping the bracket-letter tag as the fallback while icons are WIP.
    auto drawSettingSectionIcon = [&](const char* iconPath, const char* fallbackIcon,
                                      const char* title, const char* helper,
                                      bool defaultOpen = true) {
        const ProjectSettingsUiIcon icon = resolveProjectSettingsIcon(iconPath);
        return DrawSettingSection(icon.id, icon.flipY, fallbackIcon, title, helper, defaultOpen);
    };

    struct ProjectSettingsTabInfo {
        const char* label;
        const char* iconPath;
        const char* fallback;
    };

    static constexpr ProjectSettingsTabInfo tabs[] = {
        { "Scenes", "Resources/Engine-Root/Project Settings/Tabs/Scenes.png", "S" },
        { "Assets", "Resources/Engine-Root/Project Settings/Tabs/Assets.png", "A" },
        { "Input Manager", "Resources/Engine-Root/Project Settings/Tabs/Input Manager.png", "I" },
        { "Physics Manager", "Resources/Engine-Root/Project Settings/Tabs/Physics Manager.png", "P" },
        { "Graphics Manager", "Resources/Engine-Root/Project Settings/Tabs/Graphics.png", "G" },
        { "Lighting Manager", "Resources/Engine-Root/Project Settings/Tabs/Lighting Manager.png", "L" },
        { "Tags & Layers", "Resources/Engine-Root/Project Settings/Tabs/Tags And Layers.png", "T" },
        { "Console Config", "Resources/Engine-Root/Project Settings/Tabs/Console Config.png", "C" },
        { "Audio Config", "Resources/Engine-Root/Project Settings/Tabs/Audio.png", "A" },
        { "Player Config", "Resources/Engine-Root/Project Settings/Tabs/Player Config.png", "P" },
        { "Editor", "Resources/Engine-Root/Project Settings/Tabs/Editor.png", "E" },
        { "Build", "Resources/Engine-Root/Project Settings/Tabs/Build.png", "B" },
        { "Compilation", "Resources/Engine-Root/Project Settings/Tabs/Compilation.png", "C" },
        { "OpenXR Manager", "Resources/Engine-Root/Project Settings/Tabs/ModuVR Manager.png", "X" },
        { "Version Control", "Resources/Engine-Root/Project Settings/Tabs/Work in Progress.png", "V" }
    };

    static constexpr int kInputManagerTab = 2;
    static constexpr int kPhysicsManagerTab = 3;
    static constexpr int kGraphicsManagerTab = 4;
    static constexpr int kLightingManagerTab = 5;
    static constexpr int kTagsLayersTab = 6;
    static constexpr int kConsoleConfigTab = 7;
    static constexpr int kAudioConfigTab = 8;
    static constexpr int kPlayerConfigTab = 9;
    static constexpr int kEditorTab = 10;
    static constexpr int kBuildTab = 11;
    static constexpr int kCompilationTab = 12;
    static constexpr int kOpenXRTab = 13;
    static constexpr int kVersionControlTab = 14;

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

    auto drawProjectSettingsTab = [&](int index, bool compactSidebar) {
        const ProjectSettingsTabInfo& tab = tabs[index];
        const bool isCurrentSelection = (selectedTab == index);
        const float selectedBlend = tabSelectionAnim[index];
        const float pulse = isCurrentSelection ? std::sin(static_cast<float>(ImGui::GetTime()) * 7.5f) : 0.0f;
        const float brighten = std::max(0.0f, pulse) * selectedBlend;
        const float darken = std::max(0.0f, -pulse) * selectedBlend;

        const ImVec2 slotPos = ImGui::GetCursorScreenPos();
        const float compactButtonSize = 44.0f;
        const ImVec2 slotSize = compactSidebar
            ? ImVec2(compactButtonSize, compactButtonSize)
            : ImVec2(ImGui::GetContentRegionAvail().x, 44.0f);
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
        const float insetX = compactSidebar ? std::max(1.5f, 2.5f - expand * 0.45f) : std::max(3.0f, 5.0f - expand);
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
            if (compactSidebar) {
                drawList->AddRectFilled(
                    ImVec2(cardMin.x + 8.0f, cardMax.y - stripeWidth),
                    ImVec2(cardMax.x - 8.0f, cardMax.y),
                    ImGui::GetColorU32(accent),
                    4.0f);
            } else {
                drawList->AddRectFilled(
                    ImVec2(cardMin.x, cardMin.y + 7.0f),
                    ImVec2(cardMin.x + stripeWidth, cardMax.y - 7.0f),
                    ImGui::GetColorU32(accent),
                    4.0f);
            }
        }

        const ProjectSettingsUiIcon icon = resolveProjectSettingsIcon(tab.iconPath);
        const float cardHeight = cardMax.y - cardMin.y;
        const float baseIconSize = compactSidebar
            ? std::max(24.0f, cardHeight - 11.0f)
            : std::max(24.0f, cardHeight - 9.0f);
        const float iconSize = isCurrentSelection
            ? baseIconSize + selectedBlend * (2.0f + brighten * 1.2f - darken * 0.5f)
            : baseIconSize + selectedBlend * 0.4f;
        const float iconX = compactSidebar
            ? cardMin.x + (cardMax.x - cardMin.x - iconSize) * 0.5f
            : cardMin.x + 10.0f;
        const ImVec2 iconMin(iconX, cardMin.y + (cardMax.y - cardMin.y - iconSize) * 0.5f);
        const ImVec2 iconMax(iconMin.x + iconSize, iconMin.y + iconSize);
        const int iconAlpha = selectedBlend > 0.05f ? 255 : hovered ? 235 : 214;
        if (icon.id != static_cast<ImTextureID>(0)) {
            const ImVec2 uvMin = icon.flipY ? ImVec2(0.0f, 1.0f) : ImVec2(0.0f, 0.0f);
            const ImVec2 uvMax = icon.flipY ? ImVec2(1.0f, 0.0f) : ImVec2(1.0f, 1.0f);
            drawList->AddImage(icon.id, iconMin, iconMax, uvMin, uvMax, IM_COL32(255, 255, 255, iconAlpha));
        } else {
            drawList->AddText(
                ImVec2(cardMin.x + 13.0f, cardMin.y + (cardMax.y - cardMin.y - ImGui::GetTextLineHeight()) * 0.5f),
                IM_COL32(255, 255, 255, iconAlpha),
                tab.fallback);
        }

        if (compactSidebar) {
            if (hovered) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(tab.label);
                ImGui::EndTooltip();
            }
        } else {
            const ImVec2 textSize = ImGui::CalcTextSize(tab.label);
            const float textX = iconMax.x + 10.0f;
            const float textY = cardMin.y + (cardMax.y - cardMin.y - textSize.y) * 0.5f;
            const ImU32 textColor = ImGui::GetColorU32(
                selectedBlend > 0.05f
                    ? ImVec4(0.95f, 0.98f, 1.0f, 1.0f)
                    : hovered
                        ? ImVec4(0.90f, 0.94f, 0.98f, 1.0f)
                        : ImVec4(0.78f, 0.83f, 0.89f, 1.0f));
            drawList->AddText(ImVec2(textX, textY), textColor, tab.label);
        }
        return pressed;
    };

    auto selectProjectSettingsTab = [&](int index) {
        if (selectedTab != index) {
            selectedTab = index;
            playEditorFeedbackPreview("Resources/Sounds/Selection Tick Main Editor.mp3", 0.95f, false, EditorFeedbackSoundCategory::Click);
        }
    };
    if (projectSettingsCompactSidebar) {
        ImGui::BeginGroup();
        for (int i = 0; i < tabCount; ++i) {
            if (drawProjectSettingsTab(i, true)) {
                selectProjectSettingsTab(i);
            }
            if (i + 1 < tabCount) {
                const float spacing = (i == 1 || i == kAudioConfigTab || i == kBuildTab) ? 6.0f : 2.0f;
                ImGui::SameLine(0.0f, spacing);
            }
        }
        ImGui::EndGroup();
        ImGui::Spacing();
        ImGui::BeginChild("SettingsBody", ImVec2(0, 0), false);
    } else {
        ImGui::BeginChild("SettingsNav", ImVec2(214.0f, 0), true);
        if (ImGui::SmallButton("<")) {
            projectSettingsCompactSidebar = true;
            saveEditorUserSettings();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Compact settings categories");
            ImGui::EndTooltip();
        }
        ImGui::Separator();
        for (int i = 0; i < tabCount; ++i) {
            if (drawProjectSettingsTab(i, false)) {
                selectProjectSettingsTab(i);
            }
            if (i + 1 < tabCount) {
                ImGui::Dummy(ImVec2(0.0f, 0.0f));
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("SettingsBody", ImVec2(0, 0), false);
    }

    static char settingsSearch[160] = "";
    static int settingsModeIndex = 0;
    settingsModeIndex = std::clamp(settingsModeIndex, 0, 2);
    const ProjectSettingsVisibilityMode settingsMode =
        static_cast<ProjectSettingsVisibilityMode>(settingsModeIndex);
    int visibleSettingCount = 0;

    auto visible = [&](ProjectSettingsVisibilityMode required,
                       const char* section,
                       const char* label,
                       const char* tags = nullptr) {
        const bool isVisible = IsSettingVisibleForMode(settingsMode, required) &&
                               ProjectSettingsMatchesSearch(settingsSearch, section, label, tags);
        if (isVisible) {
            ++visibleSettingCount;
        }
        return isVisible;
    };

    auto sectionVisible = [&](const char* section, ProjectSettingsVisibilityMode required) {
        (void)section;
        return IsSettingVisibleForMode(settingsMode, required);
    };

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 2.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(5.0f, 2.0f));
    const float controlsWidth = ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(std::max(180.0f, controlsWidth * 0.36f));
    ImGui::InputTextWithHint("##ProjectSettingsSearch", "Search settings", settingsSearch, sizeof(settingsSearch));
    ImGui::SameLine();
    const char* modeOptions[] = { "Simple", "Advanced", "Developer" };
    ImGui::SetNextItemWidth(148.0f);
    ImGui::Combo("##ProjectSettingsMode", &settingsModeIndex, modeOptions, IM_ARRAYSIZE(modeOptions));
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(ProjectSettingsModeHelp(settingsMode));
        ImGui::Separator();
        ImGui::TextUnformatted("Simple: common safe settings.");
        ImGui::TextUnformatted("Advanced: technical user settings.");
        ImGui::TextUnformatted("Developer: debug and internal settings.");
        ImGui::EndTooltip();
    }
    ImGui::Separator();

    // When the panel is wide enough, the sections below flow into two balanced
    // columns; tabs without measured sections keep a single full-width column.
    static std::array<std::vector<float>, tabCount> settingsSectionHeights;
    BeginProjectSettingsColumns(settingsSectionHeights[selectedTab]);

    if (selectedTab == 0) {
        if (ImGui::Button("+ New Scene")) {
            showNewSceneDialog = true;
            memset(newSceneName, 0, sizeof(newSceneName));
        }

        ImGui::Spacing();

        auto scenes = projectManager.currentProject.getSceneList();
        for (const auto& scene : scenes) {
            if (!ProjectSettingsMatchesSearch(settingsSearch, "Scenes", scene.c_str(), "scene load duplicate delete")) {
                continue;
            }
            ++visibleSettingCount;
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
        } else if (settingsSearch[0] != '\0' && visibleSettingCount == 0) {
            ImGui::TextDisabled("No scenes match this search.");
        }
    } else if (selectedTab == 1) {
        if (DrawSettingSection("[M]", "Loaded OBJ Meshes", "Imported .obj meshes available to this project.")) {
            const auto& meshesObj = g_objLoader.getAllMeshes();
            if (meshesObj.empty()) {
                ImGui::TextDisabled("No meshes loaded");
                ImGui::TextDisabled("Import .obj files from File Browser");
            } else {
                for (size_t i = 0; i < meshesObj.size(); i++) {
                    const auto& mesh = meshesObj[i];
                    if (!ProjectSettingsMatchesSearch(settingsSearch, "Assets", mesh.name.c_str(), mesh.path.c_str())) {
                        continue;
                    }
                    ++visibleSettingCount;
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
        }

        if (DrawSettingSection("[A]", "Loaded Models", "Imported Assimp-supported model assets available to this project.")) {
            const auto& meshesAssimp = getModelLoader().getAllMeshes();
            if (meshesAssimp.empty()) {
                ImGui::TextDisabled("No models loaded");
                ImGui::TextDisabled("Import FBX/GLTF/other supported models from File Browser");
            } else {
                for (size_t i = 0; i < meshesAssimp.size(); i++) {
                    const auto& mesh = meshesAssimp[i];
                    if (!ProjectSettingsMatchesSearch(settingsSearch, "Assets", mesh.name.c_str(), mesh.path.c_str())) {
                        continue;
                    }
                    ++visibleSettingCount;
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
        }
        if (settingsSearch[0] != '\0' && visibleSettingCount == 0) {
            ImGui::TextDisabled("No assets match this search.");
        }
    } else if (selectedTab == kInputManagerTab) {
        struct InputBindingEntry {
            std::string label;
            std::string keyName;
        };
        struct InputActionEntry {
            std::string name;
            std::string actionType = "Pass Through";
            std::string controlType = "Button";
            bool expanded = false;
            std::vector<InputBindingEntry> bindings;
        };
        struct InputActionMapEntry {
            std::string name;
            std::vector<InputActionEntry> actions;
        };

        static std::vector<InputActionMapEntry> inputActionMaps = []() {
            std::vector<InputActionMapEntry> maps;
            InputActionMapEntry player;
            player.name = "Player";
            player.actions.push_back({ "Jump",    "Pass Through", "Button", true,
                                       { { "Keyboard", "Space" }, { "Gamepad", "Button South" } } });
            player.actions.push_back({ "Sprint",  "Pass Through", "Button", false,
                                       { { "Keyboard", "LeftShift" } } });
            player.actions.push_back({ "Interact","Pass Through", "Button", false,
                                       { { "Keyboard", "E" } } });
            player.actions.push_back({ "Crouch",  "Pass Through", "Button", false,
                                       { { "Keyboard", "C" } } });
            player.actions.push_back({ "Up",      "Pass Through", "Button", false,
                                       { { "Keyboard", "W" }, { "Keyboard", "Up" } } });
            player.actions.push_back({ "Down",    "Pass Through", "Button", false,
                                       { { "Keyboard", "S" }, { "Keyboard", "Down" } } });
            player.actions.push_back({ "Left",    "Pass Through", "Button", false,
                                       { { "Keyboard", "A" }, { "Keyboard", "Left" } } });
            player.actions.push_back({ "Right",   "Pass Through", "Button", false,
                                       { { "Keyboard", "D" }, { "Keyboard", "Right" } } });
            maps.push_back(std::move(player));

            InputActionMapEntry ui;
            ui.name = "UI";
            ui.actions.push_back({ "Submit", "Pass Through", "Button", false,
                                   { { "Keyboard", "Enter" } } });
            ui.actions.push_back({ "Cancel", "Pass Through", "Button", false,
                                   { { "Keyboard", "Escape" } } });
            maps.push_back(std::move(ui));
            return maps;
        }();

        static int selectedMapIndex = 0;
        static int selectedActionIndex = 0;
        if (selectedMapIndex >= (int)inputActionMaps.size()) selectedMapIndex = 0;

        auto writeActionsFile = [&]() {
            if (!projectManager.currentProject.isLoaded) return;
            std::ofstream out(projectManager.currentProject.projectPath / "input_actions.modu");
            if (!out) return;
            out << "# Generated by Input Manager. Format: Action=Key1,Key2,...\n";
            for (const auto& map : inputActionMaps) {
                out << "# Map: " << map.name << "\n";
                for (const auto& action : map.actions) {
                    out << action.name << "=";
                    bool first = true;
                    for (const auto& b : action.bindings) {
                        if (b.keyName.empty()) continue;
                        if (!first) out << ",";
                        out << b.keyName;
                        first = false;
                    }
                    out << "\n";
                }
            }
        };

        // Top toolbar: save + auto-save indicator
        if (ImGui::Button("Save Asset")) {
            writeActionsFile();
            addConsoleMessage("Saved input_actions.modu", ConsoleMessageType::Info);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("All Control Schemes  |  All Devices  |  Auto-Save");
        ImGui::Separator();

        const float availY = std::max(260.0f, ImGui::GetContentRegionAvail().y - 6.0f);
        const float colMaps = 150.0f;
        const float colActions = std::max(220.0f, ImGui::GetContentRegionAvail().x * 0.36f);

        // Action Maps
        ImGui::BeginChild("##InputActionMaps", ImVec2(colMaps, availY), true);
        ImGui::TextDisabled("Action Maps");
        ImGui::Separator();
        for (int i = 0; i < (int)inputActionMaps.size(); ++i) {
            const bool selected = (selectedMapIndex == i);
            if (ImGui::Selectable(inputActionMaps[i].name.c_str(), selected)) {
                if (selectedMapIndex != i) {
                    selectedMapIndex = i;
                    selectedActionIndex = 0;
                }
            }
        }
        ImGui::Separator();
        if (ImGui::SmallButton("+ Map")) {
            inputActionMaps.push_back({ "NewMap", {} });
            selectedMapIndex = (int)inputActionMaps.size() - 1;
            selectedActionIndex = 0;
        }
        ImGui::EndChild();
        ImGui::SameLine();

        // Actions
        ImGui::BeginChild("##InputActions", ImVec2(colActions, availY), true);
        ImGui::TextDisabled("Actions");
        ImGui::Separator();
        if (selectedMapIndex >= 0 && selectedMapIndex < (int)inputActionMaps.size()) {
            InputActionMapEntry& map = inputActionMaps[selectedMapIndex];
            if (selectedActionIndex >= (int)map.actions.size()) selectedActionIndex = 0;
            for (int i = 0; i < (int)map.actions.size(); ++i) {
                InputActionEntry& action = map.actions[i];
                ImGui::PushID(i);
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                           ImGuiTreeNodeFlags_SpanAvailWidth;
                if (selectedActionIndex == i) flags |= ImGuiTreeNodeFlags_Selected;
                if (action.expanded) ImGui::SetNextItemOpen(true, ImGuiCond_Once);
                const bool open = ImGui::TreeNodeEx(action.name.c_str(), flags);
                if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                    selectedActionIndex = i;
                }
                if (open) {
                    action.expanded = true;
                    for (int b = 0; b < (int)action.bindings.size(); ++b) {
                        InputBindingEntry& bind = action.bindings[b];
                        ImGui::PushID(b);
                        ImGui::Bullet();
                        ImGui::SameLine();
                        ImGui::TextDisabled("%s:", bind.label.c_str());
                        ImGui::SameLine();
                        char buf[64] = {};
                        std::snprintf(buf, sizeof(buf), "%s", bind.keyName.c_str());
                        ImGui::SetNextItemWidth(-58.0f);
                        if (ImGui::InputText("##Key", buf, sizeof(buf))) {
                            bind.keyName = buf;
                        }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("X")) {
                            action.bindings.erase(action.bindings.begin() + b);
                            ImGui::PopID();
                            ImGui::TreePop();
                            ImGui::PopID();
                            goto inputActionsContinue;
                        }
                        ImGui::PopID();
                    }
                    if (ImGui::SmallButton("+ Binding")) {
                        action.bindings.push_back({ "Keyboard", "" });
                    }
                    ImGui::TreePop();
                } else {
                    action.expanded = false;
                }
                ImGui::PopID();
                inputActionsContinue: ;
            }
            ImGui::Separator();
            if (ImGui::SmallButton("+ Action")) {
                map.actions.push_back({ "NewAction", "Pass Through", "Button", true, {} });
                selectedActionIndex = (int)map.actions.size() - 1;
            }
        } else {
            ImGui::TextDisabled("Select an action map.");
        }
        ImGui::EndChild();
        ImGui::SameLine();

        // Properties
        ImGui::BeginChild("##InputActionProperties", ImVec2(0, availY), true);
        ImGui::TextDisabled("Action Properties");
        ImGui::Separator();
        if (selectedMapIndex >= 0 && selectedMapIndex < (int)inputActionMaps.size() &&
            selectedActionIndex >= 0 &&
            selectedActionIndex < (int)inputActionMaps[selectedMapIndex].actions.size()) {
            InputActionEntry& action = inputActionMaps[selectedMapIndex].actions[selectedActionIndex];
            char nameBuf[96] = {};
            std::snprintf(nameBuf, sizeof(nameBuf), "%s", action.name.c_str());
            ImGui::TextDisabled("Name");
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputText("##ActionName", nameBuf, sizeof(nameBuf))) {
                action.name = nameBuf;
            }
            ImGui::TextDisabled("Action Type");
            const char* actionTypes[] = { "Pass Through", "Button", "Value" };
            int actionTypeIdx = 0;
            for (int i = 0; i < IM_ARRAYSIZE(actionTypes); ++i) {
                if (action.actionType == actionTypes[i]) { actionTypeIdx = i; break; }
            }
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::Combo("##ActionType", &actionTypeIdx, actionTypes, IM_ARRAYSIZE(actionTypes))) {
                action.actionType = actionTypes[actionTypeIdx];
            }
            ImGui::TextDisabled("Control Type");
            const char* controlTypes[] = { "Button", "Axis", "Vector2", "Vector3" };
            int controlTypeIdx = 0;
            for (int i = 0; i < IM_ARRAYSIZE(controlTypes); ++i) {
                if (action.controlType == controlTypes[i]) { controlTypeIdx = i; break; }
            }
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::Combo("##ControlType", &controlTypeIdx, controlTypes, IM_ARRAYSIZE(controlTypes))) {
                action.controlType = controlTypes[controlTypeIdx];
            }
            ImGui::Separator();
            ImGui::TextDisabled("Interactions");
            ImGui::TextDisabled("(No interactions configured)");
            ImGui::Separator();
            ImGui::TextDisabled("Processors");
            ImGui::TextDisabled("(No processors configured)");
            ImGui::Separator();
            ImGui::TextDisabled("Scripts use: Input.ButtonDown(\"%s\"), .ButtonHeld, .ButtonUp", action.name.c_str());
        } else {
            ImGui::TextDisabled("Select an action to edit its properties.");
        }
        ImGui::EndChild();
    } else if (selectedTab == kPhysicsManagerTab) {
        ProjectPhysicsSettings& physicsSettings = projectManager.currentProject.physicsSettings;
        bool changed = false;

        if (DrawSettingSection("[G]", "Global Simulation", "Project-wide gravity, timing, and solver defaults.")) {
            if (visible(ProjectSettingsVisibilityMode::Simple, "Global Simulation", "Physics Backend", "physics engine backend jolt physx")) {
                changed |= DrawSettingRow("Physics Backend",
                                          "Engine used for 3D simulation. Jolt is the cross-platform default "
                                          "(works on Android). PhysX is available on desktop only. Switching "
                                          "rebuilds the active physics world.",
                                          [&]() {
                    const PhysicsBackendType options[] = { PhysicsBackendType::Jolt, PhysicsBackendType::PhysX };
                    int currentIdx = (physicsSettings.backend == PhysicsBackendType::PhysX) ? 1 : 0;
                    const char* preview = PhysicsBackendLabel(physicsSettings.backend);
                    bool rowChanged = false;
                    if (ImGui::BeginCombo("##PhysicsBackend", preview)) {
                        for (int i = 0; i < (int)(sizeof(options) / sizeof(options[0])); ++i) {
                            const bool selected = (i == currentIdx);
                            if (ImGui::Selectable(PhysicsBackendLabel(options[i]), selected)) {
                                if (options[i] != physicsSettings.backend) {
                                    physicsSettings.backend = options[i];
                                    // Hot-swap: tear down the live backend and stand up the new one.
                                    if (physics) physics->shutdown();
                                    physics = CreatePhysicsBackend(physicsSettings.backend);
                                    if (physics) {
                                        physics->setProjectSettings(physicsSettings);
                                        physics->init();
                                    }
                                    rowChanged = true;
                                }
                            }
                            if (selected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    return rowChanged;
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Simple, "Global Simulation", "Global Gravity", "gravity physics")) {
                changed |= DrawSettingRow("Global Gravity", "3D and 2D runtime gravity use this project-wide multiplier.", [&]() {
                    if (ImGui::DragFloat("##PhysicsGlobalGravity", &physicsSettings.globalGravityScale, 0.01f, 0.0f, 10.0f, "%.2f")) {
                        physicsSettings.globalGravityScale = std::clamp(physicsSettings.globalGravityScale, 0.0f, 10.0f);
                        physics->setProjectSettings(physicsSettings);
                        return true;
                    }
                    return false;
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Advanced, "Global Simulation", "Physics Timestep", "fixed timestep")) {
                changed |= DrawSettingRow("Physics Timestep", "Fixed simulation step used by project physics.", [&]() {
                    if (ImGui::DragFloat("##PhysicsTimestep", &physicsSettings.fixedTimestep, 0.001f, 0.001f, 0.1f, "%.4f s")) {
                        physicsSettings.fixedTimestep = std::clamp(physicsSettings.fixedTimestep, 0.001f, 0.1f);
                        return true;
                    }
                    return false;
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Advanced, "Global Simulation", "Solver Iterations", "stability constraint solver")) {
                changed |= DrawSettingRow("Solver Iterations", "More iterations improve stability at a CPU cost.", [&]() {
                    if (ImGui::DragInt("##PhysicsSolverIterations", &physicsSettings.solverIterations, 1.0f, 1, 64)) {
                        physicsSettings.solverIterations = std::clamp(physicsSettings.solverIterations, 1, 64);
                        return true;
                    }
                    return false;
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Advanced, "Global Simulation", "Physics Modes", "2d 3d toggles")) {
                changed |= DrawSettingRow("Physics Modes", "Enable the physics systems this project expects to use.", [&]() {
                    bool rowChanged = ImGui::Checkbox("3D##Physics3D", &physicsSettings.enable3DPhysics);
                    ImGui::SameLine();
                    rowChanged |= ImGui::Checkbox("2D##Physics2D", &physicsSettings.enable2DPhysics);
                    return rowChanged;
                });
            }
        }

        if (DrawSettingSection("[R]", "Raycasts", "Default raycast behavior for project tools and runtime helpers.")) {
            if (visible(ProjectSettingsVisibilityMode::Simple, "Raycasts", "Default Distance", "raycast distance")) {
                changed |= DrawSettingRow("Default Distance", "Fallback max distance for raycast-style project defaults.", [&]() {
                    if (ImGui::DragFloat("##DefaultRaycastDistance", &physicsSettings.defaultRaycastDistance, 1.0f, 0.01f, 100000.0f, "%.1f")) {
                        physicsSettings.defaultRaycastDistance = std::max(0.01f, physicsSettings.defaultRaycastDistance);
                        return true;
                    }
                    return false;
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Advanced, "Raycasts", "Hit Triggers", "trigger colliders")) {
                changed |= DrawSettingRow("Hit Triggers", "Whether default raycasts should include trigger-style colliders.", [&]() {
                    return ImGui::Checkbox("##RaycastHitTriggers", &physicsSettings.raycastHitTriggers);
                });
            }
        }

        if (DrawSettingSection("[B]", "Rigidbody Defaults", "Default values used when new rigidbodies are added.")) {
            if (visible(ProjectSettingsVisibilityMode::Simple, "Rigidbody Defaults", "Default Mass", "mass rigidbody")) {
                changed |= DrawSettingRow("Default Mass", "New Rigidbody mass before per-object edits.", [&]() {
                    if (ImGui::DragFloat("##DefaultRigidbodyMass", &physicsSettings.defaultRigidbodyMass, 0.05f, 0.0001f, 10000.0f, "%.3f")) {
                        physicsSettings.defaultRigidbodyMass = std::max(0.0001f, physicsSettings.defaultRigidbodyMass);
                        return true;
                    }
                    return false;
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Advanced, "Rigidbody Defaults", "Default Drag", "drag damping")) {
                changed |= DrawSettingRow("Default Drag", "New Rigidbody linear drag before per-object edits.", [&]() {
                    if (ImGui::DragFloat("##DefaultRigidbodyDrag", &physicsSettings.defaultRigidbodyDrag, 0.01f, 0.0f, 100.0f, "%.3f")) {
                        physicsSettings.defaultRigidbodyDrag = std::max(0.0f, physicsSettings.defaultRigidbodyDrag);
                        return true;
                    }
                    return false;
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Advanced, "Rigidbody Defaults", "Mass Units", "kilograms grams pounds ounces")) {
                changed |= DrawSettingRow("Mass Units", "Changes how Rigidbody mass values are shown and converted.", [&]() {
                    int massUnitIndex = static_cast<int>(physicsSettings.massUnit);
                    const char* massUnitOptions[] = { "Kilograms (kg)", "Grams (g)", "Pounds (lb)", "Ounces (oz)" };
                    if (ImGui::Combo("##PhysicsMassUnitsManager", &massUnitIndex, massUnitOptions, IM_ARRAYSIZE(massUnitOptions))) {
                        physicsSettings.massUnit = static_cast<ProjectMassUnit>(
                            std::clamp(massUnitIndex, 0, static_cast<int>(IM_ARRAYSIZE(massUnitOptions)) - 1));
                        return true;
                    }
                    return false;
                });
            }
        }

        if (DrawSettingSection("[M]", "Default Physics Material", "Surface behavior used by newly authored physics materials.")) {
            if (visible(ProjectSettingsVisibilityMode::Simple, "Default Physics Material", "Friction", "material friction")) {
                changed |= DrawSettingRow("Friction", "Default surface friction.", [&]() {
                    if (ImGui::SliderFloat("##DefaultMaterialFriction", &physicsSettings.defaultMaterialFriction, 0.0f, 1.0f, "%.2f")) {
                        physicsSettings.defaultMaterialFriction = std::clamp(physicsSettings.defaultMaterialFriction, 0.0f, 1.0f);
                        return true;
                    }
                    return false;
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Advanced, "Default Physics Material", "Bounciness", "restitution")) {
                changed |= DrawSettingRow("Bounciness", "Default restitution for new physics materials.", [&]() {
                    if (ImGui::SliderFloat("##DefaultMaterialBounciness", &physicsSettings.defaultMaterialBounciness, 0.0f, 1.0f, "%.2f")) {
                        physicsSettings.defaultMaterialBounciness = std::clamp(physicsSettings.defaultMaterialBounciness, 0.0f, 1.0f);
                        return true;
                    }
                    return false;
                });
            }
        }

        if (changed) {
            projectManager.currentProject.saveProjectFile();
            projectManager.currentProject.hasUnsavedChanges = true;
        }
    } else if (selectedTab == kGraphicsManagerTab) {
        ProjectGraphicsSettings& graphics = projectManager.currentProject.graphicsSettings;
        bool changed = false;
        bool buildSettingsChanged = false;

        if (DrawSettingSection("[B]", "Renderer Backend", "Renderer API and OpenGL renderer controls.")) {
            if (visible(ProjectSettingsVisibilityMode::Advanced, "Renderer Backend", "Renderer API", "opengl vulkan backend")) {
                changed |= DrawSettingRow("Renderer API", "Changing this may require an editor restart.", [&]() {
                    int rendererIndex = (projectManager.currentProject.rendererBackend == Modularity::GraphicsBackend::Vulkan) ? 1 : 0;
                    const char* rendererOptions[] = { "OpenGL", "Vulkan (Experimental)" };
                    if (ImGui::Combo("##ProjectRendererApiGraphics", &rendererIndex, rendererOptions, IM_ARRAYSIZE(rendererOptions))) {
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
                        return true;
                    }
                    return false;
                });
                if (projectManager.currentProject.rendererBackend != graphicsBackend) {
                    DrawHelpText("Current session uses a different renderer. Restart editor to apply this project renderer.");
                }
            }

            const bool openGlSettingsAvailable = rendererInitialized && !usingVulkan();
            if (!openGlSettingsAvailable && IsSettingVisibleForMode(settingsMode, ProjectSettingsVisibilityMode::Advanced)) {
                DrawHelpText("OpenGL renderer settings are editable only in OpenGL sessions.");
            }
            ImGui::BeginDisabled(!openGlSettingsAvailable);
            if (visible(ProjectSettingsVisibilityMode::Simple, "Renderer Backend", "Ambient Color", "light color")) {
                buildSettingsChanged |= DrawSettingRow("Ambient Color", "Base light color used by the OpenGL renderer.", [&]() {
                    glm::vec3 ambient = renderer.getAmbientColor();
                    if (ImGui::ColorEdit3("##RendererAmbientColorGraphics", &ambient.x)) {
                        renderer.setAmbientColor(ambient);
                        buildSettings.rendererAmbientColor = ambient;
                        return true;
                    }
                    return false;
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Advanced, "Renderer Backend", "Shadow Resolution", "shadow map quality")) {
                buildSettingsChanged |= DrawSettingRow("Shadow Resolution", "Higher values can look sharper but cost more GPU memory.", [&]() {
                    int shadowResolution = renderer.getShadowMapResolution();
                    if (ImGui::SliderInt("##RendererShadowResolutionGraphics", &shadowResolution, 128, 4096)) {
                        renderer.setShadowMapResolution(shadowResolution);
                        buildSettings.rendererShadowResolution = renderer.getShadowMapResolution();
                        return true;
                    }
                    return false;
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Developer, "Renderer Backend", "Auto Reload Shaders", "developer shader hot reload")) {
                buildSettingsChanged |= DrawSettingRow("Auto Reload Shaders", "Reloads shaders while editing. Useful for shader development.", [&]() {
                    bool shaderAutoReload = renderer.isShaderAutoReloadEnabled();
                    if (ImGui::Checkbox("##RendererAutoReloadShadersGraphics", &shaderAutoReload)) {
                        renderer.setShaderAutoReload(shaderAutoReload);
                        buildSettings.rendererAutoReloadShaders = shaderAutoReload;
                        return true;
                    }
                    return false;
                });
            }
            ImGui::EndDisabled();
        }

        if (DrawSettingSection("[R]", "Runtime Graphics", "Startup graphics behavior for the player.")) {
            if (visible(ProjectSettingsVisibilityMode::Simple, "Runtime Graphics", "VSync", "sync")) {
                changed |= DrawSettingRow("VSync", "Synchronize presentation to the display refresh.", [&]() {
                    return ImGui::Checkbox("##GraphicsVSync", &graphics.vsync);
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Simple, "Runtime Graphics", "Target FPS", "frame cap")) {
                changed |= DrawSettingRow("Target FPS", "Runtime frame-rate target when VSync is not controlling presentation.", [&]() {
                    return ImGui::DragInt("##GraphicsTargetFps", &graphics.targetFps, 1.0f, 1, 500);
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Simple, "Runtime Graphics", "Startup Mode", "fullscreen windowed")) {
                changed |= DrawSettingRow("Startup Mode", "Default player window mode.", [&]() {
                    int mode = graphics.fullscreenStartup ? 1 : 0;
                    const char* modes[] = { "Windowed", "Fullscreen" };
                    if (ImGui::Combo("##GraphicsStartupMode", &mode, modes, IM_ARRAYSIZE(modes))) {
                        graphics.fullscreenStartup = (mode == 1);
                        projectManager.currentProject.playerSettings.fullscreenStartup = graphics.fullscreenStartup;
                        return true;
                    }
                    return false;
                });
            }
        }

        if (DrawSettingSection("[Q]", "Quality", "Renderer quality defaults.")) {
            if (visible(ProjectSettingsVisibilityMode::Simple, "Quality", "Shadow Quality", "shadow resolution")) {
                changed |= DrawSettingRow("Shadow Quality", "Preset for shadow-map quality.", [&]() {
                    const char* quality[] = { "Off", "Low", "Medium", "High" };
                    return ImGui::Combo("##ShadowQuality", &graphics.shadowQuality, quality, IM_ARRAYSIZE(quality));
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Simple, "Quality", "Render Scale", "resolution scale global render scale")) {
                changed |= DrawSettingRow("Render Scale", "Scales the runtime render resolution globally. Below 1 renders fewer pixels and stretches up; above 1 supersamples.", [&]() {
                    return ImGui::SliderFloat("##RenderResolutionScale", &graphics.renderResolutionScale, 0.25f, 2.0f, "%.2fx");
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Simple, "Quality", "HDR", "high dynamic range bloom")) {
                changed |= DrawSettingRow("HDR", "Renders to float color buffers so bright values survive for bloom and tone mapping. Turning it off forces 8-bit color.", [&]() {
                    if (ImGui::Checkbox("##GraphicsHdr", &graphics.hdr)) {
                        applyProjectGraphicsToRenderer();
                        return true;
                    }
                    return false;
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Advanced, "Quality", "Color Resolution", "color precision 8-bit 16-bit float buffer format")) {
                changed |= DrawSettingRow("Color Resolution", "Storage precision of the offscreen color buffers. Auto keeps 16-bit float; 8-bit halves bandwidth for strictly LDR projects.", [&]() {
                    int colorRes = static_cast<int>(graphics.colorResolution);
                    const char* colorResOptions[] = { "Auto", "8-bit", "16-bit Float" };
                    if (ImGui::Combo("##GraphicsColorResolution", &colorRes, colorResOptions, IM_ARRAYSIZE(colorResOptions))) {
                        graphics.colorResolution = static_cast<ProjectColorResolution>(std::clamp(colorRes, 0, 2));
                        applyProjectGraphicsToRenderer();
                        return true;
                    }
                    return false;
                });
                if (!graphics.hdr) {
                    DrawHelpText("HDR is off, so color buffers stay 8-bit regardless of this pick.");
                }
            }
            if (visible(ProjectSettingsVisibilityMode::Simple, "Quality", "Texture Filtering", "point bilinear trilinear")) {
                changed |= DrawSettingRow("Texture Filtering", "Default sampling mode for newly authored materials.", [&]() {
                    int filter = static_cast<int>(graphics.textureFiltering);
                    const char* filters[] = { "Bilinear", "Point", "Trilinear" };
                    if (ImGui::Combo("##TextureFiltering", &filter, filters, IM_ARRAYSIZE(filters))) {
                        graphics.textureFiltering = static_cast<ProjectTextureFiltering>(std::clamp(filter, 0, 2));
                        return true;
                    }
                    return false;
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Simple, "Quality", "Global Texture Format", "texture format auto compression vram 16bpp")) {
                changed |= DrawSettingRow("Global Texture Format", "GPU storage format every texture defaults to. Auto adapts per texture; per-texture overrides in the File Browser still win.", [&]() {
                    const TextureFormatPolicy formatOptions[] = {
                        TextureFormatPolicy::Auto, TextureFormatPolicy::Full,
                        TextureFormatPolicy::RGB565, TextureFormatPolicy::RGB5_A1,
                        TextureFormatPolicy::RGBA4
                    };
                    const char* formatLabels[] = {
                        "Auto (adaptive)", "Full (RGBA8)", "RGB565 (16bpp opaque)",
                        "RGB5_A1 (16bpp cutout)", "RGBA4 (16bpp blended)"
                    };
                    const TextureFormatPolicy current = TextureFormatPolicyFromString(graphics.defaultTextureFormat);
                    int formatIdx = 0;
                    for (int i = 0; i < (int)(sizeof(formatOptions) / sizeof(formatOptions[0])); ++i) {
                        if (formatOptions[i] == current) { formatIdx = i; break; }
                    }
                    if (ImGui::Combo("##GlobalTextureFormat", &formatIdx, formatLabels, IM_ARRAYSIZE(formatLabels))) {
                        graphics.defaultTextureFormat = ToString(formatOptions[std::clamp(formatIdx, 0, 4)]);
                        applyProjectGraphicsToRenderer();
                        return true;
                    }
                    return false;
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Advanced, "Quality", "Anti-aliasing", "msaa")) {
                changed |= DrawSettingRow("Anti-aliasing", "Default anti-aliasing level for player rendering.", [&]() {
                    int aa = static_cast<int>(graphics.antiAliasing);
                    const char* aaOptions[] = { "Off", "MSAA 2x", "MSAA 4x", "MSAA 8x" };
                    if (ImGui::Combo("##AntiAliasing", &aa, aaOptions, IM_ARRAYSIZE(aaOptions))) {
                        graphics.antiAliasing = static_cast<ProjectAntiAliasing>(std::clamp(aa, 0, 3));
                        return true;
                    }
                    return false;
                });
            }
        }

        if (DrawSettingSection("[P]", "Preview Overrides", "Editor and game preview graphics overrides.")) {
            if (visible(ProjectSettingsVisibilityMode::Advanced, "Preview Overrides", "Editor Preview Overrides", "editor preview")) {
                changed |= DrawSettingRow("Editor Preview Overrides", "Allow editor previews to use editor-specific graphics settings.", [&]() {
                    return ImGui::Checkbox("##EditorPreviewGraphicsOverrides", &graphics.editorPreviewOverrides);
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Advanced, "Preview Overrides", "Game Preview Overrides", "game preview")) {
                changed |= DrawSettingRow("Game Preview Overrides", "Allow the Game View to override runtime graphics defaults.", [&]() {
                    return ImGui::Checkbox("##GamePreviewGraphicsOverrides", &graphics.gamePreviewOverrides);
                });
            }
        }

        if (changed) {
            graphics.targetFps = std::clamp(graphics.targetFps, 1, 500);
            graphics.shadowQuality = std::clamp(graphics.shadowQuality, 0, 3);
            graphics.renderResolutionScale = std::clamp(graphics.renderResolutionScale, 0.25f, 2.0f);
            projectManager.currentProject.saveProjectFile();
        }
        if (buildSettingsChanged) {
            saveBuildSettings();
        }
    } else if (selectedTab == kLightingManagerTab) {
        ProjectGraphicsSettings& graphics = projectManager.currentProject.graphicsSettings;
        bool changed = false;
        bool buildSettingsChanged = false;

        // Live light usage against the rendering path's budget.
        int activeDirectional = 0;
        int activeOther = 0;
        for (const SceneObject& lightObj : sceneObjects) {
            if (!IsObjectEnabledInHierarchy(lightObj) || !lightObj.hasLight || !lightObj.light.enabled) continue;
            if (lightObj.light.type == LightType::Directional) ++activeDirectional;
            else ++activeOther;
        }
        int pathLightBudget = 20;
        int pathDirectionalAllowance = 1;
        ProjectRenderingPathCaps(graphics.renderingPath, pathLightBudget, pathDirectionalAllowance);

        if (DrawSettingSection("[P]", "Rendering Path", "Light budget mode, Modularity's take on Forward/Deferred.")) {
            if (visible(ProjectSettingsVisibilityMode::Simple, "Rendering Path", "Rendering Path", "normal deferred forward light limit budget")) {
                changed |= DrawSettingRow("Rendering Path", "How many realtime lights a scene may use. Directional lights keep guaranteed slots on top of the budget.", [&]() {
                    const ProjectRenderingPath pathOptions[] = {
                        ProjectRenderingPath::Normal, ProjectRenderingPath::NormalPlus,
                        ProjectRenderingPath::Deferred, ProjectRenderingPath::HeavyDeferred
                    };
                    bool rowChanged = false;
                    if (ImGui::BeginCombo("##RenderingPath", ProjectRenderingPathLabel(graphics.renderingPath))) {
                        for (ProjectRenderingPath option : pathOptions) {
                            const bool selected = (option == graphics.renderingPath);
                            if (ImGui::Selectable(ProjectRenderingPathLabel(option), selected)) {
                                if (option != graphics.renderingPath) {
                                    graphics.renderingPath = option;
                                    applyProjectGraphicsToRenderer();
                                    rowChanged = true;
                                }
                            }
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("%s", ProjectRenderingPathNote(option));
                            }
                            if (selected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    return rowChanged;
                });
                DrawHelpText(ProjectRenderingPathNote(graphics.renderingPath));
                const bool overBudget = activeOther > pathLightBudget ||
                                        activeDirectional > pathDirectionalAllowance;
                ImGui::TextColored(overBudget ? ImVec4(1.0f, 0.55f, 0.35f, 1.0f)
                                              : ImVec4(0.55f, 0.75f, 0.55f, 1.0f),
                                   "Scene: %d light%s + %d directional (budget %d + %d directional)",
                                   activeOther, activeOther == 1 ? "" : "s", activeDirectional,
                                   pathLightBudget, pathDirectionalAllowance);
                if (overBudget) {
                    DrawHelpText("Over budget: the renderer keeps the nearest lights and drops the rest each frame.");
                }
            }
        }

        const bool lightingEditable = rendererInitialized && !usingVulkan();
        if (!lightingEditable) {
            DrawHelpText("Lighting settings apply to the OpenGL renderer and are editable only in OpenGL sessions.");
        }
        ImGui::BeginDisabled(!lightingEditable);

        if (DrawSettingSection("[M]", "Main Light", "The directional light(s) with guaranteed slots.")) {
            if (visible(ProjectSettingsVisibilityMode::Simple, "Main Light", "Main Light", "directional sun per pixel off")) {
                changed |= DrawSettingRow("Main Light", "Per Pixel keeps directional lighting on; Off removes directional lights from shading.", [&]() {
                    int mode = graphics.mainLightEnabled ? 0 : 1;
                    const char* modes[] = { "Per Pixel", "Off" };
                    if (ImGui::Combo("##MainLightMode", &mode, modes, IM_ARRAYSIZE(modes))) {
                        graphics.mainLightEnabled = (mode == 0);
                        applyProjectGraphicsToRenderer();
                        return true;
                    }
                    return false;
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Simple, "Main Light", "Cast Shadows", "directional shadows")) {
                changed |= DrawSettingRow("Cast Shadows", "Whether directional lights may render shadow maps.", [&]() {
                    if (ImGui::Checkbox("##MainLightCastShadows", &graphics.mainLightCastShadows)) {
                        applyProjectGraphicsToRenderer();
                        return true;
                    }
                    return false;
                });
            }
        }

        if (DrawSettingSection("[A]", "Additional Lights", "Point, spot, and area lights competing for the budget.")) {
            if (visible(ProjectSettingsVisibilityMode::Simple, "Additional Lights", "Additional Lights", "point spot area per pixel off")) {
                changed |= DrawSettingRow("Additional Lights", "Per Pixel keeps point/spot/area lights on; Off drops them from shading.", [&]() {
                    int mode = graphics.additionalLightsEnabled ? 0 : 1;
                    const char* modes[] = { "Per Pixel", "Off" };
                    if (ImGui::Combo("##AdditionalLightsMode", &mode, modes, IM_ARRAYSIZE(modes))) {
                        graphics.additionalLightsEnabled = (mode == 0);
                        applyProjectGraphicsToRenderer();
                        return true;
                    }
                    return false;
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Advanced, "Additional Lights", "Per Frame Limit", "max realtime lights per frame")) {
                changed |= DrawSettingRow("Per Frame Limit", "Hard cap on lights uploaded to the shader each frame (nearest lights win). The rendering path budget still applies on top.", [&]() {
                    int maxLights = renderer.getMaxRealtimeLights();
                    if (ImGui::SliderInt("##LightingMaxRealtimeLights", &maxLights, 1, kRendererMaxRealtimeLights)) {
                        renderer.setMaxRealtimeLights(maxLights);
                        saveEditorUserSettings();
                        return true;
                    }
                    return false;
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Simple, "Additional Lights", "Cast Shadows", "point spot shadows")) {
                changed |= DrawSettingRow("Cast Shadows", "Whether point/spot/area lights may render shadow maps.", [&]() {
                    if (ImGui::Checkbox("##AdditionalLightsCastShadows", &graphics.additionalLightsCastShadows)) {
                        applyProjectGraphicsToRenderer();
                        return true;
                    }
                    return false;
                });
            }
        }

        if (DrawSettingSection("[S]", "Shadows", "Project-wide shadow behavior.")) {
            if (visible(ProjectSettingsVisibilityMode::Simple, "Shadows", "Max Distance", "shadow distance far")) {
                changed |= DrawSettingRow("Max Distance", "Directional shadows only cover this far from the camera. 0 follows the camera far plane; smaller values sharpen shadows.", [&]() {
                    if (ImGui::DragFloat("##ShadowMaxDistance", &graphics.shadowMaxDistance, 1.0f, 0.0f, 100000.0f, graphics.shadowMaxDistance <= 0.0f ? "Unlimited" : "%.0f")) {
                        graphics.shadowMaxDistance = std::max(0.0f, graphics.shadowMaxDistance);
                        applyProjectGraphicsToRenderer();
                        return true;
                    }
                    return false;
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Simple, "Shadows", "Soft Shadows", "pcf soft hard")) {
                changed |= DrawSettingRow("Soft Shadows", "Allows the softened shadow mode. Off forces hard shadows even on lights configured as soft.", [&]() {
                    if (ImGui::Checkbox("##LightingSoftShadows", &graphics.softShadows)) {
                        applyProjectGraphicsToRenderer();
                        return true;
                    }
                    return false;
                });
            }
        }

        ImGui::EndDisabled();

        if (changed) {
            projectManager.currentProject.saveProjectFile();
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        if (buildSettingsChanged) {
            saveBuildSettings();
        }
    } else if (selectedTab == kTagsLayersTab) {
        auto drawNameList = [&](const char* section, std::vector<std::string>& values, const char* defaultName, int maxCount) {
            bool changed = false;
            if (values.empty()) values.push_back(defaultName);
            if (values[0].empty()) values[0] = defaultName;
            for (size_t i = 0; i < values.size(); ++i) {
                if (!ProjectSettingsMatchesSearch(settingsSearch, section, values[i].c_str(), "tags layers names")) continue;
                ++visibleSettingCount;
                ImGui::PushID(static_cast<int>(i));
                char buffer[128];
                std::snprintf(buffer, sizeof(buffer), "%s", values[i].c_str());
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 150.0f);
                if (ImGui::InputText("##Name", buffer, sizeof(buffer))) {
                    std::string next = TrimCopy(buffer);
                    if (IsValidProjectNameToken(next) && !HasDuplicateProjectNameToken(values, next, i)) {
                        values[i] = next;
                        changed = true;
                    }
                }
                ImGui::SameLine();
                ImGui::TextDisabled("#%zu", i);
                ImGui::SameLine();
                ImGui::BeginDisabled(i == 0);
                if (ImGui::SmallButton("Up") && i > 1) {
                    std::swap(values[i], values[i - 1]);
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Down") && i + 1 < values.size()) {
                    std::swap(values[i], values[i + 1]);
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove") && i != 0) {
                    values.erase(values.begin() + static_cast<std::ptrdiff_t>(i));
                    changed = true;
                    ImGui::EndDisabled();
                    ImGui::PopID();
                    break;
                }
                ImGui::EndDisabled();
                if (!IsValidProjectNameToken(values[i]) || HasDuplicateProjectNameToken(values, values[i], i)) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f), "Invalid");
                }
                ImGui::PopID();
            }
            ImGui::BeginDisabled(static_cast<int>(values.size()) >= maxCount);
            if (ImGui::Button((std::string("+ Add ") + section).c_str())) {
                std::string base = std::string(section) + " " + std::to_string(values.size());
                int suffix = static_cast<int>(values.size());
                while (HasDuplicateProjectNameToken(values, base, SIZE_MAX)) {
                    base = std::string(section) + " " + std::to_string(++suffix);
                }
                values.push_back(base);
                changed = true;
            }
            ImGui::EndDisabled();
            return changed;
        };

        bool changed = false;
        if (DrawSettingSection("[L]", "Layers", "Name-based layers with stable internal numeric indices.")) {
            DrawHelpText("Layer references keep serializing as numbers internally. Names only make the editor safer to read.");
            changed |= drawNameList("Layer", projectManager.currentProject.physicsSettings.collisionLayers, "Default", 32);
        }
        if (DrawSettingSection("[T]", "Tags", "Reusable object tags with validation.")) {
            changed |= drawNameList("Tag", projectManager.currentProject.tags, "Untagged", 256);
        }
        if (changed) {
            projectManager.currentProject.saveProjectFile();
            projectManager.currentProject.hasUnsavedChanges = true;
        }
    } else if (selectedTab == kConsoleConfigTab) {
        ProjectConsoleSettings& consoleConfig = projectManager.currentProject.consoleSettings;
        bool changed = false;
        if (DrawSettingSection("[C]", "Console Behavior", "How the console appears while editing and testing.")) {
            changed |= DrawSettingRow("Console Mode", "Choose how the console should appear by default.", [&]() {
                int mode = static_cast<int>(consoleConfig.mode);
                const char* modes[] = { "Docked mini-button", "Floating window" };
                if (ImGui::Combo("##ConsoleMode", &mode, modes, IM_ARRAYSIZE(modes))) {
                    consoleConfig.mode = static_cast<ProjectConsoleMode>(std::clamp(mode, 0, 1));
                    return true;
                }
                return false;
            });
            changed |= DrawSettingRow("Always Open on Launch", "Show the console automatically when the editor opens this project.", [&]() {
                return ImGui::Checkbox("##ConsoleAlwaysOpenOnLaunch", &consoleConfig.alwaysOpenOnLaunch);
            });
            changed |= DrawSettingRow("Open Only on Errors", "Keep the console quiet until errors happen.", [&]() {
                return ImGui::Checkbox("##ConsoleOpenOnlyOnErrors", &consoleConfig.openOnlyOnErrors);
            });
            changed |= DrawSettingRow("Console Tone", "Choose expressive or professional console message wording.", [&]() {
                int tone = static_cast<int>(consoleConfig.tone);
                const char* tones[] = { "Fun", "Concise" };
                if (ImGui::Combo("##ConsoleTone", &tone, tones, IM_ARRAYSIZE(tones))) {
                    consoleConfig.tone = static_cast<ProjectConsoleTone>(std::clamp(tone, 0, 1));
                    return true;
                }
                return false;
            });
        }
        if (changed) projectManager.currentProject.saveProjectFile();
    } else if (selectedTab == kAudioConfigTab) {
        bool editorSettingsChanged = false;

        if (sectionVisible("Audio", ProjectSettingsVisibilityMode::Simple) &&
            DrawSettingSection("[A]", "Audio", "Editor preview and feedback sounds.")) {
            if (visible(ProjectSettingsVisibilityMode::Simple, "Audio", "Feedback Sounds", "click error editor sounds")) {
                editorSettingsChanged |= DrawSettingRow("Feedback Sounds", "Plays short editor sounds for clicks and status feedback.", [&]() {
                    return ImGui::Checkbox("##FeedbackSounds", &feedbackSoundsEnabled);
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Advanced, "Audio", "Preview Volume", "asset browser audio")) {
                editorSettingsChanged |= DrawSettingRow("Preview Volume", "Volume for audio previews in editor tools.", [&]() {
                    return ImGui::SliderFloat("##AudioPreviewVolume", &audioPreviewVolume, 0.0f, 2.0f, "%.2f");
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Developer, "Audio", "Sound Categories", "click error other feedback")) {
                editorSettingsChanged |= DrawSettingRow("Sound Categories", "Developer toggles for individual editor sound groups.", [&]() {
                    bool changed = false;
                    changed |= ImGui::Checkbox("Click##FeedbackClickSounds", &feedbackClickSoundsEnabled);
                    ImGui::SameLine();
                    changed |= ImGui::Checkbox("Error##FeedbackErrorSounds", &feedbackErrorSoundsEnabled);
                    ImGui::SameLine();
                    changed |= ImGui::Checkbox("Other##FeedbackOtherSounds", &feedbackOtherSoundsEnabled);
                    return changed;
                });
            }
        }

        if (settingsSearch[0] != '\0' && visibleSettingCount == 0) {
            ImGui::TextDisabled("No audio settings match this search.");
        }

        if (editorSettingsChanged) {
            saveEditorUserSettings();
        }
    } else if (selectedTab == kPlayerConfigTab) {
        ProjectPlayerSettings& player = projectManager.currentProject.playerSettings;
        bool changed = false;
        auto drawStringSetting = [&](const char* label, const char* helper, std::string& value, size_t bufferSize = 256) {
            return DrawSettingRow(label, helper, [&]() {
                std::vector<char> buffer(bufferSize, '\0');
                std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
                if (ImGui::InputText("##Text", buffer.data(), buffer.size())) {
                    value = TrimCopy(buffer.data());
                    return true;
                }
                return false;
            });
        };
        if (DrawSettingSection("[A]", "Application", "Player identity and startup scene.")) {
            changed |= drawStringSetting("Product Name", "Name shown for exported players.", player.productName);
            changed |= drawStringSetting("Company Name", "Company or studio name used by builds and save paths.", player.companyName);
            changed |= DrawSettingRow("Default Scene", "Scene loaded first by the player.", [&]() {
                auto scenes = projectManager.currentProject.getSceneList();
                std::vector<const char*> sceneNames;
                sceneNames.reserve(scenes.size());
                int current = 0;
                for (size_t i = 0; i < scenes.size(); ++i) {
                    sceneNames.push_back(scenes[i].c_str());
                    if (scenes[i] == player.defaultScene) current = static_cast<int>(i);
                }
                if (sceneNames.empty()) {
                    ImGui::TextDisabled("No scenes available");
                    return false;
                }
                if (ImGui::Combo("##DefaultScene", &current, sceneNames.data(), static_cast<int>(sceneNames.size()))) {
                    player.defaultScene = scenes[static_cast<size_t>(current)];
                    projectManager.currentProject.currentSceneName = player.defaultScene;
                    return true;
                }
                return false;
            });
            changed |= drawStringSetting("Application Icon", "Path to the icon asset used by supported build targets.", player.applicationIconPath, 512);
        }
        if (drawSettingSectionIcon("Resources/Engine-Root/Project Settings/Dropdowns/Player Config/Window and cursor.png",
                                   "[W]", "Window and Cursor", "Startup resolution, fullscreen mode, and cursor defaults.")) {
            changed |= DrawSettingRow("Startup Resolution", "Default player window size.", [&]() {
                bool rowChanged = ImGui::DragInt("Width##PlayerStartupWidth", &player.startupWidth, 1.0f, 64, 8192);
                rowChanged |= ImGui::DragInt("Height##PlayerStartupHeight", &player.startupHeight, 1.0f, 64, 8192);
                if (rowChanged) {
                    player.startupWidth = std::clamp(player.startupWidth, 64, 8192);
                    player.startupHeight = std::clamp(player.startupHeight, 64, 8192);
                }
                return rowChanged;
            });
            changed |= DrawSettingRow("Fullscreen Startup", "Launch the player fullscreen by default.", [&]() {
                return ImGui::Checkbox("##PlayerFullscreenStartup", &player.fullscreenStartup);
            });
            changed |= DrawSettingRow("Native Display Resolution",
                                      "Render at the device's actual display surface size instead of the Startup "
                                      "Resolution above. On Android this is the EGL surface size (e.g. tablet's "
                                      "native res); on desktop, the GLFW window framebuffer. Recommended for "
                                      "mobile builds so 16:9 content doesn't get stretched into a 16:10 panel.",
                                      [&]() {
                return ImGui::Checkbox("##PlayerNativeDisplayResolution", &player.nativeDisplayResolution);
            });
            changed |= DrawSettingRow("Cursor Defaults", "Initial cursor lock and visibility.", [&]() {
                bool rowChanged = ImGui::Checkbox("Locked##CursorLocked", &player.cursorLocked);
                ImGui::SameLine();
                rowChanged |= ImGui::Checkbox("Visible##CursorVisible", &player.cursorVisible);
                return rowChanged;
            });
        }
        if (DrawSettingSection("[B]", "Build Defaults", "Default target and save-data behavior for player builds.")) {
            changed |= drawStringSetting("Build Target", "Default build target used when opening build settings.", player.buildTarget);
            changed |= drawStringSetting("Save Data Path", "How the runtime groups persistent save data.", player.saveDataPathBehavior);
        }
        if (changed) {
            if (player.productName.empty()) player.productName = projectManager.currentProject.name;
            if (player.companyName.empty()) player.companyName = "DefaultCompany";
            projectManager.currentProject.saveProjectFile();
        }
    } else if (selectedTab == kEditorTab) {
        bool editorSettingsChanged = false;
        bool buildSettingsChanged = false;

        if (sectionVisible("Project", ProjectSettingsVisibilityMode::Simple) &&
            drawSettingSectionIcon("Resources/Engine-Root/Project Settings/Dropdowns/Editor/Project.png",
                                   "[P]", "Project", "Project identity and the broad mode this project uses.")) {
            if (visible(ProjectSettingsVisibilityMode::Simple, "Project", "Project Mode", "pipeline 2d 3d 2.5d")) {
                editorSettingsChanged |= DrawSettingRow("Project Mode", "Choose the default editing pipeline for this project.", [&]() {
                    int pipelineIndex = ProjectPipelineToUiIndex(projectManager.currentProject.pipeline);
                    const char* pipelineOptions[] = { "3D Pipeline", "2.5D Pipeline (Experimental)", "2D Pipeline" };
                    if (ImGui::Combo("##ProjectPipelineMode", &pipelineIndex, pipelineOptions, IM_ARRAYSIZE(pipelineOptions))) {
                        projectManager.currentProject.pipeline = ProjectPipelineFromUiIndex(pipelineIndex);
                        projectManager.currentProject.saveProjectFile();
                        applyProjectPipelineDefaults(false);
                        projectManager.currentProject.hasUnsavedChanges = true;
                        return true;
                    }
                    return false;
                });
            }

            if (projectManager.currentProject.pipeline == ProjectPipeline::Pipeline2D &&
                visible(ProjectSettingsVisibilityMode::Simple, "Project", "Pixel Grid Snap", "2d snap grid")) {
                editorSettingsChanged |= DrawSettingRow("Pixel Grid Snap", "Keeps 2D object movement aligned to whole pixels.", [&]() {
                    return ImGui::Checkbox("##PixelGridSnap", &pixelGridSnapEnabled);
                });
            }

            if (projectManager.currentProject.pipeline == ProjectPipeline::Pipeline2D &&
                visible(ProjectSettingsVisibilityMode::Advanced, "Project", "Snap Step", "2d snap grid pixels")) {
                editorSettingsChanged |= DrawSettingRow("Snap Step", "Pixel size used by the 2D grid snap.", [&]() {
                    ImGui::BeginDisabled(!pixelGridSnapEnabled);
                    const bool changed = ImGui::DragInt("##PixelGridSnapStep", &pixelGridSnapStep, 1.0f, 1, 64);
                    ImGui::EndDisabled();
                    if (changed) pixelGridSnapStep = std::clamp(pixelGridSnapStep, 1, 64);
                    return changed;
                });
            }
        }

        if (sectionVisible("Player / Viewport", ProjectSettingsVisibilityMode::Simple) &&
            drawSettingSectionIcon("Resources/Engine-Root/Project Settings/Dropdowns/Editor/Player and viewport.png",
                                   "[V]", "Player / Viewport", "Preview and viewport behavior.")) {
            const char* resolutionOptions[] = { "Default (1280x720)", "1080p", "720p", "1440p", "Custom" };
            if (gameViewportResolutionIndex < 0 || gameViewportResolutionIndex >= static_cast<int>(IM_ARRAYSIZE(resolutionOptions))) {
                gameViewportResolutionIndex = 0;
            }
            if (visible(ProjectSettingsVisibilityMode::Simple, "Player / Viewport", "Preview Resolution", "internal render size performance retro")) {
                editorSettingsChanged |= DrawSettingRow("Preview Resolution", "Controls the internal preview render size. Lower values improve performance or create a retro look.", [&]() {
                    int resolutionIndex = gameViewportResolutionIndex;
                    if (ImGui::Combo("##PreviewResolution", &resolutionIndex, resolutionOptions, IM_ARRAYSIZE(resolutionOptions))) {
                        gameViewportResolutionIndex = resolutionIndex;
                        return true;
                    }
                    return false;
                });
            }
            if (gameViewportResolutionIndex == 4 &&
                visible(ProjectSettingsVisibilityMode::Advanced, "Player / Viewport", "Custom Preview Size", "width height resolution")) {
                editorSettingsChanged |= DrawSettingRow("Custom Preview Size", "Used only when Preview Resolution is Custom.", [&]() {
                    bool changed = false;
                    ImGui::SetNextItemWidth((ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f);
                    if (ImGui::DragInt("##PreviewCustomWidth", &gameViewportCustomWidth, 1.0f, 64, 8192)) {
                        gameViewportCustomWidth = std::clamp(gameViewportCustomWidth, 64, 8192);
                        changed = true;
                    }
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::DragInt("##PreviewCustomHeight", &gameViewportCustomHeight, 1.0f, 64, 8192)) {
                        gameViewportCustomHeight = std::clamp(gameViewportCustomHeight, 64, 8192);
                        changed = true;
                    }
                    return changed;
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Simple, "Player / Viewport", "Auto Fit Preview", "zoom scale")) {
                editorSettingsChanged |= DrawSettingRow("Auto Fit Preview", "Automatically fits the game preview inside its panel.", [&]() {
                    return ImGui::Checkbox("##AutoFitPreview", &gameViewportAutoFit);
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Advanced, "Player / Viewport", "Preview Zoom", "scale")) {
                editorSettingsChanged |= DrawSettingRow("Preview Zoom", "Manual zoom used when Auto Fit Preview is off.", [&]() {
                    ImGui::BeginDisabled(gameViewportAutoFit);
                    float zoomPercent = gameViewportZoom * 100.0f;
                    const bool changed = ImGui::SliderFloat("##PreviewZoom", &zoomPercent, 100.0f, 800.0f, "%.0f%%");
                    ImGui::EndDisabled();
                    if (changed) gameViewportZoom = std::clamp(zoomPercent / 100.0f, 1.0f, 8.0f);
                    return changed;
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Simple, "Player / Viewport", "Canvas Guides", "ui overlay guides")) {
                editorSettingsChanged |= DrawSettingRow("Canvas Guides", "Shows reference guides for UI layout while editing.", [&]() {
                    return ImGui::Checkbox("##CanvasGuides", &showCanvasOverlay);
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Simple, "Player / Viewport", "UI Canvas Preview", "ui canvas preview selection overlay")) {
                editorSettingsChanged |= DrawSettingRow("UI Canvas Preview", "Shows UI canvas previews in editor viewports while not playing.", [&]() {
                    return ImGui::Checkbox("##UICanvasPreview", &uiCanvasPreviewEnabled);
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Advanced, "Player / Viewport", "UI World Grid", "world ui grid overlay")) {
                editorSettingsChanged |= DrawSettingRow("UI World Grid", "Shows the grid while editing world-space UI.", [&]() {
                    return ImGui::Checkbox("##UIWorldGrid", &showUIWorldGrid);
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Advanced, "Player / Viewport", "Viewport Toolbar Corner", "toolbar position")) {
                editorSettingsChanged |= DrawSettingRow("Toolbar Corner", "Moves the scene viewport toolbar to a different corner.", [&]() {
                    const char* toolbarCornerOptions[] = { "Bottom Left", "Bottom Right", "Top Left", "Top Right" };
                    int toolbarCornerIndex = static_cast<int>(sceneViewportToolbarCorner);
                    toolbarCornerIndex = std::clamp(toolbarCornerIndex, 0, static_cast<int>(IM_ARRAYSIZE(toolbarCornerOptions)) - 1);
                    if (ImGui::Combo("##ViewportToolbarCorner", &toolbarCornerIndex, toolbarCornerOptions, IM_ARRAYSIZE(toolbarCornerOptions))) {
                        sceneViewportToolbarCorner = static_cast<ViewportToolbarCorner>(toolbarCornerIndex);
                        return true;
                    }
                    return false;
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Advanced, "Player / Viewport", "Viewport Hint Overlay", "hints help overlay")) {
                editorSettingsChanged |= DrawSettingRow("Viewport Hints", "Shows short helper hints in viewport toolbars.", [&]() {
                    return ImGui::Checkbox("##ViewportHints", &showViewportHintOverlay);
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Developer, "Player / Viewport", "Viewport Profiler", "performance overlay profiler")) {
                editorSettingsChanged |= DrawSettingRow("Viewport Profiler", "Shows runtime timing information over the preview.", [&]() {
                    return ImGui::Checkbox("##ViewportProfiler", &showGameProfiler);
                });
            }
        }

        if (sectionVisible("Build", ProjectSettingsVisibilityMode::Simple) &&
            drawSettingSectionIcon("Resources/Engine-Root/Project Settings/Dropdowns/Editor/ModuCPP Logo.png",
                                   "[B]", "Build", "Build profile shortcuts. Full controls remain in the Build tab.")) {
            if (visible(ProjectSettingsVisibilityMode::Simple, "Build", "Platform", "target windows linux android")) {
                buildSettingsChanged |= DrawSettingRow("Platform", "Target platform used by the build profile.", [&]() {
                    int targetIndex = buildPlatformIndex(buildSettings.platform);
                    if (ImGui::Combo("##BuildPlatformQuick", &targetIndex,
                                     kBuildPlatformLabels, kBuildPlatformCount)) {
                        BuildPlatform newPlatform = buildPlatformFromIndex(targetIndex);
                        if (newPlatform != buildSettings.platform) {
                            buildSettings.platform = newPlatform;
                            int archCount = 0;
                            const BuildArchitectureInfo* archList = buildArchitecturesForPlatform(newPlatform, archCount);
                            bool valid = false;
                            for (int i = 0; i < archCount; ++i) {
                                if (buildSettings.architecture == archList[i].serializedName) { valid = true; break; }
                            }
                            if (!valid) buildSettings.architecture = archList[0].serializedName;
                            return true;
                        }
                    }
                    return false;
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Advanced, "Build", "Development Build", "debug build profile")) {
                buildSettingsChanged |= DrawSettingRow("Development Build", "Build with development flags enabled.", [&]() {
                    return ImGui::Checkbox("##DevelopmentBuildQuick", &buildSettings.developmentBuild);
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Developer, "Build", "Profiling", "profiler script debugging deep profiling")) {
                buildSettingsChanged |= DrawSettingRow("Profiling", "Developer build diagnostics and profiling options.", [&]() {
                    bool changed = false;
                    changed |= ImGui::Checkbox("Script Debug##ScriptDebuggingQuick", &buildSettings.scriptDebugging);
                    ImGui::SameLine();
                    changed |= ImGui::Checkbox("Profiler##AutoProfilerQuick", &buildSettings.autoConnectProfiler);
                    ImGui::SameLine();
                    changed |= ImGui::Checkbox("Deep##DeepProfilingQuick", &buildSettings.deepProfiling);
                    return changed;
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Simple, "Build", "Open Build Settings", "advanced build window")) {
                DrawSettingRow("More Build Options", "Open the dedicated Build tab for scenes, packaging, and export.", [&]() {
                    if (ImGui::Button("Open Build Tab")) {
                        selectedTab = kBuildTab;
                        return true;
                    }
                    return false;
                });
            }
        }

        if (sectionVisible("Editor", ProjectSettingsVisibilityMode::Simple) &&
            drawSettingSectionIcon("Resources/Engine-Root/Project Settings/Dropdowns/Editor/Editor.png",
                                   "[E]", "Editor", "Camera movement and editor-only viewport aids.")) {
            if (visible(ProjectSettingsVisibilityMode::Simple, "Editor", "Move Speed", "camera navigation")) {
                editorSettingsChanged |= DrawSettingRow("Move Speed", "Base speed for editor camera movement.", [&]() {
                    if (ImGui::DragFloat("##CameraMoveSpeed", &camera.moveSpeed, 0.1f, 0.01f, 100.0f, "%.2f")) {
                        camera.moveSpeed = std::max(0.01f, camera.moveSpeed);
                        camera.sprintSpeed = std::max(camera.moveSpeed, camera.sprintSpeed);
                        return true;
                    }
                    return false;
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Advanced, "Editor", "Sprint Speed", "camera navigation")) {
                editorSettingsChanged |= DrawSettingRow("Sprint Speed", "Fast movement speed while sprinting in the editor camera.", [&]() {
                    if (ImGui::DragFloat("##CameraSprintSpeed", &camera.sprintSpeed, 0.1f, 0.01f, 200.0f, "%.2f")) {
                        camera.sprintSpeed = std::max(camera.moveSpeed, camera.sprintSpeed);
                        return true;
                    }
                    return false;
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Simple, "Editor", "Smooth Movement", "camera smoothing")) {
                editorSettingsChanged |= DrawSettingRow("Smooth Movement", "Softens editor camera starts and stops.", [&]() {
                    return ImGui::Checkbox("##CameraSmoothMovement", &camera.smoothMovement);
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Advanced, "Editor", "Acceleration", "camera smoothing speed")) {
                editorSettingsChanged |= DrawSettingRow("Acceleration", "How quickly smooth camera movement reaches full speed.", [&]() {
                    ImGui::BeginDisabled(!camera.smoothMovement);
                    const bool changed = ImGui::DragFloat("##CameraAcceleration", &camera.acceleration, 0.1f, 0.1f, 200.0f, "%.2f");
                    ImGui::EndDisabled();
                    if (changed) camera.acceleration = std::max(0.1f, camera.acceleration);
                    return changed;
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Advanced, "Editor", "Mouse Sensitivity", "camera look")) {
                editorSettingsChanged |= DrawSettingRow("Mouse Sensitivity", "Look sensitivity for the editor camera.", [&]() {
                    if (ImGui::SliderFloat("##CameraMouseSensitivity", &camera.mouseSensitivity, 0.001f, 1.0f, "%.3f")) {
                        camera.mouseSensitivity = std::clamp(camera.mouseSensitivity, 0.001f, 1.0f);
                        return true;
                    }
                    return false;
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Developer, "Editor", "Camera Projection", "fov near far clip")) {
                buildSettingsChanged |= DrawSettingRow("Camera Projection", "Developer controls for the editor camera projection.", [&]() {
                    bool changed = false;
                    changed |= ImGui::SliderFloat("FOV##EditorCameraFov", &buildSettings.editorCameraFov, 20.0f, 140.0f, "%.1f");
                    changed |= ImGui::DragFloat("Near##EditorCameraNear", &buildSettings.editorCameraNear, 0.01f, 0.01f, buildSettings.editorCameraFar - 0.01f, "%.3f");
                    changed |= ImGui::DragFloat("Far##EditorCameraFar", &buildSettings.editorCameraFar, 0.1f, buildSettings.editorCameraNear + 0.05f, 5000.0f, "%.1f");
                    if (changed) {
                        buildSettings.editorCameraNear = std::max(0.01f, std::min(buildSettings.editorCameraNear, buildSettings.editorCameraFar - 0.01f));
                        buildSettings.editorCameraFar = std::max(buildSettings.editorCameraNear + 0.05f, buildSettings.editorCameraFar);
                    }
                    return changed;
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Simple, "Editor", "Hierarchy Texture Preview", "hierarchy texture thumbnails")) {
                editorSettingsChanged |= DrawSettingRow("Hierarchy Texture Preview", "Shows small material texture thumbnails in the Hierarchy panel.", [&]() {
                    return ImGui::Checkbox("##HierarchyTexturePreview", &hierarchyShowTexturePreview);
                });
            }
        }

        if (projectManager.currentProject.pipeline == ProjectPipeline::Pipeline25D &&
            sectionVisible("2.5D Presentation", ProjectSettingsVisibilityMode::Advanced) &&
            DrawSettingSection("[2.5D]", "2.5D Presentation", "Technical presentation controls for TM/vexel projects.", false)) {
            auto& tmPresentation = tmOpenGLRenderer.getPresentationSettings();
            if (visible(ProjectSettingsVisibilityMode::Advanced, "2.5D Presentation", "Fake 3D", "glue mode7 lowres tile retro fake")) {
                editorSettingsChanged |= DrawSettingRow("Fake 3D", "Glue3D-style low-res mode7 presentation (tile+seg+m7,tsm).", [&]() {
                    bool changed = ImGui::Checkbox("##TMFake3D", &tmPresentation.fake3DEnabled);
                    ImGui::BeginDisabled(!tmPresentation.fake3DEnabled);
                    changed |= ImGui::DragInt("Lines##TMFake3DHeight", &tmPresentation.fake3DInternalHeight, 1.0f, 64, 2160);
                    changed |= ImGui::Checkbox("Point Sampling##TMFake3DPoint", &tmPresentation.fake3DPointSampling);
                    changed |= ImGui::Checkbox("Flat Shading##TMFake3DFlat", &tmPresentation.fake3DFlatShading);
                    changed |= ImGui::DragInt("Shade Levels##TMFake3DShadeLevels", &tmPresentation.fake3DShadeLevels, 0.1f, 0, 16);
                    changed |= ImGui::Checkbox("Affine Textures##TMFake3DAffine", &tmPresentation.fake3DAffineTextures);
                    ImGui::BeginDisabled(!tmPresentation.fake3DAffineTextures);
                    changed |= ImGui::SliderFloat("Warp##TMFake3DAffineStrength", &tmPresentation.fake3DAffineStrength, 0.0f, 1.0f, "%.2f");
                    ImGui::EndDisabled();
                    ImGui::EndDisabled();
                    if (changed) {
                        tmPresentation.fake3DInternalHeight = std::clamp(tmPresentation.fake3DInternalHeight, 64, 2160);
                        tmPresentation.fake3DShadeLevels = std::clamp(tmPresentation.fake3DShadeLevels, 0, 16);
                        tmPresentation.fake3DAffineStrength = std::clamp(tmPresentation.fake3DAffineStrength, 0.0f, 1.0f);
                    }
                    return changed;
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Advanced, "2.5D Presentation", "Pitch Stretch", "tm vexel look pitch")) {
                editorSettingsChanged |= DrawSettingRow("Pitch Stretch", "Adjusts how 2.5D art responds to camera pitch.", [&]() {
                    return ImGui::Checkbox("##TMPitchStretch", &tmPresentation.lookPitchStretchEnabled);
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Developer, "2.5D Presentation", "Pitch Strength", "stretch compress shear")) {
                editorSettingsChanged |= DrawSettingRow("Pitch Strength", "Internal stretch, compression, and shear tuning.", [&]() {
                    bool changed = false;
                    ImGui::BeginDisabled(!tmPresentation.lookPitchStretchEnabled);
                    changed |= ImGui::DragFloat("Stretch##TMPitchStretchStrength", &tmPresentation.lookPitchStretchStrength, 0.01f, 0.0f, 1.5f, "%.2f");
                    changed |= ImGui::DragFloat("Compress##TMPitchCompressStrength", &tmPresentation.lookPitchCompressStrength, 0.01f, 0.0f, 1.5f, "%.2f");
                    changed |= ImGui::DragFloat("Shear##TMPitchShearStrength", &tmPresentation.lookPitchShearStrength, 0.01f, 0.0f, 1.0f, "%.2f");
                    changed |= ImGui::DragFloat("Curve##TMPitchCurve", &tmPresentation.lookPitchCurve, 0.01f, 0.1f, 4.0f, "%.2f");
                    changed |= ImGui::DragFloat("Depth Range##TMPitchDepthRange", &tmPresentation.lookPitchDepthRange, 0.25f, 1.0f, 256.0f, "%.1f");
                    changed |= ImGui::DragFloatRange2("Pitch Range##TMPitchRange",
                                                      &tmPresentation.presentationPitchMinDegrees,
                                                      &tmPresentation.presentationPitchMaxDegrees,
                                                      0.5f, -89.0f, 89.0f, "%.0f deg");
                    ImGui::EndDisabled();
                    if (changed) {
                        tmPresentation.lookPitchStretchStrength = std::clamp(tmPresentation.lookPitchStretchStrength, 0.0f, 1.5f);
                        tmPresentation.lookPitchCompressStrength = std::clamp(tmPresentation.lookPitchCompressStrength, 0.0f, 1.5f);
                        tmPresentation.lookPitchShearStrength = std::clamp(tmPresentation.lookPitchShearStrength, 0.0f, 1.0f);
                        tmPresentation.lookPitchCurve = std::clamp(tmPresentation.lookPitchCurve, 0.1f, 4.0f);
                        tmPresentation.lookPitchDepthRange = std::clamp(tmPresentation.lookPitchDepthRange, 1.0f, 256.0f);
                        tmPresentation.presentationPitchMinDegrees = std::clamp(tmPresentation.presentationPitchMinDegrees, -89.0f, 89.0f);
                        tmPresentation.presentationPitchMaxDegrees = std::clamp(tmPresentation.presentationPitchMaxDegrees,
                                                                                tmPresentation.presentationPitchMinDegrees, 89.0f);
                    }
                    return changed;
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Advanced, "2.5D Presentation", "Snap Presets", "precision retro snap")) {
                editorSettingsChanged |= DrawSettingRow("Snap Presets", "Switch between clean precision and retro snapped presentation.", [&]() {
                    bool changed = false;
                    if (ImGui::Button("High Precision")) {
                        tmPresentation.presentationSnapEnabled = false;
                        tmPresentation.cameraRelativeSnapEnabled = false;
                        tmPresentation.vertexSnapEnabled = false;
                        tmPresentation.screenSnapEnabled = false;
                        changed = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Retro Snap")) {
                        tmPresentation.presentationSnapEnabled = true;
                        tmPresentation.presentationSnapStep = 0.125f;
                        tmPresentation.cameraRelativeSnapEnabled = true;
                        tmPresentation.cameraRelativeSnapStep = 0.125f;
                        tmPresentation.vertexSnapEnabled = true;
                        tmPresentation.vertexSnapStep = 0.0625f;
                        tmPresentation.screenSnapEnabled = true;
                        tmPresentation.screenSnapStep = 2.0f;
                        changed = true;
                    }
                    return changed;
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Developer, "2.5D Presentation", "Snap Details", "world camera vertex screen snap steps")) {
                editorSettingsChanged |= DrawSettingRow("Snap Details", "Internal snap toggles and step sizes for the 2.5D presentation layer.", [&]() {
                    bool changed = false;
                    changed |= ImGui::Checkbox("World##TMPresentationWorldSnap", &tmPresentation.presentationSnapEnabled);
                    ImGui::BeginDisabled(!tmPresentation.presentationSnapEnabled);
                    changed |= ImGui::DragFloat("World Step##TMPresentationWorldSnapStep", &tmPresentation.presentationSnapStep, 0.005f, 0.001f, 8.0f, "%.3f");
                    ImGui::EndDisabled();
                    changed |= ImGui::Checkbox("Camera##TMPresentationCameraSnap", &tmPresentation.cameraRelativeSnapEnabled);
                    ImGui::BeginDisabled(!tmPresentation.cameraRelativeSnapEnabled);
                    changed |= ImGui::DragFloat("Camera Step##TMPresentationCameraSnapStep", &tmPresentation.cameraRelativeSnapStep, 0.005f, 0.001f, 8.0f, "%.3f");
                    ImGui::EndDisabled();
                    changed |= ImGui::Checkbox("Vertex##TMPresentationVertexSnap", &tmPresentation.vertexSnapEnabled);
                    ImGui::BeginDisabled(!tmPresentation.vertexSnapEnabled);
                    changed |= ImGui::DragFloat("Vertex Step##TMPresentationVertexSnapStep", &tmPresentation.vertexSnapStep, 0.0025f, 0.0005f, 4.0f, "%.4f");
                    ImGui::EndDisabled();
                    changed |= ImGui::Checkbox("Screen##TMPresentationScreenSnap", &tmPresentation.screenSnapEnabled);
                    ImGui::BeginDisabled(!tmPresentation.screenSnapEnabled);
                    changed |= ImGui::DragFloat("Screen Step##TMPresentationScreenSnapStep", &tmPresentation.screenSnapStep, 0.25f, 0.25f, 16.0f, "%.2f");
                    ImGui::EndDisabled();
                    if (changed) {
                        tmPresentation.presentationSnapStep = std::clamp(tmPresentation.presentationSnapStep, 0.001f, 8.0f);
                        tmPresentation.cameraRelativeSnapStep = std::clamp(tmPresentation.cameraRelativeSnapStep, 0.001f, 8.0f);
                        tmPresentation.vertexSnapStep = std::clamp(tmPresentation.vertexSnapStep, 0.0005f, 4.0f);
                        tmPresentation.screenSnapStep = std::clamp(tmPresentation.screenSnapStep, 0.25f, 16.0f);
                    }
                    return changed;
                });
            }
        }

        if (sectionVisible("Debug / Developer", ProjectSettingsVisibilityMode::Advanced) &&
            DrawSettingSection("[D]", "Debug / Developer", "Debug overlays and internal controls.", false)) {
            if (visible(ProjectSettingsVisibilityMode::Advanced, "Debug / Developer", "Scene Gizmos", "camera light gizmos")) {
                editorSettingsChanged |= DrawSettingRow("Scene Gizmos", "Shows editor-only visual handles and overlays.", [&]() {
                    return ImGui::Checkbox("##SceneGizmos", &showSceneGizmos);
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Advanced, "Debug / Developer", "3D Grid", "scene grid")) {
                editorSettingsChanged |= DrawSettingRow("3D Grid", "Shows the scene grid in 3D viewports.", [&]() {
                    return ImGui::Checkbox("##SceneGrid3D", &showSceneGrid3D);
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Advanced, "Debug / Developer", "Show Developer Tools", "reveal debug sections menus")) {
                editorSettingsChanged |= DrawSettingRow("Show Developer Tools", "Reveals debug sections and extra developer menus.", [&]() {
                    return ImGui::Checkbox("##ShowDeveloperTools", &revealDebugSectionsAndMenus);
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Developer, "Debug / Developer", "Selection Collider Bounds", "collision wireframe physics")) {
                editorSettingsChanged |= DrawSettingRow("Selection Collider Bounds", "Draws collider wireframes for selected objects.", [&]() {
                    return ImGui::Checkbox("##SelectionColliderBounds", &collisionWireframe);
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Developer, "Debug / Developer", "FPS Cap", "performance frame rate limit")) {
                editorSettingsChanged |= DrawSettingRow("FPS Cap", "Limits editor frame rate while testing performance.", [&]() {
                    bool changed = ImGui::Checkbox("Enabled##FpsCapEnabled", &fpsCapEnabled);
                    ImGui::BeginDisabled(!fpsCapEnabled);
                    changed |= ImGui::DragFloat("Target##FpsCapTarget", &fpsCap, 1.0f, 1.0f, 500.0f, "%.0f");
                    ImGui::EndDisabled();
                    if (changed) fpsCap = std::max(1.0f, fpsCap);
                    return changed;
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Developer, "Debug / Developer", "2D Light Debug", "stats buffer scale lighting")) {
                editorSettingsChanged |= DrawSettingRow("2D Light Debug", "Lighting stats and internal render buffer scale.", [&]() {
                    bool changed = ImGui::Checkbox("Stats##Light2DStatsOverlay", &showLight2DStatsOverlay);
                    changed |= ImGui::SliderFloat("Buffer##Light2DBufferScale", &light2DLightingBufferScale, 0.5f, 1.0f, "%.2fx");
                    if (changed) light2DLightingBufferScale = std::clamp(light2DLightingBufferScale, 0.5f, 1.0f);
                    return changed;
                });
            }
            if (revealDebugSectionsAndMenus &&
                visible(ProjectSettingsVisibilityMode::Developer, "Debug / Developer", "Game Profiler", "internal performance details")) {
                ImGui::Separator();
                if (ImGui::CollapsingHeader("Game Profiler", ImGuiTreeNodeFlags_DefaultOpen)) {
                    drawGameProfilerContent();
                }
            }
        }

        if (settingsSearch[0] != '\0' && visibleSettingCount == 0) {
            ImGui::TextDisabled("No settings match this search.");
        }

        if (editorSettingsChanged) {
            audioPreviewVolume = std::clamp(audioPreviewVolume, 0.0f, 2.0f);
            saveEditorUserSettings();
        }
        if (buildSettingsChanged) {
            saveBuildSettings();
        }
    } else if (selectedTab == kBuildTab) {
        bool changed = false;

        if (sectionVisible("Build Targets", ProjectSettingsVisibilityMode::Simple) &&
            DrawSettingSection("[B]", "Build Targets", "Platform and packaging options for exported builds.")) {
            if (visible(ProjectSettingsVisibilityMode::Simple, "Build Targets", "Platform", "windows linux android")) {
                changed |= DrawSettingRow("Platform", "Target operating system for the build profile.", [&]() {
                    int targetIndex = buildPlatformIndex(buildSettings.platform);
                    bool rowChanged = false;
                    if (ImGui::Combo("##BuildPlatform", &targetIndex,
                                     kBuildPlatformLabels, kBuildPlatformCount)) {
                        BuildPlatform newPlatform = buildPlatformFromIndex(targetIndex);
                        if (newPlatform != buildSettings.platform) {
                            buildSettings.platform = newPlatform;
                            int archCount = 0;
                            const BuildArchitectureInfo* archList = buildArchitecturesForPlatform(newPlatform, archCount);
                            bool valid = false;
                            for (int i = 0; i < archCount; ++i) {
                                if (buildSettings.architecture == archList[i].serializedName) { valid = true; break; }
                            }
                            if (!valid) buildSettings.architecture = archList[0].serializedName;
                            rowChanged = true;
                        }
                    }
                    if (buildPlatformIsExperimental(buildSettings.platform)) {
                        ImGui::TextDisabled("Experimental — runtime is not fully wired up yet.");
                    }
                    return rowChanged;
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Advanced, "Build Targets", "Architecture", "x86 x86_64 arm arm64 cpu")) {
                changed |= DrawSettingRow("Architecture", "CPU architecture for the target platform.", [&]() {
                    int archCount = 0;
                    const BuildArchitectureInfo* archList = buildArchitecturesForPlatform(buildSettings.platform, archCount);
                    const char* archLabels[8];
                    int archLabelsUsed = std::min(archCount, static_cast<int>(sizeof(archLabels) / sizeof(archLabels[0])));
                    int archIndex = 0;
                    for (int i = 0; i < archLabelsUsed; ++i) {
                        archLabels[i] = archList[i].label;
                        if (buildSettings.architecture == archList[i].serializedName) archIndex = i;
                    }
                    if (ImGui::Combo("##BuildArchitecture", &archIndex, archLabels, archLabelsUsed)) {
                        buildSettings.architecture = archList[archIndex].serializedName;
                        return true;
                    }
                    return false;
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Advanced, "Build Targets", "Development Build", "debug development")) {
                changed |= DrawSettingRow("Development Build", "Build with development options enabled.", [&]() {
                    return ImGui::Checkbox("##BuildDevelopment", &buildSettings.developmentBuild);
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Developer, "Build Targets", "Debugging", "script debugging profiler deep profiling")) {
                changed |= DrawSettingRow("Debugging", "Developer diagnostics for exported builds.", [&]() {
                    bool rowChanged = false;
                    rowChanged |= ImGui::Checkbox("Script##BuildScriptDebugging", &buildSettings.scriptDebugging);
                    ImGui::SameLine();
                    rowChanged |= ImGui::Checkbox("Profiler##BuildAutoProfiler", &buildSettings.autoConnectProfiler);
                    ImGui::SameLine();
                    rowChanged |= ImGui::Checkbox("Deep##BuildDeepProfiling", &buildSettings.deepProfiling);
                    return rowChanged;
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Developer, "Build Targets", "Special Builds", "scripts only server")) {
                changed |= DrawSettingRow("Special Builds", "Internal build modes for script-only or server exports.", [&]() {
                    bool rowChanged = false;
                    rowChanged |= ImGui::Checkbox("Scripts Only##BuildScriptsOnly", &buildSettings.scriptsOnlyBuild);
                    ImGui::SameLine();
                    rowChanged |= ImGui::Checkbox("Server##BuildServer", &buildSettings.serverBuild);
                    return rowChanged;
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Advanced, "Build Targets", "Compression", "package archive lz4")) {
                changed |= DrawSettingRow("Compression", "Compression method used by exported packages.", [&]() {
                    const char* compressionOptions[] = {"Default", "None", "LZ4", "LZ4HC"};
                    int compressionIndex = 0;
                    for (int i = 0; i < IM_ARRAYSIZE(compressionOptions); ++i) {
                        if (buildSettings.compressionMethod == compressionOptions[i]) {
                            compressionIndex = i;
                            break;
                        }
                    }
                    if (ImGui::Combo("##BuildCompression", &compressionIndex, compressionOptions, IM_ARRAYSIZE(compressionOptions))) {
                        buildSettings.compressionMethod = compressionOptions[compressionIndex];
                        return true;
                    }
                    return false;
                });
            }
        }

        if (sectionVisible("Scenes In Build", ProjectSettingsVisibilityMode::Simple) &&
            DrawSettingSection("[S]", "Scenes In Build", "Scene list included in the exported build.")) {
            ImGui::BeginChild("ProjectBuildScenes", ImVec2(0, 180), true);
            for (int i = 0; i < static_cast<int>(buildSettings.scenes.size()); ++i) {
                BuildSceneEntry& entry = buildSettings.scenes[i];
                if (!ProjectSettingsMatchesSearch(settingsSearch, "Scenes In Build", entry.name.c_str(), "scene build")) {
                    continue;
                }
                ++visibleSettingCount;
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

        if (sectionVisible("Export", ProjectSettingsVisibilityMode::Simple) &&
            DrawSettingSection("[E]", "Export", "Save or open the dedicated build/export window.")) {
            if (visible(ProjectSettingsVisibilityMode::Simple, "Export", "Build Window", "advanced export")) {
                DrawSettingRow("Build Window", "Open the full build/export window.", [&]() {
                    if (ImGui::Button("Open Advanced Build Window")) {
                        showBuildSettings = true;
                        return true;
                    }
                    return false;
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Simple, "Export", "Save Build Profile", "save profile")) {
                DrawSettingRow("Save Profile", "Save the current build profile.", [&]() {
                    if (ImGui::Button("Save Build Profile")) {
                        saveBuildSettings();
                        return true;
                    }
                    return false;
                });
            }
        }

        if (settingsSearch[0] != '\0' && visibleSettingCount == 0) {
            ImGui::TextDisabled("No build settings match this search.");
        }

        if (changed) {
            saveBuildSettings();
        }
    } else if (selectedTab == kCompilationTab) {
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
        if (sectionVisible("Compiler Workflow", ProjectSettingsVisibilityMode::Simple) &&
            DrawSettingSection("[C]", "Compiler Workflow", "C++ script compiler workflow settings.")) {
            if (visible(ProjectSettingsVisibilityMode::Simple, "Compiler Workflow", "Auto-compile on Save", "scripts compile")) {
                editorSettingsChanged |= DrawSettingRow("Auto-compile on Save", "Compile scripts automatically when a script file is saved.", [&]() {
                    return ImGui::Checkbox("##ScriptAutoCompileOnSave", &scriptEditorState.autoCompileOnSave);
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Advanced, "Compiler Workflow", "Scan Interval", "auto compile scan seconds")) {
                editorSettingsChanged |= DrawSettingRow("Scan Interval", "How often the editor checks for script changes.", [&]() {
                    float interval = static_cast<float>(scriptAutoCompileInterval);
                    if (ImGui::SliderFloat("##ScriptAutoCompileInterval", &interval, 0.1f, 10.0f, "%.2f s")) {
                        scriptAutoCompileInterval = std::clamp(static_cast<double>(interval), 0.1, 10.0);
                        return true;
                    }
                    return false;
                });
            }
        }

        if (sectionVisible("scripts.modu", ProjectSettingsVisibilityMode::Simple) &&
            drawSettingSectionIcon("Resources/Engine-Root/Project Settings/Dropdowns/Editor/ModuCPP Logo.png",
                                   "[S]", "scripts.modu", "Project script compiler configuration and compatibility settings.")) {
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
            static const char* cppStdLabels[] = {
                "c++14",
                "c++17",
                "c++20",
                "c++23 (Default)",
                "c++26",
                "gnu++14",
                "gnu++17",
                "gnu++20",
                "gnu++23",
                "gnu++26"
            };
            static const char* cppStdValues[] = {
                "c++14",
                "c++17",
                "c++20",
                "c++23",
                "c++26",
                "gnu++14",
                "gnu++17",
                "gnu++20",
                "gnu++23",
                "gnu++26"
            };

            int cppIdx = 3;
            const std::string currentCppStandard = canonicalCppStd(ui.config.cppStandard);
            for (int i = 0; i < IM_ARRAYSIZE(cppStdValues); ++i) {
                if (currentCppStandard == cppStdValues[i]) {
                    cppIdx = i;
                    break;
                }
            }
            if (visible(ProjectSettingsVisibilityMode::Simple, "scripts.modu", "C++ Standard", "cpp c++26 c++23 c++20 c++17 c++14")) {
                DrawSettingRow("C++ Standard",
                               "Default is C++23. Use another standard only when a project needs a different target.",
                               [&]() {
                    if (ImGui::Combo("##CppStandard", &cppIdx, cppStdLabels, IM_ARRAYSIZE(cppStdLabels))) {
                        ui.config.cppStandard = cppStdValues[cppIdx];
                        ui.dirty = true;
                        return true;
                    }
                    return false;
                });
            }

            if (visible(ProjectSettingsVisibilityMode::Simple, "scripts.modu", "Scripts Directory", "source folder path")) {
                DrawSettingRow("Scripts Directory", "Folder containing project script source files.", [&]() {
                    editPath("##ScriptsDirectory", ui.config.scriptsDir);
                    return false;
                });
            }
            if (visible(ProjectSettingsVisibilityMode::Advanced, "scripts.modu", "Output Directory", "compiled scripts output folder")) {
                DrawSettingRow("Output Directory", "Folder where compiled script binaries are written.", [&]() {
                    editPath("##ScriptsOutputDirectory", ui.config.outDir);
                    return false;
                });
            }

            if (visible(ProjectSettingsVisibilityMode::Advanced, "scripts.modu", "Include Directories", "include path headers")) {
                ImGui::SeparatorText("Include Directories");
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
            }

            if (visible(ProjectSettingsVisibilityMode::Advanced, "scripts.modu", "Defines", "preprocessor symbols")) {
                ImGui::SeparatorText("Defines");
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
            }

            if (visible(ProjectSettingsVisibilityMode::Developer, "scripts.modu", "Linux Link Libraries", "linker libraries linux")) {
                ImGui::SeparatorText("Linux Link Libraries");
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
            }

            if (visible(ProjectSettingsVisibilityMode::Developer, "scripts.modu", "Windows Link Libraries", "linker libraries windows")) {
                ImGui::SeparatorText("Windows Link Libraries");
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
            }

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

        if (settingsSearch[0] != '\0' && visibleSettingCount == 0) {
            ImGui::TextDisabled("No compilation settings match this search.");
        }

        if (editorSettingsChanged) {
            saveEditorUserSettings();
        }
    } else if (selectedTab == kVersionControlTab) {
        static ProjectGitUiState gitUi;
        const fs::path projectPath = projectManager.currentProject.projectPath;
        if (gitUi.projectPath != projectPath) {
            gitUi = ProjectGitUiState{};
            gitUi.projectPath = projectPath;
        }

        if (gitUi.busy && gitUi.future.valid() &&
            gitUi.future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            ProjectGitResult result = gitUi.future.get();
            gitUi.busy = false;
            gitUi.status = result.output;
            addConsoleMessage(gitUi.action + (result.success ? " completed." : " failed."),
                              result.success ? ConsoleMessageType::Success : ConsoleMessageType::Error);
        }

        auto startGitAction = [&](std::string action, auto task) {
            if (gitUi.busy) return;
            gitUi.busy = true;
            gitUi.action = std::move(action);
            gitUi.status = gitUi.action + "...";
            gitUi.future = std::async(std::launch::async, std::move(task));
        };

        if (DrawSettingSection("[G]", "Git Repository",
                               "Track this Modularity project and synchronize it with GitHub.")) {
            ImGui::TextDisabled("Project: %s", projectPath.string().c_str());
            if (gitUi.busy) {
                ImGui::Spinner("##ProjectGitSpinner", 7.0f, 2,
                               ImGui::GetColorU32(ImGuiCol_SliderGrabActive));
                ImGui::SameLine();
                ImGui::TextUnformatted(gitUi.action.c_str());
            }

            ImGui::BeginDisabled(gitUi.busy);
            if (ImGui::Button("Refresh Status")) {
                startGitAction("Refreshing Git status", [projectPath]() {
                    return RunProjectGitCommand(
                        projectPath,
                        "status --short --branch && git -C " +
                            QuoteProjectGitArgument(projectPath.string()) + " remote -v");
                });
            }
            ImGui::SameLine();
            if (ImGui::Button("Initialize Repository")) {
                startGitAction("Initializing repository", [projectPath]() {
                    WriteModularityGitIgnore(projectPath);
                    ProjectGitResult result = RunProjectGitCommand(projectPath, "init -b main");
                    if (!result.success) result = RunProjectGitCommand(projectPath, "init");
                    return result;
                });
            }
            ImGui::EndDisabled();

            ImGui::SeparatorText("GitHub Remote");
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputTextWithHint("##ProjectGitRemote", "https://github.com/owner/repository.git",
                                     gitUi.remoteUrl, sizeof(gitUi.remoteUrl));
            ImGui::BeginDisabled(gitUi.busy || gitUi.remoteUrl[0] == '\0');
            if (ImGui::Button("Connect Existing Repository")) {
                const std::string remoteUrl = gitUi.remoteUrl;
                startGitAction("Connecting GitHub repository", [projectPath, remoteUrl]() {
                    ProjectGitResult probe = RunProjectGitCommand(projectPath, "remote get-url origin");
                    return RunProjectGitCommand(
                        projectPath,
                        std::string("remote ") + (probe.success ? "set-url origin " : "add origin ") +
                            QuoteProjectGitArgument(remoteUrl));
                });
            }
            ImGui::EndDisabled();

            ImGui::SeparatorText("Publish New Repository");
            ImGui::Checkbox("Private repository", &gitUi.privateRepository);
            ImGui::TextDisabled("Uses GitHub CLI (gh). Sign in once with: gh auth login");
            ImGui::BeginDisabled(gitUi.busy);
            if (ImGui::Button("Create and Publish on GitHub")) {
                const std::string repositoryName = projectManager.currentProject.name;
                const bool privateRepository = gitUi.privateRepository;
                startGitAction("Publishing to GitHub", [projectPath, repositoryName, privateRepository]() {
                    WriteModularityGitIgnore(projectPath);
                    ProjectGitResult init = RunProjectGitCommand(projectPath, "rev-parse --git-dir");
                    if (!init.success) {
                        init = RunProjectGitCommand(projectPath, "init -b main");
                        if (!init.success) return init;
                    }
                    ProjectGitResult add = RunProjectGitCommand(projectPath, "add -A");
                    if (!add.success) return add;
                    ProjectGitResult commit = RunProjectGitCommand(
                        projectPath, "commit -m " + QuoteProjectGitArgument("Initial Modularity project"));
                    if (!commit.success && commit.output.find("nothing to commit") == std::string::npos)
                        return commit;
                    return RunProjectGitHubPublish(projectPath, repositoryName, privateRepository);
                });
            }
            ImGui::EndDisabled();
        }

        if (DrawSettingSection("[S]", "Changes and Synchronization",
                               "Commit locally, then pull or push through the configured origin.")) {
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputTextWithHint("##ProjectGitCommit", "Commit message",
                                     gitUi.commitMessage, sizeof(gitUi.commitMessage));
            ImGui::BeginDisabled(gitUi.busy);
            if (ImGui::Button("Stage All")) {
                startGitAction("Staging all project files", [projectPath]() {
                    WriteModularityGitIgnore(projectPath);
                    return RunProjectGitCommand(projectPath, "add .");
                });
            }
            ImGui::SameLine();
            if (ImGui::Button("Stage Tracked")) {
                startGitAction("Staging tracked files", [projectPath]() {
                    return RunProjectGitCommand(projectPath, "add -u");
                });
            }
            ImGui::SameLine();
            if (ImGui::Button("Unstage All")) {
                startGitAction("Unstaging files", [projectPath]() {
                    ProjectGitResult result = RunProjectGitCommand(projectPath, "restore --staged .");
                    if (!result.success) result = RunProjectGitCommand(projectPath, "reset");
                    return result;
                });
            }
            ImGui::EndDisabled();

            ImGui::BeginDisabled(gitUi.busy || gitUi.commitMessage[0] == '\0');
            if (ImGui::Button("Commit Staged")) {
                const std::string message = gitUi.commitMessage;
                startGitAction("Committing project", [projectPath, message]() {
                    return RunProjectGitCommand(projectPath,
                                                "commit -m " + QuoteProjectGitArgument(message));
                });
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(gitUi.busy);
            if (ImGui::Button("Fetch")) {
                startGitAction("Fetching remote changes", [projectPath]() {
                    return RunProjectGitCommand(projectPath, "fetch --prune origin");
                });
            }
            ImGui::SameLine();
            if (ImGui::Button("Pull")) {
                startGitAction("Pulling remote changes", [projectPath]() {
                    return RunProjectGitCommand(projectPath, "pull --ff-only");
                });
            }
            ImGui::SameLine();
            if (ImGui::Button("Push")) {
                startGitAction("Pushing project", [projectPath]() {
                    return RunProjectGitCommand(projectPath, "push -u origin HEAD");
                });
            }
            ImGui::EndDisabled();
        }

        if (DrawSettingSection("[O]", "Output", "Latest Git or GitHub operation output.")) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(gitUi.status.c_str());
            ImGui::PopTextWrapPos();
        }
    } else if (selectedTab == kOpenXRTab) {
        if (DrawSettingSection("[XR]", "OpenXR Management", "VR and OpenXR project support status.")) {
            const ProjectSettingsUiIcon wipLogo = resolveProjectSettingsIcon(
                "Resources/Engine-Root/Project Settings/Tabs/Work in Progress.png");
            const float logoSize = 72.0f;
            if (wipLogo.id != static_cast<ImTextureID>(0)) {
                const ImVec2 uvMin = wipLogo.flipY ? ImVec2(0.0f, 1.0f) : ImVec2(0.0f, 0.0f);
                const ImVec2 uvMax = wipLogo.flipY ? ImVec2(1.0f, 0.0f) : ImVec2(1.0f, 1.0f);
                ImGui::Image(wipLogo.id, ImVec2(logoSize, logoSize), uvMin, uvMax);
                ImGui::SameLine();
            }

            ImGui::BeginGroup();
            ImGui::Text("VR Support is coming soon!");
            ImGui::TextDisabled("OpenXR manager is in progress alongside Android support.");
            ImGui::TextDisabled("Some settings will appear here when the VR pipeline is ready.");
            ImGui::EndGroup();
        }
    }

    EndProjectSettingsColumns();

    ImGui::PopStyleVar(2);
    ImGui::EndChild();

    ImGui::End();
    if (wasOpen != showProjectBrowser) {
        saveEditorUserSettings();
    }
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);
}
#pragma endregion
