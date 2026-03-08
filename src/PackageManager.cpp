#include "PackageManager.h"

#include <unordered_set>
#include <array>
#include <cstdio>
#include <chrono>
#include <sstream>

#pragma region Local Path Helpers
namespace {
fs::path normalizePath(const fs::path& p) {
    std::error_code ec;
    fs::path canonical = fs::weakly_canonical(p, ec);
    if (ec) {
        canonical = fs::absolute(p, ec);
    }
    return canonical.lexically_normal();
}

std::string trimCopy(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) start++;
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) end--;
    return value.substr(start, end - start);
}

std::vector<std::string> splitTokens(const std::string& input, char delim) {
    std::vector<std::string> out;
    std::stringstream ss(input);
    std::string part;
    while (std::getline(ss, part, delim)) {
        out.push_back(part);
    }
    return out;
}

fs::path resolveManifestPath(const fs::path& projectRoot, const std::string& token) {
    if (token.empty()) return {};
    const fs::path parsed(token);
    if (parsed.is_absolute()) {
        return normalizePath(parsed);
    }
    return normalizePath(projectRoot / parsed);
}

std::string toManifestPathToken(const fs::path& pathValue, const fs::path& projectRoot) {
    if (pathValue.empty()) return "";
    std::error_code ec;
    fs::path rel = fs::relative(pathValue, projectRoot, ec);
    return (!ec ? rel : pathValue).generic_string();
}

bool containsPath(const std::vector<fs::path>& haystack, const fs::path& needle) {
    std::string target = normalizePath(needle).string();
    for (const auto& entry : haystack) {
        if (normalizePath(entry).string() == target) {
            return true;
        }
    }
    return false;
}

bool isGitRepo(const fs::path& root) {
    std::error_code ec;
    fs::path dotGit = root / ".git";
    if (fs::exists(dotGit, ec)) {
        return true;
    }
    return false;
}

fs::path guessIncludeDir(const fs::path& repoRoot, const std::string& includeRel) {
    if (!includeRel.empty()) {
        fs::path candidate = normalizePath(repoRoot / includeRel);
        if (fs::exists(candidate) && fs::is_directory(candidate)) {
            return candidate;
        }
    }

    const char* defaults[] = {"include", "Include", "includes", "inc", "Inc"};
    for (const char* name : defaults) {
        fs::path candidate = normalizePath(repoRoot / name);
        if (fs::exists(candidate) && fs::is_directory(candidate)) {
            return candidate;
        }
    }

    for (const auto& entry : fs::directory_iterator(repoRoot)) {
        if (entry.is_directory()) {
            return normalizePath(entry.path());
        }
    }

    return normalizePath(repoRoot);
}

bool copyDirectoryRecursive(const fs::path& sourceRoot,
                            const fs::path& destinationRoot,
                            std::string& outError) {
    if (!fs::exists(sourceRoot)) {
        outError = "Source folder does not exist: " + sourceRoot.string();
        return false;
    }

    std::error_code ec;
    fs::create_directories(destinationRoot, ec);
    if (ec) {
        outError = "Failed to create destination folder: " + destinationRoot.string();
        return false;
    }

    for (const auto& entry : fs::recursive_directory_iterator(sourceRoot, ec)) {
        if (ec) {
            outError = "Failed to read source folder: " + sourceRoot.string();
            return false;
        }

        const fs::path rel = fs::relative(entry.path(), sourceRoot, ec);
        if (ec) {
            outError = "Failed to compute relative path for " + entry.path().string();
            return false;
        }

        const fs::path dst = destinationRoot / rel;
        if (entry.is_directory()) {
            fs::create_directories(dst, ec);
            if (ec) {
                outError = "Failed to create directory: " + dst.string();
                return false;
            }
        } else if (entry.is_regular_file()) {
            fs::create_directories(dst.parent_path(), ec);
            if (ec) {
                outError = "Failed to create directory: " + dst.parent_path().string();
                return false;
            }
            fs::copy_file(entry.path(), dst, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                outError = "Failed to copy file: " + entry.path().string();
                return false;
            }
        }
    }

    return true;
}

