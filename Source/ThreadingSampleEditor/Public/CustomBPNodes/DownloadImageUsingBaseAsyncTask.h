#pragma once

#include "K2Node_BaseAsyncTask.h"

#include "DownloadImageUsingBaseAsyncTask.generated.h"

UCLASS()
class UK2Node_AsyncDownloadImage :public UK2Node_BaseAsyncTask
{
	GENERATED_BODY()

	UK2Node_AsyncDownloadImage();

	// UEdGraphNode Interface
	virtual FText GetTooltipText() const override;
	virtual FText GetNodeTitle(ENodeTitleType::Type InTitleType) const override;

	// UK2Node Interface
	virtual FText GetMenuCategory() const override;
};