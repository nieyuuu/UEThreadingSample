#include "ThreadingSampleBPLibrary.h"

#include "QueuedThreadPoolWrapper.h"
#include "QueuedThreadPoolWorks.h"

#include "Algo/RandomShuffle.h"

/*----------------------------------------------------------------------------------
	Async Interface Samples
----------------------------------------------------------------------------------*/
void UThreadingSampleBPLibrary::LoadTextFiles(ELoadTextFileExecution InExecution, float InSleepTimeInSeconds, const TArray<FString>& InFilesToLoad, TArray<UTextFileResult*>& OutResults)
{
	OutResults.Reserve(InFilesToLoad.Num());
	for (const FString& FileName : InFilesToLoad)
	{
		switch (InExecution)
		{
		case ELoadTextFileExecution::AsyncInterface_TaskGraph:
			OutResults.Add(LoadTextFile_AsyncInterface(FileName, EAsyncExecution::TaskGraph, InSleepTimeInSeconds));
			break;
		case ELoadTextFileExecution::AsyncInterface_ThreadPool:
			OutResults.Add(LoadTextFile_AsyncInterface(FileName, EAsyncExecution::ThreadPool, InSleepTimeInSeconds));
			break;
		case ELoadTextFileExecution::AsyncInterface_Thread:
			//Note that typically you should not use this in loops as creating and destroying thread are costly.
			//Its meant for long run tasks.
			OutResults.Add(LoadTextFile_AsyncInterface(FileName, EAsyncExecution::Thread, InSleepTimeInSeconds));
			break;
		case ELoadTextFileExecution::AsyncTaskInterface:
			OutResults.Add(LoadTextFile_AsyncTaskInterface(FileName, InSleepTimeInSeconds));
			break;
		case ELoadTextFileExecution::AsyncPoolInterface:
			OutResults.Add(LoadTextFile_AsyncPoolInterface(FileName, InSleepTimeInSeconds));
			break;
		case ELoadTextFileExecution::AsyncThreadInterface:
			//Note that typically you should not use this in loops as creating and destroying thread are costly.
			//Its meant for long run tasks.
			OutResults.Add(LoadTextFile_AsyncThreadInterface(FileName, InSleepTimeInSeconds));
			break;
		default:
			check(false);
			break;
		}
	}
}

/*----------------------------------------------------------------------------------
	Texture Filter Samples
----------------------------------------------------------------------------------*/
void UThreadingSampleBPLibrary::FilterTextureUsingParallelFor(UTexture2D* InSourceTexture, EFilterType InFilterType, int InFilterSize, float InScaleValue, bool InOnePass, bool InForceSingleThread, UTexture2D*& OutFilteredTexture)
{
	if (!ValidateParameters(InSourceTexture, InFilterSize, InScaleValue))
	{
		OutFilteredTexture = nullptr;
		return;
	}

	if (!InOnePass)
	{
		UTexture2D* VerticalPassResult = CreateTransientTextureFromSource(InSourceTexture, TEXT("VerticalPassResult"));
		UTexture2D* HorizontalPassResult = CreateTransientTextureFromSource(InSourceTexture, TEXT("HorizontalPassResult"));
		UTexture2D* ScaleAlphaResult = CreateTransientTextureFromSource(InSourceTexture, TEXT("ScaleAlphaResult"));
		UTexture2D* CompositeResult = CreateTransientTextureFromSource(InSourceTexture, TEXT("CompositeResult"));

		FilterTexture(InSourceTexture, VerticalPassResult, InFilterType, InFilterSize, EConvolutionType::OneDVertical, InForceSingleThread);
		VerticalPassResult->UpdateResource();

		FilterTexture(VerticalPassResult, HorizontalPassResult, InFilterType, InFilterSize, EConvolutionType::OneDHorizontal, InForceSingleThread);
		HorizontalPassResult->UpdateResource();

		ScaleAlphaChannel(InSourceTexture, ScaleAlphaResult, InScaleValue, InForceSingleThread);
		ScaleAlphaResult->UpdateResource();

		CompositeRGBAValue(HorizontalPassResult, ScaleAlphaResult, CompositeResult, InForceSingleThread);
		CompositeResult->UpdateResource();

		OutFilteredTexture = CompositeResult;
	}
	else
	{
		UTexture2D* FilteredResult = CreateTransientTextureFromSource(InSourceTexture, TEXT("FilteredResult"));
		UTexture2D* ScaleAlphaResult = CreateTransientTextureFromSource(InSourceTexture, TEXT("ScaleAlphaResult"));
		UTexture2D* CompositeResult = CreateTransientTextureFromSource(InSourceTexture, TEXT("CompositeResult"));

		FilterTexture(InSourceTexture, FilteredResult, InFilterType, InFilterSize, EConvolutionType::TwoD, InForceSingleThread);
		FilteredResult->UpdateResource();

		ScaleAlphaChannel(InSourceTexture, ScaleAlphaResult, InScaleValue, InForceSingleThread);
		ScaleAlphaResult->UpdateResource();

		CompositeRGBAValue(FilteredResult, ScaleAlphaResult, CompositeResult, InForceSingleThread);
		CompositeResult->UpdateResource();

		OutFilteredTexture = CompositeResult;
	}
}

