#pragma once

#include "CoreMinimal.h"
#include "Misc/Timespan.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "MultiThreadingSampleBPLibrary.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMultiThreadingSample, All, All);

/*----------------------------------------------------------------------------------
	Async Interface Samples
----------------------------------------------------------------------------------*/

//Warp the return value of the Async function into an UObject to be able to use it in Blueprints.
UCLASS(BlueprintType)
class UTextFileResult :public UObject
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category = "Threading Sample")
	bool IsReady() const
	{
		check(Future.IsValid());
		//Retures whether the Future is ready or not.
		return Future.IsReady();
	}

	UFUNCTION(BlueprintCallable, Category = "Threading Sample")
	void GetResult(FString& OutFileName, FString& OutFileContent)
	{
		check(Future.IsValid());
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

//Define the different ways to execute the async loading of the text files.
UENUM(BlueprintType)
enum class EAsyncLoadingExecution : uint8
{
	AsyncInterface_TaskGraph,  //EAsyncExecution::TaskGraph
	AsyncInterface_ThreadPool, //EAsyncExecution::ThreadPool
	AsyncInterface_Thread,     //EAsyncExecution::Thread
	AsyncPoolInterface,        //AsyncPool()
	AsyncThreadInterface       //AsyncThread()
};

//An Runnable class which impliments FRunnable and FSingleThreadRunnable
class FMyRunnable :public FRunnable, public FSingleThreadRunnable
{
public:
	//Begin FRunnable
	virtual bool Init() override
	{
		UE_LOG(LogMultiThreadingSample, Display, TEXT("Calling FMyRunnable::Init()."));
		Stoped = false;
		return true;
	}

	virtual uint32 Run() override
	{
		UE_LOG(LogMultiThreadingSample, Display, TEXT("Calling FMyRunnable::Run()."));
		while (true && !Stoped)
		{
			ThreadWork();
		}

		return 0;
	}

	virtual void Stop() override
	{
		UE_LOG(LogMultiThreadingSample, Display, TEXT("Calling FMyRunnable::Stop()."));
		Stoped = true;
	}

	virtual void Exit() override
	{
		UE_LOG(LogMultiThreadingSample, Display, TEXT("Calling FMyRunnable::Exit()."));
	}

	//This returns a FSingleThreadRunnable pointer
	virtual FSingleThreadRunnable* GetSingleThreadInterface()
	{
		return this;
	}
	//End FRunnable

	//Begin FSingleThreadRunnable
	//Tick() will be called when multi threading is not supported on current platform or is disabled by commandline(-nothreading) or some other settings
	virtual void Tick() override
	{
		ThreadWork();
	}
	//End FSingleThreadRunnable

	FMyRunnable() = default;

	virtual ~FMyRunnable() = default;

private:
	bool Stoped = false;

	void ThreadWork()
	{
		UE_LOG(LogMultiThreadingSample, Display, TEXT("Doing threaded work..."));
		FPlatformProcess::Sleep(0.1);
	}
};

UCLASS(BlueprintType)
class UMyRunnable :public UObject
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category = "Threading Sample")
	bool Startup()
	{
		checkf(!IsRunning, TEXT("Startup() should only be called once!!!"));

		Runnable = new FMyRunnable;
		//Create the Runnable Thread baseed on which platform we are running on.
		//eg. on Windows, This will eventually create a new FRunnableThreadWin instance.
		RunnableThread = FRunnableThread::Create(Runnable, TEXT("My Runnable Thread"), 0, EThreadPriority::TPri_Lowest);

		if (!RunnableThread)
		{
			UE_LOG(LogMultiThreadingSample, Warning, TEXT("Failed to create runnable thread."));
			delete Runnable;
			return false;
		}

		IsRunning = true;

		return true;
	}

	UFUNCTION(BlueprintCallable, Category = "Threading Sample")
	void Stop()
	{
		if (IsRunning)
		{
			Runnable->Stop();

			delete RunnableThread;
			RunnableThread = nullptr;
			delete Runnable;
			Runnable = nullptr;

			IsRunning = false;
		}
	}

	virtual ~UMyRunnable()
	{
		Stop();
	}

private:
	FMyRunnable* Runnable;
	FRunnableThread* RunnableThread;

	bool IsRunning = false;
};

//Warp the result returned using task system
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

//Warp the result returned using task graph system
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

//Warp the result returned using pipe
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
class THREADINGSAMPLE_API UMultiThreadingSampleBPLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

	UFUNCTION(BlueprintCallable, Category = "MultiThreading Sample")
	static void LoadTextFiles(EAsyncLoadingExecution InExecution, float InSleepTimeInSeconds, const TArray<FString>& InFilesToLoad, TArray<UTextFileResult*>& OutResults);

	UFUNCTION(BlueprintCallable, Category = "MultiThreading Sample")
	static void BoxFilterTextureUsingParallelFor(UTexture2D* InSourceTexture, int InBoxSize, float InScaleValue, bool InForceSingleThread, UTexture2D*& OutFilteredTexture);

	UFUNCTION(BlueprintCallable, Category = "MultiThreading Sample")
	static void BoxFilterTextureUsingTaskSystem(UTexture2D* InSourceTexture, int InBoxSize, float InScaleValue, UResultUsingTaskSystem*& OutResult);

	UFUNCTION(BlueprintCallable, Category = "MultiThreading Sample")
	static void BoxFilterTextureUsingTaskGraphSystem(UTexture2D* InSourceTexture, int InBoxSize, float InScaleValue, bool InHoldSourceTasks, UResultUsingTaskGraphSystem*& OutResult);

	UFUNCTION(BlueprintCallable, Category = "MultiThreading Sample")
	static void BoxFilterTextureUsingPipe(UTexture2D* InSourceTexture, int InBoxSize, float InScaleValue, UResultUsingPipe*& OutResult);

	UFUNCTION(BlueprintCallable, Category = "MultiThreading Sample")
	static void ExecuteNestedTask(int InCurrentCallIndex);

	UFUNCTION(BlueprintCallable, Category = "MultiThreading Sample")
	static void CreateRunnable(UMyRunnable*& OutMyRunnable);

	UFUNCTION(BlueprintCallable, Category = "MultiThreading Sample")
	static void DoingThreadedWorkUsingFThread();

	UFUNCTION(BlueprintCallable, Category = "MultiThreading Sample")
	static void DoingThreadedWorkUsingQueuedThreadPool(const TArray<int32>& InArrayToSort);
};
