#include "../../include/Platform/AssetSource.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>

namespace fs = std::filesystem;

namespace Modularity::Platform {

namespace {

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
        if (p.is_absolute()) return p;
        return mRoot / p;
    }
    fs::path mRoot;
};

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

} // namespace Modularity::Platform
