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
	
	// 安全缓冲区
	lSafeZoneSize = ceil(fSafeZoneLength * iDAWSampleRate / iHopSize) * iHopSize;
	safeJob.modelOutputSampleVector = std::vector<float>(lSafeZoneSize);

	// 前导缓冲区缓存初始化
	// 准备20s的缓冲区
	lPrefixBufferSize = static_cast<long>(20.0f * iDAWSampleRate);
	// 前导缓冲长度
	fPrefixBuffer = std::vector<float>(lPrefixBufferSize);
	lPrefixBufferPos = 0;

	lCrossFadeLength = ceil(1.0 * fCrossFadeLength * iDAWSampleRate / iHopSize) * iHopSize;
	hanningWindow = hanning(2 * lCrossFadeLength);
}

NetProcessJUCEVersionAudioProcessor::~NetProcessJUCEVersionAudioProcessor()
{
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
	iNumberOfChanel = 1;
	lNoOutputCount = 0;
	fServerUseTime = 0.f;

	clearState();

	lPrefixLengthSampleNumber = static_cast<long>(fPrefixLength * iDAWSampleRate);

	// worker线程安全退出相关信号
	bWorkerNeedExit = false;
	kRecordState = IDLE;
	// todo 单例
	workStart = false;
	if (!workStart) {
		runWorker();
		workStart = true;
	}
}

void NetProcessJUCEVersionAudioProcessor::clearState()
{
	// 清除前导缓冲和旧的模型输入缓冲区
	JOB_STRUCT inputJobStruct;
	modelOutputJob = inputJobStruct;
	modelOutputJob.modelOutputSampleVector = std::vector<float>();
	bHasMoreData = false;
	lRemainNeedSliceSampleNumber = lMaxSliceLengthSampleNumber;
	kRecordState = IDLE;
}

void NetProcessJUCEVersionAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
	bWorkerNeedExit = true;
	modelInputJobList.clear();
	modelInputJobList.reserve(8);
	prepareModelInputSample.clear();
	prepareModelInputSample.reserve(lMaxSliceLengthSampleNumber);
	modelOutputJobList.clear();
	modelOutputJobList.reserve(8);
	// 当子线程还在运行时，这个锁是锁上的，此时主线程还不能退出
	// 主线程通过将退出信号发给子线程，等待子线程安全退出后，释放锁，主线程再退出
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
	int dropCount = 0;
	bool bRealTimeModeNow = bRealTimeMode;
	float fPrefixLengthNow = fPrefixLength;
	long lPrefixLengthSampleNumberNow = lPrefixLengthSampleNumber;


	if (bRealTimeModeNow) {
		// 实时模式
		if (kRecordState == IDLE) {
			// 当前是空闲状态
			/*if (bEnableDebug) {
				OutputDebugStringA("切换到工作状态");
			}*/
			kRecordState = WORK;

			// 从IDLE切换到WORD，表示需要一段新的模型输入
			// 将前导缓冲区的数据写入到模型入参缓冲区中
			// 从前导缓冲区当前位置向前寻找lPrefixLengthSampleNumber个数据
			std::vector<float> prefixSampleVector(lPrefixLengthSampleNumberNow);
			int prefixSampleVectorWritePos = 0;
			int readPosStart = lPrefixBufferPos - lPrefixLengthSampleNumberNow;
			int readPosEnd = lPrefixBufferPos;
			if (readPosStart >= 0) {
				// 场景1：[.....start......end...]，直接从中间读取需要的数据
				for (int i = readPosStart; i < readPosEnd; i++) {
					prefixSampleVector[prefixSampleVectorWritePos++] = fPrefixBuffer[i];
				}
			}
			else {
				// 场景2：[.....end......start...]，需要从循环缓冲区尾部获取一些数据，然后再从头部读取一些数据
				readPosStart = lPrefixBufferSize + readPosStart;
				for (int i = readPosStart; i < lPrefixBufferSize; i++) {
					prefixSampleVector[prefixSampleVectorWritePos++] = fPrefixBuffer[i];
				}
				for (int i = 0; i < readPosEnd; i++) {
					prefixSampleVector[prefixSampleVectorWritePos++] = fPrefixBuffer[i];
				}
			};

			if (bEnableDebug) {
				snprintf(buff, sizeof(buff), "实时模式 - 前导缓冲区大小:%lld，时长:%.1fms\n", prefixSampleVector.size(), 1.0 * prefixSampleVector.size() / getSampleRate() * 1000);
				OutputDebugStringA(buff);
			}

			for (int i = 0; i < prefixSampleVector.size(); i++) {
				prepareModelInputSample.push_back(prefixSampleVector[i]);
			}

			// 将当前的音频数据写入到模型输入缓冲区中
			for (int i = 0; i < currentBlockVector.size(); i++) {
				prepareModelInputSample.push_back(currentBlockVector[i]);
				lRemainNeedSliceSampleNumber--;
			};
		}
		else {
			// 当前是工作状态
			// 只需要持续将当前的音频数据写入到模型入参缓冲区中
			for (int i = 0; i < currentBlockVector.size(); i++) {
				prepareModelInputSample.push_back(currentBlockVector[i]);
				lRemainNeedSliceSampleNumber--;
			}

			// 判断是否需要退出工作状态
			bool bExitWorkState = false;

			// 退出条件1：队列达到一定的大小
			if (lRemainNeedSliceSampleNumber <= 0) {
				bExitWorkState = true;
				/*if (bEnableDebug) {
					OutputDebugStringA("队列大小达到预期，直接调用模型\n");
				}*/
			}

			if (bExitWorkState) {
				// 需要退出工作状态
				// 1.模型输入队列加锁
				// 2.将当前准备的模型输入放入队列
				// 3.准备一个新的模型输入供下一次使用
				// 4.设置标记位，供消费者检查
				// 5.模型输入队列锁释放
				// 音量检查，如果整体是静音，则不需要经过模型处理什么，直接输出静音，避免静音进入模型后输出奇怪的声音
				if (bRealTimeECO) {
					double fSampleMax = -9999;
					for (juce::int32 i = 0; i < currentBlockVector.size(); i++) {
						// 获取当前块的最大音量
						float fCurrentSample = currentBlockVector[i];
						float fSampleAbs = std::abs(fCurrentSample);
						if (fSampleAbs > fSampleMax) {
							fSampleMax = fSampleAbs;
						}
					}
					bVolumeDetectFine = fSampleMax >= fSampleVolumeWorkActiveVal;
				}
				else {
					bVolumeDetectFine = true;
				}

				// 准备模型所需入参
				JOB_STRUCT inputJobStruct;
				inputJobStruct.bRealTimeModel = true;
				long prepareModelInputMatchHopSize = floor(1.0 * prepareModelInputSample.size() / iHopSize) * iHopSize;
				auto prepareModelInputMatchHopSample = std::vector<float>(prepareModelInputSample.begin(), prepareModelInputSample.begin() + prepareModelInputMatchHopSize);
				long remainSampleSize = prepareModelInputSample.size() - prepareModelInputMatchHopSize;
				inputJobStruct.lPrefixLength = lPrefixLengthSampleNumberNow;
				inputJobStruct.modelInputSampleVector = prepareModelInputMatchHopSample;
				inputJobStruct.lNewDataLength = prepareModelInputMatchHopSize - lPrefixLengthSampleNumberNow;
				inputJobStruct.bornTimeStamp = juce::Time::currentTimeMillis() - 1.0f * inputJobStruct.lNewDataLength / getSampleRate();
				if (bVolumeDetectFine) {
					inputJobStruct.jobType = JOB_WORK;
				}
				else {
					inputJobStruct.jobType = JOB_EMPTY;
				}
				modelInputJobListLock.enter();
				// 对于实时模式，未处理的数据积累到一定量的时候，可以丢弃
				auto dropModelInputJobListSize = modelInputJobList.size();
				if (dropModelInputJobListSize > 1) {
					modelInputJobList.clear();
					if (bEnableDebug) {
						snprintf(buff, sizeof(buff), "对于实时模式，旧数据还未处理的可以直接丢掉:%lld\n", dropModelInputJobListSize);
						OutputDebugStringA(buff);
					}
				}
				modelInputJobList.push_back(inputJobStruct);
				modelInputJobListLock.exit();
				std::vector<float> newprepareModelInputSample = std::vector<float>(prepareModelInputSample.begin() + prepareModelInputMatchHopSize, prepareModelInputSample.end());
				newprepareModelInputSample.reserve(lMaxSliceLengthSampleNumber);
				prepareModelInputSample = newprepareModelInputSample;
				kRecordState = IDLE;
				lRemainNeedSliceSampleNumber = lMaxSliceLengthSampleNumber - newprepareModelInputSample.size();
				if (bEnableDebug) {
					snprintf(buff, sizeof(buff), "实时模式 - 因为帧对齐，留给下次处理的数据：:%lld 时长：%.0fms\n", 
						newprepareModelInputSample.size(),
						1.0f * newprepareModelInputSample.size() / getSampleRate() * 1000);
					OutputDebugStringA(buff);
				}
			}
		};
		// 模型输出写到VST输出中
		tryGetFromModelOutputJobList();

		int outputWriteCount = 0;
		// 一直输出，直到没有数据可用，或者当前chunk写满
		bool bNeedOutput = bHasMoreData && outputWriteCount != currentBlockVector.size();
		while (bNeedOutput) {
				long lCanReadSize = min(modelOutputJob.modelOutputSampleVector.size(), currentBlockVector.size() - outputWriteCount);
				for (int i = 0; i < lCanReadSize; i++)
				{
					double currentSample = modelOutputJob.modelOutputSampleVector[i];
					inputOutputL[outputWriteCount] = static_cast<float>(currentSample);
					if (bHasRightChanel) {
						inputOutputR[outputWriteCount] = static_cast<float>(currentSample);
					};
					outputWriteCount++;
				}
				// 删除已经使用过的数据
				modelOutputJob.modelOutputSampleVector = std::vector<float>(modelOutputJob.modelOutputSampleVector.begin() + lCanReadSize, modelOutputJob.modelOutputSampleVector.end());
			tryGetFromModelOutputJobList();
			bNeedOutput = bHasMoreData && outputWriteCount != currentBlockVector.size();
		}

		// 判断当前是否有空输出
		bool hasEmptyBlock = outputWriteCount != currentBlockVector.size();
		if (hasEmptyBlock) {
			// 输出静音
			lNoOutputCount += 1;
			dropCount = 0;
			for (juce::int32 i = outputWriteCount; i < currentBlockVector.size(); i++) {
				dropCount++;
				inputOutputL[i] = 0.0000000001f;
				if (bHasRightChanel) {
					inputOutputR[i] = 0.0000000001f;
				}
			}

			// 入不敷出了，插入一个安全区
			safeJob.bornTimeStamp = juce::Time::currentTimeMillis() - 1.0 * lSafeZoneSize / getSampleRate();
			modelOutputJobListLock.enter();
			modelOutputJobList.push_back(safeJob);
			modelOutputJobListLock.exit();

			if (bEnableDebug) {
				snprintf(buff, sizeof(buff), "实时模式 - 输出缓冲空，次数:%ld，空白数据量：%d，时长: %.1fms，插入安全区：%d时长:%.0fms\n",
					lNoOutputCount,
					dropCount,
					1.0f * dropCount / getSampleRate() * 1000,
					lSafeZoneSize,
					1.0 * lSafeZoneSize / getSampleRate() * 1000);
				OutputDebugStringA(buff);
			}
		}
	}
	else {
		// 分句模式
		vAllUseTime.setValue(L"unCheck");
		// 音量检查
		double fSampleMax = -9999;
		for (juce::int32 i = 0; i < currentBlockVector.size(); i++) {
			// 获取当前块的最大音量
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
				snprintf(buff, sizeof(buff), "当前累积空闲时间:%f\n", fRecordIdleTime);
				OutputDebugStringA(buff);
			}*/
		}
		if (kRecordState == IDLE) {
			if (bVolumeDetectFine) {
				// 当前是空闲状态
				/*if (bEnableDebug) {
					OutputDebugStringA("切换到工作状态");
				}*/
				kRecordState = WORK;

				// 将当前的音频数据写入到模型输入缓冲区中
				for (int i = 0; i < currentBlockVector.size(); i++) {
					prepareModelInputSample.push_back(currentBlockVector[i]);
					lRemainNeedSliceSampleNumber--;
				};
			}
		}
		else {
			// 当前是工作状态
			// 只需要持续将当前的音频数据写入到模型入参缓冲区中
			for (int i = 0; i < currentBlockVector.size(); i++) {
				prepareModelInputSample.push_back(currentBlockVector[i]);
				lRemainNeedSliceSampleNumber--;
			}

			// 判断是否需要退出工作状态
			bool bExitWorkState = false;

			// 退出条件1：音量过小且持续超过一定时间
			if (fRecordIdleTime >= fLowVolumeDetectTime) {
				bExitWorkState = true;
				/*if (bEnableDebug) {
					OutputDebugStringA("音量过小且持续超过一定时间，直接调用模型\n");
				}*/
			}

			// 退出条件2：队列达到一定的大小
			if (lRemainNeedSliceSampleNumber <= 0) {
				bExitWorkState = true;
				/*if (bEnableDebug) {
					OutputDebugStringA("队列大小达到预期，直接调用模型\n");
				}*/
			}

			if (bExitWorkState) {
				// 需要退出工作状态
				// 1.模型输入队列加锁
				// 2.将当前准备的模型输入放入队列
				// 3.准备一个新的模型输入供下一次使用
				// 4.设置标记位，供消费者检查
				// 5.模型输入队列锁释放
				std::vector<float> newprepareModelInputSample;
				JOB_STRUCT jobStruct;
				jobStruct.jobType = JOB_WORK;
				jobStruct.modelInputSampleVector = prepareModelInputSample;
				jobStruct.bRealTimeModel = false;
				jobStruct.lPrefixLength = lPrefixLengthSampleNumberNow;
				modelInputJobListLock.enter();
				modelInputJobList.push_back(jobStruct);
				prepareModelInputSample = newprepareModelInputSample;
				kRecordState = IDLE;
				lRemainNeedSliceSampleNumber = lMaxSliceLengthSampleNumber;
				modelInputJobListLock.exit();
			}
		};

		// 模型输出写到VST输出中
		tryGetFromModelOutputJobList();

		int outputWriteCount = 0;
		// 一直输出，直到没有数据可用，或者当前chunk写满
		bool bNeedOutput = bHasMoreData && outputWriteCount != currentBlockVector.size();
		while (bNeedOutput) {
			long lCanReadSize = min(modelOutputJob.modelOutputSampleVector.size(), currentBlockVector.size() - outputWriteCount);
			for (int i = 0; i < lCanReadSize; i++)
			{
				double currentSample = modelOutputJob.modelOutputSampleVector[i];
				inputOutputL[outputWriteCount] = static_cast<float>(currentSample);
				if (bHasRightChanel) {
					inputOutputR[outputWriteCount] = static_cast<float>(currentSample);
				};
				outputWriteCount++;
			}
			// 删除已经使用过的数据
			modelOutputJob.modelOutputSampleVector = std::vector<float>(modelOutputJob.modelOutputSampleVector.begin() + lCanReadSize, modelOutputJob.modelOutputSampleVector.end());
			tryGetFromModelOutputJobList();
			bNeedOutput = bHasMoreData && outputWriteCount != currentBlockVector.size();
		}

		// 判断当前是否有空输出
		bool hasEmptyBlock = outputWriteCount != currentBlockVector.size();
		if (hasEmptyBlock) {
			// 无数据可以取了，输出静音
			lNoOutputCount += 1;
			if (bEnableDebug) {
				snprintf(buff, sizeof(buff), "!!!!!!!!!!!!!!!!!!!!!!!分句模式-输出缓冲空:%ld\n", lNoOutputCount);
				OutputDebugStringA(buff);
			}
			for (juce::int32 i = outputWriteCount; i < processerSampleBlockSize; i++) {
				// 对输出静音
				inputOutputL[i] = 0.0000000001f;
				if (bHasRightChanel) {
					inputOutputR[i] = 0.0000000001f;
				}
			}
		}
	};

	// 将当前的音频数据写入到前导缓冲区中，供下一次使用
	for (int i = 0; i < currentBlockVector.size(); i++) {
		fPrefixBuffer[lPrefixBufferPos++] = currentBlockVector[i];
		if (lPrefixBufferPos == lPrefixBufferSize) {
			lPrefixBufferPos = 0;
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
			lPrefixLengthSampleNumber = static_cast<long>(fPrefixLength * getSampleRate());
			fPitchChange = (float)xmlState->getDoubleAttribute("fPitchChange", 1.0);
			bRealTimeMode = (bool)xmlState->getBoolAttribute("bRealTimeMode", false);
			bEnableDebug = (bool)xmlState->getBoolAttribute("bEnableDebug", false);
			iSelectRoleIndex = (int)xmlState->getIntAttribute("iSelectRoleIndex", 0);
		}
}

