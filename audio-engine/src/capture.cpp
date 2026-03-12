#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <initguid.h>

#include "capture.h"

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <tlhelp32.h>
#include <propidl.h>

#include <functiondiscoverykeys_devpkey.h>

#include <cstdio>
#include <cstring>
#include <string>

// KSDATAFORMAT_SUBTYPE GUIDs — defined manually for MinGW
// {00000003-0000-0010-8000-00AA00389B71}
DEFINE_GUID(CHAIR_SUBTYPE_IEEE_FLOAT,
    0x00000003, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71);
// {00000001-0000-0010-8000-00AA00389B71}
DEFINE_GUID(CHAIR_SUBTYPE_PCM,
    0x00000001, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71);

// ---------------------------------------------------------------------------
// Process-specific loopback types (not in MinGW headers)
// ---------------------------------------------------------------------------

enum AUDIOCLIENT_ACTIVATION_TYPE {
    AUDIOCLIENT_ACTIVATION_TYPE_DEFAULT = 0,
    AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK = 1,
};

enum PROCESS_LOOPBACK_MODE {
    PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE = 0,
    PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE = 1,
};

struct AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS {
    DWORD TargetProcessId;
    PROCESS_LOOPBACK_MODE ProcessLoopbackMode;
};

struct AUDIOCLIENT_ACTIVATION_PARAMS {
    AUDIOCLIENT_ACTIVATION_TYPE ActivationType;
    union {
        AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS ProcessLoopbackParams;
    };
};

static const wchar_t VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK[] =
    L"VAD\\Process_Loopback";

// Load ActivateAudioInterfaceAsync dynamically (not in MinGW import libs)
typedef HRESULT (WINAPI *PFN_ActivateAudioInterfaceAsync)(
    LPCWSTR                                     deviceInterfacePath,
    REFIID                                      riid,
    PROPVARIANT*                                activationParams,
    IActivateAudioInterfaceCompletionHandler*   completionHandler,
    IActivateAudioInterfaceAsyncOperation**     createAsync
);

static PFN_ActivateAudioInterfaceAsync getActivateAudioInterfaceAsync() {
    static PFN_ActivateAudioInterfaceAsync fn = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        HMODULE mod = LoadLibraryA("mmdevapi.dll");
        if (mod) {
            fn = (PFN_ActivateAudioInterfaceAsync)GetProcAddress(
                mod, "ActivateAudioInterfaceAsync");
        }
    }
    return fn;
}

// ---------------------------------------------------------------------------
// Completion handler for ActivateAudioInterfaceAsync
// ---------------------------------------------------------------------------

class ActivationHandler : public IActivateAudioInterfaceCompletionHandler {
    LONG         m_ref    = 1;
    HANDLE       m_event;
    IAudioClient* m_client = nullptr;
    HRESULT      m_hr     = E_FAIL;

public:
    explicit ActivationHandler(HANDLE event) : m_event(event) {}

    HRESULT STDMETHODCALLTYPE ActivateCompleted(
            IActivateAudioInterfaceAsyncOperation* op) override {
        HRESULT hrActivate = E_FAIL;
        IUnknown* unk = nullptr;
        op->GetActivateResult(&hrActivate, &unk);
        m_hr = hrActivate;
        if (SUCCEEDED(hrActivate) && unk) {
            unk->QueryInterface(IID_IAudioClient, (void**)&m_client);
            unk->Release();
        }
        SetEvent(m_event);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_IActivateAudioInterfaceCompletionHandler) {
            *ppv = this;
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef()  override { return InterlockedIncrement(&m_ref); }
    virtual ~ActivationHandler() = default;
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = InterlockedDecrement(&m_ref);
        if (r == 0) delete this;
        return r;
    }

    IAudioClient* client() const { return m_client; }
    HRESULT       result() const { return m_hr; }
};

// ---------------------------------------------------------------------------
// Process finder
// ---------------------------------------------------------------------------

