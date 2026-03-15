#include "capture.h"
#include "analysis.h"
#include "classifier.h"
#include "ipc.h"
#include "clip_logger.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <cmath>
#include <atomic>
#include <chrono>
#include <ctime>
#include <vector>
#include <memory>

static std::atomic<bool> g_running{true};

// Energy gate: bands below this are not sent to overlay (filters ambient noise)
static constexpr float ENERGY_GATE_DB = -5.0f;     // soft gate center — full pass above, attenuated below
static constexpr float GATE_KNEE_DB  = 15.0f;     // soft knee width — fades over this range below gate
static constexpr float CONF_GATE = 0.0f;           // disabled — let everything through
static constexpr uint32_t MIN_BAND = 2;            // skip bands 0-1 (100Hz-500Hz) — ambient rumble

static BOOL WINAPI consoleHandler(DWORD event) {
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT || event == CTRL_CLOSE_EVENT) {
        g_running = false;
        return TRUE;
    }
    return FALSE;
}

static const char* directionGlyph(AudioEvent::Direction d) {
    switch (d) {
        case AudioEvent::HARD_LEFT:  return "<<";
        case AudioEvent::LEFT:       return "< ";
        case AudioEvent::CENTER:     return " *";
        case AudioEvent::RIGHT:      return " >";
        case AudioEvent::HARD_RIGHT: return ">>";
    }
    return " ?";
}

/// Cluster bands by angular proximity, return one JSON per cluster.
/// Supports multiple simultaneous sound sources.
static float CLUSTER_ANGLE = 30.0f;  // first pass: merge bands within this angle
static float MERGE_ANGLE   = 25.0f;  // second pass: collapse clusters that drifted close

struct Cluster {
    float weightSum = 0.0f;
    float angleSum  = 0.0f;
    float maxDb     = -999.0f;
    float maxConf   = 0.0f;
    float angle() const { return angleSum / weightSum; }
    void absorb(const Cluster& o) {
        angleSum  += o.angleSum;
        weightSum += o.weightSum;
        if (o.maxDb   > maxDb)   maxDb   = o.maxDb;
        if (o.maxConf > maxConf) maxConf = o.maxConf;
    }
};

// Center suppression state
static float centerGain = 1.0f;

static constexpr float OUTPUT_DEBOUNCE_MS = 100.0f;  // min time between outputs
static auto lastOutputTime = std::chrono::steady_clock::now();

static std::string aggregateReadingsToJson(const std::vector<BandReading>& readings) {
    // Temporal debounce: don't flood overlay with readings from one long sound
    auto now = std::chrono::steady_clock::now();
    float elapsedMs = std::chrono::duration<float, std::milli>(now - lastOutputTime).count();
    if (elapsedMs < OUTPUT_DEBOUNCE_MS) return {};

    // Gate and collect passing readings
    struct GatedReading { float angle, db, conf, weight; };
    std::vector<GatedReading> gated;

    for (const auto& r : readings) {
        if (r.band < MIN_BAND) continue;
        if (r.confidence < CONF_GATE && r.energy_db < ENERGY_GATE_DB) continue;

        float gain = 1.0f;
        if (r.energy_db < ENERGY_GATE_DB) {
            float below = ENERGY_GATE_DB - r.energy_db;
            gain = fmaxf(0.0f, 1.0f - (below / GATE_KNEE_DB));
            gain = gain * gain;
        }
        if (gain < 0.01f) continue;

        float gatedDb = r.energy_db + 20.0f * log10f(gain + 1e-10f);
        float weight = powf(10.0f, r.energy_db / 10.0f) * gain;
        gated.push_back({r.angle_deg, gatedDb, r.confidence * gain, weight});
    }

    if (gated.empty()) return {};

    // Require at least 2 bands — single band ILD is too noisy to trust
    if (gated.size() < 2) {
        printf("  [SKIP] bands=%zu (need 2+)\n", gated.size());
        return {};
    }

    // Compute weighted mean angle
    float wSum = 0.0f, wAngleSum = 0.0f;
    for (auto& g : gated) {
        wSum      += g.weight;
        wAngleSum += g.angle * g.weight;
    }
    float wMean = wAngleSum / wSum;

    // Check max deviation: if ANY band is far from the mean, it's noise
    static constexpr float MAX_DEV = 25.0f;
    float maxDev = 0.0f;
    for (auto& g : gated) {
        float d = fabsf(g.angle - wMean);
        if (d > maxDev) maxDev = d;
    }

    if (maxDev > MAX_DEV) {
        printf("  [SKIP] bands=%zu mean=%.1f maxdev=%.1f |", gated.size(), wMean, maxDev);
        for (auto& g : gated) printf(" %.0f", g.angle);
        printf("\n");
        return {};
    }

    // Bands agree — output single weighted-average cluster
    Cluster c;
    for (auto& g : gated) {
        c.weightSum += g.weight;
        c.angleSum  += g.angle * g.weight;
        if (g.db > c.maxDb) c.maxDb = g.db;
        if (g.conf > c.maxConf) c.maxConf = g.conf;
    }

    float avgAngle = c.angle();
    float db = c.maxDb;

    printf("  [PASS] bands=%zu mean=%.1f maxdev=%.1f |", gated.size(), wMean, maxDev);
    for (auto& g : gated) printf(" %.0f", g.angle);
    printf("\n");

    lastOutputTime = now;

    char buf[128];
    snprintf(buf, sizeof(buf), R"({"a":%.1f,"e":%.1f,"c":%.2f})",
             avgAngle, db, c.maxConf);
    return std::string(buf);
}

