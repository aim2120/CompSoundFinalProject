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
    mSampleRate = sampleRate;
    
    auto processSpec = juce::dsp::ProcessSpec();
    
    processSpec.sampleRate = sampleRate;
    processSpec.maximumBlockSize = samplesPerBlock;
    processSpec.numChannels = getTotalNumInputChannels();
    
    processorChain.prepare(processSpec);
    reverb.prepare(processSpec);
    
    settings = getSettings(apvts);
    setReverbParameters();
   
    const int numInputChannels = getTotalNumInputChannels();
    const int delayBufferLength = 2 * (samplesPerBlock + sampleRate);
    
    delayBuffer.setSize(numInputChannels, delayBufferLength);
    multiChannelBuffer.setSize(MULTICHANNEL_TOTAL_INPUTS, samplesPerBlock);
    multiChannelDiffusedBuffer.setSize(MULTICHANNEL_TOTAL_INPUTS, samplesPerBlock);
    multiChannelDelayBuffer.setSize(MULTICHANNEL_TOTAL_INPUTS, delayBufferLength);

    for (int i = 0; i < MATRIX_SIZE; i++) {
        for (int j = 0; j < MATRIX_SIZE; j++) {
            if (i == j) {
                householderMatrix(i, j) = 0.5;
            } else {
                householderMatrix(i, j) = -0.5;
            }
            currentStepMatrix(i, j) = i + j;
        }
    }
    
    currentStepMatrixOutput = currentStepMatrix * householderMatrix;
    
    for (int i = 0; i < MATRIX_SIZE; i++) {
        for (int j = 0; j < MATRIX_SIZE; j++) {
            std::cout << "currentStep: " << currentStepMatrix(i, j) << std::endl;
            std::cout << "householder: " << householderMatrix(i, j) << std::endl;
            std::cout << "output: " << currentStepMatrixOutput(i, j) << std::endl;
        }
    }
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
    
    const int bufferLength = buffer.getNumSamples();
    const int delayBufferLength = delayBuffer.getNumSamples();
    
    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    // This is here to avoid people getting screaming feedback
    // when they first compile a plugin, but obviously you don't need to keep
    // this code if your algorithm always overwrites all the output channels.
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, bufferLength);
    
    settings = getSettings(apvts);
    setReverbParameters();
    
    // fill the delay buffer with the current buffer's data
    for (int channel = 0; channel < totalNumInputChannels; ++channel) {
        const float* bufferData = buffer.getReadPointer(channel);
        fillDelayBuffer(channel, bufferLength, delayBufferLength, bufferData);
    }
   
    
    // convert the buffer and delay buffer to multiChannel
    for (int channel = 0; channel < MULTICHANNEL_TOTAL_INPUTS; ++channel) {
        int originalChannel = channel % totalNumInputChannels;
        const float* bufferData = buffer.getReadPointer(originalChannel);
        const float* delayBufferData = delayBuffer.getReadPointer(originalChannel);
        multiChannelBuffer.copyFrom(channel, 0, bufferData, bufferLength);
        multiChannelDelayBuffer.copyFrom(channel, 0, delayBufferData, delayBufferLength);
    }
    
    // apply the dry gain before diffusing or adding in delays
    multiChannelBuffer.applyGain(settings.dryLevel);
    
    float** bufferDataArr = multiChannelBuffer.getArrayOfWritePointers();
    float** diffusedBufferDataArr = multiChannelDiffusedBuffer.getArrayOfWritePointers();
    float** delayBufferDataArr = multiChannelDelayBuffer.getArrayOfWritePointers();
    
    for (int i = 1; i <= settings.numOfDelays; ++i) {
        const int delay = settings.delayLength * i;
        for (int j = 0; j < bufferLength; ++j) {
            const int bufferIndex = j;
            const int readPosition = static_cast<int>((writePosition + (delayBufferLength - (mSampleRate * delay / 1000)) + bufferIndex) % delayBufferLength);
            addFromDelayBuffer(bufferDataArr, delayBufferDataArr, readPosition, bufferIndex, delay);
        }
    }
    
    // condense the multiChannel buffer data back into the original buffer
    const float gainDivisor = static_cast<float>(totalNumInputChannels) / static_cast<float>(MULTICHANNEL_TOTAL_INPUTS);
    for (int channel = 0; channel < MULTICHANNEL_TOTAL_INPUTS; ++channel) {
        int originalChannel = channel % totalNumInputChannels;
        const float* bufferData = multiChannelBuffer.getReadPointer(channel);
        if (channel < totalNumInputChannels) {
            buffer.copyFromWithRamp(originalChannel, 0, bufferData, bufferLength, gainDivisor, gainDivisor);
        } else {
            buffer.addFromWithRamp(originalChannel, 0, bufferData, bufferLength, gainDivisor, gainDivisor);
        }
    }
    /*
    float** bufferDataArr = buffer.getArrayOfWritePointers();
    float** delayBufferDataArr = delayBuffer.getArrayOfWritePointers();
    
    for (int i = 1; i <= settings.numOfDelays; ++i) {
        const int delay = i * settings.delayLength;
        for (int j = 0; j < bufferLength; ++j) {
            const int bufferIndex = j;
            const int readPosition = static_cast<int>((writePosition + (delayBufferLength - (mSampleRate * delay / 1000)) + bufferIndex) % delayBufferLength);
            addFromDelayBuffer(bufferDataArr, delayBufferDataArr, readPosition, bufferIndex);
        }
    }
     */

    
    writePosition += bufferLength;
    writePosition %= delayBufferLength;
        
    buffer.applyGain(settings.gain);
    
    auto audioBlock = juce::dsp::AudioBlock<float>(buffer);
    auto processContext = juce::dsp::ProcessContextReplacing<float>(audioBlock);
    //reverb.setParameters(reverbParams);
    //reverb.process(processContext);
    
}

