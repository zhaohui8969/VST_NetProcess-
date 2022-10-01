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

using namespace Steinberg;

namespace MyCompanyName {
//------------------------------------------------------------------------
// NetProcessProcessor
//------------------------------------------------------------------------



// 进行声音处理，较为耗时，在单独的线程里进行，避免主线程卡顿爆音
void func_do_voice_transfer(
	int iNumberOfChanel,									// 通道数量
	double dProjectSampleRate,								// 项目采样率
	AudioFile<double>::AudioBuffer modelInputAudioBuffer,	// AI模型入参缓冲区
	long maxInputBufferSize,								// 模型入参缓冲区大小
	long* lModelInputAudioBufferPos,						// 模型入参缓冲区读写位置指针
	AudioFile<double>::AudioBuffer* modelOutputAudioBuffer,  // AI模型出参缓冲区
	long* lModelOutputAudioBufferPos,						// 模型出参缓冲区读写位置指针
	std::string sSaveModelInputWaveFileName,				// 模型的入参保存在这个文件中
	std::string sSaveModelOutputWaveFileName,				// 模型的返回结果保存在这个文件中
	bool* bHasMoreOutputData								// 开关标记，表示当前有模型返回
) {
	// 保存音频数据到文件
	AudioFile<double> audioFile;
	audioFile.shouldLogErrorsToConsole(true);
	audioFile.setAudioBuffer(modelInputAudioBuffer);
	audioFile.setAudioBufferSize(iNumberOfChanel, *lModelInputAudioBufferPos);
	audioFile.setBitDepth(24);
	audioFile.setSampleRate(dProjectSampleRate);
	audioFile.save(sSaveModelInputWaveFileName, AudioFileFormat::Wave);

	// 重置缓冲区指针以及缓冲区数据
	// 注意指针
	*lModelInputAudioBufferPos = 0;
	for (int i = 0; i < maxInputBufferSize; i++) {
		modelInputAudioBuffer[0][i] = 0;
		//modelInputAudioBuffer[1][i] = 0;
	}

	// 调用AI模型进行声音处理
	httplib::Client cli("http://192.168.3.253:6842");

	cli.set_connection_timeout(0, 1000000); // 300 milliseconds
	cli.set_read_timeout(5, 0); // 5 seconds
	cli.set_write_timeout(5, 0); // 5 seconds

	std::ifstream t_pc_file(sSaveModelInputWaveFileName, std::ios::binary);
	std::stringstream buffer_pc_file;
	buffer_pc_file << t_pc_file.rdbuf();
	auto sBuffer = buffer_pc_file.str();
	auto sBufferSize = sBuffer.size();
	char buff[100];
	snprintf(buff, sizeof(buff), "发送文件大小:%llu\n", sBufferSize);
	std::string buffAsStdStr = buff;
	OutputDebugStringA(buff);

	httplib::MultipartFormDataItems items = {
	  { "sample", sBuffer, "sample.wav", "application/octet-stream"},
	};

	OutputDebugStringA("调用AI算法模型\n");
	auto res = cli.Post("/voiceChangeModel", items);
	if (res.error() == httplib::Error::Success && res->status == 200) {
		// 调用成功，开始将结果放入到临时缓冲区，并替换输出
		// 写入文件
		std::ofstream t_out_file(sSaveModelOutputWaveFileName, std::ios::binary);
		std::string body = res->body;
		for (size_t i = 0; i < body.size(); i++)
		{
			char value = (char)body[i];
			t_out_file.write(&value, sizeof(char));
		}
		t_out_file.close();

		// 从文件中读取音频数据，放入输出缓冲区中
		AudioFile<double> tmpAudioFile;
		tmpAudioFile.load(sSaveModelOutputWaveFileName);
		int sampleRate = tmpAudioFile.getSampleRate();
		int bitDepth = tmpAudioFile.getBitDepth();

		int numSamples = tmpAudioFile.getNumSamplesPerChannel();
		double lengthInSeconds = tmpAudioFile.getLengthInSeconds();

		int numChannels = tmpAudioFile.getNumChannels();
		bool isMono = tmpAudioFile.isMono();
		bool isStereo = tmpAudioFile.isStereo();

		// 注意指针
		*modelOutputAudioBuffer = tmpAudioFile.samples;
		*bHasMoreOutputData = true;
		*lModelOutputAudioBufferPos = 0;
	}
	else {
		auto err = res.error();
		char buff[100];
		snprintf(buff, sizeof(buff), "算法服务错误:%d\n", err);
		std::string buffAsStdStr = buff;
		OutputDebugStringA(buff);
	}
}

NetProcessProcessor::NetProcessProcessor ()
	: mBuffer(nullptr)
	, mBufferPos(0)
	, modelInputAudioBuffer(0)
	, lModelInputAudioBufferPos(0)
	//1000000约20秒
	//500000约10秒
	//200000约5秒
	, maxInputBufferSize(1000000)
	, kRecordState(IDLE)
	, fRecordIdleTime(0.f)
	// 默认只处理单声道
	, iNumberOfChanel(1)
	// 模型输出文件相关参数
	, modelOutputAudioBuffer(0)
	, lModelOutputAudioBufferPos(0)
	, bHasMoreOutputData(false)
{
	//--- set the wanted controller for our processor
	setControllerClass (kNetProcessControllerUID);
}

//------------------------------------------------------------------------
NetProcessProcessor::~NetProcessProcessor ()
{}

//------------------------------------------------------------------------
tresult PLUGIN_API NetProcessProcessor::initialize (FUnknown* context)
{
	// Here the Plug-in will be instanciated
	// 初始化音频输出文件所用的缓存，双声道
	printf_s("初始化AI输入缓存");
	OutputDebugStringA("初始化AI输入缓存");
	modelInputAudioBuffer.resize(iNumberOfChanel);
	modelInputAudioBuffer[0].resize(maxInputBufferSize);
	//modelInputAudioBuffer[1].resize(maxOutBufferSize);
	
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

    /*if (data.inputParameterChanges)
    {
        int32 numParamsChanged = data.inputParameterChanges->getParameterCount ();
        for (int32 index = 0; index < numParamsChanged; index++)
        {
            if (auto* paramQueue = data.inputParameterChanges->getParameterData (index))
            {
                Vst::ParamValue value;
                int32 sampleOffset;
                int32 numPoints = paramQueue->getPointCount ();
                switch (paramQueue->getParameterId ())
                {
				}
			}
		}
	}*/
	
	//--- Here you have to implement your processing
	Vst::Sample32* inputL = data.inputs[0].channelBuffers32[0];
	//Vst::Sample32* inputR = data.inputs[0].channelBuffers32[1];
	Vst::Sample32* outputL = data.outputs[0].channelBuffers32[0];
	//Vst::Sample32* outputR = data.outputs[0].channelBuffers32[1];
	double fSampleMax = -9999;
	for (int32 i = 0; i < data.numSamples; i++) {
		// 将输入端的信号复制一遍
		// modelInputAudioBuffer[0][lModelInputAudioBufferPos + i] = inputL[i];
		// modelInputAudioBuffer[1][lModelInputAudioBufferPos + i] = inputR[i];

		// 获取当前块的最大音量
		double fCurrentSample = inputL[i];
		if (fCurrentSample > fSampleMax) {
			fSampleMax = fCurrentSample;
		}
		//fSampleSum += inputL[i] + inputR[i];

		// 在输出端对信号进行放大
		// outputL[i] = inputL[i] * 2;
		// outputR[i] = inputR[i] * 2;

		// 对输出静音
		//outputL[i] = 0.0000000001f;
		//outputR[i] = 0;
	}

	char buff[100];
	snprintf(buff, sizeof(buff), "当前音频数据的最大音量:%f\n", fSampleMax);
	std::string buffAsStdStr = buff;
	OutputDebugStringA(buff);

	double fSampleVolumeWorkActiveVal = 0.05;
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
		if (bVolumeDetectFine) {
			OutputDebugStringA("切换到工作状态");
			kRecordState = WORK;
			// 将当前的音频数据写入到模型入参缓冲区中
			for (int32 i = 0; i < data.numSamples; i++) {
				modelInputAudioBuffer[0][lModelInputAudioBufferPos + i] = inputL[i];
				//modelInputAudioBuffer[1][lModelInputAudioBufferPos + i] = inputR[i];
			}
			lModelInputAudioBufferPos += data.numSamples;
		}
	}
	else {
		// 当前是工作状态
		// 将当前的音频数据写入到模型入参缓冲区中
		for (int32 i = 0; i < data.numSamples; i++) {
			modelInputAudioBuffer[0][lModelInputAudioBufferPos + i] = inputL[i];
			//modelInputAudioBuffer[1][lModelInputAudioBufferPos + i] = inputR[i];
		}
		lModelInputAudioBufferPos += data.numSamples;
		// 判断是否需要退出工作状态
		bool bExitWorkState = false;
		// 退出条件1：当缓冲区不足以支持下一次写入的时候
		if (lModelInputAudioBufferPos + data.numSamples > maxInputBufferSize) {
			bExitWorkState = true;
			OutputDebugStringA("当缓冲区不足以支持下一次写入的时候，直接调用模型\n");
		}

		// 退出条件2：音量过小且持续超过一定时间
		float fLowVolumeDetectTime = 1.f; // 1s
		if (fRecordIdleTime >= fLowVolumeDetectTime) {
			bExitWorkState = true;
			OutputDebugStringA("音量过小且持续超过一定时间，直接调用模型\n");
		}

		if (bExitWorkState) {
			// 需要退出工作状态
			kRecordState = IDLE;
			std::thread (func_do_voice_transfer,
				iNumberOfChanel,
				this->processSetup.sampleRate,
				modelInputAudioBuffer,
				maxInputBufferSize,
				&lModelInputAudioBufferPos,
				&modelOutputAudioBuffer,
				&lModelOutputAudioBufferPos,
				sDefaultSaveModelInputWaveFileName,
				sDefaultSaveModelOutputWaveFileName,
				&bHasMoreOutputData).detach();
		}
	}

