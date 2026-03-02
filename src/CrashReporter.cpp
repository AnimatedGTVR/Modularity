#include <glad/glad.h>
#include "CrashReporter.h"
#include "AudioSystem.h"

#include "ThirdParty/glfw/include/GLFW/glfw3.h"
#include "ThirdParty/imgui/backends/imgui_impl_glfw.h"
#include "ThirdParty/imgui/backends/imgui_impl_opengl3.h"
#include "ThirdParty/imgui/imgui.h"
#include "../include/ThirdParty/stb_image.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(__linux__) || defined(__APPLE__)
#include <execinfo.h>
#include <unistd.h>
#endif

#if defined(_WIN32)
#include <shellapi.h>
#include <windows.h>
#endif

namespace Modularity::CrashReporter {
namespace {

namespace fs = std::filesystem;

struct CrashContext {
    std::string productName = "Modularity";
    fs::path executablePath;
    fs::path sessionLogPath;
    std::mutex logMutex;
    std::ofstream logFile;
    std::streambuf* oldCout = nullptr;
    std::streambuf* oldCerr = nullptr;
    std::atomic<bool> crashHandled{false};
};

CrashContext& context() {
    static CrashContext ctx;
    return ctx;
}

fs::path executableDirectory() {
    auto& ctx = context();
    if (!ctx.executablePath.empty()) {
        std::error_code ec;
        const fs::path absolute = fs::absolute(ctx.executablePath, ec);
        if (!ec && !absolute.empty()) {
            return absolute.parent_path();
        }

        const fs::path rawPath(ctx.executablePath);
        if (rawPath.has_parent_path()) {
            return rawPath.parent_path();
        }
    }
    return {};
}

std::string nowForFileName() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tmNow{};
#if defined(_WIN32)
    localtime_s(&tmNow, &time);
#else
    localtime_r(&time, &tmNow);
#endif
    char buffer[32] = {};
    std::strftime(buffer, sizeof(buffer), "%Y%m%d-%H%M%S", &tmNow);
    return buffer;
}

std::string nowForDisplay() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tmNow{};
#if defined(_WIN32)
    localtime_s(&tmNow, &time);
#else
    localtime_r(&time, &tmNow);
#endif
    char buffer[64] = {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tmNow);
    return buffer;
}

class TeeStreamBuf final : public std::streambuf {
public:
    TeeStreamBuf(std::streambuf* primary, std::streambuf* secondary)
        : primary_(primary), secondary_(secondary) {}

protected:
    int overflow(int ch) override {
        if (ch == EOF) {
            return !EOF;
        }

        const int first = primary_ ? primary_->sputc(static_cast<char>(ch)) : ch;
        const int second = secondary_ ? secondary_->sputc(static_cast<char>(ch)) : ch;
        return (first == EOF || second == EOF) ? EOF : ch;
    }

    int sync() override {
        const int first = primary_ ? primary_->pubsync() : 0;
        const int second = secondary_ ? secondary_->pubsync() : 0;
        return (first == 0 && second == 0) ? 0 : -1;
    }

private:
    std::streambuf* primary_ = nullptr;
    std::streambuf* secondary_ = nullptr;
};

TeeStreamBuf*& coutTee() {
    static TeeStreamBuf* tee = nullptr;
    return tee;
}

TeeStreamBuf*& cerrTee() {
    static TeeStreamBuf* tee = nullptr;
    return tee;
}

std::string quoteArg(const std::string& value) {
    std::string out = "\"";
    for (char c : value) {
        if (c == '"') {
            out += "\\\"";
        } else {
            out += c;
        }
    }
    out += "\"";
    return out;
}

void launchUrl(const std::string& url) {
#if defined(_WIN32)
    ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
    std::string command = "open " + quoteArg(url);
    std::system(command.c_str());
#else
    std::string command = "xdg-open " + quoteArg(url) + " >/dev/null 2>&1 &";
    std::system(command.c_str());
#endif
}

