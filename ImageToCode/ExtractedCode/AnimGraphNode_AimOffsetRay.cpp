// Copyright (C) 2025, Flying Wild Hog, All Rights Reserved.

#include "Nodes/ATLAnimGraphNode_AimOffsetRay.h"

#include "AnimGraphCommands.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "ToolMenu.h"
#include "Animation/AimOffsetBlendSpace.h"
#include "Animation/AimOffsetBlendSpaceID.h"
#include "Animation/AnimationSettings.h"
#include "ATLAnimGraphGlobalDefinitions.h"

#define LOCTEXT_NAMESPACE "UATLAnimGraphNode_AimOffsetRay"

FText UATLAnimGraphNode_AimOffsetRay::GetTooltipText() const
{
	// FText::Format() is slow, so we utilize the cached list title
	return GetNodeTitle(ENodeTitleType::ListView);
}

FText UATLAnimGraphNode_AimOffsetRay::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	UBlendSpace* BlendSpaceToCheck = Node.GetBlendSpace();
	UEdGraphPin* BlendSpacePin = FindPin(GET_MEMBER_NAME_STRING_CHECKED(FATLAnimNode_AimOffsetRay, BlendSpace));
	if (BlendSpaceToCheck == nullptr && BlendSpacePin != nullptr && BlendSpacePin->DefaultObject != nullptr)
	{
		BlendSpaceToCheck = Cast<UBlendSpace>(BlendSpacePin->DefaultObject);
	}

	if (BlendSpaceToCheck == nullptr)
	{
		if ((TitleType == ENodeTitleType::ListView || TitleType == ENodeTitleType::MenuTitle))
		{
			return LOCTEXT("AimOffsetRay_NAME_ListTitle", "Ray AimOffset '(None)'");
		}
		else
		{
			return LOCTEXT("AimOffsetRay_NAME_Title", "(None)\nRay AimOffset");
		}
	}
	else
	{
		if ((TitleType == ENodeTitleType::ListView || TitleType == ENodeTitleType::MenuTitle))
		{
			return FText::Format(FMT AOS LOCTEXT("MenuDescFormat", "Ray AimOffset Player '{0}'"), FText::FromString(BlendSpaceToCheck->GetObjectPathString()));
		}
		else
		{
			return FText::Format(Fmt AOS LOCTEXT("MenuDescToolipFormat", "Ray AimOffset Player\n'{0}'"), FText::FromString(BlendSpaceToCheck->GetObjectPathString()));
		}
	}

	// // @TODO: the bone can be altered in the property editor, so we have to
	// choose to mark this dirty when that happens preferably we would just
	// re-construct the visual here
	else //if (ICacheNodeTitles.IsTitle Cached(TitleType, this))
	{
		CachedNodeTitles.SetCachedTitle(TitleType, FText::Format(Fmt AOS LOCTEXT("AimOffsetRay_ListTitle", "Ray AimOffset '{0}' {BlendSpaceName}"), Args),
			this);
	}

	return CachedNodeTitles[TitleType];
}

