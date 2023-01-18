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

    // UI
    tToggleRealTimeMode.setButtonText("Real Time Mode");
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
            lDropDataLengthValLabel.setText("unCheck", juce::dontSendNotification);
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

    lPrefixLengthLabel.setText("Prefix audio length:", juce::dontSendNotification);
    sPrefixLengthSlider.setSliderStyle(juce::Slider::LinearBar);
    sPrefixLengthSlider.setRange(minPrefixAudioLength, maxPrefixAudioLength, 0.01);
    sPrefixLengthSlider.setValue(audioProcessor.fPrefixLength);
    sPrefixLengthSlider.addListener(this);
    addAndMakeVisible(&lPrefixLengthLabel);
    addAndMakeVisible(&sPrefixLengthSlider);

    lDropSuffixLengthLabel.setText("Drop suffix audio legth:", juce::dontSendNotification);
    sDropSuffixLengthSlider.setSliderStyle(juce::Slider::LinearBar);
    sDropSuffixLengthSlider.setRange(minDropSuffixAudioLength, maxDropSuffixAudioLength, 0.01);
    sDropSuffixLengthSlider.setValue(audioProcessor.fDropSuffixLength);
    sDropSuffixLengthSlider.addListener(this);
    addAndMakeVisible(&lDropSuffixLengthLabel);
    addAndMakeVisible(&sDropSuffixLengthSlider);

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
    lServerUseTimeValLabel.getTextValue().referTo(audioProcessor.vServerUseTime);
    lServerUseTimeValLabel.setText("unCheck", juce::dontSendNotification);
    addAndMakeVisible(&lServerUseTimeLabel);
    addAndMakeVisible(&lServerUseTimeValLabel);

    lDropDataLengthLabel.setText("Drop data:", juce::dontSendNotification);
    lDropDataLengthValLabel.getTextValue().referTo(audioProcessor.vDropDataLength);
    lDropDataLengthValLabel.setText("unCheck", juce::dontSendNotification);
    addAndMakeVisible(&lDropDataLengthLabel);
    addAndMakeVisible(&lDropDataLengthValLabel);
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
    else if (slider == &sDropSuffixLengthSlider) {
        float val = slider->getValue();
        audioProcessor.fDropSuffixLength = val;
    }
}

void NetProcessJUCEVersionAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void NetProcessJUCEVersionAudioProcessorEditor::resized()
{
    int ilabelColumnWidth = 150;
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

    auto DropSuffixLengthArea = localArea.removeFromTop(iRowHeight + iRowMargin * 2).reduced(iRowMargin, 0);
    auto DropSuffixLengthLabelArea = DropSuffixLengthArea.removeFromLeft(ilabelColumnWidth);
    lDropSuffixLengthLabel.setBounds(DropSuffixLengthLabelArea);
    auto lowVolumeLengthSliderArea = DropSuffixLengthArea;
    sDropSuffixLengthSlider.setBounds(lowVolumeLengthSliderArea);

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
}
