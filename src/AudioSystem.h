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

    struct SimpleReverbNode {
        ma_node_base baseNode;
        int channels = 0;
        int sampleRate = 0;
        std::vector<std::vector<float>> combBuffers;
        std::vector<size_t> combIndex;
        std::vector<std::vector<float>> allpassBuffers;
        std::vector<size_t> allpassIndex;
        std::vector<float> preDelayBuffer;
        size_t preDelayIndex = 0;
        std::vector<float> reflectionsBuffer;
        size_t reflectionsIndex = 0;
        std::vector<float> lpState;
        float wetGain = 0.0f;
        float reflectionsGain = 0.0f;
        float decayTime = 1.5f;
        float decayHFRatio = 0.5f;
        float diffusion = 100.0f;
        float density = 100.0f;
        float hfReference = 5000.0f;
        float preDelaySeconds = 0.01f;
        float reflectionsDelaySeconds = 0.01f;
        size_t preDelayMaxFrames = 0;
        size_t reflectionsMaxFrames = 0;
    };

private:
    struct ReverbSettings {
        float room = -10000.0f;
        float roomHF = -10000.0f;
        float roomLF = -10000.0f;
        float decayTime = 1.5f;
        float decayHFRatio = 0.5f;
        float reflections = -10000.0f;
        float reflectionsDelay = 0.01f;
        float reverb = -10000.0f;
        float reverbDelay = 0.01f;
        float hfReference = 5000.0f;
        float lfReference = 250.0f;
        float roomRolloffFactor = 0.0f;
        float diffusion = 100.0f;
        float density = 100.0f;
        float dry = 1.0f;
    };

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

    SimpleReverbNode reverbNode{};
    ma_splitter_node reverbSplitter{};
    ma_sound_group reverbGroup{};
    bool reverbReady = false;
    ReverbSettings currentReverb{};

    ma_sound previewSound{};
    bool previewActive = false;
    std::string previewPath;

    void destroyActiveSounds();
    bool ensureSoundFor(const SceneObject& obj);
    void refreshSoundParams(const SceneObject& obj, ActiveSound& snd);
    float computeCustomAttenuation(const SceneObject& obj, const glm::vec3& listenerPos) const;
    AudioClipPreview loadPreview(const std::string& path);
    void updateReverb(const std::vector<SceneObject>& objects, const glm::vec3& listenerPos);
    ReverbSettings getReverbTarget(const std::vector<SceneObject>& objects, const glm::vec3& listenerPos, float& outBlend) const;
    void applyReverbSettings(const ReverbSettings& target, float blend);
    void shutdownReverbGraph();
};
