#pragma once

// Read-only asset access abstraction. Engine code that needs to load
// packaged project content (ModuPak archives, engine fonts/icons, scene
// files when wrapped) goes through this instead of std::ifstream so the
// same source compiles + runs on:
//
//   * Desktop — DesktopAssetSource, std::filesystem-backed, paths are
//     resolved relative to a configured root (typically the current
//     working directory or the executable's resource dir).
//   * Android — AndroidAssetSource, AAssetManager-backed, paths key
//     directly into the APK's assets/ directory.
//
// Both implementations log failed reads via std::cerr / logcat so the
// path that broke shows up in logs without the engine having to add its
// own logging at every call site.
//
// Writes (savefiles, project edits) do NOT go through this abstraction —
// they're outside Stage 3's read-only-asset scope. Use std::filesystem
// for those and handle the Android writable-data-dir separately.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Modularity::Platform {

// Forward iterator-style read over an asset. Implementations are
// expected to be cheap to construct from AssetSource::Open(). Returned
// streams should be released before the AssetSource that produced them.
class AssetStream {
public:
    virtual ~AssetStream() = default;

    // Read up to `bytes` into `dest`. Returns the number actually read
    // (0 at end-of-stream, or partial reads when the buffer is exhausted).
    virtual size_t Read(void* dest, size_t bytes) = 0;

    // Reposition the stream. `whence` matches C stdio (SEEK_SET / SEEK_CUR
    // / SEEK_END). Returns false if the underlying source doesn't support
    // the requested seek (rare).
    virtual bool Seek(int64_t offset, int whence) = 0;

    // Current read position from start of stream. -1 if unknown.
    virtual int64_t Tell() const = 0;

    // Total bytes in the asset. -1 if unknown (some Android asset modes
    // are streaming-only and can't report size up-front).
    virtual int64_t Size() const = 0;
};

class AssetSource {
public:
    virtual ~AssetSource() = default;

    // True iff `path` resolves to a readable asset in this source.
    virtual bool Exists(const std::string& path) const = 0;

    // Reads the asset in full into memory. Returns empty on failure (and
    // logs the path so the failure is debuggable from outside).
    virtual std::vector<uint8_t> ReadAll(const std::string& path) const = 0;

    // Opens a stream over the asset for incremental reads. Returns nullptr
    // on failure. Useful for large assets where loading the whole thing
    // into RAM is wasteful (e.g. ModuPak archives the engine indexes
    // without fully buffering).
    virtual std::unique_ptr<AssetStream> Open(const std::string& path) const = 0;
};

// Process-wide accessor. The first call to GetAssetSource() with no
// prior SetAssetSource() returns a DesktopAssetSource rooted at the
// current working directory — matches the engine's pre-Stage-3
// behavior so unmigrated call sites keep working unchanged.
AssetSource& GetAssetSource();

// Installs a new source (taking ownership). AndroidRuntime calls this
// at startup to install an AAssetManager-backed source before Engine
// is constructed; the desktop player can also call it if it wants to
// pin the root (e.g. to the executable directory instead of cwd).
void SetAssetSource(std::unique_ptr<AssetSource> source);

} // namespace Modularity::Platform
