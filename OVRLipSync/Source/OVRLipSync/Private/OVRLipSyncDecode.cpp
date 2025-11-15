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

TArray<int16> UOVRLipSyncDecode::ResampleAudio(const int16* SourceData, int32 SourceSamples, int32 SourceSampleRate, int32 TargetSampleRate, int32 NumChannels)
{
	TArray<int16> ResampledData;

	if (SourceSampleRate == TargetSampleRate)
	{
		// No resampling needed
		ResampledData.SetNum(SourceSamples);
		FMemory::Memcpy(ResampledData.GetData(), SourceData, SourceSamples * sizeof(int16));
		return ResampledData;
	}

	// Calculate target number of samples
	float ResampleRatio = (float)TargetSampleRate / (float)SourceSampleRate;
	int32 SourceFrames = SourceSamples / NumChannels;
	int32 TargetFrames = FMath::CeilToInt(SourceFrames * ResampleRatio);
	int32 TargetSamples = TargetFrames * NumChannels;

	ResampledData.SetNum(TargetSamples);

	// Simple linear interpolation resampling
	for (int32 TargetFrame = 0; TargetFrame < TargetFrames; ++TargetFrame)
	{
		float SourceFrameFloat = TargetFrame / ResampleRatio;
		int32 SourceFrame0 = FMath::FloorToInt(SourceFrameFloat);
		int32 SourceFrame1 = FMath::Min(SourceFrame0 + 1, SourceFrames - 1);
		float Fraction = SourceFrameFloat - SourceFrame0;

		for (int32 Channel = 0; Channel < NumChannels; ++Channel)
		{
			int32 SourceIdx0 = SourceFrame0 * NumChannels + Channel;
			int32 SourceIdx1 = SourceFrame1 * NumChannels + Channel;
			int32 TargetIdx = TargetFrame * NumChannels + Channel;

			// Linear interpolation
			float Sample0 = SourceData[SourceIdx0];
			float Sample1 = SourceData[SourceIdx1];
			float InterpolatedSample = Sample0 + (Sample1 - Sample0) * Fraction;

			ResampledData[TargetIdx] = FMath::Clamp(FMath::RoundToInt(InterpolatedSample), -32768, 32767);
		}
	}

	return ResampledData;
}

