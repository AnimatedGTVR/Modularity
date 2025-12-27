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
    std::vector<float> waveformLeft; // Left channel envelope (if available)
    std::vector<float> waveformRight; // Right channel envelope (if available)
};

class AudioSystem {
public:
    bool init();
    void shutdown();
    bool isReady() const { return initialized; }

    void onPlayStart(const std::vector<SceneObject>& objects);
    void onPlayStop();
    void update(const std::vector<SceneObject>& objects, const Camera& listenerCamera, bool playing);

    bool playPreview(const std::string& path, float volume = 1.0f, bool loop = false);
    void stopPreview();
    bool isPreviewing(const std::string& path) const;
    const AudioClipPreview* getPreview(const std::string& path);
    bool getPreviewTime(const std::string& path, double& cursorSeconds, double& durationSeconds) const;
    bool seekPreview(const std::string& path, double seconds);
    bool setPreviewLoop(bool loop);

    // Scene audio control (runtime)
    bool playObjectSound(const SceneObject& obj);
    bool stopObjectSound(int objectId);
    bool setObjectLoop(const SceneObject& obj, bool loop);
    bool setObjectVolume(const SceneObject& obj, float volume);

private:
    struct ActiveSound {
        ma_sound sound;
        std::string clipPath;
        bool spatial = true;
        bool started = false; // prevents auto-restart after manual stop
    };

    ma_engine engine{};
    bool initialized = false;
    std::unordered_map<int, std::unique_ptr<ActiveSound>> activeSounds;
    std::unordered_map<std::string, AudioClipPreview> previewCache;
    std::unordered_set<std::string> missingClips;

    ma_sound previewSound{};
    bool previewActive = false;
    std::string previewPath;

    void destroyActiveSounds();
    bool ensureSoundFor(const SceneObject& obj);
    void refreshSoundParams(const SceneObject& obj, ActiveSound& snd);
    AudioClipPreview loadPreview(const std::string& path);
};