void UThreadingSampleBPLibrary::FilterTextureUsingTaskSystem(UTexture2D* InSourceTexture, EFilterType InFilterType, int InFilterSize, float InScaleValue, UResultUsingTaskSystem*& OutResult)
{
	if (!ValidateParameters(InSourceTexture, InFilterSize, InScaleValue))
	{
		OutResult = nullptr;
		return;
	}

	UTexture2D* VerticalPassResult = CreateTransientTextureFromSource(InSourceTexture, TEXT("VerticalPassResult"));
	UTexture2D* HorizontalPassResult = CreateTransientTextureFromSource(InSourceTexture, TEXT("HorizontalPassResult"));
	//We need ScaleAlphaChannelInput here because the first filter task and scale alpha channel task could overlap their execution.
	//The calling of Lock() and Unlock() could assert in such case if we pass InSourceTexture to both tasks.
	//We just duplicate InSourceTexture to another texture which we pass to scale alpha channel task for simplicity.
	UTexture2D* ScaleAlphaChannelInput = CreateTransientTextureFromSource(InSourceTexture, TEXT("ScaleAlphaChannelInput"), true);
	UTexture2D* ScaleAlphaChannelResult = CreateTransientTextureFromSource(InSourceTexture, TEXT("ScaleAlphaChannelResult"));
	UTexture2D* CompositeResult = CreateTransientTextureFromSource(InSourceTexture, TEXT("CompositeResult"));

	auto VerticalPassTask = UE::Tasks::Launch(
		UE_SOURCE_LOCATION,
		[SourceTexture = TWeakObjectPtr<UTexture2D>(InSourceTexture),
		FilteredResult = TWeakObjectPtr<UTexture2D>(VerticalPassResult),
		InFilterType, InFilterSize]()
		{
			FilterTexture(SourceTexture, FilteredResult, InFilterType, InFilterSize, EConvolutionType::OneDVertical, false);
		},
		LowLevelTasks::ETaskPriority::BackgroundHigh,
		UE::Tasks::EExtendedTaskPriority::None
	);

	auto VerticalPassResultUpdateTask = UE::Tasks::Launch(
		UE_SOURCE_LOCATION,
		[TextureToUpdate = TWeakObjectPtr<UTexture2D>(VerticalPassResult)]()
		{
			TextureToUpdate->UpdateResource();
		},
		UE::Tasks::Prerequisites(VerticalPassTask),
		LowLevelTasks::ETaskPriority::BackgroundHigh,
		UE::Tasks::EExtendedTaskPriority::GameThreadNormalPri //Executed on GameThread.
	);

	auto HorizontalPassTask = UE::Tasks::Launch(
		UE_SOURCE_LOCATION,
		[SourceTexture = TWeakObjectPtr<UTexture2D>(VerticalPassResult),
		FilteredResult = TWeakObjectPtr<UTexture2D>(HorizontalPassResult),
		InFilterType, InFilterSize]()
		{
			FilterTexture(SourceTexture, FilteredResult, InFilterType, InFilterSize, EConvolutionType::OneDHorizontal, false);
		},
		UE::Tasks::Prerequisites(VerticalPassResultUpdateTask),
		LowLevelTasks::ETaskPriority::BackgroundHigh,
		UE::Tasks::EExtendedTaskPriority::None
	);

	auto HorizontalPassResultUpdateTask = UE::Tasks::Launch(
		UE_SOURCE_LOCATION,
		[TextureToUpdate = TWeakObjectPtr<UTexture2D>(HorizontalPassResult)]()
		{
			TextureToUpdate->UpdateResource();
		},
		UE::Tasks::Prerequisites(HorizontalPassTask),
		LowLevelTasks::ETaskPriority::BackgroundHigh,
		UE::Tasks::EExtendedTaskPriority::GameThreadNormalPri //Executed on GameThread.
	);

	auto ScaleAlphaChannelTask = UE::Tasks::Launch(
		UE_SOURCE_LOCATION,
		[SourceTexture = TWeakObjectPtr<UTexture2D>(ScaleAlphaChannelInput),
		ScaledResult = TWeakObjectPtr<UTexture2D>(ScaleAlphaChannelResult),
		InScaleValue]()
		{
			ScaleAlphaChannel(SourceTexture, ScaledResult, InScaleValue, false);
		},
		LowLevelTasks::ETaskPriority::BackgroundHigh,
		UE::Tasks::EExtendedTaskPriority::None
	);

	auto ScaleAlphaChannelResultUpdateTask = UE::Tasks::Launch(
		UE_SOURCE_LOCATION,
		[TextureToUpdate = TWeakObjectPtr<UTexture2D>(ScaleAlphaChannelResult)]()
		{
			TextureToUpdate->UpdateResource();
		},
		UE::Tasks::Prerequisites(ScaleAlphaChannelTask),
		LowLevelTasks::ETaskPriority::BackgroundHigh,
		UE::Tasks::EExtendedTaskPriority::GameThreadNormalPri //Executed on GameThread.
	);

	auto CompositeTask = UE::Tasks::Launch(
		UE_SOURCE_LOCATION,
		[RGBTexture = TWeakObjectPtr<UTexture2D>(HorizontalPassResult),
		AlphaTexture = TWeakObjectPtr<UTexture2D>(ScaleAlphaChannelResult),
		Result = TWeakObjectPtr<UTexture2D>(CompositeResult)]()
		{
			CompositeRGBAValue(RGBTexture, AlphaTexture, Result, false);
		},
		UE::Tasks::Prerequisites(HorizontalPassResultUpdateTask, ScaleAlphaChannelResultUpdateTask),
		LowLevelTasks::ETaskPriority::BackgroundHigh,
		UE::Tasks::EExtendedTaskPriority::None
	);

	auto CompositeResultUpdateTask = UE::Tasks::Launch(
		UE_SOURCE_LOCATION,
		[TextureToUpdate = TWeakObjectPtr<UTexture2D>(CompositeResult)]()
		{
			TextureToUpdate->UpdateResource();
		},
		UE::Tasks::Prerequisites(CompositeTask),
		LowLevelTasks::ETaskPriority::BackgroundHigh,
		UE::Tasks::EExtendedTaskPriority::GameThreadNormalPri //Executed on GameThread.
	);

	OutResult = NewObject<UResultUsingTaskSystem>();

	OutResult->SetResult(CompositeResult, CompositeResultUpdateTask);
}

