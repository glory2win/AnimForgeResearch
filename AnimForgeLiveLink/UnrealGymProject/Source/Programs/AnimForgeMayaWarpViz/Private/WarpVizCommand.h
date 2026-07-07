// AnimForgeMayaWarpViz - WarpVizCommand.h
//
// The `animForgeWarpViz` MPxCommand - the single entry point the PySide UI
// (and any MEL/Python tooling) drives:
//
//   animForgeWarpViz -connect -host "127.0.0.1" -port 46464 -characterId "AF_Mannequin";
//   animForgeWarpViz -evaluate
//       -host "127.0.0.1" -port 46464
//       -characterId "AF_Mannequin" -clipName "MM_Vault_Low"
//       -startFrame 1 -endFrame 60
//       -method "SkewWarp" -warpTarget "warpTarget_loc"
//       [-rootJoint "root"] [-windowStart 10 -windowEnd 50]
//       [-warpRotation 1] [-warpTranslation 1] [-ghostInterval 5];
//   animForgeWarpViz -status;      // prints connection state + known clips
//   animForgeWarpViz -disconnect;

#pragma once

#include <maya/MArgDatabase.h>
#include <maya/MPxCommand.h>
#include <maya/MSyntax.h>

namespace AnimForge
{
namespace WarpViz
{

class WarpVizCommand : public MPxCommand
{
public:
    static void* Creator() { return new WarpVizCommand(); }
    static MSyntax CreateSyntax();
    static const char* CommandName() { return "animForgeWarpViz"; }

    MStatus doIt(const MArgList& Args) override;
    bool isUndoable() const override { return false; }

private:
    MStatus DoConnect(const MArgDatabase& ArgData);
    MStatus DoDisconnect();
    MStatus DoEvaluate(const MArgDatabase& ArgData);
    MStatus DoStatus();
};

} // namespace WarpViz
} // namespace AnimForge
