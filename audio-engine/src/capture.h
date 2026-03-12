#pragma once

#include <cstdint>
#include <string>

struct CaptureConfig {
    uint32_t sampleRate  = 0;
    uint32_t channels    = 0;
    uint32_t bitsPerSample = 0;
    std::string formatName;
    std::string deviceName;
};

/// Find a running process by executable name. Returns 0 if not found.
uint32_t findProcessByName(const wchar_t* exeName);

class WasapiCapture {
public:
    WasapiCapture();
    ~WasapiCapture();

    WasapiCapture(const WasapiCapture&)            = delete;
    WasapiCapture& operator=(const WasapiCapture&) = delete;

    /// Initialize capture. If targetPid != 0, uses process-specific loopback
    /// (Windows 11). If 0 or process loopback fails, falls back to full mix.
    bool initialize(uint32_t targetPid = 0);
    bool start();
    void stop();

    uint32_t captureFrames(float* buffer, uint32_t maxFrames);

    const CaptureConfig& config() const { return m_config; }
    bool isProcessSpecific() const;

private:
    struct Impl;
    Impl* m_impl = nullptr;
    CaptureConfig m_config;

    bool initFullMixLoopback();
    bool initProcessLoopback(uint32_t pid);
    bool setupFormatAndCapture();
};