USoundWave* UOVRLipSyncDecode::Base64ToSoundWave(const FString& Base64String, int32 TargetSampleRate, int32 TargetBitrate)
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

	// Parse WAV file header
	if (DecodedData.Num() < 44)
	{
		UE_LOG(LogTemp, Error, TEXT("Base64ToSoundWave: Data too small to be a valid WAV file (minimum 44 bytes required)"));
		return nullptr;
	}

	// Check RIFF header
	if (DecodedData[0] != 'R' || DecodedData[1] != 'I' || DecodedData[2] != 'F' || DecodedData[3] != 'F')
	{
		UE_LOG(LogTemp, Error, TEXT("Base64ToSoundWave: Invalid WAV file - missing RIFF header"));
		return nullptr;
	}

	// Check WAVE format
	if (DecodedData[8] != 'W' || DecodedData[9] != 'A' || DecodedData[10] != 'V' || DecodedData[11] != 'E')
	{
		UE_LOG(LogTemp, Error, TEXT("Base64ToSoundWave: Invalid WAV file - missing WAVE format"));
		return nullptr;
	}

	// Parse fmt chunk
	int32 FmtChunkPos = 12;
	while (FmtChunkPos < DecodedData.Num() - 8)
	{
		if (DecodedData[FmtChunkPos] == 'f' && DecodedData[FmtChunkPos + 1] == 'm' &&
			DecodedData[FmtChunkPos + 2] == 't' && DecodedData[FmtChunkPos + 3] == ' ')
		{
			break;
		}
		FmtChunkPos++;
	}

	if (FmtChunkPos >= DecodedData.Num() - 8)
	{
		UE_LOG(LogTemp, Error, TEXT("Base64ToSoundWave: Invalid WAV file - fmt chunk not found"));
		return nullptr;
	}

	// Read fmt chunk data
	uint16 AudioFormat = *reinterpret_cast<uint16*>(&DecodedData[FmtChunkPos + 8]);
	uint16 WavNumChannels = *reinterpret_cast<uint16*>(&DecodedData[FmtChunkPos + 10]);
	uint32 WavSampleRate = *reinterpret_cast<uint32*>(&DecodedData[FmtChunkPos + 12]);
	uint16 BitsPerSample = *reinterpret_cast<uint16*>(&DecodedData[FmtChunkPos + 22]);

	// Validate audio format (1 = PCM)
	if (AudioFormat != 1)
	{
		UE_LOG(LogTemp, Error, TEXT("Base64ToSoundWave: Only PCM format is supported (format code: %d)"), AudioFormat);
		return nullptr;
	}

	// Validate bits per sample (support 8, 16, 24, 32 bit)
	if (BitsPerSample != 8 && BitsPerSample != 16 && BitsPerSample != 24 && BitsPerSample != 32)
	{
		UE_LOG(LogTemp, Error, TEXT("Base64ToSoundWave: Unsupported bits per sample: %d (supported: 8, 16, 24, 32)"), BitsPerSample);
		return nullptr;
	}

	// Find data chunk
	int32 DataChunkPos = 12;
	while (DataChunkPos < DecodedData.Num() - 8)
	{
		if (DecodedData[DataChunkPos] == 'd' && DecodedData[DataChunkPos + 1] == 'a' &&
			DecodedData[DataChunkPos + 2] == 't' && DecodedData[DataChunkPos + 3] == 'a')
		{
			break;
		}
		DataChunkPos++;
	}

	if (DataChunkPos >= DecodedData.Num() - 8)
	{
		UE_LOG(LogTemp, Error, TEXT("Base64ToSoundWave: Invalid WAV file - data chunk not found"));
		return nullptr;
	}

	// Read data chunk size
	uint32 DataSize = *reinterpret_cast<uint32*>(&DecodedData[DataChunkPos + 4]);
	int32 DataStart = DataChunkPos + 8;

	if (DataStart + DataSize > (uint32)DecodedData.Num())
	{
		UE_LOG(LogTemp, Error, TEXT("Base64ToSoundWave: Invalid WAV file - data size exceeds file size"));
		return nullptr;
	}

	// Calculate original bitrate
	int32 OriginalBitrate = WavSampleRate * BitsPerSample * WavNumChannels;

	UE_LOG(LogTemp, Log, TEXT("Base64ToSoundWave: WAV parsed - SampleRate: %d, Channels: %d, BitsPerSample: %d, Bitrate: %d bps, DataSize: %d bytes"),
		WavSampleRate, WavNumChannels, BitsPerSample, OriginalBitrate, DataSize);

	// Determine final sample rate to use
	int32 FinalSampleRate = WavSampleRate;

	// If TargetBitrate is specified, calculate the required sample rate
	if (TargetBitrate > 0)
	{
		// bitrate = sample_rate × 16 (output is always 16-bit) × channels
		FinalSampleRate = TargetBitrate / (16 * WavNumChannels);
		UE_LOG(LogTemp, Log, TEXT("Base64ToSoundWave: Target bitrate %d bps specified, calculated sample rate: %d Hz"), TargetBitrate, FinalSampleRate);
	}
	// If TargetSampleRate is specified, use it (overrides bitrate)
	else if (TargetSampleRate > 0)
	{
		FinalSampleRate = TargetSampleRate;
		int32 FinalBitrate = FinalSampleRate * 16 * WavNumChannels;
		UE_LOG(LogTemp, Log, TEXT("Base64ToSoundWave: Target sample rate %d Hz specified, final bitrate: %d bps"), FinalSampleRate, FinalBitrate);
	}

	// Create a new SoundWave object
	USoundWave* SoundWave = NewObject<USoundWave>(GetTransientPackage(), NAME_None, RF_Public);
	if (!SoundWave)
	{
		UE_LOG(LogTemp, Error, TEXT("Base64ToSoundWave: Failed to create SoundWave object"));
		return nullptr;
	}

	// Calculate number of samples
	int32 BytesPerSample = BitsPerSample / 8;
	int32 NumSamples = DataSize / BytesPerSample;

	// Unreal Engine uses 16-bit PCM, so convert if necessary
	int32 PCMDataSize16bit = NumSamples * sizeof(int16_t);
	int16_t* PCMData16bit = static_cast<int16_t*>(FMemory::Malloc(PCMDataSize16bit));

	// Convert to 16-bit PCM based on source bit depth
	if (BitsPerSample == 16)
	{
		// Already 16-bit, just copy
		FMemory::Memcpy(PCMData16bit, &DecodedData[DataStart], DataSize);
	}
	else if (BitsPerSample == 8)
	{
		// Convert 8-bit unsigned to 16-bit signed
		uint8* SourceData = &DecodedData[DataStart];
		for (int32 i = 0; i < NumSamples; ++i)
		{
			// 8-bit is unsigned (0-255), convert to signed 16-bit (-32768 to 32767)
			PCMData16bit[i] = (static_cast<int16_t>(SourceData[i]) - 128) * 256;
		}
	}
	else if (BitsPerSample == 24)
	{
		// Convert 24-bit signed to 16-bit signed
		uint8* SourceData = &DecodedData[DataStart];
		for (int32 i = 0; i < NumSamples; ++i)
		{
			// Read 24-bit sample (little-endian)
			int32 Sample24 = (SourceData[i * 3 + 2] << 16) | (SourceData[i * 3 + 1] << 8) | SourceData[i * 3];
			// Sign extend from 24-bit to 32-bit
			if (Sample24 & 0x800000)
				Sample24 |= 0xFF000000;
			// Convert to 16-bit by taking the upper 16 bits
			PCMData16bit[i] = static_cast<int16_t>(Sample24 >> 8);
		}
	}
	else if (BitsPerSample == 32)
	{
		// Convert 32-bit signed to 16-bit signed
		int32* SourceData = reinterpret_cast<int32*>(&DecodedData[DataStart]);
		for (int32 i = 0; i < NumSamples; ++i)
		{
			// Convert to 16-bit by taking the upper 16 bits
			PCMData16bit[i] = static_cast<int16_t>(SourceData[i] >> 16);
		}
	}

	// Perform resampling if needed
	int16_t* FinalPCMData = PCMData16bit;
	int32 FinalPCMDataSize = PCMDataSize16bit;
	int32 FinalNumSamples = NumSamples;

	if (FinalSampleRate != WavSampleRate)
	{
		UE_LOG(LogTemp, Log, TEXT("Base64ToSoundWave: Resampling from %d Hz to %d Hz"), WavSampleRate, FinalSampleRate);

		TArray<int16> ResampledData = ResampleAudio(PCMData16bit, NumSamples, WavSampleRate, FinalSampleRate, WavNumChannels);

		// Free the original PCM data
		FMemory::Free(PCMData16bit);

		// Allocate new memory for resampled data
		FinalNumSamples = ResampledData.Num();
		FinalPCMDataSize = FinalNumSamples * sizeof(int16);
		FinalPCMData = static_cast<int16_t*>(FMemory::Malloc(FinalPCMDataSize));
		FMemory::Memcpy(FinalPCMData, ResampledData.GetData(), FinalPCMDataSize);
	}

	// Set up the sound wave properties
	SoundWave->SetSampleRate(FinalSampleRate);
	SoundWave->NumChannels = WavNumChannels;
	SoundWave->Duration = (float)FinalNumSamples / (WavNumChannels * FinalSampleRate);
	SoundWave->RawPCMDataSize = FinalPCMDataSize;
	SoundWave->SoundGroup = SOUNDGROUP_Default;

	// Assign the final PCM data
	SoundWave->RawPCMData = reinterpret_cast<uint8*>(FinalPCMData);

	int32 FinalBitrate = FinalSampleRate * 16 * WavNumChannels;
	UE_LOG(LogTemp, Log, TEXT("Base64ToSoundWave: Final output - SampleRate: %d Hz, Channels: %d, Bitrate: %d bps"),
		FinalSampleRate, WavNumChannels, FinalBitrate);

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
