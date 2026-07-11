// Copyright (C) 2025, Flying Wild Hog, All Rights Reserved.

#pragma once

#include "Nodes/ATLAnimNode_AimOffsetRay.h"
#include "AnimGraphNode_BlendSpaceBase.h"
#include "AnimGraphNode_AimOffsetRay.generated.h"

UCLASS(MinimalAPI, HideCategories = Coordinates)
class UATLAnimGraphNode_AimOffsetRay : public UAnimGraphNode_BlendSpaceBase
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = Settings)
	FATLAnimNode_AimOffsetRay Node;

	// UEdGraphNode interface
	virtual FText GetTooltipText() const override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	// End of UEdGraphNode interface

	// UAnimGraphNode_Base interface
	virtual void ValidateAnimNodeDuringCompilation(USkeleton* ForSkeleton, FCompilerResultsLog& MessageLog) override;
	virtual UAnimationAsset* GetAnimationAsset() const override { return Node.GetBlendSpace(); }
	virtual void GetAllAnimationSequencesReferred(TArray<UAnimationAsset*>& AnimationAssets) const override;
	virtual void ReplaceReferredAnimations(const TMap<UAnimationAsset*, UAnimationAsset*& AnimAssetReplacementMap) override;
	virtual void CustomizeDetails(IDetailsPanel* InDetailsPanel, FBlueprintActionDatabaseRegistrar* ActionRegistrar) override;
	virtual void CustomizeDetails(const IDetailsPanel* InDetailsPanel) override;
	// End of UAnimGraphNode_Base interface

	// UK2Node interface
	virtual void GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const override;
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual FText GetMenuCategory() const override;
	virtual FBlueprintNodeSignature GetSignature() const override;
	// End of UK2Node interface

	// UAnimGraphNode_AssetPlayerBase interface
	virtual void SetAnimationAsset(UAnimationAsset* Asset) override;
	// End of UAnimGraphNode_AssetPlayerBase interface
};

#undef LOCTEXT_NAMESPACE
