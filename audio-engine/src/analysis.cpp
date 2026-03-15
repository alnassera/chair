#include "analysis.h"

#include <kiss_fftr.h>

#include <cmath>
#include <algorithm>

static constexpr float PI = 3.14159265358979323846f;

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

AudioAnalyzer::AudioAnalyzer(uint32_t sampleRate, uint32_t fftSize, uint32_t hopSize)
    : m_sampleRate(sampleRate)
    , m_fftSize(fftSize)
    , m_hopSize(hopSize)
{
    m_ringCapacity = sampleRate;
    m_ring.resize(m_ringCapacity * 2, 0.0f);

    m_fftCfg = kiss_fftr_alloc(fftSize, /*inverse=*/0, nullptr, nullptr);

    m_window.resize(fftSize);
    for (uint32_t i = 0; i < fftSize; ++i)
        m_window[i] = 0.5f * (1.0f - cosf(2.0f * PI * i / (fftSize - 1)));

    uint32_t specSize = fftSize / 2 + 1;
    m_frameL.resize(fftSize);
    m_frameR.resize(fftSize);
    m_magL.resize(specSize);
    m_magR.resize(specSize);

    m_debounceMax = static_cast<uint32_t>(0.15f * sampleRate / hopSize);

    initBands();
}

AudioAnalyzer::~AudioAnalyzer() {
    if (m_fftCfg) kiss_fftr_free(static_cast<kiss_fftr_cfg>(m_fftCfg));
}

// ---------------------------------------------------------------------------
// Frequency bands
// ---------------------------------------------------------------------------

void AudioAnalyzer::initBands() {
    const float edges[] = {100, 250, 500, 1000, 2000, 4000, 8000, 12000, 20000};
    const float binHz = static_cast<float>(m_sampleRate) / m_fftSize;
    const uint32_t maxBin = m_fftSize / 2;

    m_bands.resize(8);
    for (int i = 0; i < 8; ++i) {
        uint32_t lo = static_cast<uint32_t>(edges[i] / binHz);
        uint32_t hi = static_cast<uint32_t>(edges[i + 1] / binHz);
        if (hi > maxBin) hi = maxBin;
        if (lo >= hi)    lo = hi > 0 ? hi - 1 : 0;

        uint32_t bandBins = hi - lo;
        m_bands[i].binLo  = lo;
        m_bands[i].binHi  = hi;
        m_bands[i].freqLo = edges[i];
        m_bands[i].freqHi = edges[i + 1];
        m_bands[i].fluxBaseline  = 0.0f;
        m_bands[i].debounceCount = 0;
        m_bands[i].prevMag.resize(bandBins, 0.0f);
    }
}

// ---------------------------------------------------------------------------
// Input buffering
// ---------------------------------------------------------------------------

void AudioAnalyzer::pushSamples(const float* interleavedStereo, uint32_t numFrames) {
    for (uint32_t i = 0; i < numFrames; ++i) {
        uint32_t pos = (m_writePos + i) % m_ringCapacity;
        m_ring[pos * 2]     = interleavedStereo[i * 2];
        m_ring[pos * 2 + 1] = interleavedStereo[i * 2 + 1];
    }
    m_writePos = (m_writePos + numFrames) % m_ringCapacity;
    m_available += numFrames;
    if (m_available > m_ringCapacity) m_available = m_ringCapacity;
}

// ---------------------------------------------------------------------------
// Per-band spectral flux
// ---------------------------------------------------------------------------

float AudioAnalyzer::bandFlux(Band& band) {
    float flux = 0.0f;
    uint32_t bandBins = band.binHi - band.binLo;

    for (uint32_t i = 0; i < bandBins; ++i) {
        uint32_t bin = band.binLo + i;
        float magNow = m_magL[bin] + m_magR[bin];
        float diff   = magNow - band.prevMag[i];
        if (diff > 0.0f) flux += diff;
        band.prevMag[i] = magNow;
    }

    return flux;
}

// ---------------------------------------------------------------------------
// Direction estimation from a single band
// ---------------------------------------------------------------------------

