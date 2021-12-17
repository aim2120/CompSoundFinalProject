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
    multiChannelDiffusedBufferLowPass.setSize(MULTICHANNEL_TOTAL_INPUTS, samplesPerBlock);
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
    
    
    for (int i = 0; i < MULTICHANNEL_TOTAL_INPUTS; ++i) {
        diffuseDelays[i] = rand() % 20;
    }
    
    for (int channel = 0; channel < MULTICHANNEL_TOTAL_INPUTS; ++channel) {
        lowPassFilters[channel].setCoefficients(juce::IIRCoefficients::makeLowPass(sampleRate, settings.dampingFreq));
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
    
    /*
    if (settings.mode == 0) {
        
        auto audioBlock = juce::dsp::AudioBlock<float>(buffer);
        auto processContext = juce::dsp::ProcessContextReplacing<float>(audioBlock);
        reverb.setParameters(reverbParams);
        reverb.setEnabled(true);
        reverb.process(processContext);
        
        return;
    }
     */

    // convert the buffer buffer to multichannel
    for (int channel = 0; channel < MULTICHANNEL_TOTAL_INPUTS; ++channel) {
        int originalChannel = channel % totalNumInputChannels;
        const float* bufferData = buffer.getReadPointer(originalChannel);
        multiChannelBuffer.copyFrom(channel, 0, bufferData, bufferLength);
        multiChannelDiffusedBuffer.copyFrom(channel, 0, bufferData, bufferLength);
    }
    
    if (settings.reverse) {
        multiChannelBuffer.reverse(0, bufferLength);
        multiChannelDiffusedBuffer.reverse(0, bufferLength);
    }
   
    // fill the multichannel circular delay buffer
    for (int channel = 0; channel < MULTICHANNEL_TOTAL_INPUTS; ++channel) {
        const float* bufferData = multiChannelBuffer.getReadPointer(channel);
        fillDelayBuffer(multiChannelDelayBuffer, channel, bufferLength, delayBufferLength, bufferData);
    }
 
    float** bufferDataArr = multiChannelBuffer.getArrayOfWritePointers();
    float** diffusedBufferDataArr = multiChannelDiffusedBuffer.getArrayOfWritePointers();
    float** diffusedBufferHelperDataArr = multiChannelDiffusedBufferHelper.getArrayOfWritePointers();
    float** delayBufferDataArr = multiChannelDelayBuffer.getArrayOfWritePointers();
    float** diffusedDelayBufferDataArr = multiChannelDiffusedDelayBuffer.getArrayOfWritePointers();
    
    // diffuse the signal
    for (int i = 1; i <= (int)settings.diffusion; ++i) {
        const int delay = static_cast<int>((20 + i) * pow(1.6, i));
        diffuseBuffer(diffusedBufferHelperDataArr, delayBufferDataArr, bufferLength, delayBufferLength, delay);
        
        const float diffuseGain = 0.9 - (i * 0.1); // decrease with each diffusion step
        
        // add diffuse helper buffer into main diffused buffer
        for (int channel = 0; channel < MULTICHANNEL_TOTAL_INPUTS; ++channel) {
            const float* diffusedBufferHelperData = multiChannelDiffusedBufferHelper.getReadPointer(channel);
            multiChannelDiffusedBuffer.addFromWithRamp(channel, 0, diffusedBufferHelperData, bufferLength, diffuseGain, diffuseGain);
        }
    }
    
    // apply low pass to diffused signal
    // mix low passed diffused signal w/ regular diffused signal according to settings
    for (int channel = 0; channel < MULTICHANNEL_TOTAL_INPUTS; ++channel) {
        multiChannelDiffusedBufferLowPass.copyFrom(channel, 0, diffusedBufferDataArr[channel], bufferLength);
        float* lowPassBufferData = multiChannelDiffusedBufferLowPass.getWritePointer(channel);
        
        lowPassFilters[channel].setCoefficients(juce::IIRCoefficients::makeLowPass(mSampleRate, settings.dampingFreq));
        lowPassFilters[channel].processSamples(lowPassBufferData, bufferLength);
        
        multiChannelDiffusedBuffer.applyGain(channel, 0, bufferLength, 1 - settings.damping);
        multiChannelDiffusedBuffer.addFromWithRamp(channel, 0, lowPassBufferData, bufferLength, settings.damping, settings.damping);
    }
    
    // fill the multichannel diffused circular delay buffer
    for (int channel = 0; channel < MULTICHANNEL_TOTAL_INPUTS; ++channel) {
        const float* bufferData = multiChannelDiffusedBuffer.getReadPointer(channel);
        fillDelayBuffer(multiChannelDiffusedDelayBuffer, channel, bufferLength, delayBufferLength, bufferData);
    }
 
    // add the feedback delay
    const int readPosition = getReadPosition(writePosition, settings.delayLength, 0, delayBufferLength);
    for (int i = 0; i < bufferLength; ++i) {
        const int bufferIndex = i;
        const int readPosition_ = (readPosition + i) % delayBufferLength;
        addFromDelayBuffer(bufferDataArr, diffusedDelayBufferDataArr, readPosition_, bufferIndex, settings.delayLength);
        
        const int writePosition_ = (writePosition + i) % delayBufferLength;
        feedbackDelay(bufferDataArr, diffusedDelayBufferDataArr, writePosition_, bufferIndex);
    }
    
    // apply dry and wet gain individually
    buffer.applyGain(settings.dryLevel);
    multiChannelBuffer.applyGain(settings.wetLevel * 0.8);
    
    // condense the multichannel buffer and add to original buffer
    const float gainDivisor = static_cast<float>(totalNumInputChannels) / static_cast<float>(MULTICHANNEL_TOTAL_INPUTS);
    for (int channel = 0; channel < MULTICHANNEL_TOTAL_INPUTS; ++channel) {
        int originalChannel = channel % totalNumInputChannels;
        const float* bufferData = multiChannelBuffer.getReadPointer(channel);
        //const float* bufferData = multiChannelDiffusedBuffer.getReadPointer(channel);
        buffer.addFromWithRamp(originalChannel, 0, bufferData, bufferLength, gainDivisor, gainDivisor);
    }
    
    // advance write head
    writePosition += bufferLength;
    writePosition %= delayBufferLength;
        
    // apply global gain
    buffer.applyGain(settings.gain);
}

