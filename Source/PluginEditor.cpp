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
#include <pluginterfaces/base/ftypes.h>
using namespace Steinberg;
using namespace std::chrono;

//==============================================================================

NetProcessJUCEVersionAudioProcessorEditor::NetProcessJUCEVersionAudioProcessorEditor(NetProcessJUCEVersionAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setSize(400, 250);
    setResizable(false, false);
    juce::LookAndFeel::getDefaultLookAndFeel().setDefaultSansSerifTypefaceName(L"SiHei_otf");

    // UI
    tToggleRealTimeMode.setButtonText(L"实时模式");
    tToggleRealTimeMode.setToggleState(audioProcessor.bRealTimeMode, juce::dontSendNotification);
    tToggleRealTimeMode.onClick = [this] {
        auto val = tToggleRealTimeMode.getToggleState();
        audioProcessor.bRealTimeMode = val;
        if (val) {
            // on
            audioProcessor.fMaxSliceLengthForSentenceMode = audioProcessor.fMaxSliceLength;
            sSliceSizeSlider.setValue(audioProcessor.fMaxSliceLengthForRealTimeMode);
        }
        else {
            // off
            audioProcessor.fMaxSliceLengthForRealTimeMode = audioProcessor.fMaxSliceLength;
            sSliceSizeSlider.setValue(audioProcessor.fMaxSliceLengthForSentenceMode);
            lDropDataLengthValLabel.setText(L"未检查", juce::dontSendNotification);
        }
    };
    addAndMakeVisible(&tToggleRealTimeMode);

    tToggleDebugMode.setButtonText(L"调试日志");
    tToggleDebugMode.setToggleState(audioProcessor.bEnableDebug, juce::dontSendNotification);
    tToggleDebugMode.onClick = [this] {
        auto val = tToggleDebugMode.getToggleState();
        audioProcessor.bEnableDebug = val;
    };
    addAndMakeVisible(&tToggleDebugMode);

    lSliceSizeLabel.setText(L"最长音频切片时长:", juce::dontSendNotification);
    sSliceSizeSlider.setSliderStyle(juce::Slider::LinearBar);
    sSliceSizeSlider.setRange(minMaxSliceLength, maxMaxSliceLength, 0.01);
    sSliceSizeSlider.setTextValueSuffix(" s");
    sSliceSizeSlider.setValue(audioProcessor.fMaxSliceLength);
    sSliceSizeSlider.addListener(this);
    addAndMakeVisible(&lSliceSizeLabel);
    addAndMakeVisible(&sSliceSizeSlider);

    lPitchChangeLabel.setText(L"变调:", juce::dontSendNotification);
    sPitchChangeSlider.setSliderStyle(juce::Slider::LinearBar);
    sPitchChangeSlider.setRange(minPitchChange, maxPitchChange, 0.1);
    sPitchChangeSlider.setValue(audioProcessor.fPitchChange);
    sPitchChangeSlider.addListener(this);
    addAndMakeVisible(&lPitchChangeLabel);
    addAndMakeVisible(&sPitchChangeSlider);

    lMaxLowVolumeLengthLabel.setText(L"最长静音时长:", juce::dontSendNotification);
    sMaxLowVolumeLengthSlider.setSliderStyle(juce::Slider::LinearBar);
    sMaxLowVolumeLengthSlider.setRange(minLowVolumeDetectLength, maxLowVolumeDetectLength, 0.01);
    sMaxLowVolumeLengthSlider.setTextValueSuffix(" s");
    sMaxLowVolumeLengthSlider.setValue(audioProcessor.fLowVolumeDetectTime);
    sMaxLowVolumeLengthSlider.addListener(this);
    addAndMakeVisible(&lMaxLowVolumeLengthLabel);
    addAndMakeVisible(&sMaxLowVolumeLengthSlider);

   
    lPrefixLengthLabel.setText(L"交叉淡化时长:", juce::dontSendNotification);
    sPrefixLengthSlider.setSliderStyle(juce::Slider::LinearBar);
    sPrefixLengthSlider.setRange(minPrefixAudioLength, maxPrefixAudioLength, 0.01);
    sPrefixLengthSlider.setTextValueSuffix(" s");
    sPrefixLengthSlider.setValue(audioProcessor.fPrefixLength);
    sPrefixLengthSlider.addListener(this);
    addAndMakeVisible(&lPrefixLengthLabel);
    addAndMakeVisible(&sPrefixLengthSlider);

    lChangeRoleLabel.setText(L"切换角色:", juce::dontSendNotification);
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

    lServerUseTimeLabel.setText(L"算法耗时:", juce::dontSendNotification);
    lServerUseTimeValLabel.getTextValue().referTo(audioProcessor.vServerUseTime);
    lServerUseTimeValLabel.setText(L"未检查", juce::dontSendNotification);
    addAndMakeVisible(&lServerUseTimeLabel);
    addAndMakeVisible(&lServerUseTimeValLabel);

    lDropDataLengthLabel.setText(L"丢弃数据时长:", juce::dontSendNotification);
    lDropDataLengthValLabel.getTextValue().referTo(audioProcessor.vDropDataLength);
    lDropDataLengthValLabel.setText(L"未检查", juce::dontSendNotification);
    addAndMakeVisible(&lDropDataLengthLabel);
    addAndMakeVisible(&lDropDataLengthValLabel);

    lVersionLabel.setText(L"版本号:", juce::dontSendNotification);
    lVersionValLabel.setText(L"V3.0", juce::dontSendNotification);
    addAndMakeVisible(&lVersionLabel);
    addAndMakeVisible(&lVersionValLabel);
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
    }
    else if (slider == &sPrefixLengthSlider) {
        float val = slider->getValue();
        audioProcessor.fPrefixLength = val;
        audioProcessor.lPrefixLengthSampleNumber = static_cast<long>(val * audioProcessor.getSampleRate());
    }
}

void NetProcessJUCEVersionAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void NetProcessJUCEVersionAudioProcessorEditor::resized()
{
    int ilabelColumnWidth = 160;
    int iRowHeight = 20;
    int iRowMargin = 1;
    auto localArea = getLocalBounds();
    localArea.reduce(5, 5);

    auto realTimeModeArea = localArea.removeFromTop(iRowHeight + iRowMargin * 2).reduced(iRowMargin, 0).removeFromLeft(ilabelColumnWidth);
    tToggleRealTimeMode.setBounds(realTimeModeArea);

    auto debugModeArea = localArea.removeFromTop(iRowHeight + iRowMargin * 2).reduced(iRowMargin, 0).removeFromLeft(ilabelColumnWidth);
    tToggleDebugMode.setBounds(debugModeArea);

    auto sliceSizeArea = localArea.removeFromTop(iRowHeight + iRowMargin * 2).reduced(iRowMargin, 0);
    auto sliceLabelArea = sliceSizeArea.removeFromLeft(ilabelColumnWidth);
    lSliceSizeLabel.setBounds(sliceLabelArea);
    auto sliceSizeSliderArea = sliceSizeArea;
    sSliceSizeSlider.setBounds(sliceSizeSliderArea);

    auto pitchSizeArea = localArea.removeFromTop(iRowHeight + iRowMargin * 2).reduced(iRowMargin, 0);
    auto pitchLabelArea = pitchSizeArea.removeFromLeft(ilabelColumnWidth);
    lPitchChangeLabel.setBounds(pitchLabelArea);
    auto pitchSliderArea = pitchSizeArea;
    sPitchChangeSlider.setBounds(pitchSliderArea);

    auto lowVolumeLengthArea = localArea.removeFromTop(iRowHeight + iRowMargin * 2).reduced(iRowMargin, 0);
    auto lowVolumeLengthLabelArea = lowVolumeLengthArea.removeFromLeft(ilabelColumnWidth);
    lMaxLowVolumeLengthLabel.setBounds(lowVolumeLengthLabelArea);
    auto MaxLowVolumeLengthArea = lowVolumeLengthArea;
    sMaxLowVolumeLengthSlider.setBounds(MaxLowVolumeLengthArea);

    auto PrefixLengthArea = localArea.removeFromTop(iRowHeight + iRowMargin * 2).reduced(iRowMargin, 0);
    auto PrefixLengthLabelArea = PrefixLengthArea.removeFromLeft(ilabelColumnWidth);
    lPrefixLengthLabel.setBounds(PrefixLengthLabelArea);
    auto PrefixLengthSliderArea = PrefixLengthArea;
    sPrefixLengthSlider.setBounds(PrefixLengthSliderArea);

    auto changeRoleArea = localArea.removeFromTop(iRowHeight + iRowMargin * 2).reduced(iRowMargin, 0);
    auto changeRoleLabelArea = changeRoleArea.removeFromLeft(ilabelColumnWidth);
    lChangeRoleLabel.setBounds(changeRoleLabelArea);
    auto changeRoleButtonArea = changeRoleArea;
    bChangeRoleButton.setBounds(changeRoleButtonArea);

    auto serverUseTimeArea = localArea.removeFromTop(iRowHeight + iRowMargin * 2).reduced(iRowMargin, 0);
    auto serverUseTimeLabelArea = serverUseTimeArea.removeFromLeft(ilabelColumnWidth);
    lServerUseTimeLabel.setBounds(serverUseTimeLabelArea);
    auto serverUseTimeLabelValArea = serverUseTimeArea;
    lServerUseTimeValLabel.setBounds(serverUseTimeLabelValArea);

    auto DropDataLengthArea = localArea.removeFromTop(iRowHeight + iRowMargin * 2).reduced(iRowMargin, 0);
    auto DropDataLengthLabelArea = DropDataLengthArea.removeFromLeft(ilabelColumnWidth);
    lDropDataLengthLabel.setBounds(DropDataLengthLabelArea);
    auto DropDataLengthLabelValArea = DropDataLengthArea;
    lDropDataLengthValLabel.setBounds(DropDataLengthLabelValArea);

    auto VersionArea = localArea.removeFromTop(iRowHeight + iRowMargin * 2).reduced(iRowMargin, 0);
    auto VersionLabelArea = VersionArea.removeFromLeft(ilabelColumnWidth);
    lVersionLabel.setBounds(VersionLabelArea);
    auto VersionLabelValArea = VersionArea;
    lVersionValLabel.setBounds(VersionLabelValArea);
}
