#pragma once

#include "analysis.h"

#include <cstdint>
#include <string>
#include <vector>
#include <fstream>

/// Records event-triggered audio clips and metadata for offline analysis.
/// Feed it raw audio continuously; mark events; call tick() to finalize clips.
struct ClipLoggerConfig {
    float preContextSec  = 1.0f;
    float postContextSec = 0.5f;
    float maxClipSec     = 5.0f;
};

class ClipLogger {
public:
    using Config = ClipLoggerConfig;

    ClipLogger(uint32_t sampleRate, const std::string& sessionDir, Config cfg = {});
    ~ClipLogger();

    ClipLogger(const ClipLogger&)            = delete;
    ClipLogger& operator=(const ClipLogger&) = delete;

    /// Feed raw interleaved stereo audio (called every capture cycle).
    void pushAudio(const float* interleavedStereo, uint32_t numFrames);

    /// Mark an event for clip capture.
    void markEvent(const AudioEvent& event);

    /// Check if any pending clips are ready to finalize. Call once per frame.
    void tick();

    /// Force-finalize any open clip (call on shutdown).
    void flush();

    uint32_t clipCount() const { return m_clipIndex; }
    const std::string& sessionDir() const { return m_sessionDir; }

private:
    uint32_t    m_sampleRate;
    std::string m_sessionDir;
    Config      m_cfg;

    // Rolling buffer: 10 seconds of raw stereo float
    std::vector<float> m_ring;
    uint32_t m_ringCapacity = 0;  // in frames
    uint32_t m_ringWritePos = 0;  // in frames
    uint64_t m_totalFrames  = 0;  // total frames ever pushed (monotonic clock)

    // Pending clip
    struct PendingClip {
        bool     active = false;
        uint64_t firstEventFrame = 0;  // global frame of first event
        uint64_t lastEventFrame  = 0;  // global frame of most recent event
        std::vector<AudioEvent> events;
    };
    PendingClip m_pending;

    uint32_t     m_clipIndex = 0;
    std::ofstream m_logFile;

    void finalizeClip();
    void writeWav(const std::string& path, const float* data, uint32_t numFrames);
    void copyFromRing(float* dst, uint64_t startFrame, uint32_t numFrames);
};
