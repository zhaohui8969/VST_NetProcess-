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

	std::vector<std::vector<float>>* modelInputJobList, // 模型输入队列
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

	mWorkerSafeExit->lock();
	while (!*bWorkerNeedExit) {
		// 轮训检查标志位
		std::this_thread::sleep_for(std::chrono::milliseconds(10));

		modelInputJobListMutex->lock();
		auto queueSize = modelInputJobList->size();
		if (*bEnableDebug) {
			snprintf(buff, sizeof(buff), "queueSize:%lld\n", queueSize);
			OutputDebugStringA(buff);
		}
		modelInputJobListMutex->unlock();
		if (queueSize > 0) {
			tStart = func_get_timestamp();
			tTime1 = tStart;
			modelInputJobListMutex->lock();
			auto vModelInputSampleBufferVector = modelInputJobList->at(0);
			modelInputJobList->erase(modelInputJobList->begin());
			modelInputJobListMutex->unlock();

			roleStruct roleStruct = roleStructList[*iSelectRoleIndex];

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
				std::vector<float> currentVoiceVector = func_audio_resample_vector_version(currentVoiceVectorUnResample, sampleRate, dProjectSampleRate, dllFuncSrcSimple);
				int currentVoiceSampleNumber = currentVoiceVector.size();

				// 交叉淡化，缓解前后两个音频衔接的破音
				// 交叉淡化算法
				// 上一个音频为S1，当前音频为S2，重合时间为overlap
				// S1 S2 的overlap部分做交叉淡化 + S2的后半段
				std::vector<float> s3;
				int overlapSampleNumber = static_cast<int>(*fPrefixLength * dProjectSampleRate);
				std::vector<float> processedSampleVector = currentVoiceVector;
				lastVoiceSampleForCrossFadeVectorMutex->lock();
				if (*bRealTimeModel && *fPrefixLength > 0.01f && lastVoiceSampleForCrossFadeVector->size() > 0) {
					s3.clear();
					int s1Length = lastVoiceSampleForCrossFadeVector->size();
					int s2Length = currentVoiceVector.size();
					
					int fixOverlapSampleNumber = std::min({ overlapSampleNumber, s1Length, s2Length - *lastVoiceSampleCrossFadeSkipNumber });
					float fStepAlpha = 1.0f / fixOverlapSampleNumber;

					int s1OverlapStartIndex = s1Length - fixOverlapSampleNumber;
					for (int i = 0; i < fixOverlapSampleNumber; i++) {
						float s1Sample = lastVoiceSampleForCrossFadeVector->at(i + s1OverlapStartIndex);
						// S1可能已经被预先取掉一部分，S2需要加上一个lastVoiceSampleCrossFadeSkipNumber偏移量，使得S1和S2能够对齐
						float s2Sample = currentVoiceVector[i + *lastVoiceSampleCrossFadeSkipNumber];
						float alpha = fStepAlpha * i;
						float s3Sample = s1Sample * (1.f - alpha) + s2Sample * alpha;
						s3.push_back(s3Sample);
					}
					for (int i = fixOverlapSampleNumber; i < s2Length; i++) {
						s3.push_back(currentVoiceVector[i]);
					};
					processedSampleVector = s3;
				};
				int processedSampleNumber = processedSampleVector.size();
				lastVoiceSampleForCrossFadeVectorMutex->unlock();

				// 当启用了实时模式时，缓冲区的写指针需要特殊处理
				// 例如：当出现延迟抖动时，接收到最新的数据时缓冲区还有旧数据，此时若直接丢弃数据，则声音有明显卡顿，因此设置了一个可以容忍的延迟抖动时长
				bool bDelayFix = true;
				const long iAcceptableDelaySize = static_cast<long>(0.03f * dProjectSampleRate);
				if (*bRealTimeModel && bDelayFix) {
					// 安全区大小，因为现在读写线程并非线程安全，因此这里设置一个安全区大小，避免数据出现问题
					const int iRealTimeModeBufferSafeZoneSize = 16;
					// 计算出新的写指针位置：
					// 1.当前旧数据大小 > 可容忍的延迟抖动，写指针前移定位在安全区尾部
					// 2.当前旧数据大小 < 可容忍的延迟抖动，写指针前移定位在旧数据尾部（无任何操作，保持不变）
					long inputBufferSize = func_cacl_read_write_buffer_data_size(lModelOutputBufferSize, *lModelOutputSampleBufferReadPos, *lModelOutputSampleBufferWritePos);
					if (inputBufferSize > iAcceptableDelaySize) {
						*lModelOutputSampleBufferWritePos = (*lModelOutputSampleBufferReadPos + iRealTimeModeBufferSafeZoneSize) % lModelOutputBufferSize;
						long lDropDataNumber = inputBufferSize - iRealTimeModeBufferSafeZoneSize;
						long lDropDataLength = 1000 * lDropDataNumber / dProjectSampleRate;
						vDropDataLength.setValue(juce::String(lDropDataLength) + "ms");
					}
					else {
						vDropDataLength.setValue(juce::String("0") + "ms");
					}
				}

				// 从写指针标记的缓冲区位置开始写入新的音频数据
				long lTmpModelOutputSampleBufferWritePos = *lModelOutputSampleBufferWritePos;
				// 预留一部分数据用作交叉淡化（不写入实时输出）
				// 计算预留数据长度
				overlapSampleNumber = 0;
				if (*bRealTimeModel && (* fPrefixLength > 0.01f)) {
					overlapSampleNumber = static_cast<int>(*fPrefixLength * dProjectSampleRate);
					overlapSampleNumber = std::min(overlapSampleNumber, processedSampleNumber);
				};
				for (int i = 0; i < processedSampleNumber - overlapSampleNumber; i++) {
					// 注意，因为输出缓冲区应当尽可能的大，所以此处不考虑写指针追上读指针的情况
					fModeulOutputSampleBuffer[lTmpModelOutputSampleBufferWritePos++] = processedSampleVector.at(i);
					if (lTmpModelOutputSampleBufferWritePos == lModelOutputBufferSize) {
						lTmpModelOutputSampleBufferWritePos = 0;
					}
				};
				// 将当前句子的后半段保存到“最后一句缓冲区”，供后续交叉淡化流程使用
				lastVoiceSampleForCrossFadeVectorMutex->lock();
				lastVoiceSampleForCrossFadeVector->clear();
				*lastVoiceSampleCrossFadeSkipNumber = 0;
				for (int i = currentVoiceSampleNumber - overlapSampleNumber; i < currentVoiceSampleNumber; i++) {
					lastVoiceSampleForCrossFadeVector->push_back(currentVoiceVector.at(i));
				};
				// 将写指针指向新的位置
				*lModelOutputSampleBufferWritePos = lTmpModelOutputSampleBufferWritePos;
				lastVoiceSampleForCrossFadeVectorMutex->unlock();
			}
			else {
				auto err = res.error();
				/*if (*bEnableDebug) {
					snprintf(buff, sizeof(buff), "算法服务错误:%d\n", err);
					OutputDebugStringA(buff);
				}*/
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
