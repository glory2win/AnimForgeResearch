// AnimForgeGym - primary game module.
//
// The gym itself is content: L_WarpGym holds the production character with a
// grid floor and distance markers. All bridge logic lives in the
// AnimForgeUnrealWarpViz plugin so it can be dropped into other projects.

#include "Modules/ModuleManager.h"

IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, AnimForgeGym, "AnimForgeGym");
