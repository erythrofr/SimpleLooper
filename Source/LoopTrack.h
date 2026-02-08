#pragma once

#include <JuceHeader.h>
#include <atomic>

/**
    Represents a single independent loop track with a state machine and circular buffer.
*/
class LoopTrack
{
public:
    enum class State
    {
        Empty,          // No loop recorded yet
        Recording,      // Recording the initial loop (defines length)
        Playing,        // Playing back the recorded loop
        Overdubbing,    // Playing back + mixing new input into the loop
        Stopped         // Loop exists but is silent
    };

    LoopTrack();
    ~LoopTrack();

    //==============================================================================
    /** PREPARE: Allocates memory and sets sample rate. 
        maxLoopLengthSeconds determines the buffer size. */
    void prepareToPlay(double sampleRate, int samplesPerBlock);

    /** PROCESS: Main audio callback.
        - outputBuffer: The main mix to add our loop audio to.
        - inputBuffer: The incoming audio to record/overdub.
        - globalTotalSamples: The total monotonic sample count since transport start (for global sync).
        - isMasterTrack: If true, this track is defining the master loop length.
        - masterLoopLength: The length of the master loop in samples.
        - anySoloActive: If true, track only plays if it is soloed. */
     void processBlock(juce::AudioBuffer<float>& outputBuffer, const juce::AudioBuffer<float>& inputBuffer,
                      const juce::AudioBuffer<float>& sidechainBuffer,
                      juce::int64 globalTotalSamples, bool isMasterTrack, int masterLoopLength, bool anySoloActive);

    /** RESET: Clears the buffer and state. */
    void clear();

    //==============================================================================
    // State Controls
    void setRecording();
    void setOverdubbing();
    void setPlaying();
    void stop();

    void setVolume(float newVolume) { gain.store(newVolume); }
    void setMuted(bool shouldBeMuted) { isMuted.store(shouldBeMuted); }
    void setSolo(bool shouldBeSolo) { isSolo.store(shouldBeSolo); }
    // FX Replace: one-shot apply of captured sidechain audio
    void applyFxReplace();
    bool isFxCaptureReady() const { return loopLengthSamples > 0 && fxCaptureSamplesWritten >= loopLengthSamples; }
    
    // Configuration
    void setTargetMultiplier(float multiplier) 
    { 
        if (multiplier < 1.0f / 64.0f) multiplier = 1.0f / 64.0f;
        if (multiplier > 64.0f) multiplier = 64.0f;
        targetMultiplier = multiplier; 
    }
    float getTargetMultiplier() const { return targetMultiplier; }

    State getState() const { return currentState.load(); }
    bool hasLoop() const { return loopLengthSamples > 0; }
    int getLoopLengthSamples() const { return loopLengthSamples; }
    bool getSolo() const { return isSolo.load(); }
    bool isMutedState() const { return isMuted.load(); }

    // Buffer access (for bounce back / after loop)
    const juce::AudioBuffer<float>& getLoopBuffer() const { return loopBuffer; }
    int getRecordingStartOffset() const { return recordingStartOffset; }
    juce::int64 getRecordingStartGlobalSample() const { return recordingStartGlobalSample; }
    void setLoopFromMix(const juce::AudioBuffer<float>& mixedBuffer, int length, int startOffset = 0, juce::int64 startGlobalSample = 0);
    void overdubFromBuffer(const juce::AudioBuffer<float>& inputBuffer, int inputLength, juce::int64 inputStartGlobalSample);

    // Features
    void multiplyLoop();
    void divideLoop();
    void performUndo();
    bool canUndo() const { return hasUndo; }

    // Crossfade utility: smooth the loop boundary to avoid clicks
    static void applyCrossfade(juce::AudioBuffer<float>& buffer, int loopLength, int fadeSamples);

    // Progressive buffer replacement: spreads copy over multiple processBlock calls.
    // Playhead region is refreshed first so audio is immediately correct.
    void beginProgressiveReplace(const juce::AudioBuffer<float>* source, int length,
                                 int startOffset = 0, juce::int64 startGlobal = 0);
    void processReplaceChunk(int playheadPos, int blockSize);
    bool isReplacing() const { return mReplace.active; }

private:
    // Progressive replace state
    struct ProgressiveReplace {
        const juce::AudioBuffer<float>* source = nullptr;
        int length = 0;
        int cursor = 0;
        int remaining = 0;
        bool active = false;
    };
    ProgressiveReplace mReplace;

    // Audio Data
    juce::AudioBuffer<float> loopBuffer;
    juce::AudioBuffer<float> undoBuffer; // Buffer for undo state
    juce::AudioBuffer<float> fxCaptureBuffer; // Staging buffer for FX Replace
    int fxCaptureSamplesWritten = 0;
    double trackSampleRate = 44100.0;
    
    // Playback/Recording Logic
    std::atomic<State> currentState { State::Empty };
    std::atomic<float> gain { 1.0f };
    std::atomic<bool> isMuted { false };
    std::atomic<bool> isSolo { false }; // New Solo state

    int playbackPosition = 0;       // Current read/write head position
    int loopLengthSamples = 0;      // Defined after first recording finishes
    
    // SYNC: Position dans le cycle master où l'enregistrement a commencé (pour les slaves)
    int recordingStartOffset = 0;   // Décalage de départ pour la synchronisation
    juce::int64 recordingStartGlobalSample = 0; // Absolute global sample count at recording start
    
    // Undo State
    int undoLoopLengthSamples = 0;
    bool hasUndo = false;
    void saveUndo();

    // Configuration
    float targetMultiplier = 1.0f; // How many bars (relative to master) to record
    
    // Allocating 5 minutes per track by default to avoid reallocation on audio thread
    const int maxLoopLengthSeconds = 300; 
    
    // For fixed length recording
    int recordedSamplesCurrent = 0;

    // Helpers
    void handleRecording(const juce::AudioBuffer<float>& inputBuffer, int numSamples, int startWritePos);
    void handlePlayback(juce::AudioBuffer<float>& outputBuffer, int numSamples, int startReadPos, int loopEndRes, bool shouldBeSilent);
    void handleOverdub(juce::AudioBuffer<float>& outputBuffer, const juce::AudioBuffer<float>& inputBuffer, int numSamples, int startReadPos, int loopEndRes, bool shouldBeSilent);
    void captureSidechain(const juce::AudioBuffer<float>& sidechainBuffer, int numSamples, int startWritePos, int loopEndRes);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LoopTrack)
};
