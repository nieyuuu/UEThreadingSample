#include "TextureProcessing.h"

#include "Math/GuardedInt.h"
#include "AssetCompilingManager.h"

const TCHAR* EFilterTypeToString(EFilterType InFilterType)
{
	const TCHAR* ConvertTable[] = {
		TEXT("BoxFilter"),
		TEXT("GaussianFilter")
	};

	return ConvertTable[int32(InFilterType)];
}

const TCHAR* EConvolutionTypeToString(EConvolutionType InConvolutionType)
{
	const TCHAR* ConvertTable[] = {
		TEXT("2D"),
		TEXT("1D Vertical"),
		TEXT("1D Horizontal")
	};

	return ConvertTable[int32(InConvolutionType)];
}

void ComputeBoxFilterKernel(int32 InFilterSize, EConvolutionType InConvolutionType, TArray<float>& OutWeights, TArray<FIntPoint>& OutOffsets)
{
	const int32 HalfSize = InFilterSize / 2;

	switch (InConvolutionType)
	{
	case EConvolutionType::TwoD:
		for (int x = -HalfSize; x <= HalfSize; ++x)
		{
			for (int y = -HalfSize; y <= HalfSize; ++y)
			{
				OutWeights.Add(1.0f / (InFilterSize * InFilterSize));
				OutOffsets.Add(FIntPoint(x, y));
			}
		}
		break;
	case EConvolutionType::OneDVertical:
	case EConvolutionType::OneDHorizontal:
		for (int i = -HalfSize; i <= HalfSize; ++i)
		{
			OutWeights.Add(1.0f / InFilterSize);
			if (InConvolutionType == EConvolutionType::OneDVertical)
			{
				OutOffsets.Add(FIntPoint(0, i));
			}
			else
			{
				OutOffsets.Add(FIntPoint(i, 0));
			}
		}
		break;
	default:
		check(false);
	}
}

void ComputeGaussianFilterKernel(int32 InFilterSize, EConvolutionType InConvolutionType, TArray<float>& OutWeights, TArray<FIntPoint>& OutOffsets)
{
	const int32 HalfSize = InFilterSize / 2;

	//The 3 sigma rule(from OpenCV for balancing performance and filter quality).
	const float Sigma = 0.3f * ((InFilterSize - 1) * 0.5f - 1.0f) + 0.8f;

	switch (InConvolutionType)
	{
	case EConvolutionType::TwoD:
		for (int x = -HalfSize; x <= HalfSize; ++x)
		{
			for (int y = -HalfSize; y <= HalfSize; ++y)
			{
				const float Factor = 1.0f / (2.0f * PI * Sigma * Sigma);
				const float Exp = FMath::Exp((x * x + y * y) / (2.0f * Sigma * Sigma));

				OutWeights.Add(Factor * Exp);
				OutOffsets.Add(FIntPoint(x, y));
			}
		}
		break;
	case EConvolutionType::OneDVertical:
	case EConvolutionType::OneDHorizontal:
		for (int i = -HalfSize; i <= HalfSize; ++i)
		{
			const float Factor = 1.0f / (FMath::Sqrt(2.0f * PI) * Sigma);
			const float Exp = FMath::Exp((i * i) / (2.0f * Sigma * Sigma));

			OutWeights.Add(Factor * Exp);

			if (InConvolutionType == EConvolutionType::OneDVertical)
			{
				OutOffsets.Add(FIntPoint(0, i));
			}
			else
			{
				OutOffsets.Add(FIntPoint(i, 0));
			}
		}
		break;
	default:
		check(false);
	}

	const float SumWeight = Algo::Accumulate<float>(OutWeights, 0.0f);

	//Normalize weights.
	for (int i = 0; i < OutWeights.Num(); ++i)
	{
		OutWeights[i] = OutWeights[i] / SumWeight;
	}
}

