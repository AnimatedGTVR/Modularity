#include "ScriptCompiler.h"
#include "ModuCPPTranspiler.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <optional>
#include <sstream>
#include <regex>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #include <windows.h>
#endif

namespace {
    fs::path makeAbsolute(const fs::path& base, const fs::path& value) {
        if (value.is_absolute()) return value;
        std::error_code ec;
        fs::path normalized = fs::weakly_canonical(base / value, ec);
        if (ec) {
            return fs::absolute(base / value);
        }
        return normalized;
    }

    std::string trimCopy(const std::string& value) {
        size_t start = 0;
        while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
            start++;
        }
        size_t end = value.size();
        while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
            end--;
        }
        return value.substr(start, end - start);
    }

    bool addBundledScriptSdkIncludeRoots(const fs::path& start, ScriptBuildConfig& outConfig) {
        if (start.empty()) return false;

        std::error_code ec;
        auto addIfExists = [&](const fs::path& p) {
            if (p.empty()) return;
            if (fs::exists(p, ec)) {
                outConfig.includeDirs.push_back(p);
            }
        };

        fs::path candidate = start;
        for (int depth = 0; depth < 6 && !candidate.empty(); ++depth) {
            const fs::path sdkRoot = candidate / "Resources" / "ScriptSDK";
            if (fs::exists(sdkRoot / "src" / "ScriptRuntime.h", ec)) {
                addIfExists(sdkRoot / "src");
                addIfExists(sdkRoot / "include");
                addIfExists(sdkRoot / "src/ThirdParty");
                addIfExists(sdkRoot / "src/ThirdParty/glm");
                addIfExists(sdkRoot / "src/ThirdParty/glad");
                addIfExists(sdkRoot / "src/ThirdParty/glfw/include");
                addIfExists(sdkRoot / "src/ThirdParty/imgui");
                addIfExists(sdkRoot / "src/ThirdParty/imgui/backends");
                addIfExists(sdkRoot / "src/ThirdParty/assimp/include");
                return true;
            }

            fs::path parent = candidate.parent_path();
            if (parent == candidate) break;
            candidate = parent;
        }
        return false;
    }

    bool writeTextFileIfChanged(const fs::path& path, const std::string& text,
                                std::string& error) {
        std::error_code ec;
        if (fs::exists(path, ec) && !ec) {
            std::ifstream existing(path, std::ios::binary);
            if (existing.is_open()) {
                std::ostringstream ss;
                ss << existing.rdbuf();
                if (ss.str() == text) {
                    return true;
                }
            }
        }

        fs::create_directories(path.parent_path(), ec);
        ec.clear();
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            error = "Unable to write wrapper file: " + path.string();
            return false;
        }
        out << text;
        out.close();
        if (!out.good()) {
            error = "Failed to flush wrapper file: " + path.string();
            return false;
        }
        return true;
    }

    std::optional<fs::file_time_type> getFileWriteTime(const fs::path& path) {
        if (path.empty()) return std::nullopt;
        std::error_code ec;
        if (!fs::exists(path, ec) || ec) return std::nullopt;
        auto t = fs::last_write_time(path, ec);
        if (ec) return std::nullopt;
        return t;
    }

#if defined(_WIN32)
    fs::path getCurrentExecutablePath() {
        std::wstring buffer(MAX_PATH, L'\0');
        for (;;) {
            DWORD len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (len == 0) return {};
            if (len < buffer.size() - 1) {
                buffer.resize(len);
                return fs::path(buffer);
            }
            buffer.resize(buffer.size() * 2);
        }
    }

    bool looksLikeEngineRoot(const fs::path& candidate) {
        std::error_code ec;
        return fs::exists(candidate / "CMakeLists.txt", ec) &&
               fs::exists(candidate / "src" / "ScriptCompiler.cpp", ec);
    }

    std::optional<fs::path> findEngineRootFrom(const fs::path& start) {
        fs::path current = start;
        while (!current.empty()) {
            if (looksLikeEngineRoot(current)) {
                return current;
            }
            fs::path parent = current.parent_path();
            if (parent == current) break;
            current = parent;
        }
        return std::nullopt;
    }

    void appendExistingUniquePath(std::vector<fs::path>& out, std::unordered_set<std::string>& seen,
                                  const fs::path& candidate) {
        if (candidate.empty()) return;

        std::error_code ec;
        if (!fs::exists(candidate, ec) || ec) return;

        fs::path normalized = fs::weakly_canonical(candidate, ec);
        if (ec) {
            ec.clear();
            normalized = fs::absolute(candidate, ec);
            if (ec) normalized = candidate;
        }

        const std::string key = normalized.lexically_normal().string();
        if (!seen.insert(key).second) return;
        out.push_back(normalized);
    }

    std::optional<fs::path> findNamedFileRecursively(const fs::path& root,
                                                     const std::string& filename) {
        std::error_code ec;
        if (!fs::exists(root, ec) || ec) return std::nullopt;

        fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
        fs::recursive_directory_iterator end;
        for (; !ec && it != end; it.increment(ec)) {
            const fs::directory_entry& entry = *it;
            if (!entry.is_regular_file(ec) || ec) {
                ec.clear();
                continue;
            }
            if (entry.path().filename() == filename) {
                fs::path found = fs::weakly_canonical(entry.path(), ec);
                if (ec) {
                    ec.clear();
                    return entry.path();
                }
                return found;
            }
        }
        return std::nullopt;
    }

    std::optional<fs::path> findHostImportLibrary() {
        static bool resolved = false;
        static std::optional<fs::path> cachedPath;
        if (resolved) {
            return cachedPath;
        }
        resolved = true;

        const fs::path exePath = getCurrentExecutablePath();
        if (exePath.empty()) return std::nullopt;

        const fs::path exeDir = exePath.parent_path();
        const std::string libName = exePath.stem().string() + ".lib";
        std::vector<fs::path> exactCandidates;
        std::unordered_set<std::string> seen;

        appendExistingUniquePath(exactCandidates, seen, exeDir / libName);
        appendExistingUniquePath(exactCandidates, seen, exeDir.parent_path() / libName);
        appendExistingUniquePath(exactCandidates, seen,
                                 exeDir.parent_path() / exeDir.filename() / libName);

        if (auto engineRoot = findEngineRootFrom(exeDir)) {
            const fs::path buildDir = *engineRoot / "build";
            appendExistingUniquePath(exactCandidates, seen, buildDir / libName);
            appendExistingUniquePath(exactCandidates, seen, buildDir / "Debug" / libName);
            appendExistingUniquePath(exactCandidates, seen, buildDir / "Release" / libName);
            appendExistingUniquePath(exactCandidates, seen, buildDir / "RelWithDebInfo" / libName);
            appendExistingUniquePath(exactCandidates, seen, buildDir / "MinSizeRel" / libName);
            appendExistingUniquePath(exactCandidates, seen,
                                     buildDir / "player-cache" / "Debug" / libName);
            appendExistingUniquePath(exactCandidates, seen,
                                     buildDir / "player-cache" / "Release" / libName);
            appendExistingUniquePath(exactCandidates, seen,
                                     buildDir / "player-cache" / "RelWithDebInfo" / libName);
            appendExistingUniquePath(exactCandidates, seen,
                                     buildDir / "player-cache" / "MinSizeRel" / libName);

            if (auto recursiveMatch = findNamedFileRecursively(buildDir, libName)) {
                appendExistingUniquePath(exactCandidates, seen, *recursiveMatch);
            }
        }

        if (!exactCandidates.empty()) {
            cachedPath = exactCandidates.front();
            return cachedPath;
        }
        return std::nullopt;
    }
#endif

    struct DependencyInfo {
        bool hasDepFile = false;
        bool missingDependency = false;
        std::optional<fs::file_time_type> newestInput;
    };

    DependencyInfo readDependencyInfo(const fs::path& depFilePath) {
        DependencyInfo info;
        if (depFilePath.empty()) {
            return info;
        }

        info.hasDepFile = true;
        std::error_code ec;
        if (!fs::exists(depFilePath, ec) || ec) {
            info.missingDependency = true;
            return info;
        }

        std::ifstream depFile(depFilePath, std::ios::binary);
        if (!depFile.is_open()) {
            info.missingDependency = true;
            return info;
        }

        std::ostringstream depStream;
        depStream << depFile.rdbuf();
        std::string content = depStream.str();
        if (content.empty()) {
            info.missingDependency = true;
            return info;
        }

        // Flatten line continuations used by Make-style dep files.
        std::string flattened;
        flattened.reserve(content.size());
        for (size_t i = 0; i < content.size(); ++i) {
            if (content[i] == '\\') {
                if (i + 1 < content.size() && content[i + 1] == '\n') {
                    ++i;
                    continue;
                }
                if (i + 2 < content.size() && content[i + 1] == '\r' && content[i + 2] == '\n') {
                    i += 2;
                    continue;
                }
            }
            flattened.push_back(content[i]);
        }

        const size_t colonPos = flattened.find(':');
        if (colonPos == std::string::npos || colonPos + 1 >= flattened.size()) {
            info.missingDependency = true;
            return info;
        }

        std::string depList = flattened.substr(colonPos + 1);
        std::vector<std::string> tokens;
        std::string current;
        bool escaped = false;
        for (char c : depList) {
            if (escaped) {
                current.push_back(c);
                escaped = false;
                continue;
            }
            if (c == '\\') {
                escaped = true;
                continue;
            }
            if (std::isspace(static_cast<unsigned char>(c))) {
                if (!current.empty()) {
                    tokens.push_back(current);
                    current.clear();
                }
                continue;
            }
            current.push_back(c);
        }
        if (escaped) {
            current.push_back('\\');
        }
        if (!current.empty()) {
            tokens.push_back(current);
        }

        if (tokens.empty()) {
            info.missingDependency = true;
            return info;
        }

        std::unordered_set<std::string> seen;
        for (const std::string& rawToken : tokens) {
            if (rawToken.empty() || !seen.insert(rawToken).second) continue;

            fs::path depPath = fs::path(rawToken);
            if (depPath.is_relative()) {
                depPath = fs::absolute(depPath, ec);
                ec.clear();
            }

            if (!fs::exists(depPath, ec) || ec) {
                info.missingDependency = true;
                ec.clear();
                continue;
            }

            auto depTime = fs::last_write_time(depPath, ec);
            if (ec) {
                info.missingDependency = true;
                ec.clear();
                continue;
            }
            info.newestInput = info.newestInput ? std::max(*info.newestInput, depTime) : depTime;
        }

        if (!info.newestInput) {
            info.missingDependency = true;
        }
        return info;
    }

