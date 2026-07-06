// AnimGraphNode_JacobianDLSIK.h
//
// EDITOR MODULE FILE — place this pair in your game's editor module (or an
// UncookedOnly module), not the runtime module. See INTEGRATION.md section 1.

#pragma once

#include "CoreMinimal.h"
#include "AnimGraphNode_SkeletalControlBase.h"
#include "AnimNode_JacobianDLSIK.h"
#include "AnimGraphNode_JacobianDLSIK.generated.h"

UCLASS(MinimalAPI)
class UAnimGraphNode_JacobianDLSIK : public UAnimGraphNode_SkeletalControlBase
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = Settings)
	FAnimNode_JacobianDLSIK Node;

	// UEdGraphNode interface
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual FString GetNodeCategory() const override;

protected:
	// UAnimGraphNode_SkeletalControlBase interface
	virtual FText GetControllerDescription() const override;
	virtual const FAnimNode_SkeletalControlBase* GetNode() const override { return &Node; }
};
