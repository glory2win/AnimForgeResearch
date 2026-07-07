// AnimForgeMayaWarpViz - MayaSceneExtractor.cpp

#include "MayaSceneExtractor.h"

#include <maya/MAnimControl.h>
#include <maya/MDagPath.h>
#include <maya/MEulerRotation.h>
#include <maya/MFnTransform.h>
#include <maya/MGlobal.h>
#include <maya/MMatrix.h>
#include <maya/MQuaternion.h>
#include <maya/MSelectionList.h>
#include <maya/MTime.h>
#include <maya/MTransformationMatrix.h>
#include <maya/MVector.h>

namespace AnimForge
{
namespace WarpViz
{

namespace
{

bool ResolveDagPath(const std::string& NodeName, MDagPath& OutPath, std::string& OutError)
{
    MSelectionList Selection;
    if (Selection.add(MString(NodeName.c_str())) != MS::kSuccess)
    {
        OutError = "Node '" + NodeName + "' was not found in the scene.";
        return false;
    }
    if (Selection.length() != 1)
    {
        OutError = "Node name '" + NodeName + "' is ambiguous; use a full DAG path.";
        return false;
    }
    if (Selection.getDagPath(0, OutPath) != MS::kSuccess)
    {
        OutError = "Node '" + NodeName + "' is not a DAG node.";
        return false;
    }
    return true;
}

void WorldTransformFromDagPath(const MDagPath& Path, Vec3& OutTranslation, Quat& OutRotation)
{
    const MMatrix World = Path.inclusiveMatrix();
    const MTransformationMatrix Transform(World);

    const MVector Translation = Transform.getTranslation(MSpace::kWorld);
    OutTranslation = Vec3(Translation.x, Translation.y, Translation.z);

    const MQuaternion Rotation = Transform.rotation();
    OutRotation = Quat(Rotation.x, Rotation.y, Rotation.z, Rotation.w).Normalized();
}

} // anonymous namespace

double MayaSceneExtractor::GetSceneFps()
{
    const MTime OneSecond(1.0, MTime::kSeconds);
    return OneSecond.as(MTime::uiUnit());
}

bool MayaSceneExtractor::ExtractWorldTransform(const std::string& NodeName,
                                               WarpTarget& OutTarget,
                                               std::string& OutError)
{
    MDagPath Path;
    if (!ResolveDagPath(NodeName, Path, OutError))
    {
        return false;
    }
    OutTarget.Name = NodeName;
    WorldTransformFromDagPath(Path, OutTarget.Translation, OutTarget.Rotation);
    return true;
}

bool MayaSceneExtractor::SampleRootTrajectory(const std::string& RootJointName,
                                              double StartFrame, double EndFrame, double Fps,
                                              std::vector<TrajectorySample>& OutSamples,
                                              std::string& OutError)
{
    OutSamples.clear();
    if (EndFrame <= StartFrame)
    {
        OutError = "End frame must be greater than start frame.";
        return false;
    }
    if (Fps <= 0.0)
    {
        OutError = "FPS must be positive.";
        return false;
    }

    MDagPath RootPath;
    if (!ResolveDagPath(RootJointName, RootPath, OutError))
    {
        return false;
    }

    // Step the scene one frame at a time so every deformer/constraint in the
    // rig evaluates exactly as it does on playback, then restore the time.
    const MTime OriginalTime = MAnimControl::currentTime();

    const int FrameCount = static_cast<int>(EndFrame - StartFrame) + 1;
    OutSamples.reserve(static_cast<size_t>(FrameCount));

    for (int i = 0; i < FrameCount; ++i)
    {
        const double Frame = StartFrame + static_cast<double>(i);
        MAnimControl::setCurrentTime(MTime(Frame, MTime::uiUnit()));

        TrajectorySample Sample;
        Sample.TimeSeconds = static_cast<double>(i) / Fps;
        WorldTransformFromDagPath(RootPath, Sample.Translation, Sample.Rotation);
        OutSamples.push_back(Sample);
    }

    MAnimControl::setCurrentTime(OriginalTime);

    if (OutSamples.empty())
    {
        OutError = "No frames sampled in range.";
        return false;
    }
    return true;
}

} // namespace WarpViz
} // namespace AnimForge
