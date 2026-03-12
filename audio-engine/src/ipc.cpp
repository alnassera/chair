#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "ipc.h"

#include <windows.h>
#include <cstdio>
#include <cstring>

// Ensure m_overlapped[] is large enough for OVERLAPPED
static_assert(sizeof(OVERLAPPED) <= 32, "Increase m_overlapped size");

static OVERLAPPED* ov(char* buf) { return reinterpret_cast<OVERLAPPED*>(buf); }

PipeServer::PipeServer() {}

PipeServer::~PipeServer() {
    if (m_pipe && m_pipe != INVALID_HANDLE_VALUE) {
        // Cancel any pending overlapped ConnectNamedPipe
        CancelIo(static_cast<HANDLE>(m_pipe));
        DisconnectNamedPipe(static_cast<HANDLE>(m_pipe));
        CloseHandle(static_cast<HANDLE>(m_pipe));
    }
    if (m_event) CloseHandle(static_cast<HANDLE>(m_event));
}

void PipeServer::beginConnect() {
    memset(m_overlapped, 0, sizeof(m_overlapped));
    ov(m_overlapped)->hEvent = static_cast<HANDLE>(m_event);

    BOOL result = ConnectNamedPipe(static_cast<HANDLE>(m_pipe), ov(m_overlapped));

    if (result) {
        m_connected = true;
    } else {
        DWORD err = GetLastError();
        if (err == ERROR_IO_PENDING) {
            m_connecting = true;
        } else if (err == ERROR_PIPE_CONNECTED) {
            m_connected = true;
        }
    }
}

bool PipeServer::create(const char* pipeName) {
    m_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!m_event) {
        fprintf(stderr, "[ipc] CreateEvent failed\n");
        return false;
    }

    m_pipe = CreateNamedPipeA(
        pipeName,
        PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        1, 4096, 0, 0, nullptr
    );

    if (m_pipe == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "[ipc] CreateNamedPipe failed: %lu\n", GetLastError());
        m_pipe = nullptr;
        return false;
    }

    beginConnect();

    printf("[ipc] Pipe created, waiting for overlay client...\n");
    return true;
}

void PipeServer::poll() {
    if (m_connected || !m_connecting) return;

    DWORD result = WaitForSingleObject(static_cast<HANDLE>(m_event), 0);
    if (result == WAIT_OBJECT_0) {
        m_connecting = false;
        m_connected  = true;
        printf("[ipc] Client connected\n");
    }
}

bool PipeServer::send(const std::string& message) {
    if (!m_connected) return false;

    std::string line = message + "\n";
    DWORD written = 0;
    BOOL ok = WriteFile(
        static_cast<HANDLE>(m_pipe),
        line.c_str(),
        static_cast<DWORD>(line.size()),
        &written,
        nullptr
    );

    if (!ok) {
        DWORD err = GetLastError();
        if (err == ERROR_BROKEN_PIPE || err == ERROR_NO_DATA) {
            printf("[ipc] Client disconnected, re-listening...\n");
            resetPipe();
        }
        return false;
    }

    return true;
}

void PipeServer::resetPipe() {
    m_connected  = false;
    m_connecting = false;

    CancelIo(static_cast<HANDLE>(m_pipe));
    DisconnectNamedPipe(static_cast<HANDLE>(m_pipe));
    ResetEvent(static_cast<HANDLE>(m_event));

    beginConnect();
}
