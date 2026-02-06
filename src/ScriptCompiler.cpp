#include "ScriptCompiler.h"

#include <array>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <regex>
#if defined(_WIN32)
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
    // why does windows need all of this :sob:
#if defined(_WIN32)
    std::string getEnvValue(const char* name) {
        const char* value = std::getenv(name);
        return value ? std::string(value) : std::string();
    }

    std::string runCapture(const std::string& command) {
        std::array<char, 256> buffer{};
        std::string output;
        FILE* pipe = _popen(command.c_str(), "r");
        if (!pipe) return output;
        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
            output += buffer.data();
        }
        _pclose(pipe);
        return output;
    }

    std::string findVsDevCmd() {
        std::string vsInstall = getEnvValue("VSINSTALLDIR");
        if (!vsInstall.empty()) {
            fs::path candidate = fs::path(vsInstall) / "Common7" / "Tools" / "VsDevCmd.bat";
            if (fs::exists(candidate)) return candidate.string();
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
        if (fs::exists(devCmd)) return devCmd.string();
        return std::string();
    }

    // well, that's one way to make VS Harder to implement, For God's Sake, MICROSOFT!!!!
    std::string findVsTool(const char* toolName) {
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
                    if (fs::exists(candidate)) return candidate.string();
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
        wrapped << "cmd /c \"\""
                << vsDevCmd
                << "\" -arch=x64 -host_arch=x64 >nul && "
                << command
                << "\"";
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
        fs::path candidate = start;
        for (int depth = 0; depth < 5 && !candidate.empty(); ++depth) {
            if (fs::exists(candidate / "src" / "ScriptRuntime.h", ec)) {
                outConfig.includeDirs.push_back(candidate / "src");
                outConfig.includeDirs.push_back(candidate / "include");
                outConfig.includeDirs.push_back(candidate / "src/ThirdParty");
                outConfig.includeDirs.push_back(candidate / "src/ThirdParty/glm");
                outConfig.includeDirs.push_back(candidate / "src/ThirdParty/glad");
                outConfig.includeDirs.push_back(candidate / "src/ThirdParty/imgui");
                outConfig.includeDirs.push_back(candidate / "src/ThirdParty/imgui/backends");
                return true;
            }
            candidate = candidate.parent_path();
        }
        return false;
    };

    tryAddEngineRoot(configPath.parent_path());
    tryAddEngineRoot(fs::current_path());
    tryAddEngineRoot(fs::current_path().parent_path());
#if defined(__linux__)
    {
        std::error_code ec;
        fs::path exe = fs::read_symlink("/proc/self/exe", ec);
        if (!ec) {
            tryAddEngineRoot(exe.parent_path());
            tryAddEngineRoot(exe.parent_path().parent_path());
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

    fs::path binaryPath = config.outDir / relativeParent;
#ifdef _WIN32
    objectPath = config.outDir / relativeParent / (baseName + ".obj");
    binaryPath /= baseName + ".dll";
#else
    binaryPath /= baseName + ".so";
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
        for (const auto& def : config.defines) {
            cmd << " /D" << escapeDefine(def);
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

    fs::path wrapperPath;
    bool useWrapper = false;
    std::ostringstream compileCmd;
    std::ostringstream linkCmd;

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
#endif
        std::error_code createErr;
        fs::create_directories(wrapperPath.parent_path(), createErr);

        std::ofstream wrapper(wrapperPath);
        if (!wrapper.is_open()) {
            error = "Unable to write C API wrapper file: " + wrapperPath.string();
            return false;
        }

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

#ifdef _WIN32
        compileCmd << "cl /nologo /TC /MD /Zi /Od";
        appendWindowsIncludesAndDefines(compileCmd);
        compileCmd << " /c \"" << scriptAbs.string() << "\" /Fo\"" << objectPath.string() << "\"";
        compileCmd << " && ";
        compileCmd << "cl /nologo /TP /std:" << config.cppStandard << " /EHsc /MD /Zi /Od";
        appendWindowsIncludesAndDefines(compileCmd);
        compileCmd << " /c \"" << wrapperPath.string() << "\" /Fo\"" << secondaryObjectPath.string() << "\"";
        linkCmd << "link /nologo /DLL \"" << objectPath.string() << "\" \"" << secondaryObjectPath.string()
                << "\" /OUT:\"" << binaryPath.string() << "\"";
        for (const auto& lib : config.windowsLinkLibs) {
            linkCmd << " " << lib;
        }
#else
        compileCmd << "gcc -std=c11 -fPIC -O0 -g";
        appendPosixIncludesAndDefines(compileCmd);
        compileCmd << " -c \"" << scriptAbs.string() << "\" -o \"" << objectPath.string() << "\"";
        compileCmd << " && ";
        compileCmd << "g++ -std=" << config.cppStandard << " -fPIC -O0 -g";
        appendPosixIncludesAndDefines(compileCmd);
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

        fs::path sourceToCompile = scriptAbs;
        if (useWrapper) {
            wrapperPath = config.outDir / relativeParent / (baseName + ".wrap.cpp");
            std::error_code createErr;
            fs::create_directories(wrapperPath.parent_path(), createErr);

            std::ofstream wrapper(wrapperPath);
            if (!wrapper.is_open()) {
                error = "Unable to write wrapper file: " + wrapperPath.string();
                return false;
            }

            std::string includePath = scriptAbs.lexically_normal().generic_string();
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
            sourceToCompile = wrapperPath;
        }

#ifdef _WIN32
        compileCmd << "cl /nologo /std:" << config.cppStandard << " /EHsc /MD /Zi /Od";
        appendWindowsIncludesAndDefines(compileCmd);
        compileCmd << " /c \"" << sourceToCompile.string() << "\" /Fo\"" << objectPath.string() << "\"";
        linkCmd << "link /nologo /DLL \"" << objectPath.string() << "\" /OUT:\""
                << binaryPath.string() << "\"";
        for (const auto& lib : config.windowsLinkLibs) {
            linkCmd << " " << lib;
        }
#else
        compileCmd << "g++ -std=" << config.cppStandard << " -fPIC -O0 -g";
        appendPosixIncludesAndDefines(compileCmd);
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
    outCommands.binaryPath = binaryPath;
    outCommands.wrapperPath = wrapperPath;
    outCommands.usedWrapper = useWrapper;
    return true;
}

bool ScriptCompiler::runCommand(const std::string& command, std::string& output) {
    std::array<char, 256> buffer{};
#ifdef _WIN32
    FILE* pipe = _popen(command.c_str(), "r");
#else
    FILE* pipe = popen(command.c_str(), "r");
#endif
    if (!pipe) {
        output = "Failed to spawn process: " + command;
        return false;
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }

#ifdef _WIN32
    int returnCode = _pclose(pipe);
#else
    int returnCode = pclose(pipe);
#endif
    if (returnCode != 0) {
        return false;
    }
    return true;
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

    if (!runCommand(commands.compile + " 2>&1", output.compileLog)) {
        error = "Compile failed";
        return false;
    }
    if (!runCommand(commands.link + " 2>&1", output.linkLog)) {
        error = "Link failed";
        return false;
    }
    return true;
}
