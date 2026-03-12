#include "clip_logger.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#endif

/// Recursively create directories (like mkdir -p).
static void mkdirs(const std::string& path) {
#ifdef _WIN32
    // Walk the path and create each component
    for (size_t i = 0; i < path.size(); ++i) {
        if (path[i] == '/' || path[i] == '\\') {
            std::string sub = path.substr(0, i);
            if (!sub.empty() && sub.back() != ':')
                CreateDirectoryA(sub.c_str(), nullptr);
        }
    }
    CreateDirectoryA(path.c_str(), nullptr);
#else
    // Fallback: just use system mkdir -p
    std::string cmd = "mkdir -p \"" + path + "\"";
    system(cmd.c_str());
#endif
}

// ---------------------------------------------------------------------------
// WAV file writer (16-bit PCM)
// ---------------------------------------------------------------------------

#pragma pack(push, 1)
struct WavHeader {
    char     riff[4]       = {'R','I','F','F'};
    uint32_t fileSize      = 0;
    char     wave[4]       = {'W','A','V','E'};
    char     fmt[4]        = {'f','m','t',' '};
    uint32_t fmtSize       = 16;
    uint16_t audioFormat   = 1; // PCM
    uint16_t channels      = 2;
    uint32_t sampleRate    = 48000;
    uint32_t byteRate      = 0;
    uint16_t blockAlign    = 0;
    uint16_t bitsPerSample = 16;
    char     data[4]       = {'d','a','t','a'};
    uint32_t dataSize      = 0;
};
#pragma pack(pop)

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ClipLogger::ClipLogger(uint32_t sampleRate, const std::string& sessionDir, Config cfg)
    : m_sampleRate(sampleRate)
    , m_sessionDir(sessionDir)
    , m_cfg(cfg)
{
    // Rolling buffer: 10 seconds stereo
    m_ringCapacity = sampleRate * 10;
    m_ring.resize(m_ringCapacity * 2, 0.0f);

    // Create session directory (and parents)
    mkdirs(m_sessionDir);

    // Open metadata log
    std::string logPath = m_sessionDir + "/events.jsonl";
    m_logFile.open(logPath, std::ios::out | std::ios::trunc);
    if (!m_logFile.is_open()) {
        fprintf(stderr, "[clip] Failed to open log: %s\n", logPath.c_str());
    } else {
        printf("[clip] Logging to: %s\n", logPath.c_str());
    }
}

ClipLogger::~ClipLogger() {
    flush();
    if (m_logFile.is_open()) m_logFile.close();
}

// ---------------------------------------------------------------------------
// Audio input
// ---------------------------------------------------------------------------

void ClipLogger::pushAudio(const float* interleavedStereo, uint32_t numFrames) {
    for (uint32_t i = 0; i < numFrames; ++i) {
        uint32_t pos = (m_ringWritePos + i) % m_ringCapacity;
        m_ring[pos * 2]     = interleavedStereo[i * 2];
        m_ring[pos * 2 + 1] = interleavedStereo[i * 2 + 1];
    }
    m_ringWritePos = (m_ringWritePos + numFrames) % m_ringCapacity;
    m_totalFrames += numFrames;
}

// ---------------------------------------------------------------------------
// Event marking
// ---------------------------------------------------------------------------

void ClipLogger::markEvent(const AudioEvent& event) {
    if (!m_pending.active) {
        // Start a new clip
        m_pending.active = true;
        m_pending.firstEventFrame = m_totalFrames;
        m_pending.lastEventFrame  = m_totalFrames;
        m_pending.events.clear();
        m_pending.events.push_back(event);
    } else {
        // Extend existing clip
        m_pending.lastEventFrame = m_totalFrames;
        m_pending.events.push_back(event);

        // Check max duration cap
        uint64_t preFrames = static_cast<uint64_t>(m_cfg.preContextSec * m_sampleRate);
        uint64_t clipStart = m_pending.firstEventFrame > preFrames
            ? m_pending.firstEventFrame - preFrames : 0;
        uint64_t maxFrames = static_cast<uint64_t>(m_cfg.maxClipSec * m_sampleRate);

        if (m_totalFrames - clipStart > maxFrames) {
            finalizeClip();
        }
    }
}

// ---------------------------------------------------------------------------
// Tick — check if pending clip's post-context has elapsed
// ---------------------------------------------------------------------------

void ClipLogger::tick() {
    if (!m_pending.active) return;

    uint64_t postFrames = static_cast<uint64_t>(m_cfg.postContextSec * m_sampleRate);
    if (m_totalFrames >= m_pending.lastEventFrame + postFrames) {
        finalizeClip();
    }
}

void ClipLogger::flush() {
    if (m_pending.active) {
        finalizeClip();
    }
}

// ---------------------------------------------------------------------------
// Finalize: extract clip from ring buffer, write WAV + metadata
// ---------------------------------------------------------------------------

