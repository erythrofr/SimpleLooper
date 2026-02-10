/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
SimpleLooperAudioProcessor::SimpleLooperAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",        juce::AudioChannelSet::stereo(), true)
                       .withInput  ("FX Return 1",  juce::AudioChannelSet::stereo(), false)
                       .withInput  ("FX Return 2",  juce::AudioChannelSet::stereo(), false)
                       .withInput  ("FX Return 3",  juce::AudioChannelSet::stereo(), false)
                       .withInput  ("FX Return 4",  juce::AudioChannelSet::stereo(), false)
                       .withInput  ("FX Return 5",  juce::AudioChannelSet::stereo(), false)
                       .withInput  ("FX Return 6",  juce::AudioChannelSet::stereo(), false)
                      #endif
                       .withOutput ("Monitor 1/2",   juce::AudioChannelSet::stereo(), true)
                       .withOutput ("Output 3/4",    juce::AudioChannelSet::stereo(), false)
                       .withOutput ("Output 5/6",    juce::AudioChannelSet::stereo(), false)
                       .withOutput ("Output 7/8",    juce::AudioChannelSet::stereo(), false)
                       .withOutput ("Output 9/10",   juce::AudioChannelSet::stereo(), false)
                       .withOutput ("Output 11/12",  juce::AudioChannelSet::stereo(), false)
                       .withOutput ("Output 13/14",  juce::AudioChannelSet::stereo(), false)
                     #endif
                       ),
       apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
#else
     : apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
#endif
{
    // Initialiser le logger EN PREMIER
    DebugLogger::getInstance().initialize();
    
    LOG_SEP("PLUGIN CONSTRUCTOR");
    
    // Initialize tracks immediately so they exist for the Editor
    int numTracks = NUM_TRACKS;
    for (int i = 0; i < numTracks; ++i)
    {
        mTracks.push_back(std::make_unique<LoopTrack>());
        LOG("Track " + juce::String(i) + " created");
    }

    // Cache APVTS parameter pointers for real-time access in processBlock
    for (int i = 0; i < NUM_TRACKS; ++i)
    {
        auto idx = juce::String(i);
        mParamVol[i]       = apvts.getRawParameterValue("vol_" + idx);
        mParamRecPlay[i]   = apvts.getRawParameterValue("rec_" + idx);
        mParamStop[i]      = apvts.getRawParameterValue("stop_" + idx);
        mParamMute[i]      = apvts.getRawParameterValue("mute_" + idx);
        mParamSolo[i]      = apvts.getRawParameterValue("solo_" + idx);
        mParamAfterLoop[i] = apvts.getRawParameterValue("afterloop_" + idx);
        mParamClear[i]     = apvts.getRawParameterValue("clear_" + idx);
        mParamUndo[i]      = apvts.getRawParameterValue("undo_" + idx);
        mParamMul[i]       = apvts.getRawParameterValue("mul_" + idx);
        mParamDiv[i]       = apvts.getRawParameterValue("div_" + idx);
        mParamOutSelect[i] = apvts.getRawParameterValue("out_select_" + idx);
        mParamResample[i]  = apvts.getRawParameterValue("resample_" + idx);
    }
    mParamBounce          = apvts.getRawParameterValue("bounce_back");
    mParamReset           = apvts.getRawParameterValue("reset_all");
}

SimpleLooperAudioProcessor::~SimpleLooperAudioProcessor()
{
    const juce::ScopedLock sl(mDirectMidiOutputLock);
    mDirectMidiOutput.reset();
}

juce::StringArray SimpleLooperAudioProcessor::getAvailableMidiOutputNames() const
{
    juce::StringArray names;
    for (const auto& d : juce::MidiOutput::getAvailableDevices())
        names.add(d.name);
    return names;
}

juce::String SimpleLooperAudioProcessor::getSelectedMidiOutputName() const
{
    const juce::ScopedLock sl(mDirectMidiOutputLock);
    return mSelectedMidiOutputName;
}

