/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <Windows.h>
#include <fstream>
#include "AudioWork.h"
using namespace std::chrono;

//==============================================================================
NetProcessJUCEVersionAudioProcessor::NetProcessJUCEVersionAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       )
#endif
{
	loadConfig();
}

NetProcessJUCEVersionAudioProcessor::~NetProcessJUCEVersionAudioProcessor()
{
	loadConfig();
}

//==============================================================================
const juce::String NetProcessJUCEVersionAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool NetProcessJUCEVersionAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool NetProcessJUCEVersionAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool NetProcessJUCEVersionAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double NetProcessJUCEVersionAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int NetProcessJUCEVersionAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int NetProcessJUCEVersionAudioProcessor::getCurrentProgram()
{
    return 0;
}

void NetProcessJUCEVersionAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String NetProcessJUCEVersionAudioProcessor::getProgramName (int index)
{
    return {};
}

void NetProcessJUCEVersionAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void NetProcessJUCEVersionAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Use this method as the place to do any pre-playback
    // initialisation that you need..
	// reset state and buffer
	iNumberOfChanel = 1;
	lNoOutputCount = 0;

	// ǰ�������������ʼ��
	// ׼��20s�Ļ�����
	lPrefixBufferSize = static_cast<long>(20.0f * sampleRate);
	// ǰ�����峤��
	lPrefixLengthSampleNumber = static_cast<long>(fPrefixLength * sampleRate);
	fPrefixBuffer = (float*)std::malloc(sizeof(float) * lPrefixBufferSize);
	lPrefixBufferPos = 0;
	// hopSize��д��
	iHopSize = 512;

	lModelOutputBufferSize = static_cast<long>(fModelOutputBufferSecond * sampleRate);
	fModeulOutputSampleBuffer = (float*)(std::malloc(sizeof(float) * lModelOutputBufferSize));
	lModelOutputSampleBufferReadPos = 0;
	lModelOutputSampleBufferWritePos = 0;

	fSafeZoneLength = 0.12f;
	lSafeZoneSize = ceil(1.0f * fSafeZoneLength * getSampleRate() / iHopSize) * iHopSize;

	clearState();

	// worker�̰߳�ȫ�˳�����ź�
	bWorkerNeedExit = false;
	kRecordState = IDLE;
	// todo ����
	workStart = false;
	if (!workStart) {
		runWorker();
		workStart = true;
	}
}

void NetProcessJUCEVersionAudioProcessor::clearState()
{
	// ���ǰ������;ɵ�ģ�����뻺����
	lastVoiceSampleForCrossFadeVectorMutex.lock();
	lastVoiceSampleCrossFadeSkipNumber = 0;
	lMaxAllowEmptySampleNumber = getSampleRate() * fMaxAllowEmptySampleLength;
	lEmptySampleNumberCounter = 0;
	modelInputJobList.clear();
	prepareModelInputSample.clear();
	lastVoiceSampleForCrossFadeVector.clear();
	lastVoiceSampleForCrossFadeVectorMutex.unlock();
}

void NetProcessJUCEVersionAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
	bWorkerNeedExit = true;
	// �����̻߳�������ʱ������������ϵģ���ʱ���̻߳������˳�
	// ���߳�ͨ�����˳��źŷ������̣߳��ȴ����̰߳�ȫ�˳����ͷ��������߳����˳�
	mWorkerSafeExit.lock();
	mWorkerSafeExit.unlock();
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool NetProcessJUCEVersionAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void NetProcessJUCEVersionAudioProcessor::processBlock (juce::AudioBuffer<float>& audioBuffer, juce::MidiBuffer& midiMessages)
{
	if (bConfigLoadFinished) {
		juce::ScopedNoDenormals noDenormals;
		auto totalNumInputChannels = getTotalNumInputChannels();
		auto totalNumOutputChannels = getTotalNumOutputChannels();

		// In case we have more outputs than inputs, this code clears any output
		// channels that didn't contain input data, (because these aren't
		// guaranteed to be empty - they may contain garbage).
		// This is here to avoid people getting screaming feedback
		// when they first compile a plugin, but obviously you don't need to keep
		// this code if your algorithm always overwrites all the output channels.
		int processerSampleBlockSize = audioBuffer.getNumSamples();
		for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i) {
			audioBuffer.clear(i, 0, processerSampleBlockSize);
		}

		float* inputOutputL = audioBuffer.getWritePointer(0);
		float* inputOutputR = NULL;
		bool bHasRightChanel = false;
		if (getNumOutputChannels() > 1) {
			inputOutputR = audioBuffer.getWritePointer(1);
			bHasRightChanel = true;
		}
		else {
			inputOutputR = inputOutputL;
		}
		auto currentBlockVector = std::vector(inputOutputL, inputOutputL + processerSampleBlockSize);
		if (bRealTimeMode) {
			// ʵʱģʽ
			if (kRecordState == IDLE) {
				// ��ǰ�ǿ���״̬
				/*if (bEnableDebug) {
					OutputDebugStringA("�л�������״̬");
				}*/
				kRecordState = WORK;

				// ��IDLE�л���WORD����ʾ��Ҫһ���µ�ģ������
				// ��ǰ��������������д�뵽ģ����λ�������
				// ��ǰ����������ǰλ����ǰѰ��lPrefixLengthSampleNumber������
				std::vector<float> prefixSampleVector;
				int readPosStart = lPrefixBufferPos - lPrefixLengthSampleNumber;
				int readPosEnd = lPrefixBufferPos;
				if (readPosStart >= 0) {
					// ����1��[.....start......end...]��ֱ�Ӵ��м��ȡ��Ҫ������
					for (int i = readPosStart; i < readPosEnd; i++) {
						prefixSampleVector.push_back(fPrefixBuffer[i]);
					}
				}
				else {
					// ����2��[.....end......start...]����Ҫ��ѭ��������β����ȡһЩ���ݣ�Ȼ���ٴ�ͷ����ȡһЩ����
					readPosStart = lPrefixBufferSize + readPosStart - 1;
					for (int i = readPosStart; i < lPrefixBufferSize; i++) {
						prefixSampleVector.push_back(fPrefixBuffer[i]);
					}
					for (int i = 0; i < readPosEnd; i++) {
						prefixSampleVector.push_back(fPrefixBuffer[i]);
					}
				};

				if (bEnableDebug) {
					snprintf(buff, sizeof(buff), "ʵʱģʽ - ǰ����������С:%lld��ʱ��:%.1fms\n", prefixSampleVector.size(), 1.0 * prefixSampleVector.size() / getSampleRate() * 1000);
					OutputDebugStringA(buff);
				}

				for (int i = 0; i < prefixSampleVector.size(); i++) {
					prepareModelInputSample.push_back(prefixSampleVector[i]);
				}

				// ����ǰ����Ƶ����д�뵽ģ�����뻺������
				for (int i = 0; i < currentBlockVector.size(); i++) {
					prepareModelInputSample.push_back(currentBlockVector[i]);
					lRemainNeedSliceSampleNumber--;
				};
			}
			else {
				// ��ǰ�ǹ���״̬
				// ֻ��Ҫ��������ǰ����Ƶ����д�뵽ģ����λ�������
				for (int i = 0; i < currentBlockVector.size(); i++) {
					prepareModelInputSample.push_back(currentBlockVector[i]);
					lRemainNeedSliceSampleNumber--;
				}

				// �ж��Ƿ���Ҫ�˳�����״̬
				bool bExitWorkState = false;

				// �˳�����1�����дﵽһ���Ĵ�С
				if (lRemainNeedSliceSampleNumber <= 0) {
					bExitWorkState = true;
					/*if (bEnableDebug) {
						OutputDebugStringA("���д�С�ﵽԤ�ڣ�ֱ�ӵ���ģ��\n");
					}*/
				}

				if (bExitWorkState) {
					// ��Ҫ�˳�����״̬
					// 1.ģ��������м���
					// 2.����ǰ׼����ģ������������
					// 3.׼��һ���µ�ģ�����빩��һ��ʹ��
					// 4.���ñ��λ���������߼��
					// 5.ģ������������ͷ�
					// ������飬��������Ǿ���������Ҫ����ģ�ʹ���ʲô��ֱ��������������⾲������ģ�ͺ������ֵ�����
					double fSampleMax = -9999;
					for (juce::int32 i = 0; i < currentBlockVector.size(); i++) {
						// ��ȡ��ǰ����������
						float fCurrentSample = currentBlockVector[i];
						float fSampleAbs = std::abs(fCurrentSample);
						if (fSampleAbs > fSampleMax) {
							fSampleMax = fSampleAbs;
						}
					}
					bVolumeDetectFine = fSampleMax >= fSampleVolumeWorkActiveVal;
					bVolumeDetectFine = true;
					modelInputJobListMutex.lock();
					// ����ʵʱģʽ�������ݻ�δ����Ŀ���ֱ�Ӷ���
					if (bEnableDebug) {
						auto dropModelInputJobListSize = modelInputJobList.size();
						if (dropModelInputJobListSize > 0) {
							snprintf(buff, sizeof(buff), "����ʵʱģʽ�������ݻ�δ����Ŀ���ֱ�Ӷ���:%lld\n", dropModelInputJobListSize);
							OutputDebugStringA(buff);
						}
					}
					modelInputJobList.clear();
					INPUT_JOB_STRUCT inputJobStruct;
					long prepareModelInputMatchHopSize = floor(1.0 * prepareModelInputSample.size() / iHopSize) * iHopSize;
					auto prepareModelInputMatchHopSample = std::vector<float>(prepareModelInputSample.begin(), prepareModelInputSample.begin() + prepareModelInputMatchHopSize);
					if (bVolumeDetectFine) {
						inputJobStruct.jobType = JOB_WORK;
						inputJobStruct.modelInputSampleVector = prepareModelInputMatchHopSample;
						modelInputJobList.push_back(inputJobStruct);
					}
					else {
						inputJobStruct.jobType = JOB_EMPTY;
						inputJobStruct.emptySampleNumber = prepareModelInputMatchHopSample.size();
						modelInputJobList.push_back(inputJobStruct);
					}
					std::vector<float> newprepareModelInputSample = std::vector<float>(prepareModelInputSample.begin() + prepareModelInputMatchHopSize, prepareModelInputSample.end());
					prepareModelInputSample = newprepareModelInputSample;
					kRecordState = IDLE;
					lRemainNeedSliceSampleNumber = lMaxSliceLengthSampleNumber - newprepareModelInputSample.size();
					modelInputJobListMutex.unlock();
				}
			};

			// ���֮ǰ�Ƿ������Ϊ�ӳٵ��µĿ�����������������������ռ�������С����һ��������
			bool bHasMoreData;
			// �ȴ������������������
			int dropCount = 0;
			if (lEmptySampleNumberCounter > lMaxAllowEmptySampleNumber) {
				for (int i = 0; i < lEmptySampleNumberCounter - lMaxAllowEmptySampleNumber; i++) {
					bHasMoreData = lModelOutputSampleBufferReadPos != lModelOutputSampleBufferWritePos;
					if (bHasMoreData) {
						lModelOutputSampleBufferReadPos++;
						if (lModelOutputSampleBufferReadPos == lModelOutputBufferSize) {
							lModelOutputSampleBufferReadPos = 0;
						}
						lEmptySampleNumberCounter--;
						dropCount++;
					}
					else {
						break;
					}
				};
				if (bEnableDebug) {
					snprintf(buff, sizeof(buff), "ʵʱģʽ - ��Ϊ�ӳٶ������������������������:%d\n", dropCount);
					OutputDebugStringA(buff);
				}
			};

			dropCount = 0;
			// �ٴӴ����浭���������ж�����
			if (lEmptySampleNumberCounter > lMaxAllowEmptySampleNumber) {
				lastVoiceSampleForCrossFadeVectorMutex.lock();
				int peekDataSize = min(lEmptySampleNumberCounter - lMaxAllowEmptySampleNumber, lastVoiceSampleForCrossFadeVector.size());
				if (peekDataSize > 0) {
					lastVoiceSampleCrossFadeSkipNumber += peekDataSize;
					if (bEnableDebug) {
						snprintf(buff, sizeof(buff), "ʵʱģʽ - ʹ�ý��浭��������ǰ�������������:%ld\n", lNoOutputCount);
						OutputDebugStringA(buff);
					}
					for (int i = 0; i < peekDataSize; i++) {
						lEmptySampleNumberCounter--;
						dropCount++;
						lastVoiceSampleForCrossFadeVector.erase(lastVoiceSampleForCrossFadeVector.begin());
					}
				};
				lastVoiceSampleForCrossFadeVectorMutex.unlock();
				if (bEnableDebug) {
					snprintf(buff, sizeof(buff), "!!!!!!!!!!!!!!!!!!!!!!!ʵʱģʽ-��Ϊ�ӳٶ������Ӵ����浭���������ж�����:%d\n", dropCount);
					OutputDebugStringA(buff);
				}
			};

			// ģ�����д��VST�����
			int outputWritePos = 0;
			bHasMoreData = lModelOutputSampleBufferReadPos != lModelOutputSampleBufferWritePos;
			if (bHasMoreData) {
				for (int i = 0; i < currentBlockVector.size(); i++)
				{
					bHasMoreData = lModelOutputSampleBufferReadPos != lModelOutputSampleBufferWritePos;
					if (!bHasMoreData) {
						break;
					}
					double currentSample = fModeulOutputSampleBuffer[lModelOutputSampleBufferReadPos++];
					if (lModelOutputSampleBufferReadPos == lModelOutputBufferSize) {
						lModelOutputSampleBufferReadPos = 0;
					}
					inputOutputL[i] = static_cast<float>(currentSample);
					if (bHasRightChanel) {
						inputOutputR[i] = static_cast<float>(currentSample);
					};
					outputWritePos = i + 1;
				}
			}
			// �жϵ�ǰ�Ƿ��п����
			bool hasEmptyBlock = outputWritePos != currentBlockVector.size();
			if (hasEmptyBlock) {
				// �ӡ������浭�����ݡ�����ȡ��һ��������
				int blockRemainNeedSampleNumber = currentBlockVector.size() - outputWritePos;
				lastVoiceSampleForCrossFadeVectorMutex.lock();
				int peekDataSize = min(blockRemainNeedSampleNumber, lastVoiceSampleForCrossFadeVector.size());
				if (peekDataSize > 0) {
					lastVoiceSampleCrossFadeSkipNumber += peekDataSize;
					dropCount = 0;
					for (int i = 0; i < peekDataSize; i++) {
						auto peekSample = lastVoiceSampleForCrossFadeVector.at(0);
						lastVoiceSampleForCrossFadeVector.erase(lastVoiceSampleForCrossFadeVector.begin());
						inputOutputL[outputWritePos] = static_cast<float>(peekSample);
						if (bHasRightChanel) {
							inputOutputR[outputWritePos] = static_cast<float>(peekSample);
						};
						outputWritePos++;
						dropCount++;
					}
					if (bEnableDebug) {
						snprintf(buff, sizeof(buff), "ʵʱģʽ - ����������ʹ�ý��浭��������ǰ�����ʹ�õ�������:%d��ʱ�� : %.1fms\n", dropCount, 1.0f * dropCount / getSampleRate()*1000);
						OutputDebugStringA(buff);
					}
				};
				lastVoiceSampleForCrossFadeVectorMutex.unlock();
				hasEmptyBlock = outputWritePos != currentBlockVector.size();
				if (hasEmptyBlock) {
					// ���п��������ʱ�Ѿ�û�����ݿ�������
					// ���������ۼӣ�ͨ�����ӳٶ�������������Ƶ������˴���¼���˶�ã��ں�������Ƶ�����ʱ��Ϊ�˷�ֹ�ӳ��ۼӣ���Ҫ������һ�������ݣ�
					auto emptySampleNumber = currentBlockVector.size() - outputWritePos;
					lEmptySampleNumberCounter += emptySampleNumber;
					// �������
					lNoOutputCount += 1;
					dropCount = 0;
					for (juce::int32 i = outputWritePos; i < currentBlockVector.size(); i++) {
						// ���������
						dropCount++;
						inputOutputL[i] = 0.0000000001f;
						if (bHasRightChanel) {
							inputOutputR[i] = 0.0000000001f;
						}
					}
					// �벻����ˣ�����һ����ȫ��
					lastVoiceSampleForCrossFadeVectorMutex.lock();
					for (int i = 0; i < lSafeZoneSize; i++) {
						fModeulOutputSampleBuffer[lModelOutputSampleBufferWritePos++] = 0.f;
						if (lModelOutputSampleBufferWritePos == lModelOutputBufferSize) {
							lModelOutputSampleBufferWritePos = 0;
						}
					};
					lastVoiceSampleForCrossFadeVectorMutex.unlock();

					if (bEnableDebug) {
						snprintf(buff, sizeof(buff), "ʵʱģʽ - �������գ�����:%ld���հ���������%d��ʱ��: %.1f�����밲ȫ����%d\n", 
							lNoOutputCount,
							dropCount, 
							1.0f * dropCount / getSampleRate() * 1000,
							lSafeZoneSize);
						OutputDebugStringA(buff);
					}
				}
			}
		}
		else {
			// �־�ģʽ
			// �������
			double fSampleMax = -9999;
			for (juce::int32 i = 0; i < currentBlockVector.size(); i++) {
				// ��ȡ��ǰ����������
				float fCurrentSample = currentBlockVector[i];
				float fSampleAbs = std::abs(fCurrentSample);
				if (fSampleAbs > fSampleMax) {
					fSampleMax = fSampleAbs;
				}
			}
			bVolumeDetectFine = fSampleMax >= fSampleVolumeWorkActiveVal;
			if (bVolumeDetectFine) {
				fRecordIdleTime = 0.f;
			} else {
				fRecordIdleTime += 1.f * currentBlockVector.size() / getSampleRate();
				/*if (bEnableDebug) {
					snprintf(buff, sizeof(buff), "��ǰ�ۻ�����ʱ��:%f\n", fRecordIdleTime);
					OutputDebugStringA(buff);
				}*/
			}
			if (kRecordState == IDLE) {
				if (bVolumeDetectFine) {
					// ��ǰ�ǿ���״̬
					/*if (bEnableDebug) {
						OutputDebugStringA("�л�������״̬");
					}*/
					kRecordState = WORK;

					// ����ǰ����Ƶ����д�뵽ģ�����뻺������
					for (int i = 0; i < currentBlockVector.size(); i++) {
						prepareModelInputSample.push_back(currentBlockVector[i]);
						lRemainNeedSliceSampleNumber--;
					};
				}
			}
			else {
				// ��ǰ�ǹ���״̬
				// ֻ��Ҫ��������ǰ����Ƶ����д�뵽ģ����λ�������
				for (int i = 0; i < currentBlockVector.size(); i++) {
					prepareModelInputSample.push_back(currentBlockVector[i]);
					lRemainNeedSliceSampleNumber--;
				}

				// �ж��Ƿ���Ҫ�˳�����״̬
				bool bExitWorkState = false;

				// �˳�����1��������С�ҳ�������һ��ʱ��
				if (fRecordIdleTime >= fLowVolumeDetectTime) {
					bExitWorkState = true;
					/*if (bEnableDebug) {
						OutputDebugStringA("������С�ҳ�������һ��ʱ�䣬ֱ�ӵ���ģ��\n");
					}*/
				}

				// �˳�����2�����дﵽһ���Ĵ�С
				if (lRemainNeedSliceSampleNumber <= 0) {
					bExitWorkState = true;
					/*if (bEnableDebug) {
						OutputDebugStringA("���д�С�ﵽԤ�ڣ�ֱ�ӵ���ģ��\n");
					}*/
				}

				if (bExitWorkState) {
					// ��Ҫ�˳�����״̬
					// 1.ģ��������м���
					// 2.����ǰ׼����ģ������������
					// 3.׼��һ���µ�ģ�����빩��һ��ʹ��
					// 4.���ñ��λ���������߼��
					// 5.ģ������������ͷ�
					std::vector<float> newprepareModelInputSample;
					INPUT_JOB_STRUCT jobStruct;
					jobStruct.jobType = JOB_WORK;
					jobStruct.modelInputSampleVector = prepareModelInputSample;
					modelInputJobListMutex.lock();
					modelInputJobList.push_back(jobStruct);
					prepareModelInputSample = newprepareModelInputSample;
					kRecordState = IDLE;
					lRemainNeedSliceSampleNumber = lMaxSliceLengthSampleNumber;
					modelInputJobListMutex.unlock();
				}
			};

			// ģ�����д��VST�����
			int outputWritePos = 0;
			bool bHasMoreData = lModelOutputSampleBufferReadPos != lModelOutputSampleBufferWritePos;
			if (bHasMoreData) {
				for (int i = 0; i < currentBlockVector.size(); i++)
				{
					bHasMoreData = lModelOutputSampleBufferReadPos != lModelOutputSampleBufferWritePos;
					if (!bHasMoreData) {
						break;
					}
					double currentSample = fModeulOutputSampleBuffer[lModelOutputSampleBufferReadPos++];
					if (lModelOutputSampleBufferReadPos == lModelOutputBufferSize) {
						lModelOutputSampleBufferReadPos = 0;
					}
					inputOutputL[i] = static_cast<float>(currentSample);
					if (bHasRightChanel) {
						inputOutputR[i] = static_cast<float>(currentSample);
					};
					outputWritePos = i + 1;
				}
			}
			// �жϵ�ǰ�Ƿ��п����
			bool hasEmptyBlock = outputWritePos != currentBlockVector.size();
			if (hasEmptyBlock) {
				// �����ݿ���ȡ�ˣ��������
				lNoOutputCount += 1;
				if (bEnableDebug) {
					snprintf(buff, sizeof(buff), "!!!!!!!!!!!!!!!!!!!!!!!�־�ģʽ-��������:%ld\n", lNoOutputCount);
					OutputDebugStringA(buff);
				}
				for (juce::int32 i = outputWritePos; i < processerSampleBlockSize; i++) {
					// ���������
					inputOutputL[i] = 0.0000000001f;
					if (bHasRightChanel) {
						inputOutputR[i] = 0.0000000001f;
					}
				}
			}
		};

		// ����ǰ����Ƶ����д�뵽ǰ���������У�����һ��ʹ��
		for (int i = 0; i < currentBlockVector.size(); i++) {
			fPrefixBuffer[lPrefixBufferPos++] = currentBlockVector[i];
			if (lPrefixBufferPos == lPrefixBufferSize) {
				lPrefixBufferPos = 0;
			}
		}
	}
}

