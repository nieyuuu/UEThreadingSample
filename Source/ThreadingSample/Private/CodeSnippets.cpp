#include "CoreMinimal.h"
#include "Tasks/TaskConcurrencyLimiter.h"

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
}
