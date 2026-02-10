#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "TrackComponent.h"
#include "CustomLookAndFeel.h"

class SimpleLooperAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                            public juce::Timer
{
public:
    SimpleLooperAudioProcessorEditor (SimpleLooperAudioProcessor&);
    ~SimpleLooperAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    SimpleLooperAudioProcessor& audioProcessor;
    CustomLookAndFeel customLnf;

    std::vector<std::unique_ptr<TrackComponent>> trackComponents;

    juce::TextButton resetButton  { "RESET" };
    juce::TextButton bounceButton { "BOUNCE" };

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> mResetAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> mBounceAttachment;

    juce::Label bpmLabel;
    juce::Label stateLabel;
    juce::Label midiOutLabel;
    juce::ComboBox midiOutSelector;
    juce::TextButton refreshMidiOutButton { "↻" };

    void refreshMidiOutputList();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SimpleLooperAudioProcessorEditor)
};
