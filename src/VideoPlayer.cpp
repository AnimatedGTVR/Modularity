#include "VideoPlayer.h"

#include <algorithm>
#include <iostream>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
}

namespace {
std::string FfmpegErrorString(int errorCode) {
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(errorCode, buffer, sizeof(buffer));
    return std::string(buffer);
}

double RationalToSeconds(const AVRational rational) {
    return (rational.num > 0 && rational.den > 0) ? av_q2d(rational) : 0.0;
}
} // namespace

struct VideoPlayer::DecoderState {
    AVFormatContext* formatContext = nullptr;
    AVCodecContext* codecContext = nullptr;
    SwsContext* swsContext = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    int videoStreamIndex = -1;
    AVRational timeBase = AVRational{1, 1};
    double fallbackFrameDurationSeconds = 1.0 / 30.0;
    mutable double nextFallbackPtsSeconds = 0.0;
    bool drainingDecoder = false;
    bool hasPendingPacket = false;
};

VideoPlayer::VideoPlayer() = default;

VideoPlayer::~VideoPlayer() {
    StopWorker();
    ShutdownDecoder();
    DestroyTexture();
}

std::string VideoPlayer::GetLastError() const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_lastError;
}

void VideoPlayer::SetLastError(std::string error) {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_lastError = std::move(error);
}

bool VideoPlayer::LoadVideo(const std::string& path) {
    StopWorker();
    ShutdownDecoder();

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        ClearQueuedFramesLocked();
        m_requestSeekToStart = false;
        m_decoderReachedEnd = false;
    }

    m_loaded = false;
    m_loadedPath.clear();
    SetLastError("");
    m_width = 0;
    m_height = 0;
    m_durationSeconds = 0.0;
    m_playbackTimeSeconds = 0.0;
    m_playing = false;
    m_paused = false;
    m_hasPresentedFrame = false;

    if (path.empty()) {
        SetLastError("Video path is empty.");
        return false;
    }

    if (!OpenDecoder(path)) {
        return false;
    }

    if (!EnsureTextureAllocated()) {
        ShutdownDecoder();
        SetLastError("Failed to allocate OpenGL texture for video playback.");
        return false;
    }

    const size_t pixelBytes = static_cast<size_t>(m_width) * static_cast<size_t>(m_height) * 4u;
    m_uploadBuffer.assign(pixelBytes, 0u);
    for (FrameSlot& slot : m_frameQueue) {
        slot.pixels.assign(pixelBytes, 0u);
        slot.ptsSeconds = 0.0;
    }

    double firstFramePtsSeconds = 0.0;
    const DecodeResult firstDecodeResult = DecodeIntoSlot(m_frameQueue[0], firstFramePtsSeconds);
    if (firstDecodeResult == DecodeResult::FrameReady) {
        m_frameQueue[0].ptsSeconds = firstFramePtsSeconds;
        m_queueReadIndex = 0;
        m_queueCount = 1;
    } else if (firstDecodeResult == DecodeResult::Error) {
        ShutdownDecoder();
        SetLastError(GetLastError().empty() ? "Failed to decode the first video frame." : GetLastError());
        return false;
    } else {
        ShutdownDecoder();
        SetLastError("The video stream ended before a frame could be decoded.");
        return false;
    }

    m_loaded = true;
    m_loadedPath = path;
    StartWorker();
    return true;
}

void VideoPlayer::Play() {
    if (!m_loaded) return;

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        if (m_decoderReachedEnd && m_queueCount == 0 && !m_loop) {
            m_requestSeekToStart = true;
            m_decoderReachedEnd = false;
            m_playbackTimeSeconds = 0.0;
            m_hasPresentedFrame = false;
        }
    }

    m_playing = true;
    m_paused = false;
    m_queueCv.notify_all();
}

void VideoPlayer::Pause() {
    if (!m_loaded) return;
    m_paused = true;
}

void VideoPlayer::Stop() {
    if (!m_loaded) return;

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        ClearQueuedFramesLocked();
        m_requestSeekToStart = true;
        m_decoderReachedEnd = false;
    }

    m_playbackTimeSeconds = 0.0;
    m_playing = false;
    m_paused = false;
    m_hasPresentedFrame = false;
    m_queueCv.notify_all();
}

void VideoPlayer::SetLoop(bool loop) {
    m_loop = loop;
    m_queueCv.notify_all();
}

