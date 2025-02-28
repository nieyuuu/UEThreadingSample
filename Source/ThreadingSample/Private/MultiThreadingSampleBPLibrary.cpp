#include "MultiThreadingSampleBPLibrary.h"

#include "Misc/Timeout.h"
#include "Math/GuardedInt.h"
#include "AssetCompilingManager.h"

DEFINE_LOG_CATEGORY(LogMultiThreadingSample);

/*----------------------------------------------------------------------------------
	Async Interface Samples
----------------------------------------------------------------------------------*/
FString LoadTextFileToString(const FString& InFileName, float InSleepTimeInSeconds = 0.0f)
{
	//InSleepTimeInSeconds is used to simulate a long loading task which we clamp here to a reasonable range.
	InSleepTimeInSeconds = FMath::Clamp(InSleepTimeInSeconds, 0.0f, 5.0f);

	const FString FullPath = FPaths::ConvertRelativePathToFull(InFileName);
	FString OutFileContent;
	if (FFileHelper::LoadFileToString(OutFileContent, *FullPath))
	{
		UE_LOG(LogMultiThreadingSample, Display, TEXT("Successfully loaded file: %s (Will now sleep for %f seconds)."), *InFileName, InSleepTimeInSeconds);
		FPlatformProcess::Sleep(InSleepTimeInSeconds);
		return MoveTemp(OutFileContent);
	}
	else
	{
		UE_LOG(LogMultiThreadingSample, Warning, TEXT("Failed to load file: %s (Full Path: %s)."), *InFileName, *FullPath);
		return FString();
	}
}

auto LaunchAsyncLoadingTextFile_AsyncInterface_TaskGraph(const FString& InFileName, float InSleepTimeInSeconds = 0.0f)
{
	auto Result = NewObject<UTextFileResult>();
	Result->SetResult(InFileName, Async(
		EAsyncExecution::TaskGraph,
		[&InFileName, InSleepTimeInSeconds]() {
			return LoadTextFileToString(InFileName, InSleepTimeInSeconds);
		}));
	return Result;
}

auto LaunchAsyncLoadingTextFile_AsyncInterface_ThreadPool(const FString& InFileName, float InSleepTimeInSeconds = 0.0f)
{
	auto Result = NewObject<UTextFileResult>();
	Result->SetResult(InFileName, Async(
		EAsyncExecution::ThreadPool,
		[&InFileName, InSleepTimeInSeconds]() {
			return LoadTextFileToString(InFileName, InSleepTimeInSeconds);
		}));
	return Result;
}

auto LaunchAsyncLoadingTextFile_AsyncInterface_Thread(const FString& InFileName, float InSleepTimeInSeconds = 0.0f)
{
	auto Result = NewObject<UTextFileResult>();
	Result->SetResult(InFileName, Async(
		EAsyncExecution::Thread,
		[&InFileName, InSleepTimeInSeconds]() {
			return LoadTextFileToString(InFileName, InSleepTimeInSeconds);
		}));
	return Result;
}

auto LaunchAsyncLoadingTextFile_AsyncPoolInterface(const FString& InFileName, float InSleepTimeInSeconds = 0.0f)
{
	auto Result = NewObject<UTextFileResult>();
	Result->SetResult(InFileName, AsyncPool(
		*GThreadPool,
		[&InFileName, InSleepTimeInSeconds]() {
			return LoadTextFileToString(InFileName, InSleepTimeInSeconds);
		},
		nullptr,
		EQueuedWorkPriority::Normal
	));
	return Result;
}

auto LaunchAsyncLoadingTextFile_AsyncThreadInterface(const FString& InFileName, float InSleepTimeInSeconds = 0.0f)
{
	auto Result = NewObject<UTextFileResult>();
	Result->SetResult(InFileName, AsyncThread(
		[&InFileName, InSleepTimeInSeconds]() {
			return LoadTextFileToString(InFileName, InSleepTimeInSeconds);
		},
		0,
		EThreadPriority::TPri_Normal,
		nullptr
	));
	return Result;
}

void LaunchVoidReturnTask_AsyncTaskInterface()
{
	AsyncTask(
		ENamedThreads::AnyThread,
		[]() {
			UE_LOG(LogMultiThreadingSample, Warning, TEXT("Running Void Return Task!!!"));
			FPlatformProcess::Sleep(0.1f);
		}
	);
}

void UMultiThreadingSampleBPLibrary::LoadTextFiles(EAsyncLoadingExecution InExecution, float InSleepTimeInSeconds, const TArray<FString>& InFilesToLoad, TArray<UTextFileResult*>& OutResults)
{
	OutResults.Reserve(InFilesToLoad.Num());
	for (const FString& FileName : InFilesToLoad)
	{
		switch (InExecution)
		{
		case EAsyncLoadingExecution::AsyncInterface_TaskGraph:
			OutResults.Add(LaunchAsyncLoadingTextFile_AsyncInterface_TaskGraph(FileName, InSleepTimeInSeconds));
			break;
		case EAsyncLoadingExecution::AsyncInterface_ThreadPool:
			OutResults.Add(LaunchAsyncLoadingTextFile_AsyncInterface_ThreadPool(FileName, InSleepTimeInSeconds));
			break;
		case EAsyncLoadingExecution::AsyncInterface_Thread:
			OutResults.Add(LaunchAsyncLoadingTextFile_AsyncInterface_Thread(FileName, InSleepTimeInSeconds));
			break;
		case EAsyncLoadingExecution::AsyncPoolInterface:
			OutResults.Add(LaunchAsyncLoadingTextFile_AsyncPoolInterface(FileName, InSleepTimeInSeconds));
			break;
		case EAsyncLoadingExecution::AsyncThreadInterface:
			OutResults.Add(LaunchAsyncLoadingTextFile_AsyncThreadInterface(FileName, InSleepTimeInSeconds));
			break;
		default:
			check(false);
			break;
		}
	}
}

/*----------------------------------------------------------------------------------
	Texture Filter Samples
----------------------------------------------------------------------------------*/

void Compute1DBoxFilterKernel(int32 InSize, TArray<float>& OutWeights, TArray<int32>& OutOffsets)
{
	const int32 HalfSize = InSize / 2;

	for (int i = -HalfSize; i <= HalfSize; ++i)
	{
		OutWeights.Add(1.0f / InSize);
		OutOffsets.Add(i);
	}
}

void Compute1DGaussianFilterKernel(int32 InSize, TArray<float>& OutWeights, TArray<int32>& OutOffsets)
{
	//The 3 sigma rule(from OpenCV for balancing performance and filter quality).
	const float Sigma = 0.3f * ((InSize - 1) * 0.5f - 1.0f) + 0.8f;

	const float TwoPISqrted = FMath::Sqrt(2.0f * PI);
	const float SigmaSquared = FMath::Pow(Sigma, 2.0f);

	const int32 HalfSize = InSize / 2;

	for (int i = -HalfSize; i <= HalfSize; ++i)
	{
		const float Exponent = FMath::Exp(-(i * i) / (2.0f * SigmaSquared));
		OutWeights.Add(Exponent / (TwoPISqrted * Sigma));
		OutOffsets.Add(i);
	}

	const float SumWeight = Algo::Accumulate<float>(OutWeights, 0.0f);

	//Normalize weights.
	for (int i = 0; i < OutWeights.Num(); ++i)
	{
		OutWeights[i] = OutWeights[i] / SumWeight;
	}
}

const TCHAR* EFilterTypeToString(EFilterType InFilterType)
{
	const TCHAR* ConvertTable[] = {
		TEXT("BoxFilter"),
		TEXT("GaussianFilter")
	};

	return ConvertTable[int32(InFilterType)];
}

