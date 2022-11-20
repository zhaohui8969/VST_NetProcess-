//------------------------------------------------------------------------
// Copyright(c) 2022 natas.
//------------------------------------------------------------------------

#include "ss_processor.h"
#include "ss_cids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "AudioFile.h"
#include "httplib.h"
#include <numeric>
#include <Query.h>
#include "params.h"
#include "json/json.h"
#include <windows.h>
#include <filesystem>
#include <chrono>
using namespace std::chrono;

long long func_get_timestamp() {
	return (duration_cast<milliseconds>(system_clock::now().time_since_epoch())).count();
}

using namespace Steinberg;

namespace MyCompanyName {
//------------------------------------------------------------------------
// NetProcessProcessor
//------------------------------------------------------------------------

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
		} else {
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

		bool* bDisableVolumeDetect,				// 占位符，停用音量检测（持续处理模式）
		bool* bFoundJit,						// 占位符，是否出现了Jitter问题
		float fAvoidJitPrefixTime,				// Jitter后，增加的前导缓冲区长度(s)
		bool* bDoItSignal						// 占位符，表示该worker有待处理的数据
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

	while (true) {
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


			if (*bEnableSOVITSPreResample) {
				// 提前对音频重采样，C++重采样比Python端快
				dSOVITSInputSamplerate = iSOVITSModelInputSamplerate;
				
				// SOVITS输入音频重采样
				float* fReSampleInBuffer = vModelInputSampleBufferVector.data();
				float* fReSampleOutBuffer = fReSampleInBuffer;
				int iResampleNumbers = vModelInputSampleBufferVector.size();

				if (dProjectSampleRate != iSOVITSModelInputSamplerate) {
					double fScaleRate = iSOVITSModelInputSamplerate / dProjectSampleRate;
					iResampleNumbers = fScaleRate * iResampleNumbers;
					fReSampleOutBuffer = (float*)(std::malloc(sizeof(float) * iResampleNumbers));
					func_audio_resample(dllFuncSrcSimple, fReSampleInBuffer, fReSampleOutBuffer, fScaleRate, vModelInputSampleBufferVector.size(), iResampleNumbers);
				}

				snprintf(sSOVITSSamplerateBuff, sizeof(sSOVITSSamplerateBuff), "%d", iSOVITSModelInputSamplerate);
				modelInputAudioBuffer[0].resize(iResampleNumbers);
				for (int i = 0; i < iResampleNumbers; i++) {
					modelInputAudioBuffer[0][i] = fReSampleOutBuffer[i];
				}

				if (*bEnableHUBERTPreResample) {
					// HUBERT输入音频重采样
					double fScaleRate = iHUBERTInputSampleRate / dProjectSampleRate;
					iResampleNumbers = fScaleRate * vModelInputSampleBufferVector.size();
					fReSampleOutBuffer = (float*)(std::malloc(sizeof(float) * iResampleNumbers));
					func_audio_resample(dllFuncSrcSimple, fReSampleInBuffer, fReSampleOutBuffer, fScaleRate, vModelInputSampleBufferVector.size(), iResampleNumbers);

					AudioFile<double>::AudioBuffer HUBERTModelInputAudioBuffer;
					HUBERTModelInputAudioBuffer.resize(iNumberOfChanel);
					HUBERTModelInputAudioBuffer[0].resize(iResampleNumbers);
					for (int i = 0; i < iResampleNumbers; i++) {
						HUBERTModelInputAudioBuffer[0][i] = fReSampleOutBuffer[i];
					}
					AudioFile<double> HUBERTAudioFile;
					HUBERTAudioFile.shouldLogErrorsToConsole(false);
					HUBERTAudioFile.setAudioBuffer(HUBERTModelInputAudioBuffer);
					HUBERTAudioFile.setAudioBufferSize(iNumberOfChanel, HUBERTModelInputAudioBuffer[0].size());
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

			int iModelInputNumSamples = modelInputAudioBuffer[0].size();
			AudioFile<double> audioFile;
			audioFile.shouldLogErrorsToConsole(false);
			audioFile.setAudioBuffer(modelInputAudioBuffer);
			audioFile.setAudioBufferSize(iNumberOfChanel, iModelInputNumSamples);
			audioFile.setBitDepth(24);
			audioFile.setSampleRate(dSOVITSInputSamplerate);
			
			tTime2 = func_get_timestamp();
			tUseTime = tTime2 - tTime1;
			snprintf(buff, sizeof(buff), "准备保存到音频文件耗时:%lldms\n", tUseTime);
			OutputDebugStringA(buff);
			tTime1 = tTime2;

			// 保存音频文件到内存
			std::vector<uint8_t> vModelInputMemoryBuffer;
			audioFile.saveToWaveMemory(&vModelInputMemoryBuffer);

			tTime2 = func_get_timestamp();
			tUseTime = tTime2 - tTime1;
			snprintf(buff, sizeof(buff), "保存到音频文件耗时:%lldms\n", tUseTime);
			OutputDebugStringA(buff);
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
			OutputDebugStringA("调用AI算法模型\n");
			auto res = cli.Post("/voiceChangeModel", items);

			tTime2 = func_get_timestamp();
			tUseTime = tTime2 - tTime1;
			snprintf(buff, sizeof(buff), "调用HTTP接口耗时:%lldms\n", tUseTime);
			OutputDebugStringA(buff);
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
				double lengthInSeconds = tmpAudioFile.getLengthInSeconds();
				// int numChannels = tmpAudioFile.getNumChannels();
				bool isMono = tmpAudioFile.isMono();

				// 音频流式处理
				// 做1s滑窗，但是丢掉最后0.1s再取最后的输出进行拼接
				
				// 跳过前导音频信号
				int iSkipSamplePosStart = *fPrefixLength * sampleRate;
				// 丢弃尾部信号
				int iSkipSamplePosEnd = numSamples - (*fDropSuffixLength * sampleRate);
				int iSliceSampleNumber = iSkipSamplePosEnd - iSkipSamplePosStart;
				float* fSliceSampleBuffer = (float*)(std::malloc(sizeof(float) * iSliceSampleNumber));
				int iSlicePos = 0;
				auto fOriginAudioBuffer = tmpAudioFile.samples[0];
				for (int i = iSkipSamplePosStart; i < iSkipSamplePosEnd; i++) {
					fSliceSampleBuffer[iSlicePos++] = fOriginAudioBuffer[i];
				}

				// 音频重采样
				float* fReSampleInBuffer = (float*)malloc(iSliceSampleNumber * sizeof(float));
				float* fReSampleOutBuffer = fReSampleInBuffer;
				int iResampleNumbers = iSliceSampleNumber;
				for (int i = 0; i < iSliceSampleNumber; i++) {
					fReSampleInBuffer[i] = fSliceSampleBuffer[i];
				}
				if (sampleRate != dProjectSampleRate) {
					double fScaleRate = dProjectSampleRate / sampleRate;
					iResampleNumbers = fScaleRate * iSliceSampleNumber;
					fReSampleOutBuffer = (float*)(std::malloc(sizeof(float) * iResampleNumbers));
					func_audio_resample(dllFuncSrcSimple, fReSampleInBuffer, fReSampleOutBuffer, fScaleRate, iSliceSampleNumber, iResampleNumbers);
				}

				tTime2 = func_get_timestamp();
				tUseTime = tTime2 - tTime1;
				snprintf(buff, sizeof(buff), "对模型输出重采样耗时:%lldms\n", tUseTime);
				OutputDebugStringA(buff);
				tTime1 = tTime2;

				long lTmpModelOutputSampleBufferWritePos = *lModelOutputSampleBufferWritePos;
				
				// 为了避免模型JIT，写一段静音的缓冲区
				if (*bFoundJit && *bDisableVolumeDetect) {
					*bFoundJit = false;
					int iRepeatSilenceSampleNumber = dProjectSampleRate * fAvoidJitPrefixTime;
					for (int i = 0; i < iRepeatSilenceSampleNumber; i++) {
						fModeulOutputSampleBuffer[lTmpModelOutputSampleBufferWritePos++] = 0.f;
						if (lTmpModelOutputSampleBufferWritePos == lModelInputOutputBufferSize) {
							lTmpModelOutputSampleBufferWritePos = 0;
						}
					}
				}
				for (int i = 0; i < iResampleNumbers; i++) {
					fModeulOutputSampleBuffer[lTmpModelOutputSampleBufferWritePos++] = fReSampleOutBuffer[i];
					if (lTmpModelOutputSampleBufferWritePos == lModelInputOutputBufferSize) {
						lTmpModelOutputSampleBufferWritePos = 0;
					}
					// 注意，因为缓冲区应当尽可能的大，所以此处不考虑写指针追上读指针的情况
				}
				// 将写指针指向新的位置
				*lModelOutputSampleBufferWritePos = lTmpModelOutputSampleBufferWritePos;
				snprintf(buff, sizeof(buff), "输出写指针:%ld\n", lTmpModelOutputSampleBufferWritePos);
				OutputDebugStringA(buff);

				tTime2 = func_get_timestamp();
				tUseTime = tTime2 - tTime1;
				snprintf(buff, sizeof(buff), "写缓冲区耗时:%lldms\n", tUseTime);
				OutputDebugStringA(buff);
				tTime1 = tTime2;
			}
			else {
				auto err = res.error();
				snprintf(buff, sizeof(buff), "算法服务错误:%d\n", err);
				OutputDebugStringA(buff);
			}
			tUseTime = func_get_timestamp() - tStart;
			snprintf(buff, sizeof(buff), "该次woker轮训总耗时:%lld\n", tUseTime);
			OutputDebugStringA(buff);
		}
	}
}

