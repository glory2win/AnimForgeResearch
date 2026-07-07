// AnimForgeUnrealWarpViz - WarpVizServer.cpp

#include "WarpVizServer.h"

#include "WarpVizEvaluator.h"
#include "WarpVizSettings.h"

#include "Async/Async.h"
#include "Misc/EngineVersion.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogWarpViz, Log, All);

namespace AnimForge
{
namespace WarpViz
{

// ---------------------------------------------------------------------------
// FWarpVizServer
// ---------------------------------------------------------------------------

FWarpVizServer::~FWarpVizServer()
{
	Stop();
}

bool FWarpVizServer::Start(uint16 Port)
{
	Stop();

	const FIPv4Endpoint Endpoint(FIPv4Address::Any, Port);
	Listener = MakeUnique<FTcpListener>(Endpoint);
	Listener->OnConnectionAccepted().BindRaw(this, &FWarpVizServer::HandleIncomingConnection);

	if (!Listener->IsActive())
	{
		UE_LOG(LogWarpViz, Error, TEXT("WarpViz server could not listen on port %d (in use?)."), Port);
		Listener.Reset();
		return false;
	}

	UE_LOG(LogWarpViz, Display, TEXT("WarpViz gym server listening on port %d."), Port);
	return true;
}

void FWarpVizServer::Stop()
{
	if (Listener.IsValid())
	{
		Listener.Reset();
	}

	TArray<TSharedPtr<FWarpVizConnection>> ToClose;
	{
		FScopeLock Lock(&ConnectionsLock);
		ToClose = MoveTemp(Connections);
		Connections.Empty();
	}
	for (const TSharedPtr<FWarpVizConnection>& Connection : ToClose)
	{
		Connection->Close();
	}
}

int32 FWarpVizServer::GetConnectionCount() const
{
	FScopeLock Lock(&ConnectionsLock);
	return Connections.Num();
}

bool FWarpVizServer::HandleIncomingConnection(FSocket* Socket, const FIPv4Endpoint& Endpoint)
{
	UE_LOG(LogWarpViz, Display, TEXT("WarpViz: Maya client connected from %s."), *Endpoint.ToString());

	TSharedPtr<FWarpVizConnection> Connection =
		MakeShared<FWarpVizConnection>(Socket, Endpoint.ToString());
	{
		FScopeLock Lock(&ConnectionsLock);
		ReapClosedConnections();
		Connections.Add(Connection);
	}
	Connection->Start();
	return true; // we take ownership of the socket
}

void FWarpVizServer::ReapClosedConnections()
{
	// Caller holds ConnectionsLock.
	Connections.RemoveAll([](const TSharedPtr<FWarpVizConnection>& Connection)
	{
		return !Connection.IsValid() || Connection->IsClosed();
	});
}

// ---------------------------------------------------------------------------
// FWarpVizConnection
// ---------------------------------------------------------------------------

FWarpVizConnection::FWarpVizConnection(FSocket* InSocket, const FString& InPeerName)
	: Socket(InSocket)
	, PeerName(InPeerName)
{
}

FWarpVizConnection::~FWarpVizConnection()
{
	Close();
}

void FWarpVizConnection::Start()
{
	Thread = FRunnableThread::Create(
		this, *FString::Printf(TEXT("WarpVizConnection_%s"), *PeerName));
}

void FWarpVizConnection::Close()
{
	bStopRequested = true;
	if (Socket)
	{
		Socket->Close(); // unblocks the reader thread
	}
	if (Thread)
	{
		Thread->WaitForCompletion();
		delete Thread;
		Thread = nullptr;
	}
	if (Socket)
	{
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
		Socket = nullptr;
	}
	bClosed = true;
}

uint32 FWarpVizConnection::Run()
{
	FrameDecoder Decoder;
	TArray<uint8> Buffer;
	Buffer.SetNumUninitialized(64 * 1024);

	while (!bStopRequested)
	{
		if (!Socket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromMilliseconds(100)))
		{
			if (Socket->GetConnectionState() == SCS_ConnectionError)
			{
				break;
			}
			continue;
		}

		int32 BytesRead = 0;
		if (!Socket->Recv(Buffer.GetData(), Buffer.Num(), BytesRead) || BytesRead <= 0)
		{
			break; // closed by peer
		}

		Decoder.Feed(Buffer.GetData(), static_cast<size_t>(BytesRead));

		std::string Payload;
		while (Decoder.Next(Payload))
		{
			DispatchPayload(Payload);
		}
	}

