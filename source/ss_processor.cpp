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

using namespace Steinberg;

namespace MyCompanyName {
//------------------------------------------------------------------------
// NetProcessProcessor
//------------------------------------------------------------------------
NetProcessProcessor::NetProcessProcessor ()
	:mBuffer(nullptr)
	,mBufferPos(0)
	,audioFile(sDefaultSaveWaveFileName)
	,audioBuffer(0)
	//1000000约20秒
	//500000约10秒
	//200000约5秒
	,maxOutBufferSize(500000)
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
	audioFile.shouldLogErrorsToConsole(true);
	audioBuffer.resize(2);
	audioBuffer[0].resize(maxOutBufferSize);
	audioBuffer[1].resize(maxOutBufferSize);
		
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
	Vst::Sample32* inputR = data.inputs[0].channelBuffers32[1];
	for (int32 i = 0; i < data.numSamples; i++) {
		// 将输入端的信号复制一遍
		audioBuffer[0][mBufferPos + i] = inputL[i];
		audioBuffer[1][mBufferPos + i] = inputR[i];
	}
	mBufferPos += data.numSamples;
	// 当缓冲区不足以支持下一次写入的时候，写一次文件
	if (mBufferPos + data.numSamples > maxOutBufferSize){
		bool ok = audioFile.setAudioBuffer(audioBuffer);
		// Set both the number of channels and number of samples per channel
		audioFile.setAudioBufferSize(2, maxOutBufferSize);
		audioFile.setBitDepth(24);
		audioFile.setSampleRate(this->processSetup.sampleRate);

		// Wave file (explicit)
		audioFile.save(sDefaultSaveWaveFileName, AudioFileFormat::Wave);

		mBufferPos = 0;
		
		// 调用AI模型进行声音处理
		// httplib::Client cli("http://localhost", 6842);
		httplib::Client cli("http://192.168.3.253:6842");

		cli.set_connection_timeout(0, 1000000); // 300 milliseconds
		cli.set_read_timeout(5, 0); // 5 seconds
		cli.set_write_timeout(5, 0); // 5 seconds

		std::ifstream t_pc_file(sDefaultSaveWaveFileName, std::ios::binary);
		std::stringstream buffer_pc_file;
		buffer_pc_file << t_pc_file.rdbuf();
		auto sBuffer = buffer_pc_file.str();
		auto sBufferSize = sBuffer.size();
		char buff[100];
		snprintf(buff, sizeof(buff), "发送文件大小:%d\n", sBufferSize);
		std::string buffAsStdStr = buff;
		OutputDebugStringA(buff);

		httplib::MultipartFormDataItems items = {
		  { "sample", sBuffer, "sample.wav", "application/octet-stream"},
		};

		OutputDebugStringA("调用AI算法模型\n");
		auto res = cli.Post("/voiceChangeModel", items);
		if (res.error() == httplib::Error::Success && res->status == 200) {

		} else {
			auto err = res.error();
			printf("算法服务错误:%d", err);
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
