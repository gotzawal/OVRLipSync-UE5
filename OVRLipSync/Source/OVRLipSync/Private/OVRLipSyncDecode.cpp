/*******************************************************************************
 * Filename    :   OVRLipSyncDecode.cpp
 * Content     :   Runtime LipSync Decode Blueprint Nodes Implementation
 * Created     :   2025
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

// LipSync sequence update frequency (same as editor module)
constexpr auto LipSyncSequenceUpdateFrequency = 100;
constexpr auto LipSyncSequenceDuration = 1.0f / LipSyncSequenceUpdateFrequency;

bool UOVRLipSyncDecode::ValidateWavHeader(const TArray<uint8>& WavData, int32& OutSampleRate,
	int32& OutNumChannels, int32& OutBitsPerSample, int32& OutDataOffset, FString& OutErrorMessage)
{
	// WAV file minimum size check
	if (WavData.Num() < 44)
	{
		OutErrorMessage = FString::Printf(TEXT("WAV data too small: %d bytes (minimum 44 bytes required)"), WavData.Num());
		UE_LOG(LogTemp, Error, TEXT("[OVRLipSyncDecode] %s"), *OutErrorMessage);
		return false;
	}

	const uint8* Data = WavData.GetData();

	// Validate RIFF header
	if (Data[0] != 'R' || Data[1] != 'I' || Data[2] != 'F' || Data[3] != 'F')
	{
		OutErrorMessage = TEXT("Invalid WAV format: RIFF header not found");
		UE_LOG(LogTemp, Error, TEXT("[OVRLipSyncDecode] %s"), *OutErrorMessage);
		return false;
	}

	// Validate WAVE format
	if (Data[8] != 'W' || Data[9] != 'A' || Data[10] != 'V' || Data[11] != 'E')
	{
		OutErrorMessage = TEXT("Invalid WAV format: WAVE identifier not found");
		UE_LOG(LogTemp, Error, TEXT("[OVRLipSyncDecode] %s"), *OutErrorMessage);
		return false;
	}

	// Find fmt chunk
	bool FmtFound = false;
	int32 FmtOffset = 12;

	while (FmtOffset + 8 <= WavData.Num())
	{
		if (Data[FmtOffset] == 'f' && Data[FmtOffset + 1] == 'm' &&
			Data[FmtOffset + 2] == 't' && Data[FmtOffset + 3] == ' ')
		{
			FmtFound = true;
			break;
		}

		// Skip this chunk
		uint32 ChunkSize = *reinterpret_cast<const uint32*>(&Data[FmtOffset + 4]);
		FmtOffset += 8 + ChunkSize;
	}

	if (!FmtFound || FmtOffset + 24 > WavData.Num())
	{
		OutErrorMessage = TEXT("Invalid WAV format: fmt chunk not found");
		UE_LOG(LogTemp, Error, TEXT("[OVRLipSyncDecode] %s"), *OutErrorMessage);
		return false;
	}

	// Parse fmt chunk
	uint16 AudioFormat = *reinterpret_cast<const uint16*>(&Data[FmtOffset + 8]);
	OutNumChannels = *reinterpret_cast<const uint16*>(&Data[FmtOffset + 10]);
	OutSampleRate = *reinterpret_cast<const uint32*>(&Data[FmtOffset + 12]);
	OutBitsPerSample = *reinterpret_cast<const uint16*>(&Data[FmtOffset + 22]);

	UE_LOG(LogTemp, Log, TEXT("[OVRLipSyncDecode] WAV Format - AudioFormat: %d, Channels: %d, SampleRate: %d, BitsPerSample: %d"),
		AudioFormat, OutNumChannels, OutSampleRate, OutBitsPerSample);

	// Validate audio format (1 = PCM)
	if (AudioFormat != 1)
	{
		OutErrorMessage = FString::Printf(TEXT("Unsupported audio format: %d (only PCM format 1 is supported)"), AudioFormat);
		UE_LOG(LogTemp, Error, TEXT("[OVRLipSyncDecode] %s"), *OutErrorMessage);
		return false;
	}

	// Validate channels
	if (OutNumChannels < 1 || OutNumChannels > 2)
	{
		OutErrorMessage = FString::Printf(TEXT("Unsupported channel count: %d (only mono and stereo are supported)"), OutNumChannels);
		UE_LOG(LogTemp, Error, TEXT("[OVRLipSyncDecode] %s"), *OutErrorMessage);
		return false;
	}

	// Validate bits per sample
	if (OutBitsPerSample != 16)
	{
		OutErrorMessage = FString::Printf(TEXT("Unsupported bits per sample: %d (only 16-bit is supported)"), OutBitsPerSample);
		UE_LOG(LogTemp, Error, TEXT("[OVRLipSyncDecode] %s"), *OutErrorMessage);
		return false;
	}

	// Find data chunk
	uint32 FmtChunkSize = *reinterpret_cast<const uint32*>(&Data[FmtOffset + 4]);
	int32 DataOffset = FmtOffset + 8 + FmtChunkSize;
	bool DataFound = false;

	while (DataOffset + 8 <= WavData.Num())
	{
		if (Data[DataOffset] == 'd' && Data[DataOffset + 1] == 'a' &&
			Data[DataOffset + 2] == 't' && Data[DataOffset + 3] == 'a')
		{
			DataFound = true;
			OutDataOffset = DataOffset + 8;
			uint32 DataChunkSize = *reinterpret_cast<const uint32*>(&Data[DataOffset + 4]);
			UE_LOG(LogTemp, Log, TEXT("[OVRLipSyncDecode] Found data chunk at offset %d with size %d bytes"), OutDataOffset, DataChunkSize);
			break;
		}

		// Skip this chunk
		uint32 ChunkSize = *reinterpret_cast<const uint32*>(&Data[DataOffset + 4]);
		DataOffset += 8 + ChunkSize;
	}

	if (!DataFound)
	{
		OutErrorMessage = TEXT("Invalid WAV format: data chunk not found");
		UE_LOG(LogTemp, Error, TEXT("[OVRLipSyncDecode] %s"), *OutErrorMessage);
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("[OVRLipSyncDecode] WAV header validation successful"));
	return true;
}

bool UOVRLipSyncDecode::Base64ToSoundWave(const FString& Base64Data, USoundWave*& OutSoundWave, FString& OutErrorMessage)
{
	UE_LOG(LogTemp, Log, TEXT("[OVRLipSyncDecode] Starting Base64 to SoundWave conversion, input length: %d"), Base64Data.Len());

	// Decode Base64
	TArray<uint8> WavData;
	if (!FBase64::Decode(Base64Data, WavData))
	{
		OutErrorMessage = TEXT("Failed to decode Base64 data");
		UE_LOG(LogTemp, Error, TEXT("[OVRLipSyncDecode] %s"), *OutErrorMessage);
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("[OVRLipSyncDecode] Base64 decoded successfully, WAV data size: %d bytes"), WavData.Num());

	// Validate WAV header and extract format information
	int32 SampleRate, NumChannels, BitsPerSample, DataOffset;
	if (!ValidateWavHeader(WavData, SampleRate, NumChannels, BitsPerSample, DataOffset, OutErrorMessage))
	{
		return false;
	}

	// Create new SoundWave object
	OutSoundWave = NewObject<USoundWave>(GetTransientPackage(), NAME_None, RF_Transient);
	if (!OutSoundWave)
	{
		OutErrorMessage = TEXT("Failed to create SoundWave object");
		UE_LOG(LogTemp, Error, TEXT("[OVRLipSyncDecode] %s"), *OutErrorMessage);
		return false;
	}

	// Extract PCM data
	int32 PCMDataSize = WavData.Num() - DataOffset;
	if (PCMDataSize <= 0)
	{
		OutErrorMessage = FString::Printf(TEXT("Invalid PCM data size: %d"), PCMDataSize);
		UE_LOG(LogTemp, Error, TEXT("[OVRLipSyncDecode] %s"), *OutErrorMessage);
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("[OVRLipSyncDecode] PCM data size: %d bytes"), PCMDataSize);

	// Allocate and copy PCM data
	OutSoundWave->RawPCMDataSize = PCMDataSize;
	OutSoundWave->RawPCMData = static_cast<uint8*>(FMemory::Malloc(PCMDataSize));
	FMemory::Memcpy(OutSoundWave->RawPCMData, &WavData[DataOffset], PCMDataSize);

	// Set SoundWave properties
	OutSoundWave->NumChannels = NumChannels;
	OutSoundWave->SetSampleRate(SampleRate);
	OutSoundWave->Duration = (float)PCMDataSize / (float)(SampleRate * NumChannels * sizeof(int16));
	OutSoundWave->SoundGroup = SOUNDGROUP_Default;

	UE_LOG(LogTemp, Log, TEXT("[OVRLipSyncDecode] SoundWave created successfully - Duration: %.2f seconds, SampleRate: %d, Channels: %d"),
		OutSoundWave->Duration, SampleRate, NumChannels);

	return true;
}

bool UOVRLipSyncDecode::DecompressSoundWaveRuntime(USoundWave* SoundWave, FString& OutErrorMessage)
{
	if (!SoundWave)
	{
		OutErrorMessage = TEXT("SoundWave is null");
		UE_LOG(LogTemp, Error, TEXT("[OVRLipSyncDecode] %s"), *OutErrorMessage);
		return false;
	}

	// Check if PCM data already exists
	if (SoundWave->RawPCMData && SoundWave->RawPCMDataSize > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("[OVRLipSyncDecode] SoundWave already has PCM data (%d bytes)"), SoundWave->RawPCMDataSize);
		return true;
	}

	// At runtime, we cannot decompress compressed audio formats
	// The SoundWave must already have RawPCMData or be created with it
	OutErrorMessage = TEXT("SoundWave does not have RawPCMData. For runtime processing, use Base64ToSoundWave or ensure the SoundWave has PCM data.");
	UE_LOG(LogTemp, Error, TEXT("[OVRLipSyncDecode] %s"), *OutErrorMessage);
	return false;
}

bool UOVRLipSyncDecode::GenerateLipSyncSequenceRuntime(USoundWave* SoundWave, bool UseOfflineModel,
	UOVRLipSyncFrameSequence*& OutSequence, FString& OutErrorMessage)
{
	UE_LOG(LogTemp, Log, TEXT("[OVRLipSyncDecode] Starting LipSync sequence generation (UseOfflineModel: %s)"),
		UseOfflineModel ? TEXT("true") : TEXT("false"));

	if (!SoundWave)
	{
		OutErrorMessage = TEXT("SoundWave is null");
		UE_LOG(LogTemp, Error, TEXT("[OVRLipSyncDecode] %s"), *OutErrorMessage);
		return false;
	}

	// Validate channel count
	if (SoundWave->NumChannels > 2)
	{
		OutErrorMessage = FString::Printf(TEXT("Unsupported channel count: %d (only mono and stereo are supported)"), SoundWave->NumChannels);
		UE_LOG(LogTemp, Error, TEXT("[OVRLipSyncDecode] %s"), *OutErrorMessage);
		return false;
	}

	// Prepare PCM data
	if (!DecompressSoundWaveRuntime(SoundWave, OutErrorMessage))
	{
		return false;
	}

	// Validate PCM data
	if (SoundWave->RawPCMData == nullptr || SoundWave->RawPCMDataSize <= 0)
	{
		OutErrorMessage = TEXT("SoundWave has no valid RawPCMData");
		UE_LOG(LogTemp, Error, TEXT("[OVRLipSyncDecode] %s"), *OutErrorMessage);
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("[OVRLipSyncDecode] Processing SoundWave - Channels: %d, SampleRate: %d, PCMDataSize: %d bytes"),
		SoundWave->NumChannels, SoundWave->GetSampleRateForCurrentPlatform(), SoundWave->RawPCMDataSize);

	// Create output sequence
	OutSequence = NewObject<UOVRLipSyncFrameSequence>(GetTransientPackage(), NAME_None, RF_Transient);
	if (!OutSequence)
	{
		OutErrorMessage = TEXT("Failed to create LipSync sequence object");
		UE_LOG(LogTemp, Error, TEXT("[OVRLipSyncDecode] %s"), *OutErrorMessage);
		return false;
	}

	// Setup processing parameters (same as editor module)
	auto NumChannels = SoundWave->NumChannels;
	auto SampleRate = SoundWave->GetSampleRateForCurrentPlatform();
	auto PCMDataSize = SoundWave->RawPCMDataSize / sizeof(int16);
	auto PCMData = reinterpret_cast<int16*>(SoundWave->RawPCMData);
	auto ChunkSizeSamples = static_cast<int>(SampleRate * LipSyncSequenceDuration);
	auto ChunkSize = NumChannels * ChunkSizeSamples;

	UE_LOG(LogTemp, Log, TEXT("[OVRLipSyncDecode] Processing parameters - ChunkSizeSamples: %d, ChunkSize: %d, TotalSamples: %d"),
		ChunkSizeSamples, ChunkSize, PCMDataSize);

	// Setup LipSync context
	FString ModelPath = UseOfflineModel ? FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("OVRLipSync"),
		TEXT("OfflineModel"), TEXT("ovrlipsync_offline_model.pb")) : FString();

	if (UseOfflineModel)
	{
		UE_LOG(LogTemp, Log, TEXT("[OVRLipSyncDecode] Using offline model: %s"), *ModelPath);
	}

	UOVRLipSyncContextWrapper Context(ovrLipSyncContextProvider_Enhanced, SampleRate, 4096, ModelPath);

	float LaughterScore = 0.0f;
	int32 FrameDelayInMs = 0;
	TArray<float> Visemes;

	// Initialize with silence to get frame delay
	TArray<int16> Samples;
	Samples.SetNumZeroed(ChunkSize);
	Context.ProcessFrame(Samples.GetData(), ChunkSizeSamples, Visemes, LaughterScore, FrameDelayInMs, NumChannels > 1);

	int FrameOffset = static_cast<int>(FrameDelayInMs * SampleRate / 1000 * NumChannels);

	UE_LOG(LogTemp, Log, TEXT("[OVRLipSyncDecode] Frame delay: %d ms, Frame offset: %d samples"), FrameDelayInMs, FrameOffset);

	// Process all audio data
	int32 ProcessedFrames = 0;
	for (int Offset = 0; Offset < PCMDataSize + FrameOffset; Offset += ChunkSize)
	{
		int RemainingSamples = PCMDataSize - Offset;

		if (RemainingSamples >= ChunkSize)
		{
			// Process full chunk
			Context.ProcessFrame(PCMData + Offset, ChunkSizeSamples, Visemes, LaughterScore, FrameDelayInMs, NumChannels > 1);
		}
		else
		{
			// Process partial chunk with padding
			if (RemainingSamples > 0)
			{
				FMemory::Memcpy(Samples.GetData(), PCMData + Offset, sizeof(int16) * RemainingSamples);
				FMemory::Memset(Samples.GetData() + RemainingSamples, 0, sizeof(int16) * (ChunkSize - RemainingSamples));
			}
			else
			{
				FMemory::Memset(Samples.GetData(), 0, sizeof(int16) * ChunkSize);
			}
			Context.ProcessFrame(Samples.GetData(), ChunkSizeSamples, Visemes, LaughterScore, FrameDelayInMs, NumChannels > 1);
		}

		// Add frame to sequence (accounting for frame offset)
		if (Offset >= FrameOffset)
		{
			OutSequence->Add(Visemes, LaughterScore);
			ProcessedFrames++;
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[OVRLipSyncDecode] LipSync sequence generation completed successfully - Total frames: %d"), ProcessedFrames);

	return true;
}
