#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

#include "dsp/EnvelopeFollower.h"
#include "dsp/Growl.h"
#include "dsp/MultibandDrive.h"
#include "dsp/Ott.h"
#include "dsp/PitchDetector.h"
#include "dsp/PitchTracker.h"
#include "dsp/SubOscillator.h"
#include "dsp/Vocoder.h"

class BassmentProcessor : public juce::AudioProcessor
{
public:
    BassmentProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    void processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState params;

    // Read by the editor: the pitch the bass is playing and whether the detector is sure of it.
    std::atomic<float> bassHz { 0.0f };
    std::atomic<bool> bassActive { false };
    std::array<std::atomic<float>, Vocoder::numBands> inputSpectrum {};

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    using Filter = juce::dsp::StateVariableTPTFilter<float>;

    void processChunk (juce::AudioBuffer<float>& buffer);

    std::atomic<float>* mix = nullptr;

    // Pitch detection runs on a band-passed, decimated copy of the mono input.
    Filter highPass, lowPass1, lowPass2;
    PitchDetector detector;
    PitchTracker tracker;

    // The bass: a saw carrier shaped by the input's spectrum, plus a sine sub that follows its level,
    // levelled by Ott, then distorted and swept.
    Vocoder vocoder;
    Ott ott;
    EnvelopeFollower subEnvelope;
    SubOscillator sawCarrier, subSine;
    MultibandDrive drive;
    Growl growl;

    // Pitch estimates trail the input, so the dry signal and the bass modulator are delayed to match.
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> dryDelay { 1 };
    std::vector<float> bass;

    double sampleRate = 44100.0;
    double tempoBpm = 140.0;
    int decimationFactor = 4;
    int decimationCount = 0;

    juce::SmoothedValue<float> mixSmoothed;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BassmentProcessor)
};
