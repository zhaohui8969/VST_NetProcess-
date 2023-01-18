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

	long lModelInputOutputBufferSize,		// 模型输入输出缓冲区大小
	float* fModeulInputSampleBuffer,		// 模型输入缓冲区
	long* lModelInputSampleBufferReadPos,	// 模型输入缓冲区读指针
	long* lModelInputSampleBufferWritePos,	// 模型输入缓冲区写指针

	float* fModeulOutputSampleBuffer,		// 模型输出缓冲区
	long* lModelOutputSampleBufferReadPos,	// 模型输出缓冲区读指针
	long* lModelOutputSampleBufferWritePos,	// 模型输出缓冲区写指针

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
	bool* bDoItSignal,						// 占位符，表示该worker有待处理的数据
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
		if (*bDoItSignal) {
			// 有需要处理的信号，开始处理，并将标志位设置为false
			// 此处考虑到性能问题，并未使用互斥锁实现原子操作
			// 若同一时间产生了需要处理的信号，则等到下一个标志位为true时再处理也无妨
			*bDoItSignal = false;
			tStart = func_get_timestamp();
			tTime1 = tStart;


			roleStruct roleStruct = roleStructList[*iSelectRoleIndex];

			// 保存音频数据到文件
			// 获取当前写指针的位置
			long lTmpModelInputSampleBufferWritePos = *lModelInputSampleBufferWritePos;

			AudioFile<double>::AudioBuffer modelInputAudioBuffer;
			modelInputAudioBuffer.resize(iNumberOfChanel);

			// 从队列中取出所需的音频数据
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
			// 读取完毕，将读指针指向最后写指针
			*lModelInputSampleBufferReadPos = lTmpModelInputSampleBufferWritePos;
			float readLength = 1.0 * vModelInputSampleBufferVector.size() / 44100;

			if (*bEnableSOVITSPreResample) {
				// 提前对音频重采样，C++重采样比Python端快
				dSOVITSInputSamplerate = iSOVITSModelInputSamplerate;

				// SOVITS输入音频重采样
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
					// HUBERT输入音频重采样
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
			if (*bEnableDebug) {
				snprintf(buff, sizeof(buff), "准备保存到音频文件耗时:%lldms\n", tUseTime);
				OutputDebugStringA(buff);
			}
			tTime1 = tTime2;

			// 保存音频文件到内存
			std::vector<uint8_t> vModelInputMemoryBuffer;
			audioFile.saveToWaveMemory(&vModelInputMemoryBuffer);

			tTime2 = func_get_timestamp();
			tUseTime = tTime2 - tTime1;
			if (*bEnableDebug) {
				snprintf(buff, sizeof(buff), "保存到音频文件耗时:%lldms\n", tUseTime);
				OutputDebugStringA(buff);
			}
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
			if (*bEnableDebug) {
				OutputDebugStringA("算法模型\n");
			}
			auto res = cli.Post("/voiceChangeModel", items);

			tTime2 = func_get_timestamp();
			tUseTime = tTime2 - tTime1;
			vServerUseTime.setValue(juce::String(tUseTime) + "ms");
			if (*bEnableDebug) {
				snprintf(buff, sizeof(buff), "调用HTTP接口耗时:%lldms\n", tUseTime);
				OutputDebugStringA(buff);
			}
			tTime1 = tTime2;

			if (res.error() == httplib::Error::Success && res->status == 200) {
				// 调用成功，开始将结果放入到临时缓冲区，并替换输出
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

				// 跳过前导音频信号
				int iSkipSamplePosStart = static_cast<int>(*fPrefixLength * sampleRate);
				// 丢弃尾部信号
				int iSkipSamplePosEnd = static_cast<int>(numSamples - (*fDropSuffixLength * sampleRate));
				int iSliceSampleNumber = iSkipSamplePosEnd - iSkipSamplePosStart;
				float* fSliceSampleBuffer = (float*)(std::malloc(sizeof(float) * iSliceSampleNumber));
				int iSlicePos = 0;
				for (int i = iSkipSamplePosStart; i < iSkipSamplePosEnd; i++) {
					fSliceSampleBuffer[iSlicePos++] = static_cast<float>(fOriginAudioBuffer[i]);
				}

				// 音频重采样
				float* fReSampleInBuffer = (float*)malloc(iSliceSampleNumber * sizeof(float));
				float* fReSampleOutBuffer = fReSampleInBuffer;
				int iResampleNumbers = iSliceSampleNumber;
				for (int i = 0; i < iSliceSampleNumber; i++) {
					fReSampleInBuffer[i] = static_cast<float>(fSliceSampleBuffer[i]);
				}
				if (sampleRate != dProjectSampleRate) {
					double fScaleRate = dProjectSampleRate / sampleRate;
					iResampleNumbers = static_cast<int>(fScaleRate * iSliceSampleNumber);
					fReSampleOutBuffer = (float*)(std::malloc(sizeof(float) * iResampleNumbers));
					func_audio_resample(dllFuncSrcSimple, fReSampleInBuffer, fReSampleOutBuffer, fScaleRate, iSliceSampleNumber, iResampleNumbers);
				}

				tTime2 = func_get_timestamp();
				tUseTime = tTime2 - tTime1;
				if (*bEnableDebug) {
					snprintf(buff, sizeof(buff), "对模型输出重采样耗时:%lldms\n", tUseTime);
					OutputDebugStringA(buff);
				}
				tTime1 = tTime2;

				// 当启用了实时模式时，缓冲区的写指针需要特殊处理
				// 例如：当出现延迟抖动时，接收到最新的数据时缓冲区还有旧数据，此时若直接丢弃数据，则声音有明显卡顿，因此设置了一个可以容忍的延迟抖动时长
				const long iAcceptableDelaySize = static_cast<long>(0.03f * sampleRate);
				if (*bRealTimeModel) {
					// 安全区大小，因为现在读写线程并非线程安全，因此这里设置一个安全区大小，避免数据出现问题
					const int iRealTimeModeBufferSafeZoneSize = 16;
					// 计算出新的写指针位置：
					// 1.当前旧数据大小 > 可容忍的延迟抖动，写指针前移定位在安全区尾部
					// 2.当前旧数据大小 < 可容忍的延迟抖动，写指针前移定位在旧数据尾部（无任何操作，保持不变）
					long inputBufferSize = func_cacl_read_write_buffer_data_size(lModelInputOutputBufferSize, *lModelOutputSampleBufferReadPos, *lModelOutputSampleBufferWritePos);
					if (inputBufferSize > iAcceptableDelaySize) {
						*lModelOutputSampleBufferWritePos = (*lModelOutputSampleBufferReadPos + iRealTimeModeBufferSafeZoneSize) % lModelInputOutputBufferSize;
						long lDropDataNumber = inputBufferSize - iRealTimeModeBufferSafeZoneSize;
						long lDropDataLength = 1000 * lDropDataNumber / sampleRate;
						vDropDataLength.setValue(juce::String(lDropDataLength) + "ms");
					}
					else {
						vDropDataLength.setValue(juce::String("0") + "ms");
					}
				}

				// 从写指针标记的缓冲区位置开始写入新的音频数据
				long lTmpModelOutputSampleBufferWritePos = *lModelOutputSampleBufferWritePos;

				for (int i = 0; i < iResampleNumbers; i++) {
					fModeulOutputSampleBuffer[lTmpModelOutputSampleBufferWritePos++] = fReSampleOutBuffer[i];
					if (lTmpModelOutputSampleBufferWritePos == lModelInputOutputBufferSize) {
						lTmpModelOutputSampleBufferWritePos = 0;
					}
					// 注意，因为缓冲区应当尽可能的大，所以此处不考虑写指针追上读指针的情况
				}
				// 将写指针指向新的位置
				*lModelOutputSampleBufferWritePos = lTmpModelOutputSampleBufferWritePos;
				if (*bEnableDebug) {
					snprintf(buff, sizeof(buff), "输出写指针:%ld\n", lTmpModelOutputSampleBufferWritePos);
					OutputDebugStringA(buff);
				}

				tTime2 = func_get_timestamp();
				tUseTime = tTime2 - tTime1;
				if (*bEnableDebug) {
					snprintf(buff, sizeof(buff), "写缓冲区耗时:%lldms\n", tUseTime);
					OutputDebugStringA(buff);
				}
				tTime1 = tTime2;
			}
			else {
				auto err = res.error();
				if (*bEnableDebug) {
					snprintf(buff, sizeof(buff), "算法服务错误:%d\n", err);
					OutputDebugStringA(buff);
				}
			}
			tUseTime = func_get_timestamp() - tStart;
			if (*bEnableDebug) {
				snprintf(buff, sizeof(buff), "该次woker轮训总耗时:%lld\n", tUseTime);
				OutputDebugStringA(buff);
			}
		}
	}
	mWorkerSafeExit->unlock();
}
