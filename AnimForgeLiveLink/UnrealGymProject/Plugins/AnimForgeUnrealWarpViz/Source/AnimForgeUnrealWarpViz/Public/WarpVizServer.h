// AnimForgeUnrealWarpViz - WarpVizServer.h
//
// TCP server the Maya plugin connects to. Accepting and socket IO run off the
// game thread; evaluation is marshalled onto the game thread (asset loading
// and anim evaluation require it) and the result is sent back on the
// connection's thread.

#pragma once

#include "CoreMinimal.h"
#include "Common/TcpListener.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"

#include "WarpVizProtocol.h"

#include <memory>

namespace AnimForge
{
namespace WarpViz
{

class FWarpVizConnection;

class ANIMFORGEUNREALWARPVIZ_API FWarpVizServer
{
public:
	~FWarpVizServer();

	bool Start(uint16 Port);
	void Stop();
	bool IsRunning() const { return Listener.IsValid(); }
	int32 GetConnectionCount() const;

private:
	bool HandleIncomingConnection(FSocket* Socket, const FIPv4Endpoint& Endpoint);
	void ReapClosedConnections();

	TUniquePtr<FTcpListener> Listener;
	mutable FCriticalSection ConnectionsLock;
	TArray<TSharedPtr<FWarpVizConnection>> Connections;
};

/**
 * One connected Maya client. Owns a reader thread that feeds the shared
 * FrameDecoder and dispatches complete messages.
 */
class FWarpVizConnection : public FRunnable, public TSharedFromThis<FWarpVizConnection>
{
public:
	explicit FWarpVizConnection(FSocket* InSocket, const FString& InPeerName);
	virtual ~FWarpVizConnection() override;

	void Start();
	void Close();
	bool IsClosed() const { return bClosed; }

	// FRunnable
	virtual uint32 Run() override;
	virtual void Stop() override { bStopRequested = true; }

private:
	void DispatchPayload(const std::string& Payload);
	void HandleEvaluateRequest(const std::string& Payload);
	bool SendPayload(const std::string& Payload);

	FSocket* Socket = nullptr;
	FString PeerName;
	FRunnableThread* Thread = nullptr;
	FThreadSafeBool bStopRequested = false;
	FThreadSafeBool bClosed = false;
	FCriticalSection SendLock; // game-thread results vs reader-thread errors
};

} // namespace WarpViz
} // namespace AnimForge
