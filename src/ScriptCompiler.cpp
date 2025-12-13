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

    fs::path relativeParent = relToScripts.has_parent_path() ? relToScripts.parent_path() : fs::path();
    std::string baseName = scriptAbs.stem().string();
    fs::path objectPath = config.outDir / relativeParent / (baseName + ".o");

    fs::path binaryPath = config.outDir / relativeParent;
#ifdef _WIN32
    objectPath = config.outDir / relativeParent / (baseName + ".obj");
    binaryPath /= baseName + ".dll";
#else
    binaryPath /= baseName + ".so";
#endif

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

    auto detectFunction = [](const std::string& source, const std::string& name) -> FunctionSpec {
        FunctionSpec spec;
        try {
            const std::string prefix = "\\bvoid\\s+(?:IEnum\\s+)?";
            std::regex ctxDeltaPattern(prefix + name + "\\s*\\(\\s*ScriptContext\\s*[&*][^,\\)]*,[^\\)]*(float|double)[^\\)]*\\)");
            std::regex ctxOnlyPattern(prefix + name + "\\s*\\(\\s*ScriptContext\\s*[&*][^\\)]*\\)");
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

    FunctionSpec beginSpec = detectFunction(scriptSource, "Begin");
    FunctionSpec specSpec = detectFunction(scriptSource, "Spec");
    FunctionSpec testEditorSpec = detectFunction(scriptSource, "TestEditor");
    FunctionSpec updateSpec = detectFunction(scriptSource, "Update");
    FunctionSpec tickUpdateSpec = detectFunction(scriptSource, "TickUpdate");

    fs::path wrapperPath;
    bool useWrapper = beginSpec.present || specSpec.present || testEditorSpec.present
                      || updateSpec.present || tickUpdateSpec.present;
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
        wrapper << "#include \"ScriptRuntime.h\"\n";
        wrapper << "#include \"" << includePath << "\"\n\n";
        wrapper << "extern \"C\" {\n";

        auto emitWrapper = [&](const char* exportedName, const char* implName,
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

        emitWrapper("Script_Begin", "Begin", beginSpec);
        emitWrapper("Script_Spec", "Spec", specSpec);
        emitWrapper("Script_TestEditor", "TestEditor", testEditorSpec);
        emitWrapper("Script_Update", "Update", updateSpec);
        emitWrapper("Script_TickUpdate", "TickUpdate", tickUpdateSpec);

        wrapper << "}\n";
        sourceToCompile = wrapperPath;
    }

    std::ostringstream compileCmd;
#ifdef _WIN32
    compileCmd << "cl /nologo /std:" << config.cppStandard << " /EHsc /MD /Zi /Od";
    for (const auto& inc : config.includeDirs) {
        compileCmd << " /I\"" << inc.string() << "\"";
    }
    for (const auto& def : config.defines) {
        compileCmd << " /D" << escapeDefine(def);
    }
    compileCmd << " /c \"" << sourceToCompile.string() << "\" /Fo\"" << objectPath.string() << "\"";
#else
    compileCmd << "g++ -std=" << config.cppStandard << " -fPIC -O0 -g";
    for (const auto& inc : config.includeDirs) {
        compileCmd << " -I\"" << inc.string() << "\"";
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
        compileCmd << formatDefine(def);
    }
    compileCmd << " -c \"" << sourceToCompile.string() << "\" -o \"" << objectPath.string() << "\"";
#endif

    std::ostringstream linkCmd;
#ifdef _WIN32
    linkCmd << "link /nologo /DLL \"" << objectPath.string() << "\" /OUT:\""
            << binaryPath.string() << "\"";
    for (const auto& lib : config.windowsLinkLibs) {
        linkCmd << " " << lib;
    }
#else
    linkCmd << "g++ -shared \"" << objectPath.string() << "\" -o \"" << binaryPath.string() << "\"";
    for (const auto& lib : config.linuxLinkLibs) {
        linkCmd << " " << formatLinkFlag(lib);
    }
#endif

    outCommands.compile = compileCmd.str();
    outCommands.link = linkCmd.str();
    outCommands.objectPath = objectPath;
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
