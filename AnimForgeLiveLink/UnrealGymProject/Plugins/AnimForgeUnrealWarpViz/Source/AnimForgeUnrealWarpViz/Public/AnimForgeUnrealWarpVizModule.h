// AnimForgeUnrealWarpViz - AnimForgeUnrealWarpVizModule.h

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

namespace AnimForge { namespace WarpViz { class FWarpVizServer; } }

class FAnimForgeUnrealWarpVizModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	AnimForge::WarpViz::FWarpVizServer* GetServer() const { return Server.Get(); }

	static FAnimForgeUnrealWarpVizModule& Get()
	{
		return FModuleManager::LoadModuleChecked<FAnimForgeUnrealWarpVizModule>("AnimForgeUnrealWarpViz");
	}

private:
	TUniquePtr<AnimForge::WarpViz::FWarpVizServer> Server;
};
