// AnimForgeWarpVizShared - WarpVizProtocol.cpp

#include "WarpVizProtocol.h"

#include <cstring>

namespace AnimForge
{
namespace WarpViz
{

// ---------------------------------------------------------------------------
// Enum <-> string
// ---------------------------------------------------------------------------

const char* WarpMethodToString(WarpMethod Method)
{
    switch (Method)
    {
    case WarpMethod::SkewWarp:   return "SkewWarp";
    case WarpMethod::SimpleWarp: return "SimpleWarp";
    case WarpMethod::Scale:      return "Scale";
    default:                     return "Unknown";
    }
}

WarpMethod WarpMethodFromString(const std::string& Name)
{
    if (Name == "SkewWarp")   return WarpMethod::SkewWarp;
    if (Name == "SimpleWarp") return WarpMethod::SimpleWarp;
    if (Name == "Scale")      return WarpMethod::Scale;
    return WarpMethod::Unknown;
}

const char* MessageTypeToString(MessageType Type)
{
    switch (Type)
    {
    case MessageType::Handshake:        return "Handshake";
    case MessageType::HandshakeAck:     return "HandshakeAck";
    case MessageType::EvaluateRequest:  return "EvaluateRequest";
    case MessageType::EvaluateProgress: return "EvaluateProgress";
    case MessageType::EvaluateResult:   return "EvaluateResult";
    case MessageType::Error:            return "Error";
    default:                            return "Unknown";
    }
}

MessageType MessageTypeFromString(const std::string& Name)
{
    if (Name == "Handshake")        return MessageType::Handshake;
    if (Name == "HandshakeAck")     return MessageType::HandshakeAck;
    if (Name == "EvaluateRequest")  return MessageType::EvaluateRequest;
    if (Name == "EvaluateProgress") return MessageType::EvaluateProgress;
    if (Name == "EvaluateResult")   return MessageType::EvaluateResult;
    if (Name == "Error")            return MessageType::Error;
    return MessageType::Unknown;
}

// ---------------------------------------------------------------------------
// Framing
// ---------------------------------------------------------------------------

std::vector<uint8_t> EncodeFrame(const std::string& Payload)
{
    std::vector<uint8_t> Frame;
    Frame.reserve(8 + Payload.size());
    Frame.insert(Frame.end(), kFrameMagic, kFrameMagic + 4);

    const uint32_t Length = static_cast<uint32_t>(Payload.size());
    Frame.push_back(static_cast<uint8_t>(Length & 0xFF));
    Frame.push_back(static_cast<uint8_t>((Length >> 8) & 0xFF));
    Frame.push_back(static_cast<uint8_t>((Length >> 16) & 0xFF));
    Frame.push_back(static_cast<uint8_t>((Length >> 24) & 0xFF));

    Frame.insert(Frame.end(), Payload.begin(), Payload.end());
    return Frame;
}

void FrameDecoder::Feed(const uint8_t* Data, size_t Length)
{
    Buffer.insert(Buffer.end(), Data, Data + Length);
}

void FrameDecoder::Resync()
{
    // Drop bytes until the buffer starts with the magic (or could - a partial
    // magic at the tail is kept so a split magic still decodes).
    size_t Drop = 0;
    while (Drop < Buffer.size())
    {
        const size_t Remaining = Buffer.size() - Drop;
        const size_t Compare = Remaining < 4 ? Remaining : 4;
        if (std::memcmp(Buffer.data() + Drop, kFrameMagic, Compare) == 0)
        {
            break;
        }
        ++Drop;
    }
    if (Drop > 0)
    {
        Buffer.erase(Buffer.begin(), Buffer.begin() + Drop);
        TotalDroppedBytes += Drop;
    }
}

bool FrameDecoder::Next(std::string& OutPayload)
{
    Resync();
    if (Buffer.size() < 8)
    {
        return false;
    }

    const uint32_t Length =
        static_cast<uint32_t>(Buffer[4])
        | (static_cast<uint32_t>(Buffer[5]) << 8)
        | (static_cast<uint32_t>(Buffer[6]) << 16)
        | (static_cast<uint32_t>(Buffer[7]) << 24);

    if (Length > kMaxPayloadBytes)
    {
        // Corrupt length: skip this magic and try to resync further on.
        Buffer.erase(Buffer.begin(), Buffer.begin() + 4);
        TotalDroppedBytes += 4;
        return Next(OutPayload);
    }

    if (Buffer.size() < 8 + static_cast<size_t>(Length))
    {
        return false; // frame incomplete, wait for more bytes
    }

    OutPayload.assign(reinterpret_cast<const char*>(Buffer.data() + 8), Length);
    Buffer.erase(Buffer.begin(), Buffer.begin() + 8 + Length);
    return true;
}

// ---------------------------------------------------------------------------
// JSON helpers
// ---------------------------------------------------------------------------

JsonValue Vec3ToJson(const Vec3& V)
{
    JsonValue Arr = JsonValue::MakeArray();
    Arr.Add(JsonValue(V.X));
    Arr.Add(JsonValue(V.Y));
    Arr.Add(JsonValue(V.Z));
    return Arr;
}

JsonValue QuatToJson(const Quat& Q)
{
    JsonValue Arr = JsonValue::MakeArray();
    Arr.Add(JsonValue(Q.X));
    Arr.Add(JsonValue(Q.Y));
    Arr.Add(JsonValue(Q.Z));
    Arr.Add(JsonValue(Q.W));
    return Arr;
}

bool Vec3FromJson(const JsonValue& J, Vec3& Out)
{
    if (!J.IsArray() || J.Items().size() != 3)
    {
        return false;
    }
    Out.X = J.Items()[0].AsNumber();
    Out.Y = J.Items()[1].AsNumber();
    Out.Z = J.Items()[2].AsNumber();
    return true;
}

bool QuatFromJson(const JsonValue& J, Quat& Out)
{
    if (!J.IsArray() || J.Items().size() != 4)
    {
        return false;
    }
    Out.X = J.Items()[0].AsNumber();
    Out.Y = J.Items()[1].AsNumber();
    Out.Z = J.Items()[2].AsNumber();
    Out.W = J.Items()[3].AsNumber();
    return true;
}

JsonValue TrajectorySampleToJson(const TrajectorySample& S)
{
    JsonValue Obj = JsonValue::MakeObject();
    Obj.Set("t", JsonValue(S.TimeSeconds));
    Obj.Set("pos", Vec3ToJson(S.Translation));
    Obj.Set("rot", QuatToJson(S.Rotation));
    return Obj;
}

bool TrajectorySampleFromJson(const JsonValue& J, TrajectorySample& Out)
{
    if (!J.IsObject())
    {
        return false;
    }
    Out.TimeSeconds = J.GetNumber("t");
    const JsonValue* Pos = J.Find("pos");
    const JsonValue* Rot = J.Find("rot");
    return Pos && Rot && Vec3FromJson(*Pos, Out.Translation) && QuatFromJson(*Rot, Out.Rotation);
}

namespace
{

JsonValue TimeRangeToJson(const TimeRange& R)
{
    JsonValue Obj = JsonValue::MakeObject();
    Obj.Set("startFrame", JsonValue(R.StartFrame));
    Obj.Set("endFrame", JsonValue(R.EndFrame));
    Obj.Set("fps", JsonValue(R.Fps));
    return Obj;
}

bool TimeRangeFromJson(const JsonValue* J, TimeRange& Out)
{
    if (!J || !J->IsObject())
    {
        return false;
    }
    Out.StartFrame = J->GetNumber("startFrame");
    Out.EndFrame = J->GetNumber("endFrame");
    Out.Fps = J->GetNumber("fps", 30.0);
    return true;
}

JsonValue MakeEnvelope(MessageType Type, JsonValue Payload)
{
    JsonValue Envelope = JsonValue::MakeObject();
    Envelope.Set("type", JsonValue(MessageTypeToString(Type)));
    Envelope.Set("protocolVersion", JsonValue(static_cast<double>(kProtocolVersion)));
    Envelope.Set("payload", std::move(Payload));
    return Envelope;
}

// Parses the envelope, validates type + version, and returns the payload.
bool OpenEnvelope(const std::string& PayloadJson, MessageType Expected,
                  JsonValue& OutRoot, const JsonValue*& OutPayload, std::string& OutError)
{
    if (!JsonValue::Parse(PayloadJson, OutRoot, OutError))
    {
        return false;
    }
    MessageType Type = MessageType::Unknown;
    uint32_t Version = 0;
    // Re-derive from the parsed root to avoid parsing twice.
    Type = MessageTypeFromString(OutRoot.GetString("type"));
    Version = static_cast<uint32_t>(OutRoot.GetNumber("protocolVersion"));

    if (Version != kProtocolVersion)
    {
        OutError = "Protocol version mismatch: got " + std::to_string(Version)
                 + ", expected " + std::to_string(kProtocolVersion);
        return false;
    }
    if (Type != Expected)
    {
        OutError = std::string("Unexpected message type: got '") + OutRoot.GetString("type")
                 + "', expected '" + MessageTypeToString(Expected) + "'";
        return false;
    }
    OutPayload = OutRoot.Find("payload");
    if (!OutPayload || !OutPayload->IsObject())
    {
        OutError = "Missing 'payload' object";
        return false;
    }
    return true;
}

} // anonymous namespace

bool PeekEnvelope(const std::string& PayloadJson, MessageType& OutType,
                  uint32_t& OutVersion, std::string& OutError)
{
    JsonValue Root;
    if (!JsonValue::Parse(PayloadJson, Root, OutError))
    {
        return false;
    }
    OutType = MessageTypeFromString(Root.GetString("type"));
    OutVersion = static_cast<uint32_t>(Root.GetNumber("protocolVersion"));
    return true;
}

// ---------------------------------------------------------------------------
// Handshake
// ---------------------------------------------------------------------------

std::string EncodeHandshake(const HandshakeMessage& Msg)
{
    JsonValue Payload = JsonValue::MakeObject();
    Payload.Set("clientName", JsonValue(Msg.ClientName));
    Payload.Set("characterId", JsonValue(Msg.CharacterId));
    return MakeEnvelope(MessageType::Handshake, std::move(Payload)).Write();
}

bool DecodeHandshake(const std::string& PayloadJson, HandshakeMessage& Out, std::string& OutError)
{
    JsonValue Root;
    const JsonValue* Payload = nullptr;
    if (!OpenEnvelope(PayloadJson, MessageType::Handshake, Root, Payload, OutError))
    {
        return false;
    }
    Out.ClientName = Payload->GetString("clientName");
    Out.CharacterId = Payload->GetString("characterId");
    return true;
}

std::string EncodeHandshakeAck(const HandshakeAckMessage& Msg)
{
    JsonValue Clips = JsonValue::MakeArray();
    for (const std::string& Clip : Msg.KnownClips)
    {
        Clips.Add(JsonValue(Clip));
    }
    JsonValue Payload = JsonValue::MakeObject();
    Payload.Set("serverName", JsonValue(Msg.ServerName));
    Payload.Set("knownClips", std::move(Clips));
    return MakeEnvelope(MessageType::HandshakeAck, std::move(Payload)).Write();
}

bool DecodeHandshakeAck(const std::string& PayloadJson, HandshakeAckMessage& Out, std::string& OutError)
{
    JsonValue Root;
    const JsonValue* Payload = nullptr;
    if (!OpenEnvelope(PayloadJson, MessageType::HandshakeAck, Root, Payload, OutError))
    {
        return false;
    }
    Out.ServerName = Payload->GetString("serverName");
    Out.KnownClips.clear();
    if (const JsonValue* Clips = Payload->Find("knownClips"))
    {
        for (const JsonValue& Clip : Clips->Items())
        {
            if (Clip.IsString())
            {
                Out.KnownClips.push_back(Clip.AsString());
            }
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// EvaluateRequest
// ---------------------------------------------------------------------------

std::string EncodeEvaluateRequest(const EvaluateRequest& Msg)
{
    JsonValue Target = JsonValue::MakeObject();
    Target.Set("name", JsonValue(Msg.Target.Name));
    Target.Set("pos", Vec3ToJson(Msg.Target.Translation));
    Target.Set("rot", QuatToJson(Msg.Target.Rotation));

    JsonValue Samples = JsonValue::MakeArray();
    for (const TrajectorySample& S : Msg.MayaRootSamples)
    {
        Samples.Add(TrajectorySampleToJson(S));
    }

    JsonValue Payload = JsonValue::MakeObject();
    Payload.Set("requestId", JsonValue(Msg.RequestId));
    Payload.Set("characterId", JsonValue(Msg.CharacterId));
    Payload.Set("clipName", JsonValue(Msg.ClipName));
    Payload.Set("range", TimeRangeToJson(Msg.Range));
    Payload.Set("warpWindow", TimeRangeToJson(Msg.WarpWindow));
    Payload.Set("method", JsonValue(WarpMethodToString(Msg.Method)));
    Payload.Set("target", std::move(Target));
    Payload.Set("warpRotation", JsonValue(Msg.bWarpRotation));
    Payload.Set("warpTranslation", JsonValue(Msg.bWarpTranslation));
    Payload.Set("ghostIntervalFrames", JsonValue(Msg.GhostIntervalFrames));
    Payload.Set("mayaRootSamples", std::move(Samples));
    return MakeEnvelope(MessageType::EvaluateRequest, std::move(Payload)).Write();
}

bool DecodeEvaluateRequest(const std::string& PayloadJson, EvaluateRequest& Out, std::string& OutError)
{
    JsonValue Root;
    const JsonValue* Payload = nullptr;
    if (!OpenEnvelope(PayloadJson, MessageType::EvaluateRequest, Root, Payload, OutError))
    {
        return false;
    }

    Out.RequestId = Payload->GetString("requestId");
    Out.CharacterId = Payload->GetString("characterId");
    Out.ClipName = Payload->GetString("clipName");
    Out.Method = WarpMethodFromString(Payload->GetString("method"));
    Out.bWarpRotation = Payload->GetBool("warpRotation", true);
    Out.bWarpTranslation = Payload->GetBool("warpTranslation", true);
    Out.GhostIntervalFrames = Payload->GetNumber("ghostIntervalFrames", 5.0);

    if (Out.RequestId.empty())
    {
        OutError = "EvaluateRequest missing 'requestId'";
        return false;
    }
    if (Out.Method == WarpMethod::Unknown)
    {
        OutError = "EvaluateRequest has unknown warp method '" + Payload->GetString("method") + "'";
        return false;
    }
    if (!TimeRangeFromJson(Payload->Find("range"), Out.Range))
    {
        OutError = "EvaluateRequest missing 'range'";
        return false;
    }
    if (!TimeRangeFromJson(Payload->Find("warpWindow"), Out.WarpWindow))
    {
        Out.WarpWindow = Out.Range; // window defaults to the full range
    }

    const JsonValue* Target = Payload->Find("target");
    if (!Target || !Target->IsObject())
    {
        OutError = "EvaluateRequest missing 'target'";
        return false;
    }
    Out.Target.Name = Target->GetString("name");
    const JsonValue* TargetPos = Target->Find("pos");
    const JsonValue* TargetRot = Target->Find("rot");
    if (!TargetPos || !Vec3FromJson(*TargetPos, Out.Target.Translation)
        || !TargetRot || !QuatFromJson(*TargetRot, Out.Target.Rotation))
    {
        OutError = "EvaluateRequest target transform malformed";
        return false;
    }

    Out.MayaRootSamples.clear();
    if (const JsonValue* Samples = Payload->Find("mayaRootSamples"))
    {
        for (const JsonValue& Item : Samples->Items())
        {
            TrajectorySample S;
            if (!TrajectorySampleFromJson(Item, S))
            {
                OutError = "EvaluateRequest root sample malformed";
                return false;
            }
            Out.MayaRootSamples.push_back(S);
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// EvaluateProgress
// ---------------------------------------------------------------------------

std::string EncodeEvaluateProgress(const EvaluateProgress& Msg)
{
    JsonValue Payload = JsonValue::MakeObject();
    Payload.Set("requestId", JsonValue(Msg.RequestId));
    Payload.Set("progress", JsonValue(Msg.Progress));
    Payload.Set("stage", JsonValue(Msg.Stage));
    return MakeEnvelope(MessageType::EvaluateProgress, std::move(Payload)).Write();
}

bool DecodeEvaluateProgress(const std::string& PayloadJson, EvaluateProgress& Out, std::string& OutError)
{
    JsonValue Root;
    const JsonValue* Payload = nullptr;
    if (!OpenEnvelope(PayloadJson, MessageType::EvaluateProgress, Root, Payload, OutError))
    {
        return false;
    }
    Out.RequestId = Payload->GetString("requestId");
    Out.Progress = Payload->GetNumber("progress");
    Out.Stage = Payload->GetString("stage");
    return true;
}

// ---------------------------------------------------------------------------
// EvaluateResult
// ---------------------------------------------------------------------------

std::string EncodeEvaluateResult(const EvaluateResult& Msg)
{
    JsonValue Warped = JsonValue::MakeArray();
    for (const TrajectorySample& S : Msg.WarpedTrajectory)
    {
        Warped.Add(TrajectorySampleToJson(S));
    }
    JsonValue Original = JsonValue::MakeArray();
    for (const TrajectorySample& S : Msg.OriginalTrajectory)
    {
        Original.Add(TrajectorySampleToJson(S));
    }

    JsonValue Ghosts = JsonValue::MakeArray();
    for (const GhostPose& Pose : Msg.GhostPoses)
    {
        JsonValue Joints = JsonValue::MakeArray();
        for (const JointTransform& Joint : Pose.Joints)
        {
            JsonValue J = JsonValue::MakeObject();
            J.Set("name", JsonValue(Joint.JointName));
            J.Set("pos", Vec3ToJson(Joint.Translation));
            J.Set("rot", QuatToJson(Joint.Rotation));
            Joints.Add(std::move(J));
        }
        JsonValue PoseObj = JsonValue::MakeObject();
        PoseObj.Set("t", JsonValue(Pose.TimeSeconds));
        PoseObj.Set("joints", std::move(Joints));
        Ghosts.Add(std::move(PoseObj));
    }

    JsonValue WarningsArr = JsonValue::MakeArray();
    for (const std::string& Warning : Msg.Warnings)
    {
        WarningsArr.Add(JsonValue(Warning));
    }

    JsonValue Payload = JsonValue::MakeObject();
    Payload.Set("requestId", JsonValue(Msg.RequestId));
    Payload.Set("success", JsonValue(Msg.bSuccess));
    Payload.Set("errorMessage", JsonValue(Msg.ErrorMessage));
    Payload.Set("warpedTrajectory", std::move(Warped));
    Payload.Set("originalTrajectory", std::move(Original));
    Payload.Set("ghostPoses", std::move(Ghosts));
    Payload.Set("warnings", std::move(WarningsArr));
    Payload.Set("evaluationMs", JsonValue(Msg.EvaluationMs));
    return MakeEnvelope(MessageType::EvaluateResult, std::move(Payload)).Write();
}

bool DecodeEvaluateResult(const std::string& PayloadJson, EvaluateResult& Out, std::string& OutError)
{
    JsonValue Root;
    const JsonValue* Payload = nullptr;
    if (!OpenEnvelope(PayloadJson, MessageType::EvaluateResult, Root, Payload, OutError))
    {
        return false;
    }

    Out.RequestId = Payload->GetString("requestId");
    Out.bSuccess = Payload->GetBool("success");
    Out.ErrorMessage = Payload->GetString("errorMessage");
    Out.EvaluationMs = Payload->GetNumber("evaluationMs");

    auto ReadTrajectory = [&](const char* Key, std::vector<TrajectorySample>& OutSamples) -> bool
    {
        OutSamples.clear();
        const JsonValue* Arr = Payload->Find(Key);
        if (!Arr)
        {
            return true; // optional
        }
        for (const JsonValue& Item : Arr->Items())
        {
            TrajectorySample S;
            if (!TrajectorySampleFromJson(Item, S))
            {
                OutError = std::string("EvaluateResult '") + Key + "' sample malformed";
                return false;
            }
            OutSamples.push_back(S);
        }
        return true;
    };

    if (!ReadTrajectory("warpedTrajectory", Out.WarpedTrajectory)) return false;
    if (!ReadTrajectory("originalTrajectory", Out.OriginalTrajectory)) return false;

    Out.GhostPoses.clear();
    if (const JsonValue* Ghosts = Payload->Find("ghostPoses"))
    {
        for (const JsonValue& PoseJson : Ghosts->Items())
        {
            GhostPose Pose;
            Pose.TimeSeconds = PoseJson.GetNumber("t");
            if (const JsonValue* Joints = PoseJson.Find("joints"))
            {
                for (const JsonValue& JointJson : Joints->Items())
                {
                    JointTransform Joint;
                    Joint.JointName = JointJson.GetString("name");
                    const JsonValue* Pos = JointJson.Find("pos");
                    const JsonValue* Rot = JointJson.Find("rot");
                    if (!Pos || !Vec3FromJson(*Pos, Joint.Translation)
                        || !Rot || !QuatFromJson(*Rot, Joint.Rotation))
                    {
                        OutError = "EvaluateResult ghost joint malformed";
                        return false;
                    }
                    Pose.Joints.push_back(std::move(Joint));
                }
            }
            Out.GhostPoses.push_back(std::move(Pose));
        }
    }

    Out.Warnings.clear();
    if (const JsonValue* WarningsArr = Payload->Find("warnings"))
    {
        for (const JsonValue& Warning : WarningsArr->Items())
        {
            if (Warning.IsString())
            {
                Out.Warnings.push_back(Warning.AsString());
            }
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Error
// ---------------------------------------------------------------------------

std::string EncodeError(const ErrorMessage& Msg)
{
    JsonValue Payload = JsonValue::MakeObject();
    Payload.Set("requestId", JsonValue(Msg.RequestId));
    Payload.Set("message", JsonValue(Msg.Message));
    return MakeEnvelope(MessageType::Error, std::move(Payload)).Write();
}

bool DecodeError(const std::string& PayloadJson, ErrorMessage& Out, std::string& OutError)
{
    JsonValue Root;
    const JsonValue* Payload = nullptr;
    if (!OpenEnvelope(PayloadJson, MessageType::Error, Root, Payload, OutError))
    {
        return false;
    }
    Out.RequestId = Payload->GetString("requestId");
    Out.Message = Payload->GetString("message");
    return true;
}

} // namespace WarpViz
} // namespace AnimForge