bool parseExternalManifestPackage(const fs::path& projectRoot,
                                  const std::string& payload,
                                  bool modupak,
                                  PackageInfo& outPackage) {
    const auto parts = splitTokens(payload, '|');
    if (parts.size() < 4) return false;

    outPackage = PackageInfo{};
    outPackage.id = trimCopy(parts[0]);
    outPackage.name = trimCopy(parts[1]);
    outPackage.external = true;
    outPackage.modupak = modupak;
    outPackage.localPath = resolveManifestPath(projectRoot, trimCopy(parts[3]));
    outPackage.description = modupak ? "External package from .modupak" : "External package from git";

    const std::string sourceToken = trimCopy(parts[2]);
    if (modupak) {
        outPackage.modupakSourcePath = resolveManifestPath(projectRoot, sourceToken);
    } else {
        outPackage.gitUrl = sourceToken;
    }

    if (parts.size() > 4) {
        for (const auto& inc : splitTokens(parts[4], ';')) {
            const std::string cleaned = trimCopy(inc);
            if (cleaned.empty()) continue;
            outPackage.includeDirs.push_back(resolveManifestPath(projectRoot, cleaned));
        }
    }
    auto readCleanList = [](const std::string& raw) {
        std::vector<std::string> vals;
        for (const std::string& token : splitTokens(raw, ';')) {
            const std::string cleaned = trimCopy(token);
            if (!cleaned.empty()) vals.push_back(cleaned);
        }
        return vals;
    };

    if (parts.size() > 5) {
        outPackage.defines = readCleanList(parts[5]);
    }
    if (parts.size() > 6) {
        outPackage.linuxLibs = readCleanList(parts[6]);
    }
    if (parts.size() > 7) {
        outPackage.windowsLibs = readCleanList(parts[7]);
    }
    if (parts.size() > 8) {
        const std::string desc = trimCopy(parts[8]);
        if (!desc.empty()) outPackage.description = desc;
    }

    if (outPackage.id.empty()) return false;
    if (outPackage.name.empty()) outPackage.name = outPackage.id;
    if (outPackage.includeDirs.empty()) {
        outPackage.includeDirs.push_back(guessIncludeDir(outPackage.localPath, "include"));
    }
    return true;
}
} // namespace
#pragma endregion

#pragma region Lifecycle
PackageManager::PackageManager() {
    buildRegistry();
}

void PackageManager::setProjectRoot(const fs::path& root) {
    buildRegistry();
    projectRoot = root;
    manifestPath = projectRoot / "packages.modu";
    loadManifest();
}
#pragma endregion

#pragma region Install / Remove
bool PackageManager::isInstalled(const std::string& id) const {
    return std::find(installedIds.begin(), installedIds.end(), id) != installedIds.end();
}

bool PackageManager::install(const std::string& id) {
    lastError.clear();
    const PackageInfo* pkg = findPackage(id);
    if (!pkg) {
        lastError = "Unknown package: " + id;
        return false;
    }
    if (isInstalled(id)) {
        return true;
    }
    installedIds.push_back(id);
    saveManifest();
    return true;
}

