/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

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

enum RECORD_STATE
{
	IDLE, WORK
};

struct roleStruct {

	// 角色ID
	std::string sSpeakId;
	// 角色名称
	std::string sName;
	// 服务URL
	std::string sApiUrl;

};

//==============================================================================
/**
*/
class NetProcessJUCEVersionAudioProcessor  : public juce::AudioProcessor
                            #if JucePlugin_Enable_ARA
                             , public juce::AudioProcessorARAExtension
                            #endif
{
public:
    //==============================================================================
    NetProcessJUCEVersionAudioProcessor();
    ~NetProcessJUCEVersionAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

	void initThis();
	void runWorker();

	RECORD_STATE kRecordState;
	double fRecordIdleTime;
	int iNumberOfChanel;

	bool initDone = false;
	bool bEnableDebug;

	// 基于双指针缓冲区的线程数据交换机制
	long lModelInputOutputBufferSize;
	float* fModeulInputSampleBuffer;
	long lModelInputSampleBufferReadPos;
	long lModelInputSampleBufferWritePos;

	float* fModeulOutputSampleBuffer;
	long lModelOutputSampleBufferReadPos;
	long lModelOutputSampleBufferWritePos;

	bool bCalcPitchError;
	float fMaxSliceLength;
	float fMaxSliceLengthForRealTimeMode;
	float fMaxSliceLengthForSentenceMode;
	long lMaxSliceLengthSampleNumber;
	float fPitchChange;

	// JSON配置
	std::vector<roleStruct> roleList;
	int iSelectRoleIndex;

	double fSampleVolumeWorkActiveVal;
	FUNC_SRC_SIMPLE dllFuncSrcSimple;
	// debug 
	long lNoOutputCount;

	// preReSamplerate
	bool bEnableSOVITSPreResample;
	int iSOVITSModelInputSamplerate;
	bool bEnableHUBERTPreResample;
	int iHUBERTInputSampleRate;
	bool bDoItSignal;

	// 实时模式
	bool bRealTimeMode;

	// 音量检测
	bool bVolumeDetectFine;
	float fLowVolumeDetectTime;

	// 工作线程状态
	std::mutex mWorkerSafeExit;
	bool bWorkerNeedExit;

	// Model state
	juce::Value vServerUseTime;

private:
    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NetProcessJUCEVersionAudioProcessor)
};
