#include "CoreMinimal.h"
#include "Tasks/TaskConcurrencyLimiter.h"
#include "Misc/QueuedThreadPoolWrapper.h"

void CodeSnippets()
{
	using namespace LowLevelTasks;
	using namespace UE::Tasks;

	//Wait task to be completed
	{
		auto TaskA = Launch(
			UE_SOURCE_LOCATION,
			[]() {},
			ETaskPriority::Normal,
			EExtendedTaskPriority::None
		);
		auto TaskB = Launch(
			UE_SOURCE_LOCATION,
			[]() {},
			ETaskPriority::Normal,
			EExtendedTaskPriority::None
		);
		auto TaskC = Launch(
			UE_SOURCE_LOCATION,
			[]() {},
			ETaskPriority::Normal,
			EExtendedTaskPriority::None
		);

		//Wait for single task
		TaskA.Wait(FTimespan::FromMicroseconds(100));
		TaskA.Wait();

		//Wait for a collection of tasks
		TArray<UE::Tasks::FTask> Tasks;
		Tasks.Add(TaskA);
		Tasks.Add(TaskB);
		Tasks.Add(TaskC);

		Wait(Tasks, FTimespan::FromMicroseconds(100));

		//Wait for any of the tasks
		int32 FirstCompletedIndex = WaitAny(Tasks, FTimespan::FromMicroseconds(100));
		auto WaitTask = Any(TArray({ TaskA,TaskB,TaskC }));
		WaitTask.Wait();
	}

	//Task cancellation token
	{
		{
			//Cancel task
			FCancellationToken Token;
			auto TaskA = Launch(
				UE_SOURCE_LOCATION,
				[&Token]() {
					if (Token.IsCanceled())
						return;
				}
			);
			auto TaskB = Launch(
				UE_SOURCE_LOCATION,
				[]() {
					//Will not be cancelled
				},
				Prerequisites(TaskA)
				);

			Token.Cancel();
		}

		{
			//Cancel task and its subsequents
			FCancellationToken Token1;
			auto TaskC = Launch(
				UE_SOURCE_LOCATION,
				[&Token1]() {
					if (Token1.IsCanceled())
						return;
				}
			);
			auto TaskD = Launch(
				UE_SOURCE_LOCATION,
				[&Token1]() {
					if (Token1.IsCanceled())
						return;
				},
				Prerequisites(TaskC)
			);

			Token1.Cancel();
		}
	}

	//Task concurrency limiter
	{
		FTaskConcurrencyLimiter Limiter(2/* MaxConcurrency */, ETaskPriority::Default);
		for (int i = 0; i < 100; ++i)
		{
			Limiter.Push(
				UE_SOURCE_LOCATION,
				[](uint32 ConcurrencySlot) {}
			);
		}

		Limiter.Wait(FTimespan::MaxValue());
	}

	//Task events as task holder
	{
		//Define a task event
		FTaskEvent Event{ UE_SOURCE_LOCATION };

		//Launch a task
		auto Task = Launch(
			UE_SOURCE_LOCATION,
			[]() {},
			Prerequisites(Event), //The event is a prerequisite of this task
			ETaskPriority::Normal,
			EExtendedTaskPriority::None
		);

		//The task will not be executed until we trigger(signal) the evnet
		Event.Trigger();
	}

	//Task events as task joiner
	{
		auto TaskA = Launch(
			UE_SOURCE_LOCATION,
			[]() {},
			ETaskPriority::Normal,
			EExtendedTaskPriority::None
		);
		auto TaskB = Launch(
			UE_SOURCE_LOCATION,
			[]() {},
			ETaskPriority::Normal,
			EExtendedTaskPriority::None
		);

		FTaskEvent Joiner{ UE_SOURCE_LOCATION };
		//Adds tasks as the prerequisites of the joiner
		Joiner.AddPrerequisites(Prerequisites(TaskA, TaskB));
		//Trigger the joiner
		Joiner.Trigger();

		//Wait for the joiner means waiting for the prerequisites to be completed
		Joiner.Wait();
	}

	//Complete task explicitly/manually
	{
		FTaskEvent Event{ UE_SOURCE_LOCATION };

		auto OuterTask = Launch(
			UE_SOURCE_LOCATION,
			[&Event]() {
				//Adds the event as a nested task of the launched task
				AddNested(Event);
			},
			ETaskPriority::Normal,
			EExtendedTaskPriority::None
		);

		//As the nested task defines the completion timing of the outer task, the outer task will not
		//complete(even if the task body has already finished execution) until the event is triggered.
		//This is convenient when you need to control the completion of a task (which is normally flagged
		//automatically by the task system).
		//Some time later, trigger the event to manually complete the outer task.
		FPlatformProcess::Sleep(0.05);
		Event.Trigger();
	}

	//Task with return value
	{
		auto Task = Launch(
			UE_SOURCE_LOCATION,
			[]() {
				return 100;
			},
			ETaskPriority::Normal,
			EExtendedTaskPriority::None
		);

		//Get the return value of task body. It will block the caller if task is not completed.
		Task.Wait();
		int& Result = Task.GetResult();
	}

	//Task Graph Interfaces
	{
		FTaskGraphInterface& Instance = FTaskGraphInterface::Get();

		ENamedThreads::Type CurrentThread = Instance.GetCurrentThreadIfKnown(false/* bLocalQueue */);
		int32 NumBackgroundThreads = Instance.GetNumBackgroundThreads();
		int32 NumForegroundThreads = Instance.GetNumForegroundThreads();
		int32 NumNumWorkerThreads = Instance.GetNumWorkerThreads();
		bool IsCurrentThreadKnown = Instance.IsCurrentThreadKnown();
		bool IsRunning = Instance.IsRunning();
		bool IsGameThreadProcessingTasks = Instance.IsThreadProcessingTasks(ENamedThreads::GameThread);
	}

	//Task graph process named thread
	{
		//Supposing we are in game thread and we dispatched a game thread task.
		auto GameThreadTask = FFunctionGraphTask::CreateAndDispatchWhenReady(
			[]() {},
			TStatId{},
			nullptr,
			ENamedThreads::GameThread
		);

		//process game thread tasks until idle.
		{
			FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
		}

		//Process game thread tasks until RequestReturn is called.
		{
			FTaskGraphInterface::Get().ProcessThreadUntilRequestReturn(ENamedThreads::GameThread);

			//Launch another task use Task System
			auto RequestReturnTask = Launch(
				UE_SOURCE_LOCATION,
				[]()
				{
					//The task body is to request game thread to stop process tasks and return.
					FTaskGraphInterface::Get().RequestReturn(ENamedThreads::GameThread);
				},
				Prerequisites(GameThreadTask), //Take GameThreadTask as its prerequisite
				LowLevelTasks::ETaskPriority::High,
				EExtendedTaskPriority::GameThreadNormalPri //Executed on game thread
			);

			//Or dispatch another task use Task Graph System
			FGraphEventArray Prerequisites{ GameThreadTask };
			TGraphTask<FReturnGraphTask>::CreateTask(&Prerequisites).ConstructAndDispatchWhenReady(ENamedThreads::GameThread);
		}

		//Wait here will not deadlock
		//1. If we process thread until idle.
		//2. Or if we procee thread until explicitly request return.
		//3. Even if we dont perform the above operations, Wait() itself will help with that.
		GameThreadTask->Wait();
	}

	//Queued thread pool wrapper
	{
		struct DummyWork :public IQueuedWork
		{
			virtual void DoThreadedWork()override
			{
				delete this;
			}

			virtual void Abandon()override
			{

			}
		};

		//Task Graph System wrapper
		{
			auto PriorityMapper = [](EQueuedWorkPriority InPriority) -> ENamedThreads::Type
				{
					return ENamedThreads::AnyBackgroundThreadNormalTask;
				};

			TUniquePtr<FQueuedThreadPoolTaskGraphWrapper> TaskGraphWrapper = MakeUnique<FQueuedThreadPoolTaskGraphWrapper>(PriorityMapper);
			((FQueuedThreadPool*)TaskGraphWrapper.Get())->AddQueuedWork(new DummyWork);
		}

		//Low Level Task System wrapper
		{
			auto PriorityMapper = [](EQueuedWorkPriority InPriority) -> EQueuedWorkPriority
				{
					return InPriority;
				};

			auto LowLevelTaskScheduler = &LowLevelTasks::FScheduler::Get();

			TUniquePtr<FQueuedLowLevelThreadPool> LowLevelTaskWrapper = MakeUnique<FQueuedLowLevelThreadPool>(PriorityMapper, LowLevelTaskScheduler);
			((FQueuedThreadPool*)LowLevelTaskWrapper.Get())->AddQueuedWork(new DummyWork);
		}
	}
}
