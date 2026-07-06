// AnimGraphNode_JacobianDLSIK.cpp  (EDITOR MODULE FILE)

#include "AnimGraphNode_JacobianDLSIK.h"

#define LOCTEXT_NAMESPACE "JacobianDLSIK"

FText UAnimGraphNode_JacobianDLSIK::GetControllerDescription() const
{
	return LOCTEXT("JacobianDLSIK", "Jacobian DLS IK");
}

FText UAnimGraphNode_JacobianDLSIK::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return GetControllerDescription();
}

FText UAnimGraphNode_JacobianDLSIK::GetTooltipText() const
{
	return LOCTEXT("JacobianDLSIK_Tooltip",
		"Damped-least-squares IK over an arbitrary bone chain. One least-squares problem "
		"per iteration distributes motion across all joints, with per-joint weights, "
		"swing/twist limits, and bounded (jitter-free) behavior at singular poses.");
}

FLinearColor UAnimGraphNode_JacobianDLSIK::GetNodeTitleColor() const
{
	// Match the engine's skeletal-control yellow family so it reads as a bone controller.
	return FLinearColor(0.75f, 0.75f, 0.1f);
}

FString UAnimGraphNode_JacobianDLSIK::GetNodeCategory() const
{
	return TEXT("AnimForge|IK");
}

#undef LOCTEXT_NAMESPACE