void Compute1DFilterKernel(EFilterType InFilterType, int32 InFilterSize, TArray<float>& OutWeights, TArray<int32>& OutOffsets)
{
	OutWeights.Empty();
	OutOffsets.Empty();

	if (InFilterSize <= 2 || InFilterSize % 2 == 0)
	{
		UE_LOG(LogMultiThreadingSample, Warning, TEXT("Invalid filter size [%d]. filter size should be a positive odd number and should be greater than 2."), InFilterSize);
		return;
	}

	OutWeights.Reserve(InFilterSize);
	OutOffsets.Reserve(InFilterSize);

	switch (InFilterType)
	{
	case EFilterType::BoxFilter:
		Compute1DBoxFilterKernel(InFilterSize, OutWeights, OutOffsets);
		break;
	case EFilterType::GaussianFilter:
		Compute1DGaussianFilterKernel(InFilterSize, OutWeights, OutOffsets);
		break;
	default:
		check(false);
	}

	check(OutWeights.Num() == OutOffsets.Num());
	check(OutWeights.Num() % 2 == 1);
}

//A function that filters the RGB channels of InSourceTexture using ParallelFor.
void FilterTexture(TWeakObjectPtr<UTexture2D> InSourceTexture, TWeakObjectPtr<UTexture2D> OutFilteredTexture, EFilterType InFilterType, int32 InFilterSize, bool InVertical, bool InForceSingleThread)
{
	check(InSourceTexture.Get() && OutFilteredTexture.Get());
	check(InSourceTexture->SRGB == OutFilteredTexture->SRGB);

	FTexture2DMipMap* SourceMip = &InSourceTexture->GetPlatformData()->Mips[0];
	FByteBulkData* SourceRawImageData = &SourceMip->BulkData;
	FColor* SourceColorData = static_cast<FColor*>(SourceRawImageData->Lock(LOCK_READ_ONLY));
	check(SourceColorData);

	FTexture2DMipMap* FilteredMip = &OutFilteredTexture->GetPlatformData()->Mips[0];
	FByteBulkData* FilteredRawImageData = &FilteredMip->BulkData;
	FColor* FilteredColorData = static_cast<FColor*>(FilteredRawImageData->Lock(LOCK_READ_WRITE));
	check(FilteredColorData);

	check(SourceMip->SizeX == FilteredMip->SizeX && SourceMip->SizeY == FilteredMip->SizeY);

	const uint16 TextureWidth = SourceMip->SizeX;
	const uint16 TextureHeight = SourceMip->SizeY;

	const bool IsSRGB = InSourceTexture->SRGB;

	TArray<float> Weights;
	TArray<int32> Offsets;

	Compute1DFilterKernel(InFilterType, InFilterSize, Weights, Offsets);

	if (Weights.Num() == 0 || Offsets.Num() == 0)
	{
		UE_LOG(LogMultiThreadingSample, Warning, TEXT("Empty filter weights or offsets."));
		return;
	}

	auto LoopBody = [=](int32 Index) {
		const int32 CurrentPixelPosX = Index % TextureWidth;
		const int32 CurrentPixelPosY = Index / TextureWidth;

		float WeightedLinearSumR = 0.0f, WeightedLinearSumG = 0.0f, WeightedLinearSumB = 0.0f;

		for (int i = 0; i < Weights.Num(); ++i)
		{
			FIntPoint SamplePosition;

			if (InVertical)
			{
				int32 ClampedSamplePositionY = FMath::Clamp(CurrentPixelPosY + Offsets[i], 0, TextureHeight);
				SamplePosition = FIntPoint(CurrentPixelPosX, ClampedSamplePositionY);
			}
			else
			{
				int32 ClampedSamplePositionX = FMath::Clamp(CurrentPixelPosX + Offsets[i], 0, TextureWidth);
				SamplePosition = FIntPoint(ClampedSamplePositionX, CurrentPixelPosY);
			}

			const FColor SampledColor = SourceColorData[SamplePosition.Y * TextureWidth + SamplePosition.X];

			if (IsSRGB)
			{
				//Convert to linear space.
				FLinearColor LinearColor = FLinearColor(SampledColor);
				WeightedLinearSumR += LinearColor.R * Weights[i];
				WeightedLinearSumG += LinearColor.G * Weights[i];
				WeightedLinearSumB += LinearColor.B * Weights[i];
			}
			else
			{
				//Already in linear space?
				WeightedLinearSumR += SampledColor.R / 255.0f * Weights[i];
				WeightedLinearSumG += SampledColor.G / 255.0f * Weights[i];
				WeightedLinearSumB += SampledColor.B / 255.0f * Weights[i];
			}
		}

		const FColor Result = FLinearColor(WeightedLinearSumR, WeightedLinearSumG, WeightedLinearSumB).ToFColor(IsSRGB);

		FilteredColorData[Index] = FColor(Result.R, Result.G, Result.B, SourceColorData[Index].A);
		};

	const double StartTime = FPlatformTime::Seconds();

	//ParallelFor will return until all loop bodies finish execution.
	ParallelFor(
		TEXT("Parallel Texture Filter"),
		TextureWidth * TextureHeight,
		8192,
		LoopBody,
		InForceSingleThread ? EParallelForFlags::ForceSingleThread : EParallelForFlags::None);

	const double EndTime = FPlatformTime::Seconds();

	UE_LOG(LogMultiThreadingSample, Display, TEXT("%s(%s, %s, texture size: %dx%d, filter size: %d) execution finished in: %f seconds."),
		EFilterTypeToString(InFilterType),
		InForceSingleThread ? TEXT("singlethreaded") : TEXT("multithreaded"),
		InVertical ? TEXT("vertical pass") : TEXT("horizontal pass"),
		TextureWidth, TextureHeight, InFilterSize,
		EndTime - StartTime);

	FilteredRawImageData->Unlock();
	SourceRawImageData->Unlock();
}

//A function that scales the alpha channel of InSourceTexture using ParallelFor.
void ScaleAlphaChannel(TWeakObjectPtr<UTexture2D> InSourceTexture, TWeakObjectPtr<UTexture2D> OutScaledTexture, float InScaleValue, bool InForceSingleThread)
{
	check(InSourceTexture.Get() && OutScaledTexture.Get());

	FTexture2DMipMap* SourceMip = &InSourceTexture->GetPlatformData()->Mips[0];
	FByteBulkData* SourceRawImageData = &SourceMip->BulkData;
	FColor* SourceColorData = static_cast<FColor*>(SourceRawImageData->Lock(LOCK_READ_ONLY));
	check(SourceColorData);

	FTexture2DMipMap* ScaledMip = &OutScaledTexture->GetPlatformData()->Mips[0];
	FByteBulkData* ScaledRawImageData = &ScaledMip->BulkData;
	FColor* ScaledColorData = static_cast<FColor*>(ScaledRawImageData->Lock(LOCK_READ_WRITE));
	check(ScaledColorData);

	const uint16 TextureWidth = SourceMip->SizeX;
	const uint16 TextureHeight = SourceMip->SizeY;

	const double StartTime = FPlatformTime::Seconds();

	//ParallelFor will return until all loop bodies finish execution, so the caller will be blocked.
	ParallelFor(
		TEXT("Parallel Scale Alpha Channel"),
		TextureWidth * TextureHeight,
		2048,
		[&](int32 Index) {
			ScaledColorData[Index].A = SourceColorData[Index].A * FMath::Clamp(InScaleValue, 0.0f, 1.0f);
		},
		InForceSingleThread ? EParallelForFlags::ForceSingleThread : EParallelForFlags::None);

	const double EndTime = FPlatformTime::Seconds();

	UE_LOG(LogMultiThreadingSample, Display, TEXT("Scale alpha channel(%s, texture size: %dx%d, scale value: %f) execution finished in: %f seconds."),
		InForceSingleThread ? TEXT("singlethreaded") : TEXT("multithreaded"),
		TextureWidth, TextureHeight, InScaleValue,
		EndTime - StartTime);

	ScaledRawImageData->Unlock();
	SourceRawImageData->Unlock();
}

