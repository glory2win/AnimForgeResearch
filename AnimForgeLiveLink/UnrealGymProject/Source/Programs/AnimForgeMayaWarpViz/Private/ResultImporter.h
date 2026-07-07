// AnimForgeMayaWarpViz - ResultImporter.h
//
// Builds the Maya-side visualization of an EvaluateResult:
//   * an anim layer "AnimForgeWarpViz_Result" so the import never touches the
//     animator's keys,
//   * a NURBS curve through the warped root trajectory (and a second, dimmed
//     curve for the original trajectory for side-by-side comparison),
//   * a locator per ghost pose joint, keyed at the ghost's time, grouped under
//     one "AnimForgeWarpViz_Ghosts" transform.
//
// Everything lands under a single "AnimForgeWarpViz_Result_grp" root so a
// re-evaluate can wipe and rebuild it in one shot.

#pragma once

#include "WarpVizProtocol.h"

#include <string>

namespace AnimForge
{
namespace WarpViz
{

class ResultImporter
{
public:
    // Frame the result was evaluated from; used to convert result seconds back
    // to Maya frames when keying.
    struct ImportContext
    {
        double StartFrame = 0.0;
        double Fps = 30.0;
    };

    // Imports the result into the scene. Must run on the Maya main thread.
    // Returns false with OutError set when scene edits fail.
    static bool Import(const EvaluateResult& Result, const ImportContext& Context,
                       std::string& OutError);

    // Removes a previous result group if present (called before re-import).
    static void ClearPrevious();

    // The command records the frame context of the in-flight request here so
    // the async result (arriving via the idle callback) can key at the right
    // frames. Single in-flight request by design: a new Evaluate supersedes.
    static void SetPendingContext(const ImportContext& Context);
    static ImportContext GetPendingContext();

    static const char* ResultGroupName() { return "AnimForgeWarpViz_Result_grp"; }
    static const char* AnimLayerName() { return "AnimForgeWarpViz_Result"; }
};

} // namespace WarpViz
} // namespace AnimForge
