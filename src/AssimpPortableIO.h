#pragma once

#include "../include/Platform/AssetSource.h"

#include <assimp/DefaultIOSystem.h>
#include <assimp/IOStream.hpp>
#include <assimp/IOSystem.hpp>
#include <assimp/Importer.hpp>

#include <cstring>
#include <string>

namespace Modularity {

// Assimp opens every file it touches through its own IOSystem: the model itself, and then
// whatever that model references - .mtl siblings, external texture maps, .blend libraries -
// resolved relative to the model's own directory. None of that ever reaches AssetSource, so
// the portable-path rescue that fixes textures does nothing for meshes. A scene authored on
// another machine stores an absolute model path that exists nowhere locally, Assimp reports
// only "Unable to open file", and the model silently fails to load while its textures (which
// DO go through AssetSource) come back fine.
//
// Routing the importer through this handler applies the rescue to every open Assimp makes,
// not just the one path we happened to hand it - which is what makes it a fix for the whole
// class of problem rather than for the top-level filename. DefaultIOSystem is `final`, so
// this wraps one instead of deriving from it, keeping Assimp's own wide-char-aware file IO
// underneath and changing only which path that IO is pointed at.
class PortablePathIOSystem final : public Assimp::IOSystem {
public:
    bool Exists(const char* file) const override {
        return mBackend.Exists(Rescue(file).c_str());
    }

    char getOsSeparator() const override { return mBackend.getOsSeparator(); }

    Assimp::IOStream* Open(const char* file, const char* mode = "rb") override {
        // Reads only. A write is creating a path, so redirecting it onto some existing file
        // elsewhere would be actively destructive rather than merely unhelpful.
        if (mode != nullptr && std::strchr(mode, 'w') != nullptr) {
            return mBackend.Open(file, mode);
        }
        return mBackend.Open(Rescue(file).c_str(), mode);
    }

    void Close(Assimp::IOStream* file) override { mBackend.Close(file); }

    bool ComparePaths(const char* one, const char* second) const override {
        return mBackend.ComparePaths(one, second);
    }

private:
    static std::string Rescue(const char* file) {
        if (file == nullptr) return std::string();
        return Modularity::Platform::ResolveAssetPath(file);
    }

    Assimp::DefaultIOSystem mBackend;
};

// Point an importer at the handler above. Importer::SetIOHandler takes ownership and deletes
// whatever handler it held, so this is safe to call once per importer and never unwound.
inline void AttachPortableAssetIO(Assimp::Importer& importer) {
    importer.SetIOHandler(new PortablePathIOSystem());
}

} // namespace Modularity
