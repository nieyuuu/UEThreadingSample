#pragma once

#include "CoreMinimal.h"
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
		return Future.IsReady();
	}

	UFUNCTION(BlueprintCallable, Category = "Threading Sample")
	void GetResult(FString& OutFileName, FString& OutFileContent)
	{
		check(Future.IsValid());
		OutFileName = FileName;
		OutFileContent = Future.Get();
	}

	void SetFuture(const FString& InFileName, TFuture<FString>&& InFuture)
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
	AsyncInterface_TaskGraph,
	AsyncInterface_ThreadPool,
	AsyncInterface_Thread,
	AsyncPoolInterface,
	AsyncThreadInterface
};

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

	virtual FSingleThreadRunnable* GetSingleThreadInterface()
	{
		return this;
	}
	//End FRunnable

	//Begin FSingleThreadRunnable
	virtual void Tick() override
	{
		ThreadWork();
	}
	//End FSingleThreadRunnable

	virtual ~FMyRunnable() = default;

	FMyRunnable() = default;

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
		Thread = FRunnableThread::Create(Runnable, TEXT("My Runnable Thread"), 0, EThreadPriority::TPri_Lowest);

		if (!Thread)
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

			delete Thread;
			Thread = nullptr;
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
	FRunnableThread* Thread;

	bool IsRunning = false;
};

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

UCLASS()
class THREADINGSAMPLE_API UMultiThreadingSampleBPLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

	UFUNCTION(BlueprintCallable, Category = "MultiThreading Sample")
	static void LoadTextFiles(EAsyncLoadingExecution InExecution, float InSleepTimeInSeconds, const TArray<FString>& InFilesToLoad, TArray<UTextFileResult*>& OutResults);

	UFUNCTION(BlueprintCallable, Category = "MultiThreading Sample")
	static void BoxFilterTextureUsingParallelFor(UTexture2D* InSourceTexture, int InBoxSize, bool InForceSingleThread, UTexture2D*& OutFilteredTexture);

	UFUNCTION(BlueprintCallable, Category = "MultiThreading Sample")
	static void BoxFilterTextureUsingTaskSystem(UTexture2D* InSourceTexture, int InBoxSize, float InScaleValue, UResultUsingTaskSystem*& OutResult);
	
	UFUNCTION(BlueprintCallable, Category = "MultiThreading Sample")
	static void BoxFilterTextureUsingTaskGraphSystem(UTexture2D* InSourceTexture, int InBoxSize, float InScaleValue, UResultUsingTaskGraphSystem*& OutResult);

	UFUNCTION(BlueprintCallable, Category = "MultiThreading Sample")
	static void CreateRunnable(UMyRunnable*& OutMyRunnable);

	UFUNCTION(BlueprintCallable, Category = "MultiThreading Sample")
	static void DoingThreadedWorkUsingFThread();

	UFUNCTION(BlueprintCallable, Category = "MultiThreading Sample")
	static void DoingThreadedWorkUsingQueuedThreadPool();
};
