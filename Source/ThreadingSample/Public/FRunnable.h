#pragma once

#include "ThreadingSample/ThreadingSample.h"

#include "FRunnable.generated.h"

//A Runnable class that implements FRunnable and FSingleThreadRunnable
class FMyRunnable :public FRunnable, public FSingleThreadRunnable
{
public:
	//Begin FRunnable
	virtual bool Init() override
	{
		UE_LOG(LogThreadingSample, Display, TEXT("CurrentThreadID:%d::Initializing My Runnable."), FPlatformTLS::GetCurrentThreadId());
		Stopped = false;
		return true;
	}

	virtual uint32 Run() override
	{
		UE_LOG(LogThreadingSample, Display, TEXT("CurrentThreadID:%d::Entering FMyRunnable::Run()."), FPlatformTLS::GetCurrentThreadId());
		while (!Stopped)
		{
			ThreadedWork();
		}
		UE_LOG(LogThreadingSample, Display, TEXT("CurrentThreadID:%d::Exiting FMyRunnable::Run()."), FPlatformTLS::GetCurrentThreadId());
		return 0;
	}

	virtual void Stop() override
	{
		if (!Stopped)
		{
			UE_LOG(LogThreadingSample, Display, TEXT("CurrentThreadID:%d::Stopping My Runnable."), FPlatformTLS::GetCurrentThreadId());
			Stopped = true;
		}
	}

	virtual void Exit() override
	{
		UE_LOG(LogThreadingSample, Display, TEXT("CurrentThreadID:%d::Exiting My Runnable."), FPlatformTLS::GetCurrentThreadId());
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
		UE_LOG(LogThreadingSample, Display, TEXT("CurrentThreadID:%d::FMyRunnable::Doing threaded work..."), FPlatformTLS::GetCurrentThreadId());
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
		//Create the Runnable Thread based on which platform we are running on.
		//eg. on Windows, This will eventually create a new FRunnableThreadWin instance.
		RunnableThread.Reset(FRunnableThread::Create(Runnable.Get(), TEXT("My Runnable Thread"), 0, EThreadPriority::TPri_Lowest));
	}

	UFUNCTION(BlueprintCallable, Category = "Threading Sample")
	void Shutdown(bool InAsyncShutDown)
	{
		if (bIsRunning)
		{
			Runnable->Stop();

			if (InAsyncShutDown)
			{
				auto ShutdownTask = [LocalThread = RunnableThread.Release(), LocalRunnable = Runnable.Release()]() {
					LocalThread->WaitForCompletion();
					delete LocalThread;
					delete LocalRunnable;
					};

				//Destroy runnable and runnable thread in a worker thread.
				AsyncTask(ENamedThreads::AnyThread, MoveTemp(ShutdownTask));
			}
			else
			{
				RunnableThread->WaitForCompletion();
				RunnableThread.Reset();
				Runnable.Reset();
			}

			bIsRunning = false;
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