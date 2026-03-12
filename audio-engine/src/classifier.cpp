#include "classifier.h"

#include <cmath>
#include <algorithm>

const char* soundClassName(SoundClass c) {
    switch (c) {
        case SoundClass::FOOTSTEPS:  return "footsteps";
        case SoundClass::GUNFIRE:    return "gunfire";
        case SoundClass::RELOAD:     return "reload";
        case SoundClass::ABILITY:    return "ability";
        case SoundClass::GLOBAL_CUE: return "global";
        default:                     return "unknown";
    }
}

const char* soundClassShort(SoundClass c) {
    switch (c) {
        case SoundClass::FOOTSTEPS:  return "STEP";
        case SoundClass::GUNFIRE:    return "FIRE";
        case SoundClass::RELOAD:     return "RELD";
        case SoundClass::ABILITY:    return "UTIL";
        case SoundClass::GLOBAL_CUE: return "CUE ";
        default:                     return "SND ";
    }
}

// ---------------------------------------------------------------------------
// Feature extraction
// ---------------------------------------------------------------------------

SpectralFeatures computeFeatures(
    const float* magL, const float* magR,
    uint32_t specSize, uint32_t sampleRate, uint32_t fftSize,
    const uint32_t bandBounds[][2], uint32_t numBands,
    float attackRatio)
{
    SpectralFeatures f{};
    f.attackRatio = attackRatio;

    float binHz = static_cast<float>(sampleRate) / fftSize;

    // Compute total magnitude per bin and overall stats
    float totalEnergy = 0.0f;
    float weightedFreqSum = 0.0f;

    for (uint32_t i = 1; i < specSize; ++i) {  // skip DC
        float mag = magL[i] + magR[i];
        float energy = mag * mag;
        float freq = i * binHz;

        totalEnergy += energy;
        weightedFreqSum += freq * energy;
    }

    if (totalEnergy < 1e-10f) return f;

    // Spectral centroid
    f.centroid = weightedFreqSum / totalEnergy;

    // Spectral bandwidth (std dev around centroid)
    float varSum = 0.0f;
    for (uint32_t i = 1; i < specSize; ++i) {
        float mag = magL[i] + magR[i];
        float energy = mag * mag;
        float freq = i * binHz;
        float diff = freq - f.centroid;
        varSum += diff * diff * energy;
    }
    f.bandwidth = sqrtf(varSum / totalEnergy);

    // Per-band energy (normalized)
    float bandEnergyRaw[8] = {};
    float bandTotal = 0.0f;

    for (uint32_t b = 0; b < numBands && b < 8; ++b) {
        uint32_t lo = bandBounds[b][0];
        uint32_t hi = bandBounds[b][1];
        for (uint32_t i = lo; i < hi && i < specSize; ++i) {
            float mag = magL[i] + magR[i];
            bandEnergyRaw[b] += mag * mag;
        }
        bandTotal += bandEnergyRaw[b];
    }

    if (bandTotal > 1e-10f) {
        float maxE = 0.0f;
        for (uint32_t b = 0; b < 8; ++b) {
            f.bandEnergy[b] = bandEnergyRaw[b] / bandTotal;
            if (bandEnergyRaw[b] > maxE) {
                maxE = bandEnergyRaw[b];
                f.peakBand = b;
            }
        }
    }

    // Region ratios
    f.lowRatio  = f.bandEnergy[0] + f.bandEnergy[1];
    f.midRatio  = f.bandEnergy[2] + f.bandEnergy[3] + f.bandEnergy[4];
    f.highRatio = f.bandEnergy[5] + f.bandEnergy[6] + f.bandEnergy[7];

    return f;
}

// ---------------------------------------------------------------------------
// Heuristic classifier
// ---------------------------------------------------------------------------

SoundClass classify(const SpectralFeatures& f) {
    // Score each class, highest wins.
    // Scores are simple additive — easy to tune.

    float scores[6] = {}; // indexed by SoundClass

    // --- FOOTSTEPS ---
    // Low-frequency dominant, narrow bandwidth, moderate attack
    if (f.lowRatio > 0.50f) scores[1] += 1.5f;
    if (f.lowRatio > 0.65f) scores[1] += 1.0f;
    if (f.centroid < 500.0f) scores[1] += 1.0f;
    if (f.centroid < 350.0f) scores[1] += 0.5f;
    if (f.bandwidth < 800.0f) scores[1] += 0.5f;
    if (f.peakBand <= 1) scores[1] += 0.5f;

    // --- GUNFIRE ---
    // Broadband, sharp attack, energy spread across many bands
    if (f.bandwidth > 2000.0f) scores[2] += 1.0f;
    if (f.bandwidth > 3000.0f) scores[2] += 1.0f;
    if (f.attackRatio > 4.0f) scores[2] += 1.5f;
    if (f.attackRatio > 8.0f) scores[2] += 1.0f;
    if (f.midRatio > 0.35f && f.lowRatio < 0.40f) scores[2] += 0.5f;
    if (f.centroid > 1500.0f) scores[2] += 0.5f;
    // Gunfire has energy across many bands (low + mid + some high)
    if (f.lowRatio > 0.1f && f.midRatio > 0.3f && f.highRatio > 0.05f) scores[2] += 0.5f;

    // --- RELOAD ---
    // Mid-range, moderate bandwidth, metallic (centered 1-3 kHz)
    if (f.centroid > 1000.0f && f.centroid < 3000.0f) scores[3] += 0.5f;
    if (f.bandwidth > 500.0f && f.bandwidth < 2000.0f) scores[3] += 0.5f;
    if (f.midRatio > 0.5f) scores[3] += 1.0f;
    if (f.peakBand >= 2 && f.peakBand <= 4) scores[3] += 0.5f;
    if (f.attackRatio > 2.0f && f.attackRatio < 6.0f) scores[3] += 0.5f;

    // ABILITY scoring removed — too noisy, catches ambient/map sounds.
    // Anything that would match goes to UNKNOWN (silenced in overlay).

    // --- GLOBAL CUE ---
    // Tonal, high-frequency content, gentle attack (round start, spike plant)
    if (f.highRatio > 0.25f) scores[5] += 1.0f;
    if (f.highRatio > 0.40f) scores[5] += 0.5f;
    if (f.attackRatio < 2.5f) scores[5] += 0.5f;
    if (f.centroid > 3000.0f) scores[5] += 0.5f;
    if (f.bandwidth < 2000.0f) scores[5] += 0.5f;  // tonal = narrower

    // Find best class
    float bestScore = 1.5f; // minimum threshold — below this, stay UNKNOWN
    SoundClass best = SoundClass::UNKNOWN;

    for (int c = 1; c <= 5; ++c) {
        if (scores[c] > bestScore) {
            bestScore = scores[c];
            best = static_cast<SoundClass>(c);
        }
    }

    return best;
}