void ComputeFilterKernel(EFilterType InFilterType, int32 InFilterSize, EConvolutionType InConvolutionType, TArray<float>& OutWeights, TArray<FIntPoint>& OutOffsets)
{
	OutWeights.Empty();
	OutOffsets.Empty();

	if (InFilterSize <= 2 || InFilterSize % 2 == 0)
	{
		UE_LOG(LogThreadingSample, Warning, TEXT("Invalid filter size [%d]. filter size should be a positive odd number and should be greater than 2."), InFilterSize);
		return;
	}

	OutWeights.Reserve(InConvolutionType == EConvolutionType::TwoD ? InFilterSize * InFilterSize : InFilterSize);
	OutOffsets.Reserve(InConvolutionType == EConvolutionType::TwoD ? InFilterSize * InFilterSize : InFilterSize);

	switch (InFilterType)
	{
	case EFilterType::BoxFilter:
		ComputeBoxFilterKernel(InFilterSize, InConvolutionType, OutWeights, OutOffsets);
		break;
	case EFilterType::GaussianFilter:
		ComputeGaussianFilterKernel(InFilterSize, InConvolutionType, OutWeights, OutOffsets);
		break;
	default:
		check(false);
	}

	check(OutWeights.Num() == OutOffsets.Num());
	check(OutWeights.Num() % 2 == 1);
}

void FilterTexture(TWeakObjectPtr<UTexture2D> InSourceTexture, TWeakObjectPtr<UTexture2D> OutFilteredTexture, EFilterType InFilterType, int32 InFilterSize, EConvolutionType InConvolutionType, bool InForceSingleThread)
{
	check(InSourceTexture.Get() && OutFilteredTexture.Get());
	check(InSourceTexture->SRGB == OutFilteredTexture->SRGB);

	FTexture2DMipMap* SourceMip = &InSourceTexture->GetPlatformData()->Mips[0];
	FByteBulkData* SourceRawImageData = &SourceMip->BulkData;
	FColor* SourceColorData = static_cast<FColor*>(SourceRawImageData->Lock(LOCK_READ_ONLY));
	check(SourceColorData);

	FTexture2DMipMap* FilteredMip = &OutFilteredTexture->GetPlatformData()->Mips[0];
	FByteBulkData* FilteredRawImageData = &FilteredMip->BulkData;
	FColor* FilteredColorData = static_cast<FColor*>(FilteredRawImageData->Lock(LOCK_READ_WRITE));
	check(FilteredColorData);

	check(SourceMip->SizeX == FilteredMip->SizeX && SourceMip->SizeY == FilteredMip->SizeY);

	const uint16 TextureWidth = SourceMip->SizeX;
	const uint16 TextureHeight = SourceMip->SizeY;

	const bool IsSRGB = InSourceTexture->SRGB;

	TArray<float> Weights;
	TArray<FIntPoint> Offsets;

	ComputeFilterKernel(InFilterType, InFilterSize, InConvolutionType, Weights, Offsets);

	if (Weights.Num() == 0 || Offsets.Num() == 0)
	{
		UE_LOG(LogThreadingSample, Warning, TEXT("Empty filter weights or offsets."));
		return;
	}

	auto LoopBody = [=](int32 Index) {
		const int32 CurrentPixelPosX = Index % TextureWidth;
		const int32 CurrentPixelPosY = Index / TextureWidth;

		float WeightedLinearSumR = 0.0f, WeightedLinearSumG = 0.0f, WeightedLinearSumB = 0.0f;

		for (int i = 0; i < Weights.Num(); ++i)
		{
			FIntPoint SamplePosition;

			SamplePosition.X = FMath::Clamp(CurrentPixelPosX + Offsets[i].X, 0, TextureWidth - 1);
			SamplePosition.Y = FMath::Clamp(CurrentPixelPosY + Offsets[i].Y, 0, TextureHeight - 1);

			const FColor SampledColor = SourceColorData[SamplePosition.Y * TextureWidth + SamplePosition.X];

			if (IsSRGB)
			{
				//Convert to linear space.
				FLinearColor LinearColor = FLinearColor(SampledColor);
				WeightedLinearSumR += LinearColor.R * Weights[i];
				WeightedLinearSumG += LinearColor.G * Weights[i];
				WeightedLinearSumB += LinearColor.B * Weights[i];
			}
			else
			{
				//Already in linear space?
				WeightedLinearSumR += SampledColor.R / 255.0f * Weights[i];
				WeightedLinearSumG += SampledColor.G / 255.0f * Weights[i];
				WeightedLinearSumB += SampledColor.B / 255.0f * Weights[i];
			}
		}

		const FColor Result = FLinearColor(WeightedLinearSumR, WeightedLinearSumG, WeightedLinearSumB).ToFColor(IsSRGB);

		FilteredColorData[Index] = FColor(Result.R, Result.G, Result.B, SourceColorData[Index].A);
		};

	const double StartTime = FPlatformTime::Seconds();

	//ParallelFor will return until all loop bodies finish execution.
	ParallelFor(
		TEXT("Parallel Texture Filter"),
		TextureWidth * TextureHeight,
		8192,
		LoopBody,
		InForceSingleThread ? EParallelForFlags::ForceSingleThread : EParallelForFlags::None);

	const double EndTime = FPlatformTime::Seconds();

	UE_LOG(LogThreadingSample, Display, TEXT("%s(%s, %s, Texture Size: %dx%d, Filter Size: %d) Execution Finished in %f Seconds."),
		EFilterTypeToString(InFilterType),
		InForceSingleThread ? TEXT("Singlethreaded") : TEXT("Multithreaded"),
		EConvolutionTypeToString(InConvolutionType),
		TextureWidth, TextureHeight, InFilterSize,
		EndTime - StartTime);

	FilteredRawImageData->Unlock();
	SourceRawImageData->Unlock();
}