void NetProcessJUCEVersionAudioProcessor::tryGetFromModelOutputJobList() {
	if (modelOutputJob.modelOutputSampleVector.size() > 0) {
		bHasMoreData = true;
		/*
		if (bEnableDebug) {
			snprintf(buff, sizeof(buff), "实时模式 - tryGetFromModelOutputJobList 当前job还有数据\n");
			OutputDebugStringA(buff);
		}*/
	}
	else {
		// 当前chunk数据取完了，按照job里的时间戳可以计算出当前整体延迟
		if (modelOutputJobList.size() > 0) {
			bHasMoreData = true;
			modelOutputJobListLock.enter();
			modelOutputJob = modelOutputJobList.at(0);
			modelOutputJobList.erase(modelOutputJobList.begin());
			modelOutputJobListLock.exit();
			// 延迟计算，以及延迟补偿计算

			if (modelOutputJob.jobType == JOB_EMPTY) {
				vAllUseTime.setValue(juce::String(L"0ms"));
			}
			else if (modelOutputJob.bRealTimeModel) {
				auto timeNow = juce::Time::currentTimeMillis();
				long fAllUseTime = timeNow - modelOutputJob.bornTimeStamp;
				fAllUseTime += lCrossFadeLength / getSampleRate();
				vAllUseTime.setValue(juce::String(fAllUseTime) + "ms");
			}

			/*
			if (bEnableDebug) {
				snprintf(buff, sizeof(buff), "实时模式 - tryGetFromModelOutputJobList 当前job没数据，从list拿一个job\n");
				OutputDebugStringA(buff);
			}*/
		}
		else {
			bHasMoreData = false;
			/*
			if (bEnableDebug) {
				snprintf(buff, sizeof(buff), "实时模式 - tryGetFromModelOutputJobList 当前job无数据，List空\n");
				OutputDebugStringA(buff);
			}*/
		}
	}
}