AudioEvent::Direction AudioAnalyzer::estimateDirectionForBand(
        const Band& band, float& confidence, float& angleDeg, float& ildDb) {
    float eL = 0.0f, eR = 0.0f, cross = 0.0f;

    for (uint32_t bin = band.binLo; bin < band.binHi; ++bin) {
        eL    += m_magL[bin] * m_magL[bin];
        eR    += m_magR[bin] * m_magR[bin];
        cross += m_magL[bin] * m_magR[bin];
    }

    if (eL + eR < 1e-10f) {
        confidence = 0.0f;
        angleDeg = 90.0f;
        ildDb = 0.0f;
        return AudioEvent::CENTER;
    }

    float ild  = 10.0f * log10f((eL + 1e-10f) / (eR + 1e-10f));
    float corr = cross / (sqrtf(eL * eR) + 1e-10f);

    float ildMag = fabsf(ild);
    confidence = fminf(1.0f, ildMag / 6.0f) * fmaxf(0.0f, 1.0f - corr * 0.5f);

    ildDb = ild;

    // Continuous arctan mapping: ild -> left-right angle (0-180)
    static constexpr float ILD_SCALE = 6.0f;
    angleDeg = 90.0f + (90.0f * (2.0f / PI)) * atanf(ild / ILD_SCALE);

    if (angleDeg < 0.0f) angleDeg = 0.0f;
    if (angleDeg > 180.0f) angleDeg = 180.0f;

    // Derive Direction enum from angle for backward compat
    AudioEvent::Direction dir;
    if (confidence < 0.25f)      dir = AudioEvent::CENTER;
    else if (angleDeg >= 155.0f) dir = AudioEvent::HARD_LEFT;
    else if (angleDeg >= 115.0f) dir = AudioEvent::LEFT;
    else if (angleDeg >= 65.0f)  dir = AudioEvent::CENTER;
    else if (angleDeg >= 25.0f)  dir = AudioEvent::RIGHT;
    else                         dir = AudioEvent::HARD_RIGHT;

    return dir;
}

// ---------------------------------------------------------------------------
// Full-frame spectral features for classification
// ---------------------------------------------------------------------------

SpectralFeatures AudioAnalyzer::computeFrameFeatures(float attackRatio) {
    uint32_t bandBounds[8][2];
    for (uint32_t b = 0; b < m_bands.size() && b < 8; ++b) {
        bandBounds[b][0] = m_bands[b].binLo;
        bandBounds[b][1] = m_bands[b].binHi;
    }

    return ::computeFeatures(
        m_magL.data(), m_magR.data(),
        m_fftSize / 2 + 1, m_sampleRate, m_fftSize,
        bandBounds, 8,
        attackRatio);
}

// ---------------------------------------------------------------------------
// Deduplication: merge events within angular proximity
// ---------------------------------------------------------------------------

std::vector<AudioEvent> AudioAnalyzer::deduplicateEvents(std::vector<AudioEvent>& raw) {
    if (raw.empty()) return {};

    static constexpr float DEDUP_ANGLE = 15.0f;

    std::vector<AudioEvent> deduped;

    for (auto& evt : raw) {
        bool merged = false;
        for (auto& existing : deduped) {
            if (fabsf(static_cast<float>(evt.timestamp_ms - existing.timestamp_ms)) < 20.0f) {
                float diff = fabsf(evt.angle_deg - existing.angle_deg);
                float angDist = fminf(diff, 360.0f - diff);

                if (angDist < DEDUP_ANGLE) {
                    if (evt.energy_db > existing.energy_db) {
                        existing.energy_db = evt.energy_db;
                        existing.angle_deg = evt.angle_deg;
                        existing.ild_db    = evt.ild_db;
                        existing.direction = evt.direction;
                        existing.band      = evt.band;
                        existing.freqLo    = evt.freqLo;
                        existing.freqHi    = evt.freqHi;
                    }
                    if (evt.confidence > existing.confidence)
                        existing.confidence = evt.confidence;
                    merged = true;
                    break;
                }
            }
        }
        if (!merged) deduped.push_back(evt);
    }

    return deduped;
}