void UThreadingSampleBPLibrary::FilterTextureUsingTaskGraphSystem(UTexture2D* InSourceTexture, EFilterType InFilterType, int InFilterSize, float InScaleValue, bool InHoldSourceTasks, UResultUsingTaskGraphSystem*& OutResult)
{
	if (!ValidateParameters(InSourceTexture, InFilterSize, InScaleValue))
	{
		OutResult = nullptr;
		return;
	}

	UTexture2D* VerticalPassResult = CreateTransientTextureFromSource(InSourceTexture, TEXT("VerticalPassResult"));
	UTexture2D* HorizontalPassResult = CreateTransientTextureFromSource(InSourceTexture, TEXT("HorizontalPassResult"));
	//We need ScaleAlphaChannelInput here because the first filter task and scale alpha channel task could overlap their execution.
	//The calling of Lock() and Unlock() could assert in such case if we pass InSourceTexture to both tasks.
	//We just duplicate InSourceTexture to another texture which we pass to scale alpha channel task for simplicity.
	UTexture2D* ScaleAlphaChannelInput = CreateTransientTextureFromSource(InSourceTexture, TEXT("ScaleAlphaChannelInput"), true);
	UTexture2D* ScaleAlphaChannelResult = CreateTransientTextureFromSource(InSourceTexture, TEXT("ScaleAlphaChannelResult"));
	UTexture2D* CompositeResult = CreateTransientTextureFromSource(InSourceTexture, TEXT("CompositeResult"));

	//Construct and hold or construct and dispatch when ready.
	//If construct and hold, the task will not start execute until we explicitly unlock it(And of course its subsequents will not execute).
	auto VerticalPassTask = InHoldSourceTasks ?
		TGraphTask<FTextureFilterTask>::CreateTask(
			nullptr, ENamedThreads::GameThread).ConstructAndHold(
				TWeakObjectPtr<UTexture2D>(InSourceTexture),
				TWeakObjectPtr<UTexture2D>(VerticalPassResult),
				InFilterType, InFilterSize, EConvolutionType::OneDVertical)
		: TGraphTask<FTextureFilterTask>::CreateTask(
			nullptr, ENamedThreads::GameThread).ConstructAndDispatchWhenReady(
				TWeakObjectPtr<UTexture2D>(InSourceTexture),
				TWeakObjectPtr<UTexture2D>(VerticalPassResult),
				InFilterType, InFilterSize, EConvolutionType::OneDVertical);

	FGraphEventArray Prerequisites1;
	Prerequisites1.Add(VerticalPassTask);

	auto VerticalPassResultUpdateTask = TGraphTask<FUpdateResourceTask>::CreateTask(
		&Prerequisites1, ENamedThreads::GameThread).ConstructAndDispatchWhenReady(
			TWeakObjectPtr<UTexture2D>(VerticalPassResult));

	FGraphEventArray Prerequisites2;
	Prerequisites2.Add(VerticalPassResultUpdateTask);

	auto HorizontalPassTask = TGraphTask<FTextureFilterTask>::CreateTask(
		&Prerequisites2, ENamedThreads::GameThread).ConstructAndDispatchWhenReady(
			TWeakObjectPtr<UTexture2D>(VerticalPassResult),
			TWeakObjectPtr<UTexture2D>(HorizontalPassResult),
			InFilterType, InFilterSize, EConvolutionType::OneDHorizontal);

	FGraphEventArray Prerequisites3;
	Prerequisites3.Add(HorizontalPassTask);

	auto HorizontalPassResultUpdateTask = TGraphTask<FUpdateResourceTask>::CreateTask(
		&Prerequisites3, ENamedThreads::GameThread).ConstructAndDispatchWhenReady(
			TWeakObjectPtr<UTexture2D>(HorizontalPassResult));

	auto ScaleAlphaChannelTask = InHoldSourceTasks ?
		TGraphTask<FScaleAlphaChannelTask>::CreateTask(
			nullptr, ENamedThreads::GameThread).ConstructAndHold(
				TWeakObjectPtr<UTexture2D>(ScaleAlphaChannelInput),
				TWeakObjectPtr<UTexture2D>(ScaleAlphaChannelResult),
				InScaleValue)
		: TGraphTask<FScaleAlphaChannelTask>::CreateTask(
			nullptr, ENamedThreads::GameThread).ConstructAndDispatchWhenReady(
				TWeakObjectPtr<UTexture2D>(ScaleAlphaChannelInput),
				TWeakObjectPtr<UTexture2D>(ScaleAlphaChannelResult),
				InScaleValue);

	FGraphEventArray Prerequisites4;
	Prerequisites4.Add(ScaleAlphaChannelTask);

	auto ScaleAlphaChannelResultUpdateTask = TGraphTask<FUpdateResourceTask>::CreateTask(
		&Prerequisites4, ENamedThreads::GameThread).ConstructAndDispatchWhenReady(
			TWeakObjectPtr<UTexture2D>(ScaleAlphaChannelResult));

	FGraphEventArray Prerequisites5;
	Prerequisites5.Add(HorizontalPassResultUpdateTask);
	Prerequisites5.Add(ScaleAlphaChannelResultUpdateTask);

	auto CompositeTask = TGraphTask<FCompositeRGBAValueTask>::CreateTask(
		&Prerequisites5, ENamedThreads::GameThread).ConstructAndDispatchWhenReady(
			HorizontalPassResult,
			ScaleAlphaChannelResult,
			CompositeResult);

	FGraphEventArray Prerequisites6;
	Prerequisites6.Add(CompositeTask);

	auto CompositeResultUpdateTask = TGraphTask<FUpdateResourceTask>::CreateTask(
		&Prerequisites6, ENamedThreads::GameThread).ConstructAndDispatchWhenReady(
			TWeakObjectPtr<UTexture2D>(CompositeResult));

	if (InHoldSourceTasks)
	{
		//Let the task begin execute(Let the scheduler schedule the task to be executed on a worker thread).
		VerticalPassTask->Unlock();
		ScaleAlphaChannelTask->Unlock();
	}

	OutResult = NewObject<UResultUsingTaskGraphSystem>();

	OutResult->SetResult(CompositeResult, CompositeResultUpdateTask);
}