void SimpleLooperAudioProcessor::setSelectedMidiOutputName(const juce::String& deviceName)
{
    auto devices = juce::MidiOutput::getAvailableDevices();

    juce::String identifierToOpen;
    juce::String normalizedName = deviceName.trim();
    if (!normalizedName.isEmpty() && normalizedName != "Host MIDI Output")
    {
        for (const auto& d : devices)
        {
            if (d.name == normalizedName)
            {
                identifierToOpen = d.identifier;
                break;
            }
        }
    }

    std::unique_ptr<juce::MidiOutput> newOutput;
    if (!identifierToOpen.isEmpty())
        newOutput = juce::MidiOutput::openDevice(identifierToOpen);

    const juce::ScopedLock sl(mDirectMidiOutputLock);
    mDirectMidiOutput = std::move(newOutput);
    mSelectedMidiOutputName = normalizedName;
}

//==============================================================================
const juce::String SimpleLooperAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool SimpleLooperAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool SimpleLooperAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool SimpleLooperAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double SimpleLooperAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int SimpleLooperAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int SimpleLooperAudioProcessor::getCurrentProgram()
{
    return 0;
}

void SimpleLooperAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String SimpleLooperAudioProcessor::getProgramName (int index)
{
    return {};
}

void SimpleLooperAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void SimpleLooperAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    LOG_SEP("PREPARE TO PLAY");
    LOG_VALUE("Sample Rate", sampleRate);
    LOG_VALUE("Samples Per Block", samplesPerBlock);
    
    // --- INITIALIZATION ---

    // 1. Prepare auxiliary input buffer
    // We need at least the number of input channels and the block size
    int numInputChannels = getTotalNumInputChannels();
    // Safety check for 0 channels (rare but possible)
    if (numInputChannels == 0) numInputChannels = 2; 

    mInputCache.setSize(numInputChannels, samplesPerBlock);
    for (int i = 0; i < NUM_TRACKS; ++i)
    {
        mFxReturnCache[i].setSize(2, samplesPerBlock);
        mFxReturnCache[i].clear();
    }

    // 2. Setup Loop Tracks
    // Tracks are already created in constructor. Just prepare them.
    for (int i = 0; i < mTracks.size(); ++i)
    {
        LOG_TRACK(i, "PREPARE", "");
        mTracks[i]->prepareToPlay(sampleRate, samplesPerBlock);
    }

    // 3. Setup Retrospective Buffer (After Loop) - 5 minutes circular buffer
    int retroSize = static_cast<int>(sampleRate * 300.0);
    mRetrospectiveBuffer.setSize(2, retroSize);
    mRetrospectiveBuffer.clear();
    mRetroWritePos = 0;
    mRetroBufferSize = retroSize;

    // 4. Pre-allocate work buffer for bounce/afterloop operations
    mWorkBuffer.setSize(2, retroSize);
    mWorkBuffer.clear();
    
    LOG("Preparation complete");
}

void SimpleLooperAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool SimpleLooperAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // Main output must be stereo
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // Input must be stereo
   #if ! JucePlugin_IsSynth
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
   #endif

    // Additional input buses (sidechain): must be stereo or disabled
    for (int i = 1; i < layouts.inputBuses.size(); ++i)
    {
        if (!layouts.inputBuses[i].isDisabled()
            && layouts.inputBuses[i] != juce::AudioChannelSet::stereo())
            return false;
    }

    // Additional output buses: must be stereo or disabled
    for (int i = 1; i < layouts.outputBuses.size(); ++i)
    {
        if (!layouts.outputBuses[i].isDisabled()
            && layouts.outputBuses[i] != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
  #endif
}
#endif

void SimpleLooperAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // --- SNAPSHOT ALL INPUTS before touching the buffer ---

    // Ensure our input cache is big enough (in case block size changes or wasn't set right)
    if (mInputCache.getNumSamples() < buffer.getNumSamples())
        mInputCache.setSize(juce::jmax(1, totalNumInputChannels), buffer.getNumSamples());

    // Snapshot main input (Bus 0)
    auto mainInputBuf = getBusBuffer(buffer, true, 0);
    for (int ch = 0; ch < mainInputBuf.getNumChannels(); ++ch)
        mInputCache.copyFrom(ch, 0, mainInputBuf, ch, 0, buffer.getNumSamples());

    // Snapshot per-track FX Return inputs (Buses 1-8) if enabled
    for (int t = 0; t < NUM_TRACKS; ++t)
    {
        if (mFxReturnCache[t].getNumSamples() < buffer.getNumSamples())
            mFxReturnCache[t].setSize(2, buffer.getNumSamples());
        mFxReturnCache[t].clear(0, buffer.getNumSamples());

        int fxBusIdx = t + 1; // Bus 0 = main input, Buses 1-4 = FX Returns
        if (fxBusIdx < getBusCount(true) && getBus(true, fxBusIdx)->isEnabled())
        {
            auto fxBuf = getBusBuffer(buffer, true, fxBusIdx);
            for (int ch = 0; ch < juce::jmin(2, fxBuf.getNumChannels()); ++ch)
                mFxReturnCache[t].copyFrom(ch, 0, fxBuf, ch, 0, buffer.getNumSamples());
        }
    }

    // --- CLEAR ALL output buses to prevent FX Return bleed ---
    // Input and output buses share channels in the buffer (e.g. FX Return 1 and
    // Output 3/4 both map to channels 2-3). Without clearing, FX Return audio
    // passes straight through to the output, causing feedback.
    for (int bus = 0; bus < getBusCount(false); ++bus)
    {
        if (getBus(false, bus)->isEnabled())
        {
            auto outBuf = getBusBuffer(buffer, false, bus);
            outBuf.clear(0, buffer.getNumSamples());
        }
    }

    // Re-add main input to Main Output for input monitoring
    {
        auto mainOutBuf = getBusBuffer(buffer, false, 0);
        for (int ch = 0; ch < juce::jmin(mInputCache.getNumChannels(), mainOutBuf.getNumChannels()); ++ch)
            mainOutBuf.copyFrom(ch, 0, mInputCache, ch, 0, buffer.getNumSamples());
    }

    // 2. Record input into retrospective buffer (circular) for After Loop feature
    if (mRetroBufferSize > 0)
    {
        int numSamples = buffer.getNumSamples();
        int retroCh = juce::jmin(mInputCache.getNumChannels(), mRetrospectiveBuffer.getNumChannels());
        for (int ch = 0; ch < retroCh; ++ch)
        {
            int toEnd = mRetroBufferSize - mRetroWritePos;
            if (numSamples <= toEnd)
            {
                mRetrospectiveBuffer.copyFrom(ch, mRetroWritePos, mInputCache, ch, 0, numSamples);
            }
            else
            {
                mRetrospectiveBuffer.copyFrom(ch, mRetroWritePos, mInputCache, ch, 0, toEnd);
                mRetrospectiveBuffer.copyFrom(ch, 0, mInputCache, ch, toEnd, numSamples - toEnd);
            }
        }
        mRetroWritePos = (mRetroWritePos + numSamples) % mRetroBufferSize;
    }

    // 3. Handle DAW parameter triggers (MIDI mapped via Ableton Configure, etc.)
    handleParameterChanges();

    // 4. Track Control Logic
    // Access via pointer
    if (!mTracks.empty())
    {
        auto* track1 = mTracks[0].get(); 
        LoopTrack::State state = track1->getState();

        // NOTE: We removed the automatic mIsRecording override logic here 
        // because it was conflicting with the UI buttons (TrackComponent), 
        // causing the loop to close immediately (buzzing).
        // The UI now manages the state directly.
        
        // --- FIRST LOOP LOGIC ---

        // Detect if the First Loop just finished recording
        if (mIsFirstLoop.load())
        {
            // If track 1 has transitioned to Playing and has a valid length
            if (track1->getState() == LoopTrack::State::Playing && track1->getLoopLengthSamples() > 0)
            {
                 // Capture the primary loop details
                 int len = track1->getLoopLengthSamples();
                 mPrimaryLoopLengthSamples.store(len);
                 calculateBpm(len, getSampleRate());
                 
                 LOG_SEP("FIRST LOOP COMPLETED");
                 LOG_VALUE("Master Loop Length", len);
                 LOG_VALUE("BPM", mBpm.load());
                 LOG_VALUE("Global Position", mGlobalPlaybackPosition);
                 
                 // Switch mode
                 mIsFirstLoop.store(false);
                 mGlobalPlaybackPosition = 0; 
            }
        }
    }

    // 3. Process All Tracks
    int masterLength = mPrimaryLoopLengthSamples.load();
    bool isFirstLoopPhase = mIsFirstLoop.load();
    juce::int64 currentGlobalTotal = mGlobalTotalSamples.load();

    // Check for Global Solo
    bool anySolo = false;
    for (auto& t : mTracks)
    {
        if (t->getSolo())
        {
            anySolo = true;
            break;
        }
    }

    for (size_t i = 0; i < mTracks.size(); ++i)
    {
        bool isMaster = (i == 0);
        
        // If Master Track changes status (e.g. multiplied/divided), we need to update mPrimaryLoopLengthSamples
        if (isMaster && masterLength > 0 && !isFirstLoopPhase)
        {
             // Check if loop length changed
             int currentLen = mTracks[i]->getLoopLengthSamples();
             if (currentLen > 0 && currentLen != masterLength)
             {
                 // Update global length
                 masterLength = currentLen;
                 mPrimaryLoopLengthSamples.store(masterLength);
             }
        }
        
        // Determine output bus for this track from APVTS parameter
        int outChoice = (i < NUM_TRACKS) ? juce::roundToInt(mParamOutSelect[i]->load()) : 0;
        int targetBus = outChoice;
        
        // Safety: fallback to main output if selected bus is out of range or disabled
        if (targetBus < 0 || targetBus >= getBusCount(false)
            || !getBus(false, targetBus)->isEnabled())
            targetBus = 0;
        
        auto busBuffer = getBusBuffer(buffer, false, targetBus);
        
        auto& fxCache = (i < NUM_TRACKS) ? mFxReturnCache[i] : mFxReturnCache[0];
        mTracks[i]->processBlock(busBuffer, mInputCache, fxCache, currentGlobalTotal, isMaster, masterLength, anySolo);
    }
    
    // 4. Update Global Transport (Playback & Synchronization)
    if (!isFirstLoopPhase && masterLength > 0)
    {
        mGlobalPlaybackPosition += buffer.getNumSamples();
        mGlobalPlaybackPosition %= masterLength;
        
        mGlobalTotalSamples.store(currentGlobalTotal + buffer.getNumSamples());
    }

    // 5. Execute deferred heavy operations (bounce, afterloop)
    executePendingOperations();

    // 6. Output MIDI Clock (24 PPQN) based on detected BPM
    auto sendToSelectedDirectPort = [this](const juce::MidiMessage& msg)
    {
        const juce::ScopedLock sl(mDirectMidiOutputLock);
        if (mDirectMidiOutput != nullptr)
            mDirectMidiOutput->sendMessageNow(msg);
    };

    double bpm = mBpm.load();
    if (bpm > 10.0 && masterLength > 0 && !isFirstLoopPhase)
    {
        if (!mMidiClockRunning)
        {
            // Send MIDI Start
            auto startMsg = juce::MidiMessage(0xFA);
            midiMessages.addEvent(startMsg, 0);
            sendToSelectedDirectPort(startMsg);
            mMidiClockRunning = true;
            mMidiClockAccumulator = 0.0;
        }

        double samplesPerTick = (getSampleRate() * 60.0) / (bpm * 24.0);
        int numSamples = buffer.getNumSamples();

        while (mMidiClockAccumulator < (double)numSamples)
        {
            int tickPos = static_cast<int>(mMidiClockAccumulator);
            if (tickPos >= numSamples) break;

            auto tickMsg = juce::MidiMessage(0xF8);
            midiMessages.addEvent(tickMsg, tickPos);
            sendToSelectedDirectPort(tickMsg);

            mMidiClockAccumulator += samplesPerTick;
        }
        mMidiClockAccumulator -= (double)numSamples;
    }
    else if (mMidiClockRunning)
    {
        // Send MIDI Stop
        auto stopMsg = juce::MidiMessage(0xFC);
        midiMessages.addEvent(stopMsg, 0);
        sendToSelectedDirectPort(stopMsg);
        mMidiClockRunning = false;
        mMidiClockAccumulator = 0.0;
    }
}

