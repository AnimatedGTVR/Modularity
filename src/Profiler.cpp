#include "Profiler.h"

namespace {
using ProfilerClock = std::chrono::steady_clock;
}

Profiler& Profiler::instance() {
    static Profiler profiler;
    return profiler;
}

void Profiler::setRecording(bool enabled) {
    recording = enabled;
    if (!recording && frameActive) {
        frameActive = false;
        currentFrame = {};
        sampleStack.clear();
    }
}

void Profiler::clearHistory() {
    historyWriteIndex = 0;
    historyCount = 0;
    latestFrameId = 0;
    currentFrame = {};
    sampleStack.clear();
}

void Profiler::beginFrame(uint64_t frameId) {
    resolvePendingGpuQueries();

    frameActive = recording;
    currentFrame = {};
    sampleStack.clear();
    latestFrameId = std::max(latestFrameId, frameId);

    if (!frameActive) {
        return;
    }

    currentFrame.frameId = frameId;
    currentFrame.samples.reserve(64);
    currentFrame.samples.push_back({
        "Frame",
        ProfilerSampleCategory::Frame,
        -1,
        0,
        1,
        0.0,
        0.0,
        0,
        0,
        false
    });
    sampleStack.push_back({ 0, ProfilerClock::now() });
}

void Profiler::endFrame() {
    resolvePendingGpuQueries();

    if (!frameActive) {
        return;
    }

    while (sampleStack.size() > 1) {
        endSample();
    }

    const auto now = ProfilerClock::now();
    if (!sampleStack.empty()) {
        ActiveSample root = sampleStack.back();
        sampleStack.pop_back();
        currentFrame.samples[root.sampleIndex].cpuMs =
            std::chrono::duration<double, std::milli>(now - root.startTime).count();
        currentFrame.cpuMs = currentFrame.samples[root.sampleIndex].cpuMs;
    }

    currentFrame.renderMs = 0.0;
    for (const ProfilerSampleRecord& sample : currentFrame.samples) {
        if (sample.category == ProfilerSampleCategory::Render) {
            currentFrame.renderMs += sample.cpuMs;
        }
    }

    historyFrames[historyWriteIndex] = std::move(currentFrame);
    historyWriteIndex = (historyWriteIndex + 1) % kMaxHistoryFrames;
    historyCount = std::min(historyCount + 1, kMaxHistoryFrames);
    frameActive = false;
}

bool Profiler::beginSample(std::string_view name, ProfilerSampleCategory category) {
    if (!frameActive || sampleStack.empty()) {
        return false;
    }

    const int parentIndex = sampleStack.back().sampleIndex;
    const int depth = currentFrame.samples[parentIndex].depth + 1;
    const int sampleIndex = static_cast<int>(currentFrame.samples.size());
    currentFrame.samples.push_back({
        std::string(name),
        category,
        parentIndex,
        depth,
        1,
        0.0,
        0.0,
        0,
        0,
        false
    });
    sampleStack.push_back({ sampleIndex, ProfilerClock::now() });
    return true;
}

void Profiler::endSample() {
    if (!frameActive || sampleStack.size() <= 1) {
        return;
    }

    const auto now = ProfilerClock::now();
    ActiveSample active = sampleStack.back();
    sampleStack.pop_back();
    currentFrame.samples[active.sampleIndex].cpuMs =
        std::chrono::duration<double, std::milli>(now - active.startTime).count();
}

void Profiler::addSyntheticSample(std::string_view name,
                                  ProfilerSampleCategory category,
                                  double cpuMs,
                                  uint32_t callCount,
                                  int parentIndex,
                                  int64_t memoryDeltaBytes,
                                  uint64_t memoryTotalBytes) {
    if (!frameActive || parentIndex < 0 || parentIndex >= static_cast<int>(currentFrame.samples.size())) {
        return;
    }

    const int depth = currentFrame.samples[parentIndex].depth + 1;
    currentFrame.samples.push_back({
        std::string(name),
        category,
        parentIndex,
        depth,
        std::max<uint32_t>(1, callCount),
        cpuMs,
        0.0,
        memoryDeltaBytes,
        memoryTotalBytes,
        true
    });
}