void UThreadingSampleBPLibrary::FilterTextureUsingPipe(UTexture2D* InSourceTexture, EFilterType InFilterType, int InFilterSize, float InScaleValue, UResultUsingPipe*& OutResult)
{
	if (!ValidateParameters(InSourceTexture, InFilterSize, InScaleValue))
	{
		OutResult = nullptr;
		return;
	}

	UTexture2D* VerticalPassResult = CreateTransientTextureFromSource(InSourceTexture, TEXT("VerticalPassResult"));
	UTexture2D* HorizontalPassResult = CreateTransientTextureFromSource(InSourceTexture, TEXT("HorizontalPassResult"));
	//We dont need this anymore, as we are launching tasks through FPipe(The DAG becomes a chain of tasks).
	// UTexture2D* ScaleAlphaChannelInput = CreateTransientTextureFromSource(InSourceTexture, TEXT("ScaleAlphaChannelInput"), true);
	UTexture2D* ScaleAlphaChannelResult = CreateTransientTextureFromSource(InSourceTexture, TEXT("ScaleAlphaChannelResult"));
	UTexture2D* CompositeResult = CreateTransientTextureFromSource(InSourceTexture, TEXT("CompositeResult"));

	//We are launching tasks through FPipe.
	TUniquePtr<UE::Tasks::FPipe> Pipe = MakeUnique<UE::Tasks::FPipe>(TEXT("TextureFilterPipe"));

	auto VerticalPassTask = Pipe->Launch(
		UE_SOURCE_LOCATION,
		[SourceTexture = TWeakObjectPtr<UTexture2D>(InSourceTexture),
		FilteredResult = TWeakObjectPtr<UTexture2D>(VerticalPassResult),
		InFilterType, InFilterSize]()
		{
			FilterTexture(SourceTexture, FilteredResult, InFilterType, InFilterSize, EConvolutionType::OneDVertical, false);
		},
		LowLevelTasks::ETaskPriority::BackgroundHigh,
		UE::Tasks::EExtendedTaskPriority::None
	);

	auto VerticalPassResultUpdateTask = Pipe->Launch(
		UE_SOURCE_LOCATION,
		[TextureToUpdate = TWeakObjectPtr<UTexture2D>(VerticalPassResult)]()
		{
			TextureToUpdate->UpdateResource();
		},
		UE::Tasks::Prerequisites(VerticalPassTask),
		LowLevelTasks::ETaskPriority::BackgroundHigh,
		UE::Tasks::EExtendedTaskPriority::GameThreadNormalPri //Executed on GameThread.
	);

	auto HorizontalPassTask = Pipe->Launch(
		UE_SOURCE_LOCATION,
		[SourceTexture = TWeakObjectPtr<UTexture2D>(VerticalPassResult),
		FilteredResult = TWeakObjectPtr<UTexture2D>(HorizontalPassResult),
		InFilterType, InFilterSize]()
		{

			FilterTexture(SourceTexture, FilteredResult, InFilterType, InFilterSize, EConvolutionType::OneDHorizontal, false);
		},
		UE::Tasks::Prerequisites(VerticalPassResultUpdateTask),
		LowLevelTasks::ETaskPriority::BackgroundHigh,
		UE::Tasks::EExtendedTaskPriority::None
	);

	auto HorizontalPassResultUpdateTask = Pipe->Launch(
		UE_SOURCE_LOCATION,
		[TextureToUpdate = TWeakObjectPtr<UTexture2D>(HorizontalPassResult)]()
		{
			TextureToUpdate->UpdateResource();
		},
		UE::Tasks::Prerequisites(HorizontalPassTask),
		LowLevelTasks::ETaskPriority::BackgroundHigh,
		UE::Tasks::EExtendedTaskPriority::GameThreadNormalPri //Executed on GameThread.
	);

	auto ScaleAlphaChannelTask = Pipe->Launch(
		UE_SOURCE_LOCATION,
		[SourceTexture = TWeakObjectPtr<UTexture2D>(/*ScaleAlphaChannelInput*/InSourceTexture),
		ScaledResult = TWeakObjectPtr<UTexture2D>(ScaleAlphaChannelResult),
		InScaleValue]()
		{
			ScaleAlphaChannel(SourceTexture, ScaledResult, InScaleValue, false);
		},
		LowLevelTasks::ETaskPriority::BackgroundHigh,
		UE::Tasks::EExtendedTaskPriority::None
	);

	auto ScaleAlphaChannelResultUpdateTask = Pipe->Launch(
		UE_SOURCE_LOCATION,
		[TextureToUpdate = TWeakObjectPtr<UTexture2D>(ScaleAlphaChannelResult)]()
		{
			TextureToUpdate->UpdateResource();
		},
		UE::Tasks::Prerequisites(ScaleAlphaChannelTask),
		LowLevelTasks::ETaskPriority::BackgroundHigh,
		UE::Tasks::EExtendedTaskPriority::GameThreadNormalPri //Executed on GameThread.
	);

	auto CompositeTask = Pipe->Launch(
		UE_SOURCE_LOCATION,
		[RGBTexture = TWeakObjectPtr<UTexture2D>(HorizontalPassResult),
		AlphaTexture = TWeakObjectPtr<UTexture2D>(ScaleAlphaChannelResult),
		Result = TWeakObjectPtr<UTexture2D>(CompositeResult)]()
		{
			CompositeRGBAValue(RGBTexture, AlphaTexture, Result, false);
		},
		UE::Tasks::Prerequisites(HorizontalPassResultUpdateTask, ScaleAlphaChannelResultUpdateTask),
		LowLevelTasks::ETaskPriority::BackgroundHigh,
		UE::Tasks::EExtendedTaskPriority::None
	);

	auto CompositeResultUpdateTask = Pipe->Launch(
		UE_SOURCE_LOCATION,
		[TextureToUpdate = TWeakObjectPtr<UTexture2D>(CompositeResult)]()
		{
			TextureToUpdate->UpdateResource();
		},
		UE::Tasks::Prerequisites(CompositeTask),
		LowLevelTasks::ETaskPriority::BackgroundHigh,
		UE::Tasks::EExtendedTaskPriority::GameThreadNormalPri //Executed on GameThread.
	);

	OutResult = NewObject<UResultUsingPipe>();

	OutResult->SetResult(CompositeResult, MoveTemp(Pipe));
}

