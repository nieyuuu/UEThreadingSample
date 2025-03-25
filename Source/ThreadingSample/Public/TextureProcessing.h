#pragma once

#include "ThreadingSample/ThreadingSample.h"

#include "TextureProcessing.generated.h"

UENUM(BlueprintType)
enum class EFilterType : uint8
{
	BoxFilter,
	GaussianFilter
};

enum class EConvolutionType : uint8
{
	TwoD,
	OneDVertical,
	OneDHorizontal
};

const TCHAR* EFilterTypeToString(EFilterType InFilterType);

const TCHAR* EConvolutionTypeToString(EConvolutionType InConvolutionType);

//A function that filters the RGB channels of InSourceTexture using ParallelFor.
//Can be done by one 2D convolution or two 1D convolutions.
//[TextureWidth * TextureHeight * FilterSize * FilterSize] Or [2 * TextureWidth * TextureHeight * FilterSize]
void FilterTexture(TWeakObjectPtr<UTexture2D> InSourceTexture, TWeakObjectPtr<UTexture2D> OutFilteredTexture, EFilterType InFilterType, int32 InFilterSize, EConvolutionType InConvolutionType, bool InForceSingleThread);

//A function that scales the alpha channel of InSourceTexture using ParallelFor.
void ScaleAlphaChannel(TWeakObjectPtr<UTexture2D> InSourceTexture, TWeakObjectPtr<UTexture2D> OutScaledTexture, float InScaleValue, bool InForceSingleThread);

//A function that composites the RGB channels of a texture and the Alpha channel of another texture using ParallelFor.
void CompositeRGBAValue(TWeakObjectPtr<UTexture2D> InRGBTexture, TWeakObjectPtr<UTexture2D> InATexture, TWeakObjectPtr<UTexture2D> OutTexture, bool InForceSingleThread);

bool ValidateParameters(UTexture2D* InSourceTexture, int InFilterSize, float InScaleValue);

UTexture2D* CreateTransientTextureFromSource(UTexture2D* InSourceTexture, const FString& InTextureName, bool InCopySourceImage = false);

//The task graph system tasks
class FTextureFilterTask
{
public:
	FTextureFilterTask(TWeakObjectPtr<UTexture2D> InSourceTexture, TWeakObjectPtr<UTexture2D> InFilteredTexture, EFilterType InFilterType, int InFilterSize, EConvolutionType InConvolutionType)
		:FilterType(InFilterType), FilterSize(InFilterSize), ConvolutionType(InConvolutionType), SourceTexture(InSourceTexture), FilteredTexture(InFilteredTexture)
	{
	}

	TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FTextureFilterTask, STATGROUP_TaskGraphTasks);
	}

	static ENamedThreads::Type GetDesiredThread()
	{
		return ENamedThreads::AnyBackgroundHiPriTask;
	}

	static ESubsequentsMode::Type GetSubsequentsMode()
	{
		return ESubsequentsMode::TrackSubsequents;
	}

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		FilterTexture(SourceTexture, FilteredTexture, FilterType, FilterSize, ConvolutionType, false);
	}

private:
	EFilterType FilterType = EFilterType::BoxFilter;
	int FilterSize = 3;
	EConvolutionType ConvolutionType = EConvolutionType::TwoD;

	TWeakObjectPtr<UTexture2D> SourceTexture;
	TWeakObjectPtr<UTexture2D> FilteredTexture;
};

//The task graph system tasks
class FScaleAlphaChannelTask
{
public:
	FScaleAlphaChannelTask(TWeakObjectPtr<UTexture2D> InSourceTexture, TWeakObjectPtr<UTexture2D> InScaledTexture, float InScaleValue)
		: ScaleValue(InScaleValue), SourceTexture(InSourceTexture), ScaledTexture(InScaledTexture)
	{
	}

	TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FScaleAlphaChannelTask, STATGROUP_TaskGraphTasks);
	}

	static ENamedThreads::Type GetDesiredThread()
	{
		return ENamedThreads::AnyBackgroundHiPriTask;
	}

	static ESubsequentsMode::Type GetSubsequentsMode()
	{
		return ESubsequentsMode::TrackSubsequents;
	}

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		ScaleAlphaChannel(SourceTexture, ScaledTexture, ScaleValue, false);
	}

private:
	float ScaleValue = 0.5;

	TWeakObjectPtr<UTexture2D> SourceTexture;
	TWeakObjectPtr<UTexture2D> ScaledTexture;
};

//The task graph system tasks
class FCompositeRGBAValueTask
{
public:
	FCompositeRGBAValueTask(TWeakObjectPtr<UTexture2D> InRGBTexture, TWeakObjectPtr<UTexture2D> InATexture, TWeakObjectPtr<UTexture2D> OutTexture)
		: RGBTexture(InRGBTexture), AlphaTexture(InATexture), CompositedTexture(OutTexture)
	{
	}

	TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FCompositeRGBAValueTask, STATGROUP_TaskGraphTasks);
	}

	static ENamedThreads::Type GetDesiredThread()
	{
		return ENamedThreads::AnyBackgroundHiPriTask;
	}

	static ESubsequentsMode::Type GetSubsequentsMode()
	{
		return ESubsequentsMode::TrackSubsequents;
	}

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		CompositeRGBAValue(RGBTexture, AlphaTexture, CompositedTexture, false);
	}

private:
	TWeakObjectPtr<UTexture2D> RGBTexture;
	TWeakObjectPtr<UTexture2D> AlphaTexture;
	TWeakObjectPtr<UTexture2D> CompositedTexture;
};
