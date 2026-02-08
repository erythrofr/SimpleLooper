#include "TrackComponent.h"
#include "PluginProcessor.h"
#include "CustomLookAndFeel.h"
#include "DebugLogger.h"

TrackComponent::TrackComponent(SimpleLooperAudioProcessor& p, LoopTrack& trackToControl, int trackIndex)
    : processor(p), track(trackToControl), trackID(trackIndex)
{
    auto setupButton = [&](juce::TextButton& btn) {
        addAndMakeVisible(btn);
        btn.setClickingTogglesState(true);
        btn.setColour(juce::TextButton::buttonColourId, Colours_::idle);
        btn.setColour(juce::TextButton::textColourOnId,  Colours_::textPrimary);
        btn.setColour(juce::TextButton::textColourOffId, Colours_::textPrimary);
    };
    setupButton(recPlayButton); setupButton(stopButton); setupButton(undoButton);
    setupButton(divButton); setupButton(mulButton); setupButton(afterLoopButton);
    setupButton(clearButton); setupButton(fxReplaceButton);
    setupButton(muteButton); setupButton(soloButton);

    addAndMakeVisible(volumeSlider);
    volumeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    volumeSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);

    addAndMakeVisible(mOutputSelector);
    mOutputSelector.addItem("Monitor 1/2", 1);
    mOutputSelector.addItem("Output 3/4", 2);
    mOutputSelector.addItem("Output 5/6", 3);
    mOutputSelector.addItem("Output 7/8", 4);
    mOutputSelector.addItem("Output 9/10", 5);
    mOutputSelector.addItem("Output 11/12", 6);
    mOutputSelector.addItem("Output 13/14", 7);

    auto idx = juce::String(trackIndex);
    mVolAttachment       = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts, "vol_" + idx, volumeSlider);
    mRecAttachment       = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts, "rec_" + idx, recPlayButton);
    mStopAttachment      = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts, "stop_" + idx, stopButton);
    mClearAttachment     = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts, "clear_" + idx, clearButton);
    mMuteAttachment      = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts, "mute_" + idx, muteButton);
    mSoloAttachment      = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts, "solo_" + idx, soloButton);
    mUndoAttachment      = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts, "undo_" + idx, undoButton);
    mMulAttachment       = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts, "mul_" + idx, mulButton);
    mDivAttachment       = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts, "div_" + idx, divButton);
    mAfterLoopAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts, "afterloop_" + idx, afterLoopButton);
    mOutSelectAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.apvts, "out_select_" + idx, mOutputSelector);
    mResampleAttachment  = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts, "resample_" + idx, fxReplaceButton);
    startTimerHz(30);
}

TrackComponent::~TrackComponent() { stopTimer(); }

void TrackComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour(Colours_::surface);
    g.fillRoundedRectangle(bounds, 8.0f);
    g.setColour(Colours_::border);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 8.0f, 1.0f);

    auto area = getLocalBounds().reduced(10);
    auto badge = area.removeFromLeft(28).removeFromTop(22);
    g.setColour(Colours_::surfaceLight);
    g.fillRoundedRectangle(badge.toFloat(), 4.0f);
    g.setColour(Colours_::textPrimary);
    g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    g.drawText(juce::String(trackID + 1), badge, juce::Justification::centred);
    area.removeFromLeft(8);

    auto visArea = area.removeFromTop(28);
    int loopLength = track.getLoopLengthSamples();
    double bpm = processor.getBpm();

    if (loopLength > 0 && bpm > 10.0)
    {
        double sr = processor.getSampleRate();
        if (sr > 0)
        {
            double spb = (60.0 / bpm) * sr;
            int numBeats = juce::jmax(1, (int)std::round((double)loopLength / spb));
            int dBeats = juce::jmin(numBeats, 128);
            float bW = (float)visArea.getWidth() / (float)dBeats;
            float gp = (dBeats > 32) ? 0.5f : 1.5f;
            juce::int64 el = processor.getGlobalTotalSamples() - track.getRecordingStartGlobalSample();
            if (el < 0) el = 0;
            int active = (int)((double)(el % loopLength) / spb);
            for (int i = 0; i < dBeats; ++i)
            {
                juce::Rectangle<float> bl(visArea.getX() + i * bW, (float)visArea.getY(), bW - gp, (float)visArea.getHeight());
                if (track.isMutedState())
                    g.setColour(Colours_::beatMuted);
                else if (i == active && track.getState() == LoopTrack::State::Playing)
                    g.setColour(Colours_::beatActive);
                else
                    g.setColour(Colours_::beatIdle.brighter(0.15f));
                g.fillRoundedRectangle(bl, 2.0f);
            }
        }
    }
    else if (track.getState() == LoopTrack::State::Recording)
    {
        g.setColour(Colours_::rec.withAlpha(0.7f));
        g.fillRoundedRectangle(visArea.toFloat(), 4.0f);
        g.setColour(Colours_::textPrimary);
        g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        g.drawText("RECORDING...", visArea, juce::Justification::centred);
    }
    else
    {
        double sr = processor.getSampleRate();
        int ml = processor.getPrimaryLoopLength();
        float mult = track.getTargetMultiplier();
        int pBeats = 4;
        if (bpm > 10.0 && sr > 0 && ml > 0)
        {
            double spb = (60.0 / bpm) * sr;
            int mb = juce::jmax(1, (int)std::round((double)ml / spb));
            pBeats = juce::jmax(1, (int)std::round((float)mb * mult));
        }
        else
            pBeats = juce::jmax(1, (int)std::round(4.0f * mult));

        int dB = juce::jmin(pBeats, 128);
        float bW = (float)visArea.getWidth() / (float)dB;
        float gp = (dB > 32) ? 0.5f : 1.5f;
        g.setColour(Colours_::beatIdle.withAlpha(0.5f));
        for (int i = 0; i < dB; ++i)
        {
            juce::Rectangle<float> bl(visArea.getX() + i * bW, (float)visArea.getY(), bW - gp, (float)visArea.getHeight());
            g.fillRoundedRectangle(bl, 2.0f);
        }
        juce::String mt = (mult < 1.0f) ? "1/" + juce::String(juce::roundToInt(1.0f / mult))
                                         : "x" + juce::String(juce::roundToInt(mult));
        g.setColour(Colours_::textDim);
        g.setFont(juce::FontOptions(10.0f));
        g.drawText(mt + "  " + juce::String(pBeats) + " beats", visArea, juce::Justification::centred);
    }
}

