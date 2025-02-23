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
	Result->SetFuture(InFileName, Async(
		EAsyncExecution::TaskGraph,
		[&InFileName, InSleepTimeInSeconds]() {
			return LoadTextFileToString(InFileName, InSleepTimeInSeconds);
		}));
	return Result;
}

auto LaunchAsyncLoadingTextFile_AsyncInterface_ThreadPool(const FString& InFileName, float InSleepTimeInSeconds = 0.0f)
{
	auto Result = NewObject<UTextFileResult>();
	Result->SetFuture(InFileName, Async(
		EAsyncExecution::ThreadPool,
		[&InFileName, InSleepTimeInSeconds]() {
			return LoadTextFileToString(InFileName, InSleepTimeInSeconds);
		}));
	return Result;
}

auto LaunchAsyncLoadingTextFile_AsyncInterface_Thread(const FString& InFileName, float InSleepTimeInSeconds = 0.0f)
{
	auto Result = NewObject<UTextFileResult>();
	Result->SetFuture(InFileName, Async(
		EAsyncExecution::Thread,
		[&InFileName, InSleepTimeInSeconds]() {
			return LoadTextFileToString(InFileName, InSleepTimeInSeconds);
		}));
	return Result;
}

auto LaunchAsyncLoadingTextFile_AsyncPoolInterface(const FString& InFileName, float InSleepTimeInSeconds = 0.0f)
{
	auto Result = NewObject<UTextFileResult>();
	Result->SetFuture(InFileName, AsyncPool(
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
	Result->SetFuture(InFileName, AsyncThread(
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

void BoxFilter(TWeakObjectPtr<UTexture2D> InSourceTexture, TWeakObjectPtr<UTexture2D> OutFilteredTexture, int InBoxSize, bool InForceSingleThread)
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

	const uint16 TextureWidth = SourceMip->SizeX;
	const uint16 TextureHeight = SourceMip->SizeY;

	const bool IsSRGB = InSourceTexture->SRGB;

	auto LoopBody = [&](int32 Index) {
		const int32 CurrentPixelPosX = Index % TextureWidth;
		const int32 CurrentPixelPosY = Index / TextureWidth;

		const int HalfBoxSize = InBoxSize / 2;

		int SampleCount = 0;
		float LinearSumR = 0.0f, LinearSumG = 0.0f, LinearSumB = 0.0f;

		for (int X = -HalfBoxSize; X <= HalfBoxSize; ++X)
		{
			for (int Y = -HalfBoxSize; Y <= HalfBoxSize; ++Y)
			{
				const FIntPoint SamplePosition = FIntPoint(CurrentPixelPosX + X, CurrentPixelPosY + Y);
				//If sample is within the range of the texture.
				if ((0 <= SamplePosition.X && SamplePosition.X < TextureWidth) && (0 <= SamplePosition.Y && SamplePosition.Y < TextureHeight))
				{
					SampleCount++;

					const FColor SampledColor = SourceColorData[SamplePosition.Y * TextureWidth + SamplePosition.X];

					if (IsSRGB)
					{
						//Convert to linear space.
						FLinearColor LinearColor = FLinearColor(SampledColor);
						LinearSumR += LinearColor.R;
						LinearSumG += LinearColor.G;
						LinearSumB += LinearColor.B;
					}
					else
					{
						//Already in linear space?
						LinearSumR += SampledColor.R / 255.0f;
						LinearSumG += SampledColor.G / 255.0f;
						LinearSumB += SampledColor.B / 255.0f;
					}
				}
			}
		}

		check(SampleCount > 0);
		const FColor Result = FLinearColor(LinearSumR / (float)SampleCount, LinearSumG / (float)SampleCount, LinearSumB / (float)SampleCount).ToFColor(IsSRGB);

		FilteredColorData[CurrentPixelPosY * TextureWidth + CurrentPixelPosX] = FColor(Result.R, Result.G, Result.B, SourceColorData[CurrentPixelPosY * TextureWidth + CurrentPixelPosX].A);
		};

	const double StartTime = FPlatformTime::Seconds();

	ParallelFor(
		TEXT("Parallel Box Filter"),
		TextureWidth * TextureHeight,
		1024,
		LoopBody,
		InForceSingleThread ? EParallelForFlags::ForceSingleThread : EParallelForFlags::None);

	const double EndTime = FPlatformTime::Seconds();

	UE_LOG(LogMultiThreadingSample, Display, TEXT("Box filter(%s, texture size: %dx%d, box size: %d) execution finished in: %f seconds."),
		InForceSingleThread ? TEXT("singlethreaded") : TEXT("multithreaded"),
		TextureWidth, TextureHeight, InBoxSize,
		EndTime - StartTime);

	FilteredRawImageData->Unlock();
	SourceRawImageData->Unlock();
}

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

bool ValidateParameters(UTexture2D* InSourceTexture, int InBoxSize, float InScaleValue)
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

	if (InBoxSize <= 2 || InBoxSize >= 64 || InBoxSize % 2 == 0)
	{
		UE_LOG(LogMultiThreadingSample, Warning, TEXT("Invalid box size:[%d]. Valid box size range [3, 63] and has to be an odd number."), InBoxSize);
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

	//Add to root set incase it get GCed.
	//TODO: RemoveFromRoot
	OutCreatedResult->AddToRoot();
	//OutCreatedResult->RemoveFromRoot();

	FAssetCompilingManager::Get().ProcessAsyncTasks();
	FAssetCompilingManager::Get().FinishAllCompilation();

	return OutCreatedResult;
}

void UMultiThreadingSampleBPLibrary::BoxFilterTextureUsingParallelFor(UTexture2D* InSourceTexture, int InBoxSize, float InScaleValue, bool InForceSingleThread, UTexture2D*& OutFilteredTexture)
{
	if (!ValidateParameters(InSourceTexture, InBoxSize, InScaleValue))
	{
		OutFilteredTexture = nullptr;
		return;
	}

	UTexture2D* FirstFilterResult = CreateTransientTextureFromSource(InSourceTexture, TEXT("FirstFilterResult"));
	UTexture2D* SecondFilterResult = CreateTransientTextureFromSource(InSourceTexture, TEXT("SecondFilterResult"));
	UTexture2D* ScaleAlphaResult = CreateTransientTextureFromSource(InSourceTexture, TEXT("ScaleAlphaResult"));
	UTexture2D* CompositeResult = CreateTransientTextureFromSource(InSourceTexture, TEXT("CompositeResult"));

	BoxFilter(InSourceTexture, FirstFilterResult, InBoxSize, InForceSingleThread);
	FirstFilterResult->UpdateResource();

	BoxFilter(FirstFilterResult, SecondFilterResult, InBoxSize, InForceSingleThread);
	SecondFilterResult->UpdateResource();

	ScaleAlphaChannel(InSourceTexture, ScaleAlphaResult, InScaleValue, InForceSingleThread);
	ScaleAlphaResult->UpdateResource();

	CompositeRGBAValue(SecondFilterResult, ScaleAlphaResult, CompositeResult, InForceSingleThread);
	CompositeResult->UpdateResource();

	OutFilteredTexture = CompositeResult;
}

void UMultiThreadingSampleBPLibrary::BoxFilterTextureUsingTaskSystem(UTexture2D* InSourceTexture, int InBoxSize, float InScaleValue, UResultUsingTaskSystem*& OutResult)
{
	if (!ValidateParameters(InSourceTexture, InBoxSize, InScaleValue))
	{
		OutResult = nullptr;
		return;
	}

	UTexture2D* FirstFilterResult = CreateTransientTextureFromSource(InSourceTexture, TEXT("FirstFilterResult"));
	UTexture2D* SecondFilterResult = CreateTransientTextureFromSource(InSourceTexture, TEXT("SecondFilterResult"));
	UTexture2D* ScaleAlphaChannelInput = CreateTransientTextureFromSource(InSourceTexture, TEXT("ScaleAlphaChannelInput"), true);
	UTexture2D* ScaleAlphaChannelResult = CreateTransientTextureFromSource(InSourceTexture, TEXT("ScaleAlphaChannelResult"));
	UTexture2D* CompositeResult = CreateTransientTextureFromSource(InSourceTexture, TEXT("CompositeResult"));

	auto FirstFilterTask = UE::Tasks::Launch(
		UE_SOURCE_LOCATION,
		[SourceTexture = TWeakObjectPtr<UTexture2D>(InSourceTexture), FilteredResult = TWeakObjectPtr<UTexture2D>(FirstFilterResult), InBoxSize]()
		{
			BoxFilter(SourceTexture, FilteredResult, InBoxSize, false);
		},
		LowLevelTasks::ETaskPriority::BackgroundHigh,
		UE::Tasks::EExtendedTaskPriority::None
	);

	auto FirstFilterResultUpdateTask = UE::Tasks::Launch(
		UE_SOURCE_LOCATION,
		[TextureToUpdate = TWeakObjectPtr<UTexture2D>(FirstFilterResult)]()
		{
			TextureToUpdate->UpdateResource();
		},
		UE::Tasks::Prerequisites(FirstFilterTask),
		LowLevelTasks::ETaskPriority::BackgroundHigh,
		UE::Tasks::EExtendedTaskPriority::GameThreadNormalPri //Executed on GameThread.
	);

	auto SecondFilterTask = UE::Tasks::Launch(
		UE_SOURCE_LOCATION,
		[SourceTexture = TWeakObjectPtr<UTexture2D>(FirstFilterResult), FilteredResult = TWeakObjectPtr<UTexture2D>(SecondFilterResult), InBoxSize]()
		{
			BoxFilter(SourceTexture, FilteredResult, InBoxSize, false);
		},
		UE::Tasks::Prerequisites(FirstFilterResultUpdateTask),
		LowLevelTasks::ETaskPriority::BackgroundHigh,
		UE::Tasks::EExtendedTaskPriority::None
	);

	auto SecondFilterResultUpdateTask = UE::Tasks::Launch(
		UE_SOURCE_LOCATION,
		[TextureToUpdate = TWeakObjectPtr<UTexture2D>(SecondFilterResult)]()
		{
			TextureToUpdate->UpdateResource();
		},
		UE::Tasks::Prerequisites(SecondFilterTask),
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
		[RGBTexture = TWeakObjectPtr<UTexture2D>(SecondFilterResult), AlphaTexture = TWeakObjectPtr<UTexture2D>(ScaleAlphaChannelResult), Result = TWeakObjectPtr<UTexture2D>(CompositeResult)]()
		{
			CompositeRGBAValue(RGBTexture, AlphaTexture, Result, false);
		},
		UE::Tasks::Prerequisites(SecondFilterResultUpdateTask, ScaleAlphaChannelResultUpdateTask),
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

class FBoxFilterTask
{
public:
	FBoxFilterTask(TWeakObjectPtr<UTexture2D> InSourceTexture, TWeakObjectPtr<UTexture2D> InFilteredTexture, int InBoxSize)
		: BoxSize(InBoxSize), SourceTexture(InSourceTexture), FilteredTexture(InFilteredTexture)
	{
	}

	TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FBoxFilterTask, STATGROUP_TaskGraphTasks);
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
		BoxFilter(SourceTexture, FilteredTexture, BoxSize, false);
	}

private:
	int BoxSize = 3;

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

void UMultiThreadingSampleBPLibrary::BoxFilterTextureUsingTaskGraphSystem(UTexture2D* InSourceTexture, int InBoxSize, float InScaleValue, bool InHoldSourceTasks, UResultUsingTaskGraphSystem*& OutResult)
{
	if (!ValidateParameters(InSourceTexture, InBoxSize, InScaleValue))
	{
		OutResult = nullptr;
		return;
	}

	UTexture2D* FirstFilterResult = CreateTransientTextureFromSource(InSourceTexture, TEXT("FirstFilterResult"));
	UTexture2D* SecondFilterResult = CreateTransientTextureFromSource(InSourceTexture, TEXT("SecondFilterResult"));
	UTexture2D* ScaleAlphaChannelInput = CreateTransientTextureFromSource(InSourceTexture, TEXT("ScaleAlphaChannelInput"), true);
	UTexture2D* ScaleAlphaChannelResult = CreateTransientTextureFromSource(InSourceTexture, TEXT("ScaleAlphaChannelResult"));
	UTexture2D* CompositeResult = CreateTransientTextureFromSource(InSourceTexture, TEXT("CompositeResult"));

	//Construnt and hold or construct and dispatch when ready.
	//If construct and hold, the task will not start execute until we unlock it(And of cource its subsequents will not execute).
	auto FirstFilterTask = InHoldSourceTasks ? TGraphTask<FBoxFilterTask>::CreateTask(nullptr, ENamedThreads::GameThread).ConstructAndHold(TWeakObjectPtr<UTexture2D>(InSourceTexture), TWeakObjectPtr<UTexture2D>(FirstFilterResult), InBoxSize)
		: TGraphTask<FBoxFilterTask>::CreateTask(nullptr, ENamedThreads::GameThread).ConstructAndDispatchWhenReady(TWeakObjectPtr<UTexture2D>(InSourceTexture), TWeakObjectPtr<UTexture2D>(FirstFilterResult), InBoxSize);

	FGraphEventArray Prerequisites1;
	Prerequisites1.Add(FirstFilterTask);

	auto FirstFilterResultUpdateTask = TGraphTask<FUpdateResourceTask>::CreateTask(&Prerequisites1, ENamedThreads::GameThread).ConstructAndDispatchWhenReady(TWeakObjectPtr<UTexture2D>(FirstFilterResult));

	FGraphEventArray Prerequisites2;
	Prerequisites2.Add(FirstFilterResultUpdateTask);

	auto SecondFilterTask = TGraphTask<FBoxFilterTask>::CreateTask(&Prerequisites2, ENamedThreads::GameThread).ConstructAndDispatchWhenReady(TWeakObjectPtr<UTexture2D>(FirstFilterResult), TWeakObjectPtr<UTexture2D>(SecondFilterResult), InBoxSize);

	FGraphEventArray Prerequisites3;
	Prerequisites3.Add(SecondFilterTask);

	auto SecondFilterResultUpdateTask = TGraphTask<FUpdateResourceTask>::CreateTask(&Prerequisites3, ENamedThreads::GameThread).ConstructAndDispatchWhenReady(TWeakObjectPtr<UTexture2D>(SecondFilterResult));

	auto ScaleAlphaChannelTask = InHoldSourceTasks ? TGraphTask<FScaleAlphaChannelTask>::CreateTask(nullptr, ENamedThreads::GameThread).ConstructAndHold(TWeakObjectPtr<UTexture2D>(ScaleAlphaChannelInput), TWeakObjectPtr<UTexture2D>(ScaleAlphaChannelResult), InScaleValue)
		: TGraphTask<FScaleAlphaChannelTask>::CreateTask(nullptr, ENamedThreads::GameThread).ConstructAndDispatchWhenReady(TWeakObjectPtr<UTexture2D>(ScaleAlphaChannelInput), TWeakObjectPtr<UTexture2D>(ScaleAlphaChannelResult), InScaleValue);

	FGraphEventArray Prerequisites4;
	Prerequisites4.Add(ScaleAlphaChannelTask);

	auto ScaleAlphaChannelResultUpdateTask = TGraphTask<FUpdateResourceTask>::CreateTask(&Prerequisites4, ENamedThreads::GameThread).ConstructAndDispatchWhenReady(TWeakObjectPtr<UTexture2D>(ScaleAlphaChannelResult));

	FGraphEventArray Prerequisites5;
	Prerequisites5.Add(SecondFilterResultUpdateTask);
	Prerequisites5.Add(ScaleAlphaChannelResultUpdateTask);

	auto CompositeTask = TGraphTask<FCompositeRGBAValueTask>::CreateTask(&Prerequisites5, ENamedThreads::GameThread).ConstructAndDispatchWhenReady(SecondFilterResult, ScaleAlphaChannelResult, CompositeResult);

	FGraphEventArray Prerequisites6;
	Prerequisites6.Add(CompositeTask);

	auto CompositeResultUpdateTask = TGraphTask<FUpdateResourceTask>::CreateTask(&Prerequisites6, ENamedThreads::GameThread).ConstructAndDispatchWhenReady(TWeakObjectPtr<UTexture2D>(CompositeResult));

	if (InHoldSourceTasks)
	{
		//Let the task begin execute(Let the scheduler schedule the task to be executed on a worker thread).
		FirstFilterTask->Unlock();
		ScaleAlphaChannelTask->Unlock();
	}

	OutResult = NewObject<UResultUsingTaskGraphSystem>();

	OutResult->SetResult(CompositeResult, CompositeResultUpdateTask);
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

void UMultiThreadingSampleBPLibrary::DoingThreadedWorkUsingQueuedThreadPool()
{
	FQueuedThreadPool* ThreadPool = GThreadPool;

#if WITH_EDITOR
	ThreadPool = GLargeThreadPool;
#endif

	check(ThreadPool);

	ThreadPool->AddQueuedWork(new FDummyEmptyWork);
	ThreadPool->AddQueuedWork(new FFibonacciComputationWork(20));
	ThreadPool->AddQueuedWork(new FFibonacciComputationWork(15));
	ThreadPool->AddQueuedWork(new FOutputStringToLogWork("Test"));
	ThreadPool->AddQueuedWork(new FOutputStringToLogWork("Test1"));
}
