// AnimForgeMayaWarpViz - ResultImporter.cpp

#include "ResultImporter.h"

#include <maya/MGlobal.h>
#include <maya/MString.h>

#include <cstdio>
#include <string>

namespace AnimForge
{
namespace WarpViz
{

namespace
{

std::string FormatDouble(double Value)
{
    char Buffer[32];
    std::snprintf(Buffer, sizeof(Buffer), "%.6f", Value);
    return Buffer;
}

// Builds `curve -d 3 -p x y z -p ...` through the trajectory translations.
std::string BuildCurveCommand(const std::vector<TrajectorySample>& Samples,
                              const std::string& CurveName)
{
    std::string Command = "curve -d 3 -name \"" + CurveName + "\"";
    for (const TrajectorySample& Sample : Samples)
    {
        Command += " -p " + FormatDouble(Sample.Translation.X)
                 + " " + FormatDouble(Sample.Translation.Y)
                 + " " + FormatDouble(Sample.Translation.Z);
    }
    Command += ";";
    return Command;
}

bool Run(const std::string& MelCommand, std::string& OutError)
{
    if (MGlobal::executeCommand(MString(MelCommand.c_str())) != MS::kSuccess)
    {
        OutError = "MEL command failed: " + MelCommand.substr(0, 200);
        return false;
    }
    return true;
}

ResultImporter::ImportContext GPendingContext; // main thread only

} // anonymous namespace

void ResultImporter::SetPendingContext(const ImportContext& Context)
{
    GPendingContext = Context;
}

ResultImporter::ImportContext ResultImporter::GetPendingContext()
{
    return GPendingContext;
}

void ResultImporter::ClearPrevious()
{
    // Silently ignore failures: nothing to clear on the first import.
    MGlobal::executeCommand(MString(
        "if (`objExists \"AnimForgeWarpViz_Result_grp\"`) delete \"AnimForgeWarpViz_Result_grp\";"));
    MGlobal::executeCommand(MString(
        "if (`animLayer -query -exists \"AnimForgeWarpViz_Result\"`) "
        "delete \"AnimForgeWarpViz_Result\";"));
}

bool ResultImporter::Import(const EvaluateResult& Result, const ImportContext& Context,
                            std::string& OutError)
{
    if (!Result.bSuccess)
    {
        OutError = "Result marked failed: " + Result.ErrorMessage;
        return false;
    }
    if (Result.WarpedTrajectory.empty())
    {
        OutError = "Result contains no trajectory samples.";
        return false;
    }

    ClearPrevious();

    if (!Run(std::string("group -empty -name \"") + ResultGroupName() + "\";", OutError))
    {
        return false;
    }

    // Dedicated anim layer so ghost/trajectory keys never touch animator keys.
    if (!Run(std::string("animLayer \"") + AnimLayerName() + "\";", OutError))
    {
        return false;
    }

    // --- warped trajectory curve (green) --------------------------------
    if (!Run(BuildCurveCommand(Result.WarpedTrajectory, "AnimForgeWarpViz_WarpedTrajectory"),
             OutError))
    {
        return false;
    }
    Run("parent \"AnimForgeWarpViz_WarpedTrajectory\" \"" + std::string(ResultGroupName()) + "\";",
        OutError);
    Run("setAttr \"AnimForgeWarpViz_WarpedTrajectory.overrideEnabled\" 1; "
        "setAttr \"AnimForgeWarpViz_WarpedTrajectory.overrideColor\" 14;", OutError);

    // --- original trajectory curve (dim gray), when provided -------------
    if (Result.OriginalTrajectory.size() >= 2)
    {
        if (!Run(BuildCurveCommand(Result.OriginalTrajectory, "AnimForgeWarpViz_OriginalTrajectory"),
                 OutError))
        {
            return false;
        }
        Run("parent \"AnimForgeWarpViz_OriginalTrajectory\" \""
            + std::string(ResultGroupName()) + "\";", OutError);
        Run("setAttr \"AnimForgeWarpViz_OriginalTrajectory.overrideEnabled\" 1; "
            "setAttr \"AnimForgeWarpViz_OriginalTrajectory.overrideColor\" 3; "
            "setAttr \"AnimForgeWarpViz_OriginalTrajectory.template\" 1;", OutError);
    }

    // --- ghost poses ------------------------------------------------------
    if (!Result.GhostPoses.empty())
    {
        Run("group -empty -name \"AnimForgeWarpViz_Ghosts\"; "
            "parent \"AnimForgeWarpViz_Ghosts\" \"" + std::string(ResultGroupName()) + "\";",
            OutError);

        int GhostIndex = 0;
        for (const GhostPose& Pose : Result.GhostPoses)
        {
            const double Frame = Context.StartFrame + Pose.TimeSeconds * Context.Fps;
            for (const JointTransform& Joint : Pose.Joints)
            {
                const std::string LocatorName =
                    "AFWV_ghost" + std::to_string(GhostIndex) + "_" + Joint.JointName;

                std::string Command =
                    "spaceLocator -name \"" + LocatorName + "\"; "
                    "parent \"" + LocatorName + "\" \"AnimForgeWarpViz_Ghosts\"; "
                    "setAttr \"" + LocatorName + ".translate\" "
                        + FormatDouble(Joint.Translation.X) + " "
                        + FormatDouble(Joint.Translation.Y) + " "
                        + FormatDouble(Joint.Translation.Z) + "; ";
                // Tag the ghost with its source frame for the outliner and key
                // its visibility on the anim layer so scrubbing reveals ghosts
                // as the character passes them.
                Command +=
                    "addAttr -longName \"afwvFrame\" -attributeType \"double\" \"" + LocatorName + "\"; "
                    "setAttr \"" + LocatorName + ".afwvFrame\" " + FormatDouble(Frame) + "; "
                    "setKeyframe -animLayer \"" + std::string(AnimLayerName()) + "\" "
                        "-attribute \"visibility\" -time " + FormatDouble(Frame) + " -value 1 \""
                        + LocatorName + "\";";
                if (!Run(Command, OutError))
                {
                    return false;
                }
                ++GhostIndex;
            }
        }
    }

    // Surface gym warnings in the script editor where animators expect them.
    for (const std::string& Warning : Result.Warnings)
    {
        MGlobal::displayWarning(MString(("[AnimForgeWarpViz] " + Warning).c_str()));
    }

    MGlobal::displayInfo(MString(("[AnimForgeWarpViz] Imported gym result: "
        + std::to_string(Result.WarpedTrajectory.size()) + " trajectory samples, "
        + std::to_string(Result.GhostPoses.size()) + " ghost poses (evaluated in "
        + std::to_string(Result.EvaluationMs) + " ms).").c_str()));
    return true;
}

} // namespace WarpViz
} // namespace AnimForge
