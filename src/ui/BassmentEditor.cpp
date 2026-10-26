#include "BassmentEditor.h"

namespace
{
    // Layout is designed at this size and scaled with the window.
    constexpr int designWidth = 400;
    constexpr int designHeight = 520;

    // Input band levels are drawn between these two, so a quiet vocal still moves the bars.
    constexpr float barFloorDb = -60.0f;
    constexpr float barCeilingDb = -6.0f;
    constexpr float barFall = 0.82f; // per frame, 30 frames a second
}

BassmentEditor::BassmentEditor (BassmentProcessor& owner)
    : AudioProcessorEditor (owner),
      processor (owner),
      mixAttachment (owner.params, "mix", mixKnob)
{
    setLookAndFeel (&lookAndFeel);

    mixKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    mixKnob.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    mixKnob.setMouseDragSensitivity (250);
    mixKnob.setWantsKeyboardFocus (true);
    mixKnob.setTitle ("Mix");
    mixKnob.textFromValueFunction = [] (double value) { return juce::String (juce::roundToInt (value)) + " %"; };
    mixKnob.updateText();
    addAndMakeVisible (mixKnob);

    setResizable (true, true);
    setResizeLimits (designWidth * 3 / 4, designHeight * 3 / 4, designWidth * 2, designHeight * 2);
    getConstrainer()->setFixedAspectRatio (static_cast<double> (designWidth) / designHeight);
    setSize (designWidth, designHeight);

    startTimerHz (30);
}

BassmentEditor::~BassmentEditor()
{
    setLookAndFeel (nullptr);
}

void BassmentEditor::timerCallback()
{
    const auto active = processor.bassActive.load (std::memory_order_relaxed);
    const auto hz = processor.bassHz.load (std::memory_order_relaxed);

    juce::String text = "No pitch";
    if (active)
    {
        const auto note = juce::roundToInt (69.0f + 12.0f * std::log2 (hz / 440.0f));
        text = juce::MidiMessage::getMidiNoteName (note, true, true, 3) + "  " + juce::String (hz, 1) + " Hz";
    }

    if (text != readoutText || active != readoutActive)
    {
        readoutText = text;
        readoutActive = active;
        repaint (headerArea);
    }

    auto barsChanged = false;
    for (size_t band = 0; band < barHeights.size(); ++band)
    {
        const auto level = processor.inputSpectrum[band].load (std::memory_order_relaxed);
        const auto db = juce::Decibels::gainToDecibels (level, barFloorDb);
        const auto target = juce::jlimit (0.0f, 1.0f, (db - barFloorDb) / (barCeilingDb - barFloorDb));
        const auto next = juce::jmax (target, barHeights[band] * barFall);
        barsChanged = barsChanged || std::abs (next - barHeights[band]) > 0.002f;
        barHeights[band] = next;
    }

    if (barsChanged)
        repaint (spectrumPanel);
}

void BassmentEditor::paintSpectrum (juce::Graphics& g, float scale) const
{
    const auto panel = spectrumPanel.toFloat();
    const auto radius = 10.0f * scale;

    g.setColour (palette::surface);
    g.fillRoundedRectangle (panel, radius);
    g.setColour (palette::border);
    g.drawRoundedRectangle (panel.reduced (0.5f), radius, 1.0f);

    const auto bars = spectrumBars.toFloat();
    const auto slot = bars.getWidth() / static_cast<float> (barHeights.size());
    const auto barWidth = slot * 0.68f;
    const auto idleHeight = 2.0f * scale;

    for (size_t band = 0; band < barHeights.size(); ++band)
    {
        const auto height = juce::jmax (idleHeight, barHeights[band] * bars.getHeight());
        g.setColour (barHeights[band] > 0.02f ? palette::accent : palette::track);
        g.fillRoundedRectangle (bars.getX() + slot * static_cast<float> (band) + (slot - barWidth) * 0.5f,
                                bars.getBottom() - height, barWidth, height, 2.0f * scale);
    }

    g.setColour (palette::textMuted);
    g.setFont (juce::FontOptions (12.0f * scale));
    const auto labels = panel.reduced (16.0f * scale, 10.0f * scale);
    g.drawText ("Input spectrum", labels, juce::Justification::topLeft);
    g.drawText ("100 Hz", labels, juce::Justification::bottomLeft);
    g.drawText ("8 kHz", labels, juce::Justification::bottomRight);
}

void BassmentEditor::paint (juce::Graphics& g)
{
    const auto scale = static_cast<float> (getWidth()) / designWidth;

    g.fillAll (palette::background);

    g.setColour (palette::border);
    g.fillRect (0, headerArea.getBottom(), getWidth(), 1);

    g.setColour (palette::text);
    g.setFont (juce::FontOptions (17.0f * scale, juce::Font::bold));
    g.drawText ("Bassment", headerArea.withTrimmedLeft (static_cast<int> (24 * scale)), juce::Justification::centredLeft);

    const juce::Font readoutFont { juce::FontOptions (14.0f * scale) };
    const auto dot = 8.0f * scale;
    const auto textWidth = juce::GlyphArrangement::getStringWidth (readoutFont, readoutText);
    g.setColour (readoutActive ? palette::accent : palette::track);
    g.fillEllipse (static_cast<float> (readoutArea.getRight()) - textWidth - dot - 8.0f * scale,
                   static_cast<float> (readoutArea.getCentreY()) - dot * 0.5f, dot, dot);
    g.setColour (readoutActive ? palette::text : palette::textMuted);
    g.setFont (readoutFont);
    g.drawText (readoutText, readoutArea, juce::Justification::centredRight);

    paintSpectrum (g, scale);

    g.setColour (palette::textMuted);
    g.setFont (juce::FontOptions (13.0f * scale));
    g.drawText ("Dry", dryLabelArea, juce::Justification::centredLeft);
    g.drawText ("Bass", bassLabelArea, juce::Justification::centredRight);
    g.drawText ("Mix", captionArea, juce::Justification::centred);
}

void BassmentEditor::resized()
{
    const auto scale = static_cast<float> (getWidth()) / designWidth;
    const auto px = [scale] (int designUnits) { return juce::roundToInt (designUnits * scale); };

    headerArea = { 0, 0, getWidth(), px (64) };
    readoutArea = headerArea.withTrimmedRight (px (24)).withTrimmedLeft (getWidth() / 3);

    spectrumPanel = { px (24), px (84), getWidth() - 2 * px (24), px (132) };
    spectrumBars = spectrumPanel.withTrimmedLeft (px (16)).withTrimmedRight (px (16)).withTrimmedTop (px (32)).withTrimmedBottom (px (30));

    // The knob is the square above the text box.
    const auto knobSize = px (210);
    const auto textBoxHeight = px (28);
    mixKnob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, px (110), textBoxHeight);
    mixKnob.setBounds ((getWidth() - knobSize) / 2, spectrumPanel.getBottom() + px (24), knobSize, knobSize + textBoxHeight);

    // The ends of the knob's arc sit at its lower left and right.
    const auto endLabelY = mixKnob.getY() + knobSize - px (16);
    dryLabelArea = { mixKnob.getX() - px (8), endLabelY, px (60), px (20) };
    bassLabelArea = { mixKnob.getRight() - px (52), endLabelY, px (60), px (20) };

    captionArea = { 0, mixKnob.getBottom() + px (4), getWidth(), px (20) };
}