	bClosed = true;
	UE_LOG(LogWarpViz, Display, TEXT("WarpViz: connection %s closed."), *PeerName);
	return 0;
}

void FWarpVizConnection::DispatchPayload(const std::string& Payload)
{
	MessageType Type = MessageType::Unknown;
	uint32_t Version = 0;
	std::string Error;
	if (!PeekEnvelope(Payload, Type, Version, Error))
	{
		ErrorMessage Message;
		Message.Message = "Malformed payload: " + Error;
		SendPayload(EncodeError(Message));
		return;
	}
	if (Version != kProtocolVersion)
	{
		ErrorMessage Message;
		Message.Message = "Protocol version mismatch: got " + std::to_string(Version)
			+ ", expected " + std::to_string(kProtocolVersion)
			+ ". Rebuild the Maya plugin?";
		SendPayload(EncodeError(Message));
		return;
	}

	switch (Type)
	{
	case MessageType::Handshake:
	{
		HandshakeMessage Handshake;
		if (DecodeHandshake(Payload, Handshake, Error))
		{
			UE_LOG(LogWarpViz, Display, TEXT("WarpViz: handshake from '%hs' (character '%hs')."),
				Handshake.ClientName.c_str(), Handshake.CharacterId.c_str());

			HandshakeAckMessage Ack;
			Ack.ServerName = std::string("AnimForgeUnrealWarpViz 1.0 / UE ")
				+ TCHAR_TO_UTF8(*FEngineVersion::Current().ToString(EVersionComponent::Patch));
			for (const FWarpVizClipEntry& Entry : GetDefault<UWarpVizSettings>()->Clips)
			{
				Ack.KnownClips.push_back(TCHAR_TO_UTF8(*Entry.ClipName));
			}
			SendPayload(EncodeHandshakeAck(Ack));
		}
		break;
	}
	case MessageType::EvaluateRequest:
		HandleEvaluateRequest(Payload);
		break;
	default:
	{
		ErrorMessage Message;
		Message.Message = std::string("Gym cannot handle message type '")
			+ MessageTypeToString(Type) + "'";
		SendPayload(EncodeError(Message));
		break;
	}
	}
}

void FWarpVizConnection::HandleEvaluateRequest(const std::string& Payload)
{
	EvaluateRequest Request;
	std::string Error;
	if (!DecodeEvaluateRequest(Payload, Request, Error))
	{
		ErrorMessage Message;
		Message.Message = "Bad EvaluateRequest: " + Error;
		SendPayload(EncodeError(Message));
		return;
	}

	UE_LOG(LogWarpViz, Display,
		TEXT("WarpViz: evaluate %hs clip='%hs' method=%hs frames=[%.1f, %.1f]."),
		Request.RequestId.substr(0, 8).c_str(), Request.ClipName.c_str(),
		WarpMethodToString(Request.Method), Request.Range.StartFrame, Request.Range.EndFrame);

	SendPayload(EncodeEvaluateProgress({ Request.RequestId, 0.1, "extracting" }));

	// Evaluation must run on the game thread; keep this connection alive via
	// a weak ref so an editor shutdown mid-evaluate cannot touch a dead socket.
	TWeakPtr<FWarpVizConnection> WeakSelf = AsShared();
	AsyncTask(ENamedThreads::GameThread, [WeakSelf, Request]()
	{
		const EvaluateResult Result = FWarpVizEvaluator::Evaluate(Request);
		if (TSharedPtr<FWarpVizConnection> Self = WeakSelf.Pin())
		{
			Self->SendPayload(EncodeEvaluateResult(Result));
		}
	});
}

bool FWarpVizConnection::SendPayload(const std::string& Payload)
{
	FScopeLock Lock(&SendLock);
	if (!Socket || bClosed)
	{
		return false;
	}

	const std::vector<uint8_t> Frame = EncodeFrame(Payload);
	int32 TotalSent = 0;
	while (TotalSent < static_cast<int32>(Frame.size()))
	{
		int32 Sent = 0;
		if (!Socket->Send(Frame.data() + TotalSent,
		                  static_cast<int32>(Frame.size()) - TotalSent, Sent) || Sent <= 0)
		{
			UE_LOG(LogWarpViz, Warning, TEXT("WarpViz: send to %s failed."), *PeerName);
			return false;
		}
		TotalSent += Sent;
	}
	return true;
}

} // namespace WarpViz
} // namespace AnimForge
