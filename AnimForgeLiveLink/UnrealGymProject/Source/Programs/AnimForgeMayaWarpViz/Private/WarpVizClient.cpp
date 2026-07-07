// AnimForgeMayaWarpViz - WarpVizClient.cpp

#include "WarpVizClient.h"

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")

namespace AnimForge
{
namespace WarpViz
{

namespace
{

struct WinsockScope
{
    bool bInitialized = false;
    WinsockScope()
    {
        WSADATA Data;
        bInitialized = (WSAStartup(MAKEWORD(2, 2), &Data) == 0);
    }
    ~WinsockScope()
    {
        if (bInitialized)
        {
            WSACleanup();
        }
    }
};

// One WSAStartup for the plugin's lifetime.
WinsockScope& GetWinsock()
{
    static WinsockScope Scope;
    return Scope;
}

std::string LastSocketError()
{
    return "winsock error " + std::to_string(WSAGetLastError());
}

} // anonymous namespace

WarpVizClient& WarpVizClient::Get()
{
    static WarpVizClient Instance;
    return Instance;
}

WarpVizClient::~WarpVizClient()
{
    Disconnect();
}

bool WarpVizClient::Connect(const std::string& Host, uint16_t Port,
                            const std::string& CharacterId, std::string& OutError)
{
    if (!GetWinsock().bInitialized)
    {
        OutError = "WSAStartup failed";
        return false;
    }

    Disconnect();

    addrinfo Hints = {};
    Hints.ai_family = AF_INET;
    Hints.ai_socktype = SOCK_STREAM;
    Hints.ai_protocol = IPPROTO_TCP;

    addrinfo* Resolved = nullptr;
    const std::string PortString = std::to_string(Port);
    if (getaddrinfo(Host.c_str(), PortString.c_str(), &Hints, &Resolved) != 0 || !Resolved)
    {
        OutError = "Could not resolve host '" + Host + "'";
        return false;
    }

    SOCKET NewSocket = socket(Resolved->ai_family, Resolved->ai_socktype, Resolved->ai_protocol);
    if (NewSocket == INVALID_SOCKET)
    {
        freeaddrinfo(Resolved);
        OutError = LastSocketError();
        return false;
    }

    if (connect(NewSocket, Resolved->ai_addr, static_cast<int>(Resolved->ai_addrlen)) == SOCKET_ERROR)
    {
        freeaddrinfo(Resolved);
        closesocket(NewSocket);
        OutError = "Could not connect to " + Host + ":" + PortString
                 + " - is the AnimForge gym listening? (" + LastSocketError() + ")";
        return false;
    }
    freeaddrinfo(Resolved);

    // Trajectory payloads are small; latency matters more than throughput.
    const int NoDelay = 1;
    setsockopt(NewSocket, IPPROTO_TCP, TCP_NODELAY,
               reinterpret_cast<const char*>(&NoDelay), sizeof(NoDelay));

    Socket = static_cast<uintptr_t>(NewSocket);
    bStopRequested.store(false);
    bConnected.store(true);
    ReceiveThread = std::thread(&WarpVizClient::ReceiveThreadMain, this);

    HandshakeMessage Handshake;
    Handshake.ClientName = "AnimForgeMayaWarpViz 1.0";
    Handshake.CharacterId = CharacterId;
    if (!SendPayload(EncodeHandshake(Handshake), OutError))
    {
        Disconnect();
        return false;
    }
    return true;
}

void WarpVizClient::Disconnect()
{
    bStopRequested.store(true);
    if (Socket != 0)
    {
        // shutdown unblocks the recv() on the worker thread.
        shutdown(static_cast<SOCKET>(Socket), SD_BOTH);
        closesocket(static_cast<SOCKET>(Socket));
        Socket = 0;
    }
    if (ReceiveThread.joinable())
    {
        ReceiveThread.join();
    }
    bConnected.store(false);
}

bool WarpVizClient::SendEvaluateRequest(const EvaluateRequest& Request, std::string& OutError)
{
    if (!IsConnected())
    {
        OutError = "Not connected to the gym. Use 'animForgeWarpViz -connect' first.";
        return false;
    }
    return SendPayload(EncodeEvaluateRequest(Request), OutError);
}

bool WarpVizClient::SendPayload(const std::string& Payload, std::string& OutError)
{
    const std::vector<uint8_t> Frame = EncodeFrame(Payload);
    size_t Sent = 0;
    while (Sent < Frame.size())
    {
        const int Result = send(static_cast<SOCKET>(Socket),
                                reinterpret_cast<const char*>(Frame.data() + Sent),
                                static_cast<int>(Frame.size() - Sent), 0);
        if (Result == SOCKET_ERROR)
        {
            OutError = "send failed: " + LastSocketError();
            bConnected.store(false);
            return false;
        }
        Sent += static_cast<size_t>(Result);
    }
    return true;
}

void WarpVizClient::ReceiveThreadMain()
{
    FrameDecoder Decoder;
    char Buffer[65536];

    while (!bStopRequested.load())
    {
        const int Received = recv(static_cast<SOCKET>(Socket), Buffer, sizeof(Buffer), 0);
        if (Received <= 0)
        {
            break; // closed or errored; the drain reports it via the queue below
        }
        Decoder.Feed(reinterpret_cast<const uint8_t*>(Buffer), static_cast<size_t>(Received));

        std::string Payload;
        while (Decoder.Next(Payload))
        {
            EnqueueIncoming(std::move(Payload));
        }
    }
    bConnected.store(false);
}

void WarpVizClient::EnqueueIncoming(std::string Payload)
{
    std::lock_guard<std::mutex> Lock(QueueMutex);
    IncomingPayloads.push_back(std::move(Payload));
}

void WarpVizClient::SetHandlers(ResultHandler InOnResult, ProgressHandler InOnProgress,
                                ErrorHandler InOnError)
{
    OnResult = std::move(InOnResult);
    OnProgress = std::move(InOnProgress);
    OnError = std::move(InOnError);
}

void WarpVizClient::DrainMainThread()
{
    std::vector<std::string> Pending;
    {
        std::lock_guard<std::mutex> Lock(QueueMutex);
        Pending.swap(IncomingPayloads);
    }

    for (const std::string& Payload : Pending)
    {
        MessageType Type = MessageType::Unknown;
        uint32_t Version = 0;
        std::string Error;
        if (!PeekEnvelope(Payload, Type, Version, Error))
        {
            if (OnError) OnError("Malformed message from gym: " + Error);
            continue;
        }

        switch (Type)
        {
        case MessageType::HandshakeAck:
        {
            HandshakeAckMessage Ack;
            if (DecodeHandshakeAck(Payload, Ack, Error))
            {
                KnownClips = Ack.KnownClips;
            }
            break;
        }
        case MessageType::EvaluateProgress:
        {
            EvaluateProgress Progress;
            if (DecodeEvaluateProgress(Payload, Progress, Error) && OnProgress)
            {
                OnProgress(Progress);
            }
            break;
        }
        case MessageType::EvaluateResult:
        {
            EvaluateResult Result;
            if (DecodeEvaluateResult(Payload, Result, Error))
            {
                if (OnResult) OnResult(Result);
            }
            else if (OnError)
            {
                OnError("Could not decode EvaluateResult: " + Error);
            }
            break;
        }
        case MessageType::Error:
        {
            ErrorMessage Message;
            if (DecodeError(Payload, Message, Error) && OnError)
            {
                OnError("Gym error: " + Message.Message);
            }
            break;
        }
        default:
            if (OnError) OnError("Unexpected message type from gym");
            break;
        }
    }
}

} // namespace WarpViz
} // namespace AnimForge
