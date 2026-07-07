// AnimForgeUnrealWarpViz - AnimForgeUnrealWarpVizModule.cpp

#include "AnimForgeUnrealWarpVizModule.h"

#include "WarpVizServer.h"
#include "WarpVizSettings.h"

#define LOCTEXT_NAMESPACE "AnimForgeUnrealWarpViz"

void FAnimForgeUnrealWarpVizModule::StartupModule()
{
	const UWarpVizSettings* Settings = GetDefault<UWarpVizSettings>();
	if (Settings->bAutoStartServer)
	{
		Server = MakeUnique<AnimForge::WarpViz::FWarpVizServer>();
		Server->Start(static_cast<uint16>(Settings->ListenPort));
	}
}

void FAnimForgeUnrealWarpVizModule::ShutdownModule()
{
	if (Server.IsValid())
	{
		Server->Stop();
		Server.Reset();
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FAnimForgeUnrealWarpVizModule, AnimForgeUnrealWarpViz)