//==============================================================================
bool SimpleLooperAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* SimpleLooperAudioProcessor::createEditor()
{
    return new SimpleLooperAudioProcessorEditor (*this);
}

//==============================================================================
void SimpleLooperAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
}

void SimpleLooperAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
}

void SimpleLooperAudioProcessor::calculateBpm(int lengthSamples, double sampleRate)
{
    if (lengthSamples <= 0 || sampleRate <= 0) return;
    
    double durationSecs = (double)lengthSamples / sampleRate;
    
    // Calculate raw BPM based on the assumption that the loop is 1 Beat
    double rawBpm = 60.0 / durationSecs; 
    
    // Sanity Check & Adjustment (Target 70-140 BPM)
    // If BPM is too low (long loop), assume it contains multiple beats/bars
    while (rawBpm < 70.0)
    {
        rawBpm *= 2.0;
    }
    // If BPM is too high (short loop), assume it's a fraction of a beat (though less common for loopers)
    while (rawBpm > 140.0)
    {
        rawBpm /= 2.0;
    }
    
    mBpm.store(rawBpm);
    
    // Log finding (debug)
    // DBG("Calculated BPM: " << rawBpm);
}

void SimpleLooperAudioProcessor::resetAll()
{
    LOG_SEP("RESET ALL");
    suspendProcessing(true);
    resetAllInternal();
    LOG("Reset complete");
    LOG_VALUE("IsFirstLoop", true);
    LOG_VALUE("PrimaryLoopLength", 0);
    LOG_VALUE("GlobalPosition", 0);
    suspendProcessing(false);
}

