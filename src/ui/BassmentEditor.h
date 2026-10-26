#pragma once

#include "BassmentLookAndFeel.h"
#include "../BassmentProcessor.h"

class BassmentEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit BassmentEditor (BassmentProcessor&);
    ~BassmentEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void paintSpectrum (juce::Graphics&, float scale) const;

    BassmentProcessor& processor;
    BassmentLookAndFeel lookAndFeel;

    juce::Slider mixKnob;
    juce::AudioProcessorValueTreeState::SliderAttachment mixAttachment;

    // Laid out in resized(), drawn in paint().
    juce::Rectangle<int> headerArea, readoutArea, spectrumPanel, spectrumBars, dryLabelArea, bassLabelArea, captionArea;
    juce::String readoutText { "No pitch" };
    bool readoutActive = false;

    // 0..1 height of each input band, with a slow fall so the bars stay readable.
    std::array<float, Vocoder::numBands> barHeights {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BassmentEditor)
};
