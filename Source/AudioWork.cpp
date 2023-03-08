#include "PluginProcessor.h"
#include "AudioFile.h"
#include "httplib.h"
#include "AudioWork.h"

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

// 用于计算一个读写缓存里的有效数据大小
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

// bool值序列化为字符串
std::string func_bool_to_string(bool bVal) {
	if (bVal) {
		return "true";
	}
	else {
		return "false";
	}
}


// 进行声音处理，较为耗时，在单独的线程里进行，避免主线程卡顿爆音
void func_do_voice_transfer_worker(
	int iNumberOfChanel,					// 通道数量
	double dProjectSampleRate,				// 项目采样率

	std::vector<INPUT_JOB_STRUCT>* modelInputJobList, // 模型输入队列
	std::mutex* modelInputJobListMutex,		// 模型输入队列锁

	long lModelOutputBufferSize,			// 模型输出缓冲区大小
	float* fModeulOutputSampleBuffer,		// 模型输出缓冲区
	long* lModelOutputSampleBufferReadPos,	// 模型输出缓冲区读指针
	long* lModelOutputSampleBufferWritePos,	// 模型输出缓冲区写指针

	std::mutex* lastVoiceSampleForCrossFadeVectorMutex,
	std::vector<float>* lastVoiceSampleForCrossFadeVector, // 最后一条模型输出音频的尾部，用于交叉淡化处理
	int* lastVoiceSampleCrossFadeSkipNumber,

	float* fPrefixLength,					// 前导缓冲区时长(s)
	float* fDropSuffixLength,				// 丢弃的尾部时长(s)
	float* fPitchChange,					// 音调变化数值
	bool* bCalcPitchError,					// 启用音调误差检测

	std::vector<roleStruct> roleStructList,	// 配置的可用音色列表
	int* iSelectRoleIndex,					// 选择的角色ID
	FUNC_SRC_SIMPLE dllFuncSrcSimple,		// DLL内部SrcSimple方法

	bool* bEnableSOVITSPreResample,			// 启用SOVITS模型入参音频重采样预处理
	int iSOVITSModelInputSamplerate,		// SOVITS模型入参采样率
	bool* bEnableHUBERTPreResample,			// 启用HUBERT模型入参音频重采样预处理
	int iHUBERTInputSampleRate,				// HUBERT模型入参采样率

	bool* bRealTimeModel,					// 占位符，实时模式
	bool* bEnableDebug,						// 占位符，启用DEBUG输出
	juce::Value vServerUseTime,				// UI变量，显示服务调用耗时
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
		// 轮训检查标志位
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
				// 这个输入块是静音的，直接准备相等长度的静音输出
				currentVoiceVector.clear();
				for (int i = 0; i < jobStruct.emptySampleNumber; i++) {
					currentVoiceVector.push_back(0.f);
				}
				currentVoiceSampleNumber = jobStruct.emptySampleNumber;
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
					OutputDebugStringA("算法模型\n");
				}*/
				auto res = cli.Post("/voiceChangeModel", items);

				tTime2 = func_get_timestamp();
				tUseTime = tTime2 - tTime1;
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
					snprintf(buff, sizeof(buff), "模型输出样本数不足，补充静音长度:%d，时长 : %.1fms\n", padNumber, 1.0f * padNumber / dProjectSampleRate * 1000);
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
				// 对s1f针对hop做对齐，从首部修剪它，修剪大小为S1f_skip_more_skip
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
						// 如果当前为静音块，则不需要对上一句结尾做交叉淡化
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

			// 从写指针标记的缓冲区位置开始写入新的音频数据
			long lTmpModelOutputSampleBufferWritePos = *lModelOutputSampleBufferWritePos;
			// 预留一部分数据用作交叉淡化（不写入实时输出），长度为overlap2
			for (int i = 0; i < processedSampleNumber - lOverlap2; i++) {
				// 注意，因为输出缓冲区应当尽可能的大，所以此处不考虑写指针追上读指针的情况
				fModeulOutputSampleBuffer[lTmpModelOutputSampleBufferWritePos++] = processedSampleVector.at(i);
				if (lTmpModelOutputSampleBufferWritePos == lModelOutputBufferSize) {
					lTmpModelOutputSampleBufferWritePos = 0;
				}
			};
			// 将当前句子的尾部lOverlap2长度保存到“最后一句缓冲区”，供后续交叉淡化流程使用
			//lastVoiceSampleForCrossFadeVectorMutex->lock();
			lastVoiceSampleForCrossFadeVector->clear();
			*lastVoiceSampleCrossFadeSkipNumber = 0;
			for (int i = currentVoiceSampleNumber - lOverlap2; i < currentVoiceSampleNumber; i++) {
				lastVoiceSampleForCrossFadeVector->push_back(currentVoiceVector.at(i));
			};
			// 将写指针指向新的位置
			*lModelOutputSampleBufferWritePos = lTmpModelOutputSampleBufferWritePos;
			lastVoiceSampleForCrossFadeVectorMutex->unlock();

			if (*bEnableDebug) {
				snprintf(buff, sizeof(buff), "输出样本数:%d，时长:%.1fms，保留用于淡化样本数：%d，时长:%.1fms\n", 
					processedSampleNumber, 
					1.0f * processedSampleNumber / dProjectSampleRate * 1000,
					lOverlap2, 
					1.0f * lOverlap2 / dProjectSampleRate * 1000);
				OutputDebugStringA(buff);
			}

			tUseTime = func_get_timestamp() - tStart;
			/*if (*bEnableDebug) {
				snprintf(buff, sizeof(buff), "该次woker轮训总耗时:%lld\n", tUseTime);
				OutputDebugStringA(buff);
			}*/
		}
	}
	mWorkerSafeExit->unlock();
}