void SimpleLooperAudioProcessor::resetAllInternal()
{
    for (int i = 0; i < mTracks.size(); ++i)
    {
        LOG_TRACK(i, "RESET", "");
        mTracks[i]->clear();
    }
        
    mIsFirstLoop.store(true);
    mPrimaryLoopLengthSamples.store(0);
    mGlobalPlaybackPosition = 0;
    mGlobalTotalSamples.store(0);
    mBpm.store(0.0);
}

//==============================================================================
// Parameter Layout for DAW integration (Ableton Configure, MIDI mapping)

juce::AudioProcessorValueTreeState::ParameterLayout SimpleLooperAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (int i = 0; i < NUM_TRACKS; ++i)
    {
        auto idx = juce::String(i);
        auto name = "Track " + juce::String(i + 1);

        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID("vol_" + idx, 1), name + " Volume",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.8f));
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID("rec_" + idx, 1), name + " Rec/Play", false));
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID("mute_" + idx, 1), name + " Mute", false));
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID("stop_" + idx, 1), name + " Stop", false));
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID("solo_" + idx, 1), name + " Solo", false));
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID("afterloop_" + idx, 1), name + " After Loop", false));
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID("clear_" + idx, 1), name + " Clear", false));
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID("undo_" + idx, 1), name + " Undo", false));
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID("mul_" + idx, 1), name + " Multiply", false));
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID("div_" + idx, 1), name + " Divide", false));
        layout.add(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID("out_select_" + idx, 1), name + " Output",
            juce::StringArray{"Monitor 1/2", "Output 3/4", "Output 5/6", "Output 7/8",
                              "Output 9/10", "Output 11/12", "Output 13/14"}, i + 1));
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID("resample_" + idx, 1), name + " FX Replace", false));
    }

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("bounce_back", 1), "Bounce Back", false));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("reset_all", 1), "Reset All", false));

    return layout;
}