void ScaleAlphaChannel(TWeakObjectPtr<UTexture2D> InSourceTexture, TWeakObjectPtr<UTexture2D> OutScaledTexture, float InScaleValue, bool InForceSingleThread)
{
	check(InSourceTexture.Get() && OutScaledTexture.Get());

	FTexture2DMipMap* SourceMip = &InSourceTexture->GetPlatformData()->Mips[0];
	FByteBulkData* SourceRawImageData = &SourceMip->BulkData;
	FColor* SourceColorData = static_cast<FColor*>(SourceRawImageData->Lock(LOCK_READ_ONLY));
	check(SourceColorData);

	FTexture2DMipMap* ScaledMip = &OutScaledTexture->GetPlatformData()->Mips[0];
	FByteBulkData* ScaledRawImageData = &ScaledMip->BulkData;
	FColor* ScaledColorData = static_cast<FColor*>(ScaledRawImageData->Lock(LOCK_READ_WRITE));
	check(ScaledColorData);

	check(SourceMip->SizeX == ScaledMip->SizeX && SourceMip->SizeY == ScaledMip->SizeY);

	const uint16 TextureWidth = SourceMip->SizeX;
	const uint16 TextureHeight = SourceMip->SizeY;

	const double StartTime = FPlatformTime::Seconds();

	//ParallelFor will return until all loop bodies finish execution, so the caller will be blocked.
	ParallelFor(
		TEXT("Parallel Scale Alpha Channel"),
		TextureWidth * TextureHeight,
		8192,
		[&](int32 Index) {
			ScaledColorData[Index].R = SourceColorData[Index].R;
			ScaledColorData[Index].G = SourceColorData[Index].G;
			ScaledColorData[Index].B = SourceColorData[Index].B;
			ScaledColorData[Index].A = SourceColorData[Index].A * FMath::Clamp(InScaleValue, 0.0f, 1.0f);
		},
		InForceSingleThread ? EParallelForFlags::ForceSingleThread : EParallelForFlags::None);

	const double EndTime = FPlatformTime::Seconds();

	UE_LOG(LogThreadingSample, Display, TEXT("Scale Alpha Channel(%s, Texture Size: %dx%d, Scale Value: %f) Execution Finished in %f Seconds."),
		InForceSingleThread ? TEXT("Singlethreaded") : TEXT("Multithreaded"),
		TextureWidth, TextureHeight, InScaleValue,
		EndTime - StartTime);

	ScaledRawImageData->Unlock();
	SourceRawImageData->Unlock();
}

