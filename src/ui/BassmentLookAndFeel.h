#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// One neutral dark base and a single warm accent that marks value and focus.
namespace palette
{
    const juce::Colour background { 0xff111113 };
    const juce::Colour surface { 0xff1b1b1f };
    const juce::Colour border { 0xff2c2c32 };
    const juce::Colour track { 0xff303037 };
    const juce::Colour text { 0xffececef };
    const juce::Colour textMuted { 0xff9a9aa4 };
    const juce::Colour accent { 0xffff7a3c };
}

class BassmentLookAndFeel : public juce::LookAndFeel_V4
{
public:
    BassmentLookAndFeel()
    {
        setColour (juce::Slider::textBoxTextColourId, palette::text);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId, palette::accent.withAlpha (0.4f));
        setColour (juce::TextEditor::backgroundColourId, palette::surface);
        setColour (juce::TextEditor::textColourId, palette::text);
        setColour (juce::TextEditor::highlightedTextColourId, palette::text);
        setColour (juce::TextEditor::outlineColourId, palette::border);
        setColour (juce::TextEditor::focusedOutlineColourId, palette::accent);
        setColour (juce::CaretComponent::caretColourId, palette::accent);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPosition,
                           float startAngle, float endAngle, juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y),
                                                    static_cast<float> (width), static_cast<float> (height));
        const auto outerRadius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f - 3.0f;
        const auto thickness = juce::jmax (3.0f, outerRadius * 0.13f);
        const auto arcRadius = outerRadius - thickness * 0.5f;
        const auto centre = bounds.getCentre();
        const auto angle = startAngle + sliderPosition * (endAngle - startAngle);
        const auto stroke = juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

        juce::Path trackArc;
        trackArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
        g.setColour (palette::track);
        g.strokePath (trackArc, stroke);

        if (sliderPosition > 0.0f)
        {
            juce::Path valueArc;
            valueArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, angle, true);
            g.setColour (palette::accent);
            g.strokePath (valueArc, stroke);
        }

        const auto bodyRadius = arcRadius - thickness * 1.6f;
        g.setColour (palette::surface);
        g.fillEllipse (centre.x - bodyRadius, centre.y - bodyRadius, bodyRadius * 2.0f, bodyRadius * 2.0f);
        g.setColour (palette::border);
        g.drawEllipse (centre.x - bodyRadius, centre.y - bodyRadius, bodyRadius * 2.0f, bodyRadius * 2.0f, 1.0f);

        const auto pointerStart = juce::Point<float> (centre.x + std::sin (angle) * bodyRadius * 0.45f,
                                                      centre.y - std::cos (angle) * bodyRadius * 0.45f);
        const auto pointerEnd = juce::Point<float> (centre.x + std::sin (angle) * bodyRadius * 0.85f,
                                                    centre.y - std::cos (angle) * bodyRadius * 0.85f);
        g.setColour (palette::text);
        g.drawLine ({ pointerStart, pointerEnd }, juce::jmax (2.0f, thickness * 0.5f));

        if (slider.hasKeyboardFocus (false))
        {
            const auto ringRadius = outerRadius + 1.5f;
            g.setColour (palette::accent);
            g.drawEllipse (centre.x - ringRadius, centre.y - ringRadius, ringRadius * 2.0f, ringRadius * 2.0f, 1.5f);
        }
    }

    juce::Label* createSliderTextBox (juce::Slider& slider) override
    {
        auto* label = juce::LookAndFeel_V4::createSliderTextBox (slider);
        label->setJustificationType (juce::Justification::centred);
        return label;
    }

    juce::Font getLabelFont (juce::Label& label) override
    {
        return juce::Font (juce::FontOptions (juce::jmax (11.0f, label.getHeight() * 0.72f)));
    }
};