#if !defined(_WIN32)
    std::string posixCompileDriver(bool cxx) {
        static int ccacheAvailable = -1;
        if (ccacheAvailable < 0) {
            ccacheAvailable = (std::system("command -v ccache >/dev/null 2>&1") == 0) ? 1 : 0;
        }
        if (ccacheAvailable == 1) {
            return cxx ? "ccache g++" : "ccache gcc";
        }
        return cxx ? "g++" : "gcc";
    }
#endif
    // why does windows need all of this :sob:
#if defined(_WIN32)
    std::string getEnvValue(const char* name) {
        const char* value = std::getenv(name);
        return value ? std::string(value) : std::string();
    }

    std::wstring utf8ToWide(const std::string& value) {
        if (value.empty()) return std::wstring();

        int len = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
        if (len <= 0) return std::wstring(value.begin(), value.end());

        std::wstring wide(static_cast<size_t>(len) - 1, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, wide.data(), len);
        return wide;
    }

    fs::path getCommandProcessorPath() {
        std::string comspec = getEnvValue("ComSpec");
        if (!comspec.empty()) {
            return fs::path(comspec);
        }

        wchar_t buffer[MAX_PATH];
        UINT len = GetSystemDirectoryW(buffer, MAX_PATH);
        if (len == 0 || len >= MAX_PATH) {
            return fs::path(L"cmd.exe");
        }
        return fs::path(buffer) / "cmd.exe";
    }

    bool runWindowsShellCommand(const std::string& command, std::string& output, int& exitCode) {
        output.clear();
        exitCode = -1;
        if (command.empty()) return false;

        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;

        HANDLE readPipe = nullptr;
        HANDLE writePipe = nullptr;
        if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) {
            return false;
        }
        if (!SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0)) {
            CloseHandle(readPipe);
            CloseHandle(writePipe);
            return false;
        }

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        si.hStdOutput = writePipe;
        si.hStdError = writePipe;

        PROCESS_INFORMATION pi{};
        const fs::path cmdPath = getCommandProcessorPath();
        std::wstring commandLine = L"\"";
        commandLine += cmdPath.wstring();
        commandLine += L"\" /d /s /c \"";
        commandLine += utf8ToWide(command);
        commandLine += L"\"";

        std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
        mutableCommand.push_back(L'\0');

        BOOL launched = CreateProcessW(
            nullptr,
            mutableCommand.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &si,
            &pi
        );

        CloseHandle(writePipe);
        writePipe = nullptr;

        if (!launched) {
            CloseHandle(readPipe);
            return false;
        }

        std::array<char, 4096> buffer{};
        DWORD bytesRead = 0;
        while (ReadFile(readPipe, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr) &&
               bytesRead > 0) {
            output.append(buffer.data(), bytesRead);
        }

        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD processExitCode = 0;
        if (!GetExitCodeProcess(pi.hProcess, &processExitCode)) {
            processExitCode = static_cast<DWORD>(-1);
        }

        exitCode = static_cast<int>(processExitCode);

        CloseHandle(readPipe);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return true;
    }

    std::string runCapture(const std::string& command) {
        std::string output;
        int exitCode = 0;
        if (!runWindowsShellCommand(command, output, exitCode)) {
            return std::string();
        }
        return output;
    }

    std::string findVsDevCmd() {
        static bool resolved = false;
        static std::string cachedPath;
        if (resolved) return cachedPath;
        resolved = true;

        std::string vsInstall = getEnvValue("VSINSTALLDIR");
        if (!vsInstall.empty()) {
            fs::path candidate = fs::path(vsInstall) / "Common7" / "Tools" / "VsDevCmd.bat";
            if (fs::exists(candidate)) {
                cachedPath = candidate.string();
                return cachedPath;
            }
        }

        std::string programFilesX86 = getEnvValue("ProgramFiles(x86)");
        if (programFilesX86.empty()) {
            programFilesX86 = getEnvValue("ProgramFiles");
        }
        if (programFilesX86.empty()) return std::string();

        fs::path vswhere = fs::path(programFilesX86) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe";
        if (!fs::exists(vswhere)) return std::string();

        std::ostringstream cmd;
        cmd << "\"" << vswhere.string() << "\" -latest -products * -requires Microsoft.Component.MSBuild "
            << "-property installationPath";
        std::string installPath = trimCopy(runCapture(cmd.str()));
        if (installPath.empty()) return std::string();

        fs::path devCmd = fs::path(installPath) / "Common7" / "Tools" / "VsDevCmd.bat";
        if (fs::exists(devCmd)) {
            cachedPath = devCmd.string();
            return cachedPath;
        }
        return std::string();
    }

    // well, that's one way to make VS Harder to implement, For God's Sake, MICROSOFT!!!!
    std::string findVsTool(const char* toolName) {
        static std::unordered_map<std::string, std::string> cache;
        const std::string cacheKey = toolName ? std::string(toolName) : std::string();
        if (auto it = cache.find(cacheKey); it != cache.end()) {
            return it->second;
        }

        std::string programFilesX86 = getEnvValue("ProgramFiles(x86)");
        if (programFilesX86.empty()) {
            programFilesX86 = getEnvValue("ProgramFiles");
        }
        if (programFilesX86.empty()) return std::string();

        fs::path vswhere = fs::path(programFilesX86) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe";
        if (fs::exists(vswhere)) {
            std::ostringstream cmd;
            cmd << "\"" << vswhere.string() << "\" -latest -products * "
                << "-requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 "
                << "-find \"VC\\Tools\\MSVC\\**\\bin\\Hostx64\\x64\\" << toolName << "\"";
            std::string found = trimCopy(runCapture(cmd.str()));
            if (!found.empty() && fs::exists(found)) {
                cache.emplace(cacheKey, found);
                return found;
            }
        }

        std::string vsInstall = getEnvValue("VSINSTALLDIR");
        if (!vsInstall.empty()) {
            fs::path fallback = fs::path(vsInstall) / "VC" / "Tools" / "MSVC";
            if (fs::exists(fallback)) {
                for (const auto& entry : fs::directory_iterator(fallback)) {
                    if (!entry.is_directory()) continue;
                    fs::path candidate = entry.path() / "bin" / "Hostx64" / "x64" / toolName;
                    if (fs::exists(candidate)) {
                        cache.emplace(cacheKey, candidate.string());
                        return candidate.string();
                    }
                }
            }
        }
        return std::string();
    }

    std::string applyToolOverride(const std::string& command, const char* toolName,
                                  const std::string& toolPath) {
        if (toolPath.empty()) return command;
        std::string prefix = std::string(toolName) + " ";
        if (command.rfind(prefix, 0) != 0) return command;
        std::ostringstream replaced;
        replaced << "\"" << toolPath << "\" " << command.substr(prefix.size());
        return replaced.str();
    }

    std::string wrapWithVsDevCmdIfNeeded(const std::string& command) {
        std::string includeEnv = getEnvValue("INCLUDE");
        if (!includeEnv.empty()) return command;

        std::string vsDevCmd = findVsDevCmd();
        if (vsDevCmd.empty()) return command;

        std::ostringstream wrapped;
        // _popen() already runs through cmd.exe on Windows. Adding another
        // nested `cmd /c` here breaks quoted paths under `C:\Program Files\...`.
        wrapped << "call \""
                << vsDevCmd
                << "\" -arch=x64 -host_arch=x64 >nul && "
                << command;
        return wrapped.str();
    }
#endif
}

std::string ScriptCompiler::trim(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        start++;
    }
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        end--;
    }
    return value.substr(start, end - start);
}

std::string ScriptCompiler::escapeDefine(const std::string& def) {
    std::string escaped;
    escaped.reserve(def.size());
    for (char c : def) {
        if (c == '"') {
            escaped += "\\\"";
        } else {
            escaped += c;
        }
    }
    return escaped;
}

