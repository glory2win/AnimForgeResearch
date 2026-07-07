// AnimForgeWarpVizShared - WarpVizProtocol.h
//
// Wire protocol between AnimForgeMayaWarpViz (.mll, TCP client) and
// AnimForgeUnrealWarpViz (UE plugin, TCP server).
//
// Frame layout (little-endian):
//   [4 bytes magic "AFWV"] [4 bytes payload length] [payload: UTF-8 JSON]
//
// Payload envelope:
//   { "type": "<MessageType>", "protocolVersion": 1, "payload": { ... } }
//
// Message flow:
//   Maya -> Unreal : Handshake            (client info, character id)
//   Unreal -> Maya : HandshakeAck         (engine version, available clips)
//   Maya -> Unreal : EvaluateRequest      (time range, method, target, root curve)
//   Unreal -> Maya : EvaluateProgress     (optional, 0..1)
//   Unreal -> Maya : EvaluateResult       (warped + original trajectory, ghosts)
//   either -> other: Error                (requestId when applicable)
//
// Mirrored 1:1 by Scripts/warpviz_protocol.py so the Python mock server and
// the automated tests speak the same dialect.

#pragma once

#include "WarpVizJson.h"
#include "WarpVizTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace AnimForge
{
namespace WarpViz
{

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

constexpr uint32_t kProtocolVersion = 1;
constexpr uint16_t kDefaultPort = 46464;
constexpr char kFrameMagic[4] = { 'A', 'F', 'W', 'V' };
constexpr uint32_t kMaxPayloadBytes = 256u * 1024u * 1024u; // sanity cap: 256 MB

// ---------------------------------------------------------------------------
// Framing
// ---------------------------------------------------------------------------

// Wraps a payload in [magic][length][payload].
std::vector<uint8_t> EncodeFrame(const std::string& Payload);

// Incremental frame decoder. Feed raw socket bytes in any fragmentation; call
// Next() until it returns false. Resynchronizes on the magic if the stream is
// corrupted (drops garbage bytes and counts them in DroppedBytes()).
class FrameDecoder
{
public:
    void Feed(const uint8_t* Data, size_t Length);

    // Pops the next complete payload. Returns false when no full frame is
    // buffered yet.
    bool Next(std::string& OutPayload);

    size_t DroppedBytes() const { return TotalDroppedBytes; }
    size_t BufferedBytes() const { return Buffer.size(); }

private:
    void Resync();

    std::vector<uint8_t> Buffer;
    size_t TotalDroppedBytes = 0;
};

// ---------------------------------------------------------------------------
// Messages
// ---------------------------------------------------------------------------

enum class MessageType
{
    Handshake,
    HandshakeAck,
    EvaluateRequest,
    EvaluateProgress,
    EvaluateResult,
    Error,
    Unknown
};

const char* MessageTypeToString(MessageType Type);
MessageType MessageTypeFromString(const std::string& Name);

struct HandshakeMessage
{
    std::string ClientName;     // e.g. "Maya 2025 / AnimForgeMayaWarpViz 1.0"
    std::string CharacterId;    // must match a character mapping in the gym
};

struct HandshakeAckMessage
{
    std::string ServerName;             // e.g. "UE 5.4 / AnimForgeUnrealWarpViz 1.0"
    std::vector<std::string> KnownClips; // clip names registered in WarpVizSettings
};

struct EvaluateRequest
{
    std::string RequestId;      // GUID minted by the Maya side
    std::string CharacterId;
    std::string ClipName;       // maps to a montage/sequence in the gym settings
    TimeRange Range;            // evaluation range (Maya frames)
    TimeRange WarpWindow;       // warp window; defaults to Range when unset in the UI
    WarpMethod Method = WarpMethod::SkewWarp;
    WarpTarget Target;
    bool bWarpRotation = true;
    bool bWarpTranslation = true;
    double GhostIntervalFrames = 5.0;   // ghost pose sampling stride; <= 0 disables ghosts

    // Root trajectory sampled from the Maya scene (one sample per frame).
    // The gym uses it to validate that the Maya clip and the imported UE asset
    // agree before evaluating, and reports drift as a warning.
    std::vector<TrajectorySample> MayaRootSamples;
};

struct EvaluateProgress
{
    std::string RequestId;
    double Progress = 0.0;      // 0..1
    std::string Stage;          // "extracting", "warping", "sampling_ghosts"
};

struct EvaluateResult
{
    std::string RequestId;
    bool bSuccess = false;
    std::string ErrorMessage;               // set when bSuccess == false

    std::vector<TrajectorySample> WarpedTrajectory;
    std::vector<TrajectorySample> OriginalTrajectory; // unwarped, for side-by-side viz
    std::vector<GhostPose> GhostPoses;
    std::vector<std::string> Warnings;      // e.g. Maya/UE root drift above tolerance
    double EvaluationMs = 0.0;
};

struct ErrorMessage
{
    std::string RequestId;      // empty when not tied to a request
    std::string Message;
};

// ---------------------------------------------------------------------------
// Envelope encode / decode
// ---------------------------------------------------------------------------

// Peeks type + version from an envelope without decoding the payload.
bool PeekEnvelope(const std::string& PayloadJson, MessageType& OutType,
                  uint32_t& OutVersion, std::string& OutError);

std::string EncodeHandshake(const HandshakeMessage& Msg);
std::string EncodeHandshakeAck(const HandshakeAckMessage& Msg);
std::string EncodeEvaluateRequest(const EvaluateRequest& Msg);
std::string EncodeEvaluateProgress(const EvaluateProgress& Msg);
std::string EncodeEvaluateResult(const EvaluateResult& Msg);
std::string EncodeError(const ErrorMessage& Msg);

bool DecodeHandshake(const std::string& PayloadJson, HandshakeMessage& Out, std::string& OutError);
bool DecodeHandshakeAck(const std::string& PayloadJson, HandshakeAckMessage& Out, std::string& OutError);
bool DecodeEvaluateRequest(const std::string& PayloadJson, EvaluateRequest& Out, std::string& OutError);
bool DecodeEvaluateProgress(const std::string& PayloadJson, EvaluateProgress& Out, std::string& OutError);
bool DecodeEvaluateResult(const std::string& PayloadJson, EvaluateResult& Out, std::string& OutError);
bool DecodeError(const std::string& PayloadJson, ErrorMessage& Out, std::string& OutError);

// JSON helpers shared with the plugin code (exposed for tests).
JsonValue Vec3ToJson(const Vec3& V);
JsonValue QuatToJson(const Quat& Q);
JsonValue TrajectorySampleToJson(const TrajectorySample& S);
bool Vec3FromJson(const JsonValue& J, Vec3& Out);
bool QuatFromJson(const JsonValue& J, Quat& Out);
bool TrajectorySampleFromJson(const JsonValue& J, TrajectorySample& Out);

} // namespace WarpViz
} // namespace AnimForge