void TrackComponent::resized()
{
    auto area = getLocalBounds().reduced(10);
    area.removeFromLeft(36);
    area.removeFromTop(32);
    int bh = 26, gp = 3;

    auto r1 = area.removeFromTop(bh);
    int n = 8, bw = (r1.getWidth() - (n - 1) * gp) / n;
    recPlayButton.setBounds(r1.removeFromLeft(bw));   r1.removeFromLeft(gp);
    stopButton.setBounds(r1.removeFromLeft(bw));      r1.removeFromLeft(gp);
    undoButton.setBounds(r1.removeFromLeft(bw));      r1.removeFromLeft(gp);
    divButton.setBounds(r1.removeFromLeft(bw));       r1.removeFromLeft(gp);
    mulButton.setBounds(r1.removeFromLeft(bw));       r1.removeFromLeft(gp);
    afterLoopButton.setBounds(r1.removeFromLeft(bw)); r1.removeFromLeft(gp);
    clearButton.setBounds(r1.removeFromLeft(bw));     r1.removeFromLeft(gp);
    fxReplaceButton.setBounds(r1);

    area.removeFromTop(4);
    auto r2 = area.removeFromTop(bh);
    muteButton.setBounds(r2.removeFromLeft(30));  r2.removeFromLeft(gp);
    soloButton.setBounds(r2.removeFromLeft(30));  r2.removeFromLeft(6);
    mOutputSelector.setBounds(r2.removeFromRight(110)); r2.removeFromRight(6);
    volumeSlider.setBounds(r2);
}

void TrackComponent::updateButtonVisuals()
{
    auto state = track.getState();
    juce::Colour rc = Colours_::idle;
    juce::String rt = "REC";
    switch (state)
    {
        case LoopTrack::State::Empty:       rc = Colours_::idle; rt = "REC";  break;
        case LoopTrack::State::Recording:   rc = Colours_::rec;  rt = "REC";  break;
        case LoopTrack::State::Playing:     rc = Colours_::play; rt = "PLAY"; break;
        case LoopTrack::State::Overdubbing: rc = Colours_::dub;  rt = "DUB";  break;
        case LoopTrack::State::Stopped:     rc = Colours_::stop; rt = "PLAY"; break;
    }
    recPlayButton.setColour(juce::TextButton::buttonColourId, rc);
    recPlayButton.setButtonText(rt);
    muteButton.setColour(juce::TextButton::buttonColourId, track.isMutedState() ? Colours_::mute : Colours_::idle);
    soloButton.setColour(juce::TextButton::buttonColourId, track.getSolo() ? Colours_::solo : Colours_::idle);
    soloButton.setColour(juce::TextButton::textColourOnId, track.getSolo() ? Colours_::bg : Colours_::textPrimary);
    undoButton.setEnabled(track.canUndo());
    undoButton.setColour(juce::TextButton::buttonColourId, track.canUndo() ? Colours_::undo.brighter(0.2f) : Colours_::idle);

    float mult = track.getTargetMultiplier();
    juce::String mt = (mult < 1.0f) ? "1/" + juce::String(juce::roundToInt(1.0f / mult))
                                     : "x" + juce::String(juce::roundToInt(mult));
    mulButton.setButtonText("X2 " + mt);
    divButton.setButtonText("/2 " + mt);
    bool cm = (mult < 63.9f), cd = (mult > 1.0f / 63.9f);
    mulButton.setEnabled(cm);
    divButton.setEnabled(cd);
    mulButton.setColour(juce::TextButton::buttonColourId, cm ? Colours_::divMul : Colours_::idle);
    divButton.setColour(juce::TextButton::buttonColourId, cd ? Colours_::divMul : Colours_::idle);

    bool ca = (processor.getPrimaryLoopLength() > 0);
    afterLoopButton.setEnabled(ca);
    afterLoopButton.setColour(juce::TextButton::buttonColourId, ca ? Colours_::afterloop : Colours_::idle);
    clearButton.setColour(juce::TextButton::buttonColourId, Colours_::clear);
    bool fx = track.isFxCaptureReady();
    fxReplaceButton.setEnabled(fx);
    fxReplaceButton.setColour(juce::TextButton::buttonColourId, fx ? Colours_::fxReady : Colours_::idle);
    stopButton.setColour(juce::TextButton::buttonColourId, Colours_::idle);
}

void TrackComponent::timerCallback() { updateButtonVisuals(); }
