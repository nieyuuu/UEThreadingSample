#include "AsyncLoadTextFile.h"

FString LoadTextFileToString(const FString& InFileName, float InSleepTimeInSeconds)
{
	//InSleepTimeInSeconds is used to simulate a long loading task and we clamp here to some range.
	InSleepTimeInSeconds = FMath::Clamp(InSleepTimeInSeconds, 0.0f, 5.0f);

	const FString FullPath = FPaths::ConvertRelativePathToFull(InFileName);
	FString OutFileContent;
	if (FFileHelper::LoadFileToString(OutFileContent, *FullPath))
	{
		UE_LOG(LogThreadingSample, Display, TEXT("Successfully loaded file: %s (Will now sleep for %f seconds)."), *InFileName, InSleepTimeInSeconds);
		FPlatformProcess::Sleep(InSleepTimeInSeconds);
		return MoveTemp(OutFileContent);
	}
	else
	{
		UE_LOG(LogThreadingSample, Warning, TEXT("Failed to load file: %s (Full Path: %s)."), *InFileName, *FullPath);
		return FString();
	}
}

UTextFileResult* LoadTextFile_AsyncInterface(const FString& InFileName, EAsyncExecution Execution, float InSleepTimeInSeconds)
{
	auto Result = NewObject<UTextFileResult>();
	Result->SetResult(InFileName, Async(
		Execution,
		[&InFileName, InSleepTimeInSeconds]() {
			return LoadTextFileToString(InFileName, InSleepTimeInSeconds);
		}));
	return Result;
}

UTextFileResult* LoadTextFile_AsyncPoolInterface(const FString& InFileName, float InSleepTimeInSeconds)
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

UTextFileResult* LoadTextFile_AsyncThreadInterface(const FString& InFileName, float InSleepTimeInSeconds)
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

UTextFileResult* LoadTextFile_AsyncTaskInterface(const FString& InFileName, float InSleepTimeInSeconds)
{
	//The promise and future
	TPromise<FString> Promise;
	auto Future = Promise.GetFuture();
	auto Result = NewObject<UTextFileResult>();

	AsyncTask(
		ENamedThreads::AnyThread,
		[LocalPromise = MoveTemp(Promise), &InFileName, InSleepTimeInSeconds]() {
			TPromise<FString>& MutablePromise = const_cast<TPromise<FString>&>(LocalPromise);
			//Set promise value in task body
			MutablePromise.SetValue(LoadTextFileToString(InFileName, InSleepTimeInSeconds));
		}
	);

	Result->SetResult(InFileName, MoveTemp(Future));
	return Result;
}