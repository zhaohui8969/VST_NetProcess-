/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

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
    juce::Label lChangeRoleLabel;
    juce::Label lServerUseTimeLabel;
    juce::Label lServerUseTimeValLabel;
    juce::TextButton bChangeRoleButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NetProcessJUCEVersionAudioProcessorEditor)
};