//A function that composites the RGB channels of a texture and the Alpha channel of another texture using ParallelFor.
void CompositeRGBAValue(TWeakObjectPtr<UTexture2D> InRGBTexture, TWeakObjectPtr<UTexture2D> InATexture, TWeakObjectPtr<UTexture2D> OutTexture, bool InForceSingleThread)
{
	check(InRGBTexture.Get() && InATexture.Get() && OutTexture.Get());

	FTexture2DMipMap* RGBMip = &InRGBTexture->GetPlatformData()->Mips[0];
	FByteBulkData* RGBRawImageData = &RGBMip->BulkData;
	FColor* RGBColorData = static_cast<FColor*>(RGBRawImageData->Lock(LOCK_READ_ONLY));
	check(RGBColorData);

	FTexture2DMipMap* AlphaMip = &InATexture->GetPlatformData()->Mips[0];
	FByteBulkData* AlphaRawImageData = &AlphaMip->BulkData;
	FColor* AlphaColorData = static_cast<FColor*>(AlphaRawImageData->Lock(LOCK_READ_ONLY));
	check(AlphaColorData);

	FTexture2DMipMap* Result = &OutTexture->GetPlatformData()->Mips[0];
	FByteBulkData* ResultRawImageData = &Result->BulkData;
	FColor* ResultColorData = static_cast<FColor*>(ResultRawImageData->Lock(LOCK_READ_WRITE));
	check(ResultColorData);

	const uint16 TextureWidth = RGBMip->SizeX;
	const uint16 TextureHeight = RGBMip->SizeY;

	const double StartTime = FPlatformTime::Seconds();

	//ParallelFor will return until all loop bodies finish execution, so the caller will be blocked.
	ParallelFor(
		TEXT("Parallel Composite RGBA Value"),
		TextureWidth * TextureHeight,
		2048,
		[&](int32 Index) {
			ResultColorData[Index].R = RGBColorData[Index].R;
			ResultColorData[Index].G = RGBColorData[Index].G;
			ResultColorData[Index].B = RGBColorData[Index].B;
			ResultColorData[Index].A = AlphaColorData[Index].A;
		},
		InForceSingleThread ? EParallelForFlags::ForceSingleThread : EParallelForFlags::None);

	const double EndTime = FPlatformTime::Seconds();

	UE_LOG(LogMultiThreadingSample, Display, TEXT("Composite RGBA value(%s, texture size: %dx%d) execution finished in: %f seconds."),
		InForceSingleThread ? TEXT("singlethreaded") : TEXT("multithreaded"),
		TextureWidth, TextureHeight,
		EndTime - StartTime);

	ResultRawImageData->Unlock();
	AlphaRawImageData->Unlock();
	RGBRawImageData->Unlock();
}

bool ValidateParameters(UTexture2D* InSourceTexture, int InFilterSize, float InScaleValue)
{
#if !TASKGRAPH_NEW_FRONTEND
	UE_LOG(LogMultiThreadingSample, Warning, TEXT("Task graph use new frontend?"));
	return false;
#endif

	if (!IsValid(InSourceTexture))
	{
		UE_LOG(LogMultiThreadingSample, Warning, TEXT("Invalid source texture!!!"));
		return false;
	}

	if (InFilterSize <= 2 || InFilterSize >= 64 || InFilterSize % 2 == 0)
	{
		UE_LOG(LogMultiThreadingSample, Warning, TEXT("Invalid filter size:[%d]. Valid filter size range [3, 63] and has to be an odd number."), InFilterSize);
		return false;
	}

	if (InScaleValue < 0.0f || InScaleValue > 1.0f)
	{
		UE_LOG(LogMultiThreadingSample, Warning, TEXT("Invalid scale value:[%f]. Valid scale value range [0, 1]."), InScaleValue);
		return false;
	}

	if (InSourceTexture->CompressionSettings != TextureCompressionSettings::TC_VectorDisplacementmap)
	{
		UE_LOG(LogMultiThreadingSample, Warning, TEXT("Currently only support texture with compression setting [VectorDisplacementmap (RGBA8)]."));
		return false;
	}

	if (InSourceTexture->MipGenSettings != TextureMipGenSettings::TMGS_NoMipmaps)
	{
		UE_LOG(LogMultiThreadingSample, Warning, TEXT("Currently only support texture with mipmap generation setting [TMGS_NoMipmaps]."));
		return false;
	}

	FTexture2DMipMap* SourceMip = &InSourceTexture->GetPlatformData()->Mips[0];
	const uint16 TextureWidth = SourceMip->SizeX;
	const uint16 TextureHeight = SourceMip->SizeY;

	if (TextureWidth < 256 || TextureHeight < 256)
	{
		UE_LOG(LogMultiThreadingSample, Warning, TEXT("Source texture size is too small!!!"));
		return false;
	}

	return true;
}

UTexture2D* CreateTransientTextureFromSource(UTexture2D* InSourceTexture, const FString& InTextureName, bool InCopySourceImage = false)
{
	check(InSourceTexture);

	FTexture2DMipMap* SourceMip = &InSourceTexture->GetPlatformData()->Mips[0];

	const uint16 TextureWidth = SourceMip->SizeX, TextureHeight = SourceMip->SizeY;
	const EPixelFormat PixelFormat = InSourceTexture->GetPixelFormat();

	TArrayView64<uint8> PixelsView;
	FByteBulkData* SourceRawImageData = nullptr;

	if (InCopySourceImage)
	{
		const int32 NumBlocksX = TextureWidth / GPixelFormats[PixelFormat].BlockSizeX;
		const int32 NumBlocksY = TextureHeight / GPixelFormats[PixelFormat].BlockSizeY;
		FGuardedInt64 BytesForImageGuarded = FGuardedInt64(NumBlocksX) * NumBlocksY * GPixelFormats[PixelFormat].BlockBytes;
		int64 BytesForImage = BytesForImageGuarded.Get(0);
		check(BytesForImage > 0);

		SourceRawImageData = &SourceMip->BulkData;
		FColor* SourceColorData = static_cast<FColor*>(SourceRawImageData->Lock(LOCK_READ_ONLY));
		check(SourceColorData);

		PixelsView = TArrayView64<uint8>((uint8*)SourceColorData, BytesForImage);
	}

	UTexture2D* OutCreatedResult = UTexture2D::CreateTransient(TextureWidth, TextureHeight, PixelFormat, *InTextureName, PixelsView);

	if (InSourceTexture->SRGB != OutCreatedResult->SRGB)
	{
		OutCreatedResult->SRGB = InSourceTexture->SRGB;
	}

	if (InCopySourceImage)
	{
		SourceRawImageData->Unlock();
	}

	//Add to root set incase it gets GCed.
	//TODO: RemoveFromRoot
	OutCreatedResult->AddToRoot();
	//OutCreatedResult->RemoveFromRoot();

	FAssetCompilingManager::Get().ProcessAsyncTasks();
	FAssetCompilingManager::Get().FinishAllCompilation();

	return OutCreatedResult;
}