uint32_t findProcessByName(const wchar_t* exeName) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    uint32_t pid = 0;
    if (Process32FirstW(snap, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, exeName) == 0) {
                pid = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &entry));
    }

    CloseHandle(snap);
    return pid;
}

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------

struct WasapiCapture::Impl {
    IMMDeviceEnumerator* enumerator    = nullptr;
    IMMDevice*           device        = nullptr;
    IAudioClient*        audioClient   = nullptr;
    IAudioCaptureClient* captureClient = nullptr;
    WAVEFORMATEX*        mixFormat     = nullptr;
    HANDLE               captureEvent  = nullptr;
    bool                 capturing     = false;
    bool                 isFloat       = false;
    bool                 processSpecific = false;
};

WasapiCapture::WasapiCapture() : m_impl(new Impl()) {}

WasapiCapture::~WasapiCapture() {
    stop();
    if (m_impl->captureClient) m_impl->captureClient->Release();
    if (m_impl->audioClient)   m_impl->audioClient->Release();
    if (m_impl->device)        m_impl->device->Release();
    if (m_impl->enumerator)    m_impl->enumerator->Release();
    if (m_impl->mixFormat)     CoTaskMemFree(m_impl->mixFormat);
    if (m_impl->captureEvent)  CloseHandle(m_impl->captureEvent);
    CoUninitialize();
    delete m_impl;
}

bool WasapiCapture::isProcessSpecific() const {
    return m_impl->processSpecific;
}

// ---------------------------------------------------------------------------
// Main initialize — try process-specific, fall back to full mix
// ---------------------------------------------------------------------------

bool WasapiCapture::initialize(uint32_t targetPid, const std::string& deviceSubstr) {
    // If a specific device is requested (e.g. virtual audio cable), skip process loopback
    if (targetPid != 0 && deviceSubstr.empty()) {
        printf("[capture] Attempting process-specific loopback for PID %u...\n", targetPid);
        fflush(stdout);

        // Process loopback runs in its own thread with separate COM init.
        // If it succeeds, we still need MTA on main thread for the capture loop.
        if (initProcessLoopback(targetPid)) {
            HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
                fprintf(stderr, "[capture] CoInitializeEx failed: 0x%08lx\n", hr);
                return false;
            }
            m_impl->processSpecific = true;
            return true;
        }
        printf("[capture] Process loopback unavailable (anti-cheat may block it), using full mix\n");
        fflush(stdout);
    }

    // Full mix / specific device path — initialize COM here
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        fprintf(stderr, "[capture] CoInitializeEx failed: 0x%08lx\n", hr);
        return false;
    }

    return initFullMixLoopback(deviceSubstr);
}

// ---------------------------------------------------------------------------
// Full-mix loopback (original code path)
// ---------------------------------------------------------------------------

