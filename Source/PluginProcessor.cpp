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
    
    const int numInputChannels = getTotalNumInputChannels();
    
    auto processSpec = juce::dsp::ProcessSpec();
    
    processSpec.sampleRate = sampleRate;
    processSpec.maximumBlockSize = samplesPerBlock;
    processSpec.numChannels = numInputChannels;
    
    processorChain.prepare(processSpec);
    reverb.prepare(processSpec);
    
    settings = getSettings(apvts);
    setReverbParameters();
   
    const int delayBufferLength = 3 * (samplesPerBlock + sampleRate);
    
    multiChannelBuffer.setSize(MULTICHANNEL_TOTAL_INPUTS, samplesPerBlock);
    multiChannelDiffusedBuffer.setSize(MULTICHANNEL_TOTAL_INPUTS, samplesPerBlock);
    multiChannelDiffusedBufferHelper.setSize(MULTICHANNEL_TOTAL_INPUTS, samplesPerBlock);
    multiChannelDelayBuffer.setSize(MULTICHANNEL_TOTAL_INPUTS, delayBufferLength);
    multiChannelDiffusedDelayBuffer.setSize(MULTICHANNEL_TOTAL_INPUTS, delayBufferLength);

    // setting householder matrix
    for (int i = 0; i < MATRIX_SIZE; i++) {
        for (int j = 0; j < MATRIX_SIZE; j++) {
            if (i == j) {
                householderMatrix(i, j) = 0.5;
            } else {
                householderMatrix(i, j) = -0.5;
            }
        }
    }
    
    // (tediously) setting hadamard matrix
    for (int i = 0; i < MATRIX_SIZE; i++) {
        for (int j = 0; j < MATRIX_SIZE; j++) {
            if (i == 0 || j == 0 || (i == 3 && j == 3)
                || (i == 1 && j == 2) || (i == 2 && j == 1)) {
                hadamardMatrix(i, j) = 1;
            } else {
                hadamardMatrix(i, j) = -1;
            }
        }
    }
    
    for (int i = 0; i < MATRIX_SIZE; i++) {
        for (int j = 0; j < MATRIX_SIZE; j++) {
            permutationMatrix(i, j) = 0;
        }
    }
    
    // statically setting for now
    permutationMatrix(0,3) = 1;
    permutationMatrix(1,2) = -1;
    permutationMatrix(2,0) = 1;
    permutationMatrix(3,1) = -1;
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
    const int delayBufferLength = multiChannelDelayBuffer.getNumSamples();
    
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
  
    // convert the buffer buffer to multiChannel
    for (int channel = 0; channel < MULTICHANNEL_TOTAL_INPUTS; ++channel) {
        int originalChannel = channel % totalNumInputChannels;
        const float* bufferData = buffer.getReadPointer(originalChannel);
        multiChannelBuffer.copyFrom(channel, 0, bufferData, bufferLength);
        multiChannelDiffusedBuffer.copyFrom(channel, 0, bufferData, bufferLength);
        multiChannelDiffusedBufferHelper.copyFrom(channel, 0, bufferData, bufferLength);
    }
     
    // apply the dry gain before diffusing or adding in delays
    multiChannelBuffer.applyGain(settings.dryLevel);
    
    // fill the multichannel delay buffer with the multichannel buffer's data
    for (int channel = 0; channel < MULTICHANNEL_TOTAL_INPUTS; ++channel) {
        const float* bufferData = multiChannelBuffer.getReadPointer(channel);
        fillDelayBuffer(multiChannelDelayBuffer, channel, bufferLength, delayBufferLength, bufferData);
    }
 
    float** bufferDataArr = multiChannelBuffer.getArrayOfWritePointers();
    float** diffusedBufferDataArr = multiChannelDiffusedBuffer.getArrayOfWritePointers();
    float** diffusedBufferHelperDataArr = multiChannelDiffusedBufferHelper.getArrayOfWritePointers();
    float** delayBufferDataArr = multiChannelDelayBuffer.getArrayOfWritePointers();
    float** diffusedDelayBufferDataArr = multiChannelDiffusedDelayBuffer.getArrayOfWritePointers();
    
    const int DIFFUSE_STEPS = 5;
    
    for (int i = 0; i < DIFFUSE_STEPS; ++i) {
        const int delay = 40 * pow(2, i);
        diffuseBuffer(diffusedBufferDataArr, diffusedBufferHelperDataArr, delayBufferDataArr, bufferLength, delayBufferLength, delay);
        
        // add diffuse helper buffer into main diffused buffer
        for (int channel = 0; channel < MULTICHANNEL_TOTAL_INPUTS; ++channel) {
            const float* diffusedBufferHelperData = multiChannelDiffusedBufferHelper.getReadPointer(channel);
            multiChannelDiffusedBuffer.addFrom(channel, 0, diffusedBufferHelperData, bufferLength);
        }
    }
    
    multiChannelDiffusedBuffer.applyGain(1 / (float)DIFFUSE_STEPS);
    
    // fill the multichannel diffused delay buffer with the multichannel diffused buffer's data
    for (int channel = 0; channel < MULTICHANNEL_TOTAL_INPUTS; ++channel) {
        const float* bufferData = multiChannelDiffusedBuffer.getReadPointer(channel);
        fillDelayBuffer(multiChannelDiffusedDelayBuffer, channel, bufferLength, delayBufferLength, bufferData);
    }
 

    // add the feedback delay
    /*
    for (int i = 1; i <= settings.numOfDelays; ++i) {
        const float delay = settings.delayLength * i;
        const int readPosition = getReadPosition(writePosition, delay, 0, delayBufferLength);
        for (int j = 0; j < bufferLength; ++j) {
            const int bufferIndex = j;
            const int readPosition_ = (readPosition + j) % delayBufferLength;
            //addFromDelayBuffer(bufferDataArr, diffusedDelayBufferDataArr, readPosition_, bufferIndex, delay);
            addFromDelayBuffer(bufferDataArr, delayBufferDataArr, readPosition_, bufferIndex, delay);
            
            const int writePosition_ = (writePosition + j) % delayBufferLength;
            feedbackDelay(bufferDataArr, delayBufferDataArr, writePosition_);
        }
    }
     */
    const int readPosition = getReadPosition(writePosition, settings.delayLength, 0, delayBufferLength);
    for (int j = 0; j < bufferLength; ++j) {
        const int bufferIndex = j;
        const int readPosition_ = (readPosition + j) % delayBufferLength;
        //addFromDelayBuffer(bufferDataArr, diffusedDelayBufferDataArr, readPosition_, bufferIndex, delay);
        addFromDelayBuffer(bufferDataArr, delayBufferDataArr, readPosition_, bufferIndex, settings.delayLength);
        
        const int writePosition_ = (writePosition + j) % delayBufferLength;
        feedbackDelay(bufferDataArr, delayBufferDataArr, writePosition_, bufferIndex);
    }
    
    
    // condense the multiChannel buffer data back into the original buffer
    const float gainDivisor = static_cast<float>(totalNumInputChannels) / static_cast<float>(MULTICHANNEL_TOTAL_INPUTS);
    for (int channel = 0; channel < MULTICHANNEL_TOTAL_INPUTS; ++channel) {
        int originalChannel = channel % totalNumInputChannels;
        const float* bufferData = multiChannelBuffer.getReadPointer(channel);
        //const float* bufferData = multiChannelDiffusedBuffer.getReadPointer(channel);
        if (channel < totalNumInputChannels) {
            buffer.copyFromWithRamp(originalChannel, 0, bufferData, bufferLength, gainDivisor, gainDivisor);
        } else {
            buffer.addFromWithRamp(originalChannel, 0, bufferData, bufferLength, gainDivisor, gainDivisor);
        }
    }
    
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

