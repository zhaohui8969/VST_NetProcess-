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
		bool bRepeat,
		float fRepeatTime,
		float fPitchChange,
		bool bCalcPitchError,
		roleStruct roleStruct,
		FUNC_SRC_SIMPLE dllFuncSrcSimple
) {
	// 保存音频数据到文件
	// 获取当前写指针的位置
	long lTmpModelInputSampleBufferWritePos = *lModelInputSampleBufferWritePos;
	// 计算可以获取的数据长度
	long inputBufferSize = func_cacl_read_write_buffer_data_size(lModelInputOutputBufferSize, *lModelInputSampleBufferReadPos, lTmpModelInputSampleBufferWritePos);
		
	AudioFile<double>::AudioBuffer modelInputAudioBuffer;
	modelInputAudioBuffer.resize(iNumberOfChanel);
	modelInputAudioBuffer[0].resize(inputBufferSize);

	// 从队列中取出所需的音频数据
	int iAudioBufferPos = 0;
	if (*lModelInputSampleBufferReadPos < lTmpModelInputSampleBufferWritePos) {
		for (int i = *lModelInputSampleBufferReadPos; i < lTmpModelInputSampleBufferWritePos; i++) {
			modelInputAudioBuffer[0][iAudioBufferPos++] = fModeulInputSampleBuffer[i];
		}
	}
	else {
		for (int i = *lModelInputSampleBufferReadPos; i < lModelInputOutputBufferSize; i++) {
			modelInputAudioBuffer[0][iAudioBufferPos++] = fModeulInputSampleBuffer[i];
		}
		for (int i = 0; i < lTmpModelInputSampleBufferWritePos; i++) {
			modelInputAudioBuffer[0][iAudioBufferPos++] = fModeulInputSampleBuffer[i];
		}
	}
	// 读取完毕，将读指针指向最后写指针
	*lModelInputSampleBufferReadPos = lTmpModelInputSampleBufferWritePos;

	AudioFile<double> audioFile;
	audioFile.shouldLogErrorsToConsole(true);
	audioFile.setAudioBuffer(modelInputAudioBuffer);
	audioFile.setAudioBufferSize(iNumberOfChanel, inputBufferSize);
	audioFile.setBitDepth(24);
	audioFile.setSampleRate(dProjectSampleRate);
	
	// 保存音频文件到内存
	std::vector<uint8_t> vModelInputMemoryBuffer = std::vector<uint8_t>(0);
	audioFile.saveToWaveMemory(&vModelInputMemoryBuffer);

	// 调用AI模型进行声音处理
	httplib::Client cli(roleStruct.sApiUrl);

	cli.set_connection_timeout(0, 1000000); // 300 milliseconds
	cli.set_read_timeout(5, 0); // 5 seconds
	cli.set_write_timeout(5, 0); // 5 seconds

	// 从内存读取数据
	auto vModelInputData = vModelInputMemoryBuffer.data();
	std::string sBuffer(vModelInputData, vModelInputData + vModelInputMemoryBuffer.size());

	auto sBufferSize = sBuffer.size();
	char buff[100];
	snprintf(buff, sizeof(buff), "发送文件大小:%llu\n", sBufferSize);
	std::string buffAsStdStr = buff;
	OutputDebugStringA(buff);

	snprintf(buff, sizeof(buff), "%f", fPitchChange);
	std::string sCalcPitchError;
	if (bCalcPitchError) {
		sCalcPitchError = "true";
	}
	else {
		sCalcPitchError = "false";
	}

	char buffSamplerate[100];
	snprintf(buffSamplerate, sizeof(buffSamplerate), "%f", dProjectSampleRate);

	httplib::MultipartFormDataItems items = {
		{ "sSpeakId", roleStruct.sSpeakId, "", ""},
		{ "sName", roleStruct.sName, "", ""},
		{ "fPitchChange", buff, "", ""},
		{ "sampleRate", buffSamplerate, "", ""},
		{ "bCalcPitchError", sCalcPitchError.c_str(), "", ""},
		{ "sample", sBuffer, "sample.wav", "audio/x-wav"},
	};

	OutputDebugStringA("调用AI算法模型\n");
	auto res = cli.Post("/voiceChangeModel", items);
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
			fReSampleOutBuffer = (float*)(std::malloc(sizeof(float) * (iResampleNumbers + 128)));
			func_audio_resample(dllFuncSrcSimple, fReSampleInBuffer, fReSampleOutBuffer, fScaleRate, iSliceSampleNumber, iResampleNumbers);
		}

		// 为了便于监听，加一个重复
		int iRepeatSilenceSampleNumber = dProjectSampleRate * fRepeatTime;

		long lTmpModelOutputSampleBufferWritePos = *lModelOutputSampleBufferWritePos;
		for (int i = 0; i < iResampleNumbers; i++) {
			fModeulOutputSampleBuffer[lTmpModelOutputSampleBufferWritePos++] = fReSampleOutBuffer[i];
			if (lTmpModelOutputSampleBufferWritePos == lModelInputOutputBufferSize) {
				lTmpModelOutputSampleBufferWritePos = 0;
			}
			// 注意，此处不考虑写指针追上读指针的情况，因此缓冲区应当尽可能的大
		}
		if (bRepeat) {
			for (int i = 0; i < iRepeatSilenceSampleNumber; i++) {
				fModeulOutputSampleBuffer[lTmpModelOutputSampleBufferWritePos++] = 0.00001f;
				if (lTmpModelOutputSampleBufferWritePos == lModelInputOutputBufferSize) {
					lTmpModelOutputSampleBufferWritePos = 0;
				}
			}
			for (int i = 0; i < iResampleNumbers; i++) {
				fModeulOutputSampleBuffer[lTmpModelOutputSampleBufferWritePos++] = fReSampleOutBuffer[i];
				if (lTmpModelOutputSampleBufferWritePos == lModelInputOutputBufferSize) {
					lTmpModelOutputSampleBufferWritePos = 0;
				}
			}
		}
		// 将写指针指向新的位置
		*lModelOutputSampleBufferWritePos = lTmpModelOutputSampleBufferWritePos;
	}
	else {
		auto err = res.error();
		char buff[100];
		snprintf(buff, sizeof(buff), "算法服务错误:%d\n", err);
		std::string buffAsStdStr = buff;
		OutputDebugStringA(buff);
	}
}

