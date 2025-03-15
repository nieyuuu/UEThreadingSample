#include "CoreMinimal.h"
#include "Tasks/TaskConcurrencyLimiter.h"
#include "Misc/QueuedThreadPoolWrapper.h"
#include "Misc/ScopeLock.h"
#include "Misc/SpinLock.h"
#include "Async/Mutex.h"
#include "Async/RecursiveMutex.h"

static thread_local int ThreadLocalVariable = -1;

void CodeSnippets()
{
	using namespace LowLevelTasks;
	using namespace UE::Tasks;

	//Wait task to be completed
	{
		//A collection of tasks with different result types
		TArray<UE::Tasks::FTask> Tasks = {
			Launch(
			UE_SOURCE_LOCATION,
			[]() {},
			ETaskPriority::Normal,
			EExtendedTaskPriority::None
		),
			Launch(
			UE_SOURCE_LOCATION,
			[]() { return 10.0f; },
			ETaskPriority::Normal,
			EExtendedTaskPriority::None
		),
			Launch(
			UE_SOURCE_LOCATION,
			[]() { return 100; },
			ETaskPriority::Normal,
			EExtendedTaskPriority::None
		)
		};

		//Wait for single task
		Tasks[0].Wait(FTimespan::FromMicroseconds(100));
		Tasks[0].Wait();

		//Wait for a collection of tasks
		Wait(Tasks, FTimespan::FromMicroseconds(100));

		//Wait for any of the tasks
		int32 FirstCompletedIndex = WaitAny(Tasks, FTimespan::FromMicroseconds(100));
		auto WaitAnyTask = Any(Tasks);
		WaitAnyTask.Wait();
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

		//Process game thread tasks until idle.
		{
			FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
		}

		//Process game thread tasks until RequestReturn is called.
		{
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

			//Process game thread tasks until return is requested
			FTaskGraphInterface::Get().ProcessThreadUntilRequestReturn(ENamedThreads::GameThread);
		}

		//Wait here will not deadlock
		//1. If we process thread until idle.
		//2. Or if we procee thread until explicitly request return.
		//3. Even if we dont perform the above operations, Wait() itself will help with that.
		GameThreadTask->Wait();
	}

	//Create task graph event
	{
		FGraphEventRef Event = FBaseGraphTask::CreateGraphEvent();

		//Trigger a task graph event(not an elegant way)
		Event->TryLaunch(0);
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

	//Promise and Future
	{
		TPromise<float> Promise;
		TFuture<float> Future = Promise.GetFuture();
		check(Future.IsValid());

		//These will invalidate the future
		{
			//Then
			// TFuture<void> FutureChain = Future.Then([](TFuture<float> Self) {});
			// check(!Future.IsValid());
			//Next
			// TFuture<void> FutureChain = Future.Next([](float Self) {});
			// check(!Future.IsValid());
		}

		AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [&Promise]() {
			Promise.SetValue(1.0);
			}
		);

		bool IsReady = Future.IsReady();
		Future.Wait();
		Future.WaitFor(FTimespan::MaxValue());
		Future.WaitUntil(FDateTime::MaxValue());

		const float& ResultRef = Future.Get();
		float& MutableResultRef = Future.GetMutable();

		//This will invalidate the future
		float Result = Future.Consume();
		check(!Future.IsValid());
	}

	//Mutex
	{
		{
			int SharedResource = 0;
			UE::FMutex Mutex;
			if (!Mutex.IsLocked())
			{
				//Try lock will not block the caller thread if not succeed
				bool Succeed = Mutex.TryLock();
				if (Succeed)
				{
					SharedResource++;
					Mutex.Unlock();
				}
			}
			if (!Mutex.IsLocked())
			{
				//Lock will block caller thread until it locker the mutex(deadlock might happen)
				Mutex.Lock();
				//Dont recursively lock!!!
				//Mutex.Lock();
				SharedResource++;
				Mutex.Unlock();
			}
		}

		{
			int SharedResource = 0;
			UE::FRecursiveMutex Mutex;
			{
				//Try lock will not block the caller thread if not succeed
				bool Succeed = Mutex.TryLock();
				if (Succeed)
				{
					SharedResource++;
					Mutex.Unlock();
				}
			}
			{
				//Lock will block caller thread until it locker the mutex(deadlock might happen)
				Mutex.Lock();
				//Recursively lock is supported
				Mutex.Lock();
				SharedResource++;
				Mutex.Unlock();
			}
		}

		{
			//A mutex that is constructed in locked state
			UE::FMutex MutexConstructedInLockedState(UE::AcquireLock);
			bool IsLocked = MutexConstructedInLockedState.IsLocked();
		}
	}

	//Critical Section
	{
		{
			int SharedResource = 0;
			FCriticalSection CS;
			{
				//Try lock will not block the caller thread if not succeed
				bool Succeed = CS.TryLock();
				if (Succeed)
				{
					SharedResource++;
					CS.Unlock();
				}
			}
			{
				//Lock will block caller thread until it locks the mutex(deadlock might happen)
				CS.Lock();
				//recursively lock a CS is ok on Windows. Not sure on other platforms(IOS/Android/Linux/...)
				CS.Lock();
				SharedResource++;
				CS.Unlock();
			}
		}
	}

	//FSpinLock
	{
		int SharedResource = 0;
		UE::FSpinLock Lock;

		//Try lock
		bool Succeed = Lock.TryLock();
		if (Succeed)
		{
			SharedResource++;
			Lock.Unlock();
		}

		//Lock a spin lock will not block the caller thread if anther thread owns the spin lock
		//Instead it will repeatedly tries to aquire the lock until succeed(waste CPU resource)
		//Should be used only for very short locks!!!
		Lock.Lock();
		SharedResource++;
		Lock.Unlock();
	}

	//TScopeLock and TScopeUnlock(RAII)
	{
		int SharedResource = 0;
		FCriticalSection CS;//Can also be a mutex type
		{
			//Within this scope the mutex remains locked and will unlock when goes out of this scope
			UE::TScopeLock Lock(CS);
			SharedResource++;
		}

		CS.Lock();
		{
			//Within this scope the mutex remains unlocked and will lock when goes out of this scope
			UE::TScopeUnlock Unlock(CS);
		}
		CS.Unlock();
	}

	//TUniqueLock(RAII)
	{
		int SharedResource = 0;
		UE::FMutex Mutex;
		{
			//Lock in constructor and unlock in destructor
			auto Lock = UE::TUniqueLock(Mutex);
			SharedResource++;
		}
	}

	//TDynamicUniqueLock(RAII)
	{
		int SharedResource = 0;
		UE::FMutex Mutex;
		{
			//Lock in constructor and unlock in destructor if locked
			//With the ability to dynamically lock and unlock the mutex type
			auto DynamicLock = UE::TDynamicUniqueLock(Mutex);
			SharedResource++;
			DynamicLock.Unlock();
			DynamicLock.Lock();
			SharedResource++;
		}
		{
			//Deferred lock(no lock in constructor)
			auto DynamicLock = UE::TDynamicUniqueLock(Mutex, UE::DeferLock);
			DynamicLock.Lock();
			SharedResource++;
			DynamicLock.Unlock();
			DynamicLock.Lock();
			SharedResource++;
		}
	}

	//FRWLock
	{
		int SharedResource = 0;
		FRWLock Lock;
		{
			FReadScopeLock ReadScope(Lock);
			int CurrentValue = SharedResource;
		}
		{
			FWriteScopeLock WriteScope(Lock);
			SharedResource++;
		}
		{
			FRWScopeLock ScopeLock(Lock, SLT_ReadOnly);
			int CurrentValue = SharedResource;
		}
		{
			FRWScopeLock ScopeLock(Lock, SLT_Write);
			SharedResource++;
		}
	}

	//FEvent
	{
		{
			FEvent* Event = FPlatformProcess::GetSynchEventFromPool(false /* bIsManualReset */);

			Event->Trigger();
			Event->Wait();
			Event->Trigger();
			Event->Wait(FTimespan::FromMilliseconds(10), false /* bIgnoreThreadIdleStats */);
			Event->Trigger();
			Event->Wait(10, false /* bIgnoreThreadIdleStats */);

			//Return this event to event pool
			FPlatformProcess::ReturnSynchEventToPool(Event);
		}
		{
			FEvent* Event = FPlatformProcess::GetSynchEventFromPool(true /* bIsManualReset */);

			Event->Trigger();

			//Other thread may wait for this event to be triggered
			Event->Wait();
			Event->Wait(FTimespan::FromMilliseconds(10), false /* bIgnoreThreadIdleStats */);
			Event->Wait(10, false /* bIgnoreThreadIdleStats */);

			//Manually reset the event
			Event->Reset();

			//Return this event to event pool
			FPlatformProcess::ReturnSynchEventToPool(Event);
		}
	}

	//FEventRef(RAII)
	{
		{
			FEventRef Event{ EEventMode::AutoReset };

			Event->Trigger();
			Event->Wait();
			Event->Trigger();
			Event->Wait(FTimespan::FromMilliseconds(10), false /* bIgnoreThreadIdleStats */);
			Event->Trigger();
			Event->Wait(10, false /* bIgnoreThreadIdleStats */);
		}
		{
			FEventRef Event{ EEventMode::ManualReset };

			Event->Trigger();

			//Other thread may wait for this event to be triggered
			Event->Wait();
			Event->Wait(FTimespan::FromMilliseconds(10), false /* bIgnoreThreadIdleStats */);
			Event->Wait(10, false /* bIgnoreThreadIdleStats */);

			//Manually reset the event
			Event->Reset();
		}
	}

	//thread_local keyword
	{
		for (int i = 0; i < 5; ++i)
		{
			Async(EAsyncExecution::Thread,
				[=]()
				{
					//For every thread, there will be an independent instance of ThreadLocalVariable.
					ThreadLocalVariable = i;
				}
			);
		}
	}
}
