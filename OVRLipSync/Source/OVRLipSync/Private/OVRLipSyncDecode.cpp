/*******************************************************************************
 * Filename    :   OVRLipSyncDecode.cpp
 * Content     :   Blueprint nodes for runtime LipSync decoding implementation
 * Created     :   Nov 15th, 2024
 * Copyright   :   Copyright Facebook Technologies, LLC and its affiliates.
 *                 All rights reserved.
 *
 * Licensed under the Oculus Audio SDK License Version 3.3 (the "License");
 * you may not use the Oculus Audio SDK except in compliance with the License,
 * which is provided at the time of installation or download, or which
 * otherwise accompanies this software in either electronic or hard copy form.

 * You may obtain a copy of the License at
 *
 * https://developer.oculus.com/licenses/audio-3.3/
 *
 * Unless required by applicable law or agreed to in writing, the Oculus Audio SDK
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 ******************************************************************************/

#include "OVRLipSyncDecode.h"
#include "OVRLipSyncContextWrapper.h"
#include "Misc/Base64.h"
#include "Sound/SoundWave.h"

namespace
{
// Compute LipSync sequence frames at 100 times a second rate
constexpr auto LipSyncSequenceUpateFrequency = 100;
constexpr auto LipSyncSequenceDuration = 1.0f / LipSyncSequenceUpateFrequency;
}

USoundWave* UOVRLipSyncDecode::Base64ToSoundWave(const FString& Base64String, int32 SampleRate, int32 NumChannels)
{
	if (Base64String.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Base64ToSoundWave: Empty Base64 string provided"));
		return nullptr;
	}

	// Decode Base64 string to binary data
	TArray<uint8> DecodedData;
	if (!FBase64::Decode(Base64String, DecodedData))
	{
		UE_LOG(LogTemp, Error, TEXT("Base64ToSoundWave: Failed to decode Base64 string"));
		return nullptr;
	}

	if (DecodedData.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("Base64ToSoundWave: Decoded data is empty"));
		return nullptr;
	}

	// Create a new SoundWave object
	USoundWave* SoundWave = NewObject<USoundWave>(GetTransientPackage(), NAME_None, RF_Public);
	if (!SoundWave)
	{
		UE_LOG(LogTemp, Error, TEXT("Base64ToSoundWave: Failed to create SoundWave object"));
		return nullptr;
	}

	// Set up the sound wave properties
	SoundWave->SetSampleRate(SampleRate);
	SoundWave->NumChannels = NumChannels;
	SoundWave->Duration = (float)DecodedData.Num() / (sizeof(int16_t) * NumChannels * SampleRate);
	SoundWave->RawPCMDataSize = DecodedData.Num();
	SoundWave->SoundGroup = SOUNDGROUP_Default;

	// Allocate and copy PCM data
	SoundWave->RawPCMData = static_cast<uint8*>(FMemory::Malloc(DecodedData.Num()));
	FMemory::Memcpy(SoundWave->RawPCMData, DecodedData.GetData(), DecodedData.Num());

	return SoundWave;
}

bool UOVRLipSyncDecode::DecompressSoundWave(USoundWave* SoundWave)
{
	if (!SoundWave)
		return false;

	// Already have PCM
	if (SoundWave->RawPCMData && SoundWave->RawPCMDataSize > 0)
		return true;

	// At runtime, we need to decompress the sound wave manually
	// This is a simplified version - in production, you may need to handle compressed formats
	UE_LOG(LogTemp, Warning, TEXT("DecompressSoundWave: SoundWave does not have RawPCMData. Runtime decompression may be limited."));

	// If RawPCMData is not available, the soundwave needs to be pre-processed or
	// imported with uncompressed format
	return false;
}

