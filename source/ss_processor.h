//------------------------------------------------------------------------
// Copyright(c) 2022 natas.
//------------------------------------------------------------------------

#pragma once

#include "public.sdk/source/vst/vstaudioeffect.h"
#include <iostream>
#include <cstring>
#include <queue>
#include "AudioFile.h"
#include <mutex>
#include <stdio.h>

namespace MyCompanyName {

	enum RECORD_STATE
	{
		IDLE, WORK
	};
//------------------------------------------------------------------------
//  NetProcessProcessor
//------------------------------------------------------------------------


struct roleStruct {

	// 角色ID
	std::string sSpeakId;
	// 角色名称
	std::string sName;
	// 服务URL
	std::string sApiUrl;

};


class NetProcessProcessor : public Steinberg::Vst::AudioEffect
{
public:
	NetProcessProcessor ();
	~NetProcessProcessor () SMTG_OVERRIDE;

    // Create function
	static Steinberg::FUnknown* createInstance (void* /*context*/) 
	{ 
		return (Steinberg::Vst::IAudioProcessor*)new NetProcessProcessor; 
	}

	//--- ---------------------------------------------------------------------
	// AudioEffect overrides:
	//--- ---------------------------------------------------------------------
	/** Called at first after constructor */
	Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) SMTG_OVERRIDE;
	
	/** Called at the end before destructor */
	Steinberg::tresult PLUGIN_API terminate () SMTG_OVERRIDE;
	
	/** Switch the Plug-in on/off */
	Steinberg::tresult PLUGIN_API setActive (Steinberg::TBool state) SMTG_OVERRIDE;

	/** Will be called before any process call */
	Steinberg::tresult PLUGIN_API setupProcessing (Steinberg::Vst::ProcessSetup& newSetup) SMTG_OVERRIDE;
	
	/** Asks if a given sample size is supported see SymbolicSampleSizes. */
	Steinberg::tresult PLUGIN_API canProcessSampleSize (Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE;

	/** Here we go...the process call */
	Steinberg::tresult PLUGIN_API process (Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;
		
	/** For persistence */
	Steinberg::tresult PLUGIN_API setState (Steinberg::IBStream* state) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API getState (Steinberg::IBStream* state) SMTG_OVERRIDE;

//------------------------------------------------------------------------
protected:
	// 默认将模型的入参保存在这个文件中
	std::string sDefaultSaveModelInputWaveFileName = "C:/temp/vst/vst_model_input_wave.wav";
	float** mBuffer;
	long mBufferPos;
	// AudioFile<double>::AudioBuffer modelInputAudioBuffer;
	// long lModelInputAudioBufferPos;
	// AudioFile<double> audioFile;
	std::queue<double> qModelInputSampleQueue;
	// long maxInputBufferSize;
	RECORD_STATE kRecordState;
	double fRecordIdleTime;
	int iNumberOfChanel;

	// 默认将模型的返回结果保存在这个文件中
	std::string sDefaultSaveModelOutputWaveFileName = "C:/temp/vst/vst_model_output_wave.wav";
	// std::vector<std::vector<double>> modelOutputAudioBuffer;
	// long lModelOutputAudioBufferPos;
	std::queue<double> qModelOutputSampleQueue;
	// std::queue<double> qModelInputSampleQueue;
	// 是否还有更多的输出数据待处理
	// bool bHasMoreOutputData;

	std::mutex mInputQueueMutex;
	std::mutex mOutputQueueMutex;

	bool bRepeat;
	bool bCalcPitchError;
	float fRepeatTime;
	float fMaxSliceLength;
	long lMaxSliceLengthSampleNumber;
	float fPitchChange;
	
	// 前导信号缓冲区
	//float* fPrefixBuffer;
	//long lPrefixBufferPos;
	//float fPrefixLength;
	//long lPrefixLengthSampleNumber;

	// JSON配置
	std::string sJsonConfigFileName = "C:/temp/vst/netProcessConfig.json";
	std::vector<roleStruct> roleList;
	int iSelectRoleIndex;

	double fSampleVolumeWorkActiveVal;
};

//------------------------------------------------------------------------
} // namespace MyCompanyName
