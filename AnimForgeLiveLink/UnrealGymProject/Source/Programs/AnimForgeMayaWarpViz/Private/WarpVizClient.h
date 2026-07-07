// AnimForgeMayaWarpViz - WarpVizClient.h
//
// TCP client that owns the connection from Maya to the Unreal gym. Socket IO
// runs on a worker thread; completed messages are queued and drained on the
// Maya main thread via an idle callback (Maya API calls are not thread-safe,
// so the anim layer import must never happen on the recv thread).

#pragma once

#include "WarpVizProtocol.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace AnimForge
{
namespace WarpViz
{

class WarpVizClient
{
public:
    // Called on the *main* thread when DrainMainThread() pops messages.
    using ResultHandler = std::function<void(const EvaluateResult&)>;
    using ProgressHandler = std::function<void(const EvaluateProgress&)>;
    using ErrorHandler = std::function<void(const std::string&)>;

    static WarpVizClient& Get();

    WarpVizClient(const WarpVizClient&) = delete;
    WarpVizClient& operator=(const WarpVizClient&) = delete;

    // Connects and performs the handshake. Returns false with OutError set on
    // failure. Safe to call when already connected (reconnects).
    bool Connect(const std::string& Host, uint16_t Port,
                 const std::string& CharacterId, std::string& OutError);
    void Disconnect();
    bool IsConnected() const { return bConnected.load(); }

    // Sends an EvaluateRequest. The result arrives asynchronously through the
    // handler passed to SetHandlers().
    bool SendEvaluateRequest(const EvaluateRequest& Request, std::string& OutError);

    void SetHandlers(ResultHandler OnResult, ProgressHandler OnProgress, ErrorHandler OnError);

    // Must be called from the Maya main thread (idle callback registered in
    // MayaPluginMain.cpp). Dispatches every queued message to the handlers.
    void DrainMainThread();

    // Clips reported by the gym in the handshake ack (main thread only).
    const std::vector<std::string>& GetKnownClips() const { return KnownClips; }

private:
    WarpVizClient() = default;
    ~WarpVizClient();

    void ReceiveThreadMain();
    bool SendPayload(const std::string& Payload, std::string& OutError);
    void EnqueueIncoming(std::string Payload);

    std::atomic<bool> bConnected{ false };
    std::atomic<bool> bStopRequested{ false };
    uintptr_t Socket = 0;               // SOCKET, kept opaque to avoid winsock in the header
    std::thread ReceiveThread;

    std::mutex QueueMutex;
    std::vector<std::string> IncomingPayloads;   // guarded by QueueMutex

    ResultHandler OnResult;
    ProgressHandler OnProgress;
    ErrorHandler OnError;
    std::vector<std::string> KnownClips;
};

} // namespace WarpViz
} // namespace AnimForge