void Profiler::setCurrentSampleMemory(int64_t deltaBytes, uint64_t totalBytes) {
    if (!frameActive || sampleStack.empty()) {
        return;
    }
    ProfilerSampleRecord& sample = currentFrame.samples[sampleStack.back().sampleIndex];
    sample.memoryDeltaBytes = deltaBytes;
    sample.memoryTotalBytes = totalBytes;
}

void Profiler::setCurrentFrameGcMetrics(double gcMs,
                                        int64_t heapDeltaBytes,
                                        uint64_t heapUsedBytes,
                                        uint64_t heapReservedBytes,
                                        const std::array<uint32_t, 3>& collectionCounts,
                                        const std::array<uint32_t, 3>& collectionDelta) {
    if (!frameActive) {
        return;
    }
    currentFrame.gcMs = gcMs;
    currentFrame.managedHeapDeltaBytes = heapDeltaBytes;
    currentFrame.managedHeapUsedBytes = heapUsedBytes;
    currentFrame.managedHeapReservedBytes = heapReservedBytes;
    currentFrame.gcCollections = collectionCounts;
    currentFrame.gcCollectionDelta = collectionDelta;
    uint32_t totalCollections = 0;
    for (uint32_t delta : collectionDelta) {
        totalCollections += delta;
    }
    currentFrame.gcTriggered = (totalCollections > 0) || (gcMs > 0.0001);
}

void Profiler::setCurrentFrameRenderCounters(uint64_t drawCalls, uint64_t textureBinds, uint64_t stateBinds) {
    if (!frameActive) {
        return;
    }
    currentFrame.drawCalls = drawCalls;
    currentFrame.textureBinds = textureBinds;
    currentFrame.stateBinds = stateBinds;
}

void Profiler::setCurrentFrameRenderMemory(uint64_t usedBytes, uint64_t budgetBytes) {
    if (!frameActive) {
        return;
    }
    currentFrame.renderMemoryBytes = usedBytes;
    currentFrame.renderMemoryBudgetBytes = budgetBytes;
}

void Profiler::setCurrentFrameGpuCapability(bool supported, bool partial) {
    if (!frameActive) {
        return;
    }
    currentFrame.gpuTimingSupported = supported;
    currentFrame.gpuTimingPartial = partial;
}

bool Profiler::supportsOpenGlGpuTimers() const {
#if MODULARITY_OPENGL_ES
    return false;
#elif defined(GL_TIME_ELAPSED)
    return (GLAD_GL_VERSION_3_3 != 0);
#else
    return false;
#endif
}

bool Profiler::beginOpenGlGpuFrame() {
    resolvePendingGpuQueries();
    if (!frameActive || activeGpuQuery != 0 || !supportsOpenGlGpuTimers()) {
        return false;
    }

    activeGpuQuery = acquireGpuQuery();
    if (activeGpuQuery == 0) {
        return false;
    }

    activeGpuQueryFrameId = currentFrame.frameId;
    activeGpuQueryPartial = true;
    currentFrame.gpuTimingSupported = true;
    currentFrame.gpuTimingPartial = true;
#if !MODULARITY_OPENGL_ES
    glBeginQuery(GL_TIME_ELAPSED, activeGpuQuery);
#endif
    return true;
}

void Profiler::endOpenGlGpuFrame() {
    if (activeGpuQuery == 0) {
        return;
    }

#if !MODULARITY_OPENGL_ES
    glEndQuery(GL_TIME_ELAPSED);
#endif
    pendingGpuQueries.push_back({ activeGpuQuery, activeGpuQueryFrameId, activeGpuQueryPartial });
    activeGpuQuery = 0;
    activeGpuQueryFrameId = 0;
    activeGpuQueryPartial = false;
}