void CompSoundFinalProjectAudioProcessor::setReverbParameters() {
    reverbParams.roomSize = settings.roomSize;
    reverbParams.damping = settings.damping;
    reverbParams.wetLevel = settings.wetLevel;
    reverbParams.dryLevel = settings.dryLevel;
    reverbParams.width = settings.width;
    reverbParams.freezeMode = settings.freezeMode;
    
    reverb.setParameters(reverbParams);
}

void CompSoundFinalProjectAudioProcessor::fillDelayBuffer(
                                                          int channel,
                                                          const int bufferLength,
                                                          const int delayBufferLength,
                                                          const float* bufferData
                                                          ) {
    if (delayBufferLength > bufferLength + writePosition) {
        delayBuffer.copyFromWithRamp(channel, writePosition, bufferData, bufferLength, 0.8, 0.8);
    } else {
        int bufferRemaining = delayBufferLength - writePosition;
        delayBuffer.copyFromWithRamp(channel, writePosition, bufferData, bufferRemaining, 0.8, 0.8);
        delayBuffer.copyFromWithRamp(channel, 0, bufferData + bufferRemaining, bufferLength - bufferRemaining, 0.8, 0.8);
    }
}

void CompSoundFinalProjectAudioProcessor::addFromDelayBuffer(
                                                             float** bufferDataArr,
                                                             float** delayBufferDataArr,
                                                             const int readPosition,
                                                             const int bufferIndex,
                                                             const int delay
                                                             ) {
    //const float wetGain = (settings.wetLevel * 0.8) / settings.numOfDelays;
    const float baseWetGain = (settings.wetLevel * 0.8);
    float one = 1.0;
    const float decayAmount = baseWetGain * std::min((delay / settings.decay), one);
    const float gateCutoffAmount = baseWetGain * (1 / (settings.gateCutoff - std::max((float)delay, (float)(settings.gateCutoff - 1))));
    const float wetGain = (baseWetGain - decayAmount - gateCutoffAmount) / settings.numOfDelays;
    
    for (int i = 0; i < MATRIX_SIZE; ++i) {
        for (int j = 0; j < MATRIX_SIZE; ++j) {
            currentStepMatrix(i, j) = delayBufferDataArr[j][readPosition];
        }
    }
    
    currentStepMatrixOutput = currentStepMatrix * householderMatrix;
    
    for (int i = 0; i < MATRIX_SIZE; ++i) {
        for (int j = 0; j < MATRIX_SIZE; ++j) {
            bufferDataArr[j][bufferIndex] += (currentStepMatrixOutput(i, j) * wetGain);
        }
    }
}

