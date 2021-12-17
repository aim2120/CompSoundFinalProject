/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

struct Settings {
    int mode { 0 };
    float gain { 0 };
    float roomSize { 0 };
    float damping { 0 };
    float dampingFreq { 0 };
    float wetLevel { 0 };
    float dryLevel { 0 };
    float width { 0 };
    float delayLength { 0 };
    float diffusion { 0 };
    float decay { 0 };
    bool freezeMode { false };
    bool reverse { false };
};

Settings getSettings(juce::AudioProcessorValueTreeState& apvts);

const std::string MODE = "Mode";
const std::string GAIN = "Gain";
const std::string ROOM_SIZE = "Room Size";
const std::string DAMPING = "Damping";
const std::string DAMPING_FREQ = "Damping Frequency Cutoff";
const std::string WET_LEVEL = "Wet Level";
const std::string DRY_LEVEL = "Dry Level";
const std::string WIDTH = "Width";
const std::string DELAY_LENGTH = "Pre-Delay (ms)";
const std::string DIFFUSION = "Diffusion";
const std::string DECAY = "Decay Rate";
const std::string FREEZE_MODE = "Freeze Mode";
const std::string REVERSE = "Reverse";

const int MULTICHANNEL_TOTAL_INPUTS = 4;
const int MATRIX_SIZE = 4;

const juce::String modes[] {
    "Basic Reverb",
    "My Reverb"
};

//==============================================================================
/**
*/
class CompSoundFinalProjectAudioProcessor  : public juce::AudioProcessor
{
public:
    //==============================================================================
    CompSoundFinalProjectAudioProcessor();
    ~CompSoundFinalProjectAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void setReverbParameters();
    void copyToMatrix(juce::dsp::Matrix<float>& matrix, float** buffer, const int bufferPos);
    void copyFromMatrix(juce::dsp::Matrix<float>& matrix, float** buffer, const int bufferPos);
    int getReadPosition(const int writePosition, const int delay, const int offset, const int delayBufferLength);
    void fillDelayBuffer(juce::AudioBuffer<float>& delayBuffer, int channel, const int bufferLength, const int delayBufferLength, const float* bufferData);
    void diffuseBuffer(float** diffusedBufferDataArr, float** delayBufferDataArr, const int bufferLength, const int delayBufferLength, const float delay);
    void addFromDelayBuffer(float** bufferDataArr, float** delayBufferDataArr, const int readPosition, const int bufferIndex, const int delay);
    void feedbackDelay(float** bufferDataArr, float** delayBufferDataArr, const int writePosition, const int bufferIndex);

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts {*this, nullptr, "Parameters", createParameterLayout()};

private:
    // circular buffer variables
    juce::AudioBuffer<float> multiChannelBuffer;
    juce::AudioBuffer<float> multiChannelDiffusedBuffer;
    juce::AudioBuffer<float> multiChannelDiffusedBufferHelper;
    juce::AudioBuffer<float> multiChannelDiffusedBufferLowPass;
    juce::AudioBuffer<float> multiChannelDelayBuffer;
    juce::AudioBuffer<float> multiChannelDiffusedDelayBuffer;
    std::vector<int> diffuseDelays {MULTICHANNEL_TOTAL_INPUTS};
    int writePosition { 0 };
    int mSampleRate;
    
    // dsp effects variables
        enum {
        reverbIndex
    };
    juce::dsp::ProcessorChain<juce::dsp::Reverb> processorChain;
    juce::dsp::Reverb reverb;
    juce::dsp::Reverb::Parameters reverbParams;
    Settings settings;
    
    // diffuser variables
    juce::dsp::Matrix<float> householderMatrix { juce::dsp::Matrix<float>(MATRIX_SIZE, MATRIX_SIZE) };
    juce::dsp::Matrix<float> hadamardMatrix { juce::dsp::Matrix<float>(MATRIX_SIZE, MATRIX_SIZE) };
    juce::dsp::Matrix<float> permutationMatrix { juce::dsp::Matrix<float>(MATRIX_SIZE, MATRIX_SIZE) };
    juce::dsp::Matrix<float> currentStepMatrix { juce::dsp::Matrix<float>(MATRIX_SIZE, MATRIX_SIZE) };
    juce::dsp::Matrix<float> currentStepMatrixOutput { juce::dsp::Matrix<float>(MATRIX_SIZE, MATRIX_SIZE) };
    
    
    // reverb effect variables
    juce::IIRFilter lowPassFilters[MULTICHANNEL_TOTAL_INPUTS];
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompSoundFinalProjectAudioProcessor)
};
