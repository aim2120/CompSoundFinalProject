/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

struct Settings {
    float gain { 0 };
};

Settings getSettings(juce::AudioProcessorValueTreeState& apvts);
//==============================================================================
/**
*/
class CompSoundFinalProjectAudioProcessorEditor  : public juce::AudioProcessorEditor
{
public:
    CompSoundFinalProjectAudioProcessorEditor (CompSoundFinalProjectAudioProcessor&);
    ~CompSoundFinalProjectAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    CompSoundFinalProjectAudioProcessor& audioProcessor;
    
    using Gain = juce::dsp::Gain<float>;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompSoundFinalProjectAudioProcessorEditor)
};