void CompSoundFinalProjectAudioProcessor::addFromDelayBuffer(
                                                             juce::AudioBuffer<float>& buffer,
                                                             int channel,
                                                             const int bufferLength,
                                                             const int delayBufferLength,
                                                             const float* delayBufferData,
                                                             const float gainMultiplier
                                                             ) {
    buffer.applyGain(channel, 0, bufferLength, (settings.dryLevel * 0.8 * gainMultiplier));
    
    for (int i = 1; i < settings.numOfDelays; i++) {
        int readPosition = static_cast<int>((writePosition + (delayBufferLength - (mSampleRate * (static_cast<int>(settings.delayLength) * i) / 1000))) % delayBufferLength);
        float gain = (settings.wetLevel * 0.8 * gainMultiplier) / i;
        
        if (delayBufferLength > bufferLength + readPosition) {
            buffer.addFromWithRamp(channel, 0, delayBufferData + readPosition, bufferLength, gain, gain);
        } else {
            int bufferRemaining = delayBufferLength - readPosition;
            buffer.addFromWithRamp(channel, 0, delayBufferData + readPosition, bufferRemaining, gain, gain);
            buffer.addFromWithRamp(channel, bufferRemaining, delayBufferData, bufferLength - bufferRemaining, gain, gain);
        }
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
    
    settings.gain = apvts.getRawParameterValue(GAIN)->load();
    settings.roomSize = apvts.getRawParameterValue(GAIN)->load();
    settings.damping = apvts.getRawParameterValue(DAMPING)->load();
    settings.wetLevel = apvts.getRawParameterValue(WET_LEVEL)->load();
    settings.dryLevel = apvts.getRawParameterValue(DRY_LEVEL)->load();
    settings.width = apvts.getRawParameterValue(WIDTH)->load();
    settings.freezeMode = apvts.getRawParameterValue(FREEZE_MODE)->load();
    settings.delayLength = apvts.getRawParameterValue(DELAY_LENGTH)->load();
    settings.numOfDelays = apvts.getRawParameterValue(NUM_OF_DELAYS)->load();
    settings.decay = apvts.getRawParameterValue(DECAY)->load();
    settings.gateCutoff = apvts.getRawParameterValue(GATE_CUTOFF)->load();
    
    return settings;
}

juce::AudioProcessorValueTreeState::ParameterLayout CompSoundFinalProjectAudioProcessor::createParameterLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    
    layout.add(std::make_unique<juce::AudioParameterFloat>(
                                                           GAIN,
                                                           GAIN,
                                                           juce::NormalisableRange<float>(0.f, 1.f, 0.1f, 1.f),
                                                           0.5f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
                                                           ROOM_SIZE,
                                                           ROOM_SIZE,
                                                           juce::NormalisableRange<float>(0.f, 1.f, 0.1f, 1.f),
                                                           0.5f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
                                                           DAMPING,
                                                           DAMPING,
                                                           juce::NormalisableRange<float>(0.f, 1.f, 0.1f, 1.f),
                                                           0.5f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
                                                           WET_LEVEL,
                                                           WET_LEVEL,
                                                           juce::NormalisableRange<float>(0.f, 1.f, 0.1f, 1.f),
                                                           0.5f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
                                                           DRY_LEVEL,
                                                           DRY_LEVEL,
                                                           juce::NormalisableRange<float>(0.f, 1.f, 0.1f, 1.f),
                                                           0.5f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
                                                           WIDTH,
                                                           WIDTH,
                                                           juce::NormalisableRange<float>(0.f, 1.f, 0.1f, 1.f),
                                                           0.5f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
                                                           FREEZE_MODE,
                                                           FREEZE_MODE,
                                                           juce::NormalisableRange<float>(0.f, 1.f, 0.1f, 1.f),
                                                           0.5f));
    
    layout.add(std::make_unique<juce::AudioParameterFloat>(
                                                           DELAY_LENGTH,
                                                           DELAY_LENGTH,
                                                           juce::NormalisableRange<float>(0.f, 500.f, 1.f, 1.f),
                                                           100.f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
                                                           NUM_OF_DELAYS,
                                                           NUM_OF_DELAYS,
                                                           juce::NormalisableRange<float>(0.f, 10.f, 1.f, 1.f),
                                                           5.f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
                                                           DECAY,
                                                           DECAY,
                                                           juce::NormalisableRange<float>(1.f, 1000.f, 1.f, 1.f),
                                                           500.f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
                                                           GATE_CUTOFF,
                                                           GATE_CUTOFF,
                                                           juce::NormalisableRange<float>(1.f, 1000.f, 1.f, 1.f),
                                                           1000.f));
    

    return layout;
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CompSoundFinalProjectAudioProcessor();
}