bool PackageManager::remove(const std::string& id) {
    lastError.clear();
    const PackageInfo* pkg = findPackage(id);
    if (!pkg) {
        lastError = "Unknown package: " + id;
        return false;
    }
    if (pkg->builtIn) {
        lastError = "Cannot remove built-in packages.";
        return false;
    }
    if (pkg->external) {
        std::string log;
        const bool isGitExternal = !pkg->gitUrl.empty();
        if (isGitExternal && isGitRepo(projectRoot)) {
            std::error_code ec;
            fs::path relPath = fs::relative(pkg->localPath, projectRoot, ec);
            std::string rel = (!ec ? relPath : pkg->localPath).generic_string();
            std::string deinitCmd = "git -C \"" + projectRoot.string() + "\" submodule deinit -f \"" + rel + "\"";
            runCommand(deinitCmd, log); // best-effort
            std::string rmCmd = "git -C \"" + projectRoot.string() + "\" rm -f \"" + rel + "\"";
            if (!runCommand(rmCmd, log)) {
                lastError = "Failed to remove submodule. Git log:\n" + log;
                return false;
            }
        } else {
            std::error_code ec;
            fs::remove_all(pkg->localPath, ec);
            if (ec) {
                lastError = "Failed to remove package folder: " + ec.message();
                return false;
            }
        }
    }

    auto it = std::remove(installedIds.begin(), installedIds.end(), id);
    if (it == installedIds.end()) {
        return true;
    }
    installedIds.erase(it, installedIds.end());
    registry.erase(std::remove_if(registry.begin(), registry.end(),
        [&](const PackageInfo& p){ return p.id == id && p.external; }), registry.end());

    saveManifest();
    return true;
}
#pragma endregion

#pragma region Build Config
void PackageManager::applyToBuildConfig(ScriptBuildConfig& config) const {
    std::unordered_set<std::string> defineSet(config.defines.begin(), config.defines.end());
    std::unordered_set<std::string> linuxLibSet(config.linuxLinkLibs.begin(), config.linuxLinkLibs.end());
    std::unordered_set<std::string> winLibSet(config.windowsLinkLibs.begin(), config.windowsLinkLibs.end());

    for (const auto& id : installedIds) {
        const PackageInfo* pkg = findPackage(id);
        if (!pkg) continue;

        for (const auto& dir : pkg->includeDirs) {
            if (!containsPath(config.includeDirs, dir)) {
                config.includeDirs.push_back(normalizePath(dir));
            }
        }
        for (const auto& def : pkg->defines) {
            if (defineSet.insert(def).second) {
                config.defines.push_back(def);
            }
        }
        for (const auto& lib : pkg->linuxLibs) {
            if (linuxLibSet.insert(lib).second) {
                config.linuxLinkLibs.push_back(lib);
            }
        }
        for (const auto& lib : pkg->windowsLibs) {
            if (winLibSet.insert(lib).second) {
                config.windowsLinkLibs.push_back(lib);
            }
        }
    }
}
#pragma endregion

#pragma region Registry
void PackageManager::buildRegistry() {
    registry.clear();
    fs::path engineRoot = fs::current_path();

    auto add = [this](PackageInfo info) {
        for (auto& dir : info.includeDirs) {
            dir = normalizePath(dir);
        }
        registry.push_back(std::move(info));
    };

    PackageInfo engineCore;
    engineCore.id = "engine-core";
    engineCore.name = "Engine Core";
    engineCore.description = "Modularity engine headers and common utilities";
    engineCore.builtIn = true;
    engineCore.includeDirs = {
        engineRoot / "src",
        engineRoot / "include",
        engineRoot / "src/ThirdParty",
        engineRoot / "src/ThirdParty/glm",
        engineRoot / "src/ThirdParty/glad"
    };
    engineCore.linuxLibs = {"pthread", "dl"};
    engineCore.windowsLibs = {"User32.lib", "Advapi32.lib"};
    add(engineCore);

    PackageInfo glm;
    glm.id = "glm";
    glm.name = "GLM Math";
    glm.description = "Header-only GLM math library (bundled)";
    glm.builtIn = false; // Count as installed instead of hidden built-in
    glm.includeDirs = { engineRoot / "src/ThirdParty/glm" };
    add(glm);

    PackageInfo imgui;
    imgui.id = "imgui";
    imgui.name = "Dear ImGui";
    imgui.description = "Immediate-mode UI helpers for editor-time tools";
    imgui.builtIn = false;
    imgui.includeDirs = {
        engineRoot / "src/ThirdParty/imgui",
        engineRoot / "src/ThirdParty/imgui/backends"
    };
    add(imgui);

    PackageInfo imguizmo;
    imguizmo.id = "imguizmo";
    imguizmo.name = "ImGuizmo";
    imguizmo.description = "Gizmo/transform helpers used by the editor";
    imguizmo.builtIn = false;
    imguizmo.includeDirs = { engineRoot / "src/ThirdParty/ImGuizmo" };
    add(imguizmo);

    PackageInfo miniaudio;
    miniaudio.id = "miniaudio";
    miniaudio.name = "miniaudio";
    miniaudio.description = "Single-header audio helpers (bundled)";
    miniaudio.builtIn = false;
    miniaudio.includeDirs = { engineRoot / "include/ThirdParty" };
    add(miniaudio);
}
#pragma endregion