void CompSoundFinalProjectAudioProcessor::setReverbParameters() {
    reverbParams.roomSize = settings.roomSize;
    reverbParams.damping = settings.damping;
    reverbParams.wetLevel = settings.wetLevel;
    reverbParams.dryLevel = settings.dryLevel;
    reverbParams.width = settings.width;
    if (settings.freezeMode) {
        reverbParams.freezeMode = 1.0;
    } else {
        reverbParams.freezeMode = 0.0;
    }
    
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
                                                        float** diffusedBufferDataArr,
                                                        float** delayBufferDataArr,
                                                        const int bufferLength,
                                                        const int delayBufferLength,
                                                        const float delay
                                                        ) {
    // use to break delay into evenly divided sections
    const float delaySegment = delay / MULTICHANNEL_TOTAL_INPUTS;
    
    // add in evenly-distributed random delay to each channel
    // diffuse step range = [0, delay)
    // each channel has a segment of this range
    // _____________________
    // |    |    |    |    | <- a channel's delay falls somewhere in its segment
    // |____|____|____|____|
    //  seg0 seg1 seg2 seg3
    //  delay increase-->
    for (int i = 0; i < MULTICHANNEL_TOTAL_INPUTS; ++i) {
        const int segment = delaySegment * i;
        const int randomDelay = segment + diffuseDelays[i];
        //const int randomDelay = segment + i;
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
   
    copyToMatrix(currentStepMatrix, delayBufferDataArr, readPosition);
    currentStepMatrixOutput = currentStepMatrix * householderMatrix;
    currentStepMatrixOutput *= 0.8;
    copyFromMatrix(currentStepMatrixOutput, bufferDataArr, bufferIndex);
}

void CompSoundFinalProjectAudioProcessor::feedbackDelay(
                                                        float** bufferDataArr,
                                                        float** delayBufferDataArr,
                                                        const int writePosition,
                                                        const int bufferIndex
                                                        ) {
    for (int i = 0; i < MULTICHANNEL_TOTAL_INPUTS; ++i) {
        delayBufferDataArr[i][writePosition] += (bufferDataArr[i][bufferIndex] * settings.decay);
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
    
    settings.mode = apvts.getRawParameterValue(MODE)->load();
    settings.gain = apvts.getRawParameterValue(GAIN)->load();
    settings.roomSize = apvts.getRawParameterValue(GAIN)->load();
    settings.damping = apvts.getRawParameterValue(DAMPING)->load();
    settings.dampingFreq = apvts.getRawParameterValue(DAMPING_FREQ)->load();
    settings.wetLevel = apvts.getRawParameterValue(WET_LEVEL)->load();
    settings.dryLevel = apvts.getRawParameterValue(DRY_LEVEL)->load();
    settings.width = apvts.getRawParameterValue(WIDTH)->load();
    settings.delayLength = apvts.getRawParameterValue(DELAY_LENGTH)->load();
    settings.diffusion = apvts.getRawParameterValue(DIFFUSION)->load();
    settings.decay = apvts.getRawParameterValue(DECAY)->load();
    settings.freezeMode = apvts.getRawParameterValue(FREEZE_MODE)->load();
    settings.reverse = apvts.getRawParameterValue(REVERSE)->load();
    
    return settings;
}

juce::AudioProcessorValueTreeState::ParameterLayout CompSoundFinalProjectAudioProcessor::createParameterLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    
    layout.add(std::make_unique<juce::AudioParameterChoice>(
                                                            MODE,
                                                            MODE,
                                                            juce::StringArray(modes, 2),
                                                            0
                                                            ));
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
                                                           DAMPING_FREQ,
                                                           DAMPING_FREQ,
                                                           juce::NormalisableRange<float>(200.f, 4000.f, 10.f, 1.f),
                                                           1000.f));

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
                                                           DELAY_LENGTH,
                                                           DELAY_LENGTH,
                                                           juce::NormalisableRange<float>(0.f, 500.f, 0.1f, 0.5f),
                                                           30.f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
                                                           DIFFUSION,
                                                           DIFFUSION,
                                                           juce::NormalisableRange<float>(0.f, 8.f, 1.f, 1.f),
                                                           4.f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
                                                           DECAY,
                                                           DECAY,
                                                           juce::NormalisableRange<float>(0.f, 1.f, 0.01f, 1.f),
                                                           0.8f));
    layout.add(std::make_unique<juce::AudioParameterBool>(
                                                          FREEZE_MODE,
                                                          FREEZE_MODE,
                                                          false));
    layout.add(std::make_unique<juce::AudioParameterBool>(
                                                          REVERSE,
                                                          REVERSE,
                                                          true));

    return layout;
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CompSoundFinalProjectAudioProcessor();
}