/*----------------------------------------------------------------------------------
	Nested Task Sample
----------------------------------------------------------------------------------*/
//Notice that nested task is used to define the completion timeing of the outer task, not the execution order of outer task and nested task.
//Its kinda like that the nested task is a prerequisite of the outer task but its really not that.
//Nested task can execute concurrently with outer task, but a task can only begin execute when all its prerequisites are completed.
void UThreadingSampleBPLibrary::ExecuteNestedTask(int InCurrentCallIndex)
{
	auto OuterTask = UE::Tasks::Launch(
		UE_SOURCE_LOCATION,
		[=]()
		{
			//We are lauching inside a task.
			auto NestedTask = UE::Tasks::Launch(
				UE_SOURCE_LOCATION,
				[=]()
				{
					UE_LOG(LogThreadingSample, Display, TEXT("CurrentIndex:%d(ThreadID:%d):Executing nested task."), InCurrentCallIndex, FPlatformTLS::GetCurrentThreadId());
					FPlatformProcess::Sleep(0.3);
				},
				UE::Tasks::ETaskPriority::BackgroundLow,
				UE::Tasks::EExtendedTaskPriority::None
			);

			auto AnotherNestedTask = UE::Tasks::Launch(
				UE_SOURCE_LOCATION,
				[=]()
				{
					UE_LOG(LogThreadingSample, Display, TEXT("CurrentIndex:%d(ThreadID:%d):Executing another nested task."), InCurrentCallIndex, FPlatformTLS::GetCurrentThreadId());
					FPlatformProcess::Sleep(0.4);
				},
				UE::Tasks::ETaskPriority::BackgroundLow,
				UE::Tasks::EExtendedTaskPriority::None
			);

			UE::Tasks::AddNested(AnotherNestedTask);
			UE::Tasks::AddNested(NestedTask);

			UE_LOG(LogThreadingSample, Display, TEXT("CurrentIndex:%d(ThreadID:%d):Executing outer task."), InCurrentCallIndex, FPlatformTLS::GetCurrentThreadId());
			FPlatformProcess::Sleep(0.1);
		},
		UE::Tasks::ETaskPriority::BackgroundLow,
		UE::Tasks::EExtendedTaskPriority::None
	);

	//Here we dont really care about the result, just wait with a timeout.
	//OuterTask.Wait(FTimespan::FromMilliseconds(100));

	//This can be true or false, depending on:
	//1. Whether the nested tasks have completed or not.
	//2. If all the nested tasks have completed, Whether the OuterTask itself has completed or not.
	bool IsCompleted = OuterTask.IsCompleted();
}