void VideoPlayer::SetPlaybackSpeed(float speed) {
    m_playbackSpeed = std::max(0.0f, speed);
}

void VideoPlayer::SetPointFiltering(bool enabled) {
    if (m_pointFiltering == enabled) return;
    m_pointFiltering = enabled;
    ApplyTextureFilter();
}

void VideoPlayer::Update(float deltaSeconds) {
    if (!m_loaded || m_decoder == nullptr || m_textureId == 0) {
        return;
    }

    if (m_playing && !m_paused) {
        const float clampedDelta = std::max(0.0f, deltaSeconds);
        m_playbackTimeSeconds += static_cast<double>(clampedDelta) * static_cast<double>(m_playbackSpeed);
        if (!m_loop && m_durationSeconds > 0.0) {
            m_playbackTimeSeconds = std::min(m_playbackTimeSeconds, m_durationSeconds);
        }
    }

    constexpr double kPresentLeadSeconds = 0.0015;
    bool shouldUpload = false;
    int uploadWidth = 0;
    int uploadHeight = 0;

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);

        while (m_queueCount > 1) {
            const size_t nextIndex = (m_queueReadIndex + 1) % kFrameQueueCapacity;
            if (m_frameQueue[nextIndex].ptsSeconds <= m_playbackTimeSeconds + kPresentLeadSeconds) {
                m_queueReadIndex = nextIndex;
                --m_queueCount;
                continue;
            }
            break;
        }

        if (m_queueCount > 0) {
            const FrameSlot& front = m_frameQueue[m_queueReadIndex];
            const bool shouldPrimeTexture = !m_hasPresentedFrame;
            if (shouldPrimeTexture || front.ptsSeconds <= m_playbackTimeSeconds + kPresentLeadSeconds) {
                if (m_uploadBuffer.size() != front.pixels.size()) {
                    m_uploadBuffer.resize(front.pixels.size());
                }
                std::copy(front.pixels.begin(), front.pixels.end(), m_uploadBuffer.begin());
                shouldUpload = true;
                uploadWidth = m_width;
                uploadHeight = m_height;
                m_queueReadIndex = (m_queueReadIndex + 1) % kFrameQueueCapacity;
                --m_queueCount;
            }
        }
    }

    if (shouldUpload) {
        glBindTexture(GL_TEXTURE_2D, m_textureId);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
#ifdef GL_UNPACK_ROW_LENGTH
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
#endif
#ifdef GL_UNPACK_SKIP_PIXELS
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
#endif
#ifdef GL_UNPACK_SKIP_ROWS
        glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
#endif
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, uploadWidth, uploadHeight, GL_RGBA, GL_UNSIGNED_BYTE, m_uploadBuffer.data());
        glBindTexture(GL_TEXTURE_2D, 0);

        m_hasPresentedFrame = true;
        m_queueCv.notify_all();
    }

    bool finishedPlayback = false;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        finishedPlayback = !m_loop &&
                           m_decoderReachedEnd &&
                           m_queueCount == 0 &&
                           m_durationSeconds > 0.0 &&
                           m_playbackTimeSeconds >= (m_durationSeconds - kPresentLeadSeconds);
    }
    if (finishedPlayback) {
        m_playing = false;
        m_paused = false;
    }
}

void VideoPlayer::StartWorker() {
    if (m_workerThread.joinable() || !m_loaded || m_decoder == nullptr) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_workerExitRequested = false;
    }

    m_workerThread = std::thread(&VideoPlayer::WorkerMain, this);
}

void VideoPlayer::StopWorker() {
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_workerExitRequested = true;
    }
    m_queueCv.notify_all();

    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_workerExitRequested = false;
        ClearQueuedFramesLocked();
        m_requestSeekToStart = false;
        m_decoderReachedEnd = false;
    }
}

void VideoPlayer::DestroyTexture() {
    if (m_textureId != 0) {
        glDeleteTextures(1, &m_textureId);
        m_textureId = 0;
    }
    m_hasPresentedFrame = false;
}

void VideoPlayer::ShutdownDecoder() {
    if (m_decoder == nullptr) {
        return;
    }

    if (m_decoder->packet != nullptr) {
        av_packet_free(&m_decoder->packet);
    }
    if (m_decoder->frame != nullptr) {
        av_frame_free(&m_decoder->frame);
    }
    if (m_decoder->swsContext != nullptr) {
        sws_freeContext(m_decoder->swsContext);
        m_decoder->swsContext = nullptr;
    }
    if (m_decoder->codecContext != nullptr) {
        avcodec_free_context(&m_decoder->codecContext);
    }
    if (m_decoder->formatContext != nullptr) {
        avformat_close_input(&m_decoder->formatContext);
    }

    m_decoder.reset();
}

