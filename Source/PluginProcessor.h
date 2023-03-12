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

enum JOB_TYPE
{
	JOB_EMPTY, JOB_WORK
};

// 一个切片的参数
typedef struct
{
	JOB_TYPE jobType;
	// 占位符，实时模式
	bool bRealTimeModel;
	// 前导缓冲区大小
	long lPrefixLength;
	// 增量数据大小
	long lNewDataLength;
	std::vector<float> modelInputSampleVector;
	std::vector<float> modelOutputSampleVector;
	// 切片产生的时间，记录了这个切片最后一个样本的产生时间
	juce::int64 bornTimeStamp;
	// 输出中保留的部分，用于交叉淡化，在计算延迟的时候应当考虑这个值
	long lSuffixlOverlap2;
} JOB_STRUCT;

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
	// hop size
	int iHopSize;

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

	void loadConfig();
	void runWorker();
	void clearState();
	void tryGetFromModelOutputJobList();

	// 用来保存日志
	char buff[100];

	RECORD_STATE kRecordState;
	double fRecordIdleTime;
	int iNumberOfChanel;

	bool bEnableDebug;

	// 基于线程安全队列的模型入参缓冲区
	std::vector<JOB_STRUCT> modelInputJobList;
	std::vector<float> prepareModelInputSample;
	juce::CriticalSection modelInputJobListLock;

	std::vector<JOB_STRUCT> modelOutputJobList;
	JOB_STRUCT modelOutputJob;
	juce::CriticalSection modelOutputJobListLock;
	bool bHasMoreData;
		
	// 最后一条模型输出音频的尾部，用于交叉淡化处理
	float fCrossFadeLength;
	long lCrossFadeLength;
	std::vector<float> hanningWindow;

	float fMaxSliceLength;
	float fMaxSliceLengthForRealTimeMode;
	float fMaxSliceLengthForSentenceMode;
	long lMaxSliceLengthSampleNumber;
	long lRemainNeedSliceSampleNumber;
	float fPitchChange;
	int iHopSize;
	int iDAWSampleRate;

	// 前导信号缓冲区
	std::vector<float> fPrefixBuffer;
	long lPrefixBufferSize;
	long lPrefixBufferPos;
	float fPrefixLength;
	long lPrefixLengthSampleNumber;
	float fDropSuffixLength;
	
	// 安全区大小，当没有声音信号输出的时候，插入一个这么大的静音
	float fSafeZoneLength;
	long lSafeZoneSize;
	JOB_STRUCT safeJob;

	// JSON配置
	std::vector<roleStruct> roleList;
	int iSelectRoleIndex;

	double fSampleVolumeWorkActiveVal;
	// debug 
	long lNoOutputCount;

	// 实时模式
	bool bRealTimeMode;
	bool bRealTimeECO;

	// 音量检测
	bool bVolumeDetectFine;
	float fLowVolumeDetectTime;

	// 工作线程状态
	bool workStart;
	std::mutex mWorkerSafeExit;
	bool bWorkerNeedExit;

	// Model state
	juce::Value vServerUseTime;
	float fServerUseTime;
	juce::Value vAllUseTime;
	float fAllUseTime;
	// 实时模式下丢弃的音频数据长度
	juce::Value vDropDataLength;

private:
    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NetProcessJUCEVersionAudioProcessor)
};
