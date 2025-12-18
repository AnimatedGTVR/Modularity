#pragma once

#include "Common.h"
#include "ScriptCompiler.h"
#include <cctype>

struct PackageInfo {
    std::string id;
    std::string name;
    std::string description;
    bool builtIn = false;
    bool external = false;
    std::string gitUrl;
    fs::path localPath;     // Absolute path for external packages
    fs::path includeHint;   // Absolute include root for external packages
    std::vector<fs::path> includeDirs;
    std::vector<std::string> defines;
    std::vector<std::string> linuxLibs;
    std::vector<std::string> windowsLibs;
};

// Minimal package manager for script dependencies. Keeps a small registry of
// known/bundled packages and a per-project manifest of which optional ones are
// enabled. Installed packages augment the script build config (include dirs,
// defines, link libs) before compilation.
class PackageManager {
public:
    PackageManager();

    void setProjectRoot(const fs::path& root);
    const std::vector<PackageInfo>& getRegistry() const { return registry; }
    const std::vector<std::string>& getInstalled() const { return installedIds; }
    bool isInstalled(const std::string& id) const;
    bool install(const std::string& id);
    bool installGitPackage(const std::string& url,
                           const std::string& nameHint,
                           const std::string& includeRel,
                           std::string& outId);
    bool checkGitStatus(const std::string& id, std::string& outStatus);
    bool updateGitPackage(const std::string& id, std::string& outLog);
    bool remove(const std::string& id);
    void applyToBuildConfig(ScriptBuildConfig& config) const;
    const std::string& getLastError() const { return lastError; }

private:
    void buildRegistry();
    void loadManifest();
    void saveManifest() const;
    const PackageInfo* findPackage(const std::string& id) const;
    bool isBuiltIn(const std::string& id) const;
    static std::string trim(const std::string& value);
    static std::string slugify(const std::string& value);
    static bool runCommand(const std::string& command, std::string& output);
    bool ensureProjectRoot() const;
    std::vector<std::string> split(const std::string& input, char delim) const;
    std::string join(const std::vector<std::string>& vals, char delim) const;
    fs::path packagesFolder() const;

    fs::path projectRoot;
    fs::path manifestPath;
    std::vector<PackageInfo> registry;
    std::vector<std::string> installedIds;
    std::string lastError;
};
