#include "PluginProcessor.h"
#include "AudioFile.h"
#include "httplib.h"
#include "AudioWork.h"

using namespace std::chrono;

long long func_get_timestamp() {
	return (duration_cast<milliseconds>(system_clock::now().time_since_epoch())).count();
}

// �ز���
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

// �ز���
std::vector<float> func_audio_resample_vector_version(std::vector<float> inputSampleV, int srcSampleRate, int destSampleRate, FUNC_SRC_SIMPLE dllFuncSrcSimple) {
	if (srcSampleRate == destSampleRate) {
		return inputSampleV;
	}
	else {
		double fScaleRate = 1.f * destSampleRate / srcSampleRate;
		long lInputSize = inputSampleV.size();
		long lOutSize = static_cast<long>(fScaleRate * lInputSize);
		float* fReSampleOutBuffer = (float*)(std::malloc(sizeof(float) * lOutSize));
		func_audio_resample(dllFuncSrcSimple, inputSampleV.data(), fReSampleOutBuffer, fScaleRate, lInputSize, lOutSize);
		return std::vector<float>(fReSampleOutBuffer, fReSampleOutBuffer + lOutSize);
	}
}

// ���ڼ���һ����д���������Ч���ݴ�С
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

// boolֵ���л�Ϊ�ַ���
std::string func_bool_to_string(bool bVal) {
	if (bVal) {
		return "true";
	}
	else {
		return "false";
	}
}