/*----------------------------------------------------------------------------------
	Low Level Task Sample
----------------------------------------------------------------------------------*/
void UThreadingSampleBPLibrary::RunLowLevelTaskTest()
{
	int TestValue = 100;

	UE_LOG(LogThreadingSample, Display, TEXT("Begin Running Low Level Task Test. TestValue = %d"), TestValue);

	//Create a low level task.
	TSharedPtr<LowLevelTasks::FTask> Task = MakeShared<LowLevelTasks::FTask>();

	//Initialize the low level task.
	Task->Init(
		TEXT("SimpleLowLevelTask"),
		LowLevelTasks::ETaskPriority::Default,
		[&]() {
			TestValue = 1337;
		},
		LowLevelTasks::ETaskFlags::DefaultFlags
	);

	//Try to launch the task(the scheduler will handle this).
	bool WasLaunched = LowLevelTasks::TryLaunch(*Task, LowLevelTasks::EQueuePreference::DefaultPreference, true/*bWakeUpWorker*/);

	//Try to cancel the task.
	bool WasCanceled = Task->TryCancel(LowLevelTasks::ECancellationFlags::DefaultFlags);

	if (WasCanceled)
	{
		//Try to revive the task.
		bool WasRevived = Task->TryRevive();

		if (!WasRevived)
		{
			//Wait low level task system marks this task as completed.
			while (!Task->IsCompleted())
			{
				FPlatformProcess::Sleep(0.005f);
			}

			check(Task->IsCompleted());
			check(TestValue == 100); //TestValue is unchanged(we canceled this task and failed to revive it).
		}
		else
		{
			//We canceled this task and then succeed to revive it.
			//Now we wait for the task to be completed.
			while (!Task->IsCompleted())
			{
				FPlatformProcess::Sleep(0.005f);
				//Try to expedite the task.
				// bool WasExpedite = Task->TryExpedite();
			}

			check(Task->IsCompleted());
			check(TestValue == 1337);
		}
	}
	else
	{
		//Wait for the task to be completed.
		while (!Task->IsCompleted())
		{
			FPlatformProcess::Sleep(0.005f);
			//Try to expedite the task.
			// bool WasExpedite = Task->TryExpedite();
		}

		check(Task->IsCompleted());
		check(TestValue == 1337);
	}

	UE_LOG(LogThreadingSample, Display, TEXT("End Running Low Level Task Test. TestValue = %d"), TestValue);
}

