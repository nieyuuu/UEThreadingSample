#pragma once

#include "ThreadingSample/ThreadingSample.h"

template<class BuilderType, class ArrayElementType>
const TCHAR* BuildStringFromArray(BuilderType& InStringBuilder, const TArray<ArrayElementType>& InArray)
{
	InStringBuilder << "[";

	for (int i = 0; i < InArray.Num() - 1; ++i)
	{
		InStringBuilder << InArray[i] << ",";
	}

	InStringBuilder << InArray[InArray.Num() - 1] << "]";

	return InStringBuilder.ToString();
}

//When using Queued Thread Pool, user needs to implement IQueuedWork interface, and override DoThreadedWork() function(and other virtual functions if needed).
class FDummyEmptyWork :public IQueuedWork
{
public:
	virtual void DoThreadedWork()override
	{
		UE_LOG(LogThreadingSample, Display, TEXT("FDummyEmptyWork::DoThreadedWork()."));
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
		return TEXT("DummyEmptyWork");
	}
};

//When using Queued Thread Pool, user needs to implement IQueuedWork interface, and override DoThreadedWork() function(and other virtual functions if needed).
class FFibonacciComputationWork :public IQueuedWork
{
public:
	virtual void DoThreadedWork()override
	{
		UE_LOG(LogThreadingSample, Display, TEXT("FFibonacciComputationWork::DoThreadedWork(). F(%d)=%d."), N, F(N));
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

//When using Queued Thread Pool, user needs to implement IQueuedWork interface, and override DoThreadedWork() function(and other virtual functions if needed).
class FOutputStringToLogWork :public IQueuedWork
{
public:
	virtual void DoThreadedWork()override
	{
		UE_LOG(LogThreadingSample, Display, TEXT("FOutputStringToLogWork::DoThreadedWork(). Output content: %s."), *Content);
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
		return TEXT("OutputStringToLogTask");
	}

	FOutputStringToLogWork(const FString& InContent)
	{
		Content = InContent;
	}

private:
	FString Content;
};

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
		UE_LOG(LogThreadingSample, Display, TEXT("FAutoDeleteOutputStringToLogTask::DoWork(). Output content: %s."), *Content);
	}

	//Declare stat id.
	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FAutoDeleteOutputStringToLogTask, STATGROUP_ThreadPoolAsyncTasks);
	}

	FString Content;
};

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
			UE_LOG(LogThreadingSample, Warning, TEXT("Empty array!!!"));

			return;
		}

		TStringBuilder<1024> StringBuilder;

		FString Content = BuildStringFromArray(StringBuilder, ArrayToSort);
		UE_LOG(LogThreadingSample, Display, TEXT("FSortIntArrayWork::Before sort: %s"), *Content);

		Algo::Sort(ArrayToSort);

		StringBuilder.Reset();

		Content = BuildStringFromArray(StringBuilder, ArrayToSort);
		UE_LOG(LogThreadingSample, Display, TEXT("FSortIntArrayWork::After sort: %s"), *Content);
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

class FWorkWithWeight :public IQueuedWork
{
public:
	FWorkWithWeight(float InWeight) :Weight(InWeight) {}

	float GetWeight()const
	{
		return Weight;
	}

	virtual void DoThreadedWork()override
	{
		UE_LOG(LogThreadingSample, Display, TEXT("FWorkWithWeight::DoThreadedWork(). Weight is %f."), Weight);
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

private:
	float Weight = 0.0f;
};