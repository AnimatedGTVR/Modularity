#pragma once

#include "Common.h"
#include "SceneObject.h"
#include "Camera.h"
#include "../include/ThirdParty/miniaudio.h"
#include <unordered_map>
#include <unordered_set>

struct AudioClipPreview {
    bool loaded = false;
    std::string path;
    uint32_t channels = 0;
    uint32_t sampleRate = 0;
    double durationSeconds = 0.0;
    std::vector<float> waveform; // Normalized 0..1 amplitude envelope for drawing
};

class AudioSystem {
public:
    bool init();
    void shutdown();
    bool isReady() const { return initialized; }

    void onPlayStart(const std::vector<SceneObject>& objects);
    void onPlayStop();
    void update(const std::vector<SceneObject>& objects, const Camera& listenerCamera, bool playing);

    bool playPreview(const std::string& path, float volume = 1.0f);
    void stopPreview();
    bool isPreviewing(const std::string& path) const;
    const AudioClipPreview* getPreview(const std::string& path);
    bool getPreviewTime(const std::string& path, double& cursorSeconds, double& durationSeconds) const;
    bool seekPreview(const std::string& path, double seconds);

private:
    struct ActiveSound {
        ma_sound sound;
        std::string clipPath;
        bool spatial = true;
    };

    ma_engine engine{};
    bool initialized = false;
    std::unordered_map<int, ActiveSound> activeSounds;
    std::unordered_map<std::string, AudioClipPreview> previewCache;

    ma_sound previewSound{};
    bool previewActive = false;
    std::string previewPath;

    void destroyActiveSounds();
    bool ensureSoundFor(const SceneObject& obj);
    void refreshSoundParams(const SceneObject& obj, ActiveSound& snd);
    AudioClipPreview loadPreview(const std::string& path);
};
