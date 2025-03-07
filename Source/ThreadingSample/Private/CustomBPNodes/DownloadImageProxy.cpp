//Based on Engine/Source/Runtime/UMG/Private/Blueprint/AsyncTaskDownloadImage.cpp
#include "CustomBPNodes/DownloadImageProxy.h"

#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Engine/Texture2DDynamic.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DownloadImageProxy)

UAsyncDownloadImageProxy::UAsyncDownloadImageProxy(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {}

UAsyncDownloadImageProxy* UAsyncDownloadImageProxy::CreateAsyncDownloadImageProxy(const FString& InURL)
{
	UAsyncDownloadImageProxy* Proxy = NewObject<UAsyncDownloadImageProxy>();
	Proxy->SetFlags(RF_StrongRefOnFrame);
	Proxy->Start(InURL);
	return Proxy;
}

void UAsyncDownloadImageProxy::Start(const FString& InURL)
{
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->OnProcessRequestComplete().BindUObject(this, &UAsyncDownloadImageProxy::HandleImageRequest);
	HttpRequest->SetURL(InURL);
	HttpRequest->SetVerb(TEXT("GET"));
	HttpRequest->ProcessRequest();
}

void UAsyncDownloadImageProxy::HandleImageRequest(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded)
{
	if (bSucceeded && HttpResponse.IsValid() && EHttpResponseCodes::IsOk(HttpResponse->GetResponseCode()) && HttpResponse->GetContentLength() > 0 && HttpResponse->GetContent().Num() > 0)
	{
		IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
		TSharedPtr<IImageWrapper> ImageWrappers[3] =
		{
			ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG),
			ImageWrapperModule.CreateImageWrapper(EImageFormat::JPEG),
			ImageWrapperModule.CreateImageWrapper(EImageFormat::BMP),
		};

		for (auto ImageWrapper : ImageWrappers)
		{
			if (ImageWrapper.IsValid() && ImageWrapper->SetCompressed(HttpResponse->GetContent().GetData(), HttpResponse->GetContent().Num()) && ImageWrapper->GetWidth() <= TNumericLimits<int32>::Max() && ImageWrapper->GetHeight() <= TNumericLimits<int32>::Max())
			{
				TArray64<uint8> RawData;
				const ERGBFormat InFormat = ERGBFormat::BGRA;
				if (ImageWrapper->GetRaw(InFormat, 8, RawData))
				{
					if (UTexture2DDynamic* Texture = UTexture2DDynamic::Create(static_cast<int32>(ImageWrapper->GetWidth()), static_cast<int32>(ImageWrapper->GetHeight())))
					{
						Texture->SRGB = true;
						Texture->UpdateResource();

						FTexture2DDynamicResource* TextureResource = static_cast<FTexture2DDynamicResource*>(Texture->GetResource());
						if (TextureResource)
						{
							ENQUEUE_RENDER_COMMAND(FWriteRawDataToTexture)(
								[TextureResource, RawData = MoveTemp(RawData)](FRHICommandListImmediate& RHICmdList)
								{
									TextureResource->WriteRawToTexture_RenderThread(RawData);
								});
						}
						OnSuccess.Broadcast(Texture, Texture->SizeX, Texture->SizeY);
						return;
					}
				}
			}
		}
	}

	OnFailure.Broadcast(nullptr, 0, 0);
}
