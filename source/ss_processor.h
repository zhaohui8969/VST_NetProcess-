//------------------------------------------------------------------------
// Copyright(c) 2022 natas.
//------------------------------------------------------------------------

#pragma once

#include "public.sdk/source/vst/vstaudioeffect.h"
#include <iostream>
#include <cstring>
#include "AudioFile.h"
#include <stdio.h>

typedef struct
{
	const float* data_in;
	float* data_out;

	long	input_frames, output_frames;
	long	input_frames_used, output_frames_gen;

	int		end_of_input;

	double	src_ratio;
} SRC_DATA;

enum
{
	SRC_SINC_BEST_QUALITY = 0,
	SRC_SINC_MEDIUM_QUALITY = 1,
	SRC_SINC_FASTEST = 2,
	SRC_ZERO_ORDER_HOLD = 3,
	SRC_LINEAR = 4,
};

typedef int(*FUNC_SRC_SIMPLE)(SRC_DATA* data, int converter_type, int channels);

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
	RECORD_STATE kRecordState;
	bool bNeedContinueRecord;
	double fRecordIdleTime;
	int iNumberOfChanel;

	// 基于队列和互斥锁的线程数据交换机制
	//std::queue<double> qModelInputSampleQueue;
	//std::queue<double> qModelOutputSampleQueue;
	//std::mutex mInputQueueMutex;
	//std::mutex mOutputQueueMutex;

	// 基于双指针缓冲区的线程数据交换机制
	long lModelInputOutputBufferSize;
	float* fModeulInputSampleBuffer;
	long lModelInputSampleBufferReadPos;
	long lModelInputSampleBufferWritePos;
	
	float* fModeulOutputSampleBuffer;
	long lModelOutputSampleBufferReadPos;
	long lModelOutputSampleBufferWritePos;

	bool bRepeat;
	bool bCalcPitchError;
	float fRepeatTime;
	float fMaxSliceLength;
	long lMaxSliceLengthSampleNumber;
	float fPitchChange;

	// 前导信号缓冲区
	float* fPrefixBuffer;
	long lPrefixBufferPos;
	float fPrefixLength;
	float fDropSuffixLength;
	long lPrefixLengthSampleNumber;
	
	// JSON配置
	std::string sJsonConfigFileName = "C:/temp/vst/netProcessConfig.json";
	std::vector<roleStruct> roleList;
	int iSelectRoleIndex;

	double fSampleVolumeWorkActiveVal;
	FUNC_SRC_SIMPLE dllFuncSrcSimple;
};

//------------------------------------------------------------------------
} // namespace MyCompanyName
