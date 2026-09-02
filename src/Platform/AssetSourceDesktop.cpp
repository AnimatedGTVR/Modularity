#include "../../include/Platform/AssetSource.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string_view>

namespace fs = std::filesystem;

namespace Modularity::Platform {

namespace {

// Declared up front: unqualified names used inside an inline member body are looked up
// against the namespace scope visible at the class definition, not at end of file.
fs::path PortableAssetRoot();

// Re-root an asset path saved on another machine onto the open project. The stored path
// always runs through one of the project's own top-level folders, so the tail from that
// folder onwards is the project-relative path we want. Every split point is tried and a
// candidate is only accepted once it names a file that exists, so this can never invent a
// path - a genuinely missing asset still misses, and still logs. Empty when nothing matched.
// The top-level folders a project path can be re-rooted at. Used both to rescue a
// foreign path for reading and to convert one back to a portable relative form.
constexpr std::string_view kProjectFolders[] = {
    "/Assets/", "/Library/", "/Scenes/", "/Scripts/",
    "/Resources/", "/BuildProfiles/", "/ProjectSettings/"
};

fs::path RescueForeignProjectPath(const std::string& raw) {
    const fs::path projectRoot = PortableAssetRoot();
    if (projectRoot.empty() || raw.empty()) return {};

    std::string normalized = raw;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');

    // A project-relative path - what ToPortableAssetPath produces, and now the stored form -
    // resolves straight against the project root. This has to come first: without it such a
    // path is only ever tried against the process CWD (the executable's directory), and the
    // marker loop below cannot rescue it either, because those markers carry a leading '/'
    // and a bare "Assets/..." has none. That combination silently lost every texture.
    if (!normalized.empty() && normalized.front() != '/' && !fs::path(normalized).is_absolute()) {
        const fs::path candidate = projectRoot / fs::path(normalized);
        std::error_code relEc;
        if (fs::exists(candidate, relEc) && !relEc) return candidate;
    }

    for (std::string_view folder : kProjectFolders) {
        for (size_t at = normalized.find(folder); at != std::string::npos;
             at = normalized.find(folder, at + 1)) {
            // +1 drops the leading '/' so the tail appends instead of replacing the root.
            const fs::path candidate = projectRoot / fs::path(normalized.substr(at + 1));
            std::error_code ec;
            if (fs::exists(candidate, ec) && !ec) return candidate;
        }
    }
    return {};
}

class DesktopAssetStream final : public AssetStream {
public:
    explicit DesktopAssetStream(std::ifstream stream, int64_t size)
        : mStream(std::move(stream)), mSize(size) {}

    size_t Read(void* dest, size_t bytes) override {
        mStream.read(reinterpret_cast<char*>(dest), static_cast<std::streamsize>(bytes));
        return static_cast<size_t>(mStream.gcount());
    }
    bool Seek(int64_t offset, int whence) override {
        std::ios_base::seekdir dir = std::ios_base::beg;
        if (whence == SEEK_CUR) dir = std::ios_base::cur;
        else if (whence == SEEK_END) dir = std::ios_base::end;
        mStream.clear();
        mStream.seekg(static_cast<std::streamoff>(offset), dir);
        return mStream.good();
    }
    int64_t Tell() const override {
        return const_cast<std::ifstream&>(mStream).tellg();
    }
    int64_t Size() const override { return mSize; }
private:
    std::ifstream mStream;
    int64_t mSize;
};

class DesktopAssetSource final : public AssetSource {
public:
    explicit DesktopAssetSource(fs::path root) : mRoot(std::move(root)) {}

    bool Exists(const std::string& path) const override {
        std::error_code ec;
        return fs::exists(Resolve(path), ec) && !ec;
    }

    std::vector<uint8_t> ReadAll(const std::string& path) const override {
        const fs::path resolved = Resolve(path);
        std::ifstream f(resolved, std::ios::binary | std::ios::ate);
        if (!f) {
            std::cerr << "[AssetSource] ReadAll miss: " << resolved.string() << "\n";
            return {};
        }
        const std::streamsize n = f.tellg();
        if (n <= 0) return {};
        f.seekg(0, std::ios::beg);
        std::vector<uint8_t> buf(static_cast<size_t>(n));
        if (!f.read(reinterpret_cast<char*>(buf.data()), n)) {
            std::cerr << "[AssetSource] ReadAll short read: " << resolved.string() << "\n";
            buf.clear();
        }
        return buf;
    }

