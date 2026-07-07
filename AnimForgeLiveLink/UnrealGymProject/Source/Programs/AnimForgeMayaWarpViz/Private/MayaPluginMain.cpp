// AnimForgeMayaWarpViz - MayaPluginMain.cpp
//
// Plugin entry point. Registers the `animForgeWarpViz` command and an idle
// callback that drains the network client's message queue on the Maya main
// thread - gym results arrive on a socket thread, but all scene edits (anim
// layer, trajectory curves, ghost locators) must happen here.

#include "ResultImporter.h"
#include "WarpVizClient.h"
#include "WarpVizCommand.h"

#include <maya/MEventMessage.h>
#include <maya/MFnPlugin.h>
#include <maya/MGlobal.h>

using namespace AnimForge::WarpViz;

namespace
{

MCallbackId GIdleCallbackId = 0;

void OnIdle(void* /*ClientData*/)
{
    WarpVizClient::Get().DrainMainThread();
}

void HandleResult(const EvaluateResult& Result)
{
    std::string Error;
    if (!Result.bSuccess)
    {
        MGlobal::displayError(MString(
            ("[AnimForgeWarpViz] Gym evaluation failed: " + Result.ErrorMessage).c_str()));
        return;
    }
    if (!ResultImporter::Import(Result, ResultImporter::GetPendingContext(), Error))
    {
        MGlobal::displayError(MString(
            ("[AnimForgeWarpViz] Result import failed: " + Error).c_str()));
    }
}

void HandleProgress(const EvaluateProgress& Progress)
{
    MGlobal::displayInfo(MString(
        ("[AnimForgeWarpViz] Gym: " + Progress.Stage + " ("
         + std::to_string(static_cast<int>(Progress.Progress * 100.0)) + "%)").c_str()));
}

void HandleError(const std::string& Message)
{
    MGlobal::displayError(MString(("[AnimForgeWarpViz] " + Message).c_str()));
}

} // anonymous namespace

MStatus initializePlugin(MObject Obj)
{
    MFnPlugin Plugin(Obj, "AnimForge Studios", "1.0", "Any");

    MStatus Status = Plugin.registerCommand(
        WarpVizCommand::CommandName(), WarpVizCommand::Creator, WarpVizCommand::CreateSyntax);
    if (Status != MS::kSuccess)
    {
        Status.perror("registerCommand animForgeWarpViz");
        return Status;
    }

    WarpVizClient::Get().SetHandlers(&HandleResult, &HandleProgress, &HandleError);

    GIdleCallbackId = MEventMessage::addEventCallback("idle", &OnIdle, nullptr, &Status);
    if (Status != MS::kSuccess)
    {
        Status.perror("addEventCallback idle");
        Plugin.deregisterCommand(WarpVizCommand::CommandName());
        return Status;
    }

    MGlobal::displayInfo("[AnimForgeWarpViz] Plugin loaded. Command: animForgeWarpViz");
    return MS::kSuccess;
}

MStatus uninitializePlugin(MObject Obj)
{
    MFnPlugin Plugin(Obj);

    WarpVizClient::Get().Disconnect();

    if (GIdleCallbackId != 0)
    {
        MEventMessage::removeCallback(GIdleCallbackId);
        GIdleCallbackId = 0;
    }

    return Plugin.deregisterCommand(WarpVizCommand::CommandName());
}