//==============================================================================
bool NetProcessJUCEVersionAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* NetProcessJUCEVersionAudioProcessor::createEditor()
{
    return new NetProcessJUCEVersionAudioProcessorEditor (*this);
}

//==============================================================================
void NetProcessJUCEVersionAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
	std::unique_ptr<juce::XmlElement> xml(new juce::XmlElement("Config"));
	xml->setAttribute("fMaxSliceLength", (double)fMaxSliceLength);
	xml->setAttribute("fMaxSliceLengthForRealTimeMode", (double)fMaxSliceLengthForRealTimeMode);
	xml->setAttribute("fMaxSliceLengthForSentenceMode", (double)fMaxSliceLengthForSentenceMode);
	xml->setAttribute("fLowVolumeDetectTime", (double)fLowVolumeDetectTime);
	xml->setAttribute("fPrefixLength", (double)fPrefixLength);
	xml->setAttribute("fDropSuffixLength", (double)fDropSuffixLength);
	xml->setAttribute("fPitchChange", (double)fPitchChange);
	xml->setAttribute("bRealTimeMode", bRealTimeMode);
	xml->setAttribute("bEnableDebug", bEnableDebug);
	xml->setAttribute("iSelectRoleIndex", iSelectRoleIndex);
	copyXmlToBinary(*xml, destData);
}

void NetProcessJUCEVersionAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{	
	// load last state
	std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

	if (xmlState.get() != nullptr)
		if (xmlState->hasTagName("Config")) {
			fMaxSliceLength = (float)xmlState->getDoubleAttribute("fMaxSliceLength", 1.0);
			fMaxSliceLengthForRealTimeMode = (float)xmlState->getDoubleAttribute("fMaxSliceLengthForRealTimeMode", 1.0);
			fMaxSliceLengthForSentenceMode = (float)xmlState->getDoubleAttribute("fMaxSliceLengthForSentenceMode", 1.0);
			lMaxSliceLengthSampleNumber = static_cast<long>(getSampleRate() * fMaxSliceLength);
			lRemainNeedSliceSampleNumber = lMaxSliceLengthSampleNumber;
			fLowVolumeDetectTime = (float)xmlState->getDoubleAttribute("fLowVolumeDetectTime", 0.4);
			fPrefixLength = (float)xmlState->getDoubleAttribute("fPrefixLength", 0.0);
			fDropSuffixLength = (float)xmlState->getDoubleAttribute("fDropSuffixLength", 0.0);
			fPitchChange = (float)xmlState->getDoubleAttribute("fPitchChange", 1.0);
			bRealTimeMode = (bool)xmlState->getBoolAttribute("bRealTimeMode", false);
			bEnableDebug = (bool)xmlState->getBoolAttribute("bEnableDebug", false);
			iSelectRoleIndex = (int)xmlState->getIntAttribute("iSelectRoleIndex", 0);
		}
}