void ClipLogger::finalizeClip() {
    if (!m_pending.active) return;
    m_pending.active = false;

    uint64_t preFrames  = static_cast<uint64_t>(m_cfg.preContextSec * m_sampleRate);
    uint64_t postFrames = static_cast<uint64_t>(m_cfg.postContextSec * m_sampleRate);

    uint64_t clipStart = m_pending.firstEventFrame > preFrames
        ? m_pending.firstEventFrame - preFrames : 0;
    uint64_t clipEnd = m_pending.lastEventFrame + postFrames;

    // Clamp to available data
    if (clipStart + m_ringCapacity < m_totalFrames)
        clipStart = m_totalFrames - m_ringCapacity;
    if (clipEnd > m_totalFrames)
        clipEnd = m_totalFrames;

    uint32_t clipFrames = static_cast<uint32_t>(clipEnd - clipStart);
    if (clipFrames == 0) return;

    // Extract audio from ring buffer
    std::vector<float> clipData(clipFrames * 2);
    copyFromRing(clipData.data(), clipStart, clipFrames);

    // Write WAV
    m_clipIndex++;
    char filename[64];
    snprintf(filename, sizeof(filename), "clip_%04u.wav", m_clipIndex);
    std::string wavPath = m_sessionDir + "/" + filename;
    writeWav(wavPath, clipData.data(), clipFrames);

    // Write metadata to JSONL
    if (m_logFile.is_open()) {
        // Compute event times relative to clip start
        double clipStartMs = static_cast<double>(clipStart) * 1000.0 / m_sampleRate;

        m_logFile << "{\"clip\":\"" << filename
                  << "\",\"frames\":" << clipFrames
                  << ",\"sample_rate\":" << m_sampleRate
                  << ",\"events\":[";

        for (size_t i = 0; i < m_pending.events.size(); ++i) {
            const auto& e = m_pending.events[i];
            if (i > 0) m_logFile << ",";

            const char* dirName;
            switch (e.direction) {
                case AudioEvent::HARD_LEFT:  dirName = "hard_left"; break;
                case AudioEvent::LEFT:       dirName = "left"; break;
                case AudioEvent::CENTER:     dirName = "center"; break;
                case AudioEvent::RIGHT:      dirName = "right"; break;
                case AudioEvent::HARD_RIGHT: dirName = "hard_right"; break;
                default: dirName = "unknown";
            }

            char buf[256];
            snprintf(buf, sizeof(buf),
                "{\"dir\":\"%s\",\"conf\":%.2f,\"db\":%.1f,\"t\":%.0f,"
                "\"t_in_clip\":%.0f,\"band\":%u,\"freqLo\":%.0f,\"freqHi\":%.0f}",
                dirName, e.confidence, e.energy_db, e.timestamp_ms,
                e.timestamp_ms - clipStartMs,
                e.band, e.freqLo, e.freqHi);
            m_logFile << buf;
        }

        m_logFile << "]}" << std::endl;
    }

    printf("[clip] Saved %s (%u frames, %zu events)\n",
           filename, clipFrames, m_pending.events.size());
}

// ---------------------------------------------------------------------------
// Copy from ring buffer at a global frame position
// ---------------------------------------------------------------------------

void ClipLogger::copyFromRing(float* dst, uint64_t startFrame, uint32_t numFrames) {
    for (uint32_t i = 0; i < numFrames; ++i) {
        uint64_t globalFrame = startFrame + i;
        uint32_t ringPos = static_cast<uint32_t>(globalFrame % m_ringCapacity);
        dst[i * 2]     = m_ring[ringPos * 2];
        dst[i * 2 + 1] = m_ring[ringPos * 2 + 1];
    }
}

// ---------------------------------------------------------------------------
// WAV writer (float → 16-bit PCM)
// ---------------------------------------------------------------------------

void ClipLogger::writeWav(const std::string& path, const float* data, uint32_t numFrames) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) {
        fprintf(stderr, "[clip] Failed to write: %s\n", path.c_str());
        return;
    }

    uint32_t numSamples = numFrames * 2; // stereo
    uint32_t dataSize = numSamples * sizeof(int16_t);

    WavHeader hdr;
    hdr.channels      = 2;
    hdr.sampleRate    = m_sampleRate;
    hdr.bitsPerSample = 16;
    hdr.blockAlign    = 4; // 2 channels * 2 bytes
    hdr.byteRate      = m_sampleRate * hdr.blockAlign;
    hdr.dataSize      = dataSize;
    hdr.fileSize      = sizeof(WavHeader) - 8 + dataSize;

    fwrite(&hdr, sizeof(WavHeader), 1, f);

    // Convert float → int16 in small chunks
    const uint32_t chunkSize = 4096;
    int16_t buf[chunkSize];
    uint32_t remaining = numSamples;
    const float* src = data;

    while (remaining > 0) {
        uint32_t n = std::min(remaining, chunkSize);
        for (uint32_t i = 0; i < n; ++i) {
            float s = src[i];
            if (s >  1.0f) s =  1.0f;
            if (s < -1.0f) s = -1.0f;
            buf[i] = static_cast<int16_t>(s * 32767.0f);
        }
        fwrite(buf, sizeof(int16_t), n, f);
        src       += n;
        remaining -= n;
    }

    fclose(f);
}