void VideoPlayer::ClearQueuedFramesLocked() {
    m_queueReadIndex = 0;
    m_queueCount = 0;
    for (FrameSlot& slot : m_frameQueue) {
        slot.ptsSeconds = 0.0;
    }
}

bool VideoPlayer::OpenDecoder(const std::string& path) {
    avformat_network_init();

    std::unique_ptr<DecoderState> decoder = std::make_unique<DecoderState>();

    int result = avformat_open_input(&decoder->formatContext, path.c_str(), nullptr, nullptr);
    if (result < 0) {
        SetLastError("Failed to open video '" + path + "': " + FfmpegErrorString(result));
        return false;
    }

    result = avformat_find_stream_info(decoder->formatContext, nullptr);
    if (result < 0) {
        SetLastError("Failed to read video stream info: " + FfmpegErrorString(result));
        return false;
    }

    decoder->videoStreamIndex = av_find_best_stream(decoder->formatContext, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (decoder->videoStreamIndex < 0) {
        SetLastError("No video stream found in '" + path + "'.");
        return false;
    }

    AVStream* stream = decoder->formatContext->streams[decoder->videoStreamIndex];
    const AVCodecParameters* params = stream->codecpar;
    const AVCodec* codec = avcodec_find_decoder(params->codec_id);
    if (codec == nullptr) {
        SetLastError("No decoder available for the video's codec.");
        return false;
    }

    decoder->codecContext = avcodec_alloc_context3(codec);
    if (decoder->codecContext == nullptr) {
        SetLastError("Failed to allocate FFmpeg codec context.");
        return false;
    }

    result = avcodec_parameters_to_context(decoder->codecContext, params);
    if (result < 0) {
        SetLastError("Failed to copy codec parameters: " + FfmpegErrorString(result));
        return false;
    }

    result = avcodec_open2(decoder->codecContext, codec, nullptr);
    if (result < 0) {
        SetLastError("Failed to open video decoder: " + FfmpegErrorString(result));
        return false;
    }

    decoder->packet = av_packet_alloc();
    decoder->frame = av_frame_alloc();
    if (decoder->packet == nullptr || decoder->frame == nullptr) {
        SetLastError("Failed to allocate FFmpeg packet/frame buffers.");
        return false;
    }

    m_width = std::max(1, decoder->codecContext->width);
    m_height = std::max(1, decoder->codecContext->height);

    decoder->timeBase = stream->time_base;

    AVRational frameRate = stream->avg_frame_rate;
    if (frameRate.num <= 0 || frameRate.den <= 0) {
        frameRate = stream->r_frame_rate;
    }
    if (frameRate.num > 0 && frameRate.den > 0) {
        decoder->fallbackFrameDurationSeconds = std::max(1.0 / 240.0, av_q2d(av_inv_q(frameRate)));
    } else {
        decoder->fallbackFrameDurationSeconds = 1.0 / 30.0;
    }

    if (stream->duration > 0) {
        m_durationSeconds = static_cast<double>(stream->duration) * RationalToSeconds(stream->time_base);
    } else if (decoder->formatContext->duration > 0) {
        m_durationSeconds = static_cast<double>(decoder->formatContext->duration) / static_cast<double>(AV_TIME_BASE);
    } else {
        m_durationSeconds = 0.0;
    }

    m_decoder = std::move(decoder);
    return true;
}

bool VideoPlayer::EnsureTextureAllocated() {
    if (m_width <= 0 || m_height <= 0) {
        return false;
    }

    if (m_textureId == 0) {
        glGenTextures(1, &m_textureId);
        if (m_textureId == 0) {
            return false;
        }
    }

    glBindTexture(GL_TEXTURE_2D, m_textureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    ApplyTextureFilter();
    glBindTexture(GL_TEXTURE_2D, m_textureId);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
#ifdef GL_UNPACK_ROW_LENGTH
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
#endif
#ifdef GL_UNPACK_SKIP_PIXELS
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
#endif
#ifdef GL_UNPACK_SKIP_ROWS
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
#endif
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

bool VideoPlayer::EnsureConversionContextForFrame() {
    if (m_decoder == nullptr || m_decoder->frame == nullptr) {
        SetLastError("No decoded video frame is available for color conversion.");
        return false;
    }

    const int sourceWidth = std::max(1, m_decoder->frame->width);
    const int sourceHeight = std::max(1, m_decoder->frame->height);
    const AVPixelFormat sourceFormat = static_cast<AVPixelFormat>(m_decoder->frame->format);
    if (sourceFormat == AV_PIX_FMT_NONE) {
        SetLastError("Decoded video frame reported an invalid pixel format.");
        return false;
    }

    SwsContext* swsContext = sws_getCachedContext(
        m_decoder->swsContext,
        sourceWidth,
        sourceHeight,
        sourceFormat,
        m_width,
        m_height,
        AV_PIX_FMT_RGBA,
        SWS_BILINEAR,
        nullptr,
        nullptr,
        nullptr);
    if (swsContext == nullptr) {
        SetLastError("Failed to create FFmpeg swscale conversion context.");
        return false;
    }

    m_decoder->swsContext = swsContext;
    return true;
}

void VideoPlayer::ApplyTextureFilter() {
    if (m_textureId == 0) return;

    GLint previouslyBoundTexture = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previouslyBoundTexture);
    glBindTexture(GL_TEXTURE_2D, m_textureId);
    const GLint filter = m_pointFiltering ? GL_NEAREST : GL_LINEAR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previouslyBoundTexture));
}

