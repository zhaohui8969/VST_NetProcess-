#include "PluginProcessor.h"
#include "AudioFile.h"
#include "httplib.h"
#include "AudioWork.h"
using namespace std::chrono;

long long func_get_timestamp() {
	return (duration_cast<milliseconds>(system_clock::now().time_since_epoch())).count();
}

// ÖØ²ÉÑù
int func_audio_resample(FUNC_SRC_SIMPLE dllFuncSrcSimple, float* fInBuffer, float* fOutBuffer, double src_ratio, long lInSize, long lOutSize) {
	SRC_DATA data;
	data.src_ratio = src_ratio;
	data.input_frames = lInSize;
	data.output_frames = lOutSize;
	data.data_in = fInBuffer;
	data.data_out = fOutBuffer;
	int error = dllFuncSrcSimple(&data, SRC_SINC_FASTEST, 1);
	return error;
}

// ÓÃÓÚ¼ÆËãÒ»¸ö¶ÁÐ´»º´æÀïµÄÓÐÐ§Êý¾Ý´óÐ¡
long func_cacl_read_write_buffer_data_size(long lBufferSize, long lReadPos, long lWritePos) {
	long inputBufferSize;
	if (lReadPos == lWritePos) {
		return 0;
	}
	if (lReadPos < lWritePos) {
		inputBufferSize = lWritePos - lReadPos;
	}
	else {
		inputBufferSize = lWritePos + lBufferSize - lReadPos;
	}
	return inputBufferSize;
}

// boolÖµÐòÁÐ»¯Îª×Ö·û´®
std::string func_bool_to_string(bool bVal) {
	if (bVal) {
		return "true";
	}
	else {
		return "false";
	}
}


