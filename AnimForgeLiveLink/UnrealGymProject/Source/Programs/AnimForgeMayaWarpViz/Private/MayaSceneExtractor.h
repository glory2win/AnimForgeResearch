// AnimForgeMayaWarpViz - MayaSceneExtractor.h
//
// Reads what the EvaluateRequest needs out of the open Maya scene:
//   * the warp target locator's world transform,
//   * the character root joint's world trajectory over the frame range.

#pragma once

#include "WarpVizProtocol.h"

#include <string>
#include <vector>

namespace AnimForge
{
namespace WarpViz
{

class MayaSceneExtractor
{
public:
    // World translation + rotation of a transform (the warp target locator).
    // Returns false with OutError set when the node is missing or ambiguous.
    static bool ExtractWorldTransform(const std::string& NodeName,
                                      WarpTarget& OutTarget,
                                      std::string& OutError);

    // Samples the root joint's world transform once per frame over
    // [StartFrame, EndFrame] by stepping scene time (restored afterwards).
    // Times in the output are seconds relative to StartFrame.
    static bool SampleRootTrajectory(const std::string& RootJointName,
                                     double StartFrame, double EndFrame, double Fps,
                                     std::vector<TrajectorySample>& OutSamples,
                                     std::string& OutError);

    // Current scene FPS derived from Maya's time unit.
    static double GetSceneFps();
};

} // namespace WarpViz
} // namespace AnimForge