void UMultiThreadingSampleBPLibrary::FilterTextureUsingParallelFor(UTexture2D* InSourceTexture, EFilterType InFilterType, int InFilterSize, float InScaleValue, bool InForceSingleThread, UTexture2D*& OutFilteredTexture)
{
	if (!ValidateParameters(InSourceTexture, InFilterSize, InScaleValue))
	{
		OutFilteredTexture = nullptr;
		return;
	}

	UTexture2D* VerticalPassResult = CreateTransientTextureFromSource(InSourceTexture, TEXT("VerticalPassResult"));
	UTexture2D* HorizontalPassResult = CreateTransientTextureFromSource(InSourceTexture, TEXT("HorizontalPassResult"));
	UTexture2D* ScaleAlphaResult = CreateTransientTextureFromSource(InSourceTexture, TEXT("ScaleAlphaResult"));
	UTexture2D* CompositeResult = CreateTransientTextureFromSource(InSourceTexture, TEXT("CompositeResult"));

	FilterTexture(InSourceTexture, VerticalPassResult, InFilterType, InFilterSize, true, InForceSingleThread);
	VerticalPassResult->UpdateResource();

	FilterTexture(VerticalPassResult, HorizontalPassResult, InFilterType, InFilterSize, false, InForceSingleThread);
	HorizontalPassResult->UpdateResource();

	ScaleAlphaChannel(InSourceTexture, ScaleAlphaResult, InScaleValue, InForceSingleThread);
	ScaleAlphaResult->UpdateResource();

	CompositeRGBAValue(HorizontalPassResult, ScaleAlphaResult, CompositeResult, InForceSingleThread);
	CompositeResult->UpdateResource();

	OutFilteredTexture = CompositeResult;
}

void UMultiThreadingSampleBPLibrary::FilterTextureUsingTaskSystem(UTexture2D* InSourceTexture, EFilterType InFilterType, int InFilterSize, float InScaleValue, UResultUsingTaskSystem*& OutResult)
{
	if (!ValidateParameters(InSourceTexture, InFilterSize, InScaleValue))
	{
		OutResult = nullptr;
		return;
	}

	UTexture2D* VerticalPassResult = CreateTransientTextureFromSource(InSourceTexture, TEXT("VerticalPassResult"));
	UTexture2D* HorizontalPassResult = CreateTransientTextureFromSource(InSourceTexture, TEXT("HorizontalPassResult"));
	//We need ScaleAlphaChannelInput here because the first filter task and scale alpha channel task could overlap their execution.
	//The calling of Lock() and Unlock() could assert in such case if we pass InSourceTexture to both tasks.
	//We just duplicate InSourceTexture to another texture which we pass to scale alpha channel task for simplicity.
	UTexture2D* ScaleAlphaChannelInput = CreateTransientTextureFromSource(InSourceTexture, TEXT("ScaleAlphaChannelInput"), true);
	UTexture2D* ScaleAlphaChannelResult = CreateTransientTextureFromSource(InSourceTexture, TEXT("ScaleAlphaChannelResult"));
	UTexture2D* CompositeResult = CreateTransientTextureFromSource(InSourceTexture, TEXT("CompositeResult"));

	auto VerticalPassTask = UE::Tasks::Launch(
		UE_SOURCE_LOCATION,
		[SourceTexture = TWeakObjectPtr<UTexture2D>(InSourceTexture), FilteredResult = TWeakObjectPtr<UTexture2D>(VerticalPassResult), InFilterType, InFilterSize]()
		{
			FilterTexture(SourceTexture, FilteredResult, InFilterType, InFilterSize, true, false);
		},
		LowLevelTasks::ETaskPriority::BackgroundHigh,
		UE::Tasks::EExtendedTaskPriority::None
	);

	auto VerticalPassResultUpdateTask = UE::Tasks::Launch(
		UE_SOURCE_LOCATION,
		[TextureToUpdate = TWeakObjectPtr<UTexture2D>(VerticalPassResult)]()
		{
			TextureToUpdate->UpdateResource();
		},
		UE::Tasks::Prerequisites(VerticalPassTask),
		LowLevelTasks::ETaskPriority::BackgroundHigh,
		UE::Tasks::EExtendedTaskPriority::GameThreadNormalPri //Executed on GameThread.
	);

	auto HorizontalPassTask = UE::Tasks::Launch(
		UE_SOURCE_LOCATION,
		[SourceTexture = TWeakObjectPtr<UTexture2D>(VerticalPassResult), FilteredResult = TWeakObjectPtr<UTexture2D>(HorizontalPassResult), InFilterType, InFilterSize]()
		{
			FilterTexture(SourceTexture, FilteredResult, InFilterType, InFilterSize, false, false);
		},
		UE::Tasks::Prerequisites(VerticalPassResultUpdateTask),
		LowLevelTasks::ETaskPriority::BackgroundHigh,
		UE::Tasks::EExtendedTaskPriority::None
	);

	auto HorizontalPassResultUpdateTask = UE::Tasks::Launch(
		UE_SOURCE_LOCATION,
		[TextureToUpdate = TWeakObjectPtr<UTexture2D>(HorizontalPassResult)]()
		{
			TextureToUpdate->UpdateResource();
		},
		UE::Tasks::Prerequisites(HorizontalPassTask),
		LowLevelTasks::ETaskPriority::BackgroundHigh,
		UE::Tasks::EExtendedTaskPriority::GameThreadNormalPri //Executed on GameThread.
	);

	auto ScaleAlphaChannelTask = UE::Tasks::Launch(
		UE_SOURCE_LOCATION,
		[SourceTexture = TWeakObjectPtr<UTexture2D>(ScaleAlphaChannelInput), ScaledResult = TWeakObjectPtr<UTexture2D>(ScaleAlphaChannelResult), InScaleValue]()
		{
			ScaleAlphaChannel(SourceTexture, ScaledResult, InScaleValue, false);
		},
		LowLevelTasks::ETaskPriority::BackgroundHigh,
		UE::Tasks::EExtendedTaskPriority::None
	);

	auto ScaleAlphaChannelResultUpdateTask = UE::Tasks::Launch(
		UE_SOURCE_LOCATION,
		[TextureToUpdate = TWeakObjectPtr<UTexture2D>(ScaleAlphaChannelResult)]()
		{
			TextureToUpdate->UpdateResource();
		},
		UE::Tasks::Prerequisites(ScaleAlphaChannelTask),
		LowLevelTasks::ETaskPriority::BackgroundHigh,
		UE::Tasks::EExtendedTaskPriority::GameThreadNormalPri //Executed on GameThread.
	);

	auto CompositeTask = UE::Tasks::Launch(
		UE_SOURCE_LOCATION,
		[RGBTexture = TWeakObjectPtr<UTexture2D>(HorizontalPassResult), AlphaTexture = TWeakObjectPtr<UTexture2D>(ScaleAlphaChannelResult), Result = TWeakObjectPtr<UTexture2D>(CompositeResult)]()
		{
			CompositeRGBAValue(RGBTexture, AlphaTexture, Result, false);
		},
		UE::Tasks::Prerequisites(HorizontalPassResultUpdateTask, ScaleAlphaChannelResultUpdateTask),
		LowLevelTasks::ETaskPriority::BackgroundHigh,
		UE::Tasks::EExtendedTaskPriority::None
	);

	auto CompositeResultUpdateTask = UE::Tasks::Launch(
		UE_SOURCE_LOCATION,
		[TextureToUpdate = TWeakObjectPtr<UTexture2D>(CompositeResult)]()
		{
			TextureToUpdate->UpdateResource();
		},
		UE::Tasks::Prerequisites(CompositeTask),
		LowLevelTasks::ETaskPriority::BackgroundHigh,
		UE::Tasks::EExtendedTaskPriority::GameThreadNormalPri //Executed on GameThread.
	);

	OutResult = NewObject<UResultUsingTaskSystem>();

	OutResult->SetResult(CompositeResult, CompositeResultUpdateTask);
}

//When using Task Graph System, user needs to define their own task class/struct.
class FMyTask
{
public:
	FMyTask(float InValue): SomeVar(InValue)
	{
	}

	//Defines some stats related functionality.
	TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FMyTask, STATGROUP_TaskGraphTasks);
	}

	//Defines which thread to excute this task.
	static ENamedThreads::Type GetDesiredThread()
	{
		return ENamedThreads::AnyBackgroundHiPriTask;
	}

	//Defines how Task Graph System treats the subsequents of this task.
	//Can be ESubsequentsMode::TrackSubsequents or ESubsequentsMode::FireAndForget
	static ESubsequentsMode::Type GetSubsequentsMode()
	{
		return ESubsequentsMode::TrackSubsequents;
	}

	//Defines the task body.
	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		SomeVar = SomeVar * 2.0f;
	}