// ½øÐÐÉùÒô´¦Àí£¬½ÏÎªºÄÊ±£¬ÔÚµ¥¶ÀµÄÏß³ÌÀï½øÐÐ£¬±ÜÃâÖ÷Ïß³Ì¿¨¶Ù±¬Òô
void func_do_voice_transfer_worker(
	int iNumberOfChanel,					// Í¨µÀÊýÁ¿
	double dProjectSampleRate,				// ÏîÄ¿²ÉÑùÂÊ

	long lModelInputOutputBufferSize,		// Ä£ÐÍÊäÈëÊä³ö»º³åÇø´óÐ¡
	float* fModeulInputSampleBuffer,		// Ä£ÐÍÊäÈë»º³åÇø
	long* lModelInputSampleBufferReadPos,	// Ä£ÐÍÊäÈë»º³åÇø¶ÁÖ¸Õë
	long* lModelInputSampleBufferWritePos,	// Ä£ÐÍÊäÈë»º³åÇøÐ´Ö¸Õë

	float* fModeulOutputSampleBuffer,		// Ä£ÐÍÊä³ö»º³åÇø
	long* lModelOutputSampleBufferReadPos,	// Ä£ÐÍÊä³ö»º³åÇø¶ÁÖ¸Õë
	long* lModelOutputSampleBufferWritePos,	// Ä£ÐÍÊä³ö»º³åÇøÐ´Ö¸Õë

	float* fPitchChange,					// Òôµ÷±ä»¯ÊýÖµ
	bool* bCalcPitchError,					// ÆôÓÃÒôµ÷Îó²î¼ì²â

	std::vector<roleStruct> roleStructList,	// ÅäÖÃµÄ¿ÉÓÃÒôÉ«ÁÐ±í
	int* iSelectRoleIndex,					// Ñ¡ÔñµÄ½ÇÉ«ID
	FUNC_SRC_SIMPLE dllFuncSrcSimple,		// DLLÄÚ²¿SrcSimple·½·¨

	bool* bEnableSOVITSPreResample,			// ÆôÓÃSOVITSÄ£ÐÍÈë²ÎÒôÆµÖØ²ÉÑùÔ¤´¦Àí
	int iSOVITSModelInputSamplerate,		// SOVITSÄ£ÐÍÈë²Î²ÉÑùÂÊ
	bool* bEnableHUBERTPreResample,			// ÆôÓÃHUBERTÄ£ÐÍÈë²ÎÒôÆµÖØ²ÉÑùÔ¤´¦Àí
	int iHUBERTInputSampleRate,				// HUBERTÄ£ÐÍÈë²Î²ÉÑùÂÊ

	bool* bRealTimeModel,					// Õ¼Î»·û£¬ÊµÊ±Ä£Ê½
	bool* bDoItSignal,						// Õ¼Î»·û£¬±íÊ¾¸ÃworkerÓÐ´ý´¦ÀíµÄÊý¾Ý
	bool* bEnableDebug,
	juce::Value vServerUseTime,

	bool* bWorkerNeedExit,					// Õ¼Î»·û£¬±íÊ¾workerÏß³ÌÐèÒªÍË³ö
	std::mutex* mWorkerSafeExit				// »¥³âËø£¬±íÊ¾workerÏß³ÌÒÑ¾­°²È«ÍË³ö
) {
	char buff[100];
	long long tTime1;
	long long tTime2;
	long long tUseTime;
	long long tStart;

	double dSOVITSInputSamplerate;
	char sSOVITSSamplerateBuff[100];
	char cPitchBuff[100];
	std::string sHUBERTSampleBuffer;
	std::string sCalcPitchError;
	std::string sEnablePreResample;

	mWorkerSafeExit->lock();
	while (!*bWorkerNeedExit) {
		// ÂÖÑµ¼ì²é±êÖ¾Î»
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		if (*bDoItSignal) {
			// ÓÐÐèÒª´¦ÀíµÄÐÅºÅ£¬¿ªÊ¼´¦Àí£¬²¢½«±êÖ¾Î»ÉèÖÃÎªfalse
			// ´Ë´¦¿¼ÂÇµ½ÐÔÄÜÎÊÌâ£¬²¢Î´Ê¹ÓÃ»¥³âËøÊµÏÖÔ­×Ó²Ù×÷
			// ÈôÍ¬Ò»Ê±¼ä²úÉúÁËÐèÒª´¦ÀíµÄÐÅºÅ£¬ÔòµÈµ½ÏÂÒ»¸ö±êÖ¾Î»ÎªtrueÊ±ÔÙ´¦ÀíÒ²ÎÞ·Á
			*bDoItSignal = false;
			tStart = func_get_timestamp();
			tTime1 = tStart;


			roleStruct roleStruct = roleStructList[*iSelectRoleIndex];

			// ±£´æÒôÆµÊý¾Ýµ½ÎÄ¼þ
			// »ñÈ¡µ±Ç°Ð´Ö¸ÕëµÄÎ»ÖÃ
			long lTmpModelInputSampleBufferWritePos = *lModelInputSampleBufferWritePos;

			AudioFile<double>::AudioBuffer modelInputAudioBuffer;
			modelInputAudioBuffer.resize(iNumberOfChanel);

			// ´Ó¶ÓÁÐÖÐÈ¡³öËùÐèµÄÒôÆµÊý¾Ý
			std::vector<float> vModelInputSampleBufferVector;
			if (*lModelInputSampleBufferReadPos < lTmpModelInputSampleBufferWritePos) {
				for (int i = *lModelInputSampleBufferReadPos; i < lTmpModelInputSampleBufferWritePos; i++) {
					vModelInputSampleBufferVector.push_back(fModeulInputSampleBuffer[i]);
				}
			}
			else {
				for (int i = *lModelInputSampleBufferReadPos; i < lModelInputOutputBufferSize; i++) {
					vModelInputSampleBufferVector.push_back(fModeulInputSampleBuffer[i]);
				}
				for (int i = 0; i < lTmpModelInputSampleBufferWritePos; i++) {
					vModelInputSampleBufferVector.push_back(fModeulInputSampleBuffer[i]);
				}
			}
			// ¶ÁÈ¡Íê±Ï£¬½«¶ÁÖ¸ÕëÖ¸Ïò×îºóÐ´Ö¸Õë
			*lModelInputSampleBufferReadPos = lTmpModelInputSampleBufferWritePos;


			if (*bEnableSOVITSPreResample) {
				// ÌáÇ°¶ÔÒôÆµÖØ²ÉÑù£¬C++ÖØ²ÉÑù±ÈPython¶Ë¿ì
				dSOVITSInputSamplerate = iSOVITSModelInputSamplerate;

				// SOVITSÊäÈëÒôÆµÖØ²ÉÑù
				float* fReSampleInBuffer = vModelInputSampleBufferVector.data();
				float* fReSampleOutBuffer = fReSampleInBuffer;
				long iResampleNumbers = static_cast<long>(vModelInputSampleBufferVector.size());

				if (dProjectSampleRate != iSOVITSModelInputSamplerate) {
					double fScaleRate = iSOVITSModelInputSamplerate / dProjectSampleRate;
					iResampleNumbers = static_cast<long>(fScaleRate * iResampleNumbers);
					fReSampleOutBuffer = (float*)(std::malloc(sizeof(float) * iResampleNumbers));
					func_audio_resample(dllFuncSrcSimple, fReSampleInBuffer, fReSampleOutBuffer, fScaleRate, static_cast<long>(vModelInputSampleBufferVector.size()), iResampleNumbers);
				}

				snprintf(sSOVITSSamplerateBuff, sizeof(sSOVITSSamplerateBuff), "%d", iSOVITSModelInputSamplerate);
				modelInputAudioBuffer[0].resize(iResampleNumbers);
				for (int i = 0; i < iResampleNumbers; i++) {
					modelInputAudioBuffer[0][i] = fReSampleOutBuffer[i];
				}

				if (*bEnableHUBERTPreResample) {
					// HUBERTÊäÈëÒôÆµÖØ²ÉÑù
					double fScaleRate = iHUBERTInputSampleRate / dProjectSampleRate;
					iResampleNumbers = static_cast<long>(fScaleRate * vModelInputSampleBufferVector.size());
					fReSampleOutBuffer = (float*)(std::malloc(sizeof(float) * iResampleNumbers));
					func_audio_resample(dllFuncSrcSimple, fReSampleInBuffer, fReSampleOutBuffer, fScaleRate, static_cast<long>(vModelInputSampleBufferVector.size()), iResampleNumbers);

					AudioFile<double>::AudioBuffer HUBERTModelInputAudioBuffer;
					HUBERTModelInputAudioBuffer.resize(iNumberOfChanel);
					HUBERTModelInputAudioBuffer[0].resize(iResampleNumbers);
					for (int i = 0; i < iResampleNumbers; i++) {
						HUBERTModelInputAudioBuffer[0][i] = fReSampleOutBuffer[i];
					}
					AudioFile<double> HUBERTAudioFile;
					HUBERTAudioFile.shouldLogErrorsToConsole(false);
					HUBERTAudioFile.setAudioBuffer(HUBERTModelInputAudioBuffer);
					HUBERTAudioFile.setAudioBufferSize(iNumberOfChanel, static_cast<int>(HUBERTModelInputAudioBuffer[0].size()));
					HUBERTAudioFile.setBitDepth(24);
					HUBERTAudioFile.setSampleRate(iHUBERTInputSampleRate);

					// ±£´æÒôÆµÎÄ¼þµ½ÄÚ´æ
					std::vector<uint8_t> vHUBERTModelInputMemoryBuffer;
					HUBERTAudioFile.saveToWaveMemory(&vHUBERTModelInputMemoryBuffer);

					// ´ÓÄÚ´æ¶ÁÈ¡Êý¾Ý
					auto vHUBERTModelInputData = vHUBERTModelInputMemoryBuffer.data();
					std::string sHUBERTModelInputString(vHUBERTModelInputData, vHUBERTModelInputData + vHUBERTModelInputMemoryBuffer.size());
					sHUBERTSampleBuffer = sHUBERTModelInputString;
				}
				else {
					sHUBERTSampleBuffer = "";
				}
			}
			else {
				// Î´¿ªÆôÔ¤´¦ÀíÖØ²ÉÑù£¬ÒôÆµÔ­Ñù·¢ËÍ
				sHUBERTSampleBuffer = "";
				dSOVITSInputSamplerate = dProjectSampleRate;
				snprintf(sSOVITSSamplerateBuff, sizeof(sSOVITSSamplerateBuff), "%f", dProjectSampleRate);
				modelInputAudioBuffer[0].resize(vModelInputSampleBufferVector.size());
				for (int i = 0; i < vModelInputSampleBufferVector.size(); i++) {
					modelInputAudioBuffer[0][i] = vModelInputSampleBufferVector[i];
				}
			}

			long iModelInputNumSamples = static_cast<long>(modelInputAudioBuffer[0].size());
			AudioFile<double> audioFile;
			audioFile.shouldLogErrorsToConsole(false);
			audioFile.setAudioBuffer(modelInputAudioBuffer);
			audioFile.setAudioBufferSize(iNumberOfChanel, iModelInputNumSamples);
			audioFile.setBitDepth(24);
			audioFile.setSampleRate(static_cast<UINT32>(dSOVITSInputSamplerate));

			tTime2 = func_get_timestamp();
			tUseTime = tTime2 - tTime1;
			if (*bEnableDebug) {
				snprintf(buff, sizeof(buff), "×¼±¸±£´æµ½ÒôÆµÎÄ¼þºÄÊ±:%lldms\n", tUseTime);
				OutputDebugStringA(buff);
			}
			tTime1 = tTime2;

			// ±£´æÒôÆµÎÄ¼þµ½ÄÚ´æ
			std::vector<uint8_t> vModelInputMemoryBuffer;
			audioFile.saveToWaveMemory(&vModelInputMemoryBuffer);

			tTime2 = func_get_timestamp();
			tUseTime = tTime2 - tTime1;
			if (*bEnableDebug) {
				snprintf(buff, sizeof(buff), "±£´æµ½ÒôÆµÎÄ¼þºÄÊ±:%lldms\n", tUseTime);
				OutputDebugStringA(buff);
			}
			tTime1 = tTime2;

			// µ÷ÓÃAIÄ£ÐÍ½øÐÐÉùÒô´¦Àí
			httplib::Client cli(roleStruct.sApiUrl);

			cli.set_connection_timeout(0, 1000000); // 300 milliseconds
			cli.set_read_timeout(5, 0); // 5 seconds
			cli.set_write_timeout(5, 0); // 5 seconds

			// ´ÓÄÚ´æ¶ÁÈ¡Êý¾Ý
			auto vModelInputData = vModelInputMemoryBuffer.data();
			std::string sModelInputString(vModelInputData, vModelInputData + vModelInputMemoryBuffer.size());

			// ×¼±¸HTTPÇëÇó²ÎÊý
			snprintf(cPitchBuff, sizeof(cPitchBuff), "%f", *fPitchChange);
			sCalcPitchError = func_bool_to_string(*bCalcPitchError);
			sEnablePreResample = func_bool_to_string(*bEnableSOVITSPreResample);

			httplib::MultipartFormDataItems items = {
				{ "sSpeakId", roleStruct.sSpeakId, "", ""},
				{ "sName", roleStruct.sName, "", ""},
				{ "fPitchChange", cPitchBuff, "", ""},
				{ "sampleRate", sSOVITSSamplerateBuff, "", ""},
				{ "bCalcPitchError", sCalcPitchError.c_str(), "", ""},
				{ "bEnablePreResample", sEnablePreResample.c_str(), "", ""},
				{ "sample", sModelInputString, "sample.wav", "audio/x-wav"},
				{ "hubert_sample", sHUBERTSampleBuffer, "hubert_sample.wav", "audio/x-wav"},
			};
			if (*bEnableDebug) {
				OutputDebugStringA("µ÷ÓÃAIËã·¨Ä£ÐÍ\n");
			}
			auto res = cli.Post("/voiceChangeModel", items);

			tTime2 = func_get_timestamp();
			tUseTime = tTime2 - tTime1;
			vServerUseTime.setValue(juce::String(tUseTime) + "ms");
			if (*bEnableDebug) {
				snprintf(buff, sizeof(buff), "µ÷ÓÃHTTP½Ó¿ÚºÄÊ±:%lldms\n", tUseTime);
				OutputDebugStringA(buff);
			}
			tTime1 = tTime2;

			if (res.error() == httplib::Error::Success && res->status == 200) {
				// µ÷ÓÃ³É¹¦£¬¿ªÊ¼½«½á¹û·ÅÈëµ½ÁÙÊ±»º³åÇø£¬²¢Ìæ»»Êä³ö
				std::string body = res->body;
				std::vector<uint8_t> vModelOutputBuffer(body.begin(), body.end());

				AudioFile<double> tmpAudioFile;
				tmpAudioFile.loadFromMemory(vModelOutputBuffer);
				int sampleRate = tmpAudioFile.getSampleRate();
				// int bitDepth = tmpAudioFile.getBitDepth();
				int numSamples = tmpAudioFile.getNumSamplesPerChannel();
				//double lengthInSeconds = tmpAudioFile.getLengthInSeconds();
				// int numChannels = tmpAudioFile.getNumChannels();
				//bool isMono = tmpAudioFile.isMono();

				std::vector<double> fOriginAudioBuffer = tmpAudioFile.samples[0];

				// ÒôÆµÖØ²ÉÑù
				float* fReSampleInBuffer = (float*)malloc(numSamples * sizeof(float));
				float* fReSampleOutBuffer = fReSampleInBuffer;
				int iResampleNumbers = numSamples;
				for (int i = 0; i < numSamples; i++) {
					fReSampleInBuffer[i] = fOriginAudioBuffer[i];
				}
				if (sampleRate != dProjectSampleRate) {
					double fScaleRate = dProjectSampleRate / sampleRate;
					iResampleNumbers = static_cast<int>(fScaleRate * numSamples);
					fReSampleOutBuffer = (float*)(std::malloc(sizeof(float) * iResampleNumbers));
					func_audio_resample(dllFuncSrcSimple, fReSampleInBuffer, fReSampleOutBuffer, fScaleRate, numSamples, iResampleNumbers);
				}

				tTime2 = func_get_timestamp();
				tUseTime = tTime2 - tTime1;
				if (*bEnableDebug) {
					snprintf(buff, sizeof(buff), "¶ÔÄ£ÐÍÊä³öÖØ²ÉÑùºÄÊ±:%lldms\n", tUseTime);
					OutputDebugStringA(buff);
				}
				tTime1 = tTime2;

				// µ±ÆôÓÃÁËÊµÊ±Ä£Ê½Ê±£¬»º³åÇøµÄÐ´Ö¸ÕëÐèÒªÌØÊâ´¦Àí
				// ÀýÈç£ºµ±³öÏÖÑÓ³Ù¶¶¶¯Ê±£¬½ÓÊÕµ½×îÐÂµÄÊý¾ÝÊ±»º³åÇø»¹ÓÐ¾ÉÊý¾Ý£¬´ËÊ±Ö±½Ó¶ªÆú¾ÉÊý¾Ý£¬ÓÃÐÂÊý¾Ý¸²¸Ç
				if (*bRealTimeModel) {
					// °²È«Çø´óÐ¡£¬ÒòÎªÏÖÔÚ¶ÁÐ´Ïß³Ì²¢·ÇÏß³Ì°²È«£¬Òò´ËÕâÀïÉèÖÃÒ»¸ö°²È«Çø´óÐ¡£¬±ÜÃâÊý¾Ý³öÏÖÎÊÌâ
					int iRealTimeModeBufferSafeZoneSize = 16;
					// ¼ÆËã³öÐÂµÄÐ´Ö¸ÕëÎ»ÖÃ£º
					// 1.µ±Ç°¾ÉÊý¾Ý´óÐ¡ > °²È«Çø´óÐ¡£¬Ð´Ö¸ÕëÇ°ÒÆ¶¨Î»ÔÚ°²È«ÇøÎ²²¿
					// 2.µ±Ç°¾ÉÊý¾Ý´óÐ¡ < °²È«Çø´óÐ¡£¬Ð´Ö¸ÕëÇ°ÒÆ¶¨Î»ÔÚ¾ÉÊý¾ÝÎ²²¿£¨ÎÞÈÎºÎ²Ù×÷£¬±£³Ö²»±ä£©
					long inputBufferSize = func_cacl_read_write_buffer_data_size(lModelInputOutputBufferSize, *lModelOutputSampleBufferReadPos, *lModelOutputSampleBufferWritePos);
					if (inputBufferSize > iRealTimeModeBufferSafeZoneSize) {
						*lModelOutputSampleBufferWritePos = (*lModelOutputSampleBufferReadPos + iRealTimeModeBufferSafeZoneSize) % lModelInputOutputBufferSize;
					}
				}

				// ´ÓÐ´Ö¸Õë±ê¼ÇµÄ»º³åÇøÎ»ÖÃ¿ªÊ¼Ð´ÈëÐÂµÄÒôÆµÊý¾Ý
				long lTmpModelOutputSampleBufferWritePos = *lModelOutputSampleBufferWritePos;

				for (int i = 0; i < iResampleNumbers; i++) {
					fModeulOutputSampleBuffer[lTmpModelOutputSampleBufferWritePos++] = fReSampleOutBuffer[i];
					if (lTmpModelOutputSampleBufferWritePos == lModelInputOutputBufferSize) {
						lTmpModelOutputSampleBufferWritePos = 0;
					}
					// ×¢Òâ£¬ÒòÎª»º³åÇøÓ¦µ±¾¡¿ÉÄÜµÄ´ó£¬ËùÒÔ´Ë´¦²»¿¼ÂÇÐ´Ö¸Õë×·ÉÏ¶ÁÖ¸ÕëµÄÇé¿ö
				}
				// ½«Ð´Ö¸ÕëÖ¸ÏòÐÂµÄÎ»ÖÃ
				*lModelOutputSampleBufferWritePos = lTmpModelOutputSampleBufferWritePos;
				if (*bEnableDebug) {
					snprintf(buff, sizeof(buff), "Êä³öÐ´Ö¸Õë:%ld\n", lTmpModelOutputSampleBufferWritePos);
					OutputDebugStringA(buff);
				}

				tTime2 = func_get_timestamp();
				tUseTime = tTime2 - tTime1;
				if (*bEnableDebug) {
					snprintf(buff, sizeof(buff), "Ð´»º³åÇøºÄÊ±:%lldms\n", tUseTime);
					OutputDebugStringA(buff);
				}
				tTime1 = tTime2;
			}
			else {
				auto err = res.error();
				if (*bEnableDebug) {
					snprintf(buff, sizeof(buff), "Ëã·¨·þÎñ´íÎó:%d\n", err);
					OutputDebugStringA(buff);
				}
			}
			tUseTime = func_get_timestamp() - tStart;
			if (*bEnableDebug) {
				snprintf(buff, sizeof(buff), "¸Ã´ÎwokerÂÖÑµ×ÜºÄÊ±:%lld\n", tUseTime);
				OutputDebugStringA(buff);
			}
		}
	}
	mWorkerSafeExit->unlock();
}
