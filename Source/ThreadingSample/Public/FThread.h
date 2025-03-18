#pragma once

#include "ThreadingSample/ThreadingSample.h"

#include "FThread.generated.h"

//A class that creates a FThread instance at construction and destroy it at destruction.
class FMyFThread
{
public:
	FMyFThread()
	{
		bStopped = false;

		auto ThreadedFunction = [this]() {
			while (!bStopped)
			{
				UE_LOG(LogThreadingSample, Display, TEXT("CurrentThreadID:%d::Running My FThread ThreadedFunction()."), FPlatformTLS::GetCurrentThreadId());
				FPlatformProcess::Sleep(1.0);
			}
			};

		Thread = MakeUnique<FThread>(
			TEXT("My FThread"),//The debug name of this thread.
			ThreadedFunction,
			nullptr/* SingleThreadTickFunction */, //This function will be executed when multi-threading is not supported or disabled.
			0,
			EThreadPriority::TPri_Lowest//The thread priority of this thread.
		);
	}

	virtual ~FMyFThread()
	{
		check(!bStopped);
		bStopped = true;

		//Join() must be called to wait for the FThread instance to finish execute.
		Thread->Join();
		Thread.Reset();
	}

private:
	TUniquePtr<FThread> Thread;

	std::atomic_bool bStopped = true;
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

		Thread = MakeUnique<FMyFThread>();
	}

	UFUNCTION(BlueprintCallable, Category = "Threading Sample")
	void Shutdown(bool InAsyncShutDown)
	{
		if (bIsRunning)
		{
			if (InAsyncShutDown)
			{
				auto ShutdownTask = [LocalThread = Thread.Release()]() {
					delete LocalThread;
					};

				AsyncTask(ENamedThreads::AnyThread, MoveTemp(ShutdownTask));
			}
			else
			{
				Thread.Reset();
			}

			bIsRunning = false;
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
	TUniquePtr<FMyFThread> Thread;

	bool bIsRunning = false;
};