private:
	float SomeVar;
};

class FTextureFilterTask
{
public:
	FTextureFilterTask(TWeakObjectPtr<UTexture2D> InSourceTexture, TWeakObjectPtr<UTexture2D> InFilteredTexture, EFilterType InFilterType, int InFilterSize, bool InVertical)
		:FilterType(InFilterType), FilterSize(InFilterSize), Vertival(InVertical), SourceTexture(InSourceTexture), FilteredTexture(InFilteredTexture)
	{
	}

	TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FTextureFilterTask, STATGROUP_TaskGraphTasks);
	}

	static ENamedThreads::Type GetDesiredThread()
	{
		return ENamedThreads::AnyBackgroundHiPriTask;
	}

	static ESubsequentsMode::Type GetSubsequentsMode()
	{
		return ESubsequentsMode::TrackSubsequents;
	}

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		FilterTexture(SourceTexture, FilteredTexture, FilterType, FilterSize, Vertival, false);
	}

private:
	EFilterType FilterType = EFilterType::BoxFilter;
	int FilterSize = 3;
	bool Vertival = false;

	TWeakObjectPtr<UTexture2D> SourceTexture;
	TWeakObjectPtr<UTexture2D> FilteredTexture;
};

class FScaleAlphaChannelTask
{
public:
	FScaleAlphaChannelTask(TWeakObjectPtr<UTexture2D> InSourceTexture, TWeakObjectPtr<UTexture2D> InScaledTexture, float InScaleValue)
		: ScaleValue(InScaleValue), SourceTexture(InSourceTexture), ScaledTexture(InScaledTexture)
	{
	}

	TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FScaleAlphaChannelTask, STATGROUP_TaskGraphTasks);
	}

	static ENamedThreads::Type GetDesiredThread()
	{
		return ENamedThreads::AnyBackgroundHiPriTask;
	}

	static ESubsequentsMode::Type GetSubsequentsMode()
	{
		return ESubsequentsMode::TrackSubsequents;
	}

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		ScaleAlphaChannel(SourceTexture, ScaledTexture, ScaleValue, false);
	}

private:
	float ScaleValue = 0.5;

	TWeakObjectPtr<UTexture2D> SourceTexture;
	TWeakObjectPtr<UTexture2D> ScaledTexture;
};

class FCompositeRGBAValueTask
{
public:
	FCompositeRGBAValueTask(TWeakObjectPtr<UTexture2D> InRGBTexture, TWeakObjectPtr<UTexture2D> InATexture, TWeakObjectPtr<UTexture2D> OutTexture)
		: RGBTexture(InRGBTexture), AlphaTexture(InATexture), CompositedTexture(OutTexture)
	{
	}

	TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FCompositeRGBAValueTask, STATGROUP_TaskGraphTasks);
	}

	static ENamedThreads::Type GetDesiredThread()
	{
		return ENamedThreads::AnyBackgroundHiPriTask;
	}

	static ESubsequentsMode::Type GetSubsequentsMode()
	{
		return ESubsequentsMode::TrackSubsequents;
	}

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		CompositeRGBAValue(RGBTexture, AlphaTexture, CompositedTexture, false);
	}

private:
	TWeakObjectPtr<UTexture2D> RGBTexture;
	TWeakObjectPtr<UTexture2D> AlphaTexture;
	TWeakObjectPtr<UTexture2D> CompositedTexture;
};

class FUpdateResourceTask
{
public:
	FUpdateResourceTask(TWeakObjectPtr<UTexture2D> InTextureToUpdate)
		: TextureToUpdate(InTextureToUpdate)
	{
	}

	TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FUpdateResourceTask, STATGROUP_TaskGraphTasks);
	}

	static ENamedThreads::Type GetDesiredThread()
	{
		//Executed on GameThread.
		return ENamedThreads::GameThread;
	}

	static ESubsequentsMode::Type GetSubsequentsMode()
	{
		return ESubsequentsMode::TrackSubsequents;
	}

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		TextureToUpdate->UpdateResource();
	}

private:
	TWeakObjectPtr<UTexture2D> TextureToUpdate;
};

void UMultiThreadingSampleBPLibrary::FilterTextureUsingTaskGraphSystem(UTexture2D* InSourceTexture, EFilterType InFilterType, int InFilterSize, float InScaleValue, bool InHoldSourceTasks, UResultUsingTaskGraphSystem*& OutResult)
{
	if (!ValidateParameters(InSourceTexture, InFilterSize, InScaleValue))
	{
		OutResult = nullptr;
		return;
	}

	UTexture2D* VerticalPassResult = CreateTransientTextureFromSource(InSourceTexture, TEXT("VerticalPassResult"));
	UTexture2D* HorizontalPassResult = CreateTransientTextureFromSource(InSourceTexture, TEXT("HorizontalPassResult"));
	//We need ScaleAlphaChannelInput here because the first filter task and scale alpha channel task could overlap their execution.
	//The calling of Lock() and Unlock() could assert in such case if we pass InSourceTexture to both tasks.
	//We just duplicate InSourceTexture to another texture which we pass to scale alpha channel task for simplicity.
	UTexture2D* ScaleAlphaChannelInput = CreateTransientTextureFromSource(InSourceTexture, TEXT("ScaleAlphaChannelInput"), true);
	UTexture2D* ScaleAlphaChannelResult = CreateTransientTextureFromSource(InSourceTexture, TEXT("ScaleAlphaChannelResult"));
	UTexture2D* CompositeResult = CreateTransientTextureFromSource(InSourceTexture, TEXT("CompositeResult"));

	//Construnt and hold or construct and dispatch when ready.
	//If construct and hold, the task will not start execute until we explicitly unlock it(And of cource its subsequents will not execute).
	auto VerticalPassTask = InHoldSourceTasks ? TGraphTask<FTextureFilterTask>::CreateTask(nullptr, ENamedThreads::GameThread).ConstructAndHold(TWeakObjectPtr<UTexture2D>(InSourceTexture), TWeakObjectPtr<UTexture2D>(VerticalPassResult), InFilterType, InFilterSize, true)
		: TGraphTask<FTextureFilterTask>::CreateTask(nullptr, ENamedThreads::GameThread).ConstructAndDispatchWhenReady(TWeakObjectPtr<UTexture2D>(InSourceTexture), TWeakObjectPtr<UTexture2D>(VerticalPassResult), InFilterType, InFilterSize, true);

	FGraphEventArray Prerequisites1;
	Prerequisites1.Add(VerticalPassTask);

	auto VerticalPassResultUpdateTask = TGraphTask<FUpdateResourceTask>::CreateTask(&Prerequisites1, ENamedThreads::GameThread).ConstructAndDispatchWhenReady(TWeakObjectPtr<UTexture2D>(VerticalPassResult));

	FGraphEventArray Prerequisites2;
	Prerequisites2.Add(VerticalPassResultUpdateTask);

	auto HorizontalPassTask = TGraphTask<FTextureFilterTask>::CreateTask(&Prerequisites2, ENamedThreads::GameThread).ConstructAndDispatchWhenReady(TWeakObjectPtr<UTexture2D>(VerticalPassResult), TWeakObjectPtr<UTexture2D>(HorizontalPassResult), InFilterType, InFilterSize, false);

	FGraphEventArray Prerequisites3;
	Prerequisites3.Add(HorizontalPassTask);

	auto HorizontalPassResultUpdateTask = TGraphTask<FUpdateResourceTask>::CreateTask(&Prerequisites3, ENamedThreads::GameThread).ConstructAndDispatchWhenReady(TWeakObjectPtr<UTexture2D>(HorizontalPassResult));

	auto ScaleAlphaChannelTask = InHoldSourceTasks ? TGraphTask<FScaleAlphaChannelTask>::CreateTask(nullptr, ENamedThreads::GameThread).ConstructAndHold(TWeakObjectPtr<UTexture2D>(ScaleAlphaChannelInput), TWeakObjectPtr<UTexture2D>(ScaleAlphaChannelResult), InScaleValue)
		: TGraphTask<FScaleAlphaChannelTask>::CreateTask(nullptr, ENamedThreads::GameThread).ConstructAndDispatchWhenReady(TWeakObjectPtr<UTexture2D>(ScaleAlphaChannelInput), TWeakObjectPtr<UTexture2D>(ScaleAlphaChannelResult), InScaleValue);

	FGraphEventArray Prerequisites4;
	Prerequisites4.Add(ScaleAlphaChannelTask);

	auto ScaleAlphaChannelResultUpdateTask = TGraphTask<FUpdateResourceTask>::CreateTask(&Prerequisites4, ENamedThreads::GameThread).ConstructAndDispatchWhenReady(TWeakObjectPtr<UTexture2D>(ScaleAlphaChannelResult));

	FGraphEventArray Prerequisites5;
	Prerequisites5.Add(HorizontalPassResultUpdateTask);
	Prerequisites5.Add(ScaleAlphaChannelResultUpdateTask);

	auto CompositeTask = TGraphTask<FCompositeRGBAValueTask>::CreateTask(&Prerequisites5, ENamedThreads::GameThread).ConstructAndDispatchWhenReady(HorizontalPassResult, ScaleAlphaChannelResult, CompositeResult);

	FGraphEventArray Prerequisites6;
	Prerequisites6.Add(CompositeTask);

	auto CompositeResultUpdateTask = TGraphTask<FUpdateResourceTask>::CreateTask(&Prerequisites6, ENamedThreads::GameThread).ConstructAndDispatchWhenReady(TWeakObjectPtr<UTexture2D>(CompositeResult));

	if (InHoldSourceTasks)
	{
		//Let the task begin execute(Let the scheduler schedule the task to be executed on a worker thread).
		VerticalPassTask->Unlock();
		ScaleAlphaChannelTask->Unlock();
	}

	OutResult = NewObject<UResultUsingTaskGraphSystem>();

	OutResult->SetResult(CompositeResult, CompositeResultUpdateTask);
}

