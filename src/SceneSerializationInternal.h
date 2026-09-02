#pragma once

#include "ProjectManager.h"
#include <istream>
#include <ostream>
#include <string>

namespace SceneSerializationInternal {

constexpr int kLegacySceneFormatVersion = 28;
constexpr int kModularSceneFormatVersion = 29;

// True for a value rooted on either platform ("/home/...", "\\server\share", "C:/...").
bool SceneValueLooksLikeAbsolutePath(const std::string& value);

// Rewrites absolute asset paths in emitted scene text to project-relative form, keeping a
// saved scene portable between Linux and Windows in both directions.
std::string MakeSceneTextPathsPortable(const std::string& sceneText);

bool WriteLegacySceneStream(std::ostream& out,
                            const std::vector<SceneObject>& objects,
                            int nextId,
                            float timeOfDay,
                            const SkyboxSettings& skyboxSettings);

bool SaveLegacyScene(const fs::path& filePath,
                     const std::vector<SceneObject>& objects,
                     int nextId,
                     float timeOfDay,
                     const SkyboxSettings& skyboxSettings,
                     SceneSerializer::Metadata* metadata = nullptr);

bool LoadLegacySceneStream(std::istream& in,
                           std::vector<SceneObject>& objects,
                           int& nextId,
                           int& outVersion,
                           float* outTimeOfDay,
                           SkyboxSettings* outSkyboxSettings,
                           bool deferAssetLoading,
                           SceneSerializer::Metadata* outMetadata = nullptr);

bool LoadLegacyScene(const fs::path& filePath,
                     std::vector<SceneObject>& objects,
                     int& nextId,
                     int& outVersion,
                     float* outTimeOfDay,
                     SkyboxSettings* outSkyboxSettings,
                     bool deferAssetLoading,
                     SceneSerializer::Metadata* outMetadata = nullptr);

} // namespace SceneSerializationInternal
