// AnimNode_JacobianDLSIK.cpp

#include "AnimNode_JacobianDLSIK.h"
#include "Animation/AnimInstanceProxy.h"
#include "AnimationRuntime.h"
#include "Algo/Reverse.h"

#if ENABLE_ANIM_DRAW_DEBUG
#include "HAL/IConsoleManager.h"
static TAutoConsoleVariable<int32> CVarJacobianDLSIKDebug(
	TEXT("a.AnimNode.JacobianDLSIK.Debug"), 0,
	TEXT("1 = draw the solved chain (yellow), target (green) and effector (red) for all Jacobian DLS IK nodes."));
#endif

void FAnimNode_JacobianDLSIK::InitializeBoneReferences(const FBoneContainer& RequiredBones)
{
	RootBone.Initialize(RequiredBones);
	TipBone.Initialize(RequiredBones);
	EffectorTarget.Initialize(RequiredBones);
	for (FJacobianDLSJointSettings& S : JointSettings)
	{
		S.Bone.Initialize(RequiredBones);
	}

	CachedChain.Reset();
	if (!RootBone.IsValidToEvaluate(RequiredBones) || !TipBone.IsValidToEvaluate(RequiredBones))
	{
		return;
	}

	// Walk tip -> root through the compact-pose hierarchy, then reverse so the
	// chain (and therefore OutBoneTransforms) is in increasing bone-index order.
	const FCompactPoseBoneIndex Root = RootBone.GetCompactPoseIndex(RequiredBones);
	FCompactPoseBoneIndex Bone = TipBone.GetCompactPoseIndex(RequiredBones);
	while (Bone.GetInt() != INDEX_NONE)
	{
		CachedChain.Add(Bone);
		if (Bone == Root)
		{
			break;
		}
		Bone = RequiredBones.GetParentBoneIndex(Bone);
	}

	if (CachedChain.Num() < 2 || CachedChain.Last() != Root)
	{
		// TipBone is not a descendant of RootBone (or the chain is a single bone).
		CachedChain.Reset();
		return;
	}
	Algo::Reverse(CachedChain);
}

bool FAnimNode_JacobianDLSIK::IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones)
{
	if (CachedChain.Num() < 2)
	{
		return false;
	}
	if (EffectorLocationSpace == BCS_ParentBoneSpace || EffectorLocationSpace == BCS_BoneSpace)
	{
		return EffectorTarget.IsValidToEvaluate(RequiredBones);
	}
	return true;
}