bool WasapiCapture::initFullMixLoopback(const std::string& deviceSubstr) {
    HRESULT hr;

    hr = CoCreateInstance(
        CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL,
        IID_IMMDeviceEnumerator, (void**)&m_impl->enumerator);
    if (FAILED(hr)) {
        fprintf(stderr, "[capture] CoCreateInstance(MMDeviceEnumerator) failed: 0x%08lx\n", hr);
        return false;
    }

    if (!deviceSubstr.empty()) {
        // Find device by substring match on its friendly name
        IMMDeviceCollection* devices = nullptr;
        hr = m_impl->enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices);
        if (FAILED(hr)) {
            fprintf(stderr, "[capture] EnumAudioEndpoints failed: 0x%08lx\n", hr);
            return false;
        }

        UINT count = 0;
        devices->GetCount(&count);
        bool found = false;

        printf("[capture] Searching for device matching \"%s\"...\n", deviceSubstr.c_str());
        for (UINT i = 0; i < count; ++i) {
            IMMDevice* dev = nullptr;
            devices->Item(i, &dev);
            if (!dev) continue;

            IPropertyStore* props = nullptr;
            dev->OpenPropertyStore(STGM_READ, &props);
            if (props) {
                PROPVARIANT name;
                PropVariantInit(&name);
                props->GetValue(PKEY_Device_FriendlyName, &name);
                if (name.vt == VT_LPWSTR && name.pwszVal) {
                    // Convert wide string to narrow for comparison
                    char narrow[256];
                    WideCharToMultiByte(CP_UTF8, 0, name.pwszVal, -1, narrow, sizeof(narrow), nullptr, nullptr);

                    // Case-insensitive substring search
                    std::string devName(narrow);
                    std::string needle(deviceSubstr);
                    for (auto& c : devName) c = (char)tolower(c);
                    for (auto& c : needle) c = (char)tolower(c);

                    if (devName.find(needle) != std::string::npos) {
                        printf("[capture] Found device: %s\n", narrow);
                        m_impl->device = dev;
                        found = true;
                        PropVariantClear(&name);
                        props->Release();
                        break;
                    } else {
                        printf("[capture]   skip: %s\n", narrow);
                    }
                }
                PropVariantClear(&name);
                props->Release();
            }
            dev->Release();
        }
        devices->Release();

        if (!found) {
            fprintf(stderr, "[capture] No device matching \"%s\" found.\n", deviceSubstr.c_str());
            return false;
        }
    } else {
        hr = m_impl->enumerator->GetDefaultAudioEndpoint(
            eRender, eConsole, &m_impl->device);
        if (FAILED(hr)) {
            fprintf(stderr, "[capture] GetDefaultAudioEndpoint failed: 0x%08lx\n", hr);
            return false;
        }
    }

    hr = m_impl->device->Activate(
        IID_IAudioClient, CLSCTX_ALL, nullptr, (void**)&m_impl->audioClient);
    if (FAILED(hr)) {
        fprintf(stderr, "[capture] Activate(IAudioClient) failed: 0x%08lx\n", hr);
        return false;
    }

    hr = m_impl->audioClient->GetMixFormat(&m_impl->mixFormat);
    if (FAILED(hr)) {
        fprintf(stderr, "[capture] GetMixFormat failed: 0x%08lx\n", hr);
        return false;
    }

    if (!setupFormatAndCapture()) return false;

    // Initialize loopback
    REFERENCE_TIME bufferDuration = 200000; // 20 ms
    hr = m_impl->audioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
        bufferDuration, 0, m_impl->mixFormat, nullptr);
    if (FAILED(hr)) {
        fprintf(stderr, "[capture] AudioClient::Initialize failed: 0x%08lx\n", hr);
        return false;
    }

    // Event handle + capture client
    m_impl->captureEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!m_impl->captureEvent) return false;

    hr = m_impl->audioClient->SetEventHandle(m_impl->captureEvent);
    if (FAILED(hr)) return false;

    hr = m_impl->audioClient->GetService(
        IID_IAudioCaptureClient, (void**)&m_impl->captureClient);
    if (FAILED(hr)) return false;

    printf("[capture] Full-mix loopback: %s, %u Hz, %u ch, %u bit\n",
        m_config.formatName.c_str(),
        m_config.sampleRate, m_config.channels, m_config.bitsPerSample);

    return true;
}

// ---------------------------------------------------------------------------
// Process-specific loopback (Windows 11)
// ---------------------------------------------------------------------------

// Data passed to the STA activation thread
struct ProcessLoopbackData {
    uint32_t pid;
    IAudioClient* client;
    HRESULT result;
};