void openPath(const fs::path& path) {
#if defined(_WIN32)
    ShellExecuteA(nullptr, "open", path.string().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
    std::string command = "open " + quoteArg(path.string());
    std::system(command.c_str());
#else
    std::string command = "xdg-open " + quoteArg(path.string()) + " >/dev/null 2>&1 &";
    std::system(command.c_str());
#endif
}

fs::path crashDirectory() {
    fs::path baseDir = executableDirectory();
    if (baseDir.empty()) {
        baseDir = fs::current_path();
    }
    const fs::path dir = baseDir / "CrashReports";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

void writeCrashSummary(const std::string& reason, const std::string& details) {
    auto& ctx = context();
    const fs::path summaryPath = crashDirectory() / "last_crash.txt";
    std::ofstream summary(summaryPath, std::ios::trunc);
    if (!summary.is_open()) {
        return;
    }

    summary << "product=" << ctx.productName << "\n";
    summary << "timestamp=" << nowForDisplay() << "\n";
    summary << "reason=" << reason << "\n";
    summary << "details=" << details << "\n";
    summary << "log=" << ctx.sessionLogPath.string() << "\n";
}

void launchReporterProcess(const std::string& reason, const std::string& details) {
    auto& ctx = context();
    writeCrashSummary(reason, details);
    if (ctx.executablePath.empty()) {
        return;
    }

#if defined(_WIN32)
    std::string commandLine = quoteArg(ctx.executablePath.string()) +
                              " --crash-reporter" +
                              " --product-name " + quoteArg(ctx.productName) +
                              " --crash-reason " + quoteArg(reason) +
                              " --crash-details " + quoteArg(details) +
                              " --crash-log " + quoteArg(ctx.sessionLogPath.string());
    STARTUPINFOA startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo{};
    std::vector<char> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back('\0');
    if (CreateProcessA(nullptr,
                       mutableCommand.data(),
                       nullptr,
                       nullptr,
                       FALSE,
                       DETACHED_PROCESS,
                       nullptr,
                       nullptr,
                       &startupInfo,
                       &processInfo)) {
        CloseHandle(processInfo.hThread);
        CloseHandle(processInfo.hProcess);
    }
#else
    const std::string command = quoteArg(ctx.executablePath.string()) +
                                " --crash-reporter" +
                                " --product-name " + quoteArg(ctx.productName) +
                                " --crash-reason " + quoteArg(reason) +
                                " --crash-details " + quoteArg(details) +
                                " --crash-log " + quoteArg(ctx.sessionLogPath.string()) +
                                " >/dev/null 2>&1 &";
    std::system(command.c_str());
#endif
}

[[noreturn]] void handleCrash(const std::string& reason, const std::string& details, int exitCode) {
    auto& ctx = context();
    if (!ctx.crashHandled.exchange(true)) {
        AppendLogLine("[CrashReporter] Fatal crash detected.");
        AppendLogLine("[CrashReporter] Reason: " + reason);
        if (!details.empty()) {
            AppendLogLine("[CrashReporter] Details: " + details);
        }
        launchReporterProcess(reason, details);
    }
    std::_Exit(exitCode);
}

void signalHandler(int signalValue) {
#if defined(_WIN32)
    std::ostringstream details;
    details << signalValue;
    handleCrash("Fatal signal", details.str(), 128 + signalValue);
#else
    static constexpr char kMessage[] =
        "[CrashReporter] Fatal signal received. Reporter fallback is disabled for POSIX signal crashes.\n";
    (void)!::write(STDERR_FILENO, kMessage, sizeof(kMessage) - 1);
    std::signal(signalValue, SIG_DFL);
    std::raise(signalValue);
    std::_Exit(128 + signalValue);
#endif
}

void terminateHandler() {
    std::string details = "No active exception.";
    if (const std::exception_ptr ex = std::current_exception()) {
        try {
            std::rethrow_exception(ex);
        } catch (const std::exception& e) {
            details = e.what();
        } catch (...) {
            details = "Non-standard exception";
        }
    }
    handleCrash("Unhandled termination", details, EXIT_FAILURE);
}

#if defined(_WIN32)
LONG WINAPI unhandledExceptionFilter(EXCEPTION_POINTERS* exceptionInfo) {
    DWORD code = exceptionInfo && exceptionInfo->ExceptionRecord
                     ? exceptionInfo->ExceptionRecord->ExceptionCode
                     : 0;
    std::ostringstream details;
    details << "Windows structured exception 0x" << std::hex << code;
    handleCrash("Unhandled SEH exception", details.str(), EXIT_FAILURE);
}
#endif

std::string readFilePreview(const fs::path& path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        return "Unable to read crash log.";
    }

    std::string line;
    std::deque<std::string> tail;
    while (std::getline(in, line)) {
        tail.push_back(line);
        if (tail.size() > 30) {
            tail.pop_front();
        }
    }

    std::ostringstream out;
    for (const auto& entry : tail) {
        out << entry << '\n';
    }
    return out.str();
}

void applyCrashReporterTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    const ImVec4 slate = ImVec4(0.11f, 0.12f, 0.19f, 1.00f);
    const ImVec4 panel = ImVec4(0.16f, 0.16f, 0.24f, 1.00f);
    const ImVec4 overlay = ImVec4(0.10f, 0.11f, 0.17f, 0.98f);
    const ImVec4 accent = ImVec4(0.48f, 0.56f, 0.86f, 1.00f);
    const ImVec4 accentMuted = ImVec4(0.38f, 0.46f, 0.74f, 1.00f);
    const ImVec4 highlight = ImVec4(0.22f, 0.23f, 0.34f, 1.00f);

    style.WindowPadding = ImVec2(14.0f, 14.0f);
    style.FramePadding = ImVec2(10.0f, 8.0f);
    style.ItemSpacing = ImVec2(10.0f, 8.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 6.0f);
    style.ScrollbarSize = 14.0f;
    style.WindowRounding = 10.0f;
    style.ChildRounding = 10.0f;
    style.FrameRounding = 8.0f;
    style.PopupRounding = 8.0f;
    style.GrabRounding = 8.0f;
    style.TabRounding = 8.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;

    colors[ImGuiCol_Text] = ImVec4(0.92f, 0.93f, 0.97f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.60f, 0.62f, 0.70f, 1.00f);
    colors[ImGuiCol_WindowBg] = slate;
    colors[ImGuiCol_ChildBg] = panel;
    colors[ImGuiCol_PopupBg] = overlay;
    colors[ImGuiCol_Border] = ImVec4(0.22f, 0.23f, 0.34f, 0.70f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.09f, 0.10f, 0.16f, 1.00f);
    colors[ImGuiCol_Header] = highlight;
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.28f, 0.38f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.28f, 0.30f, 0.42f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.22f, 0.23f, 0.32f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.30f, 0.42f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.33f, 0.36f, 0.48f, 1.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.21f, 0.30f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.26f, 0.28f, 0.40f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.30f, 0.34f, 0.46f, 1.00f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.11f, 0.12f, 0.18f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.17f, 0.24f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.09f, 0.10f, 0.15f, 1.00f);
    colors[ImGuiCol_Separator] = ImVec4(0.22f, 0.23f, 0.34f, 1.00f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.34f, 0.36f, 0.52f, 1.00f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.44f, 0.50f, 0.70f, 1.00f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.11f, 0.12f, 0.18f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.24f, 0.26f, 0.36f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.32f, 0.35f, 0.48f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.36f, 0.42f, 0.58f, 1.00f);
    colors[ImGuiCol_CheckMark] = accent;
    colors[ImGuiCol_SliderGrab] = accent;
    colors[ImGuiCol_SliderGrabActive] = accentMuted;
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.28f, 0.30f, 0.42f, 1.00f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.38f, 0.44f, 0.60f, 0.80f);
    colors[ImGuiCol_ResizeGripActive] = accent;
    colors[ImGuiCol_TextSelectedBg] = ImVec4(accent.x, accent.y, accent.z, 0.24f);
    colors[ImGuiCol_NavHighlight] = accent;
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.05f, 0.06f, 0.09f, 0.70f);
}