void FAnimNode_JacobianDLSIK::EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms)
{
	check(OutBoneTransforms.Num() == 0);
	const FBoneContainer& BoneContainer = Output.Pose.GetPose().GetBoneContainer();
	const int32 NumChain = CachedChain.Num();
	if (NumChain < 2)
	{
		return;
	}

	// Effector target -> component space.
	FTransform EffectorTransform(EffectorLocation);
	const FCompactPoseBoneIndex EffectorTargetIndex = EffectorTarget.IsValidToEvaluate(BoneContainer)
		? EffectorTarget.GetCompactPoseIndex(BoneContainer)
		: FCompactPoseBoneIndex(INDEX_NONE);
	FAnimationRuntime::ConvertBoneSpaceTransformToCS(Output.AnimInstanceProxy->GetComponentTransform(),
		Output.Pose, EffectorTransform, EffectorTargetIndex, EffectorLocationSpace);
	const FVector TargetCS = EffectorTransform.GetLocation();

	// Solver base = the chain root's parent, so joint LocalRotations are true
	// skeletal local rotations and limits reference the real ref pose.
	const FCompactPoseBoneIndex RootParent = BoneContainer.GetParentBoneIndex(CachedChain[0]);
	const FTransform Base = (RootParent.GetInt() != INDEX_NONE)
		? Output.Pose.GetComponentSpaceTransform(RootParent)
		: FTransform::Identity;

	ScratchJoints.SetNum(NumChain);
	TArray<FVector, TInlineAllocator<32>> OriginalScales;
	OriginalScales.Reserve(NumChain);

	FTransform ParentCS = Base;
	for (int32 i = 0; i < NumChain; ++i)
	{
		const FTransform BoneCS = Output.Pose.GetComponentSpaceTransform(CachedChain[i]);
		const FTransform Local = BoneCS.GetRelativeTransform(ParentCS);
		ParentCS = BoneCS;
		OriginalScales.Add(BoneCS.GetScale3D());

		FDLSJoint& J = ScratchJoints[i];
		J = FDLSJoint();
		J.LocalOffset = Local.GetTranslation();
		J.LocalRotation = Local.GetRotation();
		J.RefLocalRotation = BoneContainer.GetRefPoseTransform(CachedChain[i]).GetRotation();

		for (const FJacobianDLSJointSettings& S : JointSettings)
		{
			if (S.Bone.IsValidToEvaluate(BoneContainer) && S.Bone.GetCompactPoseIndex(BoneContainer) == CachedChain[i])
			{
				J.Weight = S.Weight;
				J.bUseLimits = S.bUseLimits;
				J.SwingLimitRadians = FMath::DegreesToRadians(S.SwingLimitDegrees);
				J.TwistLimitRadians = FMath::DegreesToRadians(S.TwistLimitDegrees);
				J.TwistAxis = S.TwistAxis.GetSafeNormal(UE_SMALL_NUMBER, FVector::XAxisVector);
				break;
			}
		}
	}

	FJacobianDLSSettings Settings;
	Settings.MaxIterations = MaxIterations;
	Settings.Tolerance = Precision;
	Settings.Damping = Damping;
	Settings.MaxExtraDamping = MaxExtraDamping;
	Settings.IsotropyThreshold = IsotropyThreshold;
	Settings.MaxErrorStep = MaxErrorStep;
	Settings.MaxAngleStepRadians = FMath::DegreesToRadians(MaxAngleStepDegrees);

	FJacobianDLSSolver::Solve(ScratchJoints, Base, TargetCS, Settings, &LastDebugInfo);

	OutBoneTransforms.Reserve(NumChain);
	for (int32 i = 0; i < NumChain; ++i)
	{
		const FDLSJoint& J = ScratchJoints[i];
		OutBoneTransforms.Add(FBoneTransform(CachedChain[i],
			FTransform(J.ComponentRotation, J.ComponentPosition, OriginalScales[i])));
	}

#if ENABLE_ANIM_DRAW_DEBUG
	if (CVarJacobianDLSIKDebug.GetValueOnAnyThread() > 0)
	{
		FAnimInstanceProxy* Proxy = Output.AnimInstanceProxy;
		const FTransform& ComponentTM = Proxy->GetComponentTransform();
		for (int32 i = 1; i < NumChain; ++i)
		{
			Proxy->AnimDrawDebugLine(
				ComponentTM.TransformPosition(ScratchJoints[i - 1].ComponentPosition),
				ComponentTM.TransformPosition(ScratchJoints[i].ComponentPosition),
				FColor::Yellow, false, -1.f, 0.35f);
		}
		Proxy->AnimDrawDebugSphere(ComponentTM.TransformPosition(TargetCS), 3.f, 12, FColor::Green, false, -1.f, 0.35f);
		Proxy->AnimDrawDebugSphere(ComponentTM.TransformPosition(ScratchJoints.Last().ComponentPosition), 2.f, 12, FColor::Red, false, -1.f, 0.35f);
	}
#endif
}

void FAnimNode_JacobianDLSIK::GatherDebugData(FNodeDebugData& DebugData)
{
	DECLARE_SCOPED_HIERARCHICAL_COUNTER_ANIMNODE(GatherDebugData)
	FString DebugLine = DebugData.GetNodeName(this);
	DebugLine += FString::Printf(TEXT(" (%s -> %s  iters:%d  err:%.2fcm  lambda:%.1f  iso:%.3f)"),
		*RootBone.BoneName.ToString(), *TipBone.BoneName.ToString(),
		LastDebugInfo.IterationsUsed, LastDebugInfo.FinalError,
		LastDebugInfo.LastLambda, LastDebugInfo.LastIsotropy);
	DebugData.AddDebugItem(DebugLine);
	ComponentPose.GatherDebugData(DebugData);
}