static DWORD WINAPI staActivationThread(LPVOID param) {
    auto* data = static_cast<ProcessLoopbackData*>(param);
    data->client = nullptr;
    data->result = E_FAIL;

    // STA required for ActivateAudioInterfaceAsync
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    auto fnActivate = getActivateAudioInterfaceAsync();
    if (!fnActivate) {
        data->result = E_NOTIMPL;
        CoUninitialize();
        return 1;
    }

    AUDIOCLIENT_ACTIVATION_PARAMS acParams{};
    acParams.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    acParams.ProcessLoopbackParams.TargetProcessId = data->pid;
    acParams.ProcessLoopbackParams.ProcessLoopbackMode =
        PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;

    PROPVARIANT activateParams{};
    activateParams.vt = VT_BLOB;
    activateParams.blob.cbSize = sizeof(acParams);
    activateParams.blob.pBlobData = reinterpret_cast<BYTE*>(&acParams);

    HANDLE completionEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    auto* handler = new ActivationHandler(completionEvent);
    IActivateAudioInterfaceAsyncOperation* asyncOp = nullptr;

    HRESULT hr = fnActivate(
        VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
        IID_IAudioClient,
        &activateParams,
        handler,
        &asyncOp);

    if (SUCCEEDED(hr)) {
        // Pump messages while waiting (STA requires message loop for async callbacks)
        while (true) {
            DWORD wait = MsgWaitForMultipleObjects(1, &completionEvent, FALSE, 5000, QS_ALLINPUT);
            if (wait == WAIT_OBJECT_0) break;      // event signaled
            if (wait == WAIT_OBJECT_0 + 1) {
                MSG msg;
                while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
                    DispatchMessageW(&msg);
            } else {
                break; // timeout
            }
        }

        if (SUCCEEDED(handler->result()) && handler->client()) {
            data->client = handler->client();
            data->client->AddRef();
            data->result = S_OK;
        } else {
            data->result = handler->result();
        }
    } else {
        data->result = hr;
    }

    if (asyncOp) asyncOp->Release();
    handler->Release();
    CloseHandle(completionEvent);
    CoUninitialize();
    return 0;
}

bool WasapiCapture::initProcessLoopback(uint32_t pid) {
    ProcessLoopbackData data{};
    data.pid = pid;

    HANDLE thread = CreateThread(nullptr, 0, staActivationThread, &data, 0, nullptr);
    if (!thread) return false;

    WaitForSingleObject(thread, 3000);  // 3s is plenty
    CloseHandle(thread);

    if (FAILED(data.result) || !data.client) {
        printf("[capture] Process loopback not available (0x%08lx) -- falling back to full mix\n", data.result);
        return false;
    }

    m_impl->audioClient = data.client;

    // Get mix format from the activated client
    HRESULT hr = m_impl->audioClient->GetMixFormat(&m_impl->mixFormat);
    if (FAILED(hr)) {
        fprintf(stderr, "[capture] GetMixFormat (process) failed: 0x%08lx\n", hr);
        return false;
    }

    if (!setupFormatAndCapture()) return false;

    // Initialize — no LOOPBACK flag needed for process-specific
    REFERENCE_TIME bufferDuration = 200000;
    hr = m_impl->audioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
        bufferDuration, 0, m_impl->mixFormat, nullptr);
    if (FAILED(hr)) {
        fprintf(stderr, "[capture] AudioClient::Initialize (process) failed: 0x%08lx\n", hr);
        return false;
    }

    m_impl->captureEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!m_impl->captureEvent) return false;

    hr = m_impl->audioClient->SetEventHandle(m_impl->captureEvent);
    if (FAILED(hr)) return false;

    hr = m_impl->audioClient->GetService(
        IID_IAudioCaptureClient, (void**)&m_impl->captureClient);
    if (FAILED(hr)) return false;

    printf("[capture] Process-specific loopback (PID %u): %s, %u Hz, %u ch, %u bit\n",
        pid, m_config.formatName.c_str(),
        m_config.sampleRate, m_config.channels, m_config.bitsPerSample);

    return true;
}

// ---------------------------------------------------------------------------
// Shared: parse format, fill config
// ---------------------------------------------------------------------------

