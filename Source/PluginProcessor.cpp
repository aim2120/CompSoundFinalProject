/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
CompSoundFinalProjectAudioProcessor::CompSoundFinalProjectAudioProcessor()
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
}

CompSoundFinalProjectAudioProcessor::~CompSoundFinalProjectAudioProcessor()
{
}

//==============================================================================
const juce::String CompSoundFinalProjectAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool CompSoundFinalProjectAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool CompSoundFinalProjectAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool CompSoundFinalProjectAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double CompSoundFinalProjectAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int CompSoundFinalProjectAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int CompSoundFinalProjectAudioProcessor::getCurrentProgram()
{
    return 0;
}

void CompSoundFinalProjectAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String CompSoundFinalProjectAudioProcessor::getProgramName (int index)
{
    return {};
}

void CompSoundFinalProjectAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void CompSoundFinalProjectAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Use this method as the place to do any pre-playback
    // initialisation that you need..
    auto processSpec = juce::dsp::ProcessSpec();
    processSpec.sampleRate = sampleRate;
    processSpec.maximumBlockSize = samplesPerBlock;
    processSpec.numChannels = getTotalNumInputChannels();
    
    processorChain.prepare(processSpec);
    
    auto settings = getSettings(apvts);
    
    reverb.prepare(processSpec);
    reverbParams.roomSize = settings.roomSize;
    reverbParams.damping = settings.damping;
    reverbParams.wetLevel = settings.wetLevel;
    reverbParams.dryLevel = settings.dryLevel;
    reverbParams.width = settings.width;
    reverbParams.freezeMode = settings.freezeMode;
    
    reverbDelay = settings.delay;
    
    const int bufferSize = samplesPerBlock + sampleRate;
    const int numInputChannels = getTotalNumInputChannels();
    const int delayBufferSize = 2 * bufferSize;
    
    delayBuffer.setSize(numInputChannels, delayBufferSize);
    delayBufferToRead.setSize(numInputChannels, bufferSize);
}

void CompSoundFinalProjectAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool CompSoundFinalProjectAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
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

void CompSoundFinalProjectAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();
    
    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    // This is here to avoid people getting screaming feedback
    // when they first compile a plugin, but obviously you don't need to keep
    // this code if your algorithm always overwrites all the output channels.
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());
    
    const int bufferLength = buffer.getNumSamples();
    const int delayBufferLength = delayBuffer.getNumSamples();
 
    for (int channel = 0; channel < totalNumInputChannels; ++channel) {
        const float* bufferData = buffer.getReadPointer(channel);
        const float* delayBufferData = delayBuffer.getReadPointer(channel);
        fillDelayBuffer(channel, bufferLength, delayBufferLength, bufferData, delayBufferData);
    }
    
    writePosition += bufferLength;
    writePosition %= delayBufferLength;
        
    auto settings = getSettings(apvts);
    buffer.applyGain(settings.gain);
    
    auto audioBlock = juce::dsp::AudioBlock<float>(delayBufferToRead);
    auto processContext = juce::dsp::ProcessContextReplacing<float>(audioBlock);
    reverb.setParameters(reverbParams);
    reverb.process(processContext);
}

void CompSoundFinalProjectAudioProcessor::fillDelayBuffer(
                                                          int channel,
                                                          const int bufferLength,
                                                          const int delayBufferLength,
                                                          const float* bufferData,
                                                          const float* delayBufferData
                                                          ) {
    if (delayBufferLength > bufferLength + writePosition) {
        delayBuffer.copyFrom(channel, writePosition, bufferData, bufferLength);
    } else {
        int bufferRemaining = delayBufferLength - writePosition;
        delayBuffer.copyFrom(channel, writePosition, bufferData, bufferRemaining);
        delayBuffer.copyFrom(channel, 0, bufferData + bufferRemaining, bufferLength - bufferRemaining);
    }
    
    int readPosition = (writePosition + (delayBufferLength - reverbDelay)) % delayBufferLength;
    
    if (delayBufferLength > bufferLength + readPosition) {
        delayBufferToRead.copyFrom(channel, 0, delayBufferData + readPosition, bufferLength);
    } else {
        int bufferRemaining = delayBufferLength - readPosition;
        delayBufferToRead.copyFrom(channel, 0, delayBufferData + readPosition, bufferRemaining);
        delayBufferToRead.copyFrom(channel, bufferRemaining, delayBufferData + readPosition + bufferRemaining, bufferLength - bufferRemaining);
    }
}

//==============================================================================
bool CompSoundFinalProjectAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* CompSoundFinalProjectAudioProcessor::createEditor()
{
    //return new CompSoundFinalProjectAudioProcessorEditor (*this);
    return new juce::GenericAudioProcessorEditor(*this);
}

//==============================================================================
void CompSoundFinalProjectAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
}

void CompSoundFinalProjectAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
}

Settings getSettings(juce::AudioProcessorValueTreeState& apvts) {
    Settings settings;
    
    settings.gain = apvts.getRawParameterValue("Gain")->load();
    settings.roomSize = apvts.getRawParameterValue("Room Size")->load();
    settings.damping = apvts.getRawParameterValue("Damping")->load();
    settings.wetLevel = apvts.getRawParameterValue("Wet Level")->load();
    settings.dryLevel = apvts.getRawParameterValue("Dry Level")->load();
    settings.width = apvts.getRawParameterValue("Width")->load();
    settings.freezeMode = apvts.getRawParameterValue("Freeze Mode")->load();
    settings.delay = apvts.getRawParameterValue("Delay")->load();
    
    return settings;
}

juce::AudioProcessorValueTreeState::ParameterLayout CompSoundFinalProjectAudioProcessor::createParameterLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    
    layout.add(std::make_unique<juce::AudioParameterFloat>(
                                                           "Gain",
                                                           "Gain",
                                                           juce::NormalisableRange<float>(0.f, 1.f, 0.1f, 1.f),
                                                           0.5f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
                                                           "Room Size",
                                                           "Room Size",
                                                           juce::NormalisableRange<float>(0.f, 1.f, 0.1f, 1.f),
                                                           0.5f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
                                                           "Damping",
                                                           "Damping",
                                                           juce::NormalisableRange<float>(0.f, 1.f, 0.1f, 1.f),
                                                           0.5f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
                                                           "Wet Level",
                                                           "Wet Level",
                                                           juce::NormalisableRange<float>(0.f, 1.f, 0.1f, 1.f),
                                                           0.5f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
                                                           "Dry Level",
                                                           "Dry Level",
                                                           juce::NormalisableRange<float>(0.f, 1.f, 0.1f, 1.f),
                                                           0.5f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
                                                           "Width",
                                                           "Width",
                                                           juce::NormalisableRange<float>(0.f, 1.f, 0.1f, 1.f),
                                                           0.5f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
                                                           "Freeze Mode",
                                                           "Freeze Mode",
                                                           juce::NormalisableRange<float>(0.f, 1.f, 0.1f, 1.f),
                                                           0.5f));
    
    layout.add(std::make_unique<juce::AudioParameterFloat>(
                                                           "Delay",
                                                           "Delay",
                                                           juce::NormalisableRange<float>(0.f, 1000.f, 1.f, 1.f),
                                                           500.f));

    return layout;
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CompSoundFinalProjectAudioProcessor();
}