NetProcessProcessor::NetProcessProcessor()
	: kRecordState(IDLE)
	, fRecordIdleTime(0.f)
	// 默认只处理单声道
	, iNumberOfChanel(1)
	, bCalcPitchError(defaultEnabelPitchErrorCalc)
	, fMaxSliceLength(2.f)
	, fPitchChange(0.f)
	, dllFuncSrcSimple(nullptr){
	//--- set the wanted controller for our processor
	setControllerClass (kNetProcessControllerUID);
}

//------------------------------------------------------------------------
NetProcessProcessor::~NetProcessProcessor ()
{}

//------------------------------------------------------------------------
tresult PLUGIN_API NetProcessProcessor::initialize (FUnknown* context)
{
	std::wstring sDllDir = L"C:/Program Files/Common Files/VST3/NetProcess.vst3/Contents/x86_64-win";
	AddDllDirectory(sDllDir.c_str());
	sDllDir = L"C:/Windows/SysWOW64";
	AddDllDirectory(sDllDir.c_str());
	sDllDir = L"C:/temp/vst";
	AddDllDirectory(sDllDir.c_str());
	sDllDir = L"C:/temp/vst3";
	AddDllDirectory(sDllDir.c_str());

	std::wstring sDllPath = L"C:/Program Files/Common Files/VST3/NetProcess.vst3/Contents/x86_64-win/samplerate.dll";
	auto dllClient = LoadLibrary(sDllPath.c_str());
	if (dllClient != NULL) {
		dllFuncSrcSimple = (FUNC_SRC_SIMPLE)GetProcAddress(dllClient, "src_simple");
	}
	else {
		OutputDebugStringA("samplerate.dll load Error!");
	}

	// 读取JSON配置文件
	Json::Value jsonRoot;
	std::ifstream t_pc_file(sJsonConfigFileName, std::ios::binary);
	std::stringstream buffer_pc_file;
	buffer_pc_file << t_pc_file.rdbuf();
	buffer_pc_file >> jsonRoot;

	bDisableVolumeDetect = jsonRoot["bDisableVolumeDetect"].asBool();
	bEnableSOVITSPreResample = jsonRoot["bEnableSOVITSPreResample"].asBool();
	iSOVITSModelInputSamplerate = jsonRoot["iSOVITSModelInputSamplerate"].asInt();
	bEnableHUBERTPreResample = jsonRoot["bEnableHUBERTPreResample"].asBool();
	iHUBERTInputSampleRate = jsonRoot["iHUBERTInputSampleRate"].asInt();
	fAvoidJitPrefixTime = jsonRoot["fAvoidJitPrefixTime"].asFloat();
	fLowVolumeDetectTime = jsonRoot["fLowVolumeDetectTime"].asFloat();
	fSampleVolumeWorkActiveVal = jsonRoot["fSampleVolumeWorkActiveVal"].asDouble();
	fPrefixLength = jsonRoot["fPrefixLength"].asFloat();
	fDropSuffixLength = jsonRoot["fDropSuffixLength"].asFloat();

	roleList.clear();
	int iRoleSize = jsonRoot["roleList"].size();
	for (int i = 0; i < iRoleSize; i++) {
		std::string apiUrl = jsonRoot["roleList"][i]["apiUrl"].asString();
		std::string name = jsonRoot["roleList"][i]["name"].asString();
		std::string speakId = jsonRoot["roleList"][i]["speakId"].asString();
		roleStruct role;
		role.sSpeakId = speakId;
		role.sName = name;
		role.sApiUrl = apiUrl;
		roleList.push_back(role);
	}
	iSelectRoleIndex = 0;

	// 前导缓冲区缓存初始化
	lPrefixLengthSampleNumber = fPrefixLength * this->processSetup.sampleRate;
	fPrefixBuffer = (float*)std::malloc(sizeof(float) * lPrefixLengthSampleNumber);
	lPrefixBufferPos = 0;
	lNoOutputCount = 0;
	bFoundJit = true;
	bDoItSignal = false;

	// 初始化线程间交换数据的缓冲区，120s的缓冲区足够大
	float fModelInputOutputBufferSecond = 120.f;
	lModelInputOutputBufferSize = fModelInputOutputBufferSecond * this->processSetup.sampleRate;
	fModeulInputSampleBuffer = (float*)(std::malloc(sizeof(float) * lModelInputOutputBufferSize));
	fModeulOutputSampleBuffer = (float*)(std::malloc(sizeof(float) * lModelInputOutputBufferSize));
	lModelInputSampleBufferReadPos = 0;
	lModelInputSampleBufferWritePos = 0;
	lModelOutputSampleBufferReadPos = 0;
	lModelOutputSampleBufferWritePos = 0;

	// 启动Worker线程
	std::thread (func_do_voice_transfer_worker,
				iNumberOfChanel,					// 通道数量
				this->processSetup.sampleRate,		// 项目采样率

				lModelInputOutputBufferSize,		// 模型输入输出缓冲区大小
				fModeulInputSampleBuffer,			// 模型输入缓冲区
				&lModelInputSampleBufferReadPos,	// 模型输入缓冲区读指针
				&lModelInputSampleBufferWritePos,	// 模型输入缓冲区写指针

				fModeulOutputSampleBuffer,			// 模型输出缓冲区
				&lModelOutputSampleBufferReadPos,	// 模型输出缓冲区读指针
				&lModelOutputSampleBufferWritePos,	// 模型输出缓冲区写指针

				&fPrefixLength,						// 前导缓冲区时长(s)
				&fDropSuffixLength,					// 丢弃的尾部时长(s)
				&fPitchChange,						// 音调变化数值
				&bCalcPitchError,					// 启用音调误差检测

				roleList,							// 配置的可用音色列表
				&iSelectRoleIndex,					// 选择的角色ID
				dllFuncSrcSimple,					// DLL内部SrcSimple方法

				&bEnableSOVITSPreResample,			// 启用SOVITS模型入参音频重采样预处理
				iSOVITSModelInputSamplerate,		// SOVITS模型入参采样率
				& bEnableHUBERTPreResample,			// 启用HUBERT模型入参音频重采样预处理
				iHUBERTInputSampleRate,				// HUBERT模型入参采样率
				
				&bDisableVolumeDetect,				// 占位符，停用音量检测（持续处理模式）
				&bFoundJit,							// 占位符，是否出现了Jitter问题
				fAvoidJitPrefixTime,				// Jitter后，增加的前导缓冲区长度(s)
				&bDoItSignal						// 占位符，表示该worker有待处理的数据
				).detach();

	//---always initialize the parent-------
	tresult result = AudioEffect::initialize (context);
	// if everything Ok, continue
	if (result != kResultOk)
	{
		return result;
	}

	//--- create Audio IO ------
	addAudioInput (STR16 ("Stereo In"), Steinberg::Vst::SpeakerArr::kStereo);
	addAudioOutput (STR16 ("Stereo Out"), Steinberg::Vst::SpeakerArr::kStereo);

	/* If you don't need an event bus, you can remove the next line */
	addEventInput (STR16 ("Event In"), 1);

	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API NetProcessProcessor::terminate ()
{
	// Here the Plug-in will be de-instanciated, last possibility to remove some memory!
	
	//---do not forget to call parent ------
	return AudioEffect::terminate ();
}

//------------------------------------------------------------------------
tresult PLUGIN_API NetProcessProcessor::setActive (TBool state)
{
	//--- called when the Plug-in is enable/disable (On/Off) -----
	return AudioEffect::setActive (state);
}

//------------------------------------------------------------------------
tresult PLUGIN_API NetProcessProcessor::process (Vst::ProcessData& data)
{
	//--- First : Read inputs parameter changes-----------
	char buff[100];
	
	// 处理参数变化
	if (data.inputParameterChanges)
	{
		int32 numParamsChanged = data.inputParameterChanges->getParameterCount();
		for (int32 index = 0; index < numParamsChanged; index++)
		{
			if (auto* paramQueue = data.inputParameterChanges->getParameterData(index))
			{
				Vst::ParamValue value;
				int32 sampleOffset;
				int32 numPoints = paramQueue->getPointCount();
				paramQueue->getPoint(numPoints - 1, sampleOffset, value);
				switch (paramQueue->getParameterId())
				{
				case kEnabelPitchErrorCalc:
					OutputDebugStringA("kEnabelPitchErrorCalc\n");
					bCalcPitchError = (bool)value;
					break;
				case kMaxSliceLength:
					OutputDebugStringA("kMaxSliceLength\n");
					fMaxSliceLength = value * maxMaxSliceLength + 0.1f;
					lMaxSliceLengthSampleNumber = this->processSetup.sampleRate * fMaxSliceLength;
					break;
				case kPitchChange:
					OutputDebugStringA("kPitchChange\n");
					fPitchChange = value * (maxPitchChange - minPitchChange) + minPitchChange;
					break;
				case kSelectRole:
					OutputDebugStringA("kSelectRole\n");
					iSelectRoleIndex = std::min<int>(
						(int)(roleList.size() * value), roleList.size() - 1);
					break;
				}
			}
		}
	}

	//--- Here you have to implement your processing
	Vst::Sample32* inputL = data.inputs[0].channelBuffers32[0];
	//Vst::Sample32* inputR = data.inputs[0].channelBuffers32[1];
	Vst::Sample32* outputL = data.outputs[0].channelBuffers32[0];
	Vst::Sample32* outputR = data.outputs[0].channelBuffers32[1];
	double fSampleMax = -9999;
	for (int32 i = 0; i < data.numSamples; i++) {
		// 获取当前块的最大音量
		double fCurrentSample = inputL[i];
		double fSampleAbs = std::abs(fCurrentSample);
		if (fSampleAbs > fSampleMax) {
			fSampleMax = fSampleAbs;
		}

		// 将当前信号复制到前导信号缓冲区中
		fPrefixBuffer[lPrefixBufferPos++] = fCurrentSample;
		if (lPrefixBufferPos == lPrefixLengthSampleNumber) {
			lPrefixBufferPos = 0;
		}
	}
	lPrefixBufferPos = (lPrefixBufferPos + data.numSamples) % lPrefixLengthSampleNumber;
	
	if (bDisableVolumeDetect) {
		// 如果禁用了音量检测，则音量直接合格
		bVolumeDetectFine = true;
	}
	else {
		bVolumeDetectFine = fSampleMax >= fSampleVolumeWorkActiveVal;
	}
	 
	if (bVolumeDetectFine) {
		fRecordIdleTime = 0.f;
	}
	else {
		fRecordIdleTime += 1.f * data.numSamples / this->processSetup.sampleRate;
		char buff[100];
		snprintf(buff, sizeof(buff), "当前累积空闲时间:%f\n", fRecordIdleTime);
		OutputDebugStringA(buff);
	}

	if (kRecordState == IDLE) {
		// 当前是空闲状态
		if (bVolumeDetectFine) {
			OutputDebugStringA("切换到工作状态");
			kRecordState = WORK;
			// 将当前的音频数据写入到模型入参缓冲区中
			for (int i = lPrefixBufferPos; i < lPrefixLengthSampleNumber; i++) {
				fModeulInputSampleBuffer[lModelInputSampleBufferWritePos++] = fPrefixBuffer[i];
				if (lModelInputSampleBufferWritePos == lModelInputOutputBufferSize) {
					lModelInputSampleBufferWritePos = 0;
				}
			}
			for (int i = 0; i < lPrefixBufferPos; i++) {
				fModeulInputSampleBuffer[lModelInputSampleBufferWritePos++] = fPrefixBuffer[i];
				if (lModelInputSampleBufferWritePos == lModelInputOutputBufferSize) {
					lModelInputSampleBufferWritePos = 0;
				}
			}
		}
	}
	else {
		// 当前是工作状态
		// 将当前的音频数据写入到模型入参缓冲区中
		for (int i = 0; i < data.numSamples; i++) {
			fModeulInputSampleBuffer[lModelInputSampleBufferWritePos++] = inputL[i];
			if (lModelInputSampleBufferWritePos == lModelInputOutputBufferSize) {
				lModelInputSampleBufferWritePos = 0;
			}
		}

		// 判断是否需要退出工作状态
		bool bExitWorkState = false;

		// 退出条件1：音量过小且持续超过一定时间
		if (fRecordIdleTime >= fLowVolumeDetectTime) {
			bExitWorkState = true;
			OutputDebugStringA("音量过小且持续超过一定时间，直接调用模型\n");
		}

		// 退出条件2：队列达到一定的大小
		long inputBufferSize = func_cacl_read_write_buffer_data_size(lModelInputOutputBufferSize, lModelInputSampleBufferReadPos, lModelInputSampleBufferWritePos);
		if (inputBufferSize > lMaxSliceLengthSampleNumber + lPrefixLengthSampleNumber) {
			bExitWorkState = true;
			OutputDebugStringA("队列大小达到预期，直接调用模型\n");
		}

		if (bExitWorkState) {
			// 需要退出工作状态
			kRecordState = IDLE;
			// worker标志位设置为true，供worker检查
			bDoItSignal = true;
		}
	}

	// 如果模型输出缓冲区还有数据的话，写入到输出信号中去
	int channel = 0;
	bool bHasRightChanel = true;
	if (outputR == outputL || outputR == NULL) bHasRightChanel = false;
	snprintf(buff, sizeof(buff), "输出读指针:%ld\n", lModelOutputSampleBufferReadPos);
	OutputDebugStringA(buff);
	if (lModelOutputSampleBufferReadPos != lModelOutputSampleBufferWritePos) {
		bFoundJit = false;
		bool bFinish = false;
		for (int i = 0; i < data.numSamples; i++)
		{
			bFinish = lModelOutputSampleBufferReadPos == lModelOutputSampleBufferWritePos;
			if (bFinish) {
				outputL[i] = 0.f;
				if (bHasRightChanel) {
					outputR[i] = 0.f;
				}
			}
			else {
				double currentSample = fModeulOutputSampleBuffer[lModelOutputSampleBufferReadPos++];
				if (lModelOutputSampleBufferReadPos == lModelInputOutputBufferSize) {
					lModelOutputSampleBufferReadPos = 0;
				}
				outputL[i] = currentSample;
				if (bHasRightChanel) {
					outputR[i] = currentSample;
				}
			}
		}
		if (bFinish) {
			// 数据取完了
			OutputDebugStringA("数据取完了\n");
		}
	}
	else {
		bFoundJit = true;
		lNoOutputCount += 1;
		char buff[100];
		snprintf(buff, sizeof(buff), "!!!!!!!!!!!!!!!!!!!!!!!输出缓冲空:%ld\n", lNoOutputCount);
		std::string buffAsStdStr = buff;
		OutputDebugStringA(buff);
		for (int32 i = 0; i < data.numSamples; i++) {
			// 对输出静音
			outputL[i] = 0.0000000001f;
			if (bHasRightChanel) {
				outputR[i] = 0.0000000001f;
			}
		}
	}
	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API NetProcessProcessor::setupProcessing (Vst::ProcessSetup& newSetup)
{
	//--- called before any processing ----
	return AudioEffect::setupProcessing (newSetup);
}

//------------------------------------------------------------------------
tresult PLUGIN_API NetProcessProcessor::canProcessSampleSize (int32 symbolicSampleSize)
{
	// by default kSample32 is supported
	if (symbolicSampleSize == Vst::kSample32)
		return kResultTrue;

	// disable the following comment if your processing support kSample64
	/* if (symbolicSampleSize == Vst::kSample64)
		return kResultTrue; */

	return kResultFalse;
}

//------------------------------------------------------------------------
tresult PLUGIN_API NetProcessProcessor::setState (IBStream* state)
{
	// called when we load a preset, the model has to be reloaded
	IBStreamer streamer (state, kLittleEndian);
	bool bVal;
	float fVal;
	if (streamer.readFloat(fVal) == false) {
		return kResultFalse;
	}
	fMaxSliceLength = fVal * maxMaxSliceLength + 0.1f;
	lMaxSliceLengthSampleNumber = this->processSetup.sampleRate * fMaxSliceLength;
	if (streamer.readFloat(fVal) == false) {
		return kResultFalse;
	}
	fPitchChange = fVal * (maxPitchChange - minPitchChange) + minPitchChange;
	if (streamer.readBool(bVal) == false) {
		return kResultFalse;
	}
	bCalcPitchError = bVal;

	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API NetProcessProcessor::getState (IBStream* state)
{
	// here we need to save the model
	// 保存设置到持久化文件中
	IBStreamer streamer(state, kLittleEndian);

	streamer.writeFloat((fMaxSliceLength - 0.1f) / maxMaxSliceLength);
	streamer.writeFloat((fPitchChange - minPitchChange) / (maxPitchChange - minPitchChange));
	streamer.writeBool(bCalcPitchError);
	return kResultOk;
}

//------------------------------------------------------------------------
} // namespace MyCompanyName
