// AnimForgeMayaWarpViz - SharedImpl.cpp
//
// UnrealBuildTool only compiles sources inside the module directory, so the
// shared translation units are pulled in here by inclusion. The headers are
// reached through the PublicIncludePaths entry in AnimForgeMayaWarpViz.Build.cs.
// AnimForgeUnrealWarpViz does the same on the engine side - one source of
// truth in Shared/AnimForgeWarpVizShared, compiled twice.

#include "../../../../Shared/AnimForgeWarpVizShared/WarpVizJson.cpp"
#include "../../../../Shared/AnimForgeWarpVizShared/WarpVizProtocol.cpp"
