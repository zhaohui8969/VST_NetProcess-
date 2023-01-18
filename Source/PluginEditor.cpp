/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "params.h"
#include "AudioFile.h"
#include "httplib.h"
using namespace std::chrono;

//==============================================================================

NetProcessJUCEVersionAudioProcessorEditor::NetProcessJUCEVersionAudioProcessorEditor(NetProcessJUCEVersionAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setSize (400, 160);

    std::wstring sDllPath = L"C:/Program Files/Common Files/VST3/samplerate.dll";
    auto dllClient = LoadLibraryW(sDllPath.c_str());
    if (dllClient != NULL) {
        audioProcessor.dllFuncSrcSimple = (FUNC_SRC_SIMPLE)GetProcAddress(dllClient, "src_simple");
    }
    else {
        OutputDebugStringA("samplerate.dll load Error!");
    }


    // 读取JSON配置文件
    std::ifstream t_pc_file(sJsonConfigFileName, std::ios::binary);
    std::stringstream buffer_pc_file;
    buffer_pc_file << t_pc_file.rdbuf();

    juce::var jsonVar;
    if (juce::JSON::parse(buffer_pc_file.str(), jsonVar).wasOk()) {
        auto& props = jsonVar.getDynamicObject()->getProperties();
        audioProcessor.bEnableSOVITSPreResample = props["bEnableSOVITSPreResample"];
        audioProcessor.iSOVITSModelInputSamplerate = props["iSOVITSModelInputSamplerate"];
        audioProcessor.bEnableHUBERTPreResample = props["bEnableHUBERTPreResample"];
        audioProcessor.iHUBERTInputSampleRate = props["iHUBERTInputSampleRate"];
        audioProcessor.fSampleVolumeWorkActiveVal = props["fSampleVolumeWorkActiveVal"];

        audioProcessor.roleList.clear();
        auto roleList = props["roleList"];
        int iRoleSize = roleList.size();
        for (int i = 0; i < iRoleSize; i++) {
            auto& roleListI = roleList[i].getDynamicObject()->getProperties();
            std::string apiUrl = roleListI["apiUrl"].toString().toStdString();
            std::string name = roleListI["name"].toString().toStdString();
            std::string speakId = roleListI["speakId"].toString().toStdString();
            roleStruct role;
            role.sSpeakId = speakId;
            role.sName = name;
            role.sApiUrl = apiUrl;
            audioProcessor.roleList.push_back(role);
        }
        if (audioProcessor.iSelectRoleIndex + 1 > iRoleSize || audioProcessor.iSelectRoleIndex < 0) {
            audioProcessor.iSelectRoleIndex = 0;
        }
    }
    else {
        // error read json
    };

    audioProcessor.iNumberOfChanel = 1;
    audioProcessor.lNoOutputCount = 0;
    audioProcessor.bDoItSignal = false;

    // 初始化线程间交换数据的缓冲区，120s的缓冲区足够大
    float fModelInputOutputBufferSecond = 120.f;
    audioProcessor.lModelInputOutputBufferSize = fModelInputOutputBufferSecond * audioProcessor.getSampleRate();
    audioProcessor.fModeulInputSampleBuffer = (float*)(std::malloc(sizeof(float) * audioProcessor.lModelInputOutputBufferSize));
    audioProcessor.fModeulOutputSampleBuffer = (float*)(std::malloc(sizeof(float) * audioProcessor.lModelInputOutputBufferSize));
    audioProcessor.lModelInputSampleBufferReadPos = 0;
    audioProcessor.lModelInputSampleBufferWritePos = 0;
    audioProcessor.lModelOutputSampleBufferReadPos = 0;
    audioProcessor.lModelOutputSampleBufferWritePos = 0;

    // worker线程安全退出相关信号
    audioProcessor.bWorkerNeedExit = false;
    audioProcessor.runWorker();

    // UI
    tToggleRealTimeMode.setButtonText("Real Time Mode");
    tToggleRealTimeMode.setToggleState(audioProcessor.bRealTimeMode, juce::dontSendNotification);
    tToggleRealTimeMode.onClick = [this] {
        auto val = tToggleRealTimeMode.getToggleState();
        audioProcessor.bRealTimeMode = val;
        if (val) {
            audioProcessor.fMaxSliceLengthForSentenceMode = audioProcessor.fMaxSliceLength;
            sSliceSizeSlider.setValue(audioProcessor.fMaxSliceLengthForRealTimeMode);
        }
        else {
            audioProcessor.fMaxSliceLengthForRealTimeMode = audioProcessor.fMaxSliceLength;
            sSliceSizeSlider.setValue(audioProcessor.fMaxSliceLengthForSentenceMode);
        }
    };
    addAndMakeVisible(&tToggleRealTimeMode);

    tToggleDebugMode.setButtonText("Debug Mode");
    tToggleDebugMode.setToggleState(audioProcessor.bEnableDebug, juce::dontSendNotification);
    tToggleDebugMode.onClick = [this] {
        auto val = tToggleDebugMode.getToggleState();
        audioProcessor.bEnableDebug = val;
    };
    addAndMakeVisible(&tToggleDebugMode);

    lSliceSizeLabel.setText("Max audio slice length:", juce::dontSendNotification);
    sSliceSizeSlider.setSliderStyle(juce::Slider::LinearBar);
    sSliceSizeSlider.setRange(minMaxSliceLength, maxMaxSliceLength, 0.1);
    sSliceSizeSlider.setTextValueSuffix(" s");
    sSliceSizeSlider.setValue(audioProcessor.fMaxSliceLength);
    sSliceSizeSlider.addListener(this);
    addAndMakeVisible(&lSliceSizeLabel);
    addAndMakeVisible(&sSliceSizeSlider);

    lPitchChangeLabel.setText("Pitch change:", juce::dontSendNotification);
    sPitchChangeSlider.setSliderStyle(juce::Slider::LinearBar);
    sPitchChangeSlider.setRange(minPitchChange, maxPitchChange, 0.1);
    sPitchChangeSlider.setValue(audioProcessor.fPitchChange);
    sPitchChangeSlider.addListener(this);
    addAndMakeVisible(&lPitchChangeLabel);
    addAndMakeVisible(&sPitchChangeSlider);

    lMaxLowVolumeLengthLabel.setText("Max low volume legth:", juce::dontSendNotification);
    sMaxLowVolumeLengthSlider.setSliderStyle(juce::Slider::LinearBar);
    sMaxLowVolumeLengthSlider.setRange(minLowVolumeDetectLength, maxLowVolumeDetectLength, 0.01);
    sMaxLowVolumeLengthSlider.setValue(audioProcessor.fLowVolumeDetectTime);
    sMaxLowVolumeLengthSlider.addListener(this);
    addAndMakeVisible(&lMaxLowVolumeLengthLabel);
    addAndMakeVisible(&sMaxLowVolumeLengthSlider);

    lChangeRoleLabel.setText("Change role:", juce::dontSendNotification);
    bChangeRoleButton.setButtonText(audioProcessor.roleList[audioProcessor.iSelectRoleIndex].sName);
    addAndMakeVisible(&lChangeRoleLabel);
    addAndMakeVisible(&bChangeRoleButton);
    bChangeRoleButton.onClick = [this]
    {
        juce::PopupMenu menu;
        auto roleList = audioProcessor.roleList;
        for (int i = 0; i < roleList.size(); i++) {
            auto roleItem = roleList[i];
            auto sName = roleItem.sName;
            menu.addItem(sName, [this, i, sName] {
                bChangeRoleButton.setButtonText(sName);
                audioProcessor.iSelectRoleIndex = i;
                });
        }
        menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(bChangeRoleButton));
    };

    lServerUseTimeLabel.setText("Server use time:", juce::dontSendNotification);
    lServerUseTimeValLabel.setText("unCheck", juce::dontSendNotification);
    addAndMakeVisible(&lServerUseTimeLabel);
    addAndMakeVisible(&lServerUseTimeValLabel);
    audioProcessor.vServerUseTime.addListener(this);

    audioProcessor.initDone = true;
}

