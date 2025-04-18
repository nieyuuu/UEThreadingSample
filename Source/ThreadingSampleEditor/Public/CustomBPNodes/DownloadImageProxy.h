#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"

#include "DownloadImageProxy.generated.h"

class UTexture2DDynamic;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FEditorAsyncDownloadImageDelegate, UTexture2DDynamic*, Texture, int32, SizeX, int32, SizeY);

UCLASS()
class UAsyncDownloadImageProxy :public UObject
{
	GENERATED_UCLASS_BODY()

	UFUNCTION(BlueprintCallable, Meta = (BlueprintInternalUseOnly = "true"))
	static UAsyncDownloadImageProxy* CreateAsyncDownloadImageProxy(const FString& InURL);

public:
	UPROPERTY(BlueprintAssignable)
	FEditorAsyncDownloadImageDelegate OnSuccess;

	UPROPERTY(BlueprintAssignable)
	FEditorAsyncDownloadImageDelegate OnFailure;

	void Start(const FString& InURL);

private:
	void HandleImageRequest(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded);
};