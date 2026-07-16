#pragma once

// read-only asset access. engine code loading packaged content goes through this instead
// of std::ifstream so the same code runs on desktop (DesktopAssetSource, filesystem-rooted)
// and Android (AndroidAssetSource, straight into the APK's assets/).
// both log failed reads, so broken paths show up without per-call-site logging.
// writes do NOT go through this; use std::filesystem and the Android data dir for those.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Modularity::Platform {

// forward-only read over an asset. cheap to construct from Open(); release streams
// before the AssetSource that made them.
class AssetStream {
public:
    virtual ~AssetStream() = default;

    // Read up to `bytes` into `dest`. Returns the number actually read
    // (0 at end-of-stream, or partial reads when the buffer is exhausted).
    virtual size_t Read(void* dest, size_t bytes) = 0;

    // reposition the stream, `whence` matches C stdio. false if the source can't do that seek (rare).
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

    // stream for incremental reads (nullptr on failure). for big assets where buffering the
    // whole thing is wasteful, like ModuPak archives we only index.
    virtual std::unique_ptr<AssetStream> Open(const std::string& path) const = 0;
};

// process-wide accessor. first call without SetAssetSource() gives a DesktopAssetSource
// rooted at cwd, so unmigrated call sites keep working.
AssetSource& GetAssetSource();

// install a new source (takes ownership). AndroidRuntime installs the AAssetManager one
// before Engine exists; desktop can also pin the root if cwd isn't right.
void SetAssetSource(std::unique_ptr<AssetSource> source);

} // namespace Modularity::Platform
