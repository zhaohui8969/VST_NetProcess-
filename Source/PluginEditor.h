/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <pluginterfaces/gui/iplugview.h>

#define PLUGIN_API __stdcall

//==============================================================================
/**
*/
class NetProcessJUCEVersionAudioProcessorEditor  : public juce::AudioProcessorEditor,
    private juce::Slider::Listener
{
public:
    NetProcessJUCEVersionAudioProcessorEditor (NetProcessJUCEVersionAudioProcessor&);
    ~NetProcessJUCEVersionAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

    //tresult PLUGIN_API checkSizeConstraint(ViewRect* rectToCheck);

private:
    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    NetProcessJUCEVersionAudioProcessor& audioProcessor;
    
    void sliderValueChanged(juce::Slider* slider) override;

    juce::ToggleButton tToggleRealTimeMode;
    juce::ToggleButton tToggleDebugMode;
    juce::Label lSliceSizeLabel;
    juce::Slider sSliceSizeSlider;
    juce::Label lPitchChangeLabel;
    juce::Slider sPitchChangeSlider;
    juce::Label lMaxLowVolumeLengthLabel;
    juce::Slider sMaxLowVolumeLengthSlider;
    juce::Label lPrefixLengthLabel;
    juce::Slider sPrefixLengthSlider;
    juce::Label lChangeRoleLabel;
    juce::TextButton bChangeRoleButton;
    juce::Label lServerUseTimeLabel;
    juce::Label lServerUseTimeValLabel;
    juce::Label lAllUseTimeLabel;
    juce::Label lAllUseTimeValLabel;
    juce::Label lVersionLabel;
    juce::Label lVersionValLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NetProcessJUCEVersionAudioProcessorEditor)
};