// ��������������Ϊ��ʱ���ڵ������߳�����У��������߳̿��ٱ���
void func_do_voice_transfer_worker(
	int iNumberOfChanel,					// ͨ������
	double dProjectSampleRate,				// ��Ŀ������

	std::vector<INPUT_JOB_STRUCT>* modelInputJobList, // ģ���������
	std::mutex* modelInputJobListMutex,		// ģ�����������

	long lModelOutputBufferSize,			// ģ�������������С
	float* fModeulOutputSampleBuffer,		// ģ�����������
	long* lModelOutputSampleBufferReadPos,	// ģ�������������ָ��
	long* lModelOutputSampleBufferWritePos,	// ģ�����������дָ��

	std::mutex* lastVoiceSampleForCrossFadeVectorMutex,
	std::vector<float>* lastVoiceSampleForCrossFadeVector, // ���һ��ģ�������Ƶ��β�������ڽ��浭������
	int* lastVoiceSampleCrossFadeSkipNumber,

	float* fPrefixLength,					// ǰ��������ʱ��(s)
	float* fDropSuffixLength,				// ������β��ʱ��(s)
	float* fPitchChange,					// �����仯��ֵ
	bool* bCalcPitchError,					// �������������

	std::vector<roleStruct> roleStructList,	// ���õĿ�����ɫ�б�
	int* iSelectRoleIndex,					// ѡ��Ľ�ɫID
	FUNC_SRC_SIMPLE dllFuncSrcSimple,		// DLL�ڲ�SrcSimple����

	bool* bEnableSOVITSPreResample,			// ����SOVITSģ�������Ƶ�ز���Ԥ����
	int iSOVITSModelInputSamplerate,		// SOVITSģ����β�����
	bool* bEnableHUBERTPreResample,			// ����HUBERTģ�������Ƶ�ز���Ԥ����
	int iHUBERTInputSampleRate,				// HUBERTģ����β�����

	bool* bRealTimeModel,					// ռλ����ʵʱģʽ
	bool* bEnableDebug,						// ռλ��������DEBUG���
	juce::Value vServerUseTime,				// UI��������ʾ������ú�ʱ
	juce::Value vDropDataLength,			// UI��������ʾʵʱģʽ�¶�������Ƶ���ݳ���

	bool* bWorkerNeedExit,					// ռλ������ʾworker�߳���Ҫ�˳�
	std::mutex* mWorkerSafeExit				// ����������ʾworker�߳��Ѿ���ȫ�˳�
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
	std::vector<float> vModelInputSampleBufferVector;
	std::vector<float> currentVoiceVector;
	int currentVoiceSampleNumber = 0;
	int iHopSize;
	INPUT_JOB_STRUCT jobStruct;

	mWorkerSafeExit->lock();
	while (!*bWorkerNeedExit) {
		// ��ѵ����־λ
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		modelInputJobListMutex->lock();
		auto queueSize = modelInputJobList->size();
		if (queueSize > 0) {
			jobStruct = modelInputJobList->at(0); 
			modelInputJobList->erase(modelInputJobList->begin());
		}
		modelInputJobListMutex->unlock();
		/*if (*bEnableDebug) {
			snprintf(buff, sizeof(buff), "queueSize:%lld\n", queueSize);
			OutputDebugStringA(buff);
		}*/
		if (queueSize > 0) {
			tStart = func_get_timestamp();
			tTime1 = tStart;
			roleStruct roleStruct = roleStructList[*iSelectRoleIndex];
			iHopSize = roleStruct.iHopSize;

			if (jobStruct.jobType == JOB_EMPTY) {
				// ���������Ǿ����ģ�ֱ��׼����ȳ��ȵľ������
				currentVoiceVector.clear();
				for (int i = 0; i < jobStruct.emptySampleNumber; i++) {
					currentVoiceVector.push_back(0.f);
				}
				currentVoiceSampleNumber = jobStruct.emptySampleNumber;
			}
			else {
				vModelInputSampleBufferVector = jobStruct.modelInputSampleVector;

				if (*bEnableDebug) {
					snprintf(buff, sizeof(buff), "ģ������������:%lld��ʱ�� : %.1fms\n", vModelInputSampleBufferVector.size(), 1.0f * vModelInputSampleBufferVector.size() / dProjectSampleRate * 1000);
					OutputDebugStringA(buff);
				}

				AudioFile<double>::AudioBuffer modelInputAudioBuffer;
				modelInputAudioBuffer.resize(iNumberOfChanel);

				if (*bEnableSOVITSPreResample) {
					// ��ǰ����Ƶ�ز�����C++�ز�����Python�˿�
					dSOVITSInputSamplerate = iSOVITSModelInputSamplerate;

					// SOVITS������Ƶ�ز���
					auto ReSampleOutputVector = func_audio_resample_vector_version(vModelInputSampleBufferVector, dProjectSampleRate, iSOVITSModelInputSamplerate, dllFuncSrcSimple);
					long iResampleNumbers = ReSampleOutputVector.size();

					snprintf(sSOVITSSamplerateBuff, sizeof(sSOVITSSamplerateBuff), "%d", iSOVITSModelInputSamplerate);
					modelInputAudioBuffer[0].resize(iResampleNumbers);
					for (int i = 0; i < ReSampleOutputVector.size(); i++) {
						modelInputAudioBuffer[0][i] = ReSampleOutputVector[i];
					}

					if (*bEnableHUBERTPreResample) {
						// HUBERT������Ƶ�ز���
						auto ReSampleOutputVector = func_audio_resample_vector_version(vModelInputSampleBufferVector, dProjectSampleRate, iHUBERTInputSampleRate, dllFuncSrcSimple);
						long iResampleNumbers = ReSampleOutputVector.size();

						AudioFile<double>::AudioBuffer HUBERTModelInputAudioBuffer;
						HUBERTModelInputAudioBuffer.resize(iNumberOfChanel);
						HUBERTModelInputAudioBuffer[0].resize(iResampleNumbers);
						for (int i = 0; i < iResampleNumbers; i++) {
							HUBERTModelInputAudioBuffer[0][i] = ReSampleOutputVector[i];
						}

						AudioFile<double> HUBERTAudioFile;
						HUBERTAudioFile.shouldLogErrorsToConsole(false);
						HUBERTAudioFile.setAudioBuffer(HUBERTModelInputAudioBuffer);
						HUBERTAudioFile.setAudioBufferSize(iNumberOfChanel, static_cast<int>(HUBERTModelInputAudioBuffer[0].size()));
						HUBERTAudioFile.setBitDepth(24);
						HUBERTAudioFile.setSampleRate(iHUBERTInputSampleRate);

						// ������Ƶ�ļ����ڴ�
						std::vector<uint8_t> vHUBERTModelInputMemoryBuffer;
						HUBERTAudioFile.saveToWaveMemory(&vHUBERTModelInputMemoryBuffer);

						// ���ڴ��ȡ����
						auto vHUBERTModelInputData = vHUBERTModelInputMemoryBuffer.data();
						std::string sHUBERTModelInputString(vHUBERTModelInputData, vHUBERTModelInputData + vHUBERTModelInputMemoryBuffer.size());
						sHUBERTSampleBuffer = sHUBERTModelInputString;
					}
					else {
						sHUBERTSampleBuffer = "";
					}
				}
				else {
					// δ����Ԥ�����ز�������Ƶԭ������
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
				/*if (*bEnableDebug) {
					snprintf(buff, sizeof(buff), "׼�����浽��Ƶ�ļ���ʱ:%lldms\n", tUseTime);
					OutputDebugStringA(buff);
				}*/
				tTime1 = tTime2;

				// ������Ƶ�ļ����ڴ�
				std::vector<uint8_t> vModelInputMemoryBuffer;
				audioFile.saveToWaveMemory(&vModelInputMemoryBuffer);

				tTime2 = func_get_timestamp();
				tUseTime = tTime2 - tTime1;
				/*if (*bEnableDebug) {
					snprintf(buff, sizeof(buff), "���浽��Ƶ�ļ���ʱ:%lldms\n", tUseTime);
					OutputDebugStringA(buff);
				}*/
				tTime1 = tTime2;

				// ����AIģ�ͽ�����������
				httplib::Client cli(roleStruct.sApiUrl);

				cli.set_connection_timeout(0, 1000000); // 300 milliseconds
				cli.set_read_timeout(5, 0); // 5 seconds
				cli.set_write_timeout(5, 0); // 5 seconds

				// ���ڴ��ȡ����
				auto vModelInputData = vModelInputMemoryBuffer.data();
				std::string sModelInputString(vModelInputData, vModelInputData + vModelInputMemoryBuffer.size());

				// ׼��HTTP�������
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
				/*if (*bEnableDebug) {
					OutputDebugStringA("�㷨ģ��\n");
				}*/
				auto res = cli.Post("/voiceChangeModel", items);

				tTime2 = func_get_timestamp();
				tUseTime = tTime2 - tTime1;
				vServerUseTime.setValue(juce::String(tUseTime) + "ms");
				/*if (*bEnableDebug) {
					snprintf(buff, sizeof(buff), "����HTTP�ӿں�ʱ:%lldms\n", tUseTime);
					OutputDebugStringA(buff);
				}*/
				tTime1 = tTime2;

				if (res.error() == httplib::Error::Success && res->status == 200) {
					// ���óɹ�����ʼ��������뵽��ʱ�����������滻���
					std::string body = res->body;
					std::vector<uint8_t> vModelOutputBuffer(body.begin(), body.end());

					AudioFile<double> tmpAudioFile;
					tmpAudioFile.loadFromMemory(vModelOutputBuffer);
					int sampleRate = tmpAudioFile.getSampleRate();
					// int currentVoiceSampleNumber = tmpAudioFile.getNumSamplesPerChannel();
					// double lengthInSeconds = tmpAudioFile.getLengthInSeconds();
					// int numChannels = tmpAudioFile.getNumChannels();
					// bool isMono = tmpAudioFile.isMono();
					// int bitDepth = tmpAudioFile.getBitDepth();

					std::vector<double> fOriginAudioBuffer = tmpAudioFile.samples[0];
					std::vector<float> currentVoiceVectorUnResample(fOriginAudioBuffer.begin(), fOriginAudioBuffer.end());

					// ��Ƶ�ز���
					currentVoiceVector = func_audio_resample_vector_version(currentVoiceVectorUnResample, sampleRate, dProjectSampleRate, dllFuncSrcSimple);
					currentVoiceSampleNumber = currentVoiceVector.size();
					/*if (*bEnableDebug) {
						snprintf(buff, sizeof(buff), "currentVoiceSampleNumber:%d\n", currentVoiceSampleNumber);
						OutputDebugStringA(buff);
					}*/
				}
				else {
					// ���ִ���׼����ȳ��ȵľ������
					currentVoiceVector.clear();
					for (int i = 0; i < jobStruct.modelInputSampleVector.size(); i++) {
						currentVoiceVector.push_back(0.f);
					}
					auto err = res.error();
					if (*bEnableDebug) {
						snprintf(buff, sizeof(buff), "�㷨�������:%d\n", err);
						OutputDebugStringA(buff);
					}
				}
			};

			if (*bEnableDebug) {
				snprintf(buff, sizeof(buff), "ģ�����������:%d��ʱ�� : %.1fms\n", currentVoiceSampleNumber, 1.0f * currentVoiceSampleNumber / dProjectSampleRate * 1000);
				OutputDebugStringA(buff);
			}

			if (currentVoiceSampleNumber < vModelInputSampleBufferVector.size()) {
				int padNumber = vModelInputSampleBufferVector.size() - currentVoiceSampleNumber;
				for (int i = 0;i < padNumber;i++) {
					currentVoiceVector.push_back(0.f);
				}
				currentVoiceSampleNumber = currentVoiceVector.size();

				if (*bEnableDebug) {
					snprintf(buff, sizeof(buff), "ģ��������������㣬���侲������:%d��ʱ�� : %.1fms\n", padNumber, 1.0f * padNumber / dProjectSampleRate * 1000);
					OutputDebugStringA(buff);
				}
			}

			// ���浭��������ǰ��������Ƶ�νӵ�����
			// ���浭���㷨
			// ��һ����ƵΪS1����ǰ��ƵΪS2���غ�ʱ��ΪOverlap1�����浭��ʱ��Ϊoverlap2��overlap3 = overlap1 - overlap2
			// �غϲ���������������1.ʹ��ģ���ܻ�ȡ���㹻�������룬���������ֵ�������2.�ص������������浭������������
			// S1��β��overlap1���ֺ�S2��overlap1�������ܹ�����ģ�ȡoverlap1����β����overlap2���������浭��
			// �����߼���
			// 1.��һ����ƵS1��ֻԤ����S1f���֣��˲���ΪS1��β��overlap2����
			// 2.S1fΪ�˱���VST������������Ѿ�Ԥ֧һ���֣���֮ΪS1f_skip�����ǽ�S1fǰS1f_skip���ֶ������������S1f����ΪlCrossFadeLength��ע�⣺�����Ĺ��̷�����processor.cpp��
			// 3.��S2��ǰoverlap3����������Ȼ��������S1f_skip���֣���ʱS2��ʣ�ಿ�ֺ�S1f�Ƕ���ģ��ڴ˲��������浭����S2ʣ�ಿ��S3b�ͽ��浭��S3a���ֹ�ͬ�����S3
			// 4.S3β��overlap2����Ԥ��������һ�ν��浭����ʣ�ಿ��ֱ�������VST���
			
			std::vector<float> processedSampleVector = currentVoiceVector;
			
			lastVoiceSampleForCrossFadeVectorMutex->lock();
			
			float fOverlap1 = *fPrefixLength;
			long lOverlap1 = fOverlap1 * dProjectSampleRate;
			float fOverlap2 = 0.08f;
			long lOverlap2 = fOverlap2 * dProjectSampleRate;
			lOverlap2 = ceil(1.0 * lOverlap2 / iHopSize) * iHopSize;
			float fOverlap3 = fOverlap1 - fOverlap2;
			long lOverlap3 = fOverlap3 * dProjectSampleRate;

			if (*bRealTimeModel && *fPrefixLength > 0.01f) {

				int S1f_skip = *lastVoiceSampleCrossFadeSkipNumber;

				auto s1f = *lastVoiceSampleForCrossFadeVector;
				// ��s1f���hop�����룬���ײ��޼������޼���СΪS1f_skip_more_skip
				auto s1fMatchHopSize = floor(1.0 * s1f.size() / iHopSize) * iHopSize;
				int S1f_skip_more_skip = s1f.size() - s1fMatchHopSize;
				S1f_skip += S1f_skip_more_skip;
				s1f = std::vector<float>(s1f.end() - s1fMatchHopSize, s1f.end());

				auto lCrossFadeLength = s1f.size();
				auto s2 = std::vector<float>(currentVoiceVector.begin() + S1f_skip + lOverlap3, currentVoiceVector.end());

				std::vector<float> s3;
				auto s3b = std::vector<float>(s2.begin() + lCrossFadeLength, s2.end());

				float fStepAlpha = 1.0f / lCrossFadeLength;

				for (int i = 0; i < lCrossFadeLength; i++) {
					float s1Sample = s1f.at(i);
					float s3Sample;
					if (jobStruct.jobType == JOB_EMPTY) {
						// �����ǰΪ�����飬����Ҫ����һ���β�����浭��
						s3Sample = s1Sample;
					}
					else {
						float s2Sample = s2.at(i);
						float alpha = fStepAlpha * i;
						s3Sample = s1Sample * (1.f - alpha) + s2Sample * alpha;
					}
					s3.push_back(s3Sample);
				}
				for (int i = 0; i < s3b.size(); i++) {
					s3.push_back(s3b.at(i));
				};
				processedSampleVector = s3;

				if (*bEnableDebug) {
					snprintf(buff, sizeof(buff), "fOverlap1:%.0fms\tfOverlap2:%.0fms\tfOverlap3:%.0fms\tS1f_skip:%d\tlCrossFadeLength:%d\n",
						fOverlap1 * 1000, fOverlap2 * 1000, fOverlap3 * 1000, S1f_skip, lCrossFadeLength);
					OutputDebugStringA(buff);
				}
			};
			int processedSampleNumber = processedSampleVector.size();
			//lastVoiceSampleForCrossFadeVectorMutex->unlock();

			// ��дָ���ǵĻ�����λ�ÿ�ʼд���µ���Ƶ����
			long lTmpModelOutputSampleBufferWritePos = *lModelOutputSampleBufferWritePos;
			// Ԥ��һ���������������浭������д��ʵʱ�����������Ϊoverlap2
			for (int i = 0; i < processedSampleNumber - lOverlap2; i++) {
				// ע�⣬��Ϊ���������Ӧ�������ܵĴ����Դ˴�������дָ��׷�϶�ָ������
				fModeulOutputSampleBuffer[lTmpModelOutputSampleBufferWritePos++] = processedSampleVector.at(i);
				if (lTmpModelOutputSampleBufferWritePos == lModelOutputBufferSize) {
					lTmpModelOutputSampleBufferWritePos = 0;
				}
			};
			// ����ǰ���ӵ�β��lOverlap2���ȱ��浽�����һ�仺�����������������浭������ʹ��
			//lastVoiceSampleForCrossFadeVectorMutex->lock();
			lastVoiceSampleForCrossFadeVector->clear();
			*lastVoiceSampleCrossFadeSkipNumber = 0;
			for (int i = currentVoiceSampleNumber - lOverlap2; i < currentVoiceSampleNumber; i++) {
				lastVoiceSampleForCrossFadeVector->push_back(currentVoiceVector.at(i));
			};
			// ��дָ��ָ���µ�λ��
			*lModelOutputSampleBufferWritePos = lTmpModelOutputSampleBufferWritePos;
			lastVoiceSampleForCrossFadeVectorMutex->unlock();

			if (*bEnableDebug) {
				snprintf(buff, sizeof(buff), "���������:%d��ʱ��:%.1fms���������ڵ�����������%d��ʱ��:%.1fms\n", 
					processedSampleNumber, 
					1.0f * processedSampleNumber / dProjectSampleRate * 1000,
					lOverlap2, 
					1.0f * lOverlap2 / dProjectSampleRate * 1000);
				OutputDebugStringA(buff);
			}

			tUseTime = func_get_timestamp() - tStart;
			/*if (*bEnableDebug) {
				snprintf(buff, sizeof(buff), "�ô�woker��ѵ�ܺ�ʱ:%lld\n", tUseTime);
				OutputDebugStringA(buff);
			}*/
		}
	}
	mWorkerSafeExit->unlock();
}
