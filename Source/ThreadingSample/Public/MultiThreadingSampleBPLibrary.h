#pragma once

#include "CoreMinimal.h"
#include "Misc/Timespan.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "MultiThreadingSampleBPLibrary.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMultiThreadingSample, All, All);

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
	AsyncTaskInterface,        //AsyncTask()
	AsyncPoolInterface,        //AsyncPool()
	AsyncThreadInterface       //AsyncThread()
};

//A Runnable class that impliments FRunnable and FSingleThreadRunnable
class FMyRunnable :public FRunnable, public FSingleThreadRunnable
{
public:
	//Begin FRunnable
	virtual bool Init() override
	{
		UE_LOG(LogMultiThreadingSample, Display, TEXT("CurrentThreadID:%d::Initializing My Runnable."), FPlatformTLS::GetCurrentThreadId());
		Stopped = false;
		return true;
	}

	virtual uint32 Run() override
	{
		UE_LOG(LogMultiThreadingSample, Display, TEXT("CurrentThreadID:%d::Entering FMyRunnable::Run()."), FPlatformTLS::GetCurrentThreadId());
		while (!Stopped)
		{
			ThreadedWork();
		}
		UE_LOG(LogMultiThreadingSample, Display, TEXT("CurrentThreadID:%d::Exiting FMyRunnable::Run()."), FPlatformTLS::GetCurrentThreadId());
		return 0;
	}

	virtual void Stop() override
	{
		if (!Stopped)
		{
			UE_LOG(LogMultiThreadingSample, Display, TEXT("CurrentThreadID:%d::Stopping My Runnable."), FPlatformTLS::GetCurrentThreadId());
			Stopped = true;
		}
	}

	virtual void Exit() override
	{
		UE_LOG(LogMultiThreadingSample, Display, TEXT("CurrentThreadID:%d::Exiting My Runnable."), FPlatformTLS::GetCurrentThreadId());
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
		ThreadedWork();
	}
	//End FSingleThreadRunnable

	FMyRunnable() = default;

	virtual ~FMyRunnable() = default;

private:
	std::atomic_bool Stopped = false;

	void ThreadedWork()
	{
		UE_LOG(LogMultiThreadingSample, Display, TEXT("CurrentThreadID:%d::FMyRunnable::Doing threaded work..."), FPlatformTLS::GetCurrentThreadId());
		FPlatformProcess::Sleep(1.0);
	}
};

UCLASS(BlueprintType)
class UMyRunnable :public UObject
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category = "Threading Sample")
	void Startup()
	{
		if (bIsRunning)
			return;

		bIsRunning = true;

		Runnable.Reset(new FMyRunnable);
		//Create the Runnable Thread baseed on which platform we are running on.
		//eg. on Windows, This will eventually create a new FRunnableThreadWin instance.
		RunnableThread.Reset(FRunnableThread::Create(Runnable.Get(), TEXT("My Runnable Thread"), 0, EThreadPriority::TPri_Lowest));
	}

	UFUNCTION(BlueprintCallable, Category = "Threading Sample")
	void Shutdown(bool InAsyncShutDown)
	{
		if (bIsRunning)
		{
			auto Reset = [this]() {
				Runnable->Stop();

				RunnableThread.Reset();
				Runnable.Reset();

				bIsRunning = false;
				};

			if (InAsyncShutDown)
			{
				//We destroy runnable and runnable thread in a worker thread.
				AsyncTask(ENamedThreads::AnyThread, MoveTemp(Reset));
			}
			else
			{
				Reset();
			}
		}
	}

	UFUNCTION(BlueprintCallable, Category = "Threading Sample")
	bool IsRunning()const
	{
		return bIsRunning;
	}

	virtual ~UMyRunnable()
	{
		Shutdown(true);
	}

private:
	TUniquePtr<FMyRunnable> Runnable;
	TUniquePtr<FRunnableThread> RunnableThread;

	bool bIsRunning = false;
};

