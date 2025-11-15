/*******************************************************************************
 * Filename    :   OVRLipSyncDecode.h
 * Content     :   Runtime LipSync Decode Blueprint Nodes
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

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "OVRLipSyncDecode.generated.h"

// Forward declarations
class USoundWave;
class UOVRLipSyncFrameSequence;

/**
 * Blueprint function library for runtime OVRLipSync decoding operations
 */
UCLASS()
class OVRLIPSYNC_API UOVRLipSyncDecode : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Converts Base64 encoded WAV data to a USoundWave object at runtime
	 * @param Base64Data - Base64 encoded WAV file data
	 * @param OutSoundWave - Generated SoundWave object
	 * @param OutErrorMessage - Error message if conversion fails
	 * @return true if conversion succeeded, false otherwise
	 */
	UFUNCTION(BlueprintCallable, Category = "OVRLipSync|Decode",
		meta = (DisplayName = "Base64 to SoundWave"))
	static bool Base64ToSoundWave(const FString& Base64Data, USoundWave*& OutSoundWave, FString& OutErrorMessage);

	/**
	 * Generates LipSync sequence from a SoundWave at runtime
	 * @param SoundWave - Input SoundWave to process
	 * @param UseOfflineModel - Whether to use the offline model for higher quality
	 * @param OutSequence - Generated LipSync frame sequence (added to root, call ReleaseLipSyncSequence when done)
	 * @param OutErrorMessage - Error message if generation fails
	 * @return true if generation succeeded, false otherwise
	 */
	UFUNCTION(BlueprintCallable, Category = "OVRLipSync|Decode",
		meta = (DisplayName = "Generate LipSync Sequence Runtime"))
	static bool GenerateLipSyncSequenceRuntime(USoundWave* SoundWave, bool UseOfflineModel,
		UOVRLipSyncFrameSequence*& OutSequence, FString& OutErrorMessage);

	/**
	 * Releases a runtime-generated LipSync sequence from memory
	 * Call this when you're done using a sequence created by GenerateLipSyncSequenceRuntime
	 * @param Sequence - The sequence to release
	 */
	UFUNCTION(BlueprintCallable, Category = "OVRLipSync|Decode",
		meta = (DisplayName = "Release LipSync Sequence"))
	static void ReleaseLipSyncSequence(UOVRLipSyncFrameSequence* Sequence);

private:
	// Helper function to decompress SoundWave and prepare PCM data for runtime use
	static bool DecompressSoundWaveRuntime(USoundWave* SoundWave, FString& OutErrorMessage);

	// Helper function to validate WAV header
	static bool ValidateWavHeader(const TArray<uint8>& WavData, int32& OutSampleRate,
		int32& OutNumChannels, int32& OutBitsPerSample, int32& OutDataOffset, FString& OutErrorMessage);
};