bool ScriptCompiler::loadConfig(const fs::path& configPath, ScriptBuildConfig& outConfig,
                                std::string& error) const {
    outConfig = ScriptBuildConfig();

    if (!fs::exists(configPath)) {
        error = "Config file not found: " + configPath.string();
        return false;
    }

    std::ifstream file(configPath);
    if (!file.is_open()) {
        error = "Unable to open config file: " + configPath.string();
        return false;
    }

    fs::path baseDir = configPath.parent_path();
    std::string line;
    size_t lineNumber = 0;
    while (std::getline(file, line)) {
        lineNumber++;
        std::string cleaned = trim(line);
        if (cleaned.empty() || cleaned[0] == '#') continue;

        size_t pos = cleaned.find('=');
        if (pos == std::string::npos) continue;

        std::string key = trim(cleaned.substr(0, pos));
        std::string value = trim(cleaned.substr(pos + 1));

        if (key == "cppStandard") {
            outConfig.cppStandard = value;
        } else if (key == "scriptsDir") {
            outConfig.scriptsDir = makeAbsolute(baseDir, value);
        } else if (key == "outDir") {
            outConfig.outDir = makeAbsolute(baseDir, value);
        } else if (key == "includeDir") {
            outConfig.includeDirs.push_back(makeAbsolute(baseDir, value));
        } else if (key == "define") {
            outConfig.defines.push_back(value);
        } else if (key == "linux.linkLib") {
            outConfig.linuxLinkLibs.push_back(value);
        } else if (key == "win.linkLib") {
            outConfig.windowsLinkLibs.push_back(value);
        } else {
            // Ignore unknown keys for now
        }
    }

    outConfig.scriptsDir = makeAbsolute(baseDir, outConfig.scriptsDir);
    outConfig.outDir = makeAbsolute(baseDir, outConfig.outDir);
    for (auto& dir : outConfig.includeDirs) {
        dir = makeAbsolute(baseDir, dir);
    }

    // Heuristic: auto-add engine include roots if ScriptRuntime.h is discoverable nearby.
    auto tryAddEngineRoot = [&](const fs::path& start) {
        std::error_code ec;
        auto addIfExists = [&](const fs::path& p) {
            if (p.empty()) return;
            if (fs::exists(p, ec)) {
                outConfig.includeDirs.push_back(p);
            }
        };
        fs::path candidate = start;
        for (int depth = 0; depth < 5 && !candidate.empty(); ++depth) {
            if (fs::exists(candidate / "src" / "ScriptRuntime.h", ec)) {
                addIfExists(candidate / "src");
                addIfExists(candidate / "include");
                addIfExists(candidate / "src/ThirdParty");
                addIfExists(candidate / "src/ThirdParty/glm");
                addIfExists(candidate / "src/ThirdParty/glad");
                addIfExists(candidate / "src/ThirdParty/imgui");
                addIfExists(candidate / "src/ThirdParty/imgui/backends");

                // Assimp headers live under include/, and generated config/revision headers
                // are emitted under build/*/src/ThirdParty/assimp/include.
                addIfExists(candidate / "src/ThirdParty/assimp/include");
                addIfExists(candidate / "build/src/ThirdParty/assimp/include");
                addIfExists(candidate / "build/player-cache/src/ThirdParty/assimp/include");
                return true;
            }
            candidate = candidate.parent_path();
        }
        return false;
    };

    tryAddEngineRoot(configPath.parent_path());
    tryAddEngineRoot(fs::current_path());
    tryAddEngineRoot(fs::current_path().parent_path());
    addBundledScriptSdkIncludeRoots(configPath.parent_path(), outConfig);
    addBundledScriptSdkIncludeRoots(fs::current_path(), outConfig);
    addBundledScriptSdkIncludeRoots(fs::current_path().parent_path(), outConfig);
#if defined(__linux__)
    {
        std::error_code ec;
        fs::path exe = fs::read_symlink("/proc/self/exe", ec);
        if (!ec) {
            tryAddEngineRoot(exe.parent_path());
            tryAddEngineRoot(exe.parent_path().parent_path());
            addBundledScriptSdkIncludeRoots(exe.parent_path(), outConfig);
            addBundledScriptSdkIncludeRoots(exe.parent_path().parent_path(), outConfig);
        }
    }
#elif defined(_WIN32)
    {
        wchar_t buffer[MAX_PATH];
        DWORD len = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
        if (len > 0) {
            fs::path exe(buffer);
            tryAddEngineRoot(exe.parent_path());
            tryAddEngineRoot(exe.parent_path().parent_path());
            addBundledScriptSdkIncludeRoots(exe.parent_path(), outConfig);
            addBundledScriptSdkIncludeRoots(exe.parent_path().parent_path(), outConfig);
        }
    }
#endif

    return true;
}

std::string ScriptCompiler::formatLinkFlag(const std::string& lib) {
    if (lib.rfind("-l", 0) == 0 || lib.rfind("-L", 0) == 0) return lib;
    if (lib.find('/') != std::string::npos || lib.find('\\') != std::string::npos) {
        return "\"" + lib + "\"";
    }
    return "-l" + lib;
}

