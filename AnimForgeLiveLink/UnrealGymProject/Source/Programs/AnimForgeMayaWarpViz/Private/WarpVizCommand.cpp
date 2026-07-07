// AnimForgeMayaWarpViz - WarpVizCommand.cpp

#include "WarpVizCommand.h"

#include "MayaSceneExtractor.h"
#include "ResultImporter.h"
#include "WarpVizClient.h"

#include <maya/MArgList.h>
#include <maya/MGlobal.h>
#include <maya/MString.h>

#include <random>
#include <sstream>

namespace AnimForge
{
namespace WarpViz
{

namespace
{

// Flag names (short, long).
constexpr char kConnectFlag[] = "-c",   kConnectFlagLong[] = "-connect";
constexpr char kDisconnectFlag[] = "-d", kDisconnectFlagLong[] = "-disconnect";
constexpr char kEvaluateFlag[] = "-e",  kEvaluateFlagLong[] = "-evaluate";
constexpr char kStatusFlag[] = "-s",    kStatusFlagLong[] = "-status";
constexpr char kHostFlag[] = "-h",      kHostFlagLong[] = "-host";
constexpr char kPortFlag[] = "-p",      kPortFlagLong[] = "-port";
constexpr char kCharacterFlag[] = "-ch", kCharacterFlagLong[] = "-characterId";
constexpr char kClipFlag[] = "-cl",     kClipFlagLong[] = "-clipName";
constexpr char kRootJointFlag[] = "-rj", kRootJointFlagLong[] = "-rootJoint";
constexpr char kStartFrameFlag[] = "-sf", kStartFrameFlagLong[] = "-startFrame";
constexpr char kEndFrameFlag[] = "-ef",  kEndFrameFlagLong[] = "-endFrame";
constexpr char kMethodFlag[] = "-m",    kMethodFlagLong[] = "-method";
constexpr char kWarpTargetFlag[] = "-wt", kWarpTargetFlagLong[] = "-warpTarget";
constexpr char kWarpRotationFlag[] = "-wr", kWarpRotationFlagLong[] = "-warpRotation";
constexpr char kWarpTranslationFlag[] = "-wtr", kWarpTranslationFlagLong[] = "-warpTranslation";
constexpr char kGhostIntervalFlag[] = "-gi", kGhostIntervalFlagLong[] = "-ghostInterval";
constexpr char kWindowStartFlag[] = "-ws", kWindowStartFlagLong[] = "-windowStart";
constexpr char kWindowEndFlag[] = "-we",   kWindowEndFlagLong[] = "-windowEnd";

std::string MintRequestId()
{
    static std::mt19937_64 Rng{ std::random_device{}() };
    std::ostringstream Stream;
    Stream << std::hex << Rng() << Rng();
    return Stream.str();
}

std::string GetStringArg(const MArgDatabase& ArgData, const char* Flag,
                         const std::string& Default = std::string())
{
    if (!ArgData.isFlagSet(Flag))
    {
        return Default;
    }
    MString Value;
    ArgData.getFlagArgument(Flag, 0, Value);
    return Value.asChar();
}

double GetDoubleArg(const MArgDatabase& ArgData, const char* Flag, double Default)
{
    if (!ArgData.isFlagSet(Flag))
    {
        return Default;
    }
    double Value = Default;
    ArgData.getFlagArgument(Flag, 0, Value);
    return Value;
}

int GetIntArg(const MArgDatabase& ArgData, const char* Flag, int Default)
{
    if (!ArgData.isFlagSet(Flag))
    {
        return Default;
    }
    int Value = Default;
    ArgData.getFlagArgument(Flag, 0, Value);
    return Value;
}

} // anonymous namespace

MSyntax WarpVizCommand::CreateSyntax()
{
    MSyntax Syntax;
    Syntax.addFlag(kConnectFlag, kConnectFlagLong);
    Syntax.addFlag(kDisconnectFlag, kDisconnectFlagLong);
    Syntax.addFlag(kEvaluateFlag, kEvaluateFlagLong);
    Syntax.addFlag(kStatusFlag, kStatusFlagLong);
    Syntax.addFlag(kHostFlag, kHostFlagLong, MSyntax::kString);
    Syntax.addFlag(kPortFlag, kPortFlagLong, MSyntax::kLong);
    Syntax.addFlag(kCharacterFlag, kCharacterFlagLong, MSyntax::kString);
    Syntax.addFlag(kClipFlag, kClipFlagLong, MSyntax::kString);
    Syntax.addFlag(kRootJointFlag, kRootJointFlagLong, MSyntax::kString);
    Syntax.addFlag(kStartFrameFlag, kStartFrameFlagLong, MSyntax::kDouble);
    Syntax.addFlag(kEndFrameFlag, kEndFrameFlagLong, MSyntax::kDouble);
    Syntax.addFlag(kMethodFlag, kMethodFlagLong, MSyntax::kString);
    Syntax.addFlag(kWarpTargetFlag, kWarpTargetFlagLong, MSyntax::kString);
    Syntax.addFlag(kWarpRotationFlag, kWarpRotationFlagLong, MSyntax::kLong);
    Syntax.addFlag(kWarpTranslationFlag, kWarpTranslationFlagLong, MSyntax::kLong);
    Syntax.addFlag(kGhostIntervalFlag, kGhostIntervalFlagLong, MSyntax::kDouble);
    Syntax.addFlag(kWindowStartFlag, kWindowStartFlagLong, MSyntax::kDouble);
    Syntax.addFlag(kWindowEndFlag, kWindowEndFlagLong, MSyntax::kDouble);
    Syntax.enableQuery(false);
    Syntax.enableEdit(false);
    return Syntax;
}

MStatus WarpVizCommand::doIt(const MArgList& Args)
{
    MStatus Status;
    MArgDatabase ArgData(syntax(), Args, &Status);
    if (Status != MS::kSuccess)
    {
        return Status;
    }

    if (ArgData.isFlagSet(kConnectFlag))
    {
        return DoConnect(ArgData);
    }
    if (ArgData.isFlagSet(kDisconnectFlag))
    {
        return DoDisconnect();
    }
    if (ArgData.isFlagSet(kEvaluateFlag))
    {
        return DoEvaluate(ArgData);
    }
    if (ArgData.isFlagSet(kStatusFlag))
    {
        return DoStatus();
    }

    displayError("animForgeWarpViz: one of -connect, -disconnect, -evaluate, -status is required.");
    return MS::kFailure;
}

MStatus WarpVizCommand::DoConnect(const MArgDatabase& ArgData)
{
    const std::string Host = GetStringArg(ArgData, kHostFlag, "127.0.0.1");
    const int Port = GetIntArg(ArgData, kPortFlag, kDefaultPort);
    const std::string CharacterId = GetStringArg(ArgData, kCharacterFlag);

    std::string Error;
    if (!WarpVizClient::Get().Connect(Host, static_cast<uint16_t>(Port), CharacterId, Error))
    {
        displayError(MString(("animForgeWarpViz: " + Error).c_str()));
        return MS::kFailure;
    }
    MGlobal::displayInfo(MString(("[AnimForgeWarpViz] Connected to gym at "
        + Host + ":" + std::to_string(Port) + ".").c_str()));
    return MS::kSuccess;
}

MStatus WarpVizCommand::DoDisconnect()
{
    WarpVizClient::Get().Disconnect();
    MGlobal::displayInfo("[AnimForgeWarpViz] Disconnected.");
    return MS::kSuccess;
}

MStatus WarpVizCommand::DoStatus()
{
    WarpVizClient& Client = WarpVizClient::Get();
    std::string Message = std::string("[AnimForgeWarpViz] ")
        + (Client.IsConnected() ? "Connected." : "Not connected.");
    if (!Client.GetKnownClips().empty())
    {
        Message += " Gym clips:";
        for (const std::string& Clip : Client.GetKnownClips())
        {
            Message += " " + Clip;
        }
    }
    MGlobal::displayInfo(MString(Message.c_str()));
    setResult(Client.IsConnected());
    return MS::kSuccess;
}

MStatus WarpVizCommand::DoEvaluate(const MArgDatabase& ArgData)
{
    WarpVizClient& Client = WarpVizClient::Get();

    // Lazy connect so a single -evaluate works without a prior -connect.
    if (!Client.IsConnected())
    {
        const std::string Host = GetStringArg(ArgData, kHostFlag, "127.0.0.1");
        const int Port = GetIntArg(ArgData, kPortFlag, kDefaultPort);
        std::string ConnectError;
        if (!Client.Connect(Host, static_cast<uint16_t>(Port),
                            GetStringArg(ArgData, kCharacterFlag), ConnectError))
        {
            displayError(MString(("animForgeWarpViz: " + ConnectError).c_str()));
            return MS::kFailure;
        }
    }

    EvaluateRequest Request;
    Request.RequestId = MintRequestId();
    Request.CharacterId = GetStringArg(ArgData, kCharacterFlag);
    Request.ClipName = GetStringArg(ArgData, kClipFlag);
    Request.Method = WarpMethodFromString(GetStringArg(ArgData, kMethodFlag, "SkewWarp"));
    Request.bWarpRotation = GetIntArg(ArgData, kWarpRotationFlag, 1) != 0;
    Request.bWarpTranslation = GetIntArg(ArgData, kWarpTranslationFlag, 1) != 0;
    Request.GhostIntervalFrames = GetDoubleArg(ArgData, kGhostIntervalFlag, 5.0);

    if (Request.Method == WarpMethod::Unknown)
    {
        displayError("animForgeWarpViz: unknown -method (use SkewWarp, SimpleWarp or Scale).");
        return MS::kFailure;
    }
    if (Request.ClipName.empty())
    {
        displayError("animForgeWarpViz: -clipName is required for -evaluate.");
        return MS::kFailure;
    }

    const double Fps = MayaSceneExtractor::GetSceneFps();
    Request.Range.StartFrame = GetDoubleArg(ArgData, kStartFrameFlag, 0.0);
    Request.Range.EndFrame = GetDoubleArg(ArgData, kEndFrameFlag, 0.0);
    Request.Range.Fps = Fps;
    if (Request.Range.EndFrame <= Request.Range.StartFrame)
    {
        displayError("animForgeWarpViz: -endFrame must be greater than -startFrame.");
        return MS::kFailure;
    }

    Request.WarpWindow = Request.Range;
    if (ArgData.isFlagSet(kWindowStartFlag) && ArgData.isFlagSet(kWindowEndFlag))
    {
        Request.WarpWindow.StartFrame = GetDoubleArg(ArgData, kWindowStartFlag, 0.0);
        Request.WarpWindow.EndFrame = GetDoubleArg(ArgData, kWindowEndFlag, 0.0);
        Request.WarpWindow.Fps = Fps;
    }

    // --- scene extraction -------------------------------------------------
    std::string Error;
    const std::string TargetLocator = GetStringArg(ArgData, kWarpTargetFlag);
    if (TargetLocator.empty())
    {
        displayError("animForgeWarpViz: -warpTarget is required for -evaluate.");
        return MS::kFailure;
    }
    if (!MayaSceneExtractor::ExtractWorldTransform(TargetLocator, Request.Target, Error))
    {
        displayError(MString(("animForgeWarpViz: " + Error).c_str()));
        return MS::kFailure;
    }

    const std::string RootJoint = GetStringArg(ArgData, kRootJointFlag, "root");
    if (!MayaSceneExtractor::SampleRootTrajectory(
            RootJoint, Request.Range.StartFrame, Request.Range.EndFrame, Fps,
            Request.MayaRootSamples, Error))
    {
        displayError(MString(("animForgeWarpViz: root sampling failed (" + Error
            + "). Pass -rootJoint <joint> if the root is not named 'root'.").c_str()));
        return MS::kFailure;
    }

    // --- ship it -----------------------------------------------------------
    ResultImporter::ImportContext Context;
    Context.StartFrame = Request.Range.StartFrame;
    Context.Fps = Fps;
    ResultImporter::SetPendingContext(Context);

    if (!Client.SendEvaluateRequest(Request, Error))
    {
        displayError(MString(("animForgeWarpViz: " + Error).c_str()));
        return MS::kFailure;
    }

    MGlobal::displayInfo(MString(("[AnimForgeWarpViz] Evaluate request "
        + Request.RequestId.substr(0, 8) + " sent ("
        + WarpMethodToString(Request.Method) + ", frames "
        + std::to_string(Request.Range.StartFrame) + "-"
        + std::to_string(Request.Range.EndFrame)
        + "). Result imports when the gym answers.").c_str()));
    setResult(MString(Request.RequestId.c_str()));
    return MS::kSuccess;
}

} // namespace WarpViz
} // namespace AnimForge