#pragma region Manifest IO
void PackageManager::loadManifest() {
    installedIds.clear();
    for (const auto& pkg : registry) {
        if (!isInstalled(pkg.id)) {
            installedIds.push_back(pkg.id);
        }
    }

    if (manifestPath.empty()) return;
    if (!fs::exists(manifestPath)) {
        saveManifest();
        return;
    }

    std::ifstream file(manifestPath);
    if (!file.is_open()) {
        lastError = "Unable to open package manifest.";
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        std::string cleaned = trim(line);
        if (cleaned.empty() || cleaned[0] == '#') continue;

        if (cleaned.rfind("package=", 0) == 0) {
            const std::string id = trim(cleaned.substr(8));
            if (!id.empty() && !isInstalled(id) && findPackage(id)) {
                installedIds.push_back(id);
            }
            continue;
        }

        bool isGitLine = false;
        bool isModuPakLine = false;
        std::string payload;
        if (cleaned.rfind("git=", 0) == 0) {
            payload = cleaned.substr(4);
            isGitLine = true;
        } else if (cleaned.rfind("modupak=", 0) == 0) {
            payload = cleaned.substr(8);
            isModuPakLine = true;
        }

        if (!isGitLine && !isModuPakLine) {
            continue;
        }

        PackageInfo pkg;
        if (!parseExternalManifestPackage(projectRoot, payload, isModuPakLine, pkg)) {
            continue;
        }

        registry.erase(std::remove_if(registry.begin(), registry.end(),
            [&](const PackageInfo& entry) { return entry.id == pkg.id && entry.external; }),
            registry.end());
        registry.push_back(pkg);
        if (!isInstalled(pkg.id)) {
            installedIds.push_back(pkg.id);
        }
    }
}

void PackageManager::saveManifest() const {
    if (manifestPath.empty()) return;
    std::ofstream file(manifestPath);
    if (!file.is_open()) return;

    file << "# Modularity package manifest\n";
    file << "# Add optional script-time dependencies here\n";
    file << "# package=<id>\n";
    file << "# git=<id>|<name>|<url>|<path>|<includeDirs>|<defines>|<linuxLibs>|<windowsLibs>|<description>\n";
    file << "# modupak=<id>|<name>|<bundlePath>|<path>|<includeDirs>|<defines>|<linuxLibs>|<windowsLibs>|<description>\n";
    for (const auto& id : installedIds) {
        const PackageInfo* pkg = findPackage(id);
        if (!pkg) continue;
        if (!pkg->external) {
            if (!pkg->builtIn) {
                file << "package=" << id << "\n";
            }
            continue;
        }

        // Persist external package metadata
        std::vector<std::string> relIncludes;
        for (const auto& inc : pkg->includeDirs) {
            std::error_code ec;
            fs::path rel = fs::relative(inc, projectRoot, ec);
            relIncludes.push_back((!ec ? rel : inc).generic_string());
        }

        const char* sourceTag = pkg->modupak ? "modupak=" : "git=";
        file << sourceTag << pkg->id << "|"
             << pkg->name << "|";
        if (pkg->modupak) {
            file << toManifestPathToken(pkg->modupakSourcePath, projectRoot) << "|";
        } else {
            file << pkg->gitUrl << "|";
        }

        file << toManifestPathToken(pkg->localPath, projectRoot) << "|";
        file << join(relIncludes, ';') << "|";
        file << join(pkg->defines, ';') << "|";
        file << join(pkg->linuxLibs, ';') << "|";
        file << join(pkg->windowsLibs, ';') << "|";
        file << pkg->description << "\n";
    }
}
#pragma endregion

