#include "BassmentProcessor.h"
#include "ui/BassmentEditor.h"

namespace
{
    constexpr double detectorRateHz = 12000.0;
    constexpr float smoothingSeconds = 0.02f;

    // Fixed bass voicing. Mix is the only control.
    constexpr float rangeHz = 40.0f; // bass pitch is folded into rangeHz..2*rangeHz
    constexpr float glideMs = 30.0f;
    constexpr float minConfidence = 0.7f; // below this the detector's pitch is held, not followed
    constexpr float subAttackMs = 5.0f;
    constexpr float subReleaseMs = 80.0f;
    constexpr float lowCrossoverHz = 120.0f;
    constexpr float highCrossoverHz = 1500.0f;
    constexpr float midDriveDb = 15.0f;
    constexpr float highDriveDb = 10.0f;
    constexpr float wobbleDepth = 0.3f; // 0 turns the tempo-synced filter sweep off
    constexpr float formantMotionOctaves = 0.4f; // 0 turns the vowel swing off
    constexpr float outputTrim = 0.7f; // about -3 dB, after the final limiter
}

BassmentProcessor::BassmentProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      params (*this, nullptr, "PARAMS", createParameterLayout())
{
    mix = params.getRawParameterValue ("mix");
}

juce::AudioProcessorValueTreeState::ParameterLayout BassmentProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "mix", 1 },
        "Mix",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f),
        100.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    return layout;
}

juce::AudioProcessorEditor* BassmentProcessor::createEditor()
{
    return new BassmentEditor (*this);
}

void BassmentProcessor::prepareToPlay (double newSampleRate, int samplesPerBlock)
{
    sampleRate = newSampleRate;

    const juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32> (samplesPerBlock), 1 };
    for (auto* filter : { &highPass, &lowPass1, &lowPass2 })
        filter->prepare (spec);

    highPass.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    highPass.setCutoffFrequency (50.0f);
    for (auto* filter : { &lowPass1, &lowPass2 })
    {
        filter->setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        filter->setCutoffFrequency (1000.0f);
    }

    decimationFactor = juce::jmax (1, juce::roundToInt (sampleRate / detectorRateHz));
    decimationCount = 0;
    detector.prepare (sampleRate / decimationFactor);

    tracker.prepare (sampleRate);
    tracker.setParams (rangeHz, glideMs);

    vocoder.prepare (sampleRate);
    ott.prepare (sampleRate);
    subEnvelope.prepare (sampleRate);
    subEnvelope.setTimes (subAttackMs, subReleaseMs);
    sawCarrier.setShape (1.0f);
    subSine.setShape (0.0f);
    sawCarrier.reset();
    subSine.reset();

    growl.prepare (sampleRate);
    growl.setTarget (wobbleDepth, 0.0f);

    bass.assign (static_cast<size_t> (samplesPerBlock), 0.0f);
    drive.prepare (sampleRate, samplesPerBlock);
    drive.setParams (lowCrossoverHz, highCrossoverHz, midDriveDb, highDriveDb);

    const auto latency = detector.latencySamples() * decimationFactor + drive.getLatencySamples();
    setLatencySamples (latency);
    dryDelay.setMaximumDelayInSamples (latency + 1);
    dryDelay.prepare ({ sampleRate, static_cast<juce::uint32> (samplesPerBlock), 2 });
    dryDelay.setDelay (static_cast<float> (latency));

    mixSmoothed.reset (sampleRate, smoothingSeconds);
    mixSmoothed.setCurrentAndTargetValue (mix->load() * 0.01f);
}

bool BassmentProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
        && layouts.getMainInputChannelSet() == out;
}

void BassmentProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (auto channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear (channel, 0, buffer.getNumSamples());

    if (auto* host = getPlayHead())
        if (auto position = host->getPosition())
            if (auto bpm = position->getBpm())
                tempoBpm = *bpm;

    // Hosts may exceed the block size announced in prepareToPlay, so never assume it.
    const auto maxChunk = juce::jmax (1, static_cast<int> (bass.size()));
    for (int start = 0; start < buffer.getNumSamples(); start += maxChunk)
    {
        juce::AudioBuffer<float> chunk (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), start,
                                        juce::jmin (maxChunk, buffer.getNumSamples() - start));
        processChunk (chunk);
    }
}

void BassmentProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Keep the delay the host was told about, so bypassing doesn't shift the timing.
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* samples = buffer.getWritePointer (channel);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            dryDelay.pushSample (channel, samples[i]);
            samples[i] = dryDelay.popSample (channel);
        }
    }
}

void BassmentProcessor::processChunk (juce::AudioBuffer<float>& buffer)
{
    growl.setTarget (wobbleDepth, static_cast<float> (tempoBpm / 60.0 * 2.0)); // eighth notes
    vocoder.setMotion (formantMotionOctaves, static_cast<float> (tempoBpm / 60.0)); // quarter notes
    mixSmoothed.setTargetValue (mix->load() * 0.01f);

    const auto numSamples = buffer.getNumSamples();
    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto monoIn = right != nullptr ? 0.5f * (left[i] + right[i]) : left[i];

        const auto filtered = lowPass2.processSample (0, lowPass1.processSample (0, highPass.processSample (0, monoIn)));
        if (++decimationCount >= decimationFactor)
        {
            decimationCount = 0;
            if (detector.push (filtered))
            {
                const auto estimate = detector.result();
                tracker.update (estimate.frequency, estimate.confidence > minConfidence);
            }
        }

        dryDelay.pushSample (0, left[i]);
        left[i] = dryDelay.popSample (0);
        auto delayedMono = left[i];
        if (right != nullptr)
        {
            dryDelay.pushSample (1, right[i]);
            right[i] = dryDelay.popSample (1);
            delayedMono = 0.5f * (left[i] + right[i]);
        }

        // No gate: with nothing to follow, the vocoder and the level follower fall silent on their own,
        // and unpitched material (drums) is voiced at the last held pitch.
        const auto frequency = tracker.nextFrequency();
        bass[static_cast<size_t> (i)] = ott.process (vocoder.process (delayedMono, sawCarrier.next (frequency, sampleRate))
                                                     + subSine.next (frequency, sampleRate) * subEnvelope.process (delayedMono));
    }

    drive.process (bass.data(), numSamples);
    growl.process (bass.data(), numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        const auto wet = mixSmoothed.getNextValue();
        // Heavy drive plus resonance can overshoot, so round off the peaks.
        const auto bassSample = outputTrim * std::tanh (bass[static_cast<size_t> (i)]);
        left[i] = left[i] * (1.0f - wet) + bassSample * wet;
        if (right != nullptr)
            right[i] = right[i] * (1.0f - wet) + bassSample * wet;
    }

    bassHz.store (tracker.targetHz(), std::memory_order_relaxed);
    bassActive.store (tracker.isOpen(), std::memory_order_relaxed);
    for (int band = 0; band < Vocoder::numBands; ++band)
        inputSpectrum[static_cast<size_t> (band)].store (vocoder.bandLevel (band), std::memory_order_relaxed);
}

void BassmentProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = params.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void BassmentProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (params.state.getType()))
            params.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BassmentProcessor();
}