void UMultiThreadingSampleBPLibrary::FilterTextureUsingPipe(UTexture2D* InSourceTexture, EFilterType InFilterType, int InFilterSize, float InScaleValue, UResultUsingPipe*& OutResult)
{
	if (!ValidateParameters(InSourceTexture, InFilterSize, InScaleValue))
	{
		OutResult = nullptr;
		return;
	}

	UTexture2D* VerticalPassResult = CreateTransientTextureFromSource(InSourceTexture, TEXT("VerticalPassResult"));
	UTexture2D* HorizontalPassResult = CreateTransientTextureFromSource(InSourceTexture, TEXT("HorizontalPassResult"));
	//We dont need this anymore, as we are launching tasks through FPipe.
	// UTexture2D* ScaleAlphaChannelInput = CreateTransientTextureFromSource(InSourceTexture, TEXT("ScaleAlphaChannelInput"), true);
	UTexture2D* ScaleAlphaChannelResult = CreateTransientTextureFromSource(InSourceTexture, TEXT("ScaleAlphaChannelResult"));
	UTexture2D* CompositeResult = CreateTransientTextureFromSource(InSourceTexture, TEXT("CompositeResult"));

	//We are launching tasks through FPipe.
	TUniquePtr<UE::Tasks::FPipe> Pipe = MakeUnique<UE::Tasks::FPipe>(TEXT("TextureFilterPipe"));

	auto VerticalPassTask = Pipe->Launch(
		UE_SOURCE_LOCATION,
		[SourceTexture = TWeakObjectPtr<UTexture2D>(InSourceTexture), FilteredResult = TWeakObjectPtr<UTexture2D>(VerticalPassResult), InFilterType, InFilterSize]()
		{
			FilterTexture(SourceTexture, FilteredResult, InFilterType, InFilterSize, true, false);
		},
		LowLevelTasks::ETaskPriority::BackgroundHigh,
		UE::Tasks::EExtendedTaskPriority::None
	);

	auto VerticalPassResultUpdateTask = Pipe->Launch(
		UE_SOURCE_LOCATION,
		[TextureToUpdate = TWeakObjectPtr<UTexture2D>(VerticalPassResult)]()
		{
			TextureToUpdate->UpdateResource();
		},
		UE::Tasks::Prerequisites(VerticalPassTask),
		LowLevelTasks::ETaskPriority::BackgroundHigh,
		UE::Tasks::EExtendedTaskPriority::GameThreadNormalPri //Executed on GameThread.
	);

	auto HorizontalPassTask = Pipe->Launch(
		UE_SOURCE_LOCATION,
		[SourceTexture = TWeakObjectPtr<UTexture2D>(VerticalPassResult), FilteredResult = TWeakObjectPtr<UTexture2D>(HorizontalPassResult), InFilterType, InFilterSize]()
		{

			FilterTexture(SourceTexture, FilteredResult, InFilterType, InFilterSize, false, false);
		},
		UE::Tasks::Prerequisites(VerticalPassResultUpdateTask),
		LowLevelTasks::ETaskPriority::BackgroundHigh,
		UE::Tasks::EExtendedTaskPriority::None
	);

	auto HorizontalPassResultUpdateTask = Pipe->Launch(
		UE_SOURCE_LOCATION,
		[TextureToUpdate = TWeakObjectPtr<UTexture2D>(HorizontalPassResult)]()
		{
			TextureToUpdate->UpdateResource();
		},
		UE::Tasks::Prerequisites(HorizontalPassTask),
		LowLevelTasks::ETaskPriority::BackgroundHigh,
		UE::Tasks::EExtendedTaskPriority::GameThreadNormalPri //Executed on GameThread.
	);

	auto ScaleAlphaChannelTask = Pipe->Launch(
		UE_SOURCE_LOCATION,
		[SourceTexture = TWeakObjectPtr<UTexture2D>(/*ScaleAlphaChannelInput*/InSourceTexture), ScaledResult = TWeakObjectPtr<UTexture2D>(ScaleAlphaChannelResult), InScaleValue]()
		{
			ScaleAlphaChannel(SourceTexture, ScaledResult, InScaleValue, false);
		},
		LowLevelTasks::ETaskPriority::BackgroundHigh,
		UE::Tasks::EExtendedTaskPriority::None
	);

	auto ScaleAlphaChannelResultUpdateTask = Pipe->Launch(
		UE_SOURCE_LOCATION,
		[TextureToUpdate = TWeakObjectPtr<UTexture2D>(ScaleAlphaChannelResult)]()
		{
			TextureToUpdate->UpdateResource();
		},
		UE::Tasks::Prerequisites(ScaleAlphaChannelTask),
		LowLevelTasks::ETaskPriority::BackgroundHigh,
		UE::Tasks::EExtendedTaskPriority::GameThreadNormalPri //Executed on GameThread.
	);

	auto CompositeTask = Pipe->Launch(
		UE_SOURCE_LOCATION,
		[RGBTexture = TWeakObjectPtr<UTexture2D>(HorizontalPassResult), AlphaTexture = TWeakObjectPtr<UTexture2D>(ScaleAlphaChannelResult), Result = TWeakObjectPtr<UTexture2D>(CompositeResult)]()
		{
			CompositeRGBAValue(RGBTexture, AlphaTexture, Result, false);
		},
		UE::Tasks::Prerequisites(HorizontalPassResultUpdateTask, ScaleAlphaChannelResultUpdateTask),
		LowLevelTasks::ETaskPriority::BackgroundHigh,
		UE::Tasks::EExtendedTaskPriority::None
	);

	auto CompositeResultUpdateTask = Pipe->Launch(
		UE_SOURCE_LOCATION,
		[TextureToUpdate = TWeakObjectPtr<UTexture2D>(CompositeResult)]()
		{
			TextureToUpdate->UpdateResource();
		},
		UE::Tasks::Prerequisites(CompositeTask),
		LowLevelTasks::ETaskPriority::BackgroundHigh,
		UE::Tasks::EExtendedTaskPriority::GameThreadNormalPri //Executed on GameThread.
	);

	OutResult = NewObject<UResultUsingPipe>();

	OutResult->SetResult(CompositeResult, MoveTemp(Pipe));
}