#pragma region Registry Lookup
const PackageInfo* PackageManager::findPackage(const std::string& id) const {
    auto it = std::find_if(registry.begin(), registry.end(), [&](const PackageInfo& p) {
        return p.id == id;
    });
    return it == registry.end() ? nullptr : &(*it);
}

bool PackageManager::isBuiltIn(const std::string& id) const {
    const PackageInfo* pkg = findPackage(id);
    return pkg && pkg->builtIn;
}
#pragma endregion

#pragma region Utility Helpers
std::string PackageManager::trim(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) start++;
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) end--;
    return value.substr(start, end - start);
}

std::string PackageManager::slugify(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        } else if (c == '-' || c == '_') {
            out.push_back(c);
        } else if (std::isspace(static_cast<unsigned char>(c))) {
            out.push_back('-');
        }
    }
    if (out.empty()) out = "pkg";
    return out;
}

bool PackageManager::runCommand(const std::string& command, std::string& output) {
    std::array<char, 256> buffer{};
#ifdef _WIN32
    FILE* pipe = _popen(command.c_str(), "r");
#else
    FILE* pipe = popen(command.c_str(), "r");
#endif
    if (!pipe) return false;
    const int buffer_len = static_cast<int>(buffer.size());
    while (fgets(buffer.data(), buffer_len, pipe) != nullptr) {
        output += buffer.data();
    }
    int rc = 0;
#ifdef _WIN32
    rc = _pclose(pipe);
#else
    rc = pclose(pipe);
#endif
    return rc == 0;
}

bool PackageManager::ensureProjectRoot() const {
    return !projectRoot.empty();
}

std::vector<std::string> PackageManager::split(const std::string& input, char delim) const {
    std::vector<std::string> out;
    std::stringstream ss(input);
    std::string part;
    while (std::getline(ss, part, delim)) {
        out.push_back(part);
    }
    return out;
}

std::string PackageManager::join(const std::vector<std::string>& vals, char delim) const {
    std::ostringstream oss;
    for (size_t i = 0; i < vals.size(); ++i) {
        if (i > 0) oss << delim;
        oss << vals[i];
    }
    return oss.str();
}
#pragma endregion

#pragma region External Packages
fs::path PackageManager::packagesFolder() const {
    fs::path newFolder = projectRoot / "Library" / "InstalledPackages";
    if (fs::exists(newFolder) || fs::exists(projectRoot / "scripts.modu")) {
        return newFolder;
    }
    return projectRoot / "Packages";
}

