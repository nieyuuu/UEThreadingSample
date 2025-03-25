#include "TextureProcesser.h"

ATaskTextureFilter::ATaskTextureFilter()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ATaskTextureFilter::Tick(float InDeltaTime)
{
	Super::Tick(InDeltaTime);

	//Check task status and broadcast if completed
	if (IsCompleted())
	{
		if (OnProcessFinished.IsBound())
		{
			OnProcessFinished.Broadcast(ProcessedResult);
			OnProcessFinished.Clear();
		}

		if (ProcessedResult)
		{
			ProcessedResult = nullptr;
		}

		if (Task.IsValid())
		{
			Task = UE::Tasks::FTask{};
		}
	}
}

//TODO:Dont Repeat Yourself
void ATaskTextureFilter::StartProcessing(UTexture2D* InSourceTexture)
{
	if (InSourceTexture)
	{
		if (!ValidateParameters(InSourceTexture, FilterSize, ScaleValue))
		{
			ProcessedResult = nullptr;
			Task = UE::Tasks::FTask{};

			if (OnProcessFinished.IsBound())
			{
				OnProcessFinished.Broadcast(nullptr);
				OnProcessFinished.Clear();
			}

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
			FilterType = this->FilterType, FilterSize = this->FilterSize]()
			{
				FilterTexture(SourceTexture, FilteredResult, FilterType, FilterSize, EConvolutionType::OneDVertical, false);
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
			FilterType = this->FilterType, FilterSize = this->FilterSize]()
			{
				FilterTexture(SourceTexture, FilteredResult, FilterType, FilterSize, EConvolutionType::OneDHorizontal, false);
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
			ScaleValue = this->ScaleValue]()
			{
				ScaleAlphaChannel(SourceTexture, ScaledResult, ScaleValue, false);
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

		ProcessedResult = CompositeResult;
		Task = CompositeResultUpdateTask;
	}
}

bool ATaskTextureFilter::IsCompleted() const
{
	if (Task.IsValid())
	{
		return Task.IsCompleted();
	}
	else
	{
		return false;
	}
}
