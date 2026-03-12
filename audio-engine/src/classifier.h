#pragma once

#include <cstdint>

enum class SoundClass : uint8_t {
    UNKNOWN    = 0,
    FOOTSTEPS  = 1,
    GUNFIRE    = 2,
    RELOAD     = 3,
    ABILITY    = 4,
    GLOBAL_CUE = 5,
};

const char* soundClassName(SoundClass c);
const char* soundClassShort(SoundClass c);   // 4-5 char for overlay

struct SpectralFeatures {
    float centroid;        // Hz — brightness
    float bandwidth;       // Hz — spectral spread
    float bandEnergy[8];   // normalized per-band energy (sums to ~1)
    uint32_t peakBand;     // band with highest energy
    float lowRatio;        // bands 0-1 energy / total
    float midRatio;        // bands 2-4 energy / total
    float highRatio;       // bands 5-7 energy / total
    float attackRatio;     // flux / threshold — onset sharpness
};

/// Compute spectral features from per-band magnitude arrays.
/// magL, magR: FFT magnitudes per bin.  bands: 8 pairs of (binLo, binHi).
SpectralFeatures computeFeatures(
    const float* magL, const float* magR,
    uint32_t specSize, uint32_t sampleRate, uint32_t fftSize,
    const uint32_t bandBounds[][2], uint32_t numBands,
    float attackRatio);

/// Heuristic classifier.
SoundClass classify(const SpectralFeatures& f);