NetProcessJUCEVersionAudioProcessorEditor::~NetProcessJUCEVersionAudioProcessorEditor()
{
}

//==============================================================================

void NetProcessJUCEVersionAudioProcessorEditor::sliderValueChanged(juce::Slider* slider) {
    if (slider == &sSliceSizeSlider) {
        float val = slider->getValue();
        audioProcessor.fMaxSliceLength = val;
        audioProcessor.lMaxSliceLengthSampleNumber = audioProcessor.getSampleRate() * val;
    } else if (slider == &sPitchChangeSlider) {
        float val = slider->getValue();
        audioProcessor.fPitchChange = val;
    } else if (slider == &sMaxLowVolumeLengthSlider) {
        float val = slider->getValue();
        audioProcessor.fLowVolumeDetectTime = val;
    }
}

void NetProcessJUCEVersionAudioProcessorEditor::valueChanged(juce::Value& value) {
    if (value == audioProcessor.vServerUseTime) {
        auto sModelUseTime = audioProcessor.vServerUseTime.toString() + "ms";
        lServerUseTimeValLabel.setText(sModelUseTime, juce::dontSendNotification);
    }
}

void NetProcessJUCEVersionAudioProcessorEditor::paint (juce::Graphics& g)
{
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
    g.setColour (juce::Colours::white);
}