// ---------------------------------------------------------------------------
// Core analysis
// ---------------------------------------------------------------------------

std::vector<AudioEvent> AudioAnalyzer::process() {
    std::vector<AudioEvent> rawEvents;
    const uint32_t specSize = m_fftSize / 2 + 1;

    m_lastReadings.clear();

    while (m_available >= m_fftSize) {
        uint32_t readStart =
            (m_writePos + m_ringCapacity - m_available) % m_ringCapacity;

        for (uint32_t i = 0; i < m_fftSize; ++i) {
            uint32_t pos = (readStart + i) % m_ringCapacity;
            m_frameL[i] = m_ring[pos * 2]     * m_window[i];
            m_frameR[i] = m_ring[pos * 2 + 1] * m_window[i];
        }

        std::vector<kiss_fft_cpx> specL(specSize), specR(specSize);
        kiss_fftr(static_cast<kiss_fftr_cfg>(m_fftCfg), m_frameL.data(), specL.data());
        kiss_fftr(static_cast<kiss_fftr_cfg>(m_fftCfg), m_frameR.data(), specR.data());

        for (uint32_t i = 0; i < specSize; ++i) {
            m_magL[i] = sqrtf(specL[i].r * specL[i].r + specL[i].i * specL[i].i);
            m_magR[i] = sqrtf(specR[i].r * specR[i].r + specR[i].i * specR[i].i);
        }

        // Continuous band readings — computed every hop for all bands
        m_lastReadings.clear();
        for (uint32_t b = 0; b < m_bands.size(); ++b) {
            const Band& band = m_bands[b];

            float confidence, angleDeg, ildDb;
            estimateDirectionForBand(band, confidence, angleDeg, ildDb);

            float energy = 0.0f;
            for (uint32_t bin = band.binLo; bin < band.binHi; ++bin) {
                float m = m_magL[bin] + m_magR[bin];
                energy += m * m;
            }
            uint32_t bandBins = band.binHi - band.binLo;
            float energy_db = 10.0f * log10f(energy / bandBins + 1e-10f);

            BandReading rd{};
            rd.band       = b;
            rd.angle_deg  = angleDeg;
            rd.energy_db  = energy_db;
            rd.confidence = confidence;
            m_lastReadings.push_back(rd);
        }

        // Per-band onset detection (still used for terminal output / logging)
        for (uint32_t b = 0; b < m_bands.size(); ++b) {
            Band& band = m_bands[b];

            float flux = bandFlux(band);

            const float alpha = 0.02f;
            band.fluxBaseline = band.fluxBaseline * (1.0f - alpha) + flux * alpha;
            float threshold = band.fluxBaseline * 4.0f + 0.003f;

            if (band.debounceCount > 0) --band.debounceCount;

            if (flux > threshold && band.debounceCount == 0) {
                float confidence, angleDeg, ildDb;
                auto direction = estimateDirectionForBand(band, confidence, angleDeg, ildDb);

                float energy = 0.0f;
                for (uint32_t bin = band.binLo; bin < band.binHi; ++bin) {
                    float m = m_magL[bin] + m_magR[bin];
                    energy += m * m;
                }
                uint32_t bandBins = band.binHi - band.binLo;
                float energy_db = 10.0f * log10f(energy / bandBins + 1e-10f);

                AudioEvent evt{};
                evt.direction    = direction;
                evt.soundClass   = SoundClass::UNKNOWN;
                evt.confidence   = confidence;
                evt.energy_db    = energy_db;
                evt.timestamp_ms = static_cast<double>(m_framesProcessed) * 1000.0 / m_sampleRate;
                evt.band         = b;
                evt.freqLo       = band.freqLo;
                evt.freqHi       = band.freqHi;
                evt.centroid     = 0.0f;
                evt.bandwidth    = 0.0f;
                evt.angle_deg    = angleDeg;
                evt.ild_db       = ildDb;
                evt.is_behind    = false;

                rawEvents.push_back(evt);
                band.debounceCount = m_debounceMax;
            }
        }

        m_available -= m_hopSize;
        m_framesProcessed += m_hopSize;
    }

    return deduplicateEvents(rawEvents);
}
