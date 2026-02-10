/*
  ==============================================================================
    Plugin Editor - Dark theme UI for SimpleLooper
  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

SimpleLooperAudioProcessorEditor::SimpleLooperAudioProcessorEditor (SimpleLooperAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setLookAndFeel(&customLnf);

    auto& tracks = audioProcessor.getTracks();
    int trackIndex = 0;
    for (auto& trackPtr : tracks)
    {
        auto comp = std::make_unique<TrackComponent>(audioProcessor, *trackPtr, trackIndex++);
        addAndMakeVisible(*comp);
        trackComponents.push_back(std::move(comp));
    }

    auto setupGlobalBtn = [&](juce::TextButton& btn, juce::Colour col) {
        addAndMakeVisible(btn);
        btn.setClickingTogglesState(true);
        btn.setColour(juce::TextButton::buttonColourId, col);
        btn.setColour(juce::TextButton::textColourOnId,  Colours_::textPrimary);
        btn.setColour(juce::TextButton::textColourOffId, Colours_::textPrimary);
    };

    setupGlobalBtn(resetButton,  Colours_::rec.darker(0.3f));
    setupGlobalBtn(bounceButton, juce::Colour(0xff7c3aed));

    mResetAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.apvts, "reset_all", resetButton);
    mBounceAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.apvts, "bounce_back", bounceButton);

    addAndMakeVisible(bpmLabel);
    bpmLabel.setText("BPM: --", juce::dontSendNotification);
    bpmLabel.setColour(juce::Label::textColourId, Colours_::textPrimary);
    bpmLabel.setFont(juce::FontOptions(14.0f, juce::Font::bold));

    addAndMakeVisible(stateLabel);
    stateLabel.setText("WAITING", juce::dontSendNotification);
    stateLabel.setColour(juce::Label::textColourId, Colours_::textDim);
    stateLabel.setFont(juce::FontOptions(12.0f));

    addAndMakeVisible(midiSyncLabel);
    midiSyncLabel.setText("MIDI SYNC", juce::dontSendNotification);
    midiSyncLabel.setColour(juce::Label::textColourId, Colours_::textDim);
    midiSyncLabel.setFont(juce::FontOptions(11.0f));

    addAndMakeVisible(midiSyncChannelSelector);
    for (int ch = 1; ch <= 16; ++ch)
        midiSyncChannelSelector.addItem("CH " + juce::String(ch), ch);

    mMidiSyncChannelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.apvts, "midi_sync_channel", midiSyncChannelSelector);

    setSize(720, 680);
    startTimerHz(30);
}

SimpleLooperAudioProcessorEditor::~SimpleLooperAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
    stopTimer();
}

void SimpleLooperAudioProcessorEditor::timerCallback()
{
    bool isFirst = audioProcessor.isFirstLoop();
    double bpm = audioProcessor.getBpm();

    stateLabel.setText(isFirst ? "WAITING FOR FIRST LOOP" : "LOOPING",
                       juce::dontSendNotification);
    stateLabel.setColour(juce::Label::textColourId,
                          isFirst ? Colours_::dub : Colours_::play);

    if (bpm > 0)
        bpmLabel.setText(juce::String(bpm, 1) + " BPM", juce::dontSendNotification);
    else
        bpmLabel.setText("-- BPM", juce::dontSendNotification);

    repaint();
}

void SimpleLooperAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll(Colours_::bg);

    // Title
    auto header = getLocalBounds().removeFromTop(48);
    g.setColour(Colours_::textPrimary);
    g.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    g.drawText("SIMPLE LOOPER", header.reduced(14, 0), juce::Justification::centredLeft);
}

void SimpleLooperAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    // Header bar
    auto header = area.removeFromTop(48);
    auto headerRight = header.removeFromRight(360).reduced(8);
    resetButton.setBounds(headerRight.removeFromRight(70));
    headerRight.removeFromRight(4);
    bounceButton.setBounds(headerRight.removeFromRight(70));
    headerRight.removeFromRight(10);
    midiSyncChannelSelector.setBounds(headerRight.removeFromRight(76));
    headerRight.removeFromRight(6);
    midiSyncLabel.setBounds(headerRight.removeFromRight(70));

    auto headerLeft = header.reduced(14, 0);
    headerLeft.removeFromLeft(180); // skip title space
    bpmLabel.setBounds(headerLeft.removeFromLeft(100));
    stateLabel.setBounds(headerLeft.removeFromLeft(200));

    area.removeFromTop(4);

    if (trackComponents.empty()) return;

    int trackH = (area.getHeight() - 8) / static_cast<int>(trackComponents.size());
    auto tracksArea = area.reduced(8, 0);

    for (auto& comp : trackComponents)
    {
        comp->setBounds(tracksArea.removeFromTop(trackH).reduced(0, 2));
    }
}