void NetProcessJUCEVersionAudioProcessorEditor::resized()
{
    // This is generally where you'll want to lay out the positions of any
    // subcomponents in your editor..
    int ilabelColumnWidth = 200;
    int iRowHeight = 20;
    auto localArea = getLocalBounds();
    localArea.reduce(5, 5);

    auto realTimeModeArea = localArea.removeFromTop(iRowHeight).removeFromLeft(ilabelColumnWidth);
    tToggleRealTimeMode.setBounds(realTimeModeArea);

    auto debugModeArea = localArea.removeFromTop(iRowHeight).removeFromLeft(ilabelColumnWidth);
    tToggleDebugMode.setBounds(debugModeArea);

    auto sliceSizeArea = localArea.removeFromTop(iRowHeight);
    auto sliceLabelArea = sliceSizeArea.removeFromLeft(ilabelColumnWidth);
    lSliceSizeLabel.setBounds(sliceLabelArea);
    auto sliceSizeSliderArea = sliceSizeArea;
    sSliceSizeSlider.setBounds(sliceSizeSliderArea);

    auto pitchSizeArea = localArea.removeFromTop(iRowHeight);
    auto pitchLabelArea = pitchSizeArea.removeFromLeft(ilabelColumnWidth);
    lPitchChangeLabel.setBounds(pitchLabelArea);
    auto pitchSliderArea = pitchSizeArea;
    sPitchChangeSlider.setBounds(pitchSliderArea);

    auto lowVolumeLengthArea = localArea.removeFromTop(iRowHeight);
    auto lowVolumeLengthLabelArea = lowVolumeLengthArea.removeFromLeft(ilabelColumnWidth);
    lMaxLowVolumeLengthLabel.setBounds(lowVolumeLengthLabelArea);
    auto MaxLowVolumeLengthArea = lowVolumeLengthArea;
    sMaxLowVolumeLengthSlider.setBounds(MaxLowVolumeLengthArea);

    auto changeRoleArea = localArea.removeFromTop(iRowHeight);
    auto changeRoleLabelArea = changeRoleArea.removeFromLeft(ilabelColumnWidth);
    lChangeRoleLabel.setBounds(changeRoleLabelArea);
    auto changeRoleButtonArea = changeRoleArea;
    bChangeRoleButton.setBounds(changeRoleButtonArea);

    auto serverUseTimeArea = localArea.removeFromTop(iRowHeight);
    auto serverUseTimeLabelArea = serverUseTimeArea.removeFromLeft(ilabelColumnWidth);
    lServerUseTimeLabel.setBounds(serverUseTimeLabelArea);
    auto serverUseTimeLabelValArea = serverUseTimeArea;
    lServerUseTimeValLabel.setBounds(serverUseTimeLabelValArea);
}