	// 如果模型输出缓冲区还有数据的话，写入到输出信号中去
	int channel = 0;
	if (bHasMoreOutputData) {
		OutputDebugStringA("模型输出缓冲区还有数据的话，写入到输出信号中去\n");
		bool bFinish = false;
		for (int i = 0; i < data.numSamples; i++)
		{
			int index = lModelOutputAudioBufferPos + i / 2;
			bFinish = index >= modelOutputAudioBuffer[channel].size();
			if (!bFinish) {
				double currentSample = modelOutputAudioBuffer[channel][index];
				outputL[i] = currentSample;
			}
			else {
				outputL[i] = 0.f;
			}
		}
		if (bFinish) {
			// 数据取完了
			OutputDebugStringA("数据取完了\n");
			bHasMoreOutputData = false;
			lModelOutputAudioBufferPos = 0;
		}
		else {
			lModelOutputAudioBufferPos += data.numSamples / 2;
		}
	}
	else {
		for (int32 i = 0; i < data.numSamples; i++) {
			// 对输出静音
			outputL[i] = 0.0000000001f;
			//outputR[i] = 0;
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
	
	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API NetProcessProcessor::getState (IBStream* state)
{
	// here we need to save the model
	IBStreamer streamer (state, kLittleEndian);

	return kResultOk;
}

//------------------------------------------------------------------------
} // namespace MyCompanyName