void CompositeRGBAValue(TWeakObjectPtr<UTexture2D> InRGBTexture, TWeakObjectPtr<UTexture2D> InATexture, TWeakObjectPtr<UTexture2D> OutTexture, bool InForceSingleThread)
{
	check(InRGBTexture.Get() && InATexture.Get() && OutTexture.Get());

	FTexture2DMipMap* RGBMip = &InRGBTexture->GetPlatformData()->Mips[0];
	FByteBulkData* RGBRawImageData = &RGBMip->BulkData;
	FColor* RGBColorData = static_cast<FColor*>(RGBRawImageData->Lock(LOCK_READ_ONLY));
	check(RGBColorData);

	FTexture2DMipMap* AlphaMip = &InATexture->GetPlatformData()->Mips[0];
	FByteBulkData* AlphaRawImageData = &AlphaMip->BulkData;
	FColor* AlphaColorData = static_cast<FColor*>(AlphaRawImageData->Lock(LOCK_READ_ONLY));
	check(AlphaColorData);

	FTexture2DMipMap* Result = &OutTexture->GetPlatformData()->Mips[0];
	FByteBulkData* ResultRawImageData = &Result->BulkData;
	FColor* ResultColorData = static_cast<FColor*>(ResultRawImageData->Lock(LOCK_READ_WRITE));
	check(ResultColorData);

	check(RGBMip->SizeX == AlphaMip->SizeX && RGBMip->SizeX == Result->SizeX);
	check(RGBMip->SizeY == AlphaMip->SizeY && RGBMip->SizeY == Result->SizeY);

	const uint16 TextureWidth = RGBMip->SizeX;
	const uint16 TextureHeight = RGBMip->SizeY;

	const double StartTime = FPlatformTime::Seconds();

	//ParallelFor will return until all loop bodies finish execution, so the caller will be blocked.
	ParallelFor(
		TEXT("Parallel Composite RGBA Value"),
		TextureWidth * TextureHeight,
		8192,
		[&](int32 Index) {
			ResultColorData[Index].R = RGBColorData[Index].R;
			ResultColorData[Index].G = RGBColorData[Index].G;
			ResultColorData[Index].B = RGBColorData[Index].B;
			ResultColorData[Index].A = AlphaColorData[Index].A;
		},
		InForceSingleThread ? EParallelForFlags::ForceSingleThread : EParallelForFlags::None);

	const double EndTime = FPlatformTime::Seconds();

	UE_LOG(LogThreadingSample, Display, TEXT("Composite RGBA Value(%s, Texture Size: %dx%d) Execution Finished in %f Seconds."),
		InForceSingleThread ? TEXT("Singlethreaded") : TEXT("Multithreaded"),
		TextureWidth, TextureHeight,
		EndTime - StartTime);

	ResultRawImageData->Unlock();
	AlphaRawImageData->Unlock();
	RGBRawImageData->Unlock();
}