void SimpleLooperAudioProcessor::handleParameterChanges()
{
    for (int i = 0; i < (int)mTracks.size() && i < NUM_TRACKS; ++i)
    {
        // Volume (continuous, driven by SliderAttachment)
        float volVal = mParamVol[i]->load();
        mTracks[i]->setVolume(volVal);

        // Mute (direct sync, driven by ButtonAttachment toggle)
        bool muVal = mParamMute[i]->load() >= 0.5f;
        mTracks[i]->setMuted(muVal);

        // Solo (direct sync, driven by ButtonAttachment toggle)
        bool soVal = mParamSolo[i]->load() >= 0.5f;
        mTracks[i]->setSolo(soVal);

        // FX Replace trigger (any edge)
        bool rsmpVal = mParamResample[i]->load() >= 0.5f;
        if (rsmpVal != mPrevResample[i])
            mTracks[i]->applyFxReplace();
        mPrevResample[i] = rsmpVal;

        // Rec/Play trigger (any edge = state cycle, compatible with ButtonAttachment toggle)
        bool rpVal = mParamRecPlay[i]->load() >= 0.5f;
        if (rpVal != mPrevRecPlay[i])
        {
            auto state = mTracks[i]->getState();
            switch (state)
            {
                case LoopTrack::State::Empty:       mTracks[i]->setRecording(); break;
                case LoopTrack::State::Recording:   mTracks[i]->setPlaying(); break;
                case LoopTrack::State::Playing:     mTracks[i]->setOverdubbing(); break;
                case LoopTrack::State::Overdubbing:  mTracks[i]->setPlaying(); break;
                case LoopTrack::State::Stopped:     mTracks[i]->setPlaying(); break;
            }
        }
        mPrevRecPlay[i] = rpVal;

        // Stop trigger (any edge)
        bool stVal = mParamStop[i]->load() >= 0.5f;
        if (stVal != mPrevStop[i])
            mTracks[i]->stop();
        mPrevStop[i] = stVal;

        // After Loop trigger (any edge)
        bool alVal = mParamAfterLoop[i]->load() >= 0.5f;
        if (alVal != mPrevAfterLoop[i])
            mPendingAfterLoop.store(i);
        mPrevAfterLoop[i] = alVal;

        // Clear trigger (any edge)
        bool clVal = mParamClear[i]->load() >= 0.5f;
        if (clVal != mPrevClear[i])
            mTracks[i]->clear();
        mPrevClear[i] = clVal;

        // Undo trigger (any edge)
        bool udVal = mParamUndo[i]->load() >= 0.5f;
        if (udVal != mPrevUndo[i])
            mTracks[i]->performUndo();
        mPrevUndo[i] = udVal;

        // Multiply trigger (any edge)
        bool mlVal = mParamMul[i]->load() >= 0.5f;
        if (mlVal != mPrevMul[i])
        {
            if (mTracks[i]->getState() == LoopTrack::State::Empty)
            {
                float m = mTracks[i]->getTargetMultiplier() * 2.0f;
                if (m > 64.0f) m = 64.0f;
                mTracks[i]->setTargetMultiplier(m);
            }
            else
            {
                mTracks[i]->multiplyLoop();
            }
        }
        mPrevMul[i] = mlVal;

        // Divide trigger (any edge)
        bool dvVal = mParamDiv[i]->load() >= 0.5f;
        if (dvVal != mPrevDiv[i])
        {
            if (mTracks[i]->getState() == LoopTrack::State::Empty)
            {
                float m = mTracks[i]->getTargetMultiplier() / 2.0f;
                if (m < 1.0f / 64.0f) m = 1.0f / 64.0f;
                mTracks[i]->setTargetMultiplier(m);
            }
            else
            {
                mTracks[i]->divideLoop();
            }
        }
        mPrevDiv[i] = dvVal;
    }

    // Bounce Back trigger (any edge)
    bool bnVal = mParamBounce->load() >= 0.5f;
    if (bnVal != mPrevBounce)
        mPendingBounce.store(true);
    mPrevBounce = bnVal;

    // Reset All trigger (any edge)
    bool rsVal = mParamReset->load() >= 0.5f;
    if (rsVal != mPrevReset)
        resetAllInternal();
    mPrevReset = rsVal;
}