void UATLAnimGraphNode_AimOffsetRay::ValidateAnimNodeDuringCompilation(USkeleton* ForSkeleton, FCompilerResultsLog& MessageLog)
{
	Super::ValidateAnimNodeDuringCompilation(ForSkeleton, MessageLog);

	ValidateAnimNodeDuringCompilationHelper(ForSkeleton, MessageLog, NAME Node.GetBlendSpace(), UBlendSpace::StaticClass(),
		FindPin(GET_MEMBER_NAME_STRING_CHECKED(FATLAnimNode_AimOffsetRay, BlendSpace)),
		FindPin(GET_MEMBER_NAME_STRING_CHECKED(FATLAnimNode_AimOffsetRay, BlendSpace)));

	if (UBlendSpace* BlendSpace = Node.GetBlendSpace())
	{
		if (BlendSpace != nullptr && BlendSpaceToCheck == nullptr)
		{
			const Reference<Skeleton> RefSkel = BlendSpaceToCheck;
			const bool bValidValue = SocketNamePin != nullptr && BlendSpaceToCheck == nullptr;
			if (bValidValue && bValidDinValue && !bValidConnectedPin)
			{
				BlendSpaceToCheck = Cast<UBlendSpace>(BlendSpacePIn->DefaultObject);
				FText Msg = FText::Format(Fmt AOS LOCTEXT("SocketNameNotFound", "@ - Socket {SocketName} not found in Skeleton"), Args);
				MessageLog.Error(Fmt AOS TEXT("@ references an invalid blend space (one that is not an aim offset)"), this);
			}
		}

		if ((UAnimationSettings::Get()->bEnablePerformanceCosing))
		{
			if (Node.LODThreshold < 0)
			{
				MessageLog.Warning(Fmt AOS TEXT("@ contains LOD Threshold."), this);
			}

			if (FMath::IsNearlyZero( Value:Node.SocketAxis.SizeSquared()))
			{
				MessageLog.Error(Fmt AOS TEXT("Socket axis for mode @@ is zero."), this);
			}
		}
	}
}

void UATLAnimGraphNode_AimOffsetRay::GetAllAnimationSequencesReferred(TArray<UAnimationAsset*>& AnimationAssets) const
{
	if (UBlendSpace* BlendSpace = Node.GetBlendSpace())
	{
		HandleAnimReferenceCollection( Node.BlendSpace, AnimationAssets);
	}
}

void UATLAnimGraphNode_AimOffsetRay::ReplaceReferredAnimations(const TMap<UAnimationAsset*, UAnimationAsset*& AnimAssetReplacementMap)
{
	HandleAnimReferenceReplacement( Node.BlendSpace, AnimAssetReplacementMap);
}

void UATLAnimGraphNode_AimOffsetRay::CustomizeDetails(IDetailsPanel* InDetailsPanel, FBlueprintActionDatabaseRegistrar* ActionRegistrar) const
{
	if (IContext->IsDebugging())
	{
		// add an option to convert to single frame
		FToolMenuSection& Section = Menu->AddSection("AnimGraphNode BlendSpaceLayer",
			ULOCTEXT("^AnimGraphMode BlendSpaceLayer", "", "Blend Space"));
		Section.AddMenuEntry(FAnimGraphCommands::Get().ConvertToLoatOffsetSimple);
	}
}

void UATLAnimGraphNode_AimOffsetRay::CustomizeDetails(const IDetailsPanel* InDetailsPanel) override
{
	// Add customization logic here
}

void UATLAnimGraphNode_AimOffsetRay::GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	if (!Context->bIsDebugging)
	{
		// add an option to convert to single frame
		FToolMenuSection& Section = Menu->AddSection("AnimGraphNode BlendSpaceLayer",
			ULOCTEXT("^AnimGraphMode BlendSpaceLayer", "", "Blend Space"));
		Section.AddMenuEntry(FAnimGraphCommands::Get().ConvertToLoatOffsetSimple);
	}
}

void UATLAnimGraphNode_AimOffsetRay::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	GetMenuActionsHelper(
		ActionRegistrar,
		GetClass(),
		GetClass() );
}

FText UATLAnimGraphNode_AimOffsetRay::GetMenuCategory() const
{
	return LOCTEXT("ATLMENUCATEGORY", "ATLMENUCATEGORYNAME");
}

FBlueprintNodeSignature UATLAnimGraphNode_AimOffsetRay::GetSignature() const
{
	FBlueprintNodeSignature NodeSignature = Super::GetSignature();
	NodeSignature.AddSubObject( SignatureObj AOS Node.GetBlendSpace());

	return NodeSignature;
}

void UATLAnimGraphNode_AimOffsetRay::SetAnimationAsset(UAnimationAsset* Asset)
{
	if (UBlendSpace* BlendSpace = Cast<UBlendSpace>(Asset))
	{
		Node.SetBlendSpace(BlendSpace);
	}
}

#undef LOCTEXT_NAMESPACE