bool ScriptCompiler::makeCommands(const ScriptBuildConfig& config, const fs::path& scriptPath,
                                  ScriptBuildCommands& outCommands, std::string& error) const {
    if (!fs::exists(scriptPath)) {
        error = "Script file not found: " + scriptPath.string();
        return false;
    }

    std::error_code ec;
    fs::path scriptAbs = fs::absolute(scriptPath, ec);
    if (ec) scriptAbs = scriptPath;

    fs::path relToScripts;
    relToScripts = fs::relative(scriptAbs, config.scriptsDir, ec);
    if (ec) {
        relToScripts.clear();
    }

    auto hasDotDot = [](const fs::path& path) {
        for (const auto& part : path) {
            if (part == "..") return true;
        }
        return false;
    };
    if (relToScripts.empty() || relToScripts.is_absolute() || hasDotDot(relToScripts)) {
        relToScripts.clear();
    }

    fs::path relativeParent = relToScripts.has_parent_path() ? relToScripts.parent_path() : fs::path();
    std::string baseName = scriptAbs.stem().string();
    fs::path objectPath = config.outDir / relativeParent / (baseName + ".o");
    fs::path secondaryObjectPath;
    fs::path dependencyPath;
    fs::path secondaryDependencyPath;

    fs::path binaryPath = config.outDir / relativeParent;
#ifdef _WIN32
    objectPath = config.outDir / relativeParent / (baseName + ".obj");
    binaryPath /= baseName + ".dll";
#else
    binaryPath /= baseName + ".so";
    dependencyPath = config.outDir / relativeParent / (baseName + ".d");
#endif

    std::string extLower = scriptAbs.extension().string();
    std::transform(extLower.begin(), extLower.end(), extLower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const bool isCSource = extLower == ".c";

    // Build a lightweight wrapper that exposes expected entry points with C linkage and optional deltaTime.
    auto readFileToString = [](const fs::path& path, std::string& contents) -> bool {
        std::ifstream f(path);
        if (!f.is_open()) return false;
        std::ostringstream ss;
        ss << f.rdbuf();
        contents = ss.str();
        return true;
    };

    struct FunctionSpec {
        bool present = false;
        bool takesDelta = false;
        bool takesContext = false;
    };

    auto detectFunction = [](const std::string& source, const std::string& name,
                             const std::string& contextPattern) -> FunctionSpec {
        FunctionSpec spec;
        try {
            const std::string prefix = "\\bvoid\\s+(?:IEnum\\s+)?";
            std::regex ctxDeltaPattern(prefix + name + "\\s*\\(\\s*" + contextPattern + "\\s*[^,\\)]*,[^\\)]*(float|double)[^\\)]*\\)");
            std::regex ctxOnlyPattern(prefix + name + "\\s*\\(\\s*" + contextPattern + "\\s*[^\\)]*\\)");
            std::regex deltaPattern(prefix + name + "\\s*\\(\\s*(float|double)[^\\)]*\\)");
            std::regex basicPattern(prefix + name + "\\s*\\(\\s*\\)");

            if (std::regex_search(source, ctxDeltaPattern)) {
                spec.present = true;
                spec.takesDelta = true;
                spec.takesContext = true;
                return spec;
            }
            if (std::regex_search(source, ctxOnlyPattern)) {
                spec.present = true;
                spec.takesContext = true;
                return spec;
            }
            if (std::regex_search(source, deltaPattern)) {
                spec.present = true;
                spec.takesDelta = true;
                return spec;
            }
            if (std::regex_search(source, basicPattern)) {
                spec.present = true;
            }
        } catch (...) {
            // If regex throws for any reason, fall through and treat as not present.
        }
        return spec;
    };

    std::string scriptSource;
    if (!readFileToString(scriptAbs, scriptSource)) {
        error = "Unable to read script file: " + scriptAbs.string();
        return false;
    }

    fs::path transpiledPath;
    fs::path compileSourcePath = scriptAbs;
    if (ModuCPPTranspiler::shouldTranspile(scriptAbs, scriptSource)) {
        ModuCPPTranspileResult transpiled;
        ModuCPPTranspiler transpiler;
        if (!transpiler.transpile(scriptAbs, scriptSource, transpiled, error)) {
            error = "ModuCPP transpile failed: " + error;
            return false;
        }

        transpiledPath = config.outDir / relativeParent / (baseName + ".moducpp.gen.cpp");
        if (!writeTextFileIfChanged(transpiledPath, transpiled.generatedSource, error)) {
            return false;
        }

        scriptSource = std::move(transpiled.generatedSource);
        compileSourcePath = transpiledPath;
    }

    auto hasExternCSymbol = [&](const std::string& symbol) {
        try {
            std::regex direct("extern\\s+\"C\"\\s+void\\s+" + symbol + "\\s*\\(");
            if (std::regex_search(scriptSource, direct)) return true;
            std::regex block("extern\\s+\"C\"\\s*\\{[\\s\\S]*?\\b" + symbol + "\\b");
            return std::regex_search(scriptSource, block);
        } catch (...) {
            return false;
        }
    };

    auto appendWindowsIncludesAndDefines = [&](std::ostringstream& cmd) {
        for (const auto& inc : config.includeDirs) {
            cmd << " /I\"" << inc.string() << "\"";
        }
        cmd << " /DMODULARITY_SCRIPT_IMPORTS";
        for (const auto& def : config.defines) {
            cmd << " /D\"" << escapeDefine(def) << "\"";
        }
    };

    auto appendPosixIncludesAndDefines = [&](std::ostringstream& cmd) {
        for (const auto& inc : config.includeDirs) {
            cmd << " -I\"" << inc.string() << "\"";
        }
        auto formatDefine = [&](const std::string& def) {
            std::string escaped = def;
            for (size_t pos = 0; pos < escaped.size(); ++pos) {
                if (escaped[pos] == '"') {
                    escaped.insert(pos, "\\");
                    ++pos;
                }
            }
            return std::string(" -D\"") + escaped + "\"";
        };
        for (const auto& def : config.defines) {
            cmd << formatDefine(def);
        }
    };

#ifdef _WIN32
    const std::optional<fs::path> hostImportLib = findHostImportLibrary();
    if (!hostImportLib) {
        const fs::path exePath = getCurrentExecutablePath();
        error = "Unable to locate the host import library for " +
                (exePath.empty() ? std::string("the running executable")
                                 : exePath.filename().string()) +
                ". Rebuild the current Windows target so its .lib is generated before compiling scripts.";
        return false;
    }
#endif

    fs::path wrapperPath;
    bool useWrapper = false;
    std::ostringstream compileCmd;
    std::ostringstream linkCmd;
    std::string buildSignaturePayload;

    if (isCSource) {
        const std::string cContextPattern = "(?:struct\\s+)?ModuScriptContext\\s*\\*";
        FunctionSpec beginSpec = detectFunction(scriptSource, "Modu_Begin", cContextPattern);
        FunctionSpec specSpec = detectFunction(scriptSource, "Modu_Spec", cContextPattern);
        FunctionSpec testEditorSpec = detectFunction(scriptSource, "Modu_TestEditor", cContextPattern);
        FunctionSpec updateSpec = detectFunction(scriptSource, "Modu_Update", cContextPattern);
        FunctionSpec tickUpdateSpec = detectFunction(scriptSource, "Modu_TickUpdate", cContextPattern);
        FunctionSpec inspectorSpec = detectFunction(scriptSource, "Modu_OnInspector", cContextPattern);
        FunctionSpec editorRenderSpec = detectFunction(scriptSource, "Modu_RenderEditorWindow", cContextPattern);
        FunctionSpec editorExitSpec = detectFunction(scriptSource, "Modu_ExitRenderEditorWindow", cContextPattern);

        useWrapper = beginSpec.present || specSpec.present || testEditorSpec.present ||
                     updateSpec.present || tickUpdateSpec.present || inspectorSpec.present ||
                     editorRenderSpec.present || editorExitSpec.present;
        if (!useWrapper) {
            error = "C script has no Modu_ hooks. Expected one of Modu_Begin/Modu_TickUpdate/Modu_OnInspector.";
            return false;
        }

        wrapperPath = config.outDir / relativeParent / (baseName + ".capi.wrap.cpp");
#ifdef _WIN32
        secondaryObjectPath = config.outDir / relativeParent / (baseName + ".wrap.obj");
#else
        secondaryObjectPath = config.outDir / relativeParent / (baseName + ".wrap.o");
        secondaryDependencyPath = config.outDir / relativeParent / (baseName + ".wrap.d");
#endif
        std::ostringstream wrapper;

        auto emitCImplDecl = [&](const char* name, const FunctionSpec& spec) {
            if (!spec.present) return;
            wrapper << "void " << name << "(";
            if (spec.takesContext && spec.takesDelta) {
                wrapper << "ModuScriptContext* ctx, float deltaTime";
            } else if (spec.takesContext) {
                wrapper << "ModuScriptContext* ctx";
            } else if (spec.takesDelta) {
                wrapper << "float deltaTime";
            }
            wrapper << ");\n";
        };

        wrapper << "#include \"ScriptRuntime.h\"\n";
        wrapper << "#include \"Engine.h\"\n";
        wrapper << "#include \"ThirdParty/imgui/imgui.h\"\n";
        wrapper << "#include <cstring>\n";
        wrapper << "#include <cstdio>\n\n";
        wrapper << "extern \"C\" {\n";
        wrapper << "struct ModuScriptContext { ScriptContext* ctx; };\n";
        wrapper << "struct ModuVec3 { float x; float y; float z; };\n\n";
        wrapper << "static ScriptContext* ModuAsCpp(ModuScriptContext* ctx) {\n";
        wrapper << "    return ctx ? ctx->ctx : nullptr;\n";
        wrapper << "}\n\n";
        wrapper << "int Modu_GetObjectId(ModuScriptContext* ctx) {\n";
        wrapper << "    ScriptContext* cpp = ModuAsCpp(ctx);\n";
        wrapper << "    return (cpp && cpp->object) ? cpp->object->id : -1;\n";
        wrapper << "}\n\n";
        wrapper << "int Modu_IsObjectEnabled(ModuScriptContext* ctx) {\n";
        wrapper << "    ScriptContext* cpp = ModuAsCpp(ctx);\n";
        wrapper << "    return (cpp && cpp->IsObjectEnabled()) ? 1 : 0;\n";
        wrapper << "}\n\n";
        wrapper << "void Modu_SetObjectEnabled(ModuScriptContext* ctx, int enabled) {\n";
        wrapper << "    ScriptContext* cpp = ModuAsCpp(ctx);\n";
        wrapper << "    if (cpp) cpp->SetObjectEnabled(enabled != 0);\n";
        wrapper << "}\n\n";
        wrapper << "ModuVec3 Modu_GetPosition(ModuScriptContext* ctx) {\n";
        wrapper << "    ModuVec3 out{0.0f, 0.0f, 0.0f};\n";
        wrapper << "    ScriptContext* cpp = ModuAsCpp(ctx);\n";
        wrapper << "    if (cpp && cpp->object) {\n";
        wrapper << "        out.x = cpp->object->position.x;\n";
        wrapper << "        out.y = cpp->object->position.y;\n";
        wrapper << "        out.z = cpp->object->position.z;\n";
        wrapper << "    }\n";
        wrapper << "    return out;\n";
        wrapper << "}\n\n";
        wrapper << "ModuVec3 Modu_GetRotation(ModuScriptContext* ctx) {\n";
        wrapper << "    ModuVec3 out{0.0f, 0.0f, 0.0f};\n";
        wrapper << "    ScriptContext* cpp = ModuAsCpp(ctx);\n";
        wrapper << "    if (cpp && cpp->object) {\n";
        wrapper << "        out.x = cpp->object->rotation.x;\n";
        wrapper << "        out.y = cpp->object->rotation.y;\n";
        wrapper << "        out.z = cpp->object->rotation.z;\n";
        wrapper << "    }\n";
        wrapper << "    return out;\n";
        wrapper << "}\n\n";
        wrapper << "ModuVec3 Modu_GetScale(ModuScriptContext* ctx) {\n";
        wrapper << "    ModuVec3 out{1.0f, 1.0f, 1.0f};\n";
        wrapper << "    ScriptContext* cpp = ModuAsCpp(ctx);\n";
        wrapper << "    if (cpp && cpp->object) {\n";
        wrapper << "        out.x = cpp->object->scale.x;\n";
        wrapper << "        out.y = cpp->object->scale.y;\n";
        wrapper << "        out.z = cpp->object->scale.z;\n";
        wrapper << "    }\n";
        wrapper << "    return out;\n";
        wrapper << "}\n\n";
        wrapper << "void Modu_SetPosition(ModuScriptContext* ctx, ModuVec3 value) {\n";
        wrapper << "    ScriptContext* cpp = ModuAsCpp(ctx);\n";
        wrapper << "    if (cpp) cpp->SetPosition(glm::vec3(value.x, value.y, value.z));\n";
        wrapper << "}\n\n";
        wrapper << "void Modu_SetRotation(ModuScriptContext* ctx, ModuVec3 value) {\n";
        wrapper << "    ScriptContext* cpp = ModuAsCpp(ctx);\n";
        wrapper << "    if (cpp) cpp->SetRotation(glm::vec3(value.x, value.y, value.z));\n";
        wrapper << "}\n\n";
        wrapper << "void Modu_SetScale(ModuScriptContext* ctx, ModuVec3 value) {\n";
        wrapper << "    ScriptContext* cpp = ModuAsCpp(ctx);\n";
        wrapper << "    if (cpp) cpp->SetScale(glm::vec3(value.x, value.y, value.z));\n";
        wrapper << "}\n\n";
        wrapper << "int Modu_SetRigidbodyVelocity(ModuScriptContext* ctx, ModuVec3 velocity) {\n";
        wrapper << "    ScriptContext* cpp = ModuAsCpp(ctx);\n";
        wrapper << "    return (cpp && cpp->SetRigidbodyVelocity(glm::vec3(velocity.x, velocity.y, velocity.z))) ? 1 : 0;\n";
        wrapper << "}\n\n";
        wrapper << "int Modu_AddRigidbodyForce(ModuScriptContext* ctx, ModuVec3 force) {\n";
        wrapper << "    ScriptContext* cpp = ModuAsCpp(ctx);\n";
        wrapper << "    return (cpp && cpp->AddRigidbodyForce(glm::vec3(force.x, force.y, force.z))) ? 1 : 0;\n";
        wrapper << "}\n\n";
        wrapper << "int Modu_GetRigidbodyVelocity(ModuScriptContext* ctx, ModuVec3* outVelocity) {\n";
        wrapper << "    if (!outVelocity) return 0;\n";
        wrapper << "    outVelocity->x = 0.0f;\n";
        wrapper << "    outVelocity->y = 0.0f;\n";
        wrapper << "    outVelocity->z = 0.0f;\n";
        wrapper << "    ScriptContext* cpp = ModuAsCpp(ctx);\n";
        wrapper << "    if (!cpp) return 0;\n";
        wrapper << "    glm::vec3 vel(0.0f);\n";
        wrapper << "    if (!cpp->GetRigidbodyVelocity(vel)) return 0;\n";
        wrapper << "    outVelocity->x = vel.x;\n";
        wrapper << "    outVelocity->y = vel.y;\n";
        wrapper << "    outVelocity->z = vel.z;\n";
        wrapper << "    return 1;\n";
        wrapper << "}\n\n";
        wrapper << "int Modu_SetRigidbodyRotation(ModuScriptContext* ctx, ModuVec3 rotation) {\n";
        wrapper << "    ScriptContext* cpp = ModuAsCpp(ctx);\n";
        wrapper << "    return (cpp && cpp->SetRigidbodyRotation(glm::vec3(rotation.x, rotation.y, rotation.z))) ? 1 : 0;\n";
        wrapper << "}\n\n";
        wrapper << "int Modu_EnsureCapsuleCollider(ModuScriptContext* ctx, float height, float radius) {\n";
        wrapper << "    ScriptContext* cpp = ModuAsCpp(ctx);\n";
        wrapper << "    return (cpp && cpp->EnsureCapsuleCollider(height, radius)) ? 1 : 0;\n";
        wrapper << "}\n\n";
        wrapper << "int Modu_EnsureRigidbody(ModuScriptContext* ctx, int useGravity, int kinematic) {\n";
        wrapper << "    ScriptContext* cpp = ModuAsCpp(ctx);\n";
        wrapper << "    return (cpp && cpp->EnsureRigidbody(useGravity != 0, kinematic != 0)) ? 1 : 0;\n";
        wrapper << "}\n\n";
        wrapper << "int Modu_HasAnimation(ModuScriptContext* ctx) {\n";
        wrapper << "    ScriptContext* cpp = ModuAsCpp(ctx);\n";
        wrapper << "    return (cpp && cpp->HasAnimation()) ? 1 : 0;\n";
        wrapper << "}\n\n";
        wrapper << "int Modu_PlayAnimation(ModuScriptContext* ctx, int restart) {\n";
        wrapper << "    ScriptContext* cpp = ModuAsCpp(ctx);\n";
        wrapper << "    return (cpp && cpp->PlayAnimation(restart != 0)) ? 1 : 0;\n";
        wrapper << "}\n\n";
        wrapper << "int Modu_StopAnimation(ModuScriptContext* ctx, int resetTime) {\n";
        wrapper << "    ScriptContext* cpp = ModuAsCpp(ctx);\n";
        wrapper << "    return (cpp && cpp->StopAnimation(resetTime != 0)) ? 1 : 0;\n";
        wrapper << "}\n\n";
        wrapper << "int Modu_PauseAnimation(ModuScriptContext* ctx, int pause) {\n";
        wrapper << "    ScriptContext* cpp = ModuAsCpp(ctx);\n";
        wrapper << "    return (cpp && cpp->PauseAnimation(pause != 0)) ? 1 : 0;\n";
        wrapper << "}\n\n";
        wrapper << "int Modu_ReverseAnimation(ModuScriptContext* ctx, int restartIfStopped) {\n";
        wrapper << "    ScriptContext* cpp = ModuAsCpp(ctx);\n";
        wrapper << "    return (cpp && cpp->ReverseAnimation(restartIfStopped != 0)) ? 1 : 0;\n";
        wrapper << "}\n\n";
        wrapper << "int Modu_SetAnimationTime(ModuScriptContext* ctx, float timeSeconds) {\n";
        wrapper << "    ScriptContext* cpp = ModuAsCpp(ctx);\n";
        wrapper << "    return (cpp && cpp->SetAnimationTime(timeSeconds)) ? 1 : 0;\n";
        wrapper << "}\n\n";
        wrapper << "float Modu_GetAnimationTime(ModuScriptContext* ctx) {\n";
        wrapper << "    ScriptContext* cpp = ModuAsCpp(ctx);\n";
        wrapper << "    return cpp ? cpp->GetAnimationTime() : 0.0f;\n";
        wrapper << "}\n\n";
        wrapper << "int Modu_IsAnimationPlaying(ModuScriptContext* ctx) {\n";
        wrapper << "    ScriptContext* cpp = ModuAsCpp(ctx);\n";
        wrapper << "    return (cpp && cpp->IsAnimationPlaying()) ? 1 : 0;\n";
        wrapper << "}\n\n";
        wrapper << "int Modu_SetAnimationLoop(ModuScriptContext* ctx, int loop) {\n";
        wrapper << "    ScriptContext* cpp = ModuAsCpp(ctx);\n";
        wrapper << "    return (cpp && cpp->SetAnimationLoop(loop != 0)) ? 1 : 0;\n";
        wrapper << "}\n\n";
        wrapper << "int Modu_SetAnimationPlaySpeed(ModuScriptContext* ctx, float speed) {\n";
        wrapper << "    ScriptContext* cpp = ModuAsCpp(ctx);\n";
        wrapper << "    return (cpp && cpp->SetAnimationPlaySpeed(speed)) ? 1 : 0;\n";
        wrapper << "}\n\n";
        wrapper << "int Modu_SetAnimationPlayOnAwake(ModuScriptContext* ctx, int playOnAwake) {\n";
        wrapper << "    ScriptContext* cpp = ModuAsCpp(ctx);\n";
        wrapper << "    return (cpp && cpp->SetAnimationPlayOnAwake(playOnAwake != 0)) ? 1 : 0;\n";
        wrapper << "}\n\n";
        wrapper << "int Modu_IsSprintDown(ModuScriptContext* ctx) {\n";
        wrapper << "    ScriptContext* cpp = ModuAsCpp(ctx);\n";
        wrapper << "    return (cpp && cpp->IsSprintDown()) ? 1 : 0;\n";
        wrapper << "}\n\n";
        wrapper << "int Modu_IsJumpDown(ModuScriptContext* ctx) {\n";
        wrapper << "    ScriptContext* cpp = ModuAsCpp(ctx);\n";
        wrapper << "    return (cpp && cpp->IsJumpDown()) ? 1 : 0;\n";
        wrapper << "}\n\n";
        wrapper << "ModuVec3 Modu_GetMoveInputWASD(ModuScriptContext* ctx, float pitchDeg, float yawDeg) {\n";
        wrapper << "    ModuVec3 out{0.0f, 0.0f, 0.0f};\n";
        wrapper << "    ScriptContext* cpp = ModuAsCpp(ctx);\n";
        wrapper << "    if (!cpp) return out;\n";
        wrapper << "    glm::vec3 move = cpp->GetMoveInputWASD(pitchDeg, yawDeg);\n";
        wrapper << "    out.x = move.x;\n";
        wrapper << "    out.y = move.y;\n";
        wrapper << "    out.z = move.z;\n";
        wrapper << "    return out;\n";
        wrapper << "}\n\n";
        wrapper << "int Modu_ApplyMouseLook(ModuScriptContext* ctx, float* pitchDeg, float* yawDeg,\n";
        wrapper << "                        float sensitivity, float maxDelta, float deltaTime, int requireMouseButton) {\n";
        wrapper << "    ScriptContext* cpp = ModuAsCpp(ctx);\n";
        wrapper << "    if (!cpp || !pitchDeg || !yawDeg) return 0;\n";
        wrapper << "    return cpp->ApplyMouseLook(*pitchDeg, *yawDeg, sensitivity, maxDelta, deltaTime,\n";
        wrapper << "                               requireMouseButton != 0) ? 1 : 0;\n";
        wrapper << "}\n\n";
        wrapper << "int Modu_RaycastClosestDetailed(ModuScriptContext* ctx, ModuVec3 origin, ModuVec3 dir, float distance,\n";
        wrapper << "                                ModuVec3* hitPos, ModuVec3* hitNormal, float* hitDistance,\n";
        wrapper << "                                int* hitObjectId, ModuVec3* hitObjectVelocity,\n";
        wrapper << "                                float* hitStaticFriction, float* hitDynamicFriction) {\n";
        wrapper << "    ScriptContext* cpp = ModuAsCpp(ctx);\n";
        wrapper << "    if (!cpp) return 0;\n";
        wrapper << "    glm::vec3 hp(0.0f), hn(0.0f, 1.0f, 0.0f), hov(0.0f);\n";
        wrapper << "    float hd = 0.0f;\n";
        wrapper << "    int hid = -1;\n";
        wrapper << "    float hsf = 0.9f;\n";
        wrapper << "    float hdf = 0.9f;\n";
        wrapper << "    bool ok = cpp->RaycastClosestDetailed(glm::vec3(origin.x, origin.y, origin.z),\n";
        wrapper << "                                          glm::vec3(dir.x, dir.y, dir.z), distance,\n";
        wrapper << "                                          hitPos ? &hp : nullptr,\n";
        wrapper << "                                          hitNormal ? &hn : nullptr,\n";
        wrapper << "                                          hitDistance ? &hd : nullptr,\n";
        wrapper << "                                          hitObjectId ? &hid : nullptr,\n";
        wrapper << "                                          hitObjectVelocity ? &hov : nullptr,\n";
        wrapper << "                                          hitStaticFriction ? &hsf : nullptr,\n";
        wrapper << "                                          hitDynamicFriction ? &hdf : nullptr);\n";
        wrapper << "    if (!ok) return 0;\n";
        wrapper << "    if (hitPos) { hitPos->x = hp.x; hitPos->y = hp.y; hitPos->z = hp.z; }\n";
        wrapper << "    if (hitNormal) { hitNormal->x = hn.x; hitNormal->y = hn.y; hitNormal->z = hn.z; }\n";
        wrapper << "    if (hitDistance) *hitDistance = hd;\n";
        wrapper << "    if (hitObjectId) *hitObjectId = hid;\n";
        wrapper << "    if (hitObjectVelocity) { hitObjectVelocity->x = hov.x; hitObjectVelocity->y = hov.y; hitObjectVelocity->z = hov.z; }\n";
        wrapper << "    if (hitStaticFriction) *hitStaticFriction = hsf;\n";
        wrapper << "    if (hitDynamicFriction) *hitDynamicFriction = hdf;\n";
        wrapper << "    return 1;\n";
        wrapper << "}\n\n";
        wrapper << "void Modu_AddConsoleMessage(ModuScriptContext* ctx, const char* message, int type) {\n";
        wrapper << "    ScriptContext* cpp = ModuAsCpp(ctx);\n";
        wrapper << "    if (!cpp) return;\n";
        wrapper << "    ConsoleMessageType msgType = ConsoleMessageType::Info;\n";
        wrapper << "    switch (type) {\n";
        wrapper << "        case 1: msgType = ConsoleMessageType::Warning; break;\n";
        wrapper << "        case 2: msgType = ConsoleMessageType::Error; break;\n";
        wrapper << "        case 3: msgType = ConsoleMessageType::Success; break;\n";
        wrapper << "        default: msgType = ConsoleMessageType::Info; break;\n";
        wrapper << "    }\n";
        wrapper << "    cpp->AddConsoleMessage(message ? message : \"\", msgType);\n";
        wrapper << "}\n\n";
        wrapper << "float Modu_GetSettingFloat(ModuScriptContext* ctx, const char* key, float fallback) {\n";
        wrapper << "    ScriptContext* cpp = ModuAsCpp(ctx);\n";
        wrapper << "    if (!cpp || !key) return fallback;\n";
        wrapper << "    return cpp->GetSettingFloat(key, fallback);\n";
        wrapper << "}\n\n";
        wrapper << "void Modu_SetSettingFloat(ModuScriptContext* ctx, const char* key, float value) {\n";
        wrapper << "    ScriptContext* cpp = ModuAsCpp(ctx);\n";
        wrapper << "    if (!cpp || !key) return;\n";
        wrapper << "    cpp->SetSettingFloat(key, value);\n";
        wrapper << "}\n\n";
        wrapper << "int Modu_GetSettingBool(ModuScriptContext* ctx, const char* key, int fallback) {\n";
        wrapper << "    ScriptContext* cpp = ModuAsCpp(ctx);\n";
        wrapper << "    if (!cpp || !key) return fallback ? 1 : 0;\n";
        wrapper << "    return cpp->GetSettingBool(key, fallback != 0) ? 1 : 0;\n";
        wrapper << "}\n\n";
        wrapper << "void Modu_SetSettingBool(ModuScriptContext* ctx, const char* key, int value) {\n";
        wrapper << "    ScriptContext* cpp = ModuAsCpp(ctx);\n";
        wrapper << "    if (!cpp || !key) return;\n";
        wrapper << "    cpp->SetSettingBool(key, value != 0);\n";
        wrapper << "}\n\n";
        wrapper << "void Modu_SetSettingString(ModuScriptContext* ctx, const char* key, const char* value) {\n";
        wrapper << "    ScriptContext* cpp = ModuAsCpp(ctx);\n";
        wrapper << "    if (!cpp || !key) return;\n";
        wrapper << "    cpp->SetSetting(key, value ? value : \"\");\n";
        wrapper << "}\n\n";
        wrapper << "int Modu_GetSettingString(ModuScriptContext* ctx, const char* key, const char* fallback,\n";
        wrapper << "                          char* outBuffer, int outBufferSize) {\n";
        wrapper << "    if (!outBuffer || outBufferSize <= 0) return 0;\n";
        wrapper << "    outBuffer[0] = '\\0';\n";
        wrapper << "    ScriptContext* cpp = ModuAsCpp(ctx);\n";
        wrapper << "    if (!cpp || !key) {\n";
        wrapper << "        if (fallback) {\n";
        wrapper << "            std::snprintf(outBuffer, static_cast<size_t>(outBufferSize), \"%s\", fallback);\n";
        wrapper << "            return 1;\n";
        wrapper << "        }\n";
        wrapper << "        return 0;\n";
        wrapper << "    }\n";
        wrapper << "    std::string value = cpp->GetSetting(key, fallback ? fallback : \"\");\n";
        wrapper << "    std::snprintf(outBuffer, static_cast<size_t>(outBufferSize), \"%s\", value.c_str());\n";
        wrapper << "    return 1;\n";
        wrapper << "}\n\n";
        wrapper << "int Modu_GetSpriteClipCount(ModuScriptContext* ctx) {\n";
        wrapper << "    ScriptContext* cpp = ModuAsCpp(ctx);\n";
        wrapper << "    return cpp ? cpp->GetSpriteClipCount() : 0;\n";
        wrapper << "}\n\n";
        wrapper << "int Modu_GetSpriteClipIndex(ModuScriptContext* ctx) {\n";
        wrapper << "    ScriptContext* cpp = ModuAsCpp(ctx);\n";
        wrapper << "    return cpp ? cpp->GetSpriteClipIndex() : -1;\n";
        wrapper << "}\n\n";
        wrapper << "int Modu_SetSpriteClipIndex(ModuScriptContext* ctx, int index) {\n";
        wrapper << "    ScriptContext* cpp = ModuAsCpp(ctx);\n";
        wrapper << "    return (cpp && cpp->SetSpriteClipIndex(index)) ? 1 : 0;\n";
        wrapper << "}\n\n";
        wrapper << "int Modu_SetSpriteClipName(ModuScriptContext* ctx, const char* name) {\n";
        wrapper << "    ScriptContext* cpp = ModuAsCpp(ctx);\n";
        wrapper << "    return (cpp && cpp->SetSpriteClipName(name ? name : \"\")) ? 1 : 0;\n";
        wrapper << "}\n\n";
        wrapper << "int Modu_GetSpriteClipName(ModuScriptContext* ctx, char* outBuffer, int outBufferSize) {\n";
        wrapper << "    if (!outBuffer || outBufferSize <= 0) return 0;\n";
        wrapper << "    outBuffer[0] = '\\0';\n";
        wrapper << "    ScriptContext* cpp = ModuAsCpp(ctx);\n";
        wrapper << "    if (!cpp) return 0;\n";
        wrapper << "    std::string value = cpp->GetSpriteClipName();\n";
        wrapper << "    std::snprintf(outBuffer, static_cast<size_t>(outBufferSize), \"%s\", value.c_str());\n";
        wrapper << "    return !value.empty() ? 1 : 0;\n";
        wrapper << "}\n\n";
        wrapper << "int Modu_GetSpriteClipNameAt(ModuScriptContext* ctx, int index, char* outBuffer, int outBufferSize) {\n";
        wrapper << "    if (!outBuffer || outBufferSize <= 0) return 0;\n";
        wrapper << "    outBuffer[0] = '\\0';\n";
        wrapper << "    ScriptContext* cpp = ModuAsCpp(ctx);\n";
        wrapper << "    if (!cpp) return 0;\n";
        wrapper << "    std::string value = cpp->GetSpriteClipNameAt(index);\n";
        wrapper << "    std::snprintf(outBuffer, static_cast<size_t>(outBufferSize), \"%s\", value.c_str());\n";
        wrapper << "    return !value.empty() ? 1 : 0;\n";
        wrapper << "}\n\n";
        wrapper << "void Modu_InspectorText(ModuScriptContext* ctx, const char* text) {\n";
        wrapper << "    (void)ctx;\n";
        wrapper << "    ImGui::TextUnformatted(text ? text : \"\");\n";
        wrapper << "}\n\n";
        wrapper << "void Modu_InspectorSeparator(ModuScriptContext* ctx) {\n";
        wrapper << "    (void)ctx;\n";
        wrapper << "    ImGui::Separator();\n";
        wrapper << "}\n\n";
        wrapper << "int Modu_InspectorDragFloat(ModuScriptContext* ctx, const char* label, float* value,\n";
        wrapper << "                            float speed, float minValue, float maxValue, const char* format) {\n";
        wrapper << "    (void)ctx;\n";
        wrapper << "    if (!label || !value) return 0;\n";
        wrapper << "    const char* fmt = (format && format[0]) ? format : \"%.3f\";\n";
        wrapper << "    return ImGui::DragFloat(label, value, speed, minValue, maxValue, fmt) ? 1 : 0;\n";
        wrapper << "}\n\n";
        wrapper << "int Modu_InspectorDragFloat2(ModuScriptContext* ctx, const char* label, float* value,\n";
        wrapper << "                             float speed, float minValue, float maxValue, const char* format) {\n";
        wrapper << "    (void)ctx;\n";
        wrapper << "    if (!label || !value) return 0;\n";
        wrapper << "    const char* fmt = (format && format[0]) ? format : \"%.3f\";\n";
        wrapper << "    return ImGui::DragFloat2(label, value, speed, minValue, maxValue, fmt) ? 1 : 0;\n";
        wrapper << "}\n\n";
        wrapper << "int Modu_InspectorDragFloat3(ModuScriptContext* ctx, const char* label, float* value,\n";
        wrapper << "                             float speed, float minValue, float maxValue, const char* format) {\n";
        wrapper << "    (void)ctx;\n";
        wrapper << "    if (!label || !value) return 0;\n";
        wrapper << "    const char* fmt = (format && format[0]) ? format : \"%.3f\";\n";
        wrapper << "    return ImGui::DragFloat3(label, value, speed, minValue, maxValue, fmt) ? 1 : 0;\n";
        wrapper << "}\n\n";
        wrapper << "int Modu_InspectorCheckbox(ModuScriptContext* ctx, const char* label, int* value) {\n";
        wrapper << "    (void)ctx;\n";
        wrapper << "    if (!label || !value) return 0;\n";
        wrapper << "    bool checked = (*value != 0);\n";
        wrapper << "    bool changed = ImGui::Checkbox(label, &checked);\n";
        wrapper << "    *value = checked ? 1 : 0;\n";
        wrapper << "    return changed ? 1 : 0;\n";
        wrapper << "}\n\n";
        wrapper << "int Modu_InspectorObject(ModuScriptContext* ctx, const char* label, int* objectId) {\n";
        wrapper << "    if (!objectId) return 0;\n";
        wrapper << "    ScriptContext* cpp = ModuAsCpp(ctx);\n";
        wrapper << "    int currentId = *objectId;\n";
        wrapper << "    std::string labelText = (label && label[0]) ? label : \"Object\";\n";
        wrapper << "    std::string pickerId = \"##ObjectPicker_\" + labelText;\n";
        wrapper << "    std::string useSelectedId = \"Use Selected##ObjectPicker_\" + labelText;\n";
        wrapper << "    std::string clearId = \"Clear##ObjectPicker_\" + labelText;\n";
        wrapper << "    std::string display = \"None\";\n";
        wrapper << "    if (cpp && currentId >= 0) {\n";
        wrapper << "        if (SceneObject* current = cpp->FindObjectById(currentId)) {\n";
        wrapper << "            display = current->name + \" (\" + std::to_string(current->id) + \")\";\n";
        wrapper << "        } else {\n";
        wrapper << "            currentId = -1;\n";
        wrapper << "        }\n";
        wrapper << "    }\n";
        wrapper << "    bool changed = false;\n";
        wrapper << "    ImGui::TextUnformatted(labelText.c_str());\n";
        wrapper << "    bool comboOpen = ImGui::BeginCombo(pickerId.c_str(), display.c_str());\n";
        wrapper << "    if (ImGui::BeginDragDropTarget()) {\n";
        wrapper << "        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(\"SCENE_OBJECT\")) {\n";
        wrapper << "            if (payload->DataSize == sizeof(int)) {\n";
        wrapper << "                currentId = *(const int*)payload->Data;\n";
        wrapper << "                changed = true;\n";
        wrapper << "            }\n";
        wrapper << "        }\n";
        wrapper << "        ImGui::EndDragDropTarget();\n";
        wrapper << "    }\n";
        wrapper << "    if (comboOpen) {\n";
        wrapper << "        if (ImGui::Selectable(\"None\", currentId < 0)) {\n";
        wrapper << "            currentId = -1;\n";
        wrapper << "            changed = true;\n";
        wrapper << "        }\n";
        wrapper << "        if (cpp && cpp->engine) {\n";
        wrapper << "            const auto& objects = cpp->engine->getSceneObjects();\n";
        wrapper << "            for (const SceneObject& candidate : objects) {\n";
        wrapper << "                std::string candidateLabel = candidate.name + \" (\" + std::to_string(candidate.id) + \")\";\n";
        wrapper << "                bool selected = (candidate.id == currentId);\n";
        wrapper << "                if (ImGui::Selectable(candidateLabel.c_str(), selected)) {\n";
        wrapper << "                    currentId = candidate.id;\n";
        wrapper << "                    changed = true;\n";
        wrapper << "                }\n";
        wrapper << "            }\n";
        wrapper << "        }\n";
        wrapper << "        ImGui::EndCombo();\n";
        wrapper << "    }\n";
        wrapper << "    if (cpp && ImGui::Button(useSelectedId.c_str())) {\n";
        wrapper << "        int selected = cpp->GetSelectedObjectId();\n";
        wrapper << "        if (selected >= 0) {\n";
        wrapper << "            currentId = selected;\n";
        wrapper << "            changed = true;\n";
        wrapper << "        }\n";
        wrapper << "    }\n";
        wrapper << "    if (ImGui::Button(clearId.c_str())) {\n";
        wrapper << "        if (currentId >= 0) {\n";
        wrapper << "            currentId = -1;\n";
        wrapper << "            changed = true;\n";
        wrapper << "        }\n";
        wrapper << "    }\n";
        wrapper << "    if (changed) {\n";
        wrapper << "        *objectId = currentId;\n";
        wrapper << "    }\n";
        wrapper << "    return changed ? 1 : 0;\n";
        wrapper << "}\n\n";

        emitCImplDecl("Modu_Begin", beginSpec);
        emitCImplDecl("Modu_Spec", specSpec);
        emitCImplDecl("Modu_TestEditor", testEditorSpec);
        emitCImplDecl("Modu_Update", updateSpec);
        emitCImplDecl("Modu_TickUpdate", tickUpdateSpec);
        emitCImplDecl("Modu_OnInspector", inspectorSpec);
        emitCImplDecl("Modu_RenderEditorWindow", editorRenderSpec);
        emitCImplDecl("Modu_ExitRenderEditorWindow", editorExitSpec);
        if (beginSpec.present || specSpec.present || testEditorSpec.present || updateSpec.present ||
            tickUpdateSpec.present || inspectorSpec.present || editorRenderSpec.present || editorExitSpec.present) {
            wrapper << "\n";
        }

        auto emitScriptBridge = [&](const char* exportedName, const char* implName,
                                    const FunctionSpec& spec) {
            if (!spec.present) return;
            wrapper << "void " << exportedName << "(ScriptContext& ctx, float deltaTime) {\n";
            wrapper << "    ModuScriptContext cctx{&ctx};\n";
            if (spec.takesContext && spec.takesDelta) {
                wrapper << "    " << implName << "(&cctx, deltaTime);\n";
            } else if (spec.takesContext) {
                wrapper << "    (void)deltaTime;\n";
                wrapper << "    " << implName << "(&cctx);\n";
            } else if (spec.takesDelta) {
                wrapper << "    " << implName << "(deltaTime);\n";
            } else {
                wrapper << "    (void)cctx;\n";
                wrapper << "    (void)deltaTime;\n";
                wrapper << "    " << implName << "();\n";
            }
            wrapper << "}\n\n";
        };

        auto emitEditorBridge = [&](const char* exportedName, const char* implName,
                                    const FunctionSpec& spec) {
            if (!spec.present) return;
            wrapper << "void " << exportedName << "(ScriptContext& ctx) {\n";
            wrapper << "    ModuScriptContext cctx{&ctx};\n";
            if (spec.takesContext) {
                wrapper << "    " << implName << "(&cctx);\n";
            } else if (spec.takesDelta) {
                wrapper << "    " << implName << "(0.0f);\n";
            } else {
                wrapper << "    (void)cctx;\n";
                wrapper << "    " << implName << "();\n";
            }
            wrapper << "}\n\n";
        };

        emitScriptBridge("Script_Begin", "Modu_Begin", beginSpec);
        emitScriptBridge("Script_Spec", "Modu_Spec", specSpec);
        emitScriptBridge("Script_TestEditor", "Modu_TestEditor", testEditorSpec);
        emitScriptBridge("Script_Update", "Modu_Update", updateSpec);
        emitScriptBridge("Script_TickUpdate", "Modu_TickUpdate", tickUpdateSpec);
        emitEditorBridge("Script_OnInspector", "Modu_OnInspector", inspectorSpec);
        emitEditorBridge("RenderEditorWindow", "Modu_RenderEditorWindow", editorRenderSpec);
        emitEditorBridge("ExitRenderEditorWindow", "Modu_ExitRenderEditorWindow", editorExitSpec);
        wrapper << "}\n";
        if (!writeTextFileIfChanged(wrapperPath, wrapper.str(), error)) {
            return false;
        }

#ifdef _WIN32
        fs::path scriptRspPath = config.outDir / relativeParent / (baseName + ".compile.rsp");
        fs::path wrapperRspPath = config.outDir / relativeParent / (baseName + ".wrap.compile.rsp");
        fs::path linkRspPath = config.outDir / relativeParent / (baseName + ".link.rsp");

        std::ostringstream scriptRsp;
        scriptRsp << "/nologo /TP /std:" << config.cppStandard << " /MD /Zi /Od";
        appendWindowsIncludesAndDefines(scriptRsp);
        scriptRsp << " /c \"" << compileSourcePath.string() << "\" /Fo\"" << objectPath.string() << "\"";

        std::ostringstream wrapperRsp;
        wrapperRsp << "/nologo /TP /std:" << config.cppStandard << " /EHsc /MD /Zi /Od";
        appendWindowsIncludesAndDefines(wrapperRsp);
        wrapperRsp << " /c \"" << wrapperPath.string() << "\" /Fo\"" << secondaryObjectPath.string() << "\"";

        std::ostringstream linkRsp;
        linkRsp << "/nologo /DLL \"" << objectPath.string() << "\" \"" << secondaryObjectPath.string()
                << "\" /OUT:\"" << binaryPath.string() << "\""
                << " \"" << hostImportLib->string() << "\"";
        for (const auto& lib : config.windowsLinkLibs) {
            linkRsp << " " << lib;
        }

        if (!writeTextFileIfChanged(scriptRspPath, scriptRsp.str(), error) ||
            !writeTextFileIfChanged(wrapperRspPath, wrapperRsp.str(), error) ||
            !writeTextFileIfChanged(linkRspPath, linkRsp.str(), error)) {
            return false;
        }

        compileCmd << "cl @\"" << scriptRspPath.string() << "\"";
        compileCmd << " && ";
        compileCmd << "cl @\"" << wrapperRspPath.string() << "\"";
        linkCmd << "link @\"" << linkRspPath.string() << "\"";
        buildSignaturePayload = scriptRsp.str() + "\n---rsp---\n" + wrapperRsp.str() + "\n---rsp---\n" + linkRsp.str();
#else
        compileCmd << posixCompileDriver(true) << " -std=" << config.cppStandard << " -fPIC -O0 -g";
        appendPosixIncludesAndDefines(compileCmd);
        compileCmd << " -MMD -MF \"" << dependencyPath.string() << "\"";
        compileCmd << " -c \"" << compileSourcePath.string() << "\" -o \"" << objectPath.string() << "\"";
        compileCmd << " && ";
        compileCmd << posixCompileDriver(true) << " -std=" << config.cppStandard << " -fPIC -O0 -g";
        appendPosixIncludesAndDefines(compileCmd);
        compileCmd << " -MMD -MF \"" << secondaryDependencyPath.string() << "\"";
        compileCmd << " -c \"" << wrapperPath.string() << "\" -o \"" << secondaryObjectPath.string() << "\"";
        linkCmd << "g++ -shared \"" << objectPath.string() << "\" \"" << secondaryObjectPath.string()
                << "\" -o \"" << binaryPath.string() << "\"";
        for (const auto& lib : config.linuxLinkLibs) {
            linkCmd << " " << formatLinkFlag(lib);
        }
#endif
    } else {
        const std::string cppContextPattern = "ScriptContext\\s*[&*]";
        FunctionSpec beginSpec = detectFunction(scriptSource, "Begin", cppContextPattern);
        FunctionSpec specSpec = detectFunction(scriptSource, "Spec", cppContextPattern);
        FunctionSpec testEditorSpec = detectFunction(scriptSource, "TestEditor", cppContextPattern);
        FunctionSpec updateSpec = detectFunction(scriptSource, "Update", cppContextPattern);
        FunctionSpec tickUpdateSpec = detectFunction(scriptSource, "TickUpdate", cppContextPattern);
        FunctionSpec inspectorSpec = detectFunction(scriptSource, "Script_OnInspector", cppContextPattern);
        FunctionSpec editorRenderSpec = detectFunction(scriptSource, "RenderEditorWindow", cppContextPattern);
        FunctionSpec editorExitSpec = detectFunction(scriptSource, "ExitRenderEditorWindow", cppContextPattern);

        bool needsInspectorWrap = inspectorSpec.present && !hasExternCSymbol("Script_OnInspector");
        bool needsRenderWrap = editorRenderSpec.present && !hasExternCSymbol("RenderEditorWindow");
        bool needsExitWrap = editorExitSpec.present && !hasExternCSymbol("ExitRenderEditorWindow");
        useWrapper = beginSpec.present || specSpec.present || testEditorSpec.present ||
                     updateSpec.present || tickUpdateSpec.present ||
                     needsInspectorWrap || needsRenderWrap || needsExitWrap;

        fs::path sourceToCompile = compileSourcePath;
        if (useWrapper) {
            wrapperPath = config.outDir / relativeParent / (baseName + ".wrap.cpp");
            std::ostringstream wrapper;

            std::string includePath = sourceToCompile.lexically_normal().generic_string();
            if (needsInspectorWrap) {
                wrapper << "#define Script_OnInspector Script_OnInspector_Impl\n";
            }
            if (needsRenderWrap) {
                wrapper << "#define RenderEditorWindow RenderEditorWindow_Impl\n";
            }
            if (needsExitWrap) {
                wrapper << "#define ExitRenderEditorWindow ExitRenderEditorWindow_Impl\n";
            }
            wrapper << "#include \"ScriptRuntime.h\"\n";
            wrapper << "#include \"" << includePath << "\"\n";
            if (needsExitWrap) {
                wrapper << "#undef ExitRenderEditorWindow\n";
            }
            if (needsRenderWrap) {
                wrapper << "#undef RenderEditorWindow\n";
            }
            if (needsInspectorWrap) {
                wrapper << "#undef Script_OnInspector\n";
            }
            wrapper << "\n";
            wrapper << "extern \"C\" {\n";

            auto emitTickBridge = [&](const char* exportedName, const char* implName,
                                      const FunctionSpec& spec) {
                if (!spec.present) return;
                wrapper << "void " << exportedName << "(ScriptContext& ctx, float deltaTime) {\n";
                if (spec.takesContext && spec.takesDelta) {
                    wrapper << "    " << implName << "(ctx, deltaTime);\n";
                } else if (spec.takesContext) {
                    wrapper << "    (void)deltaTime;\n";
                    wrapper << "    " << implName << "(ctx);\n";
                } else if (spec.takesDelta) {
                    wrapper << "    (void)ctx;\n";
                    wrapper << "    " << implName << "(deltaTime);\n";
                } else {
                    wrapper << "    (void)ctx;\n";
                    wrapper << "    (void)deltaTime;\n";
                    wrapper << "    " << implName << "();\n";
                }
                wrapper << "}\n\n";
            };

            auto emitEditorBridge = [&](const char* exportedName, const char* implName,
                                        const FunctionSpec& spec) {
                if (!spec.present) return;
                wrapper << "void " << exportedName << "(ScriptContext& ctx) {\n";
                if (spec.takesContext) {
                    wrapper << "    " << implName << "(ctx);\n";
                } else if (spec.takesDelta) {
                    wrapper << "    (void)ctx;\n";
                    wrapper << "    " << implName << "(0.0f);\n";
                } else {
                    wrapper << "    (void)ctx;\n";
                    wrapper << "    " << implName << "();\n";
                }
                wrapper << "}\n\n";
            };

            emitTickBridge("Script_Begin", "Begin", beginSpec);
            emitTickBridge("Script_Spec", "Spec", specSpec);
            emitTickBridge("Script_TestEditor", "TestEditor", testEditorSpec);
            emitTickBridge("Script_Update", "Update", updateSpec);
            emitTickBridge("Script_TickUpdate", "TickUpdate", tickUpdateSpec);
            if (needsInspectorWrap) {
                emitEditorBridge("Script_OnInspector", "Script_OnInspector_Impl", inspectorSpec);
            }
            if (needsRenderWrap) {
                emitEditorBridge("RenderEditorWindow", "RenderEditorWindow_Impl", editorRenderSpec);
            }
            if (needsExitWrap) {
                emitEditorBridge("ExitRenderEditorWindow", "ExitRenderEditorWindow_Impl", editorExitSpec);
            }

            wrapper << "}\n";
            if (!writeTextFileIfChanged(wrapperPath, wrapper.str(), error)) {
                return false;
            }
            sourceToCompile = wrapperPath;
        }

#ifdef _WIN32
        fs::path compileRspPath = config.outDir / relativeParent / (baseName + ".compile.rsp");
        fs::path linkRspPath = config.outDir / relativeParent / (baseName + ".link.rsp");

        std::ostringstream compileRsp;
        compileRsp << "/nologo /std:" << config.cppStandard << " /EHsc /MD /Zi /Od";
        appendWindowsIncludesAndDefines(compileRsp);
        compileRsp << " /c \"" << sourceToCompile.string() << "\" /Fo\"" << objectPath.string() << "\"";

        std::ostringstream linkRsp;
        linkRsp << "/nologo /DLL \"" << objectPath.string() << "\" /OUT:\""
                << binaryPath.string() << "\""
                << " \"" << hostImportLib->string() << "\"";
        for (const auto& lib : config.windowsLinkLibs) {
            linkRsp << " " << lib;
        }

        if (!writeTextFileIfChanged(compileRspPath, compileRsp.str(), error) ||
            !writeTextFileIfChanged(linkRspPath, linkRsp.str(), error)) {
            return false;
        }

        compileCmd << "cl @\"" << compileRspPath.string() << "\"";
        linkCmd << "link @\"" << linkRspPath.string() << "\"";
        buildSignaturePayload = compileRsp.str() + "\n---rsp---\n" + linkRsp.str();
#else
        compileCmd << posixCompileDriver(true) << " -std=" << config.cppStandard << " -fPIC -O0 -g";
        appendPosixIncludesAndDefines(compileCmd);
        compileCmd << " -MMD -MF \"" << dependencyPath.string() << "\"";
        compileCmd << " -c \"" << sourceToCompile.string() << "\" -o \"" << objectPath.string() << "\"";
        linkCmd << "g++ -shared \"" << objectPath.string() << "\" -o \"" << binaryPath.string() << "\"";
        for (const auto& lib : config.linuxLinkLibs) {
            linkCmd << " " << formatLinkFlag(lib);
        }
#endif
    }

    std::string compileStr = compileCmd.str();
    std::string linkStr = linkCmd.str();
#ifdef _WIN32
    const std::string clPath = findVsTool("cl.exe");
    const std::string linkPath = findVsTool("link.exe");
    compileStr = applyToolOverride(compileStr, "cl", clPath);
    if (!clPath.empty()) {
        const std::string marker = " && cl ";
        const std::string replacement = " && \"" + clPath + "\" ";
        size_t pos = 0;
        while ((pos = compileStr.find(marker, pos)) != std::string::npos) {
            compileStr.replace(pos, marker.size(), replacement);
            pos += replacement.size();
        }
    }
    linkStr = applyToolOverride(linkStr, "link", linkPath);
    compileStr = wrapWithVsDevCmdIfNeeded(compileStr);
    linkStr = wrapWithVsDevCmdIfNeeded(linkStr);
#endif

    outCommands.compile = compileStr;
    outCommands.link = linkStr;
    outCommands.objectPath = objectPath;
    outCommands.secondaryObjectPath = secondaryObjectPath;
    outCommands.dependencyPath = dependencyPath;
    outCommands.secondaryDependencyPath = secondaryDependencyPath;
    outCommands.binaryPath = binaryPath;
    outCommands.wrapperPath = wrapperPath.empty() ? transpiledPath : wrapperPath;
    outCommands.sourcePath = compileSourcePath;
    outCommands.signaturePath = config.outDir / relativeParent / (baseName + ".buildsig");
    outCommands.buildSignature = compileStr + "\n---link---\n" + linkStr +
                                 "\n---payload---\n" + buildSignaturePayload;
    outCommands.usedWrapper = useWrapper;
    return true;
}

bool ScriptCompiler::runCommand(const std::string& command, std::string& output) {
#ifdef _WIN32
    int exitCode = 0;
    if (!runWindowsShellCommand(command, output, exitCode)) {
        output = "Failed to spawn process: " + command;
        return false;
    }
    return exitCode == 0;
#else
    std::array<char, 256> buffer{};
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        output = "Failed to spawn process: " + command;
        return false;
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }

    int returnCode = pclose(pipe);
    if (returnCode != 0) {
        return false;
    }
    return true;
#endif
}