static std::string makeSessionDir(const std::string& baseDir) {
    time_t now = time(nullptr);
    struct tm t;
#ifdef _WIN32
    localtime_s(&t, &now);
#else
    localtime_r(&now, &t);
#endif
    char buf[64];
    strftime(buf, sizeof(buf), "session_%Y-%m-%d_%H-%M-%S", &t);
    return baseDir + "/" + buf;
}

static void printUsage() {
    printf("Usage: audio-engine [options]\n");
    printf("  --log [dir]   Enable clip logging (default dir: ./clips)\n");
    printf("  --no-pipe     Disable IPC pipe\n");
    printf("  --mix         Capture all audio (skip VALORANT auto-detect)\n");
    printf("  --device NAME Capture from a specific audio device (e.g. \"CABLE\" for VB-CABLE)\n");
    printf("  --merge-angle DEG  Second-pass cluster merge threshold (default: %.0f)\n", MERGE_ANGLE);
    printf("  --cluster-angle DEG First-pass cluster threshold (default: %.0f)\n", CLUSTER_ANGLE);
    printf("  --help        Show this message\n");
}

int main(int argc, char* argv[]) {
    setvbuf(stdout, nullptr, _IOLBF, 0);
    SetConsoleCtrlHandler(consoleHandler, TRUE);

    bool enableLog  = false;
    bool enablePipe = true;
    bool forceMix   = false;
    std::string logBaseDir = "./clips";
    std::string deviceName;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--log") == 0) {
            enableLog = true;
            if (i + 1 < argc && argv[i + 1][0] != '-')
                logBaseDir = argv[++i];
        } else if (strcmp(argv[i], "--no-pipe") == 0) {
            enablePipe = false;
        } else if (strcmp(argv[i], "--mix") == 0) {
            forceMix = true;
        } else if (strcmp(argv[i], "--device") == 0) {
            if (i + 1 < argc)
                deviceName = argv[++i];
        } else if (strcmp(argv[i], "--merge-angle") == 0) {
            if (i + 1 < argc)
                MERGE_ANGLE = strtof(argv[++i], nullptr);
        } else if (strcmp(argv[i], "--cluster-angle") == 0) {
            if (i + 1 < argc)
                CLUSTER_ANGLE = strtof(argv[++i], nullptr);
        } else if (strcmp(argv[i], "--help") == 0) {
            printUsage();
            return 0;
        }
    }

    printf("========================================\n");
    printf("  CHAIR  Audio Engine\n");
    printf("========================================\n\n");

    // --- Auto-detect VALORANT ---
    uint32_t targetPid = 0;
    if (!forceMix) {
        targetPid = findProcessByName(L"VALORANT.exe");
        if (targetPid) {
            printf("[main] Found VALORANT.exe (PID %u)\n", targetPid);
        } else {
            printf("[main] VALORANT not running — capturing all audio (use --mix to skip this check)\n");
        }
    } else {
        printf("[main] --mix flag: capturing all audio\n");
    }

    if (!deviceName.empty()) {
        printf("[main] Using device: \"%s\"\n", deviceName.c_str());
    }

    WasapiCapture capture;
    if (!capture.initialize(targetPid, deviceName)) {
        fprintf(stderr, "Fatal: could not initialize audio capture.\n");
        return 1;
    }

    const auto& cfg = capture.config();
    if (cfg.channels < 2) {
        fprintf(stderr, "Fatal: need stereo output, got %u channels.\n", cfg.channels);
        return 1;
    }

    AudioAnalyzer analyzer(cfg.sampleRate, /*fftSize=*/1024, /*hopSize=*/512);

    std::unique_ptr<PipeServer> pipe;
    if (enablePipe) {
        pipe = std::make_unique<PipeServer>();
        pipe->create();
    }

    std::unique_ptr<ClipLogger> logger;
    if (enableLog) {
        std::string sessionDir = makeSessionDir(logBaseDir);
        logger = std::make_unique<ClipLogger>(cfg.sampleRate, sessionDir);
    }

    if (!capture.start()) {
        fprintf(stderr, "Fatal: could not start capture.\n");
        return 1;
    }

    printf("[main] Listening... Ctrl+C to quit.\n");
    printf("[main] Energy gate: %.0f dB\n", ENERGY_GATE_DB);
    if (enableLog) printf("[main] Clip logging ENABLED\n");
    printf("\n");

    const uint32_t bufFrames = cfg.sampleRate / 10;
    std::vector<float> buf(bufFrames * 2);

    auto t0 = std::chrono::steady_clock::now();
    uint64_t totalFrames = 0;
    uint64_t totalEvents = 0;

    auto lastEventTime = std::chrono::steady_clock::now();
    auto lastValCheck = std::chrono::steady_clock::now();
    const bool wasValRunning = (targetPid != 0);

    while (g_running) {
        uint32_t frames = capture.captureFrames(buf.data(), bufFrames);
        if (frames == 0) continue;

        totalFrames += frames;
        analyzer.pushSamples(buf.data(), frames);

        if (logger) logger->pushAudio(buf.data(), frames);
        if (pipe)   pipe->poll();

        auto events = analyzer.process();

        // Send clustered directions to overlay
        const auto& readings = analyzer.lastBandReadings();
        if (!readings.empty()) {
            auto json = aggregateReadingsToJson(readings);
            if (!json.empty()) {
                // Send each cluster as a separate line (pipe is newline-delimited)
                if (pipe) {
                    size_t pos = 0;
                    while (pos < json.size()) {
                        size_t nl = json.find('\n', pos);
                        if (nl == std::string::npos) nl = json.size();
                        pipe->send(json.substr(pos, nl - pos));
                        pos = nl + 1;
                    }
                }
                printf("  %s\n", json.c_str());
                lastEventTime = std::chrono::steady_clock::now();
                ++totalEvents;
            }
        }

        // Log onset events if clip logger active
        if (logger) {
            for (const auto& e : events)
                logger->markEvent(e);
        }

        if (logger) logger->tick();

        // Check if VALORANT has closed (every 2s)
        auto now = std::chrono::steady_clock::now();
        if (wasValRunning && now - lastValCheck > std::chrono::seconds(2)) {
            lastValCheck = now;
            if (!findProcessByName(L"VALORANT.exe")) {
                printf("[main] VALORANT closed -- shutting down.\n");
                g_running = false;
            }
        }
    }

    capture.stop();
    if (logger) {
        logger->flush();
        printf("[main] %u clips saved to: %s\n", logger->clipCount(), logger->sessionDir().c_str());
    }

    auto elapsed = std::chrono::steady_clock::now() - t0;
    double secs = std::chrono::duration<double>(elapsed).count();

    printf("\n========== Session stats ==========\n");
    printf("  Duration       : %.1f s\n", secs);
    printf("  Frames captured: %llu\n", (unsigned long long)totalFrames);
    printf("  Effective rate : %.0f Hz\n", totalFrames / (secs + 1e-9));
    printf("  Events emitted : %llu\n", (unsigned long long)totalEvents);
    printf("  Avg event rate : %.1f / min\n", totalEvents * 60.0 / (secs + 1e-9));
    printf("===================================\n");

    return 0;
}
