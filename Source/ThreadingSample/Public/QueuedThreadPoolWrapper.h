#pragma once

#include "Misc/QueuedThreadPoolWrapper.h"

FQueuedThreadPoolWrapper* GetQueuedThreadPoolWrapper()
{
	static FQueuedThreadPoolWrapper* GThreadPoolWrapper = nullptr;

	if (GThreadPoolWrapper == nullptr)
	{
		FQueuedThreadPool* WrappedThreadPool = GThreadPool;
#if WITH_EDITOR
		WrappedThreadPool = GLargeThreadPool;
#endif

		check(WrappedThreadPool);

		//The priority mapper. Here we just map the priority to EQueuedWorkPriority::Lowest.
		auto PriorityMapper = [](EQueuedWorkPriority InPriority) -> EQueuedWorkPriority
			{
				return EQueuedWorkPriority::Lowest;
			};

		GThreadPoolWrapper = new FQueuedThreadPoolWrapper(WrappedThreadPool, 1/* InMaxConcurrency */, PriorityMapper);
	}

	return GThreadPoolWrapper;
}

FQueuedThreadPoolDynamicWrapper* GetQueuedThreadPoolDynamicWrapper()
{
	static FQueuedThreadPoolDynamicWrapper* GThreadPoolDynamicWrapper = nullptr;

	if (GThreadPoolDynamicWrapper == nullptr)
	{
		FQueuedThreadPool* WrappedThreadPool = GThreadPool;
#if WITH_EDITOR
		WrappedThreadPool = GLargeThreadPool;
#endif

		check(WrappedThreadPool);

		//The priority mapper. Here we just map the priority to EQueuedWorkPriority::Lowest.
		auto PriorityMapper = [](EQueuedWorkPriority InPriority) -> EQueuedWorkPriority
			{
				return EQueuedWorkPriority::Lowest;
			};

		GThreadPoolDynamicWrapper = new FQueuedThreadPoolDynamicWrapper(WrappedThreadPool, 1/* InMaxConcurrency */, PriorityMapper);
	}

	return GThreadPoolDynamicWrapper;
}

//Wrapper that allowing to schedule thread pool tasks on the Task Graph System
FQueuedThreadPoolTaskGraphWrapper* GetQueuedThreadPoolTaskGraphWrapper()
{
	static FQueuedThreadPoolTaskGraphWrapper* GThreadPoolTaskGraphWrapper = nullptr;

	if (GThreadPoolTaskGraphWrapper == nullptr)
	{
		//The priority mapper. Here we simply map a QueuedWorkPriority to ENamedThreads::AnyBackgroundThreadNormalTask
		auto PriorityMapper = [](EQueuedWorkPriority InPriority) -> ENamedThreads::Type
			{
				return ENamedThreads::AnyBackgroundThreadNormalTask;
			};

		GThreadPoolTaskGraphWrapper = new FQueuedThreadPoolTaskGraphWrapper(PriorityMapper);
	}

	return GThreadPoolTaskGraphWrapper;
}

//Wrapper that allowing to schedule thread pool tasks on the the low level backend which is also used by the Task Graph System
FQueuedLowLevelThreadPool* GetQueuedLowLevelThreadPool()
{
	static FQueuedLowLevelThreadPool* GQueuedLowLevelThreadPool = nullptr;

	if (GQueuedLowLevelThreadPool == nullptr)
	{
		//The priority mapper. Here we just keep what it is as unchanged.
		auto PriorityMapper = [](EQueuedWorkPriority InPriority) -> EQueuedWorkPriority
			{
				return InPriority;
			};

		GQueuedLowLevelThreadPool = new FQueuedLowLevelThreadPool(PriorityMapper, &LowLevelTasks::FScheduler::Get());
	}

	return GQueuedLowLevelThreadPool;
}
