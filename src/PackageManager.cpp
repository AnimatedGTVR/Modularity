#include "PackageManager.h"

#include <unordered_set>
#include <array>
#include <cstdio>
#include <sstream>

namespace {
fs::path normalizePath(const fs::path& p) {
    std::error_code ec;
    fs::path canonical = fs::weakly_canonical(p, ec);
    if (ec) {
        canonical = fs::absolute(p, ec);
    }
    return canonical.lexically_normal();
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
} // namespace

PackageManager::PackageManager() {
    buildRegistry();
}

void PackageManager::setProjectRoot(const fs::path& root) {
    buildRegistry();
    projectRoot = root;
    manifestPath = projectRoot / "packages.modu";
    loadManifest();
}

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
        if (isGitRepo(projectRoot)) {
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

        std::string id;
        if (cleaned.rfind("package=", 0) == 0) {
            id = cleaned.substr(8);
            if (!id.empty() && !isInstalled(id) && findPackage(id)) {
                installedIds.push_back(id);
            }
            continue;
        }

        if (cleaned.rfind("git=", 0) == 0) {
            auto payload = cleaned.substr(4);
            auto parts = split(payload, '|');
            if (parts.size() < 4) continue;
            PackageInfo pkg;
            pkg.id = parts[0];
            pkg.name = parts[1];
            pkg.description = "External package from git";
            pkg.external = true;
            pkg.gitUrl = parts[2];
            fs::path relPath = parts[3];
            pkg.localPath = normalizePath(projectRoot / relPath);

            std::vector<std::string> includeTokens;
            if (parts.size() > 4) includeTokens = split(parts[4], ';');
            for (const auto& inc : includeTokens) {
                if (inc.empty()) continue;
                pkg.includeDirs.push_back(normalizePath(projectRoot / inc));
            }
            std::vector<std::string> defTokens;
            if (parts.size() > 5) defTokens = split(parts[5], ';');
            pkg.defines = defTokens;
            if (parts.size() > 6) pkg.linuxLibs = split(parts[6], ';');
            if (parts.size() > 7) pkg.windowsLibs = split(parts[7], ';');

            registry.push_back(pkg);
            if (!isInstalled(pkg.id)) {
                installedIds.push_back(pkg.id);
            }
        }
    }
}

void PackageManager::saveManifest() const {
    if (manifestPath.empty()) return;
    std::ofstream file(manifestPath);
    if (!file.is_open()) return;

    file << "# Modularity package manifest\n";
    file << "# Add optional script-time dependencies here\n";
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

        file << "git=" << pkg->id << "|"
             << pkg->name << "|"
             << pkg->gitUrl << "|";

        std::error_code ec;
        fs::path relPath = fs::relative(pkg->localPath, projectRoot, ec);
        file << ((!ec ? relPath : pkg->localPath).generic_string()) << "|";
        file << join(relIncludes, ';') << "|";
        file << join(pkg->defines, ';') << "|";
        file << join(pkg->linuxLibs, ';') << "|";
        file << join(pkg->windowsLibs, ';') << "\n";
    }
}

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
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) return false;
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        output += buffer.data();
    }
    int rc = pclose(pipe);
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

fs::path PackageManager::packagesFolder() const {
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

bool PackageManager::checkGitStatus(const std::string& id, std::string& outStatus) {
    lastError.clear();
    outStatus.clear();
    const PackageInfo* pkg = findPackage(id);
    if (!pkg || !pkg->external) {
        lastError = "Package is not external or not found.";
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
    if (!pkg || !pkg->external) {
        lastError = "Package is not external or not found.";
        return false;
    }
    std::string cmd = "git -C \"" + pkg->localPath.string() + "\" pull --ff-only";
    if (!runCommand(cmd, outLog)) {
        lastError = "git pull failed.";
        return false;
    }
    return true;
}
