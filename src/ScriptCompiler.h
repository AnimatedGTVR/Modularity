#pragma once

#include "Common.h"

struct ScriptBuildConfig {
    std::string cppStandard = "c++20";
    fs::path scriptsDir = "Scripts";
    fs::path outDir = "Cache/ScriptBin";
    std::vector<fs::path> includeDirs;
    std::vector<std::string> defines;
    std::vector<std::string> linuxLinkLibs;
    std::vector<std::string> windowsLinkLibs;
};

struct ScriptBuildCommands {
    std::string compile;
    std::string link;
    fs::path objectPath;
    fs::path secondaryObjectPath;
    fs::path dependencyPath;
    fs::path secondaryDependencyPath;
    fs::path binaryPath;
    fs::path wrapperPath;
    fs::path sourcePath;
    bool usedWrapper = false;
};

struct ScriptCompileOutput {
    std::string compileLog;
    std::string linkLog;
};

class ScriptCompiler {
public:
    bool loadConfig(const fs::path& configPath, ScriptBuildConfig& outConfig, std::string& error) const;
    bool makeCommands(const ScriptBuildConfig& config, const fs::path& scriptPath,
                      ScriptBuildCommands& outCommands, std::string& error) const;
    bool compile(const ScriptBuildCommands& commands, ScriptCompileOutput& output,
                 std::string& error) const;

private:
    static std::string trim(const std::string& value);
    static std::string escapeDefine(const std::string& def);
    static bool runCommand(const std::string& command, std::string& output);
    static std::string formatLinkFlag(const std::string& lib);
};
