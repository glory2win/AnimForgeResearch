// Copyright (C) 2025, Flying Wild Hog, All Rights Reserved.

#include "Nodes/ATLAnimNode_AimOffsetRay.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimTrace.h"
#include "Animation/BlendSpace.h"
#include "Components/SkeletalMeshComponent.h"
#include "Debug/CDOsDebuggers.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"

FTAutoConsoleVariable CVarAimOffsetRayEnable( NAME TEXT("ATL.AnimNode.AimOffsetRay.Enable"), DefaultValue true, Help TEXT("Enable/Disable Ray AimOffset debug"));
FTAutoConsoleVariable CVarAimOffsetRayDebug( NAME TEXT("ATL.AnimNode.AimOffsetRay.Debug"), DefaultValue false, Help TEXT("Toggle Ray AimOffset debug"));

void FATLAnimNode_AimOffsetRay::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Initialize_AnyThread)
	FAnimNode_BlendSpacePlayer::Initialize_AnyThread(Context);

	BasePose.Initialize(Context);
	AimOffsetBasePose.Initialize(Context);
}

void FATLAnimNode_AimOffsetRay::OnInitializeAnyInstance(const FAnimationInitializeContext& InProxy, const UAnimInstance* InAnimInstance)
{
	FAnimNode_BlendSpacePlayer::OnInitializeAnyInstance(InProxy, InAnimInstance);

	SocketBoneReference.BoneName = NAME_None;
	if (const USkeletalMeshComponent* SkeletalMeshComp = InAnimInstance->GetSkelMeshComponent())
	{
		if (const USkeletalMesh* SkelMesh = SkeletalMeshComp->GetSkeletalMesh())
		{
			if (const USkeletalMeshSocket* Socket = SkelMesh->FindSocket(SourceSocketName))
			{
				SocketLocalTransform = Socket->GetSocketLocalTransform();
				SocketBoneReference.BoneName = Socket->BoneName;
			}
		}

		if (SkelMeshComp->GetBoneIndex(SourceSocketName) != INDEX_NONE)
		{
			SocketLocalTransform.SetIdentity();
			SocketBoneReference.BoneName = SourceSocketName;
		}
		else if (SkelMeshComp->GetBoneIndex(SourceSocketName) != INDEX_NONE)
		{
			SocketLocalTransform.SetIdentity();
			SocketBoneReference.BoneName = SourceSocketName;
		}
	}
}

void FATLAnimNode_AimOffsetRay::UpdateAssetPlayer(const FAnimationUpdateContext& Context)
{
	bIsLODEnabled = !LODEnabled(Context.AnimInstanceProxy);
	if (bIsLODEnabled)
	{
		FAnimNode_BlendSpacePlayer::UpdateAssetPlayer(Context);
	}

	BasePose.Update(Context);
	AimOffsetBasePose.Update(Context);

	TRACE_ANIM_NODE_VALUE(Context, TEXT("Playback Time"), InternalTimeAccumulator);
}

void FATLAnimNode_AimOffsetRay::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(CacheBones_AnyThread)
	FAnimNode_BlendSpacePlayer::CacheBones_AnyThread(Context);

	BasePose.CacheBones(Context);
	AimOffsetBasePose.CacheBones(Context);
}

void FATLAnimNode_AimOffsetRay::Evaluate_AnyThread(const FPoseContext& Context)
{
	ANIM_MT_SCOPE_CYCLE_COUNTER_VERBOSE(AimOffsetRay, !IsInGameThread());

	// Evaluate base pose
	BasePose.Evaluate( Context);

	FPoseContext AimOffsetPoseContext( Context);
	AimOffsetBasePose.Evaluate( AimOffsetPoseContext);

	if (bIsLODEnabled && FAnimLight::IsRelevant(Alpha) && PivotBoneReference.IsValidToEvaluate())
	{
		UpdateFromAimRay( Context);

		// Evaluate MeshSpaceRotation additive blendspace
		FPoseContext MeshSpaceRotationAdditivePoseContext( Context);
		FAnimNode_BlendSpacePlayer::Evaluate_AnyThread( MeshSpaceRotationAdditivePoseContext);

		FCompactPoseBoneIndex PivotBoneIndex = PivotBoneReference.GetCompactPoseIndex( RequiredBones: Context.Pose.GetBoneContainer());
		FTransform PivotBaseTransform = Context.Pose[PivotBoneIndex];

		Context.Pose[PivotBoneIndex].SetRotation(AimOffsetPoseContext.Pose[PivotBoneIndex].GetRotation());
		Context.Pose[PivotBoneIndex].SetRotation(PivotTransformDelta.GetRotation() * PivotBaseTransform.GetRotation());
		Context.Pose[PivotBoneIndex].NormalizeRotation();

		// Accumulate poses together
		FAnimationPoseData BaseAnimationPoseData( AimOffsetPoseContext);
		const FAnimationPoseData AdditiveAnimationPoseData( MeshSpaceRotationAdditivePoseContext);
		FAnimationRuntime::AccumulateMeshSpaceRotationAdditiveToPose( BaseAnimationPoseData, AdditiveAnimationPoseData, Alpha);

		FTransform PivotTransformDelta = Context.Pose[PivotBoneIndex];
		PivotTransformDelta.SetToRelativeTransform(AimOffsetPoseContext.Pose[PivotBoneIndex]);
		Context.Pose[PivotBoneIndex].SetRotation(PivotTransformDelta.GetRotation() * PivotBaseTransform.GetRotation());
		Context.Pose[PivotBoneIndex].NormalizeRotation();
	}
}

void FATLAnimNode_AimOffsetRay::GatherDebugData(const FNodeDebugData& DebugData)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(GatherDebugData)

	FString DebugLine = DebugData.GetNodeName(this);
	DebugLine += FString::Printf( FMT_AOS TEXT("(Play Time: %.3f)"), InternalTimeAccumulator);
	DebugData.AddDebugItem(DebugLine);

	BasePose.GatherDebugData( DebugData);
	AimOffsetBasePose.GatherDebugData( DebugData);
}

FVector FATLAnimNode_AimOffsetRay::GetPosition() const
{
	// Use our calculated coordinates rather than the folded values
	return CurrentBlendInput;
}
