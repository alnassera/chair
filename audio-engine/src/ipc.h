#pragma once

#include <cstdint>
#include <string>

/// One-way named-pipe server (engine → overlay).
/// Non-blocking: if no client is connected, send() is a silent no-op.
class PipeServer {
public:
    PipeServer();
    ~PipeServer();

    PipeServer(const PipeServer&)            = delete;
    PipeServer& operator=(const PipeServer&) = delete;

    /// Create the pipe and begin listening for a client.
    bool create(const char* pipeName = R"(\\.\pipe\chair-audio-events)");

    /// Check / advance client connection (non-blocking).
    /// Call once per frame.
    void poll();

    /// Returns true if a client is currently connected.
    bool connected() const { return m_connected; }

    /// Send a line of text. Appends '\n' automatically.
    /// Returns false if no client or write failed (resets to listening).
    bool send(const std::string& message);

private:
    void beginConnect();
    void resetPipe();

    void*  m_pipe       = nullptr;
    void*  m_event      = nullptr;
    bool   m_connected  = false;
    bool   m_connecting = false;
    char   m_overlapped[32] = {};    // OVERLAPPED — must persist for async ConnectNamedPipe
};