int CompSoundFinalProjectAudioProcessor::getReadPosition(const int writePosition, const int delay, const int offset, const int delayBufferLength) {
    int readPosition = static_cast<int>((writePosition + (delayBufferLength - (mSampleRate * delay / 1000)) + offset) % delayBufferLength);
    return readPosition;
}

void CompSoundFinalProjectAudioProcessor::copyToMatrix(juce::dsp::Matrix<float>& matrix, float** buffer, const int bufferPos) {
    for (int i = 0; i < MATRIX_SIZE; ++i) {
        for (int j = 0; j < MATRIX_SIZE; ++j) {
            matrix(i,j) = buffer[j][bufferPos];
        }
    }
}

void CompSoundFinalProjectAudioProcessor::copyFromMatrix(juce::dsp::Matrix<float>& matrix, float** buffer, const int bufferPos) {
    for (int i = 0; i < MATRIX_SIZE; ++i) {
        for (int j = 0; j < MATRIX_SIZE; ++j) {
            buffer[j][bufferPos] =  matrix(i,j);
        }
    }

}

void CompSoundFinalProjectAudioProcessor::fillDelayBuffer(
                                                          juce::AudioBuffer<float>& delayBuffer,
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

void CompSoundFinalProjectAudioProcessor::diffuseBuffer(
                                                        float** bufferDataArr,
                                                        float** diffusedBufferDataArr,
                                                        float** delayBufferDataArr,
                                                        const int bufferLength,
                                                        const int delayBufferLength,
                                                        const float delay
                                                        ) {
    // use to break delay into evenly divided sections
    const float delaySegment = delay / MULTICHANNEL_TOTAL_INPUTS;
    
    // add in evenly-distributed random delay to each channel
    // _____________________
    // |    |    |    |    | <- a channel's delay falls somewhere in its segment
    // |____|____|____|____|
    //  seg0 seg1 seg2 seg3
    for (int i = 0; i < MULTICHANNEL_TOTAL_INPUTS; ++i) {
        const int segment = delaySegment * i;
        const int randomDelay = segment + rand() % (int)delaySegment;
        const int readPosition = getReadPosition(writePosition, randomDelay, 0, delayBufferLength);
        for (int j = 0; j < bufferLength; ++j) {
            const int readPosition_ = (readPosition + j) % delayBufferLength;
            diffusedBufferDataArr[i][j] = delayBufferDataArr[i][readPosition_];
        }
    }
   
    // mix with permutation matrix
    for (int i = 0; i < bufferLength; ++i) {
        copyToMatrix(currentStepMatrix, diffusedBufferDataArr, i);
        currentStepMatrixOutput = currentStepMatrix * permutationMatrix;
        copyFromMatrix(currentStepMatrixOutput, diffusedBufferDataArr, i);
    }
    
    // mix with hadamard matrix
    for (int i = 0; i < bufferLength; ++i) {
        copyToMatrix(currentStepMatrix, diffusedBufferDataArr, i);
        currentStepMatrixOutput = currentStepMatrix * hadamardMatrix;
        copyFromMatrix(currentStepMatrixOutput, diffusedBufferDataArr, i);
    }

};

void CompSoundFinalProjectAudioProcessor::addFromDelayBuffer(
                                                             float** bufferDataArr,
                                                             float** delayBufferDataArr,
                                                             const int readPosition,
                                                             const int bufferIndex,
                                                             const int delay
                                                             ) {
    /*
    const float baseWetGain = (settings.wetLevel * 0.8);
    // linearly increases to 1 as delay approaches decay
    const float decayAmount = baseWetGain * std::min(((delay + bufferIndex) / settings.decay), (float)1.0);
    // non-linearly increases to 1 as delay approaches gateCutoff
    const float gateCutoffAmount = baseWetGain * (1 / (settings.gateCutoff - std::min((float)(delay + bufferIndex), (float)(settings.gateCutoff - 1))));
    const float wetGain = (baseWetGain - decayAmount - gateCutoffAmount) / settings.numOfDelays;
     */
    
    //const float wetGain = (settings.wetLevel * 0.8) / settings.numOfDelays;
    
    copyToMatrix(currentStepMatrix, delayBufferDataArr, readPosition);
    currentStepMatrixOutput = currentStepMatrix * householderMatrix;
    currentStepMatrixOutput *= settings.wetLevel;
    copyFromMatrix(currentStepMatrixOutput, bufferDataArr, bufferIndex);
    
    /*
    for (int i = 0; i < MATRIX_SIZE; ++i) {
        for (int j = 0; j < MATRIX_SIZE; ++j) {
            bufferDataArr[j][bufferIndex] = currentStepMatrixOutput(i, j) * wetGain;
        }
    }
     */
}

void CompSoundFinalProjectAudioProcessor::feedbackDelay(
                                                        float** bufferDataArr,
                                                        float** delayBufferDataArr,
                                                        const int writePosition,
                                                        const int bufferIndex
                                                        ) {
    for (int i = 0; i < MULTICHANNEL_TOTAL_INPUTS; ++i) {
        delayBufferDataArr[i][writePosition] += (bufferDataArr[i][bufferIndex] * 0.8);
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
        const int delay = static_cast<int>(settings.delayLength) * i;
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
                                                           juce::NormalisableRange<float>(1.f, 5000.f, 1.f, 1.f),
                                                           5000.f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
                                                           GATE_CUTOFF,
                                                           GATE_CUTOFF,
                                                           juce::NormalisableRange<float>(1.f, 5000.f, 1.f, 1.f),
                                                           5000.f));
    

    return layout;
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CompSoundFinalProjectAudioProcessor();
}
