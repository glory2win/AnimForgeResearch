// Copyright AnimForge Studios. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

RAGDOLLPRO_API DECLARE_LOG_CATEGORY_EXTERN(LogRagdollPro, Log, All);

class FRagdollProModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