using namespace juce;
void NetProcessJUCEVersionAudioProcessor::loadConfig()
{
	// 获取插件安装目录
	File pluginDir = File::getSpecialLocation(File::currentExecutableFile).getParentDirectory();
	// 创建配置文件
	File configFile = pluginDir.getChildFile("netProcessConfig.json");

	// default value
	fMaxSliceLength = 5.0f;
	fMaxSliceLengthForRealTimeMode = fMaxSliceLength;
	fMaxSliceLengthForSentenceMode = fMaxSliceLength;
	fLowVolumeDetectTime = 0.4f;
	fPrefixLength = 0.0f;
	lMaxSliceLengthSampleNumber = static_cast<long>(getSampleRate() * fMaxSliceLength);
	lRemainNeedSliceSampleNumber = lMaxSliceLengthSampleNumber;
	fPitchChange = 0.0f;
	bRealTimeMode = false;
	bEnableDebug = false;
	iSelectRoleIndex = 0;
	fSafeZoneLength = 0.05f;
	fCrossFadeLength = 0.f;
	iHopSize = 512;

	// 读取JSON配置文件
	FileInputStream configFileInStream(configFile);
	juce::var jsonVar = juce::JSON::parse(configFileInStream);
	auto& props = jsonVar.getDynamicObject()->getProperties();
	fSampleVolumeWorkActiveVal = props["fSampleVolumeWorkActiveVal"];
	fSafeZoneLength = props["fSafeZoneLength"];
	fCrossFadeLength = props["fCrossFadeLength"];
	iHopSize = props["iHopSize"];
	bRealTimeECO = props["bRealTimeECO"];
	iDAWSampleRate = props["DAWSampleRate"];

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

void NetProcessJUCEVersionAudioProcessor::runWorker()
{
    // 启动Worker线程
    std::thread(func_do_voice_transfer_worker,
        iNumberOfChanel,					// 通道数量
        getSampleRate(),		            // 项目采样率

		&modelInputJobList,					// 模型输入队列
		&modelInputJobListLock,			// 模型输入队列锁

		&modelOutputJobList,				// 模型输出队列
		&modelOutputJobListLock,			// 模型输出队列锁

		lCrossFadeLength,
		&hanningWindow,

        &fPitchChange,						// 音调变化数值

        roleList,							// 配置的可用音色列表
        &iSelectRoleIndex,					// 选择的角色ID

		&bEnableDebug,						// 占位符，启用DEBUG输出
		vServerUseTime,						// UI变量，显示服务调用耗时
		&fServerUseTime,

        &bWorkerNeedExit,					// 占位符，表示worker线程需要退出
        &mWorkerSafeExit					// 互斥锁，表示worker线程已经安全退出
    ).detach();


}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NetProcessJUCEVersionAudioProcessor();
}
