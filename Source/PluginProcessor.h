/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "LoopTrack.h"
#include "DebugLogger.h"

//==============================================================================
/**
*/
class SimpleLooperAudioProcessor  : public juce::AudioProcessor
{
public:
    static constexpr int NUM_TRACKS = 6;
    std::atomic<bool> mIsRecording{ false };
    //==============================================================================
    SimpleLooperAudioProcessor();
    ~SimpleLooperAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

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
    
    // Public Accessor for UI - returns vector of pointers
    std::vector<std::unique_ptr<LoopTrack>>& getTracks() { return mTracks; }
    
    // UI Accessors for State
    bool isFirstLoop() const { return mIsFirstLoop.load(); }
    double getBpm() const { return mBpm.load(); }
    int getPrimaryLoopLength() const { return mPrimaryLoopLengthSamples.load(); }
    int getGlobalPlaybackPosition() const { return mGlobalPlaybackPosition; }
    juce::int64 getGlobalTotalSamples() const { return mGlobalTotalSamples.load(); }

    // Commands
    void resetAll();
    void bounceBack();
    void captureAfterLoop(int trackIndex);

    // APVTS for DAW parameter automation / MIDI mapping (Ableton Configure)
    juce::AudioProcessorValueTreeState apvts;

private:
    //==============================================================================
    // --- LOOP TRACKS ---
    
    // Sync State
    std::atomic<bool> mIsFirstLoop { true };
    std::atomic<double> mBpm { 120.0 };
    std::atomic<int> mPrimaryLoopLengthSamples { 0 };
    int mGlobalPlaybackPosition = 0;
    std::atomic<juce::int64> mGlobalTotalSamples { 0 };
    
    void calculateBpm(int lengthSamples, double sampleRate);

    // Must use unique_ptr because LoopTrack contains atomics (non-copyable/non-movable)
    std::vector<std::unique_ptr<LoopTrack>> mTracks;
    
    // Temporary buffer to hold input audio while tracks process and write to output
    juce::AudioBuffer<float> mInputCache;
    // Per-track FX return capture buffers (one per input bus)
    juce::AudioBuffer<float> mFxReturnCache[NUM_TRACKS];

    // --- Parameter system (DAW / MIDI mapping) ---
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void handleParameterChanges();
    void resetAllInternal();
    void performBounceBack();
    void performCaptureAfterLoop(int trackIndex);

    // Cached parameter pointers (valid for APVTS lifetime)
    std::atomic<float>* mParamVol[NUM_TRACKS] = {};
    std::atomic<float>* mParamRecPlay[NUM_TRACKS] = {};
    std::atomic<float>* mParamStop[NUM_TRACKS] = {};
    std::atomic<float>* mParamMute[NUM_TRACKS] = {};
    std::atomic<float>* mParamSolo[NUM_TRACKS] = {};
    std::atomic<float>* mParamAfterLoop[NUM_TRACKS] = {};
    std::atomic<float>* mParamClear[NUM_TRACKS] = {};
    std::atomic<float>* mParamUndo[NUM_TRACKS] = {};
    std::atomic<float>* mParamMul[NUM_TRACKS] = {};
    std::atomic<float>* mParamDiv[NUM_TRACKS] = {};
    std::atomic<float>* mParamOutSelect[NUM_TRACKS] = {};
    std::atomic<float>* mParamResample[NUM_TRACKS] = {};
    std::atomic<float>* mParamBounce = nullptr;
    std::atomic<float>* mParamReset = nullptr;
    std::atomic<float>* mParamMidiSyncChannel = nullptr;

    // Previous param states for edge detection
    bool mPrevRecPlay[NUM_TRACKS] = {};
    bool mPrevStop[NUM_TRACKS] = {};
    bool mPrevAfterLoop[NUM_TRACKS] = {};
    bool mPrevClear[NUM_TRACKS] = {};
    bool mPrevUndo[NUM_TRACKS] = {};
    bool mPrevMul[NUM_TRACKS] = {};
    bool mPrevDiv[NUM_TRACKS] = {};
    bool mPrevResample[NUM_TRACKS] = {};
    bool mPrevBounce = false;
    bool mPrevReset = false;

    // --- Retrospective buffer (After Loop) ---
    juce::AudioBuffer<float> mRetrospectiveBuffer;
    int mRetroWritePos = 0;
    int mRetroBufferSize = 0;

    // Pre-allocated work buffer for bounce/afterloop operations
    juce::AudioBuffer<float> mWorkBuffer;

    // --- Deferred heavy operations (avoid audio thread overload) ---
    static constexpr int CROSSFADE_SAMPLES = 128;
    std::atomic<bool> mPendingBounce { false };
    std::atomic<int>  mPendingAfterLoop { -1 }; // track index, -1 = none
    void executePendingOperations();

    // --- MIDI Clock output (24 PPQN) ---
    double mMidiClockAccumulator = 0.0; // fractional sample position for next tick
    bool mMidiClockRunning = false;
    int mMidiPulseNote = 36; // C1

    // ---------------------------
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SimpleLooperAudioProcessor)
};