GLuint createTextureFromImage(const fs::path& imagePath, int& width, int& height) {
    int channels = 0;
    unsigned char* pixels = stbi_load(imagePath.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (!pixels) {
        return 0;
    }

    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    stbi_image_free(pixels);
    return texture;
}

std::string valueForArg(int argc, char** argv, const std::string& name) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], name.c_str()) == 0) {
            return argv[i + 1];
        }
    }
    return {};
}

bool hasArg(int argc, char** argv, const std::string& name) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], name.c_str()) == 0) {
            return true;
        }
    }
    return false;
}

fs::path createPreviewLog(const std::string& productName) {
    const fs::path path = crashDirectory() / (productName + "-preview.log");
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) {
        return {};
    }

    out << "[CrashReporter] Preview session started at " << nowForDisplay() << "\n";
    out << "[DEBUG] Renderer backend: OpenGL\n";
    out << "[INFO] Opening sample crash reporter preview window.\n";
    out << "[WARN] This is mock data for UI iteration only.\n";
    out << "[ERROR] Example exception: Failed to resolve preview resource handle.\n";
    out << "[ERROR] Stack frame 0: Modularity::Preview::OpenCrashReporter()\n";
    out << "[ERROR] Stack frame 1: Modularity::Engine::Tick()\n";
    out << "[ERROR] Stack frame 2: main\n";
    return path;
}

