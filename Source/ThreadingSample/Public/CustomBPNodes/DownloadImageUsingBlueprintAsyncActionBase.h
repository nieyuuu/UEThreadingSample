#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "Interfaces/IHttpRequest.h"

#include "DownloadImageUsingBlueprintAsyncActionBase.generated.h"

class UTexture2DDynamic;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FAsyncDownloadImageDelegate, UTexture2DDynamic*, Texture, int32, SizeX, int32, SizeY);

UCLASS()
class UAsyncDownloadImage :public UBlueprintAsyncActionBase
{
	GENERATED_UCLASS_BODY()

public:
	UFUNCTION(BlueprintCallable, Meta = (BlueprintInternalUseOnly = "true"))
	static UAsyncDownloadImage* AsyncDownloadImage(const FString& InURL);

public:
	UPROPERTY(BlueprintAssignable)
	FAsyncDownloadImageDelegate OnSuccess;

	UPROPERTY(BlueprintAssignable)
	FAsyncDownloadImageDelegate OnFailure;

	void Start(const FString& InURL);

private:
	void HandleImageRequest(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded);
};
