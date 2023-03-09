#include "PluginProcessor.h"
#include "AudioFile.h"
#include "httplib.h"
#include "AudioWork.h"
#define M_PI 3.14159265358979323846

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

using namespace std;
vector<float> hanning(int n) {
	vector<float> win(n);
	for (int i = 0; i < n; i++) {
		win[i] = 0.5 * (1 - cos(2 * M_PI * i / (n - 1)));
	}
	return win;
}

vector<float> hanning_crossfade(const vector<float>& x1, const vector<float>& x2, const vector<float> hanningWindow) {
	int lCrossFadeLength = x1.size();
	vector<float> s3(lCrossFadeLength);
	float x1Sample;
	float x2Sample;
	float x3Sample;
	float w1;
	float w2;
	for (int i = 0; i < lCrossFadeLength; i++) {
		x1Sample = x1.at(i);
		x2Sample = x2.at(i);
		w1 = hanningWindow[i + lCrossFadeLength];
		w2 = hanningWindow[i];
		x3Sample = x1Sample * w1 + x2Sample * w2;
		s3[i] = x3Sample;
	}
	return s3;
}


vector<float> my_crossfade(const vector<float>& x1, const vector<float>& x2) {
	int lCrossFadeLength = x1.size();
	vector<float> s3(lCrossFadeLength);
	float x1Sample;
	float x2Sample;
	float x3Sample;
	float alpha;
	float fStepAlpha = 1.0f / lCrossFadeLength;

	for (int i = 0; i < lCrossFadeLength; i++) {
		x1Sample = x1.at(i);
		x2Sample = x2.at(i);
		alpha = fStepAlpha * i;
		x3Sample = x1Sample * (1.f - alpha) + x2Sample * alpha;
		s3[i] = x3Sample;
	}
	return s3;
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

	long lCrossFadeLength,
	std::vector<float>* lastVoiceSampleForCrossFadeVector, // ���һ��ģ�������Ƶ��β�������ڽ��浭������

	float* fPitchChange,					// �����仯��ֵ
	bool* bCalcPitchError,					// �������������

	std::vector<roleStruct> roleStructList,	// ���õĿ�����ɫ�б�
	int* iSelectRoleIndex,					// ѡ��Ľ�ɫID
	FUNC_SRC_SIMPLE dllFuncSrcSimple,		// DLL�ڲ�SrcSimple����

	bool* bEnableSOVITSPreResample,			// ����SOVITSģ�������Ƶ�ز���Ԥ����
	int iSOVITSModelInputSamplerate,		// SOVITSģ����β�����
	bool* bEnableHUBERTPreResample,			// ����HUBERTģ�������Ƶ�ز���Ԥ����
	int iHUBERTInputSampleRate,				// HUBERTģ����β�����

	bool* bEnableDebug,						// ռλ��������DEBUG���
	juce::Value vServerUseTime,				// UI��������ʾ������ú�ʱ
	float *fServerUseTime,
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
	char cSafePrefixPadLength[100];
	std::string sHUBERTSampleBuffer;
	std::string sCalcPitchError;
	std::string sEnablePreResample;
	std::vector<float> vModelInputSampleBufferVector;
	std::vector<float> currentVoiceVector;
	int currentVoiceSampleNumber = 0;
	int iHopSize;
	INPUT_JOB_STRUCT jobStruct;
	long lPrefixLength;
	bool bRealTimeModel;
	auto hanningWindow = hanning(2 * lCrossFadeLength);

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
		lPrefixLength = jobStruct.lPrefixLength;
		bRealTimeModel = jobStruct.bRealTimeModel;

		iHopSize = 512;
		long lOverlap1 = lPrefixLength;
		long lOverlap2 = lCrossFadeLength;
		long lOverlap3 = lOverlap1 - lOverlap2;
		float fOverlap1 = 1.0 * lOverlap1 / dProjectSampleRate;
		float fOverlap2 = 1.0 * lOverlap2 / dProjectSampleRate;
		float fOverlap3 = 1.0 * lOverlap3 / dProjectSampleRate;

		/*if (*bEnableDebug) {
			snprintf(buff, sizeof(buff), "queueSize:%lld\n", queueSize);
			OutputDebugStringA(buff);
		}*/
		if (queueSize > 0) {
			tStart = func_get_timestamp();
			tTime1 = tStart;
			roleStruct roleStruct = roleStructList[*iSelectRoleIndex];
			//iHopSize = roleStruct.iHopSize;

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
				snprintf(cSafePrefixPadLength, sizeof(cSafePrefixPadLength), "%f", 1.0 * lOverlap3 / dProjectSampleRate);
				sCalcPitchError = func_bool_to_string(*bCalcPitchError);
				sEnablePreResample = func_bool_to_string(*bEnableSOVITSPreResample);
				httplib::MultipartFormDataItems items = {
					{ "sSpeakId", roleStruct.sSpeakId, "", ""},
					{ "sName", roleStruct.sName, "", ""},
					{ "fPitchChange", cPitchBuff, "", ""},
					{ "fSafePrefixPadLength", cSafePrefixPadLength, "", ""},
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
				*fServerUseTime = tUseTime / 1000.0f;
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
					currentVoiceSampleNumber = currentVoiceVector.size();
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
					snprintf(buff, sizeof(buff), "ģ��������������㣬���侲������:%d��ʱ�� : %.0fms\n", padNumber, 1.0f * padNumber / dProjectSampleRate * 1000);
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
			
			if (bRealTimeModel && lPrefixLength> iHopSize) {
				auto s1f = *lastVoiceSampleForCrossFadeVector;
				// ��s1f���hop�����룬���ײ��޼������޼���СΪS1f_skip_more_skip
				//auto s1fMatchHopSize = floor(1.0 * s1f.size() / iHopSize) * iHopSize;
				//int S1f_skip_more_skip = s1f.size() - s1fMatchHopSize;
				//S1f_skip += S1f_skip_more_skip;
				//s1f = std::vector<float>(s1f.end() - s1fMatchHopSize, s1f.end());

				auto lCrossFadeLength = s1f.size();
				auto s2 = std::vector<float>(currentVoiceVector.begin() + lOverlap3, currentVoiceVector.end());

				auto s2a = std::vector<float>(s2.begin(), s2.begin() + lCrossFadeLength);
				auto s2b = std::vector<float>(s2.begin() + lCrossFadeLength, s2.end());
				//auto s3 = my_crossfade(s1f, s2a);
				auto s3 = hanning_crossfade(s1f, s2a, hanningWindow);

				/*
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
				}*/
				for (int i = 0; i < s2b.size(); i++) {
					s3.push_back(s2b.at(i));
				};
				processedSampleVector = s3;

				if (*bEnableDebug) {
					snprintf(buff, sizeof(buff), "fOverlap1:%.0fms\tfOverlap2:%.0fms\tfOverlap3:%.0fms\tlCrossFadeLength:%d\n",
						fOverlap1 * 1000, fOverlap2 * 1000, fOverlap3 * 1000, lCrossFadeLength);
					OutputDebugStringA(buff);
				}
			};
			int processedSampleNumber = processedSampleVector.size();
			int outputSampleNumber = processedSampleNumber - lOverlap2;

			// ��дָ���ǵĻ�����λ�ÿ�ʼд���µ���Ƶ����
			long lTmpModelOutputSampleBufferWritePos = *lModelOutputSampleBufferWritePos;
			// Ԥ��һ���������������浭������д��ʵʱ�����������Ϊoverlap2
			for (int i = 0; i < outputSampleNumber; i++) {
				// ע�⣬��Ϊ���������Ӧ�������ܵĴ����Դ˴�������дָ��׷�϶�ָ������
				fModeulOutputSampleBuffer[lTmpModelOutputSampleBufferWritePos++] = processedSampleVector.at(i);
				if (lTmpModelOutputSampleBufferWritePos == lModelOutputBufferSize) {
					lTmpModelOutputSampleBufferWritePos = 0;
				}
			};
			// ����ǰ���ӵ�β��lOverlap2���ȱ��浽�����һ�仺�����������������浭������ʹ��
			lastVoiceSampleForCrossFadeVector->clear();
			for (int i = outputSampleNumber; i < processedSampleNumber; i++) {
				lastVoiceSampleForCrossFadeVector->push_back(processedSampleVector.at(i));
			};
			// ��дָ��ָ���µ�λ��
			*lModelOutputSampleBufferWritePos = lTmpModelOutputSampleBufferWritePos;

			if (*bEnableDebug) {
				snprintf(buff, sizeof(buff), "���������:%d��ʱ��:%.1fms���������ڵ�����������%d��ʱ��:%.1fms\n", 
					outputSampleNumber,
					1.0f * outputSampleNumber / dProjectSampleRate * 1000,
					lOverlap2, 
					1.0f * lOverlap2 / dProjectSampleRate * 1000);
				OutputDebugStringA(buff);
			}

			tUseTime = func_get_timestamp() - tStart;
			if (*bEnableDebug) {
				snprintf(buff, sizeof(buff), "�ô�woker��ѵ�ܺ�ʱ:%lldms\n", tUseTime);
				OutputDebugStringA(buff);
			}
		}
	}
	mWorkerSafeExit->unlock();
}