bool PackageManager::installGitPackage(const std::string& url,
                                       const std::string& nameHint,
                                       const std::string& includeRel,
                                       std::string& outId) {
    lastError.clear();
    if (!ensureProjectRoot()) {
        lastError = "Project root not set.";
        return false;
    }
    if (url.empty()) {
        lastError = "Git URL is required.";
        return false;
    }

    std::string repoName = nameHint;
    size_t slash = url.find_last_of("/\\");
    if (repoName.empty() && slash != std::string::npos) {
        repoName = url.substr(slash + 1);
        if (repoName.rfind(".git") != std::string::npos) {
            repoName = repoName.substr(0, repoName.size() - 4);
        }
    }
    std::string id = slugify(repoName);
    if (isInstalled(id)) {
        lastError = "Package already installed: " + id;
        return false;
    }

    fs::path dest = normalizePath(packagesFolder() / id);
    fs::create_directories(dest.parent_path());

    std::string cmd;
    if (isGitRepo(projectRoot)) {
        std::error_code ec;
        fs::path relPath = fs::relative(dest, projectRoot, ec);
        std::string rel = (!ec ? relPath : dest).generic_string();
        cmd = "git -C \"" + projectRoot.string() + "\" submodule add --force \"" + url + "\" \"" + rel + "\"";
    } else {
        cmd = "git clone \"" + url + "\" \"" + dest.string() + "\"";
    }
    std::string log;
    if (!runCommand(cmd, log)) {
        if (isGitRepo(projectRoot)) {
            lastError = "git submodule add failed:\n" + log;
        } else {
            lastError = "git clone failed:\n" + log;
        }
        return false;
    }

    PackageInfo pkg;
    pkg.id = id;
    pkg.name = repoName.empty() ? id : repoName;
    pkg.description = "External package from " + url;
    pkg.external = true;
    pkg.gitUrl = url;
    pkg.localPath = dest;
    pkg.includeDirs.push_back(guessIncludeDir(dest, includeRel));

    registry.push_back(pkg);
    installedIds.push_back(id);
    saveManifest();
    outId = id;
    return true;
}

bool PackageManager::installModuPak(const fs::path& modupakPath, std::string& outId) {
    lastError.clear();
    outId.clear();

    if (!ensureProjectRoot()) {
        lastError = "Project root not set.";
        return false;
    }
    if (modupakPath.empty()) {
        lastError = ".modupak path is required.";
        return false;
    }

    const fs::path source = normalizePath(modupakPath);
    if (!fs::exists(source)) {
        lastError = ".modupak was not found: " + source.string();
        return false;
    }
    if (source.extension() != ".modupak") {
        lastError = "Expected a .modupak bundle.";
        return false;
    }

    const fs::path tempRoot = projectRoot / "Library" / "Temp" / "ModuPakInstall";
    std::error_code ec;
    fs::create_directories(tempRoot, ec);
    if (ec) {
        lastError = "Failed to create temp folder: " + ec.message();
        return false;
    }

    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path unpackRoot = tempRoot / ("extract_" + std::to_string(now));
    fs::create_directories(unpackRoot, ec);
    if (ec) {
        lastError = "Failed to create extraction folder: " + ec.message();
        return false;
    }

    auto cleanup = [&]() {
        std::error_code removeEc;
        fs::remove_all(unpackRoot, removeEc);
    };

    if (fs::is_directory(source)) {
        std::string copyError;
        if (!copyDirectoryRecursive(source, unpackRoot, copyError)) {
            cleanup();
            lastError = copyError;
            return false;
        }
    } else {
        std::string tarLog;
        const std::string extractCmd =
            "tar -xf \"" + source.string() + "\" -C \"" + unpackRoot.string() + "\" 2>&1";
        if (!runCommand(extractCmd, tarLog)) {
            cleanup();
            lastError = "Failed to extract .modupak with tar.\n" + tarLog;
            return false;
        }
    }

    const fs::path manifestFile = unpackRoot / "manifest.modu";
    if (!fs::exists(manifestFile)) {
        cleanup();
        lastError = ".modupak is missing manifest.modu.";
        return false;
    }

    PackageInfo pkg;
    pkg.external = true;
    pkg.modupak = true;
    pkg.modupakSourcePath = source;
    pkg.description = "External package from .modupak";

    std::vector<std::string> includeHints;
    std::ifstream manifest(manifestFile);
    if (!manifest.is_open()) {
        cleanup();
        lastError = "Failed to open .modupak manifest.";
        return false;
    }
    std::string line;
    while (std::getline(manifest, line)) {
        const std::string cleaned = trim(line);
        if (cleaned.empty() || cleaned[0] == '#') continue;

        auto readValue = [&](size_t prefixSize) {
            return trim(cleaned.substr(prefixSize));
        };

        if (cleaned.rfind("id=", 0) == 0) {
            pkg.id = slugify(readValue(3));
        } else if (cleaned.rfind("name=", 0) == 0) {
            pkg.name = readValue(5);
        } else if (cleaned.rfind("description=", 0) == 0) {
            pkg.description = readValue(12);
        } else if (cleaned.rfind("includeDir=", 0) == 0) {
            const std::string value = readValue(11);
            if (!value.empty()) includeHints.push_back(value);
        } else if (cleaned.rfind("define=", 0) == 0) {
            const std::string value = readValue(7);
            if (!value.empty()) pkg.defines.push_back(value);
        } else if (cleaned.rfind("linux.linkLib=", 0) == 0) {
            const std::string value = readValue(14);
            if (!value.empty()) pkg.linuxLibs.push_back(value);
        } else if (cleaned.rfind("win.linkLib=", 0) == 0) {
            const std::string value = readValue(12);
            if (!value.empty()) pkg.windowsLibs.push_back(value);
        } else if (cleaned.rfind("windows.linkLib=", 0) == 0) {
            const std::string value = readValue(16);
            if (!value.empty()) pkg.windowsLibs.push_back(value);
        }
    }

    if (pkg.name.empty()) {
        pkg.name = source.stem().string();
    }
    if (pkg.id.empty()) {
        pkg.id = slugify(pkg.name);
    }
    if (pkg.id.empty()) {
        cleanup();
        lastError = "Unable to resolve package id from .modupak.";
        return false;
    }
    if (isInstalled(pkg.id)) {
        cleanup();
        lastError = "Package already installed: " + pkg.id;
        return false;
    }

    pkg.localPath = normalizePath(packagesFolder() / pkg.id);
    if (fs::exists(pkg.localPath)) {
        cleanup();
        lastError = "Package target path already exists: " + pkg.localPath.string();
        return false;
    }

    const fs::path payloadRoot = unpackRoot / "payload";
    const fs::path sourceContent =
        (fs::exists(payloadRoot) && fs::is_directory(payloadRoot)) ? payloadRoot : unpackRoot;

    std::string installCopyError;
    if (!copyDirectoryRecursive(sourceContent, pkg.localPath, installCopyError)) {
        cleanup();
        lastError = installCopyError;
        return false;
    }

    for (const std::string& includeRel : includeHints) {
        pkg.includeDirs.push_back(normalizePath(pkg.localPath / includeRel));
    }
    if (pkg.includeDirs.empty()) {
        pkg.includeDirs.push_back(guessIncludeDir(pkg.localPath, "include"));
    }

    registry.push_back(pkg);
    installedIds.push_back(pkg.id);
    saveManifest();
    outId = pkg.id;
    cleanup();
    return true;
}

