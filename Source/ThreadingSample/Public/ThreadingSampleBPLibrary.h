#pragma once

#include "CoreMinimal.h"
#include "Misc/Timespan.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "AsyncLoadTextFile.h"
#include "FRunnable.h"
#include "FThread.h"
#include "TextureProcessing.h"

#include "ThreadingSampleBPLibrary.generated.h"

//Define the different ways to execute the async loading of the text files.
UENUM(BlueprintType)
enum class ELoadTextFileExecution : uint8
{
	AsyncInterface_TaskGraph,  //EAsyncExecution::TaskGraph
	AsyncInterface_ThreadPool, //EAsyncExecution::ThreadPool
	AsyncInterface_Thread,     //EAsyncExecution::Thread
	AsyncTaskInterface,        //AsyncTask()
	AsyncPoolInterface,        //AsyncPool()
	AsyncThreadInterface       //AsyncThread()
};

UENUM(BlueprintType)
enum class EThreadPoolWrapperType : uint8
{
	SimpleWrapper,
	DynamicWrapper,
	TaskGraphWrapper,
	LowLevelTaskWrapper,
};

//Wrap the result returned using task system
UCLASS(BlueprintType)
class UResultUsingTaskSystem :public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Threading Sample")
	bool IsReady() const
	{
		check(Result && TaskHandle.IsValid());
		return TaskHandle.IsCompleted();
	}

	UFUNCTION(BlueprintCallable, Category = "Threading Sample")
	UTexture2D* GetResult()
	{
		check(Result && TaskHandle.IsValid());

		//Explicitly wait the task to finish(We dont Wait here because calling Wait() will block the caller).
		//
		//Wait until task finishing
		// TaskHandle.Wait();
		//
		//Wait until task finishing but with a timeout
		// FTimespan WaitTime = FTimespan::FromMilliseconds(2);
		// TaskHandle.Wait(WaitTime);

		if (TaskHandle.IsCompleted())
		{
			return Result;
		}
		else
		{
			return nullptr;
		}
	}

	void SetResult(UTexture2D* InTexture, UE::Tasks::FTask InTaskHandle)
	{
		check(InTexture && InTaskHandle.IsValid());
		check(!Result && !TaskHandle.IsValid());

		Result = InTexture;
		TaskHandle = InTaskHandle;
	}

private:
	UTexture2D* Result;

	UE::Tasks::FTask TaskHandle;
};

//Wrap the result returned using task graph system
UCLASS(BlueprintType)
class UResultUsingTaskGraphSystem :public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Threading Sample")
	bool IsReady() const
	{
		check(Result && TaskEvent.IsValid());
		return TaskEvent->IsCompleted();
	}

	UFUNCTION(BlueprintCallable, Category = "Threading Sample")
	UTexture2D* GetResult()
	{
		check(Result && TaskEvent.IsValid());

		//Explicitly wait the task to finish(We dont Wait here because calling Wait() will block the caller).
		// TaskEvent->Wait();

		if (TaskEvent->IsCompleted())
		{
			return Result;
		}
		else
		{
			return nullptr;
		}
	}

	void SetResult(UTexture2D* InTexture, FGraphEventRef InTaskEvent)
	{
		check(InTexture && InTaskEvent.IsValid());
		check(!Result && !TaskEvent.IsValid());

		Result = InTexture;
		TaskEvent = InTaskEvent;
	}

private:
	UTexture2D* Result;

	FGraphEventRef TaskEvent;
};

//Wrap the result returned using pipe
UCLASS(BlueprintType)
class UResultUsingPipe :public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Threading Sample")
	bool IsReady() const
	{
		check(Result && Pipe.IsValid());
		return !Pipe->HasWork();
	}

	UFUNCTION(BlueprintCallable, Category = "Threading Sample")
	UTexture2D* GetResult()
	{
		check(Result && Pipe.IsValid());

		//Explicitly wait the to be empty(We dont Wait here because calling Wait() will block the caller).
		// Pipe->WaitUntilEmpty();
		//Wait with a timeout
		// FTimespan WaitTime = FTimespan::FromMilliseconds(2);
		// Pipe->WaitUntilEmpty(WaitTime);

		if (!Pipe->HasWork())
		{
			return Result;
		}
		else
		{
			return nullptr;
		}
	}

	void SetResult(UTexture2D* InTexture, TUniquePtr<UE::Tasks::FPipe> InPipe)
	{
		check(InTexture && InPipe.IsValid());
		check(!Result && !Pipe.IsValid());

		Result = InTexture;
		Pipe = MoveTemp(InPipe);
	}

	UResultUsingPipe()
	{
		Result = nullptr;
		Pipe = nullptr;
	}

private:
	UTexture2D* Result;

	TUniquePtr<UE::Tasks::FPipe> Pipe;
};

UCLASS()
class THREADINGSAMPLE_API UThreadingSampleBPLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

	UFUNCTION(BlueprintCallable, Category = "Threading Sample")
	static void LoadTextFiles(ELoadTextFileExecution InExecution, float InSleepTimeInSeconds, const TArray<FString>& InFilesToLoad, TArray<UTextFileResult*>& OutResults);

	UFUNCTION(BlueprintCallable, Category = "Threading Sample")
	static void FilterTextureUsingParallelFor(UTexture2D* InSourceTexture, EFilterType InFilterType, int InFilterSize, float InScaleValue, bool InOnePass, bool InForceSingleThread, UTexture2D*& OutFilteredTexture);

	UFUNCTION(BlueprintCallable, Category = "Threading Sample")
	static void FilterTextureUsingTaskSystem(UTexture2D* InSourceTexture, EFilterType InFilterType, int InFilterSize, float InScaleValue, UResultUsingTaskSystem*& OutResult);

	UFUNCTION(BlueprintCallable, Category = "Threading Sample")
	static void FilterTextureUsingTaskGraphSystem(UTexture2D* InSourceTexture, EFilterType InFilterType, int InFilterSize, float InScaleValue, bool InHoldSourceTasks, UResultUsingTaskGraphSystem*& OutResult);

	UFUNCTION(BlueprintCallable, Category = "Threading Sample")
	static void FilterTextureUsingPipe(UTexture2D* InSourceTexture, EFilterType InFilterType, int InFilterSize, float InScaleValue, UResultUsingPipe*& OutResult);

	UFUNCTION(BlueprintCallable, Category = "Threading Sample")
	static void ExecuteNestedTask(int InCurrentCallIndex);

	UFUNCTION(BlueprintCallable, Category = "Threading Sample")
	static void RunLowLevelTaskTest(int InCurrentCallIndex);

	UFUNCTION(BlueprintCallable, Category = "Threading Sample")
	static void CreateRunnable(UMyRunnable*& OutRunnable);

	UFUNCTION(BlueprintCallable, Category = "Threading Sample")
	static void CreateFThread(UMyFThread*& OutFThread);

	UFUNCTION(BlueprintCallable, Category = "Threading Sample")
	static void ThreadPoolCommonUsage();

	UFUNCTION(BlueprintCallable, Category = "Threading Sample")
	static void ThreadPoolWrapperUsage(EThreadPoolWrapperType InWrapperType, int32 InNumSubmittedWork, int32 InMaxConcurrency, bool InResumeHalfWorks);
};
