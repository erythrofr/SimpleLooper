#pragma once
#include <JuceHeader.h>

namespace Colours_
{
    // Background & surfaces
    const juce::Colour bg           { 0xff0d0d14 };
    const juce::Colour surface      { 0xff16172b };
    const juce::Colour surfaceLight { 0xff1e1f3b };
    const juce::Colour border       { 0xff2a2b4a };

    // Text
    const juce::Colour textPrimary  { 0xffe8e8f0 };
    const juce::Colour textDim      { 0xff787896 };

    // Accent
    const juce::Colour rec          { 0xffff4b5c };
    const juce::Colour play         { 0xff00d26a };
    const juce::Colour dub          { 0xfff5a623 };
    const juce::Colour stop         { 0xff5865f2 };
    const juce::Colour idle         { 0xff2d2d4e };
    const juce::Colour mute         { 0xffff4b5c };
    const juce::Colour solo         { 0xffffd93d };
    const juce::Colour afterloop    { 0xff00b8d4 };
    const juce::Colour fxReady      { 0xffbb86fc };
    const juce::Colour clear        { 0xff6e3040 };
    const juce::Colour undo         { 0xff3d3d5c };
    const juce::Colour divMul       { 0xff3d4f7c };
    const juce::Colour sliderTrack  { 0xff2a2a46 };
    const juce::Colour sliderThumb  { 0xff00d26a };
    const juce::Colour beatActive   { 0xffffa040 };
    const juce::Colour beatIdle     { 0xff2a2b4a };
    const juce::Colour beatMuted    { 0xff3d3050 };
}

class CustomLookAndFeel : public juce::LookAndFeel_V4
{
public:
    CustomLookAndFeel()
    {
        setColour(juce::ResizableWindow::backgroundColourId, Colours_::bg);
        setColour(juce::ComboBox::backgroundColourId,        Colours_::idle);
        setColour(juce::ComboBox::outlineColourId,           Colours_::border);
        setColour(juce::ComboBox::textColourId,              Colours_::textPrimary);
        setColour(juce::PopupMenu::backgroundColourId,       Colours_::surface);
        setColour(juce::PopupMenu::textColourId,             Colours_::textPrimary);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, Colours_::surfaceLight);
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour& bgColour,
                              bool isMouseOver, bool isButtonDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);
        float cornerSize = 6.0f;

        auto colour = bgColour;
        if (isButtonDown)    colour = colour.brighter(0.15f);
        else if (isMouseOver) colour = colour.brighter(0.08f);

        g.setColour(colour);
        g.fillRoundedRectangle(bounds, cornerSize);

        g.setColour(colour.brighter(0.12f));
        g.drawRoundedRectangle(bounds, cornerSize, 1.0f);
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& button,
                        bool isMouseOver, bool isButtonDown) override
    {
        auto font = juce::FontOptions(11.0f, juce::Font::bold);
        g.setFont(font);
        g.setColour(button.findColour(juce::TextButton::textColourOnId)
                        .withAlpha(button.isEnabled() ? 1.0f : 0.4f));

        auto bounds = button.getLocalBounds();
        g.drawText(button.getButtonText(), bounds, juce::Justification::centred, false);
    }

    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float /*minPos*/, float /*maxPos*/,
                          juce::Slider::SliderStyle, juce::Slider& slider) override
    {
        auto bounds = juce::Rectangle<float>((float)x, (float)y, (float)width, (float)height);
        float trackH = 4.0f;
        float trackY = bounds.getCentreY() - trackH * 0.5f;

        // Track background
        g.setColour(Colours_::sliderTrack);
        g.fillRoundedRectangle(bounds.getX(), trackY, bounds.getWidth(), trackH, 2.0f);

        // Filled portion
        g.setColour(Colours_::sliderThumb);
        g.fillRoundedRectangle(bounds.getX(), trackY, sliderPos - (float)x, trackH, 2.0f);

        // Thumb
        float thumbSize = 12.0f;
        g.setColour(Colours_::textPrimary);
        g.fillEllipse(sliderPos - thumbSize * 0.5f, bounds.getCentreY() - thumbSize * 0.5f,
                       thumbSize, thumbSize);
    }
};
