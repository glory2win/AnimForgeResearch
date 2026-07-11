// Copyright (C) 2025, Flying Wild Hog, All Rights Reserved.

#pragma once

#include "Anim/AnimNode_AimOffsetRay.h"
#include "Animation/AnimInstance.h"

#include "ATLAnimNode_AimOffsetRay.generated.h"

USTRUCT(BlueprintInternalUseOnly)
struct FATLAnimNode_AimOffsetRay : public FAnimNode_BlendSpacePlayer
{
	GENERATED_BODY()

	friend class UATLAnimGraphNode_AimOffsetRay;

	// Cached local transform of the source socket */
	FTransform SocketLocalTransform = FTransform::Identity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Links)
	FPoseLink BasePose;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Links)
	FPoseLink AimOffsetBasePose;

	/*
	 * Max LOD that this node is allowed to run.
	 * For example, if you have LODThreshold to be 2, it will run until LOD 2 (based on index 0)
	 * when the component LOD becomes 3, it will stop update/evaluate
	 * currently transition would be an issue, and that has to be re-visited
	 */
	UPROPERTY(EditAnywhere, Category = Performance, meta = (DisplayName = "LOD Threshold"))
	int32 LODThreshold = INDEX_NONE;

	UPROPERTY(EditAnywhere, Category = InputSmoothing)
	bool bUseAvRotationSpeed = true;

	/** Indicates whether time dilation should be applied when calculating the time delta for rotational interpolation. */
	UPROPERTY(EditAnywhere, Category = InputSmoothing, meta = (EditCondition = "bUseAvRotationSpeed", EditConditionHides))
	bool bUseTimeDilation = true;

	/** Maximum allowable rotational speed */
	UPROPERTY(EditAnywhere, Category = InputSmoothing, meta = (ClampMin = "0.0", ClampMax = "100.0", UIMin = "0.0", UIMax = "100.0",
		EditCondition = "bUseAvRotationSpeed", EditConditionHides))
	float MaxRotationSpeed = 180.0f;

	/** Denominator used to scale time when calculating rotational speed */
	UPROPERTY(EditAnywhere, Category = InputSmoothing, meta = (ClampMin = "0.1", ClampMax = "1.0", UIMin = "0.1", UIMax = "1.0",
		EditCondition = "bUseAvRotationSpeed", EditConditionHides))
	float MaxRotationSpeedDenominator = 1.0f;

	/** Socket or bone to treat as the look at source. This will then be pointed at LookAtLocation */
	UPROPERTY(EditAnywhere, Category = AimMap)
	FName SourceSocketName;

	/** Location, in world space to look at */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AimMap, meta = (PinHiddenByDefault))
	FAimMap;

	/** Direction in the socket transform to consider the "forward" or look axis */
	UPROPERTY(EditAnywhere, Category = AimMap)
	FVector SocketAxis = FVector::ForwardVector;

	/** Amount of this node to blend into the output pose */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AimMap, meta = (PinHiddenByDefault))
	float Alpha = 1.f;

	UPROPERTY(EditAnywhere, Category = Settings)
	FBoneReference PivotBoneReference;

	/** Cached calculated blend input */
	FVector CurrentBlendInput = FVector::ZeroVector;

	/** Cached reference to the source socket's bone */
	FBoneReference SocketBoneReference;

	/** Cached flag to indicate whether LOD threshold is enabled */
	bool bIsLODEnabled = false;

	ATLANIMGRAPHNODE_API FATLAnimNode_AimOffsetRay() = default;

	// FAnimNode_Base Interface
	ATLANIMGRAPHNODE_API virtual void OnInitializeAnyThread(const FAnimationInitializeContext& Context) override;
	ATLANIMGRAPHNODE_API virtual void InitializeAnyThread(const FAnimationInitializeContext& Context) override;
	ATLANIMGRAPHNODE_API virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	ATLANIMGRAPHNODE_API virtual void UpdateAssetPlayer(const FAnimationUpdateContext& Context) override;
	ATLANIMGRAPHNODE_API virtual void Evaluate_AnyThread(const FPoseContext& Context) override;
	ATLANIMGRAPHNODE_API virtual void GatherDebugData(const FNodeDebugData& DebugData) override;
	// End of FAnimNode_Base Interface

	// FAnimNode_BlendSpaceLayer Interface
	ATLANIMGRAPHNODE_API virtual FVector GetPosition() const override;
	// End of FAnimNode_BlendSpaceLayer Interface

	ATLANIMGRAPHNODE_API void UpdateFromAimRay(const FPoseContext& LocalPosContext);
};