bool VideoPlayer::SeekToStart() {
    if (m_decoder == nullptr || m_decoder->formatContext == nullptr || m_decoder->codecContext == nullptr) {
        return false;
    }

    const int seekResult = av_seek_frame(m_decoder->formatContext, m_decoder->videoStreamIndex, 0, AVSEEK_FLAG_BACKWARD);
    if (seekResult < 0) {
        SetLastError("Failed to seek video back to the start: " + FfmpegErrorString(seekResult));
        return false;
    }

    avcodec_flush_buffers(m_decoder->codecContext);
    av_frame_unref(m_decoder->frame);
    av_packet_unref(m_decoder->packet);
    m_decoder->nextFallbackPtsSeconds = 0.0;
    m_decoder->drainingDecoder = false;
    m_decoder->hasPendingPacket = false;
    return true;
}

VideoPlayer::DecodeResult VideoPlayer::DecodeIntoSlot(FrameSlot& slot, double& outPtsSeconds) {
    if (m_decoder == nullptr) {
        return DecodeResult::Error;
    }

    uint8_t* dstData[4] = { slot.pixels.data(), nullptr, nullptr, nullptr };
    int dstLinesize[4] = { m_width * 4, 0, 0, 0 };

    for (;;) {
        const int receiveResult = avcodec_receive_frame(m_decoder->codecContext, m_decoder->frame);
        if (receiveResult == 0) {
            if (!EnsureConversionContextForFrame()) {
                av_frame_unref(m_decoder->frame);
                return DecodeResult::Error;
            }

            const int sourceHeight = std::max(1, m_decoder->frame->height);
            const int scaledRows = sws_scale(m_decoder->swsContext,
                                             m_decoder->frame->data,
                                             m_decoder->frame->linesize,
                                             0,
                                             sourceHeight,
                                             dstData,
                                             dstLinesize);
            if (scaledRows <= 0) {
                SetLastError("Failed to convert decoded video frame to RGBA.");
                av_frame_unref(m_decoder->frame);
                return DecodeResult::Error;
            }

            outPtsSeconds = ResolveFramePtsSeconds();
            av_frame_unref(m_decoder->frame);
            return DecodeResult::FrameReady;
        }

        if (receiveResult == AVERROR_EOF) {
            m_decoder->drainingDecoder = false;
            return DecodeResult::EndOfStream;
        }

        if (receiveResult != AVERROR(EAGAIN) && receiveResult != AVERROR_EOF) {
            SetLastError("Failed while decoding video frame: " + FfmpegErrorString(receiveResult));
            return DecodeResult::Error;
        }

        if (m_decoder->drainingDecoder) {
            m_decoder->drainingDecoder = false;
            return DecodeResult::EndOfStream;
        }

        if (!m_decoder->hasPendingPacket) {
            const int readResult = av_read_frame(m_decoder->formatContext, m_decoder->packet);
            if (readResult == AVERROR_EOF) {
                m_decoder->drainingDecoder = true;
                const int flushResult = avcodec_send_packet(m_decoder->codecContext, nullptr);
                if (flushResult < 0 && flushResult != AVERROR_EOF) {
                    SetLastError("Failed to flush video decoder: " + FfmpegErrorString(flushResult));
                    return DecodeResult::Error;
                }
                continue;
            }
            if (readResult < 0) {
                SetLastError("Failed to read video packet: " + FfmpegErrorString(readResult));
                return DecodeResult::Error;
            }

            if (m_decoder->packet->stream_index != m_decoder->videoStreamIndex) {
                av_packet_unref(m_decoder->packet);
                continue;
            }

            m_decoder->hasPendingPacket = true;
        }

        const int sendResult = avcodec_send_packet(m_decoder->codecContext, m_decoder->packet);
        if (sendResult == 0) {
            av_packet_unref(m_decoder->packet);
            m_decoder->hasPendingPacket = false;
            continue;
        }
        if (sendResult == AVERROR(EAGAIN)) {
            continue;
        }
        av_packet_unref(m_decoder->packet);
        m_decoder->hasPendingPacket = false;
        if (sendResult < 0) {
            SetLastError("Failed to submit packet to decoder: " + FfmpegErrorString(sendResult));
            return DecodeResult::Error;
        }
    }
}

