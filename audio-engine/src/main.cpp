#include "capture.h"
#include "analysis.h"
#include "classifier.h"
#include "ipc.h"
#include "clip_logger.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <atomic>
#include <chrono>
#include <ctime>
#include <vector>
#include <memory>

static std::atomic<bool> g_running{true};

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

static const char* directionName(AudioEvent::Direction d) {
    switch (d) {
        case AudioEvent::HARD_LEFT:  return "hard_left";
        case AudioEvent::LEFT:       return "left";
        case AudioEvent::CENTER:     return "center";
        case AudioEvent::RIGHT:      return "right";
        case AudioEvent::HARD_RIGHT: return "hard_right";
    }
    return "unknown";
}

static std::string eventToJson(const AudioEvent& e) {
    char buf[384];
    snprintf(buf, sizeof(buf),
        R"({"class":"%s","dir":"%s","conf":%.2f,"db":%.1f,"t":%.0f,)"
        R"("band":%u,"freqLo":%.0f,"freqHi":%.0f,"centroid":%.0f,"bandwidth":%.0f})",
        soundClassName(e.soundClass),
        directionName(e.direction),
        e.confidence, e.energy_db, e.timestamp_ms,
        e.band, e.freqLo, e.freqHi,
        e.centroid, e.bandwidth);
    return buf;
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
    printf("  --help        Show this message\n");
}

int main(int argc, char* argv[]) {
    // Line-buffer stdout so output appears immediately even when redirected
    setvbuf(stdout, nullptr, _IOLBF, 0);

    // Use Windows console handler instead of signal() — works in PowerShell/cmd
    SetConsoleCtrlHandler(consoleHandler, TRUE);

    bool enableLog  = false;
    bool enablePipe = true;
    bool forceMix   = false;
    std::string logBaseDir = "./clips";

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--log") == 0) {
            enableLog = true;
            if (i + 1 < argc && argv[i + 1][0] != '-')
                logBaseDir = argv[++i];
        } else if (strcmp(argv[i], "--no-pipe") == 0) {
            enablePipe = false;
        } else if (strcmp(argv[i], "--mix") == 0) {
            forceMix = true;
        } else if (strcmp(argv[i], "--help") == 0) {
            printUsage();
            return 0;
        }
    }

    printf("========================================\n");
    printf("  CHAIR  Audio Engine  —  M3\n");
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

    WasapiCapture capture;
    if (!capture.initialize(targetPid)) {
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
    if (enableLog) printf("[main] Clip logging ENABLED\n");
    printf("\n");

    const uint32_t bufFrames = cfg.sampleRate / 10;
    std::vector<float> buf(bufFrames * 2);

    auto t0 = std::chrono::steady_clock::now();
    uint64_t totalFrames = 0;
    uint64_t totalEvents = 0;

    while (g_running) {
        uint32_t frames = capture.captureFrames(buf.data(), bufFrames);
        if (frames == 0) continue;

        totalFrames += frames;
        analyzer.pushSamples(buf.data(), frames);

        if (logger) logger->pushAudio(buf.data(), frames);
        if (pipe)   pipe->poll();

        auto events = analyzer.process();
        for (const auto& e : events) {
            printf("  %s %s   [%+6.1f dB  conf=%.2f  cent=%.0f bw=%.0f  t=%.0f ms]\n",
                   soundClassShort(e.soundClass),
                   directionGlyph(e.direction),
                   e.energy_db, e.confidence,
                   e.centroid, e.bandwidth,
                   e.timestamp_ms);

            if (pipe)   pipe->send(eventToJson(e));
            if (logger) logger->markEvent(e);

            ++totalEvents;
        }

        if (logger) logger->tick();
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
