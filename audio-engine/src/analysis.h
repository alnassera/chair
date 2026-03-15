#pragma once

#include "classifier.h"

#include <cstdint>
#include <vector>

struct AudioEvent {
    enum Direction {
        HARD_LEFT  = 0,
        LEFT       = 1,
        CENTER     = 2,
        RIGHT      = 3,
        HARD_RIGHT = 4
    };

    Direction  direction;
    SoundClass soundClass;
    float      confidence;    // 0..1  direction confidence
    float      energy_db;     // dBFS
    double     timestamp_ms;
    uint32_t   band;          // which frequency band fired
    float      freqLo;
    float      freqHi;

    // Spectral features (full-frame, for logging/tuning)
    float centroid;
    float bandwidth;

    // v0.2.6: continuous direction
    float      angle_deg;     // 0-180
    float      ild_db;        // raw ILD in dB
    bool       is_behind;
};

/// Per-band reading emitted every hop (continuous streaming)
struct BandReading {
    uint32_t band;
    float    angle_deg;   // 0-180
    float    energy_db;
    float    confidence;
};

class AudioAnalyzer {
public:
    AudioAnalyzer(uint32_t sampleRate, uint32_t fftSize = 1024, uint32_t hopSize = 512);
    ~AudioAnalyzer();

    AudioAnalyzer(const AudioAnalyzer&)            = delete;
    AudioAnalyzer& operator=(const AudioAnalyzer&) = delete;

    void pushSamples(const float* interleavedStereo, uint32_t numFrames);

    /// Process buffered audio. Returns onset events (for terminal/logging).
    /// Also populates latest band readings accessible via lastBandReadings().
    std::vector<AudioEvent> process();

    /// Per-band angle+energy from the most recent hop (continuous output).
    const std::vector<BandReading>& lastBandReadings() const { return m_lastReadings; }

    uint64_t framesProcessed() const { return m_framesProcessed; }

private:
    uint32_t m_sampleRate;
    uint32_t m_fftSize;
    uint32_t m_hopSize;

    // Ring buffer (interleaved stereo)
    std::vector<float> m_ring;
    uint32_t m_ringCapacity = 0;
    uint32_t m_writePos     = 0;
    uint32_t m_available    = 0;

    // FFT
    void* m_fftCfg = nullptr;
    std::vector<float> m_window;

    // Per-frame working buffers
    std::vector<float> m_frameL;
    std::vector<float> m_frameR;
    std::vector<float> m_magL;
    std::vector<float> m_magR;

    // Frequency bands
    struct Band {
        uint32_t binLo, binHi;
        float    freqLo, freqHi;
        float    fluxBaseline;
        uint32_t debounceCount;
        std::vector<float> prevMag;
    };
    std::vector<Band> m_bands;

    uint32_t m_debounceMax;
    uint64_t m_framesProcessed = 0;

    // Continuous band readings (latest hop)
    std::vector<BandReading> m_lastReadings;

    void initBands();
    float bandFlux(Band& band);
    AudioEvent::Direction estimateDirectionForBand(const Band& band, float& confidence,
                                                    float& angleDeg, float& ildDb);
    SpectralFeatures computeFrameFeatures(float attackRatio);

    // Deduplication: merge events within angular proximity
    std::vector<AudioEvent> deduplicateEvents(std::vector<AudioEvent>& raw);
};
