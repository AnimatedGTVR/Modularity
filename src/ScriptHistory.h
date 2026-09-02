#pragma once

#include "Common.h"

#include <unordered_map>
#include <unordered_set>

// Provenance record for a project's compiled-script folder ("Library/CompiledScripts"
// by default, or whatever outDir the project's scripts.modu points at).
//
// Why this exists: the auto-compile scan decides "is this binary stale?" purely from
// source mtime vs binary mtime. That is blind to the *engine* changing underneath the
// binaries. Bump MODULARITY_NATIVE_SCRIPT_ABI_VERSION, or change the SceneObject /
// ScriptContext layout, and every .so on disk is still "newer than its source" - so
// nothing recompiles, and the first thing the user sees is the loader refusing every
// script with "ABI mismatch ... Recompile scripts." at play time.
//
// The history writes down which engine actually produced each binary, so the scan can
// tell "up to date" apart from "up to date against an engine that no longer exists".
struct ScriptToolchainStamp {
    int abiVersion = 0;
    unsigned long long layoutSignature = 0;
    std::string engineVersion;
    // size:mtime of the ScriptRuntime.h the compiler would actually include. Catches
    // header edits that change inlined script-facing code without an ABI bump - the
    // .d dependency files catch those at compile time, but only for scripts something
    // already decided to compile, which is exactly what this is deciding.
    std::string apiStamp;

    bool valid() const { return abiVersion != 0; }
};

// True when a binary stamped `built` is safe to load into an engine stamped `current`.
// Fields that are unknown on either side (empty version/apiStamp, zero layout signature)
// are skipped rather than treated as a mismatch: an unknown is not evidence of drift, and
// failing them open keeps odd setups (no discoverable engine headers, say) from
// recompiling the whole project on every launch.
bool scriptToolchainStampsCompatible(const ScriptToolchainStamp& built,
                                     const ScriptToolchainStamp& current);

// The stamp the running editor would compile against right now. `includeDirs` comes from
// the resolved ScriptBuildConfig; it is only used to locate ScriptRuntime.h.
ScriptToolchainStamp currentScriptToolchainStamp(const std::vector<fs::path>& includeDirs);

struct ScriptHistoryEntry {
    ScriptToolchainStamp stamp;
    fs::path binaryPath;
    // Write time of binaryPath as of the compile that produced it. A mismatch means the
    // binary was replaced behind our back (copied in from another machine, restored from
    // a backup, ...), so its provenance is no longer the one recorded here.
    long long binaryWriteTime = 0;
};

class ScriptHistory {
public:
    // Normalized absolute path, the same key shape Engine's auto-compile scan uses for
    // sources. Kept here so both sides cannot drift apart.
    static std::string makeKey(const fs::path& path);
    static long long toTimeRep(const fs::file_time_type& time);
    static fs::path fileForOutDir(const fs::path& outDir);

    // Reads the manifest. Returns false when it is absent or unreadable, which puts the
    // history in "migrating" mode: pre-existing binaries are adopted at the current stamp
    // instead of being condemned, so shipping this feature does not force every existing
    // project into a full rebuild on first launch.
    bool load(const fs::path& file);
    bool save(const fs::path& file, std::string& error);
    void reset();

    bool isMigrating() const { return migrating_; }
    bool isDirty() const { return dirty_; }

    const ScriptToolchainStamp& stamp() const { return stamp_; }
    void setStamp(const ScriptToolchainStamp& stamp);

    const ScriptHistoryEntry* find(const std::string& sourceKey) const;
    const std::unordered_map<std::string, ScriptHistoryEntry>& entries() const { return entries_; }

    void record(const std::string& sourceKey, const ScriptHistoryEntry& entry);
    void forget(const std::string& sourceKey);
    void clearEntries();
    // Drops entries whose source no longer exists in the project.
    void retainOnly(const std::unordered_set<std::string>& sourceKeys);

private:
    ScriptToolchainStamp stamp_;
    std::unordered_map<std::string, ScriptHistoryEntry> entries_;
    bool migrating_ = true;
    bool dirty_ = false;
};
