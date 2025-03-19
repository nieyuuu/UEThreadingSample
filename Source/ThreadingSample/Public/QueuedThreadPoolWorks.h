#pragma once

#include "ThreadingSample/ThreadingSample.h"

//When using Queued Thread Pool, user needs to implement IQueuedWork interface, and override DoThreadedWork() function(and other virtual functions if needed).
//User needs to manage the life cycle of the created work instances. In this work, we delete ourself once the work is done.
class FSelfDeleteWork :public IQueuedWork
{
public:
	virtual void DoThreadedWork()override
	{
		UE_LOG(LogThreadingSample, Display, TEXT("FSelfDeleteWork::DoThreadedWork()."));
		//Delete ourself when work is done
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
		return TEXT("SelfDeleteWork");
	}
};

class FAutoDeleteWork :public FNonAbandonableTask
{
	//Declare FAutoDeleteAsyncTask<FAutoDeleteWork> as a friend so it can access the private members of this class.
	friend class FAutoDeleteAsyncTask<FAutoDeleteWork>;
private:
	//Keep the constructor private to prevent user creating instances of this class(except for the friend class).
	FAutoDeleteWork() = default;

	//The threaded work to do
	void DoWork()
	{
		UE_LOG(LogThreadingSample, Display, TEXT("FAutoDeleteWork::DoWork()."));
		//We use FAutoDeleteAsyncTask<WorkType> to delete work instance, so no need to delete ourself here.
		//delete this;
	}

	//Declare stat id
	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FAutoDeleteAsyncTask, STATGROUP_ThreadPoolAsyncTasks);
	}
};

class FGenerateRandomIntWork :public FNonAbandonableTask
{
	//Declare FAsyncTask<FGenerateRandomIntWork> as a friend.
	friend class FAsyncTask<FGenerateRandomIntWork>;
private:
	FGenerateRandomIntWork() = default;

	//The threaded work to do
	void DoWork()
	{
		WorkResult = FMath::RandRange(0, 100);
		UE_LOG(LogThreadingSample, Display, TEXT("FGenerateRandomIntWork::DoWork(). FMath::RandRange(0, 100) returns %d."), WorkResult);
	}

	//Declare stat id
	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FGenerateRandomIntWork, STATGROUP_ThreadPoolAsyncTasks);
	}

private:
	int32 WorkResult = -1;

public:
	//Interface for accessing result when work is done
	const int32& GetResult()const
	{
		return WorkResult;
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