float saturate(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float easeOutCubic(float value) {
    const float t = 1.0f - saturate(value);
    return 1.0f - (t * t * t);
}

int runCrashReporterWindow(const std::string& productName,
                           const std::string& reason,
                           const std::string& details,
                           const fs::path& logPath) {
    if (!glfwInit()) {
        return EXIT_FAILURE;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(920, 720, (productName + " Crash Reporter").c_str(), nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    applyCrashReporterTheme();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    AudioSystem audio;
    const bool audioReady = audio.init();
    if (audioReady) {
        audio.playPreview("Resources/Sounds/Crash Error.mp3", 0.95f, false);
    }

    int logoWidth = 0;
    int logoHeight = 0;
    GLuint logoTexture = createTextureFromImage(fs::current_path() / "Resources/Engine-Root/Modu-Logo.png", logoWidth, logoHeight);

    bool splash = true;
    const auto splashStart = std::chrono::steady_clock::now();
    const std::string logPreview = readFilePreview(logPath);
    const float splashDurationSeconds = 1.3f;
    const float revealDurationSeconds = 0.55f;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        const float elapsedSeconds = std::chrono::duration<float>(
            std::chrono::steady_clock::now() - splashStart).count();
        splash = elapsedSeconds < splashDurationSeconds;
        const float revealT = easeOutCubic((elapsedSeconds - splashDurationSeconds) / revealDurationSeconds);
        const float heroAlpha = revealT;
        const float heroOffset = (1.0f - revealT) * 24.0f;
        const float contentT = easeOutCubic((elapsedSeconds - splashDurationSeconds - 0.08f) / revealDurationSeconds);
        const float contentAlpha = contentT;
        const float contentOffset = (1.0f - contentT) * 38.0f;

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                 ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoResize;
        ImGui::Begin("Crash Reporter Root", nullptr, flags);

        if (splash) {
            ImGui::Dummy(ImVec2(0.0f, 84.0f));
            const float imageSize = 160.0f;
            if (logoTexture != 0) {
                ImGui::SetCursorPosX((io.DisplaySize.x - imageSize) * 0.5f);
                ImGui::Image(reinterpret_cast<void*>(static_cast<intptr_t>(logoTexture)), ImVec2(imageSize, imageSize));
            }
            ImGui::Dummy(ImVec2(0.0f, 20.0f));
            ImGui::SetCursorPosX((io.DisplaySize.x - 220.0f) * 0.5f);
            ImGui::TextColored(ImVec4(0.92f, 0.93f, 0.97f, 1.0f), "Preparing crash report...");
            ImGui::Dummy(ImVec2(0.0f, 10.0f));
            ImGui::SetCursorPosX((io.DisplaySize.x - 320.0f) * 0.5f);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.48f, 0.56f, 0.86f, 1.0f));
            static float progress = 0.0f;
            progress = std::min(1.0f, progress + 0.02f);
            ImGui::ProgressBar(progress, ImVec2(320.0f, 10.0f), "");
            ImGui::PopStyleColor();
        } else {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + heroOffset);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, heroAlpha);
            ImGui::BeginChild("hero", ImVec2(0, 150), true);
            if (logoTexture != 0) {
                ImGui::SetCursorPos(ImVec2(18.0f, 22.0f));
                ImGui::Image(reinterpret_cast<void*>(static_cast<intptr_t>(logoTexture)), ImVec2(96.0f, 96.0f));
            }
            ImGui::SetCursorPos(ImVec2(136.0f, 28.0f));
            ImGui::TextColored(ImVec4(0.92f, 0.93f, 0.97f, 1.0f), "%s has crashed", productName.c_str());
            ImGui::SetCursorPos(ImVec2(136.0f, 60.0f));
            ImGui::TextWrapped("A crash log was captured for this session. Review the details below, then open the log or file an issue.");
            ImGui::SetCursorPos(ImVec2(136.0f, 104.0f));
            ImGui::TextColored(ImVec4(0.48f, 0.56f, 0.86f, 1.0f), "Crash log: %s", logPath.filename().string().c_str());
            ImGui::EndChild();
            ImGui::PopStyleVar();

            ImGui::Dummy(ImVec2(0.0f, 10.0f + contentOffset));
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, contentAlpha);
            ImGui::BeginChild("reporter_content", ImVec2(0, -70), true);
            ImGui::TextColored(ImVec4(0.48f, 0.56f, 0.86f, 1.0f), "Reason");
            ImGui::Separator();
            ImGui::TextWrapped("%s", reason.c_str());
            if (!details.empty()) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.48f, 0.56f, 0.86f, 1.0f), "Details");
                ImGui::Separator();
                ImGui::BeginChild("details_panel", ImVec2(0, 110), true, ImGuiWindowFlags_HorizontalScrollbar);
                ImGui::TextUnformatted(details.c_str());
                ImGui::EndChild();
            }
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.48f, 0.56f, 0.86f, 1.0f), "Recent log output");
            ImGui::Separator();
            ImGui::BeginChild("log_preview", ImVec2(0, 260), true, ImGuiWindowFlags_HorizontalScrollbar);
            ImGui::TextUnformatted(logPreview.c_str());
            ImGui::EndChild();
            ImGui::EndChild();
            ImGui::PopStyleVar();

            if (ImGui::Button("Open Log", ImVec2(140, 0))) {
                openPath(logPath);
            }
            ImGui::SameLine();
            if (ImGui::Button("Report Issue", ImVec2(140, 0))) {
                launchUrl("https://git.shockinteractive.xyz/Tareno-Labs-LLC/Modularity/issues");
            }
            ImGui::SameLine();
            if (ImGui::Button("Open Crash Folder", ImVec2(160, 0))) {
                openPath(logPath.parent_path());
            }
            ImGui::SameLine();
            if (ImGui::Button("Close", ImVec2(110, 0))) {
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
        }

        ImGui::End();
        ImGui::Render();

        int displayW = 0;
        int displayH = 0;
        glfwGetFramebufferSize(window, &displayW, &displayH);
        glViewport(0, 0, displayW, displayH);
        glClearColor(0.11f, 0.12f, 0.19f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    if (logoTexture != 0) {
        glDeleteTextures(1, &logoTexture);
    }
    if (audioReady) {
        audio.shutdown();
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}

}  // namespace