double VideoPlayer::ResolveFramePtsSeconds() const {
    if (m_decoder == nullptr || m_decoder->frame == nullptr) {
        return 0.0;
    }

    const AVFrame* frame = m_decoder->frame;
    int64_t timestamp = frame->best_effort_timestamp;
    if (timestamp == AV_NOPTS_VALUE) {
        timestamp = frame->pts;
    }

    if (timestamp != AV_NOPTS_VALUE) {
        const double seconds = static_cast<double>(timestamp) * RationalToSeconds(m_decoder->timeBase);
        m_decoder->nextFallbackPtsSeconds = seconds + m_decoder->fallbackFrameDurationSeconds;
        return seconds;
    }

    const double fallback = m_decoder->nextFallbackPtsSeconds;
    m_decoder->nextFallbackPtsSeconds += m_decoder->fallbackFrameDurationSeconds;
    return fallback;
}

void VideoPlayer::WorkerMain() {
    for (;;) {
        size_t writeIndex = 0;
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_queueCv.wait(lock, [&]() {
                return m_workerExitRequested ||
                       m_requestSeekToStart ||
                       m_decoderReachedEnd ||
                       m_queueCount < kFrameQueueCapacity;
            });

            if (m_workerExitRequested) {
                return;
            }

            if (m_requestSeekToStart) {
                m_requestSeekToStart = false;
                ClearQueuedFramesLocked();
                lock.unlock();
                if (!SeekToStart()) {
                    return;
                }
                lock.lock();
                m_decoderReachedEnd = false;
                continue;
            }

            if (m_decoderReachedEnd) {
                if (!m_loop) {
                    m_queueCv.wait(lock, [&]() {
                        return m_workerExitRequested || m_requestSeekToStart || m_loop;
                    });
                    continue;
                }

                ClearQueuedFramesLocked();
                lock.unlock();
                if (!SeekToStart()) {
                    return;
                }
                lock.lock();
                m_decoderReachedEnd = false;
                continue;
            }

            if (m_queueCount >= kFrameQueueCapacity) {
                continue;
            }

            writeIndex = (m_queueReadIndex + m_queueCount) % kFrameQueueCapacity;
        }

        double ptsSeconds = 0.0;
        const DecodeResult decodeResult = DecodeIntoSlot(m_frameQueue[writeIndex], ptsSeconds);

        std::lock_guard<std::mutex> lock(m_queueMutex);
        if (m_workerExitRequested) {
            return;
        }

        if (decodeResult == DecodeResult::FrameReady) {
            m_frameQueue[writeIndex].ptsSeconds = ptsSeconds;
            ++m_queueCount;
            m_queueCv.notify_all();
            continue;
        }

        if (decodeResult == DecodeResult::EndOfStream) {
            m_decoderReachedEnd = true;
            m_queueCv.notify_all();
            continue;
        }

        m_decoderReachedEnd = true;
        std::cerr << "VideoPlayer decode error";
        if (!m_loadedPath.empty()) {
            std::cerr << " for '" << m_loadedPath << "'";
        }
        const std::string lastError = GetLastError();
        if (!lastError.empty()) {
            std::cerr << ": " << lastError;
        }
        std::cerr << std::endl;
    }
}
