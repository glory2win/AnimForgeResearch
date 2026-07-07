// AnimForgeUnrealWarpViz - WarpVizSettings.h

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimSequenceBase.h"
#include "Engine/DeveloperSettings.h"

#include "WarpVizSettings.generated.h"

/**
 * Registry mapping the clip names Maya sends to gym assets. Configured in
 * Project Settings > Plugins > AnimForge WarpViz and saved to DefaultGame.ini,
 * so the gym level and its clip table travel with the project.
 */
USTRUCT()
struct FWarpVizClipEntry
{
	GENERATED_BODY()

	/** Clip name as typed in the Maya UI (e.g. the export name). */
	UPROPERTY(EditAnywhere, Category = "WarpViz")
	FString ClipName;

	/** The imported sequence/montage this clip corresponds to. */
	UPROPERTY(EditAnywhere, Category = "WarpViz")
	TSoftObjectPtr<UAnimSequenceBase> Animation;

	/** Skeleton bones to include in ghost poses; empty = root only. */
	UPROPERTY(EditAnywhere, Category = "WarpViz")
	TArray<FName> GhostBones;
};

UCLASS(config = Game, defaultconfig, meta = (DisplayName = "AnimForge WarpViz"))
class ANIMFORGEUNREALWARPVIZ_API UWarpVizSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** TCP port the gym server listens on (Maya default matches). */
	UPROPERTY(EditAnywhere, config, Category = "Connection", meta = (ClampMin = 1, ClampMax = 65535))
	int32 ListenPort = 46464;

	/** Start listening automatically when the editor boots. */
	UPROPERTY(EditAnywhere, config, Category = "Connection")
	bool bAutoStartServer = true;

	/** Character IDs this gym accepts (empty = accept any). */
	UPROPERTY(EditAnywhere, config, Category = "Gym")
	TArray<FString> AcceptedCharacterIds;

	/** Clip name -> gym asset registry. */
	UPROPERTY(EditAnywhere, config, Category = "Gym")
	TArray<FWarpVizClipEntry> Clips;

	/**
	 * Max allowed drift (cm) between the Maya root samples and the extracted
	 * UE root motion before a warning is attached to the result. Catches
	 * stale imports where the Maya scene and gym asset no longer match.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Validation", meta = (ClampMin = 0.0))
	float RootDriftWarningThreshold = 1.0f;

	const FWarpVizClipEntry* FindClip(const FString& ClipName) const
	{
		return Clips.FindByPredicate([&ClipName](const FWarpVizClipEntry& Entry)
		{
			return Entry.ClipName == ClipName;
		});
	}
};