size_t Profiler::getHistoryCount() const {
    return historyCount;
}

const ProfilerFrameRecord* Profiler::getHistoryFrame(size_t orderedIndex) const {
    if (orderedIndex >= historyCount) {
        return nullptr;
    }
    const size_t oldestIndex = (historyWriteIndex + kMaxHistoryFrames - historyCount) % kMaxHistoryFrames;
    const size_t slot = (oldestIndex + orderedIndex) % kMaxHistoryFrames;
    return &historyFrames[slot];
}

const ProfilerFrameRecord* Profiler::getFrameById(uint64_t frameId) const {
    if (frameActive && currentFrame.frameId == frameId) {
        return &currentFrame;
    }
    for (size_t i = 0; i < historyCount; ++i) {
        const ProfilerFrameRecord* frame = getHistoryFrame(i);
        if (frame && frame->frameId == frameId) {
            return frame;
        }
    }
    return nullptr;
}

uint64_t Profiler::getLatestFrameId() const {
    if (historyCount > 0) {
        const size_t latestIndex = (historyWriteIndex + kMaxHistoryFrames - 1) % kMaxHistoryFrames;
        return historyFrames[latestIndex].frameId;
    }
    return latestFrameId;
}

void Profiler::resolvePendingGpuQueries() {
#if MODULARITY_OPENGL_ES
    while (!pendingGpuQueries.empty()) {
        releaseGpuQuery(pendingGpuQueries.front().query);
        pendingGpuQueries.pop_front();
    }
#else
    while (!pendingGpuQueries.empty()) {
        const PendingGpuQuery& pending = pendingGpuQueries.front();
        GLuint available = GL_FALSE;
        glGetQueryObjectuiv(pending.query, GL_QUERY_RESULT_AVAILABLE, &available);
        if (available == GL_FALSE) {
            break;
        }

        GLuint64 elapsedNs = 0;
        glGetQueryObjectui64v(pending.query, GL_QUERY_RESULT, &elapsedNs);
        attachGpuTimeToFrame(pending.frameId,
                             static_cast<double>(elapsedNs) / 1000000.0,
                             pending.partial);
        releaseGpuQuery(pending.query);
        pendingGpuQueries.pop_front();
    }
#endif
}

GLuint Profiler::acquireGpuQuery() {
    if (!gpuQueryPool.empty()) {
        GLuint query = gpuQueryPool.back();
        gpuQueryPool.pop_back();
        return query;
    }

    GLuint query = 0;
    glGenQueries(1, &query);
    return query;
}

void Profiler::releaseGpuQuery(GLuint query) {
    if (query != 0) {
        gpuQueryPool.push_back(query);
    }
}

ProfilerFrameRecord* Profiler::findFrameMutable(uint64_t frameId) {
    if (frameActive && currentFrame.frameId == frameId) {
        return &currentFrame;
    }
    for (size_t i = 0; i < historyCount; ++i) {
        const size_t oldestIndex = (historyWriteIndex + kMaxHistoryFrames - historyCount) % kMaxHistoryFrames;
        const size_t slot = (oldestIndex + i) % kMaxHistoryFrames;
        if (historyFrames[slot].frameId == frameId) {
            return &historyFrames[slot];
        }
    }
    return nullptr;
}

void Profiler::attachGpuTimeToFrame(uint64_t frameId, double gpuMs, bool partial) {
    ProfilerFrameRecord* frame = findFrameMutable(frameId);
    if (!frame) {
        return;
    }

    frame->gpuMs = gpuMs;
    frame->gpuTimingSupported = true;
    frame->gpuTimingPartial = partial;

    for (ProfilerSampleRecord& sample : frame->samples) {
        if (sample.category == ProfilerSampleCategory::Render) {
            sample.gpuMs = gpuMs;
            break;
        }
    }
}
