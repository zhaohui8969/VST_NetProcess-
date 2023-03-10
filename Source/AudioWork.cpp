#include "PluginProcessor.h"
#include "AudioFile.h"
#include "httplib.h"
#include "AudioWork.h"
#define M_PI 3.14159265358979323846

using namespace std::chrono;

long long func_get_timestamp() {
	return (duration_cast<milliseconds>(system_clock::now().time_since_epoch())).count();
}

// 重采样
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

// 重采样
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

// bool值序列化为字符串
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

vector<float> hanning_crossfade(const vector<float>& x1, const vector<float>& x2, const vector<float>& hanningWindow) {
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


// 进行声音处理，较为耗时，在单独的线程里进行，避免主线程卡顿爆音
void func_do_voice_transfer_worker(
	int iNumberOfChanel,					// 通道数量
	double dProjectSampleRate,				// 项目采样率

	std::vector<JOB_STRUCT>* modelInputJobList, // 模型输入队列
	juce::CriticalSection* modelInputJobListLock,		// 模型输入队列锁

	std::vector<JOB_STRUCT>* modelOutputJobList, // 模型输出队列
	juce::CriticalSection* modelOutputJobListLock,		// 模型输出队列锁

	long lCrossFadeLength,
	std::vector<float>* hanningWindow,

	float* fPitchChange,					// 音调变化数值
	bool* bCalcPitchError,					// 启用音调误差检测

	std::vector<roleStruct> roleStructList,	// 配置的可用音色列表
	int* iSelectRoleIndex,					// 选择的角色ID
	FUNC_SRC_SIMPLE dllFuncSrcSimple,		// DLL内部SrcSimple方法

	bool* bEnableSOVITSPreResample,			// 启用SOVITS模型入参音频重采样预处理
	int iSOVITSModelInputSamplerate,		// SOVITS模型入参采样率
	bool* bEnableHUBERTPreResample,			// 启用HUBERT模型入参音频重采样预处理
	int iHUBERTInputSampleRate,				// HUBERT模型入参采样率

	bool* bEnableDebug,						// 占位符，启用DEBUG输出
	juce::Value vServerUseTime,				// UI变量，显示服务调用耗时
	float *fServerUseTime,
	juce::Value vDropDataLength,			// UI变量，显示实时模式下丢弃的音频数据长度

	bool* bWorkerNeedExit,					// 占位符，表示worker线程需要退出
	std::mutex* mWorkerSafeExit				// 互斥锁，表示worker线程已经安全退出
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
	JOB_STRUCT jobStruct;
	long lPrefixLength;
	bool bRealTimeModel;

	// 最后一条模型输出音频，用于交叉淡化处理
	std::vector<float> lastOutputVoiceSample(44100 * 120);
	std::vector<float> lastOutputVoiceSampleForCrossFadeVector;

	mWorkerSafeExit->lock();
	while (!*bWorkerNeedExit) {
		// 轮训检查标志位
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		modelInputJobListLock->enter();
		auto queueSize = modelInputJobList->size();
		if (queueSize > 0) {
			jobStruct = modelInputJobList->at(0); 
			modelInputJobList->erase(modelInputJobList->begin());
		}
		modelInputJobListLock->exit();
		lPrefixLength = jobStruct.lPrefixLength;
		bRealTimeModel = jobStruct.bRealTimeModel;
		currentVoiceSampleNumber = jobStruct.modelInputSampleVector.size();

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
				// 这个输入块中增量部分是静音数据，无需调用模型，直接用上一次输出的声音
				currentVoiceVector.clear();
				for (int i = 0; i < currentVoiceSampleNumber; i++) {
					currentVoiceVector.push_back(0.f);
				}
				for (int i = 0;i < lPrefixLength; i++) {
					currentVoiceVector[i] = lastOutputVoiceSample[lastOutputVoiceSample.size() - lPrefixLength + i];
				}
				vModelInputSampleBufferVector = jobStruct.modelInputSampleVector;
			}
			else {
				vModelInputSampleBufferVector = jobStruct.modelInputSampleVector;

				if (*bEnableDebug) {
					snprintf(buff, sizeof(buff), "模型输入样本数:%lld，时长 : %.1fms\n", vModelInputSampleBufferVector.size(), 1.0f * vModelInputSampleBufferVector.size() / dProjectSampleRate * 1000);
					OutputDebugStringA(buff);
				}

				AudioFile<double>::AudioBuffer modelInputAudioBuffer;
				modelInputAudioBuffer.resize(iNumberOfChanel);

				if (*bEnableSOVITSPreResample) {
					// 提前对音频重采样，C++重采样比Python端快
					dSOVITSInputSamplerate = iSOVITSModelInputSamplerate;

					// SOVITS输入音频重采样
					auto ReSampleOutputVector = func_audio_resample_vector_version(vModelInputSampleBufferVector, dProjectSampleRate, iSOVITSModelInputSamplerate, dllFuncSrcSimple);
					long iResampleNumbers = ReSampleOutputVector.size();

					snprintf(sSOVITSSamplerateBuff, sizeof(sSOVITSSamplerateBuff), "%d", iSOVITSModelInputSamplerate);
					modelInputAudioBuffer[0].resize(iResampleNumbers);
					for (int i = 0; i < ReSampleOutputVector.size(); i++) {
						modelInputAudioBuffer[0][i] = ReSampleOutputVector[i];
					}

					if (*bEnableHUBERTPreResample) {
						// HUBERT输入音频重采样
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

						// 保存音频文件到内存
						std::vector<uint8_t> vHUBERTModelInputMemoryBuffer;
						HUBERTAudioFile.saveToWaveMemory(&vHUBERTModelInputMemoryBuffer);

						// 从内存读取数据
						auto vHUBERTModelInputData = vHUBERTModelInputMemoryBuffer.data();
						std::string sHUBERTModelInputString(vHUBERTModelInputData, vHUBERTModelInputData + vHUBERTModelInputMemoryBuffer.size());
						sHUBERTSampleBuffer = sHUBERTModelInputString;
					}
					else {
						sHUBERTSampleBuffer = "";
					}
				}
				else {
					// 未开启预处理重采样，音频原样发送
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
					snprintf(buff, sizeof(buff), "准备保存到音频文件耗时:%lldms\n", tUseTime);
					OutputDebugStringA(buff);
				}*/
				tTime1 = tTime2;

				// 保存音频文件到内存
				std::vector<uint8_t> vModelInputMemoryBuffer;
				audioFile.saveToWaveMemory(&vModelInputMemoryBuffer);

				tTime2 = func_get_timestamp();
				tUseTime = tTime2 - tTime1;
				/*if (*bEnableDebug) {
					snprintf(buff, sizeof(buff), "保存到音频文件耗时:%lldms\n", tUseTime);
					OutputDebugStringA(buff);
				}*/
				tTime1 = tTime2;

				// 调用AI模型进行声音处理
				httplib::Client cli(roleStruct.sApiUrl);

				cli.set_connection_timeout(0, 1000000); // 300 milliseconds
				cli.set_read_timeout(5, 0); // 5 seconds
				cli.set_write_timeout(5, 0); // 5 seconds

				// 从内存读取数据
				auto vModelInputData = vModelInputMemoryBuffer.data();
				std::string sModelInputString(vModelInputData, vModelInputData + vModelInputMemoryBuffer.size());

				// 准备HTTP请求参数
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
					OutputDebugStringA("算法模型\n");
				}*/
				auto res = cli.Post("/voiceChangeModel", items);

				tTime2 = func_get_timestamp();
				tUseTime = tTime2 - tTime1;
				*fServerUseTime = tUseTime / 1000.0f;
				vServerUseTime.setValue(juce::String(tUseTime) + "ms");
				/*if (*bEnableDebug) {
					snprintf(buff, sizeof(buff), "调用HTTP接口耗时:%lldms\n", tUseTime);
					OutputDebugStringA(buff);
				}*/
				tTime1 = tTime2;

				if (res.error() == httplib::Error::Success && res->status == 200) {
					// 调用成功，开始将结果放入到临时缓冲区，并替换输出
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

					// 音频重采样
					currentVoiceVector = func_audio_resample_vector_version(currentVoiceVectorUnResample, sampleRate, dProjectSampleRate, dllFuncSrcSimple);
					currentVoiceSampleNumber = currentVoiceVector.size();
					/*if (*bEnableDebug) {
						snprintf(buff, sizeof(buff), "currentVoiceSampleNumber:%d\n", currentVoiceSampleNumber);
						OutputDebugStringA(buff);
					}*/
				}
				else {
					// 出现错误准备相等长度的静音输出
					currentVoiceVector.clear();
					for (int i = 0; i < jobStruct.modelInputSampleVector.size(); i++) {
						currentVoiceVector.push_back(0.f);
					}
					currentVoiceSampleNumber = currentVoiceVector.size();
					auto err = res.error();
					if (*bEnableDebug) {
						snprintf(buff, sizeof(buff), "算法服务错误:%d\n", err);
						OutputDebugStringA(buff);
					}
				}
			};

			if (*bEnableDebug) {
				snprintf(buff, sizeof(buff), "模型输出样本数:%d，时长 : %.1fms\n", currentVoiceSampleNumber, 1.0f * currentVoiceSampleNumber / dProjectSampleRate * 1000);
				OutputDebugStringA(buff);
			}

			if (currentVoiceSampleNumber < vModelInputSampleBufferVector.size()) {
				int padNumber = vModelInputSampleBufferVector.size() - currentVoiceSampleNumber;
				for (int i = 0;i < padNumber;i++) {
					currentVoiceVector.push_back(0.f);
				}
				currentVoiceSampleNumber = currentVoiceVector.size();

				if (*bEnableDebug) {
					snprintf(buff, sizeof(buff), "模型输出样本数不足，补充静音长度:%d，时长 : %.0fms\n", padNumber, 1.0f * padNumber / dProjectSampleRate * 1000);
					OutputDebugStringA(buff);
				}
			}
			else if (currentVoiceSampleNumber > vModelInputSampleBufferVector.size()) {
				int removeNumber = currentVoiceSampleNumber - vModelInputSampleBufferVector.size();
				for (int i = 0;i < removeNumber;i++) {
					currentVoiceVector.erase(currentVoiceVector.begin());
				}
				currentVoiceSampleNumber = currentVoiceVector.size();

				if (*bEnableDebug) {
					snprintf(buff, sizeof(buff), "模型输出样本数过多，切除首部:%d，时长 : %.0fms\n", removeNumber, 1.0f * removeNumber / dProjectSampleRate * 1000);
					OutputDebugStringA(buff);
				}
			}

			// 交叉淡化，缓解前后两个音频衔接的破音
			// 交叉淡化算法
			// 上一个音频为S1，当前音频为S2，重合时间为Overlap1，交叉淡化时间为overlap2，overlap3 = overlap1 - overlap2
			// 重合部分作用有两个：1.使得模型能获取到足够长的输入，不会出现奇怪的声音；2.重叠部分用来交叉淡化，避免破音
			// S1的尾部overlap1部分和S2的overlap1部分是能够对齐的，取overlap1部分尾部的overlap2部分做交叉淡化
			// 处理逻辑：
			// 1.上一个音频S1，只预留了S1f部分，此部分为S1的尾部overlap2长度
			// 2.S1f为了避免VST空输出，可能已经预支一部分，称之为S1f_skip，我们将S1f前S1f_skip部分丢掉，丢掉后的S1f长度为lCrossFadeLength，注意：丢掉的过程发生在processor.cpp中
			// 3.将S2的前overlap3部分跳过，然后再跳过S1f_skip部分，此时S2的剩余部分和S1f是对齐的，在此部分做交叉淡化，S2剩余部分S3b和交叉淡化S3a部分共同组成了S3
			// 4.S3尾部overlap2部分预留用作下一次交叉淡化，剩余部分直接输出到VST输出
			
			std::vector<float> processedSampleVector = currentVoiceVector;
			
			if (bRealTimeModel && lPrefixLength> iHopSize) {
				lastOutputVoiceSampleForCrossFadeVector = std::vector<float>(lastOutputVoiceSample.end() - lCrossFadeLength, lastOutputVoiceSample.end());

				// 对s1f针对hop做对齐，从首部修剪它，修剪大小为S1f_skip_more_skip
				//auto s1fMatchHopSize = floor(1.0 * s1f.size() / iHopSize) * iHopSize;
				//int S1f_skip_more_skip = s1f.size() - s1fMatchHopSize;
				//S1f_skip += S1f_skip_more_skip;
				//s1f = std::vector<float>(s1f.end() - s1fMatchHopSize, s1f.end());

				auto s2 = std::vector<float>(currentVoiceVector.begin() + lOverlap3, currentVoiceVector.end());

				auto s2a = std::vector<float>(s2.begin(), s2.begin() + lCrossFadeLength);
				auto s2b = std::vector<float>(s2.begin() + lCrossFadeLength, s2.end());
				//auto s3 = my_crossfade(s1f, s2a);
				auto s3 = hanning_crossfade(lastOutputVoiceSampleForCrossFadeVector, s2a, *hanningWindow);

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

			/*
			// 从写指针标记的缓冲区位置开始写入新的音频数据
			long lTmpModelOutputSampleBufferWritePos = *lModelOutputSampleBufferWritePos;
			// 预留一部分数据用作交叉淡化（不写入实时输出），长度为overlap2
			for (int i = 0; i < outputSampleNumber; i++) {
				// 注意，因为输出缓冲区应当尽可能的大，所以此处不考虑写指针追上读指针的情况
				fModeulOutputSampleBuffer[lTmpModelOutputSampleBufferWritePos++] = processedSampleVector.at(i);
				if (lTmpModelOutputSampleBufferWritePos == lModelOutputBufferSize) {
					lTmpModelOutputSampleBufferWritePos = 0;
				}
			};

			// 将写指针指向新的位置
			*lModelOutputSampleBufferWritePos = lTmpModelOutputSampleBufferWritePos;
			*/

			jobStruct.modelOutputSampleVector = std::vector<float>(processedSampleVector.begin(), processedSampleVector.begin() + outputSampleNumber);
			jobStruct.lSuffixlOverlap2 = lOverlap2;
			modelOutputJobListLock->enter();
			modelOutputJobList->push_back(jobStruct);
			modelOutputJobListLock->exit();

			// 保留当前句子的输出，供后续交叉淡化流程使用
			lastOutputVoiceSample = currentVoiceVector;

			if (*bEnableDebug) {
				snprintf(buff, sizeof(buff), "输出样本数:%d，时长:%.1fms，保留用于淡化样本数：%d，时长:%.1fms\n", 
					outputSampleNumber,
					1.0f * outputSampleNumber / dProjectSampleRate * 1000,
					lOverlap2, 
					1.0f * lOverlap2 / dProjectSampleRate * 1000);
				OutputDebugStringA(buff);
			}

			tUseTime = func_get_timestamp() - tStart;
			if (*bEnableDebug) {
				snprintf(buff, sizeof(buff), "该次woker轮训总耗时:%lldms\n", tUseTime);
				OutputDebugStringA(buff);
			}
		}
	}
	mWorkerSafeExit->unlock();
}

