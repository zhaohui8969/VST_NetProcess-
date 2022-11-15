//------------------------------------------------------------------------
// Copyright(c) 2022 natas.
//------------------------------------------------------------------------

#include "ss_processor.h"
#include "ss_cids.h"

#include "base/source/fstreamer.h"
// 引入必要的头文件
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

	// Resample
	void func_audio_resample(FUNC_SRC_SIMPLE dllFuncSrcSimple, float* fInBuffer, float* fOutBuffer, double src_ratio, long lInSize, long lOutSize) {
		SRC_DATA data;
		data.src_ratio = src_ratio;
		data.input_frames = lInSize;
		data.output_frames = lOutSize;
		data.data_in = fInBuffer;
		data.data_out = fOutBuffer;
		int error = dllFuncSrcSimple(&data, SRC_SINC_FASTEST, 1);

		/*if (error > 0) {
			char buff[100];
			const char* cError = src_strerror(error);
			snprintf(buff, sizeof(buff), "Resample error%s\n", cError);
			OutputDebugStringA(buff);
		}*/
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

// 进行声音处理，较为耗时，在单独的线程里进行，避免主线程卡顿爆音
	void func_do_voice_transfer(
		int iNumberOfChanel,									// 通道数量
		double dProjectSampleRate,								// 项目采样率
		//std::queue<double>* qModelInputSampleQueue,				// 模型入参队列
		//std::queue<double>* qModelOutputSampleQueue,			// 模型返回队列
		
		long lModelInputOutputBufferSize,

		float* fModeulInputSampleBuffer,
		long* lModelInputSampleBufferReadPos,
		long* lModelInputSampleBufferWritePos,

		float* fModeulOutputSampleBuffer,
		long* lModelOutputSampleBufferReadPos,
		long* lModelOutputSampleBufferWritePos,

		float fPrefixLength,
		float fDropSuffixLength,
		float fPitchChange,
		bool bCalcPitchError,
		roleStruct roleStruct,
		FUNC_SRC_SIMPLE dllFuncSrcSimple,
		long long tStart,
		bool bEnablePreResample,
		int iModelInputSamplerate,
		int iHUBERTInputSampleRate,

		bool bFoundJit,
		float fAvoidJitPrefixTime
) {
	char buff[100];
	long long tTime1;
	long long tTime2;
	long long tUseTime;
	tTime1 = tStart;
	tTime2 = func_get_timestamp();
	tUseTime = tTime2 - tTime1;
	snprintf(buff, sizeof(buff), "AI线程启动耗时:%lldms\n", tUseTime);
	OutputDebugStringA(buff);
	tTime1 = tTime2;

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

	char buffSamplerate[100];
	std::string sHUBERTSampleBuffer;
	double dSOVITSInputSamplerate;
	if (bEnablePreResample) {
		// 提前对音频重采样，C++重采样比Python端快
		// SOVITS输入音频重采样
		float* fReSampleInBuffer = vModelInputSampleBufferVector.data();
		float* fReSampleOutBuffer = fReSampleInBuffer;
		int iResampleNumbers = vModelInputSampleBufferVector.size();

		if (dProjectSampleRate != iModelInputSamplerate) {
			double fScaleRate = iModelInputSamplerate / dProjectSampleRate;
			iResampleNumbers = fScaleRate * iResampleNumbers;
			fReSampleOutBuffer = (float*)(std::malloc(sizeof(float) * iResampleNumbers));
			func_audio_resample(dllFuncSrcSimple, fReSampleInBuffer, fReSampleOutBuffer, fScaleRate, vModelInputSampleBufferVector.size(), iResampleNumbers);
		}

		snprintf(buffSamplerate, sizeof(buffSamplerate), "%d", iModelInputSamplerate);
		dSOVITSInputSamplerate = iModelInputSamplerate;
		modelInputAudioBuffer[0].resize(iResampleNumbers);
		for (int i = 0; i < iResampleNumbers; i++) {
			modelInputAudioBuffer[0][i] = fReSampleOutBuffer[i];
		}

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
		HUBERTAudioFile.shouldLogErrorsToConsole(true);
		HUBERTAudioFile.setAudioBuffer(HUBERTModelInputAudioBuffer);
		HUBERTAudioFile.setAudioBufferSize(iNumberOfChanel, HUBERTModelInputAudioBuffer[0].size());
		HUBERTAudioFile.setBitDepth(24);
		HUBERTAudioFile.setSampleRate(iHUBERTInputSampleRate);
		// 保存音频文件到内存
		std::vector<uint8_t> vHUBERTModelInputMemoryBuffer = std::vector<uint8_t>();
		HUBERTAudioFile.saveToWaveMemory(&vHUBERTModelInputMemoryBuffer);
		// 从内存读取数据
		auto vHUBERTModelInputData = vHUBERTModelInputMemoryBuffer.data();
		std::string sHUBERTModelInputString(vHUBERTModelInputData, vHUBERTModelInputData + vHUBERTModelInputMemoryBuffer.size());
		sHUBERTSampleBuffer = sHUBERTModelInputString;
	}
	else {
		// 未开启预处理重采样，音频原样发送
		dSOVITSInputSamplerate = dProjectSampleRate;
		snprintf(buffSamplerate, sizeof(buffSamplerate), "%f", dProjectSampleRate);
		modelInputAudioBuffer[0].resize(vModelInputSampleBufferVector.size());
		for (int i = 0; i < vModelInputSampleBufferVector.size(); i++) {
			modelInputAudioBuffer[0][i] = vModelInputSampleBufferVector[i];
		}
	}

	int iModelInputNumSamples = modelInputAudioBuffer[0].size();
	AudioFile<double> audioFile;
	audioFile.shouldLogErrorsToConsole(true);
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
	std::vector<uint8_t> vModelInputMemoryBuffer = std::vector<uint8_t>(iModelInputNumSamples * 3);
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

	auto sBufferSize = sModelInputString.size();
	snprintf(buff, sizeof(buff), "发送文件大小:%llu\n", sBufferSize);
	OutputDebugStringA(buff);
	
	char cPitchBuff[100];
	snprintf(cPitchBuff, sizeof(cPitchBuff), "%f", fPitchChange);

	std::string sCalcPitchError;
	if (bCalcPitchError) {
		sCalcPitchError = "true";
	}
	else {
		sCalcPitchError = "false";
	}

	std::string sEnablePreResample;
	if (bEnablePreResample) {
		sEnablePreResample = "true";
	}
	else {
		sEnablePreResample = "false";
	}

	tTime2 = func_get_timestamp();
	tUseTime = tTime2 - tTime1;
	snprintf(buff, sizeof(buff), "调用HTTP接口准备耗时:%lldms\n", tUseTime);
	OutputDebugStringA(buff);
	tTime1 = tTime2;

	httplib::MultipartFormDataItems items = {
		{ "sSpeakId", roleStruct.sSpeakId, "", ""},
		{ "sName", roleStruct.sName, "", ""},
		{ "fPitchChange", cPitchBuff, "", ""},
		{ "sampleRate", buffSamplerate, "", ""},
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
		int bitDepth = tmpAudioFile.getBitDepth();

		int numSamples = tmpAudioFile.getNumSamplesPerChannel();
		double lengthInSeconds = tmpAudioFile.getLengthInSeconds();

		int numChannels = tmpAudioFile.getNumChannels();
		bool isMono = tmpAudioFile.isMono();
		bool isStereo = tmpAudioFile.isStereo();

		tTime2 = func_get_timestamp();
		tUseTime = tTime2 - tTime1;
		snprintf(buff, sizeof(buff), "从HTTP结果中获取音频文件耗时:%lldms\n", tUseTime);
		OutputDebugStringA(buff);
		tTime1 = tTime2;

		// 音频流式处理
		// 做1s滑窗，但是丢掉最后0.1s再取最后的输出进行拼接
		
		// 跳过前导音频信号
		int iSkipSamplePosStart = fPrefixLength * sampleRate;
		// 丢弃尾部信号
		int iSkipSamplePosEnd = numSamples - (fDropSuffixLength * sampleRate);
		int iSliceSampleNumber = iSkipSamplePosEnd - iSkipSamplePosStart;
		float* fSliceSampleBuffer = (float*)(std::malloc(sizeof(float) * iSliceSampleNumber));
		int iSlicePos = 0;
		auto fOriginAudioBuffer = tmpAudioFile.samples[0];
		for (int i = iSkipSamplePosStart; i < iSkipSamplePosEnd; i++) {
			fSliceSampleBuffer[iSlicePos++] = fOriginAudioBuffer[i];
		}

		tTime2 = func_get_timestamp();
		tUseTime = tTime2 - tTime1;
		snprintf(buff, sizeof(buff), "前后裁剪耗时:%lldms\n", tUseTime);
		OutputDebugStringA(buff);
		tTime1 = tTime2;

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
		snprintf(buff, sizeof(buff), "重采样耗时:%lldms\n", tUseTime);
		OutputDebugStringA(buff);
		tTime1 = tTime2;

		long lTmpModelOutputSampleBufferWritePos = *lModelOutputSampleBufferWritePos;
		
		// 为了避免模型JIT，写一段静音的缓冲区
		if (bFoundJit) {
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
			*lModelOutputSampleBufferWritePos = lTmpModelOutputSampleBufferWritePos;
			// 注意，此处不考虑写指针追上读指针的情况，因此缓冲区应当尽可能的大
		}
		// 将写指针指向新的位置
		//*lModelOutputSampleBufferWritePos = lTmpModelOutputSampleBufferWritePos;
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
	snprintf(buff, sizeof(buff), "线程总耗时:%lld\n", tUseTime);
	OutputDebugStringA(buff);
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
		OutputDebugStringA("DLL load Error!");
	}

	// JSON配置文件
	std::ifstream t_pc_file(sJsonConfigFileName, std::ios::binary);
	std::stringstream buffer_pc_file;
	buffer_pc_file << t_pc_file.rdbuf();
	
	Json::Value jsonRoot;
	buffer_pc_file >> jsonRoot;
	int iRoleSize = jsonRoot["roleList"].size();

	bDisableVolumeDetect = jsonRoot["bDisableVolumeDetect"].asBool();
	bEnablePreResample = jsonRoot["bEnablePreResample"].asBool();
	iModelInputSamplerate = jsonRoot["iModelInputSamplerate"].asInt();
	iHUBERTInputSampleRate = jsonRoot["iHUBERTInputSampleRate"].asInt();
	fAvoidJitPrefixTime = jsonRoot["fAvoidJitPrefixTime"].asFloat();

	fSampleVolumeWorkActiveVal = jsonRoot["fSampleVolumeWorkActiveVal"].asDouble();

	roleList.clear();
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
	fPrefixLength = jsonRoot["fPrefixLength"].asFloat();
	fDropSuffixLength = jsonRoot["fDropSuffixLength"].asFloat();
	lPrefixLengthSampleNumber = fPrefixLength * this->processSetup.sampleRate;
	fPrefixBuffer = (float*)std::malloc(sizeof(float) * lPrefixLengthSampleNumber);
	lPrefixBufferPos = 0;
	
	lNoOutputCount = 0;
	bFoundJit = true;

	// 初始化线程间交换数据的缓冲区
	float fModelInputOutputBufferSecond = 120.f;
	lModelInputOutputBufferSize = fModelInputOutputBufferSecond * this->processSetup.sampleRate;
	fModeulInputSampleBuffer = (float*)(std::malloc(sizeof(float) * lModelInputOutputBufferSize));
	fModeulOutputSampleBuffer = (float*)(std::malloc(sizeof(float) * lModelInputOutputBufferSize));
	lModelInputSampleBufferReadPos = 0;
	lModelInputSampleBufferWritePos = 0;
	lModelOutputSampleBufferReadPos = 0;
	lModelOutputSampleBufferWritePos = 0;

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
					iSelectRoleIndex= std::min<int>(
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
		// 将输入端的信号复制一遍
		// modelInputAudioBuffer[0][lModelInputAudioBufferPos + i] = inputL[i];
		// modelInputAudioBuffer[1][lModelInputAudioBufferPos + i] = inputR[i];

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

	/*
	char buff[100];
	snprintf(buff, sizeof(buff), "当前音频数据的最大音量:%f\n", fSampleMax);
	std::string buffAsStdStr = buff;
	OutputDebugStringA(buff);
	*/
	
	bool bVolumeDetectFine;
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
			//mInputQueueMutex.lock();
			/*for (int32 i = 0; i < data.numSamples; i++) {
				qModelInputSampleQueue.push(inputL[i]);
			}*/
			for (int i = lPrefixBufferPos; i < lPrefixLengthSampleNumber; i++) {
				fModeulInputSampleBuffer[lModelInputSampleBufferWritePos++] = fPrefixBuffer[i];
				if (lModelInputSampleBufferWritePos == lModelInputOutputBufferSize) {
					lModelInputSampleBufferWritePos = 0;
				}
				//qModelInputSampleQueue.push(fPrefixBuffer[i]);
			}
			for (int i = 0; i < lPrefixBufferPos; i++) {
				fModeulInputSampleBuffer[lModelInputSampleBufferWritePos++] = fPrefixBuffer[i];
				if (lModelInputSampleBufferWritePos == lModelInputOutputBufferSize) {
					lModelInputSampleBufferWritePos = 0;
				}
				//qModelInputSampleQueue.push(fPrefixBuffer[i]);
			}
			//mInputQueueMutex.unlock();
		}
	}
	else {
		// 当前是工作状态
		// 将当前的音频数据写入到模型入参缓冲区中
		//mInputQueueMutex.lock();
		for (int i = 0; i < data.numSamples; i++) {
			fModeulInputSampleBuffer[lModelInputSampleBufferWritePos++] = inputL[i];
			if (lModelInputSampleBufferWritePos == lModelInputOutputBufferSize) {
				lModelInputSampleBufferWritePos = 0;
			}
			//qModelInputSampleQueue.push(inputL[i]);
		}
		//mInputQueueMutex.unlock();
		// 判断是否需要退出工作状态
		bool bExitWorkState = false;

		// 退出条件2：音量过小且持续超过一定时间
		float fLowVolumeDetectTime = 1.f; // 1s
		if (fRecordIdleTime >= fLowVolumeDetectTime) {
			bExitWorkState = true;
			OutputDebugStringA("音量过小且持续超过一定时间，直接调用模型\n");
		}

		// 退出条件3：队列达到一定的大小
		long inputBufferSize = func_cacl_read_write_buffer_data_size(lModelInputOutputBufferSize, lModelInputSampleBufferReadPos, lModelInputSampleBufferWritePos);
		if (inputBufferSize > lMaxSliceLengthSampleNumber + lPrefixLengthSampleNumber) {
			bExitWorkState = true;
			OutputDebugStringA("队列大小达到预期，直接调用模型\n");
		}

		if (bExitWorkState) {
			// 需要退出工作状态
			kRecordState = IDLE;
			long long tStart = func_get_timestamp();
			std::thread (func_do_voice_transfer,
				iNumberOfChanel,
				this->processSetup.sampleRate,
				//&qModelInputSampleQueue,
				//&qModelOutputSampleQueue,
				lModelInputOutputBufferSize,
				fModeulInputSampleBuffer,
				&lModelInputSampleBufferReadPos,
				&lModelInputSampleBufferWritePos,
				fModeulOutputSampleBuffer,
				&lModelOutputSampleBufferReadPos,
				&lModelOutputSampleBufferWritePos,
				fPrefixLength,
				fDropSuffixLength,
				fPitchChange,
				bCalcPitchError,
				roleList[iSelectRoleIndex],
				dllFuncSrcSimple,
				tStart,
				bEnablePreResample,
				iModelInputSamplerate,
				iHUBERTInputSampleRate,
				bFoundJit,
				fAvoidJitPrefixTime).detach();
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
		//OutputDebugStringA("模型输出缓冲区还有数据的话，写入到输出信号中去\n");
		bool bFinish = false;
		//mOutputQueueMutex.lock();
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
				//double currentSample = qModelOutputSampleQueue.front();
				//qModelOutputSampleQueue.pop();
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
		//mOutputQueueMutex.unlock();
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