    std::unique_ptr<AssetStream> Open(const std::string& path) const override {
        const fs::path resolved = Resolve(path);
        std::ifstream f(resolved, std::ios::binary | std::ios::ate);
        if (!f) {
            std::cerr << "[AssetSource] Open miss: " << resolved.string() << "\n";
            return nullptr;
        }
        const int64_t size = f.tellg();
        f.seekg(0, std::ios::beg);
        return std::make_unique<DesktopAssetStream>(std::move(f), size);
    }

private:
    fs::path Resolve(const std::string& path) const {
        fs::path p(path);
        // A POSIX-rooted path is NOT is_absolute() on Windows, and appending one keeps only
        // the drive from mRoot ("C:/home/anemunt/..."). Either way the result does not exist,
        // which is what sends us into the rescue below.
        const fs::path direct = p.is_absolute() ? p : (mRoot / p);
        std::error_code ec;
        if (fs::exists(direct, ec) && !ec) return direct;

        const fs::path rescued = RescueForeignProjectPath(path);
        return rescued.empty() ? direct : rescued;
    }
    fs::path mRoot;
};

std::mutex& portableRootMutex() {
    static std::mutex m;
    return m;
}
fs::path& portableRootStorage() {
    static fs::path root;
    return root;
}
fs::path PortableAssetRoot() {
    std::lock_guard<std::mutex> lock(portableRootMutex());
    return portableRootStorage();
}

// Global storage. Lazy-initialized to a DesktopAssetSource rooted at
// the current working directory if nothing has been explicitly set.
std::unique_ptr<AssetSource>& globalSource() {
    static std::unique_ptr<AssetSource> source;
    return source;
}
std::mutex& globalMutex() {
    static std::mutex m;
    return m;
}

} // namespace

AssetSource& GetAssetSource() {
    std::lock_guard<std::mutex> lock(globalMutex());
    auto& src = globalSource();
    if (!src) {
        src = std::make_unique<DesktopAssetSource>(fs::current_path());
    }
    return *src;
}

void SetAssetSource(std::unique_ptr<AssetSource> source) {
    std::lock_guard<std::mutex> lock(globalMutex());
    globalSource() = std::move(source);
}

void SetPortableAssetRoot(const std::string& projectRoot) {
    std::lock_guard<std::mutex> lock(portableRootMutex());
    portableRootStorage() = projectRoot.empty() ? fs::path() : fs::path(projectRoot);
}

std::string ResolveAssetPath(const std::string& path) {
    if (path.empty()) return path;
    std::error_code ec;
    if (fs::exists(fs::path(path), ec) && !ec) return path;

    const fs::path rescued = RescueForeignProjectPath(path);
    return rescued.empty() ? path : rescued.string();
}

std::string ToPortableAssetPath(const std::string& path) {
    if (path.empty()) return path;
    const fs::path projectRoot = PortableAssetRoot();
    if (projectRoot.empty()) return path;

    std::string normalized = path;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');

    // A POSIX-rooted path is not is_absolute() on Windows (and a "C:/..." one is not
    // absolute on Linux), so test for a leading '/' separately rather than trusting
    // is_absolute() to recognise the other platform's roots.
    const fs::path candidate(normalized);
    const bool posixRooted = normalized.front() == '/';
    if (!candidate.is_absolute() && !posixRooted) {
        return normalized;  // already relative: just normalised separators
    }

    // Native absolute path inside the open project - take the plain relative form.
    std::error_code ec;
    const fs::path relative = fs::relative(candidate, projectRoot, ec);
    if (!ec && !relative.empty()) {
        const std::string generic = relative.generic_string();
        // relative() happily walks upwards; anything starting ".." is outside the
        // project and must stay absolute.
        if (generic.rfind("..", 0) != 0 && generic != ".") return generic;
    }

    // Otherwise it was authored on another machine or OS. Keep the tail from the
    // first project folder marker that names a file which actually exists here.
    for (std::string_view folder : kProjectFolders) {
        for (size_t at = normalized.find(folder); at != std::string::npos;
             at = normalized.find(folder, at + 1)) {
            const std::string tail = normalized.substr(at + 1);
            std::error_code existsEc;
            if (fs::exists(projectRoot / fs::path(tail), existsEc) && !existsEc) return tail;
        }
    }
    return path;
}

} // namespace Modularity::Platform