bool ScriptCompiler::compile(const ScriptBuildCommands& commands, ScriptCompileOutput& output,
                             std::string& error) const {
    if (!commands.objectPath.empty()) {
        std::error_code ec;
        fs::create_directories(commands.objectPath.parent_path(), ec);
    }
    if (!commands.secondaryObjectPath.empty()) {
        std::error_code ec;
        fs::create_directories(commands.secondaryObjectPath.parent_path(), ec);
    }
    if (!commands.binaryPath.empty()) {
        std::error_code ec;
        fs::create_directories(commands.binaryPath.parent_path(), ec);
    }
    if (!commands.signaturePath.empty()) {
        std::error_code ec;
        fs::create_directories(commands.signaturePath.parent_path(), ec);
    }

    bool needsCompile = true;
    bool needsLink = true;

    const auto sourceTime = getFileWriteTime(commands.sourcePath);
    const auto wrapperTime = getFileWriteTime(commands.wrapperPath);
    const auto objectTime = getFileWriteTime(commands.objectPath);
    const bool hasSecondaryObject = !commands.secondaryObjectPath.empty();
    const auto secondaryObjectTime = hasSecondaryObject
                                         ? getFileWriteTime(commands.secondaryObjectPath)
                                         : std::optional<fs::file_time_type>{};
    const auto binaryTime = getFileWriteTime(commands.binaryPath);
    std::optional<std::string> previousSignature;
    if (!commands.signaturePath.empty()) {
        std::ifstream signatureIn(commands.signaturePath, std::ios::binary);
        if (signatureIn.is_open()) {
            std::ostringstream ss;
            ss << signatureIn.rdbuf();
            previousSignature = ss.str();
        }
    }

    std::optional<fs::file_time_type> fallbackNewestInput = sourceTime;
    if (wrapperTime) {
        fallbackNewestInput = fallbackNewestInput
            ? std::max(*fallbackNewestInput, *wrapperTime)
            : wrapperTime;
    }

    auto objectNeedsCompile = [&](const std::optional<fs::file_time_type>& builtTime,
                                  const fs::path& depPath) {
        if (!builtTime) return true;

        std::optional<fs::file_time_type> newestInput = fallbackNewestInput;
        if (!depPath.empty()) {
            DependencyInfo depInfo = readDependencyInfo(depPath);
            if (!depInfo.hasDepFile || depInfo.missingDependency || !depInfo.newestInput) {
                return true;
            }
            newestInput = depInfo.newestInput;
        }

        if (!newestInput) return true;
        return *builtTime < *newestInput;
    };

    needsCompile = objectNeedsCompile(objectTime, commands.dependencyPath);
    if (hasSecondaryObject) {
        needsCompile = needsCompile ||
                       objectNeedsCompile(secondaryObjectTime, commands.secondaryDependencyPath);
    }
    if (!previousSignature || *previousSignature != commands.buildSignature) {
        needsCompile = true;
    }

    if (!needsCompile) {
        output.compileLog += "Skipped compile (up-to-date)\n";
    }

    if (binaryTime && objectTime) {
        fs::file_time_type newestObject = *objectTime;
        if (hasSecondaryObject) {
            if (!secondaryObjectTime) {
                needsLink = true;
            } else {
                newestObject = std::max(newestObject, *secondaryObjectTime);
                needsLink = (*binaryTime < newestObject);
            }
        } else {
            needsLink = (*binaryTime < newestObject);
        }
    } else {
        needsLink = true;
    }

    if (needsCompile) {
        if (!runCommand(commands.compile + " 2>&1", output.compileLog)) {
            error = "Compile failed";
            return false;
        }
        needsLink = true;
    }

    if (needsLink) {
        if (!runCommand(commands.link + " 2>&1", output.linkLog)) {
            error = "Link failed";
            return false;
        }
    } else {
        output.linkLog += "Skipped link (up-to-date)\n";
    }

    if (!commands.signaturePath.empty()) {
        std::string writeError;
        if (!writeTextFileIfChanged(commands.signaturePath, commands.buildSignature, writeError)) {
            error = writeError;
            return false;
        }
    }

    return true;
}