UCLASS(BlueprintType)
class UMyFThread :public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Threading Sample")
	void Startup()
	{
		if (bIsRunning)
			return;

		bIsRunning = true;

		//The threaded function that will be exectuted on other thread.
		auto ThreadedFunction = [this]() {
			while (this->bIsRunning)
			{
				UE_LOG(LogMultiThreadingSample, Display, TEXT("Running My FThread ThreadedFunction()."));
				FPlatformProcess::Sleep(1.0);
			}
			};

		Thread = MakeUnique<FThread>(
			TEXT("My FThread"),//The debug name of this thread.
			ThreadedFunction,
			nullptr/* SingleThreadTickFunction */, //This function will be exectuted when multi-threading is not supported or disabled.
			0,
			EThreadPriority::TPri_Lowest//The thread priority of this thread.
		);
	}

	UFUNCTION(BlueprintCallable, Category = "Threading Sample")
	void Shutdown(bool InAsyncShutDown)
	{
		auto Reset = [this]() {
			bIsRunning = false;

			//Join() must be called to wait for the FThread instance to finish execute.
			Thread->Join();
			Thread.Reset();
			};

		if (bIsRunning)
		{
			if (InAsyncShutDown)
			{
				//We wait for the created thread to join in a worker thread.
				AsyncTask(ENamedThreads::AnyThread, MoveTemp(Reset));
			}
			else
			{
				Reset();
			}
		}
	}

	UFUNCTION(BlueprintCallable, Category = "Threading Sample")
	bool IsRunning()const
	{
		return bIsRunning;
	}

	virtual ~UMyFThread()
	{
		Shutdown(true);
	}

private:
	TUniquePtr<FThread> Thread;

	std::atomic_bool bIsRunning = false;
};

UENUM(BlueprintType)
enum class EFilterType : uint8
{
	BoxFilter,
	GaussianFilter
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
class THREADINGSAMPLE_API UMultiThreadingSampleBPLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

	UFUNCTION(BlueprintCallable, Category = "MultiThreading Sample")
	static void LoadTextFiles(EAsyncLoadingExecution InExecution, float InSleepTimeInSeconds, const TArray<FString>& InFilesToLoad, TArray<UTextFileResult*>& OutResults);

	UFUNCTION(BlueprintCallable, Category = "MultiThreading Sample")
	static void FilterTextureUsingParallelFor(UTexture2D* InSourceTexture, EFilterType InFilterType, int InFilterSize, float InScaleValue, bool InForceSingleThread, UTexture2D*& OutFilteredTexture);

	UFUNCTION(BlueprintCallable, Category = "MultiThreading Sample")
	static void FilterTextureUsingTaskSystem(UTexture2D* InSourceTexture, EFilterType InFilterType, int InFilterSize, float InScaleValue, UResultUsingTaskSystem*& OutResult);

	UFUNCTION(BlueprintCallable, Category = "MultiThreading Sample")
	static void FilterTextureUsingTaskGraphSystem(UTexture2D* InSourceTexture, EFilterType InFilterType, int InFilterSize, float InScaleValue, bool InHoldSourceTasks, UResultUsingTaskGraphSystem*& OutResult);

	UFUNCTION(BlueprintCallable, Category = "MultiThreading Sample")
	static void FilterTextureUsingPipe(UTexture2D* InSourceTexture, EFilterType InFilterType, int InFilterSize, float InScaleValue, UResultUsingPipe*& OutResult);

	UFUNCTION(BlueprintCallable, Category = "MultiThreading Sample")
	static void ExecuteNestedTask(int InCurrentCallIndex);

	UFUNCTION(BlueprintCallable, Category = "MultiThreading Sample")
	static void RunLowLevelTaskTest();

	UFUNCTION(BlueprintCallable, Category = "MultiThreading Sample")
	static void CreateRunnable(UMyRunnable*& OutRunnable);

	UFUNCTION(BlueprintCallable, Category = "MultiThreading Sample")
	static void CreateFThread(UMyFThread*& OutFThread);

	UFUNCTION(BlueprintCallable, Category = "MultiThreading Sample")
	static void DoThreadedWorkUsingQueuedThreadPool(const TArray<int32>& InArrayToSort, const FString& InStringToLog, int32 InFibonacciToCompute, int32 InNumWorksForWrapper);
};