void NetProcessJUCEVersionAudioProcessor::loadConfig()
{
	std::wstring sDllPath = L"C:/Program Files/Common Files/VST3/NetProcessJUCEVersion/samplerate.dll";
	std::string sJsonConfigFileName = "C:/Program Files/Common Files/VST3/NetProcessJUCEVersion/netProcessConfig.json";
	
	if (!bConfigLoadFinished) {
		// default value
		fMaxSliceLength = 5.0f;
		fMaxSliceLengthForRealTimeMode = fMaxSliceLength;
		fMaxSliceLengthForSentenceMode = fMaxSliceLength;
		fLowVolumeDetectTime = 0.4f;
		fPrefixLength = 0.0f;
		fDropSuffixLength = 0.0f;
		lMaxSliceLengthSampleNumber = static_cast<long>(getSampleRate() * fMaxSliceLength);
		lRemainNeedSliceSampleNumber = lMaxSliceLengthSampleNumber;
		fPitchChange = 0.0f;
		bRealTimeMode = false;
		bEnableDebug = false;
		iSelectRoleIndex = 0;

		auto dllClient = LoadLibraryW(sDllPath.c_str());
		if (dllClient != NULL) {
			dllFuncSrcSimple = (FUNC_SRC_SIMPLE)GetProcAddress(dllClient, "src_simple");
		}
		else {
			OutputDebugStringA("samplerate.dll load Error!");
		}

		// ��ȡJSON�����ļ�
		std::ifstream t_pc_file(sJsonConfigFileName, std::ios::binary);
		std::stringstream buffer_pc_file;
		buffer_pc_file << t_pc_file.rdbuf();

		juce::var jsonVar;
		if (juce::JSON::parse(buffer_pc_file.str(), jsonVar).wasOk()) {
			auto& props = jsonVar.getDynamicObject()->getProperties();
			bEnableSOVITSPreResample = props["bEnableSOVITSPreResample"];
			iSOVITSModelInputSamplerate = props["iSOVITSModelInputSamplerate"];
			bEnableHUBERTPreResample = props["bEnableHUBERTPreResample"];
			iHUBERTInputSampleRate = props["iHUBERTInputSampleRate"];
			fSampleVolumeWorkActiveVal = props["fSampleVolumeWorkActiveVal"];
			fMaxAllowEmptySampleLength = props["fMaxAllowEmptySampleLength"];

			roleList.clear();
			auto jsonRoleList = props["roleList"];
			int iRoleSize = jsonRoleList.size();
			for (int i = 0; i < iRoleSize; i++) {
				auto& roleListI = jsonRoleList[i].getDynamicObject()->getProperties();
				std::string apiUrl = roleListI["apiUrl"].toString().toStdString();
				std::string name = roleListI["name"].toString().toStdString();
				std::string speakId = roleListI["speakId"].toString().toStdString();
				roleStruct role;
				role.sSpeakId = speakId;
				role.sName = name;
				role.sApiUrl = apiUrl;
				role.iHopSize = 512;
				roleList.push_back(role);
			}
			if (iSelectRoleIndex + 1 > iRoleSize || iSelectRoleIndex < 0) {
				iSelectRoleIndex = 0;
			}
		}
		else {
			// error read json
		};

		bConfigLoadFinished = true;
	}
}