/*----------------------------------------------------------------------------------
	FRunnable/FRunnableThread and FThread Samples
----------------------------------------------------------------------------------*/
void UThreadingSampleBPLibrary::CreateRunnable(UMyRunnable*& OutRunnable)
{
	OutRunnable = NewObject<UMyRunnable>(GetTransientPackage());
}

void UThreadingSampleBPLibrary::CreateFThread(UMyFThread*& OutFThread)
{
	OutFThread = NewObject<UMyFThread>(GetTransientPackage());
}

/*----------------------------------------------------------------------------------
	Queued Thread Pool Samples
----------------------------------------------------------------------------------*/
void UThreadingSampleBPLibrary::DoThreadedWorkUsingQueuedThreadPool(const TArray<int32>& InArrayToSort, const FString& InStringToLog, int32 InFibonacciToCompute, int32 InNumWorksForWrapper)
{
	FQueuedThreadPool* ThreadPool = GThreadPool;

#if WITH_EDITOR
	ThreadPool = GLargeThreadPool;
#endif

	check(ThreadPool);

	//Simply add work to the thread pool.
	ThreadPool->AddQueuedWork(new FDummyEmptyWork);
	ThreadPool->AddQueuedWork(new FOutputStringToLogWork(InStringToLog));

	//Start executing the task(the task will be auto deleted).
	(new FAutoDeleteAsyncTask<FAutoDeleteOutputStringToLogTask>(InStringToLog))->StartBackgroundTask(ThreadPool, EQueuedWorkPriority::Lowest);
	//Or just run the task on current thread.
	// (new FAutoDeleteAsyncTask<FAutoDeleteOutputStringToLogTask>(TEXT("Test from auto delete task")))->StartSynchronousTask();

	//Start executing the task.
	auto SortArrayTask = new FAsyncTask<FSortIntArrayTask>(InArrayToSort);
	SortArrayTask->StartBackgroundTask(ThreadPool, EQueuedWorkPriority::Highest);
	//Or just run the task on current thread.
	// SortArrayTask->StartSynchronousTask();

	if (SortArrayTask->IsDone()) //See if the task has completed.
	{
		UE_LOG(LogThreadingSample, Display, TEXT("Sort array task has completed!"));
	}
	else if (SortArrayTask->IsWorkDone()) //See if the work is done(But the task might not be completed).
	{
		UE_LOG(LogThreadingSample, Display, TEXT("Sort array work is done!"));
	}

	UE_LOG(LogThreadingSample, Display, TEXT("Ensure sort array task completion!"));
	SortArrayTask->EnsureCompletion(false/*bDoWorkOnThisThreadIfNotStarted*/, false/*bIsLatencySensitive*/);

	//Retrieve the result(and do something).
	FSortIntArrayTask& UserTask = SortArrayTask->GetTask();
	TStringBuilder<1024> StringBuilder;
	FString RetrievedResult = BuildStringFromArray(StringBuilder, UserTask.GetArray());
	UE_LOG(LogThreadingSample, Display, TEXT("The retrieved sorted result: %s"), *RetrievedResult);

	//Now the time to delete our task.
	delete SortArrayTask;

	/*----------------------------------------------------------------------------------
	Queued Thread Pool Wrapper Samples
	----------------------------------------------------------------------------------*/
	//Clamp incase deep recursive call / integer overflow / stack overflow
	InFibonacciToCompute = FMath::Clamp(InFibonacciToCompute, 0, 45);

	InNumWorksForWrapper = FMath::Clamp(InNumWorksForWrapper, 1, 30);

	FQueuedThreadPoolWrapper* Wrapper = GetQueuedThreadPoolWrapper();
	for (int i = 0; i < InNumWorksForWrapper; ++i)
	{
		//We added with EQueuedWorkPriority::Highest, but they will be mapped to other priority based on the provided priority mapper.
		//And we limited the max concurrency to 1 at the creation of this wrapper.
		Wrapper->AddQueuedWork(new FFibonacciComputationWork(InFibonacciToCompute), EQueuedWorkPriority::Highest);
	}

	FQueuedThreadPoolDynamicWrapper* DynamicWrapper = GetQueuedThreadPoolDynamicWrapper();
	//Initialize work weights and randomly shuffle works.
	TArray<FWorkWithWeight*> Works;
	for (int i = 0; i < InNumWorksForWrapper; ++i)
	{
		Works.Add(new FWorkWithWeight(float(i)));
	}
	Algo::RandomShuffle(Works);

	//Pause the thread pool wrapper so we can sort/reorder works before it gets executed and check the results in logs.
	DynamicWrapper->Pause();

	//Start queueing works to the thread pool wrapper.
	for (int i = 0; i < Works.Num(); ++i)
	{
		DynamicWrapper->AddQueuedWork(Works[i]);
	}

	//The sort predicate which is based on the work weight.
	//Theoretically you can do anything since you can get the work instance itself.
	auto SortPredicate = [](const IQueuedWork* Lhs, const IQueuedWork* Rhs) {
		const FWorkWithWeight* WorkA = (const FWorkWithWeight*)Lhs;
		const FWorkWithWeight* WorkB = (const FWorkWithWeight*)Rhs;

		float WeightA = WorkA->GetWeight();
		float WeightB = WorkB->GetWeight();

		return WeightA > WeightB;
		};

	//Do the sort operation.
	DynamicWrapper->Sort(SortPredicate);

	//Resume the thread pool wrapper.
	DynamicWrapper->Resume();
}