UOVRLipSyncFrameSequence* UOVRLipSyncDecode::GenerateLipSyncSequenceRuntime(USoundWave* SoundWave, bool UseOfflineModel)
{
	if (!SoundWave)
	{
		UE_LOG(LogTemp, Error, TEXT("GenerateLipSyncSequenceRuntime: Invalid SoundWave"));
		return nullptr;
	}

	if (SoundWave->NumChannels > 2)
	{
		UE_LOG(LogTemp, Error, TEXT("GenerateLipSyncSequenceRuntime: Only mono and stereo streams are supported"));
		return nullptr;
	}

	// Attempt to decompress/init PCM data
	if (!DecompressSoundWave(SoundWave))
	{
		UE_LOG(LogTemp, Error, TEXT("GenerateLipSyncSequenceRuntime: Failed to decompress SoundWave"));
		return nullptr;
	}

	// Defensive checks: make sure RawPCMData is present and sized
	if (SoundWave->RawPCMData == nullptr || SoundWave->RawPCMDataSize <= 0)
	{
		UE_LOG(LogTemp, Error, TEXT("GenerateLipSyncSequenceRuntime: SoundWave has no RawPCMData after decompression"));
		return nullptr;
	}

	// Create the sequence object
	UOVRLipSyncFrameSequence* Sequence = NewObject<UOVRLipSyncFrameSequence>(GetTransientPackage(), NAME_None, RF_Public);
	if (!Sequence)
	{
		UE_LOG(LogTemp, Error, TEXT("GenerateLipSyncSequenceRuntime: Failed to create sequence object"));
		return nullptr;
	}

	auto NumChannels = SoundWave->NumChannels;
	auto SampleRate = SoundWave->GetSampleRateForCurrentPlatform();
	auto PCMDataSize = SoundWave->RawPCMDataSize / sizeof(int16_t);
	auto PCMData = reinterpret_cast<int16_t*>(SoundWave->RawPCMData);
	auto ChunkSizeSamples = static_cast<int>(SampleRate * LipSyncSequenceDuration);
	auto ChunkSize = NumChannels * ChunkSizeSamples;

	FString ModelPath = UseOfflineModel ? FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("OVRLipSync"),
														  TEXT("OfflineModel"), TEXT("ovrlipsync_offline_model.pb"))
										: FString();
	UOVRLipSyncContextWrapper context(ovrLipSyncContextProvider_Enhanced, SampleRate, 4096, ModelPath);

	float LaughterScore = 0.0f;
	int32_t FrameDelayInMs = 0;
	TArray<float> Visemes;

	TArray<int16_t> samples;
	samples.SetNumZeroed(ChunkSize);
	context.ProcessFrame(samples.GetData(), ChunkSizeSamples, Visemes, LaughterScore, FrameDelayInMs, NumChannels > 1);

	int FrameOffset = (int)(FrameDelayInMs * SampleRate / 1000 * NumChannels);

	for (int offs = 0; offs < PCMDataSize + FrameOffset; offs += ChunkSize)
	{
		int remainingSamples = PCMDataSize - offs;
		if (remainingSamples >= ChunkSize)
		{
			context.ProcessFrame(PCMData + offs, ChunkSizeSamples, Visemes, LaughterScore, FrameDelayInMs,
								 NumChannels > 1);
		}
		else
		{
			if (remainingSamples > 0)
			{
				FMemory::Memcpy(samples.GetData(), PCMData + offs, sizeof(int16_t) * remainingSamples);
				FMemory::Memset(samples.GetData() + remainingSamples, 0, sizeof(int16_t) * (ChunkSize - remainingSamples));
			}
			else
			{
				FMemory::Memset(samples.GetData(), 0, sizeof(int16_t) * ChunkSize);
			}
			context.ProcessFrame(samples.GetData(), ChunkSizeSamples, Visemes, LaughterScore, FrameDelayInMs,
								 NumChannels > 1);
		}

		if (offs >= FrameOffset)
		{
			Sequence->Add(Visemes, LaughterScore);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("GenerateLipSyncSequenceRuntime: Successfully generated sequence with %d frames"), Sequence->Num());
	return Sequence;
}