void NetProcessJUCEVersionAudioProcessor::runWorker()
{
    // ����Worker�߳�
    std::thread(func_do_voice_transfer_worker,
        iNumberOfChanel,					// ͨ������
        getSampleRate(),		            // ��Ŀ������

		&modelInputJobList,					// ģ���������
		&modelInputJobListMutex,			// ģ�����������

		lModelOutputBufferSize,				// ģ�������������С
        fModeulOutputSampleBuffer,			// ģ�����������
        &lModelOutputSampleBufferReadPos,	// ģ�������������ָ��
        &lModelOutputSampleBufferWritePos,	// ģ�����������дָ��

		&lastVoiceSampleForCrossFadeVectorMutex,
		&lastVoiceSampleForCrossFadeVector, //���һ��ģ�������Ƶ��β�������ڽ��浭������
		&lastVoiceSampleCrossFadeSkipNumber,

		&fPrefixLength,						// ǰ��������ʱ��(s)
		&fDropSuffixLength,					// ������β��ʱ��(s)
        &fPitchChange,						// �����仯��ֵ
        &bCalcPitchError,					// �������������

        roleList,							// ���õĿ�����ɫ�б�
        &iSelectRoleIndex,					// ѡ��Ľ�ɫID
        dllFuncSrcSimple,					// DLL�ڲ�SrcSimple����

        &bEnableSOVITSPreResample,			// ����SOVITSģ�������Ƶ�ز���Ԥ����
        iSOVITSModelInputSamplerate,		// SOVITSģ����β�����
        &bEnableHUBERTPreResample,			// ����HUBERTģ�������Ƶ�ز���Ԥ����
        iHUBERTInputSampleRate,				// HUBERTģ����β�����

        &bRealTimeMode,					    // ռλ����ʵʱģʽ
		&bEnableDebug,						// ռλ��������DEBUG���
		vServerUseTime,						// UI��������ʾ������ú�ʱ
		vDropDataLength,					// UI��������ʾʵʱģʽ�¶�������Ƶ���ݳ���

        &bWorkerNeedExit,					// ռλ������ʾworker�߳���Ҫ�˳�
        &mWorkerSafeExit					// ����������ʾworker�߳��Ѿ���ȫ�˳�
    ).detach();


}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NetProcessJUCEVersionAudioProcessor();
}
