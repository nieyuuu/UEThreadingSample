#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "TextureProcessing.h"

#include "TextureProcesser.generated.h"

//The delegate declaration
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnProcessFinished, UTexture2D*, ProcessedResult);

UCLASS()
class ATaskTextureFilter :public AActor
{
	GENERATED_BODY()

	ATaskTextureFilter();

	UPROPERTY(EditAnywhere)
	EFilterType FilterType = EFilterType::BoxFilter;

	UPROPERTY(EditAnywhere, Meta=(ClampMin = 3, ClampMax = 127))
	int32 FilterSize = 27;

	UPROPERTY(EditAnywhere, Meta = (ClampMin = 0.0f, ClampMax = 1.0f))
	float ScaleValue = 1.0f;

protected:
	UTexture2D* ProcessedResult = nullptr;

	UPROPERTY(BlueprintAssignable)
	FOnProcessFinished OnProcessFinished;

public:
	virtual void Tick(float InDeltaTime) override;

	UFUNCTION(BlueprintCallable)
	void StartProcessing(UTexture2D* InSourceTexture);

protected:
	bool IsCompleted()const;

	UE::Tasks::FTask Task;
};
