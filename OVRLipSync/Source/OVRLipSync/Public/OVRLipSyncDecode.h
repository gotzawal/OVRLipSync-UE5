/*******************************************************************************
 * Filename    :   OVRLipSyncDecode.h
 * Content     :   Blueprint nodes for runtime LipSync decoding
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

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Sound/SoundWave.h"
#include "OVRLipSyncFrame.h"
#include "OVRLipSyncDecode.generated.h"

UCLASS()
class OVRLIPSYNC_API UOVRLipSyncDecode : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Converts a Base64 encoded string to a USoundWave object
	 * @param Base64String - The base64 encoded audio data
	 * @param SampleRate - Sample rate of the audio (default: 44100)
	 * @param NumChannels - Number of audio channels (default: 1 for mono)
	 * @return A new USoundWave object or nullptr if conversion fails
	 */
	UFUNCTION(BlueprintCallable, Category = "OVRLipSync")
	static USoundWave* Base64ToSoundWave(const FString& Base64String, int32 SampleRate = 44100, int32 NumChannels = 1);

	/**
	 * Generates a LipSync sequence from a SoundWave at runtime
	 * @param SoundWave - The sound wave to process
	 * @param UseOfflineModel - Whether to use the offline model for processing (default: false)
	 * @return A new UOVRLipSyncFrameSequence object or nullptr if generation fails
	 */
	UFUNCTION(BlueprintCallable, Category = "OVRLipSync")
	static UOVRLipSyncFrameSequence* GenerateLipSyncSequenceRuntime(USoundWave* SoundWave, bool UseOfflineModel = false);

private:
	/**
	 * Decompresses SoundWave object by initializing RawPCM data
	 * @param SoundWave - The sound wave to decompress
	 * @return true if decompression was successful, false otherwise
	 */
	static bool DecompressSoundWave(USoundWave* SoundWave);
};
