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

// һ����Ƭ�Ĳ���
typedef struct
{
	JOB_TYPE jobType;
	// ռλ����ʵʱģʽ
	bool bRealTimeModel;
	// ǰ����������С
	long lPrefixLength;
	// �������ݴ�С
	long lNewDataLength;
	std::vector<float> modelInputSampleVector;
	std::vector<float> modelOutputSampleVector;
	// ��Ƭ������ʱ�䣬��¼�������Ƭ���һ�������Ĳ���ʱ��
	juce::int64 bornTimeStamp;
	// ����б����Ĳ��֣����ڽ��浭�����ڼ����ӳٵ�ʱ��Ӧ���������ֵ
	long lSuffixlOverlap2;
} JOB_STRUCT;

enum RECORD_STATE
{
	IDLE, WORK
};

struct roleStruct {

	// ��ɫID
	std::string sSpeakId;
	// ��ɫ����
	std::string sName;
	// ����URL
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

	// ����������־
	char buff[100];

	RECORD_STATE kRecordState;
	double fRecordIdleTime;
	int iNumberOfChanel;

	bool bEnableDebug;

	// �����̰߳�ȫ���е�ģ����λ�����
	std::vector<JOB_STRUCT> modelInputJobList;
	std::vector<float> prepareModelInputSample;
	juce::CriticalSection modelInputJobListLock;

	std::vector<JOB_STRUCT> modelOutputJobList;
	JOB_STRUCT modelOutputJob;
	juce::CriticalSection modelOutputJobListLock;
	bool bHasMoreData;
		
	// ���һ��ģ�������Ƶ��β�������ڽ��浭������
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

	// ǰ���źŻ�����
	std::vector<float> fPrefixBuffer;
	long lPrefixBufferSize;
	long lPrefixBufferPos;
	float fPrefixLength;
	long lPrefixLengthSampleNumber;
	float fDropSuffixLength;
	
	// ��ȫ����С����û�������ź������ʱ�򣬲���һ����ô��ľ���
	float fSafeZoneLength;
	long lSafeZoneSize;
	JOB_STRUCT safeJob;

	// JSON����
	std::vector<roleStruct> roleList;
	int iSelectRoleIndex;

	double fSampleVolumeWorkActiveVal;
	// debug 
	long lNoOutputCount;

	// ʵʱģʽ
	bool bRealTimeMode;
	bool bRealTimeECO;

	// �������
	bool bVolumeDetectFine;
	float fLowVolumeDetectTime;

	// �����߳�״̬
	bool workStart;
	std::mutex mWorkerSafeExit;
	bool bWorkerNeedExit;

	// Model state
	juce::Value vServerUseTime;
	float fServerUseTime;
	juce::Value vAllUseTime;
	float fAllUseTime;
	// ʵʱģʽ�¶�������Ƶ���ݳ���
	juce::Value vDropDataLength;

private:
    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NetProcessJUCEVersionAudioProcessor)
};
