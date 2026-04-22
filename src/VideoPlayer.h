#pragma once

#include <array>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <glad/glad.h>

class VideoPlayer {
public:
    VideoPlayer();
    ~VideoPlayer();

    bool LoadVideo(const std::string& path);
    void Play();
    void Pause();
    void Stop();
    void SetLoop(bool loop);
    void SetPlaybackSpeed(float speed);
    void SetPointFiltering(bool enabled);
    void Update(float deltaSeconds);

    GLuint GetTextureId() const { return m_textureId; }
    bool HasTextureOverride() const { return m_textureId != 0 && m_hasPresentedFrame; }
    bool IsLoaded() const { return m_loaded; }
    bool IsPlaying() const { return m_playing && !m_paused; }
    const std::string& GetLoadedPath() const { return m_loadedPath; }
    const std::string& GetLastError() const { return m_lastError; }

private:
    struct DecoderState;
    struct FrameSlot {
        std::vector<unsigned char> pixels;
        double ptsSeconds = 0.0;
    };

    enum class DecodeResult {
        FrameReady,
        EndOfStream,
        Error
    };

    static constexpr size_t kFrameQueueCapacity = 3;

    void StartWorker();
    void StopWorker();
    void DestroyTexture();
    void ShutdownDecoder();
    void ClearQueuedFramesLocked();
    bool OpenDecoder(const std::string& path);
    bool EnsureTextureAllocated();
    void ApplyTextureFilter();
    bool SeekToStart();
    DecodeResult DecodeIntoSlot(FrameSlot& slot, double& outPtsSeconds);
    double ResolveFramePtsSeconds() const;
    void WorkerMain();

    std::unique_ptr<DecoderState> m_decoder;
    std::thread m_workerThread;
    std::mutex m_queueMutex;
    std::condition_variable m_queueCv;
    std::array<FrameSlot, kFrameQueueCapacity> m_frameQueue;
    size_t m_queueReadIndex = 0;
    size_t m_queueCount = 0;
    bool m_workerExitRequested = false;
    bool m_requestSeekToStart = false;
    bool m_decoderReachedEnd = false;

    std::string m_loadedPath;
    std::string m_lastError;
    int m_width = 0;
    int m_height = 0;
    double m_durationSeconds = 0.0;
    double m_playbackTimeSeconds = 0.0;
    float m_playbackSpeed = 1.0f;
    bool m_loop = true;
    bool m_playing = false;
    bool m_paused = false;
    bool m_loaded = false;
    bool m_pointFiltering = false;
    bool m_hasPresentedFrame = false;
    GLuint m_textureId = 0;
};
