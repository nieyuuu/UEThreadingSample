#pragma once

#include "ThreadingSample/ThreadingSample.h"

#include "AsyncLoadTextFile.generated.h"

//Wrap the return value of the Async function into an UObject to be able to use it in Blueprints.
UCLASS(BlueprintType)
class UTextFileResult :public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Threading Sample")
	bool IsReady() const
	{
		check(Future.IsValid());
		//Returns whether the Future is ready or not.
		return Future.IsReady();
	}

	UFUNCTION(BlueprintCallable, Category = "Threading Sample")
	void PrintToLog()
	{
		check(Future.IsValid());
		if (!Future.IsReady())
			UE_LOG(LogThreadingSample, Warning, TEXT("Future is not ready and will block the caller thread."));

		UE_LOG(LogThreadingSample, Display, TEXT("[FileName:%s] [FileContent:%s]"), *FileName, *(Future.Get()));
	}

	UFUNCTION(BlueprintCallable, Category = "Threading Sample")
	void GetResult(FString& OutFileName, FString& OutFileContent)
	{
		check(Future.IsValid());
		if (!Future.IsReady())
			UE_LOG(LogThreadingSample, Warning, TEXT("Future is not ready and will block the caller thread."));

		OutFileName = FileName;
		//Calling Get() will block the caller until Future is ready.
		OutFileContent = Future.Get();
	}

	void SetResult(const FString& InFileName, TFuture<FString>&& InFuture)
	{
		FileName = InFileName;
		Future = MoveTemp(InFuture);
	}

private:
	FString FileName;
	TFuture<FString> Future;
};

FString LoadTextFileToString(const FString& InFileName, float InSleepTimeInSeconds = 0.0f);

UTextFileResult* LoadTextFile_AsyncInterface(const FString& InFileName, EAsyncExecution Execution, float InSleepTimeInSeconds = 0.0f);

UTextFileResult* LoadTextFile_AsyncPoolInterface(const FString& InFileName, float InSleepTimeInSeconds = 0.0f);

UTextFileResult* LoadTextFile_AsyncThreadInterface(const FString& InFileName, float InSleepTimeInSeconds = 0.0f);

UTextFileResult* LoadTextFile_AsyncTaskInterface(const FString& InFileName, float InSleepTimeInSeconds = 0.0f);