NetProcessProcessor::NetProcessProcessor()
	: kRecordState(IDLE)
	, bNeedContinueRecord(false)
	, fRecordIdleTime(0.f)
	// 默认只处理单声道
	, iNumberOfChanel(1)
	, bRepeat(defaultEnableTwiceRepeat)
	, bCalcPitchError(defaultEnabelPitchErrorCalc)
	, fRepeatTime(0.f)
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
				case kEnableTwiceRepeat:
					OutputDebugStringA("kEnableTwiceRepeat\n");
					bRepeat = (bool)value;
					break;
				case kEnabelPitchErrorCalc:
					OutputDebugStringA("kEnabelPitchErrorCalc\n");
					bCalcPitchError = (bool)value;
					break;
				case kTwiceRepeatIntvalTime:
					OutputDebugStringA("kTwiceRepeatIntvalTime\n");
					fRepeatTime = value * maxTwiceRepeatIntvalTime;
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
					iSelectRoleIndex= std::min<int8>(
						(int8)(roleList.size() * value), roleList.size() - 1);
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


	char buff[100];
	snprintf(buff, sizeof(buff), "当前音频数据的最大音量:%f\n", fSampleMax);
	std::string buffAsStdStr = buff;
	OutputDebugStringA(buff);

	bool bVolumeDetectFine = fSampleMax >= fSampleVolumeWorkActiveVal;

	if (bVolumeDetectFine) {
		fRecordIdleTime = 0.f;
	}
	else {
		fRecordIdleTime += 1.f * data.numSamples / this->processSetup.sampleRate;
		char buff[100];
		snprintf(buff, sizeof(buff), "当前累积空闲时间:%f\n", fRecordIdleTime);
		std::string buffAsStdStr = buff;
		OutputDebugStringA(buff);
	}

	if (kRecordState == IDLE) {
		// 当前是空闲状态
		if (bNeedContinueRecord || bVolumeDetectFine) {
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
			bNeedContinueRecord = false;
			OutputDebugStringA("音量过小且持续超过一定时间，直接调用模型\n");
		}

		// 退出条件3：队列达到一定的大小
		long inputBufferSize = func_cacl_read_write_buffer_data_size(lModelInputOutputBufferSize, lModelInputSampleBufferReadPos, lModelInputSampleBufferWritePos);
		if (inputBufferSize > lMaxSliceLengthSampleNumber + lPrefixLengthSampleNumber) {
			bExitWorkState = true;
			bNeedContinueRecord = true;
			OutputDebugStringA("队列大小达到预期，直接调用模型\n");
		}

		if (bExitWorkState) {
			// 需要退出工作状态
			kRecordState = IDLE;
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
				bRepeat,
				fRepeatTime,
				fPitchChange,
				bCalcPitchError,
				roleList[iSelectRoleIndex],
				dllFuncSrcSimple).detach();
		}
	}

	// 如果模型输出缓冲区还有数据的话，写入到输出信号中去
	int channel = 0;
	bool bHasRightChanel = true;
	if (outputR == outputL || outputR == NULL) bHasRightChanel = false;
	if (lModelOutputSampleBufferReadPos != lModelOutputSampleBufferWritePos) {
		OutputDebugStringA("模型输出缓冲区还有数据的话，写入到输出信号中去\n");
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
	if (streamer.readBool(bVal) == false) {
		return kResultFalse;
	}
	bRepeat = bVal;
	if (streamer.readFloat(fVal) == false) {
		return kResultFalse;
	}
	fRepeatTime = fVal * maxTwiceRepeatIntvalTime;
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

	streamer.writeBool(bRepeat);
	streamer.writeFloat(fRepeatTime / maxTwiceRepeatIntvalTime);
	streamer.writeFloat((fMaxSliceLength - 0.1f) / maxMaxSliceLength);
	streamer.writeFloat((fPitchChange - minPitchChange) / (maxPitchChange - minPitchChange));
	streamer.writeBool(bCalcPitchError);
	return kResultOk;
}

//------------------------------------------------------------------------
} // namespace MyCompanyName