bool ValidateParameters(UTexture2D* InSourceTexture, int InFilterSize, float InScaleValue)
{
#if !TASKGRAPH_NEW_FRONTEND
	UE_LOG(LogThreadingSample, Warning, TEXT("Task graph use new frontend?"));
	return false;
#endif

	if (!IsValid(InSourceTexture))
	{
		UE_LOG(LogThreadingSample, Warning, TEXT("Invalid source texture!!!"));
		return false;
	}

	if (InFilterSize <= 2 || InFilterSize >= 128 || InFilterSize % 2 == 0)
	{
		UE_LOG(LogThreadingSample, Warning, TEXT("Invalid filter size:[%d]. Valid filter size range [3, 127] and has to be an odd number."), InFilterSize);
		return false;
	}

	if (InScaleValue < 0.0f || InScaleValue > 1.0f)
	{
		UE_LOG(LogThreadingSample, Warning, TEXT("Invalid scale value:[%f]. Valid scale value range [0.0, 1.0]."), InScaleValue);
		return false;
	}

	if (InSourceTexture->CompressionSettings != TextureCompressionSettings::TC_VectorDisplacementmap)
	{
		UE_LOG(LogThreadingSample, Warning, TEXT("Currently only support texture with compression setting [VectorDisplacementmap (RGBA8)]."));
		return false;
	}

#if WITH_EDITORONLY_DATA
	if (InSourceTexture->MipGenSettings != TextureMipGenSettings::TMGS_NoMipmaps)
	{
		UE_LOG(LogThreadingSample, Warning, TEXT("Currently only support texture with mipmap generation setting [TMGS_NoMipmaps]."));
		return false;
	}
#endif

	FTexture2DMipMap* SourceMip = &InSourceTexture->GetPlatformData()->Mips[0];
	const uint16 TextureWidth = SourceMip->SizeX;
	const uint16 TextureHeight = SourceMip->SizeY;

	if (TextureWidth < 256 || TextureHeight < 256)
	{
		UE_LOG(LogThreadingSample, Warning, TEXT("Source texture size is too small!!!"));
		return false;
	}

	return true;
}

UTexture2D* CreateTransientTextureFromSource(UTexture2D* InSourceTexture, const FString& InTextureName, bool InCopySourceImage)
{
	check(InSourceTexture);

	FTexture2DMipMap* SourceMip = &InSourceTexture->GetPlatformData()->Mips[0];

	const uint16 TextureWidth = SourceMip->SizeX, TextureHeight = SourceMip->SizeY;
	const EPixelFormat PixelFormat = InSourceTexture->GetPixelFormat();

	TArrayView64<uint8> PixelsView;
	FByteBulkData* SourceRawImageData = nullptr;

	if (InCopySourceImage)
	{
		const int32 NumBlocksX = TextureWidth / GPixelFormats[PixelFormat].BlockSizeX;
		const int32 NumBlocksY = TextureHeight / GPixelFormats[PixelFormat].BlockSizeY;
		FGuardedInt64 BytesForImageGuarded = FGuardedInt64(NumBlocksX) * NumBlocksY * GPixelFormats[PixelFormat].BlockBytes;
		int64 BytesForImage = BytesForImageGuarded.Get(0);
		check(BytesForImage > 0);

		SourceRawImageData = &SourceMip->BulkData;
		FColor* SourceColorData = static_cast<FColor*>(SourceRawImageData->Lock(LOCK_READ_ONLY));
		check(SourceColorData);

		PixelsView = TArrayView64<uint8>((uint8*)SourceColorData, BytesForImage);
	}

	UTexture2D* OutCreatedResult = UTexture2D::CreateTransient(TextureWidth, TextureHeight, PixelFormat, *InTextureName, PixelsView);

	if (InSourceTexture->SRGB != OutCreatedResult->SRGB)
	{
		OutCreatedResult->SRGB = InSourceTexture->SRGB;
	}

	if (InSourceTexture->CompressionSettings != OutCreatedResult->CompressionSettings)
	{
		OutCreatedResult->CompressionSettings = InSourceTexture->CompressionSettings;
	}

#if WITH_EDITORONLY_DATA
	if (InSourceTexture->MipGenSettings != OutCreatedResult->MipGenSettings)
	{
		OutCreatedResult->MipGenSettings = InSourceTexture->MipGenSettings;
	}
#endif

	if (InCopySourceImage)
	{
		SourceRawImageData->Unlock();
	}

	//Add to root set incase it gets GCed.
	//TODO: RemoveFromRoot
	OutCreatedResult->AddToRoot();
	//OutCreatedResult->RemoveFromRoot();

	FAssetCompilingManager::Get().ProcessAsyncTasks();
	FAssetCompilingManager::Get().FinishAllCompilation();

	return OutCreatedResult;
}