bool WasapiCapture::setupFormatAndCapture() {
    WAVEFORMATEX* fmt = m_impl->mixFormat;
    m_config.sampleRate    = fmt->nSamplesPerSec;
    m_config.channels      = fmt->nChannels;
    m_config.bitsPerSample = fmt->wBitsPerSample;

    if (fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        m_impl->isFloat = true;
        m_config.formatName = "IEEE Float";
    } else if (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE && fmt->cbSize >= 22) {
        auto* fmtEx = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(fmt);
        if (IsEqualGUID(fmtEx->SubFormat, CHAIR_SUBTYPE_IEEE_FLOAT)) {
            m_impl->isFloat = true;
            m_config.formatName = "IEEE Float (Extensible)";
        } else if (IsEqualGUID(fmtEx->SubFormat, CHAIR_SUBTYPE_PCM)) {
            m_impl->isFloat = false;
            m_config.formatName = "PCM (Extensible)";
        } else {
            fprintf(stderr, "[capture] Unsupported subformat\n");
            return false;
        }
    } else if (fmt->wFormatTag == WAVE_FORMAT_PCM) {
        m_impl->isFloat = false;
        m_config.formatName = "PCM";
    } else {
        fprintf(stderr, "[capture] Unsupported wave format: 0x%04x\n", fmt->wFormatTag);
        return false;
    }

    if (m_config.channels < 2) {
        fprintf(stderr, "[capture] Need stereo, got %u channels\n", m_config.channels);
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Start / Stop / Capture
// ---------------------------------------------------------------------------

bool WasapiCapture::start() {
    HRESULT hr = m_impl->audioClient->Start();
    if (FAILED(hr)) {
        fprintf(stderr, "[capture] Start failed: 0x%08lx\n", hr);
        return false;
    }
    m_impl->capturing = true;
    return true;
}

void WasapiCapture::stop() {
    if (m_impl && m_impl->capturing) {
        m_impl->audioClient->Stop();
        m_impl->capturing = false;
    }
}

uint32_t WasapiCapture::captureFrames(float* buffer, uint32_t maxFrames) {
    if (!m_impl->capturing) return 0;

    DWORD waitResult = WaitForSingleObject(m_impl->captureEvent, 10);
    if (waitResult != WAIT_OBJECT_0) return 0;

    uint32_t totalCaptured = 0;
    const uint32_t srcChannels = m_config.channels;

    while (totalCaptured < maxFrames) {
        BYTE*    data      = nullptr;
        UINT32   numFrames = 0;
        DWORD    flags     = 0;

        HRESULT hr = m_impl->captureClient->GetBuffer(&data, &numFrames, &flags, nullptr, nullptr);
        if (FAILED(hr) || numFrames == 0) break;

        uint32_t framesToCopy = numFrames;
        if (totalCaptured + framesToCopy > maxFrames)
            framesToCopy = maxFrames - totalCaptured;

        float* dst = buffer + totalCaptured * 2;

        if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
            memset(dst, 0, framesToCopy * 2 * sizeof(float));
        } else if (m_impl->isFloat) {
            const float* src = reinterpret_cast<const float*>(data);
            if (srcChannels == 2) {
                memcpy(dst, src, framesToCopy * 2 * sizeof(float));
            } else {
                for (uint32_t i = 0; i < framesToCopy; ++i) {
                    dst[i * 2]     = src[i * srcChannels];
                    dst[i * 2 + 1] = src[i * srcChannels + 1];
                }
            }
        } else {
            const int16_t* src = reinterpret_cast<const int16_t*>(data);
            for (uint32_t i = 0; i < framesToCopy; ++i) {
                dst[i * 2]     = src[i * srcChannels]     / 32768.0f;
                dst[i * 2 + 1] = src[i * srcChannels + 1] / 32768.0f;
            }
        }

        m_impl->captureClient->ReleaseBuffer(numFrames);
        totalCaptured += framesToCopy;
    }

    return totalCaptured;
}