bool PackageManager::checkGitStatus(const std::string& id, std::string& outStatus) {
    lastError.clear();
    outStatus.clear();
    const PackageInfo* pkg = findPackage(id);
    if (!pkg || !pkg->external || pkg->gitUrl.empty()) {
        lastError = "Package is not a Git package or was not found.";
        return false;
    }

    std::string fetchCmd = "git -C \"" + pkg->localPath.string() + "\" fetch --quiet";
    runCommand(fetchCmd, outStatus); // ignore fetch failures here

    std::string statusCmd = "git -C \"" + pkg->localPath.string() + "\" status -sb";
    if (!runCommand(statusCmd, outStatus)) {
        lastError = "Failed to read git status.";
        return false;
    }
    return true;
}

bool PackageManager::updateGitPackage(const std::string& id, std::string& outLog) {
    lastError.clear();
    outLog.clear();
    const PackageInfo* pkg = findPackage(id);
    if (!pkg || !pkg->external || pkg->gitUrl.empty()) {
        lastError = "Package is not a Git package or was not found.";
        return false;
    }
    std::string cmd = "git -C \"" + pkg->localPath.string() + "\" pull --ff-only";
    if (!runCommand(cmd, outLog)) {
        lastError = "git pull failed.";
        return false;
    }
    return true;
}
#pragma endregion