//==============================================================================
// Deferred operations - run inside processBlock after tracks have been processed

void SimpleLooperAudioProcessor::executePendingOperations()
{
    int pendingAL = mPendingAfterLoop.load();
    bool pendingBn = mPendingBounce.load();

    if (!pendingBn && pendingAL < 0)
        return;

    // No suspendProcessing: bounce uses progressive replace,
    // afterloop copies are small enough to run in one block.

    if (pendingBn)
    {
        mPendingBounce.store(false);
        performBounceBack();
    }

    if (pendingAL >= 0)
    {
        mPendingAfterLoop.store(-1);
        performCaptureAfterLoop(pendingAL);
    }
}

//==============================================================================
// Bounce Back: mix all tracks into track 1, clear others

void SimpleLooperAudioProcessor::bounceBack()
{
    LOG_SEP("BOUNCE BACK");
    performBounceBack();
}

void SimpleLooperAudioProcessor::performBounceBack()
{
    int masterLen = mPrimaryLoopLengthSamples.load();
    if (masterLen <= 0) return;

    // Find the longest loop across all tracks
    int bounceLen = masterLen;
    for (auto& t : mTracks)
    {
        if (t->hasLoop())
            bounceLen = juce::jmax(bounceLen, t->getLoopLengthSamples());
    }

    if (bounceLen > mWorkBuffer.getNumSamples()) return;

    mWorkBuffer.clear(0, bounceLen);

    // Mix all tracks into work buffer using block-based operations
    for (auto& t : mTracks)
    {
        if (!t->hasLoop()) continue;

        auto& lb = t->getLoopBuffer();
        int trackLen = t->getLoopLengthSamples();
        juce::int64 startGlobal = t->getRecordingStartGlobalSample();

        int numCh = juce::jmin(2, lb.getNumChannels());

        // Compute where in the track's loop sample 0 of the bounce corresponds to
        juce::int64 elapsedAtZero = -startGlobal;
        int readStart = static_cast<int>(((elapsedAtZero % trackLen) + trackLen) % trackLen);

        // Block-copy with wrapping
        for (int ch = 0; ch < numCh; ++ch)
        {
            int remaining = bounceLen;
            int dstPos = 0;
            int srcPos = readStart;

            while (remaining > 0)
            {
                int toLoopEnd = trackLen - srcPos;
                int chunk = juce::jmin(remaining, toLoopEnd);
                mWorkBuffer.addFrom(ch, dstPos, lb, ch, srcPos, chunk);
                dstPos += chunk;
                srcPos += chunk;
                if (srcPos >= trackLen) srcPos = 0;
                remaining -= chunk;
            }
        }
    }

    // Apply crossfade to avoid click at loop boundary
    LoopTrack::applyCrossfade(mWorkBuffer, bounceLen, CROSSFADE_SAMPLES);

    // Start progressive replacement on track 1 (playhead-first, zero glitch)
    mTracks[0]->beginProgressiveReplace(&mWorkBuffer, bounceLen, 0, 0);

    // Clear all other tracks immediately (they go silent)
    for (size_t i = 1; i < mTracks.size(); ++i)
        mTracks[i]->clear();

    mPrimaryLoopLengthSamples.store(bounceLen);
    LOG("Bounce started progressive | bounceLen=" + juce::String(bounceLen));
}