//Notice that nested task is used to define the completion timeing of the outer task, not the execution order of outer task and nested task.
//Its kinda like that the nested task is a prerequisite of the outer task but its really not that.
//Nested task can execute concurrently with outer task, but a task can noly begin execute when all its prerequisites are completed.
void UMultiThreadingSampleBPLibrary::ExecuteNestedTask(int InCurrentCallIndex)
{
	auto AnotherNestedTask = UE::Tasks::Launch(
		UE_SOURCE_LOCATION,
		[=]()
		{
			UE_LOG(LogMultiThreadingSample, Display, TEXT("CurrentIndex:%d(ThreadID:%d):Executing another nested task."), InCurrentCallIndex, FPlatformTLS::GetCurrentThreadId());
			FPlatformProcess::Sleep(0.4);
		},
		UE::Tasks::ETaskPriority::BackgroundLow,
		UE::Tasks::EExtendedTaskPriority::None
	);

	auto OuterTask = UE::Tasks::Launch(
		UE_SOURCE_LOCATION,
		[&]()
		{
			//We are lauching inside a task.
			auto NestedTask = UE::Tasks::Launch(
				UE_SOURCE_LOCATION,
				[=]()
				{
					UE_LOG(LogMultiThreadingSample, Display, TEXT("CurrentIndex:%d(ThreadID:%d):Executing nested task."), InCurrentCallIndex, FPlatformTLS::GetCurrentThreadId());
					FPlatformProcess::Sleep(0.3);
				},
				UE::Tasks::ETaskPriority::BackgroundLow,
				UE::Tasks::EExtendedTaskPriority::None
			);

			UE::Tasks::AddNested(AnotherNestedTask);
			UE::Tasks::AddNested(NestedTask);

			UE_LOG(LogMultiThreadingSample, Display, TEXT("CurrentIndex:%d(ThreadID:%d):Executing outer task."), InCurrentCallIndex, FPlatformTLS::GetCurrentThreadId());
			FPlatformProcess::Sleep(0.1);
		},
		UE::Tasks::ETaskPriority::BackgroundLow,
		UE::Tasks::EExtendedTaskPriority::None
	);

	//Here we dont really care about the result, just wait with a timeout.
	OuterTask.Wait(FTimespan::FromMilliseconds(100));
	
	//This can be true or false, depending on:
	//1. Whether the nested tasks have completed or not.
	//2. If all the nested tasks have completed, Whether the OuterTask itself has completed or not.
	bool IsCompleted = OuterTask.IsCompleted();
}

void UMultiThreadingSampleBPLibrary::CreateRunnable(UMyRunnable*& OutMyRunnable)
{
	OutMyRunnable = NewObject<UMyRunnable>(GetTransientPackage());
}

void UMultiThreadingSampleBPLibrary::DoingThreadedWorkUsingFThread()
{
	//The threaded function that will be exectuted on other thread.
	auto ThreadedFunction = []() {
		FTimespan Timespan = FTimespan::FromSeconds(0.05);
		UE::FTimeout Timeout(Timespan);

		while (!Timeout.IsExpired())
		{
			UE_LOG(LogMultiThreadingSample, Display, TEXT("Running My FThread Threaded Function."));
			FPlatformProcess::Sleep(0.01);
		}
		};

	//The function that will be exectuted when multi threading is not supported or disabled.
	auto SingleThreadedTickFunction = []() {
		UE_LOG(LogMultiThreadingSample, Display, TEXT("Running My FThread Single Threaded Tick Function."));
		};

	TUniquePtr<FThread> Thread = MakeUnique<FThread>(
		TEXT("My FThread"),//The debug name of this thread.
		ThreadedFunction,
		SingleThreadedTickFunction,
		0,
		EThreadPriority::TPri_Lowest//The thread priority of this thread.
	);

	//The caller thread may continue do some other work while the created FThread instance is processing its work.
	FTimespan Timespan = FTimespan::FromSeconds(0.05);
	UE::FTimeout Timeout(Timespan);
	while (!Timeout.IsExpired())
	{
		UE_LOG(LogMultiThreadingSample, Display, TEXT("Caller is doing other work."));
		FPlatformProcess::Sleep(0.01);
	}

	//Before destroy the created FThread instance, Join() must be called to wait for the FThread instance to finish execute.
	Thread->Join();

	UE_LOG(LogMultiThreadingSample, Display, TEXT("FThread joined..."));

	Thread.Reset();
}

//When using Queued Thread Pool, user needs to impliment IQueuedWork interface, and override DoThreadedWork() function(and other virtual functions if needed).
class FDummyEmptyWork :public IQueuedWork
{
public:
	virtual void DoThreadedWork()override
	{
		UE_LOG(LogMultiThreadingSample, Display, TEXT("FDummyEmptyWork::DoThreadedWork()."));
		delete this;
	}

	virtual void Abandon()override
	{
		//Do nothing
	}

	virtual EQueuedWorkFlags GetQueuedWorkFlags()const override
	{
		return EQueuedWorkFlags::None;
	}

	virtual int64 GetRequiredMemory()const override
	{
		return -1;
	}

	virtual const TCHAR* GetDebugName()const override
	{
		return nullptr;
	}
};

//When using Queued Thread Pool, user needs to impliment IQueuedWork interface, and override DoThreadedWork() function(and other virtual functions if needed).
class FFibonacciComputationWork :public IQueuedWork
{
public:
	virtual void DoThreadedWork()override
	{
		UE_LOG(LogMultiThreadingSample, Display, TEXT("FFibonacciComputationWork::DoThreadedWork(). F(%d)=%d."), N, F(N));
		delete this;
	}

	virtual void Abandon()override
	{
		//Do nothing
	}

	FFibonacciComputationWork(int N)
	{
		this->N = N;
	}

private:
	int F(int Num)
	{
		if (Num == 0)
			return 0;
		if (Num == 1 || Num == 2)
			return 1;

		return F(Num - 1) + F(Num - 2);
	}

	int N = 0;
};

//When using Queued Thread Pool, user needs to impliment IQueuedWork interface, and override DoThreadedWork() function(and other virtual functions if needed).
class FOutputStringToLogWork :public IQueuedWork
{
public:
	virtual void DoThreadedWork()override
	{
		UE_LOG(LogMultiThreadingSample, Display, TEXT("FOutputStringToLogWork::DoThreadedWork(). Output content: %s."), *Content);
		delete this;
	}

	virtual void Abandon()override
	{
		//Do nothing
	}

	virtual EQueuedWorkFlags GetQueuedWorkFlags()const override
	{
		return EQueuedWorkFlags::None;
	}

	virtual int64 GetRequiredMemory()const override
	{
		return -1;
	}

	virtual const TCHAR* GetDebugName()const override
	{
		return DebugName;
	}

	FOutputStringToLogWork(const FString& InContent)
	{
		Content = InContent;
	}

private:
	static const TCHAR* DebugName;

	FString Content;
};

const TCHAR* FOutputStringToLogWork::DebugName = TEXT("OutputStringToLogTask");