bool HandleCrashReporterMode(int argc, char** argv) {
    const bool previewMode = hasArg(argc, argv, "--crash-reporter-preview");
    if (!previewMode && !hasArg(argc, argv, "--crash-reporter")) {
        return false;
    }

    if (argc > 0 && argv && argv[0]) {
        context().executablePath = argv[0];
    }

    const std::string productName = valueForArg(argc, argv, "--product-name");
    std::string reason = valueForArg(argc, argv, "--crash-reason");
    std::string details = valueForArg(argc, argv, "--crash-details");
    fs::path logPath = valueForArg(argc, argv, "--crash-log");

    const std::string resolvedProductName = productName.empty() ? "Modularity" : productName;
    if (previewMode) {
        if (reason.empty()) {
            reason = "Preview crash dialog";
        }
        if (details.empty()) {
            details =
                "This is a preview-only crash reporter window.\n"
                "This is used to preview the layout, text and it helps to debug the output of the text output on screen.";
        }
        if (logPath.empty()) {
            logPath = createPreviewLog(resolvedProductName);
        }
    }

    runCrashReporterWindow(productName.empty() ? "Modularity" : productName,
                           reason.empty() ? "Unknown crash" : reason,
                           details,
                           logPath);
    return true;
}

void Initialize(const std::string& productName, const std::string& executablePath) {
    auto& ctx = context();
    ctx.productName = productName;
    ctx.executablePath = executablePath;
    ctx.sessionLogPath = crashDirectory() / (productName + "-session-" + nowForFileName() + ".log");
    ctx.logFile.open(ctx.sessionLogPath, std::ios::out | std::ios::trunc);
    if (ctx.logFile.is_open()) {
        ctx.oldCout = std::cout.rdbuf();
        ctx.oldCerr = std::cerr.rdbuf();
        coutTee() = new TeeStreamBuf(ctx.oldCout, ctx.logFile.rdbuf());
        cerrTee() = new TeeStreamBuf(ctx.oldCerr, ctx.logFile.rdbuf());
        std::cout.rdbuf(coutTee());
        std::cerr.rdbuf(cerrTee());
        AppendLogLine("[CrashReporter] Session started at " + nowForDisplay());
    }

    std::set_terminate(terminateHandler);
#if defined(_WIN32)
    std::signal(SIGABRT, signalHandler);
    std::signal(SIGILL, signalHandler);
    std::signal(SIGFPE, signalHandler);
    std::signal(SIGSEGV, signalHandler);
    SetUnhandledExceptionFilter(unhandledExceptionFilter);
#else
    struct sigaction action {};
    action.sa_handler = signalHandler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    sigaction(SIGABRT, &action, nullptr);
    sigaction(SIGILL, &action, nullptr);
    sigaction(SIGFPE, &action, nullptr);
    sigaction(SIGSEGV, &action, nullptr);
#if defined(SIGBUS)
    sigaction(SIGBUS, &action, nullptr);
#endif
#endif
}

int RunProtected(const std::function<int()>& entryPoint) {
    try {
        return entryPoint();
    } catch (const std::exception& e) {
        handleCrash("Unhandled exception", e.what(), EXIT_FAILURE);
    } catch (...) {
        handleCrash("Unhandled exception", "Non-standard exception", EXIT_FAILURE);
    }
}

void AppendLogLine(const std::string& line) {
    auto& ctx = context();
    std::lock_guard<std::mutex> lock(ctx.logMutex);
    if (!ctx.logFile.is_open()) {
        return;
    }
    ctx.logFile << line << '\n';
    ctx.logFile.flush();
}

}  // namespace Modularity::CrashReporter