//==============================================================================
// After Loop: capture last N bars of audio from retrospective buffer into a specific track

void SimpleLooperAudioProcessor::captureAfterLoop(int trackIndex)
{
    LOG_SEP("AFTER LOOP (Track " + juce::String(trackIndex) + ")");
    performCaptureAfterLoop(trackIndex);
}

void SimpleLooperAudioProcessor::performCaptureAfterLoop(int trackIndex)
{
    int masterLen = mPrimaryLoopLengthSamples.load();
    if (masterLen <= 0 || mRetroBufferSize <= 0) return;
    if (trackIndex < 0 || trackIndex >= (int)mTracks.size()) return;

    // Determine capture length based on targetMultiplier
    float mult = mTracks[trackIndex]->getTargetMultiplier();
    if (mult < 1.0f / 64.0f) mult = 1.0f / 64.0f;
    if (mult > 64.0f) mult = 64.0f;
    int captureLen = static_cast<int>((float)masterLen * mult);

    if (captureLen <= 0) return;
    if (captureLen > mRetroBufferSize) return;
    if (captureLen > mWorkBuffer.getNumSamples()) return;

    // Read last captureLen samples from the circular retrospective buffer
    int retroReadStart = (mRetroWritePos - captureLen + mRetroBufferSize) % mRetroBufferSize;

    mWorkBuffer.clear(0, captureLen);

    int retroCh = juce::jmin(2, mRetrospectiveBuffer.getNumChannels());
    for (int ch = 0; ch < retroCh; ++ch)
    {
        int toEnd = mRetroBufferSize - retroReadStart;
        if (captureLen <= toEnd)
        {
            mWorkBuffer.copyFrom(ch, 0, mRetrospectiveBuffer, ch, retroReadStart, captureLen);
        }
        else
        {
            mWorkBuffer.copyFrom(ch, 0, mRetrospectiveBuffer, ch, retroReadStart, toEnd);
            mWorkBuffer.copyFrom(ch, toEnd, mRetrospectiveBuffer, ch, 0, captureLen - toEnd);
        }
    }

    juce::int64 globalNow = mGlobalTotalSamples.load();
    juce::int64 captureStartGlobal = globalNow - (juce::int64)captureLen;
    if (captureStartGlobal < 0) captureStartGlobal = 0;

    // Apply crossfade to avoid click at loop boundary
    LoopTrack::applyCrossfade(mWorkBuffer, captureLen, CROSSFADE_SAMPLES);

    if (!mTracks[trackIndex]->hasLoop())
    {
        // Track is empty: create a fresh loop from captured audio
        int alignedOffset = static_cast<int>(captureStartGlobal % masterLen);
        mTracks[trackIndex]->setLoopFromMix(mWorkBuffer, captureLen, alignedOffset, captureStartGlobal);
        LOG("After Loop (new): " + juce::String(captureLen) + " samples (mult=" + juce::String(mult) +
            ") into track " + juce::String(trackIndex) + " globalStart=" + juce::String(captureStartGlobal));
    }
    else
    {
        // Track has a loop: overdub captured audio on top
        mTracks[trackIndex]->overdubFromBuffer(mWorkBuffer, captureLen, captureStartGlobal);
        LOG("After Loop (overdub): " + juce::String(captureLen) + " samples (mult=" + juce::String(mult) +
            ") into track " + juce::String(trackIndex));
    }
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SimpleLooperAudioProcessor();
}