//The above tasks can be done using FAutoDeleteAsyncTask(as they delete themselves when task is done).
class FAutoDeleteOutputStringToLogTask :public FNonAbandonableTask
{
	//Declare FAutoDeleteAsyncTask<FAutoDeleteOutputStringToLogTask> as a friend so it can access the private members of this class.
	friend class FAutoDeleteAsyncTask<FAutoDeleteOutputStringToLogTask>;
private:
	FAutoDeleteOutputStringToLogTask(const FString& InContent) :Content(InContent)
	{
	}

	//The threaded work to do.
	void DoWork()
	{
		UE_LOG(LogMultiThreadingSample, Display, TEXT("FAutoDeleteOutputStringToLogTask::DoWork(). Output content: %s."), *Content);
	}

	//Declare stat id.
	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FAutoDeleteOutputStringToLogTask, STATGROUP_ThreadPoolAsyncTasks);
	}

	FString Content;
};

template<class T>
const TCHAR* BuildStringFromIntArray(T& InStringBuilder, const TArray<int32>& InArray)
{
	InStringBuilder << "[";

	for (int i = 0; i < InArray.Num(); ++i)
	{
		if (i != InArray.Num() - 1)
		{
			InStringBuilder << InArray[i] << ",";
		}
		else
		{
			InStringBuilder << InArray[i];
		}
	}

	InStringBuilder << "]";

	return InStringBuilder.ToString();
}

//A task that use FAsyncTask to perform threaded work.
class FSortIntArrayTask :public FNonAbandonableTask
{
	//Again declare FAsyncTask<FSortIntArrayTask> as a friend.
	friend class FAsyncTask<FSortIntArrayTask>;
private:
	FSortIntArrayTask(const TArray<int32>& InArray)
		: ArrayToSort(InArray)
	{
	}

	//The threaded work to do.
	void DoWork()
	{
		if (!ArrayToSort.Num())
		{
			UE_LOG(LogMultiThreadingSample, Warning, TEXT("Empty array!!!"));
		}

		TStringBuilder<1024> StringBuilder;

		FString Content = BuildStringFromIntArray(StringBuilder, ArrayToSort);
		UE_LOG(LogMultiThreadingSample, Display, TEXT("FSortIntArrayWork::Before sort: %s"), *Content);

		Algo::Sort(ArrayToSort);

		StringBuilder.Reset();

		Content = BuildStringFromIntArray(StringBuilder, ArrayToSort);
		UE_LOG(LogMultiThreadingSample, Display, TEXT("FSortIntArrayWork::After sort: %s"), *Content);
	}

	//Declare stat id.
	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FSortIntArrayTask, STATGROUP_ThreadPoolAsyncTasks);
	}

	TArray<int32> ArrayToSort;

public:
	const TArray<int32>& GetArray() const
	{
		return ArrayToSort;
	}
};

void UMultiThreadingSampleBPLibrary::DoingThreadedWorkUsingQueuedThreadPool(const TArray<int32>& InArrayToSort, const FString& InStringToLog, int32 InFibonacciToCompute)
{
	FQueuedThreadPool* ThreadPool = GThreadPool;

#if WITH_EDITOR
	ThreadPool = GLargeThreadPool;
#endif

	check(ThreadPool);

	//Clamp incase deep recursive call / integer overflow
	InFibonacciToCompute = FMath::Clamp(InFibonacciToCompute, 0, 45);

	//Simply add work to the thread pool.
	ThreadPool->AddQueuedWork(new FDummyEmptyWork);
	ThreadPool->AddQueuedWork(new FFibonacciComputationWork(InFibonacciToCompute));
	ThreadPool->AddQueuedWork(new FOutputStringToLogWork(InStringToLog));

	//Start executing the task(the task will be auto deleted).
	(new FAutoDeleteAsyncTask<FAutoDeleteOutputStringToLogTask>(InStringToLog))->StartBackgroundTask(ThreadPool, EQueuedWorkPriority::Lowest);
	//Or just run the task on current thread.
	// (new FAutoDeleteAsyncTask<FAutoDeleteOutputStringToLogTask>(TEXT("Test from auto delete task")))->StartSynchronousTask();

	//Start executing the task.
	auto SortArrayTask = new FAsyncTask<FSortIntArrayTask>(InArrayToSort);
	SortArrayTask->StartBackgroundTask(ThreadPool, EQueuedWorkPriority::Highest);
	//Or just run the task on current thread.
	// SortArrayTask->StartSynchronousTask();

	if (SortArrayTask->IsDone()) //See if the task has completed.
	{
		UE_LOG(LogMultiThreadingSample, Display, TEXT("Sort array task has completed!"));
	} 
	else if (SortArrayTask->IsWorkDone()) //See if the work is done(But the task might not be completed).
	{
		UE_LOG(LogMultiThreadingSample, Display, TEXT("Sort array work is done!"));
	}

	UE_LOG(LogMultiThreadingSample, Display, TEXT("Ensure sort array task completion!"));
	SortArrayTask->EnsureCompletion(false/*bDoWorkOnThisThreadIfNotStarted*/, false/*bIsLatencySensitive*/);

	//Retrieve the result(and do something).
	FSortIntArrayTask& UserTask = SortArrayTask->GetTask();
	TStringBuilder<1024> StringBuilder;
	FString RetrievedResult = BuildStringFromIntArray(StringBuilder, UserTask.GetArray());

	UE_LOG(LogMultiThreadingSample, Display, TEXT("The retrieved sorted result: %s"), *RetrievedResult);

	//Now the time to delete our task.
	delete SortArrayTask;
}

void UMultiThreadingSampleBPLibrary::RunLowLevelTaskTest()
{
	int TestValue = 100;

	UE_LOG(LogMultiThreadingSample, Display, TEXT("Begin Running Low Level Task Test. TestValue = %d"), TestValue);

	//Create a low level task.
	TSharedPtr<LowLevelTasks::FTask> Task = MakeShared<LowLevelTasks::FTask>();

	//Initialize the low level task.
	Task->Init(
		TEXT("SimpleLowLevelTask"),
		LowLevelTasks::ETaskPriority::Default,
		[&]() {
			TestValue = 1337;
		},
		LowLevelTasks::ETaskFlags::DefaultFlags
	);

	//Try to launch the task(the scheduler will handle this).
	bool WasLaunched = LowLevelTasks::TryLaunch(*Task, LowLevelTasks::EQueuePreference::DefaultPreference, true/*bWakeUpWorker*/);

	//Try to cancel the task.
	bool WasCanceled = Task->TryCancel(LowLevelTasks::ECancellationFlags::DefaultFlags);

	if (WasCanceled)
	{
		//Try to revive the task.
		bool WasRevived = Task->TryRevive();
		
		if (!WasRevived)
		{
			//Wait low level task system marks this task as completed.
			while (!Task->IsCompleted())
			{
				FPlatformProcess::Sleep(0.005f);
			}

			check(Task->IsCompleted());
			check(TestValue == 100); //TestValue is unchanged(we cancled this task and failed to revive it).
		}
		else
		{
			//We cancled this task and then succeed to revive it.
			//Now we wait for the task to be completed.
			while (!Task->IsCompleted())
			{
				FPlatformProcess::Sleep(0.005f);
				//Try to expedite the task.
				// bool WasExpedite = Task->TryExpedite();
			}

			check(Task->IsCompleted());
			check(TestValue == 1337);
		}
	}
	else
	{
		//Wait for the task to be completed.
		while (!Task->IsCompleted())
		{
			FPlatformProcess::Sleep(0.005f);
			//Try to expedite the task.
			// bool WasExpedite = Task->TryExpedite();
		}

		check(Task->IsCompleted());
		check(TestValue == 1337);
	}

	UE_LOG(LogMultiThreadingSample, Display, TEXT("End Running Low Level Task Test. TestValue = %d"), TestValue);